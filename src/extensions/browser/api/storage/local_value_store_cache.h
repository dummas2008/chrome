// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_STORAGE_LOCAL_VALUE_STORE_CACHE_H_
#define EXTENSIONS_BROWSER_API_STORAGE_LOCAL_VALUE_STORE_CACHE_H_

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "extensions/browser/api/storage/settings_storage_quota_enforcer.h"
#include "extensions/browser/api/storage/value_store_cache.h"

namespace extensions {

class ValueStoreFactory;

// ValueStoreCache for the LOCAL namespace. It owns a backend for apps and
// another for extensions. Each backend takes care of persistence.
class LocalValueStoreCache : public ValueStoreCache {
 public:
  explicit LocalValueStoreCache(
      const scoped_refptr<ValueStoreFactory>& factory);
  ~LocalValueStoreCache() override;

  // ValueStoreCache implementation:
  void RunWithValueStoreForExtension(
      const StorageCallback& callback,
      scoped_refptr<const Extension> extension) override;
  void DeleteStorageSoon(const std::string& extension_id) override;

 private:
  typedef std::map<std::string, linked_ptr<ValueStore> > StorageMap;

  ValueStore* GetStorage(const Extension* extension);

  // The Factory to use for creating new ValueStores.
  const scoped_refptr<ValueStoreFactory> storage_factory_;

  // Quota limits (see SettingsStorageQuotaEnforcer).
  const SettingsStorageQuotaEnforcer::Limits quota_;

  // The collection of ValueStores for local storage.
  StorageMap storage_map_;

  DISALLOW_COPY_AND_ASSIGN(LocalValueStoreCache);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_STORAGE_LOCAL_VALUE_STORE_CACHE_H_
