// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/widget/desktop_aura/desktop_screen_win.h"

#include "base/logging.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_tree_host.h"
#include "ui/gfx/display.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_win.h"

namespace views {

////////////////////////////////////////////////////////////////////////////////
// DesktopScreenWin, public:

DesktopScreenWin::DesktopScreenWin() {
}

DesktopScreenWin::~DesktopScreenWin() {
}

////////////////////////////////////////////////////////////////////////////////
// DesktopScreenWin, display::win::ScreenWin implementation:

gfx::Display DesktopScreenWin::GetDisplayMatching(
    const gfx::Rect& match_rect) const {
  return GetDisplayNearestPoint(match_rect.CenterPoint());
}

HWND DesktopScreenWin::GetHWNDFromNativeView(gfx::NativeView window) const {
  aura::WindowTreeHost* host = window->GetHost();
  return host ? host->GetAcceleratedWidget() : NULL;
}

gfx::NativeWindow DesktopScreenWin::GetNativeWindowFromHWND(HWND hwnd) const {
  return (::IsWindow(hwnd)) ?
      DesktopWindowTreeHostWin::GetContentWindowForHWND(hwnd) : NULL;
}

////////////////////////////////////////////////////////////////////////////////

gfx::Screen* CreateDesktopScreen() {
  return new DesktopScreenWin;
}

}  // namespace views
