// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_LAUNCHER_CHROME_MASH_SHELF_CONTROLLER_H_
#define CHROME_BROWSER_UI_ASH_LAUNCHER_CHROME_MASH_SHELF_CONTROLLER_H_

#include <map>
#include <string>

#include "base/memory/scoped_ptr.h"
#include "mash/shelf/public/interfaces/shelf.mojom.h"
#include "mojo/public/cpp/bindings/associated_binding.h"

class ChromeShelfItemDelegate;

// ChromeMashShelfController manages chrome's interaction with the mash shelf.
class ChromeMashShelfController : public mash::shelf::mojom::ShelfObserver {
 public:
  ~ChromeMashShelfController() override;

  // Creates an instance.
  static ChromeMashShelfController* CreateInstance();

  // Returns the single ChromeMashShelfController instance.
  static ChromeMashShelfController* instance() { return instance_; }

 private:
  ChromeMashShelfController();

  void Init();

  // mash::shelf::mojom::ShelfObserver:
  void OnAlignmentChanged(mash::shelf::mojom::Alignment alignment) override;
  void OnAutoHideBehaviorChanged(
      mash::shelf::mojom::AutoHideBehavior auto_hide) override;

  static ChromeMashShelfController* instance_;

  mash::shelf::mojom::ShelfControllerPtr shelf_controller_;
  mojo::AssociatedBinding<mash::shelf::mojom::ShelfObserver> observer_binding_;
  std::map<std::string, scoped_ptr<ChromeShelfItemDelegate>>
      app_id_to_item_delegate_;

  DISALLOW_COPY_AND_ASSIGN(ChromeMashShelfController);
};

#endif  // CHROME_BROWSER_UI_ASH_LAUNCHER_CHROME_MASH_SHELF_CONTROLLER_H_
