// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/pepper/quota_reservation.h"

#include <stdint.h>

#include <memory>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "storage/browser/fileapi/quota/quota_reservation.h"
#include "testing/gtest/include/gtest/gtest.h"

using storage::QuotaReservationManager;

namespace content {

namespace {

const char kOrigin[] = "http://example.com";
const storage::FileSystemType kType = storage::kFileSystemTypeTemporary;

const base::FilePath::StringType file1_name = FILE_PATH_LITERAL("file1");
const base::FilePath::StringType file2_name = FILE_PATH_LITERAL("file2");
const base::FilePath::StringType file3_name = FILE_PATH_LITERAL("file3");
const int kFile1ID = 1;
const int kFile2ID = 2;
const int kFile3ID = 3;

class FakeBackend : public QuotaReservationManager::QuotaBackend {
 public:
  FakeBackend() {}
  ~FakeBackend() override {}

  void ReserveQuota(
      const GURL& origin,
      storage::FileSystemType type,
      int64_t delta,
      const QuotaReservationManager::ReserveQuotaCallback& callback) override {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::Bind(base::IgnoreResult(callback), base::File::FILE_OK, delta));
  }

  void ReleaseReservedQuota(const GURL& origin,
                            storage::FileSystemType type,
                            int64_t size) override {}

  void CommitQuotaUsage(const GURL& origin,
                        storage::FileSystemType type,
                        int64_t delta) override {}

  void IncrementDirtyCount(const GURL& origin,
                           storage::FileSystemType type) override {}
  void DecrementDirtyCount(const GURL& origin,
                           storage::FileSystemType type) override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeBackend);
};

}  // namespace

class QuotaReservationTest : public testing::Test {
 public:
  QuotaReservationTest() {}
  ~QuotaReservationTest() override {}

  void SetUp() override {
    ASSERT_TRUE(work_dir_.CreateUniqueTempDir());

    reservation_manager_.reset(new QuotaReservationManager(
        std::unique_ptr<QuotaReservationManager::QuotaBackend>(
            new FakeBackend)));
  }

  void TearDown() override {
    reservation_manager_.reset();
    base::RunLoop().RunUntilIdle();
  }

  base::FilePath MakeFilePath(const base::FilePath::StringType& file_name) {
    return work_dir_.path().Append(file_name);
  }

  storage::FileSystemURL MakeFileSystemURL(
      const base::FilePath::StringType& file_name) {
    return storage::FileSystemURL::CreateForTest(
        GURL(kOrigin), kType, MakeFilePath(file_name));
  }

  scoped_refptr<QuotaReservation> CreateQuotaReservation(
      scoped_refptr<storage::QuotaReservation> reservation,
      const GURL& origin,
      storage::FileSystemType type) {
    // Sets reservation_ as a side effect.
    return scoped_refptr<QuotaReservation>(
        new QuotaReservation(reservation, origin, type));
  }

  void SetFileSize(const base::FilePath::StringType& file_name, int64_t size) {
    base::File file(MakeFilePath(file_name),
                    base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_WRITE);
    ASSERT_TRUE(file.IsValid());
    ASSERT_TRUE(file.SetLength(size));
  }

  QuotaReservationManager* reservation_manager() {
    return reservation_manager_.get();
  }

 private:
  base::MessageLoop message_loop_;
  base::ScopedTempDir work_dir_;
  std::unique_ptr<storage::QuotaReservationManager> reservation_manager_;

  DISALLOW_COPY_AND_ASSIGN(QuotaReservationTest);
};

void GotReservedQuota(int64_t* reserved_quota_ptr,
                      ppapi::FileGrowthMap* file_growths_ptr,
                      int64_t reserved_quota,
                      const ppapi::FileSizeMap& maximum_written_offsets) {
  *reserved_quota_ptr = reserved_quota;

  file_growths_ptr->clear();
  for (ppapi::FileSizeMap::const_iterator it = maximum_written_offsets.begin();
       it != maximum_written_offsets.end();
       ++it)
    (*file_growths_ptr)[it->first] = ppapi::FileGrowth(it->second, 0);
}

void ReserveQuota(scoped_refptr<QuotaReservation> quota_reservation,
                  int64_t amount,
                  int64_t* reserved_quota,
                  ppapi::FileGrowthMap* file_growths) {
  quota_reservation->ReserveQuota(
      amount,
      *file_growths,
      base::Bind(&GotReservedQuota, reserved_quota, file_growths));
  base::RunLoop().RunUntilIdle();
}

// Tests that:
// 1) We can reserve quota with no files open.
// 2) Open a file, grow it, close it, and reserve quota with correct sizes.
TEST_F(QuotaReservationTest, ReserveQuota) {
  GURL origin(kOrigin);
  storage::FileSystemType type = kType;

  scoped_refptr<storage::QuotaReservation> reservation(
      reservation_manager()->CreateReservation(origin, type));
  scoped_refptr<QuotaReservation> test =
      CreateQuotaReservation(reservation, origin, type);

  // Reserve quota with no files open.
  int64_t amount = 100;
  int64_t reserved_quota;
  ppapi::FileGrowthMap file_growths;
  ReserveQuota(test, amount, &reserved_quota, &file_growths);
  EXPECT_EQ(amount, reserved_quota);
  EXPECT_EQ(0U, file_growths.size());

  // Open a file, refresh the reservation, extend the file, and close it.
  int64_t file_size = 10;
  SetFileSize(file1_name, file_size);
  int64_t open_file_size =
      test->OpenFile(kFile1ID, MakeFileSystemURL(file1_name));
  EXPECT_EQ(file_size, open_file_size);

  file_growths[kFile1ID] = ppapi::FileGrowth(file_size, 0);  // 1 file open.
  ReserveQuota(test, amount, &reserved_quota, &file_growths);
  EXPECT_EQ(amount, reserved_quota);
  EXPECT_EQ(1U, file_growths.size());
  EXPECT_EQ(file_size, file_growths[kFile1ID].max_written_offset);

  int64_t new_file_size = 30;
  SetFileSize(file1_name, new_file_size);

  EXPECT_EQ(amount, reservation->remaining_quota());
  test->CloseFile(kFile1ID, ppapi::FileGrowth(new_file_size, 0));
  EXPECT_EQ(amount - (new_file_size - file_size),
            reservation->remaining_quota());
}

// Tests that:
// 1) We can open and close multiple files.
TEST_F(QuotaReservationTest, MultipleFiles) {
  GURL origin(kOrigin);
  storage::FileSystemType type = kType;

  scoped_refptr<storage::QuotaReservation> reservation(
      reservation_manager()->CreateReservation(origin, type));
  scoped_refptr<QuotaReservation> test =
      CreateQuotaReservation(reservation, origin, type);

  // Open some files of different sizes.
  int64_t file1_size = 10;
  SetFileSize(file1_name, file1_size);
  int64_t open_file1_size =
      test->OpenFile(kFile1ID, MakeFileSystemURL(file1_name));
  EXPECT_EQ(file1_size, open_file1_size);
  int64_t file2_size = 20;
  SetFileSize(file2_name, file2_size);
  int64_t open_file2_size =
      test->OpenFile(kFile2ID, MakeFileSystemURL(file2_name));
  EXPECT_EQ(file2_size, open_file2_size);
  int64_t file3_size = 30;
  SetFileSize(file3_name, file3_size);
  int64_t open_file3_size =
      test->OpenFile(kFile3ID, MakeFileSystemURL(file3_name));
  EXPECT_EQ(file3_size, open_file3_size);

  // Reserve quota.
  int64_t amount = 100;
  int64_t reserved_quota;
  ppapi::FileGrowthMap file_growths;
  file_growths[kFile1ID] = ppapi::FileGrowth(file1_size, 0);  // 3 files open.
  file_growths[kFile2ID] = ppapi::FileGrowth(file2_size, 0);
  file_growths[kFile3ID] = ppapi::FileGrowth(file3_size, 0);

  ReserveQuota(test, amount, &reserved_quota, &file_growths);
  EXPECT_EQ(amount, reserved_quota);
  EXPECT_EQ(3U, file_growths.size());
  EXPECT_EQ(file1_size, file_growths[kFile1ID].max_written_offset);
  EXPECT_EQ(file2_size, file_growths[kFile2ID].max_written_offset);
  EXPECT_EQ(file3_size, file_growths[kFile3ID].max_written_offset);

  test->CloseFile(kFile2ID, ppapi::FileGrowth(file2_size, 0));

  file_growths.erase(kFile2ID);
  ReserveQuota(test, amount, &reserved_quota, &file_growths);
  EXPECT_EQ(amount, reserved_quota);
  EXPECT_EQ(2U, file_growths.size());
  EXPECT_EQ(file1_size, file_growths[kFile1ID].max_written_offset);
  EXPECT_EQ(file3_size, file_growths[kFile3ID].max_written_offset);

  test->CloseFile(kFile1ID, ppapi::FileGrowth(file1_size, 0));
  test->CloseFile(kFile3ID, ppapi::FileGrowth(file3_size, 0));
}

}  // namespace content
