// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_APPCACHE_APPCACHE_STORAGE_IMPL_H_
#define CONTENT_BROWSER_APPCACHE_APPCACHE_STORAGE_IMPL_H_

#include <stdint.h>

#include <deque>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/browser/appcache/appcache_database.h"
#include "content/browser/appcache/appcache_disk_cache.h"
#include "content/browser/appcache/appcache_storage.h"
#include "content/common/content_export.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace content {
class AppCacheStorageImplTest;
class ChromeAppCacheServiceTest;

class AppCacheStorageImpl : public AppCacheStorage {
 public:
  explicit AppCacheStorageImpl(AppCacheServiceImpl* service);
  ~AppCacheStorageImpl() override;

  void Initialize(
      const base::FilePath& cache_directory,
      const scoped_refptr<base::SingleThreadTaskRunner>& db_thread,
      const scoped_refptr<base::SingleThreadTaskRunner>& cache_thread);
  void Disable();
  bool is_disabled() const { return is_disabled_; }

  // AppCacheStorage methods, see the base class for doc comments.
  void GetAllInfo(Delegate* delegate) override;
  void LoadCache(int64_t id, Delegate* delegate) override;
  void LoadOrCreateGroup(const GURL& manifest_url, Delegate* delegate) override;
  void StoreGroupAndNewestCache(AppCacheGroup* group,
                                AppCache* newest_cache,
                                Delegate* delegate) override;
  void FindResponseForMainRequest(const GURL& url,
                                  const GURL& preferred_manifest_url,
                                  Delegate* delegate) override;
  void FindResponseForSubRequest(AppCache* cache,
                                 const GURL& url,
                                 AppCacheEntry* found_entry,
                                 AppCacheEntry* found_fallback_entry,
                                 bool* found_network_namespace) override;
  void MarkEntryAsForeign(const GURL& entry_url, int64_t cache_id) override;
  void MakeGroupObsolete(AppCacheGroup* group,
                         Delegate* delegate,
                         int response_code) override;
  void StoreEvictionTimes(AppCacheGroup* group) override;
  AppCacheResponseReader* CreateResponseReader(const GURL& manifest_url,
                                               int64_t group_id,
                                               int64_t response_id) override;
  AppCacheResponseWriter* CreateResponseWriter(const GURL& manifest_url,
                                               int64_t group_id) override;
  AppCacheResponseMetadataWriter* CreateResponseMetadataWriter(
      int64_t group_id,
      int64_t response_id) override;
  void DoomResponses(const GURL& manifest_url,
                     const std::vector<int64_t>& response_ids) override;
  void DeleteResponses(const GURL& manifest_url,
                       const std::vector<int64_t>& response_ids) override;

 private:
  // The AppCacheStorageImpl class methods and datamembers may only be
  // accessed on the IO thread. This class manufactures seperate DatabaseTasks
  // which access the DB on a seperate background thread.
  class DatabaseTask;
  class InitTask;
  class DisableDatabaseTask;
  class GetAllInfoTask;
  class StoreOrLoadTask;
  class CacheLoadTask;
  class GroupLoadTask;
  class StoreGroupAndCacheTask;
  class FindMainResponseTask;
  class MarkEntryAsForeignTask;
  class MakeGroupObsoleteTask;
  class GetDeletableResponseIdsTask;
  class InsertDeletableResponseIdsTask;
  class DeleteDeletableResponseIdsTask;
  class LazyUpdateLastAccessTimeTask;
  class CommitLastAccessTimesTask;
  class UpdateEvictionTimesTask;

  typedef std::deque<DatabaseTask*> DatabaseTaskQueue;
  typedef std::map<int64_t, CacheLoadTask*> PendingCacheLoads;
  typedef std::map<GURL, GroupLoadTask*> PendingGroupLoads;
  typedef std::deque<std::pair<GURL, int64_t>> PendingForeignMarkings;
  typedef std::set<StoreGroupAndCacheTask*> PendingQuotaQueries;

  bool IsInitTaskComplete() {
    return last_cache_id_ != AppCacheStorage::kUnitializedId;
  }

  CacheLoadTask* GetPendingCacheLoadTask(int64_t cache_id);
  GroupLoadTask* GetPendingGroupLoadTask(const GURL& manifest_url);
  void GetPendingForeignMarkingsForCache(int64_t cache_id,
                                         std::vector<GURL>* urls);

  void ScheduleSimpleTask(const base::Closure& task);
  void RunOnePendingSimpleTask();

  void DelayedStartDeletingUnusedResponses();
  void StartDeletingResponses(const std::vector<int64_t>& response_ids);
  void ScheduleDeleteOneResponse();
  void DeleteOneResponse();
  void OnDeletedOneResponse(int rv);
  void OnDiskCacheInitialized(int rv);
  void DeleteAndStartOver();
  void DeleteAndStartOverPart2();
  void CallScheduleReinitialize();
  void LazilyCommitLastAccessTimes();
  void OnLazyCommitTimer();

  // Sometimes we can respond without having to query the database.
  bool FindResponseForMainRequestInGroup(
      AppCacheGroup* group,  const GURL& url, Delegate* delegate);
  void DeliverShortCircuitedFindMainResponse(
      const GURL& url,
      const AppCacheEntry& found_entry,
      scoped_refptr<AppCacheGroup> group,
      scoped_refptr<AppCache> newest_cache,
      scoped_refptr<DelegateReference> delegate_ref);

  void CallOnMainResponseFound(DelegateReferenceVector* delegates,
                               const GURL& url,
                               const AppCacheEntry& entry,
                               const GURL& namespace_entry_url,
                               const AppCacheEntry& fallback_entry,
                               int64_t cache_id,
                               int64_t group_id,
                               const GURL& manifest_url);

  CONTENT_EXPORT AppCacheDiskCache* disk_cache();

  // The directory in which we place files in the file system.
  base::FilePath cache_directory_;
  bool is_incognito_;

  // This class operates primarily on the IO thread, but schedules
  // its DatabaseTasks on the db thread. Separately, the disk_cache uses
  // the cache thread.
  scoped_refptr<base::SingleThreadTaskRunner> db_thread_;
  scoped_refptr<base::SingleThreadTaskRunner> cache_thread_;

  // Structures to keep track of DatabaseTasks that are in-flight.
  DatabaseTaskQueue scheduled_database_tasks_;
  PendingCacheLoads pending_cache_loads_;
  PendingGroupLoads pending_group_loads_;
  PendingForeignMarkings pending_foreign_markings_;
  PendingQuotaQueries pending_quota_queries_;

  // Structures to keep track of lazy response deletion.
  std::deque<int64_t> deletable_response_ids_;
  std::vector<int64_t> deleted_response_ids_;
  bool is_response_deletion_scheduled_;
  bool did_start_deleting_responses_;
  int64_t last_deletable_response_rowid_;

  // Created on the IO thread, but only used on the DB thread.
  AppCacheDatabase* database_;

  // Set if we discover a fatal error like a corrupt SQL database or
  // disk cache and cannot continue.
  bool is_disabled_;

  std::unique_ptr<AppCacheDiskCache> disk_cache_;
  base::OneShotTimer lazy_commit_timer_;

  // Used to short-circuit certain operations without having to schedule
  // any tasks on the background database thread.
  std::deque<base::Closure> pending_simple_tasks_;
  base::WeakPtrFactory<AppCacheStorageImpl> weak_factory_;

  friend class content::AppCacheStorageImplTest;
  friend class content::ChromeAppCacheServiceTest;
};

}  // namespace content

#endif  // CONTENT_BROWSER_APPCACHE_APPCACHE_STORAGE_IMPL_H_
