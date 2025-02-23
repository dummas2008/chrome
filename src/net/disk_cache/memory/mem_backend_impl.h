// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See net/disk_cache/disk_cache.h for the public interface of the cache.

#ifndef NET_DISK_CACHE_MEMORY_MEM_BACKEND_IMPL_H_
#define NET_DISK_CACHE_MEMORY_MEM_BACKEND_IMPL_H_

#include <stdint.h>

#include <string>
#include <unordered_map>

#include "base/compiler_specific.h"
#include "base/containers/linked_list.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_split.h"
#include "base/time/time.h"
#include "net/disk_cache/disk_cache.h"
#include "net/disk_cache/memory/mem_entry_impl.h"

namespace net {
class NetLog;
}  // namespace net

namespace disk_cache {

// This class implements the Backend interface. An object of this class handles
// the operations of the cache without writing to disk.
class NET_EXPORT_PRIVATE MemBackendImpl final : public Backend {
 public:
  explicit MemBackendImpl(net::NetLog* net_log);
  ~MemBackendImpl() override;

  // Returns an instance of a Backend implemented only in memory. The returned
  // object should be deleted when not needed anymore. max_bytes is the maximum
  // size the cache can grow to. If zero is passed in as max_bytes, the cache
  // will determine the value to use based on the available memory. The returned
  // pointer can be NULL if a fatal error is found.
  static scoped_ptr<Backend> CreateBackend(int max_bytes, net::NetLog* net_log);

  // Performs general initialization for this current instance of the cache.
  bool Init();

  // Sets the maximum size for the total amount of data stored by this instance.
  bool SetMaxSize(int max_bytes);

  // Returns the maximum size for a file to reside on the cache.
  int MaxFileSize() const;

  // These next methods (before the implementation of the Backend interface) are
  // called by MemEntryImpl to update the state of the backend during the entry
  // lifecycle.

  // Signals that new entry has been created, and should be placed in
  // |lru_list_| so that it is eligable for eviction.
  void OnEntryInserted(MemEntryImpl* entry);

  // Signals that an entry has been updated, and thus should be moved to the end
  // of |lru_list_|.
  void OnEntryUpdated(MemEntryImpl* entry);

  // Signals that an entry has been doomed, and so it should be removed from the
  // list of active entries as appropriate, as well as removed from the
  // |lru_list_|.
  void OnEntryDoomed(MemEntryImpl* entry);

  // Adjust the current size of this backend by |delta|. This is used to
  // determine if eviction is neccessary and when eviction is finished.
  void ModifyStorageSize(int32_t delta);

  // Backend interface.
  net::CacheType GetCacheType() const override;
  int32_t GetEntryCount() const override;
  int OpenEntry(const std::string& key,
                Entry** entry,
                const CompletionCallback& callback) override;
  int CreateEntry(const std::string& key,
                  Entry** entry,
                  const CompletionCallback& callback) override;
  int DoomEntry(const std::string& key,
                const CompletionCallback& callback) override;
  int DoomAllEntries(const CompletionCallback& callback) override;
  int DoomEntriesBetween(base::Time initial_time,
                         base::Time end_time,
                         const CompletionCallback& callback) override;
  int DoomEntriesSince(base::Time initial_time,
                       const CompletionCallback& callback) override;
  int CalculateSizeOfAllEntries(const CompletionCallback& callback) override;
  scoped_ptr<Iterator> CreateIterator() override;
  void GetStats(base::StringPairs* stats) override {}
  void OnExternalCacheHit(const std::string& key) override;

 private:
  class MemIterator;
  friend class MemIterator;

  using EntryMap = std::unordered_map<std::string, MemEntryImpl*>;

  // Deletes entries from the cache until the current size is below the limit.
  void EvictIfNeeded();

  EntryMap entries_;

  // Stored in increasing order of last use time, from least recently used to
  // most recently used.
  base::LinkedList<MemEntryImpl> lru_list_;

  int32_t max_size_;      // Maximum data size for this instance.
  int32_t current_size_;

  net::NetLog* net_log_;

  base::WeakPtrFactory<MemBackendImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(MemBackendImpl);
};

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_MEMORY_MEM_BACKEND_IMPL_H_
