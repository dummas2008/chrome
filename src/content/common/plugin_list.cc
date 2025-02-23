// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/plugin_list.h"

#include <stddef.h>

#include <algorithm>

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "content/public/common/content_switches.h"
#include "net/base/mime_util.h"
#include "url/gurl.h"

namespace content {

namespace {

base::LazyInstance<PluginList> g_singleton = LAZY_INSTANCE_INITIALIZER;

}  // namespace

// static
PluginList* PluginList::Singleton() {
  return g_singleton.Pointer();
}

void PluginList::RefreshPlugins() {
  base::AutoLock lock(lock_);
  loading_state_ = LOADING_STATE_NEEDS_REFRESH;
}

void PluginList::RegisterInternalPlugin(const WebPluginInfo& info,
                                        bool add_at_beginning) {
  base::AutoLock lock(lock_);

  internal_plugins_.push_back(info);
  if (add_at_beginning) {
    // Newer registrations go earlier in the list so they can override the MIME
    // types of older registrations.
    extra_plugin_paths_.insert(extra_plugin_paths_.begin(), info.path);
  } else {
    extra_plugin_paths_.push_back(info.path);
  }
}

void PluginList::UnregisterInternalPlugin(const base::FilePath& path) {
  base::AutoLock lock(lock_);
  bool found = false;
  for (size_t i = 0; i < internal_plugins_.size(); i++) {
    if (internal_plugins_[i].path == path) {
      internal_plugins_.erase(internal_plugins_.begin() + i);
      found = true;
      break;
    }
  }
  DCHECK(found);
  RemoveExtraPluginPathLocked(path);
}

void PluginList::GetInternalPlugins(
    std::vector<WebPluginInfo>* internal_plugins) {
  base::AutoLock lock(lock_);

  for (std::vector<WebPluginInfo>::iterator it = internal_plugins_.begin();
       it != internal_plugins_.end();
       ++it) {
    internal_plugins->push_back(*it);
  }
}

bool PluginList::ReadPluginInfo(const base::FilePath& filename,
                                WebPluginInfo* info) {
  base::AutoLock lock(lock_);
  for (const auto& plugin : internal_plugins_) {
    if (filename == plugin.path) {
      *info = plugin;
      return true;
    }
  }
  return false;
}

PluginList::PluginList()
    : loading_state_(LOADING_STATE_NEEDS_REFRESH) {
}

bool PluginList::PrepareForPluginLoading() {
  base::AutoLock lock(lock_);
  if (loading_state_ == LOADING_STATE_UP_TO_DATE)
    return false;

  loading_state_ = LOADING_STATE_REFRESHING;
  return true;
}

void PluginList::LoadPlugins() {
  if (!PrepareForPluginLoading())
    return;

  std::vector<WebPluginInfo> new_plugins;
  base::Closure will_load_callback;
  {
    base::AutoLock lock(lock_);
    will_load_callback = will_load_plugins_callback_;
  }
  if (!will_load_callback.is_null())
    will_load_callback.Run();

  std::vector<base::FilePath> plugin_paths;
  GetPluginPathsToLoad(&plugin_paths);

  for (std::vector<base::FilePath>::const_iterator it = plugin_paths.begin();
       it != plugin_paths.end();
       ++it) {
    WebPluginInfo plugin_info;
    LoadPluginIntoPluginList(*it, &new_plugins, &plugin_info);
  }

  SetPlugins(new_plugins);
}

bool PluginList::LoadPluginIntoPluginList(
    const base::FilePath& path,
    std::vector<WebPluginInfo>* plugins,
    WebPluginInfo* plugin_info) {
  if (!ReadPluginInfo(path, plugin_info))
    return false;

  // TODO(piman): Do we still need this after NPAPI removal?
  for (size_t i = 0; i < plugin_info->mime_types.size(); ++i) {
    // TODO: don't load global handlers for now.
    // WebKit hands to the Plugin before it tries
    // to handle mimeTypes on its own.
    const std::string &mime_type = plugin_info->mime_types[i].mime_type;
    if (mime_type == "*")
      return false;
  }
  plugins->push_back(*plugin_info);
  return true;
}

void PluginList::GetPluginPathsToLoad(
    std::vector<base::FilePath>* plugin_paths) {
  // Don't want to hold the lock while loading new plugins, so we don't block
  // other methods if they're called on other threads.
  std::vector<base::FilePath> extra_plugin_paths;
  {
    base::AutoLock lock(lock_);
    extra_plugin_paths = extra_plugin_paths_;
  }

  for (size_t i = 0; i < extra_plugin_paths.size(); ++i) {
    const base::FilePath& path = extra_plugin_paths[i];
    if (std::find(plugin_paths->begin(), plugin_paths->end(), path) !=
        plugin_paths->end()) {
      continue;
    }
    plugin_paths->push_back(path);
  }
}

void PluginList::SetPlugins(const std::vector<WebPluginInfo>& plugins) {
  base::AutoLock lock(lock_);

  // If we haven't been invalidated in the mean time, mark the plugin list as
  // up-to-date.
  if (loading_state_ != LOADING_STATE_NEEDS_REFRESH)
    loading_state_ = LOADING_STATE_UP_TO_DATE;

  plugins_list_ = plugins;
}

void PluginList::set_will_load_plugins_callback(const base::Closure& callback) {
  base::AutoLock lock(lock_);
  will_load_plugins_callback_ = callback;
}

void PluginList::GetPlugins(std::vector<WebPluginInfo>* plugins) {
  LoadPlugins();
  base::AutoLock lock(lock_);
  plugins->insert(plugins->end(), plugins_list_.begin(), plugins_list_.end());
}

bool PluginList::GetPluginsNoRefresh(std::vector<WebPluginInfo>* plugins) {
  base::AutoLock lock(lock_);
  plugins->insert(plugins->end(), plugins_list_.begin(), plugins_list_.end());

  return loading_state_ == LOADING_STATE_UP_TO_DATE;
}

void PluginList::GetPluginInfoArray(
    const GURL& url,
    const std::string& mime_type,
    bool allow_wildcard,
    bool* use_stale,
    std::vector<WebPluginInfo>* info,
    std::vector<std::string>* actual_mime_types) {
  DCHECK(mime_type == base::ToLowerASCII(mime_type));
  DCHECK(info);

  if (!use_stale)
    LoadPlugins();
  base::AutoLock lock(lock_);
  if (use_stale)
    *use_stale = (loading_state_ != LOADING_STATE_UP_TO_DATE);
  info->clear();
  if (actual_mime_types)
    actual_mime_types->clear();

  std::set<base::FilePath> visited_plugins;

  // Add in plugins by mime type.
  for (size_t i = 0; i < plugins_list_.size(); ++i) {
    if (SupportsType(plugins_list_[i], mime_type, allow_wildcard)) {
      base::FilePath path = plugins_list_[i].path;
      if (visited_plugins.insert(path).second) {
        info->push_back(plugins_list_[i]);
        if (actual_mime_types)
          actual_mime_types->push_back(mime_type);
      }
    }
  }

  // Add in plugins by url.
  // We do not permit URL-sniff based plugin MIME type overrides aside from
  // the case where the "type" was initially missing.
  // We collected stats to determine this approach isn't a major compat issue,
  // and we defend against content confusion attacks in various cases, such
  // as when the user doesn't have the Flash plugin enabled.
  std::string path = url.path();
  std::string::size_type last_dot = path.rfind('.');
  if (last_dot != std::string::npos && mime_type.empty()) {
    std::string extension =
        base::ToLowerASCII(base::StringPiece(path).substr(last_dot + 1));
    std::string actual_mime_type;
    for (size_t i = 0; i < plugins_list_.size(); ++i) {
      if (SupportsExtension(plugins_list_[i], extension, &actual_mime_type)) {
        base::FilePath path = plugins_list_[i].path;
        if (visited_plugins.insert(path).second) {
          info->push_back(plugins_list_[i]);
          if (actual_mime_types)
            actual_mime_types->push_back(actual_mime_type);
        }
      }
    }
  }
}

bool PluginList::SupportsType(const WebPluginInfo& plugin,
                              const std::string& mime_type,
                              bool allow_wildcard) {
  // Webkit will ask for a plugin to handle empty mime types.
  if (mime_type.empty())
    return false;

  for (size_t i = 0; i < plugin.mime_types.size(); ++i) {
    const WebPluginMimeType& mime_info = plugin.mime_types[i];
    if (net::MatchesMimeType(mime_info.mime_type, mime_type)) {
      if (!allow_wildcard && mime_info.mime_type == "*")
        continue;
      return true;
    }
  }
  return false;
}

bool PluginList::SupportsExtension(const WebPluginInfo& plugin,
                                   const std::string& extension,
                                   std::string* actual_mime_type) {
  for (size_t i = 0; i < plugin.mime_types.size(); ++i) {
    const WebPluginMimeType& mime_type = plugin.mime_types[i];
    for (size_t j = 0; j < mime_type.file_extensions.size(); ++j) {
      if (mime_type.file_extensions[j] == extension) {
        if (actual_mime_type)
          *actual_mime_type = mime_type.mime_type;
        return true;
      }
    }
  }
  return false;
}

void PluginList::RemoveExtraPluginPathLocked(
    const base::FilePath& plugin_path) {
  lock_.AssertAcquired();
  std::vector<base::FilePath>::iterator it =
      std::find(extra_plugin_paths_.begin(), extra_plugin_paths_.end(),
                plugin_path);
  if (it != extra_plugin_paths_.end())
    extra_plugin_paths_.erase(it);
}

PluginList::~PluginList() {
}


}  // namespace content
