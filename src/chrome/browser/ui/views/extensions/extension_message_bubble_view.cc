// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/extensions/extension_message_bubble_view.h"

#include <utility>

#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/thread_task_runner_handle.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_bar_bubble_delegate.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/grit/locale_settings.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

// Layout constants.
const int kExtensionListPadding = 10;
const int kInsetBottomRight = 13;
const int kInsetLeft = 14;
const int kInsetTop = 9;
const int kHeadlineMessagePadding = 4;
const int kHeadlineRowPadding = 10;
const int kMessageBubblePadding = 11;

// How long to wait until showing the bubble (in seconds).
int g_bubble_appearance_wait_time = 5;

}  // namespace

namespace extensions {

ExtensionMessageBubbleView::ExtensionMessageBubbleView(
    views::View* anchor_view,
    views::BubbleBorder::Arrow arrow_location,
    std::unique_ptr<ToolbarActionsBarBubbleDelegate> delegate)
    : BubbleDelegateView(anchor_view, arrow_location),
      delegate_(std::move(delegate)),
      anchor_view_(anchor_view),
      headline_(NULL),
      learn_more_(NULL),
      dismiss_button_(NULL),
      link_clicked_(false),
      action_taken_(false),
      weak_factory_(this) {
  DCHECK(anchor_view->GetWidget());
  set_close_on_deactivate(delegate_->ShouldCloseOnDeactivate());
  set_close_on_esc(true);

  // Compensate for built-in vertical padding in the anchor view's image.
  set_anchor_view_insets(gfx::Insets(
      GetLayoutConstant(LOCATION_BAR_BUBBLE_ANCHOR_VERTICAL_INSET), 0));
}

void ExtensionMessageBubbleView::Show() {
  // Not showing the bubble right away (during startup) has a few benefits:
  // We don't have to worry about focus being lost due to the Omnibox (or to
  // other things that want focus at startup). This allows Esc to work to close
  // the bubble and also solves the keyboard accessibility problem that comes
  // with focus being lost (we don't have a good generic mechanism of injecting
  // bubbles into the focus cycle). Another benefit of delaying the show is
  // that fade-in works (the fade-in isn't apparent if the the bubble appears at
  // startup).
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::Bind(&ExtensionMessageBubbleView::ShowBubble,
                            weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromSeconds(g_bubble_appearance_wait_time));
}

void ExtensionMessageBubbleView::OnWidgetDestroying(views::Widget* widget) {
  // To catch Esc, we monitor destroy message. Unless the link has been clicked,
  // we assume Dismiss was the action taken.
  if (!link_clicked_ && !action_taken_) {
    bool closed_on_deactivation = close_reason() == CloseReason::DEACTIVATION;
    delegate_->OnBubbleClosed(
        closed_on_deactivation
            ? ToolbarActionsBarBubbleDelegate::CLOSE_DISMISS_DEACTIVATION
            : ToolbarActionsBarBubbleDelegate::CLOSE_DISMISS_USER_ACTION);
  }
}

void ExtensionMessageBubbleView::set_bubble_appearance_wait_time_for_testing(
    int time_in_seconds) {
  g_bubble_appearance_wait_time = time_in_seconds;
}

////////////////////////////////////////////////////////////////////////////////
// ExtensionMessageBubbleView - private.

ExtensionMessageBubbleView::~ExtensionMessageBubbleView() {}

void ExtensionMessageBubbleView::ShowBubble() {
  // Since we delay in showing the bubble, the applicable extension(s) may
  // have been removed.
  if (delegate_->ShouldShow()) {
    delegate_->OnBubbleShown();
    GetWidget()->Show();
  } else {
    GetWidget()->Close();
  }
}

void ExtensionMessageBubbleView::Init() {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();

  views::GridLayout* layout = views::GridLayout::CreatePanel(this);
  layout->SetInsets(kInsetTop, kInsetLeft,
                    kInsetBottomRight, kInsetBottomRight);
  SetLayoutManager(layout);

  const int headline_column_set_id = 0;
  views::ColumnSet* top_columns = layout->AddColumnSet(headline_column_set_id);
  top_columns->AddColumn(views::GridLayout::LEADING, views::GridLayout::CENTER,
                         0, views::GridLayout::USE_PREF, 0, 0);
  top_columns->AddPaddingColumn(1, 0);
  layout->StartRow(0, headline_column_set_id);

  headline_ = new views::Label(delegate_->GetHeadingText(),
                               rb.GetFontList(ui::ResourceBundle::MediumFont));
  layout->AddView(headline_);

  layout->AddPaddingRow(0, kHeadlineRowPadding);

  const int text_column_set_id = 1;
  views::ColumnSet* upper_columns = layout->AddColumnSet(text_column_set_id);
  upper_columns->AddColumn(
      views::GridLayout::LEADING, views::GridLayout::LEADING,
      0, views::GridLayout::USE_PREF, 0, 0);
  layout->StartRow(0, text_column_set_id);

  views::Label* message = new views::Label();
  message->SetMultiLine(true);
  message->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  message->SetText(
      delegate_->GetBodyText(anchor_view_->id() == VIEW_ID_BROWSER_ACTION));
  message->SizeToFit(views::Widget::GetLocalizedContentsWidth(
      IDS_EXTENSION_WIPEOUT_BUBBLE_WIDTH_CHARS));
  layout->AddView(message);

  base::string16 item_list_text = delegate_->GetItemListText();
  if (!item_list_text.empty()) {
    const int extension_list_column_set_id = 2;
    views::ColumnSet* middle_columns =
        layout->AddColumnSet(extension_list_column_set_id);
    middle_columns->AddPaddingColumn(0, kExtensionListPadding);
    middle_columns->AddColumn(
        views::GridLayout::LEADING, views::GridLayout::CENTER,
        0, views::GridLayout::USE_PREF, 0, 0);

    layout->StartRowWithPadding(0, extension_list_column_set_id,
        0, kHeadlineMessagePadding);
    views::Label* extensions = new views::Label();
    extensions->SetMultiLine(true);
    extensions->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    extensions->SetText(item_list_text);
    extensions->SizeToFit(views::Widget::GetLocalizedContentsWidth(
        IDS_EXTENSION_WIPEOUT_BUBBLE_WIDTH_CHARS));
    layout->AddView(extensions);
  }

  base::string16 action_button = delegate_->GetActionButtonText();

  const int action_row_column_set_id = 3;
  views::ColumnSet* bottom_columns =
      layout->AddColumnSet(action_row_column_set_id);
  bottom_columns->AddColumn(views::GridLayout::LEADING,
      views::GridLayout::CENTER, 0, views::GridLayout::USE_PREF, 0, 0);
  bottom_columns->AddPaddingColumn(1, 0);
  bottom_columns->AddColumn(views::GridLayout::TRAILING,
      views::GridLayout::CENTER, 0, views::GridLayout::USE_PREF, 0, 0);
  if (!action_button.empty()) {
    bottom_columns->AddColumn(views::GridLayout::TRAILING,
        views::GridLayout::CENTER, 0, views::GridLayout::USE_PREF, 0, 0);
  }
  layout->StartRowWithPadding(0, action_row_column_set_id,
                              0, kMessageBubblePadding);

  learn_more_ = new views::Link(delegate_->GetLearnMoreButtonText());
  learn_more_->set_listener(this);
  layout->AddView(learn_more_);

  if (!action_button.empty()) {
    action_button_ = new views::LabelButton(this, action_button);
    action_button_->SetStyle(views::Button::STYLE_BUTTON);
    layout->AddView(action_button_);
  }

  dismiss_button_ =
      new views::LabelButton(this, delegate_->GetDismissButtonText());
  dismiss_button_->SetStyle(views::Button::STYLE_BUTTON);
  layout->AddView(dismiss_button_);
}

void ExtensionMessageBubbleView::ButtonPressed(views::Button* sender,
                                               const ui::Event& event) {
  action_taken_ = true;
  ToolbarActionsBarBubbleDelegate::CloseAction close_action;
  if (sender == action_button_) {
    close_action = ToolbarActionsBarBubbleDelegate::CLOSE_EXECUTE;
  } else {
    DCHECK_EQ(dismiss_button_, sender);
    close_action = ToolbarActionsBarBubbleDelegate::CLOSE_DISMISS_USER_ACTION;
  }
  delegate_->OnBubbleClosed(close_action);
  GetWidget()->Close();
}

void ExtensionMessageBubbleView::LinkClicked(views::Link* source,
                                             int event_flags) {
  DCHECK_EQ(learn_more_, source);
  link_clicked_ = true;
  delegate_->OnBubbleClosed(ToolbarActionsBarBubbleDelegate::CLOSE_LEARN_MORE);
  GetWidget()->Close();
}

void ExtensionMessageBubbleView::GetAccessibleState(
    ui::AXViewState* state) {
  state->role = ui::AX_ROLE_ALERT;
}

void ExtensionMessageBubbleView::ViewHierarchyChanged(
    const ViewHierarchyChangedDetails& details) {
  if (details.is_add && details.child == this)
    NotifyAccessibilityEvent(ui::AX_EVENT_ALERT, true);
}

}  // namespace extensions
