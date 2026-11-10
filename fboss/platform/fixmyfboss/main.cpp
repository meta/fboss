/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

#include <CLI/CLI.hpp>
#include <folly/String.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/platform/fixmyfboss/CheckRegistry.h"
#include "fboss/platform/fixmyfboss/CheckRunner.h"
#include "fboss/platform/fixmyfboss/CliOptions.h"
#include "fboss/platform/fixmyfboss/Extensions.h"
#include "fboss/platform/fixmyfboss/PasswordPrompt.h"
#include "fboss/platform/fixmyfboss/ResultPrinter.h"

using namespace facebook::fboss::platform;

namespace {

void listChecks(const std::vector<fixmyfboss::PlannedCheck>& plan) {
  std::cout << "List of checks:\n";
  int index = 1;
  for (const auto& planned : plan) {
    std::string runIndicator;
    if (planned.skipReason) {
      runIndicator = " (skipped: " + *planned.skipReason + ")";
    } else if (planned.skipReasonError) {
      runIndicator = " (error: " + *planned.skipReasonError + ")";
    }
    const auto& check = planned.check.get();
    std::cout << index++ << ". " << check.getName() << ": "
              << check.getDescription() << runIndicator << "\n";
  }
}

void configureLogging(bool debugFlag, bool verboseFlag) {
  // Set default log level to WARNING
  folly::LoggerDB::get().setLevel("", folly::LogLevel::WARN);

  // Update log level based on flags
  if (debugFlag) {
    folly::LoggerDB::get().setLevel("", folly::LogLevel::DBG);
  } else if (verboseFlag) {
    folly::LoggerDB::get().setLevel("", folly::LogLevel::INFO);
  }
}

platform_checks::CheckReport makeReport(
    std::vector<platform_checks::CheckResult> results,
    const fixmyfboss::CheckEnvironment& env) {
  // Cannot happen: createEnvironment() always sets x86.
  if (!env.x86) {
    throw std::logic_error("CheckEnvironment has no x86 host");
  }
  platform_checks::CheckReport report;
  report.results() = std::move(results);
  report.platformName() = env.platformName;
  report.target() = env.x86->name();
  return report;
}

// Makes it obvious, before anything happens, that the switch will be power
// cycled or rebooted.
void warnAboutDisruptiveChecks(
    const std::vector<fixmyfboss::PlannedCheck>& plan,
    const fixmyfboss::CheckEnvironment& env) {
  std::vector<std::string> names;
  for (const auto& planned : plan) {
    if (planned.check.get().isDisruptive() && planned.appliesToPlatform &&
        !planned.skipReason && !planned.skipReasonError) {
      names.push_back(planned.check.get().getName());
    }
  }
  if (!names.empty()) {
    XLOG(WARN) << "Running disruptive checks on " << env.x86->name() << ": "
               << folly::join(", ", names);
  }
}

std::vector<platform_checks::CheckResult> runChecks(
    const std::vector<fixmyfboss::PlannedCheck>& plan,
    const fixmyfboss::CheckRunner& runner,
    const std::string& platformName,
    bool showProgress) {
  XLOG(INFO) << "Starting fixmyfboss checks for platform: " << platformName;
  if (!showProgress) {
    return runner.run(plan);
  }
  fixmyfboss::ResultPrinter printer;
  auto results = runner.run(plan, [&](int index, int total, const auto& check) {
    printer.printProgress(
        "Platform: " + platformName + " - Running " + std::to_string(index) +
        "/" + std::to_string(total) + ": " + check.getName());
  });
  printer.clearLine();
  return results;
}

} // namespace

int main(int argc, char* argv[]) {
  CLI::App app{
      "fixmyfboss - Diagnose and fix FBOSS device issues", "fixmyfboss"};
  fixmyfboss::CliOptions options;
  fixmyfboss::addOptions(app, options);
  const auto reportHooks = fixmyfboss::addExtensionOptions(app);
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e);
  }
  configureLogging(options.debug, options.verbose);
  fixmyfboss::resolveTransport(options);

  // Never taken from the command line, which is logged.
  options.connect.askPassword = fixmyfboss::readPasswordFromTerminal;

  fixmyfboss::CheckEnvironment env;
  try {
    env = fixmyfboss::createEnvironment(options.connect);
  } catch (const std::exception& ex) {
    XLOG(ERR) << ex.what();
    return EXIT_FAILURE;
  }
  fixmyfboss::applyToEnvironment(options, env);
  auto checks = fixmyfboss::createAllChecks(env);

  std::optional<fixmyfboss::CheckRunner> runner;
  try {
    runner.emplace(env.platformName, options.run);
  } catch (const std::invalid_argument& ex) {
    XLOG(ERR) << "Invalid --checks regex '"
              << options.run.nameFilter.value_or("") << "': " << ex.what();
    return EXIT_FAILURE;
  }
  const auto plan = runner->plan(checks);

  if (options.listChecks) {
    listChecks(plan);
    return EXIT_SUCCESS;
  }

  warnAboutDisruptiveChecks(plan, env);
  const auto report = makeReport(
      runChecks(plan, *runner, env.platformName, !options.json), env);
  const auto& results = *report.results();
  if (options.json) {
    std::cout << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                     report)
              << "\n";
  } else {
    fixmyfboss::ResultPrinter printer(
        std::cout, options.verbose || options.debug);
    printer.printSummary(results);
    printer.printDetails(results);
  }
  for (const auto& hook : reportHooks) {
    hook(report);
  }

  bool allPassed =
      std::all_of(results.begin(), results.end(), [](const auto& result) {
        return *result.status() == platform_checks::CheckStatus::OK ||
            *result.status() == platform_checks::CheckStatus::SKIPPED;
      });

  return allPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
