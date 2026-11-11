//  Copyright 2021-present Facebook. All Rights Reserved.

#include <CLI/CLI.hpp>
#include <filesystem>

#include <folly/logging/FileHandlerFactory.h>
#include <folly/logging/Init.h>
#include <folly/logging/LogConfigParser.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/fw_util/FwUtilImpl.h"
#include "fboss/platform/helpers/InitCli.h"

using namespace facebook::fboss::platform::fw_util;
using namespace facebook::fboss::platform;

/*
 * This utility will perform firmware upgrade for
 * all BMC Lite platform. Firmware upgrade will
 * include cpld, fpga, and bios.
 */
int main(int argc, char* argv[]) {
  // TODO: Add file lock to prevent multiple instance of fw_util from running
  // simultaneously.
  folly::initLoggingOrDie();
  CLI::App app{"Firmware utility for FBOSS platforms"};
  std::string fw_target_name;
  std::string fw_action;
  std::string fw_binary_file;
  std::string config_file_path;
  bool verify_sha1sum = false;
  bool dry_run = false;

  app.set_version_flag("--version", helpers::getBuildVersion());

  // Group: Firmware Options
  auto fwGroup = app.add_option_group(
      "Firmware Options",
      "Specify firmware action, target, and optional binary file");

  fwGroup
      ->add_option(
          "--fw_target_name",
          fw_target_name,
          "Firmware target name, to see list of targets, run fw_util --list")
      ->default_val("all");

  fwGroup
      ->add_option(
          "--fw_action",
          fw_action,
          "Firmware action (program, verify, read, version, list, audit)")
      ->check(
          CLI::IsMember(
              {"program", "verify", "read", "version", "list", "audit"}))
      ->default_val("version");

  fwGroup->add_option(
      "--fw_binary_file",
      fw_binary_file,
      "Firmware binary file path to be programmed");

  fwGroup->add_option(
      "--config_file", config_file_path, "Path to the fw_util config flie");

  // Group: Flags
  auto flagsGroup = app.add_option_group("Flags", "Additional flags");
  flagsGroup->add_flag(
      "--verify_sha1sum", verify_sha1sum, "Verify SHA1 sum of the firmware");
  flagsGroup->add_flag(
      "--dry_run", dry_run, "Dry run without actually programming firmware");

  // Add examples in the footer/help
  app.footer(
      "Examples:\n"
      "  fw_util  (defaults to --fw_action=version --fw_target_name=all)\n"
      "  fw_util --fw_action=program --fw_target_name=bios --fw_binary_file=/path/to/firmware.bin\n"
      "  fw_util --fw_action=verify --fw_target_name=FPGA --fw_binary_file=/path/to/firmware.bin\n"
      "  fw_util --fw_action=version --fw_target_name=CPLD\n"
      "  fw_util --fw_action=list\n"
      "  fw_util --fw_action=audit\n"
      "  Audit exit codes: 0 match, 1 mismatch or read failure,\n"
      "  2 no target with a desired version.\n");

  CLI11_PARSE(app, argc, argv);

  // Log to a file
  const std::filesystem::path logFolder = "/var/facebook/logs/fboss";
  std::filesystem::create_directories(logFolder);
  const std::filesystem::path logPath = logFolder / "fw_util.log";
  folly::LoggerDB::get().registerHandlerFactory(
      std::make_unique<folly::FileHandlerFactory>(), false);
  folly::LoggerDB::get().updateConfig(
      folly::parseLogConfig(
          "INFO:filehandler:default;"
          "filehandler=file:path=" +
          logPath.string() + ",async=true"));

  // TODO: To be removed once XFN change the commands in their codes
  if (fw_action.empty()) {
    if (argc > 1) {
      fw_target_name = argv[1];
    }
    if (argc > 2) {
      fw_action = argv[2];
    }
    if (argc > 3) {
      fw_binary_file = argv[3];
    }
    XLOG(WARNING)
        << "Deprecation Warning: This current command line format will soon be deprecated.Please consider using fw_util --fw_target_name='binaryName' --fw_action='action' --fw_binary_file='path_to_file_to_be_upraded'";
  }

  if (fw_action == "version" && !fw_binary_file.empty()) {
    XLOG(ERR) << "--fw_binary_file cannot be part of version command";
    exit(1);
  }

  if (fw_action == "program" &&
      (fw_target_name.empty() || fw_binary_file.empty())) {
    XLOG(ERR)
        << "--fw_binary_file and --fw_target_name need to be specified for firmware programming";
    exit(1);
  }

  // Check for file existence only for actions that require a binary file
  if (!fw_binary_file.empty() && fw_action != "read" &&
      !std::filesystem::exists(fw_binary_file)) {
    XLOG(ERR) << "--fw_binary_file cannot be found in the specified path";
    exit(1);
  }

  try {
    FwUtilImpl fwUtilImpl(
        fw_binary_file, config_file_path, verify_sha1sum, dry_run);

    if (fw_action == "version" && !fw_target_name.empty()) {
      fwUtilImpl.printVersion(fw_target_name);
    } else if (
        fw_action == "program" || fw_action == "verify" ||
        fw_action == "read") {
      // For actions which involve more than just reading versions/config, we
      // want to always log all the commands that are run.
      folly::LoggerDB::get()
          .getCategory("fboss.platform.helpers.PlatformUtils")
          ->setLevel(folly::LogLevel::DBG2);
      fwUtilImpl.doFirmwareAction(fw_target_name, fw_action);
    } else if (fw_action == "list") {
      XLOG(INFO) << fmt::format(
          "Supported binary names: {}",
          folly::join(" ", fwUtilImpl.getFpdNameList()));
    } else if (fw_action == "audit") {
      fwUtilImpl.doVersionAudit();
    } else {
      XLOG(ERR)
          << "Wrong usage. please run fw_util --help for the flags needed for proper usage";
      return 1;
    }
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Firmware action failed: " << ex.what();
    return 1;
  }

  return 0;
}
