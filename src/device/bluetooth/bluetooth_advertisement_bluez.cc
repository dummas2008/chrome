// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_advertisement_bluez.h"

#include <memory>
#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "dbus/bus.h"
#include "dbus/object_path.h"
#include "device/bluetooth/bluetooth_adapter_bluez.h"
#include "device/bluetooth/dbus/bluetooth_le_advertising_manager_client.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace {

void UnregisterFailure(device::BluetoothAdvertisement::ErrorCode error) {
  LOG(ERROR)
      << "BluetoothAdvertisementBlueZ::Unregister failed with error code = "
      << error;
}

device::BluetoothAdvertisement::ErrorCode GetErrorCodeFromErrorStrings(
    const std::string& error_name,
    const std::string& error_message) {
  if (error_name == bluetooth_advertising_manager::kErrorFailed ||
      error_name == bluetooth_advertising_manager::kErrorAlreadyExists) {
    return device::BluetoothAdvertisement::ErrorCode::
        ERROR_ADVERTISEMENT_ALREADY_EXISTS;
  } else if (error_name ==
             bluetooth_advertising_manager::kErrorInvalidArguments) {
    return device::BluetoothAdvertisement::ErrorCode::
        ERROR_ADVERTISEMENT_INVALID_LENGTH;
  } else if (error_name == bluetooth_advertising_manager::kErrorDoesNotExist) {
    return device::BluetoothAdvertisement::ErrorCode::
        ERROR_ADVERTISEMENT_DOES_NOT_EXIST;
  }
  return device::BluetoothAdvertisement::ErrorCode::
      INVALID_ADVERTISEMENT_ERROR_CODE;
}

void RegisterErrorCallbackConnector(
    const device::BluetoothAdapter::CreateAdvertisementErrorCallback&
        error_callback,
    const std::string& error_name,
    const std::string& error_message) {
  LOG(ERROR) << "Error while registering advertisement. error_name = "
             << error_name << ", error_message = " << error_message;
  error_callback.Run(GetErrorCodeFromErrorStrings(error_name, error_message));
}

void UnregisterErrorCallbackConnector(
    const device::BluetoothAdapter::CreateAdvertisementErrorCallback&
        error_callback,
    const std::string& error_name,
    const std::string& error_message) {
  LOG(WARNING) << "Error while unregistering advertisement. error_name = "
               << error_name << ", error_message = " << error_message;
  error_callback.Run(GetErrorCodeFromErrorStrings(error_name, error_message));
}

}  // namespace

namespace bluez {

BluetoothAdvertisementBlueZ::BluetoothAdvertisementBlueZ(
    std::unique_ptr<device::BluetoothAdvertisement::Data> data,
    scoped_refptr<BluetoothAdapterBlueZ> adapter)
    : adapter_(adapter) {
  // Generate a new object path - make sure that we strip any -'s from the
  // generated GUID string since object paths can only contain alphanumeric
  // characters and _ characters.
  std::string GuidString = base::GenerateGUID();
  base::RemoveChars(GuidString, "-", &GuidString);
  dbus::ObjectPath advertisement_object_path =
      dbus::ObjectPath("/org/chromium/bluetooth_advertisement/" + GuidString);

  DCHECK(bluez::BluezDBusManager::Get());
  provider_ = bluez::BluetoothLEAdvertisementServiceProvider::Create(
      bluez::BluezDBusManager::Get()->GetSystemBus(), advertisement_object_path,
      this,
      static_cast<
          bluez::BluetoothLEAdvertisementServiceProvider::AdvertisementType>(
          data->type()),
      data->service_uuids(), data->manufacturer_data(), data->solicit_uuids(),
      data->service_data());
}

void BluetoothAdvertisementBlueZ::Register(
    const base::Closure& success_callback,
    const device::BluetoothAdapter::CreateAdvertisementErrorCallback&
        error_callback) {
  DCHECK(bluez::BluezDBusManager::Get());
  bluez::BluezDBusManager::Get()
      ->GetBluetoothLEAdvertisingManagerClient()
      ->RegisterAdvertisement(
          adapter_->object_path(), provider_->object_path(), success_callback,
          base::Bind(&RegisterErrorCallbackConnector, error_callback));
}

BluetoothAdvertisementBlueZ::~BluetoothAdvertisementBlueZ() {
  Unregister(base::Bind(&base::DoNothing), base::Bind(&UnregisterFailure));
}

void BluetoothAdvertisementBlueZ::Unregister(
    const SuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  // If we don't have a provider, that means we have already been unregistered,
  // return an error.
  if (!provider_) {
    error_callback.Run(device::BluetoothAdvertisement::ErrorCode::
                           ERROR_ADVERTISEMENT_DOES_NOT_EXIST);
    return;
  }

  DCHECK(bluez::BluezDBusManager::Get());
  bluez::BluezDBusManager::Get()
      ->GetBluetoothLEAdvertisingManagerClient()
      ->UnregisterAdvertisement(
          adapter_->object_path(), provider_->object_path(), success_callback,
          base::Bind(&UnregisterErrorCallbackConnector, error_callback));
  provider_.reset();
}

void BluetoothAdvertisementBlueZ::Released() {
  LOG(WARNING) << "Advertisement released.";
  provider_.reset();
  FOR_EACH_OBSERVER(BluetoothAdvertisement::Observer, observers_,
                    AdvertisementReleased(this));
}

}  // namespace bluez
