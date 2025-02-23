// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_NTP_MOST_VISITED_SITES_H_
#define CHROME_BROWSER_ANDROID_NTP_MOST_VISITED_SITES_H_

#include <jni.h>
#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/android/scoped_java_ref.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observer.h"
#include "chrome/browser/supervised_user/supervised_user_service.h"
#include "chrome/browser/supervised_user/supervised_user_service_observer.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/top_sites_observer.h"
#include "components/suggestions/proto/suggestions.pb.h"
#include "components/suggestions/suggestions_service.h"
#include "url/gurl.h"

namespace suggestions {
class SuggestionsService;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

class PopularSites;
class Profile;

// Provides the list of most visited sites and their thumbnails to Java.
class MostVisitedSites : public history::TopSitesObserver,
                         public SupervisedUserServiceObserver {
 public:
  explicit MostVisitedSites(Profile* profile);
  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void SetMostVisitedURLsObserver(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jobject>& j_observer,
      jint num_sites);
  void GetURLThumbnail(JNIEnv* env,
                       const base::android::JavaParamRef<jobject>& obj,
                       const base::android::JavaParamRef<jstring>& url,
                       const base::android::JavaParamRef<jobject>& j_callback);

  void AddOrRemoveBlacklistedUrl(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jstring>& j_url,
      jboolean add_url);
  void RecordTileTypeMetrics(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jintArray>& jtile_types);
  void RecordOpenedMostVisitedItem(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      jint index,
      jint tile_type);

  // SupervisedUserServiceObserver implementation.
  void OnURLFilterChanged() override;

  // Registers JNI methods.
  static bool Register(JNIEnv* env);

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

 private:
  friend class MostVisitedSitesTest;

  // The source of the Most Visited sites.
  enum MostVisitedSource { TOP_SITES, SUGGESTIONS_SERVICE, POPULAR, WHITELIST };

  struct Suggestion {
    base::string16 title;
    GURL url;
    MostVisitedSource source;

    // Only valid for source == WHITELIST (empty otherwise).
    base::FilePath whitelist_icon_path;

    // Only valid for source == SUGGESTIONS_SERVICE (-1 otherwise).
    int provider_index;

    Suggestion();
    ~Suggestion();

    // Get the Histogram name associated with the source.
    std::string GetSourceHistogramName() const;

   private:
    DISALLOW_COPY_AND_ASSIGN(Suggestion);
  };

  using SuggestionsVector = std::vector<std::unique_ptr<Suggestion>>;

  ~MostVisitedSites() override;
  void QueryMostVisitedURLs();

  // Initialize the query to Top Sites. Called if the SuggestionsService is not
  // enabled, or if it returns no data.
  void InitiateTopSitesQuery();

  // If there's a whitelist entry point for the URL, return the large icon path.
  base::FilePath GetWhitelistLargeIconPath(const GURL& url);

  // Callback for when data is available from TopSites.
  void OnMostVisitedURLsAvailable(
      const history::MostVisitedURLList& visited_list);

  // Callback for when data is available from the SuggestionsService.
  void OnSuggestionsProfileAvailable(
      const suggestions::SuggestionsProfile& suggestions_profile);

  // Takes the personal suggestions and creates whitelist entry point
  // suggestions if necessary.
  SuggestionsVector CreateWhitelistEntryPointSuggestions(
      const SuggestionsVector& personal_suggestions);

  // Takes the personal and whitelist suggestions and creates popular
  // suggestions if necessary.
  SuggestionsVector CreatePopularSitesSuggestions(
      const SuggestionsVector& personal_suggestions,
      const SuggestionsVector& whitelist_suggestions);

  // Takes the personal suggestions, creates and merges in whitelist and popular
  // suggestions if appropriate, and saves the new suggestions.
  void SaveNewNTPSuggestions(SuggestionsVector* personal_suggestions);

  // Workhorse for SaveNewNTPSuggestions above. Implemented as a separate static
  // method for ease of testing.
  static SuggestionsVector MergeSuggestions(
      SuggestionsVector* personal_suggestions,
      SuggestionsVector* whitelist_suggestions,
      SuggestionsVector* popular_suggestions,
      const std::vector<std::string>& old_sites_url,
      const std::vector<bool>& old_sites_is_personal);

  void GetPreviousNTPSites(size_t num_tiles,
                           std::vector<std::string>* old_sites_url,
                           std::vector<bool>* old_sites_source) const;

  void SaveCurrentNTPSites();

  // Takes suggestions from |src_suggestions| and moves them to
  // |dst_suggestions| if the suggestion's url/host matches
  // |match_urls|/|match_hosts| respectively. Unmatched suggestion indices from
  // |src_suggestions| are returned for ease of insertion later.
  static std::vector<size_t> InsertMatchingSuggestions(
      SuggestionsVector* src_suggestions,
      SuggestionsVector* dst_suggestions,
      const std::vector<std::string>& match_urls,
      const std::vector<std::string>& match_hosts);

  // Inserts suggestions from |src_suggestions| at positions |insert_positions|
  // into |dst_suggestions| where ever empty starting from |start_position|.
  // Returns the last filled position so that future insertions can start from
  // there.
  static size_t InsertAllSuggestions(
      size_t start_position,
      const std::vector<size_t>& insert_positions,
      SuggestionsVector* src_suggestions,
      SuggestionsVector* dst_suggestions);

  // Notifies the Java side observer about the availability of suggestions.
  // Also records impressions UMA if not done already.
  void NotifyMostVisitedURLsObserver();

  void OnPopularSitesAvailable(bool success);

  // Runs on the UI Thread.
  void OnLocalThumbnailFetched(
      const GURL& url,
      std::unique_ptr<base::android::ScopedJavaGlobalRef<jobject>> j_callback,
      std::unique_ptr<SkBitmap> bitmap);

  // Callback for when the thumbnail lookup is complete.
  // Runs on the UI Thread.
  void OnObtainedThumbnail(
      bool is_local_thumbnail,
      std::unique_ptr<base::android::ScopedJavaGlobalRef<jobject>> j_callback,
      const GURL& url,
      const SkBitmap* bitmap);

  // Records thumbnail-related UMA histogram metrics.
  void RecordThumbnailUMAMetrics();

  // Records UMA histogram metrics related to the number of impressions.
  void RecordImpressionUMAMetrics();

  // history::TopSitesObserver implementation.
  void TopSitesLoaded(history::TopSites* top_sites) override;
  void TopSitesChanged(history::TopSites* top_sites,
                       ChangeReason change_reason) override;

  // The profile whose most visited sites will be queried.
  Profile* profile_;

  // The observer to be notified when the list of most visited sites changes.
  base::android::ScopedJavaGlobalRef<jobject> observer_;

  // The maximum number of most visited sites to return.
  int num_sites_;

  // Whether we have received an initial set of most visited sites (from either
  // TopSites or the SuggestionsService).
  bool received_most_visited_sites_;

  // Whether we have received the set of popular sites. Immediately set to true
  // if popular sites are disabled.
  bool received_popular_sites_;

  // Whether we have recorded one-shot UMA metrics such as impressions. They are
  // recorded once both the previous flags are true.
  bool recorded_uma_;

  std::unique_ptr<
      suggestions::SuggestionsService::ResponseCallbackList::Subscription>
      suggestions_subscription_;

  ScopedObserver<history::TopSites, history::TopSitesObserver> scoped_observer_;

  MostVisitedSource mv_source_;

  std::unique_ptr<PopularSites> popular_sites_;

  SuggestionsVector current_suggestions_;

  // For callbacks may be run after destruction.
  base::WeakPtrFactory<MostVisitedSites> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(MostVisitedSites);
};

#endif  // CHROME_BROWSER_ANDROID_NTP_MOST_VISITED_SITES_H_
