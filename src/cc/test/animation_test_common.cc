// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/animation_test_common.h"

#include "base/memory/ptr_util.h"
#include "cc/animation/animation_host.h"
#include "cc/animation/animation_id_provider.h"
#include "cc/animation/animation_player.h"
#include "cc/animation/keyframed_animation_curve.h"
#include "cc/animation/layer_animation_controller.h"
#include "cc/animation/transform_operations.h"
#include "cc/base/time_util.h"
#include "cc/layers/layer.h"
#include "cc/layers/layer_impl.h"

using cc::Animation;
using cc::AnimationCurve;
using cc::EaseTimingFunction;
using cc::FloatKeyframe;
using cc::KeyframedFloatAnimationCurve;
using cc::KeyframedTransformAnimationCurve;
using cc::TimingFunction;
using cc::TransformKeyframe;

namespace cc {

template <class Target>
int AddOpacityTransition(Target* target,
                         double duration,
                         float start_opacity,
                         float end_opacity,
                         bool use_timing_function) {
  std::unique_ptr<KeyframedFloatAnimationCurve> curve(
      KeyframedFloatAnimationCurve::Create());

  std::unique_ptr<TimingFunction> func;
  if (!use_timing_function)
    func = EaseTimingFunction::Create();
  if (duration > 0.0)
    curve->AddKeyframe(FloatKeyframe::Create(base::TimeDelta(), start_opacity,
                                             std::move(func)));
  curve->AddKeyframe(FloatKeyframe::Create(
      base::TimeDelta::FromSecondsD(duration), end_opacity, nullptr));

  int id = AnimationIdProvider::NextAnimationId();

  std::unique_ptr<Animation> animation(Animation::Create(
      std::move(curve), id, AnimationIdProvider::NextGroupId(),
      TargetProperty::OPACITY));
  animation->set_needs_synchronized_start_time(true);

  target->AddAnimation(std::move(animation));
  return id;
}

template <class Target>
int AddAnimatedTransform(Target* target,
                         double duration,
                         TransformOperations start_operations,
                         TransformOperations operations) {
  std::unique_ptr<KeyframedTransformAnimationCurve> curve(
      KeyframedTransformAnimationCurve::Create());

  if (duration > 0.0) {
    curve->AddKeyframe(TransformKeyframe::Create(base::TimeDelta(),
                                                 start_operations, nullptr));
  }

  curve->AddKeyframe(TransformKeyframe::Create(
      base::TimeDelta::FromSecondsD(duration), operations, nullptr));

  int id = AnimationIdProvider::NextAnimationId();

  std::unique_ptr<Animation> animation(Animation::Create(
      std::move(curve), id, AnimationIdProvider::NextGroupId(),
      TargetProperty::TRANSFORM));
  animation->set_needs_synchronized_start_time(true);

  target->AddAnimation(std::move(animation));
  return id;
}

template <class Target>
int AddAnimatedTransform(Target* target,
                         double duration,
                         int delta_x,
                         int delta_y) {
  TransformOperations start_operations;
  if (duration > 0.0) {
    start_operations.AppendTranslate(0, 0, 0.0);
  }

  TransformOperations operations;
  operations.AppendTranslate(delta_x, delta_y, 0.0);
  return AddAnimatedTransform(target, duration, start_operations, operations);
}

template <class Target>
int AddAnimatedFilter(Target* target,
                      double duration,
                      float start_brightness,
                      float end_brightness) {
  std::unique_ptr<KeyframedFilterAnimationCurve> curve(
      KeyframedFilterAnimationCurve::Create());

  if (duration > 0.0) {
    FilterOperations start_filters;
    start_filters.Append(
        FilterOperation::CreateBrightnessFilter(start_brightness));
    curve->AddKeyframe(
        FilterKeyframe::Create(base::TimeDelta(), start_filters, nullptr));
  }

  FilterOperations filters;
  filters.Append(FilterOperation::CreateBrightnessFilter(end_brightness));
  curve->AddKeyframe(FilterKeyframe::Create(
      base::TimeDelta::FromSecondsD(duration), filters, nullptr));

  int id = AnimationIdProvider::NextAnimationId();

  std::unique_ptr<Animation> animation(Animation::Create(
      std::move(curve), id, AnimationIdProvider::NextGroupId(),
      TargetProperty::FILTER));
  animation->set_needs_synchronized_start_time(true);

  target->AddAnimation(std::move(animation));
  return id;
}

FakeFloatAnimationCurve::FakeFloatAnimationCurve()
    : duration_(base::TimeDelta::FromSecondsD(1.0)) {
}

FakeFloatAnimationCurve::FakeFloatAnimationCurve(double duration)
    : duration_(base::TimeDelta::FromSecondsD(duration)) {
}

FakeFloatAnimationCurve::~FakeFloatAnimationCurve() {}

base::TimeDelta FakeFloatAnimationCurve::Duration() const {
  return duration_;
}

float FakeFloatAnimationCurve::GetValue(base::TimeDelta now) const {
  return 0.0f;
}

std::unique_ptr<AnimationCurve> FakeFloatAnimationCurve::Clone() const {
  return base::WrapUnique(new FakeFloatAnimationCurve);
}

FakeTransformTransition::FakeTransformTransition(double duration)
    : duration_(base::TimeDelta::FromSecondsD(duration)) {
}

FakeTransformTransition::~FakeTransformTransition() {}

base::TimeDelta FakeTransformTransition::Duration() const {
  return duration_;
}

gfx::Transform FakeTransformTransition::GetValue(base::TimeDelta time) const {
  return gfx::Transform();
}

bool FakeTransformTransition::AnimatedBoundsForBox(const gfx::BoxF& box,
                                                   gfx::BoxF* bounds) const {
  return false;
}

bool FakeTransformTransition::AffectsScale() const { return false; }

bool FakeTransformTransition::IsTranslation() const { return true; }

bool FakeTransformTransition::PreservesAxisAlignment() const {
  return true;
}

bool FakeTransformTransition::AnimationStartScale(bool forward_direction,
                                                  float* start_scale) const {
  *start_scale = 1.f;
  return true;
}

bool FakeTransformTransition::MaximumTargetScale(bool forward_direction,
                                                 float* max_scale) const {
  *max_scale = 1.f;
  return true;
}

std::unique_ptr<AnimationCurve> FakeTransformTransition::Clone() const {
  return base::WrapUnique(new FakeTransformTransition(*this));
}

FakeFloatTransition::FakeFloatTransition(double duration, float from, float to)
    : duration_(base::TimeDelta::FromSecondsD(duration)), from_(from), to_(to) {
}

FakeFloatTransition::~FakeFloatTransition() {}

base::TimeDelta FakeFloatTransition::Duration() const {
  return duration_;
}

float FakeFloatTransition::GetValue(base::TimeDelta time) const {
  double progress = TimeUtil::Divide(time, duration_);
  if (progress >= 1.0)
    progress = 1.0;
  return (1.0 - progress) * from_ + progress * to_;
}

FakeLayerAnimationValueObserver::FakeLayerAnimationValueObserver()
    : opacity_(0.0f),
      animation_waiting_for_deletion_(false),
      transform_is_animating_(false) {}

FakeLayerAnimationValueObserver::~FakeLayerAnimationValueObserver() {}

void FakeLayerAnimationValueObserver::OnFilterAnimated(
    const FilterOperations& filters) {
  filters_ = filters;
}

void FakeLayerAnimationValueObserver::OnOpacityAnimated(float opacity) {
  opacity_ = opacity;
}

void FakeLayerAnimationValueObserver::OnTransformAnimated(
    const gfx::Transform& transform) {
  transform_ = transform;
}

void FakeLayerAnimationValueObserver::OnScrollOffsetAnimated(
    const gfx::ScrollOffset& scroll_offset) {
  scroll_offset_ = scroll_offset;
}

void FakeLayerAnimationValueObserver::OnAnimationWaitingForDeletion() {
  animation_waiting_for_deletion_ = true;
}

void FakeLayerAnimationValueObserver::OnTransformIsPotentiallyAnimatingChanged(
    bool is_animating) {
  transform_is_animating_ = is_animating;
}

bool FakeLayerAnimationValueObserver::IsActive() const {
  return true;
}

bool FakeInactiveLayerAnimationValueObserver::IsActive() const {
  return false;
}

gfx::ScrollOffset FakeLayerAnimationValueProvider::ScrollOffsetForAnimation()
    const {
  return scroll_offset_;
}

std::unique_ptr<AnimationCurve> FakeFloatTransition::Clone() const {
  return base::WrapUnique(new FakeFloatTransition(*this));
}

int AddOpacityTransitionToController(LayerAnimationController* controller,
                                     double duration,
                                     float start_opacity,
                                     float end_opacity,
                                     bool use_timing_function) {
  return AddOpacityTransition(controller,
                              duration,
                              start_opacity,
                              end_opacity,
                              use_timing_function);
}

int AddAnimatedTransformToController(LayerAnimationController* controller,
                                     double duration,
                                     int delta_x,
                                     int delta_y) {
  return AddAnimatedTransform(controller,
                              duration,
                              delta_x,
                              delta_y);
}

int AddAnimatedFilterToController(LayerAnimationController* controller,
                                  double duration,
                                  float start_brightness,
                                  float end_brightness) {
  return AddAnimatedFilter(
      controller, duration, start_brightness, end_brightness);
}

int AddAnimatedTransformToPlayer(AnimationPlayer* player,
                                 double duration,
                                 int delta_x,
                                 int delta_y) {
  return AddAnimatedTransform(player, duration, delta_x, delta_y);
}

int AddAnimatedTransformToPlayer(AnimationPlayer* player,
                                 double duration,
                                 TransformOperations start_operations,
                                 TransformOperations operations) {
  return AddAnimatedTransform(player, duration, start_operations, operations);
}

int AddOpacityTransitionToPlayer(AnimationPlayer* player,
                                 double duration,
                                 float start_opacity,
                                 float end_opacity,
                                 bool use_timing_function) {
  return AddOpacityTransition(player, duration, start_opacity, end_opacity,
                              use_timing_function);
}

int AddAnimatedFilterToPlayer(AnimationPlayer* player,
                              double duration,
                              float start_brightness,
                              float end_brightness) {
  return AddAnimatedFilter(player, duration, start_brightness, end_brightness);
}

int AddOpacityStepsToController(LayerAnimationController* target,
                                double duration,
                                float start_opacity,
                                float end_opacity,
                                int num_steps) {
  std::unique_ptr<KeyframedFloatAnimationCurve> curve(
      KeyframedFloatAnimationCurve::Create());

  std::unique_ptr<TimingFunction> func =
      StepsTimingFunction::Create(num_steps, 0.5f);
  if (duration > 0.0)
    curve->AddKeyframe(FloatKeyframe::Create(base::TimeDelta(), start_opacity,
                                             std::move(func)));
  curve->AddKeyframe(FloatKeyframe::Create(
      base::TimeDelta::FromSecondsD(duration), end_opacity, nullptr));

  int id = AnimationIdProvider::NextAnimationId();

  std::unique_ptr<Animation> animation(Animation::Create(
      std::move(curve), id, AnimationIdProvider::NextGroupId(),
      TargetProperty::OPACITY));
  animation->set_needs_synchronized_start_time(true);

  target->AddAnimation(std::move(animation));
  return id;
}

void AddAnimationToLayerWithPlayer(int layer_id,
                                   scoped_refptr<AnimationTimeline> timeline,
                                   std::unique_ptr<Animation> animation) {
  scoped_refptr<AnimationPlayer> player =
      AnimationPlayer::Create(AnimationIdProvider::NextPlayerId());
  timeline->AttachPlayer(player);
  player->AttachLayer(layer_id);
  DCHECK(player->element_animations());
  player->AddAnimation(std::move(animation));
}

void AddAnimationToLayerWithExistingPlayer(
    int layer_id,
    scoped_refptr<AnimationTimeline> timeline,
    std::unique_ptr<Animation> animation) {
  LayerAnimationController* controller =
      timeline->animation_host()->GetControllerForLayerId(layer_id);
  DCHECK(controller);
  controller->AddAnimation(std::move(animation));
}

void RemoveAnimationFromLayerWithExistingPlayer(
    int layer_id,
    scoped_refptr<AnimationTimeline> timeline,
    int animation_id) {
  LayerAnimationController* controller =
      timeline->animation_host()->GetControllerForLayerId(layer_id);
  DCHECK(controller);
  controller->RemoveAnimation(animation_id);
}

Animation* GetAnimationFromLayerWithExistingPlayer(
    int layer_id,
    scoped_refptr<AnimationTimeline> timeline,
    int animation_id) {
  LayerAnimationController* controller =
      timeline->animation_host()->GetControllerForLayerId(layer_id);
  DCHECK(controller);
  return controller->GetAnimationById(animation_id);
}

int AddAnimatedFilterToLayerWithPlayer(
    int layer_id,
    scoped_refptr<AnimationTimeline> timeline,
    double duration,
    float start_brightness,
    float end_brightness) {
  scoped_refptr<AnimationPlayer> player =
      AnimationPlayer::Create(AnimationIdProvider::NextPlayerId());
  timeline->AttachPlayer(player);
  player->AttachLayer(layer_id);
  DCHECK(player->element_animations());
  return AddAnimatedFilterToPlayer(player.get(), duration, start_brightness,
                                   end_brightness);
}

int AddAnimatedTransformToLayerWithPlayer(
    int layer_id,
    scoped_refptr<AnimationTimeline> timeline,
    double duration,
    int delta_x,
    int delta_y) {
  scoped_refptr<AnimationPlayer> player =
      AnimationPlayer::Create(AnimationIdProvider::NextPlayerId());
  timeline->AttachPlayer(player);
  player->AttachLayer(layer_id);
  DCHECK(player->element_animations());
  return AddAnimatedTransformToPlayer(player.get(), duration, delta_x, delta_y);
}

int AddAnimatedTransformToLayerWithPlayer(
    int layer_id,
    scoped_refptr<AnimationTimeline> timeline,
    double duration,
    TransformOperations start_operations,
    TransformOperations operations) {
  scoped_refptr<AnimationPlayer> player =
      AnimationPlayer::Create(AnimationIdProvider::NextPlayerId());
  timeline->AttachPlayer(player);
  player->AttachLayer(layer_id);
  DCHECK(player->element_animations());
  return AddAnimatedTransformToPlayer(player.get(), duration, start_operations,
                                      operations);
}

int AddOpacityTransitionToLayerWithPlayer(
    int layer_id,
    scoped_refptr<AnimationTimeline> timeline,
    double duration,
    float start_opacity,
    float end_opacity,
    bool use_timing_function) {
  scoped_refptr<AnimationPlayer> player =
      AnimationPlayer::Create(AnimationIdProvider::NextPlayerId());
  timeline->AttachPlayer(player);
  player->AttachLayer(layer_id);
  DCHECK(player->element_animations());
  return AddOpacityTransitionToPlayer(player.get(), duration, start_opacity,
                                      end_opacity, use_timing_function);
}

void AbortAnimationsOnLayerWithPlayer(int layer_id,
                                      scoped_refptr<AnimationTimeline> timeline,
                                      TargetProperty::Type target_property) {
  LayerAnimationController* controller =
      timeline->animation_host()->GetControllerForLayerId(layer_id);
  DCHECK(controller);
  controller->AbortAnimations(target_property);
}

}  // namespace cc
