// Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
// All Rights Reserved.

#include <chrono>
#include <cstdlib>
#include <stdexcept>

#include <fb303/FollyLoggingHandler.h>
#include <fb303/ServiceData.h>
#include <systemd/sd-daemon.h>

#include "fboss/platform/helpers/Init.h"
#include "fboss/platform/helpers/StructuredLogger.h"
#include "fboss/platform/platform_manager/ConfigUtils.h"
#include "fboss/platform/platform_manager/PkgManager.h"
#include "fboss/platform/platform_manager/PlatformExplorer.h"
#include "fboss/platform/platform_manager/PlatformManagerHandler.h"

using namespace facebook;
using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::platform_manager;
using namespace std::chrono_literals;

DEFINE_int32(thrift_port, 5975, "Port for the thrift service");

DEFINE_int32(
    explore_interval_s,
    60,
    "Frequency at which the platform needs to be explored");

DEFINE_bool(
    run_once,
    true,
    "Setup platform once and exit. If set to false, setup platform once "
    "and run thrift service.");

DECLARE_bool(run_in_netos);

void sdNotifyReady() {
  if (auto rc = sd_notify(0, "READY=1"); rc < 0) {
    XLOG(WARNING) << "Failed to send READY signal to systemd: "
                  << folly::errnoStr(-rc);
    throw std::runtime_error(
        fmt::format(
            "Failed to sd_notify ready by run command, ExitStatus: {}",
            folly::errnoStr(-rc)));
  } else {
    XLOG(INFO) << "Sent sd_notify ready by running command";
  }
}

int main(int argc, char** argv) {
  auto startTime = std::chrono::steady_clock::now();
  fb303::registerFollyLoggingOptionHandlers();
  helpers::init(&argc, &argv);

  std::optional<DataStore> dataStore;
  try {
    ConfigUtils configUtils;
    bool configChanged = configUtils.hasConfigChanged();
    PlatformConfig config = configUtils.getConfig();

    dataStore.emplace(config);

    if (configChanged) {
      XLOG(INFO) << "Config changed since last run, reloading kmods";
    }
    bool reloadKmods = FLAGS_reload_kmods || configChanged;
    auto systemInterface = std::make_shared<package_manager::SystemInterface>();
    resolveBspKmodsRpmVersion(config, *systemInterface);
    PkgManager pkgManager(config, systemInterface);
    pkgManager.processAll(FLAGS_enable_pkg_mgmnt, reloadKmods);
    PlatformExplorer platformExplorer(config, dataStore.value());
    platformExplorer.explore();

    // Store config hash after successful exploration
    configUtils.storeConfigHash();

    auto duration = std::chrono::steady_clock::now() - startTime;
    auto elapsedSeconds = duration_cast<std::chrono::seconds>(duration).count();

    // Publish exploration time counters
    if (pkgManager.wereKmodsUnloaded()) {
      fb303::fbData->setCounter(
          "package_manager.exploration_time.with_kmod_unload", elapsedSeconds);
    } else {
      fb303::fbData->setCounter(
          "package_manager.exploration_time.without_kmod_unload",
          elapsedSeconds);
    }
    XLOG(INFO) << fmt::format(
        "Exploration complete in {} seconds. Kmods were {}reloaded.",
        elapsedSeconds,
        pkgManager.wereKmodsUnloaded() ? "" : "NOT ");

    if (duration > PlatformExplorer::kMaxSetupTime) {
      XLOG(ERR) << fmt::format(
          "Setup time {}s exceeded maximum allowed {}s",
          elapsedSeconds,
          PlatformExplorer::kMaxSetupTime.count());
    }

    if (FLAGS_run_once) {
      XLOG(INFO) << fmt::format(
          "Ran with --run_once={}. Skipping running as a daemon...",
          FLAGS_run_once);
      return 0;
    }

    // When systemd starts PlatformManager, it sets the below env in PM
    // environment. This is a path to Unix domain socket at /run/systemd/notify.
    if (!FLAGS_run_in_netos) {
      const auto notifySocketEnv{"NOTIFY_SOCKET"};
      if (std::getenv(notifySocketEnv)) {
        sdNotifyReady();
      } else {
        XLOG(WARNING) << fmt::format(
            "Skipping sd_notify since ${} is not set which does not "
            "imply systemd execution.",
            notifySocketEnv);
      }
    }
    std::optional<DataStore> ds = platformExplorer.getDataStore();
    if (!ds.has_value()) {
      XLOG(ERR)
          << "PlatformExplorer did not succeed. Not starting Thrift Service.";
      return 1;
    }
    XLOG(INFO) << "Running PlatformManager thrift service...";

    auto server = std::make_shared<apache::thrift::ThriftServer>();
    auto handler = std::make_shared<PlatformManagerHandler>(
        platformExplorer, ds.value(), config);

    helpers::runThriftService(
        server, handler, "PlatformManagerService", FLAGS_thrift_port);

    XLOG(INFO) << "================ STOPPED PLATFORM BINARY ================";
    return 0;
  } catch (const std::exception& ex) {
    helpers::StructuredLogger structuredLogger("platform_manager");
    if (dataStore.has_value()) {
      structuredLogger.addFwTags(dataStore->getFirmwareVersions());
      structuredLogger.setTags(dataStore->getHardwareVersions());
    }
    structuredLogger.logAlert("unexpected_crash", ex.what());
    XLOG(FATAL) << "Platform Manager crashed with unexpected exception: "
                << ex.what();
    return 1;
  }
}
