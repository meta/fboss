/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <folly/Subprocess.h>
#include <folly/logging/xlog.h>
#include <folly/system/Shell.h>
#include <gtest/gtest_prod.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include "fboss/platform/fw_util/FwUtilVersionHandler.h"
#include "fboss/platform/fw_util/if/gen-cpp2/fw_util_config_types.h"
#include "fboss/platform/helpers/PlatformUtils.h"

namespace facebook::fboss::platform::fw_util {

using namespace facebook::fboss::platform::fw_util_config;

class FwUtilImpl {
 public:
  explicit FwUtilImpl(
      const std::string& fwBinaryFile,
      const std::string& configFilePath,
      bool verifySha1sum,
      bool dryRun)
      : fwBinaryFile_(fwBinaryFile),
        configFilePath_(configFilePath),
        verifySha1sum_(verifySha1sum),
        dryRun_(dryRun) {
    init();
  }
  void doVersionAudit();
  std::vector<std::string> getFpdNameList();
  void doFirmwareAction(const std::string&, const std::string&);
  void printVersion(const std::string&);
  // Finds matching fpd case-insensitive
  std::tuple<std::string, FwConfig> getFpd(const std::string&);

 private:
  // Friend declarations for unit tests
  FRIEND_TEST(FwUtilOperationsTest, DoJtagOperationValidPath);
  FRIEND_TEST(FwUtilOperationsTest, DoJtagOperationEmptyPath);
  FRIEND_TEST(FwUtilOperationsTest, DoJtagOperationVariousValues);
  FRIEND_TEST(FwUtilOperationsTest, DoJtagOperationFileOverwrite);
  FRIEND_TEST(FwUtilVerifyTest, PerformVerifyFlashromWithArgs);
  FRIEND_TEST(FwUtilVerifyTest, PerformVerifyFlashromWithoutArgs);
  FRIEND_TEST(FwUtilVerifyTest, PerformVerifyUnsupportedCommandType);
  FRIEND_TEST(FwUtilUpgradeTest, DoUpgradeDryRunMode);
  FRIEND_TEST(FwUtilUpgradeTest, DoUpgradeWithValidConfig);
  FRIEND_TEST(FwUtilUpgradeTest, DoUpgradeNoUpgradeConfig);
  FRIEND_TEST(FwUtilUpgradeTest, DoUpgradeWaitsForConfiguredDelay);
  FRIEND_TEST(FwUtilPreUpgradeTest, DoPreUpgradeOperationEmptyCommandType);
  FRIEND_TEST(FwUtilPreUpgradeTest, DoPreUpgradeOperationJtagWithArgs);
  FRIEND_TEST(FwUtilPostUpgradeTest, DoPostUpgradeOperationWithValidArgs);
  FRIEND_TEST(FwUtilPostUpgradeTest, DoPostUpgradeOperationInvalidConfigs);
  FRIEND_TEST(FwUtilReadTest, PerformReadFlashromWithArgs);
  FRIEND_TEST(FwUtilReadTest, PerformReadUnsupportedCommandType);
  FRIEND_TEST(FwUtilFlashromTest, DetectFlashromChipFound);
  FRIEND_TEST(FwUtilFlashromTest, DetectFlashromChipNotFound);
  FRIEND_TEST(FwUtilFlashromTest, DetectFlashromChipMultiple);
  FRIEND_TEST(FwUtilFlashromTest, CreateCustomContentFileSuccess);
  FRIEND_TEST(FwUtilFlashromTest, CreateCustomContentFileFailure);
  FRIEND_TEST(FwUtilFlashromTest, SetProgrammerAndChipBoth);
  FRIEND_TEST(FwUtilFlashromTest, SetProgrammerAndChipTypeOnly);
  FRIEND_TEST(FwUtilFlashromTest, SetProgrammerAndChipDetected);
  FRIEND_TEST(FwUtilFlashromTest, SetProgrammerAndChipNoProgrammerType);
  FRIEND_TEST(FwUtilFlashromTest, AddLayoutFileValid);
  FRIEND_TEST(FwUtilFlashromTest, AddCommonFlashromArgsWithLayout);
  FRIEND_TEST(FwUtilFlashromTest, AddCommonFlashromArgsWithCustomContent);
  FRIEND_TEST(FwUtilFlashromTest, AddCommonFlashromArgsWithFileOption);
  FRIEND_TEST(FwUtilFlashromTest, AddCommonFlashromArgsWithImageOption);
  FRIEND_TEST(FwUtilFlashromTest, AddCommonFlashromArgsMinimal);
  FRIEND_TEST(FwUtilFlashromTest, PerformFlashromUpgradeSuccess);
  FRIEND_TEST(FwUtilFlashromTest, PerformFlashromUpgradeDryRun);
  FRIEND_TEST(FwUtilFlashromTest, PerformFlashromUpgradeMissingBinary);
  FRIEND_TEST(FwUtilFlashromTest, PerformFlashromReadSuccess);
  FRIEND_TEST(FwUtilOperationsTest, PerformJamUpgradeMissingBinary);
  FRIEND_TEST(FwUtilOperationsTest, PerformXappUpgradeMissingBinary);
  FRIEND_TEST(FwUtilOperationsTest, PerformPsuUtilUpgradeMissingBinary);
  FRIEND_TEST(FwUtilOperationsTest, DoGpiosetOperationSuccess);
  FRIEND_TEST(FwUtilOperationsTest, DoGpiosetOperationInvalidChip);
  FRIEND_TEST(FwUtilOperationsTest, DoGpiogetOperationSuccess);
  FRIEND_TEST(FwUtilOperationsTest, DoWriteToPortOperationSuccess);
  FRIEND_TEST(FwUtilImplTest, GetFpdNameListReturnsAll);
  FRIEND_TEST(FwUtilImplTest, GetFpdCaseInsensitive);
  FRIEND_TEST(FwUtilImplTest, GetFpdReturnsAll);
  FRIEND_TEST(FwUtilImplTest, GetFpdInvalidThrows);
  FRIEND_TEST(FwUtilImplTest, DoVersionAuditVersionMismatch);
  FRIEND_TEST(FwUtilImplTest, DoFirmwareActionInvalidAction);
  FRIEND_TEST(FwUtilImplTest, PrintVersionInvalidFpd);
  FRIEND_TEST(FwUtilImplTest, FpdListSortedByPriority);

  void doPreUpgrade(const std::string&);

  void doPreUpgradeOperation(
      const PreFirmwareOperationConfig&,
      const std::string&);

  void doJtagOperation(const JtagConfig&, const std::string&);
  void doGpiosetOperation(const GpiosetConfig&, const std::string&);
  void doUpgrade(const std::string&);
  void performFlashromUpgrade(const FlashromConfig&, const std::string&);
  void addCommonFlashromArgs(
      const FlashromConfig&,
      const std::string&,
      const std::string&,
      std::vector<std::string>&);
  void setProgrammerAndChip(
      const FlashromConfig&,
      const std::string&,
      std::vector<std::string>&);
  void addLayoutFile(
      const FlashromConfig&,
      std::vector<std::string>&,
      const std::string&);
  bool
  createCustomContentFile(const std::string&, const int&, const std::string&);
  std::string detectFlashromChip(const FlashromConfig&, const std::string&);
  void performJamUpgrade(const JamConfig&);
  void performXappUpgrade(const XappConfig&);
  void performPsuUtilUpgrade(const PsuUtilConfig&);
  void doPostUpgrade(const std::string&);
  void doPostUpgradeOperation(
      const PostFirmwareOperationConfig&,
      const std::string&);
  void doGpiogetOperation(const GpiogetConfig&, const std::string&);
  void performRead(const ReadFirmwareOperationConfig&, const std::string& fpd);
  void performFlashromRead(const FlashromConfig&, const std::string&);
  void addFileOption(
      const std::string&,
      std::vector<std::string>&,
      std::optional<std::string>&);
  void performFlashromVerify(const FlashromConfig&, const std::string&);
  void performVerify(const VerifyFirmwareOperationConfig&, const std::string&);
  void doWriteToPortOperation(const WriteToPortConfig&, const std::string&);
  void doUpgradeOperation(const UpgradeConfig&, const std::string&);

  FwUtilConfig fwUtilConfig_{};
  std::map<std::string, std::vector<std::string>> spiChip_;
  std::string platformName_;
  std::string fwBinaryFile_;
  std::string configFilePath_;
  bool verifySha1sum_;
  bool dryRun_;

  void init();

  // use to map firmware device with priority for cases where
  // we have to upgrade all the firmware device at once and we
  // have to take the priority into consideration

  std::vector<std::pair<std::string, int>> fwDeviceNamesByPrio_;
  std::unique_ptr<FwUtilVersionHandler> fwUtilVersionHandler_;
};

} // namespace facebook::fboss::platform::fw_util
