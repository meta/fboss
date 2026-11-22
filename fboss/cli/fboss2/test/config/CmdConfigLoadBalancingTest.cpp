/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/load_balancing/CmdConfigLoadBalancing.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {

// Return the LoadBalancer with the given id, or nullptr if none.
const cfg::LoadBalancer* findLoadBalancer(
    const cfg::SwitchConfig& sw,
    cfg::LoadBalancerID id) {
  for (const auto& lb : *sw.loadBalancers()) {
    if (*lb.id() == id) {
      return &lb;
    }
  }
  return nullptr;
}

} // namespace

class CmdConfigLoadBalancingTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigLoadBalancingTestFixture()
      : CmdConfigTestBase(
            "fboss_load_balancing_config_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "loadBalancers": [
      {
        "id": 1,
        "fieldSelection": {
          "ipv4Fields": [1, 2],
          "ipv6Fields": [1, 2, 3],
          "transportFields": [1, 2],
          "mplsFields": [],
          "udfGroups": []
        },
        "algorithm": 9,
        "seed": 123456
      },
      {
        "id": 2,
        "fieldSelection": {
          "ipv4Fields": [],
          "ipv6Fields": [],
          "transportFields": [],
          "mplsFields": [],
          "udfGroups": []
        },
        "algorithm": 1
      }
    ]
  }
})") {}

 protected:
  const std::string ecmpCmdPrefix_ = "config load-balancing ecmp";
  const std::string lagCmdPrefix_ = "config load-balancing lag";
};

// =============================================================
// LoadBalancingConfigArgs validation tests
// =============================================================

TEST_F(CmdConfigLoadBalancingTestFixture, argValidation_valid) {
  LoadBalancingConfigArgs a({"hash-algorithm", "crc"});
  EXPECT_EQ(a.getAttribute(), "hash-algorithm");
  EXPECT_EQ(a.getValue(), "crc");

  LoadBalancingConfigArgs b({"hash-seed", "42"});
  EXPECT_EQ(b.getAttribute(), "hash-seed");
  EXPECT_EQ(b.getValue(), "42");

  LoadBalancingConfigArgs c({"hash-fields-ipv4", "src-ip,dst-ip"});
  EXPECT_EQ(c.getAttribute(), "hash-fields-ipv4");
  EXPECT_EQ(c.getValue(), "src-ip,dst-ip");

  LoadBalancingConfigArgs d({"hash-fields-mpls", "none"});
  EXPECT_EQ(d.getAttribute(), "hash-fields-mpls");
  EXPECT_EQ(d.getValue(), "none");
}

TEST_F(CmdConfigLoadBalancingTestFixture, argValidation_badArity) {
  EXPECT_THROW(LoadBalancingConfigArgs({}), std::invalid_argument);
  EXPECT_THROW(LoadBalancingConfigArgs({"hash-seed"}), std::invalid_argument);
  EXPECT_THROW(
      LoadBalancingConfigArgs({"hash-seed", "1", "extra"}),
      std::invalid_argument);
}

TEST_F(CmdConfigLoadBalancingTestFixture, argValidation_unknownAttr) {
  EXPECT_THROW(
      LoadBalancingConfigArgs({"unknown", "x"}), std::invalid_argument);
  EXPECT_THROW(
      LoadBalancingConfigArgs({"Hash-Seed", "1"}), std::invalid_argument);
  EXPECT_THROW(
      LoadBalancingConfigArgs({"hash-fields", "ipv4"}), std::invalid_argument);
}

// =============================================================
// applyLoadBalancerConfig() tests — attribute-level semantics shared across
// ECMP and LAG. Running these against the ECMP seed object also exercises the
// "update existing entry" branch; running against a fresh ID exercises the
// "create new entry" branch.
// =============================================================

TEST_F(CmdConfigLoadBalancingTestFixture, applyHashAlgorithm) {
  cfg::SwitchConfig sw;
  applyLoadBalancerConfig(
      sw, cfg::LoadBalancerID::ECMP, "hash-algorithm", "crc32-hi");
  const auto* lb = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(*lb->algorithm(), cfg::HashingAlgorithm::CRC32_HI);
}

TEST_F(CmdConfigLoadBalancingTestFixture, applyHashAlgorithm_invalid) {
  cfg::SwitchConfig sw;
  EXPECT_THROW(
      applyLoadBalancerConfig(
          sw, cfg::LoadBalancerID::ECMP, "hash-algorithm", "md5"),
      std::invalid_argument);
}

TEST_F(CmdConfigLoadBalancingTestFixture, applyHashSeed_setAndClear) {
  cfg::SwitchConfig sw;
  applyLoadBalancerConfig(sw, cfg::LoadBalancerID::ECMP, "hash-seed", "99");
  const auto* lb = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  ASSERT_TRUE(lb->seed().has_value());
  EXPECT_EQ(*lb->seed(), 99);

  applyLoadBalancerConfig(
      sw, cfg::LoadBalancerID::ECMP, "hash-seed", "default");
  lb = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_FALSE(lb->seed().has_value());
}

TEST_F(
    CmdConfigLoadBalancingTestFixture,
    applyUpdatesOnlyRequestedLoadBalancer) {
  cfg::LoadBalancer ecmp;
  ecmp.id() = cfg::LoadBalancerID::ECMP;
  ecmp.algorithm() = cfg::HashingAlgorithm::CRC;
  ecmp.seed() = 11;

  cfg::LoadBalancer lag;
  lag.id() = cfg::LoadBalancerID::AGGREGATE_PORT;
  lag.algorithm() = cfg::HashingAlgorithm::CRC16_CCITT;
  lag.seed() = 22;

  cfg::SwitchConfig sw;
  sw.loadBalancers() = {ecmp, lag};
  const auto originalLag =
      *findLoadBalancer(sw, cfg::LoadBalancerID::AGGREGATE_PORT);

  applyLoadBalancerConfig(sw, cfg::LoadBalancerID::ECMP, "hash-seed", "99");

  ASSERT_EQ(sw.loadBalancers()->size(), 2);
  const auto* updatedEcmp = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(updatedEcmp, nullptr);
  ASSERT_TRUE(updatedEcmp->seed().has_value());
  EXPECT_EQ(*updatedEcmp->seed(), 99);
  const auto* unchangedLag =
      findLoadBalancer(sw, cfg::LoadBalancerID::AGGREGATE_PORT);
  ASSERT_NE(unchangedLag, nullptr);
  EXPECT_EQ(*unchangedLag, originalLag);
}

TEST_F(CmdConfigLoadBalancingTestFixture, applyHashSeed_invalid) {
  cfg::SwitchConfig sw;
  EXPECT_THROW(
      applyLoadBalancerConfig(
          sw, cfg::LoadBalancerID::ECMP, "hash-seed", "abc"),
      std::invalid_argument);
}

TEST_F(CmdConfigLoadBalancingTestFixture, applyHashFieldsIpv4) {
  cfg::SwitchConfig sw;
  applyLoadBalancerConfig(
      sw, cfg::LoadBalancerID::ECMP, "hash-fields-ipv4", "src-ip,dst-ip");
  const auto* lb = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(lb->fieldSelection()->ipv4Fields()->size(), 2);

  applyLoadBalancerConfig(
      sw, cfg::LoadBalancerID::ECMP, "hash-fields-ipv4", "src-ip");
  lb = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(lb->fieldSelection()->ipv4Fields()->size(), 1);
  EXPECT_EQ(
      *lb->fieldSelection()->ipv4Fields()->begin(),
      cfg::IPv4Field::SOURCE_ADDRESS);

  applyLoadBalancerConfig(
      sw, cfg::LoadBalancerID::ECMP, "hash-fields-ipv4", "none");
  lb = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_TRUE(lb->fieldSelection()->ipv4Fields()->empty());
}

TEST_F(CmdConfigLoadBalancingTestFixture, applyHashFieldsIpv4_invalid) {
  cfg::SwitchConfig sw;
  EXPECT_THROW(
      applyLoadBalancerConfig(
          sw, cfg::LoadBalancerID::ECMP, "hash-fields-ipv4", "flow-label"),
      std::invalid_argument);
  EXPECT_THROW(
      applyLoadBalancerConfig(
          sw, cfg::LoadBalancerID::ECMP, "hash-fields-ipv4", "src-ip,"),
      std::invalid_argument);
}

TEST_F(CmdConfigLoadBalancingTestFixture, applyHashFieldsIpv6) {
  cfg::SwitchConfig sw;
  applyLoadBalancerConfig(
      sw,
      cfg::LoadBalancerID::ECMP,
      "hash-fields-ipv6",
      "src-ip,dst-ip,flow-label");
  const auto* lb = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(lb->fieldSelection()->ipv6Fields()->size(), 3);
}

TEST_F(CmdConfigLoadBalancingTestFixture, applyHashFieldsTransport) {
  cfg::SwitchConfig sw;
  applyLoadBalancerConfig(
      sw,
      cfg::LoadBalancerID::ECMP,
      "hash-fields-transport",
      "src-port,dst-port");
  const auto* lb = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(lb->fieldSelection()->transportFields()->size(), 2);
}

TEST_F(CmdConfigLoadBalancingTestFixture, applyHashFieldsMpls) {
  cfg::SwitchConfig sw;
  applyLoadBalancerConfig(
      sw, cfg::LoadBalancerID::ECMP, "hash-fields-mpls", "top,second,third");
  const auto* lb = findLoadBalancer(sw, cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(lb->fieldSelection()->mplsFields()->size(), 3);
}

TEST_F(CmdConfigLoadBalancingTestFixture, applyCreatesNewLoadBalancer) {
  cfg::SwitchConfig sw;
  EXPECT_EQ(findLoadBalancer(sw, cfg::LoadBalancerID::AGGREGATE_PORT), nullptr);
  applyLoadBalancerConfig(
      sw, cfg::LoadBalancerID::AGGREGATE_PORT, "hash-seed", "7");
  EXPECT_NE(findLoadBalancer(sw, cfg::LoadBalancerID::AGGREGATE_PORT), nullptr);
  // ECMP entry was not created as a side effect.
  EXPECT_EQ(findLoadBalancer(sw, cfg::LoadBalancerID::ECMP), nullptr);
}

// =============================================================
// queryClient() end-to-end tests through ConfigSession
// =============================================================

TEST_F(CmdConfigLoadBalancingTestFixture, ecmp_queryClient_setAlgorithm) {
  setupTestableConfigSession(ecmpCmdPrefix_, "hash-algorithm crc32-hi");
  CmdConfigLoadBalancingEcmp cmd;
  HostInfo hostInfo("testhost");
  LoadBalancingConfigArgs args({"hash-algorithm", "crc32-hi"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("ecmp"));
  EXPECT_THAT(result, HasSubstr("hash-algorithm"));

  const auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  const auto* lb = findLoadBalancer(*config.sw(), cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(*lb->algorithm(), cfg::HashingAlgorithm::CRC32_HI);
}

TEST_F(CmdConfigLoadBalancingTestFixture, ecmp_queryClient_setSeed) {
  setupTestableConfigSession(ecmpCmdPrefix_, "hash-seed 555");
  CmdConfigLoadBalancingEcmp cmd;
  HostInfo hostInfo("testhost");
  LoadBalancingConfigArgs args({"hash-seed", "555"});

  cmd.queryClient(hostInfo, args);

  const auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  const auto* lb = findLoadBalancer(*config.sw(), cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  ASSERT_TRUE(lb->seed().has_value());
  EXPECT_EQ(*lb->seed(), 555);
}

TEST_F(CmdConfigLoadBalancingTestFixture, ecmp_queryClient_setFieldsIpv4) {
  setupTestableConfigSession(ecmpCmdPrefix_, "hash-fields-ipv4 src-ip");
  CmdConfigLoadBalancingEcmp cmd;
  HostInfo hostInfo("testhost");
  LoadBalancingConfigArgs args({"hash-fields-ipv4", "src-ip"});

  cmd.queryClient(hostInfo, args);

  const auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  const auto* lb = findLoadBalancer(*config.sw(), cfg::LoadBalancerID::ECMP);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(lb->fieldSelection()->ipv4Fields()->size(), 1);
  EXPECT_EQ(
      *lb->fieldSelection()->ipv4Fields()->begin(),
      cfg::IPv4Field::SOURCE_ADDRESS);
}

TEST_F(CmdConfigLoadBalancingTestFixture, lag_queryClient_setAlgorithm) {
  setupTestableConfigSession(lagCmdPrefix_, "hash-algorithm crc32-lo");
  CmdConfigLoadBalancingLag cmd;
  HostInfo hostInfo("testhost");
  LoadBalancingConfigArgs args({"hash-algorithm", "crc32-lo"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("lag"));
  EXPECT_THAT(result, HasSubstr("hash-algorithm"));

  const auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  const auto* lb =
      findLoadBalancer(*config.sw(), cfg::LoadBalancerID::AGGREGATE_PORT);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(*lb->algorithm(), cfg::HashingAlgorithm::CRC32_LO);
}

TEST_F(CmdConfigLoadBalancingTestFixture, lag_queryClient_setFieldsIpv6) {
  setupTestableConfigSession(
      lagCmdPrefix_, "hash-fields-ipv6 src-ip,dst-ip,flow-label");
  CmdConfigLoadBalancingLag cmd;
  HostInfo hostInfo("testhost");
  LoadBalancingConfigArgs args(
      {"hash-fields-ipv6", "src-ip,dst-ip,flow-label"});

  cmd.queryClient(hostInfo, args);

  const auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  const auto* lb =
      findLoadBalancer(*config.sw(), cfg::LoadBalancerID::AGGREGATE_PORT);
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(lb->fieldSelection()->ipv6Fields()->size(), 3);
}

} // namespace facebook::fboss
