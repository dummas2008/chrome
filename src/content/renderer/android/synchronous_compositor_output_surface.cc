// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/android/synchronous_compositor_output_surface.h"

#include "base/auto_reset.h"
#include "base/logging.h"
#include "base/macros.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/context_provider.h"
#include "cc/output/output_surface_client.h"
#include "cc/output/software_output_device.h"
#include "content/renderer/android/synchronous_compositor_external_begin_frame_source.h"
#include "content/renderer/android/synchronous_compositor_registry.h"
#include "content/renderer/gpu/frame_swap_message_queue.h"
#include "gpu/command_buffer/client/context_support.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/command_buffer/common/gpu_memory_allocation.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/skia_util.h"
#include "ui/gfx/transform.h"

namespace content {

namespace {

const int64_t kFallbackTickTimeoutInMilliseconds = 100;

// Do not limit number of resources, so use an unrealistically high value.
const size_t kNumResourcesLimit = 10 * 1000 * 1000;

}  // namespace

class SynchronousCompositorOutputSurface::SoftwareDevice
  : public cc::SoftwareOutputDevice {
 public:
  SoftwareDevice(SynchronousCompositorOutputSurface* surface)
    : surface_(surface) {
  }
  void Resize(const gfx::Size& pixel_size, float scale_factor) override {
    // Intentional no-op: canvas size is controlled by the embedder.
  }
  SkCanvas* BeginPaint(const gfx::Rect& damage_rect) override {
    if (!surface_->current_sw_canvas_) {
      NOTREACHED() << "BeginPaint with no canvas set";
      return &null_canvas_;
    }
    LOG_IF(WARNING, surface_->did_swap_)
        << "Mutliple calls to BeginPaint per frame";
    return surface_->current_sw_canvas_;
  }
  void EndPaint() override {}

 private:
  SynchronousCompositorOutputSurface* surface_;
  SkCanvas null_canvas_;

  DISALLOW_COPY_AND_ASSIGN(SoftwareDevice);
};

SynchronousCompositorOutputSurface::SynchronousCompositorOutputSurface(
    const scoped_refptr<cc::ContextProvider>& context_provider,
    const scoped_refptr<cc::ContextProvider>& worker_context_provider,
    int routing_id,
    uint32_t output_surface_id,
    SynchronousCompositorRegistry* registry,
    scoped_refptr<FrameSwapMessageQueue> frame_swap_message_queue)
    : cc::OutputSurface(
          context_provider,
          worker_context_provider,
          std::unique_ptr<cc::SoftwareOutputDevice>(new SoftwareDevice(this))),
      routing_id_(routing_id),
      output_surface_id_(output_surface_id),
      registry_(registry),
      registered_(false),
      sync_client_(nullptr),
      current_sw_canvas_(nullptr),
      memory_policy_(0u),
      did_swap_(false),
      frame_swap_message_queue_(frame_swap_message_queue),
      fallback_tick_pending_(false),
      fallback_tick_running_(false) {
  thread_checker_.DetachFromThread();
  DCHECK(registry_);
  capabilities_.adjust_deadline_for_parent = false;
  capabilities_.delegated_rendering = true;
  memory_policy_.priority_cutoff_when_visible =
      gpu::MemoryAllocation::CUTOFF_ALLOW_NICE_TO_HAVE;
}

SynchronousCompositorOutputSurface::~SynchronousCompositorOutputSurface() {}

void SynchronousCompositorOutputSurface::SetSyncClient(
    SynchronousCompositorOutputSurfaceClient* compositor) {
  DCHECK(CalledOnValidThread());
  sync_client_ = compositor;
}

bool SynchronousCompositorOutputSurface::BindToClient(
    cc::OutputSurfaceClient* surface_client) {
  DCHECK(CalledOnValidThread());
  if (!cc::OutputSurface::BindToClient(surface_client))
    return false;

  client_->SetMemoryPolicy(memory_policy_);
  registry_->RegisterOutputSurface(routing_id_, this);
  registered_ = true;
  return true;
}

void SynchronousCompositorOutputSurface::DetachFromClient() {
  DCHECK(CalledOnValidThread());
  if (registered_) {
    registry_->UnregisterOutputSurface(routing_id_, this);
  }
  cc::OutputSurface::DetachFromClient();
  CancelFallbackTick();
}

void SynchronousCompositorOutputSurface::Reshape(const gfx::Size& size,
                                                 float scale_factor,
                                                 bool has_alpha) {
  // Intentional no-op: surface size is controlled by the embedder.
}

void SynchronousCompositorOutputSurface::SwapBuffers(
    cc::CompositorFrame* frame) {
  DCHECK(CalledOnValidThread());
  DCHECK(sync_client_);
  if (!fallback_tick_running_)
    sync_client_->SwapBuffers(output_surface_id_, frame);
  client_->DidSwapBuffers();
  did_swap_ = true;
}

void SynchronousCompositorOutputSurface::CancelFallbackTick() {
  fallback_tick_.Cancel();
  fallback_tick_pending_ = false;
}

void SynchronousCompositorOutputSurface::FallbackTickFired() {
  DCHECK(CalledOnValidThread());
  TRACE_EVENT0("renderer",
               "SynchronousCompositorOutputSurface::FallbackTickFired");
  base::AutoReset<bool> in_fallback_tick(&fallback_tick_running_, true);
  SkBitmap bitmap;
  bitmap.allocN32Pixels(1, 1);
  bitmap.eraseColor(0);
  SkCanvas canvas(bitmap);
  fallback_tick_pending_ = false;
  DemandDrawSw(&canvas);
}

void SynchronousCompositorOutputSurface::Invalidate() {
  DCHECK(CalledOnValidThread());
  if (sync_client_)
    sync_client_->Invalidate();

  if (!fallback_tick_pending_) {
    fallback_tick_.Reset(
        base::Bind(&SynchronousCompositorOutputSurface::FallbackTickFired,
                   base::Unretained(this)));
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE, fallback_tick_.callback(),
        base::TimeDelta::FromMilliseconds(kFallbackTickTimeoutInMilliseconds));
    fallback_tick_pending_ = true;
  }
}

void SynchronousCompositorOutputSurface::DemandDrawHw(
    const gfx::Size& surface_size,
    const gfx::Transform& transform,
    const gfx::Rect& viewport,
    const gfx::Rect& clip,
    const gfx::Rect& viewport_rect_for_tile_priority,
    const gfx::Transform& transform_for_tile_priority) {
  DCHECK(CalledOnValidThread());
  DCHECK(HasClient());
  DCHECK(context_provider_.get());
  CancelFallbackTick();

  surface_size_ = surface_size;
  client_->SetExternalTilePriorityConstraints(viewport_rect_for_tile_priority,
                                              transform_for_tile_priority);
  const bool software_draw = false;
  InvokeComposite(transform, viewport, clip, software_draw);
}

void SynchronousCompositorOutputSurface::DemandDrawSw(SkCanvas* canvas) {
  DCHECK(CalledOnValidThread());
  DCHECK(canvas);
  DCHECK(!current_sw_canvas_);
  CancelFallbackTick();

  base::AutoReset<SkCanvas*> canvas_resetter(&current_sw_canvas_, canvas);

  SkIRect canvas_clip;
  canvas->getClipDeviceBounds(&canvas_clip);
  gfx::Rect clip = gfx::SkIRectToRect(canvas_clip);

  gfx::Transform transform(gfx::Transform::kSkipInitialization);
  transform.matrix() = canvas->getTotalMatrix();  // Converts 3x3 matrix to 4x4.

  surface_size_ = gfx::Size(canvas->getBaseLayerSize().width(),
                            canvas->getBaseLayerSize().height());
  const bool software_draw = true;
  InvokeComposite(transform, clip, clip, software_draw);
}

void SynchronousCompositorOutputSurface::InvokeComposite(
    const gfx::Transform& transform,
    const gfx::Rect& viewport,
    const gfx::Rect& clip,
    bool software_draw) {
  gfx::Transform adjusted_transform = transform;
  adjusted_transform.matrix().postTranslate(-viewport.x(), -viewport.y(), 0);
  did_swap_ = false;
  client_->OnDraw(adjusted_transform, viewport, clip, software_draw);

  if (did_swap_)
    client_->DidSwapBuffersComplete();
}

void SynchronousCompositorOutputSurface::ReturnResources(
    uint32_t output_surface_id,
    const cc::CompositorFrameAck& frame_ack) {
  if (output_surface_id_ == output_surface_id)
    ReclaimResources(&frame_ack);
}

void SynchronousCompositorOutputSurface::SetMemoryPolicy(size_t bytes_limit) {
  DCHECK(CalledOnValidThread());
  bool became_zero = memory_policy_.bytes_limit_when_visible && !bytes_limit;
  bool became_non_zero =
      !memory_policy_.bytes_limit_when_visible && bytes_limit;
  memory_policy_.bytes_limit_when_visible = bytes_limit;
  memory_policy_.num_resources_limit = kNumResourcesLimit;

  if (client_)
    client_->SetMemoryPolicy(memory_policy_);

  if (became_zero) {
    // This is small hack to drop context resources without destroying it
    // when this compositor is put into the background.
    context_provider()->ContextSupport()->SetAggressivelyFreeResources(
        true /* aggressively_free_resources */);
  } else if (became_non_zero) {
    context_provider()->ContextSupport()->SetAggressivelyFreeResources(
        false /* aggressively_free_resources */);
  }
}

void SynchronousCompositorOutputSurface::SetTreeActivationCallback(
    const base::Closure& callback) {
  DCHECK(client_);
  client_->SetTreeActivationCallback(callback);
}

void SynchronousCompositorOutputSurface::GetMessagesToDeliver(
    std::vector<std::unique_ptr<IPC::Message>>* messages) {
  DCHECK(CalledOnValidThread());
  std::unique_ptr<FrameSwapMessageQueue::SendMessageScope> send_message_scope =
      frame_swap_message_queue_->AcquireSendMessageScope();
  frame_swap_message_queue_->DrainMessages(messages);
}

bool SynchronousCompositorOutputSurface::CalledOnValidThread() const {
  return thread_checker_.CalledOnValidThread();
}

}  // namespace content
