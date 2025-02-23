// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_BLUETOOTH_REMOTE_GATT_CHARACTERISTIC_ANDROID_H_
#define DEVICE_BLUETOOTH_BLUETOOTH_REMOTE_GATT_CHARACTERISTIC_ANDROID_H_

#include <stdint.h>

#include <memory>

#include "base/android/jni_android.h"
#include "base/containers/scoped_ptr_hash_map.h"
#include "base/macros.h"
#include "device/bluetooth/bluetooth_gatt_characteristic.h"

namespace device {

class BluetoothAdapterAndroid;
class BluetoothRemoteGattDescriptorAndroid;
class BluetoothRemoteGattServiceAndroid;

// BluetoothRemoteGattCharacteristicAndroid along with its owned Java class
// org.chromium.device.bluetooth.ChromeBluetoothRemoteGattCharacteristic
// implement BluetootGattCharacteristic.
//
// TODO(crbug.com/551634): When notifications are enabled characteristic updates
// should call observers' GattCharacteristicValueChanged.
class DEVICE_BLUETOOTH_EXPORT BluetoothRemoteGattCharacteristicAndroid
    : public BluetoothGattCharacteristic {
 public:
  // Create a BluetoothRemoteGattCharacteristicAndroid instance and associated
  // Java
  // ChromeBluetoothRemoteGattCharacteristic using the provided
  // |bluetooth_gatt_characteristic_wrapper|.
  //
  // The ChromeBluetoothRemoteGattCharacteristic instance will hold a Java
  // reference
  // to |bluetooth_gatt_characteristic_wrapper|.
  static std::unique_ptr<BluetoothRemoteGattCharacteristicAndroid> Create(
      BluetoothAdapterAndroid* adapter,
      BluetoothRemoteGattServiceAndroid* service,
      const std::string& instance_id,
      jobject /* BluetoothGattCharacteristicWrapper */
      bluetooth_gatt_characteristic_wrapper,
      jobject /* ChromeBluetoothDevice */ chrome_bluetooth_device);

  ~BluetoothRemoteGattCharacteristicAndroid() override;

  // Register C++ methods exposed to Java using JNI.
  static bool RegisterJNI(JNIEnv* env);

  // Returns the associated ChromeBluetoothRemoteGattCharacteristic Java object.
  base::android::ScopedJavaLocalRef<jobject> GetJavaObject();

  // BluetoothGattCharacteristic interface:
  std::string GetIdentifier() const override;
  BluetoothUUID GetUUID() const override;
  bool IsLocal() const override;
  const std::vector<uint8_t>& GetValue() const override;
  BluetoothGattService* GetService() const override;
  Properties GetProperties() const override;
  Permissions GetPermissions() const override;
  bool IsNotifying() const override;
  std::vector<BluetoothGattDescriptor*> GetDescriptors() const override;
  BluetoothGattDescriptor* GetDescriptor(
      const std::string& identifier) const override;
  bool AddDescriptor(BluetoothGattDescriptor* descriptor) override;
  bool UpdateValue(const std::vector<uint8_t>& value) override;
  void StartNotifySession(const NotifySessionCallback& callback,
                          const ErrorCallback& error_callback) override;
  void ReadRemoteCharacteristic(const ValueCallback& callback,
                                const ErrorCallback& error_callback) override;
  void WriteRemoteCharacteristic(const std::vector<uint8_t>& new_value,
                                 const base::Closure& callback,
                                 const ErrorCallback& error_callback) override;

  // Called when StartNotifySession operation succeeds.
  void OnStartNotifySessionSuccess();

  // Called when StartNotifySession operation fails.
  void OnStartNotifySessionError(BluetoothGattService::GattErrorCode error);

  // Called when value changed event occurs.
  void OnChanged(JNIEnv* env,
                 const base::android::JavaParamRef<jobject>& jcaller,
                 const base::android::JavaParamRef<jbyteArray>& value);

  // Called when Read operation completes.
  void OnRead(JNIEnv* env,
              const base::android::JavaParamRef<jobject>& jcaller,
              int32_t status,
              const base::android::JavaParamRef<jbyteArray>& value);

  // Called when Write operation completes.
  void OnWrite(JNIEnv* env,
               const base::android::JavaParamRef<jobject>& jcaller,
               int32_t status);

  // Creates a Bluetooth GATT descriptor object and adds it to |descriptors_|,
  // DCHECKing that it has not already been created.
  void CreateGattRemoteDescriptor(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& caller,
      const base::android::JavaParamRef<jstring>& instanceId,
      const base::android::JavaParamRef<
          jobject>& /* BluetoothGattDescriptorWrapper */
      bluetooth_gatt_descriptor_wrapper,
      const base::android::JavaParamRef<
          jobject>& /* ChromeBluetoothCharacteristic */
      chrome_bluetooth_characteristic);

 private:
  BluetoothRemoteGattCharacteristicAndroid(
      BluetoothAdapterAndroid* adapter,
      BluetoothRemoteGattServiceAndroid* service,
      const std::string& instance_id);

  // Populates |descriptors_| from Java objects if necessary.
  void EnsureDescriptorsCreated() const;

  // The adapter and service associated with this characteristic. It's ok to
  // store a raw pointers here since they indirectly own this instance.
  BluetoothAdapterAndroid* adapter_;
  BluetoothRemoteGattServiceAndroid* service_;

  // Java object
  // org.chromium.device.bluetooth.ChromeBluetoothRemoteGattCharacteristic.
  base::android::ScopedJavaGlobalRef<jobject> j_characteristic_;

  // Adapter unique instance ID.
  std::string instance_id_;

  // StartNotifySession callbacks and pending state.
  typedef std::pair<NotifySessionCallback, ErrorCallback>
      PendingStartNotifyCall;
  std::vector<PendingStartNotifyCall> pending_start_notify_calls_;

  // ReadRemoteCharacteristic callbacks and pending state.
  bool read_pending_ = false;
  ValueCallback read_callback_;
  ErrorCallback read_error_callback_;

  // WriteRemoteCharacteristic callbacks and pending state.
  bool write_pending_ = false;
  base::Closure write_callback_;
  ErrorCallback write_error_callback_;

  std::vector<uint8_t> value_;

  // Map of descriptors, keyed by descriptor identifier.
  base::ScopedPtrHashMap<std::string,
                         std::unique_ptr<BluetoothRemoteGattDescriptorAndroid>>
      descriptors_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothRemoteGattCharacteristicAndroid);
};

}  // namespace device

#endif  // DEVICE_BLUETOOTH_BLUETOOTH_REMOTE_GATT_CHARACTERISTIC_ANDROID_H_
