// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_LAYER_TREE_HOST_COMMON_TEST_H_
#define CC_TEST_LAYER_TREE_HOST_COMMON_TEST_H_

#include <algorithm>
#include <memory>
#include <vector>

#include "cc/layers/layer_collections.h"
#include "cc/test/fake_layer_tree_host_client.h"
#include "cc/test/layer_test_common.h"
#include "cc/test/test_task_graph_runner.h"
#include "cc/trees/layer_tree_settings.h"
#include "cc/trees/property_tree.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace gfx {
class PointF;
class Point3F;
class Size;
class Transform;
}

namespace cc {

class FakeLayerTreeHost;
class Layer;
class LayerImpl;

class LayerTreeHostCommonTestBase : public LayerTestCommon::LayerImplTest {
 protected:
  explicit LayerTreeHostCommonTestBase(const LayerTreeSettings& settings);
  virtual ~LayerTreeHostCommonTestBase();

  template <typename LayerType>
  void SetLayerPropertiesForTestingInternal(
      LayerType* layer,
      const gfx::Transform& transform,
      const gfx::Point3F& transform_origin,
      const gfx::PointF& position,
      const gfx::Size& bounds,
      bool flatten_transform,
      bool is_3d_sorted) {
    layer->SetTransform(transform);
    layer->SetTransformOrigin(transform_origin);
    layer->SetPosition(position);
    layer->SetBounds(bounds);
    layer->SetShouldFlattenTransform(flatten_transform);
    layer->Set3dSortingContextId(is_3d_sorted ? 1 : 0);
  }

  void SetLayerPropertiesForTesting(Layer* layer,
                                    const gfx::Transform& transform,
                                    const gfx::Point3F& transform_origin,
                                    const gfx::PointF& position,
                                    const gfx::Size& bounds,
                                    bool flatten_transform,
                                    bool is_3d_sorted);

  void SetLayerPropertiesForTesting(LayerImpl* layer,
                                    const gfx::Transform& transform,
                                    const gfx::Point3F& transform_origin,
                                    const gfx::PointF& position,
                                    const gfx::Size& bounds,
                                    bool flatten_transform,
                                    bool is_3d_sorted);

  void SetLayerPropertiesForTesting(LayerImpl* layer,
                                    const gfx::Transform& transform,
                                    const gfx::Point3F& transform_origin,
                                    const gfx::PointF& position,
                                    const gfx::Size& bounds,
                                    bool flatten_transform,
                                    bool is_3d_sorted,
                                    bool create_render_surface);

  void ExecuteCalculateDrawProperties(Layer* root_layer,
                                      float device_scale_factor,
                                      float page_scale_factor,
                                      Layer* page_scale_application_layer,
                                      bool can_use_lcd_text,
                                      bool layers_always_allowed_lcd_text);

  void ExecuteCalculateDrawProperties(LayerImpl* root_layer,
                                      float device_scale_factor,
                                      float page_scale_factor,
                                      LayerImpl* page_scale_application_layer,
                                      bool can_use_lcd_text,
                                      bool layers_always_allowed_lcd_text);

  template <class LayerType>
  void ExecuteCalculateDrawProperties(LayerType* root_layer) {
    LayerType* page_scale_application_layer = NULL;
    ExecuteCalculateDrawProperties(root_layer, 1.f, 1.f,
                                   page_scale_application_layer, false, false);
  }

  template <class LayerType>
  void ExecuteCalculateDrawProperties(LayerType* root_layer,
                                      float device_scale_factor) {
    LayerType* page_scale_application_layer = NULL;
    ExecuteCalculateDrawProperties(root_layer, device_scale_factor, 1.f,
                                   page_scale_application_layer, false, false);
  }

  template <class LayerType>
  void ExecuteCalculateDrawProperties(LayerType* root_layer,
                                      float device_scale_factor,
                                      float page_scale_factor,
                                      LayerType* page_scale_application_layer) {
    ExecuteCalculateDrawProperties(root_layer, device_scale_factor,
                                   page_scale_factor,
                                   page_scale_application_layer, false, false);
  }

  void ExecuteCalculateDrawPropertiesWithPropertyTrees(Layer* layer);
  void ExecuteCalculateDrawPropertiesWithPropertyTrees(LayerImpl* layer);

  void ExecuteCalculateDrawPropertiesWithoutSeparateSurfaces(
      LayerImpl* root_layer);

  LayerImplList* render_surface_layer_list_impl() const {
    return render_surface_layer_list_impl_.get();
  }

  LayerImplList* update_layer_list_impl() const {
    return update_layer_list_impl_.get();
  }
  bool UpdateLayerListImplContains(int id) const {
    return std::count_if(
               update_layer_list_impl_->begin(), update_layer_list_impl_->end(),
               [id](LayerImpl* layer) { return layer->id() == id; }) != 0;
  }

  const LayerList& update_layer_list() const { return update_layer_list_; }
  bool UpdateLayerListContains(int id) const;

  int render_surface_layer_list_count() const {
    return render_surface_layer_list_count_;
  }

 private:
  std::unique_ptr<std::vector<LayerImpl*>> render_surface_layer_list_impl_;
  LayerList update_layer_list_;
  std::unique_ptr<LayerImplList> update_layer_list_impl_;

  int render_surface_layer_list_count_;
};

class LayerTreeHostCommonTest : public LayerTreeHostCommonTestBase,
                                public testing::Test {
 public:
  LayerTreeHostCommonTest();
  explicit LayerTreeHostCommonTest(const LayerTreeSettings& settings);

 protected:
  static void SetScrollOffsetDelta(LayerImpl* layer_impl,
                                   const gfx::Vector2dF& delta) {
    if (layer_impl->layer_tree_impl()
            ->property_trees()
            ->scroll_tree.SetScrollOffsetDeltaForTesting(layer_impl->id(),
                                                         delta))
      layer_impl->layer_tree_impl()->DidUpdateScrollOffset(
          layer_impl->id(), layer_impl->transform_tree_index());
  }
};

}  // namespace cc

#endif  // CC_TEST_LAYER_TREE_HOST_COMMON_TEST_H_
