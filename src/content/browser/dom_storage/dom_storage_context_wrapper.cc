// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/dom_storage/dom_storage_context_wrapper.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/thread_task_runner_handle.h"
#include "components/filesystem/public/interfaces/directory.mojom.h"
#include "components/leveldb/public/interfaces/leveldb.mojom.h"
#include "components/profile_service/public/interfaces/profile.mojom.h"
#include "content/browser/dom_storage/dom_storage_area.h"
#include "content/browser/dom_storage/dom_storage_context_impl.h"
#include "content/browser/dom_storage/dom_storage_task_runner.h"
#include "content/browser/dom_storage/session_storage_namespace_impl.h"
#include "content/browser/leveldb_wrapper_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/local_storage_usage_info.h"
#include "content/public/browser/mojo_app_connection.h"
#include "content/public/browser/session_storage_usage_info.h"
#include "mojo/common/common_type_converters.h"

namespace content {
namespace {

const char kLocalStorageDirectory[] = "Local Storage";
const char kSessionStorageDirectory[] = "Session Storage";

void InvokeLocalStorageUsageCallbackHelper(
      const DOMStorageContext::GetLocalStorageUsageCallback& callback,
      const std::vector<LocalStorageUsageInfo>* infos) {
  callback.Run(*infos);
}

void GetLocalStorageUsageHelper(
    base::SingleThreadTaskRunner* reply_task_runner,
    DOMStorageContextImpl* context,
    const DOMStorageContext::GetLocalStorageUsageCallback& callback) {
  std::vector<LocalStorageUsageInfo>* infos =
      new std::vector<LocalStorageUsageInfo>;
  context->GetLocalStorageUsage(infos, true);
  reply_task_runner->PostTask(
      FROM_HERE, base::Bind(&InvokeLocalStorageUsageCallbackHelper, callback,
                            base::Owned(infos)));
}

void InvokeSessionStorageUsageCallbackHelper(
      const DOMStorageContext::GetSessionStorageUsageCallback& callback,
      const std::vector<SessionStorageUsageInfo>* infos) {
  callback.Run(*infos);
}

void GetSessionStorageUsageHelper(
    base::SingleThreadTaskRunner* reply_task_runner,
    DOMStorageContextImpl* context,
    const DOMStorageContext::GetSessionStorageUsageCallback& callback) {
  std::vector<SessionStorageUsageInfo>* infos =
      new std::vector<SessionStorageUsageInfo>;
  context->GetSessionStorageUsage(infos);
  reply_task_runner->PostTask(
      FROM_HERE, base::Bind(&InvokeSessionStorageUsageCallbackHelper, callback,
                            base::Owned(infos)));
}

}  // namespace

// Used for mojo-based LocalStorage implementation (behind --mojo-local-storage
// for now).
class DOMStorageContextWrapper::MojoState {
 public:
  MojoState(const std::string& mojo_user_id, const base::FilePath& subdirectory)
      : mojo_user_id_(mojo_user_id),
        subdirectory_(subdirectory),
        connection_state_(NO_CONNECTION),
        weak_ptr_factory_(this) {}

  void OpenLocalStorage(const url::Origin& origin,
                        mojom::LevelDBObserverPtr observer,
                        mojom::LevelDBWrapperRequest request);

 private:
  void OnLevelDDWrapperHasNoBindings(const url::Origin& origin) {
    DCHECK(level_db_wrappers_.find(origin) != level_db_wrappers_.end());
    level_db_wrappers_.erase(origin);
  }

  // Part of our asynchronous directory opening called from OpenLocalStorage().
  void OnDirectoryOpened(filesystem::FileError err);
  void OnDatabaseOpened(leveldb::DatabaseError status);

  // The (possibly delayed) implementation of OpenLocalStorage(). Can be called
  // directly from that function, or through |on_database_open_callbacks_|.
  void BindLocalStorage(const url::Origin& origin,
                        mojom::LevelDBObserverPtr observer,
                        mojom::LevelDBWrapperRequest request);

  // Maps between an origin and its prefixed LevelDB view.
  std::map<url::Origin, std::unique_ptr<LevelDBWrapperImpl>> level_db_wrappers_;

  std::string mojo_user_id_;
  base::FilePath subdirectory_;

  enum ConnectionState {
    NO_CONNECTION,
    CONNECTION_IN_PROGRESS,
    CONNECTION_FINISHED
  } connection_state_;

  std::unique_ptr<MojoAppConnection> profile_app_connection_;
  profile::ProfileServicePtr profile_service_;
  filesystem::DirectoryPtr directory_;

  leveldb::LevelDBServicePtr leveldb_service_;
  leveldb::LevelDBDatabasePtr database_;

  std::vector<base::Closure> on_database_opened_callbacks_;

  base::WeakPtrFactory<MojoState> weak_ptr_factory_;
};

void DOMStorageContextWrapper::MojoState::OpenLocalStorage(
    const url::Origin& origin,
    mojom::LevelDBObserverPtr observer,
    mojom::LevelDBWrapperRequest request) {
  // If we don't have a filesystem_connection_, we'll need to establish one.
  if (connection_state_ == NO_CONNECTION) {
    profile_app_connection_ = MojoAppConnection::Create(
        mojo_user_id_, "mojo:profile", kBrowserMojoAppUrl);

    connection_state_ = CONNECTION_IN_PROGRESS;

    if (!subdirectory_.empty()) {
      // We were given a subdirectory to write to. Get it and use a disk backed
      // database.
      profile_app_connection_->GetInterface(&profile_service_);
      profile_service_->GetSubDirectory(
          mojo::String::From(subdirectory_.AsUTF8Unsafe()),
          GetProxy(&directory_),
          base::Bind(&MojoState::OnDirectoryOpened,
                     weak_ptr_factory_.GetWeakPtr()));
    } else {
      // We were not given a subdirectory. Use a memory backed database.
      profile_app_connection_->GetInterface(&leveldb_service_);
      leveldb_service_->OpenInMemory(
          GetProxy(&database_),
          base::Bind(&MojoState::OnDatabaseOpened,
                     weak_ptr_factory_.GetWeakPtr()));
    }
  }

  if (connection_state_ == CONNECTION_IN_PROGRESS) {
    // Queue this OpenLocalStorage call for when we have a level db pointer.
    on_database_opened_callbacks_.push_back(
        base::Bind(&MojoState::BindLocalStorage, weak_ptr_factory_.GetWeakPtr(),
                   origin, base::Passed(&observer), base::Passed(&request)));
    return;
  }

  BindLocalStorage(origin, std::move(observer), std::move(request));
}

void DOMStorageContextWrapper::MojoState::OnDirectoryOpened(
    filesystem::FileError err) {
  if (err != filesystem::FileError::OK) {
    // We failed to open the directory; continue with startup so that we create
    // the |level_db_wrappers_|.
    OnDatabaseOpened(leveldb::DatabaseError::IO_ERROR);
    return;
  }

  // Now that we have a directory, connect to the LevelDB service and get our
  // database.
  profile_app_connection_->GetInterface(&leveldb_service_);

  leveldb_service_->Open(
      std::move(directory_), "leveldb", GetProxy(&database_),
      base::Bind(&MojoState::OnDatabaseOpened, weak_ptr_factory_.GetWeakPtr()));
}

void DOMStorageContextWrapper::MojoState::OnDatabaseOpened(
    leveldb::DatabaseError status) {
  if (status != leveldb::DatabaseError::OK) {
    // If we failed to open the database, reset the service object so we pass
    // null pointers to our wrappers.
    database_.reset();
    leveldb_service_.reset();
  }

  // We no longer need the profile service; we've either transferred
  // |directory_| to the leveldb service, or we got a file error and no more is
  // possible.
  directory_.reset();
  profile_service_.reset();

  // |leveldb_| should be known to either be valid or invalid by now. Run our
  // delayed bindings.
  connection_state_ = CONNECTION_FINISHED;
  for (size_t i = 0; i < on_database_opened_callbacks_.size(); ++i)
    on_database_opened_callbacks_[i].Run();
  on_database_opened_callbacks_.clear();
}

void DOMStorageContextWrapper::MojoState::BindLocalStorage(
    const url::Origin& origin,
    mojom::LevelDBObserverPtr observer,
    mojom::LevelDBWrapperRequest request) {
  auto found = level_db_wrappers_.find(origin);
  if (found == level_db_wrappers_.end()) {
    level_db_wrappers_[origin] = base::WrapUnique(new LevelDBWrapperImpl(
        database_.get(), origin.Serialize(),
        kPerStorageAreaQuota + kPerStorageAreaOverQuotaAllowance,
        base::Bind(&MojoState::OnLevelDDWrapperHasNoBindings,
                   base::Unretained(this), origin)));
    found = level_db_wrappers_.find(origin);
  }

  found->second->Bind(std::move(request));
  found->second->AddObserver(std::move(observer));
}

DOMStorageContextWrapper::DOMStorageContextWrapper(
    const std::string& mojo_user_id,
    const base::FilePath& profile_path,
    const base::FilePath& local_partition_path,
    storage::SpecialStoragePolicy* special_storage_policy) {
  base::FilePath storage_dir;
  if (!profile_path.empty())
    storage_dir = local_partition_path.AppendASCII(kLocalStorageDirectory);
  mojo_state_.reset(new MojoState(mojo_user_id, storage_dir));

  base::FilePath data_path;
  if (!profile_path.empty())
    data_path = profile_path.Append(local_partition_path);
  base::SequencedWorkerPool* worker_pool = BrowserThread::GetBlockingPool();
  context_ = new DOMStorageContextImpl(
      data_path.empty() ? data_path
                        : data_path.AppendASCII(kLocalStorageDirectory),
      data_path.empty() ? data_path
                        : data_path.AppendASCII(kSessionStorageDirectory),
      special_storage_policy,
      new DOMStorageWorkerPoolTaskRunner(
          worker_pool,
          worker_pool->GetNamedSequenceToken("dom_storage_primary"),
          worker_pool->GetNamedSequenceToken("dom_storage_commit"),
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO)
              .get()));
}

DOMStorageContextWrapper::~DOMStorageContextWrapper() {}

void DOMStorageContextWrapper::GetLocalStorageUsage(
    const GetLocalStorageUsageCallback& callback) {
  DCHECK(context_.get());
  context_->task_runner()->PostShutdownBlockingTask(
      FROM_HERE, DOMStorageTaskRunner::PRIMARY_SEQUENCE,
      base::Bind(&GetLocalStorageUsageHelper,
                 base::RetainedRef(base::ThreadTaskRunnerHandle::Get()),
                 base::RetainedRef(context_), callback));
}

void DOMStorageContextWrapper::GetSessionStorageUsage(
    const GetSessionStorageUsageCallback& callback) {
  DCHECK(context_.get());
  context_->task_runner()->PostShutdownBlockingTask(
      FROM_HERE, DOMStorageTaskRunner::PRIMARY_SEQUENCE,
      base::Bind(&GetSessionStorageUsageHelper,
                 base::RetainedRef(base::ThreadTaskRunnerHandle::Get()),
                 base::RetainedRef(context_), callback));
}

void DOMStorageContextWrapper::DeleteLocalStorage(const GURL& origin) {
  DCHECK(context_.get());
  context_->task_runner()->PostShutdownBlockingTask(
      FROM_HERE,
      DOMStorageTaskRunner::PRIMARY_SEQUENCE,
      base::Bind(&DOMStorageContextImpl::DeleteLocalStorage, context_, origin));
}

void DOMStorageContextWrapper::DeleteSessionStorage(
    const SessionStorageUsageInfo& usage_info) {
  DCHECK(context_.get());
  context_->task_runner()->PostShutdownBlockingTask(
      FROM_HERE,
      DOMStorageTaskRunner::PRIMARY_SEQUENCE,
      base::Bind(&DOMStorageContextImpl::DeleteSessionStorage,
                 context_, usage_info));
}

void DOMStorageContextWrapper::SetSaveSessionStorageOnDisk() {
  DCHECK(context_.get());
  context_->SetSaveSessionStorageOnDisk();
}

scoped_refptr<SessionStorageNamespace>
DOMStorageContextWrapper::RecreateSessionStorage(
    const std::string& persistent_id) {
  return scoped_refptr<SessionStorageNamespace>(
      new SessionStorageNamespaceImpl(this, persistent_id));
}

void DOMStorageContextWrapper::StartScavengingUnusedSessionStorage() {
  DCHECK(context_.get());
  context_->task_runner()->PostShutdownBlockingTask(
      FROM_HERE,
      DOMStorageTaskRunner::PRIMARY_SEQUENCE,
      base::Bind(&DOMStorageContextImpl::StartScavengingUnusedSessionStorage,
                 context_));
}

void DOMStorageContextWrapper::SetForceKeepSessionState() {
  DCHECK(context_.get());
  context_->task_runner()->PostShutdownBlockingTask(
      FROM_HERE,
      DOMStorageTaskRunner::PRIMARY_SEQUENCE,
      base::Bind(&DOMStorageContextImpl::SetForceKeepSessionState, context_));
}

void DOMStorageContextWrapper::Shutdown() {
  DCHECK(context_.get());
  mojo_state_.reset();
  context_->task_runner()->PostShutdownBlockingTask(
      FROM_HERE,
      DOMStorageTaskRunner::PRIMARY_SEQUENCE,
      base::Bind(&DOMStorageContextImpl::Shutdown, context_));
}

void DOMStorageContextWrapper::Flush() {
  DCHECK(context_.get());
  context_->task_runner()->PostShutdownBlockingTask(
      FROM_HERE, DOMStorageTaskRunner::PRIMARY_SEQUENCE,
      base::Bind(&DOMStorageContextImpl::Flush, context_));
}

void DOMStorageContextWrapper::OpenLocalStorage(
    const url::Origin& origin,
    mojom::LevelDBObserverPtr observer,
    mojom::LevelDBWrapperRequest request) {
  mojo_state_->OpenLocalStorage(
      origin, std::move(observer), std::move(request));
}

}  // namespace content
