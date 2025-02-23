// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/button/custom_button.h"

#include "ui/accessibility/ax_view_state.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/throb_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/screen.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/animation/ink_drop_delegate.h"
#include "ui/views/animation/ink_drop_hover.h"
#include "ui/views/controls/button/blue_button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/widget/widget.h"

#if defined(USE_AURA)
#include "ui/aura/client/capture_client.h"
#include "ui/aura/window.h"
#endif

namespace views {

namespace {

// How long the hover animation takes if uninterrupted.
const int kHoverFadeDurationMs = 150;

// The amount to enlarge the focus border in all directions relative to the
// button.
const int kFocusBorderOutset = -2;

// The corner radius of the focus border roundrect.
const int kFocusBorderCornerRadius = 3;

class MdFocusRing : public views::View {
 public:
  MdFocusRing() {
    SetPaintToLayer(true);
    layer()->SetFillsBoundsOpaquely(false);
  }
  ~MdFocusRing() override {}

  void OnPaint(gfx::Canvas* canvas) override {
    CustomButton::PaintMdFocusRing(canvas, this);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MdFocusRing);
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// CustomButton, public:

// static
const char CustomButton::kViewClassName[] = "CustomButton";

// static
const CustomButton* CustomButton::AsCustomButton(const views::View* view) {
  return AsCustomButton(const_cast<View*>(view));
}

// static
CustomButton* CustomButton::AsCustomButton(views::View* view) {
  if (view) {
    const char* classname = view->GetClassName();
    if (!strcmp(classname, Checkbox::kViewClassName) ||
        !strcmp(classname, CustomButton::kViewClassName) ||
        !strcmp(classname, ImageButton::kViewClassName) ||
        !strcmp(classname, LabelButton::kViewClassName) ||
        !strcmp(classname, RadioButton::kViewClassName) ||
        !strcmp(classname, MenuButton::kViewClassName)) {
      return static_cast<CustomButton*>(view);
    }
  }
  return NULL;
}

// static
void CustomButton::PaintMdFocusRing(gfx::Canvas* canvas, views::View* view) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(view->GetNativeTheme()->GetSystemColor(
      ui::NativeTheme::kColorId_CallToActionColor));
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(1);
  gfx::RectF rect(view->GetLocalBounds());
  rect.Inset(gfx::InsetsF(0.5));
  canvas->DrawRoundRect(rect, kFocusBorderCornerRadius, paint);
}

CustomButton::~CustomButton() {}

void CustomButton::SetState(ButtonState state) {
  if (state == state_)
    return;

  if (animate_on_state_change_ &&
      (!is_throbbing_ || !hover_animation_.is_animating())) {
    is_throbbing_ = false;
    if ((state_ == STATE_HOVERED) && (state == STATE_NORMAL)) {
      // For HOVERED -> NORMAL, animate from hovered (1) to not hovered (0).
      hover_animation_.Hide();
    } else if (state != STATE_HOVERED) {
      // For HOVERED -> PRESSED/DISABLED, or any transition not involving
      // HOVERED at all, simply set the state to not hovered (0).
      hover_animation_.Reset();
    } else if (state_ == STATE_NORMAL) {
      // For NORMAL -> HOVERED, animate from not hovered (0) to hovered (1).
      hover_animation_.Show();
    } else {
      // For PRESSED/DISABLED -> HOVERED, simply set the state to hovered (1).
      hover_animation_.Reset(1);
    }
  }

  state_ = state;
  StateChanged();
  SchedulePaint();
}

void CustomButton::StartThrobbing(int cycles_til_stop) {
  is_throbbing_ = true;
  hover_animation_.StartThrobbing(cycles_til_stop);
}

void CustomButton::StopThrobbing() {
  if (hover_animation_.is_animating()) {
    hover_animation_.Stop();
    SchedulePaint();
  }
}

void CustomButton::SetAnimationDuration(int duration) {
  hover_animation_.SetSlideDuration(duration);
}

void CustomButton::SetHotTracked(bool is_hot_tracked) {
  if (state_ != STATE_DISABLED)
    SetState(is_hot_tracked ? STATE_HOVERED : STATE_NORMAL);

  if (is_hot_tracked)
    NotifyAccessibilityEvent(ui::AX_EVENT_HOVER, true);
}

bool CustomButton::IsHotTracked() const {
  return state_ == STATE_HOVERED;
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, View overrides:

void CustomButton::OnEnabledChanged() {
  // TODO(bruthig): Is there any reason we are not calling
  // Button::OnEnabledChanged() here?
  if (enabled() ? (state_ != STATE_DISABLED) : (state_ == STATE_DISABLED))
    return;

  if (enabled())
    SetState(ShouldEnterHoveredState() ? STATE_HOVERED : STATE_NORMAL);
  else
    SetState(STATE_DISABLED);

  if (ink_drop_delegate_)
    ink_drop_delegate_->SetHovered(ShouldShowInkDropHover());
}

const char* CustomButton::GetClassName() const {
  return kViewClassName;
}

bool CustomButton::OnMousePressed(const ui::MouseEvent& event) {
  if (state_ == STATE_DISABLED)
    return true;
  if (state_ != STATE_PRESSED && ShouldEnterPushedState(event) &&
      HitTestPoint(event.location())) {
    SetState(STATE_PRESSED);
    if (ink_drop_delegate_)
      ink_drop_delegate_->OnAction(views::InkDropState::ACTION_PENDING);
  }
  if (request_focus_on_press_)
    RequestFocus();
  if (IsTriggerableEvent(event) && notify_action_ == NOTIFY_ON_PRESS) {
    NotifyClick(event);
    // NOTE: We may be deleted at this point (by the listener's notification
    // handler).
  }
  return true;
}

bool CustomButton::OnMouseDragged(const ui::MouseEvent& event) {
  if (state_ != STATE_DISABLED) {
    if (HitTestPoint(event.location()))
      SetState(ShouldEnterPushedState(event) ? STATE_PRESSED : STATE_HOVERED);
    else
      SetState(STATE_NORMAL);
  }
  return true;
}

void CustomButton::OnMouseReleased(const ui::MouseEvent& event) {
  if (state_ != STATE_DISABLED) {
    if (!HitTestPoint(event.location())) {
      SetState(STATE_NORMAL);
    } else {
      SetState(STATE_HOVERED);
      if (IsTriggerableEvent(event) && notify_action_ == NOTIFY_ON_RELEASE) {
        NotifyClick(event);
        // NOTE: We may be deleted at this point (by the listener's notification
        // handler).
        return;
      }
    }
  }
  if (notify_action_ == NOTIFY_ON_RELEASE)
    OnClickCanceled(event);
}

void CustomButton::OnMouseCaptureLost() {
  // Starting a drag results in a MouseCaptureLost. Reset button state.
  // TODO(varkha) While in drag only reset the state with Material Design.
  // The same logic may applies everywhere so gather any feedback and update.
  bool reset_button_state =
      !InDrag() || ui::MaterialDesignController::IsModeMaterial();
  if (state_ != STATE_DISABLED && reset_button_state)
    SetState(STATE_NORMAL);
  if (ink_drop_delegate_)
    ink_drop_delegate_->OnAction(views::InkDropState::HIDDEN);
}

void CustomButton::OnMouseEntered(const ui::MouseEvent& event) {
  if (state_ != STATE_DISABLED)
    SetState(STATE_HOVERED);
}

void CustomButton::OnMouseExited(const ui::MouseEvent& event) {
  // Starting a drag results in a MouseExited, we need to ignore it.
  if (state_ != STATE_DISABLED && !InDrag())
    SetState(STATE_NORMAL);
}

void CustomButton::OnMouseMoved(const ui::MouseEvent& event) {
  if (state_ != STATE_DISABLED)
    SetState(HitTestPoint(event.location()) ? STATE_HOVERED : STATE_NORMAL);
}

bool CustomButton::OnKeyPressed(const ui::KeyEvent& event) {
  if (state_ == STATE_DISABLED)
    return false;

  // Space sets button state to pushed. Enter clicks the button. This matches
  // the Windows native behavior of buttons, where Space clicks the button on
  // KeyRelease and Enter clicks the button on KeyPressed.
  if (event.key_code() == ui::VKEY_SPACE) {
    SetState(STATE_PRESSED);
  } else if (event.key_code() == ui::VKEY_RETURN) {
    SetState(STATE_NORMAL);
    NotifyClick(event);
  } else {
    return false;
  }
  return true;
}

bool CustomButton::OnKeyReleased(const ui::KeyEvent& event) {
  if ((state_ == STATE_DISABLED) || (event.key_code() != ui::VKEY_SPACE))
    return false;

  SetState(STATE_NORMAL);
  NotifyClick(event);
  return true;
}

void CustomButton::OnGestureEvent(ui::GestureEvent* event) {
  if (state_ == STATE_DISABLED) {
    Button::OnGestureEvent(event);
    return;
  }

  if (event->type() == ui::ET_GESTURE_TAP && IsTriggerableEvent(*event)) {
    // Set the button state to hot and start the animation fully faded in. The
    // GESTURE_END event issued immediately after will set the state to
    // STATE_NORMAL beginning the fade out animation. See
    // http://crbug.com/131184.
    SetState(STATE_HOVERED);
    hover_animation_.Reset(1.0);
    NotifyClick(*event);
    event->StopPropagation();
  } else if (event->type() == ui::ET_GESTURE_TAP_DOWN &&
             ShouldEnterPushedState(*event)) {
    SetState(STATE_PRESSED);
    if (request_focus_on_press_)
      RequestFocus();
    event->StopPropagation();
  } else if (event->type() == ui::ET_GESTURE_TAP_CANCEL ||
             event->type() == ui::ET_GESTURE_END) {
    SetState(STATE_NORMAL);
  }
  if (!event->handled())
    Button::OnGestureEvent(event);
}

bool CustomButton::AcceleratorPressed(const ui::Accelerator& accelerator) {
  SetState(STATE_NORMAL);
  // TODO(beng): remove once NotifyClick takes ui::Event.
  ui::MouseEvent synthetic_event(
      ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(), ui::EventTimeForNow(),
      ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  NotifyClick(synthetic_event);
  return true;
}

bool CustomButton::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  // If this button is focused and the user presses space or enter, don't let
  // that be treated as an accelerator.
  return (event.key_code() == ui::VKEY_SPACE) ||
         (event.key_code() == ui::VKEY_RETURN);
}

void CustomButton::ShowContextMenu(const gfx::Point& p,
                                   ui::MenuSourceType source_type) {
  if (!context_menu_controller())
    return;

  // We're about to show the context menu. Showing the context menu likely means
  // we won't get a mouse exited and reset state. Reset it now to be sure.
  if (state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
  if (ink_drop_delegate_) {
    ink_drop_delegate_->SetHovered(false);
    ink_drop_delegate_->OnAction(InkDropState::HIDDEN);
  }
  View::ShowContextMenu(p, source_type);
}

void CustomButton::OnDragDone() {
  // Only reset the state to normal if the button isn't currently disabled
  // (since disabled buttons may still be able to be dragged).
  if (state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
  if (ink_drop_delegate_)
    ink_drop_delegate_->OnAction(InkDropState::HIDDEN);
}

void CustomButton::GetAccessibleState(ui::AXViewState* state) {
  Button::GetAccessibleState(state);
  switch (state_) {
    case STATE_HOVERED:
      state->AddStateFlag(ui::AX_STATE_HOVERED);
      break;
    case STATE_PRESSED:
      state->AddStateFlag(ui::AX_STATE_PRESSED);
      break;
    case STATE_DISABLED:
      state->AddStateFlag(ui::AX_STATE_DISABLED);
      break;
    case STATE_NORMAL:
    case STATE_COUNT:
      // No additional accessibility state set for this button state.
      break;
  }
}

void CustomButton::VisibilityChanged(View* starting_from, bool visible) {
  if (state_ == STATE_DISABLED)
    return;
  SetState(visible && ShouldEnterHoveredState() ? STATE_HOVERED : STATE_NORMAL);
}

scoped_ptr<InkDropHover> CustomButton::CreateInkDropHover() const {
  return ShouldShowInkDropHover() ? Button::CreateInkDropHover() : nullptr;
}

SkColor CustomButton::GetInkDropBaseColor() const {
  return ink_drop_base_color_;
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, gfx::AnimationDelegate implementation:

void CustomButton::AnimationProgressed(const gfx::Animation* animation) {
  SchedulePaint();
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, View overrides (public):

void CustomButton::Layout() {
  Button::Layout();
  gfx::Rect focus_bounds = GetLocalBounds();
  focus_bounds.Inset(gfx::Insets(kFocusBorderOutset));
  if (md_focus_ring_)
    md_focus_ring_->SetBoundsRect(focus_bounds);
}

void CustomButton::ViewHierarchyChanged(
    const ViewHierarchyChangedDetails& details) {
  if (!details.is_add && state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
}

void CustomButton::OnFocus() {
  Button::OnFocus();
  if (md_focus_ring_)
    md_focus_ring_->SetVisible(true);
}

void CustomButton::OnBlur() {
  Button::OnBlur();
  if (IsHotTracked())
    SetState(STATE_NORMAL);
  if (md_focus_ring_)
    md_focus_ring_->SetVisible(false);
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, protected:

CustomButton::CustomButton(ButtonListener* listener)
    : Button(listener),
      state_(STATE_NORMAL),
      hover_animation_(this),
      animate_on_state_change_(true),
      is_throbbing_(false),
      triggerable_event_flags_(ui::EF_LEFT_MOUSE_BUTTON),
      request_focus_on_press_(true),
      ink_drop_delegate_(nullptr),
      notify_action_(NOTIFY_ON_RELEASE),
      has_ink_drop_action_on_click_(false),
      ink_drop_action_on_click_(InkDropState::ACTION_TRIGGERED),
      ink_drop_base_color_(gfx::kPlaceholderColor),
      md_focus_ring_(nullptr) {
  hover_animation_.SetSlideDuration(kHoverFadeDurationMs);
}

void CustomButton::StateChanged() {
}

bool CustomButton::IsTriggerableEvent(const ui::Event& event) {
  return event.type() == ui::ET_GESTURE_TAP_DOWN ||
         event.type() == ui::ET_GESTURE_TAP ||
         (event.IsMouseEvent() &&
             (triggerable_event_flags_ & event.flags()) != 0);
}

bool CustomButton::ShouldEnterPushedState(const ui::Event& event) {
  return IsTriggerableEvent(event);
}

bool CustomButton::ShouldShowInkDropHover() const {
  return enabled() && IsMouseHovered() && !InDrag();
}

bool CustomButton::ShouldEnterHoveredState() {
  if (!visible())
    return false;

  bool check_mouse_position = true;
#if defined(USE_AURA)
  // If another window has capture, we shouldn't check the current mouse
  // position because the button won't receive any mouse events - so if the
  // mouse was hovered, the button would be stuck in a hovered state (since it
  // would never receive OnMouseExited).
  const Widget* widget = GetWidget();
  if (widget && widget->GetNativeWindow()) {
    aura::Window* root_window = widget->GetNativeWindow()->GetRootWindow();
    aura::client::CaptureClient* capture_client =
        aura::client::GetCaptureClient(root_window);
    aura::Window* capture_window =
        capture_client ? capture_client->GetGlobalCaptureWindow() : nullptr;
    check_mouse_position = !capture_window || capture_window == root_window;
  }
#endif

  return check_mouse_position && IsMouseHovered();
}

void CustomButton::UseMdFocusRing() {
  DCHECK(!md_focus_ring_);
  md_focus_ring_ = new MdFocusRing();
  AddChildView(md_focus_ring_);
  md_focus_ring_->SetVisible(false);
  set_request_focus_on_press(false);
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, Button overrides (protected):

void CustomButton::NotifyClick(const ui::Event& event) {
  if (ink_drop_delegate() && has_ink_drop_action_on_click_)
    ink_drop_delegate()->OnAction(ink_drop_action_on_click_);
  Button::NotifyClick(event);
}

void CustomButton::OnClickCanceled(const ui::Event& event) {
  if (ink_drop_delegate())
    ink_drop_delegate()->OnAction(views::InkDropState::HIDDEN);
  Button::OnClickCanceled(event);
}

}  // namespace views
