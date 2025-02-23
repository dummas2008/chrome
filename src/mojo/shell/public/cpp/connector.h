// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SHELL_PUBLIC_CPP_CONNECTOR_H_
#define MOJO_SHELL_PUBLIC_CPP_CONNECTOR_H_

#include "mojo/shell/public/cpp/connection.h"
#include "mojo/shell/public/cpp/identity.h"
#include "mojo/shell/public/interfaces/connector.mojom.h"
#include "mojo/shell/public/interfaces/shell.mojom.h"
#include "mojo/shell/public/interfaces/shell_client.mojom.h"

namespace mojo {

// An interface that encapsulates the Mojo Shell's broker interface by which
// connections between applications are established. Once Connect() is called,
// this class is bound to the thread the call was made on and it cannot be
// passed to another thread without calling Clone().
// An instance of this class is created internally by ShellConnection for use
// on the thread ShellConnection is instantiated on, and this interface is
// wrapped by the Shell interface.
// To use this interface on other threads, call Shell::CloneConnector() and
// pass the result to another thread. To pass to subsequent threads, call
// Clone() on instances of this object.
// While instances of this object are owned by the caller, the underlying
// connection with the shell is bound to the lifetime of the instance that
// created it, i.e. when the application is terminated the Connector pipe is
// closed.
class Connector {
 public:
  virtual ~Connector() {}

  class ConnectParams {
   public:
    explicit ConnectParams(const Identity& target);
    explicit ConnectParams(const std::string& name);
    ~ConnectParams();

    const Identity& target() { return target_; }
    void set_target(const Identity& target) { target_ = target; }
    void set_client_process_connection(
        shell::mojom::ShellClientPtr shell_client,
        shell::mojom::PIDReceiverRequest pid_receiver_request) {
      shell_client_ = std::move(shell_client);
      pid_receiver_request_ = std::move(pid_receiver_request);
    }
    void TakeClientProcessConnection(
        shell::mojom::ShellClientPtr* shell_client,
        shell::mojom::PIDReceiverRequest* pid_receiver_request) {
      *shell_client = std::move(shell_client_);
      *pid_receiver_request = std::move(pid_receiver_request_);
    }

   private:
    Identity target_;
    shell::mojom::ShellClientPtr shell_client_;
    shell::mojom::PIDReceiverRequest pid_receiver_request_;

    DISALLOW_COPY_AND_ASSIGN(ConnectParams);
  };

  // Requests a new connection to an application. Returns a pointer to the
  // connection if the connection is permitted by this application's delegate,
  // or nullptr otherwise. Caller takes ownership.
  // Once this method is called, this object is bound to the thread on which the
  // call took place. To pass to another thread, call Clone() and pass the
  // result.
  virtual scoped_ptr<Connection> Connect(const std::string& name) = 0;
  virtual scoped_ptr<Connection> Connect(ConnectParams* params) = 0;

  // Connect to application identified by |request->name| and connect to the
  // service implementation of the interface identified by |Interface|.
  template <typename Interface>
  void ConnectToInterface(ConnectParams* params, InterfacePtr<Interface>* ptr) {
    scoped_ptr<Connection> connection = Connect(params);
    if (connection)
      connection->GetInterface(ptr);
  }
  template <typename Interface>
  void ConnectToInterface(const Identity& target,
                          InterfacePtr<Interface>* ptr) {
    ConnectParams params(target);
    return ConnectToInterface(&params, ptr);
  }
  template <typename Interface>
  void ConnectToInterface(const std::string& name,
                          InterfacePtr<Interface>* ptr) {
    ConnectParams params(name);
    return ConnectToInterface(&params, ptr);
  }

  // Creates a new instance of this class which may be passed to another thread.
  // The returned object may be passed multiple times until Connect() is called,
  // at which point this method must be called again to pass again.
  virtual scoped_ptr<Connector> Clone() = 0;
};

}  // namespace mojo

#endif  // MOJO_SHELL_PUBLIC_CPP_CONNECTOR_H_
