// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_PPB_GRAPHICS_3D_IMPL_H_
#define CONTENT_RENDERER_PEPPER_PPB_GRAPHICS_3D_IMPL_H_

#include <stdint.h>

#include "base/macros.h"
#include "base/memory/shared_memory.h"
#include "base/memory/weak_ptr.h"
#include "gpu/command_buffer/common/command_buffer_id.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "ppapi/shared_impl/ppb_graphics_3d_shared.h"
#include "ppapi/shared_impl/resource.h"

namespace gpu {
struct Capabilities;
class CommandBufferProxyImpl;
class GpuChannelHost;
}

namespace content {

class PPB_Graphics3D_Impl : public ppapi::PPB_Graphics3D_Shared {
 public:
  static PP_Resource CreateRaw(PP_Instance instance,
                               PP_Resource share_context,
                               const int32_t* attrib_list,
                               gpu::Capabilities* capabilities,
                               base::SharedMemoryHandle* shared_state_handle,
                               gpu::CommandBufferId* command_buffer_id);

  // PPB_Graphics3D_API trusted implementation.
  PP_Bool SetGetBuffer(int32_t transfer_buffer_id) override;
  scoped_refptr<gpu::Buffer> CreateTransferBuffer(uint32_t size,
                                                  int32_t* id) override;
  PP_Bool DestroyTransferBuffer(int32_t id) override;
  PP_Bool Flush(int32_t put_offset) override;
  gpu::CommandBuffer::State WaitForTokenInRange(int32_t start,
                                                int32_t end) override;
  gpu::CommandBuffer::State WaitForGetOffsetInRange(int32_t start,
                                                    int32_t end) override;
  void EnsureWorkVisible() override;

  // Binds/unbinds the graphics of this context with the associated instance.
  // Returns true if binding/unbinding is successful.
  bool BindToInstance(bool bind);

  // Returns true if the backing texture is always opaque.
  bool IsOpaque();

  // Notifications about the view's progress painting.  See PluginInstance.
  // These messages are used to send Flush callbacks to the plugin.
  void ViewInitiatedPaint();

  void GetBackingMailbox(gpu::Mailbox* mailbox, gpu::SyncToken* sync_token) {
    *mailbox = mailbox_;
    *sync_token = sync_token_;
  }

  gpu::CommandBufferProxyImpl* GetCommandBufferProxy();

  gpu::GpuChannelHost* channel() { return channel_.get(); }

 protected:
  ~PPB_Graphics3D_Impl() override;
  // ppapi::PPB_Graphics3D_Shared overrides.
  gpu::CommandBuffer* GetCommandBuffer() override;
  gpu::GpuControl* GetGpuControl() override;
  int32_t DoSwapBuffers(const gpu::SyncToken& sync_token) override;

 private:
  explicit PPB_Graphics3D_Impl(PP_Instance instance);

  bool InitRaw(PPB_Graphics3D_API* share_context,
               const int32_t* attrib_list,
               gpu::Capabilities* capabilities,
               base::SharedMemoryHandle* shared_state_handle,
               gpu::CommandBufferId* command_buffer_id);

  // Notifications received from the GPU process.
  void OnSwapBuffers();
  void OnContextLost();
  void OnConsoleMessage(const std::string& msg, int id);
  // Notifications sent to plugin.
  void SendContextLost();

  // True if context is bound to instance.
  bool bound_to_instance_;
  // True when waiting for compositor to commit our backing texture.
  bool commit_pending_;

  gpu::Mailbox mailbox_;
  gpu::SyncToken sync_token_;
  bool has_alpha_;
  scoped_refptr<gpu::GpuChannelHost> channel_;
  std::unique_ptr<gpu::CommandBufferProxyImpl> command_buffer_;

  base::WeakPtrFactory<PPB_Graphics3D_Impl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PPB_Graphics3D_Impl);
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_PPB_GRAPHICS_3D_IMPL_H_
