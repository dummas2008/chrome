// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_CLIENT_CONTEXT_PROVIDER_COMMAND_BUFFER_H_
#define CONTENT_COMMON_GPU_CLIENT_CONTEXT_PROVIDER_COMMAND_BUFFER_H_

#include <stdint.h>

#include <memory>

#include "base/compiler_specific.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "cc/blink/context_provider_web_context.h"
#include "cc/output/context_provider.h"
#include "content/common/content_export.h"
#include "content/common/gpu/client/command_buffer_metrics.h"
#include "content/common/gpu/client/webgraphicscontext3d_command_buffer_impl.h"

namespace content {

class GrContextForGLES2Interface;

// Implementation of cc::ContextProvider that provides a
// WebGraphicsContext3DCommandBufferImpl context and a GrContext.
class CONTENT_EXPORT ContextProviderCommandBuffer
    : NON_EXPORTED_BASE(public cc_blink::ContextProviderWebContext) {
 public:
  static scoped_refptr<ContextProviderCommandBuffer> Create(
      std::unique_ptr<WebGraphicsContext3DCommandBufferImpl> context3d,
      CommandBufferContextType type);

  gpu::CommandBufferProxyImpl* GetCommandBufferProxy();

  // cc_blink::ContextProviderWebContext implementation.
  WebGraphicsContext3DCommandBufferImpl* WebContext3D() override;

  // cc::ContextProvider implementation.
  bool BindToCurrentThread() override;
  void DetachFromThread() override;
  gpu::gles2::GLES2Interface* ContextGL() override;
  gpu::ContextSupport* ContextSupport() override;
  class GrContext* GrContext() override;
  void InvalidateGrContext(uint32_t state) override;
  void SetupLock() override;
  base::Lock* GetLock() override;
  Capabilities ContextCapabilities() override;
  void DeleteCachedResources() override;
  void SetLostContextCallback(
      const LostContextCallback& lost_context_callback) override;

 protected:
  ContextProviderCommandBuffer(
      std::unique_ptr<WebGraphicsContext3DCommandBufferImpl> context3d,
      CommandBufferContextType type);
  ~ContextProviderCommandBuffer() override;

  void OnLostContext();

 private:
  void InitializeCapabilities();

  base::ThreadChecker main_thread_checker_;
  base::ThreadChecker context_thread_checker_;

  std::unique_ptr<WebGraphicsContext3DCommandBufferImpl> context3d_;
  std::unique_ptr<GrContextForGLES2Interface> gr_context_;

  cc::ContextProvider::Capabilities capabilities_;
  CommandBufferContextType context_type_;
  std::string debug_name_;

  LostContextCallback lost_context_callback_;

  base::Lock context_lock_;

  class LostContextCallbackProxy;
  std::unique_ptr<LostContextCallbackProxy> lost_context_callback_proxy_;
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_CLIENT_CONTEXT_PROVIDER_COMMAND_BUFFER_H_
