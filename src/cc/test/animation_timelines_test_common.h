// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_ANIMATION_TIMELINES_TEST_COMMON_H_
#define CC_TEST_ANIMATION_TIMELINES_TEST_COMMON_H_

#include <memory>
#include <unordered_map>

#include "cc/animation/animation.h"
#include "cc/animation/animation_delegate.h"
#include "cc/animation/animation_host.h"
#include "cc/trees/mutator_host_client.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/scroll_offset.h"

namespace cc {

class TestLayer {
 public:
  static std::unique_ptr<TestLayer> Create();

  void ClearMutatedProperties();

  int transform_x() const { return transform_x_; }
  int transform_y() const { return transform_y_; }

  void set_transform(int transform_x, int transform_y) {
    transform_x_ = transform_x;
    transform_y_ = transform_y;
    mutated_properties_[TargetProperty::TRANSFORM] = true;
  }

  float opacity() const { return opacity_; }
  void set_opacity(float opacity) {
    opacity_ = opacity;
    mutated_properties_[TargetProperty::OPACITY] = true;
  }

  float brightness() const { return brightness_; }
  void set_brightness(float brightness) {
    brightness_ = brightness;
    mutated_properties_[TargetProperty::FILTER] = true;
  }

  gfx::ScrollOffset scroll_offset() const { return scroll_offset_; }
  void set_scroll_offset(const gfx::ScrollOffset& scroll_offset) {
    scroll_offset_ = scroll_offset;
    mutated_properties_[TargetProperty::SCROLL_OFFSET] = true;
  }

  bool is_property_mutated(TargetProperty::Type property) const {
    return mutated_properties_[property];
  }

 private:
  TestLayer();

  int transform_x_;
  int transform_y_;

  float opacity_;
  float brightness_;
  gfx::ScrollOffset scroll_offset_;

  bool mutated_properties_[TargetProperty::LAST_TARGET_PROPERTY + 1];
};

class TestHostClient : public MutatorHostClient {
 public:
  explicit TestHostClient(ThreadInstance thread_instance);
  ~TestHostClient();

  void ClearMutatedProperties();

  bool IsLayerInTree(int layer_id, LayerTreeType tree_type) const override;

  void SetMutatorsNeedCommit() override;
  void SetMutatorsNeedRebuildPropertyTrees() override;

  void SetLayerFilterMutated(int layer_id,
                             LayerTreeType tree_type,
                             const FilterOperations& filters) override;

  void SetLayerOpacityMutated(int layer_id,
                              LayerTreeType tree_type,
                              float opacity) override;

  void SetLayerTransformMutated(int layer_id,
                                LayerTreeType tree_type,
                                const gfx::Transform& transform) override;

  void SetLayerScrollOffsetMutated(
      int layer_id,
      LayerTreeType tree_type,
      const gfx::ScrollOffset& scroll_offset) override;

  void LayerTransformIsPotentiallyAnimatingChanged(int layer_id,
                                                   LayerTreeType tree_type,
                                                   bool is_animating) override {
  }

  void ScrollOffsetAnimationFinished() override {}
  gfx::ScrollOffset GetScrollOffsetForAnimation(int layer_id) const override;

  bool mutators_need_commit() const { return mutators_need_commit_; }
  void set_mutators_need_commit(bool need) { mutators_need_commit_ = need; }

  void RegisterLayer(int layer_id, LayerTreeType tree_type);
  void UnregisterLayer(int layer_id, LayerTreeType tree_type);

  AnimationHost* host() {
    DCHECK(host_);
    return host_.get();
  }

  bool IsPropertyMutated(int layer_id,
                         LayerTreeType tree_type,
                         TargetProperty::Type property) const;

  void ExpectFilterPropertyMutated(int layer_id,
                                   LayerTreeType tree_type,
                                   float brightness) const;
  void ExpectOpacityPropertyMutated(int layer_id,
                                    LayerTreeType tree_type,
                                    float opacity) const;
  void ExpectTransformPropertyMutated(int layer_id,
                                      LayerTreeType tree_type,
                                      int transform_x,
                                      int transform_y) const;

  TestLayer* FindTestLayer(int layer_id, LayerTreeType tree_type) const;

 private:
  std::unique_ptr<AnimationHost> host_;

  using LayerIdToTestLayer =
      std::unordered_map<int, std::unique_ptr<TestLayer>>;
  LayerIdToTestLayer layers_in_active_tree_;
  LayerIdToTestLayer layers_in_pending_tree_;

  bool mutators_need_commit_;
};

class TestAnimationDelegate : public AnimationDelegate {
 public:
  TestAnimationDelegate();

  void NotifyAnimationStarted(base::TimeTicks monotonic_time,
                              TargetProperty::Type target_property,
                              int group) override;
  void NotifyAnimationFinished(base::TimeTicks monotonic_time,
                               TargetProperty::Type target_property,
                               int group) override;
  void NotifyAnimationAborted(base::TimeTicks monotonic_time,
                              TargetProperty::Type target_property,
                              int group) override {}
  void NotifyAnimationTakeover(base::TimeTicks monotonic_time,
                               TargetProperty::Type target_property,
                               double animation_start_time,
                               std::unique_ptr<AnimationCurve> curve) override {
  }
  bool started_;
  bool finished_;
};

class AnimationTimelinesTest : public testing::Test {
 public:
  AnimationTimelinesTest();
  ~AnimationTimelinesTest() override;

 protected:
  void SetUp() override;
  void TearDown() override;

  void GetImplTimelineAndPlayerByID();

  void ReleaseRefPtrs();

  void AnimateLayersTransferEvents(base::TimeTicks time,
                                   unsigned expect_events);

  AnimationPlayer* GetPlayerForLayerId(int layer_id);
  AnimationPlayer* GetImplPlayerForLayerId(int layer_id);

  int NextTestLayerId();

  TestHostClient client_;
  TestHostClient client_impl_;

  AnimationHost* host_;
  AnimationHost* host_impl_;

  const int timeline_id_;
  const int player_id_;
  int layer_id_;

  int next_test_layer_id_;

  scoped_refptr<AnimationTimeline> timeline_;
  scoped_refptr<AnimationPlayer> player_;

  scoped_refptr<AnimationTimeline> timeline_impl_;
  scoped_refptr<AnimationPlayer> player_impl_;
};

}  // namespace cc

#endif  // CC_TEST_ANIMATION_TIMELINES_TEST_COMMON_H_
