// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SHELL_INTEGRATION_H_
#define CHROME_BROWSER_SHELL_INTEGRATION_H_

#include <string>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string16.h"
#include "build/build_config.h"
#include "ui/gfx/image/image_family.h"
#include "url/gurl.h"

namespace base {
class CommandLine;
}

namespace shell_integration {

// Sets Chrome as the default browser (only for the current user). Returns false
// if this operation fails. This does not work on Windows version 8 or higher.
// Prefer to use the DefaultBrowserWorker class below since it works on all OSs.
bool SetAsDefaultBrowser();

// Sets Chrome as the default client application for the given protocol
// (only for the current user). Returns false if this operation fails.
// Prefer to use the DefaultProtocolClientWorker class below since it works on
// all OSs.
bool SetAsDefaultProtocolClient(const std::string& protocol);

// The different types of permissions required to set a default web client.
enum DefaultWebClientSetPermission {
  // The browser distribution is not permitted to be made default.
  SET_DEFAULT_NOT_ALLOWED,
  // No special permission or interaction is required to set the default
  // browser. This is used in Linux, Mac and Windows 7 and under.
  SET_DEFAULT_UNATTENDED,
  // On Windows 8+, a browser can be made default only in an interactive flow.
  SET_DEFAULT_INTERACTIVE,
};

// Returns requirements for making the running browser either the default
// browser or the default client application for a specific protocols for the
// current user.
DefaultWebClientSetPermission GetDefaultWebClientSetPermission();

// Returns true if the running browser can be set as the default browser,
// whether user interaction is needed or not. Use
// GetDefaultWebClientSetPermission() if this distinction is important.
bool CanSetAsDefaultBrowser();

// Returns true if making the running browser the default client for any
// protocol requires elevated privileges.
bool IsElevationNeededForSettingDefaultProtocolClient();

// Returns a string representing the application to be launched given the
// protocol of the requested url. This string may be a name or a path, but
// neither is guaranteed and it should only be used as a display string.
// Returns an empty string on failure.
base::string16 GetApplicationNameForProtocol(const GURL& url);

// On Linux, it may not be possible to determine or set the default browser
// on some desktop environments or configurations. So, we use this enum and
// not a plain bool.
enum DefaultWebClientState {
  NOT_DEFAULT,
  IS_DEFAULT,
  UNKNOWN_DEFAULT,
  NUM_DEFAULT_STATES
};

// Attempt to determine if this instance of Chrome is the default browser and
// return the appropriate state. (Defined as being the handler for HTTP/HTTPS
// protocols; we don't want to report "no" here if the user has simply chosen
// to open HTML files in a text editor and FTP links with an FTP client.)
DefaultWebClientState GetDefaultBrowser();

// Returns true if Firefox is likely to be the default browser for the current
// user. This method is very fast so it can be invoked in the UI thread.
bool IsFirefoxDefaultBrowser();

// Attempt to determine if this instance of Chrome is the default client
// application for the given protocol and return the appropriate state.
DefaultWebClientState IsDefaultProtocolClient(const std::string& protocol);

// Data that needs to be passed between the app launcher stub and Chrome.
struct AppModeInfo {};
void SetAppModeInfo(const AppModeInfo* info);
const AppModeInfo* AppModeInfo();

// Is the current instance of Chrome running in App mode.
bool IsRunningInAppMode();

// Set up command line arguments for launching a URL or an app.
// The new command line reuses the current process's user data directory (and
// login profile, for ChromeOS).
// If |extension_app_id| is non-empty, the arguments use kAppId=<id>.
// Otherwise, kApp=<url> is used.
base::CommandLine CommandLineArgsForLauncher(
    const GURL& url,
    const std::string& extension_app_id,
    const base::FilePath& profile_path);

// Append command line arguments for launching a new chrome.exe process
// based on the current process.
// The new command line reuses the current process's user data directory and
// profile.
void AppendProfileArgs(const base::FilePath& profile_path,
                       base::CommandLine* command_line);

#if defined(OS_WIN)
// Generates an application user model ID (AppUserModelId) for a given app
// name and profile path. The returned app id is in the format of
// "|app_name|[.<profile_id>]". "profile_id" is appended when user override
// the default value.
// Note: If the app has an installation specific suffix (e.g. on user-level
// Chrome installs), |app_name| should already be suffixed, this method will
// then further suffix it with the profile id as described above.
base::string16 GetAppModelIdForProfile(const base::string16& app_name,
                                       const base::FilePath& profile_path);

// Generates an application user model ID (AppUserModelId) for Chromium by
// calling GetAppModelIdForProfile() with ShellUtil::GetAppId() as app_name.
base::string16 GetChromiumModelIdForProfile(const base::FilePath& profile_path);

// Get the AppUserModelId for the App List, for the profile in |profile_path|.
base::string16 GetAppListAppModelIdForProfile(
    const base::FilePath& profile_path);

// Migrates existing chrome taskbar pins by tagging them with correct app id.
// see http://crbug.com/28104
void MigrateTaskbarPins();

// Migrates all shortcuts in |path| which point to |chrome_exe| such that they
// have the appropriate AppUserModelId. Also clears the legacy dual_mode
// property from shortcuts with the default chrome app id.
// Returns the number of shortcuts migrated.
// This method should not be called prior to Windows 7.
// This method is only public for the sake of tests and shouldn't be called
// externally otherwise.
int MigrateShortcutsInPathInternal(const base::FilePath& chrome_exe,
                                   const base::FilePath& path);

// Returns the path to the Start Menu shortcut for the given Chrome.
base::FilePath GetStartMenuShortcut(const base::FilePath& chrome_exe);
#endif  // defined(OS_WIN)

#if !defined(OS_WIN)
// TODO(calamity): replace with
// BrowserDistribution::GetStartMenuShortcutSubfolder() once
// BrowserDistribution is cross-platform.
// Gets the name of the Chrome Apps menu folder in which to place app
// shortcuts. This is needed for Mac and Linux.
base::string16 GetAppShortcutsSubdirName();
#endif

// The type of callback used to communicate processing state to consumers of
// DefaultBrowserWorker and DefaultProtocolClientWorker.
using DefaultWebClientWorkerCallback =
    base::Callback<void(DefaultWebClientState)>;

//  Helper objects that handle checking if Chrome is the default browser
//  or application for a url protocol on Windows and Linux, and also setting
//  it as the default. These operations are performed asynchronously on the
//  file thread since registry access (on Windows) or the preference database
//  (on Linux) are involved and this can be slow.
//  By default, the worker will present the user with an interactive flow if
//  required by the platform. This can be suppressed via
//  set_interactive_permitted(), in which case an attempt to set Chrome as
//  the default handler will silently fail on such platforms.
class DefaultWebClientWorker
    : public base::RefCountedThreadSafe<DefaultWebClientWorker> {
 public:
  // Controls whether the worker can use user interaction to set the default
  // web client. If false, the set-as-default operation will fail on OS where
  // it is required.
  void set_interactive_permitted(bool interactive_permitted) {
    interactive_permitted_ = interactive_permitted;
  }

  // Checks to see if Chrome is the default web client application. The
  // instance's callback will be run to communicate the default state to the
  // caller.
  void StartCheckIsDefault();

  // Sets Chrome as the default web client application. Once done, it will
  // trigger a check for the default state using StartCheckIsDefault() to return
  // the default state to the caller.
  void StartSetAsDefault();

 protected:
  friend class base::RefCountedThreadSafe<DefaultWebClientWorker>;

  DefaultWebClientWorker(const DefaultWebClientWorkerCallback& callback,
                         const char* worker_name);
  virtual ~DefaultWebClientWorker();

  // Communicates the result via the |callback_|. When
  // |is_following_set_as_default| is true, |state| will be reported to UMA as
  // the result of the set-as-default operation.
  void OnCheckIsDefaultComplete(DefaultWebClientState state,
                                bool is_following_set_as_default);

  // When false, the operation to set as default will fail for interactive
  // flows.
  bool interactive_permitted_ = true;

 private:
  // Checks whether Chrome is the default web client. Always called on the
  // FILE thread. When |is_following_set_as_default| is true, The default state
  // will be reported to UMA as the result of the set-as-default operation.
  void CheckIsDefault(bool is_following_set_as_default);

  // Sets Chrome as the default web client. Always called on the FILE thread.
  void SetAsDefault();

  // Implementation of CheckIsDefault() and SetAsDefault() for subclasses.
  virtual DefaultWebClientState CheckIsDefaultImpl() = 0;

  // The callback may be run synchronously or at an arbitrary time later on this
  // thread.
  // Note: Subclasses MUST make sure |on_finished_callback| is executed.
  virtual void SetAsDefaultImpl(const base::Closure& on_finished_callback) = 0;

  // Reports the result for the set-as-default operation.
  void ReportSetDefaultResult(DefaultWebClientState state);

  // Updates the UI in our associated view with the current default web
  // client state.
  void UpdateUI(DefaultWebClientState state);

  // Called with the default state after the worker is done.
  DefaultWebClientWorkerCallback callback_;

  // Used to differentiate UMA metrics for setting the default browser and
  // setting the default protocol client. The pointer must be valid for the
  // lifetime of the worker.
  const char* worker_name_;

  // Wait until Chrome has been confirmed as the default browser before
  // reporting a successful attempt.
  bool check_default_should_report_success_ = false;

  DISALLOW_COPY_AND_ASSIGN(DefaultWebClientWorker);
};

// Worker for checking and setting the default browser.
class DefaultBrowserWorker : public DefaultWebClientWorker {
 public:
  explicit DefaultBrowserWorker(const DefaultWebClientWorkerCallback& callback);

 private:
  ~DefaultBrowserWorker() override;

  // Check if Chrome is the default browser.
  DefaultWebClientState CheckIsDefaultImpl() override;

  // Set Chrome as the default browser.
  void SetAsDefaultImpl(const base::Closure& on_finished_callback) override;

  DISALLOW_COPY_AND_ASSIGN(DefaultBrowserWorker);
};

// Worker for checking and setting the default client application
// for a given protocol. A different worker instance is needed for each
// protocol you are interested in, so to check or set the default for
// multiple protocols you should use multiple worker objects.
class DefaultProtocolClientWorker : public DefaultWebClientWorker {
 public:
  DefaultProtocolClientWorker(const DefaultWebClientWorkerCallback& callback,
                              const std::string& protocol);

  const std::string& protocol() const { return protocol_; }

 protected:
  ~DefaultProtocolClientWorker() override;

 private:
  // Check if Chrome is the default handler for this protocol.
  DefaultWebClientState CheckIsDefaultImpl() override;

  // Set Chrome as the default handler for this protocol.
  void SetAsDefaultImpl(const base::Closure& on_finished_callback) override;

  std::string protocol_;

  DISALLOW_COPY_AND_ASSIGN(DefaultProtocolClientWorker);
};

}  // namespace shell_integration

#endif  // CHROME_BROWSER_SHELL_INTEGRATION_H_
