/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"

#include <folly/ScopeGuard.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

DECLARE_bool(multi_switch);

using namespace facebook::fboss;

namespace {

cfg::SwitchConfig createSwitchConfig() {
  cfg::SwitchConfig config;
  config.vlans()->resize(2);
  *config.vlans()[0].name() = "PrimaryVlan";
  *config.vlans()[0].id() = 5;
  *config.vlans()[0].routable() = true;
  config.vlans()[0].intfID() = 1234;
  *config.vlans()[1].name() = "DefaultHWVlan";
  *config.vlans()[1].id() = 1;
  *config.vlans()[1].routable() = true;
  config.vlans()[1].intfID() = 4321;

  config.vlanPorts()->resize(10);
  config.ports()->resize(10);
  for (int n = 0; n < 10; ++n) {
    *config.ports()[n].logicalID() = n + 1;
    *config.ports()[n].state() = cfg::PortState::ENABLED;
    *config.ports()[n].minFrameSize() = 64;
    *config.ports()[n].maxFrameSize() = 9000;
    *config.ports()[n].routable() = true;
    *config.ports()[n].ingressVlan() = 5;

    *config.vlanPorts()[n].vlanID() = 5;
    *config.vlanPorts()[n].logicalPort() = n + 1;
    *config.vlanPorts()[n].spanningTreeState() =
        cfg::SpanningTreeState::FORWARDING;
    *config.vlanPorts()[n].emitTags() = 0;
  }

  config.interfaces()->resize(2);
  *config.interfaces()[0].intfID() = 1234;
  *config.interfaces()[0].vlanID() = 5;
  config.interfaces()[0].name() = "PrimaryInterface";
  config.interfaces()[0].mtu() = 9000;
  config.interfaces()[0].ipAddresses()->resize(5);
  config.interfaces()[0].ipAddresses()[0] = "10.164.4.10/24";
  config.interfaces()[0].ipAddresses()[1] = "10.164.4.1/24";
  config.interfaces()[0].ipAddresses()[2] = "10.164.4.2/24";
  config.interfaces()[0].ipAddresses()[3] = "2401:db00:2110:3004::/64";
  config.interfaces()[0].ipAddresses()[4] = "2401:db00:2110:3004::000a/64";
  config.interfaces()[0].ndp() = cfg::NdpConfig();
  *config.interfaces()[0].ndp()->routerAdvertisementSeconds() = 1;
  *config.interfaces()[1].intfID() = 4321;
  *config.interfaces()[1].vlanID() = 1;
  config.interfaces()[1].name() = "DefaultHWInterface";
  config.interfaces()[1].mtu() = 9000;
  config.interfaces()[1].ipAddresses()->resize(0);
  config.interfaces()[1].ndp() = cfg::NdpConfig();
  *config.interfaces()[1].ndp()->routerAdvertisementSeconds() = 1;
  *config.arpTimeoutSeconds() = 1;
  return config;
}

cfg::AgentConfig createAgentConfig() {
  cfg::AgentConfig config;
  *config.sw() = createSwitchConfig();
  return config;
}

} // namespace

TEST(AgentConfigTest, AcceptsNewAndOldStyleConfigs) {
  auto oldStyle = createSwitchConfig();
  auto newStyle = createAgentConfig();

  auto oldStyleRaw =
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(oldStyle);
  auto newStyleRaw =
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(newStyle);

  auto fromOldStyle = AgentConfig::fromRawConfig(oldStyleRaw);
  auto fromNewStyle = AgentConfig::fromRawConfig(oldStyleRaw);

  EXPECT_EQ(fromOldStyle->swConfigRaw(), fromNewStyle->swConfigRaw());
}

TEST(AgentConfigTest, RunModeDoesNotRequireAgentConfig) {
  const auto savedMultiSwitch = FLAGS_multi_switch;
  SCOPE_EXIT {
    FLAGS_multi_switch = savedMultiSwitch;
  };

  FLAGS_multi_switch = false;
  EXPECT_EQ(AgentConfig::getRunMode(), cfg::AgentRunMode::MONO);

  FLAGS_multi_switch = true;
  EXPECT_EQ(AgentConfig::getRunMode(), cfg::AgentRunMode::MULTI_SWITCH);
}
