// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/json/json.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/BgpConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigDomainRegistry.h"
#include "fboss/cli/fboss2/test/config/MockServiceControllers.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "folly/IPAddress.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

namespace {
const std::string kWrapped =
    R"({"sw": {"ports": [{"logicalID": 1, "name": "eth1/1/1"}]}})";
const std::string kBare =
    R"({"ports": [{"logicalID": 1, "name": "eth1/1/1"}]})";
const std::string kBareOtherName =
    R"({"ports": [{"logicalID": 1, "name": "eth1/2/1"}]})";
} // namespace

TEST(ConfigDomainTest, RegistryListsAgentAndBgp) {
  const auto domains = makeDefaultConfigDomains("/home/u/.fboss2", "/etc/coop");
  ASSERT_EQ(domains.all().size(), 2);
  EXPECT_EQ(domains.all()[0]->info().service, cli::ServiceType::AGENT);
  EXPECT_EQ(domains.all()[1]->info().service, cli::ServiceType::BGP);

  const auto& agent = domains.get(cli::ServiceType::AGENT).info();
  EXPECT_EQ(agent.sessionPath, "/home/u/.fboss2/agent.conf");
  EXPECT_EQ(agent.gitRelPath, "cli/agent.conf");
  EXPECT_EQ(agent.desiredPath, "/etc/coop/cli/agent.conf");
  EXPECT_EQ(agent.minActionLevel, cli::ConfigActionLevel::HITLESS);

  const auto& bgp = domains.get(cli::ServiceType::BGP).info();
  EXPECT_EQ(bgp.sessionPath, "/home/u/.fboss2/bgp_config.json");
  EXPECT_EQ(bgp.gitRelPath, "bgpcpp/bgpcpp.conf");
  EXPECT_EQ(bgp.desiredPath, "/etc/coop/bgpcpp/bgpcpp.conf");
  EXPECT_EQ(bgp.minActionLevel, cli::ConfigActionLevel::SERVICE_RESTART);

  EXPECT_EQ(&domains.get<AgentConfigDomain>(), domains.all()[0].get());
  EXPECT_EQ(&domains.get<BgpConfigDomain>(), domains.all()[1].get());
}

TEST(ConfigDomainTest, RegistryThrowsForUnknownService) {
  const ConfigDomainRegistry empty;
  EXPECT_THROW(empty.get(cli::ServiceType::AGENT), std::runtime_error);
  EXPECT_THROW(empty.get<AgentConfigDomain>(), std::runtime_error);
}

TEST(ConfigDomainTest, RegistryRejectsSecondDomainForAService) {
  ConfigDomainRegistry domains;
  domains.add(std::make_unique<AgentConfigDomain>("/a", "/b"));
  EXPECT_THROW(
      domains.add(std::make_unique<AgentConfigDomain>("/c", "/d")),
      std::runtime_error);
  EXPECT_EQ(domains.all().size(), 1);
  domains.add(std::make_unique<BgpConfigDomain>("/a", "/b"));
  EXPECT_EQ(domains.all().size(), 2);
}

TEST(ConfigDomainTest, AgentAndBgpPolicies) {
  const AgentConfigDomain agent("", "");
  EXPECT_TRUE(agent.requiresConfig());
  const BgpConfigDomain bgp("", "");
  EXPECT_FALSE(bgp.requiresConfig());
}

// A bare SwitchConfig is the same config as one wrapped in "sw".
TEST(ConfigDomainTest, AgentEqualAcceptsBareSwitchConfig) {
  const AgentConfigDomain agent("", "");
  EXPECT_TRUE(agent.equal(kWrapped, kBare));
  EXPECT_EQ(agent.normalize(kWrapped), agent.normalize(kBare));
}

// Two different bare SwitchConfig texts must not compare equal.
TEST(ConfigDomainTest, AgentEqualTellsBareSwitchConfigsApart) {
  const AgentConfigDomain agent("", "");
  EXPECT_FALSE(agent.equal(kBare, kBareOtherName));
}

TEST(ConfigDomainTest, AgentEqualIgnoresFormatting) {
  const AgentConfigDomain agent("", "");
  EXPECT_TRUE(agent.equal(kWrapped, agent.normalize(kWrapped)));
}

TEST(ConfigDomainTest, UnparseableTextThrows) {
  const AgentConfigDomain agent("", "");
  EXPECT_THROW(agent.normalize("not json"), std::exception);
  EXPECT_THROW(agent.equal("not json", kWrapped), std::exception);
  const BgpConfigDomain bgp("", "");
  EXPECT_THROW(bgp.normalize("not json"), std::exception);
}

TEST(ConfigDomainTest, AgentLoadSerializeUnload) {
  AgentConfigDomain agent("", "");
  EXPECT_FALSE(agent.loaded());
  EXPECT_THROW(agent.config(), std::runtime_error);

  agent.load(kBare);
  ASSERT_TRUE(agent.loaded());
  ASSERT_EQ(agent.config().sw()->ports()->size(), 1);
  EXPECT_EQ(*(*agent.config().sw()->ports())[0].name(), "eth1/1/1");
  EXPECT_TRUE(agent.equal(agent.serialize(), kWrapped));

  agent.unload();
  EXPECT_FALSE(agent.loaded());
}

// serialize() writes pretty-printed JSON, the same text normalize() produces.
TEST(ConfigDomainTest, SerializeWritesNormalizedPrettyJson) {
  AgentConfigDomain agent("", "");
  agent.load(kWrapped);
  const std::string agentText = agent.serialize();
  EXPECT_EQ(agentText, agent.normalize(kWrapped));
  EXPECT_EQ(agentText, folly::toPrettyJson(folly::parseJson(agentText)));

  const std::string bgpJson = R"({"router_id": "1.2.3.4"})";
  BgpConfigDomain bgp("", "");
  bgp.load(bgpJson);
  const std::string bgpText = bgp.serialize();
  EXPECT_EQ(bgpText, bgp.normalize(bgpJson));
  EXPECT_EQ(bgpText, folly::toPrettyJson(folly::parseJson(bgpText)));
}

TEST(ConfigDomainTest, BgpEqualComparesByValue) {
  const BgpConfigDomain bgp("", "");
  const std::string json = R"({"router_id": "1.2.3.4"})";
  EXPECT_TRUE(bgp.equal(json, bgp.normalize(json)));
  EXPECT_FALSE(bgp.equal(json, R"({"router_id": "1.2.3.5"})"));
  EXPECT_THROW(bgp.equal("not json", json), std::exception);
}

// BGP starts from the schema defaults when there is nothing usable to load.
TEST(ConfigDomainTest, BgpLoadFallsBackToDefaults) {
  BgpConfigDomain bgp("", "");
  bgp.load("");
  ASSERT_TRUE(bgp.loaded());
  EXPECT_TRUE(bgp.config() == bgp::thrift::BgpConfig());

  bgp.load("not json");
  ASSERT_TRUE(bgp.loaded());
  EXPECT_TRUE(bgp.config() == bgp::thrift::BgpConfig());

  bgp.load(R"({"router_id": "1.2.3.4"})");
  EXPECT_EQ(*bgp.config().router_id(), "1.2.3.4");
}

namespace {
class AgentDomainWithController : public AgentConfigDomain {
 public:
  explicit AgentDomainWithController(
      std::unique_ptr<AgentServiceController> controller)
      : AgentConfigDomain("", "") {
    controller_ = std::move(controller);
  }
};

class BgpDomainWithController : public BgpConfigDomain {
 public:
  explicit BgpDomainWithController(
      std::unique_ptr<BgpServiceController> controller)
      : BgpConfigDomain("", "") {
    controller_ = std::move(controller);
  }
};

HostInfo localhost() {
  return {"localhost", "localhost-oob", folly::IPAddress("127.0.0.1")};
}
} // namespace

// An agent restart is not done until the agent reports itself configured.
TEST(ConfigDomainTest, AgentApplyRestartWaitsForAgent) {
  auto controller =
      std::make_unique<::testing::StrictMock<MockAgentServiceController>>();
  ::testing::InSequence seq;
  EXPECT_CALL(*controller, restart(cli::ConfigActionLevel::SERVICE_RESTART))
      .WillOnce(::testing::Return(std::vector<std::string>{"wedge_agent"}));
  EXPECT_CALL(*controller, isAgentConfigured(::testing::_))
      .WillOnce(::testing::Return(true));
  AgentDomainWithController agent(std::move(controller));

  const auto units =
      agent.apply(cli::ConfigActionLevel::SERVICE_RESTART, localhost());
  EXPECT_EQ(units, std::vector<std::string>{"wedge_agent"});
}

TEST(ConfigDomainTest, AgentApplyHitlessReloads) {
  auto controller =
      std::make_unique<::testing::StrictMock<MockAgentServiceController>>();
  EXPECT_CALL(*controller, reloadConfig(::testing::_))
      .WillOnce(::testing::Return(std::vector<std::string>{"fboss_sw_agent"}));
  AgentDomainWithController agent(std::move(controller));

  const auto units = agent.apply(cli::ConfigActionLevel::HITLESS, localhost());
  EXPECT_EQ(units, std::vector<std::string>{"fboss_sw_agent"});
}

TEST(ConfigDomainTest, BgpApplyRestarts) {
  auto controller =
      std::make_unique<::testing::StrictMock<MockBgpServiceController>>();
  EXPECT_CALL(*controller, restart())
      .WillOnce(::testing::Return(std::vector<std::string>{"bgpd"}));
  BgpDomainWithController bgp(std::move(controller));

  const auto units =
      bgp.apply(cli::ConfigActionLevel::SERVICE_RESTART, localhost());
  EXPECT_EQ(units, std::vector<std::string>{"bgpd"});
}

// bgpd has no reload, so a HITLESS action never reaches the controller.
TEST(ConfigDomainTest, BgpApplyHitlessThrows) {
  BgpDomainWithController bgp(
      std::make_unique<::testing::StrictMock<MockBgpServiceController>>());
  EXPECT_THROW(
      bgp.apply(cli::ConfigActionLevel::HITLESS, localhost()),
      std::runtime_error);
}

// A HITLESS request for a restart-only service is refused in prepare(), which
// apply() runs before it restarts anything.
TEST(ConfigDomainTest, RestartOnlyPrepareRejectsHitless) {
  BgpConfigDomain bgp("", "");
  EXPECT_THROW(
      bgp.prepare(cli::ConfigActionLevel::HITLESS, localhost()),
      std::runtime_error);
  EXPECT_NO_THROW(
      bgp.prepare(cli::ConfigActionLevel::SERVICE_RESTART, localhost()));
}

TEST(ConfigDomainTest, ActionNames) {
  const AgentConfigDomain agent("", "");
  const BgpConfigDomain bgp("", "");
  EXPECT_EQ(
      agent.actionName(cli::ConfigActionLevel::SERVICE_RESTART), "warmboot");
  EXPECT_EQ(
      agent.actionName(cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART),
      "coldboot");
  EXPECT_EQ(bgp.actionName(cli::ConfigActionLevel::SERVICE_RESTART), "restart");
  EXPECT_EQ(
      bgp.actionName(cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART),
      "restart");
  // HITLESS keeps its name for both, although bgpd rejects the level.
  EXPECT_EQ(agent.actionName(cli::ConfigActionLevel::HITLESS), "reload");
  EXPECT_EQ(bgp.actionName(cli::ConfigActionLevel::HITLESS), "reload");
}

} // namespace facebook::fboss
