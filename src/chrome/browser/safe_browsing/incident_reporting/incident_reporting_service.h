// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SAFE_BROWSING_INCIDENT_REPORTING_INCIDENT_REPORTING_SERVICE_H_
#define CHROME_BROWSER_SAFE_BROWSING_INCIDENT_REPORTING_INCIDENT_REPORTING_SERVICE_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/safe_browsing/download_protection_service.h"
#include "chrome/browser/safe_browsing/incident_reporting/delayed_analysis_callback.h"
#include "chrome/browser/safe_browsing/incident_reporting/delayed_callback_runner.h"
#include "chrome/browser/safe_browsing/incident_reporting/download_metadata_manager.h"
#include "chrome/browser/safe_browsing/incident_reporting/incident_report_uploader.h"
#include "chrome/browser/safe_browsing/incident_reporting/last_download_finder.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class Profile;
class TrackedPreferenceValidationDelegate;

namespace base {
class TaskRunner;
}

namespace content {
class DownloadManager;
class NotificationDetails;
class NotificationSource;
}

namespace net {
class URLRequestContextGetter;
}

namespace safe_browsing {

class ClientDownloadRequest;
class ClientIncidentReport;
class ClientIncidentReport_DownloadDetails;
class ClientIncidentReport_EnvironmentData;
class ClientIncidentReport_ExtensionData;
class ClientIncidentReport_IncidentData;
class Incident;
class IncidentReceiver;
class SafeBrowsingDatabaseManager;
class SafeBrowsingService;

// A class that manages the collection of incidents and submission of incident
// reports to the safe browsing client-side detection service. The service
// begins operation when an incident is reported via the AddIncident method.
// Incidents reported from a profile that is loading are held until the profile
// is fully created. Incidents originating from profiles that do not participate
// in safe browsing are dropped. Process-wide incidents are affiliated with a
// profile that participates in safe browsing when one becomes available.
// Following the addition of an incident that is not dropped, the service
// collects environmental data, finds the most recent binary download, and waits
// a bit. Additional incidents that arrive during this time are collated with
// the initial incident. Finally, already-reported incidents are pruned and any
// remaining are uploaded in an incident report.
class IncidentReportingService : public content::NotificationObserver {
 public:
  IncidentReportingService(
      SafeBrowsingService* safe_browsing_service,
      const scoped_refptr<net::URLRequestContextGetter>&
          request_context_getter);

  // All incident collection, data collection, and uploads in progress are
  // dropped at destruction.
  ~IncidentReportingService() override;

  // Returns true if incident reporting is enabled for the given profile.
  static bool IsEnabledForProfile(Profile* profile);

  // Returns an object by which external components can add an incident to the
  // service. The object may outlive the service, but will no longer have any
  // effect after the service is deleted.
  std::unique_ptr<IncidentReceiver> GetIncidentReceiver();

  // Returns a preference validation delegate that adds incidents to the service
  // for validation failures in |profile|. The delegate may outlive the service,
  // but incidents reported by it will no longer have any effect after the
  // service is deleted.
  std::unique_ptr<TrackedPreferenceValidationDelegate>
  CreatePreferenceValidationDelegate(Profile* profile);

  // Registers |callback| to be run after some delay following process launch.
  void RegisterDelayedAnalysisCallback(const DelayedAnalysisCallback& callback);

  // Registers |callback| to be run after some delay following process launch if
  // a profile participating in extended reporting is found.
  void RegisterExtendedReportingOnlyDelayedAnalysisCallback(
      const DelayedAnalysisCallback& callback);

  // Adds |download_manager| to the set monitored for client download request
  // storage.
  void AddDownloadManager(content::DownloadManager* download_manager);

 protected:
  // A pointer to a function that populates a protobuf with environment data.
  typedef void (*CollectEnvironmentDataFn)(
      ClientIncidentReport_EnvironmentData*);

  // For testing so that the TaskRunner used for delayed analysis callbacks can
  // be specified.
  IncidentReportingService(
      SafeBrowsingService* safe_browsing_service,
      const scoped_refptr<net::URLRequestContextGetter>& request_context_getter,
      base::TimeDelta delayed_task_interval,
      const scoped_refptr<base::TaskRunner>& delayed_task_runner);

  // Sets the function called by the service to collect environment data and the
  // task runner on which it is called. Used by unit tests to provide a fake
  // environment data collector.
  void SetCollectEnvironmentHook(
      CollectEnvironmentDataFn collect_environment_data_hook,
      const scoped_refptr<base::TaskRunner>& task_runner);

  // Initiates extension collection. Overriden by unit tests to provide fake
  // extension data.
  virtual void DoExtensionCollection(
      ClientIncidentReport_ExtensionData* extension_data);

  // Handles the addition of a new profile to the ProfileManager. Creates a new
  // context for |profile| if one does not exist, drops any received incidents
  // for the profile if the profile is not participating in safe browsing, and
  // initiates a new search for the most recent download if a report is being
  // assembled and the most recent has not been found. Overridden by unit tests
  // to inject incidents prior to creation.
  virtual void OnProfileAdded(Profile* profile);

  // Initiates a search for the most recent binary download. Overriden by unit
  // tests to provide a fake finder.
  virtual std::unique_ptr<LastDownloadFinder> CreateDownloadFinder(
      const LastDownloadFinder::LastDownloadCallback& callback);

  // Initiates an upload. Overridden by unit tests to provide a fake uploader.
  virtual std::unique_ptr<IncidentReportUploader> StartReportUpload(
      const IncidentReportUploader::OnResultCallback& callback,
      const scoped_refptr<net::URLRequestContextGetter>& request_context_getter,
      const ClientIncidentReport& report);

  // Returns true if a report is currently being processed.
  bool IsProcessingReport() const;

 private:
  struct ProfileContext;
  class UploadContext;
  class Receiver;

  // A mapping of profiles to contexts holding state about received incidents.
  typedef std::map<Profile*, ProfileContext*> ProfileContextCollection;

  // Returns the context for |profile|, creating it if it does not exist.
  ProfileContext* GetOrCreateProfileContext(Profile* profile);

  // Returns the context for |profile|, or NULL if it is unknown.
  ProfileContext* GetProfileContext(Profile* profile);

  // Handles the destruction of a profile. Incidents reported for the profile
  // but not yet uploaded are dropped.
  void OnProfileDestroyed(Profile* profile);

  // Returns an initialized profile that participates in safe browsing. Profiles
  // participating in extended safe browsing are preferred.
  Profile* FindEligibleProfile() const;

  // Adds |incident_data| relating to the optional |profile| to the service.
  void AddIncident(Profile* profile, std::unique_ptr<Incident> incident);

  // Clears all data associated with the |incident| relating to the optional
  // |profile|.
  void ClearIncident(Profile* profile, std::unique_ptr<Incident> incident);

  // Returns true if there are incidents waiting to be sent.
  bool HasIncidentsToUpload() const;

  // Begins processing a report. If processing is already underway, ensures that
  // collection tasks have completed or are running.
  void BeginReportProcessing();

  // Begins the process of collating incidents by waiting for incidents to
  // arrive. This function is idempotent.
  void BeginIncidentCollation();

  // Returns true if the service is waiting for additional incidents before
  // uploading a report.
  bool WaitingToCollateIncidents();

  // Cancels the collection timeout.
  void CancelIncidentCollection();

  // A callback invoked on the UI thread after which incident collation has
  // completed. Incident report processing continues, either by waiting for
  // environment data or the most recent download to arrive or by sending an
  // incident report.
  void OnCollationTimeout();

  // Starts a task to collect environment data in the blocking pool.
  void BeginEnvironmentCollection();

  // Returns true if the environment collection task is outstanding.
  bool WaitingForEnvironmentCollection();

  // Cancels any pending environment collection task and drops any data that has
  // already been collected.
  void CancelEnvironmentCollection();

  // A callback invoked on the UI thread when environment data collection is
  // complete. Incident report processing continues, either by waiting for the
  // collection timeout or by sending an incident report.
  void OnEnvironmentDataCollected(
      std::unique_ptr<ClientIncidentReport_EnvironmentData> environment_data);

  // Starts the asynchronous process of finding the most recent executable
  // download if one is not currently being search for and/or has not already
  // been found.
  void BeginDownloadCollection();

  // True if the service is waiting to discover the most recent download either
  // because a task to do so is outstanding, or because one or more profiles
  // have yet to be added to the ProfileManager.
  bool WaitingForMostRecentDownload();

  // Cancels the search for the most recent executable download.
  void CancelDownloadCollection();

  // A callback invoked on the UI thread by the last download finder when the
  // search for the most recent binary download and most recent non-binary
  // download is complete.
  void OnLastDownloadFound(
      std::unique_ptr<ClientIncidentReport_DownloadDetails>
          last_binary_download,
      std::unique_ptr<ClientIncidentReport_NonBinaryDownloadDetails>
          last_non_binary_download);

  // Processes all received incidents once all data collection is
  // complete. Incidents originating from profiles that do not participate in
  // safe browsing are dropped, incidents that have already been reported are
  // pruned, and prune state is cleared for incidents that are now clear. Report
  // upload is started if any incidents remain.
  void ProcessIncidentsIfCollectionComplete();

  // Cancels all uploads, discarding all reports and responses in progress.
  void CancelAllReportUploads();

  // Continues an upload after checking for the CSD whitelist killswitch.
  void OnKillSwitchResult(UploadContext* context, bool is_killswitch_on);

  // Performs processing for a report after succesfully receiving a response.
  void HandleResponse(const UploadContext& context);

  // IncidentReportUploader::OnResultCallback implementation.
  void OnReportUploadResult(UploadContext* context,
                            IncidentReportUploader::Result result,
                            std::unique_ptr<ClientIncidentResponse> response);

  // DownloadProtectionService::ClientDownloadRequestCallback implementation.
  void OnClientDownloadRequest(content::DownloadItem* download,
                               const ClientDownloadRequest* request);

  // content::NotificationObserver methods.
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  base::ThreadChecker thread_checker_;

  // The safe browsing database manager, through which the whitelist killswitch
  // is checked.
  scoped_refptr<SafeBrowsingDatabaseManager> database_manager_;

  // Accessor for an URL context with which reports will be sent.
  scoped_refptr<net::URLRequestContextGetter> url_request_context_getter_;

  // A pointer to a function that collects environment data. The function will
  // be run by |environment_collection_task_runner_|. This is ordinarily
  // CollectEnvironmentData, but may be overridden by tests; see
  // SetCollectEnvironmentHook.
  CollectEnvironmentDataFn collect_environment_data_fn_;

  // The task runner on which environment collection takes place. This is
  // ordinarily a runner in the browser's blocking pool that will skip the
  // collection task at shutdown if it has not yet started.
  scoped_refptr<base::TaskRunner> environment_collection_task_runner_;

  // Registrar for observing profile lifecycle notifications.
  content::NotificationRegistrar notification_registrar_;

  // A subscription for ClientDownloadRequests, used to persist them for later
  // use.
  DownloadProtectionService::ClientDownloadRequestSubscription
      client_download_request_subscription_;

  // True when the asynchronous environment collection task has been fired off
  // but has not yet completed.
  bool environment_collection_pending_;

  // True when an incident has been received and the service is waiting for the
  // collation_timer_ to fire.
  bool collation_timeout_pending_;

  // A timer upon the firing of which the service will report received
  // incidents.
  base::DelayTimer collation_timer_;

  // The report currently being assembled. This becomes non-NULL when an initial
  // incident is reported, and returns to NULL when the report is sent for
  // upload.
  std::unique_ptr<ClientIncidentReport> report_;

  // The time at which the initial incident is reported.
  base::Time first_incident_time_;

  // The time at which the last incident is reported.
  base::TimeTicks last_incident_time_;

  // The time at which environmental data collection was initiated.
  base::TimeTicks environment_collection_begin_;

  // The time at which download collection was initiated.
  base::TimeTicks last_download_begin_;

  // Context data for all on-the-record profiles plus the process-wide (NULL)
  // context.
  ProfileContextCollection profiles_;

  // Callbacks registered for performing delayed analysis.
  DelayedCallbackRunner delayed_analysis_callbacks_;

  // Callbacks registered for performing delayed analysis that should only
  // be executed for safebrowsing extended reporting users.
  DelayedCallbackRunner extended_reporting_only_delayed_analysis_callbacks_;

  DownloadMetadataManager download_metadata_manager_;

  // The collection of uploads in progress.
  std::vector<std::unique_ptr<UploadContext>> uploads_;

  // An object that asynchronously searches for the most recent binary download.
  // Non-NULL while such a search is outstanding.
  std::unique_ptr<LastDownloadFinder> last_download_finder_;

  // True if IncidentReportingService is enabled at the process level, by a
  // field trial.
  bool enabled_by_field_trial_;

  // A factory for handing out weak pointers for IncidentReceiver objects.
  base::WeakPtrFactory<IncidentReportingService> receiver_weak_ptr_factory_;

  // A factory for handing out weak pointers for internal asynchronous tasks
  // that are posted during normal processing (e.g., environment collection,
  // safe browsing database checks, and report uploads).
  base::WeakPtrFactory<IncidentReportingService> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(IncidentReportingService);
};

}  // namespace safe_browsing

#endif  // CHROME_BROWSER_SAFE_BROWSING_INCIDENT_REPORTING_INCIDENT_REPORTING_SERVICE_H_
