// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_MANAGER_WIN_H_
#define CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_MANAGER_WIN_H_

#include <oleacc.h>

#include <memory>

#include "base/macros.h"
#include "base/win/scoped_comptr.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#include "ui/accessibility/platform/ax_platform_node_win.h"

namespace content {
class BrowserAccessibilityWin;

// Manages a tree of BrowserAccessibilityWin objects.
class CONTENT_EXPORT BrowserAccessibilityManagerWin
    : public BrowserAccessibilityManager,
      public ui::IAccessible2UsageObserver {
 public:
  BrowserAccessibilityManagerWin(
      const ui::AXTreeUpdate& initial_tree,
      BrowserAccessibilityDelegate* delegate,
      BrowserAccessibilityFactory* factory = new BrowserAccessibilityFactory());

  ~BrowserAccessibilityManagerWin() override;

  static ui::AXTreeUpdate GetEmptyDocument();

  // Get the closest containing HWND.
  HWND GetParentHWND();

  // The IAccessible for the parent window.
  IAccessible* GetParentIAccessible();

  // Calls NotifyWinEvent if the parent window's IAccessible pointer is known.
  void MaybeCallNotifyWinEvent(DWORD event, BrowserAccessibility* node);

  // IAccessible2UsageObserver
  void OnIAccessible2Used() override;

  // BrowserAccessibilityManager methods
  void UserIsReloading() override;
  void NotifyAccessibilityEvent(
      ui::AXEvent event_type, BrowserAccessibility* node) override;
  bool CanFireEvents() override;
  void FireFocusEvent(BrowserAccessibility* node) override;

  // Track this object and post a VISIBLE_DATA_CHANGED notification when
  // its container scrolls.
  // TODO(dmazzoni): remove once http://crbug.com/113483 is fixed.
  void TrackScrollingObject(BrowserAccessibilityWin* node);

  // Called when |accessible_hwnd_| is deleted by its parent.
  void OnAccessibleHwndDeleted();

 protected:
  // AXTreeDelegate methods.
  void OnNodeWillBeDeleted(ui::AXTree* tree, ui::AXNode* node) override;
  void OnNodeCreated(ui::AXTree* tree, ui::AXNode* node) override;
  void OnAtomicUpdateFinished(
      ui::AXTree* tree,
      bool root_changed,
      const std::vector<ui::AXTreeDelegate::Change>& changes) override;

 private:
  // Give BrowserAccessibilityManager::Create access to our constructor.
  friend class BrowserAccessibilityManager;

  // Track the most recent object that has been asked to scroll and
  // post a notification directly on it when it reaches its destination.
  // TODO(dmazzoni): remove once http://crbug.com/113483 is fixed.
  BrowserAccessibilityWin* tracked_scroll_object_;

  DISALLOW_COPY_AND_ASSIGN(BrowserAccessibilityManagerWin);
};

}  // namespace content

#endif  // CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_MANAGER_WIN_H_
