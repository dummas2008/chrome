// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/shell_integration.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "chrome/browser/policy/policy_path_parser.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_thread.h"

#if defined(OS_CHROMEOS)
#include "chromeos/chromeos_switches.h"
#endif

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#include "chrome/browser/shell_integration_win.h"
#endif

#if !defined(OS_WIN)
#include "chrome/common/channel_info.h"
#include "chrome/grit/chromium_strings.h"
#include "ui/base/l10n/l10n_util.h"
#endif

using content::BrowserThread;

namespace shell_integration {

namespace {

const struct AppModeInfo* gAppModeInfo = nullptr;

}  // namespace

bool CanSetAsDefaultBrowser() {
  return GetDefaultWebClientSetPermission() != SET_DEFAULT_NOT_ALLOWED;
}

#if !defined(OS_WIN)
bool IsElevationNeededForSettingDefaultProtocolClient() {
  return false;
}
#endif  // !defined(OS_WIN)

void SetAppModeInfo(const struct AppModeInfo* info) {
  gAppModeInfo = info;
}

const struct AppModeInfo* AppModeInfo() {
  return gAppModeInfo;
}

bool IsRunningInAppMode() {
  return gAppModeInfo != NULL;
}

base::CommandLine CommandLineArgsForLauncher(
    const GURL& url,
    const std::string& extension_app_id,
    const base::FilePath& profile_path) {
  base::ThreadRestrictions::AssertIOAllowed();
  base::CommandLine new_cmd_line(base::CommandLine::NO_PROGRAM);

  AppendProfileArgs(
      extension_app_id.empty() ? base::FilePath() : profile_path,
      &new_cmd_line);

  // If |extension_app_id| is present, we use the kAppId switch rather than
  // the kApp switch (the launch url will be read from the extension app
  // during launch.
  if (!extension_app_id.empty()) {
    new_cmd_line.AppendSwitchASCII(switches::kAppId, extension_app_id);
  } else {
    // Use '--app=url' instead of just 'url' to launch the browser with minimal
    // chrome.
    // Note: Do not change this flag!  Old Gears shortcuts will break if you do!
    new_cmd_line.AppendSwitchASCII(switches::kApp, url.spec());
  }
  return new_cmd_line;
}

void AppendProfileArgs(const base::FilePath& profile_path,
                       base::CommandLine* command_line) {
  DCHECK(command_line);
  const base::CommandLine& cmd_line = *base::CommandLine::ForCurrentProcess();

  // Use the same UserDataDir for new launches that we currently have set.
  base::FilePath user_data_dir =
      cmd_line.GetSwitchValuePath(switches::kUserDataDir);
#if defined(OS_MACOSX) || defined(OS_WIN)
  policy::path_parser::CheckUserDataDirPolicy(&user_data_dir);
#endif
  if (!user_data_dir.empty()) {
    // Make sure user_data_dir is an absolute path.
    user_data_dir = base::MakeAbsoluteFilePath(user_data_dir);
    if (!user_data_dir.empty() && base::PathExists(user_data_dir))
      command_line->AppendSwitchPath(switches::kUserDataDir, user_data_dir);
  }

#if defined(OS_CHROMEOS)
  base::FilePath profile = cmd_line.GetSwitchValuePath(
      chromeos::switches::kLoginProfile);
  if (!profile.empty())
    command_line->AppendSwitchPath(chromeos::switches::kLoginProfile, profile);
#else
  if (!profile_path.empty())
    command_line->AppendSwitchPath(switches::kProfileDirectory,
                                   profile_path.BaseName());
#endif
}

#if !defined(OS_WIN)
base::string16 GetAppShortcutsSubdirName() {
  if (chrome::GetChannel() == version_info::Channel::CANARY)
    return l10n_util::GetStringUTF16(IDS_APP_SHORTCUTS_SUBDIR_NAME_CANARY);
  return l10n_util::GetStringUTF16(IDS_APP_SHORTCUTS_SUBDIR_NAME);
}
#endif  // !defined(OS_WIN)

///////////////////////////////////////////////////////////////////////////////
// DefaultWebClientWorker
//

void DefaultWebClientWorker::StartCheckIsDefault() {
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&DefaultWebClientWorker::CheckIsDefault, this, false));
}

void DefaultWebClientWorker::StartSetAsDefault() {
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&DefaultWebClientWorker::SetAsDefault, this));
}

///////////////////////////////////////////////////////////////////////////////
// DefaultWebClientWorker, protected:

DefaultWebClientWorker::DefaultWebClientWorker(
    const DefaultWebClientWorkerCallback& callback,
    const char* worker_name)
    : callback_(callback), worker_name_(worker_name) {}

DefaultWebClientWorker::~DefaultWebClientWorker() = default;

void DefaultWebClientWorker::OnCheckIsDefaultComplete(
    DefaultWebClientState state,
    bool is_following_set_as_default) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  UpdateUI(state);

  if (is_following_set_as_default)
    ReportSetDefaultResult(state);
}

///////////////////////////////////////////////////////////////////////////////
// DefaultWebClientWorker, private:

void DefaultWebClientWorker::CheckIsDefault(bool is_following_set_as_default) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  DefaultWebClientState state = CheckIsDefaultImpl();
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&DefaultBrowserWorker::OnCheckIsDefaultComplete, this, state,
                 is_following_set_as_default));
}

void DefaultWebClientWorker::SetAsDefault() {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  // SetAsDefaultImpl will make sure the callback is executed exactly once.
  SetAsDefaultImpl(
      base::Bind(&DefaultWebClientWorker::CheckIsDefault, this, true));
}

void DefaultWebClientWorker::ReportSetDefaultResult(
    DefaultWebClientState state) {
  base::LinearHistogram::FactoryGet(
      base::StringPrintf("%s.SetDefaultResult2", worker_name_), 1,
      DefaultWebClientState::NUM_DEFAULT_STATES,
      DefaultWebClientState::NUM_DEFAULT_STATES + 1,
      base::HistogramBase::kUmaTargetedHistogramFlag)
      ->Add(state);
}

void DefaultWebClientWorker::UpdateUI(DefaultWebClientState state) {
  if (!callback_.is_null()) {
    switch (state) {
      case NOT_DEFAULT:
        callback_.Run(NOT_DEFAULT);
        break;
      case IS_DEFAULT:
        callback_.Run(IS_DEFAULT);
        break;
      case UNKNOWN_DEFAULT:
        callback_.Run(UNKNOWN_DEFAULT);
        break;
      case NUM_DEFAULT_STATES:
        NOTREACHED();
        break;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// DefaultBrowserWorker
//

DefaultBrowserWorker::DefaultBrowserWorker(
    const DefaultWebClientWorkerCallback& callback)
    : DefaultWebClientWorker(callback, "DefaultBrowser") {}

///////////////////////////////////////////////////////////////////////////////
// DefaultBrowserWorker, private:

DefaultBrowserWorker::~DefaultBrowserWorker() = default;

DefaultWebClientState DefaultBrowserWorker::CheckIsDefaultImpl() {
  return GetDefaultBrowser();
}

void DefaultBrowserWorker::SetAsDefaultImpl(
    const base::Closure& on_finished_callback) {
  switch (GetDefaultWebClientSetPermission()) {
    case SET_DEFAULT_NOT_ALLOWED:
      NOTREACHED();
      break;
    case SET_DEFAULT_UNATTENDED:
      SetAsDefaultBrowser();
      break;
    case SET_DEFAULT_INTERACTIVE:
#if defined(OS_WIN)
      if (interactive_permitted_) {
        // The Windows 8 API for choosing the default browser was deprecated on
        // Windows 10.
        if (base::win::GetVersion() >= base::win::VERSION_WIN10) {
          win::SetAsDefaultBrowserUsingSystemSettings(on_finished_callback);
          return;
        } else {
          win::SetAsDefaultBrowserUsingIntentPicker();
        }
      }
#endif  // defined(OS_WIN)
      break;
  }
  on_finished_callback.Run();
}

///////////////////////////////////////////////////////////////////////////////
// DefaultProtocolClientWorker
//

DefaultProtocolClientWorker::DefaultProtocolClientWorker(
    const DefaultWebClientWorkerCallback& callback,
    const std::string& protocol)
    : DefaultWebClientWorker(callback, "DefaultProtocolClient"),
      protocol_(protocol) {}

///////////////////////////////////////////////////////////////////////////////
// DefaultProtocolClientWorker, protected:

DefaultProtocolClientWorker::~DefaultProtocolClientWorker() = default;

///////////////////////////////////////////////////////////////////////////////
// DefaultProtocolClientWorker, private:

DefaultWebClientState DefaultProtocolClientWorker::CheckIsDefaultImpl() {
  return IsDefaultProtocolClient(protocol_);
}

void DefaultProtocolClientWorker::SetAsDefaultImpl(
    const base::Closure& on_finished_callback) {
  switch (GetDefaultWebClientSetPermission()) {
    case SET_DEFAULT_NOT_ALLOWED:
      // Not allowed, do nothing.
      break;
    case SET_DEFAULT_UNATTENDED:
      SetAsDefaultProtocolClient(protocol_);
      break;
    case SET_DEFAULT_INTERACTIVE:
#if defined(OS_WIN)
      // TODO(pmonette): Implement a working flow for Windows 10.
      if (interactive_permitted_)
        win::SetAsDefaultProtocolClientUsingIntentPicker(protocol_);
#endif  // defined(OS_WIN)
      break;
  }
  on_finished_callback.Run();
}

}  // namespace shell_integration
