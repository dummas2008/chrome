// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/gcm/gcm_api.h"

#include <stddef.h>
#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/services/gcm/gcm_profile_service_factory.h"
#include "chrome/common/extensions/api/gcm.h"
#include "components/gcm_driver/common/gcm_messages.h"
#include "components/gcm_driver/gcm_driver.h"
#include "components/gcm_driver/gcm_profile_service.h"
#include "extensions/browser/event_router.h"
#include "extensions/common/extension.h"

namespace {

const size_t kMaximumMessageSize = 4096;  // in bytes.
const char kCollapseKey[] = "collapse_key";
const char kGoogDotRestrictedPrefix[] = "goog.";
const char kGoogleRestrictedPrefix[] = "google";

// Error messages.
const char kInvalidParameter[] =
    "Function was called with invalid parameters.";
const char kGCMDisabled[] = "GCM is currently disabled.";
const char kAsyncOperationPending[] =
    "Asynchronous operation is pending.";
const char kNetworkError[] = "Network error occurred.";
const char kServerError[] = "Server error occurred.";
const char kTtlExceeded[] = "Time-to-live exceeded.";
const char kUnknownError[] = "Unknown error occurred.";

const char* GcmResultToError(gcm::GCMClient::Result result) {
  switch (result) {
    case gcm::GCMClient::SUCCESS:
      return "";
    case gcm::GCMClient::INVALID_PARAMETER:
      return kInvalidParameter;
    case gcm::GCMClient::GCM_DISABLED:
      return kGCMDisabled;
    case gcm::GCMClient::ASYNC_OPERATION_PENDING:
      return kAsyncOperationPending;
    case gcm::GCMClient::NETWORK_ERROR:
      return kNetworkError;
    case gcm::GCMClient::SERVER_ERROR:
      return kServerError;
    case gcm::GCMClient::TTL_EXCEEDED:
      return kTtlExceeded;
    case gcm::GCMClient::UNKNOWN_ERROR:
      return kUnknownError;
    default:
      NOTREACHED() << "Unexpected value of result cannot be converted: "
                   << result;
  }

  // Never reached, but prevents missing return statement warning.
  return "";
}

bool IsMessageKeyValid(const std::string& key) {
  std::string lower = base::ToLowerASCII(key);
  return !key.empty() &&
         key.compare(0, arraysize(kCollapseKey) - 1, kCollapseKey) != 0 &&
         lower.compare(0,
                       arraysize(kGoogleRestrictedPrefix) - 1,
                       kGoogleRestrictedPrefix) != 0 &&
         lower.compare(0,
                       arraysize(kGoogDotRestrictedPrefix),
                       kGoogDotRestrictedPrefix) != 0;
}

}  // namespace

namespace extensions {

bool GcmApiFunction::RunAsync() {
  if (!IsGcmApiEnabled())
    return false;

  return DoWork();
}

bool GcmApiFunction::IsGcmApiEnabled() const {
  Profile* profile = Profile::FromBrowserContext(browser_context());

  // GCM is not supported in incognito mode.
  if (profile->IsOffTheRecord())
    return false;

  return gcm::GCMProfileService::IsGCMEnabled(profile->GetPrefs());
}

gcm::GCMDriver* GcmApiFunction::GetGCMDriver() const {
  return gcm::GCMProfileServiceFactory::GetForProfile(
      Profile::FromBrowserContext(browser_context()))->driver();
}

GcmRegisterFunction::GcmRegisterFunction() {}

GcmRegisterFunction::~GcmRegisterFunction() {}

bool GcmRegisterFunction::DoWork() {
  std::unique_ptr<api::gcm::Register::Params> params(
      api::gcm::Register::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  GetGCMDriver()->Register(
      extension()->id(),
      params->sender_ids,
      base::Bind(&GcmRegisterFunction::CompleteFunctionWithResult, this));

  return true;
}

void GcmRegisterFunction::CompleteFunctionWithResult(
    const std::string& registration_id,
    gcm::GCMClient::Result result) {
  SetResult(new base::StringValue(registration_id));
  SetError(GcmResultToError(result));
  SendResponse(gcm::GCMClient::SUCCESS == result);
}

GcmUnregisterFunction::GcmUnregisterFunction() {}

GcmUnregisterFunction::~GcmUnregisterFunction() {}

bool GcmUnregisterFunction::DoWork() {
  UMA_HISTOGRAM_BOOLEAN("GCM.APICallUnregister", true);

  GetGCMDriver()->Unregister(
      extension()->id(),
      base::Bind(&GcmUnregisterFunction::CompleteFunctionWithResult, this));

  return true;
}

void GcmUnregisterFunction::CompleteFunctionWithResult(
    gcm::GCMClient::Result result) {
  SetError(GcmResultToError(result));
  SendResponse(gcm::GCMClient::SUCCESS == result);
}

GcmSendFunction::GcmSendFunction() {}

GcmSendFunction::~GcmSendFunction() {}

bool GcmSendFunction::DoWork() {
  std::unique_ptr<api::gcm::Send::Params> params(
      api::gcm::Send::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  EXTENSION_FUNCTION_VALIDATE(
      ValidateMessageData(params->message.data.additional_properties));

  gcm::OutgoingMessage outgoing_message;
  outgoing_message.id = params->message.message_id;
  outgoing_message.data = params->message.data.additional_properties;
  if (params->message.time_to_live.get())
    outgoing_message.time_to_live = *params->message.time_to_live;

  GetGCMDriver()->Send(
      extension()->id(),
      params->message.destination_id,
      outgoing_message,
      base::Bind(&GcmSendFunction::CompleteFunctionWithResult, this));

  return true;
}

void GcmSendFunction::CompleteFunctionWithResult(
    const std::string& message_id,
    gcm::GCMClient::Result result) {
  SetResult(new base::StringValue(message_id));
  SetError(GcmResultToError(result));
  SendResponse(gcm::GCMClient::SUCCESS == result);
}

bool GcmSendFunction::ValidateMessageData(const gcm::MessageData& data) const {
  size_t total_size = 0u;
  for (std::map<std::string, std::string>::const_iterator iter = data.begin();
       iter != data.end(); ++iter) {
    total_size += iter->first.size() + iter->second.size();

    if (!IsMessageKeyValid(iter->first) ||
        kMaximumMessageSize < iter->first.size() ||
        kMaximumMessageSize < iter->second.size() ||
        kMaximumMessageSize < total_size)
      return false;
  }

  return total_size != 0;
}

GcmJsEventRouter::GcmJsEventRouter(Profile* profile) : profile_(profile) {
}

GcmJsEventRouter::~GcmJsEventRouter() {
}

void GcmJsEventRouter::OnMessage(const std::string& app_id,
                                 const gcm::IncomingMessage& message) {
  api::gcm::OnMessage::Message message_arg;
  message_arg.data.additional_properties = message.data;
  if (!message.sender_id.empty())
    message_arg.from.reset(new std::string(message.sender_id));
  if (!message.collapse_key.empty())
    message_arg.collapse_key.reset(new std::string(message.collapse_key));

  std::unique_ptr<Event> event(
      new Event(events::GCM_ON_MESSAGE, api::gcm::OnMessage::kEventName,
                api::gcm::OnMessage::Create(message_arg), profile_));
  EventRouter::Get(profile_)
      ->DispatchEventToExtension(app_id, std::move(event));
}

void GcmJsEventRouter::OnMessagesDeleted(const std::string& app_id) {
  std::unique_ptr<Event> event(new Event(
      events::GCM_ON_MESSAGES_DELETED, api::gcm::OnMessagesDeleted::kEventName,
      api::gcm::OnMessagesDeleted::Create(), profile_));
  EventRouter::Get(profile_)
      ->DispatchEventToExtension(app_id, std::move(event));
}

void GcmJsEventRouter::OnSendError(
    const std::string& app_id,
    const gcm::GCMClient::SendErrorDetails& send_error_details) {
  api::gcm::OnSendError::Error error;
  error.message_id.reset(new std::string(send_error_details.message_id));
  error.error_message = GcmResultToError(send_error_details.result);
  error.details.additional_properties = send_error_details.additional_data;

  std::unique_ptr<Event> event(
      new Event(events::GCM_ON_SEND_ERROR, api::gcm::OnSendError::kEventName,
                api::gcm::OnSendError::Create(error), profile_));
  EventRouter::Get(profile_)
      ->DispatchEventToExtension(app_id, std::move(event));
}

}  // namespace extensions
