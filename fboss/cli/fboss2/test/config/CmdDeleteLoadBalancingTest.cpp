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
#include <cstddef>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/delete/load_balancing/CmdDeleteLoadBalancing.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed mirrors the CmdConfigLoadBalancingTest fixture: both load-balancers
// configured, ECMP (id 1) fully populated, LAG (id 2) minimal.
static const std::string kSeedConfig = R"({
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
})";

class CmdDeleteLoadBalancingTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteLoadBalancingTestFixture()
      : CmdConfigTestBase(
            "fboss_del_lb_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}

 protected:
  static const cfg::LoadBalancer* findLoadBalancer(cfg::LoadBalancerID id) {
    const auto& loadBalancers =
        *ConfigSession::getInstance().getAgentConfig().sw()->loadBalancers();
    for (const auto& lb : loadBalancers) {
      if (*lb.id() == id) {
        return &lb;
      }
    }
    return nullptr;
  }

  static size_t loadBalancerCount() {
    return ConfigSession::getInstance()
        .getAgentConfig()
        .sw()
        ->loadBalancers()
        ->size();
  }
};

TEST_F(CmdDeleteLoadBalancingTestFixture, deleteEcmp) {
  setupTestableConfigSession("delete load-balancing ecmp", "");

  auto result = CmdDeleteLoadBalancingEcmp().queryClient(localhost());
  EXPECT_THAT(result, HasSubstr("Deleted ecmp load-balancer"));

  EXPECT_EQ(findLoadBalancer(cfg::LoadBalancerID::ECMP), nullptr);
  // The LAG entry is untouched.
  EXPECT_NE(findLoadBalancer(cfg::LoadBalancerID::AGGREGATE_PORT), nullptr);
  EXPECT_EQ(loadBalancerCount(), 1);
}

TEST_F(CmdDeleteLoadBalancingTestFixture, deleteLag) {
  setupTestableConfigSession("delete load-balancing lag", "");

  auto result = CmdDeleteLoadBalancingLag().queryClient(localhost());
  EXPECT_THAT(result, HasSubstr("Deleted lag load-balancer"));

  EXPECT_EQ(findLoadBalancer(cfg::LoadBalancerID::AGGREGATE_PORT), nullptr);
  // The ECMP entry is untouched.
  EXPECT_NE(findLoadBalancer(cfg::LoadBalancerID::ECMP), nullptr);
  EXPECT_EQ(loadBalancerCount(), 1);
}

TEST_F(CmdDeleteLoadBalancingTestFixture, deleteBothEmptiesList) {
  setupTestableConfigSession("delete load-balancing ecmp", "");

  CmdDeleteLoadBalancingEcmp().queryClient(localhost());
  CmdDeleteLoadBalancingLag().queryClient(localhost());

  EXPECT_EQ(loadBalancerCount(), 0);
}

TEST_F(CmdDeleteLoadBalancingTestFixture, deleteAbsentIsNoop) {
  setupTestableConfigSession("delete load-balancing ecmp", "");

  CmdDeleteLoadBalancingEcmp().queryClient(localhost());
  // Second delete: the ECMP entry is gone, so this is a warning, not an error.
  const auto sessionBefore = sessionConfigText();
  EXPECT_THAT(
      CmdDeleteLoadBalancingEcmp().queryClient(localhost()),
      HasSubstr("nothing to delete"));
  EXPECT_EQ(loadBalancerCount(), 1);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

TEST_F(CmdDeleteLoadBalancingTestFixture, parentCommandIncomplete) {
  setupTestableConfigSession("delete load-balancing", "");

  EXPECT_THROW(
      CmdDeleteLoadBalancing().queryClient(localhost()), std::runtime_error);
}

TEST_F(CmdDeleteLoadBalancingTestFixture, removeLoadBalancerHelper) {
  cfg::SwitchConfig swConfig;
  cfg::LoadBalancer ecmp;
  ecmp.id() = cfg::LoadBalancerID::ECMP;
  swConfig.loadBalancers()->push_back(ecmp);

  EXPECT_THAT(
      removeLoadBalancer(swConfig, cfg::LoadBalancerID::ECMP),
      HasSubstr("ecmp"));
  EXPECT_TRUE(swConfig.loadBalancers()->empty());
  EXPECT_THAT(
      removeLoadBalancer(swConfig, cfg::LoadBalancerID::AGGREGATE_PORT),
      HasSubstr("nothing to delete"));
  EXPECT_TRUE(swConfig.loadBalancers()->empty());
}

} // namespace facebook::fboss
