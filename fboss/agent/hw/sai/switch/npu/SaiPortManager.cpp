// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include <folly/logging/xlog.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/HwPortFb303Stats.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_constants.h"
#include "fboss/agent/hw/sai/api/SaiApiError.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"
#include "fboss/agent/hw/sai/switch/SaiBridgeManager.h"
#include "fboss/agent/hw/sai/switch/SaiDebugCounterManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortUtils.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualChannelManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

#if defined(BRCM_SAI_SDK_DNX) || defined(BRCM_SAI_SDK_XGS)
#include <experimental/saiportextensions.h>
#endif

DEFINE_bool(
    sai_use_interface_type_for_medium,
    false,
    "Use interface type in platform mapping to derive the medium "
    "instead of deriving it from the medium field");

namespace facebook::fboss {

namespace {
// A profile cannot be attached to an administratively enabled port
// (CS00012478409). Where LLR is about to be programmed on a port that config
// wants enabled, force admin state down in the attributes the port object is
// written from, and return true so the caller re-enables it once the profile is
// bound.
//
// The port is not enabled in between, so the ordering costs no traffic and the
// link up that follows is what arms LLR. A port config wants disabled needs no
// hold: the attributes already carry that admin state, so the port object write
// puts it down ahead of programLlr() on its own.
//
// Either way programLlr() runs on a port hardware considers disabled, which is
// the invariant it needs. StateUpdateValidator keeps it by allowing only the
// first bind on a port that is still down, leaving only the two cases above.
bool holdAdminEnableForLlr(
    bool programmingLlr,
    const std::shared_ptr<Port>& swPort,
    SaiPortTraits::CreateAttributes& attributes) {
  if (!programmingLlr || !swPort->isEnabled()) {
    return false;
  }
  std::get<std::optional<SaiPortTraits::Attributes::AdminState>>(attributes) =
      SaiPortTraits::Attributes::AdminState{false};
  return true;
}

// Whether programLlr() would write anything at all. Every decision taken on
// its behalf has to agree with it: holding a port down for a write that never
// happens is an admin state down/up cycle, and so a link flap, for nothing.
bool llrSupported([[maybe_unused]] const SaiPlatform* platform) {
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  return platform->getAsic()->isSupported(
      HwAsic::Feature::LINK_LAYER_RETRANSMISSION);
#else
  return false;
#endif
}

// The LLR profile a port object is bound to. SAI_NULL_OBJECT_ID is what a port
// unbound by an earlier programLlr() carries, and means the same to callers as
// never having been programmed at all.
std::optional<sai_object_id_t> boundLlrProfile(
    [[maybe_unused]] const std::shared_ptr<SaiPort>& port) {
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  if (!port) {
    return std::nullopt;
  }
  const auto& attr =
      std::get<std::optional<SaiPortTraits::Attributes::LlrProfile>>(
          port->attributes());
  if (!attr.has_value() || attr->value() == SAI_NULL_OBJECT_ID) {
    return std::nullopt;
  }
  return attr->value();
#else
  return std::nullopt;
#endif
}

// The same, for a caller that also holds the port's handle. Two sources are
// needed because neither covers both callers: addPortImpl has the attribute,
// which attributesFromSaiStore carried over from the object reclaimed on warm
// boot, but a handle it has only just constructed; changePortImpl has the
// handle, but writes the port object from the SwitchState alone, which clears
// the attribute before programLlr() gets to read it. The attribute wins where
// both are set, being what was last written to hardware.
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
std::optional<sai_object_id_t> boundLlrProfile(
    const SaiPortHandle* portHandle) {
  if (auto fromPort = boundLlrProfile(portHandle->port)) {
    return fromPort;
  }
  if (portHandle->llrProfile) {
    return portHandle->llrProfile->adapterKey();
  }
  return std::nullopt;
}
#endif

std::optional<SaiPortTraits::Attributes::SystemPortId> getSystemPortId(
    const SaiPlatform* platform,
    PortID portId) {
  if (platform->getAsic()->getAsicVendor() ==
      HwAsic::AsicVendor::ASIC_VENDOR_TAJO) {
    return std::optional<SaiPortTraits::Attributes::SystemPortId>{
        portId + platform->getAsic()->getSflowPortIDOffset()};
  }
  return std::nullopt;
}

#if defined(BRCM_SAI_SDK_DNX)
sai_int32_t getPortTypeFromCfg(const cfg::PortType& cfgPortType) {
  switch (cfgPortType) {
    case cfg::PortType::MANAGEMENT_PORT:
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      return SAI_PORT_TYPE_MGMT;
#endif
    case cfg::PortType::EVENTOR_PORT:
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      return SAI_PORT_TYPE_EVENTOR;
#endif
    case cfg::PortType::INTERFACE_PORT:
    case cfg::PortType::HYPER_PORT_MEMBER:
      return SAI_PORT_TYPE_LOGICAL;
    case cfg::PortType::FABRIC_PORT:
      return SAI_PORT_TYPE_FABRIC;
    case cfg::PortType::CPU_PORT:
      return SAI_PORT_TYPE_CPU;
    case cfg::PortType::RECYCLE_PORT:
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      return SAI_PORT_TYPE_RECYCLE;
#else
      throw FbossError("RECYCLE_PORT is not supported");
#endif
    case cfg::PortType::HYPER_PORT:
#if defined(BRCM_SAI_SDK_DNX_GTE_14_0)
      return SAI_PORT_TYPE_HYPERPORT;
#else
      throw FbossError("HYPER_PORT is not supported");
#endif
  }

  throw FbossError(
      "Invalid port type", apache::thrift::util::enumNameSafe(cfgPortType));
}
#endif

SaiPortTraits::AdapterHostKey getPortAdapterHostKeyFromAttr(
    const SaiPortTraits::CreateAttributes& attributes) {
  SaiPortTraits::AdapterHostKey portKey{
#if defined(BRCM_SAI_SDK_DNX)
      GET_ATTR(Port, Type, attributes),
#endif
      GET_ATTR(Port, HwLaneList, attributes)};

  return portKey;
}

// Return all priorities if PFC is enabled, none if PFC is not enabled
// for the port. This will ensure that PFC stats are available for all
// priorities in fb303 for ports with PFC enabled, but not for port
// with PFC disabled. We can choose to not export PFC stats for all the
// priorities to ODS, but making it available in fb303 is helpful for
// debugging.
static std::vector<PfcPriority> allPfcPriorities(
    std::optional<cfg::PortPfc> pfc) {
  std::vector<PfcPriority> priorities;
  if (pfc.has_value() && (*pfc->rx() || *pfc->tx())) {
    for (int i = 0; i <= cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX();
         i++) {
      priorities.emplace_back(i);
    }
  }
  return priorities;
}

} // namespace

void SaiPortManager::fillInSupportedStats(PortID port) {
  auto getSupportedStats = [this, port]() {
    std::vector<sai_stat_id_t> counterIds;
    if (getPortType(port) == cfg::PortType::FABRIC_PORT) {
      counterIds = std::vector<sai_stat_id_t>{
          SAI_PORT_STAT_IF_IN_OCTETS,
          SAI_PORT_STAT_IF_IN_ERRORS,
          SAI_PORT_STAT_IF_OUT_OCTETS,
      };
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::FABRIC_LINK_DOWN_CELL_DROP_COUNTER)) {
        counterIds.emplace_back(SAI_PORT_STAT_IF_IN_LINK_DOWN_CELL_DROP);
      }
#endif
// TODO(daiweix): enable fabricControlRx|TxPacketStats after resolving
// CS00012448723
#if !defined(BRCM_SAI_SDK_DNX_GTE_14_0)
      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::FABRIC_LINK_MONITORING)) {
        counterIds.insert(
            counterIds.end(),
            SaiPortTraits::fabricControlRxPacketStats().begin(),
            SaiPortTraits::fabricControlRxPacketStats().end());
        counterIds.insert(
            counterIds.end(),
            SaiPortTraits::fabricControlTxPacketStats().begin(),
            SaiPortTraits::fabricControlTxPacketStats().end());
      }
#endif
      return counterIds;
    }
    if (getPortType(port) == cfg::PortType::RECYCLE_PORT) {
      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::SAI_PORT_ETHER_STATS)) {
        return std::vector<sai_stat_id_t>{
            SAI_PORT_STAT_ETHER_STATS_RX_NO_ERRORS};
      } else {
        return std::vector<sai_stat_id_t>{
            SAI_PORT_STAT_IF_IN_UCAST_PKTS,
        };
      }
    }
    std::set<sai_stat_id_t> countersToFilter;
    if (!platform_->getAsic()->isSupported(HwAsic::Feature::ECN)) {
      countersToFilter.insert(SAI_PORT_STAT_ECN_MARKED_PACKETS);
    }
    if (!platform_->getAsic()->isSupported(
            HwAsic::Feature::PORT_WRED_COUNTER)) {
      countersToFilter.insert(SAI_PORT_STAT_WRED_DROPPED_PACKETS);
    }
    if (platform_->getAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1) {
      // ECN is supported but not this counter
      countersToFilter.insert(SAI_PORT_STAT_ECN_MARKED_PACKETS);
    }
    if (getPortType(port) == cfg::PortType::MANAGEMENT_PORT &&
        platform_->getAsic()->getAsicType() ==
            cfg::AsicType::ASIC_TYPE_TOMAHAWK5) {
      // TODO(daiweix): follow-up with brcm why this basic stats
      // does not work for TH5 management port
      countersToFilter.insert(SAI_PORT_STAT_IF_IN_OCTETS);
    }
    counterIds.reserve(SaiPortTraits::CounterIdsToRead.size() + 1);
    std::copy_if(
        SaiPortTraits::CounterIdsToRead.begin(),
        SaiPortTraits::CounterIdsToRead.end(),
        std::back_inserter(counterIds),
        [&countersToFilter](auto statId) {
          return countersToFilter.find(statId) == countersToFilter.end();
        });
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::BLACKHOLE_ROUTE_DROP_COUNTER)) {
      counterIds.emplace_back(managerTable_->debugCounterManager()
                                  .getPortL3BlackHoleCounterStatId());
    }
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::SAI_MPLS_LABEL_LOOKUP_FAIL_COUNTER)) {
#if !defined(TAJO_SDK_VERSION_1_42_8)
      counterIds.emplace_back(managerTable_->debugCounterManager()
                                  .getMPLSLookupFailedCounterStatId());
#endif
    }
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::ANY_ACL_DROP_COUNTER)) {
      counterIds.emplace_back(
          managerTable_->debugCounterManager().getAclDropCounterStatId());
    }
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::EGRESS_FORWARDING_DROP_COUNTER)) {
      counterIds.emplace_back(
          managerTable_->debugCounterManager().getEgressForwardingDropStatId());
    }
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::ANY_TRAP_DROP_COUNTER)) {
      counterIds.emplace_back(
          managerTable_->debugCounterManager().getTrapDropCounterStatId());
    }
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
    if (SaiDebugCounterManager::isSrv6MySidDropCounterSupported(
            platform_->getAsic())) {
      counterIds.emplace_back(
          managerTable_->debugCounterManager().getSrv6MySidDropCounterStatId());
    }
#endif
    if (platform_->getAsic()->isSupported(HwAsic::Feature::PFC)) {
      counterIds.reserve(
          counterIds.size() + SaiPortTraits::PfcCounterIdsToRead.size());
      std::copy(
          SaiPortTraits::PfcCounterIdsToRead.begin(),
          SaiPortTraits::PfcCounterIdsToRead.end(),
          std::back_inserter(counterIds));

      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::PFC_XON_TO_XOFF_COUNTER)) {
        counterIds.reserve(
            counterIds.size() +
            SaiPortTraits::PfcXonToXoffCounterIdsToRead.size());
        std::copy(
            SaiPortTraits::PfcXonToXoffCounterIdsToRead.begin(),
            SaiPortTraits::PfcXonToXoffCounterIdsToRead.end(),
            std::back_inserter(counterIds));
      }
    }
    // LLR counters are NOT added here: they are collected in updateStats via a
    // separate, isolated stats read (like FEC) so that a drop whose SDK returns
    // NOT_SUPPORTED for the LLR reads does not fail the whole basic
    // port-counter batch. ETHER stats used on j3 sim
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::SAI_PORT_ETHER_STATS)) {
      counterIds.emplace_back(SAI_PORT_STAT_ETHER_STATS_TX_NO_ERRORS);
      counterIds.emplace_back(SAI_PORT_STAT_ETHER_STATS_RX_NO_ERRORS);
    }
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::PQP_ERROR_EGRESS_DROP_COUNTER)) {
      counterIds.emplace_back(
          SAI_PORT_STAT_OUT_CONFIGURED_DROP_REASONS_0_DROPPED_PKTS);
    }
// 13.3+: the 13.3 tp2 package propagates SAI_VERSION_13_0_EA_ODP and 13.0 EA
// is retired from the build matrix, so GTE_13_0 is the operative gate.
#if defined(BRCM_SAI_SDK_XGS_GTE_13_0)
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::HW_PROTECTION_SWITCHOVER_COUNTERS)) {
      counterIds.emplace_back(
          SAI_PORT_STAT_IF_IN_HW_PROTECTION_SWITCHOVER_EVENTS);
      counterIds.emplace_back(
          SAI_PORT_STAT_IF_IN_HW_PROTECTION_SWITCHOVER_DROP_PKTS);
    }
#endif
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::SAI_PORT_IN_CONGESTION_DISCARDS)) {
      counterIds.emplace_back(SAI_PORT_STAT_IN_DROPPED_PKTS);
    }
    fillInSupportedVendorExtStats(counterIds);
    return counterIds;
  };
  port2SupportedStats_.emplace(port, getSupportedStats());
}

PortSaiId SaiPortManager::addPortImpl(const std::shared_ptr<Port>& swPort) {
  SaiPortHandle* portHandle = getPortHandle(swPort->getID());
  if (portHandle) {
    throw FbossError(
        "Attempted to add port which already exists: ",
        swPort->getID(),
        " SAI id: ",
        portHandle->port->adapterKey());
  }
  removeRemovedHandleIf(swPort->getID());
  SaiPortTraits::CreateAttributes attributes = attributesFromSwPort(swPort);
  attributesFromSaiStore(attributes);

  /**
   * Remove port handles if the new HwLaneList contains hw lanes that are used
   * by the removed handles. E.g. Port1 (HwLaneList[1,2]), Port2
   * (HwLaneList[3,4]) are removed to create new Port2 (HwLaneList[1,2,3,4]).
   * Port1 would be added to removedHandles_, but not removed by the SDK yet.
   * Therefore, check new hw lanes and remove ports in SDK accordingly.
   */
  auto newHwLanes = GET_ATTR(Port, HwLaneList, attributes);
  for (auto& [removedPortId, removedHandle] : removedHandles_) {
    auto removedHwLanes =
        GET_ATTR(Port, HwLaneList, removedHandle->port->attributes());
    for (auto removedHwLane : removedHwLanes) {
      if (std::find(newHwLanes.begin(), newHwLanes.end(), removedHwLane) !=
          newHwLanes.end()) {
        removeRemovedHandleIf(removedPortId);
        break;
      }
    }
  }

  SaiPortTraits::AdapterHostKey portKey{
      getPortAdapterHostKeyFromAttr(attributes)};
  auto handle = std::make_unique<SaiPortHandle>();

  auto& portStore = saiStore_->get<SaiPortTraits>();

  const bool deferAdminEnable = holdAdminEnableForLlr(
      llrSupported(platform_) &&
          llrProfileBindingChanged(
              boundLlrProfile(portStore.get(portKey)), swPort),
      swPort,
      attributes);

  auto saiPort = portStore.setObject(portKey, attributes, swPort->getID());
  handle->port = saiPort;
  programSerdes(saiPort, swPort, handle.get());
  programLlr(swPort, handle.get());
  managerTable_->virtualChannelManager().programVirtualChannels(
      swPort, saiPort->adapterKey());
  if (deferAdminEnable) {
    // Through the store, so its cached admin state tracks hardware.
    saiPort->setOptionalAttribute(SaiPortTraits::Attributes::AdminState{true});
  }

  if (swPort->isEnabled()) {
    HwBasePortFb303Stats::QueueId2Name queueId2Name{};
    portStats_.emplace(
        swPort->getID(),
        std::make_unique<HwPortFb303Stats>(
            swPort->getName(),
            queueId2Name,
            allPfcPriorities(swPort->getPfc()),
            swPort->getPfc(),
            platform_->getAsic()->isSupported(
                HwAsic::Feature::INGRESS_PRIORITY_GROUP_DROPPED_PACKETS),
            platform_->getAsic()->isSupported(
                HwAsic::Feature::SAI_PORT_PG_DROP_STATUS),
            SaiDebugCounterManager::isSrv6MySidDropCounterSupported(
                platform_->getAsic()),
            platform_->getAsic()->isSupported(
                HwAsic::Feature::SAI_MPLS_LABEL_LOOKUP_FAIL_COUNTER),
            SaiPortManager::isLinkDebounceRetriggerCounterSupported(
                platform_->getAsic()),
            platform_->getAsic()->isSupported(
                HwAsic::Feature::SLL_HLL_DISCARD_COUNTERS),
            platform_->getAsic()->isSupported(
                HwAsic::Feature::PORT_LATCH_STATUS)));
  }

  bool samplingMirror = swPort->getSampleDestination().has_value() &&
      swPort->getSampleDestination() == cfg::SampleDestination::MIRROR;
  SaiPortMirrorInfo mirrorInfo{
      swPort->getIngressMirror(), swPort->getEgressMirror(), samplingMirror};
  handle->mirrorInfo = mirrorInfo;
  handles_.emplace(swPort->getID(), std::move(handle));
  processPortBufferPoolConfigs(swPort);
  setQosPolicy(swPort->getID(), swPort->getQosPolicy());

  addSamplePacket(swPort);
  addNode(swPort);
  addPfc(swPort);
  changePfcBuffers(nullptr, swPort);

  // set platform port's speed
  auto platformPort = platform_->getPort(swPort->getID());
  platformPort->setCurrentProfile(swPort->getProfileID());

  auto portSaiId = saiPort->adapterKey();
  uint32_t hwLogicalPortId = static_cast<uint32_t>(portSaiId);
  if (platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_CHENAB) {
    // Chenab stores the SDK logical port ID in the upper 32 bits of the OID.
    hwLogicalPortId =
        static_cast<uint32_t>(static_cast<uint64_t>(portSaiId) >> 32);
  }
  XLOG(DBG2) << "swPort ID: " << swPort->getID()
             << " hwLogicalPort ID: " << hwLogicalPortId;
  platformPort->setHwLogicalPortId(hwLogicalPortId);
  auto asicPrbs = swPort->getAsicPrbs();
  if (asicPrbs.enabled().value()) {
    initAsicPrbsStats(swPort);
    if (platform_->getAsic()->getAsicVendor() ==
        HwAsic::AsicVendor::ASIC_VENDOR_BCM) {
      // linkscan is disabled after enabling PRBS on bcm platforms, thus need to
      // trigger port link state update from FBOSS
      platform_->getHwSwitch()->syncPortLinkState(swPort->getID());
    }
  }
  return portSaiId;
}

void SaiPortManager::loadPortQueuesForAddedPort(
    const std::shared_ptr<Port>& swPort) {
  loadPortQueues(*swPort);
}
void SaiPortManager::loadPortQueuesForChangedPort(
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  SaiPortTraits::CreateAttributes oldAttributes = attributesFromSwPort(oldPort);
  SaiPortTraits::CreateAttributes newAttributes = attributesFromSwPort(newPort);

  if (createOnlyAttributeChanged(oldAttributes, newAttributes)) {
    // If createOnly attributes did not change we would have
    // handled queue updates in changePortImpl itself
    loadPortQueues(*newPort);
  }
}

void SaiPortManager::changePortImpl(
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  SaiPortHandle* existingPort = getPortHandle(newPort->getID());
  if (!existingPort) {
    throw FbossError("Attempted to change non-existent port ");
  }
  SaiPortTraits::CreateAttributes oldAttributes = attributesFromSwPort(oldPort);
  SaiPortTraits::CreateAttributes newAttributes = attributesFromSwPort(newPort);

  if (createOnlyAttributeChanged(oldAttributes, newAttributes)) {
    XLOG(DBG2) << "Create only attribute (e.g. lane, speed etc.) changed for "
               << oldPort->getID();
    changePortByRecreate(oldPort, newPort);
    return;
  }

  // Decided before the port object is written, since that write is what would
  // otherwise enable the port ahead of programLlr().
  const bool programLlrForPort =
      llrSupported(platform_) && llrConfigChanged(oldPort, newPort);
  const bool deferAdminEnable =
      holdAdminEnableForLlr(programLlrForPort, newPort, newAttributes);

  SaiPortTraits::AdapterHostKey portKey{
      getPortAdapterHostKeyFromAttr(newAttributes)};
  auto& portStore = saiStore_->get<SaiPortTraits>();
  auto saiPort = portStore.setObject(portKey, newAttributes, newPort->getID());

  changeZeroPreemphasis(oldPort, newPort);

  programSerdes(saiPort, newPort, existingPort);
  // if vlan changed update it, this is important for rx processing
  if (newPort->getIngressVlan() != oldPort->getIngressVlan()) {
    concurrentIndices_->vlanIds.insert_or_assign(
        PortDescriptorSaiId(saiPort->adapterKey()), newPort->getIngressVlan());
    XLOG(DBG2) << "changed vlan on port " << newPort->getID()
               << ": old vlan: " << oldPort->getIngressVlan()
               << ", new vlan: " << newPort->getIngressVlan();
  }
  auto oldAsicPrbsEnabled = oldPort->getAsicPrbs().enabled().value();
  auto newAsicPrbsEnabled = newPort->getAsicPrbs().enabled().value();
  if (newPort->getProfileID() != oldPort->getProfileID()) {
    auto platformPort = platform_->getPort(newPort->getID());
    platformPort->setCurrentProfile(newPort->getProfileID());
    if (oldAsicPrbsEnabled == newAsicPrbsEnabled && newAsicPrbsEnabled) {
      updatePrbsStatsEntryRate(newPort);
    }
  }
  processPortBufferPoolConfigs(newPort);

  changeMirror(oldPort, newPort);
  changeSamplePacket(oldPort, newPort);
  changePfc(oldPort, newPort);
  changeRxLaneSquelch(oldPort, newPort);
  changeTxEnable(oldPort, newPort);
  changeResetQueueCreditBalance(oldPort, newPort);
  changePfcBuffers(oldPort, newPort);

  if (newPort->isEnabled()) {
    if (!oldPort->isEnabled()) {
      // Port transitioned from disabled to enabled, setup port stats
      HwBasePortFb303Stats::QueueId2Name queueId2Name{};
      portStats_.emplace(
          newPort->getID(),
          std::make_unique<HwPortFb303Stats>(
              newPort->getName(),
              queueId2Name,
              allPfcPriorities(newPort->getPfc()),
              newPort->getPfc(),
              platform_->getAsic()->isSupported(
                  HwAsic::Feature::INGRESS_PRIORITY_GROUP_DROPPED_PACKETS),
              platform_->getAsic()->isSupported(
                  HwAsic::Feature::SAI_PORT_PG_DROP_STATUS),
              SaiDebugCounterManager::isSrv6MySidDropCounterSupported(
                  platform_->getAsic()),
              platform_->getAsic()->isSupported(
                  HwAsic::Feature::SAI_MPLS_LABEL_LOOKUP_FAIL_COUNTER),
              SaiPortManager::isLinkDebounceRetriggerCounterSupported(
                  platform_->getAsic()),
              platform_->getAsic()->isSupported(
                  HwAsic::Feature::SLL_HLL_DISCARD_COUNTERS),
              platform_->getAsic()->isSupported(
                  HwAsic::Feature::PORT_LATCH_STATUS)));
    } else if (oldPort->getName() != newPort->getName()) {
      // Port was already enabled, but Port name changed - update stats
      portStats_.find(newPort->getID())
          ->second->portNameChanged(newPort->getName());
    }
    if (oldPort->getPfc() != newPort->getPfc()) {
      portStats_.find(newPort->getID())
          ->second->pfcConfigChanged(
              allPfcPriorities(newPort->getPfc()), newPort->getPfc());
    }
  } else if (oldPort->isEnabled()) {
    // Port transitioned from enabled to disabled, remove stats
    portStats_.erase(newPort->getID());
  }
  // Before the qos policy: on TU1, VC create on a second port of an input
  // buffer fails with INVALID PARAMETER once QUEUE_TO_VC is bound to it.
  managerTable_->virtualChannelManager().programVirtualChannels(
      newPort, saiPort->adapterKey());
  changeQueue(
      newPort,
      oldPort->getPortQueues()->impl(),
      newPort->getPortQueues()->impl());
  changeQosPolicy(oldPort, newPort);
  if (oldAsicPrbsEnabled != newAsicPrbsEnabled) {
    if (newAsicPrbsEnabled) {
      initAsicPrbsStats(newPort);
      if (platform_->getAsic()->getAsicVendor() ==
          HwAsic::AsicVendor::ASIC_VENDOR_BCM) {
        // linkscan is disabled after enabling PRBS on bcm platforms, thus need
        // to trigger port link state update from FBOSS
        platform_->getHwSwitch()->syncPortLinkState(newPort->getID());
      }
    } else {
      auto portAsicPrbsStatsItr = portAsicPrbsStats_.find(newPort->getID());
      if (portAsicPrbsStatsItr == portAsicPrbsStats_.end()) {
        throw FbossError(
            "Asic prbs lane error map not initialized for port ",
            newPort->getID());
      }
      portAsicPrbsStats_.erase(newPort->getID());
    }
  }
  if (newPort->isUp() != oldPort->isUp() && !newPort->isUp()) {
    resetCableLength(newPort->getID());
  }
  changePortFlowletConfig(oldPort, newPort);
  if (programLlrForPort) {
    programLlr(newPort, existingPort);
    if (deferAdminEnable) {
      // Through the store, so its cached admin state tracks hardware.
      saiPort->setOptionalAttribute(
          SaiPortTraits::Attributes::AdminState{true});
    }
  }
  changeClm(oldPort, newPort);
}

void SaiPortManager::attributesFromSaiStore(
    SaiPortTraits::CreateAttributes& attributes) {
  SaiPortTraits::AdapterHostKey portKey{
      getPortAdapterHostKeyFromAttr(attributes)};

  auto& store = saiStore_->get<SaiPortTraits>();
  std::shared_ptr<SaiPort> port = store.get(portKey);
  if (!port) {
    return;
  }
  auto getAndSetAttribute = [](auto& storeAttrs, auto& swAttrs, auto type) {
    std::get<std::optional<std::decay_t<decltype(type)>>>(swAttrs) =
        std::get<std::optional<std::decay_t<decltype(type)>>>(storeAttrs);
  };
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::IngressMirrorSession{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::EgressMirrorSession{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::IngressSampleMirrorSession{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::EgressSampleMirrorSession{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::IngressSamplePacketEnable{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::EgressSamplePacketEnable{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::QosDscpToTcMap{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::QosTcToQueueMap{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::QosTcToPriorityGroupMap{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::QosPfcPriorityToQueueMap{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::QosPfcPriorityToPriorityGroupMap{});
#if defined(BRCM_SAI_SDK_XGS_GTE_13_0)
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::QosDot1pToTcMap{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::QosTcAndColorToDot1pMap{});
#endif
  getAndSetAttribute(
      port->attributes(), attributes, SaiPortTraits::Attributes::TamObject{});
  getAndSetAttribute(
      port->attributes(), attributes, SaiPortTraits::Attributes::IngressAcl{});
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::PfcTcDldInterval{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::PfcTcDlrInterval{});
#endif
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::PORT_TTL_DECREMENT_DISABLE)) {
    getAndSetAttribute(
        port->attributes(),
        attributes,
        SaiPortTraits::Attributes::DisableTtlDecrement{});
  }
#if defined(BRCM_SAI_SDK_DNX_GTE_14_0)
  // FabricSystemPort is only applicable for fabric ports
  if (GET_ATTR(Port, Type, attributes) == SAI_PORT_TYPE_FABRIC) {
    getAndSetAttribute(
        port->attributes(),
        attributes,
        SaiPortTraits::Attributes::FabricSystemPort{});
  }
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  // LLR is programmed by programLlr(), not attributesFromSwPort(), which leaves
  // all three nullopt. Without carrying them over, setObject() below overwrites
  // the cached values with nullopt -- harmless in hardware, since
  // setAttributeInHardware() ignores a nullopt, but it leaves programLlr()
  // comparing every attribute against nullopt and so re-driving the whole
  // disable/bind/enable sequence on a port whose LLR never changed.
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::LlrModeLocal{});
  getAndSetAttribute(
      port->attributes(),
      attributes,
      SaiPortTraits::Attributes::LlrModeRemote{});
  getAndSetAttribute(
      port->attributes(), attributes, SaiPortTraits::Attributes::LlrProfile{});
#endif
}

SaiPortTraits::CreateAttributes SaiPortManager::attributesFromSwPort(
    const std::shared_ptr<Port>& swPort,
    bool /* lineSide */,
    bool basicAttributeOnly) const {
  bool adminState =
      swPort->getAdminState() == cfg::PortState::ENABLED ? true : false;

#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
  std::optional<bool> isDrained = std::nullopt;
  if (platform_->getAsic()->isSupported(HwAsic::Feature::PORT_FABRIC_ISOLATE)) {
    if (swPort->getPortType() == cfg::PortType::FABRIC_PORT) {
      isDrained = swPort->getPortDrainState() == cfg::PortDrainState::DRAINED;
    } else if (swPort->getPortDrainState() == cfg::PortDrainState::DRAINED) {
      throw FbossError(
          "Cannot isolate/drain a non-fabric port ", swPort->getID());
    }
  }
#endif

  auto portID = swPort->getID();
  auto platformPort = platform_->getPort(portID);
  auto speed = swPort->getSpeed();
  auto hwLaneList = platformPort->getHwPortLanes(swPort->getProfileID());
  if (!hwLaneListIsPmdLaneList_) {
    // On Tomahawk4, HwLaneList means physical port list instead of pmd lane
    // list for now. One physical port maps to two pmd lanes. So, do the
    // conversion here PMD lanes ==> physical ports, e.g.
    // [1,2,3,4] ==> [1,2]
    // [5,6,7,8] ==> [3,4]
    // ......
    // If only has one lane (e.g. 10G case), map to one physical port, e.g.
    // [1] ==> [1]
    // [5] ==> [3]
    // ......
    std::vector<uint32_t> pportList;
    for (int i = 0; i < std::max(1, static_cast<int>(hwLaneList.size()) / 2);
         i++) {
      pportList.push_back((hwLaneList[i * 2] + 1) / 2);
    }
    hwLaneList = pportList;
  }
  std::optional<SaiPortTraits::Attributes::GlobalFlowControlMode>
      globalFlowControlMode;
  if (swPort->getPortType() != cfg::PortType::HYPER_PORT_MEMBER) {
    globalFlowControlMode = utility::getSaiPortPauseMode(swPort->getPause());
  } else {
    CHECK(!*swPort->getPause().tx() && !*swPort->getPause().rx())
        << "Programming global flow control mode on hyper port member is not supported";
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  std::optional<int> loopbackMode;
  if (swPort->getPortType() != cfg::PortType::HYPER_PORT_MEMBER) {
    // hyper port member loopback mode should be determined by hyper port
    loopbackMode = utility::getSaiPortLoopbackMode(swPort->getLoopbackMode());
  }
#else
  auto internalLoopbackMode =
      utility::getSaiPortInternalLoopbackMode(swPort->getLoopbackMode());
#endif

  // Now use profileConfig from SW port as the source of truth
  auto portProfileConfig = swPort->getProfileConfig();
  if (!portProfileConfig.medium()) {
    throw FbossError(
        "Missing medium info in profile ",
        apache::thrift::util::enumNameSafe(swPort->getProfileID()));
  }
  auto transmitterTech = *portProfileConfig.medium();
  std::optional<sai_port_media_type_t> mediaType;
  if (platform_->getAsic()->isSupported(HwAsic::Feature::MEDIA_TYPE)) {
    if (FLAGS_sai_use_interface_type_for_medium) {
      // We are currently programming different media type for BCM-SAI
      // implementation vs Native SDK implementation. That's because for
      // native-bcm implementation, we derive the media type from interfaceType
      // in platform mapping. However, we use the 'medium' field from platform
      // mapping for the SAI implementation. This causes us to program a
      // sub-optimal media type with SAI (and cause issues like S280146) as for
      // certain profiles, the medium type is defined as TransmitterTech.OPTICAL
      // whereas the interface type is InterfaceType.KR4 and they both currently
      // lead to different SAI_PORT_MEDIA_TYPE. Since changing the media type is
      // a disruptive change, we are guarding this with a flag for now as that
      // will help us stage the roll out of this change. Eventually, we'll get
      // rid of this flag and make deriving the media type from interfaceType as
      // default
      if (!portProfileConfig.interfaceType()) {
        throw FbossError(
            "Missing interfaceType in profile ",
            apache::thrift::util::enumNameSafe(swPort->getProfileID()));
      }
      mediaType = utility::getSaiPortMediaFromInterfaceType(
          *portProfileConfig.interfaceType());
    } else {
      mediaType = utility::getSaiPortMediaType(transmitterTech, speed);
    }
  }
  std::optional<SaiPortTraits::Attributes::FecMode> fecMode;
  if (platform_->getAsic()->isSupported(HwAsic::Feature::FEC)) {
    auto enableFec = (speed >= cfg::PortSpeed::FIFTYTHREEPOINTONETWOFIVEG) ||
        !platformPort->shouldDisableFEC();
    if (!enableFec) {
      fecMode = SAI_PORT_FEC_MODE_NONE;
    } else {
      fecMode = utility::getSaiPortFecMode(*portProfileConfig.fec());
    }
  }
  std::optional<SaiPortTraits::Attributes::InterfaceType> interfaceType{};
  // TODO(joseph5wu) Maybe provide a new function to convert interfaceType from
  // profile config to sai interface type
  if (auto saiInterfaceType =
          platform_->getInterfaceType(transmitterTech, speed)) {
    interfaceType = saiInterfaceType.value();
  }
  std::optional<sai_port_media_type_t> propagationDelayMediaType;
#if defined(BRCM_SAI_SDK_DNX_GTE_14_0) || defined(BRCM_SAI_SDK_XGS_GTE_14_2)
  // Setting an attribute whose id does not resolve on this SDK is a FATAL in
  // SaiAttribute, so never populate it ahead of the id check.
  if (SaiPortTraits::Attributes::AttributeCablePropagationDelayMediaType{}() &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::CABLE_PROPOGATION_DELAY) &&
      managerTable_->switchManager().isMeasureCableLengthEnabled()) {
    if (swPort->getPortType() == cfg::PortType::HYPER_PORT_MEMBER ||
        swPort->getPortType() == cfg::PortType::INTERFACE_PORT) {
      propagationDelayMediaType =
          utility::getSaiCablePropagationDelayMediaType(transmitterTech);
    }
  }
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
  std::optional<SaiPortTraits::Attributes::InterFrameGap> interFrameGap;
  if (platform_->getAsic()->isSupported(HwAsic::Feature::MACSEC) &&
      portProfileConfig.interPacketGapBits().has_value()) {
    interFrameGap = *portProfileConfig.interPacketGapBits();
    XLOG(DBG2) << "Setting interFrameGap from platformMapping for port "
               << swPort->getID()
               << " to value: " << *portProfileConfig.interPacketGapBits();
  }
  // If interpacket gaps bits are set in switch state, overwrite interFrameGap
  // value
  if (swPort->getInterPacketGapBits().has_value()) {
    interFrameGap = *swPort->getInterPacketGapBits();
    XLOG(DBG2) << "Setting interFrameGap from switchState for port "
               << swPort->getID()
               << " to value: " << *swPort->getInterPacketGapBits();
  }
#endif
  std::optional<bool> amIdles{};
  // If amIdles is set in switch state, use that value
  if (swPort->getAmIdles().has_value()) {
    amIdles = *swPort->getAmIdles();
    XLOG(DBG2) << "Setting amIdles from switchState for port "
               << swPort->getID() << " to value: " << *swPort->getAmIdles();
  }
  std::optional<SaiPortTraits::Attributes::LinkTrainingEnable>
      linkTrainingEnable;
  if (linkTrainingSupportedOnPort(swPort)) {
    // Use config value if set, otherwise default to false (backward-compatible)
    linkTrainingEnable = swPort->getLinkTraining().value_or(false);
  }

  std::optional<bool> fdrEnable;
#if defined(BRCM_SAI_SDK_GTE_10_0) || defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  if ((swPort->getPortType() == cfg::PortType::INTERFACE_PORT ||
       swPort->getPortType() == cfg::PortType::HYPER_PORT_MEMBER) &&
      adminState &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_FEC_CODEWORDS_STATS)) {
    fdrEnable = true;
  }
#endif
  auto ptpStatusOpt = managerTable_->switchManager().getPtpTcEnabled();
  uint16_t vlanId = swPort->getIngressVlan();
  auto systemPortId = getSystemPortId(platform_, swPort->getID());

  std::optional<SaiPortTraits::Attributes::Mtu> mtu{};
  if (platform_->getAsic()->portMtuSupported(swPort->getPortType())) {
    mtu = swPort->getMaxFrameSize();
  }
  // 1. user metadata set    -> that value
  // 2. none, port tagged    -> 0, a real clear. Unset would not clear it:
  //    setObject() skips the hardware write for an unset optional but still
  //    drops the value from the store, leaving the port tagged with nothing
  //    left to re-drive it.
  // 3. none, port untagged  -> unset, nothing to say.
  // Cases 2 and 3 need a port that already exists. A new port has no handle
  // yet and comes up untagged, so nothing is said at create.
  std::optional<SaiPortTraits::Attributes::Metadata> metadata;
  if (const auto userMetaData = swPort->getUserMetaData()) {
    const auto metadataValue = static_cast<uint32_t>(*userMetaData);
    auto range = SaiApiTable::getInstance()->switchApi().getAttribute(
        managerTable_->switchManager().getSwitchSaiId(),
        SaiSwitchTraits::Attributes::PortUserMetaDataRange{});
    if (metadataValue < range.min || metadataValue > range.max) {
      throw FbossError(
          "Port user metadata ",
          metadataValue,
          " is outside the ASIC-supported range [",
          range.min,
          ", ",
          range.max,
          "]");
    }
    metadata = SaiPortTraits::Attributes::Metadata{metadataValue};
  } else if (const auto* portHandle = getPortHandle(swPort->getID())) {
    const auto programmedMetadata =
        std::get<std::optional<SaiPortTraits::Attributes::Metadata>>(
            portHandle->port->attributes());
    if (programmedMetadata && programmedMetadata->value() != 0) {
      metadata = SaiPortTraits::Attributes::Metadata{0};
    }
  }
  std::optional<SaiPortTraits::Attributes::PrbsPolynomial> prbsPolynomial =
      std::nullopt;
  std::optional<SaiPortTraits::Attributes::PrbsConfig> prbsConfig =
      std::nullopt;
  if (platform_->getAsic()->isSupported(HwAsic::Feature::SAI_PRBS) &&
      swPort->getPortType() != cfg::PortType::HYPER_PORT) {
    // PRBS should only be configured on hyper port member or normal ports,
    // not on hyper port
    auto asicPrbs = swPort->getAsicPrbs();
    prbsConfig = getSaiPortPrbsConfig(asicPrbs.enabled().value());
    if (asicPrbs.enabled().value()) {
      prbsPolynomial =
          static_cast<sai_uint32_t>(asicPrbs.polynominal().value());
    }
  }
  std::optional<SaiPortTraits::Attributes::DisableTtlDecrement> disableTtl{};
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::PORT_TTL_DECREMENT_DISABLE) &&
      swPort->getTTLDisableDecrement()) {
    disableTtl = SaiPortTraits::Attributes::DisableTtlDecrement{
        swPort->getTTLDisableDecrement().value()};
  }
  std::optional<SaiPortTraits::Attributes::PktTxEnable> pktTxEnable{};
  if (auto txEnable = swPort->getTxEnable()) {
    pktTxEnable = SaiPortTraits::Attributes::PktTxEnable{txEnable.value()};
  }
  SaiPortPfcInfo portPfcInfo;
  if (swPort->getPortType() != cfg::PortType::HYPER_PORT_MEMBER) {
    portPfcInfo = getPortPfcAttributes(swPort);
  } else {
    CHECK(!swPort->getPfc().has_value())
        << "Enabling PFC on hyper port member is not supported";
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  std::optional<SaiPortTraits::Attributes::ArsEnable> arsEnable = std::nullopt;
  std::optional<SaiPortTraits::Attributes::ArsPortLoadScalingFactor>
      arsPortLoadScalingFactor = std::nullopt;
  std::optional<SaiPortTraits::Attributes::ArsPortLoadPastWeight>
      arsPortLoadPastWeight = std::nullopt;
  std::optional<SaiPortTraits::Attributes::ArsPortLoadFutureWeight>
      arsPortLoadFutureWeight = std::nullopt;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
  std::optional<SaiPortTraits::Attributes::ArsLinkState> arsLinkState =
      std::nullopt;
#endif
  if (FLAGS_flowletSwitchingEnable &&
      platform_->getAsic()->isSupported(HwAsic::Feature::ARS)) {
    auto flowletCfg = swPort->getPortFlowletConfig();
    if (swPort->getFlowletConfigName().has_value() &&
        swPort->getPortFlowletConfig().has_value()) {
      auto flowletCfgPtr = swPort->getPortFlowletConfig().value();
      arsEnable = true;
      if (platform_->getAsic()->getAsicVendor() !=
          HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
        arsPortLoadScalingFactor = flowletCfgPtr->getScalingFactor();
        arsPortLoadPastWeight = flowletCfgPtr->getLoadWeight();
        if (platform_->getAsic()->isSupported(
                HwAsic::Feature::ARS_FUTURE_PORT_LOAD)) {
          arsPortLoadFutureWeight = flowletCfgPtr->getQueueWeight();
        }
      }
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS) && \
    defined(BRCM_SAI_SDK_GTE_13_0)
      if (swPort->getLoopbackMode() == cfg::PortLoopbackMode::MAC) {
        arsLinkState = SAI_PORT_ARS_LINK_STATE_UP;
      }
#endif
    }
  }
#endif

  std::optional<SaiPortTraits::Attributes::ReachabilityGroup>
      reachabilityGroup{};
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  if (auto reachabilityGroupId = swPort->getReachabilityGroupId()) {
    reachabilityGroup = SaiPortTraits::Attributes::ReachabilityGroup{
        static_cast<uint32_t>(reachabilityGroupId.value())};
  }
#endif

  std::optional<SaiPortTraits::Attributes::CondEntropyRehashEnable>
      condEntropyRehashEnable{};
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  condEntropyRehashEnable = swPort->getConditionalEntropyRehash();
#endif

  std::optional<SaiPortTraits::Attributes::FecErrorDetectEnable>
      fecErrorDetectEnable{};
#if defined(BRCM_SAI_SDK_DNX_GTE_11_7)
  if (auto portFecErrorDetectEnable = swPort->getFecErrorDetectEnable()) {
    fecErrorDetectEnable = SaiPortTraits::Attributes::FecErrorDetectEnable{
        *portFecErrorDetectEnable};
  }
#endif

  std::optional<SaiPortTraits::Attributes::StaticModuleId> staticModuleId{};
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0)
  staticModuleId = swPort->getPortSwitchId();
#endif

#if defined(FBOSS_SAI_PORT_LINK_DOWN_DEBOUNCE_PERIOD)
#if defined(FBOSS_SAI_PORT_LINK_UP_DEBOUNCE_PERIOD)
  std::optional<SaiPortTraits::Attributes::LinkUpDebouncePeriodMs>
      linkUpDebounce{};
#endif
  std::optional<SaiPortTraits::Attributes::LinkDownDebouncePeriodMs>
      linkDownDebounce{};
  // BRCM refuses both the get and the set of its debounce timer unless the
  // port is already in hardware linkscan, so the timer can only be programmed
  // on ports the config also puts in that mode. Leaba has no such tie-in and
  // never reports a linkscan mode at all, so only gate the BRCM build.
  bool debounceProgrammable = true;
#if defined(BRCM_SAI_SDK_GTE_15_4)
  debounceProgrammable =
      swPort->getLinkScanMode() == cfg::LinkScanMode::HARDWARE;
#endif
  const bool upHoldoffIsPerPort = !platform_->getAsic()->isSupported(
      HwAsic::Feature::SWITCH_WIDE_LINK_UP_DEBOUNCE);
  if (platform_->getAsic()->isSupported(HwAsic::Feature::PORT_DEBOUNCE) &&
      debounceProgrammable) {
    // Hold timers (ms) the SDK applies before reporting link up/down events.
    // Unset on the swPort maps to the SDK default of "no debounce".
    constexpr sai_uint32_t kSdkDefaultLinkDebouncePeriodMs = 0;
#if defined(BRCM_SAI_SDK_GTE_15_4)
    constexpr sai_uint32_t kLinkDownDebounceUnitsPerMs = 1000;
#else
    constexpr sai_uint32_t kLinkDownDebounceUnitsPerMs = 1;
#endif
#if defined(FBOSS_SAI_PORT_LINK_UP_DEBOUNCE_PERIOD)
    linkUpDebounce = SaiPortTraits::Attributes::LinkUpDebouncePeriodMs{
        static_cast<sai_uint32_t>(swPort->getPortUpHoldoffTimeMs().value_or(
            kSdkDefaultLinkDebouncePeriodMs))};
#else
    if (upHoldoffIsPerPort && swPort->getPortUpHoldoffTimeMs().has_value()) {
      throw FbossError(
          "Per-port link up debounce timer (portUpHoldoffTimeMs) is not "
          "supported by this SAI SDK; cannot apply to port ",
          swPort->getID());
    }
#endif
    // Leaba takes this timer in milliseconds (hence the attribute name), but
    // the BRCM extension it maps to is documented -- and implemented -- in
    // microseconds. Broadcom additionally rounds nothing: only exactly
    // 0/50/75/100/150/250/500ms are accepted, anything else fails the set with
    // SAI_STATUS_INVALID_ATTR_VALUE_0.
    linkDownDebounce = SaiPortTraits::Attributes::LinkDownDebouncePeriodMs{
        static_cast<sai_uint32_t>(
            swPort->getPortDownHoldoffTimeMs().value_or(
                kSdkDefaultLinkDebouncePeriodMs) *
            kLinkDownDebounceUnitsPerMs)};
  } else if (
      (upHoldoffIsPerPort && swPort->getPortUpHoldoffTimeMs().has_value()) ||
      swPort->getPortDownHoldoffTimeMs().has_value()) {
    if (!platform_->getAsic()->isSupported(HwAsic::Feature::PORT_DEBOUNCE)) {
      throw FbossError(
          "Per-port link debounce timers (portUpHoldoffTimeMs / "
          "portDownHoldoffTimeMs) are not supported on this ASIC; cannot apply "
          "to port ",
          swPort->getID());
    }
    throw FbossError(
        "Per-port link debounce timers require hardware linkscan; set "
        "linkScanMode to HARDWARE on port ",
        swPort->getID());
  }
#else
  if (swPort->getPortUpHoldoffTimeMs().has_value() ||
      swPort->getPortDownHoldoffTimeMs().has_value()) {
    throw FbossError(
        "Per-port link debounce timers (portUpHoldoffTimeMs / "
        "portDownHoldoffTimeMs) are only supported on Leaba SAI SDK 26.2 or "
        "Broadcom SAI SDK 15.4 or newer; cannot apply to port ",
        swPort->getID());
  }
#endif

  // Left out of the create list unless the port configures a linkscan mode: an
  // attribute the SDK does not accept fails the port create outright.
  std::optional<SaiPortTraits::Attributes::LinkScanMode> linkScanMode;
  if (const auto mode = swPort->getLinkScanMode()) {
    linkScanMode = linkScanModeAttribute(*mode);
  }

  if ((swPort->getCsigTagAction() || swPort->getCsigLinkLocator()) &&
      !platform_->getAsic()->isSupported(HwAsic::Feature::CSIG)) {
    throw FbossError(
        "CSIG is not supported on this ASIC/SDK; cannot apply to port ",
        swPort->getID());
  }
  // cfg::CsigTagAction values match sai_port_csig_tag_action_t.
  std::optional<SaiPortTraits::Attributes::CsigTagAction> csigTagAction;
  if (const auto action = swPort->getCsigTagAction()) {
    csigTagAction = static_cast<sai_int32_t>(*action);
  }
  std::optional<SaiPortTraits::Attributes::CsigLinkLocator> csigLinkLocator;
  if (const auto linkLocator = swPort->getCsigLinkLocator()) {
    csigLinkLocator = static_cast<sai_uint8_t>(*linkLocator);
  }

  if (basicAttributeOnly) {
    return SaiPortTraits::CreateAttributes{
#if defined(BRCM_SAI_SDK_DNX)
        getPortTypeFromCfg(swPort->getPortType()),
#endif
        hwLaneList,
        static_cast<uint32_t>(speed),
        adminState,
        fecMode,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
        std::nullopt,
        std::nullopt,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
        std::nullopt, // Port Fabric Isolate
#endif
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        disableTtl,
        std::nullopt,
        pktTxEnable, /* PktTxEnable */
        std::nullopt, // TAM Object
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
#if !defined(TAJO_SDK)
        std::nullopt,
        std::nullopt,
#endif
        std::nullopt, // TC to Priority Group map
        std::nullopt, // PFC Priority to Queue map
        std::nullopt, // PFC Priority to Priority Group map
#if defined(SAI_CBFC_SUPPORTED)
        std::nullopt, // TC to VC map
        std::nullopt, // Queue to VC map
        std::nullopt, // CBFC sender credit size
        std::nullopt, // CBFC sender packet overhead
        std::nullopt, // CBFC sender credit limit
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        std::nullopt,
#endif
        std::nullopt, // Link Training Enable
        std::nullopt, // FDR Enable
        std::nullopt, // Rx Squelch Enable
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
        std::nullopt, // PFC Deadlock Detection Interval
        std::nullopt, // PFC Deadlock Recovery Interval
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
        std::nullopt, // ARS enable
        std::nullopt, // ARS scaling factor
        std::nullopt, // ARS port load past weight
        std::nullopt, // ARS port load future weight
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
        std::nullopt, // ARS link state
#endif
        std::nullopt, // Reachability Group
        std::nullopt, // CondEntropyRehashEnable
        std::nullopt, // CondEntropyRehashPeriodUS
        std::nullopt, // CondEntropyRehashSeed
        std::nullopt, // ShelEnable
#if defined(CHENAB_SAI_SDK)
        false,
#endif
        fecErrorDetectEnable,
        std::nullopt, // AmIdles
        std::nullopt, // FabricSystemPort
        staticModuleId,
        std::nullopt, // IsHyperPortMember
        std::nullopt, // HyperPortMemberList
        std::nullopt, // PfcMonitorDirection
        std::nullopt, // QosDot1pToTcMap
        std::nullopt, // QosTcAndColorToDot1pMap
        std::nullopt, // QosIngressBufferProfileList
        std::nullopt, // QosEgressBufferProfileList
        std::nullopt, // CablePropagationDelayMediaType
        std::nullopt, // LinkScanMode
#if defined(FBOSS_SAI_PORT_LINK_UP_DEBOUNCE_PERIOD)
        std::nullopt, // LinkUpDebouncePeriodMs
#endif
#if defined(FBOSS_SAI_PORT_LINK_DOWN_DEBOUNCE_PERIOD)
        std::nullopt, // LinkDownDebouncePeriodMs
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
        std::nullopt, // LlrModeLocal
        std::nullopt, // LlrModeRemote
        std::nullopt, // LlrProfile
#endif
        std::nullopt, // PfcPauseDurationOverride
        std::nullopt, // Ingress ACL
        std::nullopt, // IsolationGroup
        std::nullopt, // Metadata
        std::nullopt, // CsigTagAction
        std::nullopt, // CsigLinkLocator
    };
  }
  std::optional<SaiPortTraits::Attributes::PortVlanId> vlanIdAttr{vlanId};
  if (vlanId == 0) {
    // vlan ID with value 0 is invalid
    vlanIdAttr.reset();
  }

  // 1. table name set and programmed  -> that table's object id
  // 2. table name set, not programmed -> unset. Ports are programmed before ACL
  //    tables, so setIngressAcl() binds this later, after the ACL delta.
  // 3. no table name, port bound      -> SAI_NULL_OBJECT_ID. Unset would not
  //    unbind: setObject() skips the hardware write for an unset optional but
  //    still drops the value from the store, leaving the port bound.
  // 4. no table name, port unbound    -> unset, nothing to say.
  // Only for a port that already exists. A new port has no handle;
  // setIngressAcl() binds it later, once the ACL tables exist.
  std::optional<SaiPortTraits::Attributes::IngressAcl> ingressAcl;
  if (const auto* portHandle = getPortHandle(swPort->getID())) {
    if (const auto aclTableName = swPort->getIngressAclTableName()) {
      if (const auto* aclTableHandle =
              managerTable_->aclTableManager().getAclTableHandle(
                  *aclTableName)) {
        ingressAcl = SaiPortTraits::Attributes::IngressAcl{
            aclTableHandle->aclTable->adapterKey()};
      }
    } else if (std::get<std::optional<SaiPortTraits::Attributes::IngressAcl>>(
                   portHandle->port->attributes())) {
      ingressAcl = SaiPortTraits::Attributes::IngressAcl{SAI_NULL_OBJECT_ID};
    }
  }
  XLOGF(
      DBG2,
      "Port {} ingress ACL table {} resolved to {}",
      swPort->getID(),
      swPort->getIngressAclTableName().value_or("none"),
      ingressAcl);

#if defined(SAI_CBFC_SUPPORTED)
  // Only on a port with virtual channels: brcm-sai rejects a sender set on a
  // port with no CBFC.
  std::optional<SaiPortTraits::Attributes::CbfcSenderCreditSize>
      cbfcSenderCreditSize;
  std::optional<SaiPortTraits::Attributes::CbfcSenderPacketOverhead>
      cbfcSenderPacketOverhead;
  std::optional<SaiPortTraits::Attributes::CbfcSenderCreditLimit>
      cbfcSenderCreditLimit;
  if (platform_->getAsic()->isSupported(HwAsic::Feature::CBFC) &&
      swPort->getVirtualChannels()) {
    if (auto creditSize = swPort->getCbfcSenderCreditSize()) {
      cbfcSenderCreditSize = static_cast<sai_uint16_t>(*creditSize);
    }
    if (auto packetOverhead = swPort->getCbfcSenderPacketOverhead()) {
      cbfcSenderPacketOverhead = *packetOverhead;
    }
    if (auto creditLimit = swPort->getCbfcSenderCreditLimit()) {
      cbfcSenderCreditLimit = static_cast<sai_uint32_t>(*creditLimit);
    }
  }
#endif

  return SaiPortTraits::CreateAttributes{
#if defined(BRCM_SAI_SDK_DNX)
      getPortTypeFromCfg(swPort->getPortType()),
#endif
      hwLaneList,
      static_cast<uint32_t>(speed),
      adminState,
      fecMode,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      std::nullopt,
      std::nullopt,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
      isDrained,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      loopbackMode,
#else
      internalLoopbackMode,
#endif
      mediaType,
      globalFlowControlMode,
      vlanIdAttr,
      mtu,
      std::nullopt,
      std::nullopt,
      disableTtl,
      interfaceType,
      std::nullopt,
      std::nullopt, // TAM Object
      std::nullopt, // Ingress Mirror Session
      std::nullopt, // Egress Mirror Session
      std::nullopt, // Ingress Sample Packet
      std::nullopt, // Egress Sample Packet
      std::nullopt, // Ingress mirror sample session
      std::nullopt, // Egress mirror sample session
      prbsPolynomial, // PRBS Polynomial
      prbsConfig, // PRBS Config
      std::nullopt, // Ingress MacSec ACL
      std::nullopt, // Egress MacSec ACL
      systemPortId, // System Port Id
      ptpStatusOpt, // PTP Mode, can be std::nullopt
      portPfcInfo.pfcMode, // PFC Mode
      portPfcInfo.pfcTxRx, // PFC Priorities
#if !defined(TAJO_SDK)
      portPfcInfo.pfcRx, // PFC Rx Priorities
      portPfcInfo.pfcTx, // PFC Tx Priorities
#endif
      std::nullopt, // TC to Priority Group map
      std::nullopt, // PFC Priority to Queue map
      std::nullopt, // PFC Priority to Priority Group map
#if defined(SAI_CBFC_SUPPORTED)
      std::nullopt, // TC to VC map
      std::nullopt, // Queue to VC map
      cbfcSenderCreditSize,
      cbfcSenderPacketOverhead,
      cbfcSenderCreditLimit,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      interFrameGap, // Inter Frame Gap
#endif
      linkTrainingEnable,
      fdrEnable, // FDR Enable,
      std::nullopt, // Rx Lane Squelch Enable
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      std::nullopt, // PFC Deadlock Detection Interval
      std::nullopt, // PFC Deadlock Recovery Interval
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      arsEnable, // ARS enable
      arsPortLoadScalingFactor, // ARS scaling factor
      arsPortLoadPastWeight, // ARS port load past weight
      arsPortLoadFutureWeight, // ARS port load future weight
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
      arsLinkState,
#endif
      reachabilityGroup,
      condEntropyRehashEnable,
      std::nullopt, // CondEntropyRehashPeriodUS
      std::nullopt, // CondEntropyRehashSeed
      std::nullopt, // ShelEnable
#if defined(CHENAB_SAI_SDK)
      false,
#endif
      fecErrorDetectEnable,
      amIdles, // AmIdles
      std::nullopt, // FabricSystemPort
      staticModuleId,
      std::nullopt, // IsHyperPortMember
      std::nullopt, // HyperPortMemberList
      std::nullopt, // PfcMonitorDirection
      std::nullopt, // QosDot1pToTcMap
      std::nullopt, // QosTcAndColorToDot1pMap
      std::nullopt, // QosIngressBufferProfileList
      std::nullopt, // QosEgressBufferProfileList
      propagationDelayMediaType, // CablePropagationDelayMediaType
      linkScanMode, // LinkScanMode
#if defined(FBOSS_SAI_PORT_LINK_UP_DEBOUNCE_PERIOD)
      linkUpDebounce, // LinkUpDebouncePeriodMs
#endif
#if defined(FBOSS_SAI_PORT_LINK_DOWN_DEBOUNCE_PERIOD)
      linkDownDebounce, // LinkDownDebouncePeriodMs
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
      std::nullopt, // LlrModeLocal
      std::nullopt, // LlrModeRemote
      std::nullopt, // LlrProfile
#endif
#if defined(CHENAB_SAI_SDK)
      0xffff, // PfcPauseDurationOverride
#else
      std::nullopt, // PfcPauseDurationOverride
#endif
      ingressAcl,
      std::nullopt, // IsolationGroup
      metadata,
      csigTagAction,
      csigLinkLocator,
  };
}

bool SaiPortManager::linkTrainingSupportedOnPort(
    const std::shared_ptr<Port>& swPort) const {
  return platform_->getAsic()->isSupported(HwAsic::Feature::LINK_TRAINING) &&
      swPort->getPortType() != cfg::PortType::HYPER_PORT;
}

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
static sai_int32_t cfgLlrFrameActionToSai(cfg::LlrFrameAction action) {
  switch (action) {
    case cfg::LlrFrameAction::DISCARD:
      return SAI_LLR_FRAME_ACTION_DISCARD;
    case cfg::LlrFrameAction::BLOCK:
      return SAI_LLR_FRAME_ACTION_BLOCK;
    case cfg::LlrFrameAction::BEST_EFFORT:
      return SAI_LLR_FRAME_ACTION_BEST_EFFORT;
  }
  throw FbossError("Unknown cfg::LlrFrameAction: ", static_cast<int>(action));
}

static SaiPortLlrProfileTraits::CreateAttributes llrProfileAttributes(
    const std::shared_ptr<LlrConfig>& cfg) {
  return SaiPortLlrProfileTraits::CreateAttributes{
      SaiPortLlrProfileTraits::Attributes::OutstandingFramesMax{
          static_cast<sai_uint32_t>(cfg->getOutstandingFramesMax())},
      SaiPortLlrProfileTraits::Attributes::OutstandingBytesMax{
          static_cast<sai_uint32_t>(cfg->getOutstandingBytesMax())},
      SaiPortLlrProfileTraits::Attributes::ReplayTimerMax{
          static_cast<sai_uint32_t>(cfg->getReplayTimerMax())},
      SaiPortLlrProfileTraits::Attributes::ReplayCountMax{
          static_cast<sai_uint8_t>(cfg->getReplayCountMax())},
      SaiPortLlrProfileTraits::Attributes::PcsLostTimeout{
          static_cast<sai_uint32_t>(cfg->getPcsLostTimeout())},
      SaiPortLlrProfileTraits::Attributes::DataAgeTimeout{
          static_cast<sai_uint32_t>(cfg->getDataAgeTimeout())},
      SaiPortLlrProfileTraits::Attributes::InitLlrFrameAction{
          cfgLlrFrameActionToSai(cfg->getInitFrameAction())},
      SaiPortLlrProfileTraits::Attributes::FlushLlrFrameAction{
          cfgLlrFrameActionToSai(cfg->getFlushFrameAction())},
      SaiPortLlrProfileTraits::Attributes::ReInitOnFlush{
          cfg->getReInitOnFlush()},
      SaiPortLlrProfileTraits::Attributes::CtlosTargetSpacing{
          static_cast<sai_uint16_t>(cfg->getCtlosTargetSpacing())}};
}
#endif

// Whether the LLR profile bound to a port in hardware differs from the one its
// config resolves to. This is the SwitchState-level llrConfigChanged() asked at
// the SAI layer, for callers that have no old Port to compare against.
//
// It is the only thing that decides whether LLR is written: programLlr() skips
// its writes when the binding already matches, and addPortImpl() holds the
// port's admin enable only when it does not, since a profile cannot be attached
// or detached on an enabled port (CS00012478409).
//
// Matching is the warm boot case, where addPortImpl replays against a store
// holding the object reclaimed from hardware on a port that is up and
// forwarding. An existing port object is not on its own evidence of a match:
// the flexport VCO path arrives with one too, pre-created by
// createPortWithBasicAttributes() administratively enabled and carrying no
// profile.
bool SaiPortManager::llrProfileBindingChanged(
    [[maybe_unused]] std::optional<sai_object_id_t> boundProfile,
    [[maybe_unused]] const std::shared_ptr<Port>& swPort) {
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  // The OID config asks for, unset if it asks for no LLR. Resolving the profile
  // by content key is what turns the config into an OID to compare against.
  std::optional<sai_object_id_t> wanted;
  auto llrConfig = swPort->getLlrConfig();
  if (swPort->getLlrConfigName().has_value() && llrConfig.has_value()) {
    SaiPortLlrProfileTraits::AdapterHostKey key =
        llrProfileAttributes(llrConfig.value());
    auto profile = saiStore_->get<SaiPortLlrProfileTraits>().get(key);
    if (!profile) {
      // Config wants a profile that has never been created, so whatever the
      // port is bound to, it is not that one.
      return true;
    }
    wanted = profile->adapterKey();
  }
  return boundProfile != wanted;
#else
  return false;
#endif
}

// Program UEC Link Layer Retry (UE Spec 1.0.2 section 5.1) on a port from its
// resolved LlrConfig. Creates a content-keyed SAI PORT_LLR_PROFILE (shared and
// warm-boot reclaimed via SaiStore) and binds it to the port. Modes are
// disabled first so the profile is configured while LLR is not transmitting.
//
// The port must be administratively disabled on entry: a mode cannot be cleared
// and a profile cannot be attached or detached on an enabled port
// (CS00012478409). Both callers hold to that through holdAdminEnableForLlr(),
// which forces admin state down in the attributes the port object is written
// from and leaves the caller to enable it once this returns.
void SaiPortManager::programLlr(
    [[maybe_unused]] std::shared_ptr<Port> swPort,
    [[maybe_unused]] SaiPortHandle* portHandle) {
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  if (!llrSupported(platform_)) {
    return;
  }
  auto llrConfig = swPort->getLlrConfig();
  const bool wantLlr =
      swPort->getLlrConfigName().has_value() && llrConfig.has_value();

  std::shared_ptr<SaiPortLlrProfile> profile;
  if (wantLlr) {
    auto attributes = llrProfileAttributes(llrConfig.value());
    auto& store = saiStore_->get<SaiPortLlrProfileTraits>();
    SaiPortLlrProfileTraits::AdapterHostKey key = attributes;
    // Unconditional, and before the check below. On warm boot this reclaims the
    // object the store loaded from hardware and claims its warm boot handle, so
    // it is not swept as unreferenced -- which has to happen even on the path
    // that writes nothing else.
    profile = store.setObject(key, attributes);
  }

  // The binding already matches, so there is nothing to write. Rebinding an
  // unchanged profile would tear down a live session for nothing:
  // LlrModeLocal{false} stops acknowledging the partner, and LlrModeRemote is a
  // one-shot LLR_INIT rather than a persistent enable (CS00012475411). This
  // also covers a port that wants no LLR and has none, where touching the LLR
  // attributes at all would issue a set_port_attribute that returns
  // NOT_SUPPORTED on SDK drops shipping the 1.18 headers without a runtime LLR
  // implementation, crashing normal port bring-up.
  if (!llrProfileBindingChanged(boundLlrProfile(portHandle), swPort)) {
    portHandle->llrProfile = std::move(profile);
    return;
  }

  // Clear both LLR modes so the profile is bound while LLR is idle.
  portHandle->port->setOptionalAttribute(
      SaiPortTraits::Attributes::LlrModeLocal{false});
  portHandle->port->setOptionalAttribute(
      SaiPortTraits::Attributes::LlrModeRemote{false});

  if (wantLlr) {
    // Bind before releasing the old handle: the move below drops the last
    // reference to the old profile and removes it from hardware, so the port
    // must not still point at that OID.
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::LlrProfile{profile->adapterKey()});
    portHandle->llrProfile = std::move(profile);
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::LlrModeLocal{true});
    // The port is down here, so this sets the mode bit without sending an
    // LLR_INIT. brcm-sai 16.0EA4 re-arms it on link up (CS00012475411).
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::LlrModeRemote{true});
  } else {
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::LlrProfile{SAI_NULL_OBJECT_ID});
    portHandle->llrProfile.reset();
  }
#endif
}

void SaiPortManager::programSerdes(
    std::shared_ptr<SaiPort> saiPort,
    std::shared_ptr<Port> swPort,
    SaiPortHandle* portHandle) {
  if (!platform_->isSerdesApiSupported() ||
      !platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_PORT_SERDES_PROGRAMMING) ||
      swPort->getPortType() == cfg::PortType::RECYCLE_PORT ||
      swPort->getPortType() == cfg::PortType::EVENTOR_PORT ||
      swPort->getPortType() == cfg::PortType::HYPER_PORT) {
    return;
  }

  SaiPortSerdesTraits::AdapterHostKey serdesKey{saiPort->adapterKey()};
  auto& store = saiStore_->get<SaiPortSerdesTraits>();
  // check if serdes object already exists for given port,
  // either already programmed or reloaded from adapter.
  std::shared_ptr<SaiPortSerdes> serdes = store.get(serdesKey);

  if (!serdes) {
    /* Serdes programming model is changed starting brcm-sai 6.0.0.14, where
     * Serdes objects are no longer created beforehand. Therefore, agent only
     * needs to create a new serdes object without knowing the serdes Id or
     * reloading the object to sai store.
     */
#if !defined(BRCM_SAI_SDK_XGS_AND_DNX)
    // serdes is not yet programmed or reloaded from adapter
    std::optional<SaiPortTraits::Attributes::SerdesId> serdesAttr{};
    auto serdesId = SaiApiTable::getInstance()->portApi().getAttribute(
        saiPort->adapterKey(), serdesAttr);
    if (serdesId.has_value() && serdesId.value() != SAI_NULL_OBJECT_ID) {
      // but default serdes exists in the adapter.
      serdes =
          store.reloadObject(static_cast<PortSerdesSaiId>(serdesId.value()));
    }
#endif
  } else {
    // ensure warm boot handles are reclaimed removed
    // no-op if serdes is already programmed
    // remove warm boot handle if reloaded from adapter but not yet programmed
    serdes = store.setObject(serdesKey, serdes->attributes());
  }

  // Check if there are expected tx/rx settings from SW port, the number of
  // lanes should match to the number of portKey
  auto numExpectedTxLanes = 0;
  auto numExpectedRxLanes = 0;
  for (const auto& pinConfig : swPort->getPinConfigs()) {
    if (auto tx = pinConfig.tx()) {
      ++numExpectedTxLanes;
    }
    if (auto rx = pinConfig.rx()) {
      ++numExpectedRxLanes;
    }
  }
  auto numPmdLanes =
      GET_ATTR(Port, HwLaneList, saiPort->adapterHostKey()).size();
  if (!hwLaneListIsPmdLaneList_) {
    // On Tomahawk4, HwLaneList means physical port list instead of pmd lane
    // list for now. One physical port maps to two pmd lanes, except for one
    // lane use case like 10G.
    if (static_cast<cfg::PortSpeed>(GET_ATTR(
            Port, Speed, saiPort->attributes())) >= cfg::PortSpeed::FORTYG) {
      numPmdLanes *= 2;
    }
  }
  if (numExpectedTxLanes) {
    CHECK_EQ(numExpectedTxLanes, numPmdLanes)
        << "some lanes are missing for tx-settings";
  }
  if (numExpectedRxLanes) {
    CHECK_EQ(numExpectedRxLanes, numPmdLanes)
        << "some lanes are missing for rx-settings";
  }

  // Check if the platform supports setting zero preemphasis.
  // TH4 and TH5 starts supporting zero preemphasis starting 11.0
#if defined(BRCM_SAI_SDK_GTE_11_0)
  bool supportsZeroPreemphasis =
      platform_->getAsic()->isSupported(
          HwAsic::Feature::PORT_SERDES_ZERO_PREEMPHASIS) ||
      platform_->getAsic()->getAsicType() ==
          cfg::AsicType::ASIC_TYPE_TOMAHAWK4 ||
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK5;
#else
  bool supportsZeroPreemphasis = platform_->getAsic()->isSupported(
      HwAsic::Feature::PORT_SERDES_ZERO_PREEMPHASIS);
#endif

  bool linkTrainingEnabled = linkTrainingSupportedOnPort(swPort) &&
      swPort->getLinkTraining().value_or(false);

  // Note: We currently use LT only on broadcom, we will need to see if any
  // attributes need to be programmed on other vendors
  bool skipSerdesProgramming = linkTrainingEnabled;

  // TODO: Remove the flag fallback once precoding is populated in all port
  // configs.
  const auto txPrecodingEnabled =
      FLAGS_montblanc_precoding || swPort->getTxPrecoding().value_or(false);
  const auto rxPrecodingEnabled =
      FLAGS_montblanc_precoding || swPort->getRxPrecoding().value_or(false);
  SaiPortSerdesTraits::CreateAttributes serdesAttributes =
      serdesAttributesFromSwPinConfigs(
          saiPort->adapterKey(),
          swPort->getPinConfigs(),
          serdes,
          swPort->getZeroPreemphasis() && supportsZeroPreemphasis,
          swPort->getSerdesCustomCollection(),
          skipSerdesProgramming,
          txPrecodingEnabled,
          rxPrecodingEnabled);
  if (serdes &&
      checkPortSerdesAttributes(serdes->attributes(), serdesAttributes)) {
    portHandle->serdes = serdes;
    return;
  }
  // Currently TH3 requires setting sixtap attributes at once, but sai
  // interface only suppports setAttribute() one at a time. Therefore, we'll
  // need to remove the serdes object and then recreate with the sixtap
  // attributes.
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_PORT_SERDES_FIELDS_RESET) &&
      serdes) {
    // Give up all references to the serdes object to delete the serdes object.

    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::PrbsConfig{SAI_PORT_PRBS_CONFIG_DISABLE});
    portHandle->serdes.reset();
    serdes.reset();
  }
  if (platform_->getAsic()->getAsicType() ==
          cfg::AsicType::ASIC_TYPE_TOMAHAWK3 &&
      swPort->getZeroPreemphasis()) {
    createSerdesWithZeroPreemphasis(portHandle, swPort->getPinConfigs());
  }
  // Adopt existing serdes on ITEM_ALREADY_EXISTS (XGS keeps it on recreate).
  auto setSerdesObject =
      [&](const SaiPortSerdesTraits::CreateAttributes& serdesCreateAttributes) {
        try {
          return store.setObject(serdesKey, serdesCreateAttributes);
        } catch (const SaiApiError& e) {
          if (e.getSaiStatus() != SAI_STATUS_ITEM_ALREADY_EXISTS) {
            throw;
          }
          std::optional<SaiPortTraits::Attributes::SerdesId> serdesIdAttr{};
          auto serdesId = SaiApiTable::getInstance()->portApi().getAttribute(
              saiPort->adapterKey(), serdesIdAttr);
          if (!serdesId.has_value() || serdesId.value() == SAI_NULL_OBJECT_ID) {
            // Nothing to adopt; surface the original error.
            throw;
          }
          XLOG(WARNING) << "Port serdes already exists for port "
                        << swPort->getID()
                        << "; adopting existing serdes object "
                        << serdesId.value();
          store.reloadObject(static_cast<PortSerdesSaiId>(serdesId.value()));
          return store.setObject(serdesKey, serdesCreateAttributes);
        }
      };
  if (platform_->getAsic()->getAsicType() ==
          cfg::AsicType::ASIC_TYPE_TOMAHAWK5 ||
      platform_->getAsic()->getAsicType() ==
          cfg::AsicType::ASIC_TYPE_TOMAHAWK6 ||
      platform_->getAsic()->getAsicType() ==
          cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1) {
    // TODO(daiweix): enable SAI_PORT_SERDES_FIELDS_RESET feature for TH4, TH5
    // and TH6, so as to set sixtap attributes all at once. Also need to verify
    // no tests broken because of it.
    auto platformPort = platform_->getPort(swPort->getID());
    if (platformPort->getPortType() == cfg::PortType::MANAGEMENT_PORT) {
      XLOG(DBG2)
          << "Tomahawk5/6/Ultra1 management port only support 3 tap serdes setting";
      std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirPre2>>(
          serdesAttributes) = std::nullopt;
      std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirPre3>>(
          serdesAttributes) = std::nullopt;
      std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirPost2>>(
          serdesAttributes) = std::nullopt;
      std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirPost3>>(
          serdesAttributes) = std::nullopt;
    }
    auto newTxFirMain =
        std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirMain>>(
            serdesAttributes);
    if (swPort->getZeroPreemphasis() && supportsZeroPreemphasis &&
        newTxFirMain.has_value()) {
      // Broadcom rejects *creating* a port serdes object with TxFirMain set to
      // 0. When zero preemphasis is requested (e.g. HW loopback tests) and the
      // serdes is being created from scratch (such as during a VCO change that
      // recreates the port), create it with only preemphasis set and leave the
      // TX FIR taps unprogrammed. The setObject() below then programs the
      // zeroed taps as an update, which the SDK does allow. This only matters
      // when the profile actually has TX FIR taps; copper profiles have no
      // TxFirMain and fall through to the path below.
      createSerdesWithZeroPreemphasis(portHandle, swPort->getPinConfigs());
    } else if (newTxFirMain.has_value()) {
      // set main txfir only first to avoid programming errors, see
      // CS00012393198
      auto attributes = serdesAttributes;
      auto numLanes = newTxFirMain.value().value().size();
      SaiPortSerdesTraits::Attributes::TxFirPre1::ValueType txPre1;
      txPre1.resize(numLanes, 0);
      std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirPre1>>(
          attributes) = txPre1;
      std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirMain>>(
          attributes) = newTxFirMain;
      SaiPortSerdesTraits::Attributes::TxFirPre1::ValueType txPost1;
      txPost1.resize(numLanes, 0);
      std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirPost1>>(
          attributes) = txPost1;
      portHandle->serdes = setSerdesObject(attributes);
    } else {
      XLOG(DBG2) << "No tx main setting for port " << swPort->getID();
    }
  }
  // create if serdes doesn't exist or update existing serdes
  portHandle->serdes = setSerdesObject(serdesAttributes);

  if (platform_->getAsic()->getAsicType() ==
          cfg::AsicType::ASIC_TYPE_TOMAHAWK5 &&
      platform_->getHwSwitch()->getBootType() == BootType::COLD_BOOT &&
      swPort->getAdminState() == cfg::PortState::ENABLED) {
    /*
     * SI settings are not programmed to the hardware when the port is
     * created with admin UP. We need to explicitly toggle the admin
     * state temporarily on Garonne to bring the port up to unblock testing.
     * NOTE this will cause flaps during warmboots and cannot be deployed.
     */
    SaiPortTraits::Attributes::AdminState adminDisable{false};
    SaiApiTable::getInstance()->portApi().setAttribute(
        portHandle->port->adapterKey(), adminDisable);
    SaiPortTraits::Attributes::AdminState adminEnable{true};
    SaiApiTable::getInstance()->portApi().setAttribute(
        portHandle->port->adapterKey(), adminEnable);
  }
}

SaiPortSerdesTraits::CreateAttributes
SaiPortManager::serdesAttributesFromSwPinConfigs(
    PortSaiId portSaiId,
    const std::vector<phy::PinConfig>& pinConfigs,
    const std::shared_ptr<SaiPortSerdes>& serdes,
    bool zeroPreemphasis,
    [[maybe_unused]] const std::optional<std::string>& customCollection,
    bool skipSerdesProgramming,
    [[maybe_unused]] bool txPrecodingEnabled,
    [[maybe_unused]] bool rxPrecodingEnabled) {
  SaiPortSerdesTraits::CreateAttributes attrs;

  std::get<SaiPortSerdesTraits::Attributes::PortId>(attrs) =
      static_cast<sai_object_id_t>(portSaiId);

  SaiPortSerdesTraits::Attributes::TxFirPre1::ValueType txPre1;
  SaiPortSerdesTraits::Attributes::TxFirMain::ValueType txMain;
  SaiPortSerdesTraits::Attributes::TxFirPost1::ValueType txPost1;
  SaiPortSerdesTraits::Attributes::IDriver::ValueType txIDriver;
  SaiPortSerdesTraits::Attributes::TxFirPre3::ValueType txPre3;
  SaiPortSerdesTraits::Attributes::TxFirPre2::ValueType txPre2;
  SaiPortSerdesTraits::Attributes::TxFirPost2::ValueType txPost2;
  SaiPortSerdesTraits::Attributes::TxFirPost3::ValueType txPost3;
  SaiPortSerdesTraits::Attributes::TxLutMode::ValueType txLutMode;
  SaiPortSerdesTraits::Attributes::RxCtleCode::ValueType rxCtleCode;
  SaiPortSerdesTraits::Attributes::RxDspMode::ValueType rxDspMode;
  SaiPortSerdesTraits::Attributes::RxAfeTrim::ValueType rxAfeTrim;
  SaiPortSerdesTraits::Attributes::RxAcCouplingByPass::ValueType
      rxAcCouplingByPass;
  // TX Params
  SaiPortSerdesTraits::Attributes::TxDiffEncoderEn::ValueType txDiffEncoderEn;
  SaiPortSerdesTraits::Attributes::TxDigGain::ValueType txDigGain;
  SaiPortSerdesTraits::Attributes::TxFfeCoeff0::ValueType txFfeCoeff0;
  SaiPortSerdesTraits::Attributes::TxFfeCoeff1::ValueType txFfeCoeff1;
  SaiPortSerdesTraits::Attributes::TxFfeCoeff2::ValueType txFfeCoeff2;
  SaiPortSerdesTraits::Attributes::TxFfeCoeff3::ValueType txFfeCoeff3;
  SaiPortSerdesTraits::Attributes::TxFfeCoeff4::ValueType txFfeCoeff4;
  SaiPortSerdesTraits::Attributes::TxDriverSwing::ValueType txDriverSwing;
  SaiPortSerdesTraits::Attributes::TxLdoBypass::ValueType txLdoBypass;
  // RX Params
  SaiPortSerdesTraits::Attributes::RxInstgBoost1Start::ValueType
      rxInstgBoost1Start;
  SaiPortSerdesTraits::Attributes::RxInstgBoost1Step::ValueType
      rxInstgBoost1Step;
  SaiPortSerdesTraits::Attributes::RxInstgBoost1Stop::ValueType
      rxInstgBoost1Stop;
  SaiPortSerdesTraits::Attributes::RxInstgBoost2OrHrStart::ValueType
      rxInstgBoost2OrHrStart;
  SaiPortSerdesTraits::Attributes::RxInstgBoost2OrHrStep::ValueType
      rxInstgBoost2OrHrStep;
  SaiPortSerdesTraits::Attributes::RxInstgBoost2OrHrStop::ValueType
      rxInstgBoost2OrHrStop;
  SaiPortSerdesTraits::Attributes::RxInstgC1Start1p7::ValueType
      rxInstgC1Start1p7;
  SaiPortSerdesTraits::Attributes::RxInstgC1Step1p7::ValueType rxInstgC1Step1p7;
  SaiPortSerdesTraits::Attributes::RxInstgC1Stop1p7::ValueType rxInstgC1Stop1p7;
  SaiPortSerdesTraits::Attributes::RxInstgDfeStart1p7::ValueType
      rxInstgDfeStart1p7;
  SaiPortSerdesTraits::Attributes::RxInstgDfeStep1p7::ValueType
      rxInstgDfeStep1p7;
  SaiPortSerdesTraits::Attributes::RxInstgDfeStop1p7::ValueType
      rxInstgDfeStop1p7;
  SaiPortSerdesTraits::Attributes::RxEnableScanSelection::ValueType
      rxEnableScanSelection;
  SaiPortSerdesTraits::Attributes::RxInstgScanUseSrSettings::ValueType
      rxInstgScanUseSrSettings;
  SaiPortSerdesTraits::Attributes::RxCdrCfgOvEn::ValueType rxCdrCfgOvEn;
  SaiPortSerdesTraits::Attributes::RxCdrTdet1stOrdStepOvVal::ValueType
      rxCdrTdet1stOrdStepOvVal;
  SaiPortSerdesTraits::Attributes::RxCdrTdet2ndOrdStepOvVal::ValueType
      rxCdrTdet2ndOrdStepOvVal;
  SaiPortSerdesTraits::Attributes::RxCdrTdetFineStepOvVal::ValueType
      rxCdrTdetFineStepOvVal;
  SaiPortSerdesTraits::Attributes::RxLdoBypass::ValueType rxLdoBypass;
  SaiPortSerdesTraits::Attributes::RxDiffEncoderEn::ValueType rxDiffEncoderEn;
  SaiPortSerdesTraits::Attributes::RxInstgEnableScan::ValueType
      rxInstgEnableScan;
  SaiPortSerdesTraits::Attributes::RxFfeLengthBitmap::ValueType
      rxFfeLengthBitmap;
  SaiPortSerdesTraits::Attributes::RxFfeLmsDynamicGatingEn::ValueType
      rxFfeLmsDynamicGatingEn;
#if defined(BRCM_SAI_SDK_GTE_13_0)
  std::vector<phy::RxReach> rxReach;
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0) ||            \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) && \
     !defined(BRCM_SAI_SDK_XGS_AND_DNX))
  SaiPortSerdesTraits::Attributes::TxPrecodingAttr::ValueType txPrecoding;
  SaiPortSerdesTraits::Attributes::RxPrecodingAttr::ValueType rxPrecoding;
#endif

  // Now use pinConfigs from SW port as the source of truth
  [[maybe_unused]] auto numExpectedTxLanes = 0;
  auto numExpectedRxLanes = 0;
  for (const auto& pinConfig : pinConfigs) {
#if defined(BRCM_SAI_SDK_GTE_13_0) ||            \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) && \
     !defined(BRCM_SAI_SDK_XGS_AND_DNX))
    if (auto tx = pinConfig.tx(); tx && tx->precoding()) {
      txPrecoding.push_back(tx->precoding().value());
    }
    if (auto rx = pinConfig.rx(); rx && rx->precoding()) {
      rxPrecoding.push_back(rx->precoding().value());
    }
#endif
    if (skipSerdesProgramming) {
      continue;
    }
    if (auto tx = pinConfig.tx()) {
      ++numExpectedTxLanes;
      if (platform_->getAsic()->getAsicType() ==
              cfg::AsicType::ASIC_TYPE_YUBA ||
          platform_->getAsic()->getAsicType() ==
              cfg::AsicType::ASIC_TYPE_G202X ||
          platform_->getAsic()->getAsicType() ==
              cfg::AsicType::ASIC_TYPE_P200) {
        if (auto firPre1 = tx->firPre1()) {
          txPre1.push_back(zeroPreemphasis ? 0 : *firPre1);
        }
        if (auto firPre2 = tx->firPre2()) {
          txPre2.push_back(zeroPreemphasis ? 0 : *firPre2);
        }
        if (auto firPre3 = tx->firPre3()) {
          txPre3.push_back(zeroPreemphasis ? 0 : *firPre3);
        }
        if (auto firMain = tx->firMain()) {
          txMain.push_back(zeroPreemphasis ? 0 : *firMain);
        }
        if (auto firPost1 = tx->firPost1()) {
          txPost1.push_back(zeroPreemphasis ? 0 : *firPost1);
        }
        if (auto diffEncoderEn = tx->diffEncoderEn()) {
          txDiffEncoderEn.push_back(diffEncoderEn.value());
        }
        if (auto digGain = tx->digGain()) {
          txDigGain.push_back(digGain.value());
        }
        if (auto ffeCoeff0 = tx->ffeCoeff0()) {
          txFfeCoeff0.push_back(ffeCoeff0.value());
        }
        if (auto ffeCoeff1 = tx->ffeCoeff1()) {
          txFfeCoeff1.push_back(ffeCoeff1.value());
        }
        if (auto ffeCoeff2 = tx->ffeCoeff2()) {
          txFfeCoeff2.push_back(ffeCoeff2.value());
        }
        if (auto ffeCoeff3 = tx->ffeCoeff3()) {
          txFfeCoeff3.push_back(ffeCoeff3.value());
        }
        if (auto ffeCoeff4 = tx->ffeCoeff4()) {
          txFfeCoeff4.push_back(ffeCoeff4.value());
        }
        if (auto driverSwing = tx->driverSwing()) {
          txDriverSwing.push_back(driverSwing.value());
        }
        if (auto ldoBypass = tx->ldoBypass()) {
          txLdoBypass.push_back(ldoBypass.value());
        }
      } else {
        txPre1.push_back(zeroPreemphasis ? 0 : *tx->pre());
        txMain.push_back(zeroPreemphasis ? 0 : *tx->main());
        txPost1.push_back(zeroPreemphasis ? 0 : *tx->post());
        if (FLAGS_sai_configure_six_tap &&
            platform_->getAsic()->isSupported(
                HwAsic::Feature::SAI_CONFIGURE_SIX_TAP)) {
          txPost2.push_back(zeroPreemphasis ? 0 : *tx->post2());
          txPost3.push_back(zeroPreemphasis ? 0 : *tx->post3());
          txPre2.push_back(zeroPreemphasis ? 0 : *tx->pre2());
          if (platform_->getAsic()->getAsicVendor() ==
              HwAsic::AsicVendor::ASIC_VENDOR_TAJO) {
            if (auto lutMode = tx->lutMode()) {
              txLutMode.push_back(zeroPreemphasis ? 0 : *lutMode);
            }
          }
        }
        if (auto pre3 = tx->pre3()) {
          txPre3.push_back(zeroPreemphasis ? 0 : *pre3);
        }

        if (auto driveCurrent = tx->driveCurrent()) {
          txIDriver.push_back(zeroPreemphasis ? 0 : driveCurrent.value());
        }
      }
    }
    if (auto rx = pinConfig.rx()) {
      ++numExpectedRxLanes;
#if defined(BRCM_SAI_SDK_GTE_13_0)
      if (auto reach = rx->rxReach()) {
        rxReach.push_back(reach.value());
      }
#endif
      if (auto ctlCode = rx->ctlCode()) {
        rxCtleCode.push_back(*ctlCode);
      }
      if (auto dscpMode = rx->dspMode()) {
        rxDspMode.push_back(*dscpMode);
      }
      if (auto afeTrim = rx->afeTrim()) {
        rxAfeTrim.push_back(*afeTrim);
      }
      if (auto acCouplingByPass = rx->acCouplingBypass()) {
        rxAcCouplingByPass.push_back(*acCouplingByPass);
      }
      // RX Params
      if (auto instgBoost1Start = rx->instgBoost1Start()) {
        rxInstgBoost1Start.push_back(instgBoost1Start.value());
      }
      if (auto instgBoost1Step = rx->instgBoost1Step()) {
        rxInstgBoost1Step.push_back(instgBoost1Step.value());
      }
      if (auto instgBoost1Stop = rx->instgBoost1Stop()) {
        rxInstgBoost1Stop.push_back(instgBoost1Stop.value());
      }
      if (auto instgBoost2OrHrStart = rx->instgBoost2OrHrStart()) {
        rxInstgBoost2OrHrStart.push_back(instgBoost2OrHrStart.value());
      }
      if (auto instgBoost2OrHrStep = rx->instgBoost2OrHrStep()) {
        rxInstgBoost2OrHrStep.push_back(instgBoost2OrHrStep.value());
      }
      if (auto instgBoost2OrHrStop = rx->instgBoost2OrHrStop()) {
        rxInstgBoost2OrHrStop.push_back(instgBoost2OrHrStop.value());
      }
      if (auto instgC1Start1p7 = rx->instgC1Start1p7()) {
        rxInstgC1Start1p7.push_back(instgC1Start1p7.value());
      }
      if (auto instgC1Step1p7 = rx->instgC1Step1p7()) {
        rxInstgC1Step1p7.push_back(instgC1Step1p7.value());
      }
      if (auto instgC1Stop1p7 = rx->instgC1Stop1p7()) {
        rxInstgC1Stop1p7.push_back(instgC1Stop1p7.value());
      }
      if (auto instgDfeStart1p7 = rx->instgDfeStart1p7()) {
        rxInstgDfeStart1p7.push_back(instgDfeStart1p7.value());
      }
      if (auto instgDfeStep1p7 = rx->instgDfeStep1p7()) {
        rxInstgDfeStep1p7.push_back(instgDfeStep1p7.value());
      }
      if (auto instgDfeStop1p7 = rx->instgDfeStop1p7()) {
        rxInstgDfeStop1p7.push_back(instgDfeStop1p7.value());
      }
      if (auto enableScanSelection = rx->enableScanSelection()) {
        rxEnableScanSelection.push_back(enableScanSelection.value());
      }
      if (auto instgScanUseSrSettings = rx->instgScanUseSrSettings()) {
        rxInstgScanUseSrSettings.push_back(instgScanUseSrSettings.value());
      }
      if (auto cdrCfgOvEn = rx->cdrCfgOvEn()) {
        rxCdrCfgOvEn.push_back(cdrCfgOvEn.value());
      }
      if (auto cdrTdet1stOrdStepOvVal = rx->cdrTdet1stOrdStepOvVal()) {
        rxCdrTdet1stOrdStepOvVal.push_back(cdrTdet1stOrdStepOvVal.value());
      }
      if (auto cdrTdet2ndOrdStepOvVal = rx->cdrTdet2ndOrdStepOvVal()) {
        rxCdrTdet2ndOrdStepOvVal.push_back(cdrTdet2ndOrdStepOvVal.value());
      }
      if (auto cdrTdetFineStepOvVal = rx->cdrTdetFineStepOvVal()) {
        rxCdrTdetFineStepOvVal.push_back(cdrTdetFineStepOvVal.value());
      }
      if (auto ldoBypass = rx->ldoBypass()) {
        rxLdoBypass.push_back(ldoBypass.value());
      }
      if (auto diffEncoderEn = rx->diffEncoderEn()) {
        rxDiffEncoderEn.push_back(diffEncoderEn.value());
      }
      if (auto instgEnableScan = rx->instgEnableScan()) {
        rxInstgEnableScan.push_back(instgEnableScan.value());
      }
      if (auto ffeLengthBitmap = rx->ffeLengthBitmap()) {
        rxFfeLengthBitmap.push_back(ffeLengthBitmap.value());
      }
      if (auto ffeLmsDynamicGatingEn = rx->ffeLmsDynamicGatingEn()) {
        rxFfeLmsDynamicGatingEn.push_back(ffeLmsDynamicGatingEn.value());
      }
    }
  }

  auto setTxRxAttr = [](auto& attrs, auto type, const auto& val) {
    auto& attr = std::get<std::optional<std::decay_t<decltype(type)>>>(attrs);
    if (!val.empty()) {
      attr = val;
    }
  };

#if defined(BRCM_SAI_SDK_GTE_13_0) ||            \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) && \
     !defined(BRCM_SAI_SDK_XGS_AND_DNX))
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_SERDES_PRECODING)) {
    if (txPrecodingEnabled) {
      setTxRxAttr(
          attrs,
          SaiPortSerdesTraits::Attributes::TxPrecodingAttr{},
          txPrecoding);
    }
    if (rxPrecodingEnabled) {
      setTxRxAttr(
          attrs,
          SaiPortSerdesTraits::Attributes::RxPrecodingAttr{},
          rxPrecoding);
    }
  }
#endif
  if (skipSerdesProgramming) {
    return attrs;
  }

  setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::TxFirPre1{}, txPre1);
  setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::TxFirPost1{}, txPost1);
  setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::TxFirMain{}, txMain);
  setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::IDriver{}, txIDriver);
#if defined(BRCM_SAI_SDK_GTE_13_0)
  if (rxPrecodingEnabled &&
      platform_->getAsic()->isSupported(HwAsic::Feature::SAI_SERDES_RX_REACH)) {
    setTxRxAttr(
        attrs,
        SaiPortSerdesTraits::Attributes::RxReach{},
        getSaiRxReach(rxReach));
  }
#endif

  if (FLAGS_sai_configure_six_tap &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_CONFIGURE_SIX_TAP)) {
    setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::TxFirPre2{}, txPre2);
    setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::TxFirPost2{}, txPost2);
    setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::TxFirPost3{}, txPost3);

    if (platform_->getAsic()->getAsicVendor() ==
        HwAsic::AsicVendor::ASIC_VENDOR_TAJO) {
      setTxRxAttr(
          attrs, SaiPortSerdesTraits::Attributes::TxLutMode{}, txLutMode);
    }
  }

#if defined(TAJO_SDK_GTE_24_8_3001)
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::TxDiffEncoderEn{},
      txDiffEncoderEn);
  setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::TxDigGain{}, txDigGain);
  setTxRxAttr(
      attrs, SaiPortSerdesTraits::Attributes::TxFfeCoeff0{}, txFfeCoeff0);
  setTxRxAttr(
      attrs, SaiPortSerdesTraits::Attributes::TxFfeCoeff1{}, txFfeCoeff1);
  setTxRxAttr(
      attrs, SaiPortSerdesTraits::Attributes::TxFfeCoeff2{}, txFfeCoeff2);
  setTxRxAttr(
      attrs, SaiPortSerdesTraits::Attributes::TxFfeCoeff3{}, txFfeCoeff3);
  setTxRxAttr(
      attrs, SaiPortSerdesTraits::Attributes::TxFfeCoeff4{}, txFfeCoeff4);
  setTxRxAttr(
      attrs, SaiPortSerdesTraits::Attributes::TxDriverSwing{}, txDriverSwing);
  setTxRxAttr(
      attrs, SaiPortSerdesTraits::Attributes::TxLdoBypass{}, txLdoBypass);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgBoost1Start{},
      rxInstgBoost1Start);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgBoost1Step{},
      rxInstgBoost1Step);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgBoost1Stop{},
      rxInstgBoost1Stop);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgBoost2OrHrStart{},
      rxInstgBoost2OrHrStart);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgBoost2OrHrStep{},
      rxInstgBoost2OrHrStep);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgBoost2OrHrStop{},
      rxInstgBoost2OrHrStop);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgC1Start1p7{},
      rxInstgC1Start1p7);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgC1Step1p7{},
      rxInstgC1Step1p7);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgC1Stop1p7{},
      rxInstgC1Stop1p7);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgDfeStart1p7{},
      rxInstgDfeStart1p7);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgDfeStep1p7{},
      rxInstgDfeStep1p7);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgDfeStop1p7{},
      rxInstgDfeStop1p7);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxEnableScanSelection{},
      rxEnableScanSelection);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgScanUseSrSettings{},
      rxInstgScanUseSrSettings);
  setTxRxAttr(
      attrs, SaiPortSerdesTraits::Attributes::RxCdrCfgOvEn{}, rxCdrCfgOvEn);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxCdrTdet1stOrdStepOvVal{},
      rxCdrTdet1stOrdStepOvVal);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxCdrTdet2ndOrdStepOvVal{},
      rxCdrTdet2ndOrdStepOvVal);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxCdrTdetFineStepOvVal{},
      rxCdrTdetFineStepOvVal);
  setTxRxAttr(
      attrs, SaiPortSerdesTraits::Attributes::RxLdoBypass{}, rxLdoBypass);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxDiffEncoderEn{},
      rxDiffEncoderEn);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxInstgEnableScan{},
      rxInstgEnableScan);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxFfeLengthBitmap{},
      rxFfeLengthBitmap);
  setTxRxAttr(
      attrs,
      SaiPortSerdesTraits::Attributes::RxFfeLmsDynamicGatingEn{},
      rxFfeLmsDynamicGatingEn);
#endif

  if (!txPre3.empty()) {
    setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::TxFirPre3{}, txPre3);
  }

#if !defined(CHENAB_SAI_SDK)
  if (platform_->getAsic()->getPortSerdesPreemphasis().has_value() ||
      zeroPreemphasis) {
    SaiPortSerdesTraits::Attributes::Preemphasis::ValueType preempahsis(
        numExpectedTxLanes,
        zeroPreemphasis
            ? 0
            : platform_->getAsic()->getPortSerdesPreemphasis().value());
    setTxRxAttr(
        attrs, SaiPortSerdesTraits::Attributes::Preemphasis{}, preempahsis);
  }
#endif

  if (numExpectedRxLanes &&
      platform_->getAsic()->getAsicVendor() ==
          HwAsic::AsicVendor::ASIC_VENDOR_TAJO) {
    setTxRxAttr(
        attrs, SaiPortSerdesTraits::Attributes::RxCtleCode{}, rxCtleCode);

    /*
     * TODO (srikrishnagopu) : Remove after rolling out AFE TRIM (S249471)
     * Coldboot. The DspMode and AfeTrim values provided by vendor are swapped
     * (incorrect) for 25G serdes. With this diff stack (D31939792), it is
     * rectified but this cannot be rolled out during warmboot since this will
     * cause port flaps.
     *
     * Hence, swap the values back if its a warmboot. As we are scheduling
     * coldboots for AFE TRIM SEV S249471, utilize this to propagate the correct
     * values.
     *
     * This will help us carry on the warmboot upgrades on 400C devices while
     * we wait on coldboots to happen over a period of 3 months.
     *
     * --------------------------------------------------------------------
     *               | PIN DSP | PIN AFE | STORE DSP | STORE AFE | ACTION |
     * --------------------------------------------------------------------
     * WB before CB  |    7    |    4    |     4     |     7     |  SWAP  |
     * --------------------------------------------------------------------
     *       CB      |    7    |    4    |     -     |     -     |  SKIP  |
     * --------------------------------------------------------------------
     * WB after CB   |    7    |    4    |     7     |     4     |  SKIP  |
     * --------------------------------------------------------------------
     */
    if (platform_->getHwSwitch()->getBootType() == BootType::WARM_BOOT &&
        (platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_EBRO ||
         platform_->getAsic()->getAsicType() ==
             cfg::AsicType::ASIC_TYPE_P200) &&
        serdes) {
      auto rxDspModeFromStore = std::get<std::optional<std::decay_t<
          decltype(SaiPortSerdesTraits::Attributes::RxDspMode{})>>>(
          serdes->attributes());
      auto rxAfeTrimFromStore = std::get<std::optional<std::decay_t<
          decltype(SaiPortSerdesTraits::Attributes::RxAfeTrim{})>>>(
          serdes->attributes());
      if (!rxDspMode.empty() && !rxAfeTrim.empty() &&
          rxDspModeFromStore.has_value() && rxAfeTrimFromStore.has_value() &&
          !rxDspModeFromStore.value().value().empty() &&
          !rxAfeTrimFromStore.value().value().empty() && rxDspMode[0] == 7 &&
          rxAfeTrim[0] == 4 && rxDspModeFromStore.value().value()[0] == 4 &&
          rxAfeTrimFromStore.value().value()[0] == 7) {
        rxDspMode.swap(rxAfeTrim);
      }
    }
    setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::RxDspMode{}, rxDspMode);
    setTxRxAttr(attrs, SaiPortSerdesTraits::Attributes::RxAfeTrim{}, rxAfeTrim);
    setTxRxAttr(
        attrs,
        SaiPortSerdesTraits::Attributes::RxAcCouplingByPass{},
        rxAcCouplingByPass);
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
  if (customCollection.has_value()) {
    std::get<std::optional<SaiPortSerdesTraits::Attributes::CustomCollection>>(
        attrs) = SaiPortSerdesTraits::Attributes::CustomCollection{
        customCollection.value()};
  }
#endif
  return attrs;
}

void SaiPortManager::createSerdesWithZeroPreemphasis(
    SaiPortHandle* portHandle,
    const std::vector<phy::PinConfig>& pinConfigs) {
  SaiPortSerdesTraits::CreateAttributes attributes;

  auto portSaiId = portHandle->port->adapterKey();
  std::get<SaiPortSerdesTraits::Attributes::PortId>(attributes) =
      static_cast<sai_object_id_t>(portSaiId);

  [[maybe_unused]] auto numExpectedTxLanes = 0;
  for (const auto& pinConfig : pinConfigs) {
    if (auto tx = pinConfig.tx()) {
      ++numExpectedTxLanes;
    }
  }

#if !defined(CHENAB_SAI_SDK)
  // Only program Preemphasis when the profile actually has TX lanes. Setting an
  // empty Preemphasis vector (e.g. for copper profiles with no tx pin configs)
  // is rejected by the SDK with INVALID ATTRIBUTE MAX.
  if (numExpectedTxLanes > 0) {
    SaiPortSerdesTraits::Attributes::Preemphasis::ValueType preemphasis;
    preemphasis.resize(numExpectedTxLanes, 0);
    std::get<std::optional<std::decay_t<
        decltype(SaiPortSerdesTraits::Attributes::Preemphasis{})>>>(
        attributes) = preemphasis;
  }
#endif
  SaiPortSerdesTraits::AdapterHostKey serdesKey{portSaiId};
  auto& store = saiStore_->get<SaiPortSerdesTraits>();
  portHandle->serdes = store.setObject(serdesKey, attributes);

  // Reload attributes
  reloadSixTapAttributes(portHandle, attributes);
  portHandle->serdes->setAttributes(attributes, true /* skipHwWrite */);
}
} // namespace facebook::fboss
