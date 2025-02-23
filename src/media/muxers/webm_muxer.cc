// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/muxers/webm_muxer.h"

#include "base/bind.h"
#include "media/audio/audio_parameters.h"
#include "media/base/limits.h"
#include "media/base/video_frame.h"
#include "media/filters/opus_constants.h"
#include "ui/gfx/geometry/size.h"

namespace media {

namespace {

void WriteOpusHeader(const media::AudioParameters& params, uint8_t* header) {
  // See https://wiki.xiph.org/OggOpus#ID_Header.
  // Set magic signature.
  std::string label = "OpusHead";
  memcpy(header + OPUS_EXTRADATA_LABEL_OFFSET, label.c_str(), label.size());
  // Set Opus version.
  header[OPUS_EXTRADATA_VERSION_OFFSET] = 1;
  // Set channel count.
  header[OPUS_EXTRADATA_CHANNELS_OFFSET] = params.channels();
  // Set pre-skip
  uint16_t skip = 0;
  memcpy(header + OPUS_EXTRADATA_SKIP_SAMPLES_OFFSET, &skip, sizeof(uint16_t));
  // Set original input sample rate in Hz.
  uint32_t sample_rate = params.sample_rate();
  memcpy(header + OPUS_EXTRADATA_SAMPLE_RATE_OFFSET, &sample_rate,
         sizeof(uint32_t));
  // Set output gain in dB.
  uint16_t gain = 0;
  memcpy(header + OPUS_EXTRADATA_GAIN_OFFSET, &gain, 2);

  // Set channel mapping.
  if (params.channels() > 2) {
    // Also possible to have a multistream, not supported for now.
    DCHECK_LE(params.channels(), OPUS_MAX_VORBIS_CHANNELS);
    header[OPUS_EXTRADATA_CHANNEL_MAPPING_OFFSET] = 1;
    // Assuming no coupled streams. This should actually be
    // channels() - |coupled_streams|.
    header[OPUS_EXTRADATA_NUM_STREAMS_OFFSET] = params.channels();
    header[OPUS_EXTRADATA_NUM_COUPLED_OFFSET] = 0;
    // Set the actual stream map.
    for (int i = 0; i < params.channels(); ++i) {
      header[OPUS_EXTRADATA_STREAM_MAP_OFFSET + i] =
          kOpusVorbisChannelMap[params.channels() - 1][i];
    }
  } else {
    header[OPUS_EXTRADATA_CHANNEL_MAPPING_OFFSET] = 0;
  }
}

static double GetFrameRate(const scoped_refptr<VideoFrame>& video_frame) {
  const double kZeroFrameRate = 0.0;
  const double kDefaultFrameRate = 30.0;

  double frame_rate = kDefaultFrameRate;
  if (!video_frame->metadata()->GetDouble(VideoFrameMetadata::FRAME_RATE,
                                          &frame_rate) ||
      frame_rate <= kZeroFrameRate ||
      frame_rate > media::limits::kMaxFramesPerSecond) {
    frame_rate = kDefaultFrameRate;
  }
  return frame_rate;
}

}  // anonymous namespace

WebmMuxer::WebmMuxer(VideoCodec codec,
                     bool has_video,
                     bool has_audio,
                     const WriteDataCB& write_data_callback)
    : use_vp9_(codec == kCodecVP9),
      video_track_index_(0),
      audio_track_index_(0),
      has_video_(has_video),
      has_audio_(has_audio),
      write_data_callback_(write_data_callback),
      position_(0) {
  DCHECK(has_video_ || has_audio_);
  DCHECK(!write_data_callback_.is_null());
  DCHECK(codec == kCodecVP8 || codec == kCodecVP9)
      << " Only Vp8 and VP9 are supported in WebmMuxer";

  segment_.Init(this);
  segment_.set_mode(mkvmuxer::Segment::kLive);
  segment_.OutputCues(false);

  mkvmuxer::SegmentInfo* const info = segment_.GetSegmentInfo();
  info->set_writing_app("Chrome");
  info->set_muxing_app("Chrome");

  // Creation is done on a different thread than main activities.
  thread_checker_.DetachFromThread();
}

WebmMuxer::~WebmMuxer() {
  // No need to segment_.Finalize() since is not Seekable(), i.e. a live
  // stream, but is a good practice.
  DCHECK(thread_checker_.CalledOnValidThread());
  segment_.Finalize();
}

void WebmMuxer::OnEncodedVideo(const scoped_refptr<VideoFrame>& video_frame,
                               scoped_ptr<std::string> encoded_data,
                               base::TimeTicks timestamp,
                               bool is_key_frame) {
  DVLOG(1) << __FUNCTION__ << " - " << encoded_data->size() << "B";
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!video_track_index_) {
    // |track_index_|, cannot be zero (!), initialize WebmMuxer in that case.
    // http://www.matroska.org/technical/specs/index.html#Tracks
    AddVideoTrack(video_frame->visible_rect().size(),
                  GetFrameRate(video_frame));
    if (first_frame_timestamp_video_.is_null())
      first_frame_timestamp_video_ = timestamp;
  }

  // TODO(ajose): Support multiple tracks: http://crbug.com/528523
  if (has_audio_ && !audio_track_index_) {
    DVLOG(1) << __FUNCTION__ << ": delaying until audio track ready.";
    if (is_key_frame)  // Upon Key frame reception, empty the encoded queue.
      encoded_frames_queue_.clear();

    encoded_frames_queue_.push_back(make_scoped_ptr(new EncodedVideoFrame(
        std::move(encoded_data), timestamp, is_key_frame)));
    return;
  }

  // Dump all saved encoded video frames if any.
  while (!encoded_frames_queue_.empty()) {
    AddFrame(
        std::move(encoded_frames_queue_.front()->data), video_track_index_,
        encoded_frames_queue_.front()->timestamp - first_frame_timestamp_video_,
        encoded_frames_queue_.front()->is_keyframe);
    encoded_frames_queue_.pop_front();
  }

  AddFrame(std::move(encoded_data), video_track_index_,
           timestamp - first_frame_timestamp_video_, is_key_frame);
}

void WebmMuxer::OnEncodedAudio(const media::AudioParameters& params,
                               scoped_ptr<std::string> encoded_data,
                               base::TimeTicks timestamp) {
  DVLOG(2) << __FUNCTION__ << " - " << encoded_data->size() << "B";
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!audio_track_index_) {
    AddAudioTrack(params);
    if (first_frame_timestamp_audio_.is_null())
      first_frame_timestamp_audio_ = timestamp;
  }

  // TODO(ajose): Don't drop audio data: http://crbug.com/547948
  // TODO(ajose): Support multiple tracks: http://crbug.com/528523
  if (has_video_ && !video_track_index_) {
    DVLOG(1) << __FUNCTION__ << ": delaying until video track ready.";
    return;
  }

  // Dump all saved encoded video frames if any.
  while (!encoded_frames_queue_.empty()) {
    AddFrame(
        std::move(encoded_frames_queue_.front()->data), video_track_index_,
        encoded_frames_queue_.front()->timestamp - first_frame_timestamp_video_,
        encoded_frames_queue_.front()->is_keyframe);
    encoded_frames_queue_.pop_front();
  }

  AddFrame(std::move(encoded_data), audio_track_index_,
           timestamp - first_frame_timestamp_audio_,
           true /* is_key_frame -- always true for audio */);
}

void WebmMuxer::Pause() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!elapsed_time_in_pause_)
    elapsed_time_in_pause_.reset(new base::ElapsedTimer());
}

void WebmMuxer::Resume() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());
  if (elapsed_time_in_pause_) {
    total_time_in_pause_ += elapsed_time_in_pause_->Elapsed();
    elapsed_time_in_pause_.reset();
  }
}

void WebmMuxer::AddVideoTrack(const gfx::Size& frame_size, double frame_rate) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(0u, video_track_index_)
      << "WebmMuxer can only be initialized once.";

  video_track_index_ =
      segment_.AddVideoTrack(frame_size.width(), frame_size.height(), 0);
  DCHECK_GT(video_track_index_, 0u);

  mkvmuxer::VideoTrack* const video_track =
      reinterpret_cast<mkvmuxer::VideoTrack*>(
          segment_.GetTrackByNumber(video_track_index_));
  DCHECK(video_track);
  video_track->set_codec_id(use_vp9_ ? mkvmuxer::Tracks::kVp9CodecId
                                     : mkvmuxer::Tracks::kVp8CodecId);
  DCHECK_EQ(0ull, video_track->crop_right());
  DCHECK_EQ(0ull, video_track->crop_left());
  DCHECK_EQ(0ull, video_track->crop_top());
  DCHECK_EQ(0ull, video_track->crop_bottom());
  DCHECK_EQ(0.0f, video_track->frame_rate());

  video_track->set_default_duration(base::Time::kNanosecondsPerSecond /
                                    frame_rate);
  // Segment's timestamps should be in milliseconds, DCHECK it. See
  // http://www.webmproject.org/docs/container/#muxer-guidelines
  DCHECK_EQ(1000000ull, segment_.GetSegmentInfo()->timecode_scale());
}

void WebmMuxer::AddAudioTrack(const media::AudioParameters& params) {
  DVLOG(1) << __FUNCTION__ << " " << params.AsHumanReadableString();
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(0u, audio_track_index_)
      << "WebmMuxer audio can only be initialised once.";

  audio_track_index_ =
      segment_.AddAudioTrack(params.sample_rate(), params.channels(), 0);
  DCHECK_GT(audio_track_index_, 0u);

  mkvmuxer::AudioTrack* const audio_track =
      reinterpret_cast<mkvmuxer::AudioTrack*>(
          segment_.GetTrackByNumber(audio_track_index_));
  DCHECK(audio_track);
  audio_track->set_codec_id(mkvmuxer::Tracks::kOpusCodecId);

  DCHECK_EQ(params.sample_rate(), audio_track->sample_rate());
  DCHECK_EQ(params.channels(), static_cast<int>(audio_track->channels()));

  uint8_t opus_header[OPUS_EXTRADATA_SIZE];
  WriteOpusHeader(params, opus_header);

  if (!audio_track->SetCodecPrivate(opus_header, OPUS_EXTRADATA_SIZE))
    LOG(ERROR) << __FUNCTION__ << ": failed to set opus header.";

  // Segment's timestamps should be in milliseconds, DCHECK it. See
  // http://www.webmproject.org/docs/container/#muxer-guidelines
  DCHECK_EQ(1000000ull, segment_.GetSegmentInfo()->timecode_scale());
}

mkvmuxer::int32 WebmMuxer::Write(const void* buf, mkvmuxer::uint32 len) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(buf);
  write_data_callback_.Run(
      base::StringPiece(reinterpret_cast<const char*>(buf), len));
  position_ += len;
  return 0;
}

mkvmuxer::int64 WebmMuxer::Position() const {
  return position_.ValueOrDie();
}

mkvmuxer::int32 WebmMuxer::Position(mkvmuxer::int64 position) {
  // The stream is not Seekable() so indicate we cannot set the position.
  return -1;
}

bool WebmMuxer::Seekable() const {
  return false;
}

void WebmMuxer::ElementStartNotify(mkvmuxer::uint64 element_id,
                                   mkvmuxer::int64 position) {
  // This method gets pinged before items are sent to |write_data_callback_|.
  DCHECK_GE(position, position_.ValueOrDefault(0))
      << "Can't go back in a live WebM stream.";
}

void WebmMuxer::AddFrame(scoped_ptr<std::string> encoded_data,
                         uint8_t track_index,
                         base::TimeDelta timestamp,
                         bool is_key_frame) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!has_video_ || video_track_index_);
  DCHECK(!has_audio_ || audio_track_index_);

  most_recent_timestamp_ =
      std::max(most_recent_timestamp_, timestamp - total_time_in_pause_);

  segment_.AddFrame(reinterpret_cast<const uint8_t*>(encoded_data->data()),
                    encoded_data->size(), track_index,
                    most_recent_timestamp_.InMicroseconds() *
                        base::Time::kNanosecondsPerMicrosecond,
                    is_key_frame);
}

WebmMuxer::EncodedVideoFrame::EncodedVideoFrame(scoped_ptr<std::string> data,
                                                base::TimeTicks timestamp,
                                                bool is_keyframe)
    : data(std::move(data)), timestamp(timestamp), is_keyframe(is_keyframe) {}

WebmMuxer::EncodedVideoFrame::~EncodedVideoFrame() {}

}  // namespace media
