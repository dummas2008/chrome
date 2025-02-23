// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_FAKE_LAYER_TREE_HOST_IMPL_CLIENT_H_
#define CC_TEST_FAKE_LAYER_TREE_HOST_IMPL_CLIENT_H_

#include "cc/debug/frame_timing_tracker.h"
#include "cc/output/begin_frame_args.h"
#include "cc/trees/layer_tree_host_impl.h"

namespace cc {

class FakeLayerTreeHostImplClient : public LayerTreeHostImplClient {
 public:
  // LayerTreeHostImplClient implementation.
  void UpdateRendererCapabilitiesOnImplThread() override {}
  void DidLoseOutputSurfaceOnImplThread() override {}
  void CommitVSyncParameters(base::TimeTicks timebase,
                             base::TimeDelta interval) override {}
  void SetEstimatedParentDrawTime(base::TimeDelta draw_time) override {}
  void DidSwapBuffersOnImplThread() override {}
  void DidSwapBuffersCompleteOnImplThread() override {}
  void OnCanDrawStateChanged(bool can_draw) override {}
  void NotifyReadyToActivate() override {}
  void NotifyReadyToDraw() override {}
  void SetNeedsRedrawOnImplThread() override {}
  void SetNeedsRedrawRectOnImplThread(const gfx::Rect& damage_rect) override {}
  void SetNeedsOneBeginImplFrameOnImplThread() override {}
  void SetNeedsCommitOnImplThread() override {}
  void SetNeedsPrepareTilesOnImplThread() override {}
  void SetVideoNeedsBeginFrames(bool needs_begin_frames) override {}
  void PostAnimationEventsToMainThreadOnImplThread(
      std::unique_ptr<AnimationEvents> events) override;
  bool IsInsideDraw() override;
  void RenewTreePriority() override {}
  void PostDelayedAnimationTaskOnImplThread(const base::Closure& task,
                                            base::TimeDelta delay) override {}
  void DidActivateSyncTree() override {}
  void WillPrepareTiles() override {}
  void DidPrepareTiles() override {}
  void DidCompletePageScaleAnimationOnImplThread() override {}
  void OnDrawForOutputSurface(bool resourceless_software_draw) override {}
  void PostFrameTimingEventsOnImplThread(
      std::unique_ptr<FrameTimingTracker::CompositeTimingSet> composite_events,
      std::unique_ptr<FrameTimingTracker::MainFrameTimingSet> main_frame_events)
      override {}
};

}  // namespace cc

#endif  // CC_TEST_FAKE_LAYER_TREE_HOST_IMPL_CLIENT_H_
