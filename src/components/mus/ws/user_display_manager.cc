// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mus/ws/user_display_manager.h"

#include "components/mus/ws/display.h"
#include "components/mus/ws/display_manager.h"
#include "components/mus/ws/window_manager_state.h"

namespace mus {
namespace ws {

UserDisplayManager::UserDisplayManager(ws::DisplayManager* display_manager,
                                       const UserId& user_id)
    : display_manager_(display_manager), user_id_(user_id) {
  for (const WindowManagerState* wms : GetWindowManagerStatesForUser()) {
    if (wms->got_frame_decoration_values()) {
      got_valid_frame_decorations_ = true;
      break;
    }
  }
}

UserDisplayManager::~UserDisplayManager() {}

void UserDisplayManager::OnFrameDecorationValuesChanged(
    WindowManagerState* wms) {
  if (!got_valid_frame_decorations_) {
    got_valid_frame_decorations_ = true;
    display_manager_observers_.ForAllPtrs([this](
        mojom::DisplayManagerObserver* observer) { CallOnDisplays(observer); });
    if (test_observer_)
      CallOnDisplays(test_observer_);
    return;
  }

  display_manager_observers_.ForAllPtrs(
      [this, &wms](mojom::DisplayManagerObserver* observer) {
        CallOnDisplayChanged(wms, observer);
      });
  if (test_observer_)
    CallOnDisplayChanged(wms, test_observer_);
}

void UserDisplayManager::AddDisplayManagerBinding(
    mojo::InterfaceRequest<mojom::DisplayManager> request) {
  display_manager_bindings_.AddBinding(this, std::move(request));
}

void UserDisplayManager::OnWillDestroyDisplay(Display* display) {
  if (!display->GetWindowManagerStateForUser(user_id_)
           ->got_frame_decoration_values()) {
    return;
  }

  display_manager_observers_.ForAllPtrs(
      [this, &display](mojom::DisplayManagerObserver* observer) {
        observer->OnDisplayRemoved(display->id());
      });
  if (test_observer_)
    test_observer_->OnDisplayRemoved(display->id());
}

std::set<const WindowManagerState*>
UserDisplayManager::GetWindowManagerStatesForUser() const {
  std::set<const WindowManagerState*> result;
  for (const Display* display : display_manager_->displays()) {
    const WindowManagerState* wms =
        display->GetWindowManagerStateForUser(user_id_);
    if (wms && wms->got_frame_decoration_values())
      result.insert(wms);
  }
  return result;
}

void UserDisplayManager::OnObserverAdded(
    mojom::DisplayManagerObserver* observer) {
  // Many clients key off the frame decorations to size widgets. Wait for frame
  // decorations before notifying so that we don't have to worry about clients
  // resizing appropriately.
  if (!got_valid_frame_decorations_)
    return;

  CallOnDisplays(observer);
}

void UserDisplayManager::CallOnDisplays(
    mojom::DisplayManagerObserver* observer) {
  std::set<const WindowManagerState*> wmss = GetWindowManagerStatesForUser();
  mojo::Array<mojom::DisplayPtr> display_ptrs(wmss.size());
  {
    size_t i = 0;
    // TODO(sky): need ordering!
    for (const WindowManagerState* wms : wmss) {
      display_ptrs[i] = wms->ToMojomDisplay();
      ++i;
    }
  }
  observer->OnDisplays(std::move(display_ptrs));
}

void UserDisplayManager::CallOnDisplayChanged(
    WindowManagerState* wms,
    mojom::DisplayManagerObserver* observer) {
  mojo::Array<mojom::DisplayPtr> displays(1);
  displays[0] = wms->ToMojomDisplay();
  display_manager_observers_.ForAllPtrs(
      [&displays](mojom::DisplayManagerObserver* observer) {
        observer->OnDisplaysChanged(displays.Clone());
      });
  if (test_observer_)
    test_observer_->OnDisplaysChanged(displays.Clone());
}

void UserDisplayManager::AddObserver(
    mojom::DisplayManagerObserverPtr observer) {
  mojom::DisplayManagerObserver* observer_impl = observer.get();
  display_manager_observers_.AddPtr(std::move(observer));
  OnObserverAdded(observer_impl);
}

}  // namespace ws
}  // namespace mus
