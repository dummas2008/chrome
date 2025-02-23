// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/drive/change_list_processor.h"

#include <stddef.h>
#include <utility>

#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/cancellation_flag.h"
#include "components/drive/drive.pb.h"
#include "components/drive/drive_api_util.h"
#include "components/drive/file_change.h"
#include "components/drive/file_system_core_util.h"
#include "components/drive/resource_entry_conversion.h"
#include "components/drive/resource_metadata.h"
#include "google_apis/drive/drive_api_parser.h"

namespace drive {
namespace internal {

namespace {

class ChangeListToEntryMapUMAStats {
 public:
  ChangeListToEntryMapUMAStats()
    : num_regular_files_(0),
      num_hosted_documents_(0),
      num_shared_with_me_entries_(0) {
  }

  // Increments number of files.
  void IncrementNumFiles(bool is_hosted_document) {
    is_hosted_document ? num_hosted_documents_++ : num_regular_files_++;
  }

  // Increments number of shared-with-me entries.
  void IncrementNumSharedWithMeEntries() {
    num_shared_with_me_entries_++;
  }

  // Updates UMA histograms with file counts.
  void UpdateFileCountUmaHistograms() {
    const int num_total_files = num_hosted_documents_ + num_regular_files_;
    UMA_HISTOGRAM_COUNTS("Drive.NumberOfRegularFiles", num_regular_files_);
    UMA_HISTOGRAM_COUNTS("Drive.NumberOfHostedDocuments",
                         num_hosted_documents_);
    UMA_HISTOGRAM_COUNTS("Drive.NumberOfTotalFiles", num_total_files);
    UMA_HISTOGRAM_COUNTS("Drive.NumberOfSharedWithMeEntries",
                         num_shared_with_me_entries_);
  }

 private:
  int num_regular_files_;
  int num_hosted_documents_;
  int num_shared_with_me_entries_;
};

// Returns true if it's OK to overwrite the local entry with the remote one.
bool ShouldApplyChange(const ResourceEntry& local_entry,
                       const ResourceEntry& remote_entry) {
  if (local_entry.metadata_edit_state() == ResourceEntry::CLEAN)
    return true;
  return base::Time::FromInternalValue(remote_entry.modification_date()) >
      base::Time::FromInternalValue(local_entry.modification_date());
}

}  // namespace

std::string DirectoryFetchInfo::ToString() const {
  return ("local_id: " + local_id_ +
          ", resource_id: " + resource_id_ +
          ", changestamp: " + base::Int64ToString(changestamp_));
}

ChangeList::ChangeList() {}

ChangeList::ChangeList(const google_apis::ChangeList& change_list)
    : next_url_(change_list.next_link()),
      largest_changestamp_(change_list.largest_change_id()) {
  const ScopedVector<google_apis::ChangeResource>& items = change_list.items();
  entries_.resize(items.size());
  parent_resource_ids_.resize(items.size());
  size_t entries_index = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    if (ConvertChangeResourceToResourceEntry(
            *items[i],
            &entries_[entries_index],
            &parent_resource_ids_[entries_index])) {
      ++entries_index;
    }
  }
  entries_.resize(entries_index);
  parent_resource_ids_.resize(entries_index);
}

ChangeList::ChangeList(const google_apis::FileList& file_list)
    : next_url_(file_list.next_link()),
      largest_changestamp_(0) {
  const ScopedVector<google_apis::FileResource>& items = file_list.items();
  entries_.resize(items.size());
  parent_resource_ids_.resize(items.size());
  size_t entries_index = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    if (ConvertFileResourceToResourceEntry(
            *items[i],
            &entries_[entries_index],
            &parent_resource_ids_[entries_index])) {
      ++entries_index;
    }
  }
  entries_.resize(entries_index);
  parent_resource_ids_.resize(entries_index);
}

ChangeList::~ChangeList() {}

ChangeListProcessor::ChangeListProcessor(ResourceMetadata* resource_metadata,
                                         base::CancellationFlag* in_shutdown)
    : resource_metadata_(resource_metadata),
      in_shutdown_(in_shutdown),
      changed_files_(new FileChange) {
}

ChangeListProcessor::~ChangeListProcessor() {
}

FileError ChangeListProcessor::Apply(
    std::unique_ptr<google_apis::AboutResource> about_resource,
    ScopedVector<ChangeList> change_lists,
    bool is_delta_update) {
  DCHECK(about_resource);

  int64_t largest_changestamp = 0;
  if (is_delta_update) {
    if (!change_lists.empty()) {
      // The changestamp appears in the first page of the change list.
      // The changestamp does not appear in the full resource list.
      largest_changestamp = change_lists[0]->largest_changestamp();
      DCHECK_GE(change_lists[0]->largest_changestamp(), 0);
    }
  } else {
    largest_changestamp = about_resource->largest_change_id();

    DVLOG(1) << "Root folder ID is " << about_resource->root_folder_id();
    DCHECK(!about_resource->root_folder_id().empty());
  }

  // Convert ChangeList to map.
  ChangeListToEntryMapUMAStats uma_stats;
  for (size_t i = 0; i < change_lists.size(); ++i) {
    ChangeList* change_list = change_lists[i];

    std::vector<ResourceEntry>* entries = change_list->mutable_entries();
    for (size_t i = 0; i < entries->size(); ++i) {
      ResourceEntry* entry = &(*entries)[i];

      // Count the number of files.
      if (!entry->file_info().is_directory()) {
        uma_stats.IncrementNumFiles(
            entry->file_specific_info().is_hosted_document());
        if (entry->shared_with_me())
          uma_stats.IncrementNumSharedWithMeEntries();
      }
      parent_resource_id_map_[entry->resource_id()] =
          change_list->parent_resource_ids()[i];
      entry_map_[entry->resource_id()].Swap(entry);
      LOG_IF(WARNING, !entry->resource_id().empty())
          << "Found duplicated file: " << entry->base_name();
    }
  }

  // Add the largest changestamp for directories.
  for (ResourceEntryMap::iterator it = entry_map_.begin();
       it != entry_map_.end(); ++it) {
    if (it->second.file_info().is_directory()) {
      it->second.mutable_directory_specific_info()->set_changestamp(
          largest_changestamp);
    }
  }

  FileError error =
      ApplyEntryMap(largest_changestamp, std::move(about_resource));
  if (error != FILE_ERROR_OK) {
    DLOG(ERROR) << "ApplyEntryMap failed: " << FileErrorToString(error);
    return error;
  }

  // Update changestamp.
  error = resource_metadata_->SetLargestChangestamp(largest_changestamp);
  if (error != FILE_ERROR_OK) {
    DLOG(ERROR) << "SetLargestChangeStamp failed: " << FileErrorToString(error);
    return error;
  }

  // Shouldn't record histograms when processing delta update.
  if (!is_delta_update)
    uma_stats.UpdateFileCountUmaHistograms();

  return FILE_ERROR_OK;
}

FileError ChangeListProcessor::ApplyEntryMap(
    int64_t changestamp,
    std::unique_ptr<google_apis::AboutResource> about_resource) {
  DCHECK(about_resource);

  // Create the entry for "My Drive" directory with the latest changestamp.
  ResourceEntry root;
  FileError error = resource_metadata_->GetResourceEntryByPath(
      util::GetDriveMyDriveRootPath(), &root);
  if (error != FILE_ERROR_OK) {
    LOG(ERROR) << "Failed to get root entry: " << FileErrorToString(error);
    return error;
  }

  root.mutable_directory_specific_info()->set_changestamp(changestamp);
  root.set_resource_id(about_resource->root_folder_id());
  error = resource_metadata_->RefreshEntry(root);
  if (error != FILE_ERROR_OK) {
    LOG(ERROR) << "Failed to update root entry: " << FileErrorToString(error);
    return error;
  }

  // Gather the set of changes in the old path.
  // Note that we want to notify the change in both old and new paths (suppose
  // /a/b/c is moved to /x/y/c. We want to notify both "/a/b" and "/x/y".)
  // The old paths must be calculated before we apply any actual changes.
  // The new paths are calculated after each change is applied. It correctly
  // sets the new path because we apply changes in such an order (see below).
  for (ResourceEntryMap::iterator it = entry_map_.begin();
       it != entry_map_.end(); ++it) {
    UpdateChangedDirs(it->second);
  }

  // Apply all entries except deleted ones to the metadata.
  std::vector<std::string> deleted_resource_ids;
  while (!entry_map_.empty()) {
    if (in_shutdown_ && in_shutdown_->IsSet())
      return FILE_ERROR_ABORT;

    ResourceEntryMap::iterator it = entry_map_.begin();

    // Process deleted entries later to avoid deleting moved entries under it.
    if (it->second.deleted()) {
      deleted_resource_ids.push_back(it->first);
      entry_map_.erase(it);
      continue;
    }

    // Start from entry_map_.begin() and traverse ancestors using the
    // parent-child relationships in the result (after this apply) tree.
    // Then apply the topmost change first.
    //
    // By doing this, assuming the result tree does not contain any cycles, we
    // can guarantee that no cycle is made during this apply (i.e. no entry gets
    // moved under any of its descendants) because the following conditions are
    // always satisfied in any move:
    // - The new parent entry is not a descendant of the moved entry.
    // - The new parent and its ancestors will no longer move during this apply.
    std::vector<ResourceEntryMap::iterator> entries;
    for (ResourceEntryMap::iterator it = entry_map_.begin();
         it != entry_map_.end();) {
      entries.push_back(it);

      DCHECK(parent_resource_id_map_.count(it->first)) << it->first;
      const std::string& parent_resource_id =
          parent_resource_id_map_[it->first];

      if (parent_resource_id.empty())  // This entry has no parent.
        break;

      ResourceEntryMap::iterator it_parent =
          entry_map_.find(parent_resource_id);
      if (it_parent == entry_map_.end()) {
        // Current entry's parent is already updated or not going to be updated,
        // get the parent from the local tree.
        std::string parent_local_id;
        FileError error = resource_metadata_->GetIdByResourceId(
            parent_resource_id, &parent_local_id);
        if (error != FILE_ERROR_OK) {
          // See crbug.com/326043. In some complicated situations, parent folder
          // for shared entries may be accessible (and hence its resource id is
          // included), but not in the change/file list.
          // In such a case, clear the parent and move it to drive/other.
          if (error == FILE_ERROR_NOT_FOUND) {
            parent_resource_id_map_[it->first] = "";
          } else {
            LOG(ERROR) << "Failed to get local ID: " << parent_resource_id
                       << ", error = " << FileErrorToString(error);
          }
          break;
        }
        ResourceEntry parent_entry;
        while (it_parent == entry_map_.end() && !parent_local_id.empty()) {
          error = resource_metadata_->GetResourceEntryById(
              parent_local_id, &parent_entry);
          if (error != FILE_ERROR_OK) {
            LOG(ERROR) << "Failed to get local entry: "
                       << FileErrorToString(error);
            break;
          }
          it_parent = entry_map_.find(parent_entry.resource_id());
          parent_local_id = parent_entry.parent_local_id();
        }
      }
      it = it_parent;
    }

    // Apply the parent first.
    std::reverse(entries.begin(), entries.end());
    for (size_t i = 0; i < entries.size(); ++i) {
      // Skip root entry in the change list. We don't expect servers to send
      // root entry, but we should better be defensive (see crbug.com/297259).
      ResourceEntryMap::iterator it = entries[i];
      if (it->first != root.resource_id()) {
        FileError error = ApplyEntry(it->second);
        if (error != FILE_ERROR_OK) {
          LOG(ERROR) << "ApplyEntry failed: " << FileErrorToString(error)
                     << ", title = " << it->second.title();
          return error;
        }
      }
      entry_map_.erase(it);
    }
  }

  // Apply deleted entries.
  for (size_t i = 0; i < deleted_resource_ids.size(); ++i) {
    std::string local_id;
    FileError error = resource_metadata_->GetIdByResourceId(
        deleted_resource_ids[i], &local_id);
    switch (error) {
      case FILE_ERROR_OK:
        error = resource_metadata_->RemoveEntry(local_id);
        break;
      case FILE_ERROR_NOT_FOUND:
        error = FILE_ERROR_OK;
        break;
      default:
        break;
    }
    if (error != FILE_ERROR_OK) {
      LOG(ERROR) << "Failed to delete: " << FileErrorToString(error)
                 << ", resource_id = " << deleted_resource_ids[i];
      return error;
    }
  }

  return FILE_ERROR_OK;
}

FileError ChangeListProcessor::ApplyEntry(const ResourceEntry& entry) {
  DCHECK(!entry.deleted());
  DCHECK(parent_resource_id_map_.count(entry.resource_id()));
  const std::string& parent_resource_id =
      parent_resource_id_map_[entry.resource_id()];

  ResourceEntry new_entry(entry);
  FileError error = SetParentLocalIdOfEntry(resource_metadata_, &new_entry,
                                            parent_resource_id);
  if (error != FILE_ERROR_OK)
    return error;

  // Lookup the entry.
  std::string local_id;
  error = resource_metadata_->GetIdByResourceId(entry.resource_id(), &local_id);

  ResourceEntry existing_entry;
  if (error == FILE_ERROR_OK)
    error = resource_metadata_->GetResourceEntryById(local_id, &existing_entry);

  switch (error) {
    case FILE_ERROR_OK:
      if (ShouldApplyChange(existing_entry, new_entry)) {
        // Entry exists and needs to be refreshed.
        new_entry.set_local_id(local_id);
        // Keep the to-be-synced properties of the existing resource entry.
        new_entry.mutable_new_properties()->CopyFrom(
            existing_entry.new_properties());
        error = resource_metadata_->RefreshEntry(new_entry);
      } else {
        if (entry.file_info().is_directory()) {
          // No need to refresh, but update the changestamp.
          new_entry = existing_entry;
          new_entry.mutable_directory_specific_info()->set_changestamp(
              new_entry.directory_specific_info().changestamp());
          error = resource_metadata_->RefreshEntry(new_entry);
        }
        DVLOG(1) << "Change was discarded for: " << entry.resource_id();
      }
      break;
    case FILE_ERROR_NOT_FOUND: {  // Adding a new entry.
      std::string local_id;
      error = resource_metadata_->AddEntry(new_entry, &local_id);
      break;
    }
    default:
      return error;
  }
  if (error != FILE_ERROR_OK)
    return error;

  UpdateChangedDirs(entry);
  return FILE_ERROR_OK;
}

// static
FileError ChangeListProcessor::RefreshDirectory(
    ResourceMetadata* resource_metadata,
    const DirectoryFetchInfo& directory_fetch_info,
    std::unique_ptr<ChangeList> change_list,
    std::vector<ResourceEntry>* out_refreshed_entries) {
  DCHECK(!directory_fetch_info.empty());

  ResourceEntry directory;
  FileError error = resource_metadata->GetResourceEntryById(
      directory_fetch_info.local_id(), &directory);
  if (error != FILE_ERROR_OK)
    return error;

  if (!directory.file_info().is_directory())
    return FILE_ERROR_NOT_A_DIRECTORY;

  std::vector<ResourceEntry>* entries = change_list->mutable_entries();
  for (size_t i = 0; i < entries->size(); ++i) {
    ResourceEntry* entry = &(*entries)[i];
    const std::string& parent_resource_id =
        change_list->parent_resource_ids()[i];

    // Skip if the parent resource ID does not match. This is needed to
    // handle entries with multiple parents. For such entries, the first
    // parent is picked and other parents are ignored, hence some entries may
    // have a parent resource ID which does not match the target directory's.
    if (parent_resource_id != directory_fetch_info.resource_id()) {
      DVLOG(1) << "Wrong-parent entry rejected: " << entry->resource_id();
      continue;
    }

    entry->set_parent_local_id(directory_fetch_info.local_id());

    std::string local_id;
    error = resource_metadata->GetIdByResourceId(entry->resource_id(),
                                                 &local_id);
    if (error == FILE_ERROR_OK) {
      entry->set_local_id(local_id);
      error = resource_metadata->RefreshEntry(*entry);
    }

    if (error == FILE_ERROR_NOT_FOUND) {  // If refreshing fails, try adding.
      entry->clear_local_id();
      error = resource_metadata->AddEntry(*entry, &local_id);
    }

    if (error != FILE_ERROR_OK)
      return error;

    ResourceEntry result_entry;
    error = resource_metadata->GetResourceEntryById(local_id, &result_entry);
    if (error != FILE_ERROR_OK)
      return error;
    out_refreshed_entries->push_back(result_entry);
  }
  return FILE_ERROR_OK;
}

// static
FileError ChangeListProcessor::SetParentLocalIdOfEntry(
    ResourceMetadata* resource_metadata,
    ResourceEntry* entry,
    const std::string& parent_resource_id) {
  std::string parent_local_id;
  if (parent_resource_id.empty()) {
    // Entries without parents should go under "other" directory.
    parent_local_id = util::kDriveOtherDirLocalId;
  } else {
    FileError error = resource_metadata->GetIdByResourceId(
        parent_resource_id, &parent_local_id);
    if (error != FILE_ERROR_OK)
      return error;
  }
  entry->set_parent_local_id(parent_local_id);
  return FILE_ERROR_OK;
}

void ChangeListProcessor::UpdateChangedDirs(const ResourceEntry& entry) {
  DCHECK(!entry.resource_id().empty());

  std::string local_id;
  base::FilePath file_path;
  if (resource_metadata_->GetIdByResourceId(
          entry.resource_id(), &local_id) == FILE_ERROR_OK)
    resource_metadata_->GetFilePath(local_id, &file_path);

  if (!file_path.empty()) {
    FileChange::ChangeType type = entry.deleted()
                                      ? FileChange::CHANGE_TYPE_DELETE
                                      : FileChange::CHANGE_TYPE_ADD_OR_UPDATE;
    changed_files_->Update(file_path, entry, type);
  }
}

}  // namespace internal
}  // namespace drive
