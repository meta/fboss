// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/json/json.h>
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

TEST(ConfigDomainTest, RegistryListsAgentThenBgp) {
  const auto domains = makeDefaultConfigDomains("/home/u/.fboss2", "/etc/coop");
  ASSERT_EQ(domains.all().size(), 2);
  EXPECT_EQ(domains.all()[0]->info().service, cli::ServiceType::AGENT);
  EXPECT_EQ(domains.all()[1]->info().service, cli::ServiceType::BGP);

  const auto& agent = domains.get(cli::ServiceType::AGENT).info();
  EXPECT_EQ(agent.sessionPath, "/home/u/.fboss2/agent.conf");
  EXPECT_EQ(agent.gitRelPath, "cli/agent.conf");
  EXPECT_EQ(agent.desiredPath, "/etc/coop/cli/agent.conf");
  EXPECT_EQ(agent.rollbackActionLevel, cli::ConfigActionLevel::HITLESS);

  const auto& bgp = domains.get(cli::ServiceType::BGP).info();
  EXPECT_EQ(bgp.sessionPath, "/home/u/.fboss2/bgp_config.json");
  EXPECT_EQ(bgp.gitRelPath, "bgpcpp/bgpcpp.conf");
  EXPECT_EQ(bgp.desiredPath, "/etc/coop/bgpcpp/bgpcpp.conf");
  EXPECT_EQ(bgp.rollbackActionLevel, cli::ConfigActionLevel::SERVICE_RESTART);

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

} // namespace facebook::fboss
