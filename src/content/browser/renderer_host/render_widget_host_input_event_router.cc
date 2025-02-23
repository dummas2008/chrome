// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_widget_host_input_event_router.h"

#include "cc/quads/surface_draw_quad.h"
#include "cc/surfaces/surface_id_allocator.h"
#include "cc/surfaces/surface_manager.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/common/frame_messages.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"

namespace {

void TransformEventTouchPositions(blink::WebTouchEvent* event,
                                  const gfx::Vector2d& delta) {
  for (unsigned i = 0; i < event->touchesLength; ++i) {
     event->touches[i].position.x += delta.x();
     event->touches[i].position.y += delta.y();
  }
}

}  // anonymous namespace

namespace content {

void RenderWidgetHostInputEventRouter::OnRenderWidgetHostViewBaseDestroyed(
    RenderWidgetHostViewBase* view) {
  view->RemoveObserver(this);

  // Remove this view from the owner_map.
  for (auto entry : owner_map_) {
    if (entry.second == view) {
      owner_map_.erase(entry.first);
      // There will only be one instance of a particular view in the map.
      break;
    }
  }

  if (view == touch_target_) {
    touch_target_ = nullptr;
    touch_delta_ = gfx::Vector2d();
    active_touches_ = 0;
  }

  // If the target that's being destroyed is in the gesture target queue, we
  // replace it with nullptr so that we maintain the 1:1 correspondence between
  // queue entries and the touch sequences that underly them.
  for (size_t i = 0; i < gesture_target_queue_.size(); ++i) {
    if (gesture_target_queue_[i].target == view)
      gesture_target_queue_[i].target = nullptr;
  }

  if (view == gesture_target_) {
    gesture_target_ = nullptr;
    gesture_delta_ = gfx::Vector2d();
  }
}

void RenderWidgetHostInputEventRouter::ClearAllObserverRegistrations() {
  for (auto entry : owner_map_)
    entry.second->RemoveObserver(this);
  owner_map_.clear();
}

RenderWidgetHostInputEventRouter::HittestDelegate::HittestDelegate(
    const std::unordered_map<cc::SurfaceId, HittestData, cc::SurfaceIdHash>&
        hittest_data)
    : hittest_data_(hittest_data) {}

bool RenderWidgetHostInputEventRouter::HittestDelegate::RejectHitTarget(
    const cc::SurfaceDrawQuad* surface_quad,
    const gfx::Point& point_in_quad_space) {
  auto it = hittest_data_.find(surface_quad->surface_id);
  if (it != hittest_data_.end() && it->second.ignored_for_hittest)
    return true;
  return false;
}

bool RenderWidgetHostInputEventRouter::HittestDelegate::AcceptHitTarget(
    const cc::SurfaceDrawQuad* surface_quad,
    const gfx::Point& point_in_quad_space) {
  auto it = hittest_data_.find(surface_quad->surface_id);
  if (it != hittest_data_.end() && !it->second.ignored_for_hittest)
    return true;
  return false;
}

RenderWidgetHostInputEventRouter::RenderWidgetHostInputEventRouter()
    : touch_target_(nullptr),
      gesture_target_(nullptr),
      active_touches_(0) {}

RenderWidgetHostInputEventRouter::~RenderWidgetHostInputEventRouter() {
  // We may be destroyed before some of the owners in the map, so we must
  // remove ourself from their observer lists.
  ClearAllObserverRegistrations();
}

RenderWidgetHostViewBase* RenderWidgetHostInputEventRouter::FindEventTarget(
    RenderWidgetHostViewBase* root_view,
    const gfx::Point& point,
    gfx::Point* transformed_point) {
  // Short circuit if owner_map has only one RenderWidgetHostView, no need for
  // hit testing.
  if (owner_map_.size() <= 1) {
    *transformed_point = point;
    return root_view;
  }

  // The hittest delegate is used to reject hittesting quads based on extra
  // hittesting data send by the renderer.
  HittestDelegate delegate(hittest_data_);

  // The conversion of point to transform_point is done over the course of the
  // hit testing, and reflect transformations that would normally be applied in
  // the renderer process if the event was being routed between frames within a
  // single process with only one RenderWidgetHost.
  uint32_t surface_id_namespace =
      root_view->SurfaceIdNamespaceAtPoint(&delegate, point, transformed_point);
  const SurfaceIdNamespaceOwnerMap::iterator iter =
      owner_map_.find(surface_id_namespace);
  // If the point hit a Surface whose namspace is no longer in the map, then
  // it likely means the RenderWidgetHostView has been destroyed but its
  // parent frame has not sent a new compositor frame since that happened.
  if (iter == owner_map_.end())
    return root_view;

  return iter->second;
}

void RenderWidgetHostInputEventRouter::RouteMouseEvent(
    RenderWidgetHostViewBase* root_view,
    blink::WebMouseEvent* event) {
  gfx::Point transformed_point;
  RenderWidgetHostViewBase* target = FindEventTarget(
      root_view, gfx::Point(event->x, event->y), &transformed_point);
  if (!target)
    return;

  event->x = transformed_point.x();
  event->y = transformed_point.y();

  target->ProcessMouseEvent(*event);
}

void RenderWidgetHostInputEventRouter::RouteMouseWheelEvent(
    RenderWidgetHostViewBase* root_view,
    blink::WebMouseWheelEvent* event) {
  gfx::Point transformed_point;
  RenderWidgetHostViewBase* target = FindEventTarget(
      root_view, gfx::Point(event->x, event->y), &transformed_point);
  if (!target)
    return;

  event->x = transformed_point.x();
  event->y = transformed_point.y();

  target->ProcessMouseWheelEvent(*event);
}

void RenderWidgetHostInputEventRouter::RouteGestureEvent(
    RenderWidgetHostViewBase* root_view,
    blink::WebGestureEvent* event,
    const ui::LatencyInfo& latency) {
  // We use GestureTapDown to detect the start of a gesture sequence since there
  // is no WebGestureEvent equivalent for ET_GESTURE_BEGIN. Note that this
  // means the GestureFlingCancel that always comes between ET_GESTURE_BEGIN and
  // GestureTapDown is sent to the previous target, in case it is still in a
  // fling.
  if (event->type == blink::WebInputEvent::GestureTapDown) {
    if (gesture_target_queue_.empty()) {
      LOG(ERROR) << "Gesture sequence start detected with no target available.";
      // Ignore this gesture sequence as no target is available.
      // TODO(wjmaclean): this only happens on Windows, and should not happen.
      // https://crbug.com/595422
      gesture_target_ = nullptr;
      return;
    }

    const GestureTargetData& data = gesture_target_queue_.front();
    gesture_target_ = data.target;
    gesture_delta_ = data.delta;
    gesture_target_queue_.pop_front();
  }

  if (!gesture_target_)
    return;

  event->x += gesture_delta_.x();
  event->y += gesture_delta_.y();
  gesture_target_->ProcessGestureEvent(*event, latency);
}

void RenderWidgetHostInputEventRouter::RouteTouchEvent(
    RenderWidgetHostViewBase* root_view,
    blink::WebTouchEvent* event,
    const ui::LatencyInfo& latency) {
  switch (event->type) {
    case blink::WebInputEvent::TouchStart: {
      if (!active_touches_) {
        // Since this is the first touch, it defines the target for the rest
        // of this sequence.
        DCHECK(!touch_target_);
        gfx::Point transformed_point;
        gfx::Point original_point(event->touches[0].position.x,
                                  event->touches[0].position.y);
        touch_target_ =
            FindEventTarget(root_view, original_point, &transformed_point);

        // TODO(wjmaclean): Instead of just computing a delta, we should extract
        // the complete transform. We assume it doesn't change for the duration
        // of the touch sequence, though this could be wrong; a better approach
        // might be to always transform each point to the touch_target_
        // for the duration of the sequence.
        touch_delta_ = transformed_point - original_point;
        gesture_target_queue_.emplace_back(touch_target_, touch_delta_);

        if (!touch_target_)
          return;
      }
      ++active_touches_;
      if (touch_target_) {
        TransformEventTouchPositions(event, touch_delta_);
        touch_target_->ProcessTouchEvent(*event, latency);
      }
      break;
    }
    case blink::WebInputEvent::TouchMove:
      if (touch_target_) {
        TransformEventTouchPositions(event, touch_delta_);
        touch_target_->ProcessTouchEvent(*event, latency);
      }
      break;
    case blink::WebInputEvent::TouchEnd:
    case blink::WebInputEvent::TouchCancel:
      if (!touch_target_)
        break;

      DCHECK(active_touches_);
      TransformEventTouchPositions(event, touch_delta_);
      touch_target_->ProcessTouchEvent(*event, latency);
      --active_touches_;
      if (!active_touches_) {
        touch_target_ = nullptr;
        touch_delta_ = gfx::Vector2d();
      }
      break;
    default:
      NOTREACHED();
  }
}

void RenderWidgetHostInputEventRouter::AddSurfaceIdNamespaceOwner(
    uint32_t id,
    RenderWidgetHostViewBase* owner) {
  DCHECK(owner_map_.find(id) == owner_map_.end());
  // We want to be notified if the owner is destroyed so we can remove it from
  // our map.
  owner->AddObserver(this);
  owner_map_.insert(std::make_pair(id, owner));
}

void RenderWidgetHostInputEventRouter::RemoveSurfaceIdNamespaceOwner(
    uint32_t id) {
  auto it_to_remove = owner_map_.find(id);
  if (it_to_remove != owner_map_.end()) {
    it_to_remove->second->RemoveObserver(this);
    owner_map_.erase(it_to_remove);
  }

  for (auto it = hittest_data_.begin(); it != hittest_data_.end();) {
    if (cc::SurfaceIdAllocator::NamespaceForId(it->first) == id)
      it = hittest_data_.erase(it);
    else
      ++it;
  }
}

void RenderWidgetHostInputEventRouter::OnHittestData(
    const FrameHostMsg_HittestData_Params& params) {
  if (owner_map_.find(cc::SurfaceIdAllocator::NamespaceForId(
          params.surface_id)) == owner_map_.end()) {
    return;
  }
  HittestData data;
  data.ignored_for_hittest = params.ignored_for_hittest;
  hittest_data_[params.surface_id] = data;
}

}  // namespace content
