// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/extensions/extension_popup.h"

#include "base/bind.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"

namespace {

// Override the default margin provided by views::kPanel*Margin so that the
// hosted WebContents fill more of the bubble. However, it can't fill the entire
// bubble since that would draw over the rounded corners and make the bubble
// square. See http://crbug.com/593203.
const int kBubbleMargin = 2;

ExtensionViewViews* GetExtensionView(extensions::ExtensionViewHost* host) {
  return static_cast<ExtensionViewViews*>(host->view());
}

}  // namespace

// The minimum/maximum dimensions of the popup.
// The minimum is just a little larger than the size of the button itself.
// The maximum is an arbitrary number that should be smaller than most screens.
const int ExtensionPopup::kMinWidth = 25;
const int ExtensionPopup::kMinHeight = 25;
const int ExtensionPopup::kMaxWidth = 800;
const int ExtensionPopup::kMaxHeight = 600;

#if !defined(USE_AURA)
// static
ExtensionPopup* ExtensionPopup::Create(extensions::ExtensionViewHost* host,
                                       views::View* anchor_view,
                                       views::BubbleBorder::Arrow arrow,
                                       ShowAction show_action) {
  auto popup = new ExtensionPopup(host, anchor_view, arrow, show_action);
  views::BubbleDialogDelegateView::CreateBubble(popup);
  return popup;
}
#endif

ExtensionPopup::ExtensionPopup(extensions::ExtensionViewHost* host,
                               views::View* anchor_view,
                               views::BubbleBorder::Arrow arrow,
                               ShowAction show_action)
    : BubbleDialogDelegateView(anchor_view, arrow),
      host_(host),
      devtools_callback_(base::Bind(
          &ExtensionPopup::OnDevToolsStateChanged, base::Unretained(this))),
      widget_initialized_(false) {
  inspect_with_devtools_ = show_action == SHOW_AND_INSPECT;
  set_margins(gfx::Insets(kBubbleMargin));
  SetLayoutManager(new views::FillLayout());
  AddChildView(GetExtensionView(host));
  GetExtensionView(host)->set_container(this);
  // ExtensionPopup closes itself on very specific de-activation conditions.
  set_close_on_deactivate(false);

  // Listen for the containing view calling window.close();
  registrar_.Add(
      this,
      extensions::NOTIFICATION_EXTENSION_HOST_VIEW_SHOULD_CLOSE,
      content::Source<content::BrowserContext>(host->browser_context()));
  content::DevToolsAgentHost::AddAgentStateCallback(devtools_callback_);

  GetExtensionView(host)->GetBrowser()->tab_strip_model()->AddObserver(this);

  // If the host had somehow finished loading, then we'd miss the notification
  // and not show.  This seems to happen in single-process mode.
  if (host_->has_loaded_once()) {
    ShowBubble();
  } else {
    // Wait to show the popup until the contained host finishes loading.
    registrar_.Add(this,
                   content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
                   content::Source<content::WebContents>(
                       host_->host_contents()));
  }
}

ExtensionPopup::~ExtensionPopup() {
  content::DevToolsAgentHost::RemoveAgentStateCallback(devtools_callback_);

  GetExtensionView(
      host_.get())->GetBrowser()->tab_strip_model()->RemoveObserver(this);
}

int ExtensionPopup::GetDialogButtons() const {
  return ui::DIALOG_BUTTON_NONE;
}

void ExtensionPopup::Observe(int type,
                             const content::NotificationSource& source,
                             const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME:
      DCHECK_EQ(host()->host_contents(),
                content::Source<content::WebContents>(source).ptr());
      // Show when the content finishes loading and its width is computed.
      ShowBubble();
      break;
    case extensions::NOTIFICATION_EXTENSION_HOST_VIEW_SHOULD_CLOSE:
      // If we aren't the host of the popup, then disregard the notification.
      if (content::Details<extensions::ExtensionHost>(host()) == details)
        GetWidget()->Close();
      break;
    default:
      NOTREACHED() << L"Received unexpected notification";
  }
}

void ExtensionPopup::OnDevToolsStateChanged(
    content::DevToolsAgentHost* agent_host,
    bool attached) {
  // First check that the devtools are being opened on this popup.
  if (host()->host_contents() != agent_host->GetWebContents())
    return;

  if (attached) {
    // Set inspect_with_devtools_ so the popup will be kept open while
    // the devtools are open.
    inspect_with_devtools_ = true;
  } else {
    // Widget::Close posts a task, which should give the devtools window a
    // chance to finish detaching from the inspected RenderViewHost.
    GetWidget()->Close();
  }
}

void ExtensionPopup::OnExtensionSizeChanged(ExtensionViewViews* view) {
  SizeToContents();
}

gfx::Size ExtensionPopup::GetPreferredSize() const {
  // Constrain the size to popup min/max.
  gfx::Size sz = views::View::GetPreferredSize();
  sz.set_width(std::max(kMinWidth, std::min(kMaxWidth, sz.width())));
  sz.set_height(std::max(kMinHeight, std::min(kMaxHeight, sz.height())));
  return sz;
}

void ExtensionPopup::ViewHierarchyChanged(
  const ViewHierarchyChangedDetails& details) {
  // TODO(msw): Find any remaining crashes related to http://crbug.com/327776
  // No view hierarchy changes are expected if the widget no longer exists.
  widget_initialized_ |= details.child == this && details.is_add && GetWidget();
  CHECK(GetWidget() || !widget_initialized_);
}

void ExtensionPopup::OnWidgetActivationChanged(views::Widget* widget,
                                               bool active) {
  if (active && widget == anchor_widget())
    OnAnchorWindowActivation();
}

void ExtensionPopup::ActiveTabChanged(content::WebContents* old_contents,
                                      content::WebContents* new_contents,
                                      int index,
                                      int reason) {
  GetWidget()->Close();
}

void ExtensionPopup::OnAnchorWindowActivation() {
  // TODO(msw): Find any remaining crashes related to http://crbug.com/327776
  // No calls are expected if the widget isn't initialized or no longer exists.
  CHECK(widget_initialized_);
  CHECK(GetWidget());

  if (!inspect_with_devtools_)
    GetWidget()->Close();
}

// static
ExtensionPopup* ExtensionPopup::ShowPopup(
    std::unique_ptr<extensions::ExtensionViewHost> host,
    views::View* anchor_view,
    views::BubbleBorder::Arrow arrow,
    ShowAction show_action) {
  return ExtensionPopup::Create(
      host.release(), anchor_view, arrow, show_action);
}

void ExtensionPopup::ShowBubble() {
  GetWidget()->Show();

  // Focus on the host contents when the bubble is first shown.
  host()->host_contents()->Focus();

  if (inspect_with_devtools_) {
    DevToolsWindow::OpenDevToolsWindow(host()->host_contents(),
                                       DevToolsToggleAction::ShowConsole());
  }
}
