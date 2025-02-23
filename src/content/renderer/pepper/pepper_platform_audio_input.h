// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_PEPPER_PLATFORM_AUDIO_INPUT_H_
#define CONTENT_RENDERER_PEPPER_PEPPER_PLATFORM_AUDIO_INPUT_H_

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "media/audio/audio_input_ipc.h"
#include "media/audio/audio_parameters.h"

class GURL;

namespace base {
class SingleThreadTaskRunner;
}

namespace media {
class AudioParameters;
}

namespace content {

class PepperAudioInputHost;
class PepperMediaDeviceManager;

// PepperPlatformAudioInput is operated on two threads: the main thread (the
// thread on which objects are created) and the I/O thread. All public methods,
// except the destructor, must be called on the main thread. The notifications
// to the users of this class (i.e. PepperAudioInputHost) are also sent on the
// main thread. Internally, this class sends audio input IPC messages and
// receives media::AudioInputIPCDelegate notifications on the I/O thread.

class PepperPlatformAudioInput
    : public media::AudioInputIPCDelegate,
      public base::RefCountedThreadSafe<PepperPlatformAudioInput> {
 public:
  // Factory function, returns NULL on failure. StreamCreated() will be called
  // when the stream is created.
  static PepperPlatformAudioInput* Create(
      int render_frame_id,
      const std::string& device_id,
      const GURL& document_url,
      int sample_rate,
      int frames_per_buffer,
      PepperAudioInputHost* client);

  // Called on main thread.
  void StartCapture();
  void StopCapture();
  // Closes the stream. Make sure to call this before the object is destructed.
  void ShutDown();

  // media::AudioInputIPCDelegate.
  void OnStreamCreated(base::SharedMemoryHandle handle,
                       base::SyncSocket::Handle socket_handle,
                       int length,
                       int total_segments) override;
  void OnVolume(double volume) override;
  void OnStateChanged(media::AudioInputIPCDelegateState state) override;
  void OnIPCClosed() override;

 protected:
  ~PepperPlatformAudioInput() override;

 private:
  friend class base::RefCountedThreadSafe<PepperPlatformAudioInput>;

  PepperPlatformAudioInput();

  bool Initialize(int render_frame_id,
                  const std::string& device_id,
                  const GURL& document_url,
                  int sample_rate,
                  int frames_per_buffer,
                  PepperAudioInputHost* client);

  // I/O thread backends to above functions.
  void InitializeOnIOThread(int session_id);
  void StartCaptureOnIOThread();
  void StopCaptureOnIOThread();
  void ShutDownOnIOThread();

  void OnDeviceOpened(int request_id, bool succeeded, const std::string& label);
  void CloseDevice();
  void NotifyStreamCreationFailed();

  // Can return NULL if the RenderFrame referenced by |render_frame_id_| has
  // gone away.
  PepperMediaDeviceManager* GetMediaDeviceManager();

  // The client to notify when the stream is created. THIS MUST ONLY BE
  // ACCESSED ON THE MAIN THREAD.
  PepperAudioInputHost* client_;

  // Used to send/receive IPC. THIS MUST ONLY BE ACCESSED ON THE
  // I/O THREAD.
  std::unique_ptr<media::AudioInputIPC> ipc_;

  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;

  // The frame containing the Pepper widget.
  int render_frame_id_;

  // The unique ID to identify the opened device. THIS MUST ONLY BE ACCESSED ON
  // THE MAIN THREAD.
  std::string label_;

  // Initialized on the main thread and accessed on the I/O thread afterwards.
  media::AudioParameters params_;

  // Whether we have tried to create an audio stream. THIS MUST ONLY BE ACCESSED
  // ON THE I/O THREAD.
  bool create_stream_sent_;

  // Whether we have a pending request to open a device. We have to make sure
  // there isn't any pending request before this object goes away.
  // THIS MUST ONLY BE ACCESSED ON THE MAIN THREAD.
  bool pending_open_device_;
  // THIS MUST ONLY BE ACCESSED ON THE MAIN THREAD.
  int pending_open_device_id_;

  DISALLOW_COPY_AND_ASSIGN(PepperPlatformAudioInput);
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_PEPPER_PLATFORM_AUDIO_INPUT_H_
