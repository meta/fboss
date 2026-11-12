/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

/**
 * End-to-end tests for 'fboss2-dev config interface <name> profile <P>'.
 *
 * Test candidates are derived from the device's PlatformMapping (fetched via
 * thrift) rather than hardcoded profile strings: we locate a controlling port
 * and, from its supported profiles, pick profiles that (1) it does not support,
 * (2) it supports without subsuming any port, and (3) it supports and that
 * subsume at least one port currently present in the config. This keeps the
 * tests portable across platforms and exercises the subsumed-port removal path.
 *
 * Requirements:
 * - FBOSS agent must be running with a valid configuration
 * - The test must be run as root (or with appropriate permissions)
 */

#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/gen-cpp2/platform_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

using namespace facebook::fboss;

class ConfigInterfaceProfileTest : public Fboss2IntegrationTest {
 protected:
  // A concrete profile-change to exercise, derived from the platform mapping.
  struct Candidate {
    std::string portName; // controlling port
    std::string currentProfile; // enum name currently on the port
    std::string targetProfile; // enum name to change to
    std::vector<std::string>
        subsumedPresent; // present ports the change removes
  };

  struct MultiInterfaceCandidate {
    Candidate first;
    Candidate second;
  };

  // A present controlling port C whose current profile subsumes an absent
  // subport S, plus a profile S supports. This scenario is stable (no mutation
  // needed): creating S is rejected because C still subsumes it.
  std::optional<CreatableCandidate> findStillSubsumedCandidate() const {
    auto mapping = fetchPlatformMapping();
    auto present = fetchPresentPorts();
    auto groups = utility::getSubsidiaryPortIDs(mapping.getPlatformPorts());
    for (const auto& [controlling, unused] : groups) {
      int32_t cid = static_cast<int32_t>(controlling);
      auto pit = present.find(cid);
      if (pit == present.end()) {
        continue;
      }
      cfg::PortProfileID currentProfile;
      if (!apache::thrift::TEnumTraits<cfg::PortProfileID>::findValue(
              pit->second.profileId.c_str(), &currentProfile)) {
        continue;
      }
      for (auto sp : mapping.getSubsumedPorts(controlling, currentProfile)) {
        int32_t sid = static_cast<int32_t>(sp);
        if (present.find(sid) != present.end()) {
          continue; // S must be absent
        }
        const auto& subMapping = mapping.getPlatformPort(sid);
        if (subMapping.mapping()->portType() != cfg::PortType::INTERFACE_PORT) {
          continue;
        }
        auto subProfiles = supportedProfileNames(mapping, sid);
        if (subProfiles.empty()) {
          continue;
        }
        return CreatableCandidate{
            pit->second.name,
            pit->second.profileId,
            *subMapping.mapping()->name(),
            *subProfiles.begin(),
            ""};
      }
    }
    return std::nullopt;
  }

  /**
   * Set a profile on an interface and commit the config.
   * Only commits if the CLI set succeeds (mirrors setInterfaceSpeed).
   */
  Result setInterfaceProfile(
      const std::string& interfaceName,
      const std::string& profile) {
    auto result =
        runCli({"config", "interface", interfaceName, "profile", profile});
    if (result.exitCode == 0) {
      commitConfig();
    }
    return result;
  }

  // Any port currently present in the agent, regardless of its name. Returns
  // nullopt if the agent reports no ports.
  std::optional<PresentPort> findAnyPresentPort() const {
    auto present = fetchPresentPorts();
    if (present.empty()) {
      return std::nullopt;
    }
    return present.begin()->second;
  }

  // A present port + a valid PortProfileID it does NOT support.
  std::optional<Candidate> findUnsupportedProfileCandidate() const {
    auto mapping = fetchPlatformMapping();
    auto present = fetchPresentPorts();
    const auto& platformPorts = mapping.getPlatformPorts();
    for (const auto& [id, pp] : present) {
      auto it = platformPorts.find(id);
      if (it == platformPorts.end()) {
        continue;
      }
      std::set<cfg::PortProfileID> supported;
      for (const auto& [profileId, unused] : *it->second.supportedProfiles()) {
        supported.insert(profileId);
      }
      for (const auto profileId :
           apache::thrift::TEnumTraits<cfg::PortProfileID>::values) {
        if (profileId == cfg::PortProfileID::PROFILE_DEFAULT) {
          continue;
        }
        if (supported.find(profileId) == supported.end()) {
          return Candidate{
              pp.name,
              pp.profileId,
              apache::thrift::util::enumNameSafe(profileId),
              {}};
        }
      }
    }
    return std::nullopt;
  }

  // A present, self-controlling port + a supported profile (!= current)
  // that subsumes nothing.
  std::optional<Candidate> findNonSubsumingCandidate() const {
    auto mapping = fetchPlatformMapping();
    auto present = fetchPresentPorts();
    auto groups = utility::getSubsidiaryPortIDs(mapping.getPlatformPorts());
    for (const auto& [controlling, unused] : groups) {
      int32_t cid = static_cast<int32_t>(controlling);
      auto pit = present.find(cid);
      if (pit == present.end()) {
        continue;
      }
      const auto& entry = mapping.getPlatformPort(cid);
      for (const auto& [profileId, unused2] : *entry.supportedProfiles()) {
        auto name = apache::thrift::util::enumNameSafe(profileId);
        if (name == pit->second.profileId) {
          continue;
        }
        if (mapping.getSubsumedPorts(controlling, profileId).empty()) {
          return Candidate{pit->second.name, pit->second.profileId, name, {}};
        }
      }
    }
    return std::nullopt;
  }

  // Two distinct present controlling ports with a common supported profile
  // that differs from both current profiles.
  std::optional<MultiInterfaceCandidate> findMultiInterfaceCandidate() const {
    auto mapping = fetchPlatformMapping();
    auto present = fetchPresentPorts();
    auto groups = utility::getSubsidiaryPortIDs(mapping.getPlatformPorts());
    std::map<std::string, Candidate> candidateByTargetProfile;
    for (const auto& [controlling, unused] : groups) {
      int32_t cid = static_cast<int32_t>(controlling);
      auto pit = present.find(cid);
      if (pit == present.end()) {
        continue;
      }
      const auto& entry = mapping.getPlatformPort(cid);
      for (const auto& [profileId, unused2] : *entry.supportedProfiles()) {
        auto name = apache::thrift::util::enumNameSafe(profileId);
        if (name == pit->second.profileId) {
          continue;
        }
        Candidate candidate{pit->second.name, pit->second.profileId, name, {}};
        auto [it, inserted] = candidateByTargetProfile.emplace(name, candidate);
        if (!inserted) {
          return MultiInterfaceCandidate{it->second, std::move(candidate)};
        }
      }
    }
    return std::nullopt;
  }

  // A present, self-controlling port + a supported profile (!= current)
  // that subsumes at least one currently-present port.
  std::optional<Candidate> findSubsumingCandidate() const {
    auto mapping = fetchPlatformMapping();
    auto present = fetchPresentPorts();
    auto groups = utility::getSubsidiaryPortIDs(mapping.getPlatformPorts());
    for (const auto& [controlling, unused] : groups) {
      int32_t cid = static_cast<int32_t>(controlling);
      auto pit = present.find(cid);
      if (pit == present.end()) {
        continue;
      }
      const auto& entry = mapping.getPlatformPort(cid);
      for (const auto& [profileId, unused2] : *entry.supportedProfiles()) {
        auto name = apache::thrift::util::enumNameSafe(profileId);
        if (name == pit->second.profileId) {
          continue;
        }
        std::vector<std::string> subsumedPresent;
        for (auto sp : mapping.getSubsumedPorts(controlling, profileId)) {
          auto sit = present.find(static_cast<int32_t>(sp));
          if (sit != present.end()) {
            subsumedPresent.push_back(sit->second.name);
          }
        }
        if (!subsumedPresent.empty()) {
          return Candidate{
              pit->second.name, pit->second.profileId, name, subsumedPresent};
        }
      }
    }
    return std::nullopt;
  }
};

// Case 1: Changing a port to a valid profile it does not support is rejected
// per-port (before commit), leaving the running config unchanged.
TEST_F(ConfigInterfaceProfileTest, ChangeToUnsupportedProfileRejected) {
  auto cand = findUnsupportedProfileCandidate();
  if (!cand) {
    GTEST_SKIP() << "No present port with an unsupported profile found";
  }
  XLOG(INFO) << "[Case 1] port=" << cand->portName
             << " unsupported target=" << cand->targetProfile
             << " (current=" << cand->currentProfile << ")";

  auto result = runCli(
      {"config", "interface", cand->portName, "profile", cand->targetProfile});
  EXPECT_NE(result.exitCode, 0)
      << "Expected non-zero exit for an unsupported profile";
  EXPECT_THAT(result.stderr, ::testing::HasSubstr("Invalid profile"));

  // Not committed → agent still reports the original profile.
  auto info = getPortRunningInfo(cand->portName);
  EXPECT_EQ(info.profileId, cand->currentProfile);
  XLOG(INFO) << "TEST PASSED";
}

// Case 2: Changing a controlling port to a supported profile that subsumes no
// ports succeeds and is reflected by the agent.
TEST_F(ConfigInterfaceProfileTest, ChangeProfileWithoutSubsumingSucceeds) {
  auto cand = findNonSubsumingCandidate();
  if (!cand) {
    GTEST_SKIP()
        << "No self-controlling port with a non-subsuming alternate profile";
  }
  XLOG(INFO) << "[Case 2] port=" << cand->portName << " "
             << cand->currentProfile << " -> " << cand->targetProfile;

  auto result = setInterfaceProfile(cand->portName, cand->targetProfile);
  ASSERT_EQ(result.exitCode, 0) << result.stderr;

  auto after =
      waitForPortRunningInfo(cand->portName, [&cand](const auto& info) {
        return info.profileId == cand->targetProfile;
      });
  EXPECT_EQ(after.profileId, cand->targetProfile);

  // Restore original profile.
  auto restore = setInterfaceProfile(cand->portName, cand->currentProfile);
  EXPECT_EQ(restore.exitCode, 0) << restore.stderr;
  XLOG(INFO) << "TEST PASSED";
}

// Case 3: Changing a controlling port to a supported profile that subsumes a
// currently-present port succeeds, and the subsumed port is removed from the
// config (verified via the agent).
TEST_F(ConfigInterfaceProfileTest, ChangeProfileSubsumesAndRemovesPorts) {
  auto cand = findSubsumingCandidate();
  if (!cand) {
    GTEST_SKIP()
        << "No self-controlling port with a subsuming profile over a present port";
  }
  XLOG(INFO) << "[Case 3] port=" << cand->portName << " "
             << cand->currentProfile << " -> " << cand->targetProfile
             << "; expected to remove: "
             << folly::join(", ", cand->subsumedPresent);

  auto result = setInterfaceProfile(cand->portName, cand->targetProfile);
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  // The CLI reports the ports it auto-removed.
  EXPECT_THAT(
      result.stdout, ::testing::HasSubstr("auto-removed subsumed port"));

  // A subsuming profile change removes a port, which commits as an agent
  // coldboot (T281221621). Wait for the agent to come back before querying it.
  waitForAgentReady();

  auto after =
      waitForPortRunningInfo(cand->portName, [&cand](const auto& info) {
        return info.profileId == cand->targetProfile;
      });
  EXPECT_EQ(after.profileId, cand->targetProfile);

  // Each subsumed port that was present should now be gone from the agent.
  for (const auto& sub : cand->subsumedPresent) {
    EXPECT_TRUE(waitForPortAbsent(sub))
        << sub << " should be removed after a subsuming profile change";
  }

  // Best-effort restore of the controlling port's profile. Note: removal-only
  // semantics mean the subsumed ports are NOT re-added by this restore.
  setInterfaceProfile(cand->portName, cand->currentProfile);
  XLOG(INFO) << "TEST PASSED";
}

// Setting a completely invalid profile string is rejected before commit
// (parseProfile path, distinct from the per-port unsupported-profile path).
TEST_F(ConfigInterfaceProfileTest, SetInvalidProfileFails) {
  XLOG(INFO) << "[Step 1] Finding an interface to test...";
  auto port = findAnyPresentPort();
  if (!port) {
    GTEST_SKIP() << "No present port found";
  }
  XLOG(INFO) << "  Using interface: " << port->name;

  XLOG(INFO)
      << "[Step 2] Setting invalid profile 'PROFILE_COMPLETELY_BOGUS_9999'...";
  auto result =
      setInterfaceProfile(port->name, "PROFILE_COMPLETELY_BOGUS_9999");
  XLOG(INFO) << "  Exit code: " << result.exitCode;

  EXPECT_NE(result.exitCode, 0)
      << "Expected non-zero exit code for invalid profile";
  EXPECT_NE(result.stderr.find("not a valid PortProfileID"), std::string::npos)
      << "Expected 'not a valid PortProfileID' in stderr, got: "
      << result.stderr;

  XLOG(INFO) << "TEST PASSED";
}

// Creating an absent subport is rejected while its controlling port's current
// profile still subsumes it. No mutation occurs, so the port stays absent.
TEST_F(ConfigInterfaceProfileTest, CreateStillSubsumedPortRejected) {
  auto cand = findStillSubsumedCandidate();
  if (!cand) {
    GTEST_SKIP()
        << "No absent subport whose present controlling port subsumes it";
  }
  XLOG(INFO) << "[CreateStillSubsumed] subport=" << cand->subportName
             << " profile=" << cand->subportProfile
             << " controlling=" << cand->controllingName << " ("
             << cand->controllingProfile << ")";

  auto result = runCli(
      {"config",
       "interface",
       cand->subportName,
       "profile",
       cand->subportProfile});
  EXPECT_NE(result.exitCode, 0)
      << "Expected non-zero exit creating a still-subsumed port";
  EXPECT_THAT(result.stderr, ::testing::HasSubstr("controlling port"));
  EXPECT_THAT(result.stderr, ::testing::HasSubstr("subsumes"));

  // Rejected before commit -> the subport is still absent from the agent.
  EXPECT_THROW(getPortRunningInfo(cand->subportName), std::runtime_error);
  XLOG(INFO) << "TEST PASSED";
}

// Narrowing a controlling port to free an absent subport AND creating that
// subport in a single 'config interface C S profile <narrow>' command succeeds:
// the existing controlling port is handled first (narrowed, freeing the
// subport), then the freed subport is created -- both at the narrow profile.
// This exercises the atomic existing-then-create ordering end to end.
TEST_F(ConfigInterfaceProfileTest, CreateFreedPortSucceeds) {
  auto cand = findFreeableCandidate();
  if (!cand) {
    GTEST_SKIP()
        << "No absent subport freeable by narrowing its controlling port";
  }
  XLOG(INFO) << "[CreateFreedPort] one command narrows controlling="
             << cand->controllingName << " (" << cand->controllingProfile
             << ") and creates subport=" << cand->subportName << " both at "
             << cand->narrowProfile;

  // A single command lists both the existing controlling port and the absent
  // subport at the narrow profile. The controlling port is narrowed first
  // (freeing the subport), then the subport is created.
  auto create = runCli(
      {"config",
       "interface",
       cand->controllingName,
       cand->subportName,
       "profile",
       cand->narrowProfile});
  ASSERT_EQ(create.exitCode, 0) << create.stderr;
  commitConfig();

  // Both ports now report the narrow profile in the agent.
  auto subInfo = waitForPortRunningInfo(
      cand->subportName,
      [&cand](const auto& i) { return i.profileId == cand->narrowProfile; });
  EXPECT_EQ(subInfo.profileId, cand->narrowProfile)
      << "freed subport should be created at the narrow profile";
  auto ctrlInfo = waitForPortRunningInfo(
      cand->controllingName,
      [&cand](const auto& i) { return i.profileId == cand->narrowProfile; });
  EXPECT_EQ(ctrlInfo.profileId, cand->narrowProfile)
      << "controlling port should be narrowed to the narrow profile";

  // Best-effort restore of the controlling port's original profile.
  // Removal-only semantics mean the created subport is NOT removed by this
  // restore.
  setInterfaceProfile(cand->controllingName, cand->controllingProfile);
  XLOG(INFO) << "TEST PASSED";
}

// Creating a freed subport with profile AND mtu in a single command wires a
// full L3 interface: the agent reports the new port at the profile and its
// interface MTU reflects the requested value.
TEST_F(ConfigInterfaceProfileTest, CreateFreedPortWithMtuInOneCommand) {
  auto cand = findFreeableCandidate();
  if (!cand) {
    GTEST_SKIP()
        << "No absent subport freeable by narrowing its controlling port";
  }
  constexpr int kMtu = 9000;
  XLOG(INFO) << "[CreateFreedPortWithMtu] controlling=" << cand->controllingName
             << " " << cand->controllingProfile << " -> " << cand->narrowProfile
             << "; create subport=" << cand->subportName
             << " profile=" << cand->subportProfile << " mtu=" << kMtu;

  // Free the subport by narrowing its controlling port.
  auto narrow = setInterfaceProfile(cand->controllingName, cand->narrowProfile);
  ASSERT_EQ(narrow.exitCode, 0) << narrow.stderr;

  // Create the subport with profile and mtu in one command.
  auto create = runCli(
      {"config",
       "interface",
       cand->subportName,
       "profile",
       cand->subportProfile,
       "mtu",
       std::to_string(kMtu)});
  ASSERT_EQ(create.exitCode, 0) << create.stderr;
  commitConfig();

  auto info = waitForPortRunningInfo(cand->subportName, [&cand](const auto& i) {
    return i.profileId == cand->subportProfile;
  });
  EXPECT_EQ(info.profileId, cand->subportProfile);

  // The created port wires a full L3 interface, so 'show interface' reports the
  // requested MTU.
  auto intf = waitForInterfaceInfo(
      cand->subportName, [](const auto& i) { return i.mtu == kMtu; });
  EXPECT_EQ(intf.mtu, kMtu);

  // Best-effort restore of the controlling port's original profile.
  setInterfaceProfile(cand->controllingName, cand->controllingProfile);
  XLOG(INFO) << "TEST PASSED";
}

// A single command changes two distinct ports to a shared alternate profile.
TEST_F(ConfigInterfaceProfileTest, SetProfileMultiInterface) {
  auto cand = findMultiInterfaceCandidate();
  if (!cand) {
    GTEST_SKIP() << "No two controlling ports share an alternate profile";
  }
  XLOG(INFO) << "Changing " << cand->first.portName << " ("
             << cand->first.currentProfile << ") and " << cand->second.portName
             << " (" << cand->second.currentProfile << ") to "
             << cand->first.targetProfile;

  auto result = runCli(
      {"config",
       "interface",
       cand->first.portName,
       cand->second.portName,
       "profile",
       cand->first.targetProfile});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  commitConfig();

  for (const auto& candidate : {cand->first, cand->second}) {
    auto after = waitForPortRunningInfo(
        candidate.portName, [&candidate](const auto& info) {
          return info.profileId == candidate.targetProfile;
        });
    EXPECT_EQ(after.profileId, candidate.targetProfile);
  }

  // Restore each port independently because their original profiles may differ.
  setInterfaceProfile(cand->first.portName, cand->first.currentProfile);
  setInterfaceProfile(cand->second.portName, cand->second.currentProfile);
  XLOG(INFO) << "TEST PASSED";
}
