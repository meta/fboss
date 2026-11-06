/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmSwitch.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <unordered_set>
#include <utility>

#include <boost/filesystem/operations.hpp>
#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/Synchronized.h>
#include <folly/hash/Hash.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/Constants.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/LacpTypes.h"
#include "fboss/agent/LoadBalancerUtils.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/hw/BufferStatsLogger.h"
#include "fboss/agent/hw/HwSwitchFb303Stats.h"
#include "fboss/agent/hw/UnsupportedFeatureManager.h"
#include "fboss/agent/hw/bcm/BcmAPI.h"
#include "fboss/agent/hw/bcm/BcmAclEntry.h"
#include "fboss/agent/hw/bcm/BcmAclTable.h"
#include "fboss/agent/hw/bcm/BcmAddressFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmBstStatsMgr.h"
#include "fboss/agent/hw/bcm/BcmControlPlane.h"
#include "fboss/agent/hw/bcm/BcmCosManager.h"
#include "fboss/agent/hw/bcm/BcmEcmpUtils.h"
#include "fboss/agent/hw/bcm/BcmEgressManager.h"
#include "fboss/agent/hw/bcm/BcmEgressQueueFlexCounter.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmExactMatchUtils.h"
#include "fboss/agent/hw/bcm/BcmFieldProcessorUtils.h"
#include "fboss/agent/hw/bcm/BcmHost.h"
#include "fboss/agent/hw/bcm/BcmHostKey.h"
#include "fboss/agent/hw/bcm/BcmHostUtils.h"
#include "fboss/agent/hw/bcm/BcmIngressFieldProcessorFlexCounter.h"
#include "fboss/agent/hw/bcm/BcmIntf.h"
#include "fboss/agent/hw/bcm/BcmLabelMap.h"
#include "fboss/agent/hw/bcm/BcmLabelSwitchingUtils.h"
#include "fboss/agent/hw/bcm/BcmLogBuffer.h"
#include "fboss/agent/hw/bcm/BcmMacTable.h"
#include "fboss/agent/hw/bcm/BcmMirrorTable.h"
#include "fboss/agent/hw/bcm/BcmNextHop.h"
#include "fboss/agent/hw/bcm/BcmPlatform.h"
#include "fboss/agent/hw/bcm/BcmPort.h"
#include "fboss/agent/hw/bcm/BcmPortGroup.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"
#include "fboss/agent/hw/bcm/BcmPrbs.h"
#include "fboss/agent/hw/bcm/BcmPtpTcMgr.h"
#include "fboss/agent/hw/bcm/BcmQcmManager.h"
#include "fboss/agent/hw/bcm/BcmQosPolicyTable.h"
#include "fboss/agent/hw/bcm/BcmRoute.h"
#include "fboss/agent/hw/bcm/BcmRouteCounter.h"
#include "fboss/agent/hw/bcm/BcmRtag7LoadBalancer.h"
#include "fboss/agent/hw/bcm/BcmRxPacket.h"
#include "fboss/agent/hw/bcm/BcmSflowExporter.h"
#include "fboss/agent/hw/bcm/BcmStatUpdater.h"
#include "fboss/agent/hw/bcm/BcmSwitchEventCallback.h"
#include "fboss/agent/hw/bcm/BcmSwitchEventUtils.h"
#include "fboss/agent/hw/bcm/BcmSwitchSettings.h"
#include "fboss/agent/hw/bcm/BcmTeFlowTable.h"
#include "fboss/agent/hw/bcm/BcmTrunk.h"
#include "fboss/agent/hw/bcm/BcmTrunkTable.h"
#include "fboss/agent/hw/bcm/BcmTxPacket.h"
#include "fboss/agent/hw/bcm/BcmUdfManager.h"
#include "fboss/agent/hw/bcm/BcmUnit.h"
#include "fboss/agent/hw/bcm/BcmWarmBootCache.h"
#include "fboss/agent/hw/bcm/BcmWarmBootHelper.h"
#include "fboss/agent/hw/bcm/BcmWarmBootState.h"
#include "fboss/agent/hw/bcm/PacketTraceUtils.h"
#include "fboss/agent/hw/bcm/gen-cpp2/packettrace_types.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/mock/MockRxPacket.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/if/gen-cpp2/highfreq_types.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/ArpEntry.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/ForwardingInformationBase.h"
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"
#include "fboss/agent/state/ForwardingInformationBaseDelta.h"
#include "fboss/agent/state/ForwardingInformationBaseMap.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/LoadBalancer.h"
#include "fboss/agent/state/LoadBalancerMap.h"
#include "fboss/agent/state/NodeMapDelta.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/SflowCollector.h"
#include "fboss/agent/state/SflowCollectorMap.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState-defs.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/TeFlowEntry.h"
#include "fboss/agent/state/TransceiverMap.h"
#include "fboss/agent/state/UdfGroup.h"
#include "fboss/agent/state/UdfPacketMatcher.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/agent/state/VlanMapDelta.h"
#include "fboss/agent/types.h"

extern "C" {
#include <bcm/link.h>
#include <bcm/port.h>
#include <bcm/stg.h>
#include <bcm/switch.h>
#include <bcm/vlan.h>

#if (defined(IS_OPENNSA))
#define BCM_WARM_BOOT_SUPPORT
#include <soc/opensoc.h>
#else
#include <soc/drv.h>
#endif
}

using std::make_pair;
using std::make_shared;
using std::make_unique;
using std::move;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::chrono::duration_cast;
using std::chrono::seconds;

using facebook::fboss::DeltaFunctions::forEachAdded;
using facebook::fboss::DeltaFunctions::forEachChanged;
using facebook::fboss::DeltaFunctions::forEachRemoved;

using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;

using namespace std::chrono;

using namespace facebook::fboss::utility;

DEFINE_int32(linkscan_interval_us, 250000, "The Broadcom linkscan interval");
DEFINE_string(
    script_pre_asic_init,
    "script_pre_asic_init",
    "Broadcom script file to be run before ASIC init");

DEFINE_int32(
    copp_acl_entry_priority_start,
    1,
    "Starting ACL entry priority for CoPP");

DEFINE_int32(acl_g_pri, 0, "Group priority for ACL field group");
DEFINE_int32(
    qcm_ifp_gid,
    5,
    "Content aware processor group ID for ACLs specific to qcm");
// Put lowest priority for this group among all i.e. lower than acl_g_pri.
DEFINE_int32(qcm_ifp_pri, -1, "Group priority for ACL field group");

DECLARE_int32(update_watermark_stats_interval_s);

// The default of 1024 high frequency stats size was chosen because a serialized
// stats object with the full set of stats is about 7 KB. With 1024 stats, we
// use approximately 7 MB to keep it reasonably sized. This is underestimate
// because the data is stored as C++ structs instead of serialized thrift
// structs.
DEFINE_int32(
    high_freq_stats_data_size,
    1024,
    "The maximum number of high frequency stats data to store in memory in a "
    "deque.");

DEFINE_int32(
    num_consecutive_high_freq_stats_collect,
    2,
    "Number of high frequency stats to collect consecutively for each active "
    "stats collection period");

enum : uint8_t {
  kRxCallbackPriority = 1,
};

namespace {
// On new platforms we found sflow samplig rate to be inconsistent
// BRCM found this seed provides better results (see CS00011944544)
constexpr auto kHSDKSflowSamplingSeed = 0x2f64c448;

void rethrowIfHwNotFull(const facebook::fboss::BcmError& error) {
  if (error.getBcmError() != BCM_E_FULL) {
    // If this is not because of TCAM being full, rethrow the exception.
    throw error;
  }

  XLOG(WARNING) << error.what();
}

/*
 * For the devices/SDK we use on pre-TH4, the only events we should get (and
 * process) are ADD and DELETE. Learning generates ADD, aging generates DELETE,
 * while Mac move results in DELETE followed by ADD. We ASSERT this explicitly
 * via HW tests.
 */
const std::map<int, facebook::fboss::L2EntryUpdateType>
    kL2AddrBasicUpdateOperationsOfInterest = {
        {BCM_L2_CALLBACK_DELETE,
         facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE},
        {BCM_L2_CALLBACK_ADD,
         facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD},
};

/*
 * For the devices/SDK we use on TH4, the only events we should get and process
 * are AGE, LEARN and MOVE.
 * Calling bcm_l2_addr_add() to add new entry will trigger ADD
 * Calling bcm_l2_addr_add() to update entry (e.g. class id) will trigger
 * DELETE followed by ADD.
 * Calling bcm_l2_addr_delete() to remove existing entry will trigger DELETE.
 * Learning generates LEARN, aging generates AGE, while Mac move results in
 * DELETE followed by MOVE.
 * Thus, we may only focus on AGE, LEARN, MOVE, and ignore ADD, DELETE.
 * We ASSERT this explicitly via HW tests.
 */
const std::map<int, facebook::fboss::L2EntryUpdateType>
    kL2AddrDetailedUpdateOperationsOfInterest = {
        {BCM_L2_CALLBACK_AGE_EVENT,
         facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE},
        {BCM_L2_CALLBACK_LEARN_EVENT,
         facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD},
        {BCM_L2_CALLBACK_MOVE_EVENT,
         facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD},
};

using facebook::fboss::AggregatePortID;
using facebook::fboss::L2Entry;
using facebook::fboss::L2EntryUpdateType;
using facebook::fboss::macFromBcm;
using facebook::fboss::PortDescriptor;
using facebook::fboss::PortID;
using facebook::fboss::VlanID;
using facebook::fboss::cfg::AclLookupClass;

L2Entry createL2Entry(const bcm_l2_addr_t* l2Addr, bool isPending) {
  CHECK(l2Addr);

  auto l2EntryType = isPending ? L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING
                               : L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED;

  if (!(l2Addr->flags & BCM_L2_TRUNK_MEMBER)) {
    return L2Entry(
        macFromBcm(l2Addr->mac),
        VlanID(l2Addr->vid),
        PortDescriptor(PortID(l2Addr->port)),
        l2EntryType,
        l2Addr->group != 0
            ? std::optional<AclLookupClass>(AclLookupClass(l2Addr->group))
            : std::nullopt);
  } else {
    return L2Entry(
        macFromBcm(l2Addr->mac),
        VlanID(l2Addr->vid),
        PortDescriptor(AggregatePortID(l2Addr->tgid)),
        l2EntryType,
        l2Addr->group != 0
            ? std::optional<AclLookupClass>(AclLookupClass(l2Addr->group))
            : std::nullopt);
  }
}

bool isL2OperationOfInterest(int operation, bool detailedUpdate) {
  if (detailedUpdate) {
    return kL2AddrDetailedUpdateOperationsOfInterest.find(operation) !=
        kL2AddrDetailedUpdateOperationsOfInterest.end();
  }
  return kL2AddrBasicUpdateOperationsOfInterest.find(operation) !=
      kL2AddrBasicUpdateOperationsOfInterest.end();
}

L2EntryUpdateType getL2EntryUpdateType(int operation, bool detailedUpdate) {
  CHECK(isL2OperationOfInterest(operation, detailedUpdate));
  if (detailedUpdate) {
    return kL2AddrDetailedUpdateOperationsOfInterest.find(operation)->second;
  }
  return kL2AddrBasicUpdateOperationsOfInterest.find(operation)->second;
}

/*
 * How many bytes we copy for sFlow sampling
 */
const unsigned int kMaxSflowSnapLen = 128;

bool isValidLabeledNextHopSet(
    facebook::fboss::BcmPlatform* platform,
    const facebook::fboss::LabelNextHopSet& nexthops) {
  if (!facebook::fboss::MultiLabelForwardingInformationBase::isValidNextHopSet(
          nexthops)) {
    return false;
  }
  std::optional<facebook::fboss::LabelForwardingAction::LabelForwardingType>
      forwardingType;
  for (const auto& nexthop : nexthops) {
    const auto& labelForwardingAction = nexthop.labelForwardingAction();
    if (labelForwardingAction->type() ==
            facebook::fboss::LabelForwardingAction::LabelForwardingType::PUSH &&
        labelForwardingAction->pushStack()->size() >
            platform->getAsic()->getMaxLabelStackDepth()) {
      // label stack to push exceeds what platform can support
      XLOG(ERR) << "next hoop " << nexthop.str() << " label stack exceeds "
                << platform->getAsic()->getMaxLabelStackDepth();
      return false;
    }
    if (!forwardingType.has_value()) {
      forwardingType = labelForwardingAction->type();
    } else if (forwardingType != labelForwardingAction->type()) {
      //  each next hop must have same LabelForwardingAction type.
      XLOG(ERR) << "more than one forwarding action in next hop set";
      return false;
    }
  }
  return true;
}

} // namespace

namespace facebook::fboss {

phy::FecMode BcmSwitch::getPortFECMode(PortID port) const {
  return getPortTable()->getBcmPort(port)->getFECMode();
}

cfg::PortSpeed BcmSwitch::getPortMaxSpeed(PortID port) const {
  return getPortTable()->getBcmPort(port)->getMaxSpeed();
}

BcmSwitch::BcmSwitch(BcmPlatform* platform, uint32_t featuresDesired)
    : BcmSwitchIf(featuresDesired),
      platform_(platform),
      mmuBufferBytes_(platform->getMMUBufferBytes()),
      mmuCellBytes_(platform->getMMUCellBytes()),
      warmBootCache_(new BcmWarmBootCache(this)),
      portTable_(new BcmPortTable(this)),
      intfTable_(new BcmIntfTable(this)),
      hostTable_(new BcmHostTable(this)),
      egressManager_(new BcmEgressManager(this)),
      neighborTable_(new BcmNeighborTable(this)),
      l3NextHopTable_(new BcmL3NextHopTable(this)),
      mplsNextHopTable_(new BcmMplsNextHopTable(this)),
      multiPathNextHopTable_(new BcmMultiPathNextHopTable(this)),
      multiPathNextHopStatsManager_(new BcmMultiPathNextHopStatsManager()),
      labelMap_(new BcmLabelMap(this)),
      routeCounterTable_(
          getPlatform()->getAsic()->isSupported(
              HwAsic::Feature::ROUTE_FLEX_COUNTERS)
              ? static_cast<BcmRouteCounterTableBase*>(
                    new BcmRouteFlexCounterTable(this))
              : static_cast<BcmRouteCounterTableBase*>(
                    new BcmRouteCounterTable(this))),
      routeTable_(new BcmRouteTable(this)),
      qosPolicyTable_(new BcmQosPolicyTable(this)),
      aclTable_(new BcmAclTable(this)),
      teFlowTable_(new BcmTeFlowTable(this)),
      trunkTable_(new BcmTrunkTable(this)),
      sFlowExporterTable_(new BcmSflowExporterTable()),
      rtag7LoadBalancer_(new BcmRtag7LoadBalancer(this)),
      mirrorTable_(new BcmMirrorTable(this)),
      bstStatsMgr_(new BcmBstStatsMgr(this)),
      switchSettings_(new BcmSwitchSettings(this)),
      macTable_(new BcmMacTable(this)),
      qcmManager_(new BcmQcmManager(this)),
      ptpTcMgr_(new BcmPtpTcMgr(this)),
      sysPortMgr_(new UnsupportedFeatureManager("system ports")),
      remoteRifMgr_(new UnsupportedFeatureManager("remote RIF")),
      udfManager_(new BcmUdfManager(this)) {
  CHECK(!unitObject_);
  unitObject_ = BcmAPI::createOnlyUnit(platform_);
  unit_ = unitObject_->getNumber();
  unitObject_->setCookie(this);
  BcmAPI::initUnit(unit_, platform_);
}

BcmSwitch::~BcmSwitch() {
  XLOG(DBG2) << "Destroying BcmSwitch";
  resetTables();
  if (unitObject_) {
    // In agent this would be done in the signal handler
    // gracefulExitImpl().  In bcm_tests there is no signal.
    // So if unitObject_ is still valid, destroy it.
    unitObject_.reset();
  }
}

void BcmSwitch::resetTables() {
  std::unique_lock<std::mutex> lk(lock_);
  unregisterCallbacks();
  // ACL entries & TeFlow entries now may hold reference to multi path nexthop.
  // So release ACLs and TeFlows before any nexthop related tables:
  // l3 nexthop table
  // mpls nexthop table
  // host table
  // multi path nexthop table
  // route table
  aclTable_->releaseAcls();
  aclTable_.reset();
  teFlowTable_->releaseTeFlows();
  teFlowTable_.reset();
  labelMap_.reset();
  routeTable_.reset();
  l3NextHopTable_.reset();
  mplsNextHopTable_.reset();
  // Release host entries before reseting switch's host table
  // entries so that if host try to refer to look up host table
  // via the BCM switch during their destruction the pointer
  // access is still valid.
  hostTable_->releaseHosts();
  routeCounterTable_.reset();
  // reset neighbors before resetting host table
  neighborTable_.reset();
  // reset interfaces before host table, as interfaces have
  // host references now.
  intfTable_.reset();
  egressManager_.reset();
  multiPathNextHopStatsManager_.reset();
  multiPathNextHopTable_.reset();
  hostTable_.reset();
  toCPUEgress_.reset();
  portTable_.reset();
  qosPolicyTable_.reset();
  mirrorTable_.reset();
  trunkTable_.reset();
  controlPlane_.reset();
  rtag7LoadBalancer_.reset();
  bcmStatUpdater_.reset();
  bstStatsMgr_.reset();
  switchSettings_.reset();
  macTable_.reset();
  qcmManager_.reset();
  ptpTcMgr_.reset();
  queueFlexCounterMgr_.reset();
  udfManager_.reset();
  // Reset warmboot cache last in case Bcm object destructors
  // access it during object deletion.
  warmBootCache_.reset();
}

void BcmSwitch::unregisterCallbacks() {
  if (flags_ & RX_REGISTERED) {
    int rv;

    bcm_rx_stop(unit_, nullptr);

    if (!usePKTIO()) {
      rv = bcm_rx_unregister(unit_, packetRxSdkCallback, kRxCallbackPriority);
    } else {
#ifdef INCLUDE_PKTIO
      rv = bcm_pktio_rx_unregister(
          unit_, pktioPacketRxSdkCallback, kRxCallbackPriority);
#else
      throw FbossError("invalid PKTIO configuration.");
#endif
    }
    CHECK(BCM_SUCCESS(rv)) << "failed to unregister BcmSwitch rx callback: "
                           << bcm_errmsg(rv);

    flags_ &= ~RX_REGISTERED;
  }
  // Note that we don't explicitly call bcm_linkscan_detach() here--
  // this call is not thread safe and should only be called from the main
  // thread.  However, bcm_detach() / _bcm_shutdown() will clean up the
  // linkscan module properly.
  if (flags_ & LINKSCAN_REGISTERED) {
    auto rv = bcm_linkscan_unregister(unit_, linkscanCallback);
    CHECK(BCM_SUCCESS(rv)) << "failed to unregister BcmSwitch linkscan "
                              "callback: "
                           << bcm_errmsg(rv);

    stopLinkscanThread();
    flags_ &= ~LINKSCAN_REGISTERED;
  }

  if (flags_ & PFC_WATCHDOG_REGISTERED) {
    // mostly to ensure that callback is not triggered while we are in
    // "process" of shutting down
    auto rv = bcm_cosq_pfc_deadlock_recovery_event_unregister(
        unit_, pfcDeadlockRecoveryEventCallback, callback_);
    CHECK(BCM_SUCCESS(rv)) << "failed to unregister BcmSwitch pfc watchdog"
                              "callback: "
                           << bcm_errmsg(rv);
    flags_ &= ~PFC_WATCHDOG_REGISTERED;
    XLOG(DBG2) << "Unregistered pfc watchdog";
  }

  /*
   * SOFTWARE L2 learning mode enables callback when there are updates to L2
   * table. Disable it: we don't want these callbacks (that call BcmSwitch
   * method) to fire after BcmSwitch object is destroyed.
   */
  if (switchSettings_->getL2LearningMode().has_value() &&
      switchSettings_->getL2LearningMode().value() ==
          cfg::L2LearningMode::SOFTWARE) {
    switchSettings_->disableL2LearningCallback();
  }
}

void BcmSwitch::gracefulExitImpl() {
  steady_clock::time_point begin = steady_clock::now();
  XLOG(DBG2) << "[Exit] Starting BCM Switch graceful exit";
  // Ideally, preparePortsForGracefulExit() would run in update EVB of the
  // SwSwitch, but it does not really matter at the graceful exit time. If
  // this is a concern, this can be moved to the updateEventBase_ of SwSwitch.
  portTable_->preparePortsForGracefulExit();
  bstStatsMgr_->stopBufferStatCollection();
  qcmManager_->stop();

  std::lock_guard<std::mutex> g(lock_);

  // This will run some common shell commands to give more info about
  // the underlying bcm sdk state
  dumpState(platform_->getWarmBootHelper()->shutdownSdkDumpFile());

  folly::dynamic follySwitchState = folly::dynamic::object;
  follySwitchState[kHwSwitch] = toFollyDynamic();
  unitObject_->writeWarmBootState(follySwitchState);
  unitObject_.reset();
  XLOG(DBG2)
      << "[Exit] BRCM Graceful Exit time "
      << duration_cast<duration<float>>(steady_clock::now() - begin).count();
}

folly::dynamic BcmSwitch::toFollyDynamic() const {
  return warmBootCache_->getWarmBootStateFollyDynamic();
}

int BcmSwitch::addL2TableCb(
    int /*unit*/,
    bcm_l2_addr_t* l2Addr,
    void* userData) {
  auto* bcmSw = static_cast<BcmSwitch*>(userData);
  if (bcmSw->getRunState() < SwitchRunState::CONFIGURED) {
    XLOG(WARNING)
        << "ignoreing learn notifications as switch is not configured.";
    return 0;
  }
  bcmSw->callback_->l2LearningUpdateReceived(
      createL2Entry(l2Addr, bcmSw->isL2EntryPending(l2Addr)),
      L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);

  return 0;
}

int BcmSwitch::addL2TableCbForPendingOnly(
    int /*unit*/,
    bcm_l2_addr_t* l2Addr,
    void* userData) {
  auto* bcmSw = static_cast<BcmSwitch*>(userData);
  if (bcmSw->getRunState() < SwitchRunState::CONFIGURED) {
    XLOG(WARNING)
        << "ignoreing learn notifications as switch is not configured.";
    return 0;
  }
  if (bcmSw->isL2EntryPending(l2Addr)) {
    bcmSw->callback_->l2LearningUpdateReceived(
        createL2Entry(l2Addr, bcmSw->isL2EntryPending(l2Addr)),
        L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);
  }

  return 0;
}

int BcmSwitch::deleteL2TableCb(
    int /*unit*/,
    bcm_l2_addr_t* l2Addr,
    void* userData) {
  auto* bcmSw = static_cast<BcmSwitch*>(userData);
  if (bcmSw->getRunState() < SwitchRunState::CONFIGURED) {
    XLOG(WARNING)
        << "ignoreing learn notifications as switch is not configured.";
    return 0;
  }
  bcmSw->callback_->l2LearningUpdateReceived(
      createL2Entry(l2Addr, bcmSw->isL2EntryPending(l2Addr)),
      L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE);

  return 0;
}

void BcmSwitch::switchRunStateChangedImpl(SwitchRunState newState) {
  std::lock_guard<std::mutex> g(lock_);
  switch (newState) {
    case SwitchRunState::INITIALIZED:
      setupLinkscan();
      setupPacketRx();
      break;

    case SwitchRunState::CONFIGURED: {
      /*
       * When L2LearningMode::SOFTWARE is set, during warmboot time window,
       * there is no software running that can receive L2 table update
       * callbacks. However, the hardware continues to learn unknown source MAC
       * + VLAN as PENDING entries during that time window. Once agent comes
       * up, it must update its MAC table and VALIDATE the PENDING entries.
       */
      if (bootType_ == BootType::WARM_BOOT &&
          switchSettings_->getL2LearningMode().has_value()) {
        if (switchSettings_->getL2LearningMode().value() ==
            cfg::L2LearningMode::SOFTWARE) {
          auto rv = bcm_l2_traverse(
              getUnit(), BcmSwitch::addL2TableCbForPendingOnly, this);
          bcmCheckError(rv, "bcm_l2_traverse failed");
        }
      }
      break;
    }

    default:
      break;
  }
}

bool BcmSwitch::isPortEnabled(PortID port) const {
  return portTable_->getBcmPort(port)->isEnabled();
}

bool BcmSwitch::isPortUp(PortID port) const {
  return portTable_->getBcmPort(port)->isUp();
}

std::shared_ptr<SwitchState> BcmSwitch::getColdBootSwitchState() const {
  auto bootState = getProgrammedState()->clone();
  uint32_t flags = 0;
  for (const auto& kv : std::as_const(*portTable_)) {
    uint32_t port_flags;
    PortID portID = kv.first;

    auto rv = bcm_port_learn_get(getUnit(), portID, &port_flags);
    bcmCheckError(rv, "Unable to get L2 Learning flags for port: ", portID);

    if (flags == 0) {
      flags = port_flags;
    } else if (flags != port_flags) {
      throw FbossError(
          "Every port should have same L2 Learning setting by default");
    }
  }

  // This is cold boot, so there cannot be any L2 update callback registered.
  // Thus, BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_FWD should be enough to
  // ascertain HARDWARE as L2 learning mode.
  cfg::L2LearningMode l2LearningMode;
  if (flags == (BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_FWD)) {
    l2LearningMode = cfg::L2LearningMode::HARDWARE;
  } else if (flags == (BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_PENDING)) {
    l2LearningMode = cfg::L2LearningMode::SOFTWARE;
  } else {
    throw FbossError(
        "L2 Learning mode is neither SOFTWARE, nor HARDWARE, flags: ", flags);
  }

  bcm_vlan_t defaultVlan;
  auto rv = bcm_vlan_default_get(getUnit(), &defaultVlan);
  bcmCheckError(rv, "Unable to get default VLAN");

  auto* scopeResolver = platform_->scopeResolver();
  auto multiSwitchSwitchSettings = make_shared<MultiSwitchSettings>();
  auto switchSettings = make_shared<SwitchSettings>();
  switchSettings->setL2LearningMode(l2LearningMode);
  switchSettings->setDefaultVlan(VlanID(defaultVlan));
  switchSettings->setSwitchIdToSwitchInfo(
      scopeResolver->switchIdToSwitchInfo());

  auto switchId = platform_->getAsic()->getSwitchId()
      ? *platform_->getAsic()->getSwitchId()
      : 0;
  auto matcher =
      HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(switchId)}));
  multiSwitchSwitchSettings->addNode(matcher.matcherString(), switchSettings);

  bootState->resetSwitchSettings(multiSwitchSwitchSettings);

  // get cpu queue settings
  auto cpu = make_shared<ControlPlane>();
  auto cpuQueues = controlPlane_->getMulticastQueueSettings();
  auto rxReasonToQueue = controlPlane_->getRxReasonToQueue();
  cpu->resetQueues(cpuQueues);
  cpu->resetRxReasonToQueue(rxReasonToQueue);
  auto multiSwitchControlPlane = std::make_shared<MultiControlPlane>();
  multiSwitchControlPlane->addNode(
      scopeResolver->scope(cpu).matcherString(), cpu);
  bootState->resetControlPlane(multiSwitchControlPlane);

  // On cold boot all ports are in Vlan 1
  auto vlan = make_shared<Vlan>(VlanID(1), std::string("InitVlan"));
  Vlan::MemberPorts memberPorts;
  auto ports = bootState->getPorts()->modify(&bootState);
  for (const auto& kv : std::as_const(*portTable_)) {
    PortID portID = kv.first;
    BcmPort* bcmPort = kv.second;

    state::PortFields portFields;
    portFields.portId() = portID;
    portFields.portName() = folly::to<string>("port", portID);
    auto swPort = std::make_shared<Port>(std::move(portFields));

    auto platformPort = getPlatform()->getPlatformPort(portID);
    swPort->setProfileId(
        platformPort->getProfileIDBySpeed(bcmPort->getSpeed()));
    // Coldboot state can assume transceiver doesn't exist
    PlatformPortProfileConfigMatcher profileMatcher{
        swPort->getProfileID(), portID};
    if (auto profileConfig = platform_->getPortProfileConfig(profileMatcher)) {
      swPort->setProfileConfig(*profileConfig->iphy());
    } else {
      throw FbossError(
          "No port profile config found with matcher:",
          profileMatcher.toString());
    }
    swPort->resetPinConfigs(
        platform_->getPlatformMapping()->getPortIphyPinConfigs(profileMatcher));
    swPort->setSpeed(bcmPort->getSpeed());
    if (platform_->getAsic()->isSupported(HwAsic::Feature::L3_QOS)) {
      auto queues = bcmPort->getCurrentQueueSettings();
      swPort->resetPortQueues(queues);
    }
    ports->addNode(swPort, scopeResolver->scope(swPort));

    state::VlanInfo vlanInfo;
    *vlanInfo.tagged() = false;
    *vlanInfo.priorityTagged() = false;
    memberPorts.insert(make_pair(portID, vlanInfo));
  }
  vlan->setPortsInfo(memberPorts);
  auto vlans = bootState->getVlans()->modify(&bootState);
  vlans->addNode(vlan, scopeResolver->scope(vlan));
  bootState->publish();
  return bootState;
}

void BcmSwitch::runBcmScript(const std::string& filename) const {
  std::ifstream scriptFile(filename);

  if (!scriptFile.good()) {
    return;
  }

  XLOG(DBG2) << "Run script " << filename;
  printDiagCmd(folly::to<string>("rcload ", filename));
}

void BcmSwitch::setupLinkscan() {
  if (!(getFeaturesDesired() & FeaturesDesired::LINKSCAN_DESIRED)) {
    XLOG(DBG1) << " Skipping linkscan registration as the feature is disabled";
    return;
  }
  linkScanBottomHalfThread_ = std::make_unique<std::thread>([=, this]() {
    initThread("fbossLinkScanBH");
    linkScanBottomHalfEventBase_.loopForever();
  });
  auto rv = bcm_switch_control_set(unit_, bcmSwitchLinkDownInfoSkip, 1);
  bcmCheckError(rv, "failed to skip port info get for link down ports");
  rv = bcm_linkscan_register(unit_, linkscanCallback);
  bcmCheckError(rv, "failed to register for linkscan events");
  flags_ |= LINKSCAN_REGISTERED;
  rv = bcm_linkscan_enable_set(unit_, FLAGS_linkscan_interval_us);
  bcmCheckError(rv, "failed to enable linkscan");
  if (getBootType() == BootType::WARM_BOOT) {
    bcm_port_config_t pcfg;
    bcm_port_config_t_init(&pcfg);
    rv = bcm_port_config_get(unit_, &pcfg);
    bcmCheckError(rv, "failed to get port configuration");
    forceLinkscanOn(pcfg.port);
    // Sometimes after warmboot sw does not have the correct state of every port
    // so we need to update sw state here. This needs to be done after linkscan
    // is enabled otherwise the sdk may return inconsistent results
    for (auto& port : std::as_const(*portTable_)) {
      callback_->linkStateChanged(
          port.first, port.second->isUp(), cfg::PortType::INTERFACE_PORT);
    }
  }
}

void BcmSwitch::setMacAging(std::chrono::seconds agingInterval) {
  /**
   * Enable MAC aging on the ASIC.
   * Every `seconds` seconds, all addresses
   * that have not been updated/used since the last age timer interval
   * are removed from the table.
   *
   * This is implemented by tracking the 'hit' bit in the L2 table.
   * On the aging timer, all MACs that have the hit bit set have it cleared,
   * and all MACs without the hit bit set are removed from the table.
   *
   * The ASIC will automatically set the hit bit anytime that MAC address is
   * looked up and used in a packet, either as a SRC lookup or DST lookup.
   *
   * Only set if timer is different than what's currently set.
   */
  int currentSeconds;
  int targetSeconds = agingInterval.count(); // force to 32bit
  bcmCheckError(
      bcm_l2_age_timer_get(unit_, &currentSeconds),
      "failed to get l2_age_timer");
  if (currentSeconds != targetSeconds) {
    XLOG(DBG1) << "Changing MAC Aging timer from " << currentSeconds << " to "
               << targetSeconds;
    bcmCheckError(
        bcm_l2_age_timer_set(unit_, targetSeconds),
        "failed to set l2_age_timer");
  }
}

void BcmSwitch::minimalInit() {
  std::lock_guard<std::mutex> g(lock_);

  bootType_ = platform_->getWarmBootHelper()->canWarmBoot()
      ? BootType::WARM_BOOT
      : BootType::COLD_BOOT;
}

HwInitResult BcmSwitch::initImpl(
    Callback* callback,
    BootType bootType,
    bool /*failHwCallsOnWarmboot*/) {
  CHECK(getSwitchType() == cfg::SwitchType::NPU)
      << " Only NPU switch type supported";
  HwInitResult ret;
  ret.rib = std::make_unique<RoutingInformationBase>();

  std::lock_guard<std::mutex> g(lock_);

  steady_clock::time_point begin = steady_clock::now();

  bootType_ = bootType;
  auto warmBoot = bootType_ == BootType::WARM_BOOT;
  callback_ = callback;

  ret.initializedTime =
      duration_cast<duration<float>>(steady_clock::now() - begin).count();

  XLOG(DBG2) << "Initializing BcmSwitch for unit " << unit_;

  // Add callbacks for unit and parity errors as early as possible to handle
  // critical events
  BcmSwitchEventUtils::initUnit(unit_, this);
  auto fatalCob = make_shared<BcmSwitchEventUnitFatalErrorCallback>();
  auto nonFatalCob = make_shared<BcmSwitchEventUnitNonFatalErrorCallback>();
  BcmSwitchEventUtils::registerSwitchEventCallback(
      unit_, BCM_SWITCH_EVENT_STABLE_FULL, fatalCob);
  BcmSwitchEventUtils::registerSwitchEventCallback(
      unit_, BCM_SWITCH_EVENT_STABLE_ERROR, fatalCob);
  BcmSwitchEventUtils::registerSwitchEventCallback(
      unit_, BCM_SWITCH_EVENT_UNCONTROLLED_SHUTDOWN, fatalCob);
  BcmSwitchEventUtils::registerSwitchEventCallback(
      unit_, BCM_SWITCH_EVENT_WARM_BOOT_DOWNGRADE, fatalCob);
  BcmSwitchEventUtils::registerSwitchEventCallback(
      unit_, BCM_SWITCH_EVENT_PARITY_ERROR, nonFatalCob);

  // Create bcmStatUpdater to cache the stat ids
  bcmStatUpdater_ = std::make_unique<BcmStatUpdater>(this);

  XLOG(DBG2) << " Is ALPM enabled: " << BcmAPI::isAlpmEnabled();
  // Additional switch configuration
  auto state = make_shared<SwitchState>();
  bcm_port_config_t pcfg;
  bcm_port_config_t_init(&pcfg);
  auto rv = bcm_port_config_get(unit_, &pcfg);
  bcmCheckError(rv, "failed to get port configuration");

  if (!warmBoot) {
    LOG(INFO) << " Performing cold boot ";
    /* initialize mirroring module */
    initMirrorModule();
    /* initialize MPLS */
    initMplsModule();
  } else {
    LOG(INFO) << "Performing warm boot ";
    // This dumps debug info about initial sdk state. Useful after warm boot.
    dumpState(platform_->getWarmBootHelper()->startupSdkDumpFile());
  }

  // If the platform doesn't support auto enabling l3 egress mode
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::L3_EGRESS_MODE_AUTO_ENABLED)) {
    rv = bcm_switch_control_set(unit_, bcmSwitchL3EgressMode, 1);
    bcmCheckError(rv, "failed to set L3 egress mode");
  }

  if (getPlatform()->getAsic()->getAsicType() ==
      cfg::AsicType::ASIC_TYPE_TOMAHAWK4) {
    rv = bcm_l3_enable_set(unit_, 1);
    bcmCheckError(rv, "failed to enable l3");
  }
  // TODO: Remove #if once bcmSwitchEcmpDlbOffset support in all sdk releases
#if defined(BCM_SDK_VERSION_GTE_6_5_26)
  if (FLAGS_flowletSwitchingEnable) {
    if (platform_->getAsic()->isSupported(HwAsic::Feature::ECMP_DLB_OFFSET)) {
      // set the Ecmp DlbOffset for ECMP to DLB
      rv = bcm_switch_control_set(unit_, bcmSwitchEcmpDlbOffset, 0x0);
      bcmCheckError(rv, "failed to set bcmSwitchEcmpDlbOffset");
    }
  }
#endif

  // Trap IPv4 Address Resolution Protocol (ARP) packets.
  // TODO: We may want to trap ARP on a per-port or per-VLAN basis.
  rv = bcm_switch_control_set(unit_, bcmSwitchArpRequestToCpu, 1);
  bcmCheckError(rv, "failed to set ARP request trapping");
  rv = bcm_switch_control_set(unit_, bcmSwitchArpReplyToCpu, 1);
  bcmCheckError(rv, "failed to set ARP reply trapping");
  // Trap IP header TTL or hoplimit 1 to CPU
  rv = bcm_switch_control_set(unit_, bcmSwitchL3UcastTtl1ToCpu, 1);
  bcmCheckError(rv, "failed to set L3 header error trapping");
  // Trap DHCP packets to CPU
  rv = bcm_switch_control_set(unit_, bcmSwitchDhcpPktToCpu, 1);
  bcmCheckError(rv, "failed to set DHCP packet trapping");
  // Trap Dest miss
  rv = bcm_switch_control_set(unit_, bcmSwitchUnknownL3DestToCpu, 1);
  bcmCheckError(rv, "failed to set destination miss trapping");
  rv = bcm_switch_control_set(unit_, bcmSwitchV6L3DstMissToCpu, 1);
  bcmCheckError(rv, "failed to set IPv6 destination miss trapping");
  // Trap IPv6 Neighbor Discovery Protocol (NDP) packets.
  // TODO: We may want to trap NDP on a per-port or per-VLAN basis.
  rv = bcm_switch_control_set(unit_, bcmSwitchNdPktToCpu, 1);
  bcmCheckError(rv, "failed to set NDP trapping");

  disableHotSwap();

  if (FLAGS_force_init_fp || !warmBoot || haveMissingOrQSetChangedFPGroups()) {
    initFieldProcessor();
    setupFPGroups();
  }

  dropDhcpPackets();
  setL3MtuFailPackets();
  mmuState_ = BcmAPI::getMmuState();

  // enable IPv4 and IPv6 on CPU port
  bcm_port_t idx;
  BCM_PBMP_ITER(pcfg.cpu, idx) {
    rv = bcm_port_control_set(unit_, idx, bcmPortControlIP4, 1);
    bcmCheckError(rv, "failed to enable IPv4 on cpu port ", idx);
    rv = bcm_port_control_set(unit_, idx, bcmPortControlIP6, 1);
    bcmCheckError(rv, "failed to enable IPv6 on cpu port ", idx);
    XLOG(DBG2) << "Enabled IPv4/IPv6 on CPU port " << idx;
  }

  // Setup the default drop egress
  BcmEgress::setupDefaultDropEgress(unit_, getDropEgressId());

  setupCos();

  folly::dynamic switchStateJson;
  if (warmBoot) {
    // This needs to be done after we have set
    // bcmSwitchL3EgressMode else the egress ids
    // in the host table don't show up correctly.
    // TODO: Use thrift representation for sw switch state.
    switchStateJson = getPlatform()->getWarmBootHelper()->getWarmBootState();
    warmBootCache_->populate(switchStateJson);
  }
  setupToCpuEgress();
  portTable_->initPorts(&pcfg, warmBoot);

  egressManager_->init();

  // initialize UDF module
  udfManager_->init();
  bstStatsMgr_->startBufferStatCollection();

  // Set the spanning tree state of all ports to forwarding.
  // TODO: Eventually the spanning tree state should be part of the Port
  // state, and this should be handled in applyConfig().
  //
  // Spanning tree group settings
  // TODO: This should eventually be done as part of applyConfig()
  bcm_stg_t stg = 1;
  BCM_PBMP_ITER(pcfg.port, idx) {
    rv = bcm_stg_stp_set(unit_, stg, idx, BCM_STG_STP_FORWARD);
    bcmCheckError(rv, "failed to set spanning tree state on port ", idx);
  }

  ret.bootType = bootType_;

  if (warmBoot) {
    ret.switchState = std::make_shared<SwitchState>();
    getPlatform()->preWarmbootStateApplied();
  } else {
    auto bootState = std::make_shared<SwitchState>();
    bootState->publish();
    setProgrammedState(bootState);
    ret.switchState = getColdBootSwitchState();
    ret.switchState->publish();
    setProgrammedState(ret.switchState);
    CHECK(ret.switchState->getSwitchSettings()->size());
    auto switchSettings =
        ret.switchState->getSwitchSettings()->cbegin()->second;
    setMacAging(std::chrono::seconds(switchSettings->getL2AgeTimerSeconds()));
  }

  macTable_ = std::make_unique<BcmMacTable>(this);

  // Initialize the high frequency stats thread
  highFreqStatsThread_ = std::make_unique<folly::FunctionScheduler>();
  highFreqStatsThread_->setThreadName(kHighFreqStatsThreadName_);

  ret.bootTime =
      duration_cast<duration<float>>(steady_clock::now() - begin).count();
  ret.switchState->publish();
  return ret;
}

void BcmSwitch::setupToCpuEgress() {
  // create an egress object for ToCPU
  toCPUEgress_ = make_unique<BcmEgress>(this);
  toCPUEgress_->programToCPU();
}

void BcmSwitch::setupPacketRx() {
  static bcm_rx_cfg_t rxCfg = {
      (16 * 1032), // packet alloc size (12K packets plus spare)
      16, // Packets per chain
      0, // Default pkt rate, global (all COS, one unit)
      0, // Burst
      {// 1 RX channel
       {0, 0, 0, 0}, // Channel 0 is usually TX
       {
           // Channel 1, default RX
           4, // DV count (number of chains)
           0, // Default pkt rate, DEPRECATED
           0, // No flags
           0xff // COS bitmap channel to receive
       }},
      nullptr, // Use default alloc function
      nullptr, // Use default free function
      0, // flags
      0, // Num CPU addrs
      nullptr // cpu_addresses
  };

  if (!(getFeaturesDesired() & FeaturesDesired::PACKET_RX_DESIRED)) {
    XLOG(DBG1) << " Skip setting up packet RX since its explicitly disabled";
    return;
  }
  // Register our packet handler callback function.
  uint32_t rxFlags = BCM_RCO_F_ALL_COS;

  int rv;

  if (!usePKTIO()) {
    rv = bcm_rx_register(
        unit_, // int unit
        "fboss_rx", // const char *name
        packetRxSdkCallback, // bcm_rx_cb_f callback
        kRxCallbackPriority, // uint8 priority
        this, // void *cookie
        rxFlags); // uint32 flags
  } else {
#ifdef INCLUDE_PKTIO
    rv = bcm_pktio_rx_register(
        unit_, // int unit
        "fboss_rx", // const char *name
        pktioPacketRxSdkCallback, // bcm_rx_pktio_cb_f callback
        kRxCallbackPriority, // uint8 priority
        this, // void *cookie
        rxFlags); // uint32 flags
#else
    throw FbossError("invalid PKTIO configuration");
#endif
  }

  bcmCheckError(rv, "failed to register packet rx callback");
  flags_ |= RX_REGISTERED;

  if (!unitObject_->usePKTIO()) {
    if (!isRxThreadRunning()) {
      rv = bcm_rx_start(unit_, &rxCfg);
    }
  }
  bcmCheckError(rv, "failed to start broadcom packet rx API");
}

void BcmSwitch::processSwitchSettingsChanged(const StateDelta& delta) {
  const auto switchSettingDelta = delta.getSwitchSettingsDelta();
  forEachAdded(switchSettingDelta, [&](const auto& newSwitchSettings) {
    processSwitchSettingsEntryChanged(
        std::make_shared<SwitchSettings>(), newSwitchSettings, delta);
  });
  forEachChanged(
      switchSettingDelta,
      [&](const auto& oldSwitchSettings, const auto& newSwitchSettings) {
        processSwitchSettingsEntryChanged(
            oldSwitchSettings, newSwitchSettings, delta);
      });
  forEachRemoved(switchSettingDelta, [&](const auto& oldSwitchSettings) {
    processSwitchSettingsEntryChanged(
        oldSwitchSettings, std::make_shared<SwitchSettings>(), delta);
  });
}

void BcmSwitch::processSwitchSettingsEntryChanged(
    const std::shared_ptr<SwitchSettings>& oldSwitchSettings,
    const std::shared_ptr<SwitchSettings>& newSwitchSettings,
    const StateDelta& delta) {
  /*
   * SwitchSettings are mandatory and can thus only be modified.
   * Every field in SwitchSettings must always be set in new SwitchState.
   */
  CHECK(oldSwitchSettings);
  CHECK(newSwitchSettings);

  if (oldSwitchSettings->getL2LearningMode() !=
      newSwitchSettings->getL2LearningMode()) {
    XLOG(DBG2) << "Configuring L2LearningMode old: "
               << static_cast<int>(oldSwitchSettings->getL2LearningMode())
               << " new: "
               << static_cast<int>(newSwitchSettings->getL2LearningMode());
    switchSettings_->setL2LearningMode(newSwitchSettings->getL2LearningMode());
  }

  if (oldSwitchSettings->isQcmEnable() != newSwitchSettings->isQcmEnable()) {
    XLOG(DBG3) << "Set QCM enable: " << std::boolalpha
               << newSwitchSettings->isQcmEnable();
    switchSettings_->setQcmEnable(
        newSwitchSettings->isQcmEnable(), delta.newState());
  }

  // Its possible during switch init, qcm is enabled
  // but all the ports are not enabled yet, so we trigger processing
  // ports on every cfg reload
  // QCM logic determines eligibility if there are any new ports
  // and update internally
  if (newSwitchSettings->isQcmEnable()) {
    if (oldSwitchSettings->isQcmEnable()) {
      // check whether config params changed on the fly
      qcmManager_->processQcmConfigChanged(delta.newState(), delta.oldState());
    }
    XLOG(DBG3) << "Qcm enabled. Evaluate the monitored ports";
    qcmManager_->processPortsForQcm(delta.newState());
  }

  const auto newPtpTcEnable = newSwitchSettings->isPtpTcEnable();
  if (oldSwitchSettings->isPtpTcEnable() != newPtpTcEnable) {
    XLOG(DBG3) << "Set PTP TC enable: " << std::boolalpha << newPtpTcEnable;
    switchSettings_->setPtpTc(newPtpTcEnable, delta.newState());
  }

  const auto oldVal = oldSwitchSettings->getL2AgeTimerSeconds();
  const auto newVal = newSwitchSettings->getL2AgeTimerSeconds();
  if (oldVal != newVal) {
    XLOG(DBG3) << "Configuring l2AgeTimerSeconds old: "
               << static_cast<int>(oldVal)
               << " new: " << static_cast<int>(newVal);

    setMacAging(std::chrono::seconds(newVal));
    switchSettings_->setL2AgeTimerSeconds(newVal);
  }

  // THRIFT_COPY
  if (oldSwitchSettings->getExactMatchTableConfig()->toThrift() !=
      newSwitchSettings->getExactMatchTableConfig()->toThrift()) {
    XLOG(DBG3) << "ExactMatch table setting changed";
    teFlowTable_->processTeFlowConfigChanged(newSwitchSettings);
  }

  if (oldSwitchSettings->getForceEcmpDynamicMemberUp() !=
      newSwitchSettings->getForceEcmpDynamicMemberUp()) {
    if (newSwitchSettings->getForceEcmpDynamicMemberUp().has_value() &&
        newSwitchSettings->getForceEcmpDynamicMemberUp().value()) {
      utility::setEcmpDynamicMemberUp(this);
    } else {
      throw FbossError("Reverting forceEcmpDynamicMemberUp is not supported.");
    }
  }
}

void BcmSwitch::processDynamicEgressLoadExponentChanged(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching) {
  uint16_t oldDynamicEgressLoadExponent = 0;
  uint16_t newDynamicEgressLoadExponent = 0;
  if (oldFlowletSwitching) {
    oldDynamicEgressLoadExponent =
        oldFlowletSwitching->getDynamicEgressLoadExponent();
  }
  if (newFlowletSwitching) {
    newDynamicEgressLoadExponent =
        newFlowletSwitching->getDynamicEgressLoadExponent();
  }
  if (oldDynamicEgressLoadExponent != newDynamicEgressLoadExponent) {
    auto rv = bcm_switch_control_set(
        unit_,
        bcmSwitchEcmpDynamicEgressBytesExponent,
        newDynamicEgressLoadExponent);
    bcmCheckError(rv, "Failed to set bcmSwitchEcmpDynamicEgressBytesExponent");
  }
}

void BcmSwitch::processDynamicQueueExponentChanged(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching) {
  uint16_t oldDynamicQueueExponent = 0;
  uint16_t newDynamicQueueExponent = 0;
  if (oldFlowletSwitching) {
    oldDynamicQueueExponent = oldFlowletSwitching->getDynamicQueueExponent();
  }
  if (newFlowletSwitching) {
    newDynamicQueueExponent = newFlowletSwitching->getDynamicQueueExponent();
  }
  if (oldDynamicQueueExponent != newDynamicQueueExponent) {
    auto rv = bcm_switch_control_set(
        unit_,
        bcmSwitchEcmpDynamicQueuedBytesExponent,
        newDynamicQueueExponent);
    bcmCheckError(rv, "Failed to set bcmSwitchEcmpDynamicQueuedBytesExponent");
  }
}

void BcmSwitch::processDynamicQueueMinThresholdBytesChanged(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching) {
  uint32_t oldDynamicQueueMinThresholdBytes = 0;
  uint32_t newDynamicQueueMinThresholdBytes = 0;
  if (oldFlowletSwitching) {
    oldDynamicQueueMinThresholdBytes =
        oldFlowletSwitching->getDynamicQueueMinThresholdBytes();
  }
  if (newFlowletSwitching) {
    newDynamicQueueMinThresholdBytes =
        newFlowletSwitching->getDynamicQueueMinThresholdBytes();
  }
  if (oldDynamicQueueMinThresholdBytes != newDynamicQueueMinThresholdBytes) {
    auto rv = bcm_switch_control_set(
        unit_,
        bcmSwitchEcmpDynamicQueuedBytesMinThreshold,
        newDynamicQueueMinThresholdBytes);
    bcmCheckError(
        rv, "Failed to set bcmSwitchEcmpDynamicQueuedBytesMinThreshold");

    // this is per ITM and should be half of newDynamicQueueMinThresholdBytes
    // above (as we have 2 ITMs)
    rv = bcm_switch_control_set(
        unit_,
        bcmSwitchEcmpDynamicPhysicalQueuedBytesMinThreshold,
        newDynamicQueueMinThresholdBytes >> 1);
    bcmCheckError(
        rv,
        "Failed to set bcmSwitchEcmpDynamicPhysicalQueuedBytesMinThreshold");
  }
}

void BcmSwitch::processDynamicQueueMaxThresholdBytesChanged(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching) {
  uint32_t oldDynamicQueueMaxThresholdBytes = 0;
  uint32_t newDynamicQueueMaxThresholdBytes = 0;
  if (oldFlowletSwitching) {
    oldDynamicQueueMaxThresholdBytes =
        oldFlowletSwitching->getDynamicQueueMaxThresholdBytes();
  }
  if (newFlowletSwitching) {
    newDynamicQueueMaxThresholdBytes =
        newFlowletSwitching->getDynamicQueueMaxThresholdBytes();
  }
  if (oldDynamicQueueMaxThresholdBytes != newDynamicQueueMaxThresholdBytes) {
    auto rv = bcm_switch_control_set(
        unit_,
        bcmSwitchEcmpDynamicQueuedBytesMaxThreshold,
        newDynamicQueueMaxThresholdBytes);
    bcmCheckError(
        rv, "Failed to set bcmSwitchEcmpDynamicQueuedBytesMaxThreshold");

    // this is per ITM and should be half of
    // bcmSwitchEcmpDynamicEgressBytesMaxThreshold above (as we have 2 ITMs)
    rv = bcm_switch_control_set(
        unit_,
        bcmSwitchEcmpDynamicPhysicalQueuedBytesMaxThreshold,
        newDynamicQueueMaxThresholdBytes >> 1);
    bcmCheckError(
        rv,
        "Failed to set bcmSwitchEcmpDynamicPhysicalQueuedBytesMaxThreshold");
  }
}

void BcmSwitch::processDynamicSampleRateChanged(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching) {
  uint32_t oldDynamicSampleRate = 0;
  uint32_t newDynamicSampleRate = 62500;
  if (oldFlowletSwitching) {
    oldDynamicSampleRate = oldFlowletSwitching->getDynamicSampleRate();
  }
  if (newFlowletSwitching) {
    newDynamicSampleRate = newFlowletSwitching->getDynamicSampleRate();
  }
  if (oldDynamicSampleRate != newDynamicSampleRate) {
    auto rv = bcm_switch_control_set(
        unit_, bcmSwitchEcmpDynamicSampleRate, newDynamicSampleRate);
    bcmCheckError(rv, "Failed to set bcmSwitchEcmpDynamicSampleRate");
  }
}

void BcmSwitch::processDynamicEgressMinThresholdBytesChanged(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching) {
  uint32_t oldDynamicEgressMinThresholdBytes = 0;
  uint32_t newDynamicEgressMinThresholdBytes = 0;
  if (oldFlowletSwitching) {
    oldDynamicEgressMinThresholdBytes =
        oldFlowletSwitching->getDynamicEgressMinThresholdBytes();
  }
  if (newFlowletSwitching) {
    newDynamicEgressMinThresholdBytes =
        newFlowletSwitching->getDynamicEgressMinThresholdBytes();
  }
  if (oldDynamicEgressMinThresholdBytes != newDynamicEgressMinThresholdBytes) {
    auto rv = bcm_switch_control_set(
        unit_,
        bcmSwitchEcmpDynamicEgressBytesMinThreshold,
        newDynamicEgressMinThresholdBytes);
    bcmCheckError(
        rv, "Failed to set bcmSwitchEcmpDynamicEgressBytesMinThreshold");
  }
}

void BcmSwitch::processDynamicEgressMaxThresholdBytesChanged(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching) {
  uint32_t oldDynamicEgressMaxThresholdBytes = 0;
  uint32_t newDynamicEgressMaxThresholdBytes = 0;
  if (oldFlowletSwitching) {
    oldDynamicEgressMaxThresholdBytes =
        oldFlowletSwitching->getDynamicEgressMaxThresholdBytes();
  }
  if (newFlowletSwitching) {
    newDynamicEgressMaxThresholdBytes =
        newFlowletSwitching->getDynamicEgressMaxThresholdBytes();
  }
  if (oldDynamicEgressMaxThresholdBytes != newDynamicEgressMaxThresholdBytes) {
    auto rv = bcm_switch_control_set(
        unit_,
        bcmSwitchEcmpDynamicEgressBytesMaxThreshold,
        newDynamicEgressMaxThresholdBytes);
    bcmCheckError(
        rv, "Failed to set bcmSwitchEcmpDynamicEgressBytesMaxThreshold");
  }
}

void BcmSwitch::processDynamicPhysicalQueueExponentChanged(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching) {
  uint16_t oldDynamicPhysicalQueueExponent = 0;
  uint16_t newDynamicPhysicalQueueExponent = 0;
  if (oldFlowletSwitching) {
    oldDynamicPhysicalQueueExponent =
        oldFlowletSwitching->getDynamicPhysicalQueueExponent();
  }
  if (newFlowletSwitching) {
    newDynamicPhysicalQueueExponent =
        newFlowletSwitching->getDynamicPhysicalQueueExponent();
  }
  if (oldDynamicPhysicalQueueExponent != newDynamicPhysicalQueueExponent) {
    auto rv = bcm_switch_control_set(
        unit_,
        bcmSwitchEcmpDynamicPhysicalQueuedBytesExponent,
        newDynamicPhysicalQueueExponent);
    bcmCheckError(
        rv, "Failed to set bcmSwitchEcmpDynamicPhysicalQueuedBytesExponent");
  }
}

void BcmSwitch::setEgressEcmpEtherType(uint32_t etherTypeEligiblity) {
  XLOG(DBG3) << "Flowlet switching setting ether type";
  int ecmp_dlb_ethtypes[] = {0x0800, 0x86DD};
  auto rv = bcm_l3_egress_ecmp_ethertype_set(
      unit_,
      etherTypeEligiblity,
      (sizeof(ecmp_dlb_ethtypes) / sizeof(ecmp_dlb_ethtypes[0])),
      ecmp_dlb_ethtypes);
  bcmCheckError(rv, "failed to set bcm_l3_egress_ecmp_ethertype_set");
}

void BcmSwitch::setEcmpDynamicRandomSeed(int ecmpRandomSeed) {
  XLOG(DBG3) << "Flowlet switching setting random seed";
  // seed value is as recommended by BCM
  auto rv = bcm_switch_control_set(
      unit_, bcmSwitchEcmpDynamicRandomSeed, ecmpRandomSeed);
  bcmCheckError(rv, "failed to set bcmSwitchEcmpDynamicRandomSeed");
}

void BcmSwitch::processFlowletSwitchingConfigChanges(const StateDelta& delta) {
  const auto flowletSwitchingDelta = delta.getFlowletSwitchingConfigDelta();
  const auto& newFlowletSwitching = flowletSwitchingDelta.getNew();

  egressManager_->processFlowletSwitchingConfigChanged(newFlowletSwitching);
}

void BcmSwitch::processEcmpForArsChanges(const StateDelta& delta) {
  const auto flowletSwitchingDelta = delta.getFlowletSwitchingConfigDelta();
  const auto& oldFlowletSwitching = flowletSwitchingDelta.getOld();
  const auto& newFlowletSwitching = flowletSwitchingDelta.getNew();
  // process change in the flowlet switching config
  if (!oldFlowletSwitching && !newFlowletSwitching) {
    XLOG(DBG4) << "Flowlet switching config is null";
    return;
  }
  // Set ethertype eligibility to 0 for DLB-DLB and DLB-ECMP
  // work as expected with flowlet ACL for existing DLB enabled switches.
  setEgressEcmpEtherType(0);

  // flowlet is enabled here. lets walk through all ecmp objects to ensure
  // things look ok for most purposes, this will be a no-op
  const bool updateSuccess =
      writableMultiPathNextHopTable()->updateEcmpsForFlowletTableLocked();
  XLOG(DBG3) << "Update of the flowlet table: " << std::boolalpha
             << updateSuccess;

  if (oldFlowletSwitching && newFlowletSwitching &&
      (*oldFlowletSwitching == *newFlowletSwitching)) {
    // PortFlowlet config is changed but the global Flowlet config did not
    // change then we need to update all the egress objects for TH3 here. Due
    // to ECMP-Egress object dependency in TH3, updating egress object is done
    // here.
    egressManager_->updateAllEgressForFlowletSwitching();
    XLOG(DBG4) << "Flowlet switching config is same";
    return;
  }

  processDynamicEgressLoadExponentChanged(
      oldFlowletSwitching, newFlowletSwitching);
  processDynamicQueueExponentChanged(oldFlowletSwitching, newFlowletSwitching);
  processDynamicQueueMinThresholdBytesChanged(
      oldFlowletSwitching, newFlowletSwitching);
  processDynamicQueueMaxThresholdBytesChanged(
      oldFlowletSwitching, newFlowletSwitching);
  processDynamicSampleRateChanged(oldFlowletSwitching, newFlowletSwitching);
  processDynamicEgressMinThresholdBytesChanged(
      oldFlowletSwitching, newFlowletSwitching);
  processDynamicEgressMaxThresholdBytesChanged(
      oldFlowletSwitching, newFlowletSwitching);
  processDynamicPhysicalQueueExponentChanged(
      oldFlowletSwitching, newFlowletSwitching);

  if (newFlowletSwitching) {
    if (!oldFlowletSwitching) {
      XLOG(DBG2) << "Flowlet switching config changed";
      setEcmpDynamicRandomSeed(0x5555);
    }
    // Update All egress first for flowlet config add or update
    // This ordering is needed otherwise SDK fails for TH3
    egressManager_->updateAllEgressForFlowletSwitching();
    writableMultiPathNextHopTable()->updateEcmpsForFlowletSwitching();
  } else if (oldFlowletSwitching && !newFlowletSwitching) {
    XLOG(DBG2) << "Flowlet switching config is removed";
    setEcmpDynamicRandomSeed(0);
    // Update All ecmps first for flowlet config removal
    writableMultiPathNextHopTable()->updateEcmpsForFlowletSwitching();
    egressManager_->updateAllEgressForFlowletSwitching();
  }
}

void BcmSwitch::processMacTableChanges(const StateDelta& stateDelta) {
  for (const auto& vlanDelta : stateDelta.getVlansDelta()) {
    auto vlanId = vlanDelta.getOld() ? vlanDelta.getOld()->getID()
                                     : vlanDelta.getNew()->getID();
    for (const auto& delta : vlanDelta.getMacDelta()) {
      const auto* oldEntry = delta.getOld().get();
      const auto* newEntry = delta.getNew().get();

      if (!oldEntry) {
        macTable_->processMacAdded(newEntry, vlanId);
      } else if (!newEntry) {
        macTable_->processMacRemoved(oldEntry, vlanId);
      } else {
        macTable_->processMacChanged(oldEntry, newEntry, vlanId);
      }
    }
  }
}

std::shared_ptr<SwitchState> BcmSwitch::stateChangedImpl(
    const std::vector<StateDelta>& deltas,
    const std::optional<
        StateDeltaApplication>& /* deltaApplicationBehavior */) {
  // This is unlikely to happen but if it does, return current state
  if (deltas.size() == 0) {
    return getProgrammedState();
  }
  std::shared_ptr<SwitchState> appliedState{nullptr};
  // Take the lock before modifying any objects
  std::lock_guard<std::mutex> lock(lock_);
  int count = 1;
  for (const auto& delta : deltas) {
    appliedState = stateChangedImplLocked(delta, lock);
    // if the current delta fails to apply, return the last successful state
    if (appliedState != delta.newState()) {
      XLOG(DBG2) << "Failed to apply " << count << " delta in  "
                 << deltas.size() << " deltas";
      appliedState->publish();
      return appliedState;
    }
    count++;
    appliedState->publish();
  }
  return appliedState;
}

std::shared_ptr<SwitchState> BcmSwitch::stateChangedImplLocked(
    const StateDelta& delta,
    const std::lock_guard<std::mutex>& /*lock*/) {
  // Sys ports not supported
  checkUnsupportedDelta(delta.getSystemPortsDelta(), *sysPortMgr_);
  checkUnsupportedDelta(delta.getRemoteSystemPortsDelta(), *sysPortMgr_);

  // Reconfigure port groups in case we are changing between using a port as
  // 1, 2 or 4 ports. Only do this if flexports are enabled
  // Calling reconfigure port group first to make sure the ports of SW state
  // already exists in HW.
  if (FLAGS_flexports) {
    reconfigurePortGroups(delta);
  }

  forEachAdded(delta.getPortsDelta(), [this](const auto& newPort) {
    if (!portTable_->getBcmPortIf(newPort->getID())) {
      throw FbossError(
          "Cannot add a port:", newPort->getID(), " unknown to hardware");
    }
  });

  forEachRemoved(delta.getPortsDelta(), [this](const auto& oldPort) {
    if (portTable_->getBcmPortIf(oldPort->getID())) {
      throw FbossError(
          "Cannot remove a port:", oldPort->getID(), " still in port table");
    }
  });
  auto appliedState = delta.newState();
  // TODO: This function contains high-level logic for how to apply the
  // StateDelta, and isn't particularly hardware-specific.  I plan to refactor
  // it, and move it out into a common helper class that can be shared by
  // many different HwSwitch implementations.

  // As the first step, disable ports that are now disabled.
  // This ensures that we immediately stop forwarding traffic on these ports.
  processDisabledPorts(delta);

  processSwitchSettingsChanged(delta);

  processMacTableChanges(delta);

  processUdfAdd(delta);

  processLoadBalancerChanges(delta);

  // Need to update port flowlet config for added ports
  // before neighbor/route delta programming of egress objects
  // after warm boot for TH3
  processPortFlowletConfigAdd(delta);

  // Process Flowlet config changes
  processFlowletSwitchingConfigChanges(delta);

  // remove all routes to be deleted
  processRemovedRoutes(delta);

  // Any neighbor removals, and modify appliedState if some changes fail to
  // apply
  processNeighborDelta(delta.getIntfsDelta(), &appliedState, REMOVED);

  // delete all interface not existing anymore. that should stop
  // all traffic on that interface now
  forEachRemoved(delta.getIntfsDelta(), &BcmSwitch::processRemovedIntf, this);

  // Add all new VLANs, and modify VLAN port memberships.
  // We don't actually delete removed VLANs at this point, we simply remove
  // all members from the VLAN.  This way any ports that ingress packets to
  // this VLAN will still use this VLAN until we get the new VLAN fully
  // configured.
  forEachChanged(
      delta.getVlansDelta(),
      &BcmSwitch::processChangedVlan,
      &BcmSwitch::processAddedVlan,
      &BcmSwitch::preprocessRemovedVlan,
      this);

  // Broadcom requires a default VLAN to always exist.
  // This VLAN is used as the default ingress VLAN for ports that don't have a
  // default ingress set.
  //
  // We always specify the ingress VLAN for all enabled ports, so this VLAN is
  // never really used for us.  We instead always point the default VLAN.
  if (delta.oldState()->getDefaultVlan() !=
      delta.newState()->getDefaultVlan()) {
    changeDefaultVlan(
        delta.oldState()->getDefaultVlan(), delta.newState()->getDefaultVlan());
  }

  // Update changed interfaces
  forEachChanged(delta.getIntfsDelta(), &BcmSwitch::processChangedIntf, this);

  // Remove deleted VLANs
  forEachRemoved(delta.getVlansDelta(), &BcmSwitch::processRemovedVlan, this);

  // Add all new interfaces
  forEachAdded(delta.getIntfsDelta(), &BcmSwitch::processAddedIntf, this);

  // Any changes to the Qos maps
  processQosChanges(delta);

  processControlPlaneChanges(delta);

  processAggregatePortChanges(delta);

  // Any neighbor additions/changes, and modify appliedState if some changes
  // fail to apply
  //
  // If FLAGS_intf_nbr_table is false, intf nbr table processing is no-op
  // If FLAGS_intf_nbr_table is true, vlan nbr table processing is no-op
  //
  // TODO(skhare) Once FLAGS_intf_nbr_table = true is rolled out, remove the
  // vlan neighbor processing.
  processNeighborDelta(delta.getIntfsDelta(), &appliedState, ADDED);
  processNeighborDelta(delta.getIntfsDelta(), &appliedState, CHANGED);

  processNeighborDelta(delta.getVlansDelta(), &appliedState, ADDED);
  processNeighborDelta(delta.getVlansDelta(), &appliedState, CHANGED);

  // process label forwarding changes after neighbor entries are updated
  processChangedLabelForwardingInformationBase(delta);

  // Add/update mirrors before processing Acl and port changes
  // This is to ensure that port and acls can access latest mirrors
  forEachAdded(
      delta.getMirrorsDelta(),
      &BcmMirrorTable::processAddedMirror,
      writableBcmMirrorTable());
  forEachChanged(
      delta.getMirrorsDelta(),
      &BcmMirrorTable::processChangedMirror,
      writableBcmMirrorTable());

  // Any ACL changes
  processAclChanges(delta);

  // remove any udf after we are done processing load balancer and ACL
  processUdfRemove(delta);

  // Any TeFlow changes
  processTeFlowChanges(delta, &appliedState);

  // Any changes to the set of sFlow collectors
  processSflowCollectorChanges(delta);

  // Any changes to the sampling rate of sflow
  processSflowSamplingRateChanges(delta);

  // Process any new routes or route changes
  processAddedChangedRoutes(delta, &appliedState);

  processAddedPorts(delta);
  processChangedPorts(delta);

  // Process all ECMP groups with new ARS config
  processEcmpForArsChanges(delta);

  // delete any removed mirrors after processing port and acl changes
  forEachRemoved(
      delta.getMirrorsDelta(),
      &BcmMirrorTable::processRemovedMirror,
      writableBcmMirrorTable());

  // Process global PFC watchdog configurations
  processPfcWatchdogGlobalChanges(delta);

  pickupLinkStatusChanges(delta);

  // As the last step, enable newly enabled ports.  Doing this as the
  // last step ensures that we only start forwarding traffic once the
  // ports are correctly configured. Note that this will also set the
  // ingressVlan and speed correctly before enabling.
  processEnabledPorts(delta);

  bcmStatUpdater_->refreshPostBcmStateChange(delta);

  return appliedState;
}

unique_ptr<TxPacket> BcmSwitch::allocatePacket(uint32_t size) const {
  // For future reference: Allocating the packet data requires the unit number
  // of the unit that the packet will be used with.  Our allocatePacket() API
  // doesn't require the caller to specify which ports they plan to use the
  // packet with.
  //
  // At the moment we only support a single unit, so this isn't really an
  // issue.  However, it may be more challenging for a HwSwitch implementation
  // that supports multiple units.  Fortunately, the linux userspace
  // implemetation uses the same DMA pool for all local units, so it wouldn't
  // really matter which unit we specified when allocating the buffer.
  return make_unique<BcmTxPacket>(unit_, size, this);
}

void BcmSwitch::processDisabledPorts(const StateDelta& delta) {
  forEachChanged(
      delta.getPortsDelta(),
      [&](const shared_ptr<Port>& oldPort, const shared_ptr<Port>& newPort) {
        if (oldPort->isEnabled() && !newPort->isEnabled()) {
          auto bcmPort = portTable_->getBcmPort(newPort->getID());
          XLOG(DBG2) << "Disabling port: " << newPort->getID();
          bcmPort->disable(newPort);
        }
      });
}

void BcmSwitch::processEnabledPortQueues(const shared_ptr<Port>& port) {
  auto id = port->getID();
  auto bcmPort = portTable_->getBcmPort(id);

  for (const auto& queue : std::as_const(*port->getPortQueues())) {
    XLOG(DBG1) << "Enable cos queue settings on port " << port->getID()
               << " queue: " << static_cast<int>(queue->getID());
    bcmPort->setupQueue(*queue);
  }
}

void BcmSwitch::processEnabledPorts(const StateDelta& delta) {
  forEachChanged(
      delta.getPortsDelta(),
      [&](const shared_ptr<Port>& oldPort, const shared_ptr<Port>& newPort) {
        if (!newPort->isEnabled()) {
          // skip if the port is not even enabled
          return;
        }
        // There're only two cases that we should enable the port:
        // 1) If the old port is disabled
        // 2) If the speed changes
        if (!oldPort->isEnabled() ||
            (oldPort->getSpeed() != newPort->getSpeed())) {
          auto bcmPort = portTable_->getBcmPort(newPort->getID());
          bcmPort->enable(newPort);
          processEnabledPortQueues(newPort);
        }
      });

  forEachAdded(delta.getPortsDelta(), [&](const shared_ptr<Port>& port) {
    // For any newly created port, as long as it needs enable, always enable
    if (port->isEnabled()) {
      auto bcmPort = portTable_->getBcmPort(port->getID());
      bcmPort->enable(port);
      processEnabledPortQueues(port);
    }
  });
}

bool BcmSwitch::processChangedIngressPoolCfg(
    std::optional<BufferPoolFields> oldBufferPoolCfg,
    std::optional<BufferPoolFields> newBufferPoolCfg) {
  // bufferPool <-> noBufferPool
  if ((oldBufferPoolCfg && !newBufferPoolCfg) ||
      (!oldBufferPoolCfg && newBufferPoolCfg)) {
    return true;
  }
  // bufferPool exists old and new, but doesn't match
  if (oldBufferPoolCfg && newBufferPoolCfg) {
    if (*oldBufferPoolCfg != *newBufferPoolCfg) {
      return true;
    }
  }
  return false;
}

int BcmSwitch::pfcDeadlockRecoveryEventCallback(
    int unit,
    bcm_port_t port,
    bcm_cos_queue_t cosq,
    bcm_cosq_pfc_deadlock_recovery_event_t recovery_state,
    void* userdata) {
  Callback* callback = (Callback*)userdata;
  XLOG_EVERY_MS(WARNING, 5000)
      << "PFC deadlock recovery callback invoked for unit " << unit << " port "
      << static_cast<int>(port) << " cosq " << static_cast<int>(cosq)
      << " recovery state " << static_cast<int>(recovery_state);

  CHECK(callback);
  if (recovery_state == bcmCosqPfcDeadlockRecoveryEventBegin) {
    // deadlock detected
    callback->pfcWatchdogStateChanged(PortID(port), true);
  } else if (recovery_state == bcmCosqPfcDeadlockRecoveryEventEnd) {
    callback->pfcWatchdogStateChanged(PortID(port), false);
  }
  return 0;
}

void BcmSwitch::processPfcWatchdogGlobalChanges(const StateDelta& delta) {
  auto oldRecoveryAction = delta.oldState()->getPfcWatchdogRecoveryAction();
  auto newRecoveryAction = delta.newState()->getPfcWatchdogRecoveryAction();

  if (!platform_->getAsic()->isSupported(HwAsic::Feature::PFC)) {
    // Look for PFC watchdog only if PFC is supported on platform
    return;
  }

  // Needed if PFC watchdog enabled status changes at device level
  if (oldRecoveryAction.has_value() != newRecoveryAction.has_value()) {
    int rv = 0;
    if (newRecoveryAction.has_value()) {
      // Register the PFC deadlock recovery callback function
      rv = bcm_cosq_pfc_deadlock_recovery_event_register(
          unit_, pfcDeadlockRecoveryEventCallback, callback_);
      bcmCheckError(rv, "Failed to register PFC deadlock recovery event!");
      flags_ |= PFC_WATCHDOG_REGISTERED;
    } else {
      // Unregister the PFC deadlock recovery callback function
      rv = bcm_cosq_pfc_deadlock_recovery_event_unregister(
          unit_, pfcDeadlockRecoveryEventCallback, callback_);
      bcmCheckError(rv, "Failed to unregister PFC deadlock recovery event!");
      flags_ &= ~PFC_WATCHDOG_REGISTERED;
    }
    XLOG(DBG2) << "PFC watchdog deadlock_recovery_event " << std::boolalpha
               << newRecoveryAction.has_value();
  }

  // Default recovery action is NO_DROP
  if (newRecoveryAction != oldRecoveryAction) {
    bcm_switch_pfc_deadlock_action_t recoveryAction =
        bcmSwitchPFCDeadlockActionTransmit;
    auto recoveryActionConfig = cfg::PfcWatchdogRecoveryAction::NO_DROP;
    if (newRecoveryAction.has_value() &&
        (*newRecoveryAction == cfg::PfcWatchdogRecoveryAction::DROP)) {
      recoveryAction = bcmSwitchPFCDeadlockActionDrop;
      recoveryActionConfig = *newRecoveryAction;
    }
    auto rv = bcm_switch_control_set(
        unit_, bcmSwitchPFCDeadlockRecoveryAction, recoveryAction);
    bcmCheckError(rv, "Failed to set bcmSwitchPFCDeadlockRecoveryAction");
    XLOG(DBG2) << "PFC watchdog deadlock recovery action "
               << apache::thrift::util::enumNameSafe(recoveryActionConfig)
               << " programmed!";
  }
}

bool BcmSwitch::processChangedPgCfg(
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  const auto& oldPortPgCfgs = oldPort->getPortPgConfigs();
  const auto& newPortPgCfgs = newPort->getPortPgConfigs();
  std::map<int, state::PortPgFields> newPortPgConfigMap;

  if (!oldPortPgCfgs && !newPortPgCfgs) {
    // no change before or after
    return false;
  }

  if ((oldPortPgCfgs && !newPortPgCfgs) || (!oldPortPgCfgs && newPortPgCfgs)) {
    // if go from old pgConfig <-> no pg cfg and vice versa, there is a change
    return true;
  }

  if (oldPortPgCfgs->size() != newPortPgCfgs->size()) {
    return true;
  }

  for (const auto& portPg : std::as_const(*newPortPgCfgs)) {
    // THRIFT_COPY
    newPortPgConfigMap.emplace(
        portPg->cref<switch_state_tags::id>()->cref(), portPg->toThrift());
  }

  for (const auto& oldPortPg : std::as_const(*oldPortPgCfgs)) {
    auto iter = newPortPgConfigMap.find(
        oldPortPg->cref<switch_state_tags::id>()->cref());
    // THRIFT_COPY
    auto oldPortPgThrift = oldPortPg->toThrift();
    if ((iter == newPortPgConfigMap.end()) ||
        (iter->second != oldPortPgThrift)) {
      return true;
    }

    // also validate buffer pools associated with the given pg if any
    const auto& oldBufferPoolCfg =
        oldPortPgThrift.bufferPoolConfig().to_optional();
    const auto& newBufferPoolCfg =
        iter->second.bufferPoolConfig().to_optional();
    if (processChangedIngressPoolCfg(oldBufferPoolCfg, newBufferPoolCfg)) {
      XLOG(DBG1) << "New ingress buffer pool changes on port: "
                 << newPort->getID();
      return true;
    }
  }

  // no change
  return false;
}

void BcmSwitch::processChangedPortQueues(
    const shared_ptr<Port>& oldPort,
    const shared_ptr<Port>& newPort) {
  auto id = newPort->getID();
  auto bcmPort = portTable_->getBcmPort(id);

  // We expect the number of port queues to remain constant because this is
  // defined by the hardware
  for (const auto& newQueue : newPort->getPortQueues()->impl()) {
    if (oldPort->getPortQueues()->size() > 0 &&
        *(oldPort->getPortQueues()->at(newQueue->getID())) == *newQueue) {
      continue;
    }

    XLOG(DBG1) << "New cos queue settings on port " << id << " queue "
               << static_cast<int>(newQueue->getID());
    bcmPort->setupStatsIfNeeded(newPort);
    bcmPort->setupQueue(*newQueue);
  }
}

bool BcmSwitch::processChangedPortFlowletCfg(
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  std::shared_ptr<PortFlowletCfg> oldPortFlowletCfg{nullptr};
  std::shared_ptr<PortFlowletCfg> newPortFlowletCfg{nullptr};
  if (oldPort->getPortFlowletConfig().has_value()) {
    oldPortFlowletCfg = oldPort->getPortFlowletConfig().value();
  }
  if (newPort->getPortFlowletConfig().has_value()) {
    newPortFlowletCfg = newPort->getPortFlowletConfig().value();
  }
  // old port flowlet cfg exists and new one doesn't or vice versa
  if ((newPortFlowletCfg && !oldPortFlowletCfg) ||
      (!newPortFlowletCfg && oldPortFlowletCfg)) {
    return true;
  }
  // contents changed in the port flowlet cfg
  if (oldPortFlowletCfg && newPortFlowletCfg) {
    if (*oldPortFlowletCfg != *newPortFlowletCfg) {
      return true;
    }
  }
  // no change
  return false;
}

void BcmSwitch::processPortFlowletConfigAdd(const StateDelta& delta) {
  // To update port flowlet config in bcm port object for all added ports
  // This will not do any hardware programming on the port.
  forEachAdded(delta.getPortsDelta(), [&](const shared_ptr<Port>& port) {
    auto id = port->getID();
    auto bcmPort = portTable_->getBcmPort(id);
    bcmPort->setPortFlowletConfig(port);
  });
}

void BcmSwitch::processAddedPorts(const StateDelta& delta) {
  // For now, this just enables stats on newly added ports
  forEachAdded(delta.getPortsDelta(), [&](const shared_ptr<Port>& port) {
    auto id = port->getID();
    auto bcmPort = portTable_->getBcmPort(id);

    if (port->isEnabled()) {
      // This ensures all the settings are up to date. We need to
      // be very careful that program() is both idempotent and
      // does not cause port flaps if called with the same
      // settings twice.
      bcmPort->program(port);
      bcmPort->enableStatCollection(port);
    }
  });
}

void BcmSwitch::processChangedPorts(const StateDelta& delta) {
  forEachChanged(
      delta.getPortsDelta(),
      [&](const shared_ptr<Port>& oldPort, const shared_ptr<Port>& newPort) {
        auto id = newPort->getID();
        auto bcmPort = portTable_->getBcmPort(id);

        if (!oldPort->isEnabled() && !newPort->isEnabled()) {
          // No need to process changes on disabled ports. We will pick up
          // changes should the port ever become enabled.
          return;
        }

        auto speedChanged = oldPort->getSpeed() != newPort->getSpeed();
        XLOG_IF(DBG1, speedChanged) << "New speed on port " << id;

        auto profileIDChanged =
            oldPort->getProfileID() != newPort->getProfileID();
        XLOG_IF(DBG1, profileIDChanged) << "New profileID on port " << id;

        auto vlanChanged =
            oldPort->getIngressVlan() != newPort->getIngressVlan();
        XLOG_IF(DBG1, vlanChanged) << "New ingress vlan on port " << id;

        auto pauseChanged = oldPort->getPause() != newPort->getPause();
        XLOG_IF(DBG1, pauseChanged) << "New pause settings on port " << id;

        auto sFlowChanged = (oldPort->getSflowIngressRate() !=
                             newPort->getSflowIngressRate()) ||
            (oldPort->getSflowEgressRate() != newPort->getSflowEgressRate());
        XLOG_IF(DBG1, sFlowChanged) << "New sFlow settings on port " << id;

        auto loopbackChanged =
            oldPort->getLoopbackMode() != newPort->getLoopbackMode();
        XLOG_IF(DBG1, loopbackChanged)
            << "New loopback mode settings on port " << id;

        auto mirrorChanged =
            (oldPort->getIngressMirror() != newPort->getIngressMirror()) ||
            (oldPort->getEgressMirror() != newPort->getEgressMirror());
        XLOG_IF(DBG1, mirrorChanged) << "New mirror settings on port " << id;

        auto qosPolicyChanged =
            oldPort->getQosPolicy() != newPort->getQosPolicy();
        XLOG_IF(DBG1, qosPolicyChanged) << "New Qos Policy on port " << id;

        auto nameChanged = oldPort->getName() != newPort->getName();
        XLOG_IF(DBG1, nameChanged) << "New name on port " << id;

        auto asicPrbsChanged = oldPort->getAsicPrbs() != newPort->getAsicPrbs();
        XLOG_IF(DBG1, asicPrbsChanged) << "New asicPrbs on port " << id;

        // if pfc config for port points to new PG profile, pfcChanged will
        // find it
        auto pfcChanged = oldPort->getPfc() != newPort->getPfc();
        XLOG_IF(DBG1, pfcChanged) << "New pfc settings on port " << id;

        auto pgCfgChanged = processChangedPgCfg(oldPort, newPort);
        XLOG_IF(DBG1, pgCfgChanged) << "New pg config settings on port " << id;

        // For port flowlet config changes
        auto flowletCfgChanged = processChangedPortFlowletCfg(oldPort, newPort);
        XLOG_IF(DBG1, flowletCfgChanged)
            << "New flowlet config settings on port " << id;

        if (speedChanged || profileIDChanged || vlanChanged || pauseChanged ||
            sFlowChanged || loopbackChanged || mirrorChanged ||
            qosPolicyChanged || nameChanged || asicPrbsChanged || pfcChanged ||
            pgCfgChanged || flowletCfgChanged) {
          bcmPort->program(newPort);
        }

        if (newPort->getPortQueues()->size() != 0 &&
            !platform_->getAsic()->isSupported(HwAsic::Feature::L3_QOS)) {
          throw FbossError(
              "Changing settings for cos queues not supported on ",
              "this platform");
        }

        processChangedPortQueues(oldPort, newPort);

        // For the support of setting zero preemphasis in hw tests.
        if (oldPort->getZeroPreemphasis() != newPort->getZeroPreemphasis()) {
          bcmPort->processChangedZeroPreemphasis(oldPort, newPort);
        }

        if (oldPort->getTxEnable() != newPort->getTxEnable()) {
          bcmPort->processChangedTxEnable(oldPort, newPort);
        }
      });
}

void BcmSwitch::pickupLinkStatusChanges(const StateDelta& delta) {
  forEachChanged(
      delta.getPortsDelta(),
      [&](const std::shared_ptr<Port>& oldPort,
          const std::shared_ptr<Port>& newPort) {
        auto id = newPort->getID();
        auto bcmPort = portTable_->getBcmPort(id);

        auto adminStateChanged =
            oldPort->getAdminState() != newPort->getAdminState();
        if (adminStateChanged) {
          auto adminStr = (newPort->isEnabled()) ? "ENABLED" : "DISABLED";
          XLOG(DBG1) << "Admin state changed on port " << id << ": "
                     << adminStr;
        }

        auto operStateChanged =
            oldPort->getOperState() != newPort->getOperState();
        if (operStateChanged) {
          auto operStr = (newPort->isUp()) ? "UP" : "DOWN";
          XLOG(DBG1) << "Oper state changed on port " << id << ": " << operStr;
        }

        if (adminStateChanged || operStateChanged) {
          bcmPort->linkStatusChanged(newPort);
        }

        if (auto faultStatus = newPort->getIPhyLinkFaultStatus()) {
          XLOG(DBG1) << newPort->getName() << " Fault status on port " << id
                     << " LocalFault: " << *(*faultStatus).localFault()
                     << ", RemoteFault: " << *(*faultStatus).remoteFault();
          bcmPort->cacheFaultStatus(*faultStatus);
        }

        if (oldPort->getLedPortExternalState() !=
            newPort->getLedPortExternalState()) {
          if (newPort->getLedPortExternalState().has_value()) {
            auto platformPort = bcmPort->getPlatformPort();
            platformPort->externalState(
                newPort->getLedPortExternalState().value());
          }
        }
      });
}

void BcmSwitch::reconfigurePortGroups(const StateDelta& delta) {
  // This logic is a bit messy. We could encode some notion of port
  // groups into the switch state somehow so it is easy to generate
  // deltas for these. For now, we need pass around the SwitchState
  // object and get the relevant ports manually.

  // Note that reconfigurePortGroups will program the speed and enable
  // newly enabled ports in its group. This means it can overlap a bit
  // with the work done in processEnabledPorts and processChangedPorts.
  // Both BcmPort::program and BcmPort::enable should be no-ops if already
  // programmed or already enabled. However, this MUST BE called before
  // those methods as enabling or changing the speed of a port may require
  // changing the configuration of a port group.

  auto oldState = delta.oldState();
  auto newState = delta.newState();
  forEachChanged(
      delta.getPortsDelta(),
      [&](const shared_ptr<Port>& oldPort, const shared_ptr<Port>& newPort) {
        auto enabled = !oldPort->isEnabled() && newPort->isEnabled();
        auto speedProfileChanged =
            oldPort->getProfileID() != newPort->getProfileID();
        auto sFlowChanged = (oldPort->getSflowIngressRate() !=
                             newPort->getSflowIngressRate()) ||
            (oldPort->getSflowEgressRate() != newPort->getSflowEgressRate());

        if (enabled || speedProfileChanged || sFlowChanged) {
          if (!isValidPortUpdate(oldPort, newPort, newState)) {
            // Fail hard
            throw FbossError("Invalid port configuration passed in ");
          }
          auto bcmPort = portTable_->getBcmPort(newPort->getID());
          auto portGroup = bcmPort->getPortGroup();
          if (portGroup) {
            portGroup->reconfigureIfNeeded(oldState, newState);
          }
        }
      });
}

bool BcmSwitch::isValidPortUpdate(
    const shared_ptr<Port>& oldPort,
    const shared_ptr<Port>& newPort,
    const shared_ptr<SwitchState>& newState) const {
  if (auto mirrorName = newPort->getIngressMirror()) {
    auto mirror = newState->getMirrors()->getNodeIf(mirrorName.value());
    if (!mirror) {
      XLOG(ERR) << "Ingress mirror " << mirrorName.value()
                << " for port : " << newPort->getID() << " not found";
      return false;
    }
  }

  if (auto mirrorName = newPort->getEgressMirror()) {
    auto mirror = newState->getMirrors()->getNodeIf(mirrorName.value());
    if (!mirror) {
      XLOG(ERR) << "Egress mirror " << mirrorName.value()
                << " for port : " << newPort->getID() << " not found";
      return false;
    }
  }

  if (newPort->getSampleDestination() &&
      newPort->getSampleDestination().value() ==
          cfg::SampleDestination::MIRROR) {
    auto ingressMirrorName = newPort->getIngressMirror();
    if (!ingressMirrorName) {
      XLOG(ERR)
          << "Ingress mirror  not set for sampled port with sample destination of mirror not set : "
          << newPort->getID();
      return false;
    }
    auto ingressMirror =
        newState->getMirrors()->getNodeIf(ingressMirrorName.value());
    if (ingressMirror->type() != Mirror::Type::SFLOW) {
      XLOG(ERR) << "Ingress mirror " << ingressMirrorName.value()
                << " for sampled port not sflow : " << newPort->getID();
      return false;
    }

    auto egressMirrorName = newPort->getEgressMirror();
    if (egressMirrorName) {
      XLOG(ERR) << "Egress mirror " << ingressMirrorName.value()
                << " for sampled port set on : " << newPort->getID()
                << " , egress sampling is not supported!";
      return false;
    }
  }

  auto enabled = !oldPort->isEnabled() && newPort->isEnabled();
  auto speedChanged = oldPort->getSpeed() != newPort->getSpeed();

  if (speedChanged || enabled) {
    auto bcmPort = portTable_->getBcmPort(newPort->getID());
    auto portGroup = bcmPort->getPortGroup();
    XLOG(DBG1) << "Verifying port group config for : " << newPort->getID();
    return !portGroup || portGroup->validConfiguration(newState);
  }
  if (!isValidPortQueueUpdate(
          oldPort->getPortQueues()->impl(), newPort->getPortQueues()->impl())) {
    return false;
  }
  if (!isValidPortQosPolicyUpdate(oldPort, newPort, newState)) {
    return false;
  }

  return true;
}

template <typename AddrT>
bool BcmSwitch::isRouteUpdateValid(const StateDelta& delta) const {
  using RouteT = Route<AddrT>;

  auto maxLabelStackDepth = getPlatform()->getAsic()->getMaxLabelStackDepth();
  auto validateLabeledRoute = [maxLabelStackDepth](const auto& route) {
    for (const auto& nhop : route->getForwardInfo().getNextHopSet()) {
      if (!nhop.labelForwardingAction()) {
        continue;
      }
      if (nhop.labelForwardingAction()->type() !=
          LabelForwardingAction::LabelForwardingType::PUSH) {
        return false;
      } else {
        if (nhop.labelForwardingAction()->pushStack()->size() >
            maxLabelStackDepth) {
          return false;
        }
      }
    }
    return true;
  };

  bool isValid = true;
  forEachChangedRoute<AddrT>(
      delta,
      [&isValid, validateLabeledRoute](
          RouterID /*rid*/,
          const std::shared_ptr<RouteT>& /*oldRoute*/,
          const std::shared_ptr<RouteT>& newRoute) {
        if (isValid && !validateLabeledRoute(newRoute)) {
          isValid = false;
        }
      },
      [&isValid, validateLabeledRoute](
          RouterID /*rid*/, const std::shared_ptr<RouteT>& addedRoute) {
        if (isValid && !validateLabeledRoute(addedRoute)) {
          isValid = false;
        }
      },
      [](RouterID /*rid*/, const std::shared_ptr<RouteT>& /*removedRoute*/) {});
  return isValid;
}

bool BcmSwitch::isValidStateUpdate(const StateDelta& delta) const {
  auto newState = delta.newState();
  auto isValid = true;

  forEachChanged(
      delta.getPortsDelta(),
      [&](const shared_ptr<Port>& oldPort, const shared_ptr<Port>& newPort) {
        if (isValid && !isValidPortUpdate(oldPort, newPort, newState)) {
          isValid = false;
        }
      });
  isValid = isValid &&
      (newState->getMirrors()->numNodes() <=
       platform_->getAsic()->getMaxMirrors());

  forEachChanged(
      delta.getMirrorsDelta(),
      [&](const shared_ptr<Mirror>& /* oldMirror */,
          const shared_ptr<Mirror>& newMirror) {
        if (newMirror->getTruncate() &&
            !getPlatform()->getAsic()->isSupported(
                HwAsic::Feature::MIRROR_PACKET_TRUNCATION)) {
          XLOG(ERR)
              << "Mirror packet truncation is not supported on this platform";
          isValid = false;
        }
      });

  forEachAdded(
      delta.getQosPoliciesDelta(),
      [&](const std::shared_ptr<QosPolicy>& qosPolicy) {
        isValid = isValid && BcmQosPolicyTable::isValid(qosPolicy);
      });

  forEachChanged(
      delta.getQosPoliciesDelta(),
      [&](const std::shared_ptr<QosPolicy>& oldQosPolicy,
          const std::shared_ptr<QosPolicy>& newQosPolicy) {
        isValid = isValid && BcmQosPolicyTable::isValid(newQosPolicy);
      });

  forEachChanged(
      delta.getLabelForwardingInformationBaseDelta(),
      [&](const std::shared_ptr<Route<LabelID>>& /*oldEntry*/,
          const std::shared_ptr<Route<LabelID>>& newEntry) {
        // changed Fn
        isValid = isValid && isValidLabelForwardingEntry(newEntry.get());
      },
      [&](const std::shared_ptr<Route<LabelID>>& newEntry) {
        // added Fn
        isValid = isValid && isValidLabelForwardingEntry(newEntry.get());
      },
      [](const std::shared_ptr<Route<LabelID>>& /*oldEntry*/) {
        // removed Fn
      });

  isValid = isValid && isRouteUpdateValid<folly::IPAddressV4>(delta);
  isValid = isValid && isRouteUpdateValid<folly::IPAddressV6>(delta);

  forEachChanged(
      delta.getAclsDelta(),
      [&](const shared_ptr<AclEntry>& /* oldAcl */,
          const shared_ptr<AclEntry>& newAcl) {
        isValid = isValid && hasValidAclMatcher(newAcl);
        return isValid ? LoopAction::CONTINUE : LoopAction::BREAK;
      },
      [&](const shared_ptr<AclEntry>& addAcl) {
        isValid = isValid && hasValidAclMatcher(addAcl);
        return isValid ? LoopAction::CONTINUE : LoopAction::BREAK;
      },
      [&](const shared_ptr<AclEntry>& /* delAcl */) {});

  if (getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::INGRESS_L3_INTERFACE)) {
    // default vlan l3 interface should not be created from port vlan config
    forEachAdded(
        delta.getIntfsDelta(), [&](const std::shared_ptr<Interface>& intf) {
          isValid = isValid &&
              intf->getVlanID() != delta.newState()->getDefaultVlan();
        });
  }

  std::shared_ptr<Port> firstPort;
  std::optional<cfg::PfcWatchdogRecoveryAction> recoveryAction{};
  for (const auto& portMap : std::as_const(*newState->getPorts())) {
    for (const auto& port : std::as_const(*portMap.second)) {
      if (port.second->getPfc().has_value() &&
          port.second->getPfc()->watchdog().has_value()) {
        auto pfcWd = port.second->getPfc()->watchdog().value();
        if (!recoveryAction.has_value()) {
          recoveryAction = *pfcWd.recoveryAction();
          firstPort = port.second;
        } else if (*recoveryAction != *pfcWd.recoveryAction()) {
          // Error: All ports should have the same recovery action configured
          XLOG(ERR) << "PFC watchdog deadlock recovery action on "
                    << port.second->getName() << " conflicting with "
                    << firstPort->getName();
          isValid = false;
        }
      }
    }
  }
  return isValid;
}

void BcmSwitch::changeDefaultVlan(VlanID oldId, VlanID newId) {
  auto rv = bcm_vlan_default_set(unit_, newId);
  bcmCheckError(rv, "failed to set default VLAN to ", newId);
  if (getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::INGRESS_L3_INTERFACE)) {
    // create or update L3 intf for default vlan,
    // otherwise, packets with default vlan tag will
    // be dropped due to no mathcing in ingress L3 intf
    if (intfTable_->getBcmIntfIf(oldId)) {
      auto oldIntf = make_shared<Interface>(
          InterfaceID(oldId),
          RouterID(0), /* currently VRF is always zero anyway */
          std::optional<VlanID>(oldId),
          folly::StringPiece(fmt::format("Interface-{}", uint16_t(oldId))),
          getPlatform()->getLocalMac(),
          9000, /* mtu */
          false, /* is virtual */
          false /* is state_sync disabled*/);
      intfTable_->deleteIntf(oldIntf);
    }
    auto newIntf = make_shared<Interface>(
        InterfaceID(newId),
        RouterID(0), /* currently VRF is always zero anyway */
        std::optional<VlanID>(newId),
        folly::StringPiece(fmt::format("Interface-{}", uint16_t(newId))),
        getPlatform()->getLocalMac(),
        9000, /* mtu */
        false, /* is virtual */
        false /* is state_sync disabled*/);
    intfTable_->addIntf(newIntf);
  }
}

void BcmSwitch::processChangedVlan(
    const shared_ptr<Vlan>& oldVlan,
    const shared_ptr<Vlan>& newVlan) {
  // Update port membership
  bcm_pbmp_t addedPorts;
  BCM_PBMP_CLEAR(addedPorts);
  bcm_pbmp_t addedUntaggedPorts;
  BCM_PBMP_CLEAR(addedUntaggedPorts);
  bcm_pbmp_t removedPorts;
  BCM_PBMP_CLEAR(removedPorts);
  const auto& oldPorts = oldVlan->getPortsInfo();
  const auto& newPorts = newVlan->getPortsInfo();

  auto oldIter = oldPorts.begin();
  auto newIter = newPorts.begin();
  uint32_t numAdded{0};
  uint32_t numRemoved{0};
  while (oldIter != oldPorts.end() || newIter != newPorts.end()) {
    if (oldIter == oldPorts.end() ||
        (newIter != newPorts.end() && newIter->first < oldIter->first)) {
      // This port was added
      ++numAdded;
      bcm_port_t bcmPort = portTable_->getBcmPortId(PortID(newIter->first));
      BCM_PBMP_PORT_ADD(addedPorts, bcmPort);
      if (!*newIter->second.tagged()) {
        BCM_PBMP_PORT_ADD(addedUntaggedPorts, bcmPort);
      }
      ++newIter;
    } else if (
        newIter == newPorts.end() ||
        (oldIter != oldPorts.end() && oldIter->first < newIter->first)) {
      // This port was removed
      ++numRemoved;
      bcm_port_t bcmPort = portTable_->getBcmPortId(PortID(oldIter->first));
      BCM_PBMP_PORT_ADD(removedPorts, bcmPort);
      ++oldIter;
    } else {
      ++oldIter;
      ++newIter;
    }
  }

  XLOG(DBG2) << "updating VLAN " << newVlan->getID() << ": " << numAdded
             << " ports added, " << numRemoved << " ports removed";
  if (numRemoved) {
    auto rv = bcm_vlan_port_remove(unit_, newVlan->getID(), removedPorts);
    bcmCheckError(rv, "failed to remove ports from VLAN ", newVlan->getID());
  }
  if (numAdded) {
    auto rv = bcm_vlan_port_add(
        unit_, newVlan->getID(), addedPorts, addedUntaggedPorts);
    bcmCheckError(rv, "failed to add ports to VLAN ", newVlan->getID());
  }
}

void BcmSwitch::processAddedVlan(const shared_ptr<Vlan>& vlan) {
  XLOG(DBG2) << "creating VLAN " << vlan->getID() << " with "
             << vlan->getPortsInfo().size() << " ports";

  bcm_pbmp_t pbmp;
  bcm_pbmp_t ubmp;
  BCM_PBMP_CLEAR(pbmp);
  BCM_PBMP_CLEAR(ubmp);

  for (const auto& entry : vlan->getPortsInfo()) {
    bcm_port_t bcmPort = portTable_->getBcmPortId(PortID(entry.first));
    BCM_PBMP_PORT_ADD(pbmp, bcmPort);
    if (!*entry.second.tagged()) {
      BCM_PBMP_PORT_ADD(ubmp, bcmPort);
    }
  }
  using VlanInfo = BcmWarmBootCache::VlanInfo;
  // Since during warm boot all VLAN in the config will show
  // up as added VLANs we only need to consult the warm boot
  // cache here.
  auto vlanItr = warmBootCache_->findVlanInfo(vlan->getID());
  if (vlanItr != warmBootCache_->vlan2VlanInfo_end()) {
    // Compare with existing vlan to determine if we need to reprogram
    auto equivalent = [](VlanInfo newVlan, VlanInfo existingVlan) {
      return BCM_PBMP_EQ(newVlan.allPorts, existingVlan.allPorts) &&
          BCM_PBMP_EQ(newVlan.untagged, existingVlan.untagged);
    };
    const auto& existingVlan = vlanItr->second;
    if (!equivalent(VlanInfo(vlan->getID(), ubmp, pbmp), existingVlan)) {
      XLOG(DBG1) << "updating VLAN " << vlan->getID() << " with "
                 << vlan->getPortsInfo().size() << " ports";
      auto oldVlan = vlan->clone();
      warmBootCache_->fillVlanPortInfo(oldVlan.get());
      processChangedVlan(oldVlan, vlan);
    } else {
      XLOG(DBG1) << " Vlan : " << vlan->getID() << " already exists ";
    }
    warmBootCache_->programmed(vlanItr);
  } else {
    XLOG(DBG1) << "creating VLAN " << vlan->getID() << " with "
               << vlan->getPortsInfo().size() << " ports";
    auto rv = bcm_vlan_create(unit_, vlan->getID());
    bcmCheckError(rv, "failed to add VLAN ", vlan->getID());
    rv = bcm_vlan_port_add(unit_, vlan->getID(), pbmp, ubmp);
    bcmCheckError(rv, "failed to add members to new VLAN ", vlan->getID());
  }
}

void BcmSwitch::preprocessRemovedVlan(const shared_ptr<Vlan>& vlan) {
  // Remove all ports from this VLAN at this phase.
  XLOG(DBG2) << "preparing to remove VLAN " << vlan->getID();
  auto rv = bcm_vlan_gport_delete_all(unit_, vlan->getID());
  bcmCheckError(rv, "failed to remove members from VLAN ", vlan->getID());
}

void BcmSwitch::processRemovedVlan(const shared_ptr<Vlan>& vlan) {
  XLOG(DBG2) << "removing VLAN " << vlan->getID();

  auto rv = bcm_vlan_destroy(unit_, vlan->getID());
  bcmCheckError(rv, "failed to remove VLAN ", vlan->getID());
}

void BcmSwitch::processChangedIntf(
    const shared_ptr<Interface>& oldIntf,
    const shared_ptr<Interface>& newIntf) {
  CHECK_EQ(oldIntf->getID(), newIntf->getID());
  XLOG(DBG2) << "changing interface " << oldIntf->getID();
  intfTable_->programIntf(newIntf);
}

void BcmSwitch::processAddedIntf(const shared_ptr<Interface>& intf) {
  XLOG(DBG2) << "adding interface " << intf->getID();
  intfTable_->addIntf(intf);
}

void BcmSwitch::processRemovedIntf(const shared_ptr<Interface>& intf) {
  XLOG(DBG2) << "deleting interface " << intf->getID();
  intfTable_->deleteIntf(intf);
}

void BcmSwitch::processQosChanges(const StateDelta& delta) {
  auto oldDefaultQosPolicy = delta.oldState()
      ? delta.oldState()->getDefaultDataPlaneQosPolicy()
      : nullptr;
  auto newDefaultQosPolicy = delta.newState()
      ? delta.newState()->getDefaultDataPlaneQosPolicy()
      : nullptr;

  if (oldDefaultQosPolicy && newDefaultQosPolicy) {
    qosPolicyTable_->processChangedDefaultDataPlaneQosPolicy(
        oldDefaultQosPolicy, newDefaultQosPolicy);
  } else if (oldDefaultQosPolicy) {
    qosPolicyTable_->processRemovedDefaultDataPlaneQosPolicy(
        oldDefaultQosPolicy);
  } else if (newDefaultQosPolicy) {
    qosPolicyTable_->processAddedDefaultDataPlaneQosPolicy(newDefaultQosPolicy);
  }

  forEachChanged(
      delta.getQosPoliciesDelta(),
      &BcmQosPolicyTable::processChangedQosPolicy,
      &BcmQosPolicyTable::processAddedQosPolicy,
      &BcmQosPolicyTable::processRemovedQosPolicy,
      qosPolicyTable_.get());
}

void BcmSwitch::processAclChanges(const StateDelta& delta) {
  // Removals first: processAddedAcl rejects a priority that is still occupied,
  // so an entry taking over a priority another entry is vacating must not be
  // added before the old one is gone.
  forEachRemoved(delta.getAclsDelta(), &BcmSwitch::processRemovedAcl, this);
  forEachChanged(delta.getAclsDelta(), &BcmSwitch::processChangedAcl, this);
  forEachAdded(delta.getAclsDelta(), &BcmSwitch::processAddedAcl, this);
}

void BcmSwitch::processTeFlowChanges(
    const StateDelta& delta,
    std::shared_ptr<SwitchState>* appliedState) {
  std::vector<TeFlow> discardedFlows;
  forEachChanged(
      delta.getTeFlowEntriesDelta(),
      [&](const shared_ptr<TeFlowEntry>& oldTeFlowEntry,
          const shared_ptr<TeFlowEntry>& newTeFlowEntry) {
        try {
          processChangedTeFlow(oldTeFlowEntry, newTeFlowEntry);
        } catch (const BcmError& e) {
          rethrowIfHwNotFull(e);
          discardedFlows.emplace_back(oldTeFlowEntry->getFlow()->toThrift());
        }
      },
      [&](const shared_ptr<TeFlowEntry>& addedTeFlowEntry) {
        try {
          processAddedTeFlow(addedTeFlowEntry);
        } catch (const BcmError& e) {
          rethrowIfHwNotFull(e);
          discardedFlows.emplace_back(addedTeFlowEntry->getFlow()->toThrift());
        }
      },
      [&](const shared_ptr<TeFlowEntry>& deletedTeFlowEntry) {
        processRemovedTeFlow(deletedTeFlowEntry);
      });
  if (!discardedFlows.empty()) {
    SwitchState::modify(appliedState);
    XLOG(DBG2) << "Discarded Teflows size : " << discardedFlows.size();
    auto newFlowTable = delta.newState()->getTeFlowTable();
    auto oldFlowTable = delta.oldState()->getTeFlowTable();
    for (const auto& flow : discardedFlows) {
      auto oldEntry = oldFlowTable->getNodeIf(getTeFlowStr(flow));
      auto newEntry = newFlowTable->getNodeIf(getTeFlowStr(flow));
      SwitchState::revertNewTeFlowEntry(newEntry, oldEntry, appliedState);
    }
  }
}

void BcmSwitch::processAggregatePortChanges(const StateDelta& delta) {
  forEachChanged(
      delta.getAggregatePortsDelta(),
      &BcmSwitch::processChangedAggregatePort,
      &BcmSwitch::processAddedAggregatePort,
      &BcmSwitch::processRemovedAggregatePort,
      this);
}

void BcmSwitch::processChangedSflowCollector(
    const std::shared_ptr<SflowCollector>& /* oldCollector */,
    const std::shared_ptr<SflowCollector>& /* newCollector */) {
  XLOG(ERR) << "sFlow collector should should only change on restarts";
}

void BcmSwitch::processRemovedSflowCollector(
    const std::shared_ptr<SflowCollector>& collector) {
  if (!sFlowExporterTable_->contains(collector)) {
    throw FbossError("Tried to remove non-existent sFlow exporter");
  }

  sFlowExporterTable_->removeExporter(collector->getID());
}

void BcmSwitch::processAddedSflowCollector(
    const shared_ptr<SflowCollector>& collector) {
  if (sFlowExporterTable_->contains(collector)) {
    // Something is wrong.
    throw FbossError("Tried to add an existing sFlow exporter");
  }

  sFlowExporterTable_->addExporter(collector);
}

void BcmSwitch::processSflowSamplingRateChanges(const StateDelta& delta) {
  forEachChanged(
      delta.getPortsDelta(),
      [&](const shared_ptr<Port>& oldPort, const shared_ptr<Port>& newPort) {
        auto oldIngressRate = oldPort->getSflowIngressRate();
        auto oldEgressRate = oldPort->getSflowEgressRate();
        auto newIngressRate = newPort->getSflowIngressRate();
        auto newEgressRate = newPort->getSflowEgressRate();
        auto sFlowChanged = (oldIngressRate != newIngressRate) ||
            (oldEgressRate != newEgressRate);
        if (sFlowChanged) {
          auto id = newPort->getID();
          sFlowExporterTable_->updateSamplingRates(
              id, newIngressRate, newEgressRate);
        }
      });
}

void BcmSwitch::processSflowCollectorChanges(const StateDelta& delta) {
  forEachChanged(
      delta.getSflowCollectorsDelta(),
      &BcmSwitch::processChangedSflowCollector,
      &BcmSwitch::processAddedSflowCollector,
      &BcmSwitch::processRemovedSflowCollector,
      this);
}

template <typename NeighborEntryT>
void BcmSwitch::processAddedAndChangedNeighbor(
    const BcmHostKey& neighborKey,
    const BcmIntf* intf,
    const NeighborEntryT* entry) {
  auto* neighbor = neighborTable_->getNeighbor(neighborKey);
  CHECK(neighbor);
  /*
   * Prior to S391473, we used default noHostRoute to false. However
   * some legacy SAI devices had this value erroneously set to true.
   * So to mitigate we set made the field optional. However, some
   * devices were already rolled out with the default of false.
   * In Bcm layer we don't model noHostRoute setting. That comes out
   * to a behavior of noHostRoute=false (default) or noHostRoute not
   * being set.
   */
  if (entry->getNoHostRoute().has_value() && *entry->getNoHostRoute()) {
    throw FbossError(
        "No host route = true on neighbor entry not supported on BcmSwitch");
  }
  BcmHostTableIf* hostTable;
  if (getPlatform()->getAsic()->isSupported(HwAsic::Feature::HOSTTABLE)) {
    hostTable = hostTable_.get();
  } else {
    hostTable = routeTable_.get();
  }
  if (entry->isPending()) {
    hostTable->programHostsToCPU(neighborKey, intf->getBcmIfId());
    return;
  }
  auto neighborMac = entry->getMac();
  auto isTrunk = entry->getPort().isAggregatePort();
  if (isTrunk) {
    auto trunk = entry->getPort().aggPortID();
    hostTable->programHostsToTrunk(
        neighborKey,
        intf->getBcmIfId(),
        neighborMac,
        getTrunkTable()->getBcmTrunkId(trunk));
  } else {
    auto port = entry->getPort().phyPortID();
    hostTable->programHostsToPort(
        neighborKey,
        intf->getBcmIfId(),
        neighborMac,
        getPortTable()->getBcmPortId(port),
        entry->getClassID());
  }

  if (auto disableTTLDecrement = entry->getDisableTTLDecrement()) {
    setTTLDecrement(this, neighborKey, disableTTLDecrement.value());
  }
  std::for_each(
      writableMplsNextHopTable()->getNextHops().begin(),
      writableMplsNextHopTable()->getNextHops().end(),
      [neighborKey](const auto& entry) {
        auto nhop = entry.second.lock();
        if (nhop->getBcmHostKey() == neighborKey) {
          nhop->program(neighborKey);
        }
      });
}

template <typename NeighborEntryT>
void BcmSwitch::processAddedNeighborEntry(const NeighborEntryT* addedEntry) {
  CHECK(addedEntry);
  if (addedEntry->isPending()) {
    XLOG(DBG3) << "adding pending neighbor entry to "
               << addedEntry->getIP().str()
               << (addedEntry->getClassID().has_value()
                       ? static_cast<int>(addedEntry->getClassID().value())
                       : 0);
  } else {
    XLOG(DBG3) << "adding neighbor entry " << addedEntry->getIP().str()
               << " to " << addedEntry->getMac().toString()
               << (addedEntry->getClassID().has_value()
                       ? static_cast<int>(addedEntry->getClassID().value())
                       : 0);
  }

  const auto* intf = getIntfTable()->getBcmIntf(addedEntry->getIntfID());
  auto vrf = getBcmVrfId(intf->getInterface()->getRouterID());

  auto neighborKey =
      BcmHostKey(vrf, IPAddress(addedEntry->getIP()), addedEntry->getIntfID());
  neighborTable_->registerNeighbor(neighborKey);
  processAddedAndChangedNeighbor(neighborKey, intf, addedEntry);
}

template <typename NeighborEntryT>
void BcmSwitch::processChangedNeighborEntry(
    const NeighborEntryT* oldEntry,
    const NeighborEntryT* newEntry) {
  CHECK(oldEntry);
  CHECK(newEntry);
  CHECK_EQ(oldEntry->getIP(), newEntry->getIP());
  if (newEntry->isPending()) {
    XLOG(DBG3) << "changing neighbor entry " << newEntry->getIP().str()
               << " classID: "
               << (newEntry->getClassID().has_value()
                       ? static_cast<int>(newEntry->getClassID().value())
                       : 0)
               << " to pending";
  } else {
    XLOG(DBG3) << "changing neighbor entry " << newEntry->getIP().str()
               << " classID: "
               << (newEntry->getClassID().has_value()
                       ? static_cast<int>(newEntry->getClassID().value())
                       : 0)
               << " to " << newEntry->getMac().toString();
  }

  const auto* intf = getIntfTable()->getBcmIntf(newEntry->getIntfID());
  auto vrf = getBcmVrfId(intf->getInterface()->getRouterID());

  auto neighborKey =
      BcmHostKey(vrf, IPAddress(newEntry->getIP()), newEntry->getIntfID());

  processAddedAndChangedNeighbor(neighborKey, intf, newEntry);
}

template <typename NeighborEntryT>
void BcmSwitch::processRemovedNeighborEntry(
    const NeighborEntryT* removedEntry) {
  CHECK(removedEntry);
  XLOG(DBG3) << "deleting neighbor entry " << removedEntry->getIP().str();

  const auto* intf = getIntfTable()->getBcmIntf(removedEntry->getIntfID());
  auto vrf = getBcmVrfId(intf->getInterface()->getRouterID());

  auto neighborKey = BcmHostKey(
      vrf, IPAddress(removedEntry->getIP()), removedEntry->getIntfID());
  neighborTable_->unregisterNeighbor(neighborKey);
  if (getPlatform()->getAsic()->isSupported(HwAsic::Feature::HOSTTABLE)) {
    hostTable_->programHostsToCPU(neighborKey, intf->getBcmIfId());
  } else {
    routeTable_->programHostsToCPU(neighborKey, intf->getBcmIfId());
  }
  std::for_each(
      writableMplsNextHopTable()->getNextHops().begin(),
      writableMplsNextHopTable()->getNextHops().end(),
      [neighborKey](const auto& entry) {
        auto nhop = entry.second.lock();
        if (nhop->getBcmHostKey() == neighborKey) {
          nhop->program(neighborKey);
        }
      });
}

template <typename MapDeltaT>
void BcmSwitch::processNeighborDelta(
    const MapDeltaT& delta,
    std::shared_ptr<SwitchState>* appliedState,
    DeltaType optype) {
  processNeighborTableDelta<MapDeltaT, folly::IPAddressV4>(
      delta, appliedState, optype);
  processNeighborTableDelta<MapDeltaT, folly::IPAddressV6>(
      delta, appliedState, optype);
}

template <typename MapDeltaT, typename AddrT>
void BcmSwitch::processNeighborTableDelta(
    const MapDeltaT& mapDelta,
    std::shared_ptr<SwitchState>* appliedState,
    DeltaType optype) {
  using NeighborTableT = std::conditional_t<
      std::is_same<AddrT, folly::IPAddressV4>::value,
      ArpTable,
      NdpTable>;
  using NeighborEntryT = typename NeighborTableT::Entry;
  using NeighborEntryDeltaT = typename ThriftMapDelta<NeighborTableT>::VALUE;
  std::vector<NeighborEntryDeltaT> discardedNeighborEntryDelta;

  for (const auto& d : mapDelta) {
    for (const auto& delta : d.template getNeighborDelta<NeighborTableT>()) {
      try {
        const auto* oldEntry = delta.getOld().get();
        const auto* newEntry = delta.getNew().get();

        if (optype == ADDED && !oldEntry) {
          processAddedNeighborEntry(newEntry);
        } else if (optype == REMOVED && !newEntry) {
          processRemovedNeighborEntry(oldEntry);
        } else if (optype == CHANGED && oldEntry && newEntry) {
          if (DeltaComparison::policy() == DeltaComparison::Policy::DEEP &&
              *oldEntry == *newEntry) {
            XLOG(DBG2)
                << "ignoring unchanged neighbor entry with deep delta comparison policy";
            continue;
          }
          processChangedNeighborEntry(oldEntry, newEntry);
        }
      } catch (const BcmError& error) {
        rethrowIfHwNotFull(error);
        discardedNeighborEntryDelta.push_back(delta);
      }
    }
  }

  for (const auto& delta : discardedNeighborEntryDelta) {
    SwitchState::revertNewNeighborEntry<NeighborEntryT, NeighborTableT>(
        delta.getNew(), delta.getOld(), appliedState);
  }
}

template <typename RouteT>
void BcmSwitch::processChangedRoute(
    const RouterID& id,
    const shared_ptr<RouteT>& oldRoute,
    const shared_ptr<RouteT>& newRoute) {
  std::string routeMessage;
  folly::toAppend(
      "changing route entry @ vrf ",
      id,
      " from old: ",
      oldRoute->str(),
      "to new: ",
      newRoute->str(),
      &routeMessage);
  XLOG(DBG3) << routeMessage;
  // if the new route is not resolved, delete it instead of changing it
  if (!newRoute->isResolved()) {
    XLOG(DBG1) << "Non-resolved route HW programming is skipped";
    processRemovedRoute(id, oldRoute);
  } else {
    routeTable_->addRoute(getBcmVrfId(id), newRoute.get());
  }
}

template <typename RouteT>
void BcmSwitch::processAddedRoute(
    const RouterID& id,
    const shared_ptr<RouteT>& route) {
  std::string routeMessage;
  folly::toAppend(
      "adding route entry @ vrf ", id, " ", route->str(), &routeMessage);
  XLOG(DBG3) << routeMessage;
  // if the new route is not resolved, ignore it
  if (!route->isResolved()) {
    XLOG(DBG1) << "Non-resolved route HW programming is skipped";
    return;
  }
  routeTable_->addRoute(getBcmVrfId(id), route.get());
}

template <typename RouteT>
void BcmSwitch::processRemovedRoute(
    const RouterID& id,
    const shared_ptr<RouteT>& route) {
  XLOG(DBG3) << "removing route entry @ vrf " << id << " " << route->str();
  if (!route->isResolved()) {
    XLOG(DBG1) << "Non-resolved route HW programming is skipped";
    return;
  }
  routeTable_->deleteRoute(getBcmVrfId(id), route.get());
}

void BcmSwitch::processRemovedRoutes(const StateDelta& delta) {
  forEachChangedRoute(
      delta,
      [](RouterID /*id*/, const auto& /*oldRoute*/, const auto& /*newRoute*/) {
      },
      [](RouterID /*id*/, const auto& /*addedRoute*/) {},
      [this](RouterID rid, const auto& deleted) {
        processRemovedRoute(rid, deleted);
      });
  multiPathNextHopTable_->updateDlbExhaustionStat();
}

void BcmSwitch::processAddedChangedRoutes(
    const StateDelta& delta,
    std::shared_ptr<SwitchState>* appliedState) {
  processRouteTableDelta<folly::IPAddressV4>(delta, appliedState);
  processRouteTableDelta<folly::IPAddressV6>(delta, appliedState);
  multiPathNextHopTable_->updateDlbExhaustionStat();
}

template <typename AddrT>
void BcmSwitch::processRouteTableDelta(
    const StateDelta& delta,
    std::shared_ptr<SwitchState>* appliedState) {
  using RouteT = Route<AddrT>;
  using PrefixT = typename Route<AddrT>::Prefix;
  std::map<RouterID, std::vector<PrefixT>> discardedPrefixes;
  if (!isRouteUpdateValid<AddrT>(delta)) {
    // typically indicate label stack depth exceeded.
    throw FbossError("invalid route update");
  }
  forEachChangedRoute<AddrT>(
      delta,
      [&](RouterID id,
          const shared_ptr<RouteT>& oldRoute,
          const shared_ptr<RouteT>& newRoute) {
        if (DeltaComparison::policy() == DeltaComparison::Policy::DEEP &&
            *oldRoute == *newRoute) {
          XLOG(DBG2)
              << "ignoring unchanged route entry with deep delta comparison policy";
          return;
        }
        try {
          processChangedRoute(id, oldRoute, newRoute);
        } catch (const BcmError& e) {
          rethrowIfHwNotFull(e);
          discardedPrefixes[id].push_back(oldRoute->prefix());
        }
      },
      [&](RouterID id, const shared_ptr<RouteT>& addedRoute) {
        try {
          processAddedRoute(id, addedRoute);
        } catch (const BcmError& e) {
          rethrowIfHwNotFull(e);
          discardedPrefixes[id].push_back(addedRoute->prefix());
        }
      },
      [](RouterID /*rid*/, const shared_ptr<RouteT>& /*deletedRoute*/) {
        // do nothing
      });

  // discard  routes
  for (const auto& entry : discardedPrefixes) {
    const auto id = entry.first;
    for (const auto& prefix : entry.second) {
      const auto newRoute =
          findRoute<AddrT>(id, prefix.toCidrNetwork(), delta.newState());
      const auto oldRoute =
          findRoute<AddrT>(id, prefix.toCidrNetwork(), delta.oldState());
      SwitchState::revertNewRouteEntry(id, newRoute, oldRoute, appliedState);
    }
  }
}

void BcmSwitch::linkscanCallback(
    int unit,
    bcm_port_t bcmPort,
    bcm_port_info_t* info) {
  try {
    BcmUnit* unitObj = BcmAPI::getUnit(unit);
    BcmSwitch* hw = static_cast<BcmSwitch*>(unitObj->getCookie());
    bool up = info->linkstatus == BCM_PORT_LINK_STATUS_UP;
    phy::LinkFaultStatus linkFault;
    linkFault.localFault() = info->fault & BCM_PORT_FAULT_LOCAL;
    linkFault.remoteFault() = info->fault & BCM_PORT_FAULT_REMOTE;

    hw->linkScanBottomHalfEventBase_.runInFbossEventBaseThread(
        [hw, bcmPort, up, linkFault]() {
          hw->linkStateChangedHwNotLocked(bcmPort, up, linkFault);
        });
  } catch (const std::exception& ex) {
    XLOG(ERR) << "unhandled exception while processing linkscan callback "
              << "for unit " << unit << " port " << bcmPort << ": "
              << folly::exceptionStr(ex);
  }
}

void BcmSwitch::linkStateChangedHwNotLocked(
    bcm_port_t bcmPortId,
    bool up,
    phy::LinkFaultStatus iPhyLinkFaultStatus) {
  CHECK(linkScanBottomHalfEventBase_.inRunningEventBaseThread());

  if (!up) {
    auto trunk = trunkTable_->linkDownHwNotLocked(bcmPortId);
    if (trunk != BcmTrunk::INVALID) {
      XLOG(DBG2) << "Shrinking ECMP entries egressing over trunk " << trunk;
      writableEgressManager()->trunkDownHwNotLocked(trunk);
    }
    writableEgressManager()->linkDownHwNotLocked(bcmPortId);
  } else {
    // For port up events we wait till ARP/NDP entries
    // are re resolved after port up before adding them
    // back. Adding them earlier leads to packet loss.
  }
  callback_->linkStateChanged(
      portTable_->getPortId(bcmPortId),
      up,
      cfg::PortType::INTERFACE_PORT,
      iPhyLinkFaultStatus);
}

// The callback provided to bcm_rx_register()
bcm_rx_t
BcmSwitch::packetRxSdkCallback(int unit, bcm_pkt_t* pkt, void* cookie) {
  auto* bcmSw = static_cast<BcmSwitch*>(cookie);
  DCHECK_EQ(bcmSw->getUnit(), unit);
  DCHECK_EQ(bcmSw->getUnit(), pkt->unit);

  BcmPacketT bcmPacket;
  bcmPacket.usePktIO = false;
  bcmPacket.ptrUnion.pkt = pkt;
  return (bcm_rx_t)(bcmSw->packetReceived(bcmPacket));
}

#ifdef INCLUDE_PKTIO
// The callback provided to bcm_pktio_rx_register()
bcm_pktio_rx_t BcmSwitch::pktioPacketRxSdkCallback(
    int unit,
    bcm_pktio_pkt_t* pkt,
    void* cookie) {
  auto* bcmSw = static_cast<BcmSwitch*>(cookie);
  DCHECK_EQ(bcmSw->getUnit(), unit);
  DCHECK_EQ(bcmSw->getUnit(), pkt->unit);

  BcmPacketT bcmPacket;
  bcmPacket.usePktIO = true;
  bcmPacket.ptrUnion.pktioPkt = pkt;
  return (bcm_pktio_rx_t)(bcmSw->packetReceived(bcmPacket));
}
#endif

int BcmSwitch::packetReceived(BcmPacketT& bcmPacket) noexcept {
  bool usePktIO = bcmPacket.usePktIO;
  DCHECK_EQ(usePktIO, usePKTIO());

  // Log it if it's an sFlow sample
  if (handleSflowPacket(bcmPacket)) {
    // It was just here because it was an sFlow packet
    XLOG(DBG4) << "The packet is sample-only.  Skip further procssing.";

    if (!usePktIO) {
      bcm_pkt_t* pkt = bcmPacket.ptrUnion.pkt;
      bcm_pkt_rx_free(pkt->unit, pkt);
      return BCM_RX_HANDLED_OWNED;
    } else {
#ifdef INCLUDE_PKTIO
      return BCM_PKTIO_RX_HANDLED;
#else
      // TBD should not be here.
      // exception is not allowed.  what should be proper handling here?
      return BCM_RX_HANDLED_OWNED;
#endif
    }
  }

  // Otherwise, send it to the SwSwitch
  unique_ptr<BcmRxPacket> bcmPkt;
  try {
    bcmPkt = createRxPacket(bcmPacket);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "failed to allocate BcmRxPacket for receive handling: "
              << folly::exceptionStr(ex);
#ifdef INCLUDE_PKTIO
    return usePktIO ? BCM_PKTIO_RX_NOT_HANDLED : BCM_RX_NOT_HANDLED;
#else
    return BCM_RX_NOT_HANDLED;
#endif
  }

  callback_->packetReceived(std::move(bcmPkt));
#ifdef INCLUDE_PKTIO
  return usePktIO ? BCM_PKTIO_RX_HANDLED : BCM_RX_HANDLED_OWNED;
#else
  return BCM_RX_HANDLED_OWNED;
#endif
}

bool BcmSwitch::sendPacketSwitchedAsync(unique_ptr<TxPacket> pkt) noexcept {
  unique_ptr<BcmTxPacket> bcmPkt(
      boost::polymorphic_downcast<BcmTxPacket*>(pkt.release()));
  return BCM_SUCCESS(BcmTxPacket::sendAsync(std::move(bcmPkt), this));
}

bool BcmSwitch::sendPacketOutOfPortAsync(
    unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queue) noexcept {
  unique_ptr<BcmTxPacket> bcmPkt(
      boost::polymorphic_downcast<BcmTxPacket*>(pkt.release()));
#ifdef INCLUDE_PKTIO
  bcmPkt->setSwitched(false);
#endif
  bcmPkt->setDestModPort(getPortTable()->getBcmPortId(portID));
  if (queue) {
    bcmPkt->setCos(*queue);
  }
  return BCM_SUCCESS(BcmTxPacket::sendAsync(std::move(bcmPkt), this));
}

bool BcmSwitch::sendPacketSwitchedSync(unique_ptr<TxPacket> pkt) noexcept {
  unique_ptr<BcmTxPacket> bcmPkt(
      boost::polymorphic_downcast<BcmTxPacket*>(pkt.release()));
  return BCM_SUCCESS(BcmTxPacket::sendSync(std::move(bcmPkt), this));
}

bool BcmSwitch::sendPacketOutOfPortSync(
    unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> cos) noexcept {
  unique_ptr<BcmTxPacket> bcmPkt(
      boost::polymorphic_downcast<BcmTxPacket*>(pkt.release()));
#ifdef INCLUDE_PKTIO
  bcmPkt->setSwitched(false);
#endif
  if (cos.has_value()) {
    bcmPkt->setCos(cos.value());
  }
  bcmPkt->setDestModPort(getPortTable()->getBcmPortId(portID));
  return BCM_SUCCESS(BcmTxPacket::sendSync(std::move(bcmPkt), this));
}

void BcmSwitch::updateStatsImpl() {
  // Update global statistics.
  updateGlobalStats();
  // Update cpu or host bound packet stats
  controlPlane_->updateQueueCounters();
}

folly::F14FastMap<std::string, HwPortStats> BcmSwitch::getPortStats() const {
  folly::F14FastMap<std::string, HwPortStats> portStats;
  for (auto& bcmPortEntry : std::as_const(*portTable_)) {
    auto stat = bcmPortEntry.second->getPortStats();
    if (stat) {
      portStats.emplace(bcmPortEntry.second->getPortName(), *stat);
    }
  }
  return portStats;
}

TeFlowStats BcmSwitch::getTeFlowStats() const {
  return teFlowTable_->getFlowStats();
}

HwHighFrequencyStats BcmSwitch::getHighFrequencyStats() {
  HwHighFrequencyStats stats{};
  stats.timestampUs() = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
  portTable_->populateHighFrequencyPortStats(
      highFreqStatsThreadConfig_.statsConfig()->portStatsConfig().value(),
      *stats.portStats());
  bstStatsMgr_->populateHighFrequencyBstStats(
      highFreqStatsThreadConfig_.statsConfig().value(), stats);
  return stats;
}

std::vector<EcmpDetails> BcmSwitch::getAllEcmpDetails() const {
  return multiPathNextHopStatsManager_->getAllEcmpDetails();
}

cfg::SwitchingMode BcmSwitch::getFwdSwitchingMode(
    const RouteNextHopEntry& fwd) {
  std::lock_guard<std::mutex> lock(lock_);
  return multiPathNextHopTable_->getFwdSwitchingMode(
      getBcmVrfId(RouterID(0)), fwd.normalizedNextHops());
}

shared_ptr<BcmSwitchEventCallback> BcmSwitch::registerSwitchEventCallback(
    bcm_switch_event_t eventID,
    shared_ptr<BcmSwitchEventCallback> callback) {
  return BcmSwitchEventUtils::registerSwitchEventCallback(
      unit_, eventID, callback);
}
void BcmSwitch::unregisterSwitchEventCallback(bcm_switch_event_t eventID) {
  return BcmSwitchEventUtils::unregisterSwitchEventCallback(unit_, eventID);
}

void BcmSwitch::updateGlobalStats() {
  portTable_->updatePortStats();
  trunkTable_->updateStats();
  bcmStatUpdater_->updateStats();

  auto now =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  if ((now - bstStatsUpdateTime_ >= FLAGS_update_watermark_stats_interval_s) ||
      bstStatsMgr_->isFineGrainedBufferStatLoggingEnabled()) {
    bstStatsUpdateTime_ = now;
    bstStatsMgr_->updateStats();
  }
  if (FLAGS_flowletSwitchingEnable) {
    if (multiPathNextHopTable_) {
      auto dlbExhausted = multiPathNextHopTable_->getDlbExhaustedStat();
      getSwitchStats()->arsResourceExhausted(dlbExhausted);
    }
  }
}

bool BcmSwitch::getArsExhaustionStatus() {
  if (multiPathNextHopTable_) {
    return multiPathNextHopTable_->getDlbExhaustedStat();
  }
  return false;
}

std::map<PortID, phy::PhyInfo> BcmSwitch::updateAllPhyInfoImpl() {
  return portTable_->updateIPhyInfo();
}

uint64_t BcmSwitch::getDeviceWatermarkBytes() const {
  return bstStatsMgr_->getDeviceWatermarkBytes();
}

HwFlowletStats BcmSwitch::getHwFlowletStats() const {
  return multiPathNextHopStatsManager_->getHwFlowletStats();
}

HwSwitchWatermarkStats BcmSwitch::getSwitchWatermarkStats() const {
  HwSwitchWatermarkStats stats{};
  stats.deviceWatermarkBytes() = bstStatsMgr_->getDeviceWatermarkBytes();
  stats.globalHeadroomWatermarkBytes()->insert(
      bstStatsMgr_->getGlobalHeadroomWatermarkBytes().begin(),
      bstStatsMgr_->getGlobalHeadroomWatermarkBytes().end());
  stats.globalSharedWatermarkBytes()->insert(
      bstStatsMgr_->getGlobalSharedWatermarkBytes().begin(),
      bstStatsMgr_->getGlobalSharedWatermarkBytes().end());
  return stats;
}

HwSwitchPipelineStats BcmSwitch::getSwitchPipelineStats() const {
  return HwSwitchPipelineStats{};
}

HwSwitchTemperatureStats BcmSwitch::getSwitchTemperatureStats() const {
  return HwSwitchTemperatureStats{};
}

HwSwitchHardResetStats BcmSwitch::getHwSwitchHardResetStats() const {
  return HwSwitchHardResetStats{};
}

bcm_if_t BcmSwitch::getDropEgressId() const {
  return platform_->getAsic()->getDefaultDropEgressID();
}

bcm_if_t BcmSwitch::getToCPUEgressId() const {
  if (toCPUEgress_) {
    return toCPUEgress_->getID();
  } else {
    return BcmEgressBase::INVALID;
  }
}

void BcmSwitch::exitFatal() const {
  utilCreateDir(platform_->getDirectoryUtil()->getCrashInfoDir());
  dumpState(platform_->getDirectoryUtil()->getCrashHwStateFile());
  callback_->exitFatal();
}

void BcmSwitch::processChangedAggregatePort(
    const std::shared_ptr<AggregatePort>& oldAggPort,
    const std::shared_ptr<AggregatePort>& newAggPort) {
  CHECK_EQ(oldAggPort->getID(), newAggPort->getID());

  XLOG(DBG2) << "reprogramming AggregatePort " << oldAggPort->getID();
  trunkTable_->programTrunk(oldAggPort, newAggPort);
}

void BcmSwitch::processAddedAggregatePort(
    const std::shared_ptr<AggregatePort>& aggPort) {
  XLOG(DBG2) << "creating AggregatePort " << aggPort->getID() << " with "
             << aggPort->subportsCount() << " ports";
  trunkTable_->addTrunk(aggPort);
}

void BcmSwitch::processRemovedAggregatePort(
    const std::shared_ptr<AggregatePort>& aggPort) {
  XLOG(DBG2) << "deleting AggregatePort " << aggPort->getID();
  trunkTable_->deleteTrunk(aggPort);
}

void BcmSwitch::processLoadBalancerChanges(const StateDelta& delta) {
  forEachChanged(
      delta.getLoadBalancersDelta(),
      &BcmSwitch::processChangedLoadBalancer,
      &BcmSwitch::processAddedLoadBalancer,
      &BcmSwitch::processRemovedLoadBalancer,
      this);
}

void BcmSwitch::processUdfAdd(const StateDelta& delta) {
  std::set<bcm_udf_id_t> udfAclIds;

  forEachAdded(
      delta.getUdfPacketMatcherDelta(),
      &BcmSwitch::processAddedUdfPacketMatcher,
      this);

  forEachAdded(
      delta.getUdfGroupDelta(), &BcmSwitch::processAddedUdfGroup, this);
  udfManager_->getUdfAclGroupIds(udfAclIds);
  processDefaultAclgroupForUdf(udfAclIds);
}

void BcmSwitch::processAddedUdfPacketMatcher(
    const shared_ptr<UdfPacketMatcher>& udfPacketMatcher) {
  XLOG(DBG2) << "Adding udf packet matcher: " << udfPacketMatcher->getID();
  udfManager_->createUdfPacketMatcher(udfPacketMatcher);
}

void BcmSwitch::processAddedUdfGroup(const shared_ptr<UdfGroup>& udfGroup) {
  XLOG(DBG2) << "Adding udf group: " << udfGroup->getID();
  udfManager_->createUdfGroup(udfGroup);
}

void BcmSwitch::processDefaultAclgroupForUdf(
    std::set<bcm_udf_id_t>& udfAclIds) {
  const auto& udfQsetIdsInHW = getUdfQsetIds(
      unit_,
      static_cast<bcm_field_group_t>(
          platform_->getAsic()->getDefaultACLGroupID()));

  auto aclIdsToString = [](const std::set<bcm_udf_id_t> udfSet) {
    std::string udfAclsIds;
    std::for_each(
        udfSet.begin(), udfSet.end(), [&udfAclsIds](const auto& udfId) {
          udfAclsIds.append(std::to_string(udfId)).append(",");
        });
    return udfAclsIds;
  };

  if (udfAclIds == udfQsetIdsInHW) {
    // nothing to update here
    XLOG(DBG2) << "UDF ACL id no change. HW ids: "
               << aclIdsToString(udfQsetIdsInHW)
               << ". Config ids: " << aclIdsToString(udfAclIds);
    return;
  }

  XLOG(DBG2) << "Udf id mismatch in qset. Hw ids: "
             << aclIdsToString(udfQsetIdsInHW)
             << " .Config ids: " << aclIdsToString(udfAclIds);

  writableAclTable()->clearAclTable(
      platform_->getAsic()->getDefaultACLGroupID());
  clearFPGroup(unit_, platform_->getAsic()->getDefaultACLGroupID());
  createAclGroup(
      udfAclIds.size() ? std::optional<std::set<bcm_udf_id_t>>(udfAclIds)
                       : std::nullopt);
  writableAclTable()->reprogramAclTable(
      platform_->getAsic()->getDefaultACLGroupID());
}

void BcmSwitch::processUdfRemove(const StateDelta& delta) {
  std::set<bcm_udf_id_t> udfAclIds;
  udfManager_->getUdfAclGroupIds(udfAclIds);
  // We would like to collect the latest set of udfGroups with type ACL after
  // udfRemove so that we can reprogram the default acl group with the correct
  // set of udfGroups inside  processDefaultAclgroupForUdf.
  forEachRemoved(
      delta.getUdfGroupDelta(), [&](const std::shared_ptr<UdfGroup>& udfGroup) {
        if (udfManager_->getUdfGroupType(udfGroup->getID()) ==
            cfg::UdfGroupType::ACL) {
          udfAclIds.erase(udfManager_->getBcmUdfGroupId(udfGroup->getID()));
        }
      });

  processDefaultAclgroupForUdf(udfAclIds);
  forEachRemoved(
      delta.getUdfGroupDelta(), &BcmSwitch::processRemovedUdfGroup, this);

  forEachRemoved(
      delta.getUdfPacketMatcherDelta(),
      &BcmSwitch::processRemovedUdfPacketMatcher,
      this);
}

void BcmSwitch::processRemovedUdfPacketMatcher(
    const shared_ptr<UdfPacketMatcher>& udfPacketMatcher) {
  XLOG(DBG2) << "Removing udf packet matcher: " << udfPacketMatcher->getID();
  udfManager_->deleteUdfPacketMatcher(udfPacketMatcher->getID());
}

void BcmSwitch::processRemovedUdfGroup(const shared_ptr<UdfGroup>& udfGroup) {
  XLOG(DBG2) << "Removing udf group: " << udfGroup->getID();

  udfManager_->deleteUdfGroup(udfGroup);
}

void BcmSwitch::processChangedLoadBalancer(
    const std::shared_ptr<LoadBalancer>& oldLoadBalancer,
    const std::shared_ptr<LoadBalancer>& newLoadBalancer) {
  CHECK_EQ(oldLoadBalancer->getID(), newLoadBalancer->getID());

  XLOG(DBG2) << "reprogramming LoadBalancer " << oldLoadBalancer->getID();
  rtag7LoadBalancer_->programLoadBalancer(oldLoadBalancer, newLoadBalancer);
}

void BcmSwitch::processAddedLoadBalancer(
    const std::shared_ptr<LoadBalancer>& loadBalancer) {
  XLOG(DBG2) << "creating LoadBalancer " << loadBalancer->getID();
  rtag7LoadBalancer_->addLoadBalancer(loadBalancer);
}

void BcmSwitch::processRemovedLoadBalancer(
    const std::shared_ptr<LoadBalancer>& loadBalancer) {
  XLOG(DBG2) << "deleting LoadBalancer " << loadBalancer->getID();
  rtag7LoadBalancer_->deleteLoadBalancer(loadBalancer);
}

bool BcmSwitch::isControlPlaneQueueNameChanged(
    const shared_ptr<ControlPlane>& oldCPU,
    const shared_ptr<ControlPlane>& newCPU) {
  if ((oldCPU->getQueues()->size() != newCPU->getQueues()->size())) {
    return true;
  }

  for (const auto& newQueue : std::as_const(*newCPU->getQueues())) {
    const auto& oldQueue = oldCPU->getQueues()->cref(newQueue->getID());
    if (newQueue->getName() != oldQueue->getName()) {
      return true;
    }
  }
  return false;
}

void BcmSwitch::processChangedControlPlaneQueues(
    const shared_ptr<ControlPlane>& oldCPU,
    const shared_ptr<ControlPlane>& newCPU) {
  XLOG_IF(DBG1, oldCPU->getQueues()->size() != newCPU->getQueues()->size())
      << "Old cpu queue size:" << oldCPU->getQueues()->size()
      << ", but new cpu queue size:" << newCPU->getQueues()->size();
  // first make sure queue settings changes applied
  for (const auto& newQueue : std::as_const(*newCPU->getQueues())) {
    if (oldCPU->getQueues()->size() > newQueue->getID() &&
        *(oldCPU->getQueues()->cref(newQueue->getID())) == *newQueue) {
      continue;
    }
    XLOG(DBG1) << "New cos queue settings on cpu queue "
               << static_cast<int>(newQueue->getID());
    controlPlane_->setupQueue(*newQueue);
  }

  if (isControlPlaneQueueNameChanged(oldCPU, newCPU)) {
    controlPlane_->getQueueManager()->setupQueueCounters(
        newCPU->getQueues()->impl());
  }
}

void BcmSwitch::processChangedRxReasonToQueueEntries(
    const shared_ptr<ControlPlane>& oldCPU,
    const shared_ptr<ControlPlane>& newCPU) {
  const auto& oldReasonToQueue = oldCPU->getRxReasonToQueue();
  const auto& newReasonToQueue = newCPU->getRxReasonToQueue();
  for (int index = 0;
       index < std::max(newReasonToQueue->size(), oldReasonToQueue->size());
       ++index) {
    if (index >= oldReasonToQueue->size()) { // added
      controlPlane_->setReasonToQueueEntry(
          index, newReasonToQueue->cref(index)->toThrift());
    } else if (index >= newReasonToQueue->size()) { // deleted
      controlPlane_->deleteReasonToQueueEntry(index);
    } else if (
        oldReasonToQueue->cref(index)->toThrift() !=
        newReasonToQueue->cref(index)->toThrift()) { // changed
      controlPlane_->setReasonToQueueEntry(
          index, newReasonToQueue->cref(index)->toThrift());
    }
  }
}

void BcmSwitch::processMirrorChanges(const StateDelta& delta) {
  forEachChanged(
      delta.getMirrorsDelta(),
      &BcmMirrorTable::processChangedMirror,
      &BcmMirrorTable::processAddedMirror,
      &BcmMirrorTable::processRemovedMirror,
      writableBcmMirrorTable());
}

void BcmSwitch::clearPortStats(
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  bcmStatUpdater_->clearPortStats(ports);
}

std::vector<prbs::PrbsPolynomial> BcmSwitch::getPortPrbsPolynomials(
    int32_t portId) {
  return portTable_->getBcmPort(portId)->getPortPrbsPolynomials();
}

prbs::InterfacePrbsState BcmSwitch::getPortPrbsState(PortID portId) {
  return getBcmPortPrbsState(unit_, portTable_->getBcmPortId(portId));
}

void BcmSwitch::clearInterfacePhyCounters(
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  for (auto portId : *ports) {
    getPortTable()->getBcmPort(portId)->clearInterfacePhyCounters();
  }
}

std::vector<phy::PrbsLaneStats> BcmSwitch::getPortAsicPrbsStats(PortID portId) {
  return bcmStatUpdater_->getPortAsicPrbsStats(portId);
}

void BcmSwitch::clearPortAsicPrbsStats(PortID portId) {
  bcmStatUpdater_->clearPortAsicPrbsStats(portId);
}

void BcmSwitch::dumpState(const std::string& path) const {
  auto stateString = gatherSdkState();
  if (stateString.length() > 0) {
    folly::writeFile(stateString, path.c_str());
  }
}

void BcmSwitch::processChangedLabelForwardingInformationBase(
    const StateDelta& delta) {
  forEachChanged(
      delta.getLabelForwardingInformationBaseDelta(),
      &BcmSwitch::processChangedLabelForwardingEntry,
      &BcmSwitch::processAddedLabelForwardingEntry,
      &BcmSwitch::processRemovedLabelForwardingEntry,
      this);
}

void BcmSwitch::processAddedLabelForwardingEntry(
    const std::shared_ptr<Route<LabelID>>& addedEntry) {
  if (!isValidLabelForwardingEntry(addedEntry.get())) {
    throw FbossError("Invalid MPLS routes");
  }
  writableLabelMap()->processAddedLabelSwitchAction(
      addedEntry->getID(), addedEntry->getForwardInfo());
}

void BcmSwitch::processRemovedLabelForwardingEntry(
    const std::shared_ptr<Route<LabelID>>& deletedEntry) {
  writableLabelMap()->processRemovedLabelSwitchAction(deletedEntry->getID());
}

void BcmSwitch::processChangedLabelForwardingEntry(
    const std::shared_ptr<Route<LabelID>>& /*oldEntry*/,
    const std::shared_ptr<Route<LabelID>>& newEntry) {
  if (!isValidLabelForwardingEntry(newEntry.get())) {
    throw FbossError("Invalid MPLS routes");
  }
  writableLabelMap()->processChangedLabelSwitchAction(
      newEntry->getID(), newEntry->getForwardInfo());
}

void BcmSwitch::l2LearningCallback(
    int unit,
    bcm_l2_addr_t* l2Addr,
    int operation,
    void* userData) {
  auto* bcmSw = static_cast<BcmSwitch*>(userData);
  if (bcmSw->getRunState() < SwitchRunState::CONFIGURED) {
    XLOG(WARNING)
        << "ignoreing learn notifications as switch is not configured.";
    return;
  }
  DCHECK_EQ(bcmSw->getUnit(), unit);
  bcmSw->l2LearningUpdateReceived(l2Addr, operation);
}

/*
 * TH learning behavior:
 *
 * Typical Learning workflow:
 *  - ASIC encounters unknown source MAC+vlan.
 *  - ASIC creates a PENDING L2 entry for the MAC+vlan and generates callback.
 *  - In response, wedge_agent will VALIDATE the L2 entry.
 *
 * Typical Aging workflow:
 *  - SDK thread ages out a stale MAC+vlan entry.
 *  - This triggers a callback of type DELETE.
 *  - The MAC+vlan that is aged out would be VALIDATED as when the entry is
 *    learned, in response to PENDING ADD callback, wedge_agent would have
 *    VALIDATED the entry.
 *
 * The initial implementation (which used wrong BCM API to register callbacks,
 * and got L2 mod fifo to fill up), in addition to PENDING ADD callback, when
 * PENDING entry got VALIDATED, wedge_agent received DELETE for PENDING and
 * ADD for VALIDATED. Thus, wedge_agent had an explicit logic to ignore DELETE
 * callback for * PENDING and ADD callback for VALIDATED.
 *
 * However, with correct BCM API to register callbacks, we no longer get
 * DELETE for PENDING and ADD for VALIDATED, and thus those need not be
 * ignored here (Broadcom case CS9347300).
 *
 * Furthermore, there is a legitimate case where wedge_agent receives ADD for
 * VALIDATED, and thus wedge_agent cannot ignore this callback but must handle
 * it viz. Mac Move.
 *
 * Typical Mac Move workflow:
 *  - MAC+vlan is learned on a port p1 (say) using typical Learning workflow.
 *  - ASIC encounters the same source MAC+vlan on a different port p2 (say).
 *  - In response, the MAC is moved from port p1 to p2 and two callbacks are
 *    generated viz. DELETE for VALIDATED entry on p1, followed by ADD for
 *    VALIDATED entry on p2.
 *
 * TH3
 * ---
 *
 *  For TH3, there is no notion of PENDING
 *
 *  The entries are learned as VALIDATED and thus the first callback agent
 *  receives is for VALIDATED ADD entry, and wedge_agent must process it.
 *
 *  In response, the wedge_agent will program the already VALIDATED entry
 *  again. This has no effect and does not generate additional callback.
 *
 *  For TH3, a move generates DELETE on the old port and ADD on the new port,
 *  both for VALIDATED MACs.
 */
void BcmSwitch::l2LearningUpdateReceived(
    bcm_l2_addr_t* l2Addr,
    int operation) noexcept {
  bool detailedUpdate =
      platform_->getAsic()->isSupported(HwAsic::Feature::DETAILED_L2_UPDATE);
  if (l2Addr && isL2OperationOfInterest(operation, detailedUpdate)) {
    auto l2Entry = createL2Entry(l2Addr, isL2EntryPending(l2Addr));
    XLOG(DBG2) << "received L2 event callback for " << l2Entry.str()
               << " operation " << operation;
    callback_->l2LearningUpdateReceived(
        l2Entry, getL2EntryUpdateType(operation, detailedUpdate));
  }
}

void BcmSwitch::setupCos() {
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::EGRESS_QUEUE_FLEX_COUNTER)) {
    // Need to handle warmboot
    queueFlexCounterMgr_.reset(new BcmEgressQueueFlexCounterManager(this));
  }

  cosManager_.reset(new BcmCosManager(this));
  controlPlane_.reset(new BcmControlPlane(this));

  // set up cpu queue stats counter
  controlPlane_->getQueueManager()->setupQueueCounters();
}

std::unique_ptr<BcmRxPacket> BcmSwitch::createRxPacket(BcmPacketT bcmPacket) {
  return std::make_unique<FbBcmRxPacket>(bcmPacket, this);
}

bool BcmSwitch::isRxThreadRunning() const {
  return bcm_rx_active(unit_);
}

void BcmSwitch::stopLinkscanThread() {
  // Disable the linkscan thread
  auto rv = bcm_linkscan_enable_set(unit_, 0);
  CHECK(BCM_SUCCESS(rv)) << "failed to stop BcmSwitch linkscan thread "
                         << bcm_errmsg(rv);
  if (linkScanBottomHalfThread_) {
    linkScanBottomHalfEventBase_.runInFbossEventBaseThreadAndWait(
        [this] { linkScanBottomHalfEventBase_.terminateLoopSoon(); });
    linkScanBottomHalfThread_->join();
  }
}

void BcmSwitch::createAclGroup(
    const std::optional<std::set<bcm_udf_id_t>>& udfIds) {
  // Install the master ACL group here, whose content may change overtime
  bcm_field_qset_t qset = getAclQset();
  bool enableQsetCompression = false;
  if (udfIds) {
    updateUdfQset(unit_, qset, udfIds.value());
    enableQsetCompression = true;
  }

  createFPGroup(
      unit_,
      qset,
      platform_->getAsic()->getDefaultACLGroupID(),
      FLAGS_acl_g_pri,
      getPlatform()->getAsic()->isSupported(HwAsic::Feature::HSDK),
      enableQsetCompression);
}

void BcmSwitch::dropDhcpPackets() {
  // Don't flood broadcast DHCP packets to other hosts in the vlan. We
  // should still trap the packet to the cpu in this case though.
  auto rv = bcm_switch_control_set(unit_, bcmSwitchDhcpPktDrop, 1);
  bcmCheckError(rv, "failed to set DHCP packet dropping");
}

void BcmSwitch::setL3MtuFailPackets() {
  // trap packets for which L3 MTU check fails
  auto rv = bcm_switch_control_set(unit_, bcmSwitchL3MtuFailToCpu, 1);
  bcmCheckError(rv, "Failed to set L3 MTU fail to CPU");
}

void BcmSwitch::initFieldProcessor() const {
  // We need to make sure we remove all the FlexCounter attached to acl
  // entries in all the field process group before we call the
  // bcm_field_init(). Otherwise we'll lose the mapping b/w
  // BcmIngressFieldProcessorFlexCounter and BcmAclEntry and won't be able to
  // remove attached BcmIngressFieldProcessorFlexCounter even the BcmAclEntry
  // is removed.
  if (getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::INGRESS_FIELD_PROCESSOR_FLEX_COUNTER)) {
    for (auto grpId : {platform_->getAsic()->getDefaultACLGroupID()}) {
      BcmIngressFieldProcessorFlexCounter::removeAllCountersInFpGroup(
          unit_, grpId);
    }
    if (getPlatform()->getAsic()->isSupported(HwAsic::Feature::EXACT_MATCH)) {
      auto grpId = platform_->getAsic()->getDefaultTeFlowGroupID();
      BcmIngressFieldProcessorFlexCounter::removeAllCountersInFpGroup(
          unit_, getEMGroupID(grpId));
    }
  }

  auto rv = bcm_field_init(unit_);
  bcmCheckError(rv, "failed to set up ContentAware processing");
}

void BcmSwitch::initMirrorModule() const {
  if (platform_->getAsic()->isSupported(HwAsic::Feature::HSDK)) {
    // Do not need the calls below on HSDK: T74698149. bcm_mirror_init is
    // optional but harmless to call to make sure we are cleaning any state
    auto rv = bcm_mirror_init(unit_);
    bcmCheckError(rv, "failed to set up Mirroring");
    rv = bcm_switch_control_set(
        unit_, bcmSwitchSampleIngressRandomSeed, kHSDKSflowSamplingSeed);
    bcmCheckError(rv, "failed to set ingress mirror seed");
    return;
  }

  auto rv = bcm_switch_control_set(unit_, bcmSwitchDirectedMirroring, 1);
  bcmCheckError(rv, "Failed to set Switch Directed Mirroring");
  rv = bcm_mirror_init(unit_);
  bcmCheckError(rv, "failed to set up Mirroring");
  rv = bcm_mirror_mode_set(unit_, BCM_MIRROR_L2);
  bcmCheckError(rv, "Failed to set mirror mode");
}

void BcmSwitch::initMplsModule() const {
  auto rv = bcm_mpls_init(unit_);
  bcmCheckError(rv, "failed to set up label switching");
}

void BcmSwitch::setupFPGroups() {
  for (auto grpId : {platform_->getAsic()->getDefaultACLGroupID()}) {
    auto gid = static_cast<bcm_field_group_t>(grpId);
    XLOG(DBG1) << "Setting up FP group : " << gid;
    if (gid == platform_->getAsic()->getDefaultACLGroupID()) {
      createAclGroup();
    } else {
      throw FbossError("Unknown group id : ", gid);
    }
  }
}

bool BcmSwitch::haveMissingOrQSetChangedFPGroups() const {
  std::map<std::pair<int32_t, int32_t>, bcm_field_qset_t> grp2Qset = {
      {{platform_->getAsic()->getDefaultACLGroupID(), FLAGS_acl_g_pri},
       getAclQset()},
  };
  for (const auto& grpAndQset : grp2Qset) {
    auto gid = static_cast<bcm_field_group_t>(grpAndQset.first.first);
    auto qset = grpAndQset.second;
    if (!fpGroupExists(unit_, gid)) {
      XLOG(DBG1) << " FP group : " << gid << " does not exist";
      return true;
    } else if (!FPGroupDesiredQsetCmp(this, gid, qset).hasDesiredQset()) {
      XLOG(DBG2) << " FP group : " << gid << " has a changed QSET";
      return true;
    }
  }
  XLOG(DBG1) << " All FP groups have desired QSETs ";
  return false;
}

bcm_gport_t BcmSwitch::getCpuGPort() const {
  return BCM_GPORT_LOCAL_CPU;
}

// TODO(petr): this is here only because bcmRxReasonSampleSource and the like
// are not yet open sourced.
bool BcmSwitch::handleSflowPacket(BcmPacketT& bcmPacket) noexcept {
  bool ingressSample = false, egressSample = false, sampleOnly;
  uint32 src_port, dest_port, vlan, pkt_len;
  uint8* pkt_data;

  if (!bcmPacket.usePktIO) {
    bcm_pkt_t* pkt = bcmPacket.ptrUnion.pkt;

    ingressSample =
        _SHR_RX_REASON_GET(pkt->rx_reasons, bcmRxReasonSampleSource);
    egressSample = _SHR_RX_REASON_GET(pkt->rx_reasons, bcmRxReasonSampleDest);
    src_port = pkt->src_port;
    dest_port = pkt->dest_port;
    vlan = pkt->vlan;
    pkt_data = pkt->pkt_data->data;
    pkt_len = pkt->pkt_data->len;

    sampleOnly =
        ((pkt->rx_reason ^ bcmRxReasonSampleSource ^ bcmRxReasonSampleDest) ==
         0);

  } else {
#ifdef INCLUDE_PKTIO
    bcm_pktio_pkt_t* pkt = bcmPacket.ptrUnion.pktioPkt;
    bcm_pktio_reasons_t reasons;
    uint32 val;
    auto rv = bcm_pktio_pmd_reasons_get(pkt->unit, pkt, &reasons);
    bcmCheckError(rv, "failed get PKTIO RX reason");
    ingressSample = BCM_PKTIO_REASON_GET(
        reasons.rx_reasons, BCMPKT_RX_REASON_CPU_SFLOW_SRC);
    egressSample = BCM_PKTIO_REASON_GET(
        reasons.rx_reasons, BCMPKT_RX_REASON_CPU_SFLOW_DST);

    // Getting src_port, dst_port, vlan is done here and also in RxPacket
    // constructor, which is after slow handling. TBD -- should RxPacket be
    // constructed before sflow handling to avoid duplication? Why is SFLOW
    // handling not part of general RX handling but singled out as a
    // pre-processing step before RxPacket is constructed and processed?

    rv = bcm_pktio_pmd_field_get(
        unit_, pkt, bcmPktioPmdTypeHigig2, BCM_PKTIO_HG2_SRC_PORT, &val);
    bcmCheckError(rv, "failed to get pktio SRC_PORT_NUM");
    src_port = val;

    /* The query of (bcmPktioPmdTypeHigig2, BCM_PKTIO_HG2_DST_PORT_MGIDL)
     * depends on MCST.
     * When MCST=0, it is destination port ID.
     * When MCST=1, it is LSBs of MGID.
     * MCST can be obtained from a query of
     * (bcmPktioPmdTypeHigig2, BCM_PKTIO_HG2_MCST)
     */
    rv = bcm_pktio_pmd_field_get(
        unit_, pkt, bcmPktioPmdTypeHigig2, BCM_PKTIO_HG2_DST_PORT_MGIDL, &val);
    bcmCheckError(rv, "failed to get pktio BCM_PKTIO_HG2_DST_PORT_MGIDL");
    dest_port = val;

    rv = bcm_pktio_pmd_field_get(
        unit_, pkt, bcmPktioPmdTypeRx, BCMPKT_RXPMD_OUTER_VID, &val);
    bcmCheckError(rv, "failed to get pktio OUTER_VID");
    vlan = val;

    rv = bcm_pktio_pkt_data_get(unit_, pkt, (void**)&pkt_data, &pkt_len);
    bcmCheckError(rv, "failed to get pktio packet data");

    BCM_PKTIO_REASON_CLEAR(reasons.rx_reasons, BCMPKT_RX_REASON_CPU_SFLOW_SRC);
    BCM_PKTIO_REASON_CLEAR(reasons.rx_reasons, BCMPKT_RX_REASON_CPU_SFLOW_DST);
    sampleOnly = BCM_PKTIO_REASON_IS_NULL(reasons.rx_reasons);
#endif
  }
  if (!ingressSample && !egressSample) {
    return false;
  }

  // Assemble packet metadata
  SflowPacketInfo info;

  auto currentTime = std::chrono::high_resolution_clock::now();
  auto duration =
      duration_cast<std::chrono::nanoseconds>(currentTime.time_since_epoch());
  *info.timestamp()->seconds() =
      duration_cast<std::chrono::seconds>(duration).count();
  *info.timestamp()->nanoseconds() = duration_cast<std::chrono::nanoseconds>(
                                         duration % std::chrono::seconds(1))
                                         .count();
  *info.ingressSampled() = ingressSample;
  *info.egressSampled() = egressSample;
  info.srcPort() = src_port;
  info.dstPort() = dest_port;
  info.vlan() = vlan;

  auto snapLen = std::min(kMaxSflowSnapLen, (unsigned int)(pkt_len));

  XLOG(DBG6) << "sFlow captured packet of size " << pkt_len;
  XLOG(DBG6) << "Packet dump following (max 128 bytes):\n "
             << folly::hexDump(pkt_data, snapLen);

  {
    std::string packetData(pkt_data, pkt_data + snapLen);
    *info.packetData() = std::move(packetData);
  }

  // Print it for debugging
  XLOG(DBG6) << "sFlowSample: (" << *info.timestamp()->seconds() << ','
             << *info.timestamp()->nanoseconds() << ','
             << *info.ingressSampled() << ',' << *info.egressSampled() << ','
             << *info.srcPort() << ',' << *info.dstPort() << ',' << *info.vlan()
             << ',' << info.packetData()->length() << ")\n";

  sFlowExporterTable_->sendToAll(info);

  // If it is only here because of sFlow, we're done
  if (sampleOnly) {
    return true;
  }

  return false;
}

std::string BcmSwitch::gatherSdkState() const {
  if (!platform_->isBcmShellSupported()) {
    XLOG(DBG2) << "Cannot dump SDK state since the platform does not support "
                  "bcm shell";
    return "";
  }
  BcmLogBuffer logBuffer;
  printDiagCmd("portstat");
  printDiagCmd("l2 show");
  printDiagCmd("l3 l3table show");
  printDiagCmd("l3 ip6host show");
  printDiagCmd("l3 defip show");
  printDiagCmd("l3 ip6route show");
  printDiagCmd("l3 intf show");
  printDiagCmd("l3 egress show");
  printDiagCmd("l3 multipath show");
  printDiagCmd("trunk show");
  return logBuffer.getBuffer()->to<std::string>();
}

bool BcmSwitch::portExists(PortID port) const {
  return getPortTable()->portExists(port);
}

/*
 * BroadView is currently only supported in certain BCM SDK version. We will
 * remove this (and move to Bcm) once all 6.3.x BCM SDK is removed.
 */
std::unique_ptr<PacketTraceInfo> BcmSwitch::getPacketTrace(
    std::unique_ptr<MockRxPacket> pkt) const {
  // Check if the port actually exists.
  if (!getPortTable()->portExists(pkt->getSrcPort())) {
    XLOG(ERR) << "Cannot getPacketTrace from non-existing port";
    return nullptr;
  }

  // Construct the arguments for bcm_switch_pkt_trace_info_get()
  unsigned int options = 0; // Unused now
  auto port = getPortTable()->getBcmPortId(pkt->getSrcPort());
  auto packetLength = pkt->getLength();
  auto packetData = pkt->buf()->writableData();
  bcm_switch_pkt_trace_info_t bcmPacketTraceInfo;
  auto rv = bcm_switch_pkt_trace_info_get(
      unit_, options, port, packetLength, packetData, &bcmPacketTraceInfo);
  bcmCheckError(rv, "Failed to get packet trace info");

  // Parse bcmPacketTraceInfo
  std::unique_ptr<PacketTraceInfo> packetTraceInfo =
      std::make_unique<PacketTraceInfo>();
  transformPacketTraceInfo(
      this,
      bcmPacketTraceInfo, /* bcm_switch_pkt_trace_info_t */
      *packetTraceInfo /* fboss::PacketTraceInfo */
  );
  return packetTraceInfo;
}

static int _addL2Entry(int /*unit*/, bcm_l2_addr_t* l2addr, void* user_data) {
  L2EntryThrift entry;
  auto* cookie =
      static_cast<std::pair<BcmSwitch*, std::vector<L2EntryThrift>*>*>(
          user_data);
  auto [hw, l2Table] = *cookie;
  *entry.mac() = fmt::format(
      "{0:02x}:{1:02x}:{2:02x}:{3:02x}:{4:02x}:{5:02x}",
      l2addr->mac[0],
      l2addr->mac[1],
      l2addr->mac[2],
      l2addr->mac[3],
      l2addr->mac[4],
      l2addr->mac[5]);
  *entry.vlanID() = l2addr->vid;
  *entry.port() = 0;
  if (l2addr->flags & BCM_L2_TRUNK_MEMBER) {
    entry.trunk() = hw->getTrunkTable()->getAggregatePortId(l2addr->tgid);
    XLOG(DBG6) << "L2 entry: Mac:" << *entry.mac() << " Vid:" << *entry.vlanID()
               << " Trunk: " << entry.trunk().value_or({});
  } else {
    *entry.port() = l2addr->port;
    XLOG(DBG6) << "L2 entry: Mac:" << *entry.mac() << " Vid:" << *entry.vlanID()
               << " Port: " << *entry.port();
  }

  *entry.l2EntryType() = (l2addr->flags & BCM_L2_PENDING)
      ? L2EntryType::L2_ENTRY_TYPE_PENDING
      : L2EntryType::L2_ENTRY_TYPE_VALIDATED;

  // If classID is not set, this field is set to 0 by default.
  // Set in L2EntryThrift only if set to non-default
  if (l2addr->group != 0) {
    entry.classID() = l2addr->group;
  }

  l2Table->push_back(entry);
  return 0;
}

void BcmSwitch::fetchL2Table(std::vector<L2EntryThrift>* l2Table, bool /*sdk*/)
    const {
  auto cookie = std::make_pair(this, l2Table);
  int rv = bcm_l2_traverse(unit_, _addL2Entry, &cookie);
  bcmCheckError(rv, "bcm_l2_traverse failed");
}

void BcmSwitch::processChangedAcl(
    const std::shared_ptr<AclEntry>& oldAcl,
    const std::shared_ptr<AclEntry>& newAcl) {
  // Unfortunately, we cannot modify a field entry due to BCM limitation
  XLOG(DBG3) << "processChangedAcl, ACL=" << oldAcl->getID();
  processRemovedAcl(oldAcl);
  processAddedAcl(newAcl);
}

void BcmSwitch::processRemovedAcl(const std::shared_ptr<AclEntry>& acl) {
  XLOG(DBG3) << "processRemovedAcl, ACL=" << acl->getID();
  aclTable_->processRemovedAcl(acl);
}

void BcmSwitch::processAddedAcl(const std::shared_ptr<AclEntry>& acl) {
  XLOG(DBG3) << "processAddedAcl, ACL=" << acl->getID();
  aclTable_->processAddedAcl(platform_->getAsic()->getDefaultACLGroupID(), acl);
}

void BcmSwitch::processChangedTeFlow(
    const std::shared_ptr<TeFlowEntry>& oldTeFlow,
    const std::shared_ptr<TeFlowEntry>& newTeFlow) {
  XLOG(DBG3) << "processChangedTeFlow, oldTeFlow=" << oldTeFlow->str()
             << " newTeFlow=" << newTeFlow->str();
  teFlowTable_->processChangedTeFlow(oldTeFlow, newTeFlow);
}

void BcmSwitch::processRemovedTeFlow(
    const std::shared_ptr<TeFlowEntry>& teFlow) {
  XLOG(DBG3) << "processRemovedTeFlow, TeFlow=" << teFlow->str();
  teFlowTable_->processRemovedTeFlow(teFlow);
}

void BcmSwitch::processAddedTeFlow(const std::shared_ptr<TeFlowEntry>& teFlow) {
  XLOG(DBG3) << "processAddedTeFlow, TeFlow=" << teFlow->str();
  teFlowTable_->processAddedTeFlow(
      platform_->getAsic()->getDefaultTeFlowGroupID(), teFlow);
}

bool BcmSwitch::hasValidAclMatcher(const std::shared_ptr<AclEntry>& acl) const {
  /*
   * Broadcom SDK provides single API to configure lookupClassNeighbor and
   * lookupClassRoute. Thus, if both are specified for an ACL, they must have
   * same value.
   */
  if ((acl->getLookupClassNeighbor() && acl->getLookupClassRoute()) &&
      acl->getLookupClassNeighbor().value() !=
          acl->getLookupClassRoute().value()) {
    return false;
  }
  if (acl->getTcpFlagsMask()) {
    return false;
  }

  return true;
}

void BcmSwitch::forceLinkscanOn(bcm_pbmp_t ports) {
  if (!(flags_ & LINKSCAN_REGISTERED)) {
    XLOG(DBG2) << "Linkscan not registered, skipping force scan";
    return;
  }
  // will immediately scan ports in the passed in port bitmap. Useful
  // for guaranteeing link status is updated during initialization.
  auto rv = bcm_linkscan_update(unit_, ports);
  bcmCheckError(rv, "failed initial scan of link status");
}

bool BcmSwitch::isValidLabelForwardingEntry(const Route<LabelID>* entry) const {
  if (!isValidLabeledNextHopSet(
          platform_, entry->getForwardInfo().getNextHopSet())) {
    return false;
  }

  for (auto client : AllClientIDs()) {
    const auto entryForClient = entry->getEntryForClient(client);
    if (!entryForClient) {
      continue;
    }
    if (!FLAGS_mpls_rib &&
        !isValidLabeledNextHopSet(platform_, entryForClient->getNextHopSet())) {
      return false;
    }
  }
  return true;
}

void BcmSwitch::processControlPlaneEntryAdded(
    const std::shared_ptr<ControlPlane>& newCPU) {
  processControlPlaneEntryChanged(std::make_shared<ControlPlane>(), newCPU);
}

void BcmSwitch::processControlPlaneEntryRemoved(
    const std::shared_ptr<ControlPlane>& oldCPU) {
  processControlPlaneEntryChanged(oldCPU, std::make_shared<ControlPlane>());
}

void BcmSwitch::processControlPlaneEntryChanged(
    const std::shared_ptr<ControlPlane>& oldCPU,
    const std::shared_ptr<ControlPlane>& newCPU) {
  processChangedControlPlaneQueues(oldCPU, newCPU);
  if (oldCPU->getQosPolicy() != newCPU->getQosPolicy()) {
    if (const auto& policy = newCPU->getQosPolicy()) {
      controlPlane_->setupIngressQosPolicy(policy->cref());
    } else {
      controlPlane_->setupIngressQosPolicy(std::nullopt);
    }
  }
  processChangedRxReasonToQueueEntries(oldCPU, newCPU);
}

void BcmSwitch::processControlPlaneChanges(const StateDelta& delta) {
  const auto controlPlaneDelta = delta.getControlPlaneDelta();
  forEachAdded(
      controlPlaneDelta, &BcmSwitch::processControlPlaneEntryAdded, this);
  forEachChanged(
      controlPlaneDelta, &BcmSwitch::processControlPlaneEntryChanged, this);
  forEachRemoved(
      controlPlaneDelta, &BcmSwitch::processControlPlaneEntryRemoved, this);

  // COPP ACL will be handled just as regular ACL in processAclChanges()
}

void BcmSwitch::disableHotSwap() const {
  if (getPlatform()->isDisableHotSwapSupported()) {
    switch (getPlatform()->getAsic()->getAsicType()) {
      case cfg::AsicType::ASIC_TYPE_FAKE:
      case cfg::AsicType::ASIC_TYPE_FAKE_NO_WARMBOOT:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK:
        // For TH, we patch the SDK to disable hot swap.
        break;
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1: {
        auto rv = bcm_switch_control_set(unit_, bcmSwitchPcieHotSwapDisable, 1);
        bcmCheckError(rv, "Failed to disable hotswap");
      } break;
      case cfg::AsicType::ASIC_TYPE_EBRO:
      case cfg::AsicType::ASIC_TYPE_TRIDENT2:
      case cfg::AsicType::ASIC_TYPE_P200:
      case cfg::AsicType::ASIC_TYPE_GARONNE:
      case cfg::AsicType::ASIC_TYPE_YUBA:
      case cfg::AsicType::ASIC_TYPE_CHENAB:
      case cfg::AsicType::ASIC_TYPE_CHENAB2:
      case cfg::AsicType::ASIC_TYPE_MOCK:
      case cfg::AsicType::ASIC_TYPE_ELBERT_8DD:
      case cfg::AsicType::ASIC_TYPE_AGERA3:
      case cfg::AsicType::ASIC_TYPE_SANDIA_PHY:
      case cfg::AsicType::ASIC_TYPE_JERICHO2:
      case cfg::AsicType::ASIC_TYPE_JERICHO3:
      case cfg::AsicType::ASIC_TYPE_JERICHO4:
      case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
      case cfg::AsicType::ASIC_TYPE_RAMON:
      case cfg::AsicType::ASIC_TYPE_RAMON3:
      case cfg::AsicType::ASIC_TYPE_G202X:
        CHECK(0) << " Invalid ASIC type";
    }
  }
}

bool BcmSwitch::isL2EntryPending(const bcm_l2_addr_t* l2Addr) {
  return l2Addr->flags & BCM_L2_PENDING;
}

bool BcmSwitch::isValidPortQueueUpdate(
    const std::vector<std::shared_ptr<PortQueue>>& /*oldPortQueueConfig*/,
    const std::vector<std::shared_ptr<PortQueue>>& newPortQueueConfig) const {
  if (newPortQueueConfig.empty()) {
    return true;
  }

  std::map<TrafficClass, int> tcToQueue;
  // validate that a traffic class has only one associated queue
  for (const auto& portQueue : newPortQueueConfig) {
    auto trafficClass = portQueue->getTrafficClass()
        ? portQueue->getTrafficClass().value()
        : static_cast<TrafficClass>(portQueue->getID());
    if (trafficClass > BCM_PRIO_MAX || trafficClass < BCM_PRIO_MIN) {
      return false;
    }
    if (tcToQueue.find(trafficClass) != tcToQueue.end()) {
      // this traffic class has already been mapped to some queue
      return false;
    }
    tcToQueue.emplace(trafficClass, portQueue->getID());

    // validate pfc priority if it exists on the queue
    if (const auto pfcPrioritySet = portQueue->getPfcPrioritySet()) {
      // anything more than 7 is illegal (3 bits)
      for (const auto& pfcPri : *pfcPrioritySet) {
        if (pfcPri > cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX()) {
          return false;
        }
      }
    }
  }
  return true;
}

bool BcmSwitch::isValidPortQosPolicyUpdate(
    const std::shared_ptr<Port>& /*oldPort*/,
    const std::shared_ptr<Port>& newPort,
    const std::shared_ptr<SwitchState>& newState) const {
  auto portQosPolicyName = newPort->getQosPolicy();
  if (!portQosPolicyName.has_value() ||
      portQosPolicyName.value() ==
          newState->getDefaultDataPlaneQosPolicy()->getName()) {
    // either no policy or global default poliicy is provided
    return true;
  }
  auto qosPolicy =
      newState->getQosPolicies()->getNodeIf(portQosPolicyName.value());
  // qos policy for port must not have exp, since this is not supported
  return qosPolicy->getExpMap()->cref<switch_state_tags::to>()->empty() &&
      qosPolicy->getExpMap()->cref<switch_state_tags::from>()->empty();
}

std::string BcmSwitch::listObjects(
    const std::vector<HwObjectType>& /*types*/,
    bool /*cached*/) const {
  throw FbossError("Listing hw objects not supported. Use Bcm shell");
}

bool BcmSwitch::useHSDK() const {
  return BcmAPI::isHwUsingHSDK();
}

bool BcmSwitch::usePKTIO() const {
  return unitObject_->usePKTIO();
}

uint32_t BcmSwitch::generateDeterministicSeed(
    LoadBalancerID loadBalancerID,
    folly::MacAddress platformMac) const {
  // To avoid changing the seed across graceful restarts, the seed is
  // generated deterministically using the local MAC address.
  return utility::generateDeterministicSeed(loadBalancerID, platformMac, false);
}

void BcmSwitch::initialStateApplied() {
  if (bootType_ != BootType::WARM_BOOT) {
    return;
  }
  hostTable_->warmBootHostEntriesSynced();
  // Done with warm boot, clear warm boot cache
  warmBootCache_->clear();
}

void BcmSwitch::syncLinkStates() {
  linkScanBottomHalfEventBase_.runInFbossEventBaseThread([this]() {
    for (auto& port : std::as_const(*portTable_)) {
      callback_->linkStateChanged(
          port.first, port.second->isUp(), cfg::PortType::INTERFACE_PORT);
    }
  });
}

void BcmSwitch::syncPortLinkState(PortID /*port*/) {}

CpuPortStats BcmSwitch::getCpuPortStats() const {
  CpuPortStats cpuPortStats;
  auto queueManager = getControlPlane()->getQueueManager();
  cpuPortStats.queueInPackets_() =
      queueManager->getQueueStats(BcmCosQueueStatType::OUT_PACKETS);
  cpuPortStats.queueDiscardPackets_() =
      queueManager->getQueueStats(BcmCosQueueStatType::DROPPED_PACKETS);
  HwPortStats portStats;
  getControlPlane()->updateQueueCounters(&portStats);
  cpuPortStats.portStats_() = portStats;
  return cpuPortStats;
}

AclStats BcmSwitch::getAclStats() const {
  return bcmStatUpdater_->getAclStats();
}

std::shared_ptr<SwitchState> BcmSwitch::reconstructSwitchState() const {
  throw FbossError("reconstructSwitchState not implemented for BCM");
}

HwResourceStats BcmSwitch::getResourceStats() const {
  return bcmStatUpdater_->getHwTableStats();
}

HwSwitchCounterStats BcmSwitch::getHwSwitchCounterStats() const {
  return getStatUpdater()->getHwSwitchCounterStats();
}

HwHighFrequencyStats BcmSwitch::zeroHighFrequencyStatsTimestamp(
    const HwHighFrequencyStats& stats) {
  HwHighFrequencyStats zeroed = stats;
  zeroed.timestampUs() = 0;
  return zeroed;
}

// Tests if two high frequency stats are different from each other.
bool BcmSwitch::hasHighFrequencyStatsChanged(
    const HwHighFrequencyStats& statsA,
    const HwHighFrequencyStats& statsB) {
  return zeroHighFrequencyStatsTimestamp(statsA) !=
      zeroHighFrequencyStatsTimestamp(statsB);
}

bool BcmSwitch::hasNonZeroWatermark(const HwHighFrequencyStats& stats) {
  auto isNonZero = [](int64_t value) { return value != 0; };
  return std::ranges::any_of(
             stats.itmPoolSharedWatermarkBytes().value() | std::views::values,
             isNonZero) ||
      std::ranges::any_of(
             stats.portStats().value() | std::views::values,
             [&](const HwHighFrequencyPortStats& portStats) {
               return std::ranges::any_of(
                          portStats.queueWatermarkBytes().value() |
                              std::views::values,
                          isNonZero) ||
                   std::ranges::any_of(
                          portStats.pgSharedWatermarkBytes().value() |
                              std::views::values,
                          isNonZero);
             });
}

void BcmSwitch::collectHighFrequencyStats() {
  auto endTime = std::chrono::steady_clock::now() +
      std::chrono::microseconds(highFreqStatsThreadConfig_.schedulerConfig()
                                    ->statsCollectionDurationInMicroseconds()
                                    .value());
  // Guard the high frequency stats data size with a hard limit of
  // kHighFreqStatsDataMaxSize_.
  int maxDataSize =
      std::min(FLAGS_high_freq_stats_data_size, kHighFreqStatsDataMaxSize_);
  {
    // Perform a bulk delete at the front of the deque if the size is too large.
    auto wlock = highFreqStatsData_.wlock();
    if (wlock->size() > maxDataSize) {
      wlock->erase(
          wlock->begin(), wlock->begin() + (wlock->size() - maxDataSize));
    }
  }
  int numConsecutiveHighFreqStatsCollect = std::min(
      FLAGS_num_consecutive_high_freq_stats_collect,
      kMaxConsecutiveHighFreqStatsCollect_);
  while (std::chrono::steady_clock::now() < endTime) {
    for (int i = 0; i < numConsecutiveHighFreqStatsCollect; ++i) {
      HwHighFrequencyStats stats = getHighFrequencyStats();
      {
        auto wlock = highFreqStatsData_.wlock();
        if (wlock->empty() ||
            hasHighFrequencyStatsChanged(wlock->back(), stats) ||
            hasNonZeroWatermark(stats)) {
          while (wlock->size() >= maxDataSize) {
            wlock->pop_front();
          }
          wlock->emplace_back(std::move(stats));
        }
      }
    }
    if (std::chrono::steady_clock::now() >= endTime) {
      break;
    }
    std::this_thread::sleep_for(
        std::chrono::microseconds(highFreqStatsThreadConfig_.schedulerConfig()
                                      ->statsWaitDurationInMicroseconds()
                                      .value()));
  }
}

void BcmSwitch::updateHighFrequencyStatsThreadConfig(
    const HighFrequencyStatsCollectionConfig& config) {
  highFreqStatsThreadConfig_ = config;
  auto schedulerConfig = highFreqStatsThreadConfig_.schedulerConfig();
  if (schedulerConfig->statsWaitDurationInMicroseconds().value() <
      kHfMinWaitDurationUs_) {
    XLOG(WARN) << "statsWaitDurationInMicroseconds is too small, updating to "
               << kHfMinWaitDurationUs_ << " us";
    schedulerConfig->statsWaitDurationInMicroseconds() = kHfMinWaitDurationUs_;
  }
  if (schedulerConfig->statsCollectionDurationInMicroseconds().value() >
      kHfMaxCollectionDurationUs_) {
    XLOG(WARN)
        << "statsCollectionDurationInMicroseconds is too large, updating to "
        << kHfMaxCollectionDurationUs_ << " us";
    schedulerConfig->statsCollectionDurationInMicroseconds() =
        kHfMaxCollectionDurationUs_;
  }
}

void BcmSwitch::startHighFrequencyStatsThread(
    const HighFrequencyStatsCollectionConfig& config) {
  std::lock_guard<std::mutex> lock(lock_);
  updateHighFrequencyStatsThreadConfig(config);
  highFreqStatsThread_->cancelFunctionAndWait(kHighFreqStatsFunctionName_);
  // Use addFunctionOnce instead of using periodic function scheduling to allow
  // for the thread to terminate after collection duration.
  highFreqStatsThread_->addFunctionOnce(
      [&]() { collectHighFrequencyStats(); }, kHighFreqStatsFunctionName_);
  highFreqStatsThread_->start();
}

void BcmSwitch::stopHighFrequencyStatsThread() {
  std::lock_guard<std::mutex> lock(lock_);
  highFreqStatsThread_->shutdown();
}

void BcmSwitch::getHighFrequencyTimeseriesStats(
    std::vector<HwHighFrequencyStats>& stats,
    const std::unique_ptr<GetHighFrequencyStatsOptions>& options) const {
  {
    auto rlock = highFreqStatsData_.rlock();
    auto begin = rlock->begin();
    auto end = rlock->end();
    if (options->beginTimestampUs().has_value()) {
      begin = std::lower_bound(
          begin,
          end,
          options->beginTimestampUs().value(),
          [](const HwHighFrequencyStats& stats, int64_t timestamp) {
            return stats.timestampUs().value() < timestamp;
          });
    }
    if (options->endTimestampUs().has_value()) {
      end = std::upper_bound(
          begin,
          end,
          options->endTimestampUs().value(),
          [](int64_t timestamp, const HwHighFrequencyStats& stats) {
            return timestamp <= stats.timestampUs().value();
          });
    }
    int64_t length{std::min(
        std::distance(begin, end), std::max(options->limit().value(), 0l))};
    if (options->beginTimestampUs().has_value()) {
      stats = std::vector<HwHighFrequencyStats>(begin, begin + length);
    } else {
      stats = std::vector<HwHighFrequencyStats>(end - length, end);
    }
  }
}

} // namespace facebook::fboss
