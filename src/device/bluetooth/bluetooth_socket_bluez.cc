// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_socket_bluez.h"

#include <stdint.h>

#include <memory>
#include <queue>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_util.h"
#include "base/task_runner_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/worker_pool.h"
#include "dbus/bus.h"
#include "dbus/file_descriptor.h"
#include "dbus/object_path.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_adapter_bluez.h"
#include "device/bluetooth/bluetooth_adapter_profile_bluez.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_device_bluez.h"
#include "device/bluetooth/bluetooth_socket.h"
#include "device/bluetooth/bluetooth_socket_net.h"
#include "device/bluetooth/bluetooth_socket_thread.h"
#include "device/bluetooth/dbus/bluetooth_device_client.h"
#include "device/bluetooth/dbus/bluetooth_profile_manager_client.h"
#include "device/bluetooth/dbus/bluetooth_profile_service_provider.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

using device::BluetoothAdapter;
using device::BluetoothDevice;
using device::BluetoothSocketThread;
using device::BluetoothUUID;

namespace {

const char kAcceptFailed[] = "Failed to accept connection.";
const char kInvalidUUID[] = "Invalid UUID";
const char kSocketNotListening[] = "Socket is not listening.";

}  // namespace

namespace bluez {

// static
scoped_refptr<BluetoothSocketBlueZ> BluetoothSocketBlueZ::CreateBluetoothSocket(
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
    scoped_refptr<BluetoothSocketThread> socket_thread) {
  DCHECK(ui_task_runner->RunsTasksOnCurrentThread());

  return make_scoped_refptr(
      new BluetoothSocketBlueZ(ui_task_runner, socket_thread));
}

BluetoothSocketBlueZ::AcceptRequest::AcceptRequest() {}

BluetoothSocketBlueZ::AcceptRequest::~AcceptRequest() {}

BluetoothSocketBlueZ::ConnectionRequest::ConnectionRequest()
    : accepting(false), cancelled(false) {}

BluetoothSocketBlueZ::ConnectionRequest::~ConnectionRequest() {}

BluetoothSocketBlueZ::BluetoothSocketBlueZ(
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
    scoped_refptr<BluetoothSocketThread> socket_thread)
    : BluetoothSocketNet(ui_task_runner, socket_thread), profile_(nullptr) {}

BluetoothSocketBlueZ::~BluetoothSocketBlueZ() {
  DCHECK(!profile_);

  if (adapter_.get()) {
    adapter_->RemoveObserver(this);
    adapter_ = nullptr;
  }
}

void BluetoothSocketBlueZ::Connect(
    const BluetoothDeviceBlueZ* device,
    const BluetoothUUID& uuid,
    SecurityLevel security_level,
    const base::Closure& success_callback,
    const ErrorCompletionCallback& error_callback) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(!profile_);

  if (!uuid.IsValid()) {
    error_callback.Run(kInvalidUUID);
    return;
  }

  device_address_ = device->GetAddress();
  device_path_ = device->object_path();
  uuid_ = uuid;
  options_.reset(new bluez::BluetoothProfileManagerClient::Options());
  if (security_level == SECURITY_LEVEL_LOW)
    options_->require_authentication.reset(new bool(false));

  adapter_ = device->adapter();

  RegisterProfile(device->adapter(), success_callback, error_callback);
}

void BluetoothSocketBlueZ::Listen(
    scoped_refptr<BluetoothAdapter> adapter,
    SocketType socket_type,
    const BluetoothUUID& uuid,
    const BluetoothAdapter::ServiceOptions& service_options,
    const base::Closure& success_callback,
    const ErrorCompletionCallback& error_callback) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(!profile_);

  if (!uuid.IsValid()) {
    error_callback.Run(kInvalidUUID);
    return;
  }

  adapter_ = adapter;
  adapter_->AddObserver(this);

  uuid_ = uuid;
  options_.reset(new bluez::BluetoothProfileManagerClient::Options());
  if (service_options.name)
    options_->name.reset(new std::string(*service_options.name));

  switch (socket_type) {
    case kRfcomm:
      options_->channel.reset(
          new uint16_t(service_options.channel ? *service_options.channel : 0));
      break;
    case kL2cap:
      options_->psm.reset(
          new uint16_t(service_options.psm ? *service_options.psm : 0));
      break;
    default:
      NOTREACHED();
  }

  RegisterProfile(static_cast<BluetoothAdapterBlueZ*>(adapter.get()),
                  success_callback, error_callback);
}

void BluetoothSocketBlueZ::Close() {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());

  if (profile_)
    UnregisterProfile();

  // In the case below, where an asynchronous task gets posted on the socket
  // thread in BluetoothSocketNet::Close, a reference will be held to this
  // socket by the callback. This may cause the BluetoothAdapter to outlive
  // BluezDBusManager during shutdown if that callback executes too late.
  if (adapter_.get()) {
    adapter_->RemoveObserver(this);
    adapter_ = nullptr;
  }

  if (!device_path_.value().empty()) {
    BluetoothSocketNet::Close();
  } else {
    DoCloseListening();
  }
}

void BluetoothSocketBlueZ::Disconnect(const base::Closure& callback) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());

  if (profile_)
    UnregisterProfile();

  if (!device_path_.value().empty()) {
    BluetoothSocketNet::Disconnect(callback);
  } else {
    DoCloseListening();
    callback.Run();
  }
}

void BluetoothSocketBlueZ::Accept(
    const AcceptCompletionCallback& success_callback,
    const ErrorCompletionCallback& error_callback) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());

  if (!device_path_.value().empty()) {
    error_callback.Run(kSocketNotListening);
    return;
  }

  // Only one pending accept at a time
  if (accept_request_.get()) {
    error_callback.Run(net::ErrorToString(net::ERR_IO_PENDING));
    return;
  }

  accept_request_.reset(new AcceptRequest);
  accept_request_->success_callback = success_callback;
  accept_request_->error_callback = error_callback;

  if (connection_request_queue_.size() >= 1) {
    AcceptConnectionRequest();
  }
}

void BluetoothSocketBlueZ::RegisterProfile(
    BluetoothAdapterBlueZ* adapter,
    const base::Closure& success_callback,
    const ErrorCompletionCallback& error_callback) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(!profile_);
  DCHECK(adapter);

  // If the adapter is not present, this is a listening socket and the
  // adapter isn't running yet.  Report success and carry on;
  // the profile will be registered when the daemon becomes available.
  if (!adapter->IsPresent()) {
    VLOG(1) << uuid_.canonical_value() << " on " << device_path_.value()
            << ": Delaying profile registration.";
    base::MessageLoop::current()->PostTask(FROM_HERE, success_callback);
    return;
  }

  VLOG(1) << uuid_.canonical_value() << " on " << device_path_.value()
          << ": Acquiring profile.";

  adapter->UseProfile(uuid_, device_path_, *options_, this,
                      base::Bind(&BluetoothSocketBlueZ::OnRegisterProfile, this,
                                 success_callback, error_callback),
                      base::Bind(&BluetoothSocketBlueZ::OnRegisterProfileError,
                                 this, error_callback));
}

void BluetoothSocketBlueZ::OnRegisterProfile(
    const base::Closure& success_callback,
    const ErrorCompletionCallback& error_callback,
    BluetoothAdapterProfileBlueZ* profile) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(!profile_);

  profile_ = profile;

  if (device_path_.value().empty()) {
    VLOG(1) << uuid_.canonical_value() << ": Profile registered.";
    success_callback.Run();
    return;
  }

  VLOG(1) << uuid_.canonical_value() << ": Got profile, connecting to "
          << device_path_.value();

  bluez::BluezDBusManager::Get()->GetBluetoothDeviceClient()->ConnectProfile(
      device_path_, uuid_.canonical_value(),
      base::Bind(&BluetoothSocketBlueZ::OnConnectProfile, this,
                 success_callback),
      base::Bind(&BluetoothSocketBlueZ::OnConnectProfileError, this,
                 error_callback));
}

void BluetoothSocketBlueZ::OnRegisterProfileError(
    const ErrorCompletionCallback& error_callback,
    const std::string& error_message) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());

  LOG(WARNING) << uuid_.canonical_value()
               << ": Failed to register profile: " << error_message;
  error_callback.Run(error_message);
}

void BluetoothSocketBlueZ::OnConnectProfile(
    const base::Closure& success_callback) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(profile_);

  VLOG(1) << profile_->object_path().value() << ": Profile connected.";
  UnregisterProfile();
  success_callback.Run();
}

void BluetoothSocketBlueZ::OnConnectProfileError(
    const ErrorCompletionCallback& error_callback,
    const std::string& error_name,
    const std::string& error_message) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(profile_);

  LOG(WARNING) << profile_->object_path().value()
               << ": Failed to connect profile: " << error_name << ": "
               << error_message;
  UnregisterProfile();
  error_callback.Run(error_message);
}

void BluetoothSocketBlueZ::AdapterPresentChanged(BluetoothAdapter* adapter,
                                                 bool present) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());

  if (!present) {
    // Adapter removed, we can't use the profile anymore.
    UnregisterProfile();
    return;
  }

  DCHECK(!profile_);

  VLOG(1) << uuid_.canonical_value() << " on " << device_path_.value()
          << ": Acquiring profile.";

  static_cast<BluetoothAdapterBlueZ*>(adapter)->UseProfile(
      uuid_, device_path_, *options_, this,
      base::Bind(&BluetoothSocketBlueZ::OnInternalRegisterProfile, this),
      base::Bind(&BluetoothSocketBlueZ::OnInternalRegisterProfileError, this));
}

void BluetoothSocketBlueZ::OnInternalRegisterProfile(
    BluetoothAdapterProfileBlueZ* profile) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(!profile_);

  profile_ = profile;

  VLOG(1) << uuid_.canonical_value() << ": Profile re-registered";
}

void BluetoothSocketBlueZ::OnInternalRegisterProfileError(
    const std::string& error_message) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());

  LOG(WARNING) << "Failed to re-register profile: " << error_message;
}

void BluetoothSocketBlueZ::Released() {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(profile_);

  VLOG(1) << profile_->object_path().value() << ": Release";
}

void BluetoothSocketBlueZ::NewConnection(
    const dbus::ObjectPath& device_path,
    std::unique_ptr<dbus::FileDescriptor> fd,
    const bluez::BluetoothProfileServiceProvider::Delegate::Options& options,
    const ConfirmationCallback& callback) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());

  VLOG(1) << uuid_.canonical_value()
          << ": New connection from device: " << device_path.value();

  if (!device_path_.value().empty()) {
    DCHECK(device_path_ == device_path);

    socket_thread()->task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&BluetoothSocketBlueZ::DoNewConnection, this, device_path_,
                   base::Passed(&fd), options, callback));
  } else {
    linked_ptr<ConnectionRequest> request(new ConnectionRequest());
    request->device_path = device_path;
    request->fd = std::move(fd);
    request->options = options;
    request->callback = callback;

    connection_request_queue_.push(request);
    VLOG(1) << uuid_.canonical_value() << ": Connection is now pending.";
    if (accept_request_) {
      AcceptConnectionRequest();
    }
  }
}

void BluetoothSocketBlueZ::RequestDisconnection(
    const dbus::ObjectPath& device_path,
    const ConfirmationCallback& callback) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(profile_);

  VLOG(1) << profile_->object_path().value() << ": Request disconnection";
  callback.Run(SUCCESS);
}

void BluetoothSocketBlueZ::Cancel() {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(profile_);

  VLOG(1) << profile_->object_path().value() << ": Cancel";

  if (!connection_request_queue_.size())
    return;

  // If the front request is being accepted mark it as cancelled, otherwise
  // just pop it from the queue.
  linked_ptr<ConnectionRequest> request = connection_request_queue_.front();
  if (!request->accepting) {
    request->cancelled = true;
  } else {
    connection_request_queue_.pop();
  }
}

void BluetoothSocketBlueZ::AcceptConnectionRequest() {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(accept_request_.get());
  DCHECK(connection_request_queue_.size() >= 1);
  DCHECK(profile_);

  VLOG(1) << profile_->object_path().value()
          << ": Accepting pending connection.";

  linked_ptr<ConnectionRequest> request = connection_request_queue_.front();
  request->accepting = true;

  BluetoothDeviceBlueZ* device =
      static_cast<BluetoothAdapterBlueZ*>(adapter_.get())
          ->GetDeviceWithPath(request->device_path);
  DCHECK(device);

  scoped_refptr<BluetoothSocketBlueZ> client_socket =
      BluetoothSocketBlueZ::CreateBluetoothSocket(ui_task_runner(),
                                                  socket_thread());

  client_socket->device_address_ = device->GetAddress();
  client_socket->device_path_ = request->device_path;
  client_socket->uuid_ = uuid_;

  socket_thread()->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothSocketBlueZ::DoNewConnection, client_socket,
                 request->device_path, base::Passed(&request->fd),
                 request->options,
                 base::Bind(&BluetoothSocketBlueZ::OnNewConnection, this,
                            client_socket, request->callback)));
}

void BluetoothSocketBlueZ::DoNewConnection(
    const dbus::ObjectPath& device_path,
    std::unique_ptr<dbus::FileDescriptor> fd,
    const bluez::BluetoothProfileServiceProvider::Delegate::Options& options,
    const ConfirmationCallback& callback) {
  DCHECK(socket_thread()->task_runner()->RunsTasksOnCurrentThread());
  base::ThreadRestrictions::AssertIOAllowed();
  fd->CheckValidity();

  VLOG(1) << uuid_.canonical_value() << ": Validity check complete.";
  if (!fd->is_valid()) {
    LOG(WARNING) << uuid_.canonical_value() << " :" << fd->value()
                 << ": Invalid file descriptor received from Bluetooth Daemon.";
    ui_task_runner()->PostTask(FROM_HERE, base::Bind(callback, REJECTED));
    return;
  }

  if (tcp_socket()) {
    LOG(WARNING) << uuid_.canonical_value() << ": Already connected";
    ui_task_runner()->PostTask(FROM_HERE, base::Bind(callback, REJECTED));
    return;
  }

  ResetTCPSocket();

  // Note: We don't have a meaningful |IPEndPoint|, but that is ok since the
  // TCPSocket implementation does not actually require one.
  int net_result =
      tcp_socket()->AdoptConnectedSocket(fd->value(), net::IPEndPoint());
  if (net_result != net::OK) {
    LOG(WARNING) << uuid_.canonical_value() << ": Error adopting socket: "
                 << std::string(net::ErrorToString(net_result));
    ui_task_runner()->PostTask(FROM_HERE, base::Bind(callback, REJECTED));
    return;
  }

  VLOG(2) << uuid_.canonical_value()
          << ": Taking descriptor, confirming success.";
  fd->TakeValue();
  ui_task_runner()->PostTask(FROM_HERE, base::Bind(callback, SUCCESS));
}

void BluetoothSocketBlueZ::OnNewConnection(
    scoped_refptr<BluetoothSocket> socket,
    const ConfirmationCallback& callback,
    Status status) {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(accept_request_.get());
  DCHECK(connection_request_queue_.size() >= 1);

  linked_ptr<ConnectionRequest> request = connection_request_queue_.front();
  if (status == SUCCESS && !request->cancelled) {
    BluetoothDeviceBlueZ* device =
        static_cast<BluetoothAdapterBlueZ*>(adapter_.get())
            ->GetDeviceWithPath(request->device_path);
    DCHECK(device);

    accept_request_->success_callback.Run(device, socket);
  } else {
    accept_request_->error_callback.Run(kAcceptFailed);
  }

  accept_request_.reset(nullptr);
  connection_request_queue_.pop();

  callback.Run(status);
}

void BluetoothSocketBlueZ::DoCloseListening() {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());

  if (accept_request_) {
    accept_request_->error_callback.Run(
        net::ErrorToString(net::ERR_CONNECTION_CLOSED));
    accept_request_.reset(nullptr);
  }

  while (connection_request_queue_.size() > 0) {
    linked_ptr<ConnectionRequest> request = connection_request_queue_.front();
    request->callback.Run(REJECTED);
    connection_request_queue_.pop();
  }
}

void BluetoothSocketBlueZ::UnregisterProfile() {
  DCHECK(ui_task_runner()->RunsTasksOnCurrentThread());
  DCHECK(profile_);

  VLOG(1) << profile_->object_path().value() << ": Release profile";

  static_cast<BluetoothAdapterBlueZ*>(adapter_.get())
      ->ReleaseProfile(device_path_, profile_);
  profile_ = nullptr;
}

}  // namespace bluez
