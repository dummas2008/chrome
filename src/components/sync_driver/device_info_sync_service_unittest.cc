// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_driver/device_info_sync_service.h"

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "base/message_loop/message_loop.h"
#include "components/sync_driver/local_device_info_provider_mock.h"
#include "sync/api/sync_change.h"
#include "sync/api/sync_change_processor.h"
#include "sync/api/sync_change_processor_wrapper_for_test.h"
#include "sync/api/sync_error_factory_mock.h"
#include "sync/internal_api/public/attachments/attachment_service_proxy_for_test.h"
#include "sync/util/time.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Time;
using base::TimeDelta;
using syncer::AttachmentIdList;
using syncer::AttachmentServiceProxyForTest;
using syncer::ModelType;
using syncer::SyncChange;
using syncer::SyncChangeList;
using syncer::SyncChangeProcessor;
using syncer::SyncChangeProcessorWrapperForTest;
using syncer::SyncData;
using syncer::SyncDataList;
using syncer::SyncError;
using syncer::SyncErrorFactory;
using syncer::SyncErrorFactoryMock;
using syncer::SyncMergeResult;

namespace sync_driver {

namespace {

class TestChangeProcessor : public SyncChangeProcessor {
 public:
  TestChangeProcessor() {}
  ~TestChangeProcessor() override {}

  // SyncChangeProcessor implementation.
  // Store a copy of all the changes passed in so we can examine them later.
  SyncError ProcessSyncChanges(const tracked_objects::Location& from_here,
                               const SyncChangeList& change_list) override {
    change_list_ = change_list;
    return SyncError();
  }

  // This method isn't used in these tests.
  SyncDataList GetAllSyncData(ModelType type) const override {
    return SyncDataList();
  }

  size_t change_list_size() const { return change_list_.size(); }

  SyncChange::SyncChangeType change_type_at(size_t index) const {
    CHECK_LT(index, change_list_size());
    return change_list_[index].change_type();
  }

  const sync_pb::DeviceInfoSpecifics& device_info_at(size_t index) const {
    CHECK_LT(index, change_list_size());
    return change_list_[index].sync_data().GetSpecifics().device_info();
  }

  const std::string& cache_guid_at(size_t index) const {
    return device_info_at(index).cache_guid();
  }

  const std::string& client_name_at(size_t index) const {
    return device_info_at(index).client_name();
  }

 private:
  SyncChangeList change_list_;
};

}  // namespace

class DeviceInfoSyncServiceTest : public testing::Test,
                                  public DeviceInfoTracker::Observer {
 public:
  DeviceInfoSyncServiceTest() : num_device_info_changed_callbacks_(0) {}
  ~DeviceInfoSyncServiceTest() override {}

  void SetUp() override {
    local_device_.reset(new LocalDeviceInfoProviderMock(
        "guid_1",
        "client_1",
        "Chromium 10k",
        "Chrome 10k",
        sync_pb::SyncEnums_DeviceType_TYPE_LINUX,
        "device_id"));
    sync_service_.reset(new DeviceInfoSyncService(local_device_.get()));
    sync_processor_.reset(new TestChangeProcessor());
    // Register observer
    sync_service_->AddObserver(this);
  }

  void TearDown() override { sync_service_->RemoveObserver(this); }

  void OnDeviceInfoChange() override { num_device_info_changed_callbacks_++; }

  scoped_ptr<SyncChangeProcessor> PassProcessor() {
    return scoped_ptr<SyncChangeProcessor>(
        new SyncChangeProcessorWrapperForTest(sync_processor_.get()));
  }

  scoped_ptr<SyncErrorFactory> CreateAndPassSyncErrorFactory() {
    return scoped_ptr<SyncErrorFactory>(new SyncErrorFactoryMock());
  }

  // Default |last_updated_timestamp| to now to avoid pulse update on merge.
  SyncData CreateRemoteData(const std::string& client_id,
                            const std::string& client_name,
                            Time last_updated_timestamp = Time::Now()) {
    sync_pb::EntitySpecifics entity;
    sync_pb::DeviceInfoSpecifics& specifics = *entity.mutable_device_info();

    specifics.set_cache_guid(client_id);
    specifics.set_client_name(client_name);
    specifics.set_chrome_version("Chromium 10k");
    specifics.set_sync_user_agent("Chrome 10k");
    specifics.set_device_type(sync_pb::SyncEnums_DeviceType_TYPE_LINUX);
    specifics.set_signin_scoped_device_id("device_id");
    specifics.set_last_updated_timestamp(
        syncer::TimeToProtoTime(last_updated_timestamp));

    return SyncData::CreateRemoteData(1, entity, Time(), AttachmentIdList(),
                                      AttachmentServiceProxyForTest::Create());
  }

  void AddInitialData(SyncDataList* sync_data_list,
                      const std::string& client_id,
                      const std::string& client_name) {
    SyncData sync_data = CreateRemoteData(client_id, client_name);
    sync_data_list->push_back(sync_data);
  }

  void AddChange(SyncChangeList* change_list,
                 SyncChange::SyncChangeType change_type,
                 const std::string& client_id,
                 const std::string& client_name) {
    SyncData sync_data = CreateRemoteData(client_id, client_name);
    SyncChange sync_change(FROM_HERE, change_type, sync_data);
    change_list->push_back(sync_change);
  }

 protected:
  // Private method wrappers through friend class.
  TimeDelta CalculatePulseDelay(const syncer::SyncData& data, const Time now) {
    return sync_service_->CalculatePulseDelay(data, now);
  }
  int CountActiveDevices(const Time now) {
    return sync_service_->CountActiveDevices(now);
  }
  void StoreSyncData(const std::string& client_id,
                     const syncer::SyncData& sync_data) {
    sync_service_->StoreSyncData(client_id, sync_data);
  }
  bool IsPulseTimerRunning() { return sync_service_->pulse_timer_.IsRunning(); }

  // Needs to be created for OneShotTimer to grab the current task runner.
  base::MessageLoop message_loop_;

  int num_device_info_changed_callbacks_;
  scoped_ptr<LocalDeviceInfoProviderMock> local_device_;
  scoped_ptr<DeviceInfoSyncService> sync_service_;
  scoped_ptr<TestChangeProcessor> sync_processor_;
};

namespace {

// Sync with empty initial data.
TEST_F(DeviceInfoSyncServiceTest, StartSyncEmptyInitialData) {
  EXPECT_FALSE(sync_service_->IsSyncing());

  SyncMergeResult merge_result =
      sync_service_->MergeDataAndStartSyncing(syncer::DEVICE_INFO,
                                              SyncDataList(),
                                              PassProcessor(),
                                              CreateAndPassSyncErrorFactory());

  EXPECT_TRUE(sync_service_->IsSyncing());
  EXPECT_EQ(0, merge_result.num_items_added());
  EXPECT_EQ(0, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(1, merge_result.num_items_before_association());
  EXPECT_EQ(1, merge_result.num_items_after_association());
  EXPECT_EQ(SyncChange::ACTION_ADD, sync_processor_->change_type_at(0));

  EXPECT_EQ(1U, sync_processor_->change_list_size());
  EXPECT_EQ("guid_1", sync_processor_->cache_guid_at(0));

  // Should have one device info corresponding to local device info.
  EXPECT_EQ(1U, sync_service_->GetAllSyncData(syncer::DEVICE_INFO).size());
  EXPECT_EQ(1U, sync_service_->GetAllDeviceInfo().size());
  EXPECT_TRUE(sync_service_->GetDeviceInfo("guid_1"));
  EXPECT_FALSE(sync_service_->GetDeviceInfo("guid_0"));
}

TEST_F(DeviceInfoSyncServiceTest, StopSyncing) {
  SyncMergeResult merge_result = sync_service_->MergeDataAndStartSyncing(
      syncer::DEVICE_INFO, SyncDataList(), PassProcessor(),
      CreateAndPassSyncErrorFactory());
  EXPECT_TRUE(sync_service_->IsSyncing());
  EXPECT_EQ(1, num_device_info_changed_callbacks_);
  EXPECT_TRUE(IsPulseTimerRunning());
  sync_service_->StopSyncing(syncer::DEVICE_INFO);
  EXPECT_FALSE(sync_service_->IsSyncing());
  EXPECT_EQ(2, num_device_info_changed_callbacks_);
  EXPECT_FALSE(IsPulseTimerRunning());
}

// Sync with initial data matching the local device data.
TEST_F(DeviceInfoSyncServiceTest, StartSyncMatchingInitialData) {
  SyncDataList sync_data;
  AddInitialData(&sync_data, "guid_1", "client_1");

  SyncMergeResult merge_result =
      sync_service_->MergeDataAndStartSyncing(syncer::DEVICE_INFO,
                                              sync_data,
                                              PassProcessor(),
                                              CreateAndPassSyncErrorFactory());
  EXPECT_EQ(0, merge_result.num_items_added());
  EXPECT_EQ(0, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(1, merge_result.num_items_before_association());
  EXPECT_EQ(1, merge_result.num_items_after_association());

  // No changes expected because the device info matches.
  EXPECT_EQ(0U, sync_processor_->change_list_size());

  EXPECT_EQ(1U, sync_service_->GetAllSyncData(syncer::DEVICE_INFO).size());
  EXPECT_EQ(1U, sync_service_->GetAllDeviceInfo().size());
  EXPECT_TRUE(sync_service_->GetDeviceInfo("guid_1"));
  EXPECT_FALSE(sync_service_->GetDeviceInfo("guid_0"));
}

// Sync with misc initial data.
TEST_F(DeviceInfoSyncServiceTest, StartSync) {
  SyncDataList sync_data;
  AddInitialData(&sync_data, "guid_2", "foo");
  AddInitialData(&sync_data, "guid_3", "bar");
  // This guid matches the local device but the client name is different.
  AddInitialData(&sync_data, "guid_1", "baz");

  SyncMergeResult merge_result =
      sync_service_->MergeDataAndStartSyncing(syncer::DEVICE_INFO,
                                              sync_data,
                                              PassProcessor(),
                                              CreateAndPassSyncErrorFactory());

  EXPECT_EQ(2, merge_result.num_items_added());
  EXPECT_EQ(1, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(1, merge_result.num_items_before_association());
  EXPECT_EQ(3, merge_result.num_items_after_association());

  EXPECT_EQ(1U, sync_processor_->change_list_size());
  EXPECT_EQ(SyncChange::ACTION_UPDATE, sync_processor_->change_type_at(0));
  EXPECT_EQ("client_1", sync_processor_->client_name_at(0));

  EXPECT_EQ(3U, sync_service_->GetAllSyncData(syncer::DEVICE_INFO).size());
  EXPECT_EQ(3U, sync_service_->GetAllDeviceInfo().size());
  EXPECT_TRUE(sync_service_->GetDeviceInfo("guid_1"));
  EXPECT_TRUE(sync_service_->GetDeviceInfo("guid_2"));
  EXPECT_TRUE(sync_service_->GetDeviceInfo("guid_3"));
  EXPECT_FALSE(sync_service_->GetDeviceInfo("guid_0"));
}

// Process sync change with ACTION_ADD.
// Verify callback.
TEST_F(DeviceInfoSyncServiceTest, ProcessAddChange) {
  EXPECT_EQ(0, num_device_info_changed_callbacks_);

  // Start with an empty initial data.
  SyncMergeResult merge_result =
      sync_service_->MergeDataAndStartSyncing(syncer::DEVICE_INFO,
                                              SyncDataList(),
                                              PassProcessor(),
                                              CreateAndPassSyncErrorFactory());
  // There should be only one item corresponding to the local device
  EXPECT_EQ(1, merge_result.num_items_after_association());
  EXPECT_EQ(1, num_device_info_changed_callbacks_);

  // Add a new device info with a non-matching guid.
  SyncChangeList change_list;
  AddChange(&change_list, SyncChange::ACTION_ADD, "guid_2", "foo");

  SyncError error = sync_service_->ProcessSyncChanges(FROM_HERE, change_list);
  EXPECT_FALSE(error.IsSet());
  EXPECT_EQ(2, num_device_info_changed_callbacks_);

  EXPECT_EQ(2U, sync_service_->GetAllDeviceInfo().size());

  EXPECT_TRUE(sync_service_->GetDeviceInfo("guid_1"));
  EXPECT_TRUE(sync_service_->GetDeviceInfo("guid_2"));
  EXPECT_FALSE(sync_service_->GetDeviceInfo("guid_0"));
}

// Process multiple sync change with ACTION_UPDATE and ACTION_ADD.
// Verify that callback is called multiple times.
TEST_F(DeviceInfoSyncServiceTest, ProcessMultipleChanges) {
  SyncDataList sync_data;
  AddInitialData(&sync_data, "guid_2", "foo");
  AddInitialData(&sync_data, "guid_3", "bar");

  SyncMergeResult merge_result =
      sync_service_->MergeDataAndStartSyncing(syncer::DEVICE_INFO,
                                              sync_data,
                                              PassProcessor(),
                                              CreateAndPassSyncErrorFactory());
  EXPECT_EQ(3, merge_result.num_items_after_association());
  // reset callbacks counter
  num_device_info_changed_callbacks_ = 0;

  SyncChangeList change_list;
  AddChange(&change_list, SyncChange::ACTION_UPDATE, "guid_2", "foo_2");

  SyncError error = sync_service_->ProcessSyncChanges(FROM_HERE, change_list);
  EXPECT_FALSE(error.IsSet());

  EXPECT_EQ(1, num_device_info_changed_callbacks_);
  EXPECT_EQ(3U, sync_service_->GetAllDeviceInfo().size());
  EXPECT_EQ("foo_2", sync_service_->GetDeviceInfo("guid_2")->client_name());

  change_list.clear();
  AddChange(&change_list, SyncChange::ACTION_UPDATE, "guid_3", "bar_3");
  AddChange(&change_list, SyncChange::ACTION_ADD, "guid_4", "baz_4");

  error = sync_service_->ProcessSyncChanges(FROM_HERE, change_list);
  EXPECT_FALSE(error.IsSet());

  EXPECT_EQ(2, num_device_info_changed_callbacks_);
  EXPECT_EQ(4U, sync_service_->GetAllDeviceInfo().size());
  EXPECT_EQ("bar_3", sync_service_->GetDeviceInfo("guid_3")->client_name());
  EXPECT_EQ("baz_4", sync_service_->GetDeviceInfo("guid_4")->client_name());
}

// Process update to the local device info and verify that it is ignored.
TEST_F(DeviceInfoSyncServiceTest, ProcessUpdateChangeMatchingLocalDevice) {
  SyncMergeResult merge_result =
      sync_service_->MergeDataAndStartSyncing(syncer::DEVICE_INFO,
                                              SyncDataList(),
                                              PassProcessor(),
                                              CreateAndPassSyncErrorFactory());
  EXPECT_EQ(1, merge_result.num_items_after_association());
  // reset callbacks counter
  num_device_info_changed_callbacks_ = 0;

  SyncChangeList change_list;
  AddChange(&change_list, SyncChange::ACTION_UPDATE, "guid_1", "foo_1");

  SyncError error = sync_service_->ProcessSyncChanges(FROM_HERE, change_list);
  EXPECT_FALSE(error.IsSet());
  // Callback shouldn't be sent in this case.
  EXPECT_EQ(0, num_device_info_changed_callbacks_);
  // Should still have the old local device Info.
  EXPECT_EQ(1U, sync_service_->GetAllDeviceInfo().size());
  EXPECT_EQ("client_1", sync_service_->GetDeviceInfo("guid_1")->client_name());
}

// Process sync change with ACTION_DELETE.
TEST_F(DeviceInfoSyncServiceTest, ProcessDeleteChange) {
  SyncDataList sync_data;
  AddInitialData(&sync_data, "guid_2", "foo");
  AddInitialData(&sync_data, "guid_3", "bar");

  SyncMergeResult merge_result =
      sync_service_->MergeDataAndStartSyncing(syncer::DEVICE_INFO,
                                              sync_data,
                                              PassProcessor(),
                                              CreateAndPassSyncErrorFactory());
  EXPECT_EQ(3, merge_result.num_items_after_association());
  // reset callbacks counter
  num_device_info_changed_callbacks_ = 0;

  SyncChangeList change_list;
  AddChange(&change_list, SyncChange::ACTION_DELETE, "guid_2", "foo_2");

  SyncError error = sync_service_->ProcessSyncChanges(FROM_HERE, change_list);
  EXPECT_FALSE(error.IsSet());

  EXPECT_EQ(1, num_device_info_changed_callbacks_);
  EXPECT_EQ(2U, sync_service_->GetAllDeviceInfo().size());
  EXPECT_FALSE(sync_service_->GetDeviceInfo("guid_2"));
}

// Process sync change with unexpected action.
TEST_F(DeviceInfoSyncServiceTest, ProcessInvalidChange) {
  SyncMergeResult merge_result =
      sync_service_->MergeDataAndStartSyncing(syncer::DEVICE_INFO,
                                              SyncDataList(),
                                              PassProcessor(),
                                              CreateAndPassSyncErrorFactory());
  EXPECT_EQ(1, merge_result.num_items_after_association());
  // reset callbacks counter
  num_device_info_changed_callbacks_ = 0;

  SyncChangeList change_list;
  AddChange(&change_list, (SyncChange::SyncChangeType)100, "guid_2", "foo_2");

  SyncError error = sync_service_->ProcessSyncChanges(FROM_HERE, change_list);
  EXPECT_TRUE(error.IsSet());

  // The number of callback should still be zero.
  EXPECT_EQ(0, num_device_info_changed_callbacks_);
  EXPECT_EQ(1U, sync_service_->GetAllDeviceInfo().size());
}

// Process sync change after unsubscribing from notifications.
TEST_F(DeviceInfoSyncServiceTest, ProcessChangesAfterUnsubscribing) {
  SyncMergeResult merge_result =
      sync_service_->MergeDataAndStartSyncing(syncer::DEVICE_INFO,
                                              SyncDataList(),
                                              PassProcessor(),
                                              CreateAndPassSyncErrorFactory());
  EXPECT_EQ(1, merge_result.num_items_after_association());
  // reset callbacks counter
  num_device_info_changed_callbacks_ = 0;

  SyncChangeList change_list;
  AddChange(&change_list, SyncChange::ACTION_ADD, "guid_2", "foo_2");

  // Unsubscribe observer before processing changes.
  sync_service_->RemoveObserver(this);

  SyncError error = sync_service_->ProcessSyncChanges(FROM_HERE, change_list);
  EXPECT_FALSE(error.IsSet());

  // The number of callback should still be zero.
  EXPECT_EQ(0, num_device_info_changed_callbacks_);
}

// While the initial data will match the current device, the last modified time
// should be greater than the threshold and cause an update anyways.
TEST_F(DeviceInfoSyncServiceTest, StartSyncMatchingButStale) {
  SyncDataList sync_data;
  sync_data.push_back(CreateRemoteData("guid_1", "foo_1", Time()));
  SyncMergeResult merge_result = sync_service_->MergeDataAndStartSyncing(
      syncer::DEVICE_INFO, sync_data, PassProcessor(),
      CreateAndPassSyncErrorFactory());

  EXPECT_EQ(1U, sync_processor_->change_list_size());
  EXPECT_EQ(SyncChange::ACTION_UPDATE, sync_processor_->change_type_at(0));
  EXPECT_EQ("guid_1", sync_processor_->cache_guid_at(0));
  EXPECT_EQ("client_1", sync_processor_->client_name_at(0));
}

// Verifies when the device info has not been update for exactly the pulse
//  interval, it should be updated right now.
TEST_F(DeviceInfoSyncServiceTest, CalculatePulseDelayExactlyStale) {
  const SyncData data = CreateRemoteData("guid_1", "foo_1", Time());
  const TimeDelta actual =
      CalculatePulseDelay(data, Time() + kStaleDeviceInfoThreshold);
  EXPECT_EQ(TimeDelta(), actual);
}

// Verifies when the device info has not been updated for longer than the pulse
// interval, it should be updated right now.
TEST_F(DeviceInfoSyncServiceTest, CalculatePulseDelayVeryStale) {
  const SyncData data = CreateRemoteData("guid_1", "foo_1", Time());
  const TimeDelta actual = CalculatePulseDelay(
      data, Time() + kDeviceInfoPulseInterval + TimeDelta::FromDays(1));
  EXPECT_EQ(TimeDelta(), actual);
}

// Verifies when the device info has not been updated for shorter than the pulse
// interval, it should be updated in the future.
TEST_F(DeviceInfoSyncServiceTest, CalculatePulseDelayMiddle) {
  // This is only going to work while kStaleDeviceInfoThreshold is even.
  const TimeDelta half = kDeviceInfoPulseInterval / 2;
  const SyncData data = CreateRemoteData("guid_1", "foo_1", Time());
  const TimeDelta actual = CalculatePulseDelay(data, Time() + half);
  EXPECT_EQ(half, actual);
}

// Verifies when the device info was last updated right now, it should be
// updated in exactly one pulse interval.
TEST_F(DeviceInfoSyncServiceTest, CalculatePulseDelayCurrent) {
  const SyncData data = CreateRemoteData("guid_1", "foo_1", Time());
  EXPECT_EQ(kDeviceInfoPulseInterval, CalculatePulseDelay(data, Time()));
}

// Verifies when the device info claims to have been updated in the future, it
// should be updated in exactly one pulse interval.
TEST_F(DeviceInfoSyncServiceTest, CalculatePulseDelayFuture) {
  const SyncData data =
      CreateRemoteData("guid_1", "foo_1", Time() + kDeviceInfoPulseInterval);
  const TimeDelta actual = CalculatePulseDelay(data, Time());
  EXPECT_EQ(kDeviceInfoPulseInterval, actual);
}

// Verifies the number of active devices is 0 when there is no data.
TEST_F(DeviceInfoSyncServiceTest, CountActiveDevicesNone) {
  EXPECT_EQ(0, CountActiveDevices(Time()));
}

// Verifies the number of active devices when we have one active device info.
TEST_F(DeviceInfoSyncServiceTest, CountActiveDevicesOneActive) {
  StoreSyncData("active", CreateRemoteData("active", "active", Time()));
  EXPECT_EQ(1, CountActiveDevices(Time() + (kStaleDeviceInfoThreshold / 2)));
}

// Verifies the number of active devices when we have one stale that hasn't been
// updated for exactly the threshold is considered stale.
TEST_F(DeviceInfoSyncServiceTest, CountActiveDevicesExactlyStale) {
  StoreSyncData("stale", CreateRemoteData("stale", "stale", Time()));
  EXPECT_EQ(0, CountActiveDevices(Time() + kStaleDeviceInfoThreshold));
}

// Verifies the number of active devices when we have a mix of active and stale
// device infos.
TEST_F(DeviceInfoSyncServiceTest, CountActiveDevicesManyMix) {
  StoreSyncData("stale", CreateRemoteData("stale", "stale", Time()));
  StoreSyncData("active1",
                CreateRemoteData("active1", "active1",
                                 Time() + kStaleDeviceInfoThreshold / 2));
  StoreSyncData("active2",
                CreateRemoteData("active2", "active2",
                                 Time() + kStaleDeviceInfoThreshold));
  EXPECT_EQ(2, CountActiveDevices(Time() + kStaleDeviceInfoThreshold));
}

// Verifies the number of active devices when we have many that are stale.
TEST_F(DeviceInfoSyncServiceTest, CountActiveDevicesManyStale) {
  StoreSyncData("stale1", CreateRemoteData("stale1", "stale1", Time()));
  StoreSyncData("stale2", CreateRemoteData("stale2", "stale2",
                                           Time() + kStaleDeviceInfoThreshold));
  StoreSyncData("stale3",
                CreateRemoteData("stale3", "stale3",
                                 Time() + (kStaleDeviceInfoThreshold * 2)));
  EXPECT_EQ(0, CountActiveDevices(Time() + (kStaleDeviceInfoThreshold * 3)));
}

// Verifies the number of active devices when we have devices that claim to have
// been updated in the future.
TEST_F(DeviceInfoSyncServiceTest, CountActiveDevicesFuture) {
  StoreSyncData("now", CreateRemoteData("now", "now",
                                        Time() + kStaleDeviceInfoThreshold));
  StoreSyncData("future",
                CreateRemoteData("future", "future",
                                 Time() + (kStaleDeviceInfoThreshold * 10)));
  EXPECT_EQ(2, CountActiveDevices(Time() + kStaleDeviceInfoThreshold));
}

// Verifies the number of active devices when they don't have an updated time
// set, and fallback to checking the SyncData's last modified time.
TEST_F(DeviceInfoSyncServiceTest, CountActiveDevicesModifiedTime) {
  sync_pb::EntitySpecifics stale_entity;
  sync_pb::DeviceInfoSpecifics& stale_specifics =
      *stale_entity.mutable_device_info();
  stale_specifics.set_cache_guid("stale");
  StoreSyncData("stale", SyncData::CreateRemoteData(
                             1, stale_entity, Time(), AttachmentIdList(),
                             AttachmentServiceProxyForTest::Create()));

  sync_pb::EntitySpecifics active_entity;
  sync_pb::DeviceInfoSpecifics& active_specifics =
      *active_entity.mutable_device_info();
  active_specifics.set_cache_guid("active");
  StoreSyncData(
      "active",
      SyncData::CreateRemoteData(
          1, active_entity, Time() + (kStaleDeviceInfoThreshold / 2),
          AttachmentIdList(), AttachmentServiceProxyForTest::Create()));

  EXPECT_EQ(1, CountActiveDevices(Time() + kStaleDeviceInfoThreshold));
}

// Verifies the number of active devices when they don't have an updated time
// and they're not remote, which means we cannot use SyncData's last modified
// time. If now is close to uninitialized time, should still be active.
TEST_F(DeviceInfoSyncServiceTest, CountActiveDevicesLocalActive) {
  sync_pb::EntitySpecifics entity;
  sync_pb::DeviceInfoSpecifics& specifics = *entity.mutable_device_info();
  specifics.set_cache_guid("active");
  StoreSyncData("active",
                SyncData::CreateLocalData("active", "active", entity));
  EXPECT_EQ(1, CountActiveDevices(Time() + (kStaleDeviceInfoThreshold / 2)));
}

// Verifies the number of active devices when they don't have an updated time
// and they're not remote, which means we cannot use SyncData's last modified
// time. If now is far from uninitialized time, should be stale.
TEST_F(DeviceInfoSyncServiceTest, CountActiveDevicesLocalStale) {
  sync_pb::EntitySpecifics entity;
  sync_pb::DeviceInfoSpecifics& specifics = *entity.mutable_device_info();
  specifics.set_cache_guid("stale");
  StoreSyncData("stale", SyncData::CreateLocalData("stale", "stale", entity));
  EXPECT_EQ(0, CountActiveDevices(Time() + kStaleDeviceInfoThreshold));
}

}  // namespace

}  // namespace sync_driver
