// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/chrome_mash_shelf_controller.h"

#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/ash/chrome_launcher_prefs.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/theme_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/common/mojo_shell_connection.h"
#include "mojo/shell/public/cpp/connector.h"
#include "skia/public/type_converters.h"
#include "ui/base/resource/resource_bundle.h"

class ChromeShelfItemDelegate : public mash::shelf::mojom::ShelfItemDelegate {
 public:
  ChromeShelfItemDelegate() : item_delegate_binding_(this) {}
  ~ChromeShelfItemDelegate() override {}

  mash::shelf::mojom::ShelfItemDelegateAssociatedPtrInfo
  CreateInterfacePtrInfoAndBind(mojo::AssociatedGroup* associated_group) {
    DCHECK(!item_delegate_binding_.is_bound());
    mash::shelf::mojom::ShelfItemDelegateAssociatedPtrInfo ptr_info;
    item_delegate_binding_.Bind(&ptr_info, associated_group);
    return ptr_info;
  }

 private:
  // mash::shelf::mojom::ShelfItemDelegate:
  void ExecuteCommand(uint32_t command_id, int32_t event_flags) override {
    NOTIMPLEMENTED();
  }
  void ItemPinned() override { NOTIMPLEMENTED(); }
  void ItemUnpinned() override { NOTIMPLEMENTED(); }
  void ItemReordered(uint32_t order) override { NOTIMPLEMENTED(); }

  mojo::AssociatedBinding<mash::shelf::mojom::ShelfItemDelegate>
      item_delegate_binding_;

  DISALLOW_COPY_AND_ASSIGN(ChromeShelfItemDelegate);
};

// static
ChromeMashShelfController* ChromeMashShelfController::instance_ = nullptr;

ChromeMashShelfController::~ChromeMashShelfController() {}

// static
ChromeMashShelfController* ChromeMashShelfController::CreateInstance() {
  DCHECK(!instance_);
  instance_ = new ChromeMashShelfController();
  instance_->Init();
  return instance_;
}

ChromeMashShelfController::ChromeMashShelfController()
    : observer_binding_(this) {}

void ChromeMashShelfController::Init() {
  mojo::Connector* connector =
      content::MojoShellConnection::Get()->GetConnector();
  connector->ConnectToInterface("mojo:ash_sysui", &shelf_controller_);

  // Set shelf alignment and auto-hide behavior from preferences.
  Profile* profile = ProfileManager::GetActiveUserProfile();

  const std::string& alignment_value =
      profile->GetPrefs()->GetString(prefs::kShelfAlignmentLocal);
  shelf_controller_->SetAlignment(static_cast<mash::shelf::mojom::Alignment>(
      ash::AlignmentFromPref(alignment_value)));

  const std::string& auto_hide_value =
      profile->GetPrefs()->GetString(prefs::kShelfAutoHideBehaviorLocal);
  shelf_controller_->SetAutoHideBehavior(
      static_cast<mash::shelf::mojom::AutoHideBehavior>(
          ash::AutoHideBehaviorFromPref(auto_hide_value)));

  // Create a test shortcut item to a fake application.
  mash::shelf::mojom::ShelfItemPtr item(mash::shelf::mojom::ShelfItem::New());
  std::string item_id("mojo:fake_app");
  item->id = item_id;
  item->title = "Fake Mojo App (test pinned shelf item)";
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  const gfx::Image& image = rb.GetImageNamed(IDR_PRODUCT_LOGO_32);
  item->image = skia::mojom::Bitmap::From(*image.ToSkBitmap());
  scoped_ptr<ChromeShelfItemDelegate> delegate(new ChromeShelfItemDelegate());
  shelf_controller_->AddItem(std::move(item),
                             delegate->CreateInterfacePtrInfoAndBind(
                                 shelf_controller_.associated_group()));
  app_id_to_item_delegate_.insert(std::make_pair(item_id, std::move(delegate)));

  // Start observing the shelf now that it has been initialized.
  mash::shelf::mojom::ShelfObserverAssociatedPtrInfo ptr_info;
  observer_binding_.Bind(&ptr_info, shelf_controller_.associated_group());
  shelf_controller_->AddObserver(std::move(ptr_info));
}

void ChromeMashShelfController::OnAlignmentChanged(
    mash::shelf::mojom::Alignment alignment) {
  const char* value =
      ash::AlignmentToPref(static_cast<ash::ShelfAlignment>(alignment));
  Profile* profile = ProfileManager::GetActiveUserProfile();
  profile->GetPrefs()->SetString(prefs::kShelfAlignmentLocal, value);
}

void ChromeMashShelfController::OnAutoHideBehaviorChanged(
    mash::shelf::mojom::AutoHideBehavior auto_hide) {
  const char* value = ash::AutoHideBehaviorToPref(
      static_cast<ash::ShelfAutoHideBehavior>(auto_hide));
  if (!value)
    return;
  Profile* profile = ProfileManager::GetActiveUserProfile();
  profile->GetPrefs()->SetString(prefs::kShelfAutoHideBehaviorLocal, value);
}
