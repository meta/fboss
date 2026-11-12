// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include <thrift/lib/cpp2/op/Get.h>

// @lint-ignore CLANGTIDY
#include "fboss/agent/gen-cpp2-thriftpath/agent_config.h" // @manual=//fboss/agent:agent_config-cpp2-thriftpath
#include "fboss/fsdb/tests/gen-cpp2-thriftpath/thriftpath_test.h" // @manual=//fboss/fsdb/tests:thriftpath_test_thrift-cpp2-thriftpath

TEST(PathTests, toStrSimple) {
  using namespace facebook::fboss::fsdb;

  thriftpath::RootThriftPath<TestStruct> root;

  EXPECT_EQ(root.tx().str(), "/tx");
  EXPECT_EQ(root.structMap()[3].max().str(), "/structMap/3/max");
  EXPECT_EQ(root.member().min().str(), "/member/min");
  EXPECT_EQ(root.str(), "/");
}

TEST(PathTests, toStrAgentConfig) {
  using namespace facebook::fboss::fsdb;

  thriftpath::RootThriftPath<facebook::fboss::cfg::AgentConfig> root;

  EXPECT_EQ(root.defaultCommandLineArgs().str(), "/defaultCommandLineArgs");
  EXPECT_EQ(
      root.defaultCommandLineArgs()["bla"].str(),
      "/defaultCommandLineArgs/bla");
  EXPECT_EQ(
      root.sw().portQueueConfigs()["testQueue"].str(),
      "/sw/portQueueConfigs/testQueue");
  EXPECT_EQ(
      root.platform().chip().bcm().config()["plp_freq"].str(),
      "/platform/chip/bcm/config/plp_freq");
  EXPECT_EQ(
      root.platform().chip().asic().config().str(),
      "/platform/chip/asic/config");
  EXPECT_EQ(root.str(), "/");
}

TEST(PathTests, IntegralGetOperator) {
  using namespace facebook::fboss::fsdb;
  using AgentConfig = facebook::fboss::cfg::AgentConfig;

  using RootPath = thriftpath::RootThriftPath<AgentConfig>;
  RootPath root;

  // Test field-id-based operator using modern thrift APIs
  using FieldId = apache::thrift::op::
      get_field_id<AgentConfig, apache::thrift::ident::defaultCommandLineArgs>;
  EXPECT_EQ(root(FieldId()).str(), "/defaultCommandLineArgs");
}

TEST(PathTests, AgentConfigTokens) {
  using namespace facebook::fboss::fsdb;

  using RootPath =
      thriftpath::RootThriftPath<facebook::fboss::cfg::AgentConfig>;
  RootPath root;

  using PathVec = std::vector<std::string>;
  auto cmdlineArgs = root.defaultCommandLineArgs();
  EXPECT_EQ(cmdlineArgs.tokens(), PathVec({"defaultCommandLineArgs"}));
  EXPECT_EQ(cmdlineArgs.idTokens(), PathVec({"1"}));
  EXPECT_EQ(
      cmdlineArgs["bla"].tokens(), PathVec({"defaultCommandLineArgs", "bla"}));
  EXPECT_EQ(cmdlineArgs["bla"].idTokens(), PathVec({"1", "bla"}));
  auto portQueueConfigs = root.sw().portQueueConfigs();
  EXPECT_EQ(portQueueConfigs.tokens(), PathVec({"sw", "portQueueConfigs"}));
  EXPECT_EQ(portQueueConfigs.idTokens(), PathVec({"2", "40"}));

  // Test rvalue ref child getters
  EXPECT_EQ(
      RootPath().defaultCommandLineArgs()["foo"].tokens(),
      PathVec({"defaultCommandLineArgs", "foo"}));
  EXPECT_EQ(
      RootPath().defaultCommandLineArgs()["foo"].idTokens(),
      PathVec({"1", "foo"}));
}

TEST(PathTests, WildcardPaths) {
  using namespace facebook::fboss::fsdb;

  using RootPath =
      thriftpath::RootThriftPath<facebook::fboss::cfg::AgentConfig>;
  RootPath root;
  OperPathElem wildcard;
  wildcard.set_any(true);
  auto path = root.sw().ports()[wildcard].logicalID();
  auto extTokens = path.extendedTokens();
  EXPECT_EQ(extTokens.size(), 4);
  EXPECT_EQ(extTokens[0].raw().as_const().value(), "2"); // sw id
  EXPECT_EQ(extTokens[1].raw().as_const().value(), "2"); // ports id
  EXPECT_TRUE(extTokens[2].any().as_const().value());
  EXPECT_EQ(extTokens[3].raw().as_const().value(), "1"); // logicalID id
  EXPECT_THROW(path.tokens(), std::runtime_error);
  EXPECT_THROW(path.idTokens(), std::runtime_error);
}

TEST(PathTests, ExtendedPathMatching) {
  using namespace facebook::fboss::fsdb;

  using RootPath =
      thriftpath::RootThriftPath<facebook::fboss::cfg::AgentConfig>;
  RootPath root;
  OperPathElem wildcard;
  wildcard.set_any(true);
  auto path = root.sw().ports()[wildcard].logicalID();

  auto idPath = root.sw().ports()[123].logicalID().idTokens();
  auto namePath = root.sw().ports()[123].logicalID().tokens();

  EXPECT_TRUE(path.matchesPath(namePath));
  EXPECT_TRUE(path.matchesPath(idPath));

  EXPECT_FALSE(path.matchesPath({"sw", "ports"}));
}
