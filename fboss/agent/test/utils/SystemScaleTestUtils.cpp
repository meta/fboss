#include "fboss/agent/test/utils/SystemScaleTestUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/lib/FunctionCallTimeReporter.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/RouteScaleGenerators.h"
#include "fboss/agent/test/utils/AclScaleTestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/MacLearningFloodHelper.h"
#include "fboss/agent/test/utils/PortFlapHelper.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/test/utils/ScaleTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "folly/Benchmark.h"

DECLARE_bool(json);
DECLARE_int32(max_l2_entries);
DECLARE_int32(max_ndp_entries);
DECLARE_int32(max_arp_entries);

// the number of rounds to add/churn fboss routes and neighbors is limited by
// the time to run the test. The default number of rounds is chosen to finish
// the test in 15mins
DEFINE_int32(ndp_churn_rounds, 1, "Number of rounds to churn NDP entries");
DEFINE_int32(route_churn_rounds, 3, "Number of rounds to churn routes");

// optional flags for use in manual stress tests
DEFINE_int32(
    delay_churn_start_ms,
    0,
    "After initial programming, delay churn (in msec, default 0)");
DEFINE_int32(
    delay_exit_ms,
    0,
    "At the end of test, delay exit (in msec, default 0)");

namespace {
constexpr uint64_t kBaseMac = 0xFEEEC2000010;
constexpr int kMaxScaleMacTxPortIdx = 1;
constexpr int kMaxScaleMacRxPortIdx = 0;
constexpr int kMacChurnTxPortIdx = 2;
constexpr int kMacChurnRxPortIdx = 3;
constexpr int kRxMeasurePortIdx = 4;
constexpr int kTxMeasurePortIdx = 5;
const std::string kRxMeasureDstIp = "2620:0:1cfe:face:b00c::4";
} // namespace

namespace facebook::fboss::utility {

template <typename AddrT>
void removeNeighbor(AgentEnsemble* ensemble) {
  ensemble->applyNewState(
      [&](const std::shared_ptr<SwitchState>& in) {
        auto newState = in->clone();
        auto intfID =
            ensemble->getProgrammedState()
                ->getPorts()
                ->getNodeIf(ensemble->masterLogicalInterfacePortIds()[0])
                ->getInterfaceID();
        Interface* interface = newState->getInterfaces()->getNode(intfID).get();
        interface = interface->modify(&newState);
        if (std::is_same<AddrT, folly::IPAddressV4>::value) {
          XLOG(DBG2) << "# arp entries" << interface->getArpTable()->size();
          interface->setArpTable(std::make_shared<ArpTable>());
        } else {
          XLOG(DBG2) << "# ndp entries" << interface->getNdpTable()->size();
          interface->setNdpTable(std::make_shared<NdpTable>());
        }

        return newState;
      },
      "remove neighbor",
      false);
}

void removeAllNeighbors(AgentEnsemble* ensemble) {
  removeNeighbor<folly::IPAddressV4>(ensemble);
  removeNeighbor<folly::IPAddressV6>(ensemble);
}

template <typename AddrT>
std::vector<std::pair<AddrT, folly::MacAddress>> neighborAddrs(
    int numNeighbors) {
  std::vector<std::pair<AddrT, folly::MacAddress>> macIPPairs;
  for (int i = 0; i < numNeighbors; ++i) {
    std::stringstream ipStream;

    if (std::is_same<AddrT, folly::IPAddressV6>::value) {
      ipStream << "2001:0db8:85a3:0000:0000:8a2e:" << std::hex
               << (i >> 16 & 0xffff) << ":" << std::hex << (i & 0xffff);
    } else {
      ipStream << "100.100." << (i >> 8 & 0xff) << "." << (i & 0xff);
    }
    AddrT ip(ipStream.str());
    uint64_t macBytes = folly::MacAddress("06:00:00:00:00:00").u64HBO() + 1;
    folly::MacAddress mac = folly::MacAddress::fromHBO(macBytes);
    macIPPairs.push_back(std::make_pair(ip, mac));
  }
  return macIPPairs;
}

template <typename AddrT>
std::shared_ptr<facebook::fboss::SwitchState> updateNeighborEntry(
    AgentEnsemble* ensemble,
    const std::shared_ptr<SwitchState>& in,
    const PortDescriptor& port,
    const AddrT& addr,
    const folly::MacAddress& mac,
    std::optional<cfg::AclLookupClass> lookupClass = std::nullopt) {
  using NeighborTableT = typename std::conditional_t<
      std::is_same<AddrT, folly::IPAddressV4>::value,
      ArpTable,
      NdpTable>;
  auto state = in->clone();
  NeighborTableT* neighborTable;
  auto intfID = ensemble->getProgrammedState()
                    ->getPorts()
                    ->getNodeIf(ensemble->masterLogicalInterfacePortIds()[0])
                    ->getInterfaceID();

  neighborTable = state->getInterfaces()
                      ->getNode(intfID)
                      ->template getNeighborEntryTable<AddrT>()
                      ->modify(intfID, &state);

  if (neighborTable->getEntryIf(addr)) {
    neighborTable->updateEntry(
        addr, mac, port, intfID, NeighborState::REACHABLE, lookupClass);
  } else {
    neighborTable->addEntry(addr, mac, port, intfID);
    // Update entry to add classid if any
    neighborTable->updateEntry(
        addr, mac, port, intfID, NeighborState::REACHABLE, lookupClass);
  }
  return state;
}

template <typename AddrT>
void programNeighbor(
    AgentEnsemble* ensemble,
    const PortDescriptor& port,
    const AddrT& addr,
    const folly::MacAddress neighborMac,
    std::optional<cfg::AclLookupClass> lookupClass = std::nullopt) {
  ensemble->applyNewState(
      [&](const std::shared_ptr<SwitchState>& in) {
        return updateNeighborEntry(
            ensemble, in, port, addr, neighborMac, lookupClass);
      },
      "program neighbor",
      false);
}

void programNeighbors(
    AgentEnsemble* ensemble,
    const PortDescriptor& port,
    std::optional<cfg::AclLookupClass> lookupClass) {
  int numNDPNeighbors, numARPNeighbors;

  numNDPNeighbors = FLAGS_max_ndp_entries;
  numARPNeighbors = FLAGS_max_arp_entries;
  XLOG(DBG2) << "Max NDP neighbors: " << numNDPNeighbors << " Max ARP neighbors"
             << numARPNeighbors;

  for (auto pair : neighborAddrs<folly::IPAddressV4>(numARPNeighbors)) {
    programNeighbor(ensemble, port, pair.first, pair.second, lookupClass);
  }
  for (auto pair : neighborAddrs<folly::IPAddressV6>(numNDPNeighbors)) {
    programNeighbor(ensemble, port, pair.first, pair.second, lookupClass);
  }
}

void configureMaxNeighborEntries(AgentEnsemble* ensemble) {
  programNeighbors(
      ensemble,
      PortDescriptor(ensemble->masterLogicalInterfacePortIds()[0]),
      std::nullopt);
}

std::vector<L2Entry> generateL2Entries(
    AgentEnsemble* ensemble,
    const std::vector<folly::MacAddress>& macs,
    PortID portID) {
  std::vector<L2Entry> l2Entries;
  const auto portDescr = PortDescriptor(portID);
  auto vlan = ensemble->getProgrammedState()
                  ->getPorts()
                  ->getNodeIf(portID)
                  ->getIngressVlan();
  l2Entries.reserve(macs.size());
  for (auto& mac : macs) {
    l2Entries.emplace_back(
        mac, vlan, portDescr, L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);
  }
  return l2Entries;
}

void configureMaxMacEntriesViaL2LearningUpdate(AgentEnsemble* ensemble) {
  std::vector<folly::MacAddress> macs;
  // mac learned from port
  auto rxPort =
      PortID(ensemble->masterLogicalInterfacePortIds()[kMaxScaleMacRxPortIdx]);
  auto vlan = ensemble->getProgrammedState()
                  ->getPorts()
                  ->getNodeIf(rxPort)
                  ->getIngressVlan();
  for (int i = 0; i < FLAGS_max_l2_entries; ++i) {
    folly::MacAddress mac = folly::MacAddress::fromHBO(kBaseMac + i);
    macs.push_back(mac);
  }
  auto l2Entries = generateL2Entries(ensemble, macs, rxPort);
  for (int i = 0; i < l2Entries.size(); i++) {
    XLOG(DBG2) << "Adding i " << i << " MAC " << l2Entries[i].str();
    ensemble->getSw()->l2LearningUpdateReceived(
        l2Entries[i], L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);
  }
  int numMacs;
  std::condition_variable cv;
  std::chrono::milliseconds waitForMacInstalled =
      std::chrono::milliseconds(1000);
  std::mutex mutex;
  std::unique_lock<std::mutex> lock(mutex);

  do {
    numMacs = ensemble->getProgrammedState()
                  ->getVlans()
                  ->getNodeIf(vlan)
                  ->getMacTable()
                  ->size();
    XLOG(DBG2) << "mac table size: " << numMacs;
    cv.wait_for(lock, waitForMacInstalled, [] { return false; });
  } while (numMacs < FLAGS_max_l2_entries);
}

void configureMaxMacEntriesViaPacketIn(AgentEnsemble* ensemble) {
  CHECK_GE(ensemble->masterLogicalInterfacePortIds().size(), 2);
  PortID txPort =
      PortID(ensemble->masterLogicalInterfacePortIds()[kMaxScaleMacTxPortIdx]);
  PortID rxPort =
      PortID(ensemble->masterLogicalInterfacePortIds()[kMaxScaleMacRxPortIdx]);
  auto vlan = ensemble->getProgrammedState()
                  ->getPorts()
                  ->getNodeIf(rxPort)
                  ->getIngressVlan();

  auto rxInterfaceMac = getInterfaceMac(
      ensemble->getProgrammedState(),
      ensemble->getProgrammedState()
          ->getPorts()
          ->getNodeIf(rxPort)
          ->getInterfaceID());

  std::vector<std::pair<folly::IPAddressV6, folly::MacAddress>> macIPv6Pairs;

  for (int i = 0; i < FLAGS_max_l2_entries; ++i) {
    std::stringstream ipStream;
    ipStream << "2620:0:1cfe:face:b10c::" << std::hex << i;
    folly::IPAddressV6 ip(ipStream.str());
    uint64_t macBytes = kBaseMac;
    folly::MacAddress mac = folly::MacAddress::fromHBO(macBytes + i);
    macIPv6Pairs.emplace_back(ip, mac);
  }
  const int srcPort = 47231;
  const int dstPort = 277;
  for (const auto& [ipV6, mac] : macIPv6Pairs) {
    auto txPacket = utility::makeTCPTxPacket(
        ensemble->getSw(),
        vlan,
        mac,
        // MacAddress::BROADCAST send from second last interface to last
        // if replaced by macLastInterface, then traffic won't flood
        rxInterfaceMac,
        ipV6,
        folly::IPAddressV6("2620:0:1cfe:face:b10b::"),
        srcPort,
        dstPort);
    ensemble->getSw()->sendPacketOutOfPortAsync(std::move(txPacket), txPort);
  }
}
void configureMaxMacEntries(AgentEnsemble* ensemble) {
  auto asic = checkSameAndGetAsic(
      ensemble->getHwAsicTable()->getL3Asics(), FLAGS_switch_id_for_testing);
  if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    return;
  }
  // TH3 had existing slowness of l2 callbacks. To exercise the callback path,
  // we utilize l2 callback on sw switch to simulate large scale of l2 callback
  if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK3) {
    configureMaxMacEntriesViaL2LearningUpdate(ensemble);
  } else {
    configureMaxMacEntriesViaPacketIn(ensemble);
  }
}

void syncFib(
    SwSwitchRouteUpdateWrapper& updater,
    const RouterID kRid,
    const utility::RouteDistributionGenerator::ThriftRouteChunk& routes) {
  std::for_each(
      routes.begin(), routes.end(), [&updater, kRid](const auto& route) {
        updater.addRoute(kRid, ClientID::BGPD, route);
      });
  updater.program(
      {{{kRid, ClientID::BGPD}},
       RouteUpdateWrapper::SyncFibInfo::SyncFibType::IP_ONLY});
}

void configureMaxRouteEntries(
    AgentEnsemble* ensemble,
    const RouteDistributionGenerator& routeGenerator) {
  std::set<SwitchID> switchIds = {SwitchID(FLAGS_switch_id_for_testing)};
  CHECK_EQ(switchIds.size(), 1);
  auto asic = checkSameAndGetAsic(
      ensemble->getHwAsicTable()->getL3Asics(), FLAGS_switch_id_for_testing);
  auto* sw = ensemble->getSw();
  // reserve 10 ecmp for scaling routes
  auto const kMaxEcmpGropus =
      utility::getMaxEcmpGroups(ensemble->getL3Asics()) *
          FLAGS_ecmp_resource_percentage / 100 -
      10;

  auto maxNumRoute =
      asic->getMaxRoutes().has_value() ? asic->getMaxRoutes().value() : 0;
  CHECK_GT(maxNumRoute, 0);

  auto allThriftRoutes = routeGenerator.allThriftRoutes();
  int numThriftRoutes = allThriftRoutes.size();

  std::vector<RoutePrefixV6> ecmpPrefixes;
  utility::EcmpSetupTargetedPorts6 ecmpHelper(
      ensemble->getProgrammedState(),
      ensemble->getSw()->needL2EntryForNeighbor());
  std::vector<PortID> portIds = ensemble->masterLogicalInterfacePortIds();
  std::vector<PortDescriptor> portDescriptorIds;

  for (const auto& portId : portIds) {
    portDescriptorIds.emplace_back(portId);
  }

  std::vector<std::vector<PortDescriptor>> allCombinations =
      utility::generateEcmpGroupScale(
          portDescriptorIds, kMaxEcmpGropus, portDescriptorIds.size());
  std::vector<boost::container::flat_set<PortDescriptor>> nhopSets;
  for (const auto& combination : allCombinations) {
    nhopSets.emplace_back(combination.begin(), combination.end());
  }

  XLOG(DBG2) << "Max Ecmp Groups: " << kMaxEcmpGropus
             << " Default Ecmp Width: " << kDefaulEcmpWidth
             << " combination size: " << allCombinations.size()
             << " generated routes: " << allThriftRoutes.size()
             << " routes assigned to ecmp group: " << allCombinations.size();

  // ECMP routes + generated routes should be less than max routes
  XLOG(DBG2) << "thrift routes: " << allThriftRoutes.size();
  XLOG(DBG2) << "generated routes: " << allCombinations.size();
  XLOG(DBG2) << "maximum routes: " << maxNumRoute;

  CHECK_LE(allThriftRoutes.size() + allCombinations.size(), maxNumRoute);

  ensemble->applyNewState(
      [&portDescriptorIds,
       &ecmpHelper](const std::shared_ptr<SwitchState>& in) {
        return ecmpHelper.resolveNextHops(
            in,
            boost::container::flat_set<PortDescriptor>(
                std::make_move_iterator(portDescriptorIds.begin()),
                std::make_move_iterator(portDescriptorIds.end())));
      },
      "program ecmp",
      false);

  std::generate_n(
      std::back_inserter(ecmpPrefixes), kMaxEcmpGropus, [i = 0]() mutable {
        return RoutePrefixV6{
            folly::IPAddressV6(folly::to<std::string>(2401, "::", i++)), 128};
      });

  auto wrapper = sw->getRouteUpdater();
  ecmpHelper.programRoutes(&wrapper, nhopSets, ecmpPrefixes);

  auto platformType = sw->getPlatformType();
  if (!routeGenerator.isSupported(platformType)) {
    throw FbossError(
        "Route scale generator not supported for platform: ", platformType);
  }

  ThriftHandler handler(ensemble->getSw());
  // Sync fib with same set of routes
  // TH doesn't support 75K route scale, the following is a work around
  if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK) {
    std::vector<UnicastRoute> quarterThriftRoutes = std::vector<UnicastRoute>(
        allThriftRoutes.begin(),
        allThriftRoutes.begin() + allThriftRoutes.size() / 4);
    std::unique_ptr<std::vector<UnicastRoute>> routesPtr =
        std::make_unique<std::vector<UnicastRoute>>(quarterThriftRoutes);
    StopWatch timer("program_routes_msecs", FLAGS_json);
    handler.syncFib(static_cast<int>(ClientID::BGPD), std::move(routesPtr));
  } else {
    std::unique_ptr<std::vector<UnicastRoute>> routesPtr =
        std::make_unique<std::vector<UnicastRoute>>(allThriftRoutes);
    StopWatch timer("program_routes_msecs", FLAGS_json);
    handler.syncFib(static_cast<int>(ClientID::BGPD), std::move(routesPtr));
  }
  int route_count = 0;
  auto countRoutes = [&route_count](RouterID, auto&) { ++route_count; };
  forAllRoutes(sw->getState(), countRoutes);
  if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK) {
    CHECK_GE(route_count, numThriftRoutes / 4);
  } else {
    CHECK_GE(route_count, numThriftRoutes);
  }
}

void removeAllRouteEntries(AgentEnsemble* ensemble) {
  const RouterID kRid(0);
  auto updater = ensemble->getSw()->getRouteUpdater();
  syncFib(updater, kRid, {});
  int route_count = 0;
  auto countRoutes = [&route_count](RouterID, auto&) { ++route_count; };
  forAllRoutes(ensemble->getSw()->getState(), countRoutes);
  auto directlyConnectedInterfaces =
      ensemble->getProgrammedState()->getInterfaces()->numNodes();
  CHECK_LE(route_count, 2 * (directlyConnectedInterfaces + 1) + 1);
}

void configureMaxAclEntries(AgentEnsemble* ensemble) {
  auto cfg = ensemble->getCurrentConfig();
  const auto maxAclEntries = getMaxAclEntries(ensemble->getL3Asics());
  XLOG(DBG2) << "Max Acl Entries: " << maxAclEntries;
  std::vector<cfg::AclTableQualifier> qualifiers =
      setAclQualifiers(AclWidth::SINGLE_WIDE);

  utility::addAclTable(&cfg, "aclTable0", 0 /* priority */, {}, qualifiers);
  addAclEntries(maxAclEntries, AclWidth::SINGLE_WIDE, &cfg, "aclTable0");

  ensemble->applyNewConfig(cfg);
}
void addPort2NewVlan(cfg::SwitchConfig& config, PortID portID, int vlanID) {
  auto vlanIfExist = std::find_if(
      config.vlans()->begin(), config.vlans()->end(), [vlanID](auto vlan) {
        return vlan.id() == vlanID;
      });
  if (vlanIfExist != config.vlans()->end()) {
    throw FbossError("The vlan to add already exists");
  }

  // add vlan
  auto newVlan = cfg::Vlan();
  newVlan.name() = "rx tx test";
  newVlan.id() = VlanID(vlanID);
  newVlan.routable() = true;
  config.vlans()->push_back(newVlan);

  // change the vlan id of ingressVlan of port
  for (auto& port : *config.ports()) {
    if (port.logicalID() == static_cast<int>(portID)) {
      port.ingressVlan() = vlanID;
    }
  }

  // change vlan port mapping
  auto vlanPort = std::find_if(
      config.vlanPorts()->begin(),
      config.vlanPorts()->end(),
      [portID](auto vlanPort) {
        return vlanPort.logicalPort() == static_cast<int>(portID);
      });
  if (vlanPort == config.vlanPorts()->end()) {
    throw FbossError("The port not not found in vlan port mapping");
  }
  vlanPort->vlanID() = vlanID;

  auto interfaceIfExist = std::find_if(
      config.interfaces()->begin(),
      config.interfaces()->end(),
      [vlanID](auto interface) { return interface.intfID() == vlanID; });
  if (interfaceIfExist != config.interfaces()->end()) {
    throw FbossError("The vlan interface to add already exists");
  }

  // add l3 interface to vlan
  auto newInterface = cfg::Interface();
  newInterface.intfID() = vlanID;
  newInterface.vlanID() = vlanID;
  newInterface.routerID() = 0;
  newInterface.mac() = utility::kLocalCpuMac().toString();
  newInterface.mtu() = 9000;
  newInterface.ipAddresses()->resize(2);
  newInterface.ipAddresses()[0] =
      "192.1." + std::to_string(vlanID - kBaseVlanId) + ".1/24";
  std::stringstream hexStream;
  hexStream << std::hex << vlanID;
  newInterface.ipAddresses()[1] = hexStream.str() + "::1/64";
  config.interfaces()->push_back(newInterface);
}

cfg::SwitchConfig getSystemScaleTestSwitchConfiguration(
    const AgentEnsemble& ensemble) {
  auto l3Asics = ensemble.getSw()->getHwAsicTable()->getL3Asics();
  auto asic = checkSameAndGetAsic(l3Asics, FLAGS_switch_id_for_testing);
  auto config = utility::oneL3IntfNPortConfig(
      ensemble.getSw()->getPlatformMapping(),
      asic,
      ensemble.masterLogicalPortIds(),
      ensemble.getSw()->getPlatformSupportsAddRemovePort(),
      asic->desiredLoopbackModes(),
      true, /* interfaceHasSubnet */
      kBaseVlanId, /* baseVlanId */
      true, /* optimizePortProfile */
      true, /* setInterfaceMac */
      ensemble.getSw()->getPlatformType());

  utility::setupDefaultAclTableGroups(config);
  // We don't want to set queue rate that limits the number of rx pkts
  utility::addCpuQueueConfig(
      config,
      ensemble.getL3Asics(),
      ensemble.isSai(),
      /* setQueueRate */ false);
  utility::setDefaultCpuTrafficPolicyConfig(
      config, ensemble.getL3Asics(), ensemble.isSai());

  auto trapDstIp = folly::CIDRNetwork{kRxMeasureDstIp, 128};
  utility::addTrapPacketAcl(asic, &config, trapDstIp);

  // Disable L2 learning for chenab platform to prevent
  // FDB metadata read errors on dynamically learned MAC entries.
  // Minipack3N do not support L2 learning mode.
  auto asicType = asic->getAsicType();
  if (asicType == cfg::AsicType::ASIC_TYPE_CHENAB) {
    config.switchSettings()->l2LearningMode() = cfg::L2LearningMode::DISABLED;
  } else {
    config.switchSettings()->l2LearningMode() = cfg::L2LearningMode::SOFTWARE;
  }
  addPort2NewVlan(
      config,
      ensemble.masterLogicalInterfacePortIds()[kRxMeasurePortIdx],
      kBaseVlanId + 1);

  addPort2NewVlan(
      config,
      ensemble.masterLogicalInterfacePortIds()[kTxMeasurePortIdx],
      kBaseVlanId + 2);
  return config;
};

std::pair<uint64_t, uint64_t> startRxMeasure(AgentEnsemble* ensemble) {
  // capture packet exiting port kRxMeasurePortIdx (entering due to loopback)
  auto dstMac = getInterfaceMac(
      ensemble->getProgrammedState(), InterfaceID(kBaseVlanId + 1));

  auto ecmpHelper = utility::EcmpSetupAnyNPorts6(
      ensemble->getProgrammedState(),
      ensemble->needL2EntryForNeighbor(),
      dstMac);
  boost::container::flat_set<PortDescriptor> IntfPorts;
  IntfPorts.insert(PortDescriptor(
      ensemble->masterLogicalInterfacePortIds()[kRxMeasurePortIdx]));

  ensemble->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    return ecmpHelper.resolveNextHops(in, IntfPorts);
  });
  ecmpHelper.programRoutes(
      std::make_unique<SwSwitchRouteUpdateWrapper>(
          ensemble->getSw(), ensemble->getSw()->getRib()),
      IntfPorts,
      {RoutePrefixV6{folly::IPAddressV6("2620:0:1cfe:face:b00c::4"), 128}},
      {},
      true);
  for (const auto& nextHop : ecmpHelper.getNextHops()) {
    utility::disablePortTTLDecrementIfSupported(
        ensemble, ecmpHelper.getRouterId(), nextHop);
  }
  constexpr uint8_t kCpuQueue = 0;
  std::map<int, CpuPortStats> cpuStatsBefore;
  ensemble->getSw()->getAllCpuPortStats(cpuStatsBefore);
  auto switchIndex =
      ensemble->getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(
          SwitchID(FLAGS_switch_id_for_testing));
  auto statsBefore = cpuStatsBefore.at(switchIndex);
  auto [pktsBefore, bytesBefore] = utility::getCpuQueueOutPacketsAndBytes(
      *statsBefore.portStats_(), kCpuQueue);

  const auto kSrcMac = folly::MacAddress{"fa:ce:b0:00:00:0a"};
  // Send packet
  auto vlanId =
      ensemble->getProgrammedState()
          ->getPorts()
          ->getNodeIf(
              ensemble->masterLogicalInterfacePortIds()[kRxMeasurePortIdx])
          ->getIngressVlan();

  auto constexpr kPacketToSend = 100;
  for (int i = 0; i < kPacketToSend; i++) {
    auto txPacket = utility::makeTCPTxPacket(
        ensemble->getSw(),
        vlanId,
        kSrcMac,
        dstMac,
        folly::IPAddressV6("2620:0:1cfe:face:b00c::3"),
        folly::IPAddressV6(kRxMeasureDstIp),
        47231,
        kBgpPort);
    ensemble->getSw()->sendPacketSwitchedAsync(
        std::move(txPacket),
        LocalSwitchIDs{SwitchID(FLAGS_switch_id_for_testing)});
  }
  return std::make_pair(pktsBefore, bytesBefore);
}

std::pair<uint64_t, uint64_t> stopRxMeasure(AgentEnsemble* ensemble) {
  ensemble->bringDownPorts(
      {ensemble->masterLogicalInterfacePortIds()[kRxMeasurePortIdx]});
  ensemble->bringUpPorts(
      {ensemble->masterLogicalInterfacePortIds()[kRxMeasurePortIdx]});
  constexpr uint8_t kCpuQueue = 0;
  std::map<int, CpuPortStats> cpuStatsAfter;
  ensemble->getSw()->getAllCpuPortStats(cpuStatsAfter);
  auto switchIndex =
      ensemble->getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(
          SwitchID(FLAGS_switch_id_for_testing));
  auto statsAfter = cpuStatsAfter.at(switchIndex);
  auto [pktsAfter, bytesAfter] = utility::getCpuQueueOutPacketsAndBytes(
      *statsAfter.portStats_(), kCpuQueue);
  return std::make_pair(pktsAfter, bytesAfter);
}

std::pair<uint64_t, uint64_t> getOutPktsAndBytes(
    AgentEnsemble* ensemble,
    const PortID& port) {
  auto stats = ensemble->getLatestPortStats(port);
  return {*stats.outUnicastPkts_(), *stats.outBytes_()};
}

void startTxMeasure(AgentEnsemble* ensemble, int& pps, int& bytesPerSec) {
  // configure port for tx measurement
  auto swSwitch = ensemble->getSw();
  auto dstMac = getInterfaceMac(
      ensemble->getProgrammedState(),
      ensemble->getProgrammedState()
          ->getPorts()
          ->getNodeIf(
              ensemble->masterLogicalInterfacePortIds()[kTxMeasurePortIdx])
          ->getInterfaceID());
  auto ecmpHelper = utility::EcmpSetupAnyNPorts6(
      ensemble->getProgrammedState(),
      ensemble->needL2EntryForNeighbor(),
      dstMac);
  boost::container::flat_set<PortDescriptor> IntfPorts;
  IntfPorts.insert(PortDescriptor(
      ensemble->masterLogicalInterfacePortIds()[kTxMeasurePortIdx]));

  const int kEcmpWidth = 1;
  ensemble->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    return ecmpHelper.resolveNextHops(in, kEcmpWidth);
  });
  ecmpHelper.programRoutes(
      std::make_unique<SwSwitchRouteUpdateWrapper>(
          ensemble->getSw(), ensemble->getSw()->getRib()),
      IntfPorts,
      {RoutePrefixV6{folly::IPAddressV6("2620:0:1cfd:face:b00c::5"), 64}});

  // setup Tx port
  auto portUsed = ensemble->masterLogicalInterfacePortIds()[kTxMeasurePortIdx];
  auto cpuMac = ensemble->getSw()->getLocalMac(SwitchID(0));
  auto vlanId =
      ensemble->getProgrammedState()
          ->getPorts()
          ->getNodeIf(
              ensemble->masterLogicalInterfacePortIds()[kTxMeasurePortIdx])
          ->getIngressVlan();

  std::atomic<bool> packetTxDone{false};

  std::thread t([cpuMac, vlanId, swSwitch, &packetTxDone]() {
    const auto kSrcIp = folly::IPAddressV6("2620:0:1cfd:face:b00c::5");
    const auto kDstIp = folly::IPAddressV6("2620:0:1cfd:face:b00c::6");
    const auto kSrcMac = folly::MacAddress{"fa:ce:b0:00:00:0c"};
    while (!packetTxDone) {
      for (auto i = 0; i < 1'000; ++i) {
        // Send packet
        auto txPacket = utility::makeIpTxPacket(
            [swSwitch](uint32_t size) {
              return swSwitch->allocatePacket(size);
            },
            vlanId,
            kSrcMac,
            cpuMac,
            kSrcIp,
            kDstIp);
        swSwitch->sendPacketSwitchedAsync(std::move(txPacket));
      }
    }
  });

  auto [pktsBefore, bytesBefore] =
      getOutPktsAndBytes(ensemble, PortID(portUsed));
  auto timeBefore = std::chrono::steady_clock::now();
  // Let the packet flood warm up
  std::this_thread::sleep_for(std::chrono::seconds(30));
  packetTxDone = true;
  t.join();
  auto timeAfter = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> durationMillseconds =
      timeAfter - timeBefore;
  auto pktsAfter = pktsBefore;
  auto bytesAfter = bytesBefore;
  auto kMaxIterations = 30;
  // Wait for stats increment to stop. On some platforms it
  // takes longer for port stats to reflect the sent bytes.
  for (auto i = 0; i < kMaxIterations; ++i) {
    auto pktsPrior = pktsAfter;
    auto bytesPrior = bytesAfter;
    std::tie(pktsAfter, bytesAfter) =
        getOutPktsAndBytes(ensemble, PortID(portUsed));
    if (pktsPrior == pktsAfter && bytesPrior == bytesAfter) {
      break;
    }
    XLOG(INFO) << " Stats still incrementing after iteration: " << i + 1;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (i == kMaxIterations - 1) {
      XLOG(INFO) << " Stats still incrementing after iteration: " << i + 1
                 << " Reported TX pps maybe lower than actual pps";
    }
  }
  pps = (static_cast<double>(pktsAfter - pktsBefore) /
         durationMillseconds.count()) *
      1000;
  bytesPerSec = (static_cast<double>(bytesAfter - bytesBefore) /
                 durationMillseconds.count()) *
      1000;
  return;
}

void initSystemScaleTest(
    AgentEnsemble* ensemble,
    const RouteDistributionGenerator& routeGenerator) {
  auto [rxPktsBefore, rxBytesBefore] = startRxMeasure(ensemble);
  auto timeBefore = std::chrono::steady_clock::now();
  configureMaxAclEntries(ensemble);
  configureMaxRouteEntries(ensemble, routeGenerator);
  configureMaxMacEntries(ensemble);
  configureMaxNeighborEntries(ensemble);
  auto [rxPktsAfter, rxBytesAfter] = stopRxMeasure(ensemble);
  auto timeAfter = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> durationMillseconds =
      timeAfter - timeBefore;
  auto rxPPS = static_cast<double>(rxPktsAfter - rxPktsBefore) * 1000 /
      durationMillseconds.count();
  auto rxBPS = static_cast<double>(rxBytesAfter - rxBytesBefore) * 1000 /
      durationMillseconds.count();

  if (FLAGS_json) {
    folly::dynamic memJson = folly::dynamic::object;

    memJson["rx_pps"] = rxPPS;
    memJson["rx_bps"] = rxBPS;
    memJson["tx_pps"] = 0;
    memJson["tx_bps"] = 0;
    std::cout << toPrettyJson(memJson) << std::endl;
  } else {
    XLOG(DBG2) << " rx_pps: " << rxPPS << " rx_bps: " << rxBPS;
  }

  if (FLAGS_delay_exit_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_delay_exit_ms));
  }
}

void initSystemScaleChurnTest(AgentEnsemble* ensemble) {
  if (ensemble->getSw()->getBootType() == BootType::WARM_BOOT) {
    return;
  }
  configureMaxAclEntries(ensemble);

  std::vector<PortID> portToFlap = {
      ensemble->masterLogicalInterfacePortIds()[0]};
  auto portFlapHelper = PortFlapHelper(ensemble, portToFlap);
  CHECK_GT(ensemble->masterLogicalInterfacePortIds().size(), 2);
  auto macLearningFloodHelper = MacLearningFloodHelper(
      ensemble,
      ensemble->masterLogicalInterfacePortIds()[kMacChurnTxPortIdx],
      ensemble->masterLogicalInterfacePortIds()[kMacChurnRxPortIdx],
      VlanID(kBaseVlanId));
  XLOG(INFO) << "configuring maximum acl entries";
  configureMaxMacEntries(ensemble);
  XLOG(INFO) << "starting port flaps";
  portFlapHelper.startPortFlap();
  auto asic = checkSameAndGetAsic(
      ensemble->getHwAsicTable()->getL3Asics(), FLAGS_switch_id_for_testing);

  if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK3 ||
      asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK4) {
    XLOG(INFO) << "churn mac table";
    macLearningFloodHelper.startChurnMacTable();
  }
  XLOG(INFO) << "start churn route entries";
  auto generator = ScaleTestRouteScaleGenerator(
      ensemble->getSw()->getState(),
      ensemble->getSw()->needL2EntryForNeighbor());
  configureMaxRouteEntries(ensemble, generator);
  if ((FLAGS_delay_churn_start_ms > 0) && (FLAGS_route_churn_rounds > 0)) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(FLAGS_delay_churn_start_ms));
  }
  for (auto i = 0; i < FLAGS_route_churn_rounds; i++) {
    removeAllRouteEntries(ensemble);
    configureMaxRouteEntries(ensemble, generator);
  }
  auto timeBefore = std::chrono::steady_clock::now();
  auto [rxPktsBefore, rxBytesBefore] = startRxMeasure(ensemble);
  XLOG(INFO) << "churn neighbor entries";
  configureMaxNeighborEntries(ensemble);
  if ((FLAGS_delay_churn_start_ms > 0) && (FLAGS_ndp_churn_rounds > 0)) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(FLAGS_delay_churn_start_ms));
  }
  for (auto i = 0; i < FLAGS_ndp_churn_rounds; i++) {
    removeAllNeighbors(ensemble);
    configureMaxNeighborEntries(ensemble);
  }

  portFlapHelper.stopPortFlap();

  auto [rxPktsAfter, rxBytesAfter] = stopRxMeasure(ensemble);
  auto timeAfter = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> durationMillseconds =
      timeAfter - timeBefore;
  if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK3 ||
      asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK4) {
    macLearningFloodHelper.stopChurnMacTable();
    XLOG(INFO) << "stop churn mac table";
  }

  int txPPS, txBPS;
  startTxMeasure(ensemble, txPPS, txBPS);

  XLOG(DBG2) << " rxPktsAfter: " << rxPktsAfter
             << " rxPktsBefore: " << rxPktsBefore
             << "rxBytesAfter :" << rxBytesAfter
             << " rxBytesBefore: " << rxBytesBefore
             << "duration Milliseconds: " << durationMillseconds.count();
  auto rxPPS = static_cast<double>(rxPktsAfter - rxPktsBefore) * 1000 /
      durationMillseconds.count();
  auto rxBPS = static_cast<double>(rxBytesAfter - rxBytesBefore) * 1000 /
      durationMillseconds.count();

  if (FLAGS_json) {
    folly::dynamic memJson = folly::dynamic::object;

    memJson["rx_pps"] = rxPPS;
    memJson["rx_bps"] = rxBPS;
    memJson["tx_pps"] = txPPS;
    memJson["tx_bps"] = txBPS;
    std::cout << toPrettyJson(memJson) << std::endl;
  } else {
    XLOG(DBG2) << " rx_pps: " << rxPPS << " rx_bps: " << rxBPS;
  }

  if (FLAGS_delay_exit_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_delay_exit_ms));
  }

  if (FLAGS_setup_for_warmboot) {
    ScopedCallTimer timeIt;
    // Static such that the object destructor runs as late as possible. In
    // particular in this case, destructor (and thus the duration calculation)
    // will run at the time of program exit when static variable destructors
    // run
    static StopWatch timer("warm_boot_msecs", FLAGS_json);
    ensemble->gracefulExit();
    // Leak HwSwitchEnsemble for warmboot, so that
    // we don't run destructors and unprogram h/w. We are
    // going to exit the process anyways.
    //__attribute__((unused)) auto leakedHwEnsemble = ensemble;
  }
}

} // namespace facebook::fboss::utility
