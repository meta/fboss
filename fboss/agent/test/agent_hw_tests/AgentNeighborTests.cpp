// Copyright 2004-present Facebook. All Rights Reserved.

#include <gtest/gtest.h>
#include <algorithm>

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/NeighborTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"

#include "fboss/lib/CommonUtils.h"

/*
 * Adding a new flag which can be used to setup ports in non-loopback
 * mode. Some vendors have HwTests from OSS running in their environment
 * against simulator. This new option will provide a control plane
 * (from the test binary) to program forwarding plane (setup neighbors,
 * bringup ports etc.) of simulator. Once the dataplane is programmed,
 * network namespaces could be used to setup an environment to inject
 * traffic to the simulator. This can be used for throughput test of
 * ASIC simulator in the same env vendors use for regular HwTests.
 */
DEFINE_bool(disable_loopback, false, "Disable loopback on test ports");

using namespace ::testing;

namespace {
facebook::fboss::utility::NeighborInfo getNeighborInfo(
    const facebook::fboss::InterfaceID& interfaceId,
    const folly::IPAddress& ip,
    facebook::fboss::AgentEnsemble& ensemble) {
  auto state = ensemble.getProgrammedState();
  auto intf = state->getInterfaces()->getNodeIf(interfaceId);
  if (!intf) {
    throw facebook::fboss::FbossError("no such interface ", interfaceId);
  }
  auto switchId =
      ensemble.getSw()->getScopeResolver()->scope(intf, state).switchId();
  auto client = ensemble.getHwAgentTestClient(switchId);
  facebook::fboss::IfAndIP neighbor;
  neighbor.interfaceID() = interfaceId;
  neighbor.ip() = facebook::network::toBinaryAddress(ip);
  facebook::fboss::utility::NeighborInfo neighborInfo;
  client->sync_getNeighborInfo(neighborInfo, std::move(neighbor));
  return neighborInfo;
}
} // namespace

namespace facebook::fboss {

// The typed test parameter now encodes only the Port-vs-Trunk axis. Address
// family (v4/v6) is no longer a type parameter; each test exercises both
// families in a single setup/verify cycle via the template member helpers
// below.
struct PortNeighbor {
  static constexpr bool isTrunk = false;
};
struct TrunkNeighbor {
  static constexpr bool isTrunk = true;
};

const facebook::fboss::AggregatePortID kAggID{1};

using NeighborTypes = ::testing::Types<PortNeighbor, TrunkNeighbor>;

template <typename NeighborT>
class AgentNeighborTest : public AgentHwTest {
 protected:
  static auto constexpr programToTrunk = NeighborT::isTrunk;
  template <typename AddrT>
  using NeighborTableT = std::conditional_t<
      std::is_same_v<AddrT, folly::IPAddressV4>,
      ArpTable,
      NdpTable>;

  template <typename AddrT>
  static AddrT getNeighborAddressForFamily() {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return folly::IPAddressV4("1.0.0.2");
    } else {
      return folly::IPAddressV6("1::2");
    }
  }

  template <typename AddrT>
  static AddrT getLinkLocalNeighborAddressForFamily() {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return folly::IPAddressV4("192.168.0.1");
    } else {
      return folly::IPAddressV6("fe80::2ae7:1dff:fe73:f21a");
    }
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    std::vector<ProductionFeature> features = {
        ProductionFeature::L3_FORWARDING};
    features.push_back(ProductionFeature::INTERFACE_NEIGHBOR_TABLE);

    if (programToTrunk) {
      features.push_back(ProductionFeature::LAG);
    }
    return features;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto switchId = getSwitchIdUnderTest(ensemble);
    auto asic = ensemble.getSw()->getHwAsicTable()->getHwAsic(switchId);
    auto ports = ensemble.masterLogicalPortIds({switchId});
    std::vector<PortID> members;
    std::vector<utility::AggregatePortInfo> aggPorts;
    if (programToTrunk) {
      // Keep member size to be less than/equal to HW limitation, but first add
      // the two ports for testing. Only use ports from masterLogicalPortIds()
      // which are scoped to the test switch ID, to avoid adding ports from
      // other switches in multi-NPU setups.
      // Only use INTERFACE_PORT ports for aggPorts
      auto masterPorts = ensemble.masterLogicalInterfacePortIds({switchId});
      std::set<int> portSet{masterPorts[0], masterPorts[1]};
      int idx = 0;
      while (portSet.size() < std::min(
                                  asic->getMaxLagMemberSize(),
                                  static_cast<uint32_t>(masterPorts.size()))) {
        portSet.insert(masterPorts[idx]);
        idx++;
      }
      for (auto member : portSet) {
        members.emplace_back(member);
      }
      aggPorts = {{kAggID, members}};
    }
    return utility::oneAggregatePortPerInterfaceConfig(
        ensemble.getSw(), programToTrunk ? members : ports, aggPorts);
  }

  InterfaceID kIntfID() const {
    auto switchType =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getSwitchType();
    if (switchType == cfg::SwitchType::NPU) {
      auto portId = portIdsForTest()[0];
      auto switchId = getAgentEnsemble()
                          ->getSw()
                          ->getScopeResolver()
                          ->scope(portId)
                          .switchId();
      return utility::firstInterfaceIDWithPorts(getProgrammedState(), switchId);
    } else if (switchType == cfg::SwitchType::VOQ) {
      CHECK(!programToTrunk) << " Trunks not supported yet on VOQ switches";
      auto portId = this->portDescriptor().phyPortID();
      return InterfaceID((*getProgrammedState()
                               ->getPorts()
                               ->getNodeIf(portId)
                               ->getInterfaceIDs()
                               .begin()));
    }
    XLOG(FATAL) << "Unexpected switch type " << static_cast<int>(switchType);
  }

  std::vector<PortID> portIdsForTest() const {
    if (FLAGS_hyper_port) {
      return masterLogicalHyperPortIds();
    }
    return masterLogicalInterfacePortIds();
  }

  PortDescriptor portDescriptor() const {
    if (programToTrunk) {
      return PortDescriptor(kAggID);
    }
    return PortDescriptor(portIdsForTest()[0]);
  }

  template <typename AddrT>
  auto getNeighborTable(std::shared_ptr<SwitchState> state) {
    auto switchType =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getSwitchType();

    if (switchType == cfg::SwitchType::VOQ ||
        switchType == cfg::SwitchType::NPU) {
      return state->getInterfaces()
          ->getNode(kIntfID())
          ->template getNeighborEntryTable<AddrT>()
          ->modify(kIntfID(), &state);
    }

    XLOG(FATAL) << "Unexpected switch type " << static_cast<int>(switchType);
  }
  template <typename AddrT>
  std::shared_ptr<SwitchState> addNeighbor(
      const std::shared_ptr<SwitchState>& inState,
      bool linkLocal = false) {
    auto ip = getNeighborAddress<AddrT>(linkLocal);
    auto outState{inState->clone()};
    auto neighborTable = getNeighborTable<AddrT>(outState);
    neighborTable->addPendingEntry(ip, kIntfID());
    return outState;
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> removeNeighbor(
      const std::shared_ptr<SwitchState>& inState,
      bool linkLocal = false) {
    auto ip = getNeighborAddress<AddrT>(linkLocal);
    auto outState{inState->clone()};

    auto neighborTable = getNeighborTable<AddrT>(outState);
    auto intf = outState->getInterfaces()->getNode(kIntfID());
    if (getSw()->needL2EntryForNeighbor() &&
        intf->getType() == cfg::InterfaceType::VLAN) {
      outState = utility::NeighborTestUtils::pruneMacEntryForDelNbrEntry(
          outState, intf->getVlanID(), neighborTable->getEntryIf(ip));
    }
    neighborTable->removeEntry(ip);
    return outState;
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> resolveNeighbor(
      const std::shared_ptr<SwitchState>& inState,
      std::optional<cfg::AclLookupClass> lookupClass = std::nullopt,
      bool linkLocal = false) {
    auto ip = getNeighborAddress<AddrT>(linkLocal);
    auto outState{inState->clone()};
    auto neighborTable = getNeighborTable<AddrT>(outState);
    auto switchType =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics())
            ->getSwitchType();
    PortDescriptor port = portDescriptor();
    SystemPortID systemPortID;
    // VOQ switches stores system port ID in switch state
    if (switchType == cfg::SwitchType::VOQ) {
      systemPortID = SystemPortID(kIntfID());
      port = PortDescriptor(SystemPortID(systemPortID));
    }

    neighborTable->updateEntry(
        ip,
        kNeighborMac,
        port,
        kIntfID(),
        NeighborState::REACHABLE,
        lookupClass);

    auto intf = outState->getInterfaces()->getNode(kIntfID());
    if (getSw()->needL2EntryForNeighbor() &&
        intf->getType() == cfg::InterfaceType::VLAN) {
      outState = utility::NeighborTestUtils::addMacEntryForNewNbrEntry(
          outState, intf->getVlanID(), neighborTable->getEntryIf(ip));
    }
    return outState;
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> unresolveNeighbor(
      const std::shared_ptr<SwitchState>& inState,
      bool linkLocal = false) {
    return addNeighbor<AddrT>(
        removeNeighbor<AddrT>(inState, linkLocal), linkLocal);
  }

  template <typename AddrT>
  void verifyClassId(int classID, bool linkLocal = false) {
    /*
     * Queue-per-host classIDs are only supported for physical ports.
     * Pending entry should not have a classID (0) associated with it.
     * Resolved entry should have a classID associated with it.
     */
    WITH_RETRIES({
      auto neighborInfo = getNeighborInfo(
          kIntfID(), getNeighborAddress<AddrT>(linkLocal), *getAgentEnsemble());
      EXPECT_EVENTUALLY_TRUE(neighborInfo.classId().has_value());
      if (neighborInfo.classId().has_value()) {
        EXPECT_EVENTUALLY_TRUE(
            programToTrunk || classID == neighborInfo.classId().value());
      }
    });
  }

  template <typename AddrT>
  bool nbrExists(bool linkLocal = false) {
    auto neighborInfo = getNeighborInfo(
        kIntfID(), getNeighborAddress<AddrT>(linkLocal), *getAgentEnsemble());
    return *neighborInfo.exists();
  }
  template <typename AddrT>
  AddrT getNeighborAddress(bool linkLocal) const {
    return linkLocal ? getLinkLocalNeighborAddressForFamily<AddrT>()
                     : getNeighborAddressForFamily<AddrT>();
  }

  template <typename AddrT>
  bool isProgrammedToCPU(bool linkLocal = false) {
    auto neighborInfo = getNeighborInfo(
        kIntfID(), getNeighborAddress<AddrT>(linkLocal), *getAgentEnsemble());
    return *neighborInfo.isProgrammedToCpu();
  }

  const folly::MacAddress kNeighborMac{"2:3:4:5:6:7"};
  const cfg::AclLookupClass kLookupClass{
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2};
  const cfg::AclLookupClass kLookupClass2{
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3};
};

template <typename NeighborT>
class AgentNeighborResolutionTest : public AgentNeighborTest<NeighborT> {
 protected:
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    // Enable neighbor cache so that class id is set
    FLAGS_disable_neighbor_updates = false;
    // enable neighbor update failure protection
    FLAGS_enable_hw_update_protection = true;
  }

  // Get the neighbor cache for the given address family
  template <typename AddrT>
  auto getNeighborCache() {
    auto switchType =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics())
            ->getSwitchType();

    if constexpr (std::is_same_v<AddrT, folly::IPAddressV6>) {
      if (switchType == cfg::SwitchType::VOQ ||
          switchType == cfg::SwitchType::NPU) {
        return this->getSw()
            ->getNeighborUpdater()
            ->getNdpCacheDataForIntf()
            .get();
      } else {
        XLOG(FATAL) << "Unexpected switch type "
                    << static_cast<int>(switchType);
      }
    } else if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      if (switchType == cfg::SwitchType::VOQ ||
          switchType == cfg::SwitchType::NPU) {
        return this->getSw()
            ->getNeighborUpdater()
            ->getArpCacheDataForIntf()
            .get();
      } else {
        XLOG(FATAL) << "Unexpected switch type "
                    << static_cast<int>(switchType);
      }
    } else {
      XLOG(FATAL) << "Unsupported address type";
    }
  }

  // Verify that neighbor entries are programmed in the neighbor cache and
  // switchState neighbor table has the correct port
  template <typename AddrT>
  void verifyNeighborCache(
      const PortDescriptor& port,
      const std::shared_ptr<SwitchState>& inState) {
    auto outState{inState->clone()};
    auto ipAddress = this->template getNeighborAddress<AddrT>(false);
    auto neighborTable = this->template getNeighborTable<AddrT>(outState);
    auto switchType =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics())
            ->getSwitchType();

    WITH_RETRIES({
      auto neighborCache = getNeighborCache<AddrT>();

      auto cacheEntry = std::find_if(
          neighborCache.begin(),
          neighborCache.end(),
          [&ipAddress](const auto& entry) {
            return entry.ip() == network::toBinaryAddress(ipAddress);
          });
      EXPECT_EVENTUALLY_TRUE(cacheEntry != neighborCache.end());
      auto nbrEntry = neighborTable->getEntryIf(ipAddress);
      EXPECT_EVENTUALLY_TRUE(nbrEntry != nullptr);

      if (nbrEntry) {
        XLOG(DBG2) << "NDP : " << ipAddress.str() << " " << nbrEntry->str()
                   << " port " << nbrEntry->getPort().intID() << " port type "
                   << (int)nbrEntry->getPort().type();
        XLOG(DBG2) << "Neighbor port in cache: " << cacheEntry->port().value()
                   << " Port in switchState: " << port.intID();
        if (switchType == cfg::SwitchType::VOQ) {
          // VOQ switches store physical port ID in neighbor cache
          EXPECT_EVENTUALLY_EQ(cacheEntry->port().value(), port.intID());
          // VOQ switches store system port ID in switch state which is same as
          // interface ID
          EXPECT_EVENTUALLY_EQ(
              nbrEntry->getPort().intID(), cacheEntry->interfaceID().value());
        } else {
          EXPECT_EVENTUALLY_EQ(
              cacheEntry->port().value(), nbrEntry->getPort().intID());
        }
      }
    });
  }

  // Populate the neighbor cache from the switchState
  template <typename AddrT>
  void populateNeighborsCache(const PortDescriptor& port) {
    auto interface =
        this->getProgrammedState()->getInterfaces()->getNodeIf(this->kIntfID());
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV6>) {
      this->populateNdpNeighborsToCache(interface);
    } else if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      this->populateArpNeighborsToCache(interface);
    }
  }
};

TYPED_TEST_SUITE(AgentNeighborTest, NeighborTypes);

TYPED_TEST(AgentNeighborTest, AddPendingEntry) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template addNeighbor<folly::IPAddressV4>(in);
      return this->template addNeighbor<folly::IPAddressV6>(state);
    });
  };
  auto verify = [this]() {
    {
      SCOPED_TRACE("v4");
      EXPECT_TRUE(this->template isProgrammedToCPU<folly::IPAddressV4>());
    }
    {
      SCOPED_TRACE("v6");
      EXPECT_TRUE(this->template isProgrammedToCPU<folly::IPAddressV6>());
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentNeighborTest, ResolvePendingEntry) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template addNeighbor<folly::IPAddressV4>(in);
      state = this->template resolveNeighbor<folly::IPAddressV4>(state);
      state = this->template addNeighbor<folly::IPAddressV6>(state);
      return this->template resolveNeighbor<folly::IPAddressV6>(state);
    });
  };
  auto verify = [this]() {
    {
      SCOPED_TRACE("v4");
      EXPECT_FALSE(this->template isProgrammedToCPU<folly::IPAddressV4>());
    }
    {
      SCOPED_TRACE("v6");
      EXPECT_FALSE(this->template isProgrammedToCPU<folly::IPAddressV6>());
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentNeighborTest, ResolveLinkLocalEntry) {
  bool linkLocal = true;
  auto setup = [this, linkLocal]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state =
          this->template addNeighbor<folly::IPAddressV4>(in, linkLocal);
      state = this->template resolveNeighbor<folly::IPAddressV4>(
          state, std::nullopt, linkLocal);
      state = this->template addNeighbor<folly::IPAddressV6>(state, linkLocal);
      return this->template resolveNeighbor<folly::IPAddressV6>(
          state, std::nullopt, linkLocal);
    });
  };
  // LL neighbors are not programmed in HW. So
  // there is nothing to verify. However we must
  // be able to come up and WB with LL neighbors
  // present
  this->verifyAcrossWarmBoots(setup, []() {});
}

TYPED_TEST(AgentNeighborTest, UnresolveResolvedEntry) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template resolveNeighbor<folly::IPAddressV4>(
          this->template addNeighbor<folly::IPAddressV4>(in));
      state = this->template unresolveNeighbor<folly::IPAddressV4>(state);
      state = this->template resolveNeighbor<folly::IPAddressV6>(
          this->template addNeighbor<folly::IPAddressV6>(state));
      return this->template unresolveNeighbor<folly::IPAddressV6>(state);
    });
  };
  auto verify = [this]() {
    {
      SCOPED_TRACE("v4");
      EXPECT_TRUE(this->template isProgrammedToCPU<folly::IPAddressV4>());
    }
    {
      SCOPED_TRACE("v6");
      EXPECT_TRUE(this->template isProgrammedToCPU<folly::IPAddressV6>());
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentNeighborTest, ResolveThenUnresolveEntry) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template resolveNeighbor<folly::IPAddressV4>(
          this->template addNeighbor<folly::IPAddressV4>(in));
      return this->template resolveNeighbor<folly::IPAddressV6>(
          this->template addNeighbor<folly::IPAddressV6>(state));
    });
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template unresolveNeighbor<folly::IPAddressV4>(in);
      return this->template unresolveNeighbor<folly::IPAddressV6>(state);
    });
  };
  auto verify = [this]() {
    {
      SCOPED_TRACE("v4");
      EXPECT_TRUE(this->template isProgrammedToCPU<folly::IPAddressV4>());
    }
    {
      SCOPED_TRACE("v6");
      EXPECT_TRUE(this->template isProgrammedToCPU<folly::IPAddressV6>());
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentNeighborTest, RemoveResolvedEntry) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template resolveNeighbor<folly::IPAddressV4>(
          this->template addNeighbor<folly::IPAddressV4>(in));
      state = this->template removeNeighbor<folly::IPAddressV4>(state);
      state = this->template resolveNeighbor<folly::IPAddressV6>(
          this->template addNeighbor<folly::IPAddressV6>(state));
      return this->template removeNeighbor<folly::IPAddressV6>(state);
    });
  };
  auto verify = [this]() {
    {
      SCOPED_TRACE("v4");
      EXPECT_FALSE(this->template nbrExists<folly::IPAddressV4>());
    }
    {
      SCOPED_TRACE("v6");
      EXPECT_FALSE(this->template nbrExists<folly::IPAddressV6>());
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentNeighborTest, AddPendingRemovedEntry) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template resolveNeighbor<folly::IPAddressV4>(
          this->template addNeighbor<folly::IPAddressV4>(in));
      state = this->template removeNeighbor<folly::IPAddressV4>(state);
      state = this->template resolveNeighbor<folly::IPAddressV6>(
          this->template addNeighbor<folly::IPAddressV6>(state));
      return this->template removeNeighbor<folly::IPAddressV6>(state);
    });
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template addNeighbor<folly::IPAddressV4>(in);
      return this->template addNeighbor<folly::IPAddressV6>(state);
    });
  };
  auto verify = [this]() {
    {
      SCOPED_TRACE("v4");
      EXPECT_TRUE(this->template isProgrammedToCPU<folly::IPAddressV4>());
    }
    {
      SCOPED_TRACE("v6");
      EXPECT_TRUE(this->template isProgrammedToCPU<folly::IPAddressV6>());
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentNeighborTest, LinkDownOnResolvedEntry) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template addNeighbor<folly::IPAddressV4>(in);
      state = this->template resolveNeighbor<folly::IPAddressV4>(state);
      state = this->template addNeighbor<folly::IPAddressV6>(state);
      return this->template resolveNeighbor<folly::IPAddressV6>(state);
    });
    this->bringDownPort(this->portIdsForTest()[0]);
  };
  auto verify = [this]() {
    // There is a behavior differnce b/w SAI and BcmSwitch on link down
    // w.r.t. to neighbor entries. In BcmSwitch, we leave the nbr entries
    // intact and rely on SwSwitch to prune them. In SaiSwitch we prune
    // the fdb and neighbor entries on in SaiSwitch itself
    {
      SCOPED_TRACE("v4");
      if (this->template nbrExists<folly::IPAddressV4>()) {
        // egress to neighbor entry is not updated on link down
        // if it is not part of ecmp group
        EXPECT_FALSE(this->template isProgrammedToCPU<folly::IPAddressV4>());
      }
    }
    {
      SCOPED_TRACE("v6");
      if (this->template nbrExists<folly::IPAddressV6>()) {
        EXPECT_FALSE(this->template isProgrammedToCPU<folly::IPAddressV6>());
      }
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentNeighborTest, LinkDownAndUpOnResolvedEntry) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template addNeighbor<folly::IPAddressV4>(in);
      state = this->template resolveNeighbor<folly::IPAddressV4>(state);
      state = this->template addNeighbor<folly::IPAddressV6>(state);
      return this->template resolveNeighbor<folly::IPAddressV6>(state);
    });
    this->bringDownPort(this->portIdsForTest()[0]);
    this->bringUpPort(this->portIdsForTest()[0]);
  };
  auto verify = [this]() {
    // There is a behavior differnce b/w SAI and BcmSwitch on link down
    // w.r.t. to neighbor entries. In BcmSwitch, we leave the nbr entries
    // intact and rely on SwSwitch to prune them. In SaiSwitch we prune
    // the fdb and neighbor entries on in SaiSwitch itself
    {
      SCOPED_TRACE("v4");
      if (this->template nbrExists<folly::IPAddressV4>()) {
        // egress to neighbor entry is not updated on link down
        // if it is not part of ecmp group
        EXPECT_FALSE(this->template isProgrammedToCPU<folly::IPAddressV4>());
      }
    }
    {
      SCOPED_TRACE("v6");
      if (this->template nbrExists<folly::IPAddressV6>()) {
        EXPECT_FALSE(this->template isProgrammedToCPU<folly::IPAddressV6>());
      }
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST_SUITE(AgentNeighborResolutionTest, NeighborTypes);
// This test verifies that the neighbor cache is populated correctly
// from the switchState. This is done by adding a neighbor entry
// and then populating the cache. The cache is then verified to
// have the correct port associated with the neighbor entry
TYPED_TEST(AgentNeighborResolutionTest, ResolveNeighborAndCheckCache) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template resolveNeighbor<folly::IPAddressV4>(
          this->template addNeighbor<folly::IPAddressV4>(in));
      return this->template resolveNeighbor<folly::IPAddressV6>(
          this->template addNeighbor<folly::IPAddressV6>(state));
    });

    this->template populateNeighborsCache<folly::IPAddressV4>(
        this->portDescriptor());
    this->template populateNeighborsCache<folly::IPAddressV6>(
        this->portDescriptor());
  };
  auto verify = [this]() {
    {
      SCOPED_TRACE("v4");
      this->template verifyNeighborCache<folly::IPAddressV4>(
          this->portDescriptor(), this->getProgrammedState());
    }
    {
      SCOPED_TRACE("v6");
      this->template verifyNeighborCache<folly::IPAddressV6>(
          this->portDescriptor(), this->getProgrammedState());
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

class AgentNeighborOnMultiplePortsTest : public AgentHwTest {
 protected:
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    std::vector<ProductionFeature> features = {
        ProductionFeature::L3_FORWARDING};
    features.push_back(ProductionFeature::INTERFACE_NEIGHBOR_TABLE);

    return features;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto switchId = getSwitchIdUnderTest(ensemble);
    auto ports = ensemble.masterLogicalPortIds({switchId});
    return utility::onePortPerInterfaceConfig(ensemble.getSw(), ports);
  }
  folly::IPAddressV6 neighborIP(PortID port) const {
    utility::EcmpSetupAnyNPorts6 ecmpHelper6(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    return ecmpHelper6.ip(PortDescriptor(port));
  }

  std::vector<PortID> portIdsForTest() {
    if (FLAGS_hyper_port) {
      return masterLogicalHyperPortIds();
    }
    return masterLogicalInterfacePortIds();
  }

  void oneNeighborPerPortSetup(const std::vector<PortID>& portIds) {
    auto cfg = initialConfig(*getAgentEnsemble());
    if (FLAGS_disable_loopback) {
      // Disable loopback on specific ports
      for (auto portId : portIds) {
        auto portCfg = utility::findCfgPortIf(cfg, portId);
        if (portCfg != cfg.ports()->end()) {
          portCfg->loopbackMode() = cfg::PortLoopbackMode::NONE;
        }
      }
      this->applyNewConfig(cfg);
    }

    // Create adjacencies on all test ports
    auto dstMac =
        getMacForFirstInterfaceWithPortsForTesting(this->getProgrammedState());
    for (int idx = 0; idx < portIds.size(); idx++) {
      this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        utility::EcmpSetupAnyNPorts6 ecmpHelper6(
            in,
            getSw()->needL2EntryForNeighbor(),
            utility::MacAddressGenerator().get(dstMac.u64NBO() + idx + 1));
        return ecmpHelper6.resolveNextHops(in, {PortDescriptor(portIds[idx])});
      });
    }

    // Dump the local interface config
    XLOG(DBG0) << "Dumping port configurations:";
    for (int idx = 0; idx < portIds.size(); idx++) {
      auto mac =
          getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
      XLOG(DBG0) << "   Port " << portIds[idx]
                 << ", IPv6: " << cfg.interfaces()[idx].ipAddresses()[1]
                 << ", Intf MAC: " << mac;
    }
  }

  InterfaceID getInterfaceId(const PortID& portId) const {
    auto switchType =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getSwitchType();

    if (switchType == cfg::SwitchType::VOQ ||
        switchType == cfg::SwitchType::NPU) {
      return InterfaceID(*getProgrammedState()
                              ->getPorts()
                              ->getNodeIf(portId)
                              ->getInterfaceIDs()
                              .begin());
    }

    XLOG(FATAL) << "Unexpected switch type " << static_cast<int>(switchType);
  }

  bool isProgrammedToCPU(PortID& portId, folly::IPAddress ip) {
    auto intfId = getInterfaceId(portId);
    auto neighborInfo = getNeighborInfo(intfId, ip, *getAgentEnsemble());
    return *neighborInfo.isProgrammedToCpu();
  }
};

TEST_F(AgentNeighborOnMultiplePortsTest, ResolveOnTwoPorts) {
  auto setup = [&]() {
    this->oneNeighborPerPortSetup(
        {this->portIdsForTest()[0], this->portIdsForTest()[1]});
  };
  auto verify = [&]() {
    EXPECT_FALSE(this->isProgrammedToCPU(
        this->portIdsForTest()[0],
        this->neighborIP(this->portIdsForTest()[0])));
    EXPECT_FALSE(this->isProgrammedToCPU(
        this->portIdsForTest()[1],
        this->neighborIP(this->portIdsForTest()[1])));
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

template <typename NeighborT>
class AgentNeighborMetadataTest : public AgentNeighborTest<NeighborT> {
 protected:
  static auto constexpr programToTrunk = NeighborT::isTrunk;

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    std::vector<ProductionFeature> features = {
        ProductionFeature::L3_FORWARDING,
        ProductionFeature::CLASS_ID_FOR_NEIGHBOR};
    features.push_back(ProductionFeature::INTERFACE_NEIGHBOR_TABLE);

    if (programToTrunk) {
      features.push_back(ProductionFeature::LAG);
    }
    return features;
  }
};

TYPED_TEST_SUITE(AgentNeighborMetadataTest, NeighborTypes);

TYPED_TEST(
    AgentNeighborMetadataTest,
    ResolvePendingEntryThenChangeLookupClass) {
  auto setup = [this]() {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template addNeighbor<folly::IPAddressV4>(in);
      state = this->template resolveNeighbor<folly::IPAddressV4>(
          state, this->kLookupClass);
      state = this->template addNeighbor<folly::IPAddressV6>(state);
      return this->template resolveNeighbor<folly::IPAddressV6>(
          state, this->kLookupClass);
    });

    this->template verifyClassId<folly::IPAddressV4>(
        static_cast<int>(this->kLookupClass));
    this->template verifyClassId<folly::IPAddressV6>(
        static_cast<int>(this->kLookupClass));
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto state = this->template resolveNeighbor<folly::IPAddressV4>(
          in, this->kLookupClass2);
      return this->template resolveNeighbor<folly::IPAddressV6>(
          state, this->kLookupClass2);
    });
  };
  auto verify = [this]() {
    {
      SCOPED_TRACE("v4");
      EXPECT_FALSE(this->template isProgrammedToCPU<folly::IPAddressV4>());
      this->template verifyClassId<folly::IPAddressV4>(
          static_cast<int>(this->kLookupClass2));
    }
    {
      SCOPED_TRACE("v6");
      EXPECT_FALSE(this->template isProgrammedToCPU<folly::IPAddressV6>());
      this->template verifyClassId<folly::IPAddressV6>(
          static_cast<int>(this->kLookupClass2));
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
