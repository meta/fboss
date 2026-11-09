/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <gtest/gtest.h>
#include "fboss/agent/types.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"
#include "fboss/qsfp_service/test/hw_test/gen-cpp2/qsfp_production_features_types.h"

#include <folly/logging/xlog.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

DECLARE_bool(setup_for_warmboot);
DECLARE_bool(list_production_feature);

namespace facebook::fboss {

class MultiPimPlatformPimContainer;

class HwTest : public ::testing::Test {
 public:
  explicit HwTest(bool setupOverrideTcvrToPortAndProfile = true);
  ~HwTest() override = default;

  HwQsfpEnsemble* getHwQsfpEnsemble() {
    return ensemble_.get();
  }

  MultiPimPlatformPimContainer* getPimContainer(int pimID);

  void SetUp() override;
  void TearDown() override;
  template <typename SETUP_FN, typename VERIFY_FN>
  void verifyAcrossWarmBoots(SETUP_FN setup, VERIFY_FN verify) {
    if (!didWarmBoot()) {
      XLOG(INFO) << "STAGE: cold boot setup()";
      setup();
    }

    XLOG(INFO) << " STAGE: verify";
    verify();
    if (FLAGS_setup_for_warmboot) {
      XLOG(INFO) << " STAGE: setupForWarmboot";
      setupForWarmboot();
    }
  }

  std::vector<TransceiverID> refreshTransceiversWithRetry(int numRetries = 3);

  // This function is only used if new port programming feature is enabled
  // We will wait till all the cabled transceivers reach the
  // TRANSCEIVER_PROGRAMMED state by retrying `numRetries` times of
  // TransceiverManager::refreshStateMachines()
  void waitTillCabledTcvrProgrammed(int numRetries = 30, bool portUp = true);

  std::vector<int> getCabledOpticalAndActiveTransceiverIDs();

  // Register the transceivers a test actually exercises. At TearDown we dump
  // per-transceiver metadata (vendor, firmware, media interface, etc.) for the
  // registered transceivers to a file that Netcastle ingests into Scuba. Tests
  // that register nothing dump nothing.
  void addTestedTransceiver(const TransceiverID& id);
  void addTestedTransceivers(const std::vector<TransceiverID>& ids);
  // Overload for helpers that return legacy int32_t transceiver ids.
  void addTestedTransceiverIds(const std::vector<int32_t>& ids);
  // Convenience overload for tests that operate in port space: maps each port
  // to its transceiver via the platform mapping and registers that transceiver.
  void addTestedPorts(const std::vector<PortID>& portIds);
  // Convenience overload for tests that work with (port, profile) pairs, e.g.
  // xphy tests using findAvailableXphyPorts(). Registers the transceiver behind
  // each port.
  template <typename ProfileT>
  void addTestedPorts(
      const std::vector<std::pair<PortID, ProfileT>>& portsAndProfiles) {
    std::vector<PortID> portIds;
    portIds.reserve(portsAndProfiles.size());
    for (const auto& portAndProfile : portsAndProfiles) {
      portIds.push_back(portAndProfile.first);
    }
    addTestedPorts(portIds);
  }
  // Attach a free-form key/value to a tested transceiver's Scuba row.
  void addTestMetadata(
      const TransceiverID& id,
      const std::string& key,
      const std::string& value);

  // Declare the production feature(s) this test verifies (e.g.
  // DATA_PATH_PROGRAMMING, FIRMWARE_UPGRADE). Call from the test body. The
  // feature names are recorded on every dumped Scuba row for the test. This is
  // independent of getProductionFeatures() (the gating method consumed by
  // --list_production_feature) and is used only for metadata/Scuba reporting.
  void addVerifiedProductionFeatures(
      const std::vector<qsfp_production_features::QsfpProductionFeature>&
          features);

 protected:
  bool didWarmBoot() const;
  void printProductionFeatures() const;

 private:
  void setupForWarmboot() const;
  void checkQsfpMemoryInBounds() const;
  // Dump per-transceiver metadata for registered transceivers to the
  // Scuba-ingestion file. Called from TearDown(). Best effort: never fails the
  // test.
  void dumpTestMetadata();
  // Dump one row per transceiver that is present in the chassis but absent from
  // the config, to the Scuba-ingestion file Netcastle publishes to the
  // fboss_link_qsfp_test_port_errors table. Called from dumpTestMetadata().
  // Best effort: never fails the test.
  void dumpPortErrors();
  virtual std::vector<qsfp_production_features::QsfpProductionFeature>
  getProductionFeatures() const;
  // Forbidden copy constructor and assignment operator
  HwTest(HwTest const&) = delete;
  HwTest& operator=(HwTest const&) = delete;

  std::unique_ptr<HwQsfpEnsemble> ensemble_;
  const bool setupOverrideTcvrToPortAndProfile_{false};
  std::set<TransceiverID> testedTransceivers_;
  std::map<TransceiverID, std::map<std::string, std::string>>
      perTransceiverMetadata_;
  std::vector<qsfp_production_features::QsfpProductionFeature>
      verifiedProductionFeatures_;
};
} // namespace facebook::fboss
