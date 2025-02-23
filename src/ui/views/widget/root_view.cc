// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/widget/root_view.h"

#include <algorithm>

#include "base/logging.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/ui_base_switches_util.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/views/drag_controller.h"
#include "ui/views/focus/view_storage.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_targeter.h"
#include "ui/views/widget/root_view_targeter.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

typedef ui::EventDispatchDetails DispatchDetails;

namespace views {
namespace internal {

namespace {

enum EventType {
  EVENT_ENTER,
  EVENT_EXIT
};

class MouseEnterExitEvent : public ui::MouseEvent {
 public:
  MouseEnterExitEvent(const ui::MouseEvent& event, ui::EventType type)
      : ui::MouseEvent(event,
                       static_cast<View*>(NULL),
                       static_cast<View*>(NULL)) {
    DCHECK(type == ui::ET_MOUSE_ENTERED ||
           type == ui::ET_MOUSE_EXITED);
    SetType(type);
  }

  ~MouseEnterExitEvent() override {}
};

}  // namespace

// This event handler receives events in the pre-target phase and takes care of
// the following:
//   - Shows keyboard-triggered context menus.
class PreEventDispatchHandler : public ui::EventHandler {
 public:
  explicit PreEventDispatchHandler(View* owner)
      : owner_(owner) {
  }
  ~PreEventDispatchHandler() override {}

 private:
  // ui::EventHandler:
  void OnKeyEvent(ui::KeyEvent* event) override {
    CHECK_EQ(ui::EP_PRETARGET, event->phase());
    if (event->handled())
      return;

    View* v = NULL;
    if (owner_->GetFocusManager())  // Can be NULL in unittests.
      v = owner_->GetFocusManager()->GetFocusedView();

    // Special case to handle keyboard-triggered context menus.
    if (v && v->enabled() && ((event->key_code() == ui::VKEY_APPS) ||
       (event->key_code() == ui::VKEY_F10 && event->IsShiftDown()))) {
      // Clamp the menu location within the visible bounds of each ancestor view
      // to avoid showing the menu over a completely different view or window.
      gfx::Point location = v->GetKeyboardContextMenuLocation();
      for (View* parent = v->parent(); parent; parent = parent->parent()) {
        const gfx::Rect& parent_bounds = parent->GetBoundsInScreen();
        location.SetToMax(parent_bounds.origin());
        location.SetToMin(parent_bounds.bottom_right());
      }
      v->ShowContextMenu(location, ui::MENU_SOURCE_KEYBOARD);
      event->StopPropagation();
    }
  }

  View* owner_;

  DISALLOW_COPY_AND_ASSIGN(PreEventDispatchHandler);
};

// This event handler receives events in the post-target phase and takes care of
// the following:
//   - Generates context menu, or initiates drag-and-drop, from gesture events.
class PostEventDispatchHandler : public ui::EventHandler {
 public:
  PostEventDispatchHandler()
      : touch_dnd_enabled_(::switches::IsTouchDragDropEnabled()) {
  }
  ~PostEventDispatchHandler() override {}

 private:
  // Overridden from ui::EventHandler:
  void OnGestureEvent(ui::GestureEvent* event) override {
    DCHECK_EQ(ui::EP_POSTTARGET, event->phase());
    if (event->handled())
      return;

    View* target = static_cast<View*>(event->target());
    gfx::Point location = event->location();
    if (touch_dnd_enabled_ &&
        event->type() == ui::ET_GESTURE_LONG_PRESS &&
        (!target->drag_controller() ||
         target->drag_controller()->CanStartDragForView(
             target, location, location))) {
      if (target->DoDrag(*event, location,
          ui::DragDropTypes::DRAG_EVENT_SOURCE_TOUCH)) {
        event->StopPropagation();
        return;
      }
    }

    if (target->context_menu_controller() &&
        (event->type() == ui::ET_GESTURE_LONG_PRESS ||
         event->type() == ui::ET_GESTURE_LONG_TAP ||
         event->type() == ui::ET_GESTURE_TWO_FINGER_TAP)) {
      gfx::Point screen_location(location);
      View::ConvertPointToScreen(target, &screen_location);
      target->ShowContextMenu(screen_location, ui::MENU_SOURCE_TOUCH);
      event->StopPropagation();
    }
  }

  bool touch_dnd_enabled_;

  DISALLOW_COPY_AND_ASSIGN(PostEventDispatchHandler);
};

// static
const char RootView::kViewClassName[] = "RootView";

////////////////////////////////////////////////////////////////////////////////
// RootView, public:

// Creation and lifetime -------------------------------------------------------

RootView::RootView(Widget* widget)
    : widget_(widget),
      mouse_pressed_handler_(NULL),
      mouse_move_handler_(NULL),
      last_click_handler_(NULL),
      explicit_mouse_handler_(false),
      clear_mouse_handler_on_release_(true),
      last_mouse_event_flags_(0),
      last_mouse_event_x_(-1),
      last_mouse_event_y_(-1),
      gesture_handler_(NULL),
      gesture_handler_set_before_processing_(false),
      pre_dispatch_handler_(new internal::PreEventDispatchHandler(this)),
      post_dispatch_handler_(new internal::PostEventDispatchHandler),
      focus_search_(this, false, false),
      focus_traversable_parent_(NULL),
      focus_traversable_parent_view_(NULL),
      event_dispatch_target_(NULL),
      old_dispatch_target_(NULL) {
  AddPreTargetHandler(pre_dispatch_handler_.get());
  AddPostTargetHandler(post_dispatch_handler_.get());
  SetEventTargeter(scoped_ptr<ViewTargeter>(new RootViewTargeter(this, this)));
}

RootView::~RootView() {
  // If we have children remove them explicitly so to make sure a remove
  // notification is sent for each one of them.
  if (has_children())
    RemoveAllChildViews(true);
}

// Tree operations -------------------------------------------------------------

void RootView::SetContentsView(View* contents_view) {
  DCHECK(contents_view && GetWidget()->native_widget()) <<
      "Can't be called until after the native widget is created!";
  // The ContentsView must be set up _after_ the window is created so that its
  // Widget pointer is valid.
  SetLayoutManager(new FillLayout);
  if (has_children())
    RemoveAllChildViews(true);
  AddChildView(contents_view);

  // Force a layout now, since the attached hierarchy won't be ready for the
  // containing window's bounds. Note that we call Layout directly rather than
  // calling the widget's size changed handler, since the RootView's bounds may
  // not have changed, which will cause the Layout not to be done otherwise.
  Layout();
}

View* RootView::GetContentsView() {
  return child_count() > 0 ? child_at(0) : NULL;
}

void RootView::NotifyNativeViewHierarchyChanged() {
  PropagateNativeViewHierarchyChanged();
}

// Focus -----------------------------------------------------------------------

void RootView::SetFocusTraversableParent(FocusTraversable* focus_traversable) {
  DCHECK(focus_traversable != this);
  focus_traversable_parent_ = focus_traversable;
}

void RootView::SetFocusTraversableParentView(View* view) {
  focus_traversable_parent_view_ = view;
}

// System events ---------------------------------------------------------------

void RootView::ThemeChanged() {
  View::PropagateThemeChanged();
}

void RootView::LocaleChanged() {
  View::PropagateLocaleChanged();
}

void RootView::DeviceScaleFactorChanged(float device_scale_factor) {
  View::PropagateDeviceScaleFactorChanged(device_scale_factor);
}

////////////////////////////////////////////////////////////////////////////////
// RootView, FocusTraversable implementation:

FocusSearch* RootView::GetFocusSearch() {
  return &focus_search_;
}

FocusTraversable* RootView::GetFocusTraversableParent() {
  return focus_traversable_parent_;
}

View* RootView::GetFocusTraversableParentView() {
  return focus_traversable_parent_view_;
}

////////////////////////////////////////////////////////////////////////////////
// RootView, ui::EventProcessor overrides:

ui::EventTarget* RootView::GetRootTarget() {
  return this;
}

void RootView::OnEventProcessingStarted(ui::Event* event) {
  if (!event->IsGestureEvent())
    return;

  ui::GestureEvent* gesture_event = event->AsGestureEvent();

  // Do not process ui::ET_GESTURE_BEGIN events.
  if (gesture_event->type() == ui::ET_GESTURE_BEGIN) {
    event->SetHandled();
    return;
  }

  // Do not process ui::ET_GESTURE_END events if they do not correspond to the
  // removal of the final touch point or if no gesture handler has already
  // been set.
  if (gesture_event->type() == ui::ET_GESTURE_END &&
      (gesture_event->details().touch_points() > 1 ||
       !gesture_handler_)) {
    event->SetHandled();
    return;
  }

  // Do not process subsequent gesture scroll events if no handler was set for
  // a ui::ET_GESTURE_SCROLL_BEGIN event.
  if (!gesture_handler_ &&
      (gesture_event->type() == ui::ET_GESTURE_SCROLL_UPDATE ||
       gesture_event->type() == ui::ET_GESTURE_SCROLL_END ||
       gesture_event->type() == ui::ET_SCROLL_FLING_START)) {
    event->SetHandled();
    return;
  }

  gesture_handler_set_before_processing_ = !!gesture_handler_;
}

void RootView::OnEventProcessingFinished(ui::Event* event) {
  // If |event| was not handled and |gesture_handler_| was not set by the
  // dispatch of a previous gesture event, then no default gesture handler
  // should be set prior to the next gesture event being received.
  if (event->IsGestureEvent() &&
      !event->handled() &&
      !gesture_handler_set_before_processing_) {
    gesture_handler_ = NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////
// RootView, View overrides:

const Widget* RootView::GetWidget() const {
  return widget_;
}

Widget* RootView::GetWidget() {
  return const_cast<Widget*>(const_cast<const RootView*>(this)->GetWidget());
}

bool RootView::IsDrawn() const {
  return visible();
}

void RootView::Layout() {
  View::Layout();
  widget_->OnRootViewLayout();
}

const char* RootView::GetClassName() const {
  return kViewClassName;
}

void RootView::SchedulePaintInRect(const gfx::Rect& rect) {
  if (layer()) {
    layer()->SchedulePaint(rect);
  } else {
    gfx::Rect xrect = ConvertRectToParent(rect);
    gfx::Rect invalid_rect = gfx::IntersectRects(GetLocalBounds(), xrect);
    if (!invalid_rect.IsEmpty())
      widget_->SchedulePaintInRect(invalid_rect);
  }
}

bool RootView::OnMousePressed(const ui::MouseEvent& event) {
  UpdateCursor(event);
  SetMouseLocationAndFlags(event);

  // If mouse_pressed_handler_ is non null, we are currently processing
  // a pressed -> drag -> released session. In that case we send the
  // event to mouse_pressed_handler_
  if (mouse_pressed_handler_) {
    ui::MouseEvent mouse_pressed_event(event, static_cast<View*>(this),
                                       mouse_pressed_handler_);
    drag_info_.Reset();
    ui::EventDispatchDetails dispatch_details =
        DispatchEvent(mouse_pressed_handler_, &mouse_pressed_event);
    if (dispatch_details.dispatcher_destroyed)
      return true;
    return true;
  }
  DCHECK(!explicit_mouse_handler_);

  bool hit_disabled_view = false;
  // Walk up the tree until we find a view that wants the mouse event.
  for (mouse_pressed_handler_ = GetEventHandlerForPoint(event.location());
       mouse_pressed_handler_ && (mouse_pressed_handler_ != this);
       mouse_pressed_handler_ = mouse_pressed_handler_->parent()) {
    DVLOG(1) << "OnMousePressed testing "
        << mouse_pressed_handler_->GetClassName();
    if (!mouse_pressed_handler_->enabled()) {
      // Disabled views should eat events instead of propagating them upwards.
      hit_disabled_view = true;
      break;
    }

    // See if this view wants to handle the mouse press.
    ui::MouseEvent mouse_pressed_event(event, static_cast<View*>(this),
                                       mouse_pressed_handler_);

    // Remove the double-click flag if the handler is different than the
    // one which got the first click part of the double-click.
    if (mouse_pressed_handler_ != last_click_handler_)
      mouse_pressed_event.set_flags(event.flags() & ~ui::EF_IS_DOUBLE_CLICK);

    drag_info_.Reset();
    ui::EventDispatchDetails dispatch_details =
        DispatchEvent(mouse_pressed_handler_, &mouse_pressed_event);
    if (dispatch_details.dispatcher_destroyed)
      return mouse_pressed_event.handled();

    // The view could have removed itself from the tree when handling
    // OnMousePressed().  In this case, the removal notification will have
    // reset mouse_pressed_handler_ to NULL out from under us.  Detect this
    // case and stop.  (See comments in view.h.)
    //
    // NOTE: Don't return true here, because we don't want the frame to
    // forward future events to us when there's no handler.
    if (!mouse_pressed_handler_)
      break;

    // If the view handled the event, leave mouse_pressed_handler_ set and
    // return true, which will cause subsequent drag/release events to get
    // forwarded to that view.
    if (mouse_pressed_event.handled()) {
      last_click_handler_ = mouse_pressed_handler_;
      DVLOG(1) << "OnMousePressed handled by "
          << mouse_pressed_handler_->GetClassName();
      return true;
    }
  }

  // Reset mouse_pressed_handler_ to indicate that no processing is occurring.
  mouse_pressed_handler_ = NULL;

  // In the event that a double-click is not handled after traversing the
  // entire hierarchy (even as a single-click when sent to a different view),
  // it must be marked as handled to avoid anything happening from default
  // processing if it the first click-part was handled by us.
  if (last_click_handler_ && (event.flags() & ui::EF_IS_DOUBLE_CLICK))
    hit_disabled_view = true;

  last_click_handler_ = NULL;
  return hit_disabled_view;
}

bool RootView::OnMouseDragged(const ui::MouseEvent& event) {
  if (mouse_pressed_handler_) {
    SetMouseLocationAndFlags(event);

    ui::MouseEvent mouse_event(event, static_cast<View*>(this),
                               mouse_pressed_handler_);
    ui::EventDispatchDetails dispatch_details =
        DispatchEvent(mouse_pressed_handler_, &mouse_event);
    if (dispatch_details.dispatcher_destroyed)
      return false;
  }
  return false;
}

void RootView::OnMouseReleased(const ui::MouseEvent& event) {
  UpdateCursor(event);

  if (mouse_pressed_handler_) {
    ui::MouseEvent mouse_released(event, static_cast<View*>(this),
                                  mouse_pressed_handler_);
    // We allow the view to delete us from the event dispatch callback. As such,
    // configure state such that we're done first, then call View.
    View* mouse_pressed_handler = mouse_pressed_handler_;
    if (clear_mouse_handler_on_release_)
      SetMouseHandler(nullptr);
    ui::EventDispatchDetails dispatch_details =
        DispatchEvent(mouse_pressed_handler, &mouse_released);
    if (dispatch_details.dispatcher_destroyed)
      return;
  }
}

void RootView::OnMouseCaptureLost() {
  // TODO: this likely needs to reset touch handler too.

  if (mouse_pressed_handler_ || gesture_handler_) {
    // Synthesize a release event for UpdateCursor.
    if (mouse_pressed_handler_) {
      gfx::Point last_point(last_mouse_event_x_, last_mouse_event_y_);
      ui::MouseEvent release_event(ui::ET_MOUSE_RELEASED, last_point,
                                   last_point, ui::EventTimeForNow(),
                                   last_mouse_event_flags_, 0);
      UpdateCursor(release_event);
    }
    // We allow the view to delete us from OnMouseCaptureLost. As such,
    // configure state such that we're done first, then call View.
    View* mouse_pressed_handler = mouse_pressed_handler_;
    View* gesture_handler = gesture_handler_;
    SetMouseHandler(NULL);
    if (mouse_pressed_handler)
      mouse_pressed_handler->OnMouseCaptureLost();
    else
      gesture_handler->OnMouseCaptureLost();
    // WARNING: we may have been deleted.
  }
}

void RootView::OnMouseMoved(const ui::MouseEvent& event) {
  View* v = GetEventHandlerForPoint(event.location());
  // Find the first enabled view, or the existing move handler, whichever comes
  // first.  The check for the existing handler is because if a view becomes
  // disabled while handling moves, it's wrong to suddenly send ET_MOUSE_EXITED
  // and ET_MOUSE_ENTERED events, because the mouse hasn't actually exited yet.
  while (v && !v->enabled() && (v != mouse_move_handler_))
    v = v->parent();
  if (v && v != this) {
    if (v != mouse_move_handler_) {
      if (mouse_move_handler_ != NULL &&
          (!mouse_move_handler_->notify_enter_exit_on_child() ||
           !mouse_move_handler_->Contains(v))) {
        MouseEnterExitEvent exit(event, ui::ET_MOUSE_EXITED);
        exit.ConvertLocationToTarget(static_cast<View*>(this),
                                     mouse_move_handler_);
        ui::EventDispatchDetails dispatch_details =
            DispatchEvent(mouse_move_handler_, &exit);
        if (dispatch_details.dispatcher_destroyed)
          return;
        // The mouse_move_handler_ could have been destroyed in the context of
        // the mouse exit event.
        if (!dispatch_details.target_destroyed) {
          CHECK(mouse_move_handler_);
          dispatch_details = NotifyEnterExitOfDescendant(
              event, ui::ET_MOUSE_EXITED, mouse_move_handler_, v);
          if (dispatch_details.dispatcher_destroyed)
            return;
        }
      }
      View* old_handler = mouse_move_handler_;
      mouse_move_handler_ = v;
      if (!mouse_move_handler_->notify_enter_exit_on_child() ||
          !mouse_move_handler_->Contains(old_handler)) {
        MouseEnterExitEvent entered(event, ui::ET_MOUSE_ENTERED);
        entered.ConvertLocationToTarget(static_cast<View*>(this),
                                        mouse_move_handler_);
        ui::EventDispatchDetails dispatch_details =
            DispatchEvent(mouse_move_handler_, &entered);
        if (dispatch_details.dispatcher_destroyed ||
            dispatch_details.target_destroyed) {
          return;
        }
        CHECK(mouse_move_handler_);
        dispatch_details = NotifyEnterExitOfDescendant(
            event, ui::ET_MOUSE_ENTERED, mouse_move_handler_, old_handler);
        if (dispatch_details.dispatcher_destroyed ||
            dispatch_details.target_destroyed) {
          return;
        }
      }
    }
    ui::MouseEvent moved_event(event, static_cast<View*>(this),
                               mouse_move_handler_);
    mouse_move_handler_->OnMouseMoved(moved_event);
    // TODO(tdanderson): It may be possible to avoid setting the cursor twice
    //                   (once here and once from CompoundEventFilter) on a
    //                   mousemove. See crbug.com/351469.
    if (!(moved_event.flags() & ui::EF_IS_NON_CLIENT))
      widget_->SetCursor(mouse_move_handler_->GetCursor(moved_event));
  } else if (mouse_move_handler_ != NULL) {
    MouseEnterExitEvent exited(event, ui::ET_MOUSE_EXITED);
    ui::EventDispatchDetails dispatch_details =
        DispatchEvent(mouse_move_handler_, &exited);
    if (dispatch_details.dispatcher_destroyed)
      return;
    // The mouse_move_handler_ could have been destroyed in the context of the
    // mouse exit event.
    if (!dispatch_details.target_destroyed) {
      CHECK(mouse_move_handler_);
      dispatch_details = NotifyEnterExitOfDescendant(event, ui::ET_MOUSE_EXITED,
                                                     mouse_move_handler_, v);
      if (dispatch_details.dispatcher_destroyed)
        return;
    }
    // On Aura the non-client area extends slightly outside the root view for
    // some windows.  Let the non-client cursor handling code set the cursor
    // as we do above.
    if (!(event.flags() & ui::EF_IS_NON_CLIENT))
      widget_->SetCursor(gfx::kNullCursor);
    mouse_move_handler_ = NULL;
  }
}

void RootView::OnMouseExited(const ui::MouseEvent& event) {
  if (mouse_move_handler_ != NULL) {
    MouseEnterExitEvent exited(event, ui::ET_MOUSE_EXITED);
    ui::EventDispatchDetails dispatch_details =
        DispatchEvent(mouse_move_handler_, &exited);
    if (dispatch_details.dispatcher_destroyed)
      return;
    // The mouse_move_handler_ could have been destroyed in the context of the
    // mouse exit event.
    if (!dispatch_details.target_destroyed) {
      CHECK(mouse_move_handler_);
      dispatch_details = NotifyEnterExitOfDescendant(event, ui::ET_MOUSE_EXITED,
                                                     mouse_move_handler_, NULL);
      if (dispatch_details.dispatcher_destroyed)
        return;
    }
    mouse_move_handler_ = NULL;
  }
}

bool RootView::OnMouseWheel(const ui::MouseWheelEvent& event) {
  for (View* v = GetEventHandlerForPoint(event.location());
       v && v != this && !event.handled(); v = v->parent()) {
    ui::EventDispatchDetails dispatch_details =
        DispatchEvent(v, const_cast<ui::MouseWheelEvent*>(&event));
    if (dispatch_details.dispatcher_destroyed ||
        dispatch_details.target_destroyed) {
      return event.handled();
    }
  }
  return event.handled();
}

void RootView::SetMouseHandler(View* new_mh) {
  SetMouseHandler(new_mh, true /* clear_on_release */);
}

void RootView::GetAccessibleState(ui::AXViewState* state) {
  state->name = widget_->widget_delegate()->GetAccessibleWindowTitle();
  state->role = widget_->widget_delegate()->GetAccessibleWindowRole();
}

void RootView::UpdateParentLayer() {
  if (layer())
    ReparentLayer(gfx::Vector2d(GetMirroredX(), y()), widget_->GetLayer());
}

////////////////////////////////////////////////////////////////////////////////
// RootView, protected:

void RootView::ViewHierarchyChanged(
    const ViewHierarchyChangedDetails& details) {
  widget_->ViewHierarchyChanged(details);

  if (!details.is_add) {
    if (!explicit_mouse_handler_ && mouse_pressed_handler_ == details.child)
      mouse_pressed_handler_ = NULL;
    if (mouse_move_handler_ == details.child)
      mouse_move_handler_ = NULL;
    if (gesture_handler_ == details.child)
      gesture_handler_ = NULL;
    if (event_dispatch_target_ == details.child)
      event_dispatch_target_ = NULL;
    if (old_dispatch_target_ == details.child)
      old_dispatch_target_ = NULL;
  }
}

void RootView::VisibilityChanged(View* /*starting_from*/, bool is_visible) {
  if (!is_visible) {
    // When the root view is being hidden (e.g. when widget is minimized)
    // handlers are reset, so that after it is reshown, events are not captured
    // by old handlers.
    explicit_mouse_handler_ = false;
    clear_mouse_handler_on_release_ = true;
    mouse_pressed_handler_ = NULL;
    mouse_move_handler_ = NULL;
    gesture_handler_ = NULL;
    event_dispatch_target_ = NULL;
    old_dispatch_target_ = NULL;
  }
}

void RootView::OnPaint(gfx::Canvas* canvas) {
  if (!layer() || !layer()->fills_bounds_opaquely())
    canvas->DrawColor(SK_ColorBLACK, SkXfermode::kClear_Mode);

  View::OnPaint(canvas);
}

gfx::Vector2d RootView::CalculateOffsetToAncestorWithLayer(
    ui::Layer** layer_parent) {
  gfx::Vector2d offset(View::CalculateOffsetToAncestorWithLayer(layer_parent));
  if (!layer() && layer_parent)
    *layer_parent = widget_->GetLayer();
  return offset;
}

View::DragInfo* RootView::GetDragInfo() {
  return &drag_info_;
}

////////////////////////////////////////////////////////////////////////////////
// RootView, private:

// Input -----------------------------------------------------------------------

void RootView::UpdateCursor(const ui::MouseEvent& event) {
  if (!(event.flags() & ui::EF_IS_NON_CLIENT)) {
    View* v = GetEventHandlerForPoint(event.location());
    ui::MouseEvent me(event, static_cast<View*>(this), v);
    widget_->SetCursor(v->GetCursor(me));
  }
}

void RootView::SetMouseLocationAndFlags(const ui::MouseEvent& event) {
  last_mouse_event_flags_ = event.flags();
  last_mouse_event_x_ = event.x();
  last_mouse_event_y_ = event.y();
}

ui::EventDispatchDetails RootView::NotifyEnterExitOfDescendant(
    const ui::MouseEvent& event,
    ui::EventType type,
    View* view,
    View* sibling) {
  for (View* p = view->parent(); p; p = p->parent()) {
    if (!p->notify_enter_exit_on_child())
      continue;
    if (sibling && p->Contains(sibling))
      break;
    // It is necessary to recreate the notify-event for each dispatch, since one
    // of the callbacks can mark the event as handled, and that would cause
    // incorrect event dispatch.
    MouseEnterExitEvent notify_event(event, type);
    ui::EventDispatchDetails dispatch_details = DispatchEvent(p, &notify_event);
    if (dispatch_details.dispatcher_destroyed ||
        dispatch_details.target_destroyed) {
      return dispatch_details;
    }
  }
  return ui::EventDispatchDetails();
}

void RootView::SetMouseHandler(View* new_mh, bool clear_on_release) {
  // If we're clearing the mouse handler, clear explicit_mouse_handler_ as well.
  explicit_mouse_handler_ = (new_mh != nullptr);
  clear_mouse_handler_on_release_ = clear_on_release;
  mouse_pressed_handler_ = new_mh;
  gesture_handler_ = new_mh;
  drag_info_.Reset();
}

bool RootView::CanDispatchToTarget(ui::EventTarget* target) {
  return event_dispatch_target_ == target;
}

ui::EventDispatchDetails RootView::PreDispatchEvent(ui::EventTarget* target,
                                                    ui::Event* event) {
  View* view = static_cast<View*>(target);
  if (event->IsGestureEvent()) {
    // Update |gesture_handler_| to indicate which View is currently handling
    // gesture events.
    // TODO(tdanderson): Look into moving this to PostDispatchEvent() and
    //                   using |event_dispatch_target_| instead of
    //                   |gesture_handler_| to detect if the view has been
    //                   removed from the tree.
    gesture_handler_ = view;

    // Disabled views are permitted to be targets of gesture events, but
    // gesture events should never actually be dispatched to them. Prevent
    // dispatch by marking the event as handled.
    if (!view->enabled())
      event->SetHandled();
  }

  old_dispatch_target_ = event_dispatch_target_;
  event_dispatch_target_ = view;
  return DispatchDetails();
}

ui::EventDispatchDetails RootView::PostDispatchEvent(ui::EventTarget* target,
                                                     const ui::Event& event) {
  // The GESTURE_END event corresponding to the removal of the final touch
  // point marks the end of a gesture sequence, so reset |gesture_handler_|
  // to NULL.
  if (event.type() == ui::ET_GESTURE_END) {
    // In case a drag was in progress, reset all the handlers. Otherwise, just
    // reset the gesture handler.
    if (gesture_handler_ && gesture_handler_ == mouse_pressed_handler_)
      SetMouseHandler(NULL);
    else
      gesture_handler_ = NULL;
  }

  DispatchDetails details;
  if (target != event_dispatch_target_)
    details.target_destroyed = true;

  event_dispatch_target_ = old_dispatch_target_;
  old_dispatch_target_ = NULL;

#ifndef NDEBUG
  DCHECK(!event_dispatch_target_ || Contains(event_dispatch_target_));
#endif

  return details;
}

}  // namespace internal
}  // namespace views
