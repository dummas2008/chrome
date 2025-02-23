// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SHELL_SHELL_DELEGATE_IMPL_H_
#define ASH_SHELL_SHELL_DELEGATE_IMPL_H_

#include <memory>
#include <string>

#include "ash/shell_delegate.h"
#include "base/compiler_specific.h"
#include "base/macros.h"

namespace app_list {
class AppListShowerDelegateFactory;
class AppListShowerImpl;
}

namespace keyboard {
class KeyboardUI;
}

namespace ash {
namespace shell {

class ShelfDelegateImpl;

class ShellDelegateImpl : public ash::ShellDelegate {
 public:
  ShellDelegateImpl();
  ~ShellDelegateImpl() override;

  bool IsFirstRunAfterBoot() const override;
  bool IsIncognitoAllowed() const override;
  bool IsMultiProfilesEnabled() const override;
  bool IsRunningInForcedAppMode() const override;
  bool CanShowWindowForUser(aura::Window* window) const override;
  bool IsForceMaximizeOnFirstRun() const override;
  void PreInit() override;
  void PreShutdown() override;
  void Exit() override;
  keyboard::KeyboardUI* CreateKeyboardUI() override;
  void VirtualKeyboardActivated(bool activated) override;
  void AddVirtualKeyboardStateObserver(
      VirtualKeyboardStateObserver* observer) override;
  void RemoveVirtualKeyboardStateObserver(
      VirtualKeyboardStateObserver* observer) override;
  void OpenUrl(const GURL& url) override;
  app_list::AppListShower* GetAppListShower() override;
  ShelfDelegate* CreateShelfDelegate(ShelfModel* model) override;
  ash::SystemTrayDelegate* CreateSystemTrayDelegate() override;
  ash::UserWallpaperDelegate* CreateUserWallpaperDelegate() override;
  ash::SessionStateDelegate* CreateSessionStateDelegate() override;
  ash::AccessibilityDelegate* CreateAccessibilityDelegate() override;
  ash::NewWindowDelegate* CreateNewWindowDelegate() override;
  ash::MediaDelegate* CreateMediaDelegate() override;
  ui::MenuModel* CreateContextMenu(ash::Shelf* shelf,
                                   const ash::ShelfItem* item) override;
  GPUSupport* CreateGPUSupport() override;
  base::string16 GetProductName() const override;
  gfx::Image GetDeprecatedAcceleratorImage() const override;

 private:
  ShelfDelegateImpl* shelf_delegate_;
  std::unique_ptr<app_list::AppListShowerDelegateFactory>
      app_list_shower_delegate_factory_;
  std::unique_ptr<app_list::AppListShowerImpl> app_list_shower_;

  DISALLOW_COPY_AND_ASSIGN(ShellDelegateImpl);
};

}  // namespace shell
}  // namespace ash

#endif  // ASH_SHELL_SHELL_DELEGATE_IMPL_H_
