// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/IPAddress.h>
#include <folly/IPAddressV6.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"

#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/show/route/CmdShowRoute.h"
#include "fboss/cli/fboss2/commands/show/route/utils.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "fboss/cli/fboss2/utils/FilterUtils.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {
UnicastRoute makeRoute(
    const folly::IPAddress& prefix,
    int16_t prefixLength,
    const std::vector<std::string>& nexthops) {
  UnicastRoute route;
  IpPrefix dest;
  dest.ip() = facebook::network::toBinaryAddress(prefix);
  dest.prefixLength() = prefixLength;
  route.dest() = dest;
  for (const auto& nh : nexthops) {
    NextHopThrift nhop;
    nhop.address() = facebook::network::toBinaryAddress(folly::IPAddress(nh));
    nhop.weight() = 0;
    route.nextHops()->emplace_back(nhop);
  }
  return route;
}

CmdGlobalOptions::UnionList makeFilter(
    const std::string& key,
    const std::string& value) {
  CmdGlobalOptions::FilterTerm term = {
      key, std::make_shared<FilterOpEq>(), value};
  return {{term}};
}
} // namespace

class CmdShowRouteTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<UnicastRoute> routes;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    CmdLocalOptions::getInstance()->setLocalOption(
        "show_route", "--clientID", "");
    routes.emplace_back(
        makeRoute(folly::IPAddress("198.51.100.0"), 24, {"10.99.1.2"}));
    routes.emplace_back(
        makeRoute(folly::IPAddress("2001:db8:1::"), 64, {"2001:db8:99::2"}));
    routes.emplace_back(makeRoute(folly::IPAddress("10.99.1.0"), 24, {}));
    routes.emplace_back(
        makeRoute(folly::IPAddress("::"), 0, {"2001:db8:99::2"}));
  }

  void TearDown() override {
    CmdLocalOptions::getInstance()->setLocalOption(
        "show_route", "--clientID", "");
    CmdHandlerTestBase::TearDown();
  }

  std::map<std::string, cli::RouteEntry> entriesByPrefix(
      const cli::ShowRouteModel& model) {
    std::map<std::string, cli::RouteEntry> byPrefix;
    for (const auto& entry : model.routeEntries().value()) {
      byPrefix[*entry.networkAddress()] = entry;
    }
    return byPrefix;
  }
};

TEST_F(CmdShowRouteTestFixture, createModelAddressFamily) {
  auto model = CmdShowRoute().createModel(routes);
  auto byPrefix = entriesByPrefix(model);
  ASSERT_EQ(byPrefix.size(), 4u);
  EXPECT_EQ(*byPrefix.at("198.51.100.0/24").addressFamily(), "ipv4");
  EXPECT_EQ(*byPrefix.at("10.99.1.0/24").addressFamily(), "ipv4");
  EXPECT_EQ(*byPrefix.at("2001:db8:1::/64").addressFamily(), "ipv6");
  EXPECT_EQ(*byPrefix.at("::/0").addressFamily(), "ipv6");
}

TEST_F(CmdShowRouteTestFixture, validFilterKeys) {
  auto validFilters = CmdShowRoute().getValidFilters();
  EXPECT_EQ(validFilters.count("networkAddress"), 1u);
  EXPECT_EQ(validFilters.count("addressFamily"), 1u);
  EXPECT_EQ(validFilters.count("protocol"), 0u);
  // List fields are not filterable.
  EXPECT_EQ(validFilters.count("nextHops"), 0u);
}

TEST_F(CmdShowRouteTestFixture, filterByAddressFamily) {
  auto model = CmdShowRoute().createModel(routes);
  auto validFilters = CmdShowRoute().getValidFilters();

  auto filtered = filterOutput<CmdShowRoute>(
      model, makeFilter("addressFamily", "ipv6"), validFilters);
  auto byPrefix = entriesByPrefix(filtered);
  EXPECT_EQ(byPrefix.size(), 2u);
  EXPECT_EQ(byPrefix.count("2001:db8:1::/64"), 1u);
  EXPECT_EQ(byPrefix.count("::/0"), 1u);

  filtered = filterOutput<CmdShowRoute>(
      model, makeFilter("addressFamily", "ipv4"), validFilters);
  EXPECT_EQ(filtered.routeEntries()->size(), 2u);
}

TEST_F(CmdShowRouteTestFixture, filterIntersectionAndUnion) {
  auto model = CmdShowRoute().createModel(routes);
  auto validFilters = CmdShowRoute().getValidFilters();

  // addressFamily == ipv6 && networkAddress == ::/0
  CmdGlobalOptions::FilterTerm t1 = {
      "addressFamily", std::make_shared<FilterOpEq>(), "ipv6"};
  CmdGlobalOptions::FilterTerm t2 = {
      "networkAddress", std::make_shared<FilterOpEq>(), "::/0"};
  CmdGlobalOptions::UnionList intersection = {{t1, t2}};
  auto filtered = filterOutput<CmdShowRoute>(model, intersection, validFilters);
  auto byPrefix = entriesByPrefix(filtered);
  EXPECT_EQ(byPrefix.size(), 1u);
  EXPECT_EQ(byPrefix.count("::/0"), 1u);

  // networkAddress == 10.99.1.0/24 || networkAddress == 198.51.100.0/24
  CmdGlobalOptions::FilterTerm t3 = {
      "networkAddress", std::make_shared<FilterOpEq>(), "10.99.1.0/24"};
  CmdGlobalOptions::FilterTerm t4 = {
      "networkAddress", std::make_shared<FilterOpEq>(), "198.51.100.0/24"};
  CmdGlobalOptions::UnionList unionFilter = {{t3}, {t4}};
  filtered = filterOutput<CmdShowRoute>(model, unionFilter, validFilters);
  EXPECT_EQ(filtered.routeEntries()->size(), 2u);
}

TEST_F(CmdShowRouteTestFixture, parseClientId) {
  using show::route::utils::parseClientId;
  EXPECT_EQ(parseClientId("BGPD"), ClientID::BGPD);
  EXPECT_EQ(parseClientId("bgpd"), ClientID::BGPD);
  EXPECT_EQ(parseClientId("static_route"), ClientID::STATIC_ROUTE);
  EXPECT_EQ(parseClientId("Interface_Route"), ClientID::INTERFACE_ROUTE);
  EXPECT_EQ(parseClientId("700"), ClientID::STATIC_INTERNAL);
  EXPECT_EQ(parseClientId("786"), ClientID::OPENR);
  EXPECT_THROW(parseClientId("static"), std::invalid_argument);
  EXPECT_THROW(parseClientId(""), std::invalid_argument);
  EXPECT_THROW(parseClientId("-1"), std::invalid_argument);
}

TEST_F(CmdShowRouteTestFixture, queryClient) {
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), getRouteTable(_))
      .WillOnce(Invoke([&](auto& entries) { entries = routes; }));
  EXPECT_CALL(getMockAgent(), getRouteTableByClient(_, _)).Times(0);

  auto model = CmdShowRoute().queryClient(localhost());
  EXPECT_EQ(model.routeEntries()->size(), 4u);
}

TEST_F(CmdShowRouteTestFixture, queryClientByClientId) {
  setupMockedAgentServer();
  CmdLocalOptions::getInstance()->setLocalOption(
      "show_route", "--clientID", "static_route");
  EXPECT_CALL(getMockAgent(), getRouteTable(_)).Times(0);
  EXPECT_CALL(
      getMockAgent(),
      getRouteTableByClient(_, static_cast<int16_t>(ClientID::STATIC_ROUTE)))
      .WillOnce(Invoke(
          [&](auto& entries, int16_t) { entries = {routes[0], routes[2]}; }));

  auto model = CmdShowRoute().queryClient(localhost());
  EXPECT_EQ(model.routeEntries()->size(), 2u);
}

TEST_F(CmdShowRouteTestFixture, queryClientInvalidClientId) {
  setupMockedAgentServer();
  CmdLocalOptions::getInstance()->setLocalOption(
      "show_route", "--clientID", "static");
  EXPECT_CALL(getMockAgent(), getRouteTable(_)).Times(0);
  EXPECT_CALL(getMockAgent(), getRouteTableByClient(_, _)).Times(0);
  EXPECT_THROW(CmdShowRoute().queryClient(localhost()), std::invalid_argument);
}

TEST_F(CmdShowRouteTestFixture, printOutput) {
  auto model = CmdShowRoute().createModel(routes);
  std::stringstream ss;
  CmdShowRoute().printOutput(model, ss);
  std::string out = ss.str();
  EXPECT_THAT(out, HasSubstr("Network Address: 198.51.100.0/24"));
  EXPECT_THAT(out, HasSubstr("via 10.99.1.2"));
}

TEST(CmdShowRouteTest, AnnotatesBackupNextHops) {
  UnicastRoute route;
  route.dest() = IpPrefix();
  route.dest()->ip() =
      network::toBinaryAddress(folly::IPAddressV6("2401:db00::"));
  route.dest()->prefixLength() = 64;

  NextHopThrift primary;
  primary.address() = network::toBinaryAddress(folly::IPAddressV6("1::1"));

  NextHopThrift backup;
  backup.address() = network::toBinaryAddress(folly::IPAddressV6("2::1"));
  backup.role() = NextHopRole::BACKUP;

  route.nextHops() = {primary, backup};
  std::vector<UnicastRoute> routes{route};

  auto cmd = CmdShowRoute();
  auto model = cmd.createModel(routes);
  std::stringstream output;
  cmd.printOutput(model, output);

  EXPECT_EQ(
      output.str(),
      "Network Address: 2401:db00::/64\n"
      "\tvia 1::1 weight 1\n"
      "\tvia 2::1 weight 1 (BACKUP)\n");
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowRouteTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowRouteTraits::description().empty());
  EXPECT_FALSE(CmdShowRoute::sampleModel().routeEntries()->empty());
}

} // namespace facebook::fboss
