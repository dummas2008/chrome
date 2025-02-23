// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/value_store/leveldb_value_store.h"

#include <stdint.h>

#include <utility>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "base/thread_task_runner_handle.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/process_memory_dump.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/iterator.h"
#include "third_party/leveldatabase/src/include/leveldb/write_batch.h"

using base::StringPiece;
using content::BrowserThread;

namespace {

const char kInvalidJson[] = "Invalid JSON";
const char kCannotSerialize[] = "Cannot serialize value to JSON";

}  // namespace

LeveldbValueStore::LeveldbValueStore(const std::string& uma_client_name,
                                     const base::FilePath& db_path)
    : LazyLevelDb(uma_client_name, db_path) {
  base::trace_event::MemoryDumpManager::GetInstance()->RegisterDumpProvider(
      this, "LeveldbValueStore", base::ThreadTaskRunnerHandle::Get());
}

LeveldbValueStore::~LeveldbValueStore() {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  base::trace_event::MemoryDumpManager::GetInstance()->UnregisterDumpProvider(
      this);
}

size_t LeveldbValueStore::GetBytesInUse(const std::string& key) {
  // Let SettingsStorageQuotaEnforcer implement this.
  NOTREACHED() << "Not implemented";
  return 0;
}

size_t LeveldbValueStore::GetBytesInUse(
    const std::vector<std::string>& keys) {
  // Let SettingsStorageQuotaEnforcer implement this.
  NOTREACHED() << "Not implemented";
  return 0;
}

size_t LeveldbValueStore::GetBytesInUse() {
  // Let SettingsStorageQuotaEnforcer implement this.
  NOTREACHED() << "Not implemented";
  return 0;
}

ValueStore::ReadResult LeveldbValueStore::Get(const std::string& key) {
  return Get(std::vector<std::string>(1, key));
}

ValueStore::ReadResult LeveldbValueStore::Get(
    const std::vector<std::string>& keys) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeReadResult(status);

  scoped_ptr<base::DictionaryValue> settings(new base::DictionaryValue());

  for (const std::string& key : keys) {
    scoped_ptr<base::Value> setting;
    status.Merge(Read(key, &setting));
    if (!status.ok())
      return MakeReadResult(status);
    if (setting)
      settings->SetWithoutPathExpansion(key, setting.release());
  }

  return MakeReadResult(std::move(settings), status);
}

ValueStore::ReadResult LeveldbValueStore::Get() {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeReadResult(status);

  base::JSONReader json_reader;
  scoped_ptr<base::DictionaryValue> settings(new base::DictionaryValue());

  scoped_ptr<leveldb::Iterator> it(db()->NewIterator(read_options()));
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    std::string key = it->key().ToString();
    scoped_ptr<base::Value> value =
        json_reader.Read(StringPiece(it->value().data(), it->value().size()));
    if (!value) {
      return MakeReadResult(
          Status(CORRUPTION, Delete(key).ok() ? VALUE_RESTORE_DELETE_SUCCESS
                                              : VALUE_RESTORE_DELETE_FAILURE,
                 kInvalidJson));
    }
    settings->SetWithoutPathExpansion(key, std::move(value));
  }

  if (!it->status().ok()) {
    status.Merge(ToValueStoreError(it->status()));
    return MakeReadResult(status);
  }

  return MakeReadResult(std::move(settings), status);
}

ValueStore::WriteResult LeveldbValueStore::Set(WriteOptions options,
                                               const std::string& key,
                                               const base::Value& value) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeWriteResult(status);

  leveldb::WriteBatch batch;
  scoped_ptr<ValueStoreChangeList> changes(new ValueStoreChangeList());
  status.Merge(AddToBatch(options, key, value, &batch, changes.get()));
  if (!status.ok())
    return MakeWriteResult(status);

  status.Merge(WriteToDb(&batch));
  return status.ok() ? MakeWriteResult(std::move(changes), status)
                     : MakeWriteResult(status);
}

ValueStore::WriteResult LeveldbValueStore::Set(
    WriteOptions options,
    const base::DictionaryValue& settings) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeWriteResult(status);

  leveldb::WriteBatch batch;
  scoped_ptr<ValueStoreChangeList> changes(new ValueStoreChangeList());

  for (base::DictionaryValue::Iterator it(settings);
       !it.IsAtEnd(); it.Advance()) {
    status.Merge(
        AddToBatch(options, it.key(), it.value(), &batch, changes.get()));
    if (!status.ok())
      return MakeWriteResult(status);
  }

  status.Merge(WriteToDb(&batch));
  return status.ok() ? MakeWriteResult(std::move(changes), status)
                     : MakeWriteResult(status);
}

ValueStore::WriteResult LeveldbValueStore::Remove(const std::string& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  return Remove(std::vector<std::string>(1, key));
}

ValueStore::WriteResult LeveldbValueStore::Remove(
    const std::vector<std::string>& keys) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeWriteResult(status);

  leveldb::WriteBatch batch;
  scoped_ptr<ValueStoreChangeList> changes(new ValueStoreChangeList());

  for (const std::string& key : keys) {
    scoped_ptr<base::Value> old_value;
    status.Merge(Read(key, &old_value));
    if (!status.ok())
      return MakeWriteResult(status);

    if (old_value) {
      changes->push_back(ValueStoreChange(key, old_value.release(), NULL));
      batch.Delete(key);
    }
  }

  leveldb::Status ldb_status = db()->Write(leveldb::WriteOptions(), &batch);
  if (!ldb_status.ok() && !ldb_status.IsNotFound()) {
    status.Merge(ToValueStoreError(ldb_status));
    return MakeWriteResult(status);
  }
  return MakeWriteResult(std::move(changes), status);
}

ValueStore::WriteResult LeveldbValueStore::Clear() {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  scoped_ptr<ValueStoreChangeList> changes(new ValueStoreChangeList());

  ReadResult read_result = Get();
  if (!read_result->status().ok())
    return MakeWriteResult(read_result->status());

  base::DictionaryValue& whole_db = read_result->settings();
  while (!whole_db.empty()) {
    std::string next_key = base::DictionaryValue::Iterator(whole_db).key();
    scoped_ptr<base::Value> next_value;
    whole_db.RemoveWithoutPathExpansion(next_key, &next_value);
    changes->push_back(ValueStoreChange(next_key, next_value.release(), NULL));
  }

  DeleteDbFile();
  return MakeWriteResult(std::move(changes), read_result->status());
}

bool LeveldbValueStore::WriteToDbForTest(leveldb::WriteBatch* batch) {
  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return false;
  return WriteToDb(batch).ok();
}

bool LeveldbValueStore::OnMemoryDump(
    const base::trace_event::MemoryDumpArgs& args,
    base::trace_event::ProcessMemoryDump* pmd) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  // Return true so that the provider is not disabled.
  if (!db())
    return true;

  std::string value;
  uint64_t size;
  bool res = db()->GetProperty("leveldb.approximate-memory-usage", &value);
  DCHECK(res);
  res = base::StringToUint64(value, &size);
  DCHECK(res);

  auto dump = pmd->CreateAllocatorDump(base::StringPrintf(
      "leveldb/value_store/%s/%p", open_histogram_name().c_str(), this));
  dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameSize,
                  base::trace_event::MemoryAllocatorDump::kUnitsBytes, size);

  // Memory is allocated from system allocator (malloc).
  const char* system_allocator_name =
      base::trace_event::MemoryDumpManager::GetInstance()
          ->system_allocator_pool_name();
  if (system_allocator_name)
    pmd->AddSuballocation(dump->guid(), system_allocator_name);

  return true;
}

ValueStore::Status LeveldbValueStore::AddToBatch(
    ValueStore::WriteOptions options,
    const std::string& key,
    const base::Value& value,
    leveldb::WriteBatch* batch,
    ValueStoreChangeList* changes) {
  bool write_new_value = true;

  if (!(options & NO_GENERATE_CHANGES)) {
    scoped_ptr<base::Value> old_value;
    Status status = Read(key, &old_value);
    if (!status.ok())
      return status;
    if (!old_value || !old_value->Equals(&value)) {
      changes->push_back(
          ValueStoreChange(key, old_value.release(), value.DeepCopy()));
    } else {
      write_new_value = false;
    }
  }

  if (write_new_value) {
    std::string value_as_json;
    if (!base::JSONWriter::Write(value, &value_as_json))
      return Status(OTHER_ERROR, kCannotSerialize);
    batch->Put(key, value_as_json);
  }

  return Status();
}

ValueStore::Status LeveldbValueStore::WriteToDb(leveldb::WriteBatch* batch) {
  return ToValueStoreError(db()->Write(write_options(), batch));
}
