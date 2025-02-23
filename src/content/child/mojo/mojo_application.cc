// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/mojo/mojo_application.h"

#include <utility>

#include "build/build_config.h"
#include "content/child/child_process.h"
#include "content/common/application_setup.mojom.h"
#include "content/common/mojo/channel_init.h"
#include "content/common/mojo/mojo_messages.h"
#include "ipc/ipc_message.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"

namespace content {

MojoApplication::MojoApplication(
    scoped_refptr<base::SequencedTaskRunner> io_task_runner)
    : io_task_runner_(io_task_runner) {
  DCHECK(io_task_runner_);
}

MojoApplication::~MojoApplication() {
}

bool MojoApplication::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(MojoApplication, msg)
    IPC_MESSAGE_HANDLER(MojoMsg_Activate, OnActivate)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void MojoApplication::InitWithToken(std::string token) {
  mojo::ScopedMessagePipeHandle handle =
      mojo::edk::CreateChildMessagePipe(token);
  DCHECK(handle.is_valid());

  mojom::ApplicationSetupPtr application_setup;
  application_setup.Bind(
      mojo::InterfacePtrInfo<mojom::ApplicationSetup>(std::move(handle), 0u));

  mojo::shell::mojom::InterfaceProviderPtr services;
  mojo::shell::mojom::InterfaceProviderPtr exposed_services;
  service_registry_.Bind(GetProxy(&exposed_services));
  application_setup->ExchangeInterfaceProviders(GetProxy(&services),
                                                std::move(exposed_services));
  service_registry_.BindRemoteServiceProvider(std::move(services));
}

void MojoApplication::OnActivate(
    const IPC::PlatformFileForTransit& file) {
#if defined(OS_POSIX)
  base::PlatformFile handle = file.fd;
#elif defined(OS_WIN)
  base::PlatformFile handle = file.GetHandle();
#endif

  mojo::ScopedMessagePipeHandle pipe =
      channel_init_.Init(handle, io_task_runner_);
  DCHECK(pipe.is_valid());

  mojom::ApplicationSetupPtr application_setup;
  application_setup.Bind(
      mojo::InterfacePtrInfo<mojom::ApplicationSetup>(std::move(pipe), 0u));

  mojo::shell::mojom::InterfaceProviderPtr services;
  mojo::shell::mojom::InterfaceProviderPtr exposed_services;
  service_registry_.Bind(GetProxy(&exposed_services));
  application_setup->ExchangeInterfaceProviders(GetProxy(&services),
                                                std::move(exposed_services));
  service_registry_.BindRemoteServiceProvider(std::move(services));
}

}  // namespace content
