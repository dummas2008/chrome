// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/mus/shelf_delegate_mus.h"

#include <memory>

#include "ash/shelf/shelf.h"
#include "ash/shelf/shelf_item_delegate.h"
#include "ash/shelf/shelf_item_delegate_manager.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shelf/shelf_model.h"
#include "ash/shelf/shelf_types.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shell.h"
#include "base/strings/string_util.h"
#include "components/mus/public/cpp/property_type_converters.h"
#include "components/mus/public/cpp/window.h"
#include "components/mus/public/cpp/window_property.h"
#include "mojo/common/common_type_converters.h"
#include "mojo/shell/public/cpp/connector.h"
#include "skia/public/type_converters.h"
#include "ui/aura/mus/mus_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/resources/grit/ui_resources.h"
#include "ui/views/mus/window_manager_connection.h"

using mash::wm::mojom::UserWindowController;

namespace ash {
namespace sysui {

namespace {

// A ShelfItemDelegate used for pinned items and open user windows.
class ShelfItemDelegateMus : public ShelfItemDelegate {
 public:
  ShelfItemDelegateMus(uint32_t window_id,
                       const base::string16& title,
                       UserWindowController* user_window_controller)
      : window_id_(window_id),
        title_(title),
        user_window_controller_(user_window_controller) {}
  ~ShelfItemDelegateMus() override {}

  void UpdateTitle(const base::string16& new_title) { title_ = new_title; }

 private:
  // ShelfItemDelegate:
  ShelfItemDelegate::PerformedAction ItemSelected(
      const ui::Event& event) override {
    if (window_id_ != 0) {
      user_window_controller_->FocusUserWindow(window_id_);
      return kExistingWindowActivated;
    }
    NOTIMPLEMENTED();
    return kNoAction;
  }

  base::string16 GetTitle() override { return title_; }

  bool CanPin() const override {
    NOTIMPLEMENTED();
    return false;
  }

  ShelfMenuModel* CreateApplicationMenu(int event_flags) override {
    NOTIMPLEMENTED();
    return nullptr;
  }

  bool IsDraggable() override {
    NOTIMPLEMENTED();
    return false;
  }

  bool ShouldShowTooltip() override { return true; }

  void Close() override { NOTIMPLEMENTED(); }

  // TODO(msw): Support multiple open windows per button.
  uint32_t window_id_;
  base::string16 title_;
  UserWindowController* user_window_controller_;

  DISALLOW_COPY_AND_ASSIGN(ShelfItemDelegateMus);
};

// Returns an icon image from an SkBitmap, or the default shelf icon
// image if the bitmap is empty. Assumes the bitmap is a 1x icon.
// TODO(jamescook): Support other scale factors.
gfx::ImageSkia GetShelfIconFromBitmap(const SkBitmap& bitmap) {
  gfx::ImageSkia icon_image;
  if (!bitmap.isNull()) {
    icon_image = gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
  } else {
    // Use default icon.
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    icon_image = *rb.GetImageSkiaNamed(IDR_DEFAULT_FAVICON);
  }
  return icon_image;
}

// Returns an icon image from a serialized SkBitmap.
gfx::ImageSkia GetShelfIconFromSerializedBitmap(
    const mojo::Array<uint8_t>& serialized_bitmap) {
  SkBitmap bitmap = mojo::ConvertTo<SkBitmap>(serialized_bitmap.storage());
  return GetShelfIconFromBitmap(bitmap);
}

}  // namespace

ShelfDelegateMus::ShelfDelegateMus(ShelfModel* model)
    : model_(model), binding_(this) {
  mojo::Connector* connector =
      views::WindowManagerConnection::Get()->connector();
  connector->ConnectToInterface("mojo:desktop_wm", &user_window_controller_);
  user_window_controller_->AddUserWindowObserver(
      binding_.CreateInterfacePtrAndBind());
}

ShelfDelegateMus::~ShelfDelegateMus() {}

void ShelfDelegateMus::OnShelfCreated(Shelf* shelf) {
  ShelfWidget* widget = shelf->shelf_widget();
  ShelfLayoutManager* layout_manager = widget->shelf_layout_manager();
  mus::Window* window = aura::GetMusWindow(widget->GetNativeWindow());
  gfx::Size size = layout_manager->GetIdealBounds().size();
  window->SetSharedProperty<gfx::Size>(
      mus::mojom::WindowManager::kPreferredSize_Property, size);

  StatusAreaWidget* status_widget = widget->status_area_widget();
  mus::Window* status_window =
      aura::GetMusWindow(status_widget->GetNativeWindow());
  gfx::Size status_size = status_widget->GetWindowBoundsInScreen().size();
  status_window->SetSharedProperty<gfx::Size>(
      mus::mojom::WindowManager::kPreferredSize_Property, status_size);
}

void ShelfDelegateMus::OnShelfDestroyed(Shelf* shelf) {
  NOTIMPLEMENTED();
}

void ShelfDelegateMus::OnShelfAlignmentChanged(Shelf* shelf) {
  observers_.ForAllPtrs([shelf](mash::shelf::mojom::ShelfObserver* observer) {
    observer->OnAlignmentChanged(
        static_cast<mash::shelf::mojom::Alignment>(shelf->GetAlignment()));
  });
}

void ShelfDelegateMus::OnShelfAutoHideBehaviorChanged(Shelf* shelf) {
  observers_.ForAllPtrs([shelf](mash::shelf::mojom::ShelfObserver* observer) {
    observer->OnAutoHideBehaviorChanged(
        static_cast<mash::shelf::mojom::AutoHideBehavior>(
            shelf->auto_hide_behavior()));
  });
}

ShelfID ShelfDelegateMus::GetShelfIDForAppID(const std::string& app_id) {
  NOTIMPLEMENTED();
  return 0;
}

bool ShelfDelegateMus::HasShelfIDToAppIDMapping(ShelfID id) const {
  NOTIMPLEMENTED();
  return false;
}

const std::string& ShelfDelegateMus::GetAppIDForShelfID(ShelfID id) {
  NOTIMPLEMENTED();
  return base::EmptyString();
}

void ShelfDelegateMus::PinAppWithID(const std::string& app_id) {
  NOTIMPLEMENTED();
}

bool ShelfDelegateMus::IsAppPinned(const std::string& app_id) {
  NOTIMPLEMENTED();
  return false;
}

void ShelfDelegateMus::UnpinAppWithID(const std::string& app_id) {
  NOTIMPLEMENTED();
}

void ShelfDelegateMus::AddObserver(
    mash::shelf::mojom::ShelfObserverAssociatedPtrInfo observer) {
  mash::shelf::mojom::ShelfObserverAssociatedPtr observer_ptr;
  observer_ptr.Bind(std::move(observer));
  // Notify the observer of the current state.
  Shelf* shelf = Shelf::ForPrimaryDisplay();
  observer_ptr->OnAlignmentChanged(
      static_cast<mash::shelf::mojom::Alignment>(shelf->GetAlignment()));
  observer_ptr->OnAutoHideBehaviorChanged(
      static_cast<mash::shelf::mojom::AutoHideBehavior>(
          shelf->auto_hide_behavior()));
  observers_.AddPtr(std::move(observer_ptr));
}

void ShelfDelegateMus::SetAlignment(mash::shelf::mojom::Alignment alignment) {
  ShelfAlignment value = static_cast<ShelfAlignment>(alignment);
  Shell::GetInstance()->SetShelfAlignment(value, Shell::GetPrimaryRootWindow());
}

void ShelfDelegateMus::SetAutoHideBehavior(
    mash::shelf::mojom::AutoHideBehavior auto_hide) {
  ShelfAutoHideBehavior value = static_cast<ShelfAutoHideBehavior>(auto_hide);
  Shell::GetInstance()->SetShelfAutoHideBehavior(value,
                                                 Shell::GetPrimaryRootWindow());
}

void ShelfDelegateMus::AddItem(
    mash::shelf::mojom::ShelfItemPtr item,
    mash::shelf::mojom::ShelfItemDelegateAssociatedPtrInfo delegate) {
  ShelfItem shelf_item;
  shelf_item.type = TYPE_APP_SHORTCUT;
  shelf_item.status = STATUS_CLOSED;
  shelf_item.image = GetShelfIconFromBitmap(item->image.To<SkBitmap>());

  std::string item_id(item->id.To<std::string>());
  if (app_id_to_shelf_id_.count(item_id))
    return;
  ShelfID shelf_id = model_->next_id();
  app_id_to_shelf_id_.insert(std::make_pair(item_id, shelf_id));
  model_->Add(shelf_item);

  scoped_ptr<ShelfItemDelegateMus> item_delegate(new ShelfItemDelegateMus(
      0, item->title.To<base::string16>(), user_window_controller_.get()));
  Shell::GetInstance()->shelf_item_delegate_manager()->SetShelfItemDelegate(
      shelf_id, std::move(item_delegate));
}

void ShelfDelegateMus::RemoveItem(const mojo::String& id) {
  std::string item_id(id.To<std::string>());
  DCHECK(app_id_to_shelf_id_.count(item_id));
  model_->RemoveItemAt(model_->ItemIndexByID(app_id_to_shelf_id_[item_id]));
}

void ShelfDelegateMus::OnUserWindowObserverAdded(
    mojo::Array<mash::wm::mojom::UserWindowPtr> user_windows) {
  for (size_t i = 0; i < user_windows.size(); ++i)
    OnUserWindowAdded(std::move(user_windows[i]));
}

void ShelfDelegateMus::OnUserWindowAdded(
    mash::wm::mojom::UserWindowPtr user_window) {
  ShelfItem item;
  item.type = TYPE_PLATFORM_APP;
  item.status = user_window->window_has_focus ? STATUS_ACTIVE : STATUS_RUNNING;
  item.image = GetShelfIconFromSerializedBitmap(user_window->window_app_icon);

  ShelfID shelf_id = model_->next_id();
  window_id_to_shelf_id_.insert(
      std::make_pair(user_window->window_id, shelf_id));
  model_->Add(item);

  std::unique_ptr<ShelfItemDelegate> item_delegate(new ShelfItemDelegateMus(
      user_window->window_id, user_window->window_title.To<base::string16>(),
      user_window_controller_.get()));
  Shell::GetInstance()->shelf_item_delegate_manager()->SetShelfItemDelegate(
      shelf_id, std::move(item_delegate));
}

void ShelfDelegateMus::OnUserWindowRemoved(uint32_t window_id) {
  DCHECK(window_id_to_shelf_id_.count(window_id));
  model_->RemoveItemAt(
      model_->ItemIndexByID(window_id_to_shelf_id_[window_id]));
}

void ShelfDelegateMus::OnUserWindowTitleChanged(
    uint32_t window_id,
    const mojo::String& window_title) {
  DCHECK(window_id_to_shelf_id_.count(window_id));
  ShelfID shelf_id = window_id_to_shelf_id_[window_id];
  ShelfItemDelegateManager* manager =
      Shell::GetInstance()->shelf_item_delegate_manager();
  ShelfItemDelegate* delegate = manager->GetShelfItemDelegate(shelf_id);
  DCHECK(delegate);
  static_cast<ShelfItemDelegateMus*>(delegate)->UpdateTitle(
      window_title.To<base::string16>());

  // There's nothing in the ShelfItem that needs to be updated. But we still
  // need to update the ShelfModel so that the observers can pick up any
  // changes.
  int index = model_->ItemIndexByID(shelf_id);
  DCHECK_GE(index, 0);
  ShelfItems::const_iterator iter = model_->ItemByID(shelf_id);
  DCHECK(iter != model_->items().end());
  model_->Set(index, *iter);
}

void ShelfDelegateMus::OnUserWindowAppIconChanged(
    uint32_t window_id,
    mojo::Array<uint8_t> app_icon) {
  // Find the shelf ID for this window.
  DCHECK(window_id_to_shelf_id_.count(window_id));
  ShelfID shelf_id = window_id_to_shelf_id_[window_id];
  DCHECK_GT(shelf_id, 0);

  // Update the icon in the ShelfItem.
  int index = model_->ItemIndexByID(shelf_id);
  DCHECK_GE(index, 0);
  ShelfItem item = *model_->ItemByID(shelf_id);
  item.image = GetShelfIconFromSerializedBitmap(app_icon);
  model_->Set(index, item);
}

void ShelfDelegateMus::OnUserWindowFocusChanged(uint32_t window_id,
                                                bool has_focus) {
  DCHECK(window_id_to_shelf_id_.count(window_id));
  ShelfID shelf_id = window_id_to_shelf_id_[window_id];
  int index = model_->ItemIndexByID(shelf_id);
  DCHECK_GE(index, 0);
  ShelfItems::const_iterator iter = model_->ItemByID(shelf_id);
  DCHECK(iter != model_->items().end());
  ShelfItem item = *iter;
  item.status = has_focus ? STATUS_ACTIVE : STATUS_RUNNING;
  model_->Set(index, item);
}

}  // namespace sysui
}  // namespace ash
