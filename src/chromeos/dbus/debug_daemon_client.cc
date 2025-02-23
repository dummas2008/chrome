// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/debug_daemon_client.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_path.h"
#include "base/json/json_string_value_serializer.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_util.h"
#include "base/task_runner_util.h"
#include "base/thread_task_runner_handle.h"
#include "base/threading/worker_pool.h"
#include "chromeos/dbus/pipe_reader.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"

namespace chromeos {

namespace {

const char kCrOSTracingAgentName[] = "cros";
const char kCrOSTraceLabel[] = "systemTraceEvents";

// Because the cheets logs are very huge, we set the D-Bus timeout to 2 minutes.
const int kBigLogsDBusTimeoutMS = 120 * 1000;

// The type of the callback to be invoked once the pipe reader has been
// initialized and a D-Bus file descriptor has been created for it.
using OnPipeReaderInitializedCallback =
    base::Callback<void(std::unique_ptr<dbus::FileDescriptor>)>;

// Used in DebugDaemonClient::EmptyStopAgentTracingCallback().
void EmptyStopAgentTracingCallbackBody(
    const std::string& agent_name,
    const std::string& events_label,
    const scoped_refptr<base::RefCountedString>& unused_result) {}

// Creates a D-Bus file descriptor from a base::File.
std::unique_ptr<dbus::FileDescriptor> CreateFileDescriptorForPipeWriteEnd(
    base::File pipe_write_end) {
  if (!pipe_write_end.IsValid()) {
    VLOG(1) << "Cannot create pipe reader";
    // NB: continue anyway; toss the data
    pipe_write_end.Initialize(base::FilePath(FILE_PATH_LITERAL("/dev/null")),
                              base::File::FLAG_OPEN | base::File::FLAG_WRITE);
    // TODO(afakhry): If this fails AppendFileDescriptor will abort.
  }
  std::unique_ptr<dbus::FileDescriptor> file_descriptor(
      new dbus::FileDescriptor);
  file_descriptor->PutValue(pipe_write_end.TakePlatformFile());
  file_descriptor->CheckValidity();
  return file_descriptor;
}

// A self-deleting object that wraps the pipe reader operations for reading the
// big feedback logs. It will delete itself once the pipe stream has been
// terminated. Once the data has been completely read from the pipe, it invokes
// the GetLogsCallback |callback| passing the deserialized logs data back to
// the requester.
class PipeReaderWrapper : public base::SupportsWeakPtr<PipeReaderWrapper> {
 public:
  explicit PipeReaderWrapper(const DebugDaemonClient::GetLogsCallback& callback)
      : task_runner_(
            base::WorkerPool::GetTaskRunner(true /** tasks_are_slow */)),
        pipe_reader_(task_runner_,
                     base::Bind(&PipeReaderWrapper::OnIOComplete, AsWeakPtr())),
        callback_(callback) {}

  void Initialize(const OnPipeReaderInitializedCallback& on_initialized) {
    base::File pipe_write_end = pipe_reader_.StartIO();
    base::PostTaskAndReplyWithResult(
        task_runner_.get(), FROM_HERE,
        base::Bind(&CreateFileDescriptorForPipeWriteEnd,
                   base::Passed(&pipe_write_end)),
        on_initialized);
  }

  void OnIOComplete() {
    std::string pipe_data;
    pipe_reader_.GetData(&pipe_data);
    JSONStringValueDeserializer json_reader(pipe_data);

    std::map<std::string, std::string> data;
    const base::DictionaryValue* dictionary = nullptr;
    std::unique_ptr<base::Value> logs_value =
        json_reader.Deserialize(nullptr, nullptr);
    if (!logs_value.get() || !logs_value->GetAsDictionary(&dictionary)) {
      VLOG(1) << "Failed to deserialize the JSON logs.";
      callback_.Run(false, data);
      delete this;
      return;
    }

    base::DictionaryValue::Iterator itr(*dictionary);
    for (; !itr.IsAtEnd(); itr.Advance()) {
      std::string value;
      itr.value().GetAsString(&value);
      data[itr.key()] = value;
    }

    callback_.Run(true, data);
    delete this;
  }

  void TerminateStream() { pipe_reader_.OnDataReady(-1); }

 private:
  scoped_refptr<base::TaskRunner> task_runner_;
  PipeReaderForString pipe_reader_;
  DebugDaemonClient::GetLogsCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(PipeReaderWrapper);
};

}  // namespace

// The DebugDaemonClient implementation used in production.
class DebugDaemonClientImpl : public DebugDaemonClient {
 public:
  DebugDaemonClientImpl() : debugdaemon_proxy_(NULL), weak_ptr_factory_(this) {}

  ~DebugDaemonClientImpl() override {}

  // DebugDaemonClient override.
  void DumpDebugLogs(bool is_compressed,
                     base::File file,
                     scoped_refptr<base::TaskRunner> task_runner,
                     const GetDebugLogsCallback& callback) override {
    dbus::FileDescriptor* file_descriptor = new dbus::FileDescriptor;
    file_descriptor->PutValue(file.TakePlatformFile());
    // Punt descriptor validity check to a worker thread; on return we'll
    // issue the D-Bus request to stop tracing and collect results.
    task_runner->PostTaskAndReply(
        FROM_HERE,
        base::Bind(&dbus::FileDescriptor::CheckValidity,
                   base::Unretained(file_descriptor)),
        base::Bind(&DebugDaemonClientImpl::OnCheckValidityGetDebugLogs,
                   weak_ptr_factory_.GetWeakPtr(),
                   is_compressed,
                   base::Owned(file_descriptor),
                   callback));
  }

  void SetDebugMode(const std::string& subsystem,
                    const SetDebugModeCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kSetDebugMode);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(subsystem);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnSetDebugMode,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void GetRoutes(bool numeric,
                 bool ipv6,
                 const GetRoutesCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetRoutes);
    dbus::MessageWriter writer(&method_call);
    dbus::MessageWriter sub_writer(NULL);
    writer.OpenArray("{sv}", &sub_writer);
    dbus::MessageWriter elem_writer(NULL);
    sub_writer.OpenDictEntry(&elem_writer);
    elem_writer.AppendString("numeric");
    elem_writer.AppendVariantOfBool(numeric);
    sub_writer.CloseContainer(&elem_writer);
    sub_writer.OpenDictEntry(&elem_writer);
    elem_writer.AppendString("v6");
    elem_writer.AppendVariantOfBool(ipv6);
    sub_writer.CloseContainer(&elem_writer);
    writer.CloseContainer(&sub_writer);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetRoutes,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void GetNetworkStatus(const GetNetworkStatusCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetNetworkStatus);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetNetworkStatus,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void GetModemStatus(const GetModemStatusCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetModemStatus);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetModemStatus,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void GetWiMaxStatus(const GetWiMaxStatusCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetWiMaxStatus);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetWiMaxStatus,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void GetNetworkInterfaces(
      const GetNetworkInterfacesCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetInterfaces);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetNetworkInterfaces,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void GetPerfOutput(uint32_t duration,
                     const std::vector<std::string>& perf_args,
                     const GetPerfOutputCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetPerfOutput);
    dbus::MessageWriter writer(&method_call);
    writer.AppendUint32(duration);
    writer.AppendArrayOfStrings(perf_args);

    debugdaemon_proxy_->CallMethod(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetPerfOutput,
                   weak_ptr_factory_.GetWeakPtr(), callback));
  }

  void GetScrubbedLogs(const GetLogsCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetFeedbackLogs);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetAllLogs,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void GetScrubbedBigLogs(const GetLogsCallback& callback) override {
    // The PipeReaderWrapper is a self-deleting object; we don't have to worry
    // about ownership or lifetime. We need to create a new one for each Big
    // Logs requests in order to queue these requests. One request can take a
    // long time to be processed and a new request should never be ignored nor
    // cancels the on-going one.
    PipeReaderWrapper* pipe_reader = new PipeReaderWrapper(callback);
    pipe_reader->Initialize(
        base::Bind(&DebugDaemonClientImpl::OnBigLogsPipeReaderReady,
                   weak_ptr_factory_.GetWeakPtr(), pipe_reader->AsWeakPtr()));
  }

  void GetAllLogs(const GetLogsCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetAllLogs);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetAllLogs,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void GetUserLogFiles(const GetLogsCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetUserLogFiles);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetUserLogFiles,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  // base::trace_event::TracingAgent implementation.
  std::string GetTracingAgentName() override { return kCrOSTracingAgentName; }

  std::string GetTraceEventLabel() override { return kCrOSTraceLabel; }

  void StartAgentTracing(const base::trace_event::TraceConfig& trace_config,
                         const StartAgentTracingCallback& callback) override {
    dbus::MethodCall method_call(
        debugd::kDebugdInterface,
        debugd::kSystraceStart);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString("all"); // TODO(sleffler) parameterize category list

    DVLOG(1) << "Requesting a systrace start";
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnStartMethod,
                   weak_ptr_factory_.GetWeakPtr()));

    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::Bind(callback, GetTracingAgentName(), true /* success */));
  }

  void StopAgentTracing(const StopAgentTracingCallback& callback) override {
    DCHECK(stop_agent_tracing_task_runner_);
    if (pipe_reader_ != NULL) {
      LOG(ERROR) << "Busy doing StopSystemTracing";
      return;
    }

    pipe_reader_.reset(
        new PipeReaderForString(stop_agent_tracing_task_runner_,
                                base::Bind(&DebugDaemonClientImpl::OnIOComplete,
                                           weak_ptr_factory_.GetWeakPtr())));

    base::File pipe_write_end = pipe_reader_->StartIO();
    // Create dbus::FileDescriptor on the worker thread; on return we'll
    // issue the D-Bus request to stop tracing and collect results.
    base::PostTaskAndReplyWithResult(
        stop_agent_tracing_task_runner_.get(), FROM_HERE,
        base::Bind(&CreateFileDescriptorForPipeWriteEnd,
                   base::Passed(&pipe_write_end)),
        base::Bind(
            &DebugDaemonClientImpl::OnCreateFileDescriptorRequestStopSystem,
            weak_ptr_factory_.GetWeakPtr(), callback));
  }

  void SetStopAgentTracingTaskRunner(
      scoped_refptr<base::TaskRunner> task_runner) override {
    stop_agent_tracing_task_runner_ = task_runner;
  }

  void TestICMP(const std::string& ip_address,
                const TestICMPCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kTestICMP);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(ip_address);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnTestICMP,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void TestICMPWithOptions(const std::string& ip_address,
                           const std::map<std::string, std::string>& options,
                           const TestICMPCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kTestICMPWithOptions);
    dbus::MessageWriter writer(&method_call);
    dbus::MessageWriter sub_writer(NULL);
    dbus::MessageWriter elem_writer(NULL);

    // Write the host.
    writer.AppendString(ip_address);

    // Write the options.
    writer.OpenArray("{ss}", &sub_writer);
    std::map<std::string, std::string>::const_iterator it;
    for (it = options.begin(); it != options.end(); ++it) {
      sub_writer.OpenDictEntry(&elem_writer);
      elem_writer.AppendString(it->first);
      elem_writer.AppendString(it->second);
      sub_writer.CloseContainer(&elem_writer);
    }
    writer.CloseContainer(&sub_writer);

    // Call the function.
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnTestICMP,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void UploadCrashes() override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kUploadCrashes);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnStartMethod,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  void EnableDebuggingFeatures(
      const std::string& password,
      const EnableDebuggingCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kEnableChromeDevFeatures);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(password);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnEnableDebuggingFeatures,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void QueryDebuggingFeatures(
      const QueryDevFeaturesCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kQueryDevFeatures);
    dbus::MessageWriter writer(&method_call);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnQueryDebuggingFeatures,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void RemoveRootfsVerification(
      const EnableDebuggingCallback& callback) override {
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kRemoveRootfsVerification);
    dbus::MessageWriter writer(&method_call);
    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnRemoveRootfsVerification,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  void WaitForServiceToBeAvailable(
      const WaitForServiceToBeAvailableCallback& callback) override {
    debugdaemon_proxy_->WaitForServiceToBeAvailable(callback);
  }

 protected:
  void Init(dbus::Bus* bus) override {
    debugdaemon_proxy_ =
        bus->GetObjectProxy(debugd::kDebugdServiceName,
                            dbus::ObjectPath(debugd::kDebugdServicePath));
  }

 private:
  // Called when a CheckValidity response is received.
  void OnCheckValidityGetDebugLogs(bool is_compressed,
                                   dbus::FileDescriptor* file_descriptor,
                                   const GetDebugLogsCallback& callback) {
    // Issue the dbus request to get debug logs.
    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kDumpDebugLogs);
    dbus::MessageWriter writer(&method_call);
    writer.AppendBool(is_compressed);
    writer.AppendFileDescriptor(*file_descriptor);

    debugdaemon_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnGetDebugLogs,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  // Called when a response for GetDebugLogs() is received.
  void OnGetDebugLogs(const GetDebugLogsCallback& callback,
                      dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to get debug logs";
      callback.Run(false);
      return;
    }
    callback.Run(true);
  }

  // Called when a response for SetDebugMode() is received.
  void OnSetDebugMode(const SetDebugModeCallback& callback,
                      dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to change debug mode";
      callback.Run(false);
    } else {
      callback.Run(true);
    }
  }

  void OnGetRoutes(const GetRoutesCallback& callback,
                   dbus::Response* response) {
    std::vector<std::string> routes;
    if (response) {
      dbus::MessageReader reader(response);
      if (reader.PopArrayOfStrings(&routes)) {
        callback.Run(true, routes);
      } else {
        LOG(ERROR) << "Got non-array response from GetRoutes";
        callback.Run(false, routes);
      }
    } else {
      callback.Run(false, routes);
    }
  }

  void OnGetNetworkStatus(const GetNetworkStatusCallback& callback,
                          dbus::Response* response) {
    std::string status;
    if (response && dbus::MessageReader(response).PopString(&status))
      callback.Run(true, status);
    else
      callback.Run(false, "");
  }

  void OnGetModemStatus(const GetModemStatusCallback& callback,
                        dbus::Response* response) {
    std::string status;
    if (response && dbus::MessageReader(response).PopString(&status))
      callback.Run(true, status);
    else
      callback.Run(false, "");
  }

  void OnGetWiMaxStatus(const GetWiMaxStatusCallback& callback,
                        dbus::Response* response) {
    std::string status;
    if (response && dbus::MessageReader(response).PopString(&status))
      callback.Run(true, status);
    else
      callback.Run(false, "");
  }

  void OnGetNetworkInterfaces(const GetNetworkInterfacesCallback& callback,
                              dbus::Response* response) {
    std::string status;
    if (response && dbus::MessageReader(response).PopString(&status))
      callback.Run(true, status);
    else
      callback.Run(false, "");
  }

  void OnGetPerfOutput(const GetPerfOutputCallback& callback,
                       dbus::Response* response) {
    if (!response)
      return;

    dbus::MessageReader reader(response);

    int status = 0;
    if (!reader.PopInt32(&status))
      return;

    const uint8_t* buffer = nullptr;
    size_t buf_size = 0;

    if (!reader.PopArrayOfBytes(&buffer, &buf_size))
      return;
    std::vector<uint8_t> perf_data;
    if (buf_size > 0)
      perf_data.insert(perf_data.end(), buffer, buffer + buf_size);

    if (!reader.PopArrayOfBytes(&buffer, &buf_size))
      return;
    std::vector<uint8_t> perf_stat;
    if (buf_size > 0)
      perf_stat.insert(perf_stat.end(), buffer, buffer + buf_size);

    callback.Run(status, perf_data, perf_stat);
  }

  void OnGetAllLogs(const GetLogsCallback& callback,
                    dbus::Response* response) {
    std::map<std::string, std::string> logs;
    bool broken = false; // did we see a broken (k,v) pair?
    dbus::MessageReader sub_reader(NULL);
    if (!response || !dbus::MessageReader(response).PopArray(&sub_reader)) {
      callback.Run(false, logs);
      return;
    }
    while (sub_reader.HasMoreData()) {
      dbus::MessageReader sub_sub_reader(NULL);
      std::string key, value;
      if (!sub_reader.PopDictEntry(&sub_sub_reader)
          || !sub_sub_reader.PopString(&key)
          || !sub_sub_reader.PopString(&value)) {
        broken = true;
        break;
      }
      logs[key] = value;
    }
    callback.Run(!sub_reader.HasMoreData() && !broken, logs);
  }

  void OnGetUserLogFiles(const GetLogsCallback& callback,
                         dbus::Response* response) {
    return OnGetAllLogs(callback, response);
  }

  void OnBigLogsPipeReaderReady(
      base::WeakPtr<PipeReaderWrapper> pipe_reader,
      std::unique_ptr<dbus::FileDescriptor> file_descriptor) {
    DCHECK(file_descriptor);

    dbus::MethodCall method_call(debugd::kDebugdInterface,
                                 debugd::kGetBigFeedbackLogs);
    dbus::MessageWriter writer(&method_call);
    writer.AppendFileDescriptor(*file_descriptor);

    DVLOG(1) << "Requesting big feedback logs";
    debugdaemon_proxy_->CallMethod(
        &method_call, kBigLogsDBusTimeoutMS,
        base::Bind(&DebugDaemonClientImpl::OnBigFeedbackLogsResponse,
                   weak_ptr_factory_.GetWeakPtr(), pipe_reader));
  }

  void OnBigFeedbackLogsResponse(base::WeakPtr<PipeReaderWrapper> pipe_reader,
                                 dbus::Response* response) {
    if (!response && pipe_reader.get()) {
      // We need to terminate the data stream if an error occurred while the
      // pipe reader is still waiting on read.
      pipe_reader->TerminateStream();
    }
  }

  // Called when a response for a simple start is received.
  void OnStartMethod(dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to request start";
      return;
    }
  }

  void OnEnableDebuggingFeatures(
      const EnableDebuggingCallback& callback,
      dbus::Response* response) {
    if (callback.is_null())
      return;

    callback.Run(response != NULL);
  }

  void OnQueryDebuggingFeatures(
      const QueryDevFeaturesCallback& callback,
      dbus::Response* response) {
    if (callback.is_null())
      return;

    int32_t feature_mask = DEV_FEATURE_NONE;
    if (!response || !dbus::MessageReader(response).PopInt32(&feature_mask)) {
      callback.Run(false, debugd::DevFeatureFlag::DEV_FEATURES_DISABLED);
      return;
    }

    callback.Run(true, feature_mask);
  }

  void OnRemoveRootfsVerification(
      const EnableDebuggingCallback& callback,
      dbus::Response* response) {
    if (callback.is_null())
      return;

    callback.Run(response != NULL);
  }

  // Called when a CheckValidity response is received.
  void OnCreateFileDescriptorRequestStopSystem(
      const StopAgentTracingCallback& callback,
      std::unique_ptr<dbus::FileDescriptor> file_descriptor) {
    DCHECK(file_descriptor);

    // Issue the dbus request to stop system tracing
    dbus::MethodCall method_call(
        debugd::kDebugdInterface,
        debugd::kSystraceStop);
    dbus::MessageWriter writer(&method_call);
    writer.AppendFileDescriptor(*file_descriptor);

    callback_ = callback;

    DVLOG(1) << "Requesting a systrace stop";
    debugdaemon_proxy_->CallMethod(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&DebugDaemonClientImpl::OnStopAgentTracing,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  // Called when a response for StopAgentTracing() is received.
  void OnStopAgentTracing(dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to request systrace stop";
      // If debugd crashes or completes I/O before this message is processed
      // then pipe_reader_ can be NULL, see OnIOComplete().
      if (pipe_reader_.get())
        pipe_reader_->OnDataReady(-1); // terminate data stream
    }
    // NB: requester is signaled when i/o completes
  }

  void OnTestICMP(const TestICMPCallback& callback, dbus::Response* response) {
    std::string status;
    if (response && dbus::MessageReader(response).PopString(&status))
      callback.Run(true, status);
    else
      callback.Run(false, "");
  }

  // Called when pipe i/o completes; pass data on and delete the instance.
  void OnIOComplete() {
    std::string pipe_data;
    pipe_reader_->GetData(&pipe_data);
    callback_.Run(GetTracingAgentName(), GetTraceEventLabel(),
                  base::RefCountedString::TakeString(&pipe_data));
    pipe_reader_.reset();
  }

  dbus::ObjectProxy* debugdaemon_proxy_;
  std::unique_ptr<PipeReaderForString> pipe_reader_;
  StopAgentTracingCallback callback_;
  scoped_refptr<base::TaskRunner> stop_agent_tracing_task_runner_;
  base::WeakPtrFactory<DebugDaemonClientImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DebugDaemonClientImpl);
};

DebugDaemonClient::DebugDaemonClient() {
}

DebugDaemonClient::~DebugDaemonClient() {
}

// static
DebugDaemonClient::StopAgentTracingCallback
DebugDaemonClient::EmptyStopAgentTracingCallback() {
  return base::Bind(&EmptyStopAgentTracingCallbackBody);
}

// static
DebugDaemonClient* DebugDaemonClient::Create() {
  return new DebugDaemonClientImpl();
}

}  // namespace chromeos
