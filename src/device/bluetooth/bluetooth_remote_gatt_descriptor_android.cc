// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_remote_gatt_descriptor_android.h"

#include <memory>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "device/bluetooth/bluetooth_gatt_notify_session_android.h"
#include "device/bluetooth/bluetooth_remote_gatt_service_android.h"
#include "jni/ChromeBluetoothRemoteGattDescriptor_jni.h"

using base::android::AttachCurrentThread;

namespace device {

// static
std::unique_ptr<BluetoothRemoteGattDescriptorAndroid>
BluetoothRemoteGattDescriptorAndroid::Create(
    const std::string& instance_id,
    jobject /* BluetoothGattDescriptorWrapper */
    bluetooth_gatt_descriptor_wrapper,
    jobject /* chromeBluetoothDevice */
    chrome_bluetooth_device) {
  std::unique_ptr<BluetoothRemoteGattDescriptorAndroid> descriptor(
      new BluetoothRemoteGattDescriptorAndroid(instance_id));

  descriptor->j_descriptor_.Reset(
      Java_ChromeBluetoothRemoteGattDescriptor_create(
          AttachCurrentThread(), reinterpret_cast<intptr_t>(descriptor.get()),
          bluetooth_gatt_descriptor_wrapper, chrome_bluetooth_device));

  return descriptor;
}

BluetoothRemoteGattDescriptorAndroid::~BluetoothRemoteGattDescriptorAndroid() {
  Java_ChromeBluetoothRemoteGattDescriptor_onBluetoothRemoteGattDescriptorAndroidDestruction(
      AttachCurrentThread(), j_descriptor_.obj());
}

// static
bool BluetoothRemoteGattDescriptorAndroid::RegisterJNI(JNIEnv* env) {
  return RegisterNativesImpl(
      env);  // Generated in ChromeBluetoothRemoteGattDescriptor_jni.h
}

base::android::ScopedJavaLocalRef<jobject>
BluetoothRemoteGattDescriptorAndroid::GetJavaObject() {
  return base::android::ScopedJavaLocalRef<jobject>(j_descriptor_);
}

std::string BluetoothRemoteGattDescriptorAndroid::GetIdentifier() const {
  return instance_id_;
}

BluetoothUUID BluetoothRemoteGattDescriptorAndroid::GetUUID() const {
  return device::BluetoothUUID(
      ConvertJavaStringToUTF8(Java_ChromeBluetoothRemoteGattDescriptor_getUUID(
          AttachCurrentThread(), j_descriptor_.obj())));
}

bool BluetoothRemoteGattDescriptorAndroid::IsLocal() const {
  return false;
}

const std::vector<uint8_t>& BluetoothRemoteGattDescriptorAndroid::GetValue()
    const {
  return value_;
}

BluetoothGattCharacteristic*
BluetoothRemoteGattDescriptorAndroid::GetCharacteristic() const {
  NOTIMPLEMENTED();
  return nullptr;
}

BluetoothGattCharacteristic::Permissions
BluetoothRemoteGattDescriptorAndroid::GetPermissions() const {
  NOTIMPLEMENTED();
  return 0;
}

void BluetoothRemoteGattDescriptorAndroid::ReadRemoteDescriptor(
    const ValueCallback& callback,
    const ErrorCallback& error_callback) {
  if (read_pending_ || write_pending_) {
    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(error_callback,
                              BluetoothGattService::GATT_ERROR_IN_PROGRESS));
    return;
  }

  if (!Java_ChromeBluetoothRemoteGattDescriptor_readRemoteDescriptor(
          AttachCurrentThread(), j_descriptor_.obj())) {
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(error_callback,
                   BluetoothRemoteGattServiceAndroid::GATT_ERROR_FAILED));
    return;
  }

  read_pending_ = true;
  read_callback_ = callback;
  read_error_callback_ = error_callback;
}

void BluetoothRemoteGattDescriptorAndroid::WriteRemoteDescriptor(
    const std::vector<uint8_t>& new_value,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  if (read_pending_ || write_pending_) {
    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(error_callback,
                              BluetoothGattService::GATT_ERROR_IN_PROGRESS));
    return;
  }

  JNIEnv* env = AttachCurrentThread();
  if (!Java_ChromeBluetoothRemoteGattDescriptor_writeRemoteDescriptor(
          env, j_descriptor_.obj(),
          base::android::ToJavaByteArray(env, new_value).obj())) {
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(error_callback,
                   BluetoothRemoteGattServiceAndroid::GATT_ERROR_FAILED));
    return;
  }

  write_pending_ = true;
  write_callback_ = callback;
  write_error_callback_ = error_callback;
}

void BluetoothRemoteGattDescriptorAndroid::OnRead(
    JNIEnv* env,
    const JavaParamRef<jobject>& jcaller,
    int32_t status,
    const JavaParamRef<jbyteArray>& value) {
  read_pending_ = false;

  // Clear callbacks before calling to avoid reentrancy issues.
  ValueCallback read_callback = read_callback_;
  ErrorCallback read_error_callback = read_error_callback_;
  read_callback_.Reset();
  read_error_callback_.Reset();

  if (status == 0  // android.bluetooth.BluetoothGatt.GATT_SUCCESS
      && !read_callback.is_null()) {
    base::android::JavaByteArrayToByteVector(env, value, &value_);
    read_callback.Run(value_);
    // TODO(https://crbug.com/584369): Call GattDescriptorValueChanged.
  } else if (!read_error_callback.is_null()) {
    read_error_callback.Run(
        BluetoothRemoteGattServiceAndroid::GetGattErrorCode(status));
  }
}

void BluetoothRemoteGattDescriptorAndroid::OnWrite(
    JNIEnv* env,
    const JavaParamRef<jobject>& jcaller,
    int32_t status) {
  write_pending_ = false;

  // Clear callbacks before calling to avoid reentrancy issues.
  base::Closure write_callback = write_callback_;
  ErrorCallback write_error_callback = write_error_callback_;
  write_callback_.Reset();
  write_error_callback_.Reset();

  if (status == 0  // android.bluetooth.BluetoothGatt.GATT_SUCCESS
      && !write_callback.is_null()) {
    write_callback.Run();
    // TODO(https://crbug.com/584369): Call GattDescriptorValueChanged.
  } else if (!write_error_callback.is_null()) {
    write_error_callback.Run(
        BluetoothRemoteGattServiceAndroid::GetGattErrorCode(status));
  }
}

BluetoothRemoteGattDescriptorAndroid::BluetoothRemoteGattDescriptorAndroid(
    const std::string& instance_id)
    : instance_id_(instance_id) {}

}  // namespace device
