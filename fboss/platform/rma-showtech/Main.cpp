// Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
// All Rights Reserved.
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

#include <CLI/CLI.hpp> // IWYU pragma: keep
#include <fmt/core.h>
#include <folly/ScopeGuard.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/helpers/InitCli.h"
#include "fboss/platform/rma-showtech/Utils.h"
#include "fboss/platform/rma-showtech/gen-cpp2/showtech_config_types.h"

using namespace facebook;
using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::showtech_config;

namespace {
enum class Disruptiveness {
  NONDISRUPTIVE,
  DISRUPTIVE,
};
using FunctionWithDisruptiveFlag =
    std::pair<std::function<void(Utils&)>, Disruptiveness>;
const std::vector<std::pair<std::string, FunctionWithDisruptiveFlag>>
    DETAIL_FUNCTIONS = {
        {"host",
         {[](Utils& util) { util.printHostDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"fboss",
         {[](Utils& util) { util.printFbossDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"powergood",
         {[](Utils& util) { util.printPowerGoodDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"weutil",
         {[](Utils& util) { util.printWeutilDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"fwutil",
         {[](Utils& util) {
            util.printFwutilDetails();
            util.printMfrVersionDetails();
          },
          Disruptiveness::DISRUPTIVE}},
        {"lspci",
         {[](Utils& util) { util.printLspciDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"port",
         {[](Utils& util) { util.printPortDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"sensor",
         {[](Utils& util) { util.printSensorDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"psu",
         {[](Utils& util) { util.printPsuDetails(); },
          Disruptiveness::DISRUPTIVE}},
        {"pem",
         {[](Utils& util) { util.printPemDetails(); },
          Disruptiveness::DISRUPTIVE}},
        {"fan",
         {[](Utils& util) { util.printFanDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"fanspinner",
         {[](Utils& util) { util.printFanspinnerDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"gpio",
         {[](Utils& util) { util.printGpioDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"i2c",
         {[](Utils& util) { util.printI2cDetails(); },
          Disruptiveness::DISRUPTIVE}},
        {"regdump",
         {[](Utils& util) { util.printRegDumpDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"i2cdump",
         {[](Utils& util) { util.printI2cDumpDetails(); },
          Disruptiveness::DISRUPTIVE}},
        {"i2creg",
         {[](Utils& util) { util.printDeviceRegistersDetails(); },
          Disruptiveness::DISRUPTIVE}},
        {"nvme",
         {[](Utils& util) { util.printNvmeDetails(); },
          Disruptiveness::NONDISRUPTIVE}},
        {"logs",
         {[](Utils& util) { util.printLogs(); },
          Disruptiveness::NONDISRUPTIVE}},
};

std::set<std::string> getValidDetailNames() {
  std::set<std::string> result{"all"};
  for (const auto& [name, func] : DETAIL_FUNCTIONS) {
    result.insert(name);
  }
  return result;
}

std::string getDetailsHelpText() {
  std::string nonDisruptive, disruptive;
  for (const auto& [name, func] : DETAIL_FUNCTIONS) {
    if (func.second == Disruptiveness::DISRUPTIVE) {
      if (!disruptive.empty()) {
        disruptive += ", ";
      }
      disruptive += name;
    } else {
      if (!nonDisruptive.empty()) {
        nonDisruptive += ", ";
      }
      nonDisruptive += name;
    }
  }
  return fmt::format(
      "Comma-separated list of details to print.\n"
      "  Safe:       {}\n"
      "  Disruptive: {} (require --disruptive. System behavior undefined)\n"
      "  all         Run everything (includes disruptive. System behavior undefined)\n",
      nonDisruptive,
      disruptive);
}

void executeSingleDetail(
    Utils& showtechUtil,
    const std::string& name,
    const FunctionWithDisruptiveFlag& detailDescriptor,
    bool disruptiveMode,
    const std::optional<std::string>& outputDir) {
  std::ofstream outFile;
  std::streambuf* origBuf = nullptr;
  if (outputDir) {
    auto filePath = fmt::format("{}/{}.txt", *outputDir, name);
    outFile.open(filePath);
    if (outFile.is_open()) {
      origBuf = std::cout.rdbuf(outFile.rdbuf());
    } else {
      XLOG(ERR) << "Failed to open output file: " << filePath
                << ", falling back to stdout for this detail";
    }
  }
  SCOPE_EXIT {
    if (origBuf) {
      std::cout.rdbuf(origBuf);
    }
  };

  if (disruptiveMode ||
      detailDescriptor.second == Disruptiveness::NONDISRUPTIVE) {
    detailDescriptor.first(showtechUtil);
  } else {
    std::string upperCaseName = name;
    std::transform(
        upperCaseName.begin(),
        upperCaseName.end(),
        upperCaseName.begin(),
        ::toupper);
    std::cout
        << fmt::format(
               "##### Skipping: {}(Disruptive), in non-disruptive mode #####",
               upperCaseName)
        << std::endl;
  }
}

void executeRequestedDetails(
    Utils& showtechUtil,
    const std::vector<std::string>& requestedDetails,
    bool disruptiveMode,
    const std::optional<std::string>& outputDir) {
  bool runAll =
      std::ranges::find(requestedDetails, "all") != requestedDetails.end();

  if (runAll) {
    XLOG(INFO) << "Running all detail functions";
    for (const auto& [name, funcWithFlag] : DETAIL_FUNCTIONS) {
      executeSingleDetail(
          showtechUtil, name, funcWithFlag, disruptiveMode, outputDir);
    }
  } else {
    for (const auto& requestedDetail : requestedDetails) {
      auto it = std::ranges::find_if(DETAIL_FUNCTIONS, [&](const auto& pair) {
        return pair.first == requestedDetail;
      });
      if (it != DETAIL_FUNCTIONS.end()) {
        executeSingleDetail(
            showtechUtil, it->first, it->second, disruptiveMode, outputDir);
      }
    }
  }
}

constexpr auto kTarScratchDir = "/tmp/rma-showtech-scratch";

// Creates (recreating if already present) the scratch directory under /tmp
// for --tar output. Returns its path, or std::nullopt on failure.
std::optional<std::string> makeTarScratchDir() {
  std::error_code ec;
  std::filesystem::remove_all(kTarScratchDir, ec);
  std::filesystem::create_directory(kTarScratchDir, ec);
  if (ec) {
    return std::nullopt;
  }
  return std::string(kTarScratchDir);
}

// Tars/compresses scratchDir into /tmp/rma-showtech-<timestamp>.tar.gz (a
// fresh timestamped name each run, so repeated invocations don't clobber
// each other and archives stay distinguishable once uploaded elsewhere),
// removes scratchDir either way, and returns the tarball path, or
// std::nullopt on failure.
std::optional<std::string> packageTarball(const std::string& scratchDir) {
  std::time_t now = std::time(nullptr);
  std::tm nowTm{};
  localtime_r(&now, &nowTm);
  char timestamp[32];
  std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &nowTm);
  auto tarballPath = fmt::format("/tmp/rma-showtech-{}.tar.gz", timestamp);

  auto scratchDirName = std::filesystem::path(scratchDir).filename().string();

  PlatformUtils platformUtils;
  auto [exitStatus, output] = platformUtils.runCommand(
      {"/usr/bin/tar", "-czf", tarballPath, "-C", "/tmp", scratchDirName});
  if (exitStatus != 0) {
    XLOG(ERR) << "tar failed: " << output;
  }
  std::filesystem::remove_all(scratchDir);
  return exitStatus == 0 ? std::optional<std::string>(tarballPath)
                         : std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
  CLI::App app{"Showtech utility for collecting system diagnostics"};
  app.set_version_flag("--version", helpers::getBuildVersion());

  std::vector<std::string> detailsArg = {};
  std::string configFilePath;
  bool disruptiveMode = false;
  bool tarMode = false;

  app.add_flag(
      "--disruptive",
      disruptiveMode,
      "Enable Disruptive Mode to run disruptive functions");
  app.add_option("--details", detailsArg, getDetailsHelpText())
      ->delimiter(',')
      ->required()
      ->check(CLI::IsMember(getValidDetailNames()));
  app.add_flag(
      "--tar",
      tarMode,
      "Write the requested details to per-detail files and package them "
      "into a compressed tarball under /tmp instead of printing to stdout");

  app.add_option(
      "--config_file", configFilePath, "Path to the showtech config file");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e);
  }

  try {
    std::string showtechConfJson =
        ConfigLib(configFilePath).getShowtechConfig();
    auto config =
        apache::thrift::SimpleJSONSerializer::deserialize<ShowtechConfig>(
            showtechConfJson);

    std::optional<std::string> outputDir;
    if (tarMode) {
      outputDir = makeTarScratchDir();
      if (!outputDir) {
        XLOG(ERR) << "Failed to create scratch directory under /tmp";
        return 1;
      }
    }

    Utils showtechUtil(config);
    executeRequestedDetails(
        showtechUtil, detailsArg, disruptiveMode, outputDir);

    if (tarMode) {
      auto tarballPath = packageTarball(*outputDir);
      if (!tarballPath) {
        XLOG(ERR) << "Failed to package tarball";
        return 1;
      }
      std::cout << *tarballPath << std::endl;
    }
  } catch (const std::exception& e) {
    XLOG(ERR) << "Error during showtech execution: " << e.what();
    return 1;
  } catch (...) {
    XLOG(ERR) << "Unknown error during showtech execution";
    return 1;
  }

  return 0;
}
