// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/gpu/delegated_compositor_output_surface.h"
#include "content/renderer/gpu/frame_swap_message_queue.h"

namespace content {

DelegatedCompositorOutputSurface::DelegatedCompositorOutputSurface(
    int32_t routing_id,
    uint32_t output_surface_id,
    const scoped_refptr<ContextProviderCommandBuffer>& context_provider,
    const scoped_refptr<ContextProviderCommandBuffer>& worker_context_provider,
#if defined(ENABLE_VULKAN)
    const scoped_refptr<cc::VulkanContextProvider>& vulkan_context_provider,
#endif
    scoped_refptr<FrameSwapMessageQueue> swap_frame_message_queue)
    : CompositorOutputSurface(routing_id,
                              output_surface_id,
                              context_provider,
                              worker_context_provider,
#if defined(ENABLE_VULKAN)
                              vulkan_context_provider,
#endif
                              std::unique_ptr<cc::SoftwareOutputDevice>(),
                              swap_frame_message_queue,
                              true) {
  capabilities_.delegated_rendering = true;
}

}  // namespace content
