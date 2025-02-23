// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_ANIMATION_ANIMATION_HOST_H_
#define CC_ANIMATION_ANIMATION_HOST_H_

#include <memory>
#include <unordered_map>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "cc/animation/animation.h"
#include "cc/base/cc_export.h"
#include "cc/trees/mutator_host_client.h"
#include "ui/gfx/geometry/box_f.h"
#include "ui/gfx/geometry/vector2d_f.h"

namespace gfx {
class ScrollOffset;
}

namespace cc {

class AnimationEvents;
class AnimationPlayer;
class AnimationRegistrar;
class AnimationTimeline;
class ElementAnimations;
class LayerAnimationController;
class LayerTreeHost;

enum class ThreadInstance { MAIN, IMPL };

// An AnimationHost contains all the state required to play animations.
// Specifically, it owns all the AnimationTimelines objects.
// There is just one AnimationHost for LayerTreeHost on main renderer thread
// and just one AnimationHost for LayerTreeHostImpl on impl thread.
// We synchronize them during the commit process in a one-way data flow process
// (PushPropertiesTo).
// An AnimationHost talks to its correspondent LayerTreeHost via
// LayerTreeMutatorsClient interface.
// AnimationHost has it's own instance of AnimationRegistrar,
// we want to merge AnimationRegistrar into AnimationHost.
class CC_EXPORT AnimationHost {
 public:
  static std::unique_ptr<AnimationHost> Create(ThreadInstance thread_instance);
  virtual ~AnimationHost();

  void AddAnimationTimeline(scoped_refptr<AnimationTimeline> timeline);
  void RemoveAnimationTimeline(scoped_refptr<AnimationTimeline> timeline);
  AnimationTimeline* GetTimelineById(int timeline_id) const;

  void ClearTimelines();

  void RegisterLayer(int layer_id, LayerTreeType tree_type);
  void UnregisterLayer(int layer_id, LayerTreeType tree_type);

  void RegisterPlayerForLayer(int layer_id, AnimationPlayer* player);
  void UnregisterPlayerForLayer(int layer_id, AnimationPlayer* player);

  ElementAnimations* GetElementAnimationsForLayerId(int layer_id) const;

  // TODO(loyso): Get rid of LayerAnimationController.
  LayerAnimationController* GetControllerForLayerId(int layer_id) const;

  // Parent LayerTreeHost or LayerTreeHostImpl.
  MutatorHostClient* mutator_host_client() { return mutator_host_client_; }
  const MutatorHostClient* mutator_host_client() const {
    return mutator_host_client_;
  }
  void SetMutatorHostClient(MutatorHostClient* client);

  void SetNeedsCommit();
  void SetNeedsRebuildPropertyTrees();

  void PushPropertiesTo(AnimationHost* host_impl);

  AnimationRegistrar* animation_registrar() const {
    return animation_registrar_.get();
  }

  void SetSupportsScrollAnimations(bool supports_scroll_animations);
  bool SupportsScrollAnimations() const;
  bool NeedsAnimateLayers() const;

  bool ActivateAnimations();
  bool AnimateLayers(base::TimeTicks monotonic_time);
  bool UpdateAnimationState(bool start_ready_animations,
                            AnimationEvents* events);

  std::unique_ptr<AnimationEvents> CreateEvents();
  void SetAnimationEvents(std::unique_ptr<AnimationEvents> events);

  bool ScrollOffsetAnimationWasInterrupted(int layer_id) const;

  bool IsAnimatingFilterProperty(int layer_id, LayerTreeType tree_type) const;
  bool IsAnimatingOpacityProperty(int layer_id, LayerTreeType tree_type) const;
  bool IsAnimatingTransformProperty(int layer_id,
                                    LayerTreeType tree_type) const;

  bool HasPotentiallyRunningFilterAnimation(int layer_id,
                                            LayerTreeType tree_type) const;
  bool HasPotentiallyRunningOpacityAnimation(int layer_id,
                                             LayerTreeType tree_type) const;
  bool HasPotentiallyRunningTransformAnimation(int layer_id,
                                               LayerTreeType tree_type) const;

  bool HasAnyAnimationTargetingProperty(int layer_id,
                                        TargetProperty::Type property) const;

  bool FilterIsAnimatingOnImplOnly(int layer_id) const;
  bool OpacityIsAnimatingOnImplOnly(int layer_id) const;
  bool ScrollOffsetIsAnimatingOnImplOnly(int layer_id) const;
  bool TransformIsAnimatingOnImplOnly(int layer_id) const;

  bool HasFilterAnimationThatInflatesBounds(int layer_id) const;
  bool HasTransformAnimationThatInflatesBounds(int layer_id) const;
  bool HasAnimationThatInflatesBounds(int layer_id) const;

  bool FilterAnimationBoundsForBox(int layer_id,
                                   const gfx::BoxF& box,
                                   gfx::BoxF* bounds) const;
  bool TransformAnimationBoundsForBox(int layer_id,
                                      const gfx::BoxF& box,
                                      gfx::BoxF* bounds) const;

  bool HasOnlyTranslationTransforms(int layer_id,
                                    LayerTreeType tree_type) const;
  bool AnimationsPreserveAxisAlignment(int layer_id) const;

  bool MaximumTargetScale(int layer_id,
                          LayerTreeType tree_type,
                          float* max_scale) const;
  bool AnimationStartScale(int layer_id,
                           LayerTreeType tree_type,
                           float* start_scale) const;

  bool HasAnyAnimation(int layer_id) const;
  bool HasActiveAnimationForTesting(int layer_id) const;

  void ImplOnlyScrollAnimationCreate(int layer_id,
                                     const gfx::ScrollOffset& target_offset,
                                     const gfx::ScrollOffset& current_offset);
  bool ImplOnlyScrollAnimationUpdateTarget(
      int layer_id,
      const gfx::Vector2dF& scroll_delta,
      const gfx::ScrollOffset& max_scroll_offset,
      base::TimeTicks frame_monotonic_time);

  void ScrollAnimationAbort(bool needs_completion);

 private:
  explicit AnimationHost(ThreadInstance thread_instance);

  void PushTimelinesToImplThread(AnimationHost* host_impl) const;
  void RemoveTimelinesFromImplThread(AnimationHost* host_impl) const;
  void PushPropertiesToImplThread(AnimationHost* host_impl);

  void EraseTimeline(scoped_refptr<AnimationTimeline> timeline);

  // TODO(loyso): For now AnimationPlayers share LayerAnimationController object
  // if they are attached to the same element(layer). Note that Element can
  // contain many Layers.
  using LayerToElementAnimationsMap =
      std::unordered_map<int, std::unique_ptr<ElementAnimations>>;
  LayerToElementAnimationsMap layer_to_element_animations_map_;

  // A list of all timelines which this host owns.
  using IdToTimelineMap =
      std::unordered_map<int, scoped_refptr<AnimationTimeline>>;
  IdToTimelineMap id_to_timeline_map_;

  std::unique_ptr<AnimationRegistrar> animation_registrar_;
  MutatorHostClient* mutator_host_client_;

  class ScrollOffsetAnimations;
  std::unique_ptr<ScrollOffsetAnimations> scroll_offset_animations_;

  const ThreadInstance thread_instance_;

  DISALLOW_COPY_AND_ASSIGN(AnimationHost);
};

}  // namespace cc

#endif  // CC_ANIMATION_ANIMATION_HOST_H_
