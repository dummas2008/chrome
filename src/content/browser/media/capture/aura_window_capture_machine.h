// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_CAPTURE_AURA_WINDOW_CAPTURE_MACHINE_H_
#define CONTENT_BROWSER_MEDIA_CAPTURE_AURA_WINDOW_CAPTURE_MACHINE_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/media/capture/cursor_renderer_aura.h"
#include "media/capture/content/screen_capture_device_core.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/base/cursor/cursors_aura.h"
#include "ui/compositor/compositor.h"

namespace cc {

class CopyOutputResult;

}  // namespace cc

namespace content {

class PowerSaveBlocker;
class ReadbackYUVInterface;

class AuraWindowCaptureMachine
    : public media::VideoCaptureMachine,
      public aura::WindowObserver,
      public ui::CompositorObserver {
 public:
  AuraWindowCaptureMachine();
  ~AuraWindowCaptureMachine() override;

  // VideoCaptureMachine overrides.
  void Start(const scoped_refptr<media::ThreadSafeCaptureOracle>& oracle_proxy,
             const media::VideoCaptureParams& params,
             const base::Callback<void(bool)> callback) override;
  void Stop(const base::Closure& callback) override;
  void MaybeCaptureForRefresh() override;

  // Implements aura::WindowObserver.
  void OnWindowBoundsChanged(aura::Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds) override;
  void OnWindowDestroying(aura::Window* window) override;
  void OnWindowAddedToRootWindow(aura::Window* window) override;
  void OnWindowRemovingFromRootWindow(aura::Window* window,
                                      aura::Window* new_root) override;

  // Implements ui::CompositorObserver.
  void OnCompositingDidCommit(ui::Compositor* compositor) override {}
  void OnCompositingStarted(ui::Compositor* compositor,
                            base::TimeTicks start_time) override {}
  void OnCompositingEnded(ui::Compositor* compositor) override;
  void OnCompositingAborted(ui::Compositor* compositor) override {}
  void OnCompositingLockStateChanged(ui::Compositor* compositor) override {}
  void OnCompositingShuttingDown(ui::Compositor* compositor) override {}

  // Sets the window to use for capture.
  void SetWindow(aura::Window* window);

 private:
  bool InternalStart(
      const scoped_refptr<media::ThreadSafeCaptureOracle>& oracle_proxy,
      const media::VideoCaptureParams& params);
  void InternalStop(const base::Closure& callback);

  // Captures a frame.
  // |dirty| is false for refresh requests and true for compositor updates.
  void Capture(bool dirty);

  // Update capture size. Must be called on the UI thread.
  void UpdateCaptureSize();

  using CaptureFrameCallback =
      media::ThreadSafeCaptureOracle::CaptureFrameCallback;

  // Response callback for cc::Layer::RequestCopyOfOutput().
  void DidCopyOutput(scoped_refptr<media::VideoFrame> video_frame,
                     base::TimeTicks start_time,
                     const CaptureFrameCallback& capture_frame_cb,
                     std::unique_ptr<cc::CopyOutputResult> result);

  // A helper which does the real work for DidCopyOutput. Returns true if
  // succeeded.
  bool ProcessCopyOutputResponse(scoped_refptr<media::VideoFrame> video_frame,
                                 base::TimeTicks start_time,
                                 const CaptureFrameCallback& capture_frame_cb,
                                 std::unique_ptr<cc::CopyOutputResult> result);

  // Renders the cursor if needed and then delivers the captured frame.
  static void CopyOutputFinishedForVideo(
      base::WeakPtr<AuraWindowCaptureMachine> machine,
      base::TimeTicks start_time,
      const CaptureFrameCallback& capture_frame_cb,
      const scoped_refptr<media::VideoFrame>& target,
      std::unique_ptr<cc::SingleReleaseCallback> release_callback,
      bool result);

  // The window associated with the desktop.
  aura::Window* desktop_window_;

  // Whether screen capturing or window capture.
  bool screen_capture_;

  // Makes all the decisions about which frames to copy, and how.
  scoped_refptr<media::ThreadSafeCaptureOracle> oracle_proxy_;

  // The capture parameters for this capture.
  media::VideoCaptureParams capture_params_;

  // YUV readback pipeline.
  std::unique_ptr<content::ReadbackYUVInterface> yuv_readback_pipeline_;

  // Renders mouse cursor on frame.
  std::unique_ptr<content::CursorRendererAura> cursor_renderer_;

  // TODO(jiayl): Remove power_save_blocker_ when there is an API to keep the
  // screen from sleeping for the drive-by web.
  std::unique_ptr<PowerSaveBlocker> power_save_blocker_;

  // WeakPtrs are used for the asynchronous capture callbacks passed to external
  // modules.  They are only valid on the UI thread and become invalidated
  // immediately when InternalStop() is called to ensure that no more captured
  // frames will be delivered to the client.
  base::WeakPtrFactory<AuraWindowCaptureMachine> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(AuraWindowCaptureMachine);
};

}  // namespace content

#endif  // CONTENT_BROWSER_MEDIA_CAPTURE_AURA_WINDOW_CAPTURE_MACHINE_H_
