/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AclNexthopHandler.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/AclTableGroup.h"
#include "fboss/agent/state/AclTableGroupMap.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

using namespace facebook::fboss;
using facebook::fboss::InterfaceID;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using std::make_pair;
using std::make_shared;
using std::shared_ptr;

namespace facebook::fboss {

namespace {
constexpr AdminDistance DISTANCE = AdminDistance::STATIC_ROUTE;

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

}; // namespace

const std::string kAclName = "acl0";
const RouteNextHopSet kEmptyNexthopSet;

template <typename AddrType>
class AclNexthopHandlerTest : public ::testing::Test {
 public:
  using Func = folly::Function<void()>;
  using StateUpdateFn = SwSwitch::StateUpdateFn;
  using AddrT = AddrType;

  cfg::SwitchConfig testConfig() {
    auto cfg = testConfigA();

    int prevVlanSize = cfg.vlans()->size();
    cfg.vlans()->resize(prevVlanSize + 4);
    int startVid = 100;
    for (int vid = startVid, idx = prevVlanSize; vid < startVid + 4;
         ++vid, ++idx) {
      cfg.vlans()[idx].id() = vid;
      cfg.vlans()[idx].name() = fmt::format("Vlan{}", vid);
      cfg.vlans()[idx].intfID() = vid;
    }

    cfg.interfaces()->resize(prevVlanSize + 4);
    for (int vid = startVid, idx = prevVlanSize; vid < startVid + 4;
         ++vid, ++idx) {
      cfg.interfaces()[idx].intfID() = vid;
      cfg.interfaces()[idx].routerID() = 0;
      cfg.interfaces()[idx].vlanID() = vid;
      cfg.interfaces()[idx].name() = fmt::format("interface{}", vid);
      cfg.interfaces()[idx].mac() = fmt::format("00:02:00:00:00:{:02x}", vid);
      cfg.interfaces()[idx].mtu() = 9000;
      cfg.interfaces()[idx].ipAddresses()->resize(4);
      cfg.interfaces()[idx].ipAddresses()[0] =
          fmt::format("100.0.{}.1/24", vid);
      cfg.interfaces()[idx].ipAddresses()[1] =
          fmt::format("172.16.{}.1/24", vid);
      cfg.interfaces()[idx].ipAddresses()[2] =
          fmt::format("1000:{:04x}::0001/64", vid);
      cfg.interfaces()[idx].ipAddresses()[3] = "fe80::/64"; // link local
    }
    return cfg;
  }

  cfg::SwitchConfig redirectAclConfig(
      const std::vector<cfg::RedirectNextHop>& nhops) {
    auto config = testConfig();
    cfg::AclEntry acl;
    acl.name() = kAclName;
    acl.dstIp() = "2401::2/128";
    config.acls()->push_back(acl);
    auto redirect = cfg::RedirectToNextHopAction();
    redirect.redirectNextHops() = nhops;
    auto action = cfg::MatchAction();
    action.redirectToNextHop() = redirect;
    cfg::MatchToAction mta;
    mta.matcher() = kAclName;
    mta.action() = action;
    if (!config.dataPlaneTrafficPolicy().has_value()) {
      config.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
    }
    config.dataPlaneTrafficPolicy()->matchToAction()->push_back(mta);
    return config;
  }

  void SetUp() override {
    auto config = testConfig();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
  }

  void verifyStateUpdate(Func func) {
    runInUpdateEventBaseAndWait(std::move(func));
  }

  void TearDown() override {
    schedulePendingTestStateUpdates();
  }

  void updateState(folly::StringPiece name, StateUpdateFn func) {
    sw_->updateStateBlocking(name, func);
  }

  std::vector<std::string> getAclNexthopIps(int numNextHops) const {
    std::vector<std::string> nexthopIps(numNextHops);
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      for (int i = 0; i < numNextHops; ++i) {
        nexthopIps[i] = fmt::format("123.0.{}.1", i);
      }
    } else {
      for (int i = 0; i < numNextHops; ++i) {
        nexthopIps[i] = fmt::format("1234:{}::1", i);
      }
    }
    return nexthopIps;
  }

  RoutePrefix<AddrT> makePrefix(std::string prefixStr) {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return makePrefixV4(prefixStr);
    } else {
      return makePrefixV6(prefixStr);
    }
  }

  std::vector<std::pair<InterfaceID, std::string>> getResolvedNexthops(
      int numNextHops) const {
    std::vector<std::pair<InterfaceID, std::string>> intfAndIps(numNextHops);
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      for (int i = 0, vid = 100; i < numNextHops; ++i, ++vid) {
        intfAndIps[i] = {InterfaceID(vid), fmt::format("100.0.{}.1", vid)};
      }
    } else {
      for (int i = 0, vid = 100; i < numNextHops; ++i, ++vid) {
        intfAndIps[i] = {
            InterfaceID(vid), fmt::format("1000:{:04x}::0001", vid)};
      }
    }
    return intfAndIps;
  }

  std::vector<std::string> getMatchingPrefixes(int numPrefixes) const {
    std::vector<std::string> prefixes(numPrefixes);
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      for (int i = 0; i < numPrefixes; ++i) {
        prefixes[i] = fmt::format("123.0.{}.0/24", i);
      }
    } else {
      for (int i = 0; i < numPrefixes; ++i) {
        prefixes[i] = fmt::format("1234:{}::0/64", i);
      }
    }
    return prefixes;
  }

  void addRoute(const RoutePrefix<AddrT>& prefix, RouteNextHopSet nexthops) {
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(
        RouterID(0),
        prefix.network(),
        prefix.mask(),
        ClientID(1000),
        RouteNextHopEntry(nexthops, DISTANCE));
    updater.program();
  }

  void delRoute(const RoutePrefix<AddrT>& prefix) {
    auto updater = sw_->getRouteUpdater();
    updater.delRoute(
        RouterID(0), prefix.network(), prefix.mask(), ClientID(1000));
    updater.program();
  }

  std::shared_ptr<SwitchState> addAcl(
      const std::shared_ptr<SwitchState>& state,
      const std::string& name,
      const std::vector<std::string>& nexthopIps,
      const std::vector<int32_t>& intfIDs = {},
      int priority = 0) {
    auto aclEntry = std::make_shared<AclEntry>(priority, name);
    aclEntry->setDstIp(
        folly::IPAddress::tryCreateNetwork("2401::2/128").value());
    auto cfgRedirectToNextHop = cfg::RedirectToNextHopAction();
    auto idx = 0;
    for (auto nhIp : nexthopIps) {
      cfg::RedirectNextHop nhop;
      nhop.ip() = nhIp;
      if (intfIDs.size()) {
        nhop.intfID() = intfIDs[idx];
      }
      cfgRedirectToNextHop.redirectNextHops()->push_back(nhop);
      idx++;
    }
    auto redirectToNextHop = MatchAction::RedirectToNextHopAction();
    redirectToNextHop.first = cfgRedirectToNextHop;
    MatchAction action = MatchAction();
    action.setRedirectToNextHop(redirectToNextHop);
    aclEntry->setAclAction(action);
    auto newState = state->isPublished() ? state->clone() : state;
    auto acls = newState->getAcls()->modify(&newState);
    acls->addNode(aclEntry, scope());
    return newState;
  }

  std::shared_ptr<SwitchState> addTunnelEncapAcl(
      const std::shared_ptr<SwitchState>& state,
      const std::string& name,
      const std::string& nexthopIp,
      const std::string& tunnelId) {
    auto aclEntry = std::make_shared<AclEntry>(0, name);
    aclEntry->setDstIp(
        folly::IPAddress::tryCreateNetwork("2401::2/128").value());
    auto cfgRedirectToNextHop = cfg::RedirectToNextHopAction();
    cfg::RedirectNextHop nhop;
    nhop.ip() = nexthopIp;
    nhop.tunnelType() = TunnelType::IP_IN_IP_ENCAP;
    nhop.tunnelId() = tunnelId;
    cfgRedirectToNextHop.redirectNextHops()->push_back(nhop);
    auto redirectToNextHop = MatchAction::RedirectToNextHopAction();
    redirectToNextHop.first = cfgRedirectToNextHop;
    MatchAction action = MatchAction();
    action.setRedirectToNextHop(redirectToNextHop);
    aclEntry->setAclAction(action);
    auto newState = state->isPublished() ? state->clone() : state;
    auto acls = newState->getAcls()->modify(&newState);
    acls->addNode(aclEntry, scope());
    return newState;
  }

  std::shared_ptr<SwitchState> addAclToTable(
      const std::shared_ptr<SwitchState>& state,
      const std::string& tableName,
      const std::string& name,
      const std::vector<std::string>& nexthopIps,
      const std::vector<int32_t>& intfIDs = {},
      int priority = 0) {
    auto aclEntry = std::make_shared<AclEntry>(priority, name);
    aclEntry->setDstIp(
        folly::IPAddress::tryCreateNetwork("2401::2/128").value());
    auto cfgRedirectToNextHop = cfg::RedirectToNextHopAction();
    auto idx = 0;
    for (auto nhIp : nexthopIps) {
      cfg::RedirectNextHop nhop;
      nhop.ip() = nhIp;
      if (intfIDs.size()) {
        nhop.intfID() = intfIDs[idx];
      }
      cfgRedirectToNextHop.redirectNextHops()->push_back(nhop);
      idx++;
    }
    auto redirectToNextHop = MatchAction::RedirectToNextHopAction();
    redirectToNextHop.first = cfgRedirectToNextHop;
    MatchAction action = MatchAction();
    action.setRedirectToNextHop(redirectToNextHop);
    aclEntry->setAclAction(action);

    auto newState = state->isPublished() ? state->clone() : state;
    auto groups = newState->getAclTableGroups()->modify(&newState);
    auto groupMap = groups->getMapNodeIf(scope());
    if (!groupMap) {
      groupMap = std::make_shared<AclTableGroupMap>();
      groups->addMapNode(groupMap, scope());
    }
    auto group = groupMap->getAclTableGroupIf(cfg::AclStage::INGRESS);
    if (!group) {
      group = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
      group->setAclTableMap(std::make_shared<AclTableMap>());
      groupMap->addAclTableGroup(group);
    }
    auto newGroup = group->isPublished() ? group->clone() : group;
    auto newTableMap = newGroup->getAclTableMap()->clone();
    auto origTable = newTableMap->getTableIf(tableName);
    auto origAcls = origTable ? origTable->getAclMap().unwrap() : nullptr;
    auto tableAcls = origAcls ? origAcls->clone() : std::make_shared<AclMap>();
    tableAcls->addEntry(aclEntry);
    if (origTable) {
      auto newTable = origTable->clone();
      newTable->setAclMap(tableAcls);
      newTableMap->updateNode(newTable);
    } else {
      auto newTable = std::make_shared<AclTable>(0, tableName);
      newTable->setAclMap(tableAcls);
      newTableMap->addTable(newTable);
    }
    newGroup->setAclTableMap(std::move(newTableMap));
    groupMap->updateNode(newGroup);
    return newState;
  }

  RouteNextHopSet makeResolvedMplsNextHops(
      std::vector<std::pair<InterfaceID, std::string>> intfAndIPs,
      int baseLabel) {
    RouteNextHopSet nhops;
    int idx = 0;
    for (auto intfAndIP : intfAndIPs) {
      LabelForwardingAction labelAction(
          LabelForwardingAction::LabelForwardingType::PUSH,
          LabelForwardingAction::LabelStack{baseLabel + idx});
      nhops.emplace(ResolvedNextHop(
          IPAddress(intfAndIP.second),
          intfAndIP.first,
          UCMP_DEFAULT_WEIGHT,
          labelAction));
      ++idx;
    }
    return nhops;
  }

 protected:
  void checkResolvedNexthops(
      const std::shared_ptr<AclEntry>& aclEntry,
      const RouteNextHopSet& expectedNexthops) {
    EXPECT_NE(aclEntry, nullptr);
    const auto& action = aclEntry->getAclAction();
    auto matchAction = MatchAction::fromThrift(action->toThrift());
    auto resolvedNexthopSet = matchAction.getRedirectToNextHop().value().second;
    XLOG(DBG3) << "expected nexthops: " << expectedNexthops
               << ", resolved nexthops: " << resolvedNexthopSet;
    EXPECT_EQ(resolvedNexthopSet, expectedNexthops);
    EXPECT_EQ(*aclEntry->isEnabled(), expectedNexthops.size() ? true : false);
  }

  void verifyResolvedNexthopsInAclAction(
      const std::string& aclName,
      const RouteNextHopSet& expectedNexthops) {
    auto verifyResolvedNexthops = [&]() {
      auto state = sw_->getState();
      auto aclEntry = state->getAcls()->getNodeIf(aclName);
      checkResolvedNexthops(aclEntry, expectedNexthops);
    };
    verifyStateUpdate(verifyResolvedNexthops);
  }

  void verifyResolvedNexthopsInTableAclAction(
      const std::string& tableName,
      const std::string& aclName,
      const RouteNextHopSet& expectedNexthops) {
    auto verifyResolvedNexthops = [&]() {
      checkResolvedNexthops(getTableAcl(tableName, aclName), expectedNexthops);
    };
    verifyStateUpdate(verifyResolvedNexthops);
  }

  std::shared_ptr<AclEntry> getTableAcl(
      const std::string& tableName,
      const std::string& aclName) {
    auto groupMap = sw_->getState()->getAclTableGroups()->getMapNodeIf(scope());
    auto table = groupMap->getAclTableGroup(cfg::AclStage::INGRESS)
                     ->getAclTableMap()
                     ->getTable(tableName);
    return table->getAclMap().unwrap()->getEntry(aclName);
  }

  void runInUpdateEventBaseAndWait(Func func) {
    auto* evb = sw_->getUpdateEvb();
    evb->runInFbossEventBaseThreadAndWait(std::move(func));
  }

  void schedulePendingTestStateUpdates() {
    runInUpdateEventBaseAndWait([]() {});
  }

  std::unique_ptr<HwTestHandle> handle_;
  std::unique_ptr<ThriftHandler> thriftHandler{nullptr};
  SwSwitch* sw_;
};

using AclNexthopHandlerTestTypes =
    ::testing::Types<folly::IPAddressV4, folly::IPAddressV6>;

TYPED_TEST_CASE(AclNexthopHandlerTest, AclNexthopHandlerTestTypes);

TYPED_TEST(AclNexthopHandlerTest, UnresolvedAclNextHop) {
  this->updateState(
      "UnresolvedAclNextHop",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addAcl(state, kAclName, this->getAclNexthopIps(1));
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto aclEntry = state->getAcls()->getNode(kAclName);
    EXPECT_NE(aclEntry, nullptr);
    auto action = aclEntry->getAclAction();
    auto resolvedNexthopSet = util::toRouteNextHopSet(
        action->template cref<switch_state_tags::redirectToNextHop>()
            ->template cref<switch_state_tags::resolvedNexthops>()
            ->toThrift());
    EXPECT_EQ(resolvedNexthopSet.size(), 0);
    EXPECT_EQ(*aclEntry->isEnabled(), false);
  });
}

TYPED_TEST(AclNexthopHandlerTest, ResolvedAclNextHopSingleNexthop) {
  this->updateState(
      "UnresolvedAclNextHop",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addAcl(state, kAclName, this->getAclNexthopIps(1));
      });

  auto matchingPrefixes = this->getMatchingPrefixes(1);
  auto nexthopIps = this->getResolvedNexthops(2);
  auto longestPrefix1 = this->makePrefix(matchingPrefixes[0]);
  RouteNextHopSet nexthops1 =
      makeResolvedNextHops(nexthopIps, UCMP_DEFAULT_WEIGHT);
  this->addRoute(longestPrefix1, nexthops1);
  this->verifyResolvedNexthopsInAclAction(kAclName, nexthops1);

  // Now change the route nexthops and verify the
  // change is reflected on the ACL redirect action
  this->delRoute(longestPrefix1);
  this->verifyResolvedNexthopsInAclAction(kAclName, kEmptyNexthopSet);

  RouteNextHopSet nexthops2 = makeResolvedNextHops(
      {
          nexthopIps[0],
      },
      UCMP_DEFAULT_WEIGHT);
  this->addRoute(longestPrefix1, nexthops2);
  this->verifyResolvedNexthopsInAclAction(kAclName, nexthops2);
}

TYPED_TEST(AclNexthopHandlerTest, ResolvedAclNextHopMultiNexthop) {
  this->updateState(
      "UnresolvedAclNextHop",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addAcl(state, kAclName, this->getAclNexthopIps(2));
      });

  auto nexthopIps = this->getResolvedNexthops(4);
  RouteNextHopSet nexthops1 = makeResolvedNextHops(
      {
          nexthopIps[0],
          nexthopIps[1],
      },
      UCMP_DEFAULT_WEIGHT);
  RouteNextHopSet nexthops2 = makeResolvedNextHops(
      {
          nexthopIps[2],
          nexthopIps[3],
      },
      UCMP_DEFAULT_WEIGHT);
  auto matchingPrefixes = this->getMatchingPrefixes(2);
  auto longestPrefix1 = this->makePrefix(matchingPrefixes[0]);
  this->addRoute(longestPrefix1, nexthops1);
  auto longestPrefix2 = this->makePrefix(matchingPrefixes[1]);
  this->addRoute(longestPrefix2, nexthops2);

  RouteNextHopSet expectedNexthops;
  expectedNexthops.merge(nexthops1);
  expectedNexthops.merge(nexthops2);
  this->verifyResolvedNexthopsInAclAction(kAclName, expectedNexthops);

  // Now change the route nexthops and verify the
  // change is reflected on the ACL redirect action
  this->delRoute(longestPrefix1);
  this->delRoute(longestPrefix2);
  this->verifyResolvedNexthopsInAclAction(kAclName, kEmptyNexthopSet);

  RouteNextHopSet nexthops3 = makeResolvedNextHops(
      {
          nexthopIps[0],
      },
      UCMP_DEFAULT_WEIGHT);
  RouteNextHopSet nexthops4 = makeResolvedNextHops(
      {
          nexthopIps[2],
      },
      UCMP_DEFAULT_WEIGHT);
  this->addRoute(longestPrefix1, nexthops3);
  this->addRoute(longestPrefix2, nexthops4);

  expectedNexthops.clear();
  expectedNexthops.merge(nexthops3);
  expectedNexthops.merge(nexthops4);
  this->verifyResolvedNexthopsInAclAction(kAclName, expectedNexthops);
}

// Test with nexthops that include MPLS action
TYPED_TEST(AclNexthopHandlerTest, MplsNexthops) {
  this->updateState(
      "UnresolvedAclNextHop",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addAcl(state, kAclName, this->getAclNexthopIps(2));
      });

  auto nexthopIps = this->getResolvedNexthops(4);
  RouteNextHopSet nexthops1 = this->makeResolvedMplsNextHops(
      {
          nexthopIps[0],
          nexthopIps[1],
      },
      2000);
  RouteNextHopSet nexthops2 = this->makeResolvedMplsNextHops(
      {
          nexthopIps[2],
          nexthopIps[3],
      },
      3000);
  auto matchingPrefixes = this->getMatchingPrefixes(2);
  auto longestPrefix1 = this->makePrefix(matchingPrefixes[0]);
  this->addRoute(longestPrefix1, nexthops1);
  auto longestPrefix2 = this->makePrefix(matchingPrefixes[1]);
  this->addRoute(longestPrefix2, nexthops2);

  RouteNextHopSet expectedNexthops;
  expectedNexthops.merge(nexthops1);
  expectedNexthops.merge(nexthops2);
  this->verifyResolvedNexthopsInAclAction(kAclName, expectedNexthops);

  // Now change the route nexthops and verify the
  // change is reflected on the ACL redirect action
  this->delRoute(longestPrefix1);
  this->delRoute(longestPrefix2);
  this->verifyResolvedNexthopsInAclAction(kAclName, kEmptyNexthopSet);

  RouteNextHopSet nexthops3 = this->makeResolvedMplsNextHops(
      {
          nexthopIps[0],
      },
      2000);
  RouteNextHopSet nexthops4 = this->makeResolvedMplsNextHops(
      {
          nexthopIps[2],
      },
      3000);
  this->addRoute(longestPrefix1, nexthops3);
  this->addRoute(longestPrefix2, nexthops4);

  expectedNexthops.clear();
  expectedNexthops.merge(nexthops3);
  expectedNexthops.merge(nexthops4);
  this->verifyResolvedNexthopsInAclAction(kAclName, expectedNexthops);
}

// Link-local nexthops are only meaningful with interface scoping and are
// inherently resolved, so no RIB lookup is needed.
TYPED_TEST(AclNexthopHandlerTest, LinkLocalNexthopResolvedWithInterface) {
  if constexpr (std::is_same_v<
                    typename TestFixture::AddrT,
                    folly::IPAddressV6>) {
    this->updateState(
        "LinkLocalNexthop",
        [=, this](const std::shared_ptr<SwitchState>& state) {
          return this->addAcl(state, kAclName, {"fe80::1"}, {100});
        });

    RouteNextHopSet expectedNexthops = makeResolvedNextHops(
        {{InterfaceID(100), "fe80::1"}}, UCMP_DEFAULT_WEIGHT);
    this->verifyResolvedNexthopsInAclAction(kAclName, expectedNexthops);
  }
}

// Link-local nexthop without interface scoping is invalid, resolution throws.
TYPED_TEST(AclNexthopHandlerTest, LinkLocalNexthopWithoutInterfaceThrows) {
  if constexpr (std::is_same_v<
                    typename TestFixture::AddrT,
                    folly::IPAddressV6>) {
    auto cfgRedirectToNextHop = cfg::RedirectToNextHopAction();
    cfg::RedirectNextHop nhop;
    nhop.ip() = "fe80::1";
    cfgRedirectToNextHop.redirectNextHops()->push_back(nhop);
    auto redirectToNextHop = MatchAction::RedirectToNextHopAction();
    redirectToNextHop.first = cfgRedirectToNextHop;
    MatchAction action = MatchAction();
    action.setRedirectToNextHop(redirectToNextHop);
    AclNexthopHandler handler(this->sw_);
    EXPECT_THROW(handler.resolveActionNexthops(action), FbossError);
  }
}

// Config with a link-local redirect nexthop lacking interface scoping is
// rejected at apply time.
TYPED_TEST(AclNexthopHandlerTest, LinkLocalNexthopWithoutInterfaceBadConfig) {
  if constexpr (std::is_same_v<
                    typename TestFixture::AddrT,
                    folly::IPAddressV6>) {
    auto config = this->testConfig();
    cfg::AclEntry acl;
    acl.name() = kAclName;
    acl.dstIp() = "2401::2/128";
    config.acls()->push_back(acl);
    auto redirect = cfg::RedirectToNextHopAction();
    cfg::RedirectNextHop nhop;
    nhop.ip() = "fe80::1";
    redirect.redirectNextHops()->push_back(nhop);
    auto action = cfg::MatchAction();
    action.redirectToNextHop() = redirect;
    cfg::MatchToAction mta;
    mta.matcher() = kAclName;
    mta.action() = action;
    if (!config.dataPlaneTrafficPolicy().has_value()) {
      config.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
    }
    config.dataPlaneTrafficPolicy()->matchToAction()->push_back(mta);
    EXPECT_THROW(
        this->sw_->applyConfig("LinkLocalNexthopNoIntf", config), FbossError);
  }
}

// Mixing tunnel encap and non-tunnel nexthops is invalid, resolution throws.
TYPED_TEST(AclNexthopHandlerTest, MixedTunnelAndNonTunnelNexthopThrows) {
  auto cfgRedirectToNextHop = cfg::RedirectToNextHopAction();
  cfg::RedirectNextHop tunnelNhop;
  tunnelNhop.ip() = this->getAclNexthopIps(2)[0];
  tunnelNhop.tunnelType() = TunnelType::IP_IN_IP_ENCAP;
  tunnelNhop.tunnelId() = "encapTunnel0";
  cfg::RedirectNextHop plainNhop;
  plainNhop.ip() = this->getAclNexthopIps(2)[1];
  cfgRedirectToNextHop.redirectNextHops() = {tunnelNhop, plainNhop};
  auto redirectToNextHop = MatchAction::RedirectToNextHopAction();
  redirectToNextHop.first = cfgRedirectToNextHop;
  MatchAction action = MatchAction();
  action.setRedirectToNextHop(redirectToNextHop);
  AclNexthopHandler handler(this->sw_);
  EXPECT_THROW(handler.resolveActionNexthops(action), FbossError);
}

// Non-link-local nexthop with interface scoping is invalid, resolution throws.
TYPED_TEST(AclNexthopHandlerTest, NonLinkLocalNexthopWithInterfaceThrows) {
  auto cfgRedirectToNextHop = cfg::RedirectToNextHopAction();
  cfg::RedirectNextHop nhop;
  nhop.ip() = this->getAclNexthopIps(1)[0];
  nhop.intfID() = 100;
  cfgRedirectToNextHop.redirectNextHops()->push_back(nhop);
  auto redirectToNextHop = MatchAction::RedirectToNextHopAction();
  redirectToNextHop.first = cfgRedirectToNextHop;
  MatchAction action = MatchAction();
  action.setRedirectToNextHop(redirectToNextHop);
  AclNexthopHandler handler(this->sw_);
  EXPECT_THROW(handler.resolveActionNexthops(action), FbossError);
}

// Config with a non-link-local redirect nexthop carrying interface scoping
// is rejected at apply time.
TYPED_TEST(AclNexthopHandlerTest, NonLinkLocalNexthopWithInterfaceBadConfig) {
  if constexpr (std::is_same_v<
                    typename TestFixture::AddrT,
                    folly::IPAddressV4>) {
    auto config = this->testConfig();
    cfg::AclEntry acl;
    acl.name() = kAclName;
    acl.dstIp() = "2401::2/128";
    config.acls()->push_back(acl);
    auto redirect = cfg::RedirectToNextHopAction();
    cfg::RedirectNextHop nhop;
    nhop.ip() = "123.0.0.1";
    nhop.intfID() = 100;
    redirect.redirectNextHops()->push_back(nhop);
    auto action = cfg::MatchAction();
    action.redirectToNextHop() = redirect;
    cfg::MatchToAction mta;
    mta.matcher() = kAclName;
    mta.action() = action;
    if (!config.dataPlaneTrafficPolicy().has_value()) {
      config.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
    }
    config.dataPlaneTrafficPolicy()->matchToAction()->push_back(mta);
    EXPECT_THROW(
        this->sw_->applyConfig("NonLinkLocalNexthopWithIntf", config),
        FbossError);
  }
}

// A redirect mixing tunnel encap and non-tunnel nexthops is rejected at
// config apply; the tunnel-only form of the same ACL is accepted.
TYPED_TEST(AclNexthopHandlerTest, MixedTunnelAndNonTunnelNexthopBadConfig) {
  if constexpr (std::is_same_v<
                    typename TestFixture::AddrT,
                    folly::IPAddressV6>) {
    cfg::RedirectNextHop tunnelNhop;
    tunnelNhop.ip() = "2401::1";
    tunnelNhop.tunnelType() = TunnelType::IP_IN_IP_ENCAP;
    tunnelNhop.tunnelId() = "encapTunnel0";
    cfg::RedirectNextHop plainNhop;
    plainNhop.ip() = this->getAclNexthopIps(1)[0];

    EXPECT_NO_THROW(this->sw_->applyConfig(
        "TunnelOnlyNexthop", this->redirectAclConfig({tunnelNhop})));
    EXPECT_THROW(
        this->sw_->applyConfig(
            "MixedTunnelNexthop",
            this->redirectAclConfig({tunnelNhop, plainNhop})),
        FbossError);
  }
}

// Tunnel encap redirects are routed by HW, so config apply leaves the
// resolved nexthop set empty even when a route covers the tunnel endpoint.
TYPED_TEST(AclNexthopHandlerTest, TunnelEncapNexthopSkipsResolutionOnConfig) {
  if constexpr (std::is_same_v<
                    typename TestFixture::AddrT,
                    folly::IPAddressV6>) {
    RouteNextHopSet routeNexthops =
        makeResolvedNextHops(this->getResolvedNexthops(1), UCMP_DEFAULT_WEIGHT);
    this->addRoute(this->makePrefix("2401::/64"), routeNexthops);
    cfg::RedirectNextHop tunnelNhop;
    tunnelNhop.ip() = "2401::1";
    tunnelNhop.tunnelType() = TunnelType::IP_IN_IP_ENCAP;
    tunnelNhop.tunnelId() = "encapTunnel0";

    this->sw_->applyConfig(
        "TunnelOnlyNexthop", this->redirectAclConfig({tunnelNhop}));

    this->verifyStateUpdate([this]() {
      auto aclEntry = this->sw_->getState()->getAcls()->getNodeIf(kAclName);
      ASSERT_NE(aclEntry, nullptr);
      auto action =
          MatchAction::fromThrift(aclEntry->getAclAction()->toThrift());
      EXPECT_EQ(action.getRedirectToNextHop().value().second, kEmptyNexthopSet);
      EXPECT_NE(aclEntry->isEnabled(), std::optional<bool>(false));
    });
  }
}

// A redirect with no nexthops has nothing to resolve: it is applied disabled
// and stays disabled when routes change.
TYPED_TEST(AclNexthopHandlerTest, EmptyRedirectNexthopsStayDisabled) {
  this->sw_->applyConfig("EmptyRedirect", this->redirectAclConfig({}));
  this->verifyResolvedNexthopsInAclAction(kAclName, kEmptyNexthopSet);

  RouteNextHopSet nexthops =
      makeResolvedNextHops(this->getResolvedNexthops(1), UCMP_DEFAULT_WEIGHT);
  this->addRoute(this->makePrefix(this->getMatchingPrefixes(1)[0]), nexthops);
  this->verifyResolvedNexthopsInAclAction(kAclName, kEmptyNexthopSet);
}

TYPED_TEST(AclNexthopHandlerTest, UnchangedAclEntryKeepsIdentity) {
  auto aclNexthopIps = this->getAclNexthopIps(2);
  this->updateState(
      "AclNextHops", [=, this](const std::shared_ptr<SwitchState>& state) {
        auto newState = this->addAcl(state, "acl0", {aclNexthopIps[0]});
        return this->addAcl(newState, "acl1", {aclNexthopIps[1]}, {}, 1);
      });

  auto matchingPrefixes = this->getMatchingPrefixes(2);
  auto nexthopIps = this->getResolvedNexthops(2);
  RouteNextHopSet nexthops0 =
      makeResolvedNextHops({nexthopIps[0]}, UCMP_DEFAULT_WEIGHT);
  RouteNextHopSet nexthops1 =
      makeResolvedNextHops({nexthopIps[1]}, UCMP_DEFAULT_WEIGHT);
  this->addRoute(this->makePrefix(matchingPrefixes[0]), nexthops0);
  this->verifyResolvedNexthopsInAclAction("acl0", nexthops0);

  std::shared_ptr<AclEntry> acl0;
  this->verifyStateUpdate(
      [&]() { acl0 = this->sw_->getState()->getAcls()->getNodeIf("acl0"); });
  this->addRoute(this->makePrefix(matchingPrefixes[1]), nexthops1);
  this->verifyResolvedNexthopsInAclAction("acl1", nexthops1);
  this->verifyStateUpdate([&]() {
    EXPECT_EQ(this->sw_->getState()->getAcls()->getNodeIf("acl0"), acl0);
  });
}

// Redirect entries in ACL tables resolve exactly like legacy-map entries:
// disabled while the nexthop is unreachable, resolved once a covering
// route is programmed, and cleared when it is withdrawn.
TYPED_TEST(AclNexthopHandlerTest, TableAclNextHopResolution) {
  this->updateState(
      "TableAclNextHop", [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addAclToTable(
            state, "table0", kAclName, this->getAclNexthopIps(1));
      });
  this->verifyResolvedNexthopsInTableAclAction(
      "table0", kAclName, kEmptyNexthopSet);

  auto matchingPrefixes = this->getMatchingPrefixes(1);
  auto nexthopIps = this->getResolvedNexthops(2);
  auto longestPrefix = this->makePrefix(matchingPrefixes[0]);
  RouteNextHopSet nexthops =
      makeResolvedNextHops(nexthopIps, UCMP_DEFAULT_WEIGHT);
  this->addRoute(longestPrefix, nexthops);
  this->verifyResolvedNexthopsInTableAclAction("table0", kAclName, nexthops);

  this->delRoute(longestPrefix);
  this->verifyResolvedNexthopsInTableAclAction(
      "table0", kAclName, kEmptyNexthopSet);
}

// Only the entry whose resolution changes is rewritten; its table neighbor
// keeps the same node, so the HW layer does not reprogram it.
TYPED_TEST(AclNexthopHandlerTest, TableAclUnchangedEntryKeepsIdentity) {
  auto aclNexthopIps = this->getAclNexthopIps(2);
  this->updateState(
      "TableAclNextHops", [=, this](const std::shared_ptr<SwitchState>& state) {
        auto newState =
            this->addAclToTable(state, "table0", "acl0", {aclNexthopIps[0]});
        return this->addAclToTable(
            newState, "table0", "acl1", {aclNexthopIps[1]}, {}, 1);
      });

  auto matchingPrefixes = this->getMatchingPrefixes(2);
  auto nexthopIps = this->getResolvedNexthops(2);
  auto prefix0 = this->makePrefix(matchingPrefixes[0]);
  auto prefix1 = this->makePrefix(matchingPrefixes[1]);
  RouteNextHopSet nexthops0 =
      makeResolvedNextHops({nexthopIps[0]}, UCMP_DEFAULT_WEIGHT);
  RouteNextHopSet nexthops1 =
      makeResolvedNextHops({nexthopIps[1]}, UCMP_DEFAULT_WEIGHT);

  this->addRoute(prefix0, nexthops0);
  this->verifyResolvedNexthopsInTableAclAction("table0", "acl0", nexthops0);
  this->verifyResolvedNexthopsInTableAclAction(
      "table0", "acl1", kEmptyNexthopSet);

  std::shared_ptr<AclEntry> acl0;
  this->verifyStateUpdate(
      [&]() { acl0 = this->getTableAcl("table0", "acl0"); });
  this->addRoute(prefix1, nexthops1);
  this->verifyResolvedNexthopsInTableAclAction("table0", "acl1", nexthops1);
  this->verifyStateUpdate(
      [&]() { EXPECT_EQ(this->getTableAcl("table0", "acl0"), acl0); });

  std::shared_ptr<AclEntry> acl1;
  this->verifyStateUpdate(
      [&]() { acl1 = this->getTableAcl("table0", "acl1"); });
  this->delRoute(prefix0);
  this->verifyResolvedNexthopsInTableAclAction(
      "table0", "acl0", kEmptyNexthopSet);
  this->verifyStateUpdate(
      [&]() { EXPECT_EQ(this->getTableAcl("table0", "acl1"), acl1); });
}

TYPED_TEST(AclNexthopHandlerTest, TunnelEncapNexthopSkipsResolution) {
  this->updateState(
      "TunnelEncapAcl", [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addTunnelEncapAcl(
            state, kAclName, "2401::1", "encapTunnel0");
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto aclEntry = state->getAcls()->getNodeIf(kAclName);
    EXPECT_NE(aclEntry, nullptr);
    EXPECT_NE(aclEntry->isEnabled(), std::optional<bool>(false));
    const auto& action = aclEntry->getAclAction();
    auto matchAction = MatchAction::fromThrift(action->toThrift());
    auto resolvedNexthopSet = matchAction.getRedirectToNextHop().value().second;
    EXPECT_TRUE(resolvedNexthopSet.empty());
  });
}

} // namespace facebook::fboss
