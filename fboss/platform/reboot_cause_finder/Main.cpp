// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fb303/FollyLoggingHandler.h>
#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <optional>
#include <string>

#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/helpers/Init.h"
#include "fboss/platform/helpers/StructuredLogger.h"
#include "fboss/platform/reboot_cause_finder/ConfigValidator.h"
#include "fboss/platform/reboot_cause_finder/RebootCauseFinderImpl.h"

using namespace facebook;
using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::reboot_cause_finder;

namespace {

// Loads the config. On failure config is left empty, which still lets the run
// record the boot with the software providers.
int run(reboot_cause_config::RebootCauseConfig& config) {
  std::optional<std::string> configJson;
  try {
    configJson = ConfigLib().getRebootCauseFinderConfig();
  } catch (const std::exception& ex) {
    if (!FLAGS_config_file.empty()) {
      XLOG(ERR) << "Failed to read --config_file '" << FLAGS_config_file
                << "': " << ex.what();
      return 1;
    }
    XLOG(INFO) << "No reboot_cause_finder config for this platform; "
                  "continuing with no hardware providers. Kernel panic and "
                  "manual reboot detection still apply. "
               << ex.what();
  }

  if (configJson.has_value()) {
    try {
      config = apache::thrift::SimpleJSONSerializer::deserialize<
          reboot_cause_config::RebootCauseConfig>(*configJson);
    } catch (const std::exception& ex) {
      XLOG(ERR) << "Failed to parse reboot_cause_finder config: " << ex.what();
      return 1;
    }
    if (!ConfigValidator().isValid(config)) {
      XLOG(ERR) << "Invalid reboot_cause_finder config";
      config = {};
      return 1;
    }
  }

  return 0;
}

// The per-run event is logged through this so that a failure to log, however
// unlikely, is only logged itself: it must not turn a recorded run into a
// failed one, or escape a catch block and crash the binary.
template <typename LogFn>
void logRunEvent(LogFn&& log) {
  try {
    helpers::StructuredLogger structuredLogger("reboot_cause_finder");
    log(structuredLogger);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to log the run event: " << ex.what();
  } catch (...) {
    XLOG(ERR) << "Failed to log the run event";
  }
}

} // namespace

int main(int argc, char** argv) {
  fb303::registerFollyLoggingOptionHandlers();

  helpers::init(&argc, &argv);

  // One structured event per run, which netstate streams to Scuba so runs can
  // be counted across the fleet: outcome, the record's cause status, and which
  // providers could not be read.
  // One line on every way out, so the log file shows each run ended and how.
  // An exception exits 1 instead of crashing; the run's INTERRUPTED record is
  // already on disk.
  try {
    reboot_cause_config::RebootCauseConfig config;
    const int status = run(config);
    const auto result = RebootCauseFinderImpl(config).determineRebootCause();
    logRunEvent([&](helpers::StructuredLogger& logger) {
      logger.logEvent("run_complete", runEventTags(result, status));
    });
    XLOG(INFO) << fmt::format(
        "reboot_cause_finder exiting with status {}", status);
    return status;
  } catch (const std::exception& ex) {
    logRunEvent([&](helpers::StructuredLogger& logger) {
      logger.logAlert("run_failed", ex.what());
    });
    XLOG(ERR) << fmt::format(
        "reboot_cause_finder exiting on an uncaught exception: {}", ex.what());
    return 1;
  } catch (...) {
    logRunEvent([](helpers::StructuredLogger& logger) {
      logger.logAlert("run_failed", "non-standard exception");
    });
    XLOG(ERR) << "reboot_cause_finder exiting on an uncaught exception";
    return 1;
  }
}
