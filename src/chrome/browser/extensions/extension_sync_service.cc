// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_sync_service.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/bookmark_app_helper.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_sync_data.h"
#include "chrome/browser/extensions/extension_sync_service_factory.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/launch_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/glue/sync_start_util.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/sync_helper.h"
#include "chrome/common/web_application_info.h"
#include "extensions/browser/app_sorting.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/image_util.h"
#include "extensions/common/permissions/permission_message_provider.h"
#include "extensions/common/permissions/permissions_data.h"
#include "sync/api/sync_change.h"
#include "sync/api/sync_error_factory.h"

#if defined(ENABLE_SUPERVISED_USERS)
#include "chrome/browser/supervised_user/supervised_user_service.h"
#include "chrome/browser/supervised_user/supervised_user_service_factory.h"
#endif

using extensions::AppSorting;
using extensions::Extension;
using extensions::ExtensionPrefs;
using extensions::ExtensionRegistry;
using extensions::ExtensionSet;
using extensions::ExtensionSyncData;
using extensions::ExtensionSystem;
using extensions::SyncBundle;

namespace {

void OnWebApplicationInfoLoaded(
    WebApplicationInfo synced_info,
    base::WeakPtr<ExtensionService> extension_service,
    const WebApplicationInfo& loaded_info) {
  DCHECK_EQ(synced_info.app_url, loaded_info.app_url);

  if (!extension_service)
    return;

  // Use the old icons if they exist.
  synced_info.icons = loaded_info.icons;
  CreateOrUpdateBookmarkApp(extension_service.get(), &synced_info);
}

// Returns the pref value for "all urls enabled" for the given extension id.
ExtensionSyncData::OptionalBoolean GetAllowedOnAllUrlsOptionalBoolean(
    const std::string& extension_id,
    content::BrowserContext* context) {
  bool allowed_on_all_urls =
      extensions::util::AllowedScriptingOnAllUrls(extension_id, context);
  // If the extension is not allowed on all urls (which is not the default),
  // then we have to sync the preference.
  if (!allowed_on_all_urls)
    return ExtensionSyncData::BOOLEAN_FALSE;

  // If the user has explicitly set a value, then we sync it.
  if (extensions::util::HasSetAllowedScriptingOnAllUrls(extension_id, context))
    return ExtensionSyncData::BOOLEAN_TRUE;

  // Otherwise, unset.
  return ExtensionSyncData::BOOLEAN_UNSET;
}

// Returns true if the sync type of |extension| matches |type|.
bool IsCorrectSyncType(const Extension& extension, syncer::ModelType type) {
  return (type == syncer::EXTENSIONS && extension.is_extension()) ||
         (type == syncer::APPS && extension.is_app());
}

// Predicate for PendingExtensionManager.
// TODO(treib,devlin): The !is_theme check shouldn't be necessary anymore after
// all the bad data from crbug.com/558299 has been cleaned up, after M52 or so.
bool ShouldAllowInstall(const Extension* extension) {
  return !extension->is_theme() &&
         extensions::sync_helper::IsSyncable(extension);
}

syncer::SyncDataList ToSyncerSyncDataList(
    const std::vector<ExtensionSyncData>& data) {
  syncer::SyncDataList result;
  result.reserve(data.size());
  for (const ExtensionSyncData& item : data)
    result.push_back(item.GetSyncData());
  return result;
}

static_assert(Extension::DISABLE_REASON_LAST == (1 << 15),
              "Please consider whether your new disable reason should be"
              " syncable, and if so update this bitmask accordingly!");
const int kKnownSyncableDisableReasons =
    Extension::DISABLE_USER_ACTION |
    Extension::DISABLE_PERMISSIONS_INCREASE |
    Extension::DISABLE_SIDELOAD_WIPEOUT |
    Extension::DISABLE_GREYLIST |
    Extension::DISABLE_REMOTE_INSTALL;
const int kAllKnownDisableReasons = Extension::DISABLE_REASON_LAST - 1;
// We also include any future bits for newer clients that added another disable
// reason.
const int kSyncableDisableReasons =
    kKnownSyncableDisableReasons | ~kAllKnownDisableReasons;

}  // namespace

struct ExtensionSyncService::PendingUpdate {
  PendingUpdate() : grant_permissions_and_reenable(false) {}
  PendingUpdate(const base::Version& version,
                bool grant_permissions_and_reenable)
    : version(version),
      grant_permissions_and_reenable(grant_permissions_and_reenable) {}

  base::Version version;
  bool grant_permissions_and_reenable;
};

ExtensionSyncService::ExtensionSyncService(Profile* profile)
    : profile_(profile),
      registry_observer_(this),
      prefs_observer_(this),
      ignore_updates_(false),
      flare_(sync_start_util::GetFlareForSyncableService(profile->GetPath())) {
  registry_observer_.Add(ExtensionRegistry::Get(profile_));
  prefs_observer_.Add(ExtensionPrefs::Get(profile_));
}

ExtensionSyncService::~ExtensionSyncService() {
}

// static
ExtensionSyncService* ExtensionSyncService::Get(
    content::BrowserContext* context) {
  return ExtensionSyncServiceFactory::GetForBrowserContext(context);
}

void ExtensionSyncService::SyncExtensionChangeIfNeeded(
    const Extension& extension) {
  if (ignore_updates_ || !ShouldSync(extension))
    return;

  syncer::ModelType type =
      extension.is_app() ? syncer::APPS : syncer::EXTENSIONS;
  SyncBundle* bundle = GetSyncBundle(type);
  if (bundle->IsSyncing()) {
    bundle->PushSyncAddOrUpdate(extension.id(),
                                CreateSyncData(extension).GetSyncData());
    DCHECK(!ExtensionPrefs::Get(profile_)->NeedsSync(extension.id()));
  } else {
    ExtensionPrefs::Get(profile_)->SetNeedsSync(extension.id(), true);
    if (extension_service()->is_ready() && !flare_.is_null())
      flare_.Run(type);  // Tell sync to start ASAP.
  }
}

bool ExtensionSyncService::HasPendingReenable(
    const std::string& id,
    const base::Version& version) const {
  auto it = pending_updates_.find(id);
  if (it == pending_updates_.end())
    return false;
  const PendingUpdate& pending = it->second;
  return pending.version == version &&
         pending.grant_permissions_and_reenable;
}

syncer::SyncMergeResult ExtensionSyncService::MergeDataAndStartSyncing(
    syncer::ModelType type,
    const syncer::SyncDataList& initial_sync_data,
    std::unique_ptr<syncer::SyncChangeProcessor> sync_processor,
    std::unique_ptr<syncer::SyncErrorFactory> sync_error_factory) {
  CHECK(sync_processor.get());
  LOG_IF(FATAL, type != syncer::EXTENSIONS && type != syncer::APPS)
      << "Got " << type << " ModelType";

  SyncBundle* bundle = GetSyncBundle(type);
  bundle->StartSyncing(std::move(sync_processor));

  // Apply the initial sync data, filtering out any items where we have more
  // recent local changes. Also tell the SyncBundle the extension IDs.
  for (const syncer::SyncData& sync_data : initial_sync_data) {
    std::unique_ptr<ExtensionSyncData> extension_sync_data(
        ExtensionSyncData::CreateFromSyncData(sync_data));
    // If the extension has local state that needs to be synced, ignore this
    // change (we assume the local state is more recent).
    if (extension_sync_data &&
        !ExtensionPrefs::Get(profile_)->NeedsSync(extension_sync_data->id())) {
      ApplySyncData(*extension_sync_data);
    }
  }

  // Now push the local state to sync.
  // Note: We'd like to only send out changes for extensions which have
  // NeedsSync set. However, we can't tell if our changes ever made it to the
  // sync server (they might not e.g. when there's a temporary auth error), so
  // we couldn't safely clear the flag. So just send out everything and let the
  // sync client handle no-op changes.
  std::vector<ExtensionSyncData> data_list = GetLocalSyncDataList(type);
  bundle->PushSyncDataList(ToSyncerSyncDataList(data_list));

  for (const ExtensionSyncData& data : data_list)
    ExtensionPrefs::Get(profile_)->SetNeedsSync(data.id(), false);

  if (type == syncer::APPS)
    ExtensionSystem::Get(profile_)->app_sorting()->FixNTPOrdinalCollisions();

  return syncer::SyncMergeResult(type);
}

void ExtensionSyncService::StopSyncing(syncer::ModelType type) {
  GetSyncBundle(type)->Reset();
}

syncer::SyncDataList ExtensionSyncService::GetAllSyncData(
    syncer::ModelType type) const {
  const SyncBundle* bundle = GetSyncBundle(type);
  if (!bundle->IsSyncing())
    return syncer::SyncDataList();

  std::vector<ExtensionSyncData> sync_data_list = GetLocalSyncDataList(type);

  // Add pending data (where the local extension is not installed yet).
  std::vector<ExtensionSyncData> pending_extensions =
      bundle->GetPendingExtensionData();
  sync_data_list.insert(sync_data_list.begin(),
                        pending_extensions.begin(),
                        pending_extensions.end());

  return ToSyncerSyncDataList(sync_data_list);
}

syncer::SyncError ExtensionSyncService::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const syncer::SyncChangeList& change_list) {
  for (const syncer::SyncChange& sync_change : change_list) {
    std::unique_ptr<ExtensionSyncData> extension_sync_data(
        ExtensionSyncData::CreateFromSyncChange(sync_change));
    if (extension_sync_data)
      ApplySyncData(*extension_sync_data);
  }

  ExtensionSystem::Get(profile_)->app_sorting()->FixNTPOrdinalCollisions();

  return syncer::SyncError();
}

ExtensionSyncData ExtensionSyncService::CreateSyncData(
    const Extension& extension) const {
  const std::string& id = extension.id();
  const ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(profile_);
  int disable_reasons =
      extension_prefs->GetDisableReasons(id) & kSyncableDisableReasons;
  // Note that we're ignoring the enabled state during ApplySyncData (we check
  // for the existence of disable reasons instead), we're just setting it here
  // for older Chrome versions (<M48).
  bool enabled = (disable_reasons == Extension::DISABLE_NONE);
  enabled = enabled &&
      extension_prefs->GetExtensionBlacklistState(extension.id()) ==
          extensions::NOT_BLACKLISTED;
  bool incognito_enabled = extensions::util::IsIncognitoEnabled(id, profile_);
  bool remote_install =
      extension_prefs->HasDisableReason(id, Extension::DISABLE_REMOTE_INSTALL);
  ExtensionSyncData::OptionalBoolean allowed_on_all_url =
      GetAllowedOnAllUrlsOptionalBoolean(id, profile_);
  AppSorting* app_sorting = ExtensionSystem::Get(profile_)->app_sorting();

  ExtensionSyncData result = extension.is_app()
      ? ExtensionSyncData(
            extension, enabled, disable_reasons, incognito_enabled,
            remote_install, allowed_on_all_url,
            app_sorting->GetAppLaunchOrdinal(id),
            app_sorting->GetPageOrdinal(id),
            extensions::GetLaunchTypePrefValue(extension_prefs, id))
      : ExtensionSyncData(
            extension, enabled, disable_reasons, incognito_enabled,
            remote_install, allowed_on_all_url);

  // If there's a pending update, send the new version to sync instead of the
  // installed one.
  auto it = pending_updates_.find(id);
  if (it != pending_updates_.end()) {
    const base::Version& version = it->second.version;
    // If we have a pending version, it should be newer than the installed one.
    DCHECK_EQ(-1, extension.version()->CompareTo(version));
    result.set_version(version);
    // If we'll re-enable the extension once it's updated, also send that back
    // to sync.
    if (it->second.grant_permissions_and_reenable)
      result.set_enabled(true);
  }
  return result;
}

void ExtensionSyncService::ApplySyncData(
    const ExtensionSyncData& extension_sync_data) {
  // Ignore any pref change notifications etc. while we're applying incoming
  // sync data, so that we don't end up notifying ourselves.
  base::AutoReset<bool> ignore_updates(&ignore_updates_, true);

  syncer::ModelType type = extension_sync_data.is_app() ? syncer::APPS
                                                        : syncer::EXTENSIONS;
  const std::string& id = extension_sync_data.id();
  SyncBundle* bundle = GetSyncBundle(type);
  DCHECK(bundle->IsSyncing());
  // Note: |extension| may be null if it hasn't been installed yet.
  const Extension* extension =
      ExtensionRegistry::Get(profile_)->GetInstalledExtension(id);
  if (extension && !IsCorrectSyncType(*extension, type)) {
    // The installed item isn't the same type as the sync data item, so we need
    // to remove the sync data item; otherwise it will be a zombie that will
    // keep coming back even if the installed item with this id is uninstalled.
    // First tell the bundle about the extension, so that it won't just ignore
    // the deletion, then push the deletion.
    bundle->ApplySyncData(extension_sync_data);
    bundle->PushSyncDeletion(id, extension_sync_data.GetSyncData());
    return;
  }

  // Forward to the bundle. This will just update the list of synced extensions.
  bundle->ApplySyncData(extension_sync_data);

  // Handle uninstalls first.
  if (extension_sync_data.uninstalled()) {
    if (!ExtensionService::UninstallExtensionHelper(
            extension_service(), id, extensions::UNINSTALL_REASON_SYNC)) {
      LOG(WARNING) << "Could not uninstall extension " << id << " for sync";
    }
    return;
  }

  // Extension from sync was uninstalled by the user as an external extension.
  // Honor user choice and skip installation/enabling.
  // TODO(treib): Should we still apply pref changes?
  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(profile_);
  if (extension_prefs->IsExternalExtensionUninstalled(id)) {
    LOG(WARNING) << "Extension with id " << id
                 << " from sync was uninstalled as external extension";
    return;
  }

  enum {
    NOT_INSTALLED,
    INSTALLED_OUTDATED,
    INSTALLED_MATCHING,
    INSTALLED_NEWER,
  } state = NOT_INSTALLED;
  if (extension) {
    switch (extension->version()->CompareTo(extension_sync_data.version())) {
      case -1: state = INSTALLED_OUTDATED; break;
      case 0: state = INSTALLED_MATCHING; break;
      case 1: state = INSTALLED_NEWER; break;
      default: NOTREACHED();
    }
  }

  // Figure out the resulting set of disable reasons.
  int disable_reasons = extension_prefs->GetDisableReasons(id);

  // Chrome versions M37-M44 used |extension_sync_data.remote_install()| to tag
  // not-yet-approved remote installs. It's redundant now that disable reasons
  // are synced (DISABLE_REMOTE_INSTALL should be among them already), but some
  // old sync data may still be around, and it doesn't hurt to add the reason.
  // TODO(treib,devlin): Deprecate and eventually remove |remote_install|?
  if (extension_sync_data.remote_install())
    disable_reasons |= Extension::DISABLE_REMOTE_INSTALL;

  // Add/remove disable reasons based on the incoming sync data.
  int incoming_disable_reasons = extension_sync_data.disable_reasons();
  if (!!incoming_disable_reasons == extension_sync_data.enabled()) {
    // The enabled flag disagrees with the presence of disable reasons. This
    // must either come from an old (<M45) client which doesn't sync disable
    // reasons, or the extension is blacklisted (which doesn't have a
    // corresponding disable reason).
    // Update |disable_reasons| based on the enabled flag.
    if (extension_sync_data.enabled())
      disable_reasons &= ~kKnownSyncableDisableReasons;
    else  // Assume the extension was likely disabled by the user.
      disable_reasons |= Extension::DISABLE_USER_ACTION;
  } else {
    // Replace the syncable disable reasons:
    // 1. Remove any syncable disable reasons we might have.
    disable_reasons &= ~kSyncableDisableReasons;
    // 2. Add the incoming reasons. Mask with |kSyncableDisableReasons|, because
    //    Chrome M45-M47 also wrote local disable reasons to sync, and we don't
    //    want those.
    disable_reasons |= incoming_disable_reasons & kSyncableDisableReasons;
  }

  // Enable/disable the extension.
  bool should_be_enabled = (disable_reasons == Extension::DISABLE_NONE);
  bool reenable_after_update = false;
  if (should_be_enabled && !extension_service()->IsExtensionEnabled(id)) {
    if (extension) {
      // Only grant permissions if the sync data explicitly sets the disable
      // reasons to Extension::DISABLE_NONE (as opposed to the legacy (<M45)
      // case where they're not set at all), and if the version from sync
      // matches our local one.
      bool grant_permissions = extension_sync_data.supports_disable_reasons() &&
                               (state == INSTALLED_MATCHING);
      if (grant_permissions)
        extension_service()->GrantPermissions(extension);

      // Only enable if the extension has all required permissions.
      // (Even if the version doesn't match - if the new version needs more
      // permissions, it'll get disabled after the update.)
      bool has_all_permissions =
          grant_permissions ||
          !extensions::PermissionMessageProvider::Get()->IsPrivilegeIncrease(
              *extension_prefs->GetGrantedPermissions(id),
              extension->permissions_data()->active_permissions(),
              extension->GetType());
      if (has_all_permissions)
        extension_service()->EnableExtension(id);
      else if (extension_sync_data.supports_disable_reasons())
        reenable_after_update = true;

#if defined(ENABLE_SUPERVISED_USERS)
      if (!has_all_permissions && (state == INSTALLED_NEWER) &&
          extensions::util::IsExtensionSupervised(extension, profile_)) {
        SupervisedUserServiceFactory::GetForProfile(profile_)
            ->AddExtensionUpdateRequest(id, *extension->version());
      }
#endif
    } else {
      // The extension is not installed yet. Set it to enabled; we'll check for
      // permission increase (more accurately, for a version change) when it's
      // actually installed.
      extension_service()->EnableExtension(id);
    }
  } else if (!should_be_enabled) {
    // Note that |disable_reasons| includes any pre-existing reasons that
    // weren't explicitly removed above.
    if (extension_service()->IsExtensionEnabled(id))
      extension_service()->DisableExtension(id, disable_reasons);
    else  // Already disabled, just replace the disable reasons.
      extension_prefs->ReplaceDisableReasons(id, disable_reasons);
  }

  // Update the incognito flag.
  extensions::util::SetIsIncognitoEnabled(
      id, profile_, extension_sync_data.incognito_enabled());
  extension = nullptr;  // No longer safe to use.

  // Update the all urls flag.
  if (extension_sync_data.all_urls_enabled() !=
          ExtensionSyncData::BOOLEAN_UNSET) {
    bool allowed = extension_sync_data.all_urls_enabled() ==
        ExtensionSyncData::BOOLEAN_TRUE;
    extensions::util::SetAllowedScriptingOnAllUrls(id, profile_, allowed);
  }

  // Set app-specific data.
  if (extension_sync_data.is_app()) {
    if (extension_sync_data.app_launch_ordinal().IsValid() &&
        extension_sync_data.page_ordinal().IsValid()) {
      AppSorting* app_sorting = ExtensionSystem::Get(profile_)->app_sorting();
      app_sorting->SetAppLaunchOrdinal(
          id,
          extension_sync_data.app_launch_ordinal());
      app_sorting->SetPageOrdinal(id, extension_sync_data.page_ordinal());
    }

    // The corresponding validation of this value during ExtensionSyncData
    // population is in ExtensionSyncData::ToAppSpecifics.
    if (extension_sync_data.launch_type() >= extensions::LAUNCH_TYPE_FIRST &&
        extension_sync_data.launch_type() < extensions::NUM_LAUNCH_TYPES) {
      extensions::SetLaunchType(
          profile_, id, extension_sync_data.launch_type());
    }

    if (!extension_sync_data.bookmark_app_url().empty())
      ApplyBookmarkAppSyncData(extension_sync_data);
  }

  // Finally, trigger installation/update as required.
  bool check_for_updates = false;
  if (state == INSTALLED_OUTDATED) {
    // If the extension is installed but outdated, store the new version.
    pending_updates_[id] =
        PendingUpdate(extension_sync_data.version(), reenable_after_update);
    check_for_updates = true;
  } else if (state == NOT_INSTALLED) {
    if (!extension_service()->pending_extension_manager()->AddFromSync(
            id,
            extension_sync_data.update_url(),
            extension_sync_data.version(),
            ShouldAllowInstall,
            extension_sync_data.remote_install(),
            extension_sync_data.installed_by_custodian())) {
      LOG(WARNING) << "Could not add pending extension for " << id;
      // This means that the extension is already pending installation, with a
      // non-INTERNAL location.  Add to pending_sync_data, even though it will
      // never be removed (we'll never install a syncable version of the
      // extension), so that GetAllSyncData() continues to send it.
    }
    // Track pending extensions so that we can return them in GetAllSyncData().
    bundle->AddPendingExtensionData(extension_sync_data);
    check_for_updates = true;
  }

  if (check_for_updates)
    extension_service()->CheckForUpdatesSoon();
}

void ExtensionSyncService::ApplyBookmarkAppSyncData(
    const ExtensionSyncData& extension_sync_data) {
  DCHECK(extension_sync_data.is_app());

  // Process bookmark app sync if necessary.
  GURL bookmark_app_url(extension_sync_data.bookmark_app_url());
  if (!bookmark_app_url.is_valid() ||
      extension_sync_data.uninstalled()) {
    return;
  }

  const Extension* extension =
      extension_service()->GetInstalledExtension(extension_sync_data.id());

  // Return if there are no bookmark app details that need updating.
  if (extension &&
      extension->non_localized_name() == extension_sync_data.name() &&
      extension->description() ==
          extension_sync_data.bookmark_app_description()) {
    return;
  }

  WebApplicationInfo web_app_info;
  web_app_info.app_url = bookmark_app_url;
  web_app_info.title = base::UTF8ToUTF16(extension_sync_data.name());
  web_app_info.description =
      base::UTF8ToUTF16(extension_sync_data.bookmark_app_description());
  if (!extension_sync_data.bookmark_app_icon_color().empty()) {
    extensions::image_util::ParseHexColorString(
        extension_sync_data.bookmark_app_icon_color(),
        &web_app_info.generated_icon_color);
  }
  for (const auto& icon : extension_sync_data.linked_icons()) {
    WebApplicationInfo::IconInfo icon_info;
    icon_info.url = icon.url;
    icon_info.width = icon.size;
    icon_info.height = icon.size;
    web_app_info.icons.push_back(icon_info);
  }

  // If the bookmark app already exists, keep the old icons.
  if (!extension) {
    CreateOrUpdateBookmarkApp(extension_service(), &web_app_info);
  } else {
    GetWebApplicationInfoFromApp(profile_,
                                 extension,
                                 base::Bind(&OnWebApplicationInfoLoaded,
                                            web_app_info,
                                            extension_service()->AsWeakPtr()));
  }
}

void ExtensionSyncService::SetSyncStartFlareForTesting(
    const syncer::SyncableService::StartSyncFlare& flare) {
  flare_ = flare;
}

void ExtensionSyncService::DeleteThemeDoNotUse(const Extension& theme) {
  DCHECK(theme.is_theme());
  GetSyncBundle(syncer::EXTENSIONS)->PushSyncDeletion(
      theme.id(), CreateSyncData(theme).GetSyncData());
}

ExtensionService* ExtensionSyncService::extension_service() const {
  return ExtensionSystem::Get(profile_)->extension_service();
}

void ExtensionSyncService::OnExtensionInstalled(
    content::BrowserContext* browser_context,
    const Extension* extension,
    bool is_update) {
  DCHECK_EQ(profile_, browser_context);
  // Clear pending version if the installed one has caught up.
  auto it = pending_updates_.find(extension->id());
  if (it != pending_updates_.end()) {
    int compare_result = extension->version()->CompareTo(it->second.version);
    if (compare_result == 0 && it->second.grant_permissions_and_reenable) {
      // The call to SyncExtensionChangeIfNeeded below will take care of syncing
      // changes to this extension, so we don't want to trigger sync activity
      // from the call to GrantPermissionsAndEnableExtension.
      base::AutoReset<bool> ignore_updates(&ignore_updates_, true);
      extension_service()->GrantPermissionsAndEnableExtension(extension);
    }
    if (compare_result >= 0)
      pending_updates_.erase(it);
  }
  SyncExtensionChangeIfNeeded(*extension);
}

void ExtensionSyncService::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const Extension* extension,
    extensions::UninstallReason reason) {
  DCHECK_EQ(profile_, browser_context);
  // Don't bother syncing if the extension will be re-installed momentarily.
  if (reason == extensions::UNINSTALL_REASON_REINSTALL ||
      !ShouldSync(*extension)) {
    return;
  }

  // TODO(tim): If we get here and IsSyncing is false, this will cause
  // "back from the dead" style bugs, because sync will add-back the extension
  // that was uninstalled here when MergeDataAndStartSyncing is called.
  // See crbug.com/256795.
  // Possible fix: Set NeedsSync here, then in MergeDataAndStartSyncing, if
  // NeedsSync is set but the extension isn't installed, send a sync deletion.
  if (!ignore_updates_) {
    syncer::ModelType type =
        extension->is_app() ? syncer::APPS : syncer::EXTENSIONS;
    SyncBundle* bundle = GetSyncBundle(type);
    if (bundle->IsSyncing()) {
      bundle->PushSyncDeletion(extension->id(),
                               CreateSyncData(*extension).GetSyncData());
    } else if (extension_service()->is_ready() && !flare_.is_null()) {
      flare_.Run(type);  // Tell sync to start ASAP.
    }
  }

  pending_updates_.erase(extension->id());
}

void ExtensionSyncService::OnExtensionStateChanged(
    const std::string& extension_id,
    bool state) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile_);
  const Extension* extension = registry->GetInstalledExtension(extension_id);
  // We can get pref change notifications for extensions that aren't installed
  // (yet). In that case, we'll pick up the change later via ExtensionRegistry
  // observation (in OnExtensionInstalled).
  if (extension)
    SyncExtensionChangeIfNeeded(*extension);
}

void ExtensionSyncService::OnExtensionDisableReasonsChanged(
    const std::string& extension_id,
    int disabled_reasons) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile_);
  const Extension* extension = registry->GetInstalledExtension(extension_id);
  // We can get pref change notifications for extensions that aren't installed
  // (yet). In that case, we'll pick up the change later via ExtensionRegistry
  // observation (in OnExtensionInstalled).
  if (extension)
    SyncExtensionChangeIfNeeded(*extension);
}

SyncBundle* ExtensionSyncService::GetSyncBundle(syncer::ModelType type) {
  return const_cast<SyncBundle*>(
      const_cast<const ExtensionSyncService&>(*this).GetSyncBundle(type));
}

const SyncBundle* ExtensionSyncService::GetSyncBundle(
    syncer::ModelType type) const {
  return (type == syncer::APPS) ? &app_sync_bundle_ : &extension_sync_bundle_;
}

std::vector<ExtensionSyncData> ExtensionSyncService::GetLocalSyncDataList(
    syncer::ModelType type) const {
  // Collect the local state.
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile_);
  std::vector<ExtensionSyncData> data;
  // TODO(treib, kalman): Should we be including blacklisted/blocked extensions
  // here? I.e. just calling registry->GeneratedInstalledExtensionsSet()?
  // It would be more consistent, but the danger is that the black/blocklist
  // hasn't been updated on all clients by the time sync has kicked in -
  // so it's safest not to. Take care to add any other extension lists here
  // in the future if they are added.
  FillSyncDataList(registry->enabled_extensions(), type, &data);
  FillSyncDataList(registry->disabled_extensions(), type, &data);
  FillSyncDataList(registry->terminated_extensions(), type, &data);
  return data;
}

void ExtensionSyncService::FillSyncDataList(
    const ExtensionSet& extensions,
    syncer::ModelType type,
    std::vector<ExtensionSyncData>* sync_data_list) const {
  for (const scoped_refptr<const Extension>& extension : extensions) {
    if (IsCorrectSyncType(*extension, type) && ShouldSync(*extension)) {
      // We should never have pending data for an installed extension.
      DCHECK(!GetSyncBundle(type)->HasPendingExtensionData(extension->id()));
      sync_data_list->push_back(CreateSyncData(*extension));
    }
  }
}

bool ExtensionSyncService::ShouldSync(const Extension& extension) const {
  // Themes are handled by the ThemeSyncableService.
  return extensions::util::ShouldSync(&extension, profile_) &&
         !extension.is_theme();
}
