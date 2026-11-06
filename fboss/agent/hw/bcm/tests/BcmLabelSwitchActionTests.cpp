// Copyright 2004-present Facebook. All Rights Reserved.

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include "fboss/agent/hw/bcm/BcmLabelSwitchAction.h"

#include "fboss/agent/hw/bcm/BcmHost.h"
#include "fboss/agent/hw/bcm/tests/BcmTest.h"

#include "fboss/agent/hw/test/ConfigFactory.h"

#include <gtest/gtest.h>
#include "fboss/agent/hw/bcm/BcmLabelSwitchingUtils.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/HwSwitchEnsembleRouteUpdateWrapper.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/LabelForwardingUtils.h"

using namespace ::testing;

using namespace facebook::fboss;

class BcmLabelSwitchActionTest : public BcmTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    return utility::twoL3IntfConfig(
        getHwSwitch(), masterLogicalPortIds()[0], masterLogicalPortIds()[1]);
  }

  void SetUp() override {
    BcmTest::SetUp();
    ecmpHelper_ = std::make_unique<utility::EcmpSetupTargetedPorts6>(
        getProgrammedState(),
        getHwSwitch()->needL2EntryForNeighbor(),
        RouterID(0));
  }

  std::shared_ptr<SwitchState> initState() {
    auto state = applyNewConfig(initialConfig());
    setupTestLabelForwardingEntries();
    return state;
  }

  void setupTestLabelForwardingEntries() {
    entries_.push_back(
        std::make_shared<LabelForwardingEntry>(LabelForwardingEntry::makeThrift(
            5001,
            ClientID::OPENR,
            util::getSwapLabelNextHopEntry(
                AdminDistance::DIRECTLY_CONNECTED,
                InterfaceID(utility::kBaseVlanId),
                {folly::IPAddress("1.1.1.1")}))));
    entries_.push_back(
        std::make_shared<LabelForwardingEntry>(LabelForwardingEntry::makeThrift(
            5002,
            ClientID::OPENR,
            util::getPhpLabelNextHopEntry(
                AdminDistance::DIRECTLY_CONNECTED,
                InterfaceID(utility::kBaseVlanId),
                {folly::IPAddress("1.1.1.1")}))));
    entries_.push_back(
        std::make_shared<LabelForwardingEntry>(LabelForwardingEntry::makeThrift(
            5003,
            ClientID::OPENR,
            util::getPopLabelNextHopEntry(
                AdminDistance::DIRECTLY_CONNECTED,
                InterfaceID(utility::kBaseVlanId),
                {folly::IPAddress("1.1.1.1")}))));
    entries_.push_back(
        std::make_shared<LabelForwardingEntry>(LabelForwardingEntry::makeThrift(
            5004,
            ClientID::OPENR,
            util::getPushLabelNextHopEntry(
                AdminDistance::DIRECTLY_CONNECTED,
                InterfaceID(utility::kBaseVlanId),
                {folly::IPAddress("1.1.1.1")}))));
    entries_.push_back(
        std::make_shared<LabelForwardingEntry>(LabelForwardingEntry::makeThrift(
            5005,
            ClientID::OPENR,
            util::getPushLabelNextHopEntry(
                AdminDistance::DIRECTLY_CONNECTED,
                InterfaceID(utility::kBaseVlanId)))));
    LabelNextHopEntry nexthop{
        LabelNextHopEntry::Action::TO_CPU, AdminDistance::MAX_ADMIN_DISTANCE};
    entries_.push_back(
        std::make_shared<LabelForwardingEntry>(
            LabelForwardingEntry::makeThrift(5006, ClientID::OPENR, nexthop)));
  }

  void addAllTestLabelForwardingEntries() {
    auto oldState = initState();
    auto newState = oldState->clone();
    ecmpHelper_->resolveNextHops(
        newState, {PortDescriptor(masterLogicalPortIds()[0])});
    applyNewState(newState);
    for (auto& entry : entries_) {
      addMplsRoute(entry);
    }
  }

  void removeAllTestLabelForwardingEntries() {
    for (auto& entry : entries_) {
      removeMplsRoute(entry);
    }
  }

  void verifyTestLabelForwardingEntry(
      const std::shared_ptr<LabelForwardingEntry>& mplsEntry) {
    auto state = getHwSwitchEnsemble()->getProgrammedState();
    auto entry = state->getLabelForwardingInformationBase()->getNodeIf(
        mplsEntry->getID());
    bcm_mpls_tunnel_switch_t info;
    bcm_mpls_tunnel_switch_t_init(&info);
    info.label = entry->getID();
    info.port = BCM_GPORT_INVALID;
    auto rv = bcm_mpls_tunnel_switch_get(getHwSwitch()->getUnit(), &info);
    LabelForwardingAction::LabelForwardingType labelForwardingType{
        LabelForwardingAction::LabelForwardingType::NOOP};
    auto action = entry->getForwardInfo().getAction();
    if (action != LabelNextHopEntry::Action::DROP &&
        action != LabelNextHopEntry::Action::TO_CPU) {
      labelForwardingType = entry->getForwardInfo()
                                .getNextHopSet()
                                .begin()
                                ->labelForwardingAction()
                                ->type();
    }
    ASSERT_EQ(rv, BCM_E_NONE);
    EXPECT_EQ(info.label, static_cast<int32_t>(entry->getID()));
    EXPECT_EQ(
        info.action,
        utility::getLabelSwitchAction(
            entry->getForwardInfo().getAction(), labelForwardingType));

    if (info.action == BCM_MPLS_SWITCH_ACTION_POP) {
      // no egress_if or next hop for "POP"
      return;
    }
    if (info.action != BCM_MPLS_SWITCH_ACTION_PHP) {
      // for push & swap, use labeled egresses
      auto reference = getHwSwitch()->getMultiPathNextHopTable()->getNextHop(
          BcmMultiPathNextHopKey(
              0, entry->getForwardInfo().normalizedNextHops(), std::nullopt));
      EXPECT_EQ(info.egress_if, reference->getEgressId());
    } else {
      int egressId;
      // for php reuse L3 egresses
      if (action == LabelNextHopEntry::Action::DROP) {
        egressId = getHwSwitch()->getDropEgressId();
      } else if (action == LabelNextHopEntry::Action::TO_CPU) {
        egressId = getHwSwitch()->getToCPUEgressId();
      } else {
        auto reference = getHwSwitch()->getMultiPathNextHopTable()->getNextHop(
            BcmMultiPathNextHopKey(
                0,
                utility::stripLabelForwarding(
                    entry->getForwardInfo().normalizedNextHops()),
                std::nullopt));
        egressId = reference->getEgressId();
      }
      EXPECT_EQ(info.egress_if, egressId);
    }
  }

  void verifyTestLabelForwardingEntryNotFound(
      const std::shared_ptr<LabelForwardingEntry>& entry) {
    bcm_mpls_tunnel_switch_t info;
    bcm_mpls_tunnel_switch_t_init(&info);
    info.label = entry->getID();
    info.port = BCM_GPORT_INVALID;
    auto rv = bcm_mpls_tunnel_switch_get(getHwSwitch()->getUnit(), &info);
    ASSERT_EQ(rv, BCM_E_NOT_FOUND);
  }

  void verifyAllTestLabelForwardingEntries() {
    for (const auto& entry : entries_) {
      verifyTestLabelForwardingEntry(entry);
    }
  }

  void verifyAllTestLabelForwardingEntriesNotFound() {
    for (const auto& entry : entries_) {
      verifyTestLabelForwardingEntryNotFound(entry);
    }
  }

  void addL3Route(
      const folly::IPAddressV4& network,
      uint8_t mask,
      const LabelForwardingEntry& entry) {
    RouteNextHopSet nexthops;
    for (const auto& nhop : entry.getForwardInfo().getNextHopSet()) {
      nexthops.insert(UnresolvedNextHop(nhop.addr(), nhop.weight()));
    }
    auto updater = getHwSwitchEnsemble()->getRouteUpdater();

    updater.addRoute(
        RouterID(0),
        network,
        mask,
        ClientID(786),
        RouteNextHopEntry(std::move(nexthops), AdminDistance::STATIC_ROUTE));
    updater.program();
  }

  void addL3Routes() {
    std::vector<folly::IPAddressV4> addrs{
        folly::IPAddressV4("10.1.101.10"),
        folly::IPAddressV4("10.1.102.10"),
        folly::IPAddressV4("10.1.103.10"),
        folly::IPAddressV4("10.1.104.10"),
    };
    for (auto i = 0; i < 4; i++) {
      addL3Route(addrs[i], 24, *entries_[i]);
    }
  }

  void addMplsRoute(std::shared_ptr<LabelForwardingEntry> entry) {
    MultiLabelForwardingInformationBase::resolve(entry);
    auto updater = getHwSwitchEnsemble()->getRouteUpdater();
    MplsRoute route;
    route.topLabel() = entry->getID();
    route.nextHops() =
        util::fromRouteNextHopSet(entry->getForwardInfo().getNextHopSet());
    updater.addRoute(entry->getBestEntry().first, route);
    updater.program();
  }

  void removeMplsRoute(std::shared_ptr<LabelForwardingEntry> entry) {
    auto updater = getHwSwitchEnsemble()->getRouteUpdater();
    updater.delRoute(entry->getID(), ClientID(786));
    updater.program();
  }
  std::vector<std::shared_ptr<LabelForwardingEntry>> entries_;
  std::unique_ptr<utility::EcmpSetupTargetedPorts6> ecmpHelper_;
};

TEST_F(BcmLabelSwitchActionTest, addLabelSwitchAction) {
  if (!isSupported(HwAsic::Feature::MPLS)) {
    GTEST_SKIP();
  }
  auto setup = [=, this]() { addAllTestLabelForwardingEntries(); };
  auto verify = [=, this]() { verifyAllTestLabelForwardingEntries(); };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmLabelSwitchActionTest, addLabelSwitchActionWithL3Routes) {
  if (!isSupported(HwAsic::Feature::MPLS) ||
      !isSupported(HwAsic::Feature::MPLS_ECMP)) {
    GTEST_SKIP();
  }
  auto setup = [=, this]() {
    addAllTestLabelForwardingEntries();
    addL3Routes();
  };
  auto verify = [=, this]() { verifyAllTestLabelForwardingEntries(); };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmLabelSwitchActionTest, removeLabelSwitchAction) {
  if (!isSupported(HwAsic::Feature::MPLS)) {
    GTEST_SKIP();
  }
  auto setup = [=, this]() {
    addAllTestLabelForwardingEntries();
    removeAllTestLabelForwardingEntries();
  };
  auto verify = [=, this]() { verifyAllTestLabelForwardingEntriesNotFound(); };

  verifyAcrossWarmBoots(setup, verify);
}
