// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/layers/render_surface_impl.h"

#include <stddef.h>

#include <algorithm>

#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "cc/base/math_util.h"
#include "cc/debug/debug_colors.h"
#include "cc/layers/layer_impl.h"
#include "cc/layers/render_pass_sink.h"
#include "cc/quads/debug_border_draw_quad.h"
#include "cc/quads/render_pass.h"
#include "cc/quads/render_pass_draw_quad.h"
#include "cc/quads/shared_quad_state.h"
#include "cc/trees/damage_tracker.h"
#include "cc/trees/draw_property_utils.h"
#include "cc/trees/layer_tree_impl.h"
#include "cc/trees/occlusion.h"
#include "third_party/skia/include/core/SkImageFilter.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/transform.h"

namespace cc {

RenderSurfaceImpl::RenderSurfaceImpl(LayerImpl* owning_layer)
    : owning_layer_(owning_layer),
      surface_property_changed_(false),
      contributes_to_drawn_surface_(false),
      nearest_occlusion_immune_ancestor_(nullptr),
      target_render_surface_layer_index_history_(0),
      current_layer_index_history_(0) {
  damage_tracker_ = DamageTracker::Create();
}

RenderSurfaceImpl::~RenderSurfaceImpl() {}

RenderSurfaceImpl* RenderSurfaceImpl::render_target() {
  EffectTree& effect_tree =
      owning_layer_->layer_tree_impl()->property_trees()->effect_tree;
  EffectNode* node = effect_tree.Node(EffectTreeIndex());
  EffectNode* target_node = effect_tree.Node(node->data.target_id);
  if (target_node->id != 0)
    return target_node->data.render_surface;
  else
    return this;
}

const RenderSurfaceImpl* RenderSurfaceImpl::render_target() const {
  const EffectTree& effect_tree =
      owning_layer_->layer_tree_impl()->property_trees()->effect_tree;
  const EffectNode* node = effect_tree.Node(EffectTreeIndex());
  const EffectNode* target_node = effect_tree.Node(node->data.target_id);
  if (target_node->id != 0)
    return target_node->data.render_surface;
  else
    return this;
}

RenderSurfaceImpl::DrawProperties::DrawProperties() {
  draw_opacity = 1.f;
  is_clipped = false;
}

RenderSurfaceImpl::DrawProperties::~DrawProperties() {}

gfx::RectF RenderSurfaceImpl::DrawableContentRect() const {
  gfx::RectF drawable_content_rect =
      MathUtil::MapClippedRect(draw_transform(), gfx::RectF(content_rect()));
  if (owning_layer_->has_replica()) {
    drawable_content_rect.Union(MathUtil::MapClippedRect(
        replica_draw_transform(), gfx::RectF(content_rect())));
  }
  if (!owning_layer_->filters().IsEmpty()) {
    int left, top, right, bottom;
    owning_layer_->filters().GetOutsets(&top, &right, &bottom, &left);
    drawable_content_rect.Inset(-left, -top, -right, -bottom);
  }

  // If the rect has a NaN coordinate, we return empty rect to avoid crashes in
  // functions (for example, gfx::ToEnclosedRect) that are called on this rect.
  if (std::isnan(drawable_content_rect.x()) ||
      std::isnan(drawable_content_rect.y()) ||
      std::isnan(drawable_content_rect.right()) ||
      std::isnan(drawable_content_rect.bottom()))
    return gfx::RectF();

  return drawable_content_rect;
}

SkColor RenderSurfaceImpl::GetDebugBorderColor() const {
  return DebugColors::SurfaceBorderColor();
}

SkColor RenderSurfaceImpl::GetReplicaDebugBorderColor() const {
  return DebugColors::SurfaceReplicaBorderColor();
}

float RenderSurfaceImpl::GetDebugBorderWidth() const {
  return DebugColors::SurfaceBorderWidth(owning_layer_->layer_tree_impl());
}

float RenderSurfaceImpl::GetReplicaDebugBorderWidth() const {
  return DebugColors::SurfaceReplicaBorderWidth(
      owning_layer_->layer_tree_impl());
}

int RenderSurfaceImpl::OwningLayerId() const {
  return owning_layer_ ? owning_layer_->id() : 0;
}

bool RenderSurfaceImpl::HasReplica() const {
  return owning_layer_->has_replica();
}

const LayerImpl* RenderSurfaceImpl::ReplicaLayer() const {
  return owning_layer_->replica_layer();
}

int RenderSurfaceImpl::TransformTreeIndex() const {
  return owning_layer_->transform_tree_index();
}

int RenderSurfaceImpl::ClipTreeIndex() const {
  return owning_layer_->clip_tree_index();
}

int RenderSurfaceImpl::EffectTreeIndex() const {
  return owning_layer_->effect_tree_index();
}

void RenderSurfaceImpl::SetClipRect(const gfx::Rect& clip_rect) {
  if (clip_rect == draw_properties_.clip_rect)
    return;

  surface_property_changed_ = true;
  draw_properties_.clip_rect = clip_rect;
}

void RenderSurfaceImpl::SetContentRect(const gfx::Rect& content_rect) {
  if (content_rect == draw_properties_.content_rect)
    return;

  surface_property_changed_ = true;
  draw_properties_.content_rect = content_rect;
}

void RenderSurfaceImpl::ClearAccumulatedContentRect() {
  accumulated_content_rect_ = gfx::Rect();
}

void RenderSurfaceImpl::AccumulateContentRectFromContributingLayer(
    LayerImpl* layer) {
  DCHECK(layer->DrawsContent());
  DCHECK_EQ(this, layer->render_target());

  // Root render surface doesn't accumulate content rect, it always uses
  // viewport for content rect.
  if (render_target() == this)
    return;

  accumulated_content_rect_.Union(layer->drawable_content_rect());
}

void RenderSurfaceImpl::AccumulateContentRectFromContributingRenderSurface(
    RenderSurfaceImpl* contributing_surface) {
  DCHECK_NE(this, contributing_surface);
  DCHECK_EQ(this, contributing_surface->render_target());

  // Root render surface doesn't accumulate content rect, it always uses
  // viewport for content rect.
  if (render_target() == this)
    return;

  // The content rect of contributing surface is in its own space. Instead, we
  // will use contributing surface's DrawableContentRect which is in target
  // space (local space for this render surface) as required.
  accumulated_content_rect_.Union(
      gfx::ToEnclosedRect(contributing_surface->DrawableContentRect()));
}

bool RenderSurfaceImpl::SurfacePropertyChanged() const {
  // Surface property changes are tracked as follows:
  //
  // - surface_property_changed_ is flagged when the clip_rect or content_rect
  //   change. As of now, these are the only two properties that can be affected
  //   by descendant layers.
  //
  // - all other property changes come from the owning layer (or some ancestor
  //   layer that propagates its change to the owning layer).
  //
  DCHECK(owning_layer_);
  return surface_property_changed_ || owning_layer_->LayerPropertyChanged();
}

bool RenderSurfaceImpl::SurfacePropertyChangedOnlyFromDescendant() const {
  return surface_property_changed_ && !owning_layer_->LayerPropertyChanged();
}

void RenderSurfaceImpl::ClearLayerLists() {
  layer_list_.clear();
}

RenderPassId RenderSurfaceImpl::GetRenderPassId() {
  int layer_id = owning_layer_->id();
  int sub_id = 0;
  DCHECK_GT(layer_id, 0);
  return RenderPassId(layer_id, sub_id);
}

void RenderSurfaceImpl::AppendRenderPasses(RenderPassSink* pass_sink) {
  std::unique_ptr<RenderPass> pass = RenderPass::Create(layer_list_.size());
  pass->SetNew(GetRenderPassId(), content_rect(),
               gfx::IntersectRects(content_rect(),
                                   damage_tracker_->current_damage_rect()),
               draw_properties_.screen_space_transform);
  pass_sink->AppendRenderPass(std::move(pass));
}

void RenderSurfaceImpl::AppendQuads(RenderPass* render_pass,
                                    const gfx::Transform& draw_transform,
                                    const Occlusion& occlusion_in_content_space,
                                    SkColor debug_border_color,
                                    float debug_border_width,
                                    LayerImpl* mask_layer,
                                    AppendQuadsData* append_quads_data,
                                    RenderPassId render_pass_id) {
  gfx::Rect visible_layer_rect =
      occlusion_in_content_space.GetUnoccludedContentRect(content_rect());
  if (visible_layer_rect.IsEmpty())
    return;

  SharedQuadState* shared_quad_state =
      render_pass->CreateAndAppendSharedQuadState();
  shared_quad_state->SetAll(
      draw_transform, content_rect().size(), content_rect(),
      draw_properties_.clip_rect, draw_properties_.is_clipped,
      draw_properties_.draw_opacity, owning_layer_->blend_mode(),
      owning_layer_->sorting_context_id());

  if (owning_layer_->ShowDebugBorders()) {
    DebugBorderDrawQuad* debug_border_quad =
        render_pass->CreateAndAppendDrawQuad<DebugBorderDrawQuad>();
    debug_border_quad->SetNew(shared_quad_state, content_rect(),
                              visible_layer_rect, debug_border_color,
                              debug_border_width);
  }

  ResourceId mask_resource_id = 0;
  gfx::Size mask_texture_size;
  gfx::Vector2dF mask_uv_scale;
  gfx::Transform owning_layer_draw_transform = owning_layer_->DrawTransform();
  if (mask_layer && mask_layer->DrawsContent() &&
      !mask_layer->bounds().IsEmpty()) {
    mask_layer->GetContentsResourceId(&mask_resource_id, &mask_texture_size);
    gfx::Vector2dF owning_layer_draw_scale =
        MathUtil::ComputeTransform2dScaleComponents(owning_layer_draw_transform,
                                                    1.f);
    gfx::SizeF unclipped_mask_target_size = gfx::ScaleSize(
        gfx::SizeF(owning_layer_->bounds()), owning_layer_draw_scale.x(),
        owning_layer_draw_scale.y());
    mask_uv_scale = gfx::Vector2dF(1.0f / unclipped_mask_target_size.width(),
                                   1.0f / unclipped_mask_target_size.height());
  }

  DCHECK(owning_layer_draw_transform.IsScale2d());
  gfx::Vector2dF owning_layer_to_target_scale =
      owning_layer_draw_transform.Scale2d();

  RenderPassDrawQuad* quad =
      render_pass->CreateAndAppendDrawQuad<RenderPassDrawQuad>();
  quad->SetNew(shared_quad_state, content_rect(), visible_layer_rect,
               render_pass_id, mask_resource_id, mask_uv_scale,
               mask_texture_size, owning_layer_->filters(),
               owning_layer_to_target_scale,
               owning_layer_->background_filters());
}

}  // namespace cc
