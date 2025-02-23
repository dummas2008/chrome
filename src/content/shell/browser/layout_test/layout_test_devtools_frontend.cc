// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/layout_test/layout_test_devtools_frontend.h"

#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/shell/browser/layout_test/blink_test_controller.h"
#include "content/shell/browser/shell.h"
#include "net/base/filename_util.h"

namespace content {

// static
LayoutTestDevToolsFrontend* LayoutTestDevToolsFrontend::Show(
    WebContents* inspected_contents,
    const std::string& settings,
    const std::string& frontend_url) {
  Shell* shell = Shell::CreateNewWindow(inspected_contents->GetBrowserContext(),
                                        GURL(),
                                        NULL,
                                        gfx::Size());
  LayoutTestDevToolsFrontend* devtools_frontend =
      new LayoutTestDevToolsFrontend(shell, inspected_contents);

  shell->LoadURL(GetDevToolsPathAsURL(settings, frontend_url));

  return devtools_frontend;
}

// static.
GURL LayoutTestDevToolsFrontend::GetDevToolsPathAsURL(
    const std::string& settings,
    const std::string& frontend_url) {
  if (!frontend_url.empty())
    return GURL(frontend_url);
  base::FilePath dir_exe;
  if (!PathService::Get(base::DIR_EXE, &dir_exe)) {
    NOTREACHED();
    return GURL();
  }
#if defined(OS_MACOSX)
  // On Mac, the executable is in
  // out/Release/Content Shell.app/Contents/MacOS/Content Shell.
  // We need to go up 3 directories to get to out/Release.
  dir_exe = dir_exe.AppendASCII("../../..");
#endif
  base::FilePath dev_tools_path =
      dir_exe.AppendASCII("resources/inspector/inspector.html");

  GURL result = net::FilePathToFileURL(dev_tools_path);
  std::string url_string =
      base::StringPrintf("%s?experiments=true", result.spec().c_str());
#if defined(DEBUG_DEVTOOLS)
  url_string += "&debugFrontend=true";
#endif  // defined(DEBUG_DEVTOOLS)
  if (!settings.empty())
    url_string += "&settings=" + settings;
  return GURL(url_string);
}

void LayoutTestDevToolsFrontend::ReuseFrontend(const std::string& settings,
                                               const std::string frontend_url) {
  DisconnectFromTarget();
  preferences()->Clear();
  ready_for_test_ = false;
  pending_evaluations_.clear();
  frontend_shell()->LoadURL(GetDevToolsPathAsURL(settings, frontend_url));
}

void LayoutTestDevToolsFrontend::EvaluateInFrontend(
    int call_id,
    const std::string& script) {
  if (!ready_for_test_) {
    pending_evaluations_.push_back(std::make_pair(call_id, script));
    return;
  }

  std::string encoded_script;
  base::JSONWriter::Write(base::StringValue(script), &encoded_script);
  std::string source =
      base::StringPrintf("DevToolsAPI.evaluateForTestInFrontend(%d, %s);",
                         call_id,
                         encoded_script.c_str());
  web_contents()->GetMainFrame()->ExecuteJavaScriptForTests(
      base::UTF8ToUTF16(source));
}

LayoutTestDevToolsFrontend::LayoutTestDevToolsFrontend(
    Shell* frontend_shell,
    WebContents* inspected_contents)
    : ShellDevToolsFrontend(frontend_shell, inspected_contents),
      ready_for_test_(false) {
}

LayoutTestDevToolsFrontend::~LayoutTestDevToolsFrontend() {
}

void LayoutTestDevToolsFrontend::AgentHostClosed(
    DevToolsAgentHost* agent_host, bool replaced) {
  // Do not close the front-end shell.
}

void LayoutTestDevToolsFrontend::HandleMessageFromDevToolsFrontend(
    const std::string& message) {
  std::string method;
  base::DictionaryValue* dict = nullptr;
  std::unique_ptr<base::Value> parsed_message = base::JSONReader::Read(message);
  if (parsed_message &&
      parsed_message->GetAsDictionary(&dict) &&
      dict->GetString("method", &method) &&
      method == "readyForTest") {
    ready_for_test_ = true;
    for (const auto& pair : pending_evaluations_)
      EvaluateInFrontend(pair.first, pair.second);
    pending_evaluations_.clear();
    return;
  }

  ShellDevToolsFrontend::HandleMessageFromDevToolsFrontend(message);
}

void LayoutTestDevToolsFrontend::RenderProcessGone(
    base::TerminationStatus status) {
  BlinkTestController::Get()->DevToolsProcessCrashed();
}

}  // namespace content
