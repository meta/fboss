// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/RemoteIntfRouteAuditor.h"

#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/SwitchState.h"

#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include <chrono>

namespace facebook::fboss {
namespace {

const auto kVrf = RouterID(0);
const auto kVrf2 = RouterID(1);
const auto kRemoteSwitchId = SwitchID(1);
const auto kLocalSwitchId = SwitchID(0);

const std::map<int64_t, cfg::SwitchInfo>& testSwitchInfo() {
  static const std::map<int64_t, cfg::SwitchInfo> info = []() {
    std::map<int64_t, cfg::SwitchInfo> m;
    cfg::SwitchInfo local;
    local.switchType() = cfg::SwitchType::VOQ;
    local.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
    local.switchIndex() = 0;
    m.emplace(kLocalSwitchId, local);
    cfg::SwitchInfo remote;
    remote.switchType() = cfg::SwitchType::VOQ;
    remote.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
    remote.switchIndex() = 1;
    m.emplace(kRemoteSwitchId, remote);
    return m;
  }();
  return info;
}

const SwitchIdScopeResolver* scopeResolver() {
  static const SwitchIdScopeResolver kResolver(testSwitchInfo());
  return &kResolver;
}

HwSwitchMatcher remoteMatcher() {
  return HwSwitchMatcher(std::unordered_set<SwitchID>{kRemoteSwitchId});
}

HwSwitchMatcher localMatcher() {
  return HwSwitchMatcher(std::unordered_set<SwitchID>{kLocalSwitchId});
}

std::shared_ptr<Interface> makeRif(InterfaceID id, folly::CIDRNetwork addr) {
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

void addRemoteRif(
    std::shared_ptr<SwitchState>& state,
    InterfaceID id,
    folly::CIDRNetwork addr) {
  auto remoteIntfs = state->getRemoteInterfaces()->modify(&state);
  remoteIntfs->addNode(makeRif(id, addr), remoteMatcher());
}

void addLocalRif(
    std::shared_ptr<SwitchState>& state,
    InterfaceID id,
    folly::CIDRNetwork addr) {
  auto intfs = state->getInterfaces()->modify(&state);
  intfs->addNode(makeRif(id, addr), localMatcher());
}

void programRemoteIntfRoute(
    RoutingInformationBase& rib,
    std::shared_ptr<SwitchState>& state,
    folly::CIDRNetwork prefix,
    InterfaceID intf,
    folly::IPAddress addr,
    RouterID vrf = kVrf) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes toAdd;
  toAdd[vrf][prefix] = std::make_pair(intf, addr);
  rib.updateRemoteInterfaceRoutes(
      scopeResolver(),
      toAdd,
      /*toDel=*/{},
      &ribToSwitchStateUpdate,
      static_cast<void*>(&state));
}

class AuditRemoteInterfaceRoutesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state_ = std::make_shared<SwitchState>();
    state_->publish();
    rib_ = std::make_unique<RoutingInformationBase>();
    rib_->ensureVrf(kVrf);
  }

  void addConsistentRemoteRif(InterfaceID id, folly::CIDRNetwork addr) {
    addRemoteRif(state_, id, addr);
    programRemoteIntfRoute(
        *rib_, state_, addr, id, folly::IPAddress(addr.first));
  }

  std::shared_ptr<SwitchState> state_;
  std::unique_ptr<RoutingInformationBase> rib_;
};

const auto kPrefix1 = folly::IPAddress::createNetwork("2001:db8:1::1/127");
const auto kPrefix2 = folly::IPAddress::createNetwork("2001:db8:2::1/127");
const auto kPrefix3 = folly::IPAddress::createNetwork("2001:db8:3::1/127");
const auto kIntf1 = InterfaceID(101);
const auto kIntf2 = InterfaceID(102);
const auto kIntf3 = InterfaceID(103);

TEST_F(AuditRemoteInterfaceRoutesTest, NoDriftReturnsEmpty) {
  addConsistentRemoteRif(kIntf1, kPrefix1);
  addConsistentRemoteRif(kIntf2, kPrefix2);

  auto audit = auditRemoteInterfaceRoutes(state_);

  EXPECT_TRUE(audit.noMismatches());
  EXPECT_EQ(audit.totalMismatchCount(), 0);
}

TEST_F(AuditRemoteInterfaceRoutesTest, DetectsMissing) {
  addConsistentRemoteRif(kIntf1, kPrefix1);
  addConsistentRemoteRif(kIntf2, kPrefix2);
  addRemoteRif(state_, kIntf3, kPrefix3);

  auto audit = auditRemoteInterfaceRoutes(state_);

  EXPECT_EQ(audit.totalMismatchCount(), 1);
  ASSERT_EQ(audit.missing.size(), 1);
  ASSERT_EQ(audit.missing.at(kVrf).size(), 1);
  EXPECT_EQ(audit.missing.at(kVrf).begin()->first, kPrefix3);
  EXPECT_EQ(audit.missing.at(kVrf).begin()->second.first, kIntf3);
  EXPECT_TRUE(audit.extra.empty());
}

TEST_F(AuditRemoteInterfaceRoutesTest, DetectsExtra) {
  addConsistentRemoteRif(kIntf1, kPrefix1);
  addConsistentRemoteRif(kIntf2, kPrefix2);
  programRemoteIntfRoute(
      *rib_, state_, kPrefix3, kIntf3, folly::IPAddress(kPrefix3.first));

  auto audit = auditRemoteInterfaceRoutes(state_);

  EXPECT_EQ(audit.totalMismatchCount(), 1);
  ASSERT_EQ(audit.extra.size(), 1);
  ASSERT_EQ(audit.extra.at(kVrf).size(), 1);
  EXPECT_EQ(audit.extra.at(kVrf).front().first, kPrefix3);
  EXPECT_EQ(audit.extra.at(kVrf).front().second, kIntf3);
  EXPECT_TRUE(audit.missing.empty());
}

TEST_F(AuditRemoteInterfaceRoutesTest, DetectsIPSwap) {
  addRemoteRif(state_, kIntf1, kPrefix1);
  programRemoteIntfRoute(
      *rib_, state_, kPrefix1, kIntf2, folly::IPAddress(kPrefix1.first));

  auto audit = auditRemoteInterfaceRoutes(state_);

  ASSERT_EQ(audit.missing.size(), 1);
  ASSERT_EQ(audit.missing.at(kVrf).size(), 1);
  EXPECT_EQ(audit.missing.at(kVrf).begin()->first, kPrefix1);
  EXPECT_EQ(audit.missing.at(kVrf).begin()->second.first, kIntf1);
  // Same prefix is suppressed from `extra` so the re-add isn't stomped.
  EXPECT_TRUE(audit.extra.empty());
  EXPECT_EQ(audit.totalMismatchCount(), 1);
}

TEST_F(AuditRemoteInterfaceRoutesTest, SkipsLocalMirrorRifs) {
  // NPU0/NPU1 mirror: RIF in both maps is treated as local, not remote.
  addLocalRif(state_, kIntf1, kPrefix1);
  addRemoteRif(state_, kIntf1, kPrefix1);
  auto audit = auditRemoteInterfaceRoutes(state_);

  EXPECT_TRUE(audit.noMismatches());
}

TEST_F(AuditRemoteInterfaceRoutesTest, EmptyStateReturnsEmpty) {
  auto audit = auditRemoteInterfaceRoutes(state_);

  EXPECT_TRUE(audit.noMismatches());
  EXPECT_EQ(audit.totalMismatchCount(), 0);
  EXPECT_EQ(audit.malformedRemoteIntfRoute, 0);
}

TEST_F(AuditRemoteInterfaceRoutesTest, MultiplePrefixesSameVrf) {
  // 2 consistent + 1 missing + 1 extra all in kVrf
  addConsistentRemoteRif(kIntf1, kPrefix1);
  addConsistentRemoteRif(kIntf2, kPrefix2);
  addRemoteRif(state_, kIntf3, kPrefix3);
  const auto kPrefix4 = folly::IPAddress::createNetwork("2001:db8:4::1/127");
  programRemoteIntfRoute(
      *rib_,
      state_,
      kPrefix4,
      InterfaceID(104),
      folly::IPAddress(kPrefix4.first));

  auto audit = auditRemoteInterfaceRoutes(state_);

  ASSERT_EQ(audit.missing.size(), 1);
  ASSERT_EQ(audit.extra.size(), 1);
  EXPECT_EQ(audit.missing.at(kVrf).size(), 1);
  EXPECT_EQ(audit.extra.at(kVrf).size(), 1);
  EXPECT_EQ(audit.totalMismatchCount(), 2);
}

TEST_F(AuditRemoteInterfaceRoutesTest, MultipleVrfs) {
  rib_->ensureVrf(kVrf2);
  // kVrf: missing kPrefix1
  addRemoteRif(state_, kIntf1, kPrefix1);
  // kVrf2: extra kPrefix2 (no RIF)
  programRemoteIntfRoute(
      *rib_, state_, kPrefix2, kIntf2, folly::IPAddress(kPrefix2.first), kVrf2);

  auto audit = auditRemoteInterfaceRoutes(state_);

  EXPECT_EQ(audit.totalMismatchCount(), 2);
  ASSERT_EQ(audit.missing.size(), 1);
  EXPECT_EQ(audit.missing.at(kVrf).size(), 1);
  ASSERT_EQ(audit.extra.size(), 1);
  EXPECT_EQ(audit.extra.at(kVrf2).front().first, kPrefix2);
  EXPECT_EQ(audit.extra.at(kVrf2).front().second, kIntf2);
}

TEST_F(AuditRemoteInterfaceRoutesTest, MalformedCountIsZeroOnCleanFib) {
  addConsistentRemoteRif(kIntf1, kPrefix1);

  auto audit = auditRemoteInterfaceRoutes(state_);

  EXPECT_EQ(audit.malformedRemoteIntfRoute, 0);
}

TEST_F(AuditRemoteInterfaceRoutesTest, DetectsDuplicateAcrossRifs) {
  // Same prefix configured on two distinct remote RIFs — state corruption
  // the audit must surface (would otherwise be silently dropped by
  // emplace).
  addRemoteRif(state_, kIntf1, kPrefix1);
  addRemoteRif(state_, kIntf2, kPrefix1);

  auto audit = auditRemoteInterfaceRoutes(state_);

  EXPECT_EQ(audit.duplicateAcrossRifsCount(), 1);
  ASSERT_EQ(audit.duplicateAcrossRifs.size(), 1);
  EXPECT_EQ(*audit.duplicateAcrossRifs.at(kVrf).begin(), kPrefix1);
}

// SEV S648783, transient after update i (cross-device IP swap):
//   switch-1.intf1 IP-A -> IP-B has landed; switch-2.intf2 IP-B -> IP-A has
//   not. State now has IP-B on both intf1 and intf2. FIB was rewritten in
//   place during update i, so prefix(IP-B) -> intf1.
// Expected audit: no missing/extra drift (first-writer-wins in
// computeExpectedRemoteIntfRoutes picks intf1, which matches the FIB), but
// duplicateAcrossRifs surfaces the transient corruption.
TEST_F(AuditRemoteInterfaceRoutesTest, SevTransientAfterUpdateOne) {
  addRemoteRif(state_, kIntf1, kPrefix1);
  addRemoteRif(state_, kIntf2, kPrefix1);
  programRemoteIntfRoute(
      *rib_, state_, kPrefix1, kIntf1, folly::IPAddress(kPrefix1.first));

  auto audit = auditRemoteInterfaceRoutes(state_);

  EXPECT_TRUE(audit.noMismatches());
  EXPECT_EQ(audit.duplicateAcrossRifsCount(), 1);
  EXPECT_EQ(*audit.duplicateAcrossRifs.at(kVrf).begin(), kPrefix1);
}

// SEV S648783, after update ii: cross-device IP swap completed, but the
// buggy per-update toDel removed prefix(IP-B) outright (the route belonged
// to intf1 by then; intf2's toDel didn't carry interface-ownership info).
// State has the correct post-swap mapping; FIB is missing IP-B entirely.
// Expected audit: pure missing for prefix(IP-B) on intf1, no extra, no
// duplicates. IP-swap suppression does not fire here — there is no stale
// FIB entry to suppress.
TEST_F(AuditRemoteInterfaceRoutesTest, SevLeakedPrefixAfterUpdateTwo) {
  // Post-swap state: intf1 owns kPrefix2, intf2 owns kPrefix1.
  addRemoteRif(state_, kIntf1, kPrefix2);
  addRemoteRif(state_, kIntf2, kPrefix1);
  // FIB: only kPrefix1 is programmed; kPrefix2 was leaked by the bug.
  programRemoteIntfRoute(
      *rib_, state_, kPrefix1, kIntf2, folly::IPAddress(kPrefix1.first));

  auto audit = auditRemoteInterfaceRoutes(state_);

  ASSERT_EQ(audit.missing.size(), 1);
  ASSERT_EQ(audit.missing.at(kVrf).size(), 1);
  EXPECT_EQ(audit.missing.at(kVrf).begin()->first, kPrefix2);
  EXPECT_EQ(audit.missing.at(kVrf).begin()->second.first, kIntf1);
  EXPECT_TRUE(audit.extra.empty());
  EXPECT_EQ(audit.duplicateAcrossRifsCount(), 0);
}

// Bidirectional same-device IP swap: two RIFs on the device exchange
// addresses but the FIB still has the pre-swap mapping. Both prefixes
// appear in `missing` (correct intf from state) and would appear in
// `extra` (stale intf from FIB). IP-swap suppression keeps `extra` empty
// for both so RIB's add-before-delete ordering rewrites in place without
// stomping the re-add.
TEST_F(AuditRemoteInterfaceRoutesTest, BidirectionalSameDeviceIntfSwap) {
  // State: addresses swapped between intf1 and intf2.
  addRemoteRif(state_, kIntf1, kPrefix2);
  addRemoteRif(state_, kIntf2, kPrefix1);
  // FIB: pre-swap mapping still in place.
  programRemoteIntfRoute(
      *rib_, state_, kPrefix1, kIntf1, folly::IPAddress(kPrefix1.first));
  programRemoteIntfRoute(
      *rib_, state_, kPrefix2, kIntf2, folly::IPAddress(kPrefix2.first));

  auto audit = auditRemoteInterfaceRoutes(state_);

  ASSERT_EQ(audit.missing.size(), 1);
  EXPECT_EQ(audit.missing.at(kVrf).size(), 2);
  EXPECT_TRUE(audit.extra.empty());
  EXPECT_EQ(audit.totalMismatchCount(), 2);
  EXPECT_EQ(audit.duplicateAcrossRifsCount(), 0);
}

class AuditScaleStressTest : public AuditRemoteInterfaceRoutesTest {};

TEST_F(AuditScaleStressTest, FunctionalCorrectnessAtScale) {
  constexpr int kNumRifs = 5000;
  for (int i = 0; i < kNumRifs; ++i) {
    auto prefix =
        folly::IPAddress::createNetwork(fmt::format("2001:db8:{:x}::1/127", i));
    addConsistentRemoteRif(InterfaceID(1000 + i), prefix);
  }
  auto start = std::chrono::steady_clock::now();
  auto audit = auditRemoteInterfaceRoutes(state_);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
  XLOG(INFO) << "ScaleStress audit: N=" << kNumRifs << " duration=" << ms
             << " ms";
  EXPECT_TRUE(audit.noMismatches());
  EXPECT_EQ(audit.totalMismatchCount(), 0);
  EXPECT_EQ(audit.duplicateAcrossRifsCount(), 0);
}

} // namespace
} // namespace facebook::fboss
