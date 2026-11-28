// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <boost/filesystem/operations.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/protocol/static/route/add/CmdConfigProtocolStaticRouteAdd.h"
#include "fboss/cli/fboss2/commands/delete/protocol/static/route/CmdDeleteProtocolStaticRoute.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/session/Git.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "fboss/cli/fboss2/test/TestableConfigSession.h"
#include "fboss/cli/fboss2/utils/PortMap.h" // NOLINT(misc-include-cleaner)

namespace fs = std::filesystem;

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigIpRouteTestFixture : public CmdHandlerTestBase {
 public:
  void SetUp() override {
    CmdHandlerTestBase::SetUp();

    // Create unique test directories
    auto tempBase = fs::temp_directory_path();
    auto uniquePath = boost::filesystem::unique_path(
        "fboss_ip_route_test_%%%%-%%%%-%%%%-%%%%");
    testHomeDir_ = tempBase / (uniquePath.string() + "_home");
    testEtcDir_ = tempBase / (uniquePath.string() + "_etc");

    std::error_code ec;
    if (fs::exists(testHomeDir_)) {
      fs::remove_all(testHomeDir_, ec);
    }
    if (fs::exists(testEtcDir_)) {
      fs::remove_all(testEtcDir_, ec);
    }

    // Create test directories
    fs::create_directories(testHomeDir_);
    systemConfigDir_ = testEtcDir_ / "coop";
    fs::create_directories(systemConfigDir_ / "cli");

    // NOLINTNEXTLINE(concurrency-mt-unsafe,misc-include-cleaner)
    setenv("HOME", testHomeDir_.c_str(), 1);
    // NOLINTNEXTLINE(concurrency-mt-unsafe,misc-include-cleaner)
    setenv("USER", "testuser", 1);

    // Create a test system config file at cli/agent.conf
    fs::path cliConfigPath = systemConfigDir_ / "cli" / "agent.conf";
    createTestConfig(cliConfigPath, R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000
      }
    ],
    "staticRoutesWithNhops": [],
    "staticRoutesToNull": [],
    "staticRoutesToCPU": []
  }
})");

    // Create symlink at /etc/coop/agent.conf -> cli/agent.conf
    fs::create_symlink("cli/agent.conf", systemConfigDir_ / "agent.conf");

    // Initialize Git repository and create initial commit
    Git git(systemConfigDir_.string());
    git.init();
    git.commit({cliConfigPath.string()}, "Initial commit");

    // Initialize the ConfigSession singleton for all tests
    fs::path sessionDir = testHomeDir_ / ".fboss2";
    TestableConfigSession::setInstance(
        std::make_unique<TestableConfigSession>(
            sessionDir.string(), systemConfigDir_.string()));
  }

  void TearDown() override {
    // Reset the singleton to ensure tests don't interfere with each other
    TestableConfigSession::setInstance(nullptr);
    std::error_code ec;
    if (fs::exists(testHomeDir_)) {
      fs::remove_all(testHomeDir_, ec);
    }
    if (fs::exists(testEtcDir_)) {
      fs::remove_all(testEtcDir_, ec);
    }
    CmdHandlerTestBase::TearDown();
  }

 protected:
  void createTestConfig(const fs::path& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
    file.close();
  }

  fs::path testHomeDir_;
  fs::path testEtcDir_;
  fs::path systemConfigDir_;
};

// ============== IPv4 Route Add Tests ==============

// Test adding a route with a single nexthop
TEST_F(CmdConfigIpRouteTestFixture, addIpv4RouteSingleNexthop) {
  auto cmd = CmdConfigProtocolStaticIpRouteAdd();
  StaticRouteAddIpArg routeArg({"10.0.0.0/8", "192.168.1.1"});

  auto result = cmd.queryClient(localhost(), routeArg);

  EXPECT_THAT(result, HasSubstr("Successfully added static route"));
  EXPECT_THAT(result, HasSubstr("10.0.0.0/8"));
  EXPECT_THAT(result, HasSubstr("192.168.1.1"));

  // Verify the route was added
  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nhopRoutes = *swConfig.staticRoutesWithNhops();
  ASSERT_EQ(nhopRoutes.size(), 1);
  EXPECT_EQ(*nhopRoutes[0].prefix(), "10.0.0.0/8");
  EXPECT_EQ(nhopRoutes[0].nexthops()->size(), 1);
  EXPECT_EQ((*nhopRoutes[0].nexthops())[0], "192.168.1.1");
}

// Test adding a route with multiple nexthops (ECMP)
TEST_F(CmdConfigIpRouteTestFixture, addIpv4RouteMultipleNexthops) {
  auto cmd = CmdConfigProtocolStaticIpRouteAdd();
  StaticRouteAddIpArg routeArg({"10.0.0.0/8", "192.168.1.1", "192.168.1.2"});

  auto result = cmd.queryClient(localhost(), routeArg);

  EXPECT_THAT(result, HasSubstr("Successfully added static route"));
  EXPECT_THAT(result, HasSubstr("10.0.0.0/8"));
  EXPECT_THAT(result, HasSubstr("192.168.1.1"));
  EXPECT_THAT(result, HasSubstr("192.168.1.2"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nhopRoutes = *swConfig.staticRoutesWithNhops();
  ASSERT_EQ(nhopRoutes.size(), 1);
  EXPECT_EQ(nhopRoutes[0].nexthops()->size(), 2);
  EXPECT_EQ((*nhopRoutes[0].nexthops())[0], "192.168.1.1");
  EXPECT_EQ((*nhopRoutes[0].nexthops())[1], "192.168.1.2");
}

// Test adding a null0 (drop) route
TEST_F(CmdConfigIpRouteTestFixture, addIpv4DropRoute) {
  auto cmd = CmdConfigProtocolStaticIpRouteAdd();
  StaticRouteAddIpArg routeArg({"10.0.0.0/8", "null0"});

  auto result = cmd.queryClient(localhost(), routeArg);

  EXPECT_THAT(result, HasSubstr("Successfully added static null0 route"));
  EXPECT_THAT(result, HasSubstr("10.0.0.0/8"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nullRoutes = *swConfig.staticRoutesToNull();
  ASSERT_EQ(nullRoutes.size(), 1);
  EXPECT_EQ(*nullRoutes[0].prefix(), "10.0.0.0/8");
}

// Test updating an existing route
TEST_F(CmdConfigIpRouteTestFixture, updateIpv4Route) {
  auto cmd = CmdConfigProtocolStaticIpRouteAdd();

  // Add initial route
  StaticRouteAddIpArg routeArg1({"10.0.0.0/8", "192.168.1.1"});
  cmd.queryClient(localhost(), routeArg1);

  // Update route with new nexthop
  StaticRouteAddIpArg routeArg2({"10.0.0.0/8", "192.168.1.2"});
  auto result = cmd.queryClient(localhost(), routeArg2);

  EXPECT_THAT(result, HasSubstr("Successfully updated static route"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nhopRoutes = *swConfig.staticRoutesWithNhops();
  ASSERT_EQ(nhopRoutes.size(), 1);
  EXPECT_EQ((*nhopRoutes[0].nexthops())[0], "192.168.1.2");
}

// Test invalid prefix format
TEST_F(CmdConfigIpRouteTestFixture, invalidIpv4PrefixFormat) {
  EXPECT_THROW(
      StaticRouteAddIpArg({"invalid-prefix", "192.168.1.1"}),
      std::invalid_argument);
}

// Test IPv6 prefix should fail for IPv4 command
TEST_F(CmdConfigIpRouteTestFixture, ipv6PrefixInIpv4Command) {
  EXPECT_THROW(
      StaticRouteAddIpArg({"2001:db8::/32", "192.168.1.1"}),
      std::invalid_argument);
}

// Test too few arguments
TEST_F(CmdConfigIpRouteTestFixture, tooFewArguments) {
  EXPECT_THROW(StaticRouteAddIpArg({"10.0.0.0/8"}), std::invalid_argument);
}

// Test null0 with extra nexthops should fail
TEST_F(CmdConfigIpRouteTestFixture, dropWithExtraNexthops) {
  EXPECT_THROW(
      StaticRouteAddIpArg({"10.0.0.0/8", "null0", "192.168.1.1"}),
      std::invalid_argument);
}

// Test adding IPv4 default route (0.0.0.0/0)
TEST_F(CmdConfigIpRouteTestFixture, addIpv4DefaultRoute) {
  auto cmd = CmdConfigProtocolStaticIpRouteAdd();
  StaticRouteAddIpArg routeArg({"0.0.0.0/0", "192.168.1.1"});

  auto result = cmd.queryClient(localhost(), routeArg);

  EXPECT_THAT(result, HasSubstr("Successfully added static route"));
  EXPECT_THAT(result, HasSubstr("0.0.0.0/0"));

  auto& nhopRoutes = *ConfigSession::getInstance()
                          .config<AgentConfigDomain>()
                          .sw()
                          ->staticRoutesWithNhops();
  ASSERT_EQ(nhopRoutes.size(), 1);
  EXPECT_EQ(*nhopRoutes[0].prefix(), "0.0.0.0/0");
}

// ============== IPv4 Route Delete Tests ==============

// Test deleting an existing route with nexthops
TEST_F(CmdConfigIpRouteTestFixture, deleteIpv4RouteWithNexthops) {
  // First add a route
  auto addCmd = CmdConfigProtocolStaticIpRouteAdd();
  StaticRouteAddIpArg routeArg({"10.0.0.0/8", "192.168.1.1"});
  addCmd.queryClient(localhost(), routeArg);

  // Delete the route
  auto deleteCmd = CmdDeleteProtocolStaticIpRoute();
  StaticRouteDeleteIpPrefixArg prefixArg({"10.0.0.0/8"});
  auto result = deleteCmd.queryClient(localhost(), prefixArg);

  EXPECT_THAT(result, HasSubstr("Successfully deleted static route"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nhopRoutes = *swConfig.staticRoutesWithNhops();
  EXPECT_EQ(nhopRoutes.size(), 0);
}

// Test deleting a null0 (drop) route
TEST_F(CmdConfigIpRouteTestFixture, deleteIpv4DropRoute) {
  // First add a null0 route
  auto addCmd = CmdConfigProtocolStaticIpRouteAdd();
  StaticRouteAddIpArg routeArg({"10.0.0.0/8", "null0"});
  addCmd.queryClient(localhost(), routeArg);

  // Delete the route
  auto deleteCmd = CmdDeleteProtocolStaticIpRoute();
  StaticRouteDeleteIpPrefixArg prefixArg({"10.0.0.0/8"});
  auto result = deleteCmd.queryClient(localhost(), prefixArg);

  EXPECT_THAT(result, HasSubstr("Successfully deleted static route"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nullRoutes = *swConfig.staticRoutesToNull();
  EXPECT_EQ(nullRoutes.size(), 0);
}

// Test deleting a non-existent route (idempotent)
TEST_F(CmdConfigIpRouteTestFixture, deleteNonExistentIpv4Route) {
  auto deleteCmd = CmdDeleteProtocolStaticIpRoute();
  StaticRouteDeleteIpPrefixArg prefixArg({"10.0.0.0/8"});
  auto result = deleteCmd.queryClient(localhost(), prefixArg);

  EXPECT_THAT(result, HasSubstr("not found"));
}

// Test invalid prefix format for delete
TEST_F(CmdConfigIpRouteTestFixture, deleteInvalidIpv4PrefixFormat) {
  EXPECT_THROW(
      StaticRouteDeleteIpPrefixArg({"invalid-prefix"}), std::invalid_argument);
}

// ============== IPv6 Route Add Tests ==============

// Test adding an IPv6 route with a single nexthop
TEST_F(CmdConfigIpRouteTestFixture, addIpv6RouteSingleNexthop) {
  auto cmd = CmdConfigProtocolStaticIpv6RouteAdd();
  StaticRouteAddIpv6Arg routeArg({"2001:db8::/32", "2001:db8::1"});

  auto result = cmd.queryClient(localhost(), routeArg);

  EXPECT_THAT(result, HasSubstr("Successfully added static route"));
  EXPECT_THAT(result, HasSubstr("2001:db8::/32"));
  EXPECT_THAT(result, HasSubstr("2001:db8::1"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nhopRoutes = *swConfig.staticRoutesWithNhops();
  ASSERT_EQ(nhopRoutes.size(), 1);
  EXPECT_EQ(*nhopRoutes[0].prefix(), "2001:db8::/32");
}

// Test adding an IPv6 null0 (drop) route
TEST_F(CmdConfigIpRouteTestFixture, addIpv6DropRoute) {
  auto cmd = CmdConfigProtocolStaticIpv6RouteAdd();
  StaticRouteAddIpv6Arg routeArg({"2001:db8::/32", "null0"});

  auto result = cmd.queryClient(localhost(), routeArg);

  EXPECT_THAT(result, HasSubstr("Successfully added static null0 route"));
  EXPECT_THAT(result, HasSubstr("2001:db8::/32"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nullRoutes = *swConfig.staticRoutesToNull();
  ASSERT_EQ(nullRoutes.size(), 1);
  EXPECT_EQ(*nullRoutes[0].prefix(), "2001:db8::/32");
}

// Test adding an IPv6 CPU route
TEST_F(CmdConfigIpRouteTestFixture, addIpv6CpuRoute) {
  auto cmd = CmdConfigProtocolStaticIpv6RouteAdd();
  StaticRouteAddIpv6Arg routeArg({"2001:db8:100::/48", "cpu"});

  auto result = cmd.queryClient(localhost(), routeArg);

  EXPECT_THAT(result, HasSubstr("Successfully added static CPU route"));
  EXPECT_THAT(result, HasSubstr("2001:db8:100::/48"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& cpuRoutes = *swConfig.staticRoutesToCPU();
  ASSERT_EQ(cpuRoutes.size(), 1);
  EXPECT_EQ(*cpuRoutes[0].prefix(), "2001:db8:100::/48");
}

// Test updating an existing IPv6 route
TEST_F(CmdConfigIpRouteTestFixture, updateIpv6Route) {
  auto cmd = CmdConfigProtocolStaticIpv6RouteAdd();

  // Add initial route
  StaticRouteAddIpv6Arg routeArg1({"2001:db8::/32", "2001:db8::1"});
  cmd.queryClient(localhost(), routeArg1);

  // Update route with new nexthop
  StaticRouteAddIpv6Arg routeArg2({"2001:db8::/32", "2001:db8::2"});
  auto result = cmd.queryClient(localhost(), routeArg2);

  EXPECT_THAT(result, HasSubstr("Successfully updated static route"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nhopRoutes = *swConfig.staticRoutesWithNhops();
  ASSERT_EQ(nhopRoutes.size(), 1);
  EXPECT_EQ((*nhopRoutes[0].nexthops())[0], "2001:db8::2");
}

// Test IPv4 prefix should fail for IPv6 command
TEST_F(CmdConfigIpRouteTestFixture, ipv4PrefixInIpv6Command) {
  EXPECT_THROW(
      StaticRouteAddIpv6Arg({"10.0.0.0/8", "2001:db8::1"}),
      std::invalid_argument);
}

// Test invalid IPv6 prefix format
TEST_F(CmdConfigIpRouteTestFixture, invalidIpv6PrefixFormat) {
  EXPECT_THROW(
      StaticRouteAddIpv6Arg({"invalid-prefix", "2001:db8::1"}),
      std::invalid_argument);
}

// Test too few arguments for IPv6
TEST_F(CmdConfigIpRouteTestFixture, tooFewArgumentsIpv6) {
  EXPECT_THROW(StaticRouteAddIpv6Arg({"2001:db8::/32"}), std::invalid_argument);
}

// Test IPv4 nexthop should fail for IPv6 command
TEST_F(CmdConfigIpRouteTestFixture, ipv4NexthopInIpv6Command) {
  EXPECT_THROW(
      StaticRouteAddIpv6Arg({"2001:db8::/32", "192.168.1.1"}),
      std::invalid_argument);
}

// Test adding IPv6 default route (::/0)
TEST_F(CmdConfigIpRouteTestFixture, addIpv6DefaultRoute) {
  auto cmd = CmdConfigProtocolStaticIpv6RouteAdd();
  StaticRouteAddIpv6Arg routeArg({"::/0", "2001:db8::1"});

  auto result = cmd.queryClient(localhost(), routeArg);

  EXPECT_THAT(result, HasSubstr("Successfully added static route"));
  EXPECT_THAT(result, HasSubstr("::/0"));

  auto& nhopRoutes = *ConfigSession::getInstance()
                          .config<AgentConfigDomain>()
                          .sw()
                          ->staticRoutesWithNhops();
  ASSERT_EQ(nhopRoutes.size(), 1);
  EXPECT_EQ(*nhopRoutes[0].prefix(), "::/0");
}

// ============== IPv6 Route Delete Tests ==============

// Test deleting an existing IPv6 route with nexthops
TEST_F(CmdConfigIpRouteTestFixture, deleteIpv6RouteWithNexthops) {
  // First add a route
  auto addCmd = CmdConfigProtocolStaticIpv6RouteAdd();
  StaticRouteAddIpv6Arg routeArg({"2001:db8::/32", "2001:db8::1"});
  addCmd.queryClient(localhost(), routeArg);

  // Delete the route
  auto deleteCmd = CmdDeleteProtocolStaticIpv6Route();
  StaticRouteDeleteIpv6PrefixArg prefixArg({"2001:db8::/32"});
  auto result = deleteCmd.queryClient(localhost(), prefixArg);

  EXPECT_THAT(result, HasSubstr("Successfully deleted static route"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nhopRoutes = *swConfig.staticRoutesWithNhops();
  EXPECT_EQ(nhopRoutes.size(), 0);
}

// Test deleting an IPv6 null0 route
TEST_F(CmdConfigIpRouteTestFixture, deleteIpv6DropRoute) {
  // First add a null0 route
  auto addCmd = CmdConfigProtocolStaticIpv6RouteAdd();
  StaticRouteAddIpv6Arg routeArg({"2001:db8::/32", "null0"});
  addCmd.queryClient(localhost(), routeArg);

  // Delete the route
  auto deleteCmd = CmdDeleteProtocolStaticIpv6Route();
  StaticRouteDeleteIpv6PrefixArg prefixArg({"2001:db8::/32"});
  auto result = deleteCmd.queryClient(localhost(), prefixArg);

  EXPECT_THAT(result, HasSubstr("Successfully deleted static route"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& nullRoutes = *swConfig.staticRoutesToNull();
  EXPECT_EQ(nullRoutes.size(), 0);
}

// Test deleting an IPv6 CPU route
TEST_F(CmdConfigIpRouteTestFixture, deleteIpv6CpuRoute) {
  // First add a CPU route
  auto addCmd = CmdConfigProtocolStaticIpv6RouteAdd();
  StaticRouteAddIpv6Arg routeArg({"2001:db8:100::/48", "cpu"});
  addCmd.queryClient(localhost(), routeArg);

  // Delete the route
  auto deleteCmd = CmdDeleteProtocolStaticIpv6Route();
  StaticRouteDeleteIpv6PrefixArg prefixArg({"2001:db8:100::/48"});
  auto result = deleteCmd.queryClient(localhost(), prefixArg);

  EXPECT_THAT(result, HasSubstr("Successfully deleted static route"));

  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto& cpuRoutes = *swConfig.staticRoutesToCPU();
  EXPECT_EQ(cpuRoutes.size(), 0);
}

// Test deleting a non-existent IPv6 route (idempotent)
TEST_F(CmdConfigIpRouteTestFixture, deleteNonExistentIpv6Route) {
  auto deleteCmd = CmdDeleteProtocolStaticIpv6Route();
  StaticRouteDeleteIpv6PrefixArg prefixArg({"2001:db8::/32"});
  auto result = deleteCmd.queryClient(localhost(), prefixArg);

  EXPECT_THAT(result, HasSubstr("not found"));
}

// Test IPv4 prefix should fail for IPv6 delete command
TEST_F(CmdConfigIpRouteTestFixture, deleteIpv4PrefixInIpv6Command) {
  EXPECT_THROW(
      StaticRouteDeleteIpv6PrefixArg({"10.0.0.0/8"}), std::invalid_argument);
}

// Test invalid IPv6 prefix format for delete
TEST_F(CmdConfigIpRouteTestFixture, deleteInvalidIpv6PrefixFormat) {
  EXPECT_THROW(
      StaticRouteDeleteIpv6PrefixArg({"invalid-prefix"}),
      std::invalid_argument);
}

} // namespace facebook::fboss
