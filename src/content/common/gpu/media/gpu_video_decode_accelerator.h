// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_MEDIA_GPU_VIDEO_DECODE_ACCELERATOR_H_
#define CONTENT_COMMON_GPU_MEDIA_GPU_VIDEO_DECODE_ACCELERATOR_H_

#include <stdint.h>

#include <map>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/shared_memory.h"
#include "base/synchronization/waitable_event.h"
#include "content/common/gpu/media/gpu_video_decode_accelerator_helpers.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "gpu/config/gpu_info.h"
#include "gpu/ipc/service/gpu_command_buffer_stub.h"
#include "gpu/ipc/service/gpu_command_buffer_stub.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_sender.h"
#include "media/video/video_decode_accelerator.h"
#include "ui/gfx/geometry/size.h"

namespace gpu {
struct GpuPreferences;
}  // namespace gpu

namespace content {

class GpuVideoDecodeAccelerator
    : public IPC::Listener,
      public IPC::Sender,
      public media::VideoDecodeAccelerator::Client,
      public gpu::GpuCommandBufferStub::DestructionObserver {
 public:
  // Each of the arguments to the constructor must outlive this object.
  // |stub->decoder()| will be made current around any operation that touches
  // the underlying VDA so that it can make GL calls safely.
  GpuVideoDecodeAccelerator(
      int32_t host_route_id,
      gpu::GpuCommandBufferStub* stub,
      const scoped_refptr<base::SingleThreadTaskRunner>& io_task_runner);

  // Static query for the capabilities, which includes the supported profiles.
  // This query calls the appropriate platform-specific version.  The returned
  // capabilities will not contain duplicate supported profile entries.
  static gpu::VideoDecodeAcceleratorCapabilities GetCapabilities(
      const gpu::GpuPreferences& gpu_preferences);

  // IPC::Listener implementation.
  bool OnMessageReceived(const IPC::Message& message) override;

  // media::VideoDecodeAccelerator::Client implementation.
  void NotifyInitializationComplete(bool success) override;
  void ProvidePictureBuffers(uint32_t requested_num_of_buffers,
                             uint32_t textures_per_buffer,
                             const gfx::Size& dimensions,
                             uint32_t texture_target) override;
  void DismissPictureBuffer(int32_t picture_buffer_id) override;
  void PictureReady(const media::Picture& picture) override;
  void NotifyEndOfBitstreamBuffer(int32_t bitstream_buffer_id) override;
  void NotifyFlushDone() override;
  void NotifyResetDone() override;
  void NotifyError(media::VideoDecodeAccelerator::Error error) override;

  // GpuCommandBufferStub::DestructionObserver implementation.
  void OnWillDestroyStub() override;

  // Function to delegate sending to actual sender.
  bool Send(IPC::Message* message) override;

  // Initialize VDAs from the set of VDAs supported for current platform until
  // one of them succeeds for given |config|. Send the |init_done_msg| when
  // done. filter_ is passed to gpu::GpuCommandBufferStub channel only if the
  // chosen VDA can decode on IO thread.
  bool Initialize(const media::VideoDecodeAccelerator::Config& config);

 private:
  class MessageFilter;

  // We only allow self-delete, from OnWillDestroyStub(), after cleanup there.
  ~GpuVideoDecodeAccelerator() override;

  // Handlers for IPC messages.
  void OnSetCdm(int cdm_id);
  void OnDecode(const media::BitstreamBuffer& bitstream_buffer);
  void OnAssignPictureBuffers(
      const std::vector<int32_t>& buffer_ids,
      const std::vector<media::PictureBuffer::TextureIds>& texture_ids);
  void OnReusePictureBuffer(int32_t picture_buffer_id);
  void OnFlush();
  void OnReset();
  void OnDestroy();

  // Called on IO thread when |filter_| has been removed.
  void OnFilterRemoved();

  // Sets the texture to cleared.
  void SetTextureCleared(const media::Picture& picture);

  // Route ID to communicate with the host.
  const int32_t host_route_id_;

  // Unowned pointer to the underlying gpu::GpuCommandBufferStub.  |this| is
  // registered as a DestuctionObserver of |stub_| and will self-delete when
  // |stub_| is destroyed.
  gpu::GpuCommandBufferStub* const stub_;

  // The underlying VideoDecodeAccelerator.
  std::unique_ptr<media::VideoDecodeAccelerator> video_decode_accelerator_;

  // Callback to return current GLContext, if available.
  GetGLContextCallback get_gl_context_cb_;

  // Callback for making the relevant context current for GL calls.
  MakeGLContextCurrentCallback make_context_current_cb_;

  // Callback to bind a GLImage to a given texture id and target.
  BindGLImageCallback bind_image_cb_;

  // Callback to return a WeakPtr to GLES2Decoder.
  GetGLES2DecoderCallback get_gles2_decoder_cb_;

  // The texture dimensions as requested by ProvidePictureBuffers().
  gfx::Size texture_dimensions_;

  // The texture target as requested by ProvidePictureBuffers().
  uint32_t texture_target_;

  // The number of textures per picture buffer as requests by
  // ProvidePictureBuffers()
  uint32_t textures_per_buffer_;

  // The message filter to run VDA::Decode on IO thread if VDA supports it.
  scoped_refptr<MessageFilter> filter_;

  // Used to wait on for |filter_| to be removed, before we can safely
  // destroy the VDA.
  base::WaitableEvent filter_removed_;

  // GPU child thread task runner.
  const scoped_refptr<base::SingleThreadTaskRunner> child_task_runner_;

  // GPU IO thread task runner.
  const scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;

  // Weak pointers will be invalidated on IO thread.
  base::WeakPtrFactory<Client> weak_factory_for_io_;

  // Protects |uncleared_textures_| when DCHECK is on. This is for debugging
  // only. We don't want to hold a lock on IO thread. When DCHECK is off,
  // |uncleared_textures_| is only accessed from the child thread.
  base::Lock debug_uncleared_textures_lock_;

  // A map from picture buffer ID to set of TextureRefs that have not been
  // cleared.
  std::map<int32_t, std::vector<scoped_refptr<gpu::gles2::TextureRef>>>
      uncleared_textures_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(GpuVideoDecodeAccelerator);
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_MEDIA_GPU_VIDEO_DECODE_ACCELERATOR_H_
