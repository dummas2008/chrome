// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_ANIMATION_ANIMATION_REGISTRAR_H_
#define CC_ANIMATION_ANIMATION_REGISTRAR_H_

#include <memory>
#include <unordered_map>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "cc/base/cc_export.h"

namespace cc {

class AnimationEvents;
class LayerAnimationController;

class CC_EXPORT AnimationRegistrar {
 public:
  using AnimationControllerMap =
      std::unordered_map<int, LayerAnimationController*>;

  static std::unique_ptr<AnimationRegistrar> Create() {
    return base::WrapUnique(new AnimationRegistrar());
  }

  virtual ~AnimationRegistrar();

  // If an animation has been registered for the given id, return it. Otherwise
  // creates a new one and returns a scoped_refptr to it.
  scoped_refptr<LayerAnimationController> GetAnimationControllerForId(int id);

  // Registers the given animation controller as active. An active animation
  // controller is one that has a running animation that needs to be ticked.
  void DidActivateAnimationController(LayerAnimationController* controller);

  // Unregisters the given animation controller. When this happens, the
  // animation controller will no longer be ticked (since it's not active). It
  // is not an error to call this function with a deactivated controller.
  void DidDeactivateAnimationController(LayerAnimationController* controller);

  // Registers the given controller as alive.
  void RegisterAnimationController(LayerAnimationController* controller);

  // Unregisters the given controller as alive.
  void UnregisterAnimationController(LayerAnimationController* controller);

  const AnimationControllerMap& active_animation_controllers_for_testing()
      const {
    return active_animation_controllers_;
  }

  const AnimationControllerMap& all_animation_controllers_for_testing() const {
    return all_animation_controllers_;
  }

  void set_supports_scroll_animations(bool supports_scroll_animations) {
    supports_scroll_animations_ = supports_scroll_animations;
  }

  bool supports_scroll_animations() { return supports_scroll_animations_; }

  bool needs_animate_layers() const {
    return !active_animation_controllers_.empty();
  }

  bool ActivateAnimations();
  bool AnimateLayers(base::TimeTicks monotonic_time);
  bool UpdateAnimationState(bool start_ready_animations,
                            AnimationEvents* events);

  std::unique_ptr<AnimationEvents> CreateEvents();
  void SetAnimationEvents(std::unique_ptr<AnimationEvents> events);

 private:
  AnimationRegistrar();

  AnimationControllerMap active_animation_controllers_;
  AnimationControllerMap all_animation_controllers_;

  bool supports_scroll_animations_;

  DISALLOW_COPY_AND_ASSIGN(AnimationRegistrar);
};

}  // namespace cc

#endif  // CC_ANIMATION_ANIMATION_REGISTRAR_H_
