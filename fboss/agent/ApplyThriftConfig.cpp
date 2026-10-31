/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/ApplyThriftConfig.h"

#include <fboss/thrift_cow/nodes/ThriftMapNode-inl.h>
#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/gen/Base.h>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "fboss/agent/AclNexthopHandler.h"

#include "fboss/agent/AgentFeatures.h"

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/BufferUtils.h"
#include "fboss/agent/DsfStateUpdaterUtil.h"
#include "fboss/agent/FabricLinkMonitoring.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/LacpTypes.h"
#include "fboss/agent/LoadBalancerConfigApplier.h"
#include "fboss/agent/LoadBalancerUtils.h"
#include "fboss/agent/MacTableUtils.h"
#include "fboss/agent/PfcUtils.h"
#include "fboss/agent/RouteUpdateWrapper.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/SwitchInfoUtils.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/mpls_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/rib/MySidConfigUtils.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/AclTableGroup.h"
#include "fboss/agent/state/AclTableMap.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/AggregatePortMap.h"
#include "fboss/agent/state/ArpResponseTable.h"
#include "fboss/agent/state/BufferPoolConfig.h"
#include "fboss/agent/state/BufferPoolConfigMap.h"
#include "fboss/agent/state/ControlPlane.h"
#include "fboss/agent/state/FlowletSwitchingConfig.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/IpTunnel.h"
#include "fboss/agent/state/IpTunnelMap.h"
#include "fboss/agent/state/LabelForwardingInformationBase.h"
#include "fboss/agent/state/Mirror.h"
#include "fboss/agent/state/MirrorOnDropReport.h"
#include "fboss/agent/state/MirrorOnDropReportMap.h"
#include "fboss/agent/state/NdpResponseTable.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortFlowletConfig.h"
#include "fboss/agent/state/PortFlowletConfigMap.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/PortPgConfig.h"
#include "fboss/agent/state/PortQueue.h"
#include "fboss/agent/state/QcmConfig.h"
#include "fboss/agent/state/QosPolicyMap.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/state/SflowCollector.h"
#include "fboss/agent/state/SflowCollectorMap.h"
#include "fboss/agent/state/Srv6Tunnel.h"
#include "fboss/agent/state/Srv6TunnelMap.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/agent/state/VxlanTunnel.h"
#include "fboss/agent/state/VxlanTunnelMap.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <folly/Range.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"
#include "fboss/agent/rib/RouteUpdater.h"

#include "fboss/agent/hw/switch_asics/HwAsic.h"

using boost::container::flat_map;
using boost::container::flat_set;
using folly::CIDRNetwork;
using folly::IPAddress;
using folly::IPAddressFormatException;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using folly::StringPiece;
using std::make_shared;
using std::shared_ptr;

using namespace facebook::fboss;
namespace {

const uint8_t kV6LinkLocalAddrMask{64};
constexpr auto kMcQueueScalingFactor = cfg::MMUScalingFactor::ONE_8TH;
constexpr auto kHyperPortSpeed = cfg::PortSpeed::THREEPOINTTWOT;

// Only one buffer pool is supported systemwide. Variable to track the name
// and validate during a config change.
std::optional<std::string> sharedBufferPoolName;

// ECMP resource manager re-modes existing backup group routes' overrides
// whenever the backup switching mode changes.
bool ecmpOverrideModeChanged(
    const SwitchState& orig,
    const cfg::SwitchConfig& cfg) {
  std::optional<cfg::SwitchingMode> oldMode;
  if (auto flowletCfg = orig.getFlowletSwitchingConfig()) {
    oldMode = flowletCfg->getBackupSwitchingMode();
  }
  std::optional<cfg::SwitchingMode> newMode;
  if (auto flowletCfg = cfg.flowletSwitchingConfig()) {
    newMode = *flowletCfg->backupSwitchingMode();
  }
  return oldMode != newMode;
}

StateDelta updateFibFromConfig(
    const facebook::fboss::SwitchIdScopeResolver* resolver,
    facebook::fboss::RouterID vrf,
    const facebook::fboss::IPv4NetworkToRouteMap& v4NetworkToRoute,
    const facebook::fboss::IPv6NetworkToRouteMap& v6NetworkToRoute,
    const facebook::fboss::LabelToRouteMap& labelToRoute,
    facebook::fboss::NextHopIDManager const* nextHopIDManager,
    const facebook::fboss::MySidTable& mySidTable,
    void* cookie,
    bool vrfDeleted) {
  facebook::fboss::RibToSwitchStateUpdater ribToSwitchStateUpdater(
      resolver,
      vrf,
      v4NetworkToRoute,
      v6NetworkToRoute,
      labelToRoute,
      nextHopIDManager,
      mySidTable,
      facebook::fboss::RibToSwitchStateUpdater::kAllActions,
      vrfDeleted);

  auto nextStatePtr =
      static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);

  ribToSwitchStateUpdater(*nextStatePtr);
  auto lastDelta = ribToSwitchStateUpdater.getLastDelta();
  CHECK(lastDelta.has_value());
  return StateDelta(lastDelta->oldState(), *nextStatePtr);
}

template <typename MultiMap, typename EntryT>
std::shared_ptr<MultiMap> toMultiSwitchMap(
    const std::shared_ptr<EntryT>& entry,
    const facebook::fboss::SwitchIdScopeResolver& resolver) {
  auto multiMap = std::make_shared<MultiMap>();
  for (const auto& idAndNode : *entry) {
    multiMap->addNode(idAndNode.second, resolver.scope(idAndNode.second));
  }
  return multiMap;
}
template <typename MultiMap, typename Map>
std::shared_ptr<MultiMap> toMultiSwitchMap(
    const std::shared_ptr<Map>& map,
    const facebook::fboss::cfg::SwitchConfig& cfg,
    const facebook::fboss::SwitchIdScopeResolver& resolver) {
  auto multiMap = std::make_shared<MultiMap>();
  for (const auto& idAndNode : *map) {
    multiMap->addNode(idAndNode.second, resolver.scope(idAndNode.second, cfg));
  }
  return multiMap;
}

std::shared_ptr<AclTable> scopeAclTableToSwitch(
    const std::shared_ptr<AclTable>& aclTable,
    SwitchID switchId,
    const facebook::fboss::SwitchIdScopeResolver& resolver) {
  AclMap::NodeContainer scopedAcls;
  for (const auto& aclEntry : *aclTable->getAclMap()) {
    const auto& acl = aclEntry.second;
    if (resolver.scope(acl).has(switchId)) {
      scopedAcls.emplace(acl->getID(), acl);
    }
  }
  auto scopedAclTable = aclTable->clone();
  scopedAclTable->setAclMap(std::make_shared<AclMap>(std::move(scopedAcls)));
  return scopedAclTable;
}

std::shared_ptr<AclTableGroup> scopeAclTableGroupToSwitch(
    const std::shared_ptr<AclTableGroup>& aclTableGroup,
    SwitchID switchId,
    const facebook::fboss::SwitchIdScopeResolver& resolver) {
  auto scopedAclTableMap = std::make_shared<AclTableMap>();
  for (const auto& aclTableEntry : *aclTableGroup->getAclTableMap()) {
    scopedAclTableMap->addTable(
        scopeAclTableToSwitch(aclTableEntry.second, switchId, resolver));
  }
  auto scopedAclTableGroup = aclTableGroup->clone();
  scopedAclTableGroup->setAclTableMap(scopedAclTableMap);
  return scopedAclTableGroup;
}

std::shared_ptr<MultiSwitchAclTableGroupMap>
toScopedMultiSwitchAclTableGroupMap(
    const std::shared_ptr<AclTableGroupMap>& aclTableGroups,
    const facebook::fboss::SwitchIdScopeResolver& resolver) {
  auto multiSwitchAclTableGroups =
      std::make_shared<MultiSwitchAclTableGroupMap>();
  for (const auto& switchIdAndInfo : resolver.switchIdToSwitchInfo()) {
    auto switchId = static_cast<SwitchID>(switchIdAndInfo.first);
    auto switchMatcher =
        HwSwitchMatcher(std::unordered_set<SwitchID>({switchId}));
    for (const auto& aclTableGroupEntry : *aclTableGroups) {
      const auto& aclTableGroup = aclTableGroupEntry.second;
      if (!resolver.scope(aclTableGroup).has(switchId)) {
        continue;
      }
      multiSwitchAclTableGroups->addNode(
          scopeAclTableGroupToSwitch(aclTableGroup, switchId, resolver),
          switchMatcher);
    }
  }
  return multiSwitchAclTableGroups;
}

bool checkParallelLinksToInterfaceNodes(
    const cfg::SwitchConfig* cfg,
    const std::vector<SwitchID>& localFabricSwitchIds,
    const std::unordered_map<std::string, std::vector<uint32_t>>&
        switchNameToSwitchIds,
    SwitchIdScopeResolver& scopeResolver,
    const PlatformMapping* platformMapping) {
  bool hasParallelLinks = false;
  // Determine parallel links on VD level - there are two VDs per R3 ASIC
  // For each VD, we store neighbor2PortIDs

  std::unordered_map<
      int, // VD
      std::unordered_map< // neighbor2PortIDs
          std::string,
          std::unordered_set<int>>>
      vd2VoqNeighbors;
  for (const auto& fabricSwitchId : localFabricSwitchIds) {
    for (const auto& port : *cfg->ports()) {
      // Only process ports belonging to one switchId
      if (scopeResolver.scope(port).has(SwitchID(fabricSwitchId)) &&
          port.expectedNeighborReachability()->size() > 0) {
        auto neighborRemoteSwitchId =
            getRemoteSwitchID(cfg, port, switchNameToSwitchIds);
        const auto& neighborDsfNodeIter =
            cfg->dsfNodes()->find(neighborRemoteSwitchId);
        CHECK(neighborDsfNodeIter != cfg->dsfNodes()->end());
        if (*neighborDsfNodeIter->second.type() ==
            cfg::DsfNodeType::INTERFACE_NODE) {
          CHECK(port.name().has_value());
          auto localVirtualDeviceId =
              platformMapping->getVirtualDeviceID(*port.name());
          if (!localVirtualDeviceId.has_value()) {
            throw FbossError(
                "Unable to find virtual device id for port: ",
                *port.logicalID(),
                " virtual device");
          }

          if (vd2VoqNeighbors.find(localVirtualDeviceId.value()) ==
              vd2VoqNeighbors.end()) {
            vd2VoqNeighbors.insert({localVirtualDeviceId.value(), {}});
          }
          const auto& [neighborName, _] = getExpectedNeighborAndPortName(port);
          auto& voqNeighbors = vd2VoqNeighbors[localVirtualDeviceId.value()];
          auto iter = voqNeighbors.find(neighborName);
          if (iter != voqNeighbors.end()) {
            iter->second.insert(*port.logicalID());
            hasParallelLinks = true;
          } else {
            voqNeighbors.insert({neighborName, {*port.logicalID()}});
          }
        }
      }
    }
  }
  // Enforce interface node neighbors all have either single link or parallel
  // links
  for (const auto& [_, neighbor2PortIDs] : vd2VoqNeighbors) {
    for (const auto& [neighbor, neighborPorts] : neighbor2PortIDs) {
      if (hasParallelLinks && neighborPorts.size() == 1) {
        throw FbossError(
            "Expect parallel links to interface node facing neighbor per VD. However, ",
            neighbor,
            " has only 1 link");
      }
    }
  }
  return hasParallelLinks;
}

bool isValidRxReasonToQueue(const auto& rxReasonToQueue) {
  // FBOSS config exposes two different reason codes for TTLs: TTL_0 and TTL_1.
  // For TTL_0, FBOSS configures packet action FORWARD.
  // For TTL_1, FBOSS configures packet action TRAP.
  //
  // However, SAI spec defines a single attribute to match for TTL 0 and TTL 1
  // viz.: SAI_HOSTIF_TRAP_TYPE_TTL_ERROR
  //
  // Thus, if config carries both TTL_0 and TTL_1, the second field overrides
  // the first one and results into unexpected/buggy behavior.
  //
  // TTL_0 use case is for test: don't drop TTL 0 packets, allow creating loop.
  // TTL_1 use case is for production only.
  // Thus, explicitly fail config that attempts to set both TTL_0 and TTL1.

  if (!rxReasonToQueue.has_value()) {
    return true;
  }

  bool isTtl0 = false;
  bool isTtl1 = false;
  for (auto rxEntry : *rxReasonToQueue) {
    if (*rxEntry.rxReason() == cfg::PacketRxReason::TTL_0) {
      isTtl0 = true;
    } else if (*rxEntry.rxReason() == cfg::PacketRxReason::TTL_1) {
      isTtl1 = true;
    }
  }

  if (isTtl0 && isTtl1) {
    XLOG(ERR)
        << "Setting RxReasons TTL_0 and TTL_1 simultaneously is unsupported";
    return false;
  }

  return true;
}

} // anonymous namespace

namespace facebook::fboss {

/*
 * A class for implementing applyThriftConfig().
 *
 * This implements a procedural function.  It is defined as a class purely as a
 * convenience for the implementation, to allow easily sharing state between
 * internal helper methods.
 */
class ThriftConfigApplier {
 public:
  ThriftConfigApplier(
      const std::shared_ptr<SwitchState>& orig,
      const cfg::SwitchConfig* config,
      bool supportsAddRemovePort,
      RoutingInformationBase* rib,
      AclNexthopHandler* aclNexthopHandler,
      const PlatformMapping* platformMapping,
      const HwAsicTable* hwAsicTable)
      : orig_(orig),
        cfg_(config),
        supportsAddRemovePort_(supportsAddRemovePort),
        rib_(rib),
        aclNexthopHandler_(aclNexthopHandler),
        scopeResolver_(getSwitchInfoFromConfig(config)),
        platformMapping_(platformMapping),
        hwAsicTable_(hwAsicTable) {}

  ThriftConfigApplier(
      const std::shared_ptr<SwitchState>& orig,
      const cfg::SwitchConfig* config,
      bool supportsAddRemovePort,
      RouteUpdateWrapper* routeUpdater,
      AclNexthopHandler* aclNexthopHandler,
      const PlatformMapping* platformMapping,
      const HwAsicTable* hwAsicTable)
      : orig_(orig),
        cfg_(config),
        supportsAddRemovePort_(supportsAddRemovePort),
        routeUpdater_(routeUpdater),
        aclNexthopHandler_(aclNexthopHandler),
        scopeResolver_(getSwitchInfoFromConfig(config)),
        platformMapping_(platformMapping),
        hwAsicTable_(hwAsicTable) {}

  std::shared_ptr<SwitchState> run();

 private:
  // Forbidden copy constructor and assignment operator
  ThriftConfigApplier(ThriftConfigApplier const&) = delete;
  ThriftConfigApplier& operator=(ThriftConfigApplier const&) = delete;

  template <typename Node, typename NodeMap>
  bool updateMap(
      NodeMap* map,
      std::shared_ptr<Node> origNode,
      std::shared_ptr<Node> newNode) {
    if (newNode) {
      auto ret = map->emplace(std::make_pair(newNode->getID(), newNode));
      if (!ret.second) {
        throw FbossError("duplicate entry ", newNode->getID());
      }
      return true;
    } else {
      auto ret = map->emplace(std::make_pair(origNode->getID(), origNode));
      if (!ret.second) {
        throw FbossError("duplicate entry ", origNode->getID());
      }
      return false;
    }
  }

  template <typename Node, typename NodeMap>
  bool updateThriftMapNode(
      NodeMap* map,
      std::shared_ptr<Node> origNode,
      std::shared_ptr<Node> newNode) {
    auto node = (newNode != nullptr) ? newNode : origNode;
    auto key = node->getID();
    auto ret = map->insert(key, std::move(node));
    if (!ret.second) {
      throw FbossError("duplicate entry ", key);
    }
    return newNode != nullptr;
  }

  // Interface route prefix. IPAddress has mask applied
  using IntfAddress = std::pair<InterfaceID, folly::IPAddress>;
  using IntfRoute = boost::container::flat_map<folly::CIDRNetwork, IntfAddress>;
  using IntfRouteTable = boost::container::flat_map<RouterID, IntfRoute>;
  IntfRouteTable intfRouteTables_;

  /* The ThriftConfigApplier object exposes a single, top-level method "run()".
   * In this method, a previous SwitchState "orig_" is first cloned and the
   * clone modified until it matches the specifications of the SwitchConfig
   * "cfg_". The private methods of ThriftConfigApplier implement the logic
   * necessary to perform these modifications.
   *
   * These methods generally follow a common scheme to do so based on each
   * SwitchState node being uniquely identified by an ID within the set of nodes
   * of the same type. For instance, a VLAN node is uniquely identified by
   * its "const VlanID id" member variable. No other VLAN may have the same
   * ID. But it is entirely possible for there to exist an Interface node with
   * the same numerical ID (ignoring type incompatibility between VlanID and
   * InterfaceID).
   *
   * There are 3 cases to consider:
   *
   * 1) cfg_ and orig_ both have a node with the same ID
   *    If the specifications in cfg_ differ from those of orig_, then the
   *    clone of the node is updated appropriately. This functionality is
   *    provided by methods such as updateAggPort(), updateVlan(), etc.
   * 2) cfg_ has a node with an ID that does not exist in orig_
   *    A node with this ID is added to the cloned SwitchState. This
   *    functionality is provided by methods such as createAggPort(),
   *    createVlan(), etc.
   * 3) orig_ has a node with an ID that does not exist in cfg_
   *    This node is implicity deleted in the clone.
   *
   * Methods such as updateAggregatePorts(), updateVlans(), etc. encapsulate
   * this logic for each type of NodeBase.
   */

  void processVlanPorts();
  void updateVlanInterfaces(const Interface* intf);
  std::shared_ptr<PortMap> updatePorts(
      const std::shared_ptr<MultiSwitchTransceiverMap>& transceiverMap);
  shared_ptr<SystemPortMap> updateFabricLinkMonitoringSystemPorts(
      const std::shared_ptr<MultiSwitchPortMap>& ports,
      const std::shared_ptr<MultiSwitchSettings>& multiSwitchSettings);
  std::shared_ptr<SystemPortMap> updateSystemPorts(
      const std::shared_ptr<MultiSwitchPortMap>& ports,
      const std::shared_ptr<MultiSwitchSettings>& multiSwitchSettings);
  std::shared_ptr<MultiSwitchSystemPortMap> updateRemoteSystemPorts(
      const std::shared_ptr<SystemPortMap>& systemPorts);
  bool needFabricLinkMonSystemPortUpdate(
      const std::shared_ptr<MultiSwitchSettings>& origMultiSwitchSettings,
      const std::shared_ptr<MultiSwitchSettings>& newMultiSwitchSettings,
      const SwitchIdScopeResolver& scopeResolver);
  std::optional<int32_t> getFabricLinkMonitoringPortSwitchId(
      const PortID& portId,
      const cfg::PortType& type,
      const size_t expectedNeighborCount) const;

  std::shared_ptr<Port> updatePort(
      const std::shared_ptr<Port>& orig,
      const cfg::Port* cfg,
      const std::shared_ptr<TransceiverSpec>& transceiver);
  QueueConfig updatePortQueues(
      const std::vector<std::shared_ptr<PortQueue>>& origPortQueues,
      const std::vector<cfg::PortQueue>& cfgPortQueues,
      uint16_t baseQueueId,
      uint16_t maxQueues,
      cfg::StreamType streamType,
      std::optional<cfg::QosMap> qosMap = std::nullopt,
      std::optional<cfg::PortType> portType = std::nullopt,
      bool resetDefaultQueue = true);
  // update cfg port queue attribute to state port queue object
  void setPortQueue(
      std::shared_ptr<PortQueue> newQueue,
      const cfg::PortQueue* cfg);
  std::optional<std::vector<int16_t>> findEnabledPfcPriorities(
      PortPgConfigs& portPgConfigs,
      const std::map<int16_t, int16_t>& pfcPriorityToPgId);
  std::shared_ptr<PortQueue> updatePortQueue(
      const std::shared_ptr<PortQueue>& orig,
      const cfg::PortQueue* cfg,
      std::optional<TrafficClass> trafficClass,
      std::optional<std::set<PfcPriority>> pfcPriority);
  std::shared_ptr<PortQueue> createPortQueue(
      const cfg::PortQueue* cfg,
      std::optional<TrafficClass> trafficClass,
      std::optional<std::set<PfcPriority>> pfcPriority);
  // pg specific routines
  bool isPgConfigUnchanged(
      std::optional<PortPgConfigs> newPortPgCfgs,
      const shared_ptr<Port>& orig);
  void validateUpdatePgBufferPoolName(
      const PortPgConfigs& portPgCfgs,
      const shared_ptr<Port>& port,
      const std::string& portPgName);
  std::shared_ptr<PortPgConfig> createPortPg(const cfg::PortPgConfig* cfg);
  PortPgConfigs updatePortPgConfigs(
      const std::vector<cfg::PortPgConfig>& newPortPgConfig,
      const shared_ptr<Port>& orig);
  bool isPortFlowletConfigUnchanged(
      std::shared_ptr<PortFlowletCfg> newPortFlowletCfg,
      const shared_ptr<Port>& port);
  bool isLlrConfigUnchanged(
      std::shared_ptr<LlrConfig> newLlrConfig,
      const shared_ptr<Port>& port);
  void checkPortQueueAQMValid(
      const std::vector<cfg::ActiveQueueManagement>& aqms);
  std::shared_ptr<AggregatePortMap> updateAggregatePorts();
  std::shared_ptr<AggregatePort> updateAggPort(
      const std::shared_ptr<AggregatePort>& orig,
      const cfg::AggregatePort& cfg);
  std::shared_ptr<AggregatePort> createAggPort(const cfg::AggregatePort& cfg);
  std::vector<AggregatePort::Subport> getSubportsSorted(
      const cfg::AggregatePort& cfg);
  std::vector<int32_t> getAggregatePortInterfaceIDs(
      const std::vector<AggregatePort::Subport>& subports);
  std::pair<folly::MacAddress, uint16_t> getSystemLacpConfig();
  uint8_t computeMinimumLinkCount(
      const cfg::MinimumCapacity& minCapacity,
      size_t memberPortsSize);
  std::shared_ptr<VlanMap> updateVlans();
  bool updateMacTable(
      std::shared_ptr<Vlan>& newVlan,
      const std::set<PortDescriptor>& portDescs);
  std::map<uint32_t, std::set<PortDescriptor>>
  getMapOfVlanToPortsNeedingMacClear(
      const std::shared_ptr<MultiSwitchPortMap> newPorts,
      const std::shared_ptr<MultiSwitchPortMap> origPorts);
  std::shared_ptr<Vlan> createVlan(const cfg::Vlan* config);
  std::shared_ptr<Vlan> updateVlan(
      const std::shared_ptr<Vlan>& orig,
      const cfg::Vlan* config,
      const std::set<PortDescriptor>& portDescs);
  std::shared_ptr<AclTableGroup> updateAclTableGroup(
      cfg::AclStage aclStage,
      const cfg::AclTableGroup& cfgAclTableGroup,
      const std::shared_ptr<AclTableGroup>& origAclTableGroup);
  struct AclTableGroupMaps {
    std::shared_ptr<AclTableGroupMap> switchBound;
    std::shared_ptr<AclTableGroupMap> portBound;
  };
  AclTableGroupMaps updateAclTableGroups();
  void validatePortIngressAcls() const;
  flat_map<std::string, const cfg::AclEntry*> getAllAclsByName(
      const cfg::AclTableGroup& cfgAclTableGroup);
  void checkTrafficPolicyAclsExistInConfig(
      const cfg::TrafficPolicyConfig& policy,
      flat_map<std::string, const cfg::AclEntry*> aclByName);
  std::shared_ptr<AclTable> updateAclTable(
      cfg::AclStage aclStage,
      const cfg::AclTable& configTable,
      int* numExistingTablesProcessed);
  std::shared_ptr<AclMap> updateAclsImpl(
      cfg::AclStage aclStage,
      std::vector<cfg::AclEntry> configEntries,
      std::optional<std::string> tableName = std::nullopt);
  std::shared_ptr<AclMap> updateAcls(
      cfg::AclStage aclStage,
      std::vector<cfg::AclEntry> configEntries);
  std::shared_ptr<AclMap> updateAclsForTable(
      cfg::AclStage aclStage,
      std::vector<cfg::AclEntry> configEntries,
      std::optional<std::string> tableName = std::nullopt);
  std::shared_ptr<AclEntry> createAcl(
      const cfg::AclEntry* config,
      int priority,
      const MatchAction* action = nullptr,
      bool enable = true);
  void checkUdfAcl(const std::vector<std::string>& udfGroups) const;
  std::shared_ptr<AclEntry> updateAcl(
      cfg::AclStage aclStage,
      const cfg::AclEntry& acl,
      int priority,
      int* numExistingProcessed,
      bool* changed,
      std::optional<std::string> tableName,
      const MatchAction* action = nullptr,
      bool enable = true);
  // check the acl provided by config is valid
  void checkAcl(const cfg::AclEntry* config) const;
  std::shared_ptr<QosPolicyMap> updateQosPolicies();
  std::shared_ptr<QosPolicy> updateQosPolicy(
      cfg::QosPolicy& qosPolicy,
      int* numExistingProcessed,
      bool* changed);
  std::optional<std::string> getDefaultDataPlaneQosPolicyName() const;
  std::shared_ptr<QosPolicy> updateDataplaneDefaultQosPolicy();
  shared_ptr<QosPolicy> createQosPolicy(const cfg::QosPolicy& qosPolicy);
  struct InterfaceIpInfo;
  template <typename NeighborResponseEntry, typename IPAddr>
  std::shared_ptr<NeighborResponseEntry> updateNeighborResponseEntry(
      const std::shared_ptr<NeighborResponseEntry>& orig,
      IPAddr ip,
      InterfaceIpInfo addrInfo);
  template <typename VlanOrIntfT, typename CfgVlanOrIntfT>
  bool updateDhcpOverrides(
      VlanOrIntfT* vlanOrIntf,
      const CfgVlanOrIntfT* config);
  std::shared_ptr<InterfaceMap> updateInterfaces();
  std::shared_ptr<MultiSwitchInterfaceMap> updateRemoteInterfaces(
      const std::shared_ptr<MultiSwitchInterfaceMap>& interfaces);

  shared_ptr<Interface> createInterface(
      const cfg::Interface* config,
      const Interface::Addresses& addrs);
  shared_ptr<Interface> updateInterface(
      const shared_ptr<Interface>& orig,
      const cfg::Interface* config,
      const Interface::Addresses& addrs);
  bool updateNeighborResponseTablesForIntfs(
      Interface* intf,
      const Interface::Addresses& addrs);
  std::string getInterfaceName(const cfg::Interface* config);
  folly::MacAddress getInterfaceMac(const cfg::Interface* config);
  Interface::Addresses getInterfaceAddresses(const cfg::Interface* config);
  shared_ptr<SflowCollectorMap> updateSflowCollectors();
  shared_ptr<SflowCollector> createSflowCollector(
      const cfg::SflowCollector* config);
  shared_ptr<SflowCollector> updateSflowCollector(
      const shared_ptr<SflowCollector>& orig,
      const cfg::SflowCollector* config);
  shared_ptr<MultiSwitchSettings> updateMultiSwitchSettings();
  shared_ptr<SwitchSettings> updateSwitchSettings(
      HwSwitchMatcher matcher,
      const std::shared_ptr<MultiSwitchSettings>& origSwitchSettings);

  // bufferPool specific configs
  shared_ptr<MultiSwitchBufferPoolCfgMap> updateBufferPoolConfigs(
      bool* changed);
  std::shared_ptr<BufferPoolCfg> createBufferPoolConfig(
      const std::string& id,
      const cfg::BufferPoolConfig& config);
  shared_ptr<QcmCfg> updateQcmCfg(bool* changed);
  shared_ptr<QcmCfg> createQcmCfg(const cfg::QcmConfig& config);
  shared_ptr<MultiControlPlane> updateControlPlane();
  std::shared_ptr<MultiSwitchMirrorMap> updateMirrors();
  std::shared_ptr<Mirror> createMirror(const cfg::Mirror* config);
  std::shared_ptr<Mirror> updateMirror(
      const std::shared_ptr<Mirror>& orig,
      const cfg::Mirror* config);
  std::shared_ptr<FibInfo> updateForwardingInformationBaseInfo();

  std::shared_ptr<MultiSwitchMirrorOnDropReportMap> updateMirrorOnDropReports();
  std::shared_ptr<MirrorOnDropReport> createMirrorOnDropReport(
      const cfg::MirrorOnDropReport& config);
  std::shared_ptr<MirrorOnDropReport> updateMirrorOnDropReport(
      const std::shared_ptr<MirrorOnDropReport>& orig,
      const cfg::MirrorOnDropReport& config);

  std::shared_ptr<LabelForwardingEntry> createLabelForwardingEntry(
      MplsLabel label,
      LabelNextHopEntry::Action action,
      LabelNextHopSet nexthops);

  LabelNextHopEntry getStaticLabelNextHopEntry(
      LabelNextHopEntry::Action action,
      LabelNextHopSet nexthops);

  std::shared_ptr<MultiLabelForwardingInformationBase> updateStaticMplsRoutes(
      const std::vector<cfg::StaticMplsRouteWithNextHops>&
          staticMplsRoutesWithNhops,
      const std::vector<cfg::StaticMplsRouteNoNextHops>& staticMplsRoutesToNull,
      const std::vector<cfg::StaticMplsRouteNoNextHops>& staticMplsRoutesToCPU);

  shared_ptr<IpTunnel> createIpInIpTunnel(const cfg::IpInIpTunnel& config);
  shared_ptr<IpTunnel> updateIpInIpTunnel(
      const std::shared_ptr<IpTunnel>& orig,
      const cfg::IpInIpTunnel* config);
  std::shared_ptr<IpTunnelMap> updateIpInIpTunnels();
  shared_ptr<Srv6Tunnel> createSrv6Tunnel(const cfg::Srv6Tunnel& config);
  shared_ptr<Srv6Tunnel> updateSrv6Tunnel(
      const std::shared_ptr<Srv6Tunnel>& orig,
      const cfg::Srv6Tunnel* config);
  std::shared_ptr<Srv6TunnelMap> updateSrv6Tunnels();
  shared_ptr<VxlanTunnel> createVxlanTunnel(const cfg::VxLanTunnel& config);
  shared_ptr<VxlanTunnel> updateVxlanTunnel(
      const std::shared_ptr<VxlanTunnel>& orig,
      const cfg::VxLanTunnel* config);
  std::shared_ptr<VxlanTunnelMap> updateVxlanTunnels();
  const std::vector<cfg::VrfConfig>& getConfigVrfs() const;
  void validateVrfs() const;
  std::shared_ptr<VrfMap> updateVrfs();
  std::shared_ptr<DsfNodeMap> updateDsfNodes();
  void processUpdatedDsfNodes();
  void processReachabilityGroup(
      const std::vector<SwitchID>& localFabricSwitchIds);
  void validateUdfConfig(const UdfConfig& newUdfConfig);
  std::shared_ptr<UdfConfig> updateUdfConfig(bool* changed);

  void processInterfaceForPortForNonVoqSwitches(int64_t switchId);
  void processInterfaceForPortForVoqSwitches(int64_t switchId);
  void processInterfaceForPort();

  bool processRemovedStaticMacEntries();
  bool processAddedStaticMacEntries();

  shared_ptr<FlowletSwitchingConfig> updateFlowletSwitchingConfig(
      bool* changed);
  shared_ptr<FlowletSwitchingConfig> createFlowletSwitchingConfig(
      const cfg::FlowletSwitchingConfig& config);

  shared_ptr<MultiSwitchPortFlowletCfgMap> updatePortFlowletConfigs(
      bool* changed);
  std::shared_ptr<PortFlowletCfg> createPortFlowletConfig(
      const std::string& id,
      const cfg::PortFlowletConfig& config);
  shared_ptr<MultiSwitchLlrConfigMap> updateLlrConfigs(bool* changed);
  std::shared_ptr<LlrConfig> createLlrConfig(
      const std::string& id,
      const cfg::LlrConfig& config);

  uint32_t generateDeterministicSeed(cfg::LoadBalancerID id);

  folly::MacAddress getLocalMac(SwitchID switchId) const;
  SwitchID getSwitchId(const cfg::Interface& intfConfig) const;
  std::optional<SwitchID> getAnySwitchId(cfg::SwitchType switchType);
  std::vector<SwitchID> getLocalFabricSwitchIds() const;
  std::optional<QueueConfig> getDefaultVoqConfigIfChanged(
      std::shared_ptr<SwitchSettings> switchSettings);
  QueueConfig getVoqConfig(PortID portId);
  void updateSystemPortSelfHealingEcmpLagDestinationEnable(bool enable);

  std::shared_ptr<SwitchState> orig_;
  std::shared_ptr<SwitchState> new_;
  const cfg::SwitchConfig* cfg_{nullptr};
  bool supportsAddRemovePort_{false};
  RoutingInformationBase* rib_{nullptr};
  RouteUpdateWrapper* routeUpdater_{nullptr};
  AclNexthopHandler* aclNexthopHandler_{nullptr};
  SwitchIdScopeResolver scopeResolver_;
  const PlatformMapping* platformMapping_{nullptr};
  const HwAsicTable* hwAsicTable_{nullptr};
  std::unique_ptr<const FabricLinkMonitoring> fabricLinkMon_;

  struct InterfaceIpInfo {
    InterfaceIpInfo(uint8_t mask, MacAddress mac, InterfaceID intf)
        : mask(mask), mac(mac), interfaceID(intf) {}

    uint8_t mask;
    MacAddress mac;
    InterfaceID interfaceID;
  };
  struct InterfaceInfo {
    RouterID routerID{0};
    flat_set<InterfaceID> interfaces;
    flat_map<IPAddress, InterfaceIpInfo> addresses;
  };

  flat_map<PortID, Port::VlanMembership> portVlans_;
  flat_map<VlanID, Vlan::MemberPorts> vlanPorts_;
  flat_map<VlanID, InterfaceInfo> vlanInterfaces_;
  flat_map<PortID, std::vector<int32_t>> port2InterfaceId_;
};

shared_ptr<SwitchState> ThriftConfigApplier::run() {
  new_ = orig_->clone();
  bool changed = false;

  {
    auto newControlPlane = updateControlPlane();
    if (newControlPlane) {
      new_->resetControlPlane(std::move(newControlPlane));
      changed = true;
    }
  }

  processVlanPorts();

  {
    bool bufferPoolConfigChanged = false;
    auto newBufferPoolCfg = updateBufferPoolConfigs(&bufferPoolConfigChanged);
    if (bufferPoolConfigChanged) {
      new_->resetBufferPoolCfgs(newBufferPoolCfg);
      changed = true;
    }
  }

  {
    bool portFlowletConfigChanged = false;
    auto newPortFlowletCfg =
        updatePortFlowletConfigs(&portFlowletConfigChanged);
    if (portFlowletConfigChanged) {
      new_->resetPortFlowletCfgs(newPortFlowletCfg);
      changed = true;
    }
  }

  {
    bool llrConfigChanged = false;
    auto newLlrCfg = updateLlrConfigs(&llrConfigChanged);
    if (llrConfigChanged) {
      new_->resetLlrConfigs(newLlrCfg);
      changed = true;
    }
  }

  auto newMultiSwitchSettings = updateMultiSwitchSettings();
  if (newMultiSwitchSettings) {
    new_->resetSwitchSettings(newMultiSwitchSettings);
    changed = true;
  }

  processInterfaceForPort();
  if (FLAGS_enable_fabric_link_monitoring) {
    // Create the fabric link mon object in case we have
    // fabric link monitoring enabled.
    fabricLinkMon_ = std::make_unique<FabricLinkMonitoring>(cfg_);
  }

  {
    auto newPorts = updatePorts(new_->getTransceivers());
    if (newPorts) {
      new_->resetPorts(
          toMultiSwitchMap<MultiSwitchPortMap>(newPorts, scopeResolver_));
      auto newSystemPorts =
          updateSystemPorts(new_->getPorts(), new_->getSwitchSettings());
      new_->resetSystemPorts(
          toMultiSwitchMap<MultiSwitchSystemPortMap>(
              newSystemPorts, scopeResolver_));
      new_->resetRemoteSystemPorts(updateRemoteSystemPorts(newSystemPorts));
      changed = true;
    }
  }

  {
    if (FLAGS_enable_fabric_link_monitoring &&
        needFabricLinkMonSystemPortUpdate(
            orig_->getSwitchSettings(),
            new_->getSwitchSettings(),
            scopeResolver_)) {
      new_->resetFabricLinkMonitoringSystemPorts(
          toMultiSwitchMap<MultiSwitchSystemPortMap>(
              updateFabricLinkMonitoringSystemPorts(
                  new_->getPorts(), new_->getSwitchSettings()),
              scopeResolver_));
      changed = true;
    }
  }

  {
    auto newAggPorts = updateAggregatePorts();
    if (newAggPorts) {
      new_->resetAggregatePorts(
          toMultiSwitchMap<MultiSwitchAggregatePortMap>(
              newAggPorts, scopeResolver_));
      changed = true;
    }

    // Collect IDs before modifying to avoid iterator invalidation:
    // modify(&new_) can clone the map being iterated.
    std::vector<AggregatePortID> aggPortIds;
    for (const auto& idAndAggPorts :
         std::as_const(*new_->getAggregatePorts())) {
      for (const auto& idAndAggPort : std::as_const(*idAndAggPorts.second)) {
        aggPortIds.emplace_back(idAndAggPort.first);
      }
    }
    for (auto aggPortId : aggPortIds) {
      auto aggPort = new_->getAggregatePorts()->getNodeIf(aggPortId);
      if (!aggPort) {
        continue;
      }
      auto capacityResult =
          computeAggregatePortCapacityAndStatus(aggPort, new_);

      if (aggPort->getConfiguredCapacityMbps() !=
              capacityResult.configuredCapacityMbps ||
          aggPort->getActiveCapacityMbps() !=
              capacityResult.activeCapacityMbps ||
          aggPort->getStatus() != capacityResult.status) {
        auto* writableAggPort = aggPort->modify(&new_);
        if (capacityResult.configuredCapacityMbps.has_value()) {
          writableAggPort->setConfiguredCapacityMbps(
              *capacityResult.configuredCapacityMbps);
        } else {
          writableAggPort->clearConfiguredCapacityMbps();
        }
        if (capacityResult.activeCapacityMbps.has_value()) {
          writableAggPort->setActiveCapacityMbps(
              *capacityResult.activeCapacityMbps);
        } else {
          writableAggPort->clearActiveCapacityMbps();
        }
        writableAggPort->setStatus(capacityResult.status);
        changed = true;
      }
    }
  }

  // updateMirrors must be called after updatePorts, mirror needs ports!
  {
    auto newMirrors = updateMirrors();
    if (newMirrors) {
      new_->resetMirrors(newMirrors);
      changed = true;
    }
  }

  // updateAcls must be called after updateMirrors, acls may need mirror!
  {
    if (FLAGS_enable_acl_table_group) {
      auto newAclTableGroups = updateAclTableGroups();
      if (newAclTableGroups.switchBound) {
        new_->resetAclTableGroups(toScopedMultiSwitchAclTableGroupMap(
            newAclTableGroups.switchBound, scopeResolver_));
        changed = true;
      }
      if (newAclTableGroups.portBound) {
        new_->resetPortAclTableGroups(toScopedMultiSwitchAclTableGroupMap(
            newAclTableGroups.portBound, scopeResolver_));
        changed = true;
      }
    } else {
      auto newAcls = updateAcls(cfg::AclStage::INGRESS, *cfg_->acls());
      if (newAcls) {
        new_->resetAcls(
            toMultiSwitchMap<MultiSwitchAclMap>(
                std::move(newAcls), scopeResolver_));
        changed = true;
      }
    }
  }
  validatePortIngressAcls();

  {
    auto newQosPolicies = updateQosPolicies();
    if (newQosPolicies) {
      new_->resetQosPolicies(
          toMultiSwitchMap<MultiSwitchQosPolicyMap>(
              newQosPolicies, scopeResolver_));
      changed = true;
    }
  }

  {
    auto newIntfs = updateInterfaces();
    if (newIntfs) {
      new_->resetIntfs(
          toMultiSwitchMap<MultiSwitchInterfaceMap>(
              std::move(newIntfs), *cfg_, scopeResolver_));
      new_->resetRemoteIntfs(updateRemoteInterfaces(new_->getInterfaces()));
      changed = true;
    }
  }

  // Remove static MAC entries before VLAN processing
  if (processRemovedStaticMacEntries()) {
    changed = true;
  }

  // Note: updateInterfaces() must be called before updateVlans(),
  // as updateInterfaces() populates the vlanInterfaces_ data structure.
  {
    auto newVlans = updateVlans();
    if (newVlans) {
      new_->resetVlans(
          toMultiSwitchMap<MultiSwitchVlanMap>(newVlans, scopeResolver_));
      changed = true;
    }
  }

  // Process static MAC entries after VLANs are updated
  if (processAddedStaticMacEntries()) {
    changed = true;
  }

  // Convert mySidConfig to (MySid state, unresolved next-hops) pairs up
  // front. Building the port-name -> InterfaceID map and resolving
  // adjacency entries happens here in the ApplyConfig caller thread, so any
  // config errors (e.g., unknown port name) surface before we hand work off
  // to the RIB event-base thread, and the RIB layer doesn't have to know
  // about cfg::MySidConfig at all.
  std::vector<MySidWithNextHops> staticMySids;
  if (auto mySidConfig = cfg_->mySidConfig().to_optional()) {
    staticMySids =
        convertMySidConfig(*mySidConfig, buildPortNameToInterfaceIdMap(*cfg_));
  }

  std::vector<std::pair<RouterID, IntfRoute>> vrfRouteTables;
  for (const auto& vrfCfg : getConfigVrfs()) {
    vrfRouteTables.emplace_back(RouterID(*vrfCfg.vrfId()), IntfRoute{});
  }
  // Keeps existing entries for VRFs that already have interface routes
  intfRouteTables_.insert(vrfRouteTables.begin(), vrfRouteTables.end());

  if (routeUpdater_) {
    routeUpdater_->setRoutesToConfig(
        intfRouteTables_,
        *cfg_->staticRoutesWithNhops(),
        *cfg_->staticRoutesToNull(),
        *cfg_->staticRoutesToCPU(),
        *cfg_->staticIp2MplsRoutes(),
        *cfg_->staticMplsRoutesWithNhops(),
        *cfg_->staticMplsRoutesToNull(),
        *cfg_->staticMplsRoutesToCPU(),
        std::move(staticMySids),
        ecmpOverrideModeChanged(*orig_, *cfg_));
  } else if (rib_) {
    auto newFibInfo = updateForwardingInformationBaseInfo();
    if (newFibInfo) {
      auto newFibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
      newFibInfoMap->updateFibInfo(
          newFibInfo, scopeResolver_.scope(newFibInfo));
      new_->resetFibsInfoMap(newFibInfoMap);
      changed = true;
    }

    // reconfigure() below resolves routes, so set the width first.
    rib_->setEcmpWidth(getEcmpWidth(new_));

    rib_->reconfigure(
        &scopeResolver_,
        intfRouteTables_,
        *cfg_->staticRoutesWithNhops(),
        *cfg_->staticRoutesToNull(),
        *cfg_->staticRoutesToCPU(),
        *cfg_->staticIp2MplsRoutes(),
        *cfg_->staticMplsRoutesWithNhops(),
        *cfg_->staticMplsRoutesToNull(),
        *cfg_->staticMplsRoutesToCPU(),
        staticMySids,
        &updateFibFromConfig,
        static_cast<void*>(&new_),
        ecmpOverrideModeChanged(*orig_, *cfg_));
  } else {
    // switch state UTs don't necessary care about RIB updates
    XLOG(WARNING)
        << " Ignoring config updates to rib, should never happen outside of tests";
  }

  // resolving mpls next hops may need interfaces to be setup
  // process static mpls routes after processing interfaces
  auto labelFib = updateStaticMplsRoutes(
      *cfg_->staticMplsRoutesWithNhops(),
      *cfg_->staticMplsRoutesToNull(),
      *cfg_->staticMplsRoutesToNull());
  if (labelFib) {
    new_->resetLabelForwardingInformationBase(labelFib);
    changed = true;
  }

  auto newVlans = new_->getVlans();
  VlanID dfltVlan(*cfg_->defaultVlan());
  if (orig_->getDefaultVlan() != dfltVlan) {
    if (newVlans->getNodeIf(dfltVlan) == nullptr) {
      throw FbossError("Default VLAN ", dfltVlan, " does not exist");
    }
  }

  // Make sure all interfaces refer to valid VLANs.
  for (const auto& vlanInfo : vlanInterfaces_) {
    if (newVlans->getNodeIf(vlanInfo.first) == nullptr) {
      throw FbossError(
          "Interface ",
          *(vlanInfo.second.interfaces.begin()),
          " refers to non-existent VLAN ",
          vlanInfo.first);
    }
    // Make sure there is a one-to-one map between vlan and interface
    // Remove this sanity check if multiple interfaces are allowed per vlans
    auto& entry = vlanInterfaces_[vlanInfo.first];
    if (entry.interfaces.size() != 1) {
      auto cpu_vlan = new_->getDefaultVlan();
      if (vlanInfo.first != cpu_vlan) {
        throw FbossError(
            "Vlan ",
            vlanInfo.first,
            " refers to ",
            entry.interfaces.size(),
            " interfaces ");
      }
    }
  }

  // Add sFlow collectors
  {
    auto newCollectors = updateSflowCollectors();
    if (newCollectors) {
      new_->resetSflowCollectors(
          toMultiSwitchMap<MultiSwitchSflowCollectorMap>(
              newCollectors, scopeResolver_));
      changed = true;
    }
  }

  // MirrorOnDrop must come after interfaces, since it looks up interface IPs.
  {
    auto newMirrorOnDropReports = updateMirrorOnDropReports();
    if (newMirrorOnDropReports) {
      new_->resetMirrorOnDropReports(newMirrorOnDropReports);
      changed = true;
    }
  }

  {
    LoadBalancerConfigApplier loadBalancerConfigApplier(
        orig_->getLoadBalancers(), cfg_->loadBalancers().value());
    auto newLoadBalancers = loadBalancerConfigApplier.updateLoadBalancers(
        generateDeterministicSeed(cfg::LoadBalancerID::ECMP),
        generateDeterministicSeed(cfg::LoadBalancerID::AGGREGATE_PORT));
    if (newLoadBalancers) {
      new_->resetLoadBalancers(
          toMultiSwitchMap<MultiSwitchLoadBalancerMap>(
              newLoadBalancers, scopeResolver_));
      changed = true;
    }
  }

  {
    auto newTunnels = updateIpInIpTunnels();
    if (newTunnels) {
      new_->resetTunnels(
          toMultiSwitchMap<MultiSwitchIpTunnelMap>(newTunnels, scopeResolver_));
      changed = true;
    }
  }

  {
    auto newSrv6Tunnels = updateSrv6Tunnels();
    if (newSrv6Tunnels) {
      new_->resetSrv6Tunnels(
          toMultiSwitchMap<MultiSwitchSrv6TunnelMap>(
              newSrv6Tunnels, *cfg_, scopeResolver_));
      changed = true;
    }
  }

  {
    auto newVxlanTunnels = updateVxlanTunnels();
    if (newVxlanTunnels) {
      new_->resetVxlanTunnels(
          toMultiSwitchMap<MultiSwitchVxlanTunnelMap>(
              newVxlanTunnels, *cfg_, scopeResolver_));
      changed = true;
    }
  }

  {
    auto newVrfs = updateVrfs();
    if (newVrfs) {
      new_->resetVrfs(
          toMultiSwitchMap<MultiSwitchVrfMap>(newVrfs, scopeResolver_));
      changed = true;
    }
  }

  {
    auto voqSwitchId = getAnySwitchId(cfg::SwitchType::VOQ);
    std::shared_ptr<SwitchSettings> origSwitchSettings{};
    if (voqSwitchId.has_value()) {
      auto matcher =
          HwSwitchMatcher(std::unordered_set<SwitchID>({*voqSwitchId}));
      origSwitchSettings =
          orig_->getSwitchSettings()->getNodeIf(matcher.matcherString());
    }

    auto newDsfNodes = updateDsfNodes();
    if (newDsfNodes) {
      new_->resetDsfNodes(
          toMultiSwitchMap<MultiSwitchDsfNodeMap>(newDsfNodes, scopeResolver_));
      processUpdatedDsfNodes();
    }
    // defaultVoqConfig is used to program VoQ (which is tied to system
    // port) and hence needs updateSystemPorts() to be invoked if this
    // changes in addition to DsfNodes itself.
    if (newDsfNodes ||
        getDefaultVoqConfigIfChanged(origSwitchSettings).has_value()) {
      new_->resetSystemPorts(
          toMultiSwitchMap<MultiSwitchSystemPortMap>(
              updateSystemPorts(new_->getPorts(), new_->getSwitchSettings()),
              scopeResolver_));
      changed = true;
    }
  }

  {
    // Update reachability group setting for input balanced
    auto localFabricSwitchIds = getLocalFabricSwitchIds();
    if (FLAGS_enable_balanced_input_mode && !localFabricSwitchIds.empty()) {
      processReachabilityGroup(localFabricSwitchIds);
    }
  }

  if (!changed) {
    return nullptr;
  }
  return new_;
}

std::optional<SwitchID> ThriftConfigApplier::getAnySwitchId(
    cfg::SwitchType switchType) {
  std::optional<SwitchID> switchId;
  for (const auto& switchIdAndSwitchInfo :
       *cfg_->switchSettings()->switchIdToSwitchInfo()) {
    if (switchIdAndSwitchInfo.second.switchType() == switchType) {
      switchId = static_cast<SwitchID>(switchIdAndSwitchInfo.first);
      break;
    }
  }
  return switchId;
}

std::vector<SwitchID> ThriftConfigApplier::getLocalFabricSwitchIds() const {
  std::vector<SwitchID> localFabricSwitchIds;
  for (const auto& switchIdAndSwitchInfo :
       *cfg_->switchSettings()->switchIdToSwitchInfo()) {
    if (switchIdAndSwitchInfo.second.switchType() == cfg::SwitchType::FABRIC) {
      localFabricSwitchIds.push_back(
          static_cast<SwitchID>(switchIdAndSwitchInfo.first));
    }
  }
  return localFabricSwitchIds;
}

// Return the new defaultVoqConfig if it is different from the
// original config.
std::optional<QueueConfig> ThriftConfigApplier::getDefaultVoqConfigIfChanged(
    std::shared_ptr<SwitchSettings> origSwitchSettings) {
  if (cfg_->defaultVoqConfig()->size()) {
    // TODO(daiweix): do not hardcode 8
    const auto kNumVoqs = 8;
    auto origPortQueues = QueueConfig();
    if (origSwitchSettings &&
        !origSwitchSettings->getDefaultVoqConfig().empty()) {
      origPortQueues = origSwitchSettings->getDefaultVoqConfig();
    }
    std::optional<QueueConfig> defaultVoqConfig = updatePortQueues(
        origPortQueues,
        *cfg_->defaultVoqConfig(),
        0 /*baseQueueId*/,
        kNumVoqs,
        cfg::StreamType::UNICAST,
        std::nullopt,
        std::nullopt,
        false);
    if (!origSwitchSettings ||
        (origSwitchSettings->getDefaultVoqConfig() != *defaultVoqConfig)) {
      return defaultVoqConfig;
    }
  }
  return std::nullopt;
}

QueueConfig ThriftConfigApplier::getVoqConfig(PortID portId) {
  for (const auto& portCfg : *cfg_->ports()) {
    if (PortID(*portCfg.logicalID()) == portId) {
      if (auto portVoqConfigName = portCfg.portVoqConfigName()) {
        auto it = cfg_->portQueueConfigs()->find(*portVoqConfigName);
        if (it == cfg_->portQueueConfigs()->end()) {
          throw FbossError(
              "Port voq config name: ",
              *portVoqConfigName,
              " does not exist in PortQueueConfig map");
        }
        std::vector<cfg::PortQueue> cfgPortVoqs = it->second;
        QueueConfig voqs;
        // TODO(daiweix): do not hardcode 8
        const auto kNumVoqs = 8;
        return updatePortQueues(
            voqs,
            cfgPortVoqs,
            0 /*baseQueueId*/,
            kNumVoqs,
            cfg::StreamType::UNICAST,
            std::nullopt,
            std::nullopt,
            false);
      } else {
        break;
      }
    }
  }
  // use default voq config if not specified
  return utility::getFirstNodeIf(new_->getSwitchSettings())
      ->getDefaultVoqConfig();
}

void ThriftConfigApplier::processUpdatedDsfNodes() {
  bool hasVoqSwitch = false;
  std::unordered_set<SwitchID> localSwitchIds;
  std::unordered_set<int> localInbandPortIds;

  IntfRouteTable remoteIntfRoutesToAdd;
  RouterIDToPrefixes remoteIntfRoutesToDel;

  for (auto& [matcherString, switchSettings] :
       std::as_const(*new_->getSwitchSettings())) {
    auto localSwitchId = HwSwitchMatcher(matcherString).switchId();
    localSwitchIds.insert(localSwitchId);
    auto switchInfo =
        switchSettings->getSwitchIdToSwitchInfo().find(localSwitchId)->second;
    if (switchInfo.inbandPortId().has_value()) {
      localInbandPortIds.insert(switchInfo.inbandPortId().value());
    }

    auto origDsfNode = orig_->getDsfNodes()->getNodeIf(localSwitchId);
    if (origDsfNode &&
        origDsfNode->getType() !=
            new_->getDsfNodes()->getNodeIf(localSwitchId)->getType()) {
      throw FbossError("Change in DSF node type is not supported");
    }

    if (switchSettings->l3SwitchType()) {
      hasVoqSwitch |=
          (switchSettings->l3SwitchType().value() == cfg::SwitchType::VOQ);
    }
  }

  PortID localInbandPortId(1);
  if (!localInbandPortIds.empty()) {
    localInbandPortId = PortID(*localInbandPortIds.begin());
  }
  // On VOQ switches, DSF nodes in the SwitchState are consumed by the DSF
  // subscriber to subscribe to every other VOQ switch. Thus, process DSF
  // nodes if at least one of the switchIDs is for VOQ switch.
  if (!hasVoqSwitch) {
    return;
  }

  auto delta = StateDelta(orig_, new_).getDsfNodesDelta();
  auto switchIdToSwitchIndex =
      computeSwitchIdToSwitchIndex(new_->getDsfNodes());

  auto getInbandSysPortId = [this](const std::shared_ptr<DsfNode>& node) {
    CHECK(node->getInbandPortId().has_value());
    auto switchId = node->getSwitchId();
    const auto& switchIdToSwitchInfo =
        *cfg_->switchSettings()->switchIdToSwitchInfo();
    auto largestSwitchId = switchIdToSwitchInfo.rbegin()->first;
    auto largestSwitchIndex =
        *switchIdToSwitchInfo.rbegin()->second.switchIndex();
    int64_t switchIndex = 0;
    if (largestSwitchIndex != 0) {
      auto switchIdStride = largestSwitchId / largestSwitchIndex;
      if (switchIdStride == 0) {
        throw FbossError(
            "Invalid switchId stride derived from largest switchId: ",
            largestSwitchId,
            " and switchIndex: ",
            largestSwitchIndex);
      }
      switchIndex = (switchId / switchIdStride) %
          static_cast<int64_t>(switchIdToSwitchInfo.size());
    }
    auto switchInfoItr = std::find_if(
        switchIdToSwitchInfo.begin(),
        switchIdToSwitchInfo.end(),
        [switchIndex](const auto& switchIdAndInfo) {
          return switchIdAndInfo.second.switchIndex() == switchIndex;
        });
    if (switchInfoItr == switchIdToSwitchInfo.end()) {
      throw FbossError(
          "Missing switch info for DSF node switchId: ",
          switchId,
          " and switchIndex: ",
          switchIndex);
    }
    const auto inbandPortId = *node->getInbandPortId();
    const auto portRangeMin = *switchInfoItr->second.portIdRange()->minimum();
    const auto portRangeMax = *switchInfoItr->second.portIdRange()->maximum();
    auto globalSystemPortOffset = node->getGlobalSystemPortOffset();
    if (inbandPortId < portRangeMin || inbandPortId > portRangeMax) {
      throw FbossError(
          "Inband port ID: ",
          inbandPortId,
          " is outside port range [",
          portRangeMin,
          ", ",
          portRangeMax,
          "] for DSF node switchId: ",
          switchId,
          " and switchIndex: ",
          switchIndex);
    }
    return SystemPortID(inbandPortId + *globalSystemPortOffset - portRangeMin);
  };
  auto getRecyclePortName =
      [&switchIdToSwitchIndex](const std::shared_ptr<DsfNode>& node) {
        int asicCore;
        switch (node->getAsicType()) {
          case cfg::AsicType::ASIC_TYPE_MOCK:
          case cfg::AsicType::ASIC_TYPE_FAKE:
          case cfg::AsicType::ASIC_TYPE_FAKE_NO_WARMBOOT:
          case cfg::AsicType::ASIC_TYPE_JERICHO2:
            asicCore = 1;
            break;
          case cfg::AsicType::ASIC_TYPE_JERICHO3:
          case cfg::AsicType::ASIC_TYPE_JERICHO4:
          case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
            if (isDualStage3Q2QMode()) {
              asicCore = 447;
            } else {
              asicCore = 441;
            }
            break;
          case cfg::AsicType::ASIC_TYPE_CHENAB:
          case cfg::AsicType::ASIC_TYPE_CHENAB2:
          case cfg::AsicType::ASIC_TYPE_TRIDENT2:
          case cfg::AsicType::ASIC_TYPE_TOMAHAWK:
          case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
          case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
          case cfg::AsicType::ASIC_TYPE_ELBERT_8DD:
          case cfg::AsicType::ASIC_TYPE_AGERA3:
          case cfg::AsicType::ASIC_TYPE_EBRO:
          case cfg::AsicType::ASIC_TYPE_P200:
          case cfg::AsicType::ASIC_TYPE_GARONNE:
          case cfg::AsicType::ASIC_TYPE_SANDIA_PHY:
          case cfg::AsicType::ASIC_TYPE_RAMON:
          case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
          case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
          case cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1:
          case cfg::AsicType::ASIC_TYPE_YUBA:
          case cfg::AsicType::ASIC_TYPE_G202X:
          case cfg::AsicType::ASIC_TYPE_RAMON3:
            throw FbossError(
                "Recycle ports are not applicable for AsicType: ",
                apache::thrift::util::enumNameSafe(node->getAsicType()));
        }

        auto iter = switchIdToSwitchIndex.find(node->getSwitchId());
        // switchIdToSwitchIndex is computed from DsfNode map. Thus, we should
        // always find the switchId
        CHECK(iter != switchIdToSwitchIndex.end());
        auto switchIndex = iter->second;

        // Recycle port name format:
        //    rcy<pim_id>/<npu_id>/<npu_core>
        // pmi_id is always 1 for Recycle port.
        // npu_id = switchIndex + 1 (switchIndex strarts at 0)
        return fmt::format(
            "{}:rcy1/{}/{}", node->getName(), switchIndex + 1, asicCore);
      };
  auto isLocal = [localSwitchIds](const std::shared_ptr<DsfNode>& node) {
    return localSwitchIds.find(node->getSwitchId()) != localSwitchIds.end();
  };
  auto isInterfaceNode = [](const std::shared_ptr<DsfNode>& node) {
    return node->getType() == cfg::DsfNodeType::INTERFACE_NODE;
  };
  auto getDsfNodeAsic = [&isInterfaceNode](
                            const std::shared_ptr<DsfNode>& node) {
    CHECK(isInterfaceNode(node))
        << " Only expect to be called for Interface nodes";
    CHECK(node->getLocalSystemPortOffset().has_value());
    CHECK(node->getGlobalSystemPortOffset().has_value());
    CHECK(node->getInbandPortId().has_value());
    CHECK(node->getMac().has_value());
    cfg::SwitchInfo switchInfo;
    switchInfo.asicType() = node->getAsicType();
    switchInfo.switchType() = cfg::SwitchType::VOQ;
    switchInfo.switchIndex() = 0; /* dummy switchIndex*/
    switchInfo.switchMac() = node->getMac()->toString();
    switchInfo.systemPortRanges() = node->getSystemPortRanges();
    switchInfo.localSystemPortOffset() = *node->getLocalSystemPortOffset();
    switchInfo.globalSystemPortOffset() = *node->getGlobalSystemPortOffset();
    switchInfo.inbandPortId() = *node->getInbandPortId();
    return HwAsic::makeAsic(
        static_cast<int64_t>(node->getSwitchId()),
        switchInfo,
        std::nullopt,
        std::nullopt /* fabricNodeRole is N/A for VOQ switches */);
  };
  auto processLoopbacks = [&](const std::shared_ptr<DsfNode>& node,
                              const HwAsic* dsfNodeAsic) {
    auto recyclePortId = getInbandSysPortId(node);
    InterfaceID intfID(recyclePortId);
    auto intfs = isLocal(node) ? new_->getInterfaces()->modify(&new_)
                               : new_->getRemoteInterfaces()->modify(&new_);
    auto intf = intfs->getNode(intfID)->clone();
    Interface::Addresses addresses;
    // THRIFT_COPY: evaluate if getting entire thrift table is needed.
    auto arpTable = intf->getArpTable()->toThrift();
    auto ndpTable = intf->getNdpTable()->toThrift();
    std::optional<int64_t> encapIdx;
    if (dsfNodeAsic->isSupported(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE)) {
      encapIdx = *dsfNodeAsic->getReservedEncapIndexRange().minimum();
    }
    for (const auto& network : node->getLoopbackIpsSorted()) {
      addresses.insert(network);
      state::NeighborEntryFields neighbor;
      neighbor.ipaddress() = network.first.str();
      CHECK(node->getMac().has_value());
      neighbor.mac() = node->getMac()->toString();
      neighbor.portId()->portType() = cfg::PortDescriptorType::SystemPort;
      neighbor.portId()->portId() = recyclePortId;
      neighbor.interfaceId() = recyclePortId;
      neighbor.state() = state::NeighborState::Reachable;
      if (encapIdx) {
        neighbor.encapIndex() = *encapIdx;
        *encapIdx = *encapIdx + 1;
      }
      neighbor.isLocal() = isLocal(node);
      neighbor.type() = state::NeighborEntryType::STATIC_ENTRY;
      neighbor.resolvedSince() = static_cast<int64_t>(std::time(nullptr));
      // For local loopback IPs we set noHostRoute to true
      // since we dont want to install the host/neighbor entry
      // in HW (the same /128, /32 is covered by ip2me routes).
      // However we still want to program the encap info (MAC,
      // encap ID) in HW, so we continue to send the entry to
      // send it to SDK, but with noHostRoute flag set.
      neighbor.noHostRoute() = isLocal(node);
      if (network.first.isV6()) {
        ndpTable.insert({*neighbor.ipaddress(), neighbor});
      } else {
        arpTable.insert({*neighbor.ipaddress(), neighbor});
      }
    }
    intf->setAddresses(std::move(addresses));
    intf->setArpTable(std::move(arpTable));
    intf->setNdpTable(std::move(ndpTable));
    intfs->updateNode(intf, scopeResolver_.scope(intf, new_));
  };
  auto addDsfNode = [&](const std::shared_ptr<DsfNode>& node) {
    if (!isInterfaceNode(node)) {
      // Only create recycle ports for INs
      return;
    }
    auto dsfNodeAsic = getDsfNodeAsic(node);
    if (isLocal(node)) {
      processLoopbacks(node, dsfNodeAsic.get());
      // For local asic recycle port and sys port information will come
      // via local config. So only process need to process loopback IPs here.
      return;
    }
    auto recyclePortId = getInbandSysPortId(node);
    auto sysPort = std::make_shared<SystemPort>(
        SystemPortID(recyclePortId),
        std::make_optional(RemoteSystemPortType::STATIC_ENTRY));
    sysPort->setSwitchId(node->getSwitchId());
    sysPort->setName(getRecyclePortName(node));
    const auto intfRole =
        isDualStage3Q2QMode() && node->getClusterId() >= k2StageEdgePodClusterId
        ? HwAsic::InterfaceNodeRole::DUAL_STAGE_EDGE_NODE
        : HwAsic::InterfaceNodeRole::IN_CLUSTER_NODE;
    const auto& recyclePortInfo = dsfNodeAsic->getRecyclePortInfo(intfRole);
    CHECK_EQ(recyclePortInfo.inbandPortId, *node->getInbandPortId());
    sysPort->setCoreIndex(recyclePortInfo.coreId);
    sysPort->setCorePortIndex(recyclePortInfo.corePortIndex);
    sysPort->setSpeedMbps(recyclePortInfo.speedMbps);
    sysPort->setNumVoqs(
        getRemotePortNumVoqs(intfRole, cfg::PortType::RECYCLE_PORT));

    sysPort->setScope(cfg::Scope::GLOBAL);
    sysPort->setShelDestinationEnabled(
        cfg_->switchSettings()->selfHealingEcmpLagConfig().has_value());
    sysPort->resetPortQueues(getVoqConfig(localInbandPortId));
    sysPort->setPortType(cfg::PortType::RECYCLE_PORT);
    if (auto dataPlaneTrafficPolicy = cfg_->dataPlaneTrafficPolicy()) {
      if (auto portIdToQosPolicy =
              dataPlaneTrafficPolicy->portIdToQosPolicy()) {
        auto qosPolicyItr = portIdToQosPolicy->find(localInbandPortId);
        if (qosPolicyItr != portIdToQosPolicy->end()) {
          sysPort->setQosPolicy(qosPolicyItr->second);
        }
      }
    }
    auto sysPorts = new_->getRemoteSystemPorts()->modify(&new_);
    sysPorts->addNode(sysPort, scopeResolver_.scope(sysPort));
    CHECK(node->getMac().has_value());
    auto intf = std::make_shared<Interface>(
        InterfaceID(recyclePortId),
        RouterID(0),
        std::optional<VlanID>(std::nullopt),
        folly::StringPiece(sysPort->getName()),
        *node->getMac(),
        9000,
        true,
        true,
        cfg::InterfaceType::SYSTEM_PORT,
        isLocal(node) ? std::optional<RemoteInterfaceType>(std::nullopt)
                      : std::make_optional(RemoteInterfaceType::STATIC_ENTRY),
        std::optional<LivenessStatus>(std::nullopt),
        cfg::Scope::GLOBAL);
    auto intfs = new_->getRemoteInterfaces()->modify(&new_);
    intfs->addNode(intf, scopeResolver_.scope(intf, new_));
    processLoopbacks(node, dsfNodeAsic.get());

    processRemoteInterfaceRoutes(
        new_->getRemoteInterfaces()->getNode(
            InterfaceID(getInbandSysPortId(node))),
        new_,
        true /* add */,
        remoteIntfRoutesToAdd,
        remoteIntfRoutesToDel);
  };
  auto rmDsfNode = [&](const std::shared_ptr<DsfNode>& node) {
    if (!isInterfaceNode(node)) {
      return;
    }
    if (!isLocal(node)) {
      auto recyclePortId = getInbandSysPortId(node);
      processRemoteInterfaceRoutes(
          new_->getRemoteInterfaces()->getNode(InterfaceID(recyclePortId)),
          new_,
          false /* add */,
          remoteIntfRoutesToAdd,
          remoteIntfRoutesToDel);

      auto sysPorts = new_->getRemoteSystemPorts()->modify(&new_);
      sysPorts->removeNode(SystemPortID(recyclePortId));
      auto intfs = new_->getRemoteInterfaces()->modify(&new_);
      intfs->removeNode(InterfaceID(recyclePortId));
    } else {
      // Local DSF node removal should be accompanied by
      // recycle port and intf removal in config. That will
      // cleanup any neighbor entries on intf removarl
    }
  };

  DeltaFunctions::forEachChanged(
      delta,
      [&](auto oldNode, auto newNode) {
        rmDsfNode(oldNode);
        addDsfNode(newNode);
      },
      [&](auto newNode) { addDsfNode(newNode); },
      [&](auto oldNode) { rmDsfNode(oldNode); });

  if (routeUpdater_) {
    routeUpdater_->setRemoteLoopbackInterfaceRoutesToConfig(
        remoteIntfRoutesToAdd, remoteIntfRoutesToDel);
  } else if (rib_) {
    rib_->updateRemoteInterfaceRoutes(
        &scopeResolver_,
        remoteIntfRoutesToAdd,
        remoteIntfRoutesToDel,
        &updateFibFromConfig,
        static_cast<void*>(&new_));
  }
}

void ThriftConfigApplier::processReachabilityGroup(
    const std::vector<SwitchID>& localFabricSwitchIds) {
  std::unordered_map<std::string, std::vector<uint32_t>> switchNameToSwitchIds;

  bool isSingleStageCluster = true;
  for (const auto& [_, dsfNode] : *cfg_->dsfNodes()) {
    std::string nodeName = *dsfNode.name();
    auto iter = switchNameToSwitchIds.find(nodeName);
    if (iter != switchNameToSwitchIds.end()) {
      iter->second.push_back(*dsfNode.switchId());
    } else {
      switchNameToSwitchIds[nodeName] = {
          static_cast<uint32_t>(*dsfNode.switchId())};
    }
    if (auto fabricLevel = dsfNode.fabricLevel()) {
      isSingleStageCluster &= (fabricLevel.value() < 2);
    }
  }

  auto updateReachabilityGroups = [&](const auto fabricSwitchId,
                                      const auto&
                                          destinationId2ReachabilityGroup) {
    std::vector<int> reachabilityGroups;
    for (const auto& [_, groupId] : destinationId2ReachabilityGroup) {
      reachabilityGroups.push_back(groupId);
    }

    auto matcher = HwSwitchMatcher(
        std::unordered_set<SwitchID>({static_cast<SwitchID>(fabricSwitchId)}));
    auto currReachabilityGroups = new_->getSwitchSettings()
                                      ->getNodeIf(matcher.matcherString())
                                      ->getReachabilityGroups();
    if (currReachabilityGroups != reachabilityGroups) {
      auto newMultiSwitchSettings = new_->getSwitchSettings()->clone();
      auto newSwitchSettings =
          newMultiSwitchSettings->getNodeIf(matcher.matcherString())->clone();
      newSwitchSettings->setReachabilityGroups(reachabilityGroups);
      newMultiSwitchSettings->updateNode(
          matcher.matcherString(), newSwitchSettings);
      new_->resetSwitchSettings(newMultiSwitchSettings);
    }
  };

  bool parallelVoqLinks = checkParallelLinksToInterfaceNodes(
      cfg_,
      localFabricSwitchIds,
      switchNameToSwitchIds,
      scopeResolver_,
      platformMapping_);

  if (!isSingleStageCluster || parallelVoqLinks) {
    auto newPortMap = new_->getPorts()->modify(&new_);
    for (const auto& fabricSwitchId : localFabricSwitchIds) {
      std::unordered_map<int, int> destinationId2ReachabilityGroup;

      // For reachability groups with parallel links, use index 1-128.
      // Else use index for single link groups in range 129-256
      int nextParallelLinkGroup = 1;
      int nextSingleLinkGroup = 129;

      for (const auto& portCfg : *cfg_->ports()) {
        if (scopeResolver_.scope(portCfg).has(SwitchID(fabricSwitchId)) &&
            portCfg.expectedNeighborReachability()->size() > 0) {
          auto neighborRemoteSwitchId =
              getRemoteSwitchID(cfg_, portCfg, switchNameToSwitchIds);

          if (std::find(
                  localFabricSwitchIds.begin(),
                  localFabricSwitchIds.end(),
                  SwitchID(neighborRemoteSwitchId)) !=
              localFabricSwitchIds.end()) {
            // Skip processing links that are expected to connected to self -
            // this should not happen in PROD since expected neighbors should be
            // other devices. However during testing, ports are put in loopback
            // mode and hence the expected neighbor will be populated to self.
            // Skip processing these links for reachability group.
            continue;
          }
          const auto& neighborDsfNode =
              new_->getDsfNodes()->getNode(neighborRemoteSwitchId);

          int destinationId;
          if (neighborDsfNode->getType() == cfg::DsfNodeType::INTERFACE_NODE) {
            destinationId = neighborRemoteSwitchId;
          } else {
            // Fabric node links: assign links based on cluster ID. Note that in
            // dual stage, FE2 has no cluster ID: use -1 for grouping purpose.
            destinationId = neighborDsfNode->getClusterId().value_or(-1);
          }
          bool singleLinkGroup =
              neighborDsfNode->getType() == cfg::DsfNodeType::INTERFACE_NODE &&
              !parallelVoqLinks;
          int& nextGroupId =
              singleLinkGroup ? nextSingleLinkGroup : nextParallelLinkGroup;

          auto [it, inserted] = destinationId2ReachabilityGroup.insert(
              {destinationId, nextGroupId});
          auto reachabilityGroupId = it->second;
          if (inserted) {
            nextGroupId++;
            XLOG(DBG2) << "Create new reachability group "
                       << reachabilityGroupId << " towards node "
                       << neighborDsfNode->getName() << " with switchId "
                       << neighborRemoteSwitchId;
          } else {
            XLOG(DBG2) << "Add node " << neighborDsfNode->getName()
                       << " with switchId " << neighborRemoteSwitchId
                       << " to existing reachability group "
                       << reachabilityGroupId;
          }

          auto newPort =
              newPortMap->getNode(PortID(*portCfg.logicalID()))->modify(&new_);
          newPort->setReachabilityGroupId(reachabilityGroupId);
        }
      }
      updateReachabilityGroups(fabricSwitchId, destinationId2ReachabilityGroup);
    }
  }
}

void ThriftConfigApplier::validateUdfConfig(const UdfConfig& newUdfConfig) {
  auto udfGroupMap = newUdfConfig.getUdfGroupMap();
  if (udfGroupMap == nullptr) {
    return;
  }

  for (const auto& loadBalancerConfig : cfg_->loadBalancers().value()) {
    auto loadBalancerId = folly::copy(loadBalancerConfig.id().value());
    auto udfGroups = loadBalancerConfig.fieldSelection().value().udfGroups();
    for (auto& udfGroupName : *udfGroups) {
      if (udfGroupMap->find(udfGroupName) == udfGroupMap->end()) {
        throw FbossError(
            "Configuration does not exist for UdfGroup: ",
            udfGroupName,
            " but exists in UdfGroupList for LoadBalancer ",
            loadBalancerId);
      }
    }
  }

  for (const auto& udfGroupEntry : *udfGroupMap) {
    const auto& udfGroupName = udfGroupEntry.first;
    const auto& udfPacketMatchersList =
        udfGroupEntry.second->getUdfPacketMatcherIds();
    for (const auto& matcherMapName : udfPacketMatchersList) {
      const auto& udfPacketMatchers = newUdfConfig.getUdfPacketMatcherMap();
      if (udfPacketMatchers->find(matcherMapName) == udfPacketMatchers->end()) {
        throw FbossError(
            "Configuration does not exist for UdfPacketMatcherMap: ",
            matcherMapName,
            " but exists in packetMatcherList for UdfGroup ",
            udfGroupName);
      }
    }
  }
}

std::shared_ptr<UdfConfig> ThriftConfigApplier::updateUdfConfig(bool* changed) {
  *changed = false;
  auto origUdfConfig = orig_->getUdfConfig() ? orig_->getUdfConfig()
                                             : std::make_shared<UdfConfig>();
  auto newUdfConfig = std::make_shared<UdfConfig>();

  if (!cfg_->udfConfig()) {
    // cfg field is optional whereas state field is not. As a result
    // check if its populated or not to ascertain if there
    // are any changes in object
    if (origUdfConfig->isUdfConfigPopulated()) {
      *changed = true;
    }
    return newUdfConfig;
  }

  // new cfg exists
  newUdfConfig->fromThrift(*cfg_->udfConfig());
  // validate cfg
  validateUdfConfig(*newUdfConfig);
  // ThriftStructNode does deep comparison internally
  if (*origUdfConfig != *newUdfConfig) {
    *changed = true;
    return newUdfConfig;
  }

  return newUdfConfig;
}

std::shared_ptr<DsfNodeMap> ThriftConfigApplier::updateDsfNodes() {
  auto origNodes = orig_->getDsfNodes();
  auto newNodes = std::make_shared<DsfNodeMap>();
  newNodes->fromThrift(*cfg_->dsfNodes());
  bool changed = false;
  std::optional<cfg::QueueScheduling> expectedScheduling;
  for (const auto& idAndNode : *newNodes) {
    auto newNode = idAndNode.second;
    if (newNode->getType() == cfg::DsfNodeType::INTERFACE_NODE) {
      auto scheduling = newNode->getScheduling();
      if (!expectedScheduling.has_value()) {
        expectedScheduling = scheduling;
      } else if (expectedScheduling.value() != scheduling) {
        throw FbossError(
            "Scheduling settings are not consistent between dsf nodes");
      }
      if (scheduling != cfg::QueueScheduling::INTERNAL &&
          !newNode->getSchedulingParam().has_value()) {
        throw FbossError(
            "Scheduling parameter should not be empty if scheduler is not INTERNAL");
      }
    }
    if (newNode->isInterfaceNode()) {
      if (!newNode->getLocalSystemPortOffset().has_value() ||
          !newNode->getGlobalSystemPortOffset().has_value()) {
        throw FbossError(
            "Local/Global system port offsets must be set for interface nodes");
      }
      if (newNode->getSystemPortRanges().systemPortRanges()->empty()) {
        throw FbossError(
            "System port range must be non-empty for interface nodes");
      }
      if (!newNode->getInbandPortId().has_value()) {
        throw FbossError("Inband port ID must be set for interface nodes");
      }
    }
    auto origNode = origNodes->getNodeIf(newNode->getID());
    if (!origNode || *origNode != *newNode) {
      changed |= true;
    } else {
      newNodes->updateNode(origNode);
    }
  }
  // We accounted for all nodes in config, now
  // account of any old nodes that may not be
  // present in config. For this just comparing
  // size is enough, since
  // a. If a node got removed, we would see a delta in size
  // b. If size remained the same and nodes got updated we would
  // see a delta in the loop above
  changed |= (origNodes->numNodes() != newNodes->size());
  if (changed) {
    return newNodes;
  }
  return nullptr;
}
void ThriftConfigApplier::processVlanPorts() {
  // Build the Port --> Vlan mappings
  //
  // The config file has a separate list for this data,
  // but it is stored in the state tree as part of both the PortMap and the
  // VlanMap.
  for (const auto& vp : *cfg_->vlanPorts()) {
    PortID portID(*vp.logicalPort());
    VlanID vlanID(*vp.vlanID());
    bool emitTags = *vp.emitTags();
    bool emitPriorityTags = *vp.emitPriorityTags();

    auto ret1 = portVlans_[portID].insert(
        std::make_pair(vlanID, Port::VlanInfo(emitTags, emitPriorityTags)));
    if (!ret1.second) {
      throw FbossError(
          "duplicate VlanPort for port ", portID, ", vlan ", vlanID);
    }

    state::VlanInfo vlanInfo;
    *vlanInfo.tagged() = emitTags;
    *vlanInfo.priorityTagged() = emitPriorityTags;
    auto ret2 = vlanPorts_[vlanID].insert(std::make_pair(portID, vlanInfo));
    if (!ret2.second) {
      // This should never fail if the first insert succeeded above.
      throw FbossError(
          "duplicate VlanPort for vlan ", vlanID, ", port ", portID);
    }
  }
}

void ThriftConfigApplier::processInterfaceForPortForVoqSwitches(
    int64_t switchId) {
  // TODO - only look at ports corresponding to the passed in switchId
  auto dsfNodeItr = cfg_->dsfNodes()->find(switchId);
  CHECK(dsfNodeItr != cfg_->dsfNodes()->end());
  auto switchInfoItr =
      cfg_->switchSettings()->switchIdToSwitchInfo()->find(switchId);
  CHECK(switchInfoItr != cfg_->switchSettings()->switchIdToSwitchInfo()->end());
  const auto& switchInfo = switchInfoItr->second;
  CHECK(
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          switchInfo.portIdRange()));
  CHECK(!switchInfo.systemPortRanges()->systemPortRanges()->empty());
  CHECK(switchInfo.localSystemPortOffset().has_value());
  CHECK(switchInfo.globalSystemPortOffset().has_value());
  CHECK(switchInfo.inbandPortId().has_value());
  for (const auto& portCfg : *cfg_->ports()) {
    auto portType = *portCfg.portType();
    auto portID = PortID(*portCfg.logicalID());
    // Only process ports belonging to the passed switchId
    if (scopeResolver_.scope(portCfg).has(SwitchID(switchId))) {
      switch (portType) {
        case cfg::PortType::INTERFACE_PORT:
        case cfg::PortType::RECYCLE_PORT:
        case cfg::PortType::EVENTOR_PORT:
        case cfg::PortType::HYPER_PORT:
        case cfg::PortType::MANAGEMENT_PORT: {
          auto interfaceID = getSystemPortID(
              portID,
              *portCfg.scope(),
              *cfg_->switchSettings()->switchIdToSwitchInfo(),
              SwitchID(switchId));
          port2InterfaceId_[portID].push_back(interfaceID);
        } break;
        case cfg::PortType::HYPER_PORT_MEMBER: {
          const std::vector<PortID>& hyperPortIDs =
              platformMapping_->getPlatformPorts(cfg::PortType::HYPER_PORT);
          for (const PortID& hyperPortID : hyperPortIDs) {
            const auto& hyperPortPlatformPortEntry =
                platformMapping_->getPlatformPort(hyperPortID);
            const auto& hyperPortPlatformCfg =
                hyperPortPlatformPortEntry.supportedProfiles()
                    ->find(cfg::PortProfileID::PROFILE_DEFAULT)
                    ->second;
            CHECK(hyperPortPlatformCfg.subsumedPorts().has_value());
            for (const auto& subsumedPortId :
                 *hyperPortPlatformCfg.subsumedPorts()) {
              if (PortID(subsumedPortId) == portID) {
                // use interface ID of the hyper port
                auto interfaceID = getSystemPortID(
                    hyperPortID,
                    hyperPortPlatformPortEntry.mapping()->scope().value(),
                    *cfg_->switchSettings()->switchIdToSwitchInfo(),
                    SwitchID(switchId));
                port2InterfaceId_[portID].push_back(interfaceID);
                break;
              }
            }
          }
        } break;
        case cfg::PortType::FABRIC_PORT:
        case cfg::PortType::CPU_PORT:
          // no interface for fabric/cpu port
          break;
      }
    }
  }
}

void ThriftConfigApplier::processInterfaceForPortForNonVoqSwitches(
    int64_t switchId) {
  flat_map<VlanID, InterfaceID> vlan2InterfaceId;
  // A port carries at most one port router interface. Binding it twice, either
  // directly and again through an aggregate port it is a member of, or through
  // two aggregate ports, is a config bug: a member port of an aggregate port
  // cannot have a standalone router interface of its own.
  auto bindPortToInterface = [this](PortID portID, int32_t intfID) {
    auto [itr, inserted] =
        port2InterfaceId_.emplace(portID, std::vector<int32_t>{intfID});
    if (!inserted) {
      throw FbossError(
          "Port ",
          portID,
          " is bound to more than one router interface: ",
          itr->second.front(),
          " and ",
          intfID);
    }
  };
  for (const auto& interfaceCfg : *cfg_->interfaces()) {
    switch (*interfaceCfg.type()) {
      case cfg::InterfaceType::VLAN: {
        vlan2InterfaceId[VlanID(*interfaceCfg.vlanID())] =
            InterfaceID(*interfaceCfg.intfID());
      } break;
      case cfg::InterfaceType::PORT: {
        // A port router interface is bound to either a physical port or an
        // aggregate port. Bind every port it covers, which for an aggregate is
        // all of its member ports, so that both the member ports and the
        // aggregate resolve to this interface downstream.
        auto intfID = *interfaceCfg.intfID();
        auto aggregatePortID = interfaceCfg.aggregatePortID();
        if (interfaceCfg.portID().has_value() == aggregatePortID.has_value()) {
          throw FbossError(
              "Port router interface ",
              intfID,
              " must set exactly one of portID and aggregatePortID");
        }
        if (aggregatePortID) {
          auto aitr = std::find_if(
              cfg_->aggregatePorts()->cbegin(),
              cfg_->aggregatePorts()->cend(),
              [aggregatePortID](const auto& aggPort) {
                return *aggPort.key() == *aggregatePortID;
              });
          if (aitr == cfg_->aggregatePorts()->cend()) {
            throw FbossError(
                "No aggregate port ",
                *aggregatePortID,
                " for interface ",
                intfID);
          }
          if (aitr->memberPorts()->empty()) {
            // Such an interface would bind no ports at all, leaving both the
            // interface and the aggregate port without a usable binding.
            throw FbossError(
                "Aggregate port ",
                *aggregatePortID,
                " for interface ",
                intfID,
                " has no member ports");
          }
          for (const auto& member : *aitr->memberPorts()) {
            bindPortToInterface(PortID(*member.memberPortID()), intfID);
          }
        } else if (auto portID = interfaceCfg.portID()) {
          bindPortToInterface(PortID(*portID), intfID);
        }
      } break;
      case cfg::InterfaceType::SYSTEM_PORT:
        throw FbossError("Unsupport interface type for NPU switch");
    }
  }

  for (const auto& portCfg : *cfg_->ports()) {
    auto portID = PortID(*portCfg.logicalID());
    if (!scopeResolver_.scope(portCfg).has(SwitchID(switchId))) {
      continue;
    }
    if (port2InterfaceId_.find(portID) != port2InterfaceId_.end()) {
      // port is associated with port router interface, vlan is immaterial
      continue;
    }

    for (const auto& [vlanID, vlanInfo] : portVlans_[portID]) {
      auto it = vlan2InterfaceId.find(vlanID);
      // Skip if vlan has no interface && port is not enabled
      if (it == vlan2InterfaceId.end()) {
        if (*portCfg.state() != cfg::PortState::ENABLED) {
          continue;
        }
        throw FbossError(
            "VLAN ",
            vlanID,
            " has no interface, even when corresp port ",
            portID,
            " is enabled");
      }
      port2InterfaceId_[portID].push_back(it->second);
    }
  }
}

void ThriftConfigApplier::processInterfaceForPort() {
  const auto& switchSettings =
      utility::getFirstNodeIf(new_->getSwitchSettings());
  // Build Port -> interface mappings in port2InterfaceId_
  for (const auto& switchIdAndInfo :
       switchSettings->getSwitchIdToSwitchInfo()) {
    auto switchId = switchIdAndInfo.first;
    auto switchType = *switchIdAndInfo.second.switchType();
    switch (switchType) {
      case cfg::SwitchType::VOQ:
        processInterfaceForPortForVoqSwitches(switchId);
        break;
      case cfg::SwitchType::NPU:
        processInterfaceForPortForNonVoqSwitches(switchId);
        break;
      case cfg::SwitchType::FABRIC:
      case cfg::SwitchType::PHY:
        // No interface on FABRIC or PHY switch types
        break;
    }
  }
}

void ThriftConfigApplier::updateVlanInterfaces(const Interface* intf) {
  if (intf->getType() != cfg::InterfaceType::VLAN) {
    return;
  }
  auto& entry = vlanInterfaces_[intf->getVlanID()];

  // Each VLAN can only be used with a single virtual router
  if (entry.interfaces.empty()) {
    entry.routerID = intf->getRouterID();
  } else {
    if (intf->getRouterID() != entry.routerID) {
      throw FbossError(
          "VLAN ",
          intf->getVlanID(),
          " configured in multiple "
          "different virtual routers: ",
          entry.routerID,
          " and ",
          intf->getRouterID());
    }
  }

  auto intfRet = entry.interfaces.insert(intf->getID());
  if (!intfRet.second) {
    // This shouldn't happen
    throw FbossError(
        "interface ",
        intf->getID(),
        " processed twice for "
        "VLAN ",
        intf->getVlanID());
  }

  for (auto iter : std::as_const(*intf->getAddresses())) {
    auto ipMask =
        std::make_pair(folly::IPAddress(iter.first), iter.second->cref());
    InterfaceIpInfo info(ipMask.second, intf->getMac(), intf->getID());
    auto ret = entry.addresses.emplace(ipMask.first, info);
    if (ret.second) {
      continue;
    }
    // Allow multiple interfaces on the same VLAN with the same IP,
    // as long as they also share the same mask and MAC address.
    const auto& oldInfo = ret.first->second;
    if (oldInfo.mask != info.mask) {
      throw FbossError(
          "VLAN ",
          intf->getVlanID(),
          " has IP ",
          ipMask.first,
          " configured multiple times with different masks (",
          oldInfo.mask,
          " and ",
          info.mask,
          ")");
    }
    if (oldInfo.mac != info.mac) {
      throw FbossError(
          "VLAN ",
          intf->getVlanID(),
          " has IP ",
          ipMask.first,
          " configured multiple times with different MACs (",
          oldInfo.mac,
          " and ",
          info.mac,
          ")");
    }
  }

  // Also add the link-local IPv6 address
  IPAddressV6 linkLocalAddr(IPAddressV6::LINK_LOCAL, intf->getMac());
  InterfaceIpInfo linkLocalInfo(64, intf->getMac(), intf->getID());
  entry.addresses.emplace(IPAddress(linkLocalAddr), linkLocalInfo);
}

shared_ptr<SystemPortMap>
ThriftConfigApplier::updateFabricLinkMonitoringSystemPorts(
    const std::shared_ptr<MultiSwitchPortMap>& ports,
    const std::shared_ptr<MultiSwitchSettings>& multiSwitchSettings) {
  auto sysPorts = std::make_shared<SystemPortMap>();

  for (const auto& [matcherString, portMap] : std::as_const(*ports)) {
    auto switchId = HwSwitchMatcher(matcherString).switchId();
    auto switchSettings = multiSwitchSettings->getNodeIf(matcherString);
    // System port creation for fabric link monitoring is applicable
    // to fabric ports of VOQ switches only!
    if (!switchSettings || !switchSettings->l3SwitchType().has_value() ||
        switchSettings->l3SwitchType().value() != cfg::SwitchType::VOQ) {
      continue;
    }
    auto dsfNode = cfg_->dsfNodes()->find(switchId)->second;

    for (const auto& port : std::as_const(*portMap)) {
      auto fabricLinkSwitchId = getFabricLinkMonitoringPortSwitchId(
          port.second->getID(),
          port.second->getPortType(),
          port.second->getExpectedNeighborValues()->size());
      if (!fabricLinkSwitchId.has_value()) {
        // Not a valid port for fabric link monitoring
        continue;
      }
      // When fabric port logical IDs are relocated into the local port-ID
      // range, their system ports use the same allocation as every other local
      // port; otherwise fall back to the legacy offset-based scheme.
      // TODO(fabric_ports_uniform_local_offset): drop the legacy branch once
      // all users migrate. The only remaining user is
      // AgentMultiNodeFabricLinkMonitoringTest, which scrapes remote DSF agents
      // whose platform mapping + flag are owned by Configerator/netcastle, so
      // migrating it is a cross-repo change, not a pure fbcode flip.
      auto sysPortId = FLAGS_fabric_ports_uniform_local_offset
          ? getSystemPortID(
                port.second->getID(),
                port.second->getScope(),
                switchSettings->getSwitchIdToSwitchInfo(),
                switchId)
          : getFabricLinkMonitoringSystemPortID(
                port.second->getID(), switchSettings);
      auto sysPort = std::make_shared<SystemPort>(sysPortId);
      sysPort->setSwitchId(SwitchID(*fabricLinkSwitchId));
      // Last 2 bits in the SwitchID determines the core ID
      int64_t coreIdx = *fabricLinkSwitchId & 0x3;
      sysPort->setCoreIndex(coreIdx);
      // Populate the CPU port for the core identified above
      for (const auto& [_, coreAndPortIdx] :
           platformMapping_->getCpuPortsCoreAndPortIdx()) {
        if (coreAndPortIdx.first == coreIdx) {
          sysPort->setCorePortIndex(coreAndPortIdx.second);
          break;
        }
      }
      sysPort->setName(
          fmt::format("{}:{}", *dsfNode.name(), port.second->getName()));
      sysPort->setNumVoqs(getLocalPortNumVoqs(
          port.second->getPortType(), port.second->getScope()));
      sysPort->setSpeedMbps(static_cast<int>(port.second->getSpeed()));
      sysPort->setScope(port.second->getScope());
      sysPort->setPortType(port.second->getPortType());
      // There is no physical port mapping to this system port and hence we need
      // to operate in PUSH mode and not wait for credits.
      sysPort->setPushQueueEnabled(true);
      sysPorts->addSystemPort(sysPort);
    }
  }
  return sysPorts;
}

shared_ptr<SystemPortMap> ThriftConfigApplier::updateSystemPorts(
    const std::shared_ptr<MultiSwitchPortMap>& ports,
    const std::shared_ptr<MultiSwitchSettings>& multiSwitchSettings) {
  static const std::set<cfg::PortType> kCreateSysPortsFor = {
      cfg::PortType::INTERFACE_PORT,
      cfg::PortType::RECYCLE_PORT,
      cfg::PortType::MANAGEMENT_PORT,
      cfg::PortType::EVENTOR_PORT,
      cfg::PortType::HYPER_PORT,
      cfg::PortType::HYPER_PORT_MEMBER};
  auto sysPorts = std::make_shared<SystemPortMap>();

  for (const auto& [matcherString, portMap] : std::as_const(*ports)) {
    auto switchId = HwSwitchMatcher(matcherString).switchId();
    auto switchSettings = multiSwitchSettings->getNodeIf(matcherString);
    if (switchSettings->l3SwitchType() != cfg::SwitchType::VOQ) {
      continue;
    }

    auto dsfNode = cfg_->dsfNodes()->find(switchId)->second;
    auto nodeName = *dsfNode.name();

    for (const auto& port : std::as_const(*portMap)) {
      if (kCreateSysPortsFor.find(port.second->getPortType()) ==
          kCreateSysPortsFor.end()) {
        continue;
      }
      auto sysPort = std::make_shared<SystemPort>(getSystemPortID(
          port.second->getID(),
          port.second->getScope(),
          switchSettings->getSwitchIdToSwitchInfo(),
          switchId));
      sysPort->setSwitchId(SwitchID(switchId));
      sysPort->setName(fmt::format("{}:{}", nodeName, port.second->getName()));
      auto platformPort =
          platformMapping_->getPlatformPort(port.second->getID());
      CHECK(platformPort.mapping()->attachedCoreId().has_value());
      CHECK(platformPort.mapping()->attachedCorePortIndex().has_value());
      sysPort->setCoreIndex(platformPort.mapping()->attachedCoreId().value());
      sysPort->setCorePortIndex(
          platformPort.mapping()->attachedCorePortIndex().value());
      sysPort->setSpeedMbps(static_cast<int>(port.second->getSpeed()));
      sysPort->setNumVoqs(getLocalPortNumVoqs(
          port.second->getPortType(), port.second->getScope()));
      sysPort->setQosPolicy(port.second->getQosPolicy());
      sysPort->resetPortQueues(getVoqConfig(port.second->getID()));
      // TODO(daiweix): remove this CHECK_EQ after verifying scope config is
      // always correct
      CHECK_EQ(
          static_cast<int>(platformPort.mapping()->scope().value()),
          static_cast<int>(port.second->getScope()));
      sysPort->setScope(port.second->getScope());
      if (port.second->getPortType() != cfg::PortType::HYPER_PORT_MEMBER) {
        sysPort->setShelDestinationEnabled(
            cfg_->switchSettings()->selfHealingEcmpLagConfig().has_value() &&
            port.second->getPortType() == cfg::PortType::RECYCLE_PORT &&
            port.second->getScope() == cfg::Scope::GLOBAL);
      }
      sysPort->setPortType(port.second->getPortType());
      sysPorts->addSystemPort(std::move(sysPort));
    }
  }

  return sysPorts;
}

std::shared_ptr<MultiSwitchSystemPortMap>
ThriftConfigApplier::updateRemoteSystemPorts(
    const std::shared_ptr<SystemPortMap>& systemPorts) {
  if (scopeResolver_.hasVoq() &&
      scopeResolver_.scope(cfg::SwitchType::VOQ).size() <= 1) {
    // remote system ports are applicable only for voq switches
    // remote system ports are updated on config only when more than voq
    // switches are configured on a given SwSwitch
    return new_->getRemoteSystemPorts();
  }
  auto globalSystemPorts = std::make_shared<SystemPortMap>();
  // Remote system port state represents ports owned by other switches. Local
  // scoped system ports are valid on every switch, so exclude them from this
  // per-remote-switch map to avoid resolving them to all switch IDs.
  for (const auto& [_, sysPort] : std::as_const(*systemPorts)) {
    if (sysPort->getScope() == cfg::Scope::GLOBAL) {
      globalSystemPorts->addSystemPort(sysPort);
    }
  }
  auto remoteSystemPorts = new_->getRemoteSystemPorts()->clone();
  auto globalSystemPortMap = toMultiSwitchMap<MultiSwitchSystemPortMap>(
      globalSystemPorts, scopeResolver_);
  for (const auto& [matcherStr, singleSwitchIdSysPorts] :
       std::as_const(*globalSystemPortMap)) {
    auto matcher = HwSwitchMatcher(matcherStr);
    CHECK_EQ(matcher.switchIds().size(), 1);
    auto remoteSystemPortMapMatcher =
        scopeResolver_.scope(cfg::SwitchType::VOQ);
    remoteSystemPortMapMatcher.exclude(matcher.switchIds());
    if (remoteSystemPorts->getMapNodeIf(remoteSystemPortMapMatcher)) {
      remoteSystemPorts->updateMapNode(
          singleSwitchIdSysPorts, remoteSystemPortMapMatcher);
    } else {
      remoteSystemPorts->addMapNode(
          singleSwitchIdSysPorts, remoteSystemPortMapMatcher);
    }
  }
  return remoteSystemPorts;
}

bool ThriftConfigApplier::needFabricLinkMonSystemPortUpdate(
    const std::shared_ptr<MultiSwitchSettings>& origMultiSwitchSettings,
    const std::shared_ptr<MultiSwitchSettings>& newMultiSwitchSettings,
    const SwitchIdScopeResolver& scopeResolver) {
  if (!scopeResolver.hasVoq()) {
    // Fabric link monitoring is applicable only for voq switches
    return false;
  }

  // Check if the fabricLinkMonitoringSystemPortOffset() configuration
  // has changed.
  for (auto& switchIdAndSwitchInfo : scopeResolver.switchIdToSwitchInfo()) {
    auto switchId = switchIdAndSwitchInfo.first;
    auto matcher = HwSwitchMatcher(
        std::unordered_set<SwitchID>({static_cast<SwitchID>(switchId)}));

    auto origSwitchSettings =
        origMultiSwitchSettings->getNodeIf(matcher.matcherString());
    auto newSwitchSettings =
        newMultiSwitchSettings->getNodeIf(matcher.matcherString());
    std::optional<int32_t> origFabricLinkMonitoringSystemPortOffset =
        origSwitchSettings
        ? origSwitchSettings->getFabricLinkMonitoringSystemPortOffset()
        : std::nullopt;
    std::optional<int32_t> newFabricLinkMonitoringSystemPortOffset =
        newSwitchSettings
        ? newSwitchSettings->getFabricLinkMonitoringSystemPortOffset()
        : std::nullopt;
    if (origFabricLinkMonitoringSystemPortOffset !=
        newFabricLinkMonitoringSystemPortOffset) {
      return true;
    }
  }
  return false;
}

std::optional<int32_t> ThriftConfigApplier::getFabricLinkMonitoringPortSwitchId(
    const PortID& portId,
    const cfg::PortType& type,
    const size_t expectedNeighborCount) const {
  std::optional<SwitchID> linkSwitchId;
  if (FLAGS_enable_fabric_link_monitoring &&
      type == cfg::PortType::FABRIC_PORT && expectedNeighborCount > 0) {
    // Fabric link mon supported only for fabric ports
    // with valid expected neighbors.
    CHECK(fabricLinkMon_ != nullptr)
        << "Fabric link monitoring not initialized!";
    linkSwitchId = fabricLinkMon_->getSwitchIdForPort(portId);
  }
  return linkSwitchId;
}

shared_ptr<PortMap> ThriftConfigApplier::updatePorts(
    const std::shared_ptr<MultiSwitchTransceiverMap>& transceiverMap) {
  const auto origPorts = orig_->getPorts();
  PortMap::NodeContainer newPorts;
  bool changed = false;

  sharedBufferPoolName.reset();

  // Process all supplied port configs
  for (const auto& portCfg : *cfg_->ports()) {
    PortID id(*portCfg.logicalID());
    auto origPort = origPorts->getNodeIf(id);
    std::shared_ptr<Port> newPort;
    // Find present Transceiver if it exists in TransceiverMap
    std::shared_ptr<TransceiverSpec> transceiver;
    auto platformPort = platformMapping_->getPlatformPort(id);
    const auto& chips = platformMapping_->getChips();
    if (auto tcvrIds = utility::getTransceiverIds(platformPort, chips);
        !tcvrIds.empty()) {
      transceiver = transceiverMap->getNodeIf(tcvrIds[0]);
    }
    if (!origPort) {
      state::PortFields portFields;
      portFields.portId() = *portCfg.logicalID();
      portFields.portName() = portCfg.name().value_or({});
      auto port = std::make_shared<Port>(std::move(portFields));
      newPort = updatePort(port, &portCfg, transceiver);
    } else {
      newPort = updatePort(origPort, &portCfg, transceiver);
    }
    changed |= updateMap(&newPorts, origPort, newPort);
  }

  for (const auto& origPortMap : std::as_const(*origPorts)) {
    for (const auto& origPort : std::as_const(*origPortMap.second)) {
      // This port was listed in the config, and has already been configured
      if (newPorts.find(origPort.second->getID()) != newPorts.end()) {
        continue;
      }

      // For platforms that support add/removing ports, we should leave the
      // ports without configs out of the switch state. For BCM tests + hardware
      // that doesn't allow add/remove, we need to leave the ports in the switch
      // state with a default (disabled) config.
      if (supportsAddRemovePort_) {
        changed = true;
      } else {
        throw FbossError(
            "New config is missing configuration for port ",
            origPort.second->getID());
      }
    }
  }

  if (!changed) {
    return nullptr;
  }

  auto ports = std::make_shared<PortMap>(std::move(newPorts));
  return ports;
}

void ThriftConfigApplier::checkPortQueueAQMValid(
    const std::vector<cfg::ActiveQueueManagement>& aqms) {
  if (aqms.empty()) {
    return;
  }
  std::set<cfg::QueueCongestionBehavior> behaviors;
  for (const auto& aqm : aqms) {
    if (aqm.detection()->getType() ==
        cfg::QueueCongestionDetection::Type::__EMPTY__) {
      throw FbossError(
          "Active Queue Management must specify a congestion detection method");
    }
    if (behaviors.find(*aqm.behavior()) != behaviors.end()) {
      throw FbossError("Same Active Queue Management behavior already exists");
    }
    behaviors.insert(*aqm.behavior());
  }
}

std::shared_ptr<PortQueue> ThriftConfigApplier::updatePortQueue(
    const std::shared_ptr<PortQueue>& orig,
    const cfg::PortQueue* cfg,
    std::optional<TrafficClass> trafficClass,
    std::optional<std::set<PfcPriority>> pfcPriorities) {
  CHECK_EQ(orig->getID(), *cfg->id());

  if (checkSwConfPortQueueMatch(orig, cfg) &&
      trafficClass == orig->getTrafficClass() &&
      pfcPriorities == orig->getPfcPrioritySet()) {
    return orig;
  }

  // We should always use the PortQueue settings from config, so that if some
  // of the attributes is removed from config, we can make sure that attribute
  // can set back to default
  return createPortQueue(cfg, trafficClass, pfcPriorities);
}

std::shared_ptr<PortQueue> ThriftConfigApplier::createPortQueue(
    const cfg::PortQueue* cfg,
    std::optional<TrafficClass> trafficClass,
    std::optional<std::set<PfcPriority>> pfcPriorities) {
  auto queue = std::make_shared<PortQueue>(static_cast<uint8_t>(*cfg->id()));
  queue->setStreamType(*cfg->streamType());
  queue->setScheduling(*cfg->scheduling());
  if (auto weight = cfg->weight()) {
    queue->setWeight(*weight);
  }
  if (auto reservedBytes = cfg->reservedBytes()) {
    queue->setReservedBytes(*reservedBytes);
  }
  if (auto scalingFactor = cfg->scalingFactor()) {
    queue->setScalingFactor(*scalingFactor);
  }
  if (auto aqms = cfg->aqms()) {
    checkPortQueueAQMValid(*aqms);
    queue->resetAqms(*aqms);
  }
  if (auto shareBytes = cfg->sharedBytes()) {
    queue->setSharedBytes(*shareBytes);
  }
  if (auto name = cfg->name()) {
    queue->setName(*name);
  }
  if (auto portQueueRate = cfg->portQueueRate()) {
    queue->setPortQueueRate(*portQueueRate);
  }
  if (auto bandwidthBurstMinKbits = cfg->bandwidthBurstMinKbits()) {
    queue->setBandwidthBurstMinKbits(*bandwidthBurstMinKbits);
  }
  if (auto bandwidthBurstMaxKbits = cfg->bandwidthBurstMaxKbits()) {
    queue->setBandwidthBurstMaxKbits(*bandwidthBurstMaxKbits);
  }
  if (trafficClass) {
    queue->setTrafficClasses(trafficClass.value());
  }
  if (pfcPriorities) {
    queue->setPfcPrioritySet(pfcPriorities.value());
  }
  if (auto maxDynamicSharedBytes = cfg->maxDynamicSharedBytes()) {
    queue->setMaxDynamicSharedBytes(*maxDynamicSharedBytes);
  }
  if (auto bufferPoolName = cfg->bufferPoolName()) {
    auto bufferPoolCfgMap = new_->getBufferPoolCfgs();
    // bufferPool cfg is keyed on the buffer pool name
    auto bufferPoolCfg = bufferPoolCfgMap->getNodeIf(*bufferPoolName);
    if (!bufferPoolCfg) {
      throw FbossError(
          "Queue: ",
          queue->getID(),
          " buffer pool: ",
          *bufferPoolName,
          " doesn't exist in the bufferPool map.");
    }
    queue->setBufferPoolName(*bufferPoolName);
    queue->setBufferPoolConfig(bufferPoolCfg);
  }
  return queue;
}

std::shared_ptr<PortPgConfig> ThriftConfigApplier::createPortPg(
    const cfg::PortPgConfig* cfg) {
  auto pgCfg = std::make_shared<PortPgConfig>(static_cast<uint8_t>(*cfg->id()));
  if (const auto scalingFactor = cfg->scalingFactor()) {
    pgCfg->setScalingFactor(*scalingFactor);
  }

  if (const auto name = cfg->name()) {
    pgCfg->setName(*name);
  }

  pgCfg->setMinLimitBytes(*cfg->minLimitBytes());

  if (const auto headroom = cfg->headroomLimitBytes()) {
    pgCfg->setHeadroomLimitBytes(*headroom);
  }
  if (cfg->resumeBytes().has_value() && cfg->resumeOffsetBytes().has_value()) {
    throw FbossError(
        "resumeBytes and resumeOffsetBytes should not be set together");
  }
  if (const auto resumeOffsetBytes = cfg->resumeOffsetBytes()) {
    pgCfg->setResumeOffsetBytes(*resumeOffsetBytes);
  }
  if (const auto resumeBytes = cfg->resumeBytes()) {
    pgCfg->setResumeBytes(*resumeBytes);
  }
  pgCfg->setBufferPoolName(*cfg->bufferPoolName());
  if (const auto maxSharedXoffThresholdBytes =
          cfg->maxSharedXoffThresholdBytes()) {
    pgCfg->setMaxSharedXoffThresholdBytes(*maxSharedXoffThresholdBytes);
  }
  if (const auto minSharedXoffThresholdBytes =
          cfg->minSharedXoffThresholdBytes()) {
    pgCfg->setMinSharedXoffThresholdBytes(*minSharedXoffThresholdBytes);
  }
  if (const auto maxSramXoffThresholdBytes = cfg->maxSramXoffThresholdBytes()) {
    pgCfg->setMaxSramXoffThresholdBytes(*maxSramXoffThresholdBytes);
  }
  if (const auto minSramXoffThresholdBytes = cfg->minSramXoffThresholdBytes()) {
    pgCfg->setMinSramXoffThresholdBytes(*minSramXoffThresholdBytes);
  }
  if (const auto sramResumeOffsetBytes = cfg->sramResumeOffsetBytes()) {
    pgCfg->setSramResumeOffsetBytes(*sramResumeOffsetBytes);
  }
  if (const auto sramScalingFactor = cfg->sramScalingFactor()) {
    pgCfg->setSramScalingFactor(*sramScalingFactor);
  }
  if (const auto staticLimitBytes = cfg->staticLimitBytes()) {
    pgCfg->setStaticLimitBytes(*staticLimitBytes);
  }
  return pgCfg;
}

bool ThriftConfigApplier::isPgConfigUnchanged(
    std::optional<PortPgConfigs> newPortPgCfgs,
    const shared_ptr<Port>& orig) {
  std::map<int, std::shared_ptr<PortPgConfig>> newPortPgConfigMap;
  const auto& origPortPgConfig = orig->getPortPgConfigs();

  if (!origPortPgConfig && !newPortPgCfgs) {
    // no change before or after
    return true;
  }

  // if go from old pgConfig <-> no pg cfg and vice versa, there is a change
  if ((origPortPgConfig && !newPortPgCfgs) ||
      (!origPortPgConfig && newPortPgCfgs)) {
    return false;
  }

  // both oldPgCfg and newPgCfg exists
  if ((*newPortPgCfgs).size() != origPortPgConfig->size()) {
    return false;
  }

  // come here only if we have both orig, and new port pg cfg and have same size
  for (const auto& portPg : *newPortPgCfgs) {
    newPortPgConfigMap[portPg->getID()] = portPg;
  }

  for (const auto& origPg : *origPortPgConfig) {
    auto newPortPgConfigIter =
        newPortPgConfigMap.find(origPg->cref<switch_state_tags::id>()->cref());
    // THRIFT_COPY - no need to compare buffer pool since toThrift() will
    // compare thrift fields for buffer pool as well.
    if ((newPortPgConfigIter == newPortPgConfigMap.end()) ||
        ((newPortPgConfigIter->second->toThrift() != origPg->toThrift()))) {
      // pg id in the original cfg, is no longer there is new one
      // or the contents of the PG doesn't match
      return false;
    }
  }
  return true;
}

PortPgConfigs ThriftConfigApplier::updatePortPgConfigs(
    const std::vector<cfg::PortPgConfig>& newPortPgConfig,
    const shared_ptr<Port>& orig) {
  PortPgConfigs newPortPgConfigs = {};
  if (newPortPgConfig.empty()) {
    // nothing to update, just return
    return newPortPgConfigs;
  }

  // pg value can be [0, PORT_PG_VALUE_MAX]
  if (newPortPgConfig.size() >
      cfg::switch_config_constants::PORT_PG_VALUE_MAX() + 1) {
    throw FbossError(
        "Port",
        orig->getID(),
        " pgConfig size ",
        newPortPgConfig.size(),
        " greater than max supported pgs ",
        cfg::switch_config_constants::PORT_PG_VALUE_MAX() + 1);
  }

  for (const auto& portPg : newPortPgConfig) {
    std::shared_ptr<PortPgConfig> tmpPortPgConfig;
    tmpPortPgConfig = createPortPg(&portPg);
    newPortPgConfigs.push_back(tmpPortPgConfig);
  }

  // sort these Pgs in order PG0 -> PG7
  std::sort(
      newPortPgConfigs.begin(),
      newPortPgConfigs.end(),
      [](const std::shared_ptr<PortPgConfig>& pg1,
         const std::shared_ptr<PortPgConfig>& pg2) {
        return pg1->getID() < pg2->getID();
      });

  return newPortPgConfigs;
}

std::optional<std::vector<int16_t>>
ThriftConfigApplier::findEnabledPfcPriorities(
    PortPgConfigs& portPgCfgs,
    const std::map<int16_t, int16_t>& pfcPriorityToPgId) {
  auto enabledPriorities =
      utility::findPfcEnabledPriorities(portPgCfgs, pfcPriorityToPgId);
  if (enabledPriorities.empty()) {
    return std::nullopt;
  }
  std::vector<int16_t> pfcPri;
  pfcPri.reserve(enabledPriorities.size());
  for (auto priority : enabledPriorities) {
    pfcPri.push_back(static_cast<int16_t>(priority));
  }
  return pfcPri;
}

bool ThriftConfigApplier::isPortFlowletConfigUnchanged(
    std::shared_ptr<PortFlowletCfg> newPortFlowletCfg,
    const shared_ptr<Port>& port) {
  std::shared_ptr<PortFlowletCfg> oldPortFlowletCfg{nullptr};
  if (port->getPortFlowletConfig().has_value()) {
    oldPortFlowletCfg = port->getPortFlowletConfig().value();
  }
  // old port flowlet cfg exists and new one doesn't or vice versa
  if ((newPortFlowletCfg && !oldPortFlowletCfg) ||
      (!newPortFlowletCfg && oldPortFlowletCfg)) {
    return false;
  }
  // contents changed in the port flowlet cfg
  if (oldPortFlowletCfg && newPortFlowletCfg) {
    if (*oldPortFlowletCfg != *newPortFlowletCfg) {
      return false;
    }
  }
  return true;
}

bool ThriftConfigApplier::isLlrConfigUnchanged(
    std::shared_ptr<LlrConfig> newLlrConfig,
    const shared_ptr<Port>& port) {
  std::shared_ptr<LlrConfig> oldLlrConfig{nullptr};
  if (port->getLlrConfig().has_value()) {
    oldLlrConfig = port->getLlrConfig().value();
  }
  if ((newLlrConfig && !oldLlrConfig) || (!newLlrConfig && oldLlrConfig)) {
    return false;
  }
  if (oldLlrConfig && newLlrConfig) {
    if (*oldLlrConfig != *newLlrConfig) {
      return false;
    }
  }
  return true;
}

QueueConfig ThriftConfigApplier::updatePortQueues(
    const QueueConfig& origPortQueues,
    const std::vector<cfg::PortQueue>& cfgPortQueues,
    uint16_t baseQueueId,
    uint16_t maxQueues,
    cfg::StreamType streamType,
    std::optional<cfg::QosMap> qosMap,
    std::optional<cfg::PortType> portType,
    bool resetDefaultQueue) {
  QueueConfig newPortQueues;

  /*
   * By default, queue config is picked from defaultPortQueues. However, per
   * port queue config, if specified, overrides it.
   */
  flat_map<int, const cfg::PortQueue*> newQueues;
  for (const auto& queue : cfgPortQueues) {
    if (streamType == queue.streamType()) {
      newQueues.emplace(std::make_pair(*queue.id(), &queue));
    }
  }

  if (newQueues.empty()) {
    for (const auto& queue : *cfg_->defaultPortQueues()) {
      if (streamType == queue.streamType()) {
        newQueues.emplace(std::make_pair(*queue.id(), &queue));
      }
    }
  }

  // Process all supplied queues
  // We retrieve the current port queue values from hardware
  // if there is a config present for any of these queues, we update the
  // PortQueue according to this
  // Otherwise we reset it to the default values for this queue type
  for (auto queueId = baseQueueId; queueId < baseQueueId + maxQueues;
       queueId++) {
    auto newQueueIter = newQueues.find(queueId);
    std::shared_ptr<PortQueue> newPortQueue;
    if (newQueueIter != newQueues.end()) {
      std::optional<TrafficClass> trafficClass;
      std::optional<std::set<PfcPriority>> pfcPriorities;
      if (qosMap) {
        // Get traffic class for such queue if exists
        for (auto entry : *qosMap->trafficClassToQueueId()) {
          if (entry.second == queueId) {
            trafficClass = static_cast<TrafficClass>(entry.first);
            break;
          }
        }

        if (const auto& pfcPri2QueueIdMap = qosMap->pfcPriorityToQueueId()) {
          std::set<PfcPriority> tmpPfcPriSet;
          for (const auto& pfcPri2QueueId : *pfcPri2QueueIdMap) {
            if (pfcPri2QueueId.second == queueId) {
              auto pfcPriority = static_cast<PfcPriority>(pfcPri2QueueId.first);
              tmpPfcPriSet.insert(pfcPriority);
            }
          }
          if (tmpPfcPriSet.size()) {
            // dont populate port queue priority if empty
            pfcPriorities = tmpPfcPriSet;
          }
        }
      }
      auto origQueueIter = std::find_if(
          origPortQueues.begin(),
          origPortQueues.end(),
          [&](const auto& origQueue) { return queueId == origQueue->getID(); });
      newPortQueue = (origQueueIter != origPortQueues.end())
          ? updatePortQueue(
                *origQueueIter,
                newQueueIter->second,
                trafficClass,
                pfcPriorities)
          : createPortQueue(newQueueIter->second, trafficClass, pfcPriorities);
      newQueues.erase(newQueueIter);
      newPortQueues.push_back(newPortQueue);
    } else if (resetDefaultQueue) {
      if (hwAsicTable_->isFeatureSupportedOnAnyAsic(
              HwAsic::Feature::MANAGEMENT_PORT_MULTICAST_QUEUE_ALPHA) &&
          portType.has_value() && *portType == cfg::PortType::MANAGEMENT_PORT &&
          streamType == cfg::StreamType::MULTICAST) {
        // Program default multicast queue alpha, to enable sFlow on mgmt ports.
        XLOG(DBG2) << "Adding multicast queue " << static_cast<int>(queueId)
                   << " with scaling factor "
                   << apache::thrift::util::enumNameSafe(kMcQueueScalingFactor);
        newPortQueue =
            std::make_shared<PortQueue>(static_cast<uint8_t>(queueId));
        newPortQueue->setStreamType(streamType);
        newPortQueue->setScalingFactor(kMcQueueScalingFactor);
        newPortQueues.push_back(newPortQueue);
      } else {
        // Resetting defaut queues are not applicable to VOQs - we only
        // configure the ones present in config.
        newPortQueue =
            std::make_shared<PortQueue>(static_cast<uint8_t>(queueId));
        newPortQueue->setStreamType(streamType);
        if (streamType == cfg::StreamType::FABRIC_TX) {
          newPortQueue->setScheduling(cfg::QueueScheduling::INTERNAL);
        }
        newPortQueues.push_back(newPortQueue);
      }
    }
  }

  std::sort(
      newPortQueues.begin(),
      newPortQueues.end(),
      [](const std::shared_ptr<PortQueue>& q1,
         const std::shared_ptr<PortQueue>& q2) {
        return q1->getID() < q2->getID();
      });
  if (newQueues.size() > 0) {
    throw FbossError(
        "Port queue config listed for invalid queues. Maximum",
        " number of queues on this platform is ",
        maxQueues);
  }
  return newPortQueues;
}

void ThriftConfigApplier::validateUpdatePgBufferPoolName(
    const PortPgConfigs& portPgCfgs,
    const shared_ptr<Port>& port,
    const std::string& portPgName) {
  // this is processed after bufferPoolConfig changes
  // validate that bufferPool name exists in the cfg
  for (const auto& portPg : portPgCfgs) {
    auto bufferPoolName = portPg->getBufferPoolName();

    // only one buffer pool is supported
    // check buffer pool name is common across pgs and across ports
    if (!sharedBufferPoolName.has_value()) {
      sharedBufferPoolName = bufferPoolName;
      XLOG(DBG2) << "sharedBufferPoolName: " << sharedBufferPoolName.value();
    } else if (sharedBufferPoolName != bufferPoolName) {
      throw FbossError(
          "Port:",
          port->getID(),
          " with pg name: ",
          portPgName,
          " buffer pool name: ",
          bufferPoolName,
          " is different from shared buffer pool name: ",
          sharedBufferPoolName.value(),
          " Only one bufferPool supported! ");
    }

    if (!portPg->getBufferPoolName().empty()) {
      auto bufferPoolCfgMap = new_->getBufferPoolCfgs();
      // bufferPool cfg is keyed on the buffer pool name
      auto bufferPoolCfg = bufferPoolCfgMap->getNodeIf(bufferPoolName);
      if (!bufferPoolCfg) {
        throw FbossError(
            "Port:",
            port->getID(),
            " with pg name: ",
            portPgName,
            " but buffer pool name: ",
            bufferPoolName,
            " doesn't exist in the bufferPool map.");
      }
      portPg->setBufferPoolConfig(bufferPoolCfg);
    }
  }
}

shared_ptr<Port> ThriftConfigApplier::updatePort(
    const shared_ptr<Port>& orig,
    const cfg::Port* portConf,
    const shared_ptr<TransceiverSpec>& transceiver) {
  CHECK_EQ(orig->getID(), PortID(*portConf->logicalID()));

  if (portConf->linkTraining().value_or(false) &&
      (portConf->txPrecoding().value_or(false) ||
       portConf->rxPrecoding().value_or(false))) {
    throw FbossError(
        "Port ",
        orig->getID(),
        " linkTraining and precoding cannot both be enabled");
  }

  auto vlans = portVlans_[orig->getID()];

  std::vector<cfg::PortQueue> cfgPortQueues;
  if (auto portQueueConfigName = portConf->portQueueConfigName()) {
    auto it = cfg_->portQueueConfigs()->find(*portQueueConfigName);
    if (it == cfg_->portQueueConfigs()->end()) {
      throw FbossError(
          "Port queue config name: ",
          *portQueueConfigName,
          " does not exist in PortQueueConfig map");
    }
    cfgPortQueues = it->second;
  }

  const auto& oldIngressMirror = orig->getIngressMirror();
  const auto& oldEgressMirror = orig->getEgressMirror();
  auto newIngressMirror = std::optional<std::string>();
  auto newEgressMirror = std::optional<std::string>();
  if (auto ingressMirror = portConf->ingressMirror()) {
    newIngressMirror = *ingressMirror;
  }
  if (auto egressMirror = portConf->egressMirror()) {
    newEgressMirror = *egressMirror;
  }
  bool mirrorsUnChanged = (oldIngressMirror == newIngressMirror) &&
      (oldEgressMirror == newEgressMirror);

  auto newQosPolicy = std::optional<std::string>();
  if (auto dataPlaneTrafficPolicy = cfg_->dataPlaneTrafficPolicy()) {
    if (auto defaultDataPlaneQosPolicy =
            dataPlaneTrafficPolicy->defaultQosPolicy()) {
      newQosPolicy = *defaultDataPlaneQosPolicy;
    }
    if (auto portIdToQosPolicy = dataPlaneTrafficPolicy->portIdToQosPolicy()) {
      auto qosPolicyItr = portIdToQosPolicy->find(*portConf->logicalID());
      if (qosPolicyItr != portIdToQosPolicy->end()) {
        newQosPolicy = qosPolicyItr->second;
      }
    }
  }

  std::optional<cfg::QosMap> qosMap;
  if (newQosPolicy) {
    bool qosPolicyFound = false;
    for (auto qosPolicy : *cfg_->qosPolicies()) {
      if (qosPolicyFound) {
        break;
      }
      qosPolicyFound = (*qosPolicy.name() == newQosPolicy.value());
      if (qosPolicyFound && qosPolicy.qosMap()) {
        qosMap = qosPolicy.qosMap().value();
      }
    }
    if (!qosPolicyFound) {
      throw FbossError("qos policy ", newQosPolicy.value(), " not found");
    }
  }

  const auto& switchSettings =
      utility::getFirstNodeIf(new_->getSwitchSettings());
  // For now, we only support update unicast port queues for ports
  auto switchIds =
      SwitchIdScopeResolver(switchSettings->getSwitchIdToSwitchInfo())
          .scope(orig->getID())
          .switchIds();
  CHECK_EQ(switchIds.size(), 1);
  auto asic = hwAsicTable_->getHwAsicIf(*switchIds.begin());
  CHECK(asic != nullptr);
  QueueConfig portQueues;
  for (auto streamType : asic->getQueueStreamTypes(*portConf->portType())) {
    auto baseQueueId =
        asic->getBasePortQueueId(streamType, *portConf->portType());
    auto maxQueues =
        asic->getDefaultNumPortQueues(streamType, *portConf->portType());
    auto tmpPortQueues = updatePortQueues(
        orig->getPortQueues()->impl(),
        cfgPortQueues,
        baseQueueId,
        maxQueues,
        streamType,
        qosMap,
        *portConf->portType());
    portQueues.insert(
        portQueues.begin(), tmpPortQueues.begin(), tmpPortQueues.end());
  }
  bool queuesUnchanged = portQueues.size() == orig->getPortQueues()->size();
  for (int i = 0; i < portQueues.size() && queuesUnchanged; i++) {
    if (*(portQueues.at(i)) != *(orig->getPortQueues()->at(i))) {
      queuesUnchanged = false;
      break;
    }
  }

  auto newSampleDest = std::optional<cfg::SampleDestination>();
  if (portConf->sampleDest()) {
    newSampleDest = portConf->sampleDest().value();

    if (newSampleDest.value() == cfg::SampleDestination::MIRROR &&
        *portConf->sFlowEgressRate() > 0) {
      throw FbossError(
          "Port ",
          orig->getID(),
          ": Egress sampling to mirror destination is unsupported");
    }
    // sampleDest applies to both directions, so an egressMirror on a
    // MIRROR-destination port would be programmed as an egress sampled
    // mirror, which is unsupported (and has no egress sampler behind it,
    // since sFlowEgressRate must be 0 above).
    if (newSampleDest.value() == cfg::SampleDestination::MIRROR &&
        portConf->egressMirror().has_value()) {
      throw FbossError(
          "Port ",
          orig->getID(),
          ": egressMirror cannot be combined with sampleDest MIRROR, "
          "egress sampling to mirror destination is unsupported");
    }
  }

  auto newPfc = std::optional<cfg::PortPfc>();
  auto newPfcPriorities = std::optional<std::vector<int16_t>>();
  std::optional<PortPgConfigs> portPgCfgs;
  // lets compare the portPgConfigs
  bool portPgConfigUnchanged = true;
  if (portConf->pfc().has_value()) {
    newPfc = portConf->pfc().value();

    auto pause = portConf->pause().value();
    bool pfc_rx = *newPfc->rx();
    bool pfc_tx = *newPfc->tx();
    bool pause_rx = *pause.rx();
    bool pause_tx = *pause.tx();

    if (pfc_rx || pfc_tx) {
      if (!asic->isSupported(HwAsic::Feature::PFC)) {
        throw FbossError(
            "Port ",
            orig->getID(),
            " has PFC enabled, but its not supported feature for this platform");
      }
      if (pause_rx || pause_tx) {
        throw FbossError(
            "Port ",
            orig->getID(),
            " PAUSE and PFC cannot be enabled on the same port");
      }
    }

    auto portPgConfigName = newPfc->portPgConfigName();
    if (newPfc->watchdog().has_value() && (*portPgConfigName).empty()) {
      throw FbossError(
          "Port ",
          orig->getID(),
          " Priority group must be associated with port "
          "when PFC watchdog is configured");
    }
    if (auto portPgConfigs = cfg_->portPgConfigs()) {
      auto it = portPgConfigs->find(*portPgConfigName);
      if (it == portPgConfigs->end()) {
        throw FbossError(
            "Port ",
            orig->getID(),
            " pg name ",
            *portPgConfigName,
            " does not exist in portPgConfig map");
      }
      portPgCfgs = updatePortPgConfigs(it->second, orig);
      // validate that the given pg profile points to valid
      // buffer pool
      validateUpdatePgBufferPoolName(
          portPgCfgs.value(), orig, *portPgConfigName);

      // Enabled PFC priorities; empty pfcPriorityToPgId => identity.
      std::map<int16_t, int16_t> pfcPriorityToPgId;
      if (qosMap && qosMap->pfcPriorityToPgId().has_value()) {
        pfcPriorityToPgId = *qosMap->pfcPriorityToPgId();
      }
      newPfcPriorities =
          findEnabledPfcPriorities(portPgCfgs.value(), pfcPriorityToPgId);
    } else if (!(*portPgConfigName).empty()) {
      throw FbossError(
          "Port: ",
          orig->getID(),
          " pg name: ",
          *portPgConfigName,
          " exist but not the portPgConfig map");
    }
  }
  // CBFC virtual channels, resolved from Port.cbfcConfigName rather than from
  // PortPfc. PortPfc may still be present to carry the PG config, but with PFC
  // disabled.
  std::optional<std::vector<state::PortVcFields>> virtualChannels;
  std::optional<int64_t> cbfcSenderCreditLimit;
  std::optional<int16_t> cbfcSenderCreditSize;
  std::optional<int16_t> cbfcSenderPacketOverhead;
  std::optional<std::string> newCbfcConfigName;
  if (auto cbfcConfigName = portConf->cbfcConfigName()) {
    newCbfcConfigName = *cbfcConfigName;
    // The SDK rejects CBFC attributes on non-interface ports (e.g. TU1's
    // management port), and a failed attribute write during port programming
    // terminates the HW agent.
    if (*portConf->portType() != cfg::PortType::INTERFACE_PORT) {
      throw FbossError(
          "Port ",
          orig->getID(),
          " of type ",
          apache::thrift::util::enumNameSafe(*portConf->portType()),
          " has cbfc config name ",
          *cbfcConfigName,
          "; CBFC is supported on only interface ports");
    }
    // TU1 cannot receive PFC and CBFC on the same port (78920-PG102 15.2.5).
    // Keeping pfc with tx and rx off is allowed: it is how the port keeps its
    // PG config.
    if (newPfc && (*newPfc->tx() || *newPfc->rx())) {
      throw FbossError(
          "Port ",
          orig->getID(),
          " has cbfc config name ",
          *cbfcConfigName,
          " and PFC enabled (tx=",
          *newPfc->tx(),
          ", rx=",
          *newPfc->rx(),
          "); PFC and CBFC cannot both be enabled on a port. Set pfc tx and rx"
          " to false; portPgConfigName may stay.");
    }
    auto cbfcConfigs = cfg_->cbfcConfigs();
    if (!cbfcConfigs) {
      throw FbossError(
          "Port ",
          orig->getID(),
          " cbfc config name ",
          *cbfcConfigName,
          " set but there is no cbfcConfigs map");
    }
    auto it = cbfcConfigs->find(*cbfcConfigName);
    if (it == cbfcConfigs->end()) {
      throw FbossError(
          "Port ",
          orig->getID(),
          " cbfc config name ",
          *cbfcConfigName,
          " does not exist in cbfcConfigs map");
    }
    if (it->second.virtualChannels()->empty()) {
      throw FbossError(
          "Port ",
          orig->getID(),
          " cbfc config name ",
          *cbfcConfigName,
          " has no virtual channels; there is nothing to credit. Leave"
          " cbfcConfigName unset on a port that does not run CBFC.");
    }
    std::set<int16_t> seenVcIds;
    std::vector<state::PortVcFields> configuredVcs;
    for (const auto& vc : *it->second.virtualChannels()) {
      auto vcId = *vc.id();
      if (vcId < 0 ||
          vcId > cfg::switch_config_constants::PORT_VC_VALUE_MAX()) {
        throw FbossError(
            "Invalid vc id ",
            vcId,
            ". Valid range is 0 to: ",
            cfg::switch_config_constants::PORT_VC_VALUE_MAX());
      }
      if (!seenVcIds.insert(vcId).second) {
        throw FbossError(
            "Duplicate vc id ", vcId, " in cbfc config ", *cbfcConfigName);
      }
      if (auto reserved = vc.reservedCreditSize(); reserved && *reserved < 0) {
        throw FbossError(
            "Invalid reservedCreditSize ", *reserved, " for vc id ", vcId);
      }
      state::PortVcFields vcFields;
      vcFields.id() = vcId;
      if (auto name = vc.name()) {
        vcFields.name() = *name;
      }
      vcFields.senderEnable() = *vc.senderEnable();
      vcFields.receiverEnable() = *vc.receiverEnable();
      if (auto reserved = vc.reservedCreditSize()) {
        vcFields.reservedCreditSize() = *reserved;
      }
      configuredVcs.push_back(std::move(vcFields));
    }
    if (auto limit = it->second.senderCreditLimit()) {
      if (*limit < 0 ||
          *limit > cfg::switch_config_constants::
                       PORT_CBFC_SENDER_CREDIT_LIMIT_MAX()) {
        throw FbossError(
            "Invalid senderCreditLimit ",
            *limit,
            ", must be 0..",
            cfg::switch_config_constants::PORT_CBFC_SENDER_CREDIT_LIMIT_MAX());
      }
      cbfcSenderCreditLimit = *limit;
    }
    auto checkSupported = [&](const char* field,
                              int16_t value,
                              const std::vector<int16_t>& supported) {
      if (std::find(supported.begin(), supported.end(), value) ==
          supported.end()) {
        throw FbossError(
            "Port ",
            orig->getID(),
            " cbfc config ",
            *cbfcConfigName,
            ": ",
            field,
            " ",
            value,
            " is not supported by ",
            asic->getAsicTypeStr(),
            ", supported: ",
            folly::join(", ", supported));
      }
    };
    auto size = it->second.senderCreditSize();
    if (!size) {
      throw FbossError(
          "Port ",
          orig->getID(),
          " cbfc config ",
          *cbfcConfigName,
          ": senderCreditSize is required");
    }
    checkSupported("senderCreditSize", *size, asic->getCbfcSenderCreditSizes());
    cbfcSenderCreditSize = *size;
    auto overhead = it->second.senderPacketOverhead();
    if (!overhead) {
      throw FbossError(
          "Port ",
          orig->getID(),
          " cbfc config ",
          *cbfcConfigName,
          ": senderPacketOverhead is required");
    }
    checkSupported(
        "senderPacketOverhead",
        *overhead,
        asic->getCbfcSenderPacketOverheads());
    cbfcSenderPacketOverhead = *overhead;
    virtualChannels = std::move(configuredVcs);
  }

  bool virtualChannelsUnchanged = [&]() {
    auto origVcs = orig->getVirtualChannels();
    if (!origVcs) {
      return !virtualChannels.has_value();
    }
    return virtualChannels.has_value() &&
        origVcs->toThrift() == *virtualChannels;
  }();

  portPgConfigUnchanged = isPgConfigUnchanged(portPgCfgs, orig);
  /*
   * The list of lookup classes would be different when first enabling the
   * feature and if we had to change the number of lookup classes (unlikely)
   * or disable the queue-per-host feature (for emergency).
   *
   * To avoid unncessary shuffling when only the order of lookup classes
   * changes, (e.g. due to config change), sort and compare. This requires a
   * deep copy and sorting, but in practice, the list of lookup classes would
   * be small (< 10).
   */
  auto origLookupClasses{orig->getLookupClassesToDistributeTrafficOn()};
  auto newLookupClasses{*portConf->lookupClasses()};
  sort(origLookupClasses.begin(), origLookupClasses.end());
  sort(newLookupClasses.begin(), newLookupClasses.end());
  auto lookupClassesUnchanged = (origLookupClasses == newLookupClasses);

  // Now use TransceiverMap as the source of truth to build matcher
  // Prepare the new profileConfig
  std::optional<cfg::PlatformPortConfigOverrideFactor> factor;
  if (transceiver != nullptr) {
    factor = transceiver->toPlatformPortConfigOverrideFactor();
  }
  platformMapping_->customizePlatformPortConfigOverrideFactor(factor);
  PlatformPortProfileConfigMatcher matcher{
      *portConf->profileID(), orig->getID(), factor};

  auto portProfileCfg = platformMapping_->getPortProfileConfig(matcher);
  if (!portProfileCfg) {
    throw FbossError(
        "No port profile config found with matcher:", matcher.toString());
  }
  if (*portConf->state() == cfg::PortState::ENABLED &&
      *portProfileCfg->speed() != *portConf->speed() &&
      *portProfileCfg->speed() != cfg::PortSpeed::DEFAULT) {
    throw FbossError(
        orig->getName(),
        " has mismatched speed on profile:",
        apache::thrift::util::enumNameSafe(*portConf->profileID()),
        " and config:",
        apache::thrift::util::enumNameSafe(*portConf->speed()));
  }
  auto newProfileConfigRef = portProfileCfg->iphy();
  auto profileConfigUnchanged =
      (*newProfileConfigRef == orig->getProfileConfig());

  const auto& oldPfcPriorities = orig->getPfcPriorities();
  auto pfcPrioritiesUnchanged = !newPfcPriorities && oldPfcPriorities.empty();
  if (newPfcPriorities && !oldPfcPriorities.empty() &&
      (*newPfcPriorities).size() == oldPfcPriorities.size()) {
    pfcPrioritiesUnchanged = true;
    for (int i = 0; i < (*newPfcPriorities).size(); ++i) {
      if (static_cast<PfcPriority>((*newPfcPriorities).at(i)) !=
          oldPfcPriorities.at(i)) {
        pfcPrioritiesUnchanged = false;
        break;
      }
    }
  }

  const auto& newPinConfigs = platformMapping_->getPortIphyPinConfigs(matcher);
  auto pinConfigsUnchanged = (newPinConfigs == orig->getPinConfigs());

  const auto& newSerdesCustomCollection =
      platformMapping_->getPortSerdesCustomCollection(matcher);
  auto serdesCustomCollectionUnchanged =
      (newSerdesCustomCollection == orig->getSerdesCustomCollection());

  XLOG_IF(
      DBG2,
      !profileConfigUnchanged || !pinConfigsUnchanged ||
          !serdesCustomCollectionUnchanged)
      << orig->getName() << " has profileConfig: "
      << (profileConfigUnchanged ? "UNCHANGED" : "CHANGED")
      << ", pinConfigs: " << (pinConfigsUnchanged ? "UNCHANGED" : "CHANGED")
      << ", serdesCustomCollection: "
      << (serdesCustomCollectionUnchanged ? "UNCHANGED" : "CHANGED")
      << ", with matcher:" << matcher.toString();

  // Port drain is applicable to fabric ports and interface ports.
  if (*portConf->drainState() == cfg::PortDrainState::DRAINED &&
      *portConf->portType() != cfg::PortType::FABRIC_PORT &&
      *portConf->portType() != cfg::PortType::INTERFACE_PORT) {
    throw FbossError(
        "Port ",
        orig->getID(),
        " cannot be drained as it's neither a DSF fabric port nor an interface port");
  }

  bool portFlowletConfigUnchanged = true;
  auto newFlowletConfigName = std::optional<cfg::PortFlowletConfigName>();
  std::shared_ptr<PortFlowletCfg> portFlowletCfg;
  if (portConf->flowletConfigName().has_value()) {
    newFlowletConfigName = portConf->flowletConfigName().value();
    if (auto portFlowletConfigs = cfg_->portFlowletConfigs()) {
      auto it = portFlowletConfigs->find(newFlowletConfigName.value());
      if (it == portFlowletConfigs->end()) {
        throw FbossError(
            "Port flowlet config name: ",
            *newFlowletConfigName,
            " does not exist in PortFlowletConfig map");
      }
    }
    auto portFlowletCfgMap = new_->getPortFlowletCfgs();
    portFlowletCfg = portFlowletCfgMap->getNodeIf(*newFlowletConfigName);
    if (!portFlowletCfg) {
      throw FbossError(
          "Port:",
          orig->getID(),
          " but flowlet config name: ",
          *newFlowletConfigName,
          " doesn't exist in the port flowlet config map.");
    }
    portFlowletConfigUnchanged =
        isPortFlowletConfigUnchanged(portFlowletCfg, orig);
  }

  bool llrConfigUnchanged = true;
  auto newLlrConfigName = std::optional<cfg::LlrConfigName>();
  std::shared_ptr<LlrConfig> llrConfig;
  if (portConf->llrConfigName().has_value()) {
    newLlrConfigName = portConf->llrConfigName().value();
    // Loud rejection: LLR is only meaningful on ASICs that support it (UE Spec
    // section 5.1; Tomahawk Ultra only today). Reject at config time rather
    // than silently ignoring so state and hardware never diverge.
    const auto portSwitchIds =
        scopeResolver_.scope(PortID(*portConf->logicalID())).switchIds();
    for (auto switchId : portSwitchIds) {
      if (!hwAsicTable_->isFeatureSupported(
              switchId, HwAsic::Feature::LINK_LAYER_RETRANSMISSION)) {
        throw FbossError(
            "Port ",
            orig->getID(),
            " has LLR config name: ",
            *newLlrConfigName,
            " but its ASIC does not support LINK_LAYER_RETRANSMISSION");
      }
    }
    if (auto llrConfigs = cfg_->llrConfigs()) {
      auto it = llrConfigs->find(newLlrConfigName.value());
      if (it == llrConfigs->end()) {
        throw FbossError(
            "Port LLR config name: ",
            *newLlrConfigName,
            " does not exist in LlrConfig map");
      }
    }
    auto llrConfigMap = new_->getLlrConfigs();
    llrConfig = llrConfigMap->getNodeIf(*newLlrConfigName);
    if (!llrConfig) {
      throw FbossError(
          "Port:",
          orig->getID(),
          " but LLR config name: ",
          *newLlrConfigName,
          " doesn't exist in the LLR config map.");
    }
    llrConfigUnchanged = isLlrConfigUnchanged(llrConfig, orig);
  }

  auto newFabricLinkMonSwitchId = getFabricLinkMonitoringPortSwitchId(
      PortID(*portConf->logicalID()),
      *portConf->portType(),
      portConf->expectedNeighborReachability()->size());
  auto newUserMetaData = portConf->userMetaData().to_optional();

  // A port's router interfaces are derived from the interface config rather
  // than from portConf, so they have to be compared separately: a port rif
  // being replaced by an aggregate port rif over the same port changes this
  // without changing anything else about the port. Only NPU switches bind
  // ports to interfaces this way; on VOQ switches a port's interface is its
  // own system port, which cannot be rebound.
  const auto switchId =
      scopeResolver_.scope(PortID(*portConf->logicalID())).switchId();
  const auto switchType = *cfg_->switchSettings()
                               ->switchIdToSwitchInfo()
                               ->at(static_cast<int64_t>(switchId))
                               .switchType();
  auto interfaceIDsItr = port2InterfaceId_.find(orig->getID());
  auto interfaceIDsUnchanged = switchType != cfg::SwitchType::NPU ||
      (interfaceIDsItr == port2InterfaceId_.end()
           ? orig->getInterfaceIDs().empty()
           : interfaceIDsItr->second == orig->getInterfaceIDs());

  // Ensure portConf has actually changed, before applying
  if (*portConf->state() == orig->getAdminState() &&
      VlanID(*portConf->ingressVlan()) == orig->getIngressVlan() &&
      *portConf->speed() == orig->getSpeed() &&
      *portConf->profileID() == orig->getProfileID() &&
      *portConf->pause() == orig->getPause() && newPfc == orig->getPfc() &&
      pfcPrioritiesUnchanged &&
      *portConf->sFlowIngressRate() == orig->getSflowIngressRate() &&
      *portConf->sFlowEgressRate() == orig->getSflowEgressRate() &&
      newSampleDest == orig->getSampleDestination() &&
      portConf->name().value_or({}) == orig->getName() &&
      portConf->description().value_or({}) == orig->getDescription() &&
      vlans == orig->getVlans() && queuesUnchanged && portPgConfigUnchanged &&
      *portConf->loopbackMode() == orig->getLoopbackMode() &&
      mirrorsUnChanged && newQosPolicy == orig->getQosPolicy() &&
      *portConf->expectedLLDPValues() == orig->getLLDPValidations() &&
      *portConf->expectedNeighborReachability() ==
          orig->getExpectedNeighborValues()->toThrift() &&
      *portConf->maxFrameSize() == orig->getMaxFrameSize() &&
      lookupClassesUnchanged && profileConfigUnchanged && pinConfigsUnchanged &&
      serdesCustomCollectionUnchanged &&
      *portConf->portType() == orig->getPortType() &&
      *portConf->drainState() == orig->getPortDrainState() &&
      portFlowletConfigUnchanged &&
      newFlowletConfigName == orig->getFlowletConfigName() &&
      llrConfigUnchanged && newLlrConfigName == orig->getLlrConfigName() &&
      *portConf->conditionalEntropyRehash() ==
          orig->getConditionalEntropyRehash() &&
      portConf->selfHealingECMPLagEnable().value_or(false) ==
          orig->getDesiredSelfHealingECMPLagEnable().value_or(false) &&
      portConf->fecErrorDetectEnable().value_or(false) ==
          orig->getFecErrorDetectEnable().value_or(false) &&
      portConf->interPacketGapBits().value_or(0) ==
          orig->getInterPacketGapBits().value_or(0) &&
      portConf->amIdles().value_or(false) ==
          orig->getAmIdles().value_or(false) &&
      portConf->amIdles().has_value() == orig->getAmIdles().has_value() &&
      portConf->clmEnable().value_or(false) ==
          orig->getClmEnable().value_or(false) &&
      portConf->clmEnable().has_value() == orig->getClmEnable().has_value() &&
      portConf->linkTraining().value_or(false) ==
          orig->getLinkTraining().value_or(false) &&
      portConf->linkTraining().has_value() ==
          orig->getLinkTraining().has_value() &&
      portConf->txPrecoding().value_or(false) ==
          orig->getTxPrecoding().value_or(false) &&
      portConf->txPrecoding().has_value() ==
          orig->getTxPrecoding().has_value() &&
      portConf->rxPrecoding().value_or(false) ==
          orig->getRxPrecoding().value_or(false) &&
      portConf->rxPrecoding().has_value() ==
          orig->getRxPrecoding().has_value() &&
      portConf->linkScanMode().to_optional() == orig->getLinkScanMode() &&
      portConf->csigTagAction().to_optional() == orig->getCsigTagAction() &&
      portConf->csigLinkLocator().to_optional() == orig->getCsigLinkLocator() &&
      portConf->ingressAclTableName().to_optional() ==
          orig->getIngressAclTableName() &&
      portConf->portDownHoldoffTimeMs().value_or(0) ==
          orig->getPortDownHoldoffTimeMs().value_or(0) &&
      portConf->portDownHoldoffTimeMs().has_value() ==
          orig->getPortDownHoldoffTimeMs().has_value() &&
      portConf->portUpHoldoffTimeMs().value_or(0) ==
          orig->getPortUpHoldoffTimeMs().value_or(0) &&
      portConf->portUpHoldoffTimeMs().has_value() ==
          orig->getPortUpHoldoffTimeMs().has_value() &&
      newUserMetaData == orig->getUserMetaData() &&
      newFabricLinkMonSwitchId == orig->getPortSwitchId() &&
      newCbfcConfigName == orig->getCbfcConfigName() &&
      cbfcSenderCreditLimit == orig->getCbfcSenderCreditLimit() &&
      cbfcSenderCreditSize == orig->getCbfcSenderCreditSize() &&
      cbfcSenderPacketOverhead == orig->getCbfcSenderPacketOverhead() &&
      virtualChannelsUnchanged && interfaceIDsUnchanged) {
    return nullptr;
  }

  auto newPort = orig->clone();

  auto lldpmap = newPort->getLLDPValidations();
  for (const auto& tag : *portConf->expectedLLDPValues()) {
    lldpmap[tag.first] = tag.second;
  }
  if (*portConf->portType() != cfg::PortType::HYPER_PORT) {
    newPort->setAdminState(*portConf->state());
  } else {
    // hyper port admin state should be controlled by LACP protocol.
    // So, hyper port is always disabled (default state) in prod config
    // hyper port could be force enabled through config only in testing
    if (*portConf->state() == cfg::PortState::ENABLED) {
      newPort->setAdminState(*portConf->state());
    }
  }
  newPort->setIngressVlan(VlanID(*portConf->ingressVlan()));
  newPort->setVlans(vlans);
  if (portConf->portType() == cfg::PortType::HYPER_PORT &&
      *portConf->speed() == cfg::PortSpeed::DEFAULT) {
    newPort->setSpeed(kHyperPortSpeed);
  } else {
    newPort->setSpeed(*portConf->speed());
  }
  newPort->setProfileId(*portConf->profileID());
  newPort->setPause(*portConf->pause());
  newPort->setSflowIngressRate(*portConf->sFlowIngressRate());
  newPort->setSflowEgressRate(*portConf->sFlowEgressRate());
  newPort->setSampleDestination(newSampleDest);
  newPort->setName(portConf->name().value_or({}));
  newPort->setDescription(portConf->description().value_or({}));
  newPort->setLoopbackMode(*portConf->loopbackMode());
  newPort->resetPortQueues(portQueues);
  newPort->setIngressMirror(newIngressMirror);
  newPort->setEgressMirror(newEgressMirror);
  newPort->setQosPolicy(newQosPolicy);
  newPort->setExpectedLLDPValues(lldpmap);
  newPort->setLookupClassesToDistributeTrafficOn(*portConf->lookupClasses());
  newPort->setMaxFrameSize(*portConf->maxFrameSize());
  newPort->setPfc(newPfc);
  newPort->setPfcPriorities(newPfcPriorities);
  newPort->resetPgConfigs(portPgCfgs);
  newPort->setVirtualChannels(virtualChannels);
  newPort->setCbfcConfigName(newCbfcConfigName);
  newPort->setCbfcSenderCreditLimit(cbfcSenderCreditLimit);
  newPort->setCbfcSenderCreditSize(cbfcSenderCreditSize);
  newPort->setCbfcSenderPacketOverhead(cbfcSenderPacketOverhead);
  newPort->setProfileConfig(*newProfileConfigRef);
  newPort->resetPinConfigs(newPinConfigs);
  newPort->setSerdesCustomCollection(newSerdesCustomCollection);
  newPort->setPortType(*portConf->portType());
  newPort->setInterfaceIDs(port2InterfaceId_[orig->getID()]);
  newPort->setExpectedNeighborReachability(
      *portConf->expectedNeighborReachability());
  newPort->setPortDrainState(*portConf->drainState());
  newPort->setFlowletConfigName(newFlowletConfigName);
  newPort->setPortFlowletConfig(portFlowletCfg);
  newPort->setLlrConfigName(newLlrConfigName);
  newPort->setLlrConfig(llrConfig);
  newPort->setScope(*portConf->scope());
  newPort->setConditionalEntropyRehash(*portConf->conditionalEntropyRehash());
  newPort->setPortSwitchId(newFabricLinkMonSwitchId);
  if (auto selfHealingECMPLagEnable = portConf->selfHealingECMPLagEnable()) {
    if (selfHealingECMPLagEnable.value() &&
        !cfg_->switchSettings()->selfHealingEcmpLagConfig().has_value()) {
      throw FbossError(
          "Switch selfHealingEcmpLagConfig needs to be enabled for port ",
          newPort->getName(),
          " to have selfHealingEcmpLag enable");
    }
    newPort->setDesiredSelfHealingECMPLagEnable(
        selfHealingECMPLagEnable.value());
  } else {
    newPort->setDesiredSelfHealingECMPLagEnable(std::nullopt);
  }
  if (portConf->fecErrorDetectEnable().has_value()) {
    newPort->setFecErrorDetectEnable(portConf->fecErrorDetectEnable().value());
  } else {
    newPort->setFecErrorDetectEnable(std::nullopt);
  }
  if (portConf->interPacketGapBits().has_value()) {
    newPort->setInterPacketGapBits(portConf->interPacketGapBits().value());
  } else {
    newPort->setInterPacketGapBits(std::nullopt);
  }
  if (portConf->amIdles().has_value()) {
    newPort->setAmIdles(portConf->amIdles().value());
  } else {
    newPort->setAmIdles(std::nullopt);
  }
  if (portConf->clmEnable().has_value()) {
    newPort->setClmEnable(portConf->clmEnable().value());
  } else {
    newPort->setClmEnable(std::nullopt);
  }
  if (portConf->linkTraining().has_value()) {
    newPort->setLinkTraining(portConf->linkTraining().value());
  } else {
    newPort->setLinkTraining(std::nullopt);
  }
  if (portConf->txPrecoding().has_value()) {
    newPort->setTxPrecoding(portConf->txPrecoding().value());
  } else {
    newPort->setTxPrecoding(std::nullopt);
  }
  if (portConf->rxPrecoding().has_value()) {
    newPort->setRxPrecoding(portConf->rxPrecoding().value());
  } else {
    newPort->setRxPrecoding(std::nullopt);
  }
  newPort->setLinkScanMode(portConf->linkScanMode().to_optional());
  newPort->setCsigTagAction(portConf->csigTagAction().to_optional());
  if (auto lm = portConf->csigLinkLocator().to_optional();
      lm.has_value() && (*lm < 0 || *lm > 0x7f)) {
    throw FbossError(
        "csigLinkLocator must be 0-127, got ", *lm, " on port ", orig->getID());
  }
  newPort->setCsigLinkLocator(portConf->csigLinkLocator().to_optional());
  newPort->setUserMetaData(newUserMetaData);
  newPort->setIngressAclTableName(
      portConf->ingressAclTableName().to_optional());
  if (portConf->portDownHoldoffTimeMs().has_value()) {
    auto v = portConf->portDownHoldoffTimeMs().value();
    if (v < 0) {
      throw FbossError(
          "portDownHoldoffTimeMs must be non-negative, got ",
          v,
          " on port ",
          orig->getID());
    }
    newPort->setPortDownHoldoffTimeMs(v);
  } else {
    newPort->setPortDownHoldoffTimeMs(std::nullopt);
  }
  if (portConf->portUpHoldoffTimeMs().has_value()) {
    auto v = portConf->portUpHoldoffTimeMs().value();
    if (v < 0) {
      throw FbossError(
          "portUpHoldoffTimeMs must be non-negative, got ",
          v,
          " on port ",
          orig->getID());
    }
    newPort->setPortUpHoldoffTimeMs(v);
  } else {
    newPort->setPortUpHoldoffTimeMs(std::nullopt);
  }
  return newPort;
}

shared_ptr<AggregatePortMap> ThriftConfigApplier::updateAggregatePorts() {
  auto origAggPorts = orig_->getAggregatePorts();
  AggregatePortMap::NodeContainer newAggPorts;
  bool changed = false;

  size_t numExistingProcessed = 0;
  for (const auto& portCfg : *cfg_->aggregatePorts()) {
    AggregatePortID id(*portCfg.key());
    auto origAggPort = origAggPorts->getNodeIf(id);

    shared_ptr<AggregatePort> newAggPort;
    if (origAggPort) {
      newAggPort = updateAggPort(origAggPort, portCfg);
      ++numExistingProcessed;
    } else {
      newAggPort = createAggPort(portCfg);
    }

    changed |= updateMap(&newAggPorts, origAggPort, newAggPort);
  }

  if (numExistingProcessed != origAggPorts->numNodes()) {
    // Some existing aggregate ports were removed.
    CHECK_LE(numExistingProcessed, origAggPorts->numNodes());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }

  return std::make_shared<AggregatePortMap>(newAggPorts);
}

shared_ptr<AggregatePort> ThriftConfigApplier::updateAggPort(
    const shared_ptr<AggregatePort>& origAggPort,
    const cfg::AggregatePort& cfg) {
  CHECK_EQ(origAggPort->getID(), AggregatePortID(*cfg.key()));

  auto cfgSubports = getSubportsSorted(cfg);
  auto origSubports = origAggPort->sortedSubports();

  auto cfgAggregatePortInterfaceIDs = getAggregatePortInterfaceIDs(cfgSubports);

  uint16_t cfgSystemPriority;
  folly::MacAddress cfgSystemID;
  std::tie(cfgSystemID, cfgSystemPriority) = getSystemLacpConfig();

  auto cfgMinLinkCount = computeMinimumLinkCount(
      *cfg.minimumCapacity(), (*cfg.memberPorts()).size());
  std::optional<uint8_t> cfgMinLinkCountToUp = std::nullopt;
  if (cfg.minimumCapacityToUp()) {
    cfgMinLinkCountToUp = computeMinimumLinkCount(
        *cfg.minimumCapacityToUp(), (*cfg.memberPorts()).size());
    CHECK_GE(cfgMinLinkCountToUp.value(), cfgMinLinkCount);
  }

  if (origAggPort->getName() == *cfg.name() &&
      origAggPort->getDescription() == *cfg.description() &&
      origAggPort->getSystemPriority() == cfgSystemPriority &&
      origAggPort->getSystemID() == cfgSystemID &&
      origAggPort->getMinimumLinkCount() == cfgMinLinkCount &&
      origAggPort->getMinimumLinkCountToUp() == cfgMinLinkCountToUp &&
      origAggPort->getAggregatePortType() == *cfg.aggregatePortType() &&
      std::equal(
          origSubports.begin(), origSubports.end(), cfgSubports.begin()) &&
      std::equal(
          origAggPort->getInterfaceIDs()->begin(),
          origAggPort->getInterfaceIDs()->end(),
          cfgAggregatePortInterfaceIDs.begin())) {
    return nullptr;
  }

  auto newAggPort = origAggPort->clone();
  newAggPort->setName(*cfg.name());
  newAggPort->setDescription(*cfg.description());
  newAggPort->setSystemPriority(cfgSystemPriority);
  newAggPort->setSystemID(cfgSystemID);
  newAggPort->setMinimumLinkCount(cfgMinLinkCount);
  newAggPort->setSubports(folly::range(cfgSubports.begin(), cfgSubports.end()));
  newAggPort->setInterfaceIDs(cfgAggregatePortInterfaceIDs);
  newAggPort->setMinimumLinkCounToUp(cfgMinLinkCountToUp);
  newAggPort->setAggregatePortType(*cfg.aggregatePortType());

  return newAggPort;
}

shared_ptr<AggregatePort> ThriftConfigApplier::createAggPort(
    const cfg::AggregatePort& cfg) {
  auto subports = getSubportsSorted(cfg);
  auto aggregatePortInterfaceIDs = getAggregatePortInterfaceIDs(subports);

  uint16_t cfgSystemPriority;
  folly::MacAddress cfgSystemID;
  std::tie(cfgSystemID, cfgSystemPriority) = getSystemLacpConfig();

  auto cfgMinLinkCount = computeMinimumLinkCount(
      *cfg.minimumCapacity(), (*cfg.memberPorts()).size());

  std::optional<uint8_t> cfgMinLinkCountToUp = std::nullopt;
  if (cfg.minimumCapacityToUp()) {
    cfgMinLinkCountToUp = computeMinimumLinkCount(
        *cfg.minimumCapacityToUp(), (*cfg.memberPorts()).size());
    CHECK_GE(cfgMinLinkCountToUp.value(), cfgMinLinkCount);
  }

  return AggregatePort::fromSubportRange(
      AggregatePortID(*cfg.key()),
      *cfg.name(),
      *cfg.description(),
      cfgSystemPriority,
      cfgSystemID,
      cfgMinLinkCount,
      folly::range(subports.begin(), subports.end()),
      aggregatePortInterfaceIDs,
      cfgMinLinkCountToUp,
      *cfg.aggregatePortType());
}

std::vector<AggregatePort::Subport> ThriftConfigApplier::getSubportsSorted(
    const cfg::AggregatePort& cfg) {
  std::vector<AggregatePort::Subport> subports(
      std::distance(cfg.memberPorts()->begin(), cfg.memberPorts()->end()));

  for (int i = 0; i < subports.size(); ++i) {
    if (*cfg.memberPorts()[i].priority() < 0 ||
        *cfg.memberPorts()[i].priority() >= 1 << 16) {
      throw FbossError("Member port ", i, " has priority outside of [0, 2^16)");
    }

    auto id = PortID(*cfg.memberPorts()[i].memberPortID());
    auto priority = static_cast<uint16_t>(*cfg.memberPorts()[i].priority());
    auto rate = *cfg.memberPorts()[i].rate();
    auto activity = *cfg.memberPorts()[i].activity();
    auto multiplier = *cfg.memberPorts()[i].holdTimerMultiplier();
    subports[i] =
        AggregatePort::Subport(id, priority, rate, activity, multiplier);
  }

  std::sort(subports.begin(), subports.end());

  if (!subports.empty()) {
    auto switchIds = scopeResolver_.scope(cfg).switchIds();
    if (switchIds.size() > 1) {
      throw FbossError("Multi Switch LAG not supported ", *cfg.key());
    }
    auto asic = hwAsicTable_->getHwAsicIf(*switchIds.begin());
    if (subports.size() > asic->getMaxLagMemberSize()) {
      throw FbossError(
          "Trying to set ",
          (*cfg.memberPorts()).size(),
          " lag members, ",
          "which is greater than the hardware limit ",
          asic->getMaxLagMemberSize());
    }
  }
  return subports;
}

std::vector<int32_t> ThriftConfigApplier::getAggregatePortInterfaceIDs(
    const std::vector<AggregatePort::Subport>& subports) {
  if (subports.size() > 0) {
    // all Aggregate member ports always belong to the same interface(s). Thus,
    // pick the interface for any member port
    auto subport = subports.front();
    return port2InterfaceId_[subport.portID];
  }

  return {};
}

std::pair<folly::MacAddress, uint16_t>
ThriftConfigApplier::getSystemLacpConfig() {
  folly::MacAddress systemID;
  uint16_t systemPriority;

  if (auto lacp = cfg_->lacp()) {
    systemID = MacAddress(*lacp->systemID());
    systemPriority = *lacp->systemPriority();
  } else {
    // If the system LACP configuration parameters were not specified,
    // we fall back to default parameters. Since the default system ID
    // is not a compile-time constant (it is derived from the CPU mac),
    // the default value is defined here, instead of, say,
    // AggregatePortFields::kDefaultSystemID.
    systemID = getLocalMacAddress();
    systemPriority = kDefaultSystemPriority;
  }

  return std::make_pair(systemID, systemPriority);
}

uint8_t ThriftConfigApplier::computeMinimumLinkCount(
    const cfg::MinimumCapacity& minCapacity,
    size_t memberPortsSize) {
  uint8_t minLinkCount = 1;
  switch (minCapacity.getType()) {
    case cfg::MinimumCapacity::Type::linkCount:
      // Thrift's byte type is an int8_t
      CHECK_GE(minCapacity.linkCount().value(), 1);

      minLinkCount = minCapacity.linkCount().value();
      break;
    case cfg::MinimumCapacity::Type::linkPercentage:
      CHECK_GT(minCapacity.linkPercentage().value(), 0);
      CHECK_LE(minCapacity.linkPercentage().value(), 1);

      minLinkCount =
          std::ceil(minCapacity.linkPercentage().value() * memberPortsSize);
      if (memberPortsSize != 0) {
        CHECK_GE(minLinkCount, 1);
      }

      break;
    case cfg::MinimumCapacity::Type::__EMPTY__:
    // needed to handle error from -Werror=switch
    default:
      folly::assume_unreachable();
  }

  return minLinkCount;
}

// If create only attributes or speed changes for a port, we
// delete the port and recreate it. In this sequence, before
// a port is deleted, we need to remove VLAN membership and
// before that, need to clear any MACs learnt on that port/
// VLAN. This API identifies VLAN id / ports where all MACs
// learnt need to be cleared.
std::map<uint32_t, std::set<PortDescriptor>>
ThriftConfigApplier::getMapOfVlanToPortsNeedingMacClear(
    const std::shared_ptr<MultiSwitchPortMap> newPorts,
    const std::shared_ptr<MultiSwitchPortMap> origPorts) {
  std::map<uint32_t, std::set<PortDescriptor>> vlanToPortMap;

  for (auto& portMap : std::as_const(*origPorts)) {
    for (auto& [oldPortId, oldPort] : std::as_const(*portMap.second)) {
      auto newPort = std::as_const(*newPorts).getNodeIf(oldPortId);
      if (!newPort || oldPort->getSpeed() != newPort->getSpeed()) {
        // If oldPort does not exist anymore or if the speed has changed,
        // we need to clear mac addresses learnt on the port for all vlans
        // before port removal is attempted.
        for (auto& [vlanId, _] : oldPort->getVlans()) {
          auto vlanIter = vlanToPortMap.find(vlanId);
          if (vlanIter == vlanToPortMap.end()) {
            vlanToPortMap[vlanId] =
                std::set<PortDescriptor>{PortDescriptor(oldPort->getID())};
          } else {
            vlanIter->second.insert(PortDescriptor(oldPort->getID()));
          }
        }
      }
    }
  }
  return vlanToPortMap;
}

shared_ptr<VlanMap> ThriftConfigApplier::updateVlans() {
  const auto& switchSettings =
      utility::getFirstNodeIf(new_->getSwitchSettings());
  // TODO(skhare)
  // VOQ/Fabric switches require that the packets are not tagged with any
  // VLAN. We are gradually enhancing wedge_agent to handle tagged as well as
  // untagged packets. During this transition, we will use PseudoVlan (VlanID
  // 0) to populate SwitchState/Neighbor cache etc. data structures.
  // Thus, for VOQ/Fabric switches, cfg_ will not carry vlan, but after
  // updatePseudoVlan() runs, origVlans will carry pseudoVlan which will be
  // removed by updateVlans() processig.
  // Avoid it by skipping updateVlans() for VOQ/Fabric switches.
  // Once wedge_agent changes are complete, we can remove this check as
  // cfg_->vlans and origVlans will always be empty for VOQ/Fabric switches and
  // then this function will be a no-op
  if (!switchSettings->vlansSupported()) {
    return nullptr;
  }

  auto origVlans = orig_->getVlans();
  VlanMap::NodeContainer newVlans;
  bool changed = false;

  std::map<uint32_t, std::set<PortDescriptor>> vlanToPortMap{};
  if (new_ && orig_) {
    vlanToPortMap =
        getMapOfVlanToPortsNeedingMacClear(new_->getPorts(), orig_->getPorts());
  }
  // Process all supplied VLAN configs
  size_t numExistingProcessed = 0;
  for (const auto& vlanCfg : *cfg_->vlans()) {
    VlanID id(*vlanCfg.id());
    auto origVlan = origVlans->getNodeIf(id);
    shared_ptr<Vlan> newVlan;
    if (origVlan) {
      std::set<PortDescriptor> portDescs;
      auto vlanIter = vlanToPortMap.find(id);
      if (vlanIter != vlanToPortMap.end()) {
        portDescs = vlanIter->second;
      }
      newVlan = updateVlan(origVlan, &vlanCfg, portDescs);
      ++numExistingProcessed;
    } else {
      newVlan = createVlan(&vlanCfg);
    }
    changed |= updateMap(&newVlans, origVlan, newVlan);
  }

  if (numExistingProcessed != origVlans->numNodes()) {
    // Some existing VLANs were removed.
    CHECK_LT(numExistingProcessed, origVlans->numNodes());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }

  auto vlans = std::make_shared<VlanMap>(std::move(newVlans));
  return vlans;
}

shared_ptr<Vlan> ThriftConfigApplier::createVlan(const cfg::Vlan* config) {
  const auto& portsInfo = vlanPorts_[VlanID(*config->id())];
  auto vlan = make_shared<Vlan>(config, portsInfo);
  updateDhcpOverrides(vlan.get(), config);

  /* TODO t7153326: Following code is added for backward compatibility
  Remove it once coop generates config with */
  if (auto intfID = config->intfID()) {
    vlan->setInterfaceID(InterfaceID(*intfID));
  } else {
    auto& entry = vlanInterfaces_[VlanID(*config->id())];
    if (!entry.interfaces.empty()) {
      vlan->setInterfaceID(*(entry.interfaces.begin()));
    }
  }

  auto dhcpV4Relay = config->dhcpRelayAddressV4()
      ? IPAddressV4(*config->dhcpRelayAddressV4())
      : IPAddressV4();
  auto dhcpV6Relay = config->dhcpRelayAddressV6()
      ? IPAddressV6(*config->dhcpRelayAddressV6())
      : IPAddressV6("::");
  vlan->setDhcpV4Relay(dhcpV4Relay);
  vlan->setDhcpV6Relay(dhcpV6Relay);

  return vlan;
}

bool ThriftConfigApplier::updateMacTable(
    std::shared_ptr<Vlan>& newVlan,
    const std::set<PortDescriptor>& portDescs) {
  auto newMacTable{newVlan->getMacTable()->clone()};
  int macRemovedCount{0};
  for (auto& [_, macEntry] : std::as_const(*(newVlan->getMacTable()))) {
    if (portDescs.contains(macEntry->getPort())) {
      newMacTable->removeEntry(macEntry->getMac());
      macRemovedCount++;
    }
  }
  if (macRemovedCount) {
    newVlan->setMacTable(newMacTable);
    XLOG(DBG2) << "Removed " << macRemovedCount << " MAC addresses on VLAN "
               << newVlan->getID();
  }
  return macRemovedCount > 0;
}

shared_ptr<Vlan> ThriftConfigApplier::updateVlan(
    const shared_ptr<Vlan>& orig,
    const cfg::Vlan* config,
    const std::set<PortDescriptor>& portSet) {
  CHECK_EQ(orig->getID(), VlanID(*config->id()));
  const auto& portsInfo = vlanPorts_[orig->getID()];

  auto newVlan = orig->clone();
  bool changed_dhcp_overrides = updateDhcpOverrides(newVlan.get(), config);
  auto oldDhcpV4Relay = orig->getDhcpV4Relay();
  auto newDhcpV4Relay = config->dhcpRelayAddressV4()
      ? IPAddressV4(*config->dhcpRelayAddressV4())
      : IPAddressV4();

  auto oldDhcpV6Relay = orig->getDhcpV6Relay();
  auto newDhcpV6Relay = config->dhcpRelayAddressV6()
      ? IPAddressV6(*config->dhcpRelayAddressV6())
      : IPAddressV6("::");
  /* TODO t7153326: Following code is added for backward compatibility
  Remove it once coop generates config with intfID */
  auto oldIntfID = orig->getInterfaceID();
  auto newIntfID = InterfaceID(0);
  if (auto intfID = config->intfID()) {
    newIntfID = InterfaceID(*intfID);
  } else {
    auto& entry = vlanInterfaces_[VlanID(*config->id())];
    if (!entry.interfaces.empty()) {
      newIntfID = *(entry.interfaces.begin());
    }
  }

  bool macChanged = updateMacTable(newVlan, portSet);
  if (orig->getName() == *config->name() && oldIntfID == newIntfID &&
      orig->getPortsInfo() == portsInfo && oldDhcpV4Relay == newDhcpV4Relay &&
      oldDhcpV6Relay == newDhcpV6Relay && !changed_dhcp_overrides &&
      !macChanged) {
    return nullptr;
  }

  newVlan->setName(*config->name());
  newVlan->setInterfaceID(newIntfID);
  newVlan->setPortsInfo(portsInfo);
  newVlan->setDhcpV4Relay(newDhcpV4Relay);
  newVlan->setDhcpV6Relay(newDhcpV6Relay);

  return newVlan;
}

std::shared_ptr<QosPolicyMap> ThriftConfigApplier::updateQosPolicies() {
  QosPolicyMap::NodeContainer newQosPolicies;
  bool changed = false;
  int numExistingProcessed = 0;
  auto defaultDataPlaneQosPolicyName = getDefaultDataPlaneQosPolicyName();

  auto qosPolicies = *cfg_->qosPolicies();
  for (auto& qosPolicy : qosPolicies) {
    if (defaultDataPlaneQosPolicyName.has_value() &&
        defaultDataPlaneQosPolicyName.value() == *qosPolicy.name()) {
      // skip default QosPolicy as it will be maintained in switch state
      continue;
    }
    auto newQosPolicy =
        updateQosPolicy(qosPolicy, &numExistingProcessed, &changed);
    if (!newQosPolicies.emplace(*qosPolicy.name(), newQosPolicy).second) {
      throw FbossError(
          "Invalid config: Qos Policy \"",
          *qosPolicy.name(),
          "\" already exists");
    }
  }
  if (numExistingProcessed != orig_->getQosPolicies()->numNodes()) {
    // Some existing Qos Policies were removed.
    changed = true;
  }
  if (!changed) {
    return nullptr;
  }

  return std::make_shared<QosPolicyMap>(std::move(newQosPolicies));
}

std::shared_ptr<QosPolicy> ThriftConfigApplier::updateQosPolicy(
    cfg::QosPolicy& qosPolicy,
    int* numExistingProcessed,
    bool* changed) {
  auto origQosPolicy = orig_->getQosPolicies()->getNodeIf(*qosPolicy.name());
  auto newQosPolicy = createQosPolicy(qosPolicy);
  if (origQosPolicy) {
    ++(*numExistingProcessed);
    if (*origQosPolicy == *newQosPolicy) {
      return origQosPolicy;
    }
  }
  *changed = true;
  return newQosPolicy;
}

std::optional<std::string>
ThriftConfigApplier::getDefaultDataPlaneQosPolicyName() const {
  if (auto dataPlaneTrafficPolicy = cfg_->dataPlaneTrafficPolicy()) {
    if (auto defaultDataPlaneQosPolicy =
            dataPlaneTrafficPolicy->defaultQosPolicy()) {
      return *defaultDataPlaneQosPolicy;
    }
  }
  return std::nullopt;
}

std::shared_ptr<QosPolicy>
ThriftConfigApplier::updateDataplaneDefaultQosPolicy() {
  auto defaultDataPlaneQosPolicyName = getDefaultDataPlaneQosPolicyName();
  if (!defaultDataPlaneQosPolicyName) {
    return nullptr;
  }
  std::shared_ptr<QosPolicy> newQosPolicy = nullptr;
  auto qosPolicies = *cfg_->qosPolicies();
  for (auto& qosPolicy : qosPolicies) {
    if (defaultDataPlaneQosPolicyName == *qosPolicy.name()) {
      newQosPolicy = createQosPolicy(qosPolicy);
      if (newQosPolicy->getExpMap()->get<switch_state_tags::from>()->empty() &&
          newQosPolicy->getExpMap()->get<switch_state_tags::to>()->empty()) {
        // if exp map is not provided, set some default mapping
        // TODO(pshaikh): remove this once default config for switches will
        // always have EXP maps
        auto expMap = ExpMap();
        for (auto i = 0; i < 8; i++) {
          expMap.addToEntry(static_cast<TrafficClass>(i), static_cast<EXP>(i));
          expMap.addFromEntry(
              static_cast<TrafficClass>(i), static_cast<EXP>(i));
        }
        newQosPolicy->setExpMap(ExpMap(expMap));
      }
      break;
    }
  }
  auto oldQosPolicy = orig_->getDefaultDataPlaneQosPolicy();
  if (oldQosPolicy && newQosPolicy && *oldQosPolicy == *newQosPolicy) {
    return oldQosPolicy;
  }
  return newQosPolicy;
}

shared_ptr<QosPolicy> ThriftConfigApplier::createQosPolicy(
    const cfg::QosPolicy& qosPolicy) {
  if (qosPolicy.rules()->empty() == !qosPolicy.qosMap().has_value()) {
    XLOG(WARN) << "both qos rules and qos map are provided in qos policy "
               << *qosPolicy.name()
               << "! dscp map if present in qos map, will override qos rules";
  }

  DscpMap ingressDscpMap;
  for (const auto& qosRule : *qosPolicy.rules()) {
    if (qosRule.dscp()->empty()) {
      throw FbossError("Invalid config: qosPolicy: empty dscp list");
    }
    for (const auto& dscpValue : *qosRule.dscp()) {
      if (dscpValue < 0 || dscpValue > 63) {
        throw FbossError("dscp value is invalid (must be [0, 63])");
      }
      ingressDscpMap.addFromEntry(
          static_cast<TrafficClass>(*qosRule.queueId()),
          static_cast<DSCP>(dscpValue));
    }
  }

  if (auto qosMap = qosPolicy.qosMap()) {
    DscpMap dscpMap(*qosMap->dscpMaps());
    ExpMap expMap(*qosMap->expMaps());

    if (const auto& pfcPriorityMap = qosMap->pfcPriorityToQueueId()) {
      for (const auto& pfcPriorityEntry : *pfcPriorityMap) {
        if (pfcPriorityEntry.first >
            cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX()) {
          throw FbossError(
              "Invalid pfc priority value. Valid range is 0 to: ",
              cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX());
        }
      }
    }

    std::optional<PcpMap> pcpMap;
    if (qosMap->pcpMaps()) {
      pcpMap = PcpMap(*qosMap->pcpMaps());
    }

    auto qosPolicyNew = make_shared<QosPolicy>(
        *qosPolicy.name(),
        dscpMap.empty() ? ingressDscpMap : dscpMap,
        expMap,
        *qosMap->trafficClassToQueueId(),
        pcpMap);

    if (qosMap->pfcPriorityToQueueId().has_value()) {
      qosPolicyNew->setPfcPriorityToQueueIdMap(*qosMap->pfcPriorityToQueueId());
    }

    if (const auto& tc2PgIdMap = qosMap->trafficClassToPgId()) {
      for (auto tc2PgIdEntry : *tc2PgIdMap) {
        if (tc2PgIdEntry.second >
            cfg::switch_config_constants::PORT_PG_VALUE_MAX()) {
          throw FbossError(
              "Invalid pg id. Valid range is 0 to: ",
              cfg::switch_config_constants::PORT_PG_VALUE_MAX());
        }
      }
      qosPolicyNew->setTrafficClassToPgIdMap(*tc2PgIdMap);
    }

    if (const auto& tc2VcIdMap = qosMap->trafficClassToVcId()) {
      for (const auto& tc2VcIdEntry : *tc2VcIdMap) {
        // trafficClassToVcId is map<i16, i16>, so a negative vc id is
        // representable and would otherwise reach the SAI layer.
        if (tc2VcIdEntry.second < 0 ||
            tc2VcIdEntry.second >
                cfg::switch_config_constants::PORT_VC_VALUE_MAX()) {
          throw FbossError(
              "Invalid vc id. Valid range is 0 to: ",
              cfg::switch_config_constants::PORT_VC_VALUE_MAX());
        }
      }
      qosPolicyNew->setTrafficClassToVcIdMap(*tc2VcIdMap);
    }

    if (const auto& queue2VcIdMap = qosMap->queueToVcId()) {
      for (const auto& queue2VcIdEntry : *queue2VcIdMap) {
        // queueToVcId is map<i16, i16>, so a negative vc id is representable
        // and would otherwise reach the SAI layer.
        if (queue2VcIdEntry.second < 0 ||
            queue2VcIdEntry.second >
                cfg::switch_config_constants::PORT_VC_VALUE_MAX()) {
          throw FbossError(
              "Invalid vc id. Valid range is 0 to: ",
              cfg::switch_config_constants::PORT_VC_VALUE_MAX());
        }
      }
      qosPolicyNew->setQueueToVcIdMap(*queue2VcIdMap);
    }

    if (const auto& pfcPriority2PgIdMap = qosMap->pfcPriorityToPgId()) {
      for (const auto& entry : *pfcPriority2PgIdMap) {
        if (entry.first >
            cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX()) {
          throw FbossError(
              "Invalid pfc priority value. Valid range is 0 to: ",
              cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX());
        }
        if (entry.second > cfg::switch_config_constants::PORT_PG_VALUE_MAX()) {
          throw FbossError(
              "Invalid pg id. Valid range is 0 to: ",
              cfg::switch_config_constants::PORT_PG_VALUE_MAX());
        }
      }
      qosPolicyNew->setPfcPriorityToPgIdMap(*pfcPriority2PgIdMap);
    }

    if (const auto& trafficClassToVoqIdMap = qosMap->trafficClassToVoqId()) {
      qosPolicyNew->setTrafficClassToVoqIdMap(*trafficClassToVoqIdMap);
    }

    return qosPolicyNew;
  }
  return make_shared<QosPolicy>(*qosPolicy.name(), ingressDscpMap);
}

ThriftConfigApplier::AclTableGroupMaps
ThriftConfigApplier::updateAclTableGroups() {
  auto origAclTableGroups = orig_->getAclTableGroups();
  auto origPortAclTableGroups = orig_->getPortAclTableGroups();
  AclTableGroupMap::NodeContainer newAclTableGroups;
  AclTableGroupMap::NodeContainer newPortAclTableGroups;

  flat_map<std::string, const cfg::AclEntry*> aclByName{};
  flat_set<std::string> aclTableNames;
  bool switchBoundChanged = false;
  bool portBoundChanged = false;

  auto updateAclTableGroupsInternal =
      [this,
       origAclTableGroups,
       origPortAclTableGroups,
       &newAclTableGroups,
       &newPortAclTableGroups,
       &aclByName,
       &aclTableNames,
       &switchBoundChanged,
       &portBoundChanged](const cfg::AclTableGroup& cfgAclTableGroup) {
        for (const auto& aclTable : *cfgAclTableGroup.aclTables()) {
          if (!aclTableNames.emplace(*aclTable.name()).second) {
            throw FbossError("Duplicate ACL table name ", *aclTable.name());
          }
        }
        aclByName.merge(getAllAclsByName(cfgAclTableGroup));
        const auto bindPoint = *cfgAclTableGroup.bindPoint();
        const auto portBound = bindPoint == cfg::AclTableGroupBindPoint::PORT;
        const auto& origGroups =
            portBound ? origPortAclTableGroups : origAclTableGroups;
        auto& newGroups = portBound ? newPortAclTableGroups : newAclTableGroups;
        auto& changed = portBound ? portBoundChanged : switchBoundChanged;
        auto origAclTableGroup =
            origGroups->getNodeIf(*cfgAclTableGroup.stage());
        auto newAclTableGroup = updateAclTableGroup(
            *cfgAclTableGroup.stage(), cfgAclTableGroup, origAclTableGroup);
        changed |= updateMap(&newGroups, origAclTableGroup, newAclTableGroup);
      };

  if (!cfg_->aclTableGroup() &&
      (!cfg_->aclTableGroups() || cfg_->aclTableGroups()->empty())) {
    throw FbossError(
        "ACL Table Group must be specified if Multiple ACL Table support is enabled");
  } else if (auto cfgAclTableGroup = cfg_->aclTableGroup()) {
    updateAclTableGroupsInternal(*cfgAclTableGroup);
  } else {
    for (const auto& entry : *cfg_->aclTableGroups()) {
      // acl entry names must be unique across all acl table groups.
      updateAclTableGroupsInternal(entry);
    }
  }

  // Check for controlPlane traffic acls
  if (cfg_->cpuTrafficPolicy() && cfg_->cpuTrafficPolicy()->trafficPolicy()) {
    checkTrafficPolicyAclsExistInConfig(
        *cfg_->cpuTrafficPolicy()->trafficPolicy(), aclByName);
  }
  // Check for dataPlane traffic acls
  if (auto dataPlaneTrafficPolicy = cfg_->dataPlaneTrafficPolicy()) {
    checkTrafficPolicyAclsExistInConfig(*dataPlaneTrafficPolicy, aclByName);
  }

  switchBoundChanged |=
      newAclTableGroups.size() != origAclTableGroups->numNodes();
  portBoundChanged |=
      newPortAclTableGroups.size() != origPortAclTableGroups->numNodes();
  return {
      switchBoundChanged
          ? std::make_shared<AclTableGroupMap>(std::move(newAclTableGroups))
          : nullptr,
      portBoundChanged
          ? std::make_shared<AclTableGroupMap>(std::move(newPortAclTableGroups))
          : nullptr};
}

void ThriftConfigApplier::validatePortIngressAcls() const {
  for (const auto& port : *cfg_->ports()) {
    const auto ingressAclTableName = port.ingressAclTableName().to_optional();
    if (!ingressAclTableName) {
      continue;
    }
    if (!FLAGS_enable_acl_table_group) {
      throw FbossError(
          "Port ",
          *port.logicalID(),
          " specifies ingress ACL table ",
          *ingressAclTableName,
          " while ACL table groups are disabled");
    }
    const auto scopedAclTableGroups =
        new_->getPortAclTableGroups()->getMapNodeIf(scopeResolver_.scope(port));
    const auto aclTableGroup = scopedAclTableGroups
        ? scopedAclTableGroups->getNodeIf(cfg::AclStage::INGRESS)
        : nullptr;
    if (!aclTableGroup) {
      throw FbossError(
          "Port ",
          *port.logicalID(),
          " references ingress ACL table ",
          *ingressAclTableName,
          " but no port-bound ingress ACL table group exists");
    }
    if (aclTableGroup->getBindPoint() != cfg::AclTableGroupBindPoint::PORT) {
      throw FbossError(
          "Ingress ACL table group ",
          aclTableGroup->getName(),
          " referenced by port ",
          *port.logicalID(),
          " must use PORT bind point");
    }
    const auto aclTableMap = aclTableGroup->getAclTableMap();
    if (!aclTableMap || !aclTableMap->getTableIf(*ingressAclTableName)) {
      throw FbossError(
          "Port ",
          *port.logicalID(),
          " references missing ingress ACL table ",
          *ingressAclTableName);
    }
  }
}

std::shared_ptr<AclTableGroup> ThriftConfigApplier::updateAclTableGroup(
    cfg::AclStage aclStage,
    const cfg::AclTableGroup& cfgAclTableGroup,
    const std::shared_ptr<AclTableGroup>& origAclTableGroup) {
  auto newAclTableMap = std::make_shared<AclTableMap>();
  bool changed = false;
  int numExistingTablesProcessed = 0;
  const auto newBindPoint = *cfgAclTableGroup.bindPoint();

  if (newBindPoint == cfg::AclTableGroupBindPoint::PORT &&
      aclStage != cfg::AclStage::INGRESS) {
    throw FbossError(
        "Port-bound ACL table group ",
        *cfgAclTableGroup.name(),
        " must use the ingress stage");
  }

  // For each table in the config, update the table entries and priority
  for (const auto& aclTable : *cfgAclTableGroup.aclTables()) {
    auto newTable =
        updateAclTable(aclStage, aclTable, &numExistingTablesProcessed);
    if (newTable) {
      changed = true;
      newAclTableMap->addTable(newTable);
    } else {
      newAclTableMap->addTable(
          origAclTableGroup->getAclTableMap()->getTableIf(*aclTable.name()));
    }
  }

  if (!origAclTableGroup ||
      (origAclTableGroup->getAclTableMap() &&
       (numExistingTablesProcessed !=
        origAclTableGroup->getAclTableMap()->numTables()))) {
    changed = true;
  }
  if (origAclTableGroup &&
      (origAclTableGroup->getName() != *cfgAclTableGroup.name() ||
       origAclTableGroup->getBindPoint() != newBindPoint)) {
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }

  auto newAclTableGroup =
      std::make_shared<AclTableGroup>(*cfgAclTableGroup.stage());
  newAclTableGroup->setAclTableMap(newAclTableMap);
  newAclTableGroup->setName(*cfgAclTableGroup.name());
  newAclTableGroup->setBindPoint(newBindPoint);

  return newAclTableGroup;
}

flat_map<std::string, const cfg::AclEntry*>
ThriftConfigApplier::getAllAclsByName(
    const cfg::AclTableGroup& cfgAclTableGroup) {
  flat_map<std::string, const cfg::AclEntry*> aclByName;
  for (const auto& aclTable : *cfgAclTableGroup.aclTables()) {
    auto aclEntries = *(aclTable.aclEntries());
    folly::gen::from(aclEntries) |
        folly::gen::map([](const cfg::AclEntry& acl) {
          return std::make_pair(*acl.name(), &acl);
        }) |
        folly::gen::appendTo(aclByName);
  }
  return aclByName;
}

void ThriftConfigApplier::checkTrafficPolicyAclsExistInConfig(
    const cfg::TrafficPolicyConfig& policy,
    flat_map<std::string, const cfg::AclEntry*> aclByName) {
  for (const auto& mta : *policy.matchToAction()) {
    auto a = aclByName.find(*mta.matcher());
    if (a == aclByName.end()) {
      throw FbossError(
          "Invalid config: No acl named ", *mta.matcher(), " found.");
    }
  }
}

std::shared_ptr<AclTable> ThriftConfigApplier::updateAclTable(
    cfg::AclStage aclStage,
    const cfg::AclTable& configTable,
    int* numExistingTablesProcessed) {
  auto tableName = *configTable.name();
  auto origTable = orig_->getAclTable(aclStage, tableName);

  // PbrAclManager computes every entry of the PBR table from the
  // class-based policies; none of them come from config. Rebuilding the
  // entries here would read the computed ones as "removed from config" and
  // wipe them, so only the table's attributes are taken from config and the
  // programmed entries are carried over untouched.
  const bool pbrTable =
      tableName == cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE();
  if (pbrTable && !configTable.aclEntries()->empty()) {
    throw FbossError(
        "ACL table ",
        tableName,
        " entries are computed from class-based policies and cannot be configured");
  }
  std::shared_ptr<AclMap> newTableEntries;
  if (!pbrTable) {
    newTableEntries = updateAclsForTable(
        aclStage, *configTable.aclEntries(), std::make_optional(tableName));
  }
  auto newTablePriority = *configTable.priority();
  std::vector<cfg::AclTableActionType> newActionTypes =
      *configTable.actionTypes();
  std::vector<cfg::AclTableQualifier> newQualifiers = *configTable.qualifiers();
  std::vector<std::string> newUdfGroups = *configTable.udfGroups();
  if (origTable) {
    ++(*numExistingTablesProcessed);
    if (!newTableEntries && newTablePriority == origTable->getPriority() &&
        newActionTypes == origTable->getActionTypes() &&
        newQualifiers == origTable->getQualifiers() &&
        newUdfGroups == origTable->getUdfGroups()->toThrift()) {
      // Original table exists with same attributes.
      return nullptr;
    }
  }

  state::AclTableFields aclTableFields{};
  aclTableFields.id() = tableName;
  aclTableFields.priority() = newTablePriority;
  auto newTable = std::make_shared<AclTable>(std::move(aclTableFields));
  if (newTableEntries) {
    // Entries changed from original table or original table does not exist
    newTable->setAclMap(newTableEntries);
  } else if (origTable) {
    // entries are unchanged from original table
    newTable->setAclMap(origTable->getAclMap()->clone());
  } else {
    // original table does not exist, and new table is empty
    newTable->setAclMap(std::make_shared<AclMap>());
  }

  newTable->setActionTypes(newActionTypes);
  newTable->setQualifiers(newQualifiers);
  newTable->setUdfGroups(newUdfGroups);

  return newTable;
}

std::shared_ptr<AclMap> ThriftConfigApplier::updateAcls(
    cfg::AclStage aclStage,
    std::vector<cfg::AclEntry> configEntries) {
  auto acls = updateAclsImpl(aclStage, configEntries);
  if (!acls) {
    return nullptr;
  }
  return acls;
}

std::shared_ptr<AclMap> ThriftConfigApplier::updateAclsForTable(
    cfg::AclStage aclStage,
    std::vector<cfg::AclEntry> configEntries,
    std::optional<std::string> tableName) {
  return updateAclsImpl(aclStage, configEntries, tableName);
}

std::shared_ptr<AclMap> ThriftConfigApplier::updateAclsImpl(
    cfg::AclStage aclStage,
    std::vector<cfg::AclEntry> configEntries,
    std::optional<std::string> tableName) {
  AclMap::NodeContainer newAcls;
  bool changed = false;
  int numExistingProcessed = 0;
  int dataPriority = AclTable::kDataplaneAclMaxPriority;
  int cpuPriority = 1;
  CHECK_LT(FLAGS_pbr_acl_priority, AclTable::kDataplaneAclMaxPriority)
      << "PBR must sit below the dataplane band so no config ACL can reach it";

  flat_map<std::string, const cfg::TrafficCounter*> counterByName;
  folly::gen::from(*cfg_->trafficCounters()) |
      folly::gen::map([](const cfg::TrafficCounter& counter) {
        return std::make_pair(*counter.name(), &counter);
      }) |
      folly::gen::appendTo(counterByName);

  // Let's get a map of traffic policies to name
  flat_map<std::string, const cfg::MatchToAction*> cpuPolicyByName;
  if (cfg_->cpuTrafficPolicy() && cfg_->cpuTrafficPolicy()->trafficPolicy()) {
    folly::gen::from(
        *cfg_->cpuTrafficPolicy()->trafficPolicy()->matchToAction()) |
        folly::gen::map([](const cfg::MatchToAction& mta) {
          return std::make_pair(*mta.matcher(), &mta);
        }) |
        folly::gen::appendTo(cpuPolicyByName);
  }

  flat_map<std::string, const cfg::MatchToAction*> dataPolicyByName;
  if (cfg_->dataPlaneTrafficPolicy()) {
    folly::gen::from(*cfg_->dataPlaneTrafficPolicy()->matchToAction()) |
        folly::gen::map([](const cfg::MatchToAction& mta) {
          return std::make_pair(*mta.matcher(), &mta);
        }) |
        folly::gen::appendTo(dataPolicyByName);
  }

  // Generates new acls from template
  auto addToAcls = [&]()
      -> const std::vector<std::pair<std::string, std::shared_ptr<AclEntry>>> {
    std::vector<std::pair<std::string, std::shared_ptr<AclEntry>>> entries;
    for (const auto& aclCfg : configEntries) {
      bool enableAcl = true;

      // The ACLs have to be processed in the order in which they are listed in
      // the config. The traffic policy for each ACL may either be in the data
      // or the cpu policy configuration or absent altogether.
      // The ACL has to be created always.
      // Find and use the traffic policy from one of the paths if present.
      const cfg::MatchToAction* matchToAction = nullptr;
      auto cpu = cpuPolicyByName.find(*aclCfg.name());
      auto data = dataPolicyByName.find(*aclCfg.name());
      bool isCoppAcl = false;
      if (cpu != cpuPolicyByName.end()) {
        matchToAction = (cpu->second);
        isCoppAcl = true;
      } else if (data != dataPolicyByName.end()) {
        matchToAction = (data->second);
      }

      // Here is sending to regular port queue action
      MatchAction* ma = nullptr;
      MatchAction matchAction = MatchAction();
      if (matchToAction) {
        const cfg::MatchToAction& mta = *matchToAction;
        if (auto sendToQueue = mta.action()->sendToQueue()) {
          matchAction.setSendToQueue(std::make_pair(*sendToQueue, isCoppAcl));
        }
        // TODO(daiweix): set setTc and userDefinedTrap actions only when
        // disruptive feature sai_user_defined_trap is enabled. Otherwise,
        // although these actions will not take effect and programmed ACL
        // won't change. Switch switch change will still trigger
        // SaiAclTableManager::changedAclEntry() to remove and re-program the
        // same ACL during warmboot. This is unnecessary and caused
        // programming issue on platforms like TH4. Avoiding this issue by
        // skip setting setTc and userDefinedTrap for now.
        if (FLAGS_sai_user_defined_trap) {
          if (auto setTc = mta.action()->setTc()) {
            matchAction.setSetTc(std::make_pair(*setTc, isCoppAcl));
          }
          if (auto userDefinedTrap = mta.action()->userDefinedTrap()) {
            matchAction.setUserDefinedTrap(*userDefinedTrap);
          }
        }
        if (auto actionCounter = mta.action()->counter()) {
          auto counter = counterByName.find(*actionCounter);
          if (counter == counterByName.end()) {
            throw FbossError(
                "Invalid config: No counter named ",
                *mta.action()->counter(),
                " found.");
          }
          matchAction.setTrafficCounter(*(counter->second));
        }
        if (auto setDscp = mta.action()->setDscp()) {
          matchAction.setSetDscp(*setDscp);
        }
        if (auto ingressMirror = mta.action()->ingressMirror()) {
          matchAction.setIngressMirror(*ingressMirror);
        }
        if (auto egressMirror = mta.action()->egressMirror()) {
          matchAction.setEgressMirror(*egressMirror);
        }
        if (auto toCpuAction = mta.action()->toCpuAction()) {
          matchAction.setToCpuAction(*toCpuAction);
        }
        if (auto flowletAction = mta.action()->flowletAction()) {
          matchAction.setFlowletAction(*flowletAction);
        }
        if (auto ecmpHashAction = mta.action()->ecmpHashAction()) {
          matchAction.setEcmpHashAction(*ecmpHashAction);
        }
        if (auto redirectToNextHop = mta.action()->redirectToNextHop()) {
          matchAction.setRedirectToNextHop(
              std::make_pair(*redirectToNextHop, MatchAction::NextHopSet()));
          bool hasTunnelRedirect = false;
          for (const auto& nh : *redirectToNextHop->redirectNextHops()) {
            auto nhIp = folly::IPAddress(*nh.ip());
            if (nhIp.isV6() && nhIp.isLinkLocal()) {
              if (!nh.intfID().has_value()) {
                throw FbossError(
                    "Invalid config: link-local nexthop ",
                    nhIp.str(),
                    " in ACL ",
                    *aclCfg.name(),
                    " must specify an interface");
              }
            } else if (nh.intfID().has_value()) {
              throw FbossError(
                  "Invalid config: non-link-local nexthop ",
                  nhIp.str(),
                  " in ACL ",
                  *aclCfg.name(),
                  " must not specify an interface");
            }
            if (nh.tunnelType().has_value() &&
                nh.tunnelType().value() == TunnelType::IP_IN_IP_ENCAP) {
              hasTunnelRedirect = true;
            }
          }
          if (aclNexthopHandler_) {
            aclNexthopHandler_->resolveActionNexthops(matchAction);
          }
          if (!hasTunnelRedirect &&
              !matchAction.getRedirectToNextHop().value().second.size()) {
            XLOG(DBG2)
                << "Setting newly configured ACL as disabled since no nexthops are available";
            enableAcl = false;
          }
        }
        ma = &matchAction;
      }

      int aclPriority = isCoppAcl ? cpuPriority++ : dataPriority++;
      if (aclPriority == FLAGS_pbr_acl_priority) {
        throw FbossError(
            "ACL ",
            *aclCfg.name(),
            " was assigned priority ",
            aclPriority,
            ", which is reserved for PBR ACL entries");
      }

      auto acl = updateAcl(
          aclStage,
          aclCfg,
          aclPriority,
          &numExistingProcessed,
          &changed,
          tableName,
          ma,
          enableAcl);

      if (const auto& aclAction = acl->getAclAction()) {
        const auto& inMirror =
            aclAction->cref<switch_state_tags::ingressMirror>();
        const auto& egMirror =
            aclAction->cref<switch_state_tags::egressMirror>();
        if (inMirror && !new_->getMirrors()->getNodeIf(inMirror->cref())) {
          throw FbossError("Mirror ", inMirror->cref(), " is undefined");
        }
        if (egMirror && !new_->getMirrors()->getNodeIf(egMirror->cref())) {
          throw FbossError("Mirror ", egMirror->cref(), " is undefined");
        }
      }
      entries.emplace_back(acl->getID(), acl);
    }
    return entries;
  };

  folly::gen::from(addToAcls()) | folly::gen::appendTo(newAcls);

  auto origAclMap = tableName.has_value()
      ? orig_->getAclsForTable(aclStage, tableName.value())
      : nullptr;

  if (FLAGS_enable_acl_table_group) {
    if (origAclMap && numExistingProcessed != origAclMap->size()) {
      // Some existing ACLs were removed from the table (multiple acl tables
      // implementation).
      changed = true;
    }
  } else {
    if (numExistingProcessed != orig_->getAcls()->numNodes()) {
      // Some existing ACLs were removed (single acl table implementation).
      changed = true;
    }
  }

  if (!changed) {
    return nullptr;
  }

  if (origAclMap) {
    return origAclMap->clone(std::move(newAcls));
  }

  return std::make_shared<AclMap>(std::move(newAcls));
}

std::shared_ptr<AclEntry> ThriftConfigApplier::updateAcl(
    cfg::AclStage aclStage,
    const cfg::AclEntry& acl,
    int priority,
    int* numExistingProcessed,
    bool* changed,
    std::optional<std::string> tableName,
    const MatchAction* action,
    bool enable) {
  std::shared_ptr<AclEntry> origAcl;

  if (FLAGS_enable_acl_table_group) { // multiple acl tables implementation
    CHECK(tableName.has_value());
    auto origAclMap = orig_->getAclsForTable(aclStage, tableName.value());
    if (origAclMap) {
      origAcl = origAclMap->getEntryIf(*acl.name());
    }
  } else { // single acl table implementation
    CHECK(!tableName.has_value());
    origAcl = orig_->getAcls()->getNodeIf(
        *acl.name()); // orig_ empty in coldboot, or comes from
                      // follydynamic in warmboot
  }

  auto newAcl =
      createAcl(&acl, priority, action, enable); // new always comes from config

  if (origAcl) {
    ++(*numExistingProcessed);
    if (*origAcl == *newAcl) {
      return origAcl;
    }
  }

  *changed = true;
  return newAcl;
}

void ThriftConfigApplier::checkUdfAcl(
    const std::vector<std::string>& udfGroups) const {
  if ((udfGroups).size() == 0) {
    throw FbossError("udf group list is empty");
  }
  if (!cfg_->udfConfig()) {
    throw FbossError("No udf config exists");
  }
  auto newUdfConfig = std::make_shared<UdfConfig>();
  newUdfConfig->fromThrift(*cfg_->udfConfig());

  auto udfGroupMap = newUdfConfig->getUdfGroupMap();
  if (udfGroupMap == nullptr) {
    throw FbossError("Udf group map does not exist");
  }

  for (const auto& udfGroupName : udfGroups) {
    if (udfGroupMap->find(udfGroupName) == udfGroupMap->end()) {
      throw FbossError(
          "Udf group in the ACL entry: ",
          udfGroupName,
          " does not exist in the Udf group map");
    }
  }
}

void ThriftConfigApplier::checkAcl(const cfg::AclEntry* config) const {
  // check l4 port
  if (auto l4SrcPort = config->l4SrcPort()) {
    if (*l4SrcPort < 0 || *l4SrcPort > AclEntry::kMaxL4Port) {
      throw FbossError(
          "L4 source port must be between 0 and ",
          std::to_string(AclEntry::kMaxL4Port));
    }
  }
  if (auto l4DstPort = config->l4DstPort()) {
    if (*l4DstPort < 0 || *l4DstPort > AclEntry::kMaxL4Port) {
      throw FbossError(
          "L4 destination port must be between 0 and ",
          std::to_string(AclEntry::kMaxL4Port));
    }
  }
  if (auto l4DstPortRange = config->l4DstPortRange()) {
    if (*l4DstPortRange->minimum() < 0 ||
        *l4DstPortRange->minimum() > AclEntry::kMaxL4Port) {
      throw FbossError(
          "L4 destination port range minimum must be between 0 and ",
          std::to_string(AclEntry::kMaxL4Port));
    }
    if (*l4DstPortRange->maximum() < 0 ||
        *l4DstPortRange->maximum() > AclEntry::kMaxL4Port) {
      throw FbossError(
          "L4 destination port range maximum must be between 0 and ",
          std::to_string(AclEntry::kMaxL4Port));
    }
    if (*l4DstPortRange->minimum() > *l4DstPortRange->maximum()) {
      throw FbossError(
          "L4 destination port range minimum must not exceed maximum");
    }
    if (config->l4DstPort()) {
      throw FbossError("l4DstPort and l4DstPortRange cannot both be set");
    }
  }
  if (config->dstIp() && (config->dstIpV6Word3() || config->dstIpV6Word2())) {
    throw FbossError(
        "dstIp cannot be combined with dstIpV6Word3 or dstIpV6Word2");
  }
  if (config->icmpCode() && !config->icmpType()) {
    throw FbossError("icmp type must be set when icmp code is set");
  }
  if (auto icmpType = config->icmpType()) {
    if (*icmpType < 0 || *icmpType > AclEntry::kMaxIcmpType) {
      throw FbossError(
          "icmp type value must be between 0 and ",
          std::to_string(AclEntry::kMaxIcmpType));
    }
  }
  if (auto icmpCode = config->icmpCode()) {
    if (*icmpCode < 0 || *icmpCode > AclEntry::kMaxIcmpCode) {
      throw FbossError(
          "icmp type value must be between 0 and ",
          std::to_string(AclEntry::kMaxIcmpCode));
    }
  }
  if (auto tcpFlagsBitMap = config->tcpFlagsBitMap()) {
    if (*tcpFlagsBitMap < 0 || *tcpFlagsBitMap > AclEntry::kMaxTcpFlags) {
      throw FbossError(
          "tcp flags bitmap must be between 0 and ",
          std::to_string(AclEntry::kMaxTcpFlags));
    }
  }
  if (auto tcpFlagsMask = config->tcpFlagsMask()) {
    if (!config->tcpFlagsBitMap()) {
      throw FbossError(
          "tcp flags bitmap must be set when tcp flags mask is set");
    }
    if (*tcpFlagsMask < 1 || *tcpFlagsMask > AclEntry::kMaxTcpFlagsMask) {
      throw FbossError(
          "tcp flags mask must be between 1 and ",
          std::to_string(AclEntry::kMaxTcpFlagsMask));
    }
    // Bits outside the mask are never compared, so a rule that sets them does
    // not match what it appears to.
    if (*config->tcpFlagsBitMap() & ~*tcpFlagsMask) {
      throw FbossError(
          "tcp flags bitmap ",
          *config->tcpFlagsBitMap(),
          " sets bits outside tcp flags mask ",
          *tcpFlagsMask);
    }
  }
  // TODO(daiweix): check proto should be 58 if icmp type/code is specified
  // after CS00012373216 is resolved.
  if (config->icmpType() && config->proto() &&
      !(*config->proto() == AclEntry::kProtoIcmp ||
        *config->proto() == AclEntry::kProtoIcmpv6)) {
    throw FbossError(
        "proto must be either icmp or icmpv6 ", "if icmp type is set");
  }
  if (auto ttl = config->ttl()) {
    if (auto ttlValue = *ttl->value()) {
      if (ttlValue > 255) {
        throw FbossError("ttl value is larger than 255");
      }
      if (ttlValue < 0) {
        throw FbossError("ttl value is less than 0");
      }
    }
    if (auto ttlMask = *ttl->mask()) {
      if (ttlMask > 255) {
        throw FbossError("ttl mask is larger than 255");
      }
      if (ttlMask < 0) {
        throw FbossError("ttl mask is less than 0");
      }
    }
  }

  if (auto udfGroups = config->udfGroups()) {
    checkUdfAcl(*udfGroups);
  }
}

shared_ptr<AclEntry> ThriftConfigApplier::createAcl(
    const cfg::AclEntry* config,
    int priority,
    const MatchAction* action,
    bool enable) {
  checkAcl(config);
  auto newAcl = make_shared<AclEntry>(priority, *config->name());
  newAcl->setActionType(*config->actionType());
  if (action) {
    newAcl->setAclAction(*action);
  }
  if (auto srcIp = config->srcIp()) {
    newAcl->setSrcIp(IPAddress::createNetwork(*srcIp));
  }
  if (auto dstIp = config->dstIp()) {
    newAcl->setDstIp(IPAddress::createNetwork(*dstIp));
  }
  if (auto dstIpV6Word3 = config->dstIpV6Word3()) {
    newAcl->setDstIpV6Word3(*dstIpV6Word3);
  }
  if (auto dstIpV6Word2 = config->dstIpV6Word2()) {
    newAcl->setDstIpV6Word2(*dstIpV6Word2);
  }
  if (auto proto = config->proto()) {
    newAcl->setProto(*proto);
  }
  if (auto tcpFlagsBitMap = config->tcpFlagsBitMap()) {
    newAcl->setTcpFlagsBitMap(*tcpFlagsBitMap);
  }
  if (auto tcpFlagsMask = config->tcpFlagsMask()) {
    newAcl->setTcpFlagsMask(*tcpFlagsMask);
  }
  if (auto srcPort = config->srcPort()) {
    newAcl->setSrcPort(*srcPort);
  }
  if (auto dstPort = config->dstPort()) {
    newAcl->setDstPort(*dstPort);
  }
  if (auto l4SrcPort = config->l4SrcPort()) {
    newAcl->setL4SrcPort(*l4SrcPort);
  }
  if (auto l4DstPort = config->l4DstPort()) {
    newAcl->setL4DstPort(*l4DstPort);
  }
  if (auto l4DstPortRange = config->l4DstPortRange()) {
    newAcl->setL4DstPortRange(*l4DstPortRange);
  }
  if (auto lookupClassPort = config->lookupClassPort()) {
    newAcl->setLookupClassPort(*lookupClassPort);
  }
  if (auto ipFrag = config->ipFrag()) {
    newAcl->setIpFrag(*ipFrag);
  }
  if (auto icmpType = config->icmpType()) {
    newAcl->setIcmpType(*icmpType);
  }
  if (auto icmpCode = config->icmpCode()) {
    newAcl->setIcmpCode(*icmpCode);
  }
  if (auto dscp = config->dscp()) {
    newAcl->setDscp(*dscp);
  }
  if (auto dstMac = config->dstMac()) {
    newAcl->setDstMac(MacAddress(*dstMac));
  }
  if (auto ipType = config->ipType()) {
    newAcl->setIpType(*ipType);
  }
  if (auto etherType = config->etherType()) {
    newAcl->setEtherType(*etherType);
  }
  if (auto ttl = config->ttl()) {
    newAcl->setTtl(AclTtl(*ttl->value(), *ttl->mask()));
  }
  if (auto mplsLabel0Ttl = config->mplsLabel0Ttl()) {
    newAcl->setMplsLabel0Ttl(
        AclTtl(*mplsLabel0Ttl->value(), *mplsLabel0Ttl->mask()));
  }
  if (auto lookupClassL2 = config->lookupClassL2()) {
    newAcl->setLookupClassL2(*lookupClassL2);
  }
  if (auto lookupClassNeighbor = config->lookupClassNeighbor()) {
    newAcl->setLookupClassNeighbor(*lookupClassNeighbor);
  }
  if (auto lookupClassRoute = config->lookupClassRoute()) {
    newAcl->setLookupClassRoute(*lookupClassRoute);
  }
  if (auto packetLookupResult = config->packetLookupResult()) {
    newAcl->setPacketLookupResult(*packetLookupResult);
  }
  if (auto vlanID = config->vlanID()) {
    newAcl->setVlanID(*vlanID);
  }
  if (auto udfGroupList = config->udfGroups()) {
    newAcl->setUdfGroups(*udfGroupList);
  }
  if (auto roceOpcode = config->roceOpcode()) {
    newAcl->setRoceOpcode(*roceOpcode);
  }
  if (auto roceBytes = config->roceBytes()) {
    newAcl->setRoceBytes(*roceBytes);
  }
  if (auto roceMask = config->roceMask()) {
    newAcl->setRoceMask(*roceMask);
  }
  if (auto udfTable = config->udfTable()) {
    newAcl->setUdfTable(*udfTable);
  }
  newAcl->setEnabled(enable);
  return newAcl;
}

template <typename VlanOrIntfT, typename CfgVlanOrIntfT>
bool ThriftConfigApplier::updateDhcpOverrides(
    VlanOrIntfT* vlanOrIntf,
    const CfgVlanOrIntfT* config) {
  DhcpV4OverrideMap newDhcpV4OverrideMap;
  if (config->dhcpRelayOverridesV4()) {
    for (const auto& pair : *config->dhcpRelayOverridesV4()) {
      try {
        newDhcpV4OverrideMap[MacAddress(pair.first)] = IPAddressV4(pair.second);
      } catch (const IPAddressFormatException& ex) {
        throw FbossError(
            "Invalid IPv4 address in DHCPv4 relay override map: ", ex.what());
      }
    }
  }

  DhcpV6OverrideMap newDhcpV6OverrideMap;
  if (config->dhcpRelayOverridesV6()) {
    for (const auto& pair : *config->dhcpRelayOverridesV6()) {
      try {
        newDhcpV6OverrideMap[MacAddress(pair.first)] = IPAddressV6(pair.second);
      } catch (const IPAddressFormatException& ex) {
        throw FbossError(
            "Invalid IPv4 address in DHCPv4 relay override map: ", ex.what());
      }
    }
  }

  bool changed = false;
  auto oldDhcpV4OverrideMap = vlanOrIntf->getDhcpV4RelayOverrides();
  if (oldDhcpV4OverrideMap != newDhcpV4OverrideMap) {
    vlanOrIntf->setDhcpV4RelayOverrides(newDhcpV4OverrideMap);
    changed = true;
  }
  auto oldDhcpV6OverrideMap = vlanOrIntf->getDhcpV6RelayOverrides();
  if (oldDhcpV6OverrideMap != newDhcpV6OverrideMap) {
    vlanOrIntf->setDhcpV6RelayOverrides(newDhcpV6OverrideMap);
    changed = true;
  }
  return changed;
}

template <typename NeighborResponseEntry, typename IPAddr>
std::shared_ptr<NeighborResponseEntry>
ThriftConfigApplier::updateNeighborResponseEntry(
    const std::shared_ptr<NeighborResponseEntry>& orig,
    IPAddr ip,
    ThriftConfigApplier::InterfaceIpInfo addrInfo) {
  if (orig && orig->getMac() == addrInfo.mac &&
      orig->getInterfaceID() == addrInfo.interfaceID) {
    return nullptr;
  } else {
    return std::make_shared<NeighborResponseEntry>(
        ip, addrInfo.mac, addrInfo.interfaceID);
  }
}

std::shared_ptr<InterfaceMap> ThriftConfigApplier::updateInterfaces() {
  auto origIntfs = orig_->getInterfaces();
  InterfaceMap::NodeContainer newIntfs;
  bool changed = false;

  // Process all supplied interface configs
  size_t numExistingProcessed = 0;

  for (const auto& interfaceCfg : *cfg_->interfaces()) {
    InterfaceID id(*interfaceCfg.intfID());
    auto origIntf = origIntfs->getNodeIf(id);
    shared_ptr<Interface> newIntf;
    auto newAddrs = getInterfaceAddresses(&interfaceCfg);
    if (interfaceCfg.type() == cfg::InterfaceType::SYSTEM_PORT) {
      auto sysPort =
          new_->getSystemPorts()->getNode(SystemPortID(*interfaceCfg.intfID()));
      auto dsfNode = cfg_->dsfNodes()->find(sysPort->getSwitchId())->second;
      // TODO - [2-stage DSF] Consider adding local sys port ranges
      // to DsfNodeConfig as well.
      if (interfaceCfg.scope() == cfg::Scope::GLOBAL &&
          !withinRange(
              *dsfNode.systemPortRanges(),
              InterfaceID(*interfaceCfg.intfID()))) {
        throw FbossError(
            "Interface intfID :",
            *interfaceCfg.intfID(),
            "is out of range for corresponding VOQ switch.",
            "sys port range");
      }
      CHECK_EQ(
          static_cast<int>(sysPort->getScope()),
          static_cast<int>(*interfaceCfg.scope()));
    }
    if (interfaceCfg.type() == cfg::InterfaceType::PORT) {
      if (auto port = interfaceCfg.portID()) {
        if (!new_->getPorts()->getNodeIf(PortID(*port))) {
          throw FbossError("Port router interface config invalid port");
        }
      } else if (auto aggPort = interfaceCfg.aggregatePortID()) {
        if (!new_->getAggregatePorts()->getNodeIf(AggregatePortID(*aggPort))) {
          throw FbossError(
              "Port router interface config invalid aggregate port");
        }
      } else {
        throw FbossError("Port router interface config missing port");
      }
    } else if (
        interfaceCfg.portID().has_value() ||
        interfaceCfg.aggregatePortID().has_value()) {
      // Only a port router interface is bound to a port or an aggregate port.
      throw FbossError(
          "Router interface ",
          *interfaceCfg.intfID(),
          " is not of type port, but is bound to a port or aggregate port");
    }
    if (origIntf) {
      newIntf = updateInterface(origIntf, &interfaceCfg, newAddrs);
      ++numExistingProcessed;
    } else {
      newIntf = createInterface(&interfaceCfg, newAddrs);
    }
    updateVlanInterfaces(newIntf ? newIntf.get() : origIntf.get());
    changed |= updateMap(&newIntfs, origIntf, newIntf);
  }

  if (numExistingProcessed != origIntfs->numNodes()) {
    // Some existing interfaces were removed.
    CHECK_LT(numExistingProcessed, origIntfs->numNodes());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }

  return std::make_shared<InterfaceMap>(std::move(newIntfs));
}

std::shared_ptr<MultiSwitchInterfaceMap>
ThriftConfigApplier::updateRemoteInterfaces(
    const std::shared_ptr<MultiSwitchInterfaceMap>& interfaces) {
  if (scopeResolver_.hasVoq() &&
      scopeResolver_.scope(cfg::SwitchType::VOQ).size() <= 1) {
    // remote system ports are applicable only for voq switches
    // remote system ports are updated on config only when more than voq
    // switches are configured on a given SwSwitch
    return orig_->getRemoteInterfaces();
  }
  auto remoteInterfaces = orig_->getRemoteInterfaces()->clone();

  for (const auto& [matcher, interfaceMap] : std::as_const(*interfaces)) {
    for (const auto& [intfID, intf] : std::as_const(*interfaceMap)) {
      if (intf->getType() != cfg::InterfaceType::SYSTEM_PORT) {
        continue;
      }
      if (intf->getScope() != cfg::Scope::GLOBAL) {
        if (remoteInterfaces->getNodeIf(intfID)) {
          remoteInterfaces->removeNode(intfID);
        }
        continue;
      }
      auto remoteIntfScope = scopeResolver_.scope(cfg::SwitchType::VOQ);
      remoteIntfScope.exclude(scopeResolver_.scope(intf, *cfg_).switchIds());
      auto remoteIntf = std::make_shared<Interface>();
      remoteIntf->fromThrift(intf->toThrift());
      auto oldRemoteInterface = remoteInterfaces->getNodeIf(intfID);
      DsfStateUpdaterUtil::updateNeighborEntry(
          oldRemoteInterface ? oldRemoteInterface->getArpTable() : nullptr,
          remoteIntf->getArpTable());
      DsfStateUpdaterUtil::updateNeighborEntry(
          oldRemoteInterface ? oldRemoteInterface->getNdpTable() : nullptr,
          remoteIntf->getNdpTable());
      if (remoteInterfaces->getNodeIf(intfID)) {
        remoteInterfaces->updateNode(std::move(remoteIntf), remoteIntfScope);
      } else {
        remoteInterfaces->addNode(std::move(remoteIntf), remoteIntfScope);
      }
    }
  }

  // check for deleted interfaces
  for (const auto& [matcher, interfaceMap] :
       std::as_const(*orig_->getInterfaces())) {
    for (const auto& [intfID, intf] : std::as_const(*interfaceMap)) {
      if (intf->getType() != cfg::InterfaceType::SYSTEM_PORT) {
        continue;
      }
      if (intf->getScope() != cfg::Scope::GLOBAL) {
        continue;
      }
      if (!interfaces->getNodeIf(intfID)) {
        remoteInterfaces->removeNode(intfID);
      }
    }
  }

  return remoteInterfaces;
}

shared_ptr<Interface> ThriftConfigApplier::createInterface(
    const cfg::Interface* config,
    const Interface::Addresses& addrs) {
  auto name = getInterfaceName(config);
  auto mac = getInterfaceMac(config);
  auto mtu = config->mtu().value_or(Interface::kDefaultMtu);
  auto intf = make_shared<Interface>(
      InterfaceID(*config->intfID()),
      RouterID(*config->routerID()),
      std::optional<VlanID>(*config->vlanID()),
      folly::StringPiece(name),
      mac,
      mtu,
      *config->isVirtual(),
      *config->isStateSyncDisabled(),
      *config->type(),
      std::optional<RemoteInterfaceType>(std::nullopt),
      std::optional<LivenessStatus>(std::nullopt),
      *config->scope());
  if (auto port = config->portID()) {
    intf->setPortID(PortID(*port));
  } else if (auto aggPort = config->aggregatePortID()) {
    intf->setAggregatePortID(AggregatePortID(*aggPort));
  }
  updateNeighborResponseTablesForIntfs(intf.get(), addrs);
  updateDhcpOverrides(intf.get(), config);
  intf->setAddresses(addrs);
  if (auto ndp = config->ndp()) {
    if (ndp->routerAddress() &&
        !intf->hasAddress(folly::IPAddress(*ndp->routerAddress()))) {
      throw FbossError(
          "Router address: ",
          *ndp->routerAddress(),
          " does not match any interface address");
    }
    intf->setNdpConfig(*ndp);
  }

  auto dhcpV4Relay = config->dhcpRelayAddressV4()
      ? IPAddressV4(*config->dhcpRelayAddressV4())
      : IPAddressV4();
  auto dhcpV6Relay = config->dhcpRelayAddressV6()
      ? IPAddressV6(*config->dhcpRelayAddressV6())
      : IPAddressV6("::");
  intf->setDhcpV4Relay(dhcpV4Relay);
  intf->setDhcpV6Relay(dhcpV6Relay);
  if (config->desiredPeerName().has_value()) {
    intf->setDesiredPeerName(config->desiredPeerName().value());
  }
  if (config->desiredPeerAddressIPv6().has_value()) {
    intf->setDesiredPeerAddressIPv6(config->desiredPeerAddressIPv6().value());
  }
  return intf;
}

shared_ptr<Interface> ThriftConfigApplier::updateInterface(
    const shared_ptr<Interface>& orig,
    const cfg::Interface* config,
    const Interface::Addresses& addrs) {
  CHECK_EQ(orig->getID(), InterfaceID(*config->intfID()));

  cfg::NdpConfig ndp;
  if (config->ndp()) {
    ndp = *config->ndp();
  }
  auto name = getInterfaceName(config);
  auto mac = getInterfaceMac(config);
  auto mtu = config->mtu().value_or(Interface::kDefaultMtu);
  auto oldDhcpV4Relay = orig->getDhcpV4Relay();
  auto newDhcpV4Relay = config->dhcpRelayAddressV4()
      ? IPAddressV4(*config->dhcpRelayAddressV4())
      : IPAddressV4();
  auto oldDhcpV6Relay = orig->getDhcpV6Relay();
  auto newDhcpV6Relay = config->dhcpRelayAddressV6()
      ? IPAddressV6(*config->dhcpRelayAddressV6())
      : IPAddressV6("::");

  auto newIntf = orig->clone();
  bool changed_neighbor_table =
      updateNeighborResponseTablesForIntfs(newIntf.get(), addrs);
  bool changed_dhcp_overrides = updateDhcpOverrides(newIntf.get(), config);
  std::optional<PortID> cfgPort{};
  if (auto portID = config->portID()) {
    cfgPort = PortID(*portID);
  }
  std::optional<AggregatePortID> cfgAggregatePort{};
  if (auto aggregatePortID = config->aggregatePortID()) {
    cfgAggregatePort = AggregatePortID(*aggregatePortID);
  }
  auto desiredPeerChanged = [](const auto& configVal,
                               const auto& origVal) -> bool {
    if (!configVal.has_value() && !origVal.has_value()) {
      return false;
    }
    if (configVal.has_value() && origVal.has_value()) {
      return configVal.value() != origVal.value();
    }
    return true;
  };

  bool changedDesiredPeer =
      desiredPeerChanged(
          config->desiredPeerName(), orig->getDesiredPeerName()) ||
      desiredPeerChanged(
          config->desiredPeerAddressIPv6(), orig->getDesiredPeerAddressIPv6());

  if (orig->getRouterID() == RouterID(*config->routerID()) &&
      (orig->getVlanIDHelper() == VlanID(*config->vlanID())) &&
      (orig->getPortIDf() == cfgPort) &&
      (orig->getAggregatePortIDf() == cfgAggregatePort) &&
      orig->getName() == name && orig->getMac() == mac &&
      orig->getAddressesCopy() == addrs &&
      orig->getNdpConfig()->toThrift() == ndp && orig->getMtu() == mtu &&
      orig->isVirtual() == *config->isVirtual() &&
      orig->isStateSyncDisabled() == *config->isStateSyncDisabled() &&
      orig->getType() == *config->type() && oldDhcpV4Relay == newDhcpV4Relay &&
      oldDhcpV6Relay == newDhcpV6Relay && !changed_neighbor_table &&
      !changed_dhcp_overrides && !changedDesiredPeer) {
    // No change
    return nullptr;
  }

  newIntf->setRouterID(RouterID(*config->routerID()));
  newIntf->setType(*config->type());
  if (newIntf->getType() == cfg::InterfaceType::VLAN) {
    newIntf->setVlanID(VlanID(*config->vlanID()));
  }
  if (auto portID = config->portID()) {
    if (newIntf->getType() != cfg::InterfaceType::PORT) {
      throw FbossError(
          "Router interface ", newIntf->getID(), " is not of of type port");
    }
    newIntf->setPortID(PortID(*portID));
  } else if (auto aggregatePortID = config->aggregatePortID()) {
    if (newIntf->getType() != cfg::InterfaceType::PORT) {
      throw FbossError(
          "Router interface ", newIntf->getID(), " is not of of type port");
    }
    newIntf->setAggregatePortID(AggregatePortID(*aggregatePortID));
  }
  newIntf->setName(name);
  newIntf->setMac(mac);
  newIntf->setAddresses(addrs);
  newIntf->setNdpConfig(ndp);
  newIntf->setMtu(mtu);
  newIntf->setIsVirtual(*config->isVirtual());
  newIntf->setIsStateSyncDisabled(*config->isStateSyncDisabled());
  newIntf->setDhcpV4Relay(newDhcpV4Relay);
  newIntf->setDhcpV6Relay(newDhcpV6Relay);
  newIntf->setScope(*config->scope());
  if (config->desiredPeerName().has_value()) {
    newIntf->setDesiredPeerName(config->desiredPeerName().value());
  }
  if (config->desiredPeerAddressIPv6().has_value()) {
    newIntf->setDesiredPeerAddressIPv6(
        config->desiredPeerAddressIPv6().value());
  }

  return newIntf;
}

bool ThriftConfigApplier::updateNeighborResponseTablesForIntfs(
    Interface* intf,
    const Interface::Addresses& addrs) {
  auto arpChanged = false, ndpChanged = false;
  auto origArp = intf->getArpResponseTable();
  auto origNdp = intf->getNdpResponseTable();
  ArpResponseTable::NodeContainer arpTable;
  NdpResponseTable::NodeContainer ndpTable;

  auto mac = intf->getMac();
  auto intfID = intf->getID();

  for (const auto& [ip, mask] : addrs) {
    if (ip.isV4()) {
      auto origNode = origArp->getEntry(ip.asV4());
      auto newNode = updateNeighborResponseEntry(
          origNode,
          ip.asV4(),
          ThriftConfigApplier::InterfaceIpInfo{mask, mac, intfID});
      arpChanged |= updateMap(&arpTable, origNode, newNode);
    } else {
      auto origNode = origNdp->getEntry(ip.asV6());
      auto newNode = updateNeighborResponseEntry(
          origNode,
          ip.asV6(),
          ThriftConfigApplier::InterfaceIpInfo{mask, mac, intfID});
      ndpChanged |= updateMap(&ndpTable, origNode, newNode);
    }
  }

  arpChanged |= origArp->size() != arpTable.size();
  ndpChanged |= origNdp->size() != ndpTable.size();

  if (arpChanged) {
    intf->setArpResponseTable(origArp->clone(std::move(arpTable)));
  }
  if (ndpChanged) {
    intf->setNdpResponseTable(origNdp->clone(std::move(ndpTable)));
  }
  return arpChanged || ndpChanged;
}

shared_ptr<SflowCollectorMap> ThriftConfigApplier::updateSflowCollectors() {
  auto origCollectors = orig_->getSflowCollectors();
  auto newCollectors = std::make_shared<SflowCollectorMap>();
  bool changed = false;

  // Process all supplied collectors
  size_t numExistingProcessed = 0;
  for (const auto& collector : *cfg_->sFlowCollectors()) {
    folly::IPAddress address(*collector.ip());
    auto id = address.toFullyQualified() + ':' +
        folly::to<std::string>(*collector.port());
    auto origCollector = origCollectors->getNodeIf(id);
    shared_ptr<SflowCollector> newCollector;

    if (origCollector) {
      newCollector = updateSflowCollector(origCollector, &collector);
      ++numExistingProcessed;
    } else {
      newCollector = createSflowCollector(&collector);
    }
    changed |=
        updateThriftMapNode(newCollectors.get(), origCollector, newCollector);
  }

  if (numExistingProcessed != origCollectors->numNodes()) {
    // Some existing SflowCollectors were removed.
    CHECK_LT(numExistingProcessed, origCollectors->numNodes());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }

  return newCollectors;
}

shared_ptr<SflowCollector> ThriftConfigApplier::createSflowCollector(
    const cfg::SflowCollector* config) {
  return make_shared<SflowCollector>(
      *config->ip(), static_cast<uint16_t>(*config->port()));
}

shared_ptr<SflowCollector> ThriftConfigApplier::updateSflowCollector(
    const shared_ptr<SflowCollector>& orig,
    const cfg::SflowCollector* config) {
  auto newCollector = createSflowCollector(config);

  if (orig->getAddress() == newCollector->getAddress()) {
    return nullptr;
  }

  return newCollector;
}

shared_ptr<QcmCfg> ThriftConfigApplier::createQcmCfg(
    const cfg::QcmConfig& config) {
  auto newQcmCfg = make_shared<QcmCfg>();

  WeightMap newWeightMap;
  for (const auto& weights : *config.flowWeights()) {
    newWeightMap.emplace(static_cast<int>(weights.first), weights.second);
  }
  newQcmCfg->setFlowWeightMap(newWeightMap);

  Port2QosQueueIdMap port2QosQueueIds;
  for (const auto& perPortQosQueueIds : *config.port2QosQueueIds()) {
    std::set<int> queueIds;
    for (const auto& queueId : perPortQosQueueIds.second) {
      queueIds.insert(queueId);
    }
    port2QosQueueIds[perPortQosQueueIds.first] = queueIds;
  }
  newQcmCfg->setPort2QosQueueIdMap(port2QosQueueIds);

  newQcmCfg->setCollectorSrcPort(*config.collectorSrcPort());
  newQcmCfg->setNumFlowSamplesPerView(*config.numFlowSamplesPerView());
  newQcmCfg->setFlowLimit(*config.flowLimit());
  newQcmCfg->setNumFlowsClear(*config.numFlowsClear());
  newQcmCfg->setScanIntervalInUsecs(*config.scanIntervalInUsecs());
  newQcmCfg->setExportThreshold(*config.exportThreshold());
  newQcmCfg->setAgingInterval(*config.agingIntervalInMsecs());
  newQcmCfg->setCollectorDstIp(
      IPAddress::createNetwork(*config.collectorDstIp()));
  newQcmCfg->setCollectorDstPort(*config.collectorDstPort());
  newQcmCfg->setMonitorQcmCfgPortsOnly(*config.monitorQcmCfgPortsOnly());
  if (auto dscp = config.collectorDscp()) {
    newQcmCfg->setCollectorDscp(*dscp);
  }
  if (auto ppsToQcm = config.ppsToQcm()) {
    newQcmCfg->setPpsToQcm(*ppsToQcm);
  }
  newQcmCfg->setCollectorSrcIp(
      IPAddress::createNetwork(*config.collectorSrcIp()));
  newQcmCfg->setMonitorQcmPortList(*config.monitorQcmPortList());
  return newQcmCfg;
}

shared_ptr<FlowletSwitchingConfig>
ThriftConfigApplier::createFlowletSwitchingConfig(
    const cfg::FlowletSwitchingConfig& config) {
  auto newFlowletSwitchingConfig = make_shared<FlowletSwitchingConfig>();

  newFlowletSwitchingConfig->setInactivityIntervalUsecs(
      *config.inactivityIntervalUsecs());
  newFlowletSwitchingConfig->setFlowletTableSize(*config.flowletTableSize());
  newFlowletSwitchingConfig->setDynamicEgressLoadExponent(
      *config.dynamicEgressLoadExponent());
  newFlowletSwitchingConfig->setDynamicQueueExponent(
      *config.dynamicQueueExponent());
  newFlowletSwitchingConfig->setDynamicQueueMinThresholdBytes(
      *config.dynamicQueueMinThresholdBytes());
  newFlowletSwitchingConfig->setDynamicQueueMaxThresholdBytes(
      *config.dynamicQueueMaxThresholdBytes());
  newFlowletSwitchingConfig->setDynamicSampleRate(*config.dynamicSampleRate());
  newFlowletSwitchingConfig->setDynamicEgressMinThresholdBytes(
      *config.dynamicEgressMinThresholdBytes());
  newFlowletSwitchingConfig->setDynamicEgressMaxThresholdBytes(
      *config.dynamicEgressMaxThresholdBytes());
  newFlowletSwitchingConfig->setDynamicPhysicalQueueExponent(
      *config.dynamicPhysicalQueueExponent());
  newFlowletSwitchingConfig->setMaxLinks(*config.maxLinks());
  newFlowletSwitchingConfig->setSwitchingMode(*config.switchingMode());
  newFlowletSwitchingConfig->setBackupSwitchingMode(
      *config.backupSwitchingMode());
  if (config.primaryPathQualityThreshold()) {
    newFlowletSwitchingConfig->setPrimaryPathQualityThreshold(
        *config.primaryPathQualityThreshold());
  }
  if (config.alternatePathCost()) {
    newFlowletSwitchingConfig->setAlternatePathCost(
        *config.alternatePathCost());
  }
  if (config.alternatePathBias()) {
    newFlowletSwitchingConfig->setAlternatePathBias(
        *config.alternatePathBias());
  }
  if (config.minWidthForArsVirtualGroup()) {
    newFlowletSwitchingConfig->setMinWidthForArsVirtualGroup(
        *config.minWidthForArsVirtualGroup());
  }
  if (config.maxArsVirtualGroupWidth()) {
    newFlowletSwitchingConfig->setMaxArsVirtualGroupWidth(
        *config.maxArsVirtualGroupWidth());
  }
  if (config.maxArsVirtualGroups()) {
    newFlowletSwitchingConfig->setMaxArsVirtualGroups(
        *config.maxArsVirtualGroups());
  }
  if (config.arsVirtualGroupAlternateMembers()) {
    if (!config.maxArsVirtualGroupWidth()) {
      throw FbossError(
          "arsVirtualGroupAlternateMembers requires maxArsVirtualGroupWidth "
          "to be set");
    }
    auto alternateMembers = *config.arsVirtualGroupAlternateMembers();
    // Alternate slots come out of the virtual group width, so reserving all of
    // them would leave no primary member able to forward.
    if (alternateMembers <= 0 ||
        alternateMembers >= *config.maxArsVirtualGroupWidth()) {
      throw FbossError(
          "arsVirtualGroupAlternateMembers ",
          alternateMembers,
          " must be greater than 0 and less than maxArsVirtualGroupWidth ",
          *config.maxArsVirtualGroupWidth());
    }
    newFlowletSwitchingConfig->setArsVirtualGroupAlternateMembers(
        alternateMembers);
  }
  if (config.arsVirtualGroupCommonMembersThreshold()) {
    if (!config.arsVirtualGroupAlternateMembers()) {
      throw FbossError(
          "arsVirtualGroupCommonMembersThreshold requires "
          "arsVirtualGroupAlternateMembers to be set");
    }
    newFlowletSwitchingConfig->setArsVirtualGroupCommonMembersThreshold(
        *config.arsVirtualGroupCommonMembersThreshold());
  }
  if (config.arsGroupWidth()) {
    newFlowletSwitchingConfig->setArsGroupWidth(*config.arsGroupWidth());
  }
  if (config.standbySwitchingMode()) {
    // Distinct switching modes keep the standby and primary ARS objects from
    // collapsing into a single SaiStore entry.
    if (*config.standbySwitchingMode() == *config.switchingMode()) {
      throw FbossError(
          "standbySwitchingMode must differ from switchingMode, both are ",
          apache::thrift::util::enumNameSafe(*config.switchingMode()));
    }
    if (!config.standbyInactivityIntervalUsecs()) {
      throw FbossError(
          "standbySwitchingMode is set but standbyInactivityIntervalUsecs "
          "is missing");
    }
    if (!config.standbyFlowletTableSize()) {
      throw FbossError(
          "standbySwitchingMode is set but standbyFlowletTableSize is missing");
    }
    newFlowletSwitchingConfig->setStandbySwitchingMode(
        *config.standbySwitchingMode());
    newFlowletSwitchingConfig->setStandbyInactivityIntervalUsecs(
        *config.standbyInactivityIntervalUsecs());
    newFlowletSwitchingConfig->setStandbyFlowletTableSize(
        *config.standbyFlowletTableSize());
  } else if (
      config.standbyInactivityIntervalUsecs() ||
      config.standbyFlowletTableSize()) {
    throw FbossError(
        "standbyInactivityIntervalUsecs and standbyFlowletTableSize require "
        "standbySwitchingMode to be set");
  }
  return newFlowletSwitchingConfig;
}

shared_ptr<MultiSwitchPortFlowletCfgMap>
ThriftConfigApplier::updatePortFlowletConfigs(bool* changed) {
  *changed = false;
  auto origPortFlowletConfigs = orig_->getPortFlowletCfgs();
  PortFlowletCfgMap::NodeContainer newPortFlowletConfigMap;
  auto newCfgedPortFlowlets = cfg_->portFlowletConfigs();

  if (!newCfgedPortFlowlets && !origPortFlowletConfigs->numNodes()) {
    return nullptr;
  }

  if (!newCfgedPortFlowlets && origPortFlowletConfigs->numNodes()) {
    // old cfg eixists but new one doesn't
    *changed = true;
    return std::make_shared<MultiSwitchPortFlowletCfgMap>();
  }

  if (newCfgedPortFlowlets && !origPortFlowletConfigs->numNodes()) {
    *changed = true;
  }

  // if old/new cfgs are present, compare size
  if (origPortFlowletConfigs->numNodes() != (*newCfgedPortFlowlets).size()) {
    *changed = true;
  }

  // origPortFlowletConfigs, newPortFlowletConfigs both are configured
  // and with with same size
  // check if there is any update on it when compared
  // with last one
  for (auto& portFlowletConfig : *newCfgedPortFlowlets) {
    auto newPortFlowletConfig = createPortFlowletConfig(
        portFlowletConfig.first, portFlowletConfig.second);
    // if port flowlet cfg map exist, check if the specific port flowlet cfg
    // exists or not
    auto origPortFlowletConfig =
        origPortFlowletConfigs->getNodeIf(portFlowletConfig.first);
    if (!origPortFlowletConfig ||
        (*origPortFlowletConfig != *newPortFlowletConfig)) {
      /* new entry added or existing entries do not match */
      *changed = true;
    }
    newPortFlowletConfigMap.emplace(
        std::make_pair(portFlowletConfig.first, newPortFlowletConfig));
  }

  if (*changed) {
    auto portFlowletConfigMap =
        std::make_shared<PortFlowletCfgMap>(std::move(newPortFlowletConfigMap));
    return toMultiSwitchMap<MultiSwitchPortFlowletCfgMap>(
        portFlowletConfigMap, scopeResolver_);
  }
  return nullptr;
}

std::shared_ptr<PortFlowletCfg> ThriftConfigApplier::createPortFlowletConfig(
    const std::string& id,
    const cfg::PortFlowletConfig& portFlowletConfig) {
  auto portFlowletCfg = std::make_shared<PortFlowletCfg>(id);
  portFlowletCfg->setScalingFactor(*portFlowletConfig.scalingFactor());
  portFlowletCfg->setLoadWeight(*portFlowletConfig.loadWeight());
  portFlowletCfg->setQueueWeight(*portFlowletConfig.queueWeight());
  return portFlowletCfg;
}

shared_ptr<MultiSwitchLlrConfigMap> ThriftConfigApplier::updateLlrConfigs(
    bool* changed) {
  *changed = false;
  auto origLlrConfigs = orig_->getLlrConfigs();
  LlrConfigMap::NodeContainer newLlrConfigMap;
  auto newCfgedLlrConfigs = cfg_->llrConfigs();

  if (!newCfgedLlrConfigs && !origLlrConfigs->numNodes()) {
    return nullptr;
  }
  if (!newCfgedLlrConfigs && origLlrConfigs->numNodes()) {
    // old cfg exists but new one doesn't
    *changed = true;
    return std::make_shared<MultiSwitchLlrConfigMap>();
  }
  if (newCfgedLlrConfigs && !origLlrConfigs->numNodes()) {
    *changed = true;
  }
  if (origLlrConfigs->numNodes() != (*newCfgedLlrConfigs).size()) {
    *changed = true;
  }
  for (auto& llrConfig : *newCfgedLlrConfigs) {
    auto newLlrConfig = createLlrConfig(llrConfig.first, llrConfig.second);
    auto origLlrConfig = origLlrConfigs->getNodeIf(llrConfig.first);
    if (!origLlrConfig || (*origLlrConfig != *newLlrConfig)) {
      *changed = true;
    }
    newLlrConfigMap.emplace(std::make_pair(llrConfig.first, newLlrConfig));
  }

  if (*changed) {
    auto llrConfigMap =
        std::make_shared<LlrConfigMap>(std::move(newLlrConfigMap));
    return toMultiSwitchMap<MultiSwitchLlrConfigMap>(
        llrConfigMap, scopeResolver_);
  }
  return nullptr;
}

static void validateLlrConfig(
    const std::string& id,
    const cfg::LlrConfig& llrConfig) {
  // LlrConfig thrift fields are signed (thrift has no unsigned type) but have
  // tighter UE Spec 1.0.2 Table 5-9 ranges than their SAI attribute widths.
  // Reject out-of-range values loudly here instead of silently wrapping when
  // narrowed in SaiPortManager::programLlr, or failing later at SAI profile
  // create.
  auto checkRange =
      [&id](const char* field, int64_t value, int64_t minVal, int64_t maxVal) {
        if (value < minVal || value > maxVal) {
          throw FbossError(
              "LlrConfig \"",
              id,
              "\": ",
              field,
              "=",
              value,
              " is out of range [",
              minVal,
              ", ",
              maxVal,
              "]");
        }
      };
  // Bounds are the UE Spec 1.0.2 Table 5-9 limits (ASIC-agnostic). Several are
  // tighter than the underlying SAI attribute width -- e.g. replay_timer_max
  // and ctlos_target_spacing map to u32/u16 SAI attrs but the spec caps them
  // lower -- so we enforce the spec value, not the register width.
  constexpr int64_t kOutstandingFramesMin = 0;
  // outstanding_seq_max: spec absolute max 524288 (inclusive), i.e. half the
  // 2^20 sequence-number space. Implementations may support a lower max, which
  // is enforced at profile-bind time, not here.
  constexpr int64_t kOutstandingFramesMax = 524288;
  // outstanding_data_max: sized to the link bandwidth-delay product; the spec
  // gives no ceiling beyond the u32 register width.
  constexpr int64_t kOutstandingBytesMin = 0;
  constexpr int64_t kOutstandingBytesMax = std::numeric_limits<uint32_t>::max();
  // replay_timer_max: 16-bit nanosecond value.
  constexpr int64_t kReplayTimerMinNs = 0;
  constexpr int64_t kReplayTimerMaxNs = 65535;
  // replay_ct_max: u8 count; 255 means "no maximum".
  constexpr int64_t kReplayCountMin = 0;
  constexpr int64_t kReplayCountMax = 255;
  // pcs_lost_timeout / data_age_timeout: 32-bit nanosecond values; the spec
  // ceiling is the full u32 range (~4.29 s).
  constexpr int64_t kTimeoutMinNs = 0;
  constexpr int64_t kTimeoutMaxNs = std::numeric_limits<uint32_t>::max();
  // ctlos_target_spacing: valid range in bytes.
  constexpr int64_t kCtlosTargetSpacingMin = 400;
  constexpr int64_t kCtlosTargetSpacingMax = 16384;

  checkRange(
      "outstandingFramesMax",
      *llrConfig.outstandingFramesMax(),
      kOutstandingFramesMin,
      kOutstandingFramesMax);
  checkRange(
      "outstandingBytesMax",
      *llrConfig.outstandingBytesMax(),
      kOutstandingBytesMin,
      kOutstandingBytesMax);
  checkRange(
      "replayTimerMax",
      *llrConfig.replayTimerMax(),
      kReplayTimerMinNs,
      kReplayTimerMaxNs);
  checkRange(
      "replayCountMax",
      *llrConfig.replayCountMax(),
      kReplayCountMin,
      kReplayCountMax);
  checkRange(
      "pcsLostTimeout",
      *llrConfig.pcsLostTimeout(),
      kTimeoutMinNs,
      kTimeoutMaxNs);
  checkRange(
      "dataAgeTimeout",
      *llrConfig.dataAgeTimeout(),
      kTimeoutMinNs,
      kTimeoutMaxNs);
  checkRange(
      "ctlosTargetSpacing",
      *llrConfig.ctlosTargetSpacing(),
      kCtlosTargetSpacingMin,
      kCtlosTargetSpacingMax);
}

std::shared_ptr<LlrConfig> ThriftConfigApplier::createLlrConfig(
    const std::string& id,
    const cfg::LlrConfig& llrConfig) {
  validateLlrConfig(id, llrConfig);
  auto cfg = std::make_shared<LlrConfig>(id);
  cfg->setOutstandingFramesMax(*llrConfig.outstandingFramesMax());
  cfg->setOutstandingBytesMax(*llrConfig.outstandingBytesMax());
  cfg->setReplayTimerMax(*llrConfig.replayTimerMax());
  cfg->setReplayCountMax(*llrConfig.replayCountMax());
  cfg->setPcsLostTimeout(*llrConfig.pcsLostTimeout());
  cfg->setDataAgeTimeout(*llrConfig.dataAgeTimeout());
  cfg->setInitFrameAction(*llrConfig.initFrameAction());
  cfg->setFlushFrameAction(*llrConfig.flushFrameAction());
  cfg->setReInitOnFlush(*llrConfig.reInitOnFlush());
  cfg->setCtlosTargetSpacing(*llrConfig.ctlosTargetSpacing());
  return cfg;
}

shared_ptr<MultiSwitchBufferPoolCfgMap>
ThriftConfigApplier::updateBufferPoolConfigs(bool* changed) {
  *changed = false;
  auto origBufferPoolConfigs = orig_->getBufferPoolCfgs();
  BufferPoolCfgMap::NodeContainer newBufferPoolConfigMap;
  auto newCfgedBufferPools = cfg_->bufferPoolConfigs();

  if (!newCfgedBufferPools && !origBufferPoolConfigs->numNodes()) {
    return nullptr;
  }

  if (!newCfgedBufferPools && origBufferPoolConfigs->numNodes()) {
    // old cfg eixists but new one doesn't
    *changed = true;
    return std::make_shared<MultiSwitchBufferPoolCfgMap>();
  }

  if (newCfgedBufferPools && !origBufferPoolConfigs->numNodes()) {
    *changed = true;
  }

  // if old/new cfgs are present, compare size
  if (origBufferPoolConfigs->numNodes() != (*newCfgedBufferPools).size()) {
    *changed = true;
  }

  // origBufferPoolConfigs, newBufferPoolConfigs both are configured
  // and with with same size
  // check if there is any update on it when compared
  // with last one
  for (auto& bufferPoolConfig : *newCfgedBufferPools) {
    auto newBufferPoolConfig =
        createBufferPoolConfig(bufferPoolConfig.first, bufferPoolConfig.second);
    // if buffer pool cfg map exist, check if the specific buffer pool cfg
    // exists or not
    auto origBufferPoolConfig =
        origBufferPoolConfigs->getNodeIf(bufferPoolConfig.first);
    if (!origBufferPoolConfig ||
        (*origBufferPoolConfig != *newBufferPoolConfig)) {
      /* new entry added or existing entries do not match */
      *changed = true;
    }
    newBufferPoolConfigMap.emplace(
        std::make_pair(bufferPoolConfig.first, newBufferPoolConfig));
  }

  if (*changed) {
    auto bufferPoolConfigMap =
        std::make_shared<BufferPoolCfgMap>(std::move(newBufferPoolConfigMap));
    return toMultiSwitchMap<MultiSwitchBufferPoolCfgMap>(
        bufferPoolConfigMap, scopeResolver_);
  }
  return nullptr;
}

std::shared_ptr<BufferPoolCfg> ThriftConfigApplier::createBufferPoolConfig(
    const std::string& id,
    const cfg::BufferPoolConfig& bufferPoolConfig) {
  auto bufferPoolCfg = std::make_shared<BufferPoolCfg>(id);
  bufferPoolCfg->setSharedBytes(*bufferPoolConfig.sharedBytes());
  if (bufferPoolConfig.headroomBytes().has_value()) {
    bufferPoolCfg->setHeadroomBytes(*bufferPoolConfig.headroomBytes());
  }
  if (bufferPoolConfig.reservedBytes().has_value()) {
    bufferPoolCfg->setReservedBytes(*bufferPoolConfig.reservedBytes());
  }

  return bufferPoolCfg;
}

shared_ptr<QcmCfg> ThriftConfigApplier::updateQcmCfg(bool* changed) {
  auto origQcmConfig = orig_->getQcmCfg();
  if (!cfg_->qcmConfig()) {
    if (origQcmConfig) {
      // going from cfg to empty
      *changed = true;
    }
    return nullptr;
  }
  auto newQcmCfg = createQcmCfg(*cfg_->qcmConfig());
  if (origQcmConfig && (*origQcmConfig == *newQcmCfg)) {
    return nullptr;
  }
  *changed = true;
  return newQcmCfg;
}

shared_ptr<FlowletSwitchingConfig>
ThriftConfigApplier::updateFlowletSwitchingConfig(bool* changed) {
  auto origFlowletSwitchingConfig = orig_->getFlowletSwitchingConfig();
  if (!cfg_->flowletSwitchingConfig()) {
    if (origFlowletSwitchingConfig) {
      // going from cfg to empty
      *changed = true;
    }
    return nullptr;
  }
  auto newFlowletSwitchingConfig =
      createFlowletSwitchingConfig(*cfg_->flowletSwitchingConfig());
  if (origFlowletSwitchingConfig &&
      (*origFlowletSwitchingConfig == *newFlowletSwitchingConfig)) {
    return nullptr;
  }
  *changed = true;
  return newFlowletSwitchingConfig;
}

shared_ptr<MultiSwitchSettings>
ThriftConfigApplier::updateMultiSwitchSettings() {
  bool multiSwitchSettingsChange = false;
  auto origMultiSwitchSettings = orig_->getSwitchSettings();
  auto newMultiSwitchSettings = origMultiSwitchSettings
      ? origMultiSwitchSettings->clone()
      : std::make_shared<MultiSwitchSettings>();

  origMultiSwitchSettings = origMultiSwitchSettings
      ? origMultiSwitchSettings
      : std::make_shared<MultiSwitchSettings>();

  if (scopeResolver_.switchIdToSwitchInfo().size() == 0) {
    throw FbossError("SwitchIdToSwitchInfo cannot be empty");
  }

  for (auto& switchIdAndSwitchInfo : scopeResolver_.switchIdToSwitchInfo()) {
    auto switchId = switchIdAndSwitchInfo.first;
    auto matcher = HwSwitchMatcher(
        std::unordered_set<SwitchID>({static_cast<SwitchID>(switchId)}));

    auto origSwitchSettings =
        origMultiSwitchSettings->getNodeIf(matcher.matcherString());

    // If origmultiSwitchSettings is already populated, and if config
    // carries switchId that is not present in origMultiSwitchSettings,
    // throw error
    if (origMultiSwitchSettings->size() != 0) {
      if (!origSwitchSettings) {
        throw FbossError("SwitchId cannot be changed on the fly");
      }
    }

    auto newSwitchSettings =
        updateSwitchSettings(matcher, origMultiSwitchSettings);
    if (newSwitchSettings) {
      if (origSwitchSettings) {
        newMultiSwitchSettings->updateNode(
            matcher.matcherString(), newSwitchSettings);
      } else {
        newMultiSwitchSettings->addNode(
            matcher.matcherString(), newSwitchSettings);
      }
      multiSwitchSettingsChange = true;
    }
  }

  if (multiSwitchSettingsChange) {
    if (newMultiSwitchSettings->empty()) {
      throw FbossError("SwitchSettings cannot be empty");
    }
    return newMultiSwitchSettings;
  }

  return nullptr;
}

shared_ptr<SwitchSettings> ThriftConfigApplier::updateSwitchSettings(
    HwSwitchMatcher matcher,
    const std::shared_ptr<MultiSwitchSettings>& origMultiSwitchSettings) {
  auto origSwitchSettings =
      origMultiSwitchSettings->getNodeIf(matcher.matcherString());
  bool switchSettingsChange = false;
  auto newSwitchSettings = origSwitchSettings
      ? origSwitchSettings->clone()
      : std::make_shared<SwitchSettings>();

  origSwitchSettings = origSwitchSettings ? origSwitchSettings
                                          : std::make_shared<SwitchSettings>();

  if (origSwitchSettings->getL2LearningMode() !=
      *cfg_->switchSettings()->l2LearningMode()) {
    newSwitchSettings->setL2LearningMode(
        *cfg_->switchSettings()->l2LearningMode());
    switchSettingsChange = true;
  }

  if (origSwitchSettings->isQcmEnable() !=
      *cfg_->switchSettings()->qcmEnable()) {
    newSwitchSettings->setQcmEnable(*cfg_->switchSettings()->qcmEnable());
    switchSettingsChange = true;
  }

  if (origSwitchSettings->isPtpTcEnable() !=
      *cfg_->switchSettings()->ptpTcEnable()) {
    newSwitchSettings->setPtpTcEnable(*cfg_->switchSettings()->ptpTcEnable());
    switchSettingsChange = true;
  }

  {
    auto oldMode = origSwitchSettings->getPacketForwardingMode();
    auto newModeRef = cfg_->switchSettings()->packetForwardingMode();
    std::optional<cfg::PacketForwardingMode> newMode = newModeRef.has_value()
        ? std::optional<cfg::PacketForwardingMode>(*newModeRef)
        : std::nullopt;
    if (oldMode != newMode) {
      newSwitchSettings->setPacketForwardingMode(newMode);
      switchSettingsChange = true;
    }
  }

  if (origSwitchSettings->getL2AgeTimerSeconds() !=
      *cfg_->switchSettings()->l2AgeTimerSeconds()) {
    newSwitchSettings->setL2AgeTimerSeconds(
        *cfg_->switchSettings()->l2AgeTimerSeconds());
    switchSettingsChange = true;
  }

  std::vector<state::BlockedNeighbor> cfgBlockNeighbors;
  for (const auto& blockNeighbor : *cfg_->switchSettings()->blockNeighbors()) {
    state::BlockedNeighbor neighbor{};
    neighbor.blockNeighborVlanID() = *blockNeighbor.vlanID();
    neighbor.blockNeighborIP() =
        network::toBinaryAddress(folly::IPAddress(*blockNeighbor.ipAddress()));
    cfgBlockNeighbors.emplace_back(neighbor);
  }

  std::vector<std::pair<VlanID, folly::MacAddress>> cfgMacAddrsToBlock;
  for (const auto& macAddrToBlock :
       *cfg_->switchSettings()->macAddrsToBlock()) {
    cfgMacAddrsToBlock.emplace_back(
        VlanID(*macAddrToBlock.vlanID()),
        folly::MacAddress(*macAddrToBlock.macAddress()));
  }
  if (origSwitchSettings->getMacAddrsToBlock_DEPRECATED() !=
      cfgMacAddrsToBlock) {
    newSwitchSettings->setMacAddrsToBlock(cfgMacAddrsToBlock);
    switchSettingsChange = true;
  }

  if (origSwitchSettings->getVendorMacOuis()->toThrift() !=
      *cfg_->switchSettings()->vendorMacOuis()) {
    newSwitchSettings->setVendorMacOuis(
        *cfg_->switchSettings()->vendorMacOuis());
    switchSettingsChange = true;
  }
  if (origSwitchSettings->getMetaMacOuis()->toThrift() !=
      *cfg_->switchSettings()->metaMacOuis()) {
    newSwitchSettings->setMetaMacOuis(*cfg_->switchSettings()->metaMacOuis());
    switchSettingsChange = true;
  }

  auto defaultVoqConfig = getDefaultVoqConfigIfChanged(origSwitchSettings);
  if (defaultVoqConfig.has_value()) {
    newSwitchSettings->setDefaultVoqConfig(*defaultVoqConfig);
    switchSettingsChange = true;
  }

  // TODO - Disallow changing any switchInfo parameter after first
  // config apply. Currently we check only switchId and SwitchType
  // This is to allow rollout of new parameters - portIdRange and
  // switchIndex without breaking warmboot
  auto validateSwitchIdToSwitchInfoChange =
      [](const auto& oldSwitchIdToSwitchInfo,
         const auto& newSwitchIdToSwitchInfo) {
        if (oldSwitchIdToSwitchInfo.size() != newSwitchIdToSwitchInfo.size()) {
          return false;
        }
        for (const auto& switchIdAndInfo : newSwitchIdToSwitchInfo) {
          const auto switchId = switchIdAndInfo.first;
          const auto& switchInfo = switchIdAndInfo.second;
          // Disallow SwitchId and SwitchType changes
          if (oldSwitchIdToSwitchInfo.find(switchId) ==
                  oldSwitchIdToSwitchInfo.end() ||
              switchInfo.switchType() !=
                  oldSwitchIdToSwitchInfo.at(switchId).switchType()) {
            return false;
          }
        }
        return true;
      };

  SwitchIdToSwitchInfo switchIdToSwitchInfo = getSwitchInfoFromConfig(cfg_);
  if (origSwitchSettings->getSwitchIdToSwitchInfo() != switchIdToSwitchInfo) {
    if (origSwitchSettings->getSwitchIdToSwitchInfo().size() &&
        !validateSwitchIdToSwitchInfoChange(
            origSwitchSettings->getSwitchIdToSwitchInfo(),
            switchIdToSwitchInfo)) {
      throw FbossError(
          "SwitchId and SwitchInfo type cannot be changed on the fly");
    }
    newSwitchSettings->setSwitchIdToSwitchInfo(switchIdToSwitchInfo);
    switchSettingsChange = true;
  }

  // computeActualSwitchDrainState relies on minLinksToRemainInVOQDomain and
  // minLinksToJoinVOQDomain. Thus, setting these fields must precede call
  // to computeActualSwitchDrainState.
  std::optional<int32_t> newMinLinksToRemainInVOQDomain{std::nullopt};
  if (cfg_->switchSettings()->minLinksToRemainInVOQDomain()) {
    if (newSwitchSettings->getSwitchIdsOfType(cfg::SwitchType::VOQ).size() ==
        0) {
      throw FbossError(
          "Min links to remain in VOQ Domain is supported only for VOQ switches");
    }

    newMinLinksToRemainInVOQDomain =
        *cfg_->switchSettings()->minLinksToRemainInVOQDomain();
  }
  if (origSwitchSettings->getMinLinksToRemainInVOQDomain() !=
      newMinLinksToRemainInVOQDomain) {
    newSwitchSettings->setMinLinksToRemainInVOQDomain(
        newMinLinksToRemainInVOQDomain);
    switchSettingsChange = true;
  }

  std::optional<int32_t> newMinLinksToJoinVOQDomain{std::nullopt};
  if (cfg_->switchSettings()->minLinksToJoinVOQDomain()) {
    if (newSwitchSettings->getSwitchIdsOfType(cfg::SwitchType::VOQ).size() ==
        0) {
      throw FbossError(
          "Min links to join VOQ Domain is supported only for VOQ switches");
    }

    newMinLinksToJoinVOQDomain =
        *cfg_->switchSettings()->minLinksToJoinVOQDomain();
  }
  if (origSwitchSettings->getMinLinksToJoinVOQDomain() !=
      newMinLinksToJoinVOQDomain) {
    newSwitchSettings->setMinLinksToJoinVOQDomain(newMinLinksToJoinVOQDomain);
    switchSettingsChange = true;
  }

  std::optional<uint8_t> newSramGlobalFreePercentXoffThreshold;
  if (cfg_->switchSettings()->sramGlobalFreePercentXoffThreshold()) {
    newSramGlobalFreePercentXoffThreshold =
        *cfg_->switchSettings()->sramGlobalFreePercentXoffThreshold();
  }
  if (newSramGlobalFreePercentXoffThreshold !=
      origSwitchSettings->getSramGlobalFreePercentXoffThreshold()) {
    newSwitchSettings->setSramGlobalFreePercentXoffThreshold(
        newSramGlobalFreePercentXoffThreshold);
    switchSettingsChange = true;
  }

  std::optional<uint8_t> newSramGlobalFreePercentXonThreshold;
  if (cfg_->switchSettings()->sramGlobalFreePercentXonThreshold()) {
    newSramGlobalFreePercentXonThreshold =
        *cfg_->switchSettings()->sramGlobalFreePercentXonThreshold();
  }
  if (newSramGlobalFreePercentXonThreshold !=
      origSwitchSettings->getSramGlobalFreePercentXonThreshold()) {
    newSwitchSettings->setSramGlobalFreePercentXonThreshold(
        newSramGlobalFreePercentXonThreshold);
    switchSettingsChange = true;
  }

  std::optional<uint16_t> newLinkFlowControlCreditThreshold;
  if (cfg_->switchSettings()->linkFlowControlCreditThreshold()) {
    newLinkFlowControlCreditThreshold =
        *cfg_->switchSettings()->linkFlowControlCreditThreshold();
  }
  if (newLinkFlowControlCreditThreshold !=
      origSwitchSettings->getLinkFlowControlCreditThreshold()) {
    newSwitchSettings->setLinkFlowControlCreditThreshold(
        newLinkFlowControlCreditThreshold);
    switchSettingsChange = true;
  }

  std::optional<uint32_t> newVoqDramBoundThreshold;
  if (cfg_->switchSettings()->voqDramBoundThreshold()) {
    newVoqDramBoundThreshold = *cfg_->switchSettings()->voqDramBoundThreshold();
  }
  if (newVoqDramBoundThreshold !=
      origSwitchSettings->getVoqDramBoundThreshold()) {
    newSwitchSettings->setVoqDramBoundThreshold(newVoqDramBoundThreshold);
    switchSettingsChange = true;
  }

  std::optional<int> newConditionalEntropyRehashPeriodUS;
  if (cfg_->switchSettings()->conditionalEntropyRehashPeriodUS()) {
    newConditionalEntropyRehashPeriodUS =
        *cfg_->switchSettings()->conditionalEntropyRehashPeriodUS();
  }
  if (newConditionalEntropyRehashPeriodUS !=
      origSwitchSettings->getConditionalEntropyRehashPeriodUS()) {
    newSwitchSettings->setConditionalEntropyRehashPeriodUS(
        newConditionalEntropyRehashPeriodUS);
    switchSettingsChange = true;
  }

  std::optional<int32_t> newLocalVoqMaxExpectedLatencyNsec;
  if (cfg_->switchSettings()->localVoqMaxExpectedLatencyNsec()) {
    newLocalVoqMaxExpectedLatencyNsec =
        *cfg_->switchSettings()->localVoqMaxExpectedLatencyNsec();
  }
  if (newLocalVoqMaxExpectedLatencyNsec !=
      origSwitchSettings->getLocalVoqMaxExpectedLatencyNsec()) {
    newSwitchSettings->setLocalVoqMaxExpectedLatencyNsec(
        newLocalVoqMaxExpectedLatencyNsec);
    switchSettingsChange = true;
  }

  std::optional<int32_t> newRemoteL1VoqMaxExpectedLatencyNsec;
  if (cfg_->switchSettings()->remoteL1VoqMaxExpectedLatencyNsec()) {
    newRemoteL1VoqMaxExpectedLatencyNsec =
        *cfg_->switchSettings()->remoteL1VoqMaxExpectedLatencyNsec();
  }
  if (newRemoteL1VoqMaxExpectedLatencyNsec !=
      origSwitchSettings->getRemoteL1VoqMaxExpectedLatencyNsec()) {
    newSwitchSettings->setRemoteL1VoqMaxExpectedLatencyNsec(
        newRemoteL1VoqMaxExpectedLatencyNsec);
    switchSettingsChange = true;
  }

  std::optional<int32_t> newRemoteL2VoqMaxExpectedLatencyNsec;
  if (cfg_->switchSettings()->remoteL2VoqMaxExpectedLatencyNsec()) {
    newRemoteL2VoqMaxExpectedLatencyNsec =
        *cfg_->switchSettings()->remoteL2VoqMaxExpectedLatencyNsec();
  }
  if (newRemoteL2VoqMaxExpectedLatencyNsec !=
      origSwitchSettings->getRemoteL2VoqMaxExpectedLatencyNsec()) {
    newSwitchSettings->setRemoteL2VoqMaxExpectedLatencyNsec(
        newRemoteL2VoqMaxExpectedLatencyNsec);
    switchSettingsChange = true;
  }

  std::optional<int32_t> newVoqOutOfBoundsLatencyNsec;
  if (cfg_->switchSettings()->voqOutOfBoundsLatencyNsec()) {
    newVoqOutOfBoundsLatencyNsec =
        *cfg_->switchSettings()->voqOutOfBoundsLatencyNsec();
  }
  if (newVoqOutOfBoundsLatencyNsec !=
      origSwitchSettings->getVoqOutOfBoundsLatencyNsec()) {
    newSwitchSettings->setVoqOutOfBoundsLatencyNsec(
        newVoqOutOfBoundsLatencyNsec);
    switchSettingsChange = true;
  }
  std::optional<std::map<int32_t, int32_t>> newTcToRateLimitKbps;
  if (cfg_->switchSettings()->tcToRateLimitKbps()) {
    newTcToRateLimitKbps = *cfg_->switchSettings()->tcToRateLimitKbps();
  }
  if (newTcToRateLimitKbps != origSwitchSettings->getTcToRateLimitKbps()) {
    newSwitchSettings->setTcToRateLimitKbps(newTcToRateLimitKbps);
    switchSettingsChange = true;
  }

  if (origSwitchSettings->getSwitchDrainState() !=
      *cfg_->switchSettings()->switchDrainState()) {
    newSwitchSettings->setSwitchDrainState(
        *cfg_->switchSettings()->switchDrainState());
    switchSettingsChange = true;
  }

  auto newActualSwitchDrainState = computeActualSwitchDrainState(
      newSwitchSettings, getNumActiveFabricPorts(orig_, matcher));
  if (newActualSwitchDrainState !=
      origSwitchSettings->getActualSwitchDrainState()) {
    newSwitchSettings->setActualSwitchDrainState(newActualSwitchDrainState);
    switchSettingsChange = true;
  }

  auto originalExactMatchTableConfig =
      origSwitchSettings->getExactMatchTableConfig();
  // THRIFT_COPY
  if (originalExactMatchTableConfig->toThrift() !=
      *cfg_->switchSettings()->exactMatchTableConfigs()) {
    if (cfg_->switchSettings()->exactMatchTableConfigs()->size() > 1) {
      throw FbossError("Multiple EM tables not supported yet");
    }
    newSwitchSettings->setExactMatchTableConfig(
        *cfg_->switchSettings()->exactMatchTableConfigs());
    switchSettingsChange = true;
  }

  VlanID defaultVlan(*cfg_->defaultVlan());
  if (orig_->getDefaultVlan() != defaultVlan) {
    newSwitchSettings->setDefaultVlan(defaultVlan);
    switchSettingsChange = true;
  }

  std::chrono::seconds arpTimeout(*cfg_->arpTimeoutSeconds());
  if (orig_->getArpTimeout() != arpTimeout) {
    newSwitchSettings->setArpTimeout(arpTimeout);

    // TODO: add ndpTimeout field to SwitchConfig. For now use the same
    // timeout for both ARP and NDP
    newSwitchSettings->setNdpTimeout(arpTimeout);
    switchSettingsChange = true;
  }

  std::chrono::seconds staleEntryInterval(*cfg_->staleEntryInterval());
  if (orig_->getStaleEntryInterval() != staleEntryInterval) {
    newSwitchSettings->setStaleEntryInterval(staleEntryInterval);
    switchSettingsChange = true;
  }

  std::chrono::seconds arpAgerInterval(*cfg_->arpAgerInterval());
  if (orig_->getArpAgerInterval() != arpAgerInterval) {
    newSwitchSettings->setArpAgerInterval(arpAgerInterval);
    switchSettingsChange = true;
  }

  uint32_t maxNeighborProbes(*cfg_->maxNeighborProbes());
  if (orig_->getMaxNeighborProbes() != maxNeighborProbes) {
    newSwitchSettings->setMaxNeighborProbes(maxNeighborProbes);
    switchSettingsChange = true;
  }

  auto oldDhcpV4RelaySrc = orig_->getDhcpV4RelaySrc();
  auto newDhcpV4RelaySrc = cfg_->dhcpRelaySrcOverrideV4()
      ? IPAddressV4(*cfg_->dhcpRelaySrcOverrideV4())
      : IPAddressV4();
  if (oldDhcpV4RelaySrc != newDhcpV4RelaySrc) {
    newSwitchSettings->setDhcpV4RelaySrc(newDhcpV4RelaySrc);
    switchSettingsChange = true;
  }

  auto oldDhcpV6RelaySrc = orig_->getDhcpV6RelaySrc();
  auto newDhcpV6RelaySrc = cfg_->dhcpRelaySrcOverrideV6()
      ? IPAddressV6(*cfg_->dhcpRelaySrcOverrideV6())
      : IPAddressV6("::");
  if (oldDhcpV6RelaySrc != newDhcpV6RelaySrc) {
    newSwitchSettings->setDhcpV6RelaySrc(newDhcpV6RelaySrc);
    switchSettingsChange = true;
  }

  auto oldDhcpV4ReplySrc = orig_->getDhcpV4ReplySrc();
  auto newDhcpV4ReplySrc = cfg_->dhcpReplySrcOverrideV4()
      ? IPAddressV4(*cfg_->dhcpReplySrcOverrideV4())
      : IPAddressV4();
  if (oldDhcpV4ReplySrc != newDhcpV4ReplySrc) {
    newSwitchSettings->setDhcpV4ReplySrc(newDhcpV4ReplySrc);
    switchSettingsChange = true;
  }

  auto oldDhcpV6ReplySrc = orig_->getDhcpV6ReplySrc();
  auto newDhcpV6ReplySrc = cfg_->dhcpReplySrcOverrideV6()
      ? IPAddressV6(*cfg_->dhcpReplySrcOverrideV6())
      : IPAddressV6("::");
  if (oldDhcpV6ReplySrc != newDhcpV6ReplySrc) {
    newSwitchSettings->setDhcpV6ReplySrc(newDhcpV6ReplySrc);
    switchSettingsChange = true;
  }

  auto oldIcmpV4UnavailableSrcAddress = orig_->getIcmpV4UnavailableSrcAddress();
  auto newIcmpV4UnavailableSrcAddress = cfg_->icmpV4UnavailableSrcAddress();
  if (newIcmpV4UnavailableSrcAddress.has_value()) {
    auto newIcmpV4Address = IPAddressV4(*newIcmpV4UnavailableSrcAddress);
    if (newIcmpV4Address != oldIcmpV4UnavailableSrcAddress) {
      newSwitchSettings->setIcmpV4UnavailableSrcAddress(newIcmpV4Address);
      switchSettingsChange = true;
    }
  }

  auto oldHostname = orig_->getHostname();
  auto newHostname = cfg_->hostname() && !(*cfg_->hostname()).empty()
      ? *cfg_->hostname()
      : getLocalHostname();
  if (oldHostname != newHostname) {
    newSwitchSettings->setHostname(newHostname);
    switchSettingsChange = true;
  }

  {
    bool qcmChanged = false;
    auto newQcmConfig = updateQcmCfg(&qcmChanged);
    if (qcmChanged) {
      newSwitchSettings->setQcmCfg(newQcmConfig);
      switchSettingsChange = true;
    }
  }

  {
    auto newDefaultQosPolicy = updateDataplaneDefaultQosPolicy();
    if (new_->getDefaultDataPlaneQosPolicy() != newDefaultQosPolicy) {
      newSwitchSettings->setDefaultDataPlaneQosPolicy(newDefaultQosPolicy);
      switchSettingsChange = true;
    }
  }

  {
    bool udfCfgChanged = false;
    auto newUdfCfg = updateUdfConfig(&udfCfgChanged);
    if (udfCfgChanged) {
      newSwitchSettings->setUdfConfig(std::move(newUdfCfg));
      switchSettingsChange = true;
    }
  }

  {
    bool flowletSwitchingChanged = false;
    auto newFlowletSwitchingConfig =
        updateFlowletSwitchingConfig(&flowletSwitchingChanged);
    if (flowletSwitchingChanged) {
      newSwitchSettings->setFlowletSwitchingConfig(
          std::move(newFlowletSwitchingConfig));
      switchSettingsChange = true;
    }
  }

  std::optional<std::string> newFirmwarePath;
  if (cfg_->switchSettings()->firmwarePath()) {
    newFirmwarePath = *cfg_->switchSettings()->firmwarePath();
  }
  if (newFirmwarePath != origSwitchSettings->getFirmwarePath()) {
    newSwitchSettings->setFirmwarePath(newFirmwarePath);
    switchSettingsChange = true;
  }

  {
    // SelfHealingEcmpLag switch configurations
    std::optional<cfg::SelfHealingEcmpLagConfig> newSwitchShelConfig;
    if (cfg_->switchSettings()->selfHealingEcmpLagConfig()) {
      newSwitchShelConfig = *cfg_->switchSettings()->selfHealingEcmpLagConfig();
    }
    if (newSwitchShelConfig !=
        origSwitchSettings->getSelfHealingEcmpLagConfig()) {
      newSwitchSettings->setSelfHealingEcmpLagConfig(newSwitchShelConfig);
      updateSystemPortSelfHealingEcmpLagDestinationEnable(
          newSwitchShelConfig.has_value());
      switchSettingsChange = true;
    }
  }

  {
    std::optional<int32_t> newPfcWatchdogTimerGranularity;
    if (cfg_->switchSettings()->pfcWatchdogTimerGranularityMsec()) {
      newPfcWatchdogTimerGranularity =
          *cfg_->switchSettings()->pfcWatchdogTimerGranularityMsec();
    }
    if (newPfcWatchdogTimerGranularity !=
        origSwitchSettings->getPfcWatchdogTimerGranularity()) {
      newSwitchSettings->setPfcWatchdogTimerGranularity(
          newPfcWatchdogTimerGranularity);
      switchSettingsChange = true;
    }
  }
  {
    std::optional<int32_t> newEcmpCompressionThresholdPct;
    if (cfg_->switchSettings()->ecmpCompressionThresholdPct()) {
      newEcmpCompressionThresholdPct =
          *cfg_->switchSettings()->ecmpCompressionThresholdPct();
    }
    if (newEcmpCompressionThresholdPct !=
        origSwitchSettings->getEcmpCompressionThresholdPct()) {
      newSwitchSettings->setEcmpCompressionThresholdPct(
          newEcmpCompressionThresholdPct);
      switchSettingsChange = true;
    }
  }
  // Source ecmpWidth from cfg.SwitchSettings, falling back to FLAGS_ecmp_width
  // during the flag->config migration. Changing it requires a coldboot; see
  // StateUpdateValidator.
  {
    const auto& configEcmpWidth = cfg_->switchSettings()->ecmpWidth();
    const auto flagEcmpWidth = static_cast<int32_t>(FLAGS_ecmp_width);
    const auto newEcmpWidth =
        configEcmpWidth.has_value() ? *configEcmpWidth : flagEcmpWidth;
    if (newEcmpWidth <= 0) {
      throw FbossError("ECMP width must be positive, got ", newEcmpWidth);
    }
    // During the flag->config migration both knobs can be set. Config wins;
    // warn on a mismatch so the precedence isn't silent. Remove once
    // FLAGS_ecmp_width is retired.
    if (configEcmpWidth.has_value() && newEcmpWidth != flagEcmpWidth) {
      XLOG(WARN) << "Config SwitchSettings.ecmpWidth (" << newEcmpWidth
                 << ") differs from FLAGS_ecmp_width (" << FLAGS_ecmp_width
                 << "); config value takes precedence";
    }
    if (origSwitchSettings->getEcmpWidth() != newEcmpWidth) {
      newSwitchSettings->setEcmpWidth(newEcmpWidth);
      switchSettingsChange = true;
    }
  }
  {
    std::optional<int32_t> fabricLinkMonitoringSystemPortOffset;
    if (cfg_->switchSettings()->fabricLinkMonitoringSystemPortOffset()) {
      fabricLinkMonitoringSystemPortOffset =
          *cfg_->switchSettings()->fabricLinkMonitoringSystemPortOffset();
    }
    if (fabricLinkMonitoringSystemPortOffset !=
        origSwitchSettings->getFabricLinkMonitoringSystemPortOffset()) {
      newSwitchSettings->setFabricLinkMonitoringSystemPortOffset(
          fabricLinkMonitoringSystemPortOffset);
      switchSettingsChange = true;
    }
  }
  {
    EcmpGroupSettingsMap ecmpGroupSettings =
        *cfg_->switchSettings()->ecmpGroupSettings();
    // Split horizon on an FRR parent arms the same-src-dst port check while the
    // backup provides the tertiary path that catches the suppressed traffic.
    // Enabling one without the other drops the flows the check suppresses, so
    // refuse the config rather than program it.
    auto enabled = [&](cfg::EcmpGroupType type) {
      auto it = ecmpGroupSettings.find(type);
      return it != ecmpGroupSettings.end() && *it->second.enableSplitHorizon();
    };
    if (enabled(cfg::EcmpGroupType::FRR_PRIMARY) !=
        enabled(cfg::EcmpGroupType::FRR_BACKUP)) {
      throw FbossError(
          "ecmpGroupSettings: FRR_PRIMARY and FRR_BACKUP must enable "
          "split horizon together; enabling it on the primary alone "
          "suppresses the source port with no tertiary path on the backup");
    }
    if (ecmpGroupSettings != origSwitchSettings->getEcmpGroupSettings()) {
      newSwitchSettings->setEcmpGroupSettings(ecmpGroupSettings);
      switchSettingsChange = true;
    }
  }
  {
    const auto& myMacs = *cfg_->switchSettings()->myMacs();
    if (!myMacs.empty() &&
        !hwAsicTable_->isFeatureSupportedOnAllAsic(HwAsic::Feature::MY_MAC)) {
      throw FbossError(
          "myMacs (MY_MAC) is not supported on ",
          folly::join(" ", hwAsicTable_->asicNames()));
    }
    for (const auto& myMac : myMacs) {
      if (!folly::MacAddress::tryFromString(*myMac.macAddress()).hasValue()) {
        throw FbossError("Invalid myMacs MAC address: ", *myMac.macAddress());
      }
    }
    if (myMacs != origSwitchSettings->getMyMacs()) {
      newSwitchSettings->setMyMacs(myMacs);
      switchSettingsChange = true;
    }
  }

  if (switchSettingsChange) {
    return newSwitchSettings;
  }

  return nullptr;
}

shared_ptr<MultiControlPlane> ThriftConfigApplier::updateControlPlane() {
  if (!hwAsicTable_->isFeatureSupportedOnAnyAsic(HwAsic::Feature::CPU_PORT)) {
    if (cfg_->cpuTrafficPolicy()) {
      throw FbossError(
          "No member of ASIC list supports CPU port ",
          folly::join<std::string, std::vector<std::string>>(
              " ", hwAsicTable_->asicNames()));
    }
    return nullptr;
  }

  if (!hwAsicTable_->isFeatureSupportedOnAnyAsic(HwAsic::Feature::CPU_QUEUES)) {
    // If a switch supports a CPU port but does not support CPU queues,
    // then the following configurations should NOT be present:
    // - cpuTrafficPolicy
    // - cpuQueues
    // - cpuVoqs
    if (cfg_->cpuTrafficPolicy().has_value() ||
        (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
             cfg_->cpuQueues()) &&
         !cfg_->cpuQueues()->empty()) ||
        (cfg_->cpuVoqs().has_value() && !cfg_->cpuVoqs()->empty())) {
      throw FbossError(
          "Without CPU queues, cpuTrafficPolicy, cpuQueues, or cpuVoqs "
          "configuration cannot be applied");
    }
    // For these switches, a minimal ControlPlane without queues or
    // policies is sufficient. Since there's no config to apply,
    // return nullptr to indicate no change.
    return nullptr;
  }

  auto multiSwitchControlPlane = orig_->getControlPlane();
  CHECK_LE(multiSwitchControlPlane->size(), 1);
  auto origCPU = multiSwitchControlPlane->size()
      ? multiSwitchControlPlane->cbegin()->second
      : std::make_shared<ControlPlane>();
  std::optional<std::string> qosPolicy;
  ControlPlane::RxReasonToQueue newRxReasonToQueue;
  bool rxReasonToQueueUnchanged = true;
  if (auto cpuTrafficPolicy = cfg_->cpuTrafficPolicy()) {
    if (auto trafficPolicy = cpuTrafficPolicy->trafficPolicy()) {
      if (auto defaultQosPolicy = trafficPolicy->defaultQosPolicy()) {
        qosPolicy = *defaultQosPolicy;
      }
    }
    if (const auto rxReasonToQueue =
            cpuTrafficPolicy->rxReasonToQueueOrderedList()) {
      if (!isValidRxReasonToQueue(rxReasonToQueue)) {
        throw FbossError("Invalid RxReasonToQueueOrderedList specified");
      }
      for (auto rxEntry : *rxReasonToQueue) {
        newRxReasonToQueue.push_back(rxEntry);
      }
      // THRIFT_COPY
      if (newRxReasonToQueue != origCPU->getRxReasonToQueue()->toThrift()) {
        rxReasonToQueueUnchanged = false;
      }
    } else if (
        const auto rxReasonToCPUQueue =
            cpuTrafficPolicy->rxReasonToCPUQueue()) {
      // TODO(pgardideh): the map version of reason to queue is deprecated.
      // Remove
      // this read when it is safe to do so.
      for (auto rxEntry : *rxReasonToCPUQueue) {
        newRxReasonToQueue.push_back(
            ControlPlane::makeRxReasonToQueueEntry(
                rxEntry.first, rxEntry.second));
      }
      // THRIFT_COPY
      if (newRxReasonToQueue != origCPU->getRxReasonToQueue()->toThrift()) {
        rxReasonToQueueUnchanged = false;
      }
    }
  } else {
    /*
     * if cpuTrafficPolicy is not configured default to
     * dataPlaneTrafficPolicy default i.e. with regards to QoS map
     * configuration, treat CPU port like any front panel port.
     */
    if (auto dataPlaneTrafficPolicy = cfg_->dataPlaneTrafficPolicy()) {
      if (auto defaultDataPlaneQosPolicy =
              dataPlaneTrafficPolicy->defaultQosPolicy()) {
        qosPolicy = *defaultDataPlaneQosPolicy;
      }
    }
  }

  bool qosPolicyUnchanged = qosPolicy == origCPU->getQosPolicy();

  std::optional<cfg::QosMap> qosMap;
  if (qosPolicy) {
    bool qosPolicyFound = false;
    for (auto policy : *cfg_->qosPolicies()) {
      if (qosPolicyFound) {
        break;
      }
      qosPolicyFound = (*policy.name() == qosPolicy.value());
      if (qosPolicyFound && policy.qosMap()) {
        qosMap = policy.qosMap().value();
      }
    }
    if (!qosPolicyFound) {
      throw FbossError("qos policy ", qosPolicy.value(), " not found");
    }
  }

  // check whether queue setting changed
  QueueConfig newQueues;
  QueueConfig newVoqs;
  auto switchIds = scopeResolver_.scope(origCPU).switchIds();
  CHECK(scopeResolver_.hasL3());
  CHECK_GT(switchIds.size(), 0);
  // all switches on a given box will have same ASIC, so just pick the first
  auto asic = hwAsicTable_->getHwAsicIf(*switchIds.begin());
  for (auto streamType : hwAsicTable_->getCpuPortQueueStreamTypes()) {
    auto tmpPortQueues = updatePortQueues(
        origCPU->getQueuesConfig(),
        *cfg_->cpuQueues(),
        asic->getBasePortQueueId(streamType, cfg::PortType::CPU_PORT),
        asic->getDefaultNumPortQueues(streamType, cfg::PortType::CPU_PORT),
        streamType,
        qosMap,
        cfg::PortType::CPU_PORT);
    newQueues.insert(
        newQueues.begin(), tmpPortQueues.begin(), tmpPortQueues.end());

    if (cfg_->cpuVoqs()) {
      std::vector<cfg::PortQueue> cfgCpuVoqs = *cfg_->cpuVoqs();
      auto tmpPortVoqs = updatePortQueues(
          origCPU->getVoqsConfig(),
          cfgCpuVoqs,
          0 /*baseQueueId*/,
          getLocalPortNumVoqs(cfg::PortType::CPU_PORT, cfg::Scope::LOCAL),
          streamType,
          qosMap,
          cfg::PortType::CPU_PORT);
      newVoqs.insert(newVoqs.begin(), tmpPortVoqs.begin(), tmpPortVoqs.end());
    }
  }
  bool queuesUnchanged = false;
  if (origCPU->getQueues()->size() > 0) {
    /* on cold boot original queues are 0 */
    queuesUnchanged = (newQueues.size() == origCPU->getQueues()->size());
    for (int i = 0; i < newQueues.size() && queuesUnchanged; i++) {
      if (*(newQueues.at(i)) != *(origCPU->getQueues()->at(i))) {
        queuesUnchanged = false;
        break;
      }
    }
  }
  bool voqsUnchanged = (newVoqs.size() == origCPU->getVoqs()->size());
  if (origCPU->getVoqs()->size() > 0) {
    /* on cold boot original queues are 0 */
    for (int i = 0; i < newVoqs.size() && voqsUnchanged; i++) {
      if (*(newVoqs.at(i)) != *(origCPU->getVoqs()->at(i))) {
        voqsUnchanged = false;
        break;
      }
    }
  }

  if (queuesUnchanged && voqsUnchanged && qosPolicyUnchanged &&
      rxReasonToQueueUnchanged) {
    return nullptr;
  }

  auto newCPU = origCPU->clone();
  newCPU->resetQueues(newQueues);
  newCPU->resetVoqs(newVoqs);
  newCPU->resetQosPolicy(qosPolicy);
  newCPU->resetRxReasonToQueue(newRxReasonToQueue);
  auto newMultiSwitchControlPlane = std::make_shared<MultiControlPlane>();
  newMultiSwitchControlPlane->addNode(
      scopeResolver_.scope(origCPU).matcherString(), newCPU);
  return newMultiSwitchControlPlane;
}

std::string ThriftConfigApplier::getInterfaceName(
    const cfg::Interface* config) {
  if (auto name = config->name()) {
    return *name;
  }
  return folly::to<std::string>("Interface ", *config->intfID());
}

MacAddress ThriftConfigApplier::getInterfaceMac(const cfg::Interface* config) {
  if (auto mac = config->mac()) {
    return MacAddress(*mac);
  }
  return getLocalMac(getSwitchId(*config));
}

Interface::Addresses ThriftConfigApplier::getInterfaceAddresses(
    const cfg::Interface* config) {
  Interface::Addresses addrs;

  // Assign auto-generate v6 link-local address to interface. Config can
  // have more link-local addresses if needed.
  folly::MacAddress macAddr;
  if (auto mac = config->mac()) {
    macAddr = folly::MacAddress(*mac);
  } else {
    macAddr = getLocalMac(getSwitchId(*config));
  }
  const folly::IPAddressV6 v6llAddr(folly::IPAddressV6::LINK_LOCAL, macAddr);
  addrs.emplace(v6llAddr, kV6LinkLocalAddrMask);

  // Add all interface addresses from config
  for (const auto& addr : *config->ipAddresses()) {
    auto intfAddr = IPAddress::createNetwork(addr, -1, false);
    auto ret = addrs.insert(intfAddr);
    if (!ret.second) {
      throw FbossError(
          "Duplicate network IP address ",
          addr,
          " in interface ",
          *config->intfID());
    }

    // NOTE: We do not want to leak link-local address into intfRouteTables_
    // TODO: For now we are allowing v4 LLs to be programmed because they
    // are used within Galaxy for LL routing. This hack should go away once
    // we move BGP sessions over non LL addresses
    if (intfAddr.first.isV6() && intfAddr.first.isLinkLocal()) {
      continue;
    }
    auto ret2 = intfRouteTables_[RouterID(*config->routerID())].emplace(
        IPAddress::createNetwork(addr),
        std::make_pair(InterfaceID(*config->intfID()), intfAddr.first));
    if (!ret2.second) {
      // we get same network, only allow it if that is from the same
      // interface
      auto other = ret2.first->second.first;
      if (other != InterfaceID(*config->intfID())) {
        throw FbossError(
            "Duplicate network address ",
            addr,
            " of interface ",
            *config->intfID(),
            " as interface ",
            other,
            " in VRF ",
            *config->routerID());
      }
      // For consistency with interface routes as added by RouteUpdater,
      // use the last address we see rather than the first. Otherwise,
      // we see pointless route updates on syncFib()
      *ret2.first = std::make_pair(
          IPAddress::createNetwork(addr),
          std::make_pair(InterfaceID(*config->intfID()), intfAddr.first));
    }
  }

  return addrs;
}

std::shared_ptr<MultiSwitchMirrorMap> ThriftConfigApplier::updateMirrors() {
  const auto& origMirrors = orig_->getMirrors();
  auto newMirrors = std::make_shared<MirrorMap>();

  bool changed = false;
  size_t numExistingProcessed = 0;
  for (const auto& mirrorCfg : *cfg_->mirrors()) {
    auto origMirror = origMirrors->getNodeIf(*mirrorCfg.name());
    std::shared_ptr<Mirror> newMirror;
    if (origMirror) {
      newMirror = updateMirror(origMirror, &mirrorCfg);
      ++numExistingProcessed;
    } else {
      newMirror = createMirror(&mirrorCfg);
    }
    changed |= updateThriftMapNode(newMirrors.get(), origMirror, newMirror);
  }

  if (numExistingProcessed != origMirrors->numNodes()) {
    // Some existing Mirrors were removed.
    CHECK_LT(numExistingProcessed, origMirrors->numNodes());
    changed = true;
  }

  std::set<std::string> sampleIngressMirrors;
  for (auto& portMap : std::as_const(*(new_->getPorts()))) {
    for (auto& port : std::as_const(*portMap.second)) {
      auto portInMirror = port.second->getIngressMirror();
      auto portEgMirror = port.second->getEgressMirror();
      if (portInMirror.has_value()) {
        auto inMirrorMapEntry = newMirrors->find(portInMirror.value());
        if (inMirrorMapEntry == newMirrors->end()) {
          throw FbossError(
              "Mirror ", portInMirror.value(), " for port is not found");
        }
        if (auto sampleDestination = port.second->getSampleDestination()) {
          if (*sampleDestination == cfg::SampleDestination::MIRROR) {
            sampleIngressMirrors.insert(portInMirror.value());
          }
        }
      }
      if (portEgMirror.has_value() &&
          newMirrors->find(portEgMirror.value()) == newMirrors->end()) {
        throw FbossError(
            "Mirror ", portEgMirror.value(), " for port is not found");
      }
    }
  }
  if (sampleIngressMirrors.size() > 1) {
    throw FbossError(
        "Only one mirror can be configured across all ports, to sample traffic");
  }

  if (!changed) {
    return nullptr;
  }

  std::map<std::string, std::set<SwitchID>> portReferencedMirrorSwitchIds;
  for (const auto& matcherAndPortMap : std::as_const(*(new_->getPorts()))) {
    auto matcher = HwSwitchMatcher(matcherAndPortMap.first);
    for (const auto& port : std::as_const(*matcherAndPortMap.second)) {
      if (auto ingressMirror = port.second->getIngressMirror()) {
        portReferencedMirrorSwitchIds[*ingressMirror].insert(
            matcher.switchIds().begin(), matcher.switchIds().end());
      }
      if (auto egressMirror = port.second->getEgressMirror()) {
        portReferencedMirrorSwitchIds[*egressMirror].insert(
            matcher.switchIds().begin(), matcher.switchIds().end());
      }
    }
  }

  auto multiSwitchMirrors = std::make_shared<MultiSwitchMirrorMap>();
  for (auto& switchIdAndSwitchInfo :
       *cfg_->switchSettings()->switchIdToSwitchInfo()) {
    if (switchIdAndSwitchInfo.second.switchType() != cfg::SwitchType::VOQ &&
        switchIdAndSwitchInfo.second.switchType() != cfg::SwitchType::NPU) {
      continue;
    }
    auto switchId = switchIdAndSwitchInfo.first;
    auto switchMatcher =
        HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(switchId)}));
    for (auto& mirrorMapEntry : std::as_const(*newMirrors)) {
      auto mirror = mirrorMapEntry.second;
      if (mirror->getEgressPort().has_value()) {
        auto portScope = scopeResolver_.scope(mirror->getEgressPort().value());
        if (!portScope.has(SwitchID(switchId))) {
          continue;
        }
      } else if (auto mirrorSwitchIds =
                     portReferencedMirrorSwitchIds.find(mirrorMapEntry.first);
                 mirrorSwitchIds != portReferencedMirrorSwitchIds.end() &&
                 !mirrorSwitchIds->second.contains(SwitchID(switchId))) {
        continue;
      }
      auto newMirror = mirror->clone();
      // Don't carry another switch's resolved egress port onto this switch.
      if (auto egressPort = mirror->getEgressPortDesc(); egressPort &&
          !scopeResolver_.scope(new_, *egressPort).has(SwitchID(switchId))) {
        newMirror = std::make_shared<Mirror>(
            mirror->getID(),
            std::optional<PortDescriptor>(),
            mirror->getDestinationIp(),
            mirror->getSrcIp(),
            mirror->getTunnelUdpPorts(),
            mirror->getDscp(),
            mirror->getTruncate(),
            mirror->getSamplingRate());
      }
      newMirror->setSwitchId(SwitchID(switchId));
      multiSwitchMirrors->addNode(std::move(newMirror), switchMatcher);
    }
  }

  return multiSwitchMirrors;
}

std::shared_ptr<Mirror> ThriftConfigApplier::createMirror(
    const cfg::Mirror* mirrorConfig) {
  if (!mirrorConfig->destination()->egressPort() &&
      !mirrorConfig->destination()->tunnel()) {
    /*
     * At least one of the egress port or tunnel is needed.
     */
    throw FbossError(
        "Must provide either egressPort or tunnel with endpoint ip for mirror");
  }

  for (auto& switchIdAndSwitchInfo :
       *cfg_->switchSettings()->switchIdToSwitchInfo()) {
    if (switchIdAndSwitchInfo.second.asicType() ==
        cfg::AsicType::ASIC_TYPE_JERICHO3) {
      auto tunnel = mirrorConfig->destination()->tunnel();
      if (tunnel.has_value() && tunnel->sflowTunnel().has_value() &&
          !mirrorConfig->truncate().value()) {
        // J3 sflow uses eventor port and needs truncation!
        throw FbossError(
            "Jericho3 asic must have truncation enabled on sflow sessions");
      }
    }
  }

  std::optional<PortID> mirrorEgressPort;
  std::optional<folly::IPAddress> destinationIp;
  std::optional<folly::IPAddress> srcIp;
  std::optional<TunnelUdpPorts> udpPorts;

  if (auto egressPort = mirrorConfig->destination()->egressPort()) {
    std::shared_ptr<Port> mirrorToPort{nullptr};
    switch (egressPort->getType()) {
      case cfg::MirrorEgressPort::Type::name:
        for (auto& portMap : std::as_const(*(new_->getPorts()))) {
          for (auto& port : std::as_const(*portMap.second)) {
            if (port.second->getName() == egressPort->name().value()) {
              mirrorToPort = port.second;
              break;
            }
          }
        }
        break;
      case cfg::MirrorEgressPort::Type::logicalID:
        mirrorToPort = new_->getPorts()->getNodeIf(
            PortID(egressPort->logicalID().value()));
        break;
      case cfg::MirrorEgressPort::Type::__EMPTY__:
        throw FbossError(
            "Must set either name or logicalID for MirrorEgressPort");
    }
    if (mirrorToPort &&
        mirrorToPort->getIngressMirror() != *mirrorConfig->name() &&
        mirrorToPort->getEgressMirror() != *mirrorConfig->name()) {
      mirrorEgressPort = PortID(mirrorToPort->getID());
    } else {
      throw FbossError(
          "MirrorConfig ",
          *mirrorConfig->name(),
          " doesn't match ingress ",
          mirrorToPort && mirrorToPort->getIngressMirror().has_value()
              ? mirrorToPort->getIngressMirror().value()
              : "",
          " or egress mirror ",
          mirrorToPort && mirrorToPort->getEgressMirror().has_value()
              ? mirrorToPort->getEgressMirror().value()
              : "");
    }
  }

  if (auto tunnel = mirrorConfig->destination()->tunnel()) {
    if (auto sflowTunnel = tunnel->sflowTunnel()) {
      destinationIp = folly::IPAddress(*sflowTunnel->ip());
      if (!sflowTunnel->udpSrcPort() || !sflowTunnel->udpDstPort()) {
        throw FbossError(
            "Both UDP source and UDP destination ports must be provided for \
            sFlow tunneling.");
      }
      if (destinationIp->isV6() &&
          !hwAsicTable_->isFeatureSupportedOnAnyAsic(
              HwAsic::Feature::SFLOWv6)) {
        throw FbossError(
            "SFLOWv6 is not supported on this platform for  ",
            *mirrorConfig->name());
      }
      udpPorts = TunnelUdpPorts(
          *sflowTunnel->udpSrcPort(), *sflowTunnel->udpDstPort());
    } else if (auto greTunnel = tunnel->greTunnel()) {
      destinationIp = folly::IPAddress(*greTunnel->ip());
      if (destinationIp->isV6() &&
          !hwAsicTable_->isFeatureSupportedOnAnyAsic(
              HwAsic::Feature::ERSPANv6)) {
        throw FbossError(
            "ERSPANv6 is not supported on this platform ",
            *mirrorConfig->name());
      }
    }

    if (auto srcIpRef = tunnel->srcIp()) {
      srcIp = folly::IPAddress(*srcIpRef);
    }
  }

  uint8_t dscpMark = folly::copy(mirrorConfig->dscp().value());
  bool truncate = folly::copy(mirrorConfig->truncate().value());

  std::optional<PortDescriptor> egressPortDesc;
  if (mirrorEgressPort.has_value()) {
    egressPortDesc = PortDescriptor(mirrorEgressPort.value());
  }

  std::optional<uint32_t> samplingRate;
  if (mirrorConfig->samplingRate().has_value()) {
    samplingRate = mirrorConfig->samplingRate().value();
  }

  auto mirror = make_shared<Mirror>(
      *mirrorConfig->name(),
      egressPortDesc,
      destinationIp,
      srcIp,
      udpPorts,
      dscpMark,
      truncate,
      samplingRate);
  return mirror;
}

std::shared_ptr<Mirror> ThriftConfigApplier::updateMirror(
    const std::shared_ptr<Mirror>& orig,
    const cfg::Mirror* mirrorConfig) {
  auto newMirror = createMirror(mirrorConfig);
  if (newMirror->getDestinationIp() == orig->getDestinationIp() &&
      newMirror->getSrcIp() == orig->getSrcIp() &&
      newMirror->getTunnelUdpPorts() == orig->getTunnelUdpPorts() &&
      newMirror->getTruncate() == orig->getTruncate() &&
      (!newMirror->configHasEgressPort() ||
       newMirror->getEgressPort() == orig->getEgressPort() ||
       newMirror->getEgressPortDesc() == orig->getEgressPortDesc()) &&
      newMirror->getSamplingRate() == orig->getSamplingRate()) {
    if (orig->getMirrorTunnel()) {
      newMirror->setMirrorTunnel(orig->getMirrorTunnel().value());
    }
    if (orig->getEgressPort()) {
      newMirror->setEgressPortDesc(
          PortDescriptor(orig->getEgressPort().value()));
      newMirror->setEgressPort(orig->getEgressPort().value());
    }
    if (orig->getEgressPortDesc()) {
      newMirror->setEgressPortDesc(
          PortDescriptor(orig->getEgressPortDesc().value()));
    }
  }
  if (*newMirror == *orig) {
    return nullptr;
  }
  return newMirror;
}

std::shared_ptr<MultiSwitchMirrorOnDropReportMap>
ThriftConfigApplier::updateMirrorOnDropReports() {
  const auto& origReports = orig_->getMirrorOnDropReports();
  auto newReports = std::make_shared<MirrorOnDropReportMap>();

  bool changed = false;
  size_t numExistingProcessed = 0;
  for (const auto& config : *cfg_->mirrorOnDropReports()) {
    auto origReport = origReports->getNodeIf(*config.name());
    std::shared_ptr<MirrorOnDropReport> newReport;
    if (origReport) {
      newReport = updateMirrorOnDropReport(origReport, config);
      ++numExistingProcessed;
    } else {
      newReport = createMirrorOnDropReport(config);
    }
    changed |= updateThriftMapNode(newReports.get(), origReport, newReport);
  }

  if (numExistingProcessed != origReports->numNodes()) {
    // Some existing MirrorOnDropReports were removed.
    CHECK_LT(numExistingProcessed, origReports->numNodes());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }

  auto multiSwitchReports =
      std::make_shared<MultiSwitchMirrorOnDropReportMap>();
  for (const auto& [switchIdInt, switchInfo] :
       *cfg_->switchSettings()->switchIdToSwitchInfo()) {
    auto switchType = switchInfo.switchType();
    if (switchType != cfg::SwitchType::VOQ &&
        switchType != cfg::SwitchType::NPU) {
      continue;
    }
    auto switchId = SwitchID(switchIdInt);
    auto switchMatcher =
        HwSwitchMatcher(std::unordered_set<SwitchID>({switchId}));
    for (const auto& mapEntry : std::as_const(*newReports)) {
      const auto& report = mapEntry.second;
      // No mirror port (XGS): every L3 switch resolves its own.
      const auto mirrorPortId = report->getMirrorPortId();
      if (mirrorPortId.has_value() &&
          !scopeResolver_.scope(*mirrorPortId).has(switchId)) {
        continue;
      }
      multiSwitchReports->addNode(report->clone(), switchMatcher);
    }
  }

  return multiSwitchReports;
}

std::shared_ptr<MirrorOnDropReport>
ThriftConfigApplier::createMirrorOnDropReport(
    const cfg::MirrorOnDropReport& config) {
  if (*config.truncateSize() < 0) {
    throw FbossError(
        "MirrorOnDropReport ",
        *config.name(),
        " truncateSize must be non-negative");
  }
  const HwAsic* asic = checkSameAndGetAsic(hwAsicTable_->getL3Asics());
  cfg::AsicType asicType = asic->getAsicType();

  folly::IPAddress localSrcIp;
  auto collectorIp = folly::IPAddress(*config.collectorIp());
  if (asic->getSwitchType() == cfg::SwitchType::VOQ) {
    auto switchId = getAnySwitchId(cfg::SwitchType::VOQ);
    if (!switchId.has_value()) {
      throw FbossError("No VOQ switchId found");
    }
    auto systemPortId = getInbandSystemPortID(new_, *switchId);

    // Find an IP address of the switch.
    localSrcIp = collectorIp.isV4()
        ? folly::IPAddress(getSwitchIntfIP(new_, InterfaceID(systemPortId)))
        : folly::IPAddress(getSwitchIntfIPv6(new_, InterfaceID(systemPortId)));
  } else {
    // Use an optional from the MirrorDestination mirrorPort
    // -> MirrorTunnel tunnel -> string srcIp
    if (!config.mirrorPort().has_value() ||
        !config.mirrorPort()->tunnel().has_value() ||
        !config.mirrorPort()->tunnel()->srcIp().has_value()) {
      throw FbossError(
          "mirrorOnDropReports requires a mirrorPort with a tunnel that also "
          "specifies a srcIp");
    }
    if (config.mirrorPort()->tunnel()->srcIp()->empty()) {
      throw FbossError(
          "MirrorOnDropReport ",
          *config.name(),
          ": mirrorPort.tunnel.srcIp sentinel was not resolved");
    }
    localSrcIp = folly::IPAddress(*config.mirrorPort()->tunnel()->srcIp());
  }

  // Use a switch that has an interface; not every NPU does.
  std::optional<InterfaceID> modIntfId;
  for (const auto& [switchId, switchInfo] :
       *cfg_->switchSettings()->switchIdToSwitchInfo()) {
    if (switchInfo.switchType() != asic->getSwitchType()) {
      continue;
    }
    modIntfId = utility::firstInterfaceIDWithPortsIf(new_, SwitchID(switchId));
    if (modIntfId.has_value()) {
      break;
    }
  }
  if (!modIntfId.has_value()) {
    throw FbossError(
        "No switch with an interface found for switch type: ",
        static_cast<int>(asic->getSwitchType()));
  }

  // Determine the mirror recirculation port.
  std::optional<PortID> mirrorPortId;
  if (asic->getSwitchType() == cfg::SwitchType::VOQ) {
    if (config.mirrorPort().has_value()) {
      auto egressPort = config.mirrorPort()->egressPort();
      if (!egressPort.has_value()) {
        throw FbossError(
            "Only egressPort can be used as a Mirror-on-Drop destination");
      }
      switch (egressPort->getType()) {
        case cfg::MirrorEgressPort::Type::name:
          for (auto& portMap : std::as_const(*(new_->getPorts()))) {
            for (auto& [portId, port] : std::as_const(*portMap.second)) {
              if (port->getName() == egressPort->name().value()) {
                mirrorPortId = portId;
                break;
              }
            }
          }
          break;
        case cfg::MirrorEgressPort::Type::logicalID:
          mirrorPortId = egressPort->logicalID().value();
          break;
        case cfg::MirrorEgressPort::Type::__EMPTY__:
        default:
          throw FbossError(
              "Must set either name or logicalID for MirrorEgressPort");
      }
    } else if (config.mirrorPortId().has_value()) {
      mirrorPortId = PortID(*config.mirrorPortId());
    } else {
      if (asicType == cfg::AsicType::ASIC_TYPE_JERICHO3) {
        // Find the lowest numbered local-scope recycle port.
        for (auto& portMap : std::as_const(*(new_->getPorts()))) {
          for (auto& [portId, port] : std::as_const(*portMap.second)) {
            if (port->getPortType() == cfg::PortType::RECYCLE_PORT &&
                port->getScope() == cfg::Scope::LOCAL) {
              if (!mirrorPortId.has_value() ||
                  portId < static_cast<int>(mirrorPortId.value())) {
                mirrorPortId = portId;
              }
            }
          }
        }
      }
    }
    if (!mirrorPortId.has_value() || *mirrorPortId == PortID(0)) {
      throw FbossError(
          "Mirror-on-Drop destination is not specified, "
          "and auto-detection is not supported on this ASIC");
    }
    if (asicType == cfg::AsicType::ASIC_TYPE_JERICHO3 &&
        !FLAGS_allow_nif_port_for_mod) {
      auto mirrorPortType = new_->getPort(*mirrorPortId)->getPortType();
      if (mirrorPortType != cfg::PortType::RECYCLE_PORT &&
          mirrorPortType != cfg::PortType::EVENTOR_PORT) {
        throw FbossError(
            "Only RECYCLE_PORT or EVENTOR_PORT can be used for Mirror-on-Drop on Jericho3, got ",
            apache::thrift::util::enumNameSafe(mirrorPortType));
      }
      if (new_->getPort(*mirrorPortId)->getScope() != cfg::Scope::LOCAL) {
        throw FbossError(
            "Mirror-on-Drop must use LOCAL scoped recycle/eventor ports");
      }
    }
  }
  // Left unset on non-VOQ switches. The only other platform that currently
  // supports Mirror-on-Drop is XGS (e.g. TH5), where TamManager resolves the
  // mirror port from the collector IP at runtime.

  return std::make_shared<MirrorOnDropReport>(
      *config.name(),
      mirrorPortId,
      localSrcIp,
      *config.localSrcPort(),
      collectorIp,
      *config.collectorPort(),
      *config.mtu(),
      *config.truncateSize(),
      static_cast<uint8_t>(*config.dscp()),
      getLocalMacAddress().toString(),
      utility::getInterfaceMac(new_, *modIntfId).toString(),
      *config.modEventToConfigMap(),
      *config.agingGroupAgingIntervalUsecs(),
      config.samplingRate().to_optional(),
      config.dropPacketRateThreshold().to_optional(),
      config.aggregateDropPacketRateThreshold().to_optional());
}

std::shared_ptr<MirrorOnDropReport>
ThriftConfigApplier::updateMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& orig,
    const cfg::MirrorOnDropReport& config) {
  auto newReport = createMirrorOnDropReport(config);
  if (newReport->getCollectorIp() == orig->getCollectorIp() &&
      newReport->getMirrorPortId() == orig->getMirrorPortId()) {
    newReport->setIsResolved(orig->isResolved());
    if (auto mac = orig->getResolvedCollectorMac()) {
      newReport->setResolvedCollectorMac(*mac);
    }
    if (auto port = orig->getResolvedEgressPort()) {
      newReport->setResolvedEgressPort(*port);
    }
  }
  if (*newReport == *orig) {
    return nullptr;
  }
  return newReport;
}

std::shared_ptr<FibInfo>
ThriftConfigApplier::updateForwardingInformationBaseInfo() {
  ForwardingInformationBaseMap::NodeContainer newFibContainers;
  bool changed = false;

  std::size_t numExistingProcessed = 0;

  auto origFibInfoMap = orig_->getFibsInfoMap();
  std::shared_ptr<ForwardingInformationBaseMap> origFibsMap;

  if (origFibInfoMap && !origFibInfoMap->empty()) {
    origFibsMap = origFibInfoMap->getAllFibNodes();
  }

  for (const auto& interfaceCfg : *cfg_->interfaces()) {
    RouterID vrf(*interfaceCfg.routerID());
    if (newFibContainers.find(vrf) != newFibContainers.end()) {
      continue;
    }

    auto origFibContainer = origFibsMap ? origFibsMap->getNodeIf(vrf) : nullptr;

    std::shared_ptr<ForwardingInformationBaseContainer> newFibContainer{
        nullptr};
    if (origFibContainer) {
      newFibContainer = origFibContainer;
      ++numExistingProcessed;
    } else {
      newFibContainer =
          std::make_shared<ForwardingInformationBaseContainer>(vrf);
    }

    changed |= updateMap(&newFibContainers, origFibContainer, newFibContainer);
  }

  if (origFibsMap && numExistingProcessed != origFibsMap->size()) {
    CHECK_LE(numExistingProcessed, origFibsMap->size());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }

  // Create a new FibInfo with the FibContainers
  auto newFibInfo = std::make_shared<FibInfo>();
  auto newFibsMap =
      std::make_shared<ForwardingInformationBaseMap>(newFibContainers);

  // Set the FibsMapV2 in FibInfo
  newFibInfo->resetFibsMap(newFibsMap);

  return newFibInfo;
}

LabelNextHopEntry ThriftConfigApplier::getStaticLabelNextHopEntry(
    LabelNextHopEntry::Action action,
    LabelNextHopSet nexthops) {
  switch (action) {
    case LabelNextHopEntry::Action::DROP:
      return LabelNextHopEntry(
          LabelNextHopEntry::Action::DROP, AdminDistance::STATIC_ROUTE);

    case LabelNextHopEntry::Action::TO_CPU:
      return LabelNextHopEntry(
          LabelNextHopEntry::Action::TO_CPU, AdminDistance::STATIC_ROUTE);

    case LabelNextHopEntry::Action::NEXTHOPS:
      return LabelNextHopEntry(nexthops, AdminDistance::STATIC_ROUTE);
  }
  throw FbossError("invalid label forwarding action ", action);
}

std::shared_ptr<LabelForwardingEntry>
ThriftConfigApplier::createLabelForwardingEntry(
    MplsLabel label,
    LabelNextHopEntry::Action action,
    LabelNextHopSet nexthops) {
  return std::make_shared<LabelForwardingEntry>(
      LabelForwardingEntry::makeThrift(
          label,
          ClientID::STATIC_ROUTE,
          getStaticLabelNextHopEntry(action, nexthops)));
}

std::shared_ptr<IpTunnelMap> ThriftConfigApplier::updateIpInIpTunnels() {
  const auto& origTunnels = orig_->getTunnels();
  auto newTunnels = std::make_shared<IpTunnelMap>();

  bool changed = false;
  size_t numExistingProcessed = 0;
  if (!cfg_->ipInIpTunnels().has_value()) {
    return nullptr;
  }
  for (const auto& tunnelCfg : cfg_->ipInIpTunnels().value()) {
    auto origTunnel = origTunnels->getNodeIf(*tunnelCfg.ipInIpTunnelId());
    std::shared_ptr<IpTunnel> newTunnel;
    if (origTunnel) {
      newTunnel = updateIpInIpTunnel(origTunnel, &tunnelCfg);
      ++numExistingProcessed;
    } else {
      newTunnel = createIpInIpTunnel(tunnelCfg);
    }

    changed |= updateThriftMapNode(newTunnels.get(), origTunnel, newTunnel);
  }

  if (numExistingProcessed != origTunnels->numNodes()) {
    // Some existing Tunnels were removed.
    CHECK_LT(numExistingProcessed, origTunnels->numNodes());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }
  return newTunnels;
}

shared_ptr<IpTunnel> ThriftConfigApplier::updateIpInIpTunnel(
    const std::shared_ptr<IpTunnel>& orig,
    const cfg::IpInIpTunnel* config) {
  // the original tunnel has the same name as the new one from config
  auto newTunnel = createIpInIpTunnel(*config);
  if (*newTunnel == *orig) {
    return nullptr;
  }
  return newTunnel;
}

shared_ptr<IpTunnel> ThriftConfigApplier::createIpInIpTunnel(
    const cfg::IpInIpTunnel& config) {
  auto tunnel = make_shared<IpTunnel>(*config.ipInIpTunnelId());
  tunnel->setType(TunnelType::IP_IN_IP_DECAP);
  if (config.tunnelType().has_value()) {
    tunnel->setType(*config.tunnelType());
  }
  tunnel->setTunnelTermType(cfg::TunnelTerminationType::P2MP);
  if (config.tunnelTermType().has_value()) {
    tunnel->setTunnelTermType(*config.tunnelTermType());
  }
  tunnel->setUnderlayIntfId(InterfaceID(*config.underlayIntfID()));
  if (auto ttl = config.ttlMode()) {
    tunnel->setTTLMode(static_cast<cfg::TunnelMode>(*ttl));
  } else {
    tunnel->setTTLMode(cfg::TunnelMode::UNIFORM);
  }
  if (auto dscp = config.dscpMode()) {
    tunnel->setDscpMode(static_cast<cfg::TunnelMode>(*dscp));
  } else {
    tunnel->setDscpMode(cfg::TunnelMode::UNIFORM);
  }
  if (auto ecn = config.ecnMode()) {
    tunnel->setEcnMode(static_cast<cfg::TunnelMode>(*ecn));
  } else {
    tunnel->setEcnMode(cfg::TunnelMode::UNIFORM);
  }
  if (tunnel->getType() == TunnelType::IP_IN_IP_ENCAP) {
    // Encap: src/dst IPs map directly to tunnel state
    if (config.srcIp().has_value()) {
      tunnel->setSrcIP(folly::IPAddressV6(*config.srcIp()));
    }
    tunnel->setDstIP(folly::IPAddressV6(*config.dstIp()));
    if (config.srcIpMask().has_value()) {
      tunnel->setSrcIPMask(folly::IPAddressV6(*config.srcIpMask()));
    }
    if (config.dstIpMask().has_value()) {
      tunnel->setDstIPMask(folly::IPAddressV6(*config.dstIpMask()));
    }
  } else {
    // Decap: config dstIp is the tunnel term match (stored as srcIP in state),
    // config srcIp is the outer src filter (stored as dstIP in state)
    tunnel->setSrcIP(folly::IPAddressV6(*config.dstIp()));
    if (config.srcIp().has_value()) {
      tunnel->setDstIP(folly::IPAddressV6(*config.srcIp()));
    }
    if (config.dstIpMask().has_value()) {
      tunnel->setSrcIPMask(folly::IPAddressV6(*config.dstIpMask()));
    }
    if (config.srcIpMask().has_value()) {
      tunnel->setDstIPMask(folly::IPAddressV6(*config.srcIpMask()));
    }
  }

  return tunnel;
}

std::shared_ptr<Srv6TunnelMap> ThriftConfigApplier::updateSrv6Tunnels() {
  const auto& origTunnels = orig_->getSrv6Tunnels();
  auto newTunnels = std::make_shared<Srv6TunnelMap>();

  bool changed = false;
  size_t numExistingProcessed = 0;
  if (!cfg_->srv6Tunnels().has_value()) {
    return nullptr;
  }
  for (const auto& tunnelCfg : cfg_->srv6Tunnels().value()) {
    auto origTunnel = origTunnels->getNodeIf(*tunnelCfg.srv6TunnelId());
    std::shared_ptr<Srv6Tunnel> newTunnel;
    if (origTunnel) {
      newTunnel = updateSrv6Tunnel(origTunnel, &tunnelCfg);
      ++numExistingProcessed;
    } else {
      newTunnel = createSrv6Tunnel(tunnelCfg);
    }

    changed |= updateThriftMapNode(newTunnels.get(), origTunnel, newTunnel);
  }

  if (numExistingProcessed != origTunnels->numNodes()) {
    CHECK_LT(numExistingProcessed, origTunnels->numNodes());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }
  return newTunnels;
}

shared_ptr<Srv6Tunnel> ThriftConfigApplier::updateSrv6Tunnel(
    const std::shared_ptr<Srv6Tunnel>& orig,
    const cfg::Srv6Tunnel* config) {
  auto newTunnel = createSrv6Tunnel(*config);
  if (*newTunnel == *orig) {
    return nullptr;
  }
  return newTunnel;
}

shared_ptr<Srv6Tunnel> ThriftConfigApplier::createSrv6Tunnel(
    const cfg::Srv6Tunnel& config) {
  auto tunnel = make_shared<Srv6Tunnel>(*config.srv6TunnelId());
  tunnel->setUnderlayIntfId(InterfaceID(*config.underlayIntfID()));
  tunnel->setType(*config.tunnelType());
  if (config.tunnelTermType().has_value()) {
    tunnel->setTunnelTermType(*config.tunnelTermType());
  }
  if (config.ttlMode().has_value()) {
    tunnel->setTTLMode(static_cast<cfg::TunnelMode>(*config.ttlMode()));
  }
  if (config.dscpMode().has_value()) {
    tunnel->setDscpMode(static_cast<cfg::TunnelMode>(*config.dscpMode()));
  }
  if (config.ecnMode().has_value()) {
    tunnel->setEcnMode(static_cast<cfg::TunnelMode>(*config.ecnMode()));
  }
  if (config.srcIp().has_value()) {
    tunnel->setSrcIP(folly::IPAddress(*config.srcIp()));
  }
  if (config.dstIp().has_value()) {
    tunnel->setDstIP(folly::IPAddress(*config.dstIp()));
  }
  auto type = tunnel->getType();
  if (type == TunnelType::SRV6_ENCAP) {
    if (!tunnel->getSrcIP()) {
      throw FbossError(
          "Src IP not set for: ", tunnel->getID(), ", SRv6 encap tunnel");
    }
    if (tunnel->getDstIP()) {
      throw FbossError(
          "DST IP set for: ",
          tunnel->getID(),
          ", must not be set for tunnels of type SRv6 encap tunnel");
    }
  } else if (type == TunnelType::SRV6_DECAP) {
    // SRv6 decap tunnel carries only decap QoS modes; src/dst IP must not be
    // set.
    if (tunnel->getSrcIP() || tunnel->getDstIP()) {
      throw FbossError(
          "Src/DST IP set for: ",
          tunnel->getID(),
          ", must not be set for tunnels of type SRv6 decap tunnel");
    }
  } else {
    throw FbossError(
        "Unsupported tunnel type for: ",
        tunnel->getID(),
        ", only SRV6_ENCAP and SRV6_DECAP are supported");
  }
  return tunnel;
}

std::shared_ptr<VxlanTunnelMap> ThriftConfigApplier::updateVxlanTunnels() {
  const auto& origTunnels = orig_->getVxlanTunnels();
  auto newTunnels = std::make_shared<VxlanTunnelMap>();

  bool changed = false;
  size_t numExistingProcessed = 0;
  if (!cfg_->vxlanTunnels().has_value()) {
    return nullptr;
  }
  if (cfg_->vxlanTunnels()->size() > 1) {
    throw FbossError(
        "Only a single VXLAN tunnel is supported, got ",
        cfg_->vxlanTunnels()->size());
  }
  for (const auto& tunnelCfg : cfg_->vxlanTunnels().value()) {
    auto origTunnel = origTunnels->getNodeIf(*tunnelCfg.vxlanTunnelId());
    std::shared_ptr<VxlanTunnel> newTunnel;
    if (origTunnel) {
      newTunnel = updateVxlanTunnel(origTunnel, &tunnelCfg);
      ++numExistingProcessed;
    } else {
      newTunnel = createVxlanTunnel(tunnelCfg);
    }

    changed |= updateThriftMapNode(newTunnels.get(), origTunnel, newTunnel);
  }

  if (numExistingProcessed != origTunnels->numNodes()) {
    CHECK_LT(numExistingProcessed, origTunnels->numNodes());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }
  return newTunnels;
}

shared_ptr<VxlanTunnel> ThriftConfigApplier::updateVxlanTunnel(
    const std::shared_ptr<VxlanTunnel>& orig,
    const cfg::VxLanTunnel* config) {
  auto newTunnel = createVxlanTunnel(*config);
  if (*newTunnel == *orig) {
    return nullptr;
  }
  return newTunnel;
}

shared_ptr<VxlanTunnel> ThriftConfigApplier::createVxlanTunnel(
    const cfg::VxLanTunnel& config) {
  auto tunnel = make_shared<VxlanTunnel>(*config.vxlanTunnelId());
  tunnel->setUnderlayIntfId(InterfaceID(*config.underlayIntfID()));
  tunnel->setType(*config.tunnelType());
  if (config.tunnelTermType().has_value()) {
    tunnel->setTunnelTermType(*config.tunnelTermType());
  }
  if (config.ttlMode().has_value()) {
    tunnel->setTTLMode(static_cast<cfg::TunnelMode>(*config.ttlMode()));
  }
  if (config.dscpMode().has_value()) {
    tunnel->setDscpMode(static_cast<cfg::TunnelMode>(*config.dscpMode()));
  }
  if (config.ecnMode().has_value()) {
    tunnel->setEcnMode(static_cast<cfg::TunnelMode>(*config.ecnMode()));
  }
  if (config.srcIp().has_value()) {
    tunnel->setSrcIP(folly::IPAddress(*config.srcIp()));
  }
  if (config.dstIp().has_value()) {
    tunnel->setDstIP(folly::IPAddress(*config.dstIp()));
  }
  if (config.vrf2Vni().has_value()) {
    tunnel->setVrf2Vni(*config.vrf2Vni());
  }
  if (config.vniToVrf().has_value()) {
    tunnel->setVniToVrf(*config.vniToVrf());
  }
  if (tunnel->getType() != TunnelType::VXLAN) {
    throw FbossError(
        "Unsupported tunnel type for: ",
        tunnel->getID(),
        ", only VXLAN is supported");
  }
  if (!tunnel->getSrcIP()) {
    throw FbossError("Src IP not set for: ", tunnel->getID(), ", VXLAN tunnel");
  }
  if (!tunnel->getDstIP()) {
    throw FbossError("Dst IP not set for: ", tunnel->getID(), ", VXLAN tunnel");
  }
  return tunnel;
}

const std::vector<cfg::VrfConfig>& ThriftConfigApplier::getConfigVrfs() const {
  static const std::vector<cfg::VrfConfig> kNoVrfs;
  return cfg_->vrfs() ? *cfg_->vrfs() : kNoVrfs;
}

void ThriftConfigApplier::validateVrfs() const {
  const auto& vrfs = getConfigVrfs();
  std::unordered_set<int32_t> vrfIds;
  for (const auto& vrfCfg : vrfs) {
    if (*vrfCfg.vrfId() <= 0) {
      throw FbossError(
          "VRF id must be positive, VRF 0 is the default VRF: ",
          *vrfCfg.vrfId());
    }
    vrfIds.insert(*vrfCfg.vrfId());
  }
  for (const auto& intfCfg : *cfg_->interfaces()) {
    if (*intfCfg.routerID() != 0 && !vrfIds.contains(*intfCfg.routerID())) {
      throw FbossError(
          "Interface ",
          *intfCfg.intfID(),
          " refers to unconfigured VRF ",
          *intfCfg.routerID());
    }
  }
  if (vrfs.empty()) {
    return;
  }
  for (const auto* asic : hwAsicTable_->getL3Asics()) {
    if (!asic->isSupported(HwAsic::Feature::VRF)) {
      throw FbossError("VRFs are not supported on ", asic->getAsicTypeStr());
    }
  }
}

std::shared_ptr<VrfMap> ThriftConfigApplier::updateVrfs() {
  validateVrfs();
  const auto& origVrfs = orig_->getVrfs();
  auto newVrfs = std::make_shared<VrfMap>();
  bool changed = false;
  size_t numExistingProcessed = 0;
  for (const auto& vrfCfg : getConfigVrfs()) {
    if (newVrfs->getNodeIf(RouterID(*vrfCfg.vrfId()))) {
      throw FbossError("Duplicate VRF id: ", *vrfCfg.vrfId());
    }
    auto origVrf = origVrfs->getNodeIf(RouterID(*vrfCfg.vrfId()));
    auto newVrf = std::make_shared<Vrf>(RouterID(*vrfCfg.vrfId()));
    newVrf->setName(vrfCfg.name().to_optional());
    if (origVrf) {
      ++numExistingProcessed;
      if (*origVrf == *newVrf) {
        newVrf = nullptr;
      }
    }
    changed |= updateThriftMapNode(newVrfs.get(), origVrf, newVrf);
  }

  if (numExistingProcessed != origVrfs->numNodes()) {
    changed = true;
  }
  if (!changed) {
    return nullptr;
  }
  return newVrfs;
}

std::shared_ptr<MultiLabelForwardingInformationBase>
ThriftConfigApplier::updateStaticMplsRoutes(
    const std::vector<cfg::StaticMplsRouteWithNextHops>&
        staticMplsRoutesWithNhops,
    const std::vector<cfg::StaticMplsRouteNoNextHops>& staticMplsRoutesToNull,
    const std::vector<cfg::StaticMplsRouteNoNextHops>& staticMplsRoutesToCPU) {
  if (FLAGS_mpls_rib) {
    return nullptr;
  }
  if (staticMplsRoutesWithNhops.empty() && staticMplsRoutesToNull.empty() &&
      staticMplsRoutesToCPU.empty()) {
    return nullptr;
  }
  auto labelFib = new_->getLabelForwardingInformationBase()->clone();
  for (auto& staticMplsRouteEntry : staticMplsRoutesWithNhops) {
    RouteNextHopSet resolvedNextHops{};
    // resolve next hops if any next hop is unresolved.
    for (auto nexthop : staticMplsRouteEntry.nexthops().value()) {
      auto nhop = util::fromThrift(nexthop);
      if (!nhop.labelForwardingAction()) {
        throw FbossError(
            "static mpls route for label ",
            folly::copy(staticMplsRouteEntry.ingressLabel().value()),
            " has next hop without label action");
      }
      folly::IPAddress nhopAddress(nhop.addr());
      if (nhopAddress.isLinkLocal() && !nhop.isResolved()) {
        throw FbossError(
            "static mpls route for label ",
            folly::copy(staticMplsRouteEntry.ingressLabel().value()),
            " has link local next hop without interface");
      }
      if (nhop.isResolved() ||
          nhop.labelForwardingAction()->type() ==
              MplsActionCode::POP_AND_LOOKUP) {
        resolvedNextHops.insert(nhop);
        continue;
      }
      // check if nhopAddress is in in one of the interface subnets
      // look up in interfaces of default router (RouterID(0))
      auto inftToReach =
          new_->getInterfaces()->getIntfToReach(RouterID(0), nhopAddress);
      if (!inftToReach) {
        throw FbossError(
            "static mpls route for label ",
            folly::copy(staticMplsRouteEntry.ingressLabel().value()),
            " has nexthop ",
            nhopAddress.str(),
            " out of interface subnets");
      }
      resolvedNextHops.insert(ResolvedNextHop(
          nhop.addr(),
          inftToReach->getID(),
          nhop.weight(),
          nhop.labelForwardingAction()));
    }
    auto entry = labelFib->getNodeIf(
        folly::copy(staticMplsRouteEntry.ingressLabel().value()));
    if (!entry) {
      auto node = createLabelForwardingEntry(
          folly::copy(staticMplsRouteEntry.ingressLabel().value()),
          LabelNextHopEntry::Action::NEXTHOPS,
          resolvedNextHops);
      MultiLabelForwardingInformationBase::resolve(node);
      labelFib->addNode(node, scopeResolver_.scope(node));
    } else {
      auto entryToUpdate = entry->clone();
      entryToUpdate->update(
          ClientID::STATIC_ROUTE,
          getStaticLabelNextHopEntry(
              LabelNextHopEntry::Action::NEXTHOPS, resolvedNextHops));
      MultiLabelForwardingInformationBase::resolve(entryToUpdate);
      labelFib->updateNode(entryToUpdate, scopeResolver_.scope(entryToUpdate));
    }
  }

  for (auto& staticMplsRouteEntry : staticMplsRoutesToNull) {
    auto entry = labelFib->getNodeIf(
        folly::copy(staticMplsRouteEntry.ingressLabel().value()));
    if (!entry) {
      auto node = createLabelForwardingEntry(
          folly::copy(staticMplsRouteEntry.ingressLabel().value()),
          LabelNextHopEntry::Action::DROP,
          LabelNextHopSet());
      MultiLabelForwardingInformationBase::resolve(node);
      labelFib->addNode(node, scopeResolver_.scope(node));
    } else {
      auto entryToUpdate = entry->clone();
      entryToUpdate->update(
          ClientID::STATIC_ROUTE,
          getStaticLabelNextHopEntry(
              LabelNextHopEntry::Action::DROP, LabelNextHopSet()));
      MultiLabelForwardingInformationBase::resolve(entryToUpdate);
      labelFib->updateNode(entryToUpdate, scopeResolver_.scope(entryToUpdate));
    }
  }

  for (auto& staticMplsRouteEntry : staticMplsRoutesToCPU) {
    auto entry = labelFib->getNodeIf(
        folly::copy(staticMplsRouteEntry.ingressLabel().value()));
    if (!entry) {
      auto node = createLabelForwardingEntry(
          folly::copy(staticMplsRouteEntry.ingressLabel().value()),
          LabelNextHopEntry::Action::TO_CPU,
          LabelNextHopSet());
      MultiLabelForwardingInformationBase::resolve(node);
      labelFib->addNode(node, scopeResolver_.scope(node));
    } else {
      auto entryToUpdate = entry->clone();
      entryToUpdate->update(
          ClientID::STATIC_ROUTE,
          getStaticLabelNextHopEntry(
              LabelNextHopEntry::Action::TO_CPU, LabelNextHopSet()));
      MultiLabelForwardingInformationBase::resolve(entryToUpdate);
      labelFib->updateNode(entryToUpdate, scopeResolver_.scope(entryToUpdate));
    }
  }
  return labelFib;
}

uint32_t ThriftConfigApplier::generateDeterministicSeed(
    cfg::LoadBalancerID id) {
  if (auto sdkVersion = cfg_->sdkVersion()) {
    if (sdkVersion->saiSdk()) {
      return utility::generateDeterministicSeed(id, getLocalMacAddress(), true);
    }
  }
  return utility::generateDeterministicSeed(id, getLocalMacAddress(), false);
}

SwitchID ThriftConfigApplier::getSwitchId(
    const cfg::Interface& intfConfig) const {
  auto scope = scopeResolver_.scope(
      *intfConfig.type(), InterfaceID(*intfConfig.intfID()), *cfg_);
  CHECK_EQ(scope.switchIds().size(), 1)
      << "Interface can belong to only one switch";
  return scope.switchId();
}

folly::MacAddress ThriftConfigApplier::getLocalMac(SwitchID switchId) const {
  const auto& info = scopeResolver_.switchIdToSwitchInfo();
  auto iter = info.find(switchId);
  if (iter != info.end()) {
    if (auto switchMac = iter->second.switchMac()) {
      return folly::MacAddress(*switchMac);
    }
  }
  XLOG(WARNING) << " No mac address found for switch " << switchId;
  return getLocalMacAddress();
}

void ThriftConfigApplier::updateSystemPortSelfHealingEcmpLagDestinationEnable(
    bool enable) {
  CHECK(getAnySwitchId(cfg::SwitchType::VOQ).has_value());
  for (const auto& [id, dsfNode] : *cfg_->dsfNodes()) {
    if (dsfNode.type().value() != cfg::DsfNodeType::INTERFACE_NODE) {
      continue;
    }
    CHECK(dsfNode.inbandPortId().has_value());
    CHECK(dsfNode.globalSystemPortOffset().has_value());
    // TODO factor in multi npu nodes where portId range maybe
    // different
    const auto inbandSystemPortId = dsfNode.inbandPortId().value() +
        dsfNode.globalSystemPortOffset().value();

    const auto& switchIdToSwitchInfo =
        cfg_->switchSettings()->switchIdToSwitchInfo();
    const bool isLocal =
        switchIdToSwitchInfo->find(id) != switchIdToSwitchInfo.value().end();

    auto inbandSystemPort = isLocal
        ? new_->getSystemPorts()->getNodeIf(inbandSystemPortId)
        : new_->getRemoteSystemPorts()->getNodeIf(inbandSystemPortId);
    if (inbandSystemPort) {
      auto systemPorts = isLocal ? new_->getSystemPorts()->modify(&new_)
                                 : new_->getRemoteSystemPorts()->modify(&new_);
      auto newInbandPort = inbandSystemPort->clone();
      newInbandPort->setShelDestinationEnabled(enable);
      systemPorts->updateNode(
          std::move(newInbandPort),
          scopeResolver_.scope(SystemPortID(inbandSystemPortId)));
    }
  }
}

std::shared_ptr<SwitchState> applyThriftConfig(
    const std::shared_ptr<SwitchState>& state,
    const cfg::SwitchConfig* config,
    const bool supportsAddRemovePort,
    const PlatformMapping* platformMapping,
    const HwAsicTable* hwAsicTable,
    RoutingInformationBase* rib,
    AclNexthopHandler* aclNexthopHandler) {
  return ThriftConfigApplier(
             state,
             config,
             supportsAddRemovePort,
             rib,
             aclNexthopHandler,
             platformMapping,
             hwAsicTable)
      .run();
}

std::shared_ptr<SwitchState> applyThriftConfig(
    const std::shared_ptr<SwitchState>& state,
    const cfg::SwitchConfig* config,
    const bool supportsAddRemovePort,
    const PlatformMapping* platformMapping,
    const HwAsicTable* hwAsicTable,
    RouteUpdateWrapper* routeUpdater,
    AclNexthopHandler* aclNexthopHandler) {
  return ThriftConfigApplier(
             state,
             config,
             supportsAddRemovePort,
             routeUpdater,
             aclNexthopHandler,
             platformMapping,
             hwAsicTable)
      .run();
}

bool ThriftConfigApplier::processRemovedStaticMacEntries() {
  std::set<std::tuple<VlanID, folly::MacAddress, PortDescriptor>>
      configMacEntries;

  if (cfg_->staticMacAddrs().has_value() && !cfg_->staticMacAddrs()->empty()) {
    for (const auto& staticMacEntry : *cfg_->staticMacAddrs()) {
      auto vlanId = VlanID(*staticMacEntry.vlanID());
      auto macAddress = folly::MacAddress(*staticMacEntry.macAddress());
      auto portDescriptor =
          PortDescriptor(PortID(*staticMacEntry.egressLogicalPortID()));
      configMacEntries.insert(
          std::make_tuple(vlanId, macAddress, portDescriptor));
    }
  }

  std::vector<std::tuple<VlanID, folly::MacAddress>> macEntriesToRemove;
  for (const auto& vlanMap : std::as_const(*orig_->getVlans())) {
    for (const auto& vlanEntry : std::as_const(*vlanMap.second)) {
      auto vlanId = VlanID(folly::to<uint16_t>(vlanEntry.first));
      auto vlan = vlanEntry.second;
      auto macTable = vlan->getMacTable();
      for (const auto& [macAddr, macEntry] : std::as_const(*macTable)) {
        if (macEntry->getConfigured().has_value() &&
            macEntry->getConfigured().value()) {
          auto entryTuple =
              std::make_tuple(vlanId, macEntry->getMac(), macEntry->getPort());

          // If this configured entry is not present in new config, mark for
          // removal
          if (configMacEntries.find(entryTuple) == configMacEntries.end()) {
            macEntriesToRemove.emplace_back(vlanId, macEntry->getMac());
          }
        }
      }
    }
  }

  // Remove all identified MAC entries
  bool stateChanged = false;
  for (const auto& [vlanId, macAddr] : macEntriesToRemove) {
    auto stateBefore = new_;
    new_ = MacTableUtils::removeEntry(new_, vlanId, macAddr);

    if (stateBefore != new_) {
      stateChanged = true;
    }

    XLOG(DBG2) << "Removed static MAC entry: " << macAddr.toString()
               << " from VLAN " << vlanId << " (no longer in config)";
  }

  return stateChanged;
}

bool ThriftConfigApplier::processAddedStaticMacEntries() {
  if (!cfg_->staticMacAddrs().has_value() || cfg_->staticMacAddrs()->empty()) {
    return false;
  }

  bool stateChanged = false;
  for (const auto& staticMacEntry : *cfg_->staticMacAddrs()) {
    auto vlanId = VlanID(*staticMacEntry.vlanID());
    auto macAddress = folly::MacAddress(*staticMacEntry.macAddress());
    auto portId = PortID(*staticMacEntry.egressLogicalPortID());
    auto portDescriptor = PortDescriptor(portId);
    // Verify that the VLAN exists in the current state
    auto vlan = new_->getVlans()->getNodeIf(vlanId);
    if (!vlan) {
      throw FbossError(
          "Static MAC entry ",
          macAddress.toString(),
          ": VLAN ",
          vlanId,
          " does not exist");
    }
    // Verify that the port exists in the current state
    auto port = new_->getPorts()->getNodeIf(portId);
    if (!port) {
      throw FbossError(
          "Static MAC entry ",
          macAddress.toString(),
          ": Port ",
          portId,
          " does not exist");
    }

    auto stateBefore = new_;
    // Always add the entry - removal logic should have handled any conflicts
    new_ = MacTableUtils::updateOrAddStaticEntry(
        new_, portDescriptor, vlanId, macAddress, true);

    if (stateBefore != new_) {
      stateChanged = true;
    }

    XLOG(DBG2) << "Added static MAC entry: " << macAddress.toString()
               << " on VLAN " << vlanId << " port " << portDescriptor.str()
               << " (configured)";
  }

  return stateChanged;
}

} // namespace facebook::fboss
