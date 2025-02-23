// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_UTILITY_CONTENT_UTILITY_CLIENT_H_
#define CONTENT_PUBLIC_UTILITY_CONTENT_UTILITY_CLIENT_H_

#include <map>
#include <memory>

#include "base/callback_forward.h"
#include "content/public/common/content_client.h"

class GURL;

namespace mojo {
class ShellClient;
}

namespace content {

class ServiceRegistry;

// Embedder API for participating in renderer logic.
class CONTENT_EXPORT ContentUtilityClient {
 public:
  using StaticMojoApplicationMap =
      std::map<std::string,
               base::Callback<std::unique_ptr<mojo::ShellClient>()>>;

  virtual ~ContentUtilityClient() {}

  // Notifies us that the UtilityThread has been created.
  virtual void UtilityThreadStarted() {}

  // Allows the embedder to filter messages.
  virtual bool OnMessageReceived(const IPC::Message& message);

  // Registers Mojo services.
  virtual void RegisterMojoServices(ServiceRegistry* registry) {}

  // Registers Mojo applications.
  virtual void RegisterMojoApplications(StaticMojoApplicationMap* apps) {}
};

}  // namespace content

#endif  // CONTENT_PUBLIC_UTILITY_CONTENT_UTILITY_CLIENT_H_
