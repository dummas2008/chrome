// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a small utility that watches for and logs network changes.

#include <string>
#include <unordered_set>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_split.h"
#include "base/values.h"
#include "build/build_config.h"
#include "net/base/network_change_notifier.h"
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_config_service.h"
#include "net/proxy/proxy_service.h"

#if defined(USE_GLIB) && !defined(OS_CHROMEOS)
#include <glib-object.h>
#endif

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
#include "net/base/network_change_notifier_linux.h"
#endif

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#endif

namespace {

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
// Flag to specifies which network interfaces to ignore. Interfaces should
// follow as a comma seperated list.
const char kIgnoreNetifFlag[] = "ignore-netif";
#endif

// Conversions from various network-related types to string.

const char* ConnectionTypeToString(
    net::NetworkChangeNotifier::ConnectionType type) {
  switch (type) {
    case net::NetworkChangeNotifier::CONNECTION_UNKNOWN:
      return "CONNECTION_UNKNOWN";
    case net::NetworkChangeNotifier::CONNECTION_ETHERNET:
      return "CONNECTION_ETHERNET";
    case net::NetworkChangeNotifier::CONNECTION_WIFI:
      return "CONNECTION_WIFI";
    case net::NetworkChangeNotifier::CONNECTION_2G:
      return "CONNECTION_2G";
    case net::NetworkChangeNotifier::CONNECTION_3G:
      return "CONNECTION_3G";
    case net::NetworkChangeNotifier::CONNECTION_4G:
      return "CONNECTION_4G";
    case net::NetworkChangeNotifier::CONNECTION_NONE:
      return "CONNECTION_NONE";
    case net::NetworkChangeNotifier::CONNECTION_BLUETOOTH:
      return "CONNECTION_BLUETOOTH";
    default:
      return "CONNECTION_UNEXPECTED";
  }
}

std::string ProxyConfigToString(const net::ProxyConfig& config) {
  scoped_ptr<base::Value> config_value(config.ToValue());
  std::string str;
  base::JSONWriter::Write(*config_value, &str);
  return str;
}

const char* ConfigAvailabilityToString(
    net::ProxyConfigService::ConfigAvailability availability) {
  switch (availability) {
    case net::ProxyConfigService::CONFIG_PENDING:
      return "CONFIG_PENDING";
    case net::ProxyConfigService::CONFIG_VALID:
      return "CONFIG_VALID";
    case net::ProxyConfigService::CONFIG_UNSET:
      return "CONFIG_UNSET";
    default:
      return "CONFIG_UNEXPECTED";
  }
}

// The main observer class that logs network events.
class NetWatcher :
      public net::NetworkChangeNotifier::IPAddressObserver,
      public net::NetworkChangeNotifier::ConnectionTypeObserver,
      public net::NetworkChangeNotifier::DNSObserver,
      public net::NetworkChangeNotifier::NetworkChangeObserver,
      public net::ProxyConfigService::Observer {
 public:
  NetWatcher() {}

  ~NetWatcher() override {}

  // net::NetworkChangeNotifier::IPAddressObserver implementation.
  void OnIPAddressChanged() override { LOG(INFO) << "OnIPAddressChanged()"; }

  // net::NetworkChangeNotifier::ConnectionTypeObserver implementation.
  void OnConnectionTypeChanged(
      net::NetworkChangeNotifier::ConnectionType type) override {
    LOG(INFO) << "OnConnectionTypeChanged("
              << ConnectionTypeToString(type) << ")";
  }

  // net::NetworkChangeNotifier::DNSObserver implementation.
  void OnDNSChanged() override { LOG(INFO) << "OnDNSChanged()"; }
  void OnInitialDNSConfigRead() override {
    LOG(INFO) << "OnInitialDNSConfigRead()";
  }

  // net::NetworkChangeNotifier::NetworkChangeObserver implementation.
  void OnNetworkChanged(
      net::NetworkChangeNotifier::ConnectionType type) override {
    LOG(INFO) << "OnNetworkChanged("
              << ConnectionTypeToString(type) << ")";
  }

  // net::ProxyConfigService::Observer implementation.
  void OnProxyConfigChanged(
      const net::ProxyConfig& config,
      net::ProxyConfigService::ConfigAvailability availability) override {
    LOG(INFO) << "OnProxyConfigChanged("
              << ProxyConfigToString(config) << ", "
              << ConfigAvailabilityToString(availability) << ")";
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NetWatcher);
};

}  // namespace

int main(int argc, char* argv[]) {
#if defined(OS_MACOSX)
  base::mac::ScopedNSAutoreleasePool pool;
#endif
#if defined(USE_GLIB) && !defined(OS_CHROMEOS)
  // g_type_init will be deprecated in 2.36. 2.35 is the development
  // version for 2.36, hence do not call g_type_init starting 2.35.
  // http://developer.gnome.org/gobject/unstable/gobject-Type-Information.html#g-type-init
#if !GLIB_CHECK_VERSION(2, 35, 0)
  // Needed so ProxyConfigServiceLinux can use gconf.
  // Normally handled by BrowserMainLoop::InitializeToolkit().
  g_type_init();
#endif
#endif  // defined(USE_GLIB) && !defined(OS_CHROMEOS)
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);

  // Just make the main message loop the network loop.
  base::MessageLoopForIO network_loop;

  NetWatcher net_watcher;

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  std::string ignored_netifs_str =
      command_line->GetSwitchValueASCII(kIgnoreNetifFlag);
  std::unordered_set<std::string> ignored_interfaces;
  if (!ignored_netifs_str.empty()) {
    for (const std::string& ignored_netif :
         base::SplitString(ignored_netifs_str, ",", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_ALL)) {
      LOG(INFO) << "Ignoring: " << ignored_netif;
      ignored_interfaces.insert(ignored_netif);
    }
  }
  scoped_ptr<net::NetworkChangeNotifier> network_change_notifier(
      new net::NetworkChangeNotifierLinux(ignored_interfaces));
#else
  scoped_ptr<net::NetworkChangeNotifier> network_change_notifier(
      net::NetworkChangeNotifier::Create());
#endif

  // Use the network loop as the file loop also.
  scoped_ptr<net::ProxyConfigService> proxy_config_service(
      net::ProxyService::CreateSystemProxyConfigService(
          network_loop.task_runner(), network_loop.task_runner()));

  // Uses |network_change_notifier|.
  net::NetworkChangeNotifier::AddIPAddressObserver(&net_watcher);
  net::NetworkChangeNotifier::AddConnectionTypeObserver(&net_watcher);
  net::NetworkChangeNotifier::AddDNSObserver(&net_watcher);
  net::NetworkChangeNotifier::AddNetworkChangeObserver(&net_watcher);

  proxy_config_service->AddObserver(&net_watcher);

  LOG(INFO) << "Initial connection type: "
            << ConnectionTypeToString(
                   net::NetworkChangeNotifier::GetConnectionType());

  {
    net::ProxyConfig config;
    const net::ProxyConfigService::ConfigAvailability availability =
        proxy_config_service->GetLatestProxyConfig(&config);
    LOG(INFO) << "Initial proxy config: "
              << ProxyConfigToString(config) << ", "
              << ConfigAvailabilityToString(availability);
  }

  LOG(INFO) << "Watching for network events...";

  // Start watching for events.
  network_loop.Run();

  proxy_config_service->RemoveObserver(&net_watcher);

  // Uses |network_change_notifier|.
  net::NetworkChangeNotifier::RemoveDNSObserver(&net_watcher);
  net::NetworkChangeNotifier::RemoveConnectionTypeObserver(&net_watcher);
  net::NetworkChangeNotifier::RemoveIPAddressObserver(&net_watcher);
  net::NetworkChangeNotifier::RemoveNetworkChangeObserver(&net_watcher);

  return 0;
}
