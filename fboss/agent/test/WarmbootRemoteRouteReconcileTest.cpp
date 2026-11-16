// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/RemoteIntfRouteAuditor.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <fb303/ThreadCachedServiceData.h>
#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <gtest/gtest.h>

namespace facebook::fboss {

namespace {

const auto kTestVrf = RouterID(0);
const auto kRemoteSwitchId = SwitchID(1);
const auto kIntfA = InterfaceID(201);
const auto kIntfB = InterfaceID(202);
const auto kPrefixA = folly::IPAddress::createNetwork("2001:db8:a::1/127");
const auto kPrefixB = folly::IPAddress::createNetwork("2001:db8:b::1/127");

HwSwitchMatcher remoteMatcher() {
  return HwSwitchMatcher(std::unordered_set<SwitchID>{kRemoteSwitchId});
}

std::shared_ptr<Interface> makeRemoteRif(
    InterfaceID id,
    folly::CIDRNetwork addr) {
  auto name = fmt::format("rif{}", static_cast<int>(id));
  auto rif = std::make_shared<Interface>(
      id,
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece(name),
      folly::MacAddress("02:00:00:00:00:01"),
      9000,
      false,
      false,
      cfg::InterfaceType::SYSTEM_PORT);
  rif->setAddresses(Interface::Addresses{{addr.first, addr.second}});
  rif->setScope(cfg::Scope::GLOBAL);
  return rif;
}

void programRoute(
    RoutingInformationBase& rib,
    std::shared_ptr<SwitchState>& state,
    const SwitchIdScopeResolver* resolver,
    folly::CIDRNetwork prefix,
    InterfaceID intf) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes toAdd;
  toAdd[kTestVrf][prefix] =
      std::make_pair(intf, folly::IPAddress(prefix.first));
  rib.updateRemoteInterfaceRoutes(
      resolver,
      toAdd,
      /*toDel=*/{},
      &ribToSwitchStateUpdate,
      static_cast<void*>(&state));
}

} // namespace

class WarmbootRemoteRouteReconcileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_enable_remote_intf_route_reconcile = true;
    handle_ = createTestHandle();
    sw_ = handle_->getSw();
    rib_ = sw_->getRib();
    resolver_ = sw_->getScopeResolver();
    rib_->ensureVrf(kTestVrf);
  }

  // Apply `mutator` to clone the SwSwitch's current state, then push back via
  // updateStateBlocking so subsequent calls see the new state.
  void mutateState(std::function<void(std::shared_ptr<SwitchState>&)> mutator) {
    sw_->updateStateBlocking(
        "test setup",
        [mutator = std::move(mutator)](const std::shared_ptr<SwitchState>& in) {
          auto out = in->clone();
          mutator(out);
          return out;
        });
  }

  void addRemoteRif(InterfaceID id, folly::CIDRNetwork addr) {
    mutateState([id, addr](std::shared_ptr<SwitchState>& s) {
      auto remoteIntfs = s->getRemoteInterfaces()->modify(&s);
      remoteIntfs->addNode(makeRemoteRif(id, addr), remoteMatcher());
    });
  }

  void programInRib(folly::CIDRNetwork prefix, InterfaceID intf) {
    auto state = sw_->getState();
    programRoute(*rib_, state, resolver_, prefix, intf);
    sw_->updateStateBlocking(
        "test rib seed",
        [state](const std::shared_ptr<SwitchState>&) { return state; });
  }

  // Drives reconcileRemoteInterfaceRoutes (the free function, bypassing the
  // SwSwitch wrapper's bootType/VOQ guards) and publishes the result so
  // sw_->getState() reflects the reconciliation.
  std::shared_ptr<SwitchState> reconcile() {
    auto state = reconcileRemoteInterfaceRoutes(
        sw_->getState(), *rib_, *resolver_, *sw_->stats());
    sw_->updateStateBlocking(
        "test reconcile publish",
        [state](const std::shared_ptr<SwitchState>&) { return state; });
    return state;
  }

  int64_t inconsistencyCounter() const {
    // TLTimeseries values are buffered per-thread; flush before reading.
    fb303::ThreadCachedServiceData::get()->publishStats();
    return sw_->stats()->getWarmbootRemoteIntfRoutesInconsistency();
  }

  gflags::FlagSaver flagSaver_;
  std::unique_ptr<HwTestHandle> handle_;
  SwSwitch* sw_{};
  RoutingInformationBase* rib_{};
  const SwitchIdScopeResolver* resolver_{};
};

TEST_F(WarmbootRemoteRouteReconcileTest, NoDriftIsNoop) {
  addRemoteRif(kIntfA, kPrefixA);
  programInRib(kPrefixA, kIntfA);

  auto before = inconsistencyCounter();
  reconcile();

  EXPECT_EQ(inconsistencyCounter(), before);
}

TEST_F(WarmbootRemoteRouteReconcileTest, MissingRouteIsReadded) {
  // RIF exists but its /127 isn't programmed in the FIB — the SEV S648783
  // failure mode.
  addRemoteRif(kIntfA, kPrefixA);

  auto audit = auditRemoteInterfaceRoutes(sw_->getState());
  ASSERT_FALSE(audit.noMismatches());

  auto before = inconsistencyCounter();
  reconcile();

  EXPECT_EQ(inconsistencyCounter(), before + 1);
  // After reconcile, audit should be clean.
  auto post = auditRemoteInterfaceRoutes(sw_->getState());
  EXPECT_TRUE(post.noMismatches());
}

TEST_F(WarmbootRemoteRouteReconcileTest, ExtraRouteIsDeleted) {
  // FIB has a REMOTE_INTERFACE_ROUTE entry whose RIF is not in the state.
  programInRib(kPrefixA, kIntfA);

  auto audit = auditRemoteInterfaceRoutes(sw_->getState());
  ASSERT_EQ(audit.totalMismatchCount(), 1);

  auto before = inconsistencyCounter();
  reconcile();

  EXPECT_EQ(inconsistencyCounter(), before + 1);
  auto post = auditRemoteInterfaceRoutes(sw_->getState());
  EXPECT_TRUE(post.noMismatches());
}

TEST_F(WarmbootRemoteRouteReconcileTest, IPSwapIsReconciled) {
  // RIF says prefix belongs to kIntfA, but FIB has it under kIntfB — the
  // S648783 IP-swap class of failure.
  addRemoteRif(kIntfA, kPrefixA);
  programInRib(kPrefixA, kIntfB);

  auto audit = auditRemoteInterfaceRoutes(sw_->getState());
  ASSERT_EQ(audit.totalMismatchCount(), 1);

  auto before = inconsistencyCounter();
  reconcile();

  EXPECT_EQ(inconsistencyCounter(), before + 1);
  auto post = auditRemoteInterfaceRoutes(sw_->getState());
  EXPECT_TRUE(post.noMismatches());
}

TEST_F(WarmbootRemoteRouteReconcileTest, ColdBootGuardIsRespected) {
  ASSERT_EQ(sw_->getBootType(), BootType::COLD_BOOT)
      << "test relies on createTestHandle defaulting to cold boot";
  addRemoteRif(kIntfA, kPrefixA);
  ASSERT_FALSE(auditRemoteInterfaceRoutes(sw_->getState()).noMismatches());

  auto before = inconsistencyCounter();
  sw_->reconcileRemoteInterfaceRoutesOnWarmboot(sw_->getState());

  EXPECT_EQ(inconsistencyCounter(), before);
  // RIB remains drifted — the guard prevented any reconciliation.
  EXPECT_FALSE(auditRemoteInterfaceRoutes(sw_->getState()).noMismatches());
}

TEST_F(WarmbootRemoteRouteReconcileTest, MultiPrefixDriftCountIsAggregate) {
  // One missing + one extra (different prefixes) → counter increments by 2.
  addRemoteRif(kIntfA, kPrefixA);
  programInRib(kPrefixB, kIntfB);

  auto audit = auditRemoteInterfaceRoutes(sw_->getState());
  ASSERT_EQ(audit.totalMismatchCount(), 2);

  auto before = inconsistencyCounter();
  reconcile();

  EXPECT_EQ(inconsistencyCounter(), before + 2);
  EXPECT_TRUE(auditRemoteInterfaceRoutes(sw_->getState()).noMismatches());
}

} // namespace facebook::fboss
