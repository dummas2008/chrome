// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_API_DISPLAY_SOURCE_WIFI_DISPLAY_WIFI_DISPLAY_MEDIA_PIPELINE_H_
#define EXTENSIONS_RENDERER_API_DISPLAY_SOURCE_WIFI_DISPLAY_WIFI_DISPLAY_MEDIA_PIPELINE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "extensions/renderer/api/display_source/wifi_display/wifi_display_media_packetizer.h"
#include "extensions/renderer/api/display_source/wifi_display/wifi_display_video_encoder.h"
#include "third_party/WebKit/public/platform/WebMediaStreamTrack.h"
#include "third_party/wds/src/libwds/public/media_manager.h"

namespace media {
class AudioBus;
}  // namespace media

namespace extensions {

// This class encapsulates the WiFi Display media pipeline including
// - encoding
// - AV multiplexing/packetization
// - sending
// Threading: should belong to IO thread.
class WiFiDisplayMediaPipeline {
 public:
  using MediaPacketCallback =
      base::Callback<void(const std::vector<uint8_t>&)>;
  using ErrorCallback = base::Callback<void(const std::string&)>;
  using InitCompletionCallback = base::Callback<void(bool)>;

  static std::unique_ptr<WiFiDisplayMediaPipeline> Create(
      wds::SessionType type,
      const WiFiDisplayVideoEncoder::InitParameters& video_parameters,
      const wds::AudioCodec& audio_codec,
      const ErrorCallback& error_callback);
  ~WiFiDisplayMediaPipeline();
  // Note: to be called only once.
  void Initialize(
      const InitCompletionCallback& callback);

  void InsertRawVideoFrame(
      const scoped_refptr<media::VideoFrame>& video_frame,
      base::TimeTicks reference_time);

  void RequestIDRPicture();

 private:
  WiFiDisplayMediaPipeline(
      wds::SessionType type,
      const WiFiDisplayVideoEncoder::InitParameters& video_parameters,
      const wds::AudioCodec& audio_codec,
      const ErrorCallback& error_callback);

  void CreateVideoEncoder();
  void CreateMediaPacketizer();
  void OnVideoEncoderCreated(
      scoped_refptr<WiFiDisplayVideoEncoder> video_encoder);

  void OnEncodedVideoFrame(const WiFiDisplayEncodedFrame& frame);

  bool OnPacketizedMediaDatagramPacket(
     WiFiDisplayMediaDatagramPacket media_datagram_packet);

  scoped_refptr<WiFiDisplayVideoEncoder> video_encoder_;
  std::unique_ptr<WiFiDisplayMediaPacketizer> packetizer_;

  wds::SessionType type_;
  WiFiDisplayVideoEncoder::InitParameters video_parameters_;
  wds::AudioCodec audio_codec_;

  ErrorCallback error_callback_;
  InitCompletionCallback init_completion_callback_;

  base::WeakPtrFactory<WiFiDisplayMediaPipeline> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(WiFiDisplayMediaPipeline);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_API_DISPLAY_SOURCE_WIFI_DISPLAY_WIFI_DISPLAY_MEDIA_PIPELINE_H_
