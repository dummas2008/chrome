// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/media/android_video_encode_accelerator.h"

#include <set>

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "content/common/gpu/media/shared_memory_region.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "media/base/android/media_codec_util.h"
#include "media/base/bitstream_buffer.h"
#include "media/base/limits.h"
#include "media/video/picture.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "ui/gl/android/scoped_java_surface.h"
#include "ui/gl/gl_bindings.h"

using media::VideoCodecBridge;
using media::VideoFrame;

namespace content {

// Limit default max video codec size for Android to avoid
// HW codec initialization failure for resolution higher than 720p.
// Default values are from Libjingle "jsepsessiondescription.cc".
const int kMaxEncodeFrameWidth = 1280;
const int kMaxEncodeFrameHeight = 720;
const int kMaxFramerateNumerator = 30;
const int kMaxFramerateDenominator = 1;

enum PixelFormat {
  // Subset of MediaCodecInfo.CodecCapabilities.
  COLOR_FORMAT_YUV420_PLANAR = 19,
  COLOR_FORMAT_YUV420_SEMIPLANAR = 21,
};

// Helper macros for dealing with failure.  If |result| evaluates false, emit
// |log| to DLOG(ERROR), register |error| with the client, and return.
#define RETURN_ON_FAILURE(result, log, error)                  \
  do {                                                         \
    if (!(result)) {                                           \
      DLOG(ERROR) << log;                                      \
      if (client_ptr_factory_->GetWeakPtr()) {                 \
        client_ptr_factory_->GetWeakPtr()->NotifyError(error); \
        client_ptr_factory_.reset();                           \
      }                                                        \
      return;                                                  \
    }                                                          \
  } while (0)

// Because MediaCodec is thread-hostile (must be poked on a single thread) and
// has no callback mechanism (b/11990118), we must drive it by polling for
// complete frames (and available input buffers, when the codec is fully
// saturated).  This function defines the polling delay.  The value used is an
// arbitrary choice that trades off CPU utilization (spinning) against latency.
// Mirrors android_video_decode_accelerator.cc::DecodePollDelay().
static inline const base::TimeDelta EncodePollDelay() {
  // An alternative to this polling scheme could be to dedicate a new thread
  // (instead of using the ChildThread) to run the MediaCodec, and make that
  // thread use the timeout-based flavor of MediaCodec's dequeue methods when it
  // believes the codec should complete "soon" (e.g. waiting for an input
  // buffer, or waiting for a picture when it knows enough complete input
  // pictures have been fed to saturate any internal buffering).  This is
  // speculative and it's unclear that this would be a win (nor that there's a
  // reasonably device-agnostic way to fill in the "believes" above).
  return base::TimeDelta::FromMilliseconds(10);
}

static inline const base::TimeDelta NoWaitTimeOut() {
  return base::TimeDelta::FromMicroseconds(0);
}

static bool GetSupportedColorFormatForMime(const std::string& mime,
                                           PixelFormat* pixel_format) {
  if (mime.empty())
    return false;

  std::set<int> formats = media::MediaCodecUtil::GetEncoderColorFormats(mime);
  if (formats.count(COLOR_FORMAT_YUV420_SEMIPLANAR) > 0)
    *pixel_format = COLOR_FORMAT_YUV420_SEMIPLANAR;
  else if (formats.count(COLOR_FORMAT_YUV420_PLANAR) > 0)
    *pixel_format = COLOR_FORMAT_YUV420_PLANAR;
  else
    return false;

  return true;
}

AndroidVideoEncodeAccelerator::AndroidVideoEncodeAccelerator()
    : num_buffers_at_codec_(0),
      last_set_bitrate_(0) {}

AndroidVideoEncodeAccelerator::~AndroidVideoEncodeAccelerator() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

media::VideoEncodeAccelerator::SupportedProfiles
AndroidVideoEncodeAccelerator::GetSupportedProfiles() {
  SupportedProfiles profiles;

  const struct {
      const media::VideoCodec codec;
      const media::VideoCodecProfile profile;
  } kSupportedCodecs[] = {
      { media::kCodecVP8, media::VP8PROFILE_ANY },
      { media::kCodecH264, media::H264PROFILE_BASELINE },
      { media::kCodecH264, media::H264PROFILE_MAIN }
  };

  for (const auto& supported_codec : kSupportedCodecs) {
    if (supported_codec.codec == media::kCodecVP8 &&
        !media::MediaCodecUtil::IsVp8EncoderAvailable()) {
      continue;
    }

    if (VideoCodecBridge::IsKnownUnaccelerated(supported_codec.codec,
                                               media::MEDIA_CODEC_ENCODER)) {
      continue;
    }

    SupportedProfile profile;
    profile.profile = supported_codec.profile;
    // It would be nice if MediaCodec exposes the maximum capabilities of
    // the encoder. Hard-code some reasonable defaults as workaround.
    profile.max_resolution.SetSize(kMaxEncodeFrameWidth,
                                   kMaxEncodeFrameHeight);
    profile.max_framerate_numerator = kMaxFramerateNumerator;
    profile.max_framerate_denominator = kMaxFramerateDenominator;
    profiles.push_back(profile);
  }
  return profiles;
}

bool AndroidVideoEncodeAccelerator::Initialize(
    media::VideoPixelFormat format,
    const gfx::Size& input_visible_size,
    media::VideoCodecProfile output_profile,
    uint32_t initial_bitrate,
    Client* client) {
  DVLOG(3) << __PRETTY_FUNCTION__ << " format: " << format
           << ", input_visible_size: " << input_visible_size.ToString()
           << ", output_profile: " << output_profile
           << ", initial_bitrate: " << initial_bitrate;
  DCHECK(!media_codec_);
  DCHECK(thread_checker_.CalledOnValidThread());

  client_ptr_factory_.reset(new base::WeakPtrFactory<Client>(client));

  if (!(media::MediaCodecUtil::SupportsSetParameters() &&
        format == media::PIXEL_FORMAT_I420)) {
    DLOG(ERROR) << "Unexpected combo: " << format << ", " << output_profile;
    return false;
  }

  std::string mime_type;
  media::VideoCodec codec;
  // The client should be prepared to feed at least this many frames into the
  // encoder before being returned any output frames, since the encoder may
  // need to hold onto some subset of inputs as reference pictures.
  uint32_t frame_input_count;
  if (output_profile == media::VP8PROFILE_ANY) {
    codec = media::kCodecVP8;
    mime_type = "video/x-vnd.on2.vp8";
    frame_input_count = 1;
  } else if (output_profile == media::H264PROFILE_BASELINE ||
             output_profile == media::H264PROFILE_MAIN) {
    codec = media::kCodecH264;
    mime_type = "video/avc";
    frame_input_count = 30;
  } else {
    return false;
  }

  frame_size_ = input_visible_size;
  last_set_bitrate_ = initial_bitrate;

  // Only consider using MediaCodec if it's likely backed by hardware.
  if (media::VideoCodecBridge::IsKnownUnaccelerated(
          codec, media::MEDIA_CODEC_ENCODER)) {
    DLOG(ERROR) << "No HW support";
    return false;
  }

  PixelFormat pixel_format = COLOR_FORMAT_YUV420_SEMIPLANAR;
  if (!GetSupportedColorFormatForMime(mime_type, &pixel_format)) {
    DLOG(ERROR) << "No color format support.";
    return false;
  }
  media_codec_.reset(media::VideoCodecBridge::CreateEncoder(codec,
                                                            input_visible_size,
                                                            initial_bitrate,
                                                            INITIAL_FRAMERATE,
                                                            IFRAME_INTERVAL,
                                                            pixel_format));

  if (!media_codec_) {
    DLOG(ERROR) << "Failed to create/start the codec: "
                << input_visible_size.ToString();
    return false;
  }

  // Conservative upper bound for output buffer size: decoded size + 2KB.
  const size_t output_buffer_capacity =
      VideoFrame::AllocationSize(format, input_visible_size) + 2048;
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&VideoEncodeAccelerator::Client::RequireBitstreamBuffers,
                 client_ptr_factory_->GetWeakPtr(),
                 frame_input_count,
                 input_visible_size,
                 output_buffer_capacity));
  return true;
}

void AndroidVideoEncodeAccelerator::MaybeStartIOTimer() {
  if (!io_timer_.IsRunning() &&
      (num_buffers_at_codec_ > 0 || !pending_frames_.empty())) {
    io_timer_.Start(FROM_HERE,
                    EncodePollDelay(),
                    this,
                    &AndroidVideoEncodeAccelerator::DoIOTask);
  }
}

void AndroidVideoEncodeAccelerator::MaybeStopIOTimer() {
  if (io_timer_.IsRunning() &&
      (num_buffers_at_codec_ == 0 && pending_frames_.empty())) {
    io_timer_.Stop();
  }
}

void AndroidVideoEncodeAccelerator::Encode(
    const scoped_refptr<VideoFrame>& frame,
    bool force_keyframe) {
  DVLOG(3) << __PRETTY_FUNCTION__ << ": " << force_keyframe;
  DCHECK(thread_checker_.CalledOnValidThread());
  RETURN_ON_FAILURE(frame->format() == media::PIXEL_FORMAT_I420,
                    "Unexpected format", kInvalidArgumentError);
  RETURN_ON_FAILURE(frame->visible_rect().size() == frame_size_,
                    "Unexpected resolution", kInvalidArgumentError);
  // MediaCodec doesn't have a way to specify stride for non-Packed formats, so
  // we insist on being called with packed frames and no cropping :(
  RETURN_ON_FAILURE(frame->row_bytes(VideoFrame::kYPlane) ==
                            frame->stride(VideoFrame::kYPlane) &&
                        frame->row_bytes(VideoFrame::kUPlane) ==
                            frame->stride(VideoFrame::kUPlane) &&
                        frame->row_bytes(VideoFrame::kVPlane) ==
                            frame->stride(VideoFrame::kVPlane) &&
                        frame->coded_size() == frame->visible_rect().size(),
                    "Non-packed frame, or visible_rect != coded_size",
                    kInvalidArgumentError);

  pending_frames_.push(
      base::MakeTuple(frame, force_keyframe, base::Time::Now()));
  DoIOTask();
}

void AndroidVideoEncodeAccelerator::UseOutputBitstreamBuffer(
    const media::BitstreamBuffer& buffer) {
  DVLOG(3) << __PRETTY_FUNCTION__ << ": bitstream_buffer_id=" << buffer.id();
  DCHECK(thread_checker_.CalledOnValidThread());
  available_bitstream_buffers_.push_back(buffer);
  DoIOTask();
}

void AndroidVideoEncodeAccelerator::RequestEncodingParametersChange(
    uint32_t bitrate,
    uint32_t framerate) {
  DVLOG(3) << __PRETTY_FUNCTION__ << ": bitrate: " << bitrate
           << ", framerate: " << framerate;
  DCHECK(thread_checker_.CalledOnValidThread());
  if (bitrate != last_set_bitrate_) {
    last_set_bitrate_ = bitrate;
    media_codec_->SetVideoBitrate(bitrate);
  }
  // Note: Android's MediaCodec doesn't allow mid-stream adjustments to
  // framerate, so we ignore that here.  This is OK because Android only uses
  // the framerate value from MediaFormat during configure() as a proxy for
  // bitrate, and we set that explicitly.
}

void AndroidVideoEncodeAccelerator::Destroy() {
  DVLOG(3) << __PRETTY_FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());
  client_ptr_factory_.reset();
  if (media_codec_) {
    if (io_timer_.IsRunning())
      io_timer_.Stop();
    media_codec_->Stop();
  }
  delete this;
}

void AndroidVideoEncodeAccelerator::DoIOTask() {
  QueueInput();
  DequeueOutput();
  MaybeStartIOTimer();
  MaybeStopIOTimer();
}

void AndroidVideoEncodeAccelerator::QueueInput() {
  if (!client_ptr_factory_->GetWeakPtr() || pending_frames_.empty())
    return;

  int input_buf_index = 0;
  media::MediaCodecStatus status =
      media_codec_->DequeueInputBuffer(NoWaitTimeOut(), &input_buf_index);
  if (status != media::MEDIA_CODEC_OK) {
    DCHECK(status == media::MEDIA_CODEC_DEQUEUE_INPUT_AGAIN_LATER ||
           status == media::MEDIA_CODEC_ERROR);
    RETURN_ON_FAILURE(status != media::MEDIA_CODEC_ERROR,
                      "MediaCodec error",
                      kPlatformFailureError);
    return;
  }

  const PendingFrames::value_type& input = pending_frames_.front();
  bool is_key_frame = base::get<1>(input);
  if (is_key_frame) {
    // Ideally MediaCodec would honor BUFFER_FLAG_SYNC_FRAME so we could
    // indicate this in the QueueInputBuffer() call below and guarantee _this_
    // frame be encoded as a key frame, but sadly that flag is ignored.
    // Instead, we request a key frame "soon".
    media_codec_->RequestKeyFrameSoon();
  }
  scoped_refptr<VideoFrame> frame = base::get<0>(input);

  uint8_t* buffer = NULL;
  size_t capacity = 0;
  status = media_codec_->GetInputBuffer(input_buf_index, &buffer, &capacity);
  RETURN_ON_FAILURE(status == media::MEDIA_CODEC_OK, "GetInputBuffer failed.",
                    kPlatformFailureError);

  size_t queued_size =
      VideoFrame::AllocationSize(media::PIXEL_FORMAT_I420, frame->coded_size());
  RETURN_ON_FAILURE(capacity >= queued_size,
                    "Failed to get input buffer: " << input_buf_index,
                    kPlatformFailureError);

  uint8_t* dst_y = buffer;
  int dst_stride_y = frame->stride(VideoFrame::kYPlane);
  uint8_t* dst_uv =
      buffer +
      frame->stride(VideoFrame::kYPlane) * frame->rows(VideoFrame::kYPlane);
  int dst_stride_uv = frame->stride(VideoFrame::kUPlane) * 2;
  // Why NV12?  Because COLOR_FORMAT_YUV420_SEMIPLANAR.  See comment at other
  // mention of that constant.
  bool converted = !libyuv::I420ToNV12(frame->data(VideoFrame::kYPlane),
                                       frame->stride(VideoFrame::kYPlane),
                                       frame->data(VideoFrame::kUPlane),
                                       frame->stride(VideoFrame::kUPlane),
                                       frame->data(VideoFrame::kVPlane),
                                       frame->stride(VideoFrame::kVPlane),
                                       dst_y,
                                       dst_stride_y,
                                       dst_uv,
                                       dst_stride_uv,
                                       frame->coded_size().width(),
                                       frame->coded_size().height());
  RETURN_ON_FAILURE(converted, "Failed to I420ToNV12!", kPlatformFailureError);

  fake_input_timestamp_ += base::TimeDelta::FromMicroseconds(1);
  status = media_codec_->QueueInputBuffer(
      input_buf_index, NULL, queued_size, fake_input_timestamp_);
  UMA_HISTOGRAM_TIMES("Media.AVEA.InputQueueTime",
                      base::Time::Now() - base::get<2>(input));
  RETURN_ON_FAILURE(status == media::MEDIA_CODEC_OK,
                    "Failed to QueueInputBuffer: " << status,
                    kPlatformFailureError);
  ++num_buffers_at_codec_;
  pending_frames_.pop();
}

void AndroidVideoEncodeAccelerator::DequeueOutput() {
  if (!client_ptr_factory_->GetWeakPtr() ||
      available_bitstream_buffers_.empty() || num_buffers_at_codec_ == 0) {
    return;
  }

  int32_t buf_index = 0;
  size_t offset = 0;
  size_t size = 0;
  bool key_frame = false;
  do {
    media::MediaCodecStatus status = media_codec_->DequeueOutputBuffer(
        NoWaitTimeOut(), &buf_index, &offset, &size, NULL, NULL, &key_frame);
    switch (status) {
      case media::MEDIA_CODEC_DEQUEUE_OUTPUT_AGAIN_LATER:
        return;

      case media::MEDIA_CODEC_ERROR:
        RETURN_ON_FAILURE(false, "Codec error", kPlatformFailureError);
        // Unreachable because of previous statement, but included for clarity.
        return;

      case media::MEDIA_CODEC_OUTPUT_FORMAT_CHANGED:
        RETURN_ON_FAILURE(false, "Unexpected output format change",
                          kPlatformFailureError);
        break;

      case media::MEDIA_CODEC_OUTPUT_BUFFERS_CHANGED:
        break;

      case media::MEDIA_CODEC_OK:
        DCHECK_GE(buf_index, 0);
        break;

      default:
        NOTREACHED();
        break;
    }
  } while (buf_index < 0);

  media::BitstreamBuffer bitstream_buffer = available_bitstream_buffers_.back();
  available_bitstream_buffers_.pop_back();
  std::unique_ptr<SharedMemoryRegion> shm(
      new SharedMemoryRegion(bitstream_buffer, false));
  RETURN_ON_FAILURE(shm->Map(), "Failed to map SHM", kPlatformFailureError);
  RETURN_ON_FAILURE(size <= shm->size(),
                    "Encoded buffer too large: " << size << ">" << shm->size(),
                    kPlatformFailureError);

  media::MediaCodecStatus status = media_codec_->CopyFromOutputBuffer(
      buf_index, offset, shm->memory(), size);
  RETURN_ON_FAILURE(status == media::MEDIA_CODEC_OK,
                    "CopyFromOutputBuffer failed", kPlatformFailureError);
  media_codec_->ReleaseOutputBuffer(buf_index, false);
  --num_buffers_at_codec_;

  UMA_HISTOGRAM_COUNTS_10000("Media.AVEA.EncodedBufferSizeKB", size / 1024);
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&VideoEncodeAccelerator::Client::BitstreamBufferReady,
                 client_ptr_factory_->GetWeakPtr(),
                 bitstream_buffer.id(),
                 size,
                 key_frame));
}

}  // namespace content
