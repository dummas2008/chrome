// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_adapter.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "build/build_config.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_discovery_session.h"
#include "device/bluetooth/test/bluetooth_test.h"
#include "device/bluetooth/test/test_bluetooth_adapter_observer.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_ANDROID)
#include "device/bluetooth/test/bluetooth_test_android.h"
#elif defined(OS_MACOSX)
#include "device/bluetooth/test/bluetooth_test_mac.h"
#elif defined(OS_WIN)
#include "device/bluetooth/test/bluetooth_test_win.h"
#endif

using device::BluetoothDevice;

namespace device {

class TestBluetoothAdapter : public BluetoothAdapter {
 public:
  TestBluetoothAdapter() {
  }

  std::string GetAddress() const override { return ""; }

  std::string GetName() const override { return ""; }

  void SetName(const std::string& name,
               const base::Closure& callback,
               const ErrorCallback& error_callback) override {}

  bool IsInitialized() const override { return false; }

  bool IsPresent() const override { return false; }

  bool IsPowered() const override { return false; }

  void SetPowered(bool powered,
                  const base::Closure& callback,
                  const ErrorCallback& error_callback) override {}

  bool IsDiscoverable() const override { return false; }

  void SetDiscoverable(bool discoverable,
                       const base::Closure& callback,
                       const ErrorCallback& error_callback) override {}

  bool IsDiscovering() const override { return false; }

  void StartDiscoverySessionWithFilter(
      std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter,
      const DiscoverySessionCallback& callback,
      const ErrorCallback& error_callback) override {
    OnStartDiscoverySession(std::move(discovery_filter), callback);
  }

  void StartDiscoverySession(const DiscoverySessionCallback& callback,
                             const ErrorCallback& error_callback) override {}

  UUIDList GetUUIDs() const override { return UUIDList(); }

  void CreateRfcommService(
      const BluetoothUUID& uuid,
      const ServiceOptions& options,
      const CreateServiceCallback& callback,
      const CreateServiceErrorCallback& error_callback) override {}

  void CreateL2capService(
      const BluetoothUUID& uuid,
      const ServiceOptions& options,
      const CreateServiceCallback& callback,
      const CreateServiceErrorCallback& error_callback) override {}

  void RegisterAudioSink(
      const BluetoothAudioSink::Options& options,
      const AcquiredCallback& callback,
      const BluetoothAudioSink::ErrorCallback& error_callback) override {}

  void RegisterAdvertisement(
      std::unique_ptr<BluetoothAdvertisement::Data> advertisement_data,
      const CreateAdvertisementCallback& callback,
      const CreateAdvertisementErrorCallback& error_callback) override {}

  void TestErrorCallback() {}

  ScopedVector<BluetoothDiscoverySession> discovery_sessions_;

  void TestOnStartDiscoverySession(
      std::unique_ptr<device::BluetoothDiscoverySession> discovery_session) {
    discovery_sessions_.push_back(std::move(discovery_session));
  }

  void CleanupSessions() { discovery_sessions_.clear(); }

  void InjectFilteredSession(
      std::unique_ptr<device::BluetoothDiscoveryFilter> discovery_filter) {
    StartDiscoverySessionWithFilter(
        std::move(discovery_filter),
        base::Bind(&TestBluetoothAdapter::TestOnStartDiscoverySession,
                   base::Unretained(this)),
        base::Bind(&TestBluetoothAdapter::TestErrorCallback,
                   base::Unretained(this)));
  }

 protected:
  ~TestBluetoothAdapter() override {}

  void AddDiscoverySession(
      BluetoothDiscoveryFilter* discovery_filter,
      const base::Closure& callback,
      const DiscoverySessionErrorCallback& error_callback) override {}

  void RemoveDiscoverySession(
      BluetoothDiscoveryFilter* discovery_filter,
      const base::Closure& callback,
      const DiscoverySessionErrorCallback& error_callback) override {}

  void SetDiscoveryFilter(
      std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter,
      const base::Closure& callback,
      const DiscoverySessionErrorCallback& error_callback) override {}

  void RemovePairingDelegateInternal(
      BluetoothDevice::PairingDelegate* pairing_delegate) override {}
};

class TestPairingDelegate : public BluetoothDevice::PairingDelegate {
 public:
  void RequestPinCode(BluetoothDevice* device) override {}
  void RequestPasskey(BluetoothDevice* device) override {}
  void DisplayPinCode(BluetoothDevice* device,
                      const std::string& pincode) override {}
  void DisplayPasskey(BluetoothDevice* device, uint32_t passkey) override {}
  void KeysEntered(BluetoothDevice* device, uint32_t entered) override {}
  void ConfirmPasskey(BluetoothDevice* device, uint32_t passkey) override {}
  void AuthorizePairing(BluetoothDevice* device) override {}
};

TEST(BluetoothAdapterTest, NoDefaultPairingDelegate) {
  scoped_refptr<BluetoothAdapter> adapter = new TestBluetoothAdapter();

  // Verify that when there is no registered pairing delegate, NULL is returned.
  EXPECT_TRUE(adapter->DefaultPairingDelegate() == NULL);
}

TEST(BluetoothAdapterTest, OneDefaultPairingDelegate) {
  scoped_refptr<BluetoothAdapter> adapter = new TestBluetoothAdapter();

  // Verify that when there is one registered pairing delegate, it is returned.
  TestPairingDelegate delegate;

  adapter->AddPairingDelegate(&delegate,
                              BluetoothAdapter::PAIRING_DELEGATE_PRIORITY_LOW);

  EXPECT_EQ(&delegate, adapter->DefaultPairingDelegate());
}

TEST(BluetoothAdapterTest, SamePriorityDelegates) {
  scoped_refptr<BluetoothAdapter> adapter = new TestBluetoothAdapter();

  // Verify that when there are two registered pairing delegates of the same
  // priority, the first one registered is returned.
  TestPairingDelegate delegate1, delegate2;

  adapter->AddPairingDelegate(&delegate1,
                              BluetoothAdapter::PAIRING_DELEGATE_PRIORITY_LOW);
  adapter->AddPairingDelegate(&delegate2,
                              BluetoothAdapter::PAIRING_DELEGATE_PRIORITY_LOW);

  EXPECT_EQ(&delegate1, adapter->DefaultPairingDelegate());

  // After unregistering the first, the second can be returned.
  adapter->RemovePairingDelegate(&delegate1);

  EXPECT_EQ(&delegate2, adapter->DefaultPairingDelegate());
}

TEST(BluetoothAdapterTest, HighestPriorityDelegate) {
  scoped_refptr<BluetoothAdapter> adapter = new TestBluetoothAdapter();

  // Verify that when there are two registered pairing delegates, the one with
  // the highest priority is returned.
  TestPairingDelegate delegate1, delegate2;

  adapter->AddPairingDelegate(&delegate1,
                              BluetoothAdapter::PAIRING_DELEGATE_PRIORITY_LOW);
  adapter->AddPairingDelegate(&delegate2,
                              BluetoothAdapter::PAIRING_DELEGATE_PRIORITY_HIGH);

  EXPECT_EQ(&delegate2, adapter->DefaultPairingDelegate());
}

TEST(BluetoothAdapterTest, UnregisterDelegate) {
  scoped_refptr<BluetoothAdapter> adapter = new TestBluetoothAdapter();

  // Verify that after unregistering a delegate, NULL is returned.
  TestPairingDelegate delegate;

  adapter->AddPairingDelegate(&delegate,
                              BluetoothAdapter::PAIRING_DELEGATE_PRIORITY_LOW);
  adapter->RemovePairingDelegate(&delegate);

  EXPECT_TRUE(adapter->DefaultPairingDelegate() == NULL);
}

TEST(BluetoothAdapterTest, GetMergedDiscoveryFilterEmpty) {
  scoped_refptr<BluetoothAdapter> adapter = new TestBluetoothAdapter();
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter;

  discovery_filter = adapter->GetMergedDiscoveryFilter();
  EXPECT_TRUE(discovery_filter.get() == nullptr);

  discovery_filter = adapter->GetMergedDiscoveryFilterMasked(nullptr);
  EXPECT_TRUE(discovery_filter.get() == nullptr);
}

TEST(BluetoothAdapterTest, GetMergedDiscoveryFilterRegular) {
  scoped_refptr<TestBluetoothAdapter> adapter = new TestBluetoothAdapter();
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter;

  // make sure adapter have one session wihout filtering.
  adapter->InjectFilteredSession(std::move(discovery_filter));

  // having one reglar session should result in no filter
  std::unique_ptr<BluetoothDiscoveryFilter> resulting_filter =
      adapter->GetMergedDiscoveryFilter();
  EXPECT_TRUE(resulting_filter.get() == nullptr);

  // omiting no filter when having one reglar session should result in no filter
  resulting_filter = adapter->GetMergedDiscoveryFilterMasked(nullptr);
  EXPECT_TRUE(resulting_filter.get() == nullptr);

  adapter->CleanupSessions();
}

TEST(BluetoothAdapterTest, GetMergedDiscoveryFilterRssi) {
  scoped_refptr<TestBluetoothAdapter> adapter = new TestBluetoothAdapter();
  int16_t resulting_rssi;
  uint16_t resulting_pathloss;
  std::unique_ptr<BluetoothDiscoveryFilter> resulting_filter;

  BluetoothDiscoveryFilter* df = new BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df->SetRSSI(-30);
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter(df);

  BluetoothDiscoveryFilter* df2 = new BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df2->SetRSSI(-65);
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter2(df2);

  // make sure adapter have one session wihout filtering.
  adapter->InjectFilteredSession(std::move(discovery_filter));

  // DO_NOTHING should have no impact
  resulting_filter = adapter->GetMergedDiscoveryFilter();
  resulting_filter->GetRSSI(&resulting_rssi);
  EXPECT_EQ(-30, resulting_rssi);

  // should not use df2 at all, as it's not associated with adapter yet
  resulting_filter = adapter->GetMergedDiscoveryFilterMasked(df2);
  resulting_filter->GetRSSI(&resulting_rssi);
  EXPECT_EQ(-30, resulting_rssi);

  adapter->InjectFilteredSession(std::move(discovery_filter2));

  // result of merging two rssi values should be lower one
  resulting_filter = adapter->GetMergedDiscoveryFilter();
  resulting_filter->GetRSSI(&resulting_rssi);
  EXPECT_EQ(-65, resulting_rssi);

  // ommit bigger value, result should stay same
  resulting_filter = adapter->GetMergedDiscoveryFilterMasked(df);
  resulting_filter->GetRSSI(&resulting_rssi);
  EXPECT_EQ(-65, resulting_rssi);

  // ommit lower value, result should change
  resulting_filter = adapter->GetMergedDiscoveryFilterMasked(df2);
  resulting_filter->GetRSSI(&resulting_rssi);
  EXPECT_EQ(-30, resulting_rssi);

  BluetoothDiscoveryFilter* df3 = new BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df3->SetPathloss(60);
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter3(df3);

  // when rssi and pathloss are merged, both should be cleared, becuase there is
  // no way to tell which filter will be more generic
  adapter->InjectFilteredSession(std::move(discovery_filter3));
  resulting_filter = adapter->GetMergedDiscoveryFilter();
  EXPECT_FALSE(resulting_filter->GetRSSI(&resulting_rssi));
  EXPECT_FALSE(resulting_filter->GetPathloss(&resulting_pathloss));

  adapter->CleanupSessions();
}

TEST(BluetoothAdapterTest, GetMergedDiscoveryFilterTransport) {
  scoped_refptr<TestBluetoothAdapter> adapter = new TestBluetoothAdapter();
  std::unique_ptr<BluetoothDiscoveryFilter> resulting_filter;

  BluetoothDiscoveryFilter* df = new BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC);
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter(df);

  BluetoothDiscoveryFilter* df2 = new BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter2(df2);

  adapter->InjectFilteredSession(std::move(discovery_filter));

  // Just one filter, make sure transport was properly rewritten
  resulting_filter = adapter->GetMergedDiscoveryFilter();
  EXPECT_EQ(BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC,
            resulting_filter->GetTransport());

  adapter->InjectFilteredSession(std::move(discovery_filter2));

  // Two filters, should have OR of both transport's
  resulting_filter = adapter->GetMergedDiscoveryFilter();
  EXPECT_EQ(BluetoothDiscoveryFilter::Transport::TRANSPORT_DUAL,
            resulting_filter->GetTransport());

  // When 1st filter is masked, 2nd filter transport should be returned.
  resulting_filter = adapter->GetMergedDiscoveryFilterMasked(df);
  EXPECT_EQ(BluetoothDiscoveryFilter::Transport::TRANSPORT_LE,
            resulting_filter->GetTransport());

  // When 2nd filter is masked, 1st filter transport should be returned.
  resulting_filter = adapter->GetMergedDiscoveryFilterMasked(df2);
  EXPECT_EQ(BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC,
            resulting_filter->GetTransport());

  BluetoothDiscoveryFilter* df3 = new BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df3->CopyFrom(BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_DUAL));
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter3(df3);

  // Merging empty filter in should result in empty filter
  adapter->InjectFilteredSession(std::move(discovery_filter3));
  resulting_filter = adapter->GetMergedDiscoveryFilter();
  EXPECT_TRUE(resulting_filter->IsDefault());

  adapter->CleanupSessions();
}

TEST(BluetoothAdapterTest, GetMergedDiscoveryFilterAllFields) {
  scoped_refptr<TestBluetoothAdapter> adapter = new TestBluetoothAdapter();
  int16_t resulting_rssi;
  std::set<device::BluetoothUUID> resulting_uuids;

  BluetoothDiscoveryFilter* df = new BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df->SetRSSI(-60);
  df->AddUUID(device::BluetoothUUID("1000"));
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter(df);

  BluetoothDiscoveryFilter* df2 = new BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df2->SetRSSI(-85);
  df2->SetTransport(BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df2->AddUUID(device::BluetoothUUID("1020"));
  df2->AddUUID(device::BluetoothUUID("1001"));
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter2(df2);

  BluetoothDiscoveryFilter* df3 = new BluetoothDiscoveryFilter(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df3->SetRSSI(-65);
  df3->SetTransport(BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC);
  df3->AddUUID(device::BluetoothUUID("1020"));
  df3->AddUUID(device::BluetoothUUID("1003"));
  std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter3(df3);

  // make sure adapter have one session wihout filtering.
  adapter->InjectFilteredSession(std::move(discovery_filter));
  adapter->InjectFilteredSession(std::move(discovery_filter2));
  adapter->InjectFilteredSession(std::move(discovery_filter3));

  std::unique_ptr<BluetoothDiscoveryFilter> resulting_filter =
      adapter->GetMergedDiscoveryFilter();
  resulting_filter->GetRSSI(&resulting_rssi);
  resulting_filter->GetUUIDs(resulting_uuids);
  EXPECT_TRUE(resulting_filter->GetTransport());
  EXPECT_EQ(BluetoothDiscoveryFilter::Transport::TRANSPORT_DUAL,
            resulting_filter->GetTransport());
  EXPECT_EQ(-85, resulting_rssi);
  EXPECT_EQ(4UL, resulting_uuids.size());
  EXPECT_TRUE(resulting_uuids.find(device::BluetoothUUID("1000")) !=
              resulting_uuids.end());
  EXPECT_TRUE(resulting_uuids.find(device::BluetoothUUID("1001")) !=
              resulting_uuids.end());
  EXPECT_TRUE(resulting_uuids.find(device::BluetoothUUID("1003")) !=
              resulting_uuids.end());
  EXPECT_TRUE(resulting_uuids.find(device::BluetoothUUID("1020")) !=
              resulting_uuids.end());

  resulting_filter = adapter->GetMergedDiscoveryFilterMasked(df);
  EXPECT_EQ(BluetoothDiscoveryFilter::Transport::TRANSPORT_DUAL,
            resulting_filter->GetTransport());

  adapter->CleanupSessions();
}

// TODO(scheib): Enable BluetoothTest fixture tests on all platforms.
#if defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)
TEST_F(BluetoothTest, ConstructDefaultAdapter) {
  InitWithDefaultAdapter();
  if (!adapter_->IsPresent()) {
    LOG(WARNING) << "Bluetooth adapter not present; skipping unit test.";
    return;
  }
  EXPECT_GT(adapter_->GetAddress().length(), 0u);
  EXPECT_GT(adapter_->GetName().length(), 0u);
  EXPECT_TRUE(adapter_->IsPresent());
  // Don't know on test machines if adapter will be powered or not, but
  // the call should be safe to make and consistent.
  EXPECT_EQ(adapter_->IsPowered(), adapter_->IsPowered());
  EXPECT_FALSE(adapter_->IsDiscoverable());
  EXPECT_FALSE(adapter_->IsDiscovering());
}
#endif  // defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)

// TODO(scheib): Enable BluetoothTest fixture tests on all platforms.
#if defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)
TEST_F(BluetoothTest, ConstructWithoutDefaultAdapter) {
  InitWithoutDefaultAdapter();
  EXPECT_EQ(adapter_->GetAddress(), "");
  EXPECT_EQ(adapter_->GetName(), "");
  EXPECT_FALSE(adapter_->IsPresent());
  EXPECT_FALSE(adapter_->IsPowered());
  EXPECT_FALSE(adapter_->IsDiscoverable());
  EXPECT_FALSE(adapter_->IsDiscovering());
}
#endif  // defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)

// TODO(scheib): Enable BluetoothTest fixture tests on all platforms.
#if defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)
TEST_F(BluetoothTest, ConstructFakeAdapter) {
  if (!PlatformSupportsLowEnergy()) {
    LOG(WARNING) << "Low Energy Bluetooth unavailable, skipping unit test.";
    return;
  }
  InitWithFakeAdapter();
  EXPECT_EQ(adapter_->GetAddress(), kTestAdapterAddress);
  EXPECT_EQ(adapter_->GetName(), kTestAdapterName);
  EXPECT_TRUE(adapter_->IsPresent());
  EXPECT_TRUE(adapter_->IsPowered());
  EXPECT_FALSE(adapter_->IsDiscoverable());
  EXPECT_FALSE(adapter_->IsDiscovering());
}
#endif  // defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)

// TODO(scheib): Enable BluetoothTest fixture tests on all platforms.
#if defined(OS_ANDROID)
// Starts and Stops a discovery session.
TEST_F(BluetoothTest, DiscoverySession) {
  InitWithFakeAdapter();
  EXPECT_FALSE(adapter_->IsDiscovering());

  StartLowEnergyDiscoverySession();
  EXPECT_EQ(1, callback_count_);
  EXPECT_EQ(0, error_callback_count_);
  EXPECT_TRUE(adapter_->IsDiscovering());
  ASSERT_EQ((size_t)1, discovery_sessions_.size());
  EXPECT_TRUE(discovery_sessions_[0]->IsActive());

  ResetEventCounts();
  discovery_sessions_[0]->Stop(GetCallback(Call::EXPECTED),
                               GetErrorCallback(Call::NOT_EXPECTED));
  EXPECT_FALSE(adapter_->IsDiscovering());
  EXPECT_FALSE(discovery_sessions_[0]->IsActive());
}
#endif  // defined(OS_ANDROID)

// Android only: this test is specific for Android and should not be
// enabled for other platforms.
#if defined(OS_ANDROID)
TEST_F(BluetoothTest, AdapterIllegalStateBeforeStartScan) {
  if (!PlatformSupportsLowEnergy()) {
    LOG(WARNING) << "Low Energy Bluetooth unavailable, skipping unit test.";
    return;
  }
  InitWithFakeAdapter();
  ForceIllegalStateException();
  StartLowEnergyDiscoverySessionExpectedToFail();
  EXPECT_EQ(0, callback_count_);
  EXPECT_EQ(1, error_callback_count_);
  EXPECT_FALSE(adapter_->IsDiscovering());
}
#endif  // defined(OS_ANDROID)

// Android only: this test is specific for Android and should not be
// enabled for other platforms.
#if defined(OS_ANDROID)
TEST_F(BluetoothTest, AdapterIllegalStateBeforeStopScan) {
  if (!PlatformSupportsLowEnergy()) {
    LOG(WARNING) << "Low Energy Bluetooth unavailable, skipping unit test.";
    return;
  }
  InitWithFakeAdapter();
  StartLowEnergyDiscoverySession();
  EXPECT_EQ(1, callback_count_);
  EXPECT_EQ(0, error_callback_count_);
  EXPECT_TRUE(adapter_->IsDiscovering());
  ForceIllegalStateException();
  discovery_sessions_[0]->Stop(GetCallback(Call::EXPECTED),
                               GetErrorCallback(Call::NOT_EXPECTED));
  EXPECT_FALSE(adapter_->IsDiscovering());
}
#endif  // defined(OS_ANDROID)

#if defined(OS_ANDROID) || defined(OS_MACOSX)
// Checks that discovery fails (instead of hanging) when permissions are denied.
TEST_F(BluetoothTest, NoPermissions) {
  if (!PlatformSupportsLowEnergy()) {
    LOG(WARNING) << "Low Energy Bluetooth unavailable, skipping unit test.";
    return;
  }
  InitWithFakeAdapter();
  TestBluetoothAdapterObserver observer(adapter_);

  if (!DenyPermission()) {
    // Platform always gives permission to scan.
    return;
  }

  StartLowEnergyDiscoverySessionExpectedToFail();

  EXPECT_EQ(0, callback_count_);
  EXPECT_EQ(1, error_callback_count_);
}
#endif  // defined(OS_ANDROID) || defined(OS_MACOSX)

#if defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)
// Discovers a device.
TEST_F(BluetoothTest, DiscoverLowEnergyDevice) {
  if (!PlatformSupportsLowEnergy()) {
    LOG(WARNING) << "Low Energy Bluetooth unavailable, skipping unit test.";
    return;
  }
  InitWithFakeAdapter();
  TestBluetoothAdapterObserver observer(adapter_);

  // Start discovery and find a device.
  StartLowEnergyDiscoverySession();
  DiscoverLowEnergyDevice(1);
  EXPECT_EQ(1, observer.device_added_count());
  BluetoothDevice* device = adapter_->GetDevice(observer.last_device_address());
  EXPECT_TRUE(device);
}
#endif  // defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)

#if defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)
// Discovers the same device multiple times.
TEST_F(BluetoothTest, DiscoverLowEnergyDeviceTwice) {
  if (!PlatformSupportsLowEnergy()) {
    LOG(WARNING) << "Low Energy Bluetooth unavailable, skipping unit test.";
    return;
  }
  InitWithFakeAdapter();
  TestBluetoothAdapterObserver observer(adapter_);

  // Start discovery and find a device.
  StartLowEnergyDiscoverySession();
  DiscoverLowEnergyDevice(1);
  EXPECT_EQ(1, observer.device_added_count());
  BluetoothDevice* device = adapter_->GetDevice(observer.last_device_address());
  EXPECT_TRUE(device);

  // Find the same device again. This should not create a new device object.
  observer.Reset();
  DiscoverLowEnergyDevice(1);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, observer.device_added_count());
  EXPECT_EQ(1u, adapter_->GetDevices().size());
}
#endif  // defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)

#if defined(OS_ANDROID) || defined(OS_MACOSX)
// Discovers a device, and then again with new Service UUIDs.
TEST_F(BluetoothTest, DiscoverLowEnergyDeviceWithUpdatedUUIDs) {
  if (!PlatformSupportsLowEnergy()) {
    LOG(WARNING) << "Low Energy Bluetooth unavailable, skipping unit test.";
    return;
  }
  InitWithFakeAdapter();
  TestBluetoothAdapterObserver observer(adapter_);

  // Start discovery and find a device.
  StartLowEnergyDiscoverySession();
  BluetoothDevice* device = DiscoverLowEnergyDevice(1);

  // Check the initial UUIDs:
  EXPECT_TRUE(
      ContainsValue(device->GetUUIDs(), BluetoothUUID(kTestUUIDGenericAccess)));
  EXPECT_FALSE(ContainsValue(device->GetUUIDs(),
                             BluetoothUUID(kTestUUIDImmediateAlert)));

  // Discover same device again with updated UUIDs:
  observer.Reset();
  DiscoverLowEnergyDevice(2);
  EXPECT_EQ(0, observer.device_added_count());
  EXPECT_EQ(1, observer.device_changed_count());
  EXPECT_EQ(1u, adapter_->GetDevices().size());
  EXPECT_EQ(device, observer.last_device());

  // Expect new AND old UUIDs:
  EXPECT_TRUE(
      ContainsValue(device->GetUUIDs(), BluetoothUUID(kTestUUIDGenericAccess)));
  EXPECT_TRUE(ContainsValue(device->GetUUIDs(),
                            BluetoothUUID(kTestUUIDImmediateAlert)));

  // Discover same device again with empty UUIDs:
  observer.Reset();
  DiscoverLowEnergyDevice(3);
  EXPECT_EQ(0, observer.device_added_count());
#if defined(OS_MACOSX)
  // TODO(scheib): Call DeviceChanged only if UUIDs change. crbug.com/547106
  EXPECT_EQ(1, observer.device_changed_count());
#else
  EXPECT_EQ(0, observer.device_changed_count());
#endif
  EXPECT_EQ(1u, adapter_->GetDevices().size());

  // Expect all UUIDs:
  EXPECT_EQ(4u, device->GetUUIDs().size());
}
#endif  // defined(OS_ANDROID) || defined(OS_MACOSX)

#if defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)
// Discovers multiple devices when addresses vary.
TEST_F(BluetoothTest, DiscoverMultipleLowEnergyDevices) {
  if (!PlatformSupportsLowEnergy()) {
    LOG(WARNING) << "Low Energy Bluetooth unavailable, skipping unit test.";
    return;
  }
  InitWithFakeAdapter();
  TestBluetoothAdapterObserver observer(adapter_);

  // Start discovery and find a device.
  StartLowEnergyDiscoverySession();
  DiscoverLowEnergyDevice(1);
  DiscoverLowEnergyDevice(4);
  EXPECT_EQ(2, observer.device_added_count());
  EXPECT_EQ(2u, adapter_->GetDevices().size());
}
#endif  // defined(OS_ANDROID) || defined(OS_MACOSX) || defined(OS_WIN)

#if defined(OS_ANDROID)
TEST_F(BluetoothTest, TogglePowerFakeAdapter) {
  InitWithFakeAdapter();
  TestBluetoothAdapterObserver observer(adapter_);

  ASSERT_TRUE(adapter_->IsPresent());
  ASSERT_TRUE(adapter_->IsPowered());
  EXPECT_EQ(0, observer.powered_changed_count());

  // Check if power can be turned off.
  adapter_->SetPowered(false, GetCallback(Call::EXPECTED),
                       GetErrorCallback(Call::NOT_EXPECTED));
  EXPECT_FALSE(adapter_->IsPowered());
  EXPECT_EQ(1, observer.powered_changed_count());

  // Check if power can be turned on again.
  adapter_->SetPowered(true, GetCallback(Call::EXPECTED),
                       GetErrorCallback(Call::NOT_EXPECTED));
  EXPECT_TRUE(adapter_->IsPowered());
  EXPECT_EQ(2, observer.powered_changed_count());
}
#endif  // defined(OS_ANDROID)

#if defined(OS_ANDROID)
TEST_F(BluetoothTest, TogglePowerBeforeScan) {
  InitWithFakeAdapter();
  TestBluetoothAdapterObserver observer(adapter_);

  ASSERT_TRUE(adapter_->IsPresent());
  ASSERT_TRUE(adapter_->IsPowered());
  EXPECT_EQ(0, observer.powered_changed_count());

  // Turn off adapter.
  adapter_->SetPowered(false, GetCallback(Call::EXPECTED),
                       GetErrorCallback(Call::NOT_EXPECTED));
  ASSERT_FALSE(adapter_->IsPowered());
  EXPECT_EQ(1, observer.powered_changed_count());

  // Try to perform a scan.
  StartLowEnergyDiscoverySessionExpectedToFail();

  // Turn on adapter.
  adapter_->SetPowered(true, GetCallback(Call::EXPECTED),
                       GetErrorCallback(Call::NOT_EXPECTED));
  ASSERT_TRUE(adapter_->IsPowered());
  EXPECT_EQ(2, observer.powered_changed_count());

  // Try to perform a scan again.
  ResetEventCounts();
  StartLowEnergyDiscoverySession();
  EXPECT_EQ(1, callback_count_);
  EXPECT_EQ(0, error_callback_count_);
  EXPECT_TRUE(adapter_->IsDiscovering());
  ASSERT_EQ((size_t)1, discovery_sessions_.size());
  EXPECT_TRUE(discovery_sessions_[0]->IsActive());
}
#endif  // defined(OS_ANDROID)

}  // namespace device
