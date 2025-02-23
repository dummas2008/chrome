// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_UI_ZOOM_ZOOM_EVENT_MANAGER_H_
#define COMPONENTS_UI_ZOOM_ZOOM_EVENT_MANAGER_H_

#include "base/callback_list.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/supports_user_data.h"
#include "content/public/browser/host_zoom_map.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ui_zoom {

class ZoomEventManagerObserver;

// This class serves as a target for event notifications from all ZoomController
// objects. Classes that need to know about browser-specific zoom events (e.g.
// manual-mode zoom) should subscribe here.
class ZoomEventManager : public base::SupportsUserData::Data {
 public:
  ZoomEventManager();
  ~ZoomEventManager() override;

  // Returns the ZoomEventManager for the specified BrowserContext. This
  // function creates the ZoomEventManager if it hasn't been created already.
  static ZoomEventManager* GetForBrowserContext(
      content::BrowserContext* context);

  // Called by ZoomControllers when changes are made to zoom levels in manual
  // mode in order that browser listeners can be notified.
  void OnZoomLevelChanged(const content::HostZoomMap::ZoomLevelChange& change);

  // Add and remove zoom level changed callbacks.
  // TODO(wjmaclean): Convert this callback mechanism to use
  // ZoomEventManagerObserver instead.
  scoped_ptr<content::HostZoomMap::Subscription> AddZoomLevelChangedCallback(
      const content::HostZoomMap::ZoomLevelChangedCallback& callback);

  // Called by ZoomLevelDelegates when changes are made to the default zoom
  // level for their associated HostZoomMap.
  void OnDefaultZoomLevelChanged();

  // Add and remove observers.
  void AddZoomEventManagerObserver(ZoomEventManagerObserver* observer);
  void RemoveZoomEventManagerObserver(ZoomEventManagerObserver* observer);

  // Get a weak ptr to be used by clients who may themselves be UserData for
  // the context, since the order of destruction is undefined between the client
  // and this class.
  base::WeakPtr<ZoomEventManager> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  base::CallbackList<void(const content::HostZoomMap::ZoomLevelChange&)>
      zoom_level_changed_callbacks_;
  base::ObserverList<ZoomEventManagerObserver> observers_;
  base::WeakPtrFactory<ZoomEventManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ZoomEventManager);
};

}  // namespace ui_zoom

#endif  // COMPONENTS_UI_ZOOM_ZOOM_EVENT_MANAGER_H_
