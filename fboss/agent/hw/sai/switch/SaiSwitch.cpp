/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiSwitch.h"

#include "fboss/agent/Constants.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/LockPolicy.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/ValidateStateUpdate.h"
#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/HwPortFb303Stats.h"
#include "fboss/agent/hw/HwSysPortFb303Stats.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_constants.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/sai/api/AclApi.h"
#include "fboss/agent/hw/sai/api/AdapterKeySerializers.h"
#include "fboss/agent/hw/sai/api/BridgeApi.h"
#include "fboss/agent/hw/sai/api/FdbApi.h"
#include "fboss/agent/hw/sai/api/HostifApi.h"
#include "fboss/agent/hw/sai/api/LoggingUtil.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"
#include "fboss/agent/hw/sai/switch/SaiArsManager.h"
#include "fboss/agent/hw/sai/switch/SaiArsProfileManager.h"
#include "fboss/agent/hw/sai/switch/SaiBufferManager.h"
#include "fboss/agent/hw/sai/switch/SaiCounterManager.h"
#include "fboss/agent/hw/sai/switch/SaiDebugCounterManager.h"
#include "fboss/agent/hw/sai/switch/SaiFirmwareManager.h"
#include "fboss/agent/hw/sai/switch/SaiHashManager.h"
#include "fboss/agent/hw/sai/switch/SaiHostifManager.h"
#include "fboss/agent/hw/sai/switch/SaiInSegEntryManager.h"
#include "fboss/agent/hw/sai/switch/SaiLagManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiMirrorManager.h"
#include "fboss/agent/hw/sai/switch/SaiMyMacManager.h"
#include "fboss/agent/hw/sai/switch/SaiNeighborManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortUtils.h"
#include "fboss/agent/hw/sai/switch/SaiRouteManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiRxPacket.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6MySidManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6TunnelManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiSystemPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiTamManager.h"
#include "fboss/agent/hw/sai/switch/SaiTunnelManager.h"
#include "fboss/agent/hw/sai/switch/SaiTxPacket.h"
#include "fboss/agent/hw/sai/switch/SaiUdfManager.h"
#include "fboss/agent/hw/sai/switch/SaiVendorSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiVlanManager.h"
#include "fboss/agent/hw/sai/switch/SaiVxlanTunnelManager.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/lib/HwWriteBehavior.h"
#include "fboss/lib/phy/CredoSdkVersion.h"

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/FabricConnectivityManager.h"
#include "fboss/agent/hw/HwSwitchWarmBootHelper.h"
#include "fboss/agent/hw/UnsupportedFeatureManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "folly/MacAddress.h"

#include "fboss/agent/LoadBalancerUtils.h"

#include "fboss/lib/phy/PhyUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

#include <folly/logging/xlog.h>

#include <boost/range/combine.hpp>
#include <chrono>
#include <optional>

extern "C" {
#include <sai.h>

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
#include <experimental/saiexperimentalfirmware.h>
#include <experimental/saiexperimentaltameventaginggroup.h>
#include <saiextensions.h>
#endif
}

using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::system_clock;

/*
 * Setting the default sai sdk logging level to CRITICAL for several reasons:
 * 1) These are synchronous writes to the syslog so that agent
 * is not blocked by this.
 * 2) Route, Port and Neighbor change frequently and so to limit the writes to
 * the syslog .
 * 3) Agent might miss some of the control packets (eg: LACP heartbeats)
 * which can cause port-channel flaps
 */
DEFINE_string(
    enable_sai_log,
    "CRITICAL",
    "Turn on SAI SDK logging. Options are DEBUG|INFO|NOTICE|WARN|ERROR|CRITICAL");

DECLARE_bool(enable_acl_table_group);
DECLARE_bool(srv6);

DEFINE_bool(
    force_recreate_acl_tables,
    false,
    "force recreate acl tables during warmboot.");

DEFINE_bool(
    check_tagged_tx,
    false,
    "Fail if untagged packet is transmitted on platform where tagged packet is required");

DEFINE_int32(
    serdes_params_poll_interval_s,
    360,
    "Interval for reading serdes stats");

namespace {
constexpr auto kExtOperStatusLatchWarmBootCaptureDelayMs = 1000;
/*
 * For the devices/SDK we use, the only events we should get (and process)
 * are LEARN and AGED.
 * Learning generates LEARN, aging generates AGED, while Mac move results in
 * DELETE followed by ADD.
 * We ASSERT this explicitly via HW tests.
 */
const std::map<sai_fdb_event_t, facebook::fboss::L2EntryUpdateType>
    kL2AddrUpdateOperationsOfInterest = {
        {SAI_FDB_EVENT_LEARNED,
         facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD},
        {SAI_FDB_EVENT_AGED,
         facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE},
};

std::string fdbEventToString(sai_fdb_event_t event) {
  switch (event) {
    case SAI_FDB_EVENT_LEARNED:
      return "learned";
    case SAI_FDB_EVENT_AGED:
      return "aged";
    case SAI_FDB_EVENT_MOVE:
      return "moved";
    case SAI_FDB_EVENT_FLUSHED:
      return "flushed";
  }
  return "unknown";
}

static std::set<facebook::fboss::cfg::PacketRxReason> kAllowedRxReasons = {
    facebook::fboss::cfg::PacketRxReason::TTL_1};

} // namespace

namespace facebook::fboss {

namespace {

std::shared_ptr<const AclTableMap> getAclTablesForBindPoint(
    const std::shared_ptr<SwitchState>& state,
    cfg::AclStage stage,
    cfg::AclTableGroupBindPoint bindPoint) {
  const auto& aclTableGroups = bindPoint == cfg::AclTableGroupBindPoint::PORT
      ? state->getPortAclTableGroups()
      : state->getAclTableGroups();
  if (auto aclTableGroup = aclTableGroups->getNodeIf(stage)) {
    return aclTableGroup->getAclTableMap();
  }
  return nullptr;
}

} // namespace

// We need this global SaiSwitch* to support registering SAI callbacks
// which can then use SaiSwitch to do their work. The current callback
// facility in SAI does not support passing user data to come back
// with the callback.
// N.B., if we want to have multiple SaiSwitches in a device with multiple
// cards being managed by one instance of FBOSS, this will need to be
// extended, presumably into an array keyed by switch id.
static folly::ConcurrentHashMap<sai_object_id_t, SaiSwitch*> __gSaiIdToSwitch;

// Free functions to register as callbacks
void __gPacketRxCallback(
    sai_object_id_t switch_id,
    sai_size_t buffer_size,
    const void* buffer,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  __gSaiIdToSwitch[switch_id]->packetRxCallback(
      SwitchSaiId{switch_id}, buffer_size, buffer, attr_count, attr_list);
}

void __gFdbEventCallback(
    uint32_t count,
    const sai_fdb_event_notification_data_t* data) {
  if (!count) {
    return;
  }
  auto switchId = data[0].fdb_entry.switch_id;
  __gSaiIdToSwitch[switchId]->fdbEventCallback(count, data);
}
/*
 * Link, parity and TAM event notifications don't have switch id information
 * in the callback. So in that sense these are not compatible with a 1 sai
 * adapter handling multiple ASICs/SaiSwitches scenario. Luckily for our use
 * case multi chip scenario happens only in phys, where we do not setup
 * call backs. If we need to expand this to a multi switch ASIC scenario,
 * the following callbacks will need a SAI spec enhancement.
 */
void __glinkStateChangedNotification(
    uint32_t count,
    const sai_port_oper_status_notification_t* data) {
  __gSaiIdToSwitch.begin()->second->linkStateChangedCallbackTopHalf(
      count, data);
}

void __gSwitchEventCallback(
    sai_size_t buffer_size,
    const void* buffer,
    uint32_t event_type) {
  __gSaiIdToSwitch.begin()->second->switchEventCallback(
      buffer_size, buffer, event_type);
}

void __gTamEventCallback(
    sai_object_id_t tam_event_id,
    sai_size_t buffer_size,
    const void* buffer,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  __gSaiIdToSwitch.begin()->second->tamEventCallback(
      tam_event_id, buffer_size, buffer, attr_count, attr_list);
}

void _gPfcDeadlockNotificationCallback(
    uint32_t count,
    const sai_queue_deadlock_notification_data_t* data) {
  __gSaiIdToSwitch.begin()->second->pfcDeadlockNotificationCallbackTopHalf(
      count, data);
}

void __gVendorSwitchEventNotificationCallback(
    sai_size_t buffer_size,
    const void* buffer,
    uint32_t event_type) {
  __gSaiIdToSwitch.begin()->second->vendorSwitchEventNotificationCallback(
      buffer_size, buffer, event_type);
}

void __gHardResetNotificationallback(
    sai_object_id_t /*switch_id*/,
    sai_size_t buffer_size,
    const void* buffer) {
  __gSaiIdToSwitch.begin()->second->hardResetSwitchEventNotificationCallback(
      buffer_size, buffer);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
void __gTxReadyStatusChangeNotification(
    sai_object_id_t switch_id,
    sai_object_id_t port_id,
    sai_port_host_tx_ready_status_t /*host_tx_ready_status*/) {
  // Today, SAI implementation always returns NULL portID in this callback.
  // This is OK, as FBOSS queries which links are active vs. inactive in the
  // callback processing.
  if (port_id != SAI_NULL_OBJECT_ID) {
    XLOG(ERR)
        << "SaiSwitch " << static_cast<long>(switch_id)
        << " received TX Ready Status changed callback with non-NULL portID: "
        << static_cast<long>(port_id);
  }

  __gSaiIdToSwitch.begin()->second->txReadyStatusChangeCallbackTopHalf(
      SwitchSaiId{switch_id});
}

void __gSwitchAsicSdkHealthNotificationCallBack(
    sai_object_id_t switch_id,
    sai_switch_asic_sdk_health_severity_t severity,
    sai_timespec_t timestamp,
    sai_switch_asic_sdk_health_category_t category,
    sai_switch_health_data_t data,
    const sai_u8_list_t description) {
  auto switchIter = __gSaiIdToSwitch.find(switch_id);
  CHECK(switchIter != __gSaiIdToSwitch.end());
  SaiHealthNotification notification(
      fromSaiTimeSpec(timestamp), severity, category, data, description);
  switchIter->second->switchAsicSdkHealthNotificationTopHalf(
      std::move(notification));
  ;
}
#endif

PortSaiId SaiSwitch::getCPUPortSaiId() const {
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::CPU_PORT)) {
    throw FbossError("CPU port not supported on this platform");
  }
  return managerTable_->switchManager().getCpuPort();
}

SaiSwitch::SaiSwitch(SaiPlatform* platform, uint32_t featuresDesired)
    : HwSwitch(featuresDesired),
      platform_(platform),
      fabricConnectivityManager_(
          std::make_unique<FabricConnectivityManager>()) {
  utilCreateDir(platform_->getDirectoryUtil()->getVolatileStateDir());
  utilCreateDir(platform_->getDirectoryUtil()->getPersistentStateDir());
}

SaiSwitch::~SaiSwitch() {}

HwInitResult SaiSwitch::initImpl(
    Callback* callback,
    BootType bootType,
    bool failHwCallsOnWarmboot) noexcept {
  asicType_ = platform_->getAsic()->getAsicType();
  bootType_ = bootType;
  auto behavior{HwWriteBehavior::WRITE};
  if (bootType_ == BootType::WARM_BOOT && failHwCallsOnWarmboot &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::ZERO_SDK_WRITE_WARMBOOT)) {
    behavior = HwWriteBehavior::FAIL;
  }
  // launch threads to handle SDK callbacks and/or events note that this does
  // not register or start processing SDK events immediately his only starts
  // threads and registering callbacks will actually begin events or callbacks
  // entering into FBOSS. this is being separated from registering callbacks as
  // during rollback, SDK callbacks need to be unregistered and registered
  // without stopping and starting threads.
  startThreads();
  HwInitResult ret;
  {
    std::lock_guard<std::mutex> lock(saiSwitchMutex_);
    ret = initLocked(lock, behavior, callback, getSwitchType(), getSwitchId());
  }

  {
    HwWriteBehaviorRAII writeBehavior{behavior};
    if (bootType_ != BootType::WARM_BOOT) {
      std::vector<StateDelta> deltas;
      deltas.emplace_back(std::make_shared<SwitchState>(), ret.switchState);
      stateChangedImpl(deltas, std::nullopt);
    }
  }
  return ret;
}

void SaiSwitch::startThreads() {
  if (getFeaturesDesired() & FeaturesDesired::LINKSCAN_DESIRED) {
    linkStateBottomHalfThread_ = std::make_unique<std::thread>([this]() {
      initThread("fbossSaiLnkScnBH");
      linkStateBottomHalfEventBase_.loopForever();
    });
  }

  fdbEventBottomHalfThread_ = std::make_unique<std::thread>([this]() {
    initThread("fbossSaiFdbBH");
    fdbEventBottomHalfEventBase_.loopForever();
  });

  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::LINK_ACTIVE_INACTIVE_NOTIFY)) {
    txReadyStatusChangeBottomHalfThread_ =
        std::make_unique<std::thread>([this]() {
          initThread("fbossSaiTxReadyStatusChangeStatusBH");
          txReadyStatusChangeBottomHalfEventBase_.loopForever();
        });
  }

  if (platform_->getAsic()->isSupported(HwAsic::Feature::FABRIC_PORTS)) {
    linkConnectivityChangeBottomHalfThread_ =
        std::make_unique<std::thread>([this]() {
          initThread("fbossLnkCnctBH");
          linkConnectivityChangeBottomHalfEventBase_.loopForever();
        });
  }

  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SWITCH_REACHABILITY_CHANGE_NOTIFY)) {
    switchReachabilityChangeProcessThread_ =
        std::make_unique<std::thread>([this]() {
          initThread("fbossSaiSwitchReachabilityChangeBH");
          switchReachabilityChangeProcessEventBase_.loopForever();
        });
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SWITCH_ASIC_SDK_HEALTH_NOTIFY)) {
    switchAsicSdkHealthNotificationBHThread_ =
        std::make_unique<std::thread>([this]() {
          initThread("fbossSaiSwitchAsicSdkHealthNotification");
          switchAsicSdkHealthNotificationBHEventBase_.loopForever();
        });
  }
#endif

  pfcDeadlockNotificationBottomHalfThread_ =
      std::make_unique<std::thread>([this]() {
        initThread("fbossPfcDeadlockNotificationBottomHalfEventBase");
        pfcDeadlockNotificationBottomHalfEventBase_.loopForever();
      });
}

void SaiSwitch::printDiagCmd(const std::string& /*cmd*/) const {
  // TODO: Needs to be implemented.
}

void SaiSwitch::unregisterCallbacks() noexcept {
  // after unregistering there could still be a single packet in our
  // pipeline. To fully shut down rx, we need to stop the thread and
  // let the possible last packet get processed. Since processing a
  // packet takes the saiSwitchMutex_, before calling join() on the thread
  // we need to release the lock.
  {
    std::lock_guard<std::mutex> lock(saiSwitchMutex_);
    unregisterCallbacksLocked(lock);
  }
  stopThreads();
}

void SaiSwitch::stopThreads() {
  // linkscan is turned off and the evb loop is set to break
  // just need to block until the last event is processed
  if (linkStateBottomHalfThread_) {
    linkStateBottomHalfEventBase_.runInFbossEventBaseThreadAndWait(
        [this]() { linkStateBottomHalfEventBase_.terminateLoopSoon(); });
    linkStateBottomHalfThread_->join();
    // link scan is completely shut-off
    linkStateBottomHalfThread_.reset();
  }

  // tx ready status change is turned off and the evb loop is set to break
  // just need to block until the last event is processed
  if (txReadyStatusChangeBottomHalfThread_) {
    txReadyStatusChangeBottomHalfEventBase_.runInFbossEventBaseThreadAndWait(
        [this]() {
          txReadyStatusChangeBottomHalfEventBase_.terminateLoopSoon();
        });
    txReadyStatusChangeBottomHalfThread_->join();
    // tx ready status change processing is completely shut-off
    txReadyStatusChangeBottomHalfThread_.reset();
  }
  if (linkConnectivityChangeBottomHalfThread_) {
    linkConnectivityChangeBottomHalfEventBase_.runInFbossEventBaseThreadAndWait(
        [this]() {
          linkConnectivityChangeBottomHalfEventBase_.terminateLoopSoon();
        });
    linkConnectivityChangeBottomHalfThread_->join();
    // link connectivity change processing is completely shut-off
    linkConnectivityChangeBottomHalfThread_.reset();
  }
  if (switchReachabilityChangeProcessThread_) {
    switchReachabilityChangeProcessEventBase_.runInFbossEventBaseThreadAndWait(
        [this]() {
          switchReachabilityChangeProcessEventBase_.terminateLoopSoon();
        });
    switchReachabilityChangeProcessThread_->join();
    // switch reachability change processing is completely shut-off
    switchReachabilityChangeProcessThread_.reset();
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  if (switchAsicSdkHealthNotificationBHThread_) {
    switchAsicSdkHealthNotificationBHEventBase_
        .runInFbossEventBaseThreadAndWait([this]() {
          switchAsicSdkHealthNotificationBHEventBase_.terminateLoopSoon();
        });
    switchAsicSdkHealthNotificationBHThread_->join();
    switchAsicSdkHealthNotificationBHThread_.reset();
  }
#endif

  if (fdbEventBottomHalfThread_) {
    fdbEventBottomHalfEventBase_.runInFbossEventBaseThreadAndWait(
        [this]() { fdbEventBottomHalfEventBase_.terminateLoopSoon(); });
    fdbEventBottomHalfThread_->join();
    fdbEventBottomHalfThread_.reset();
  }

  if (pfcDeadlockNotificationBottomHalfThread_) {
    pfcDeadlockNotificationBottomHalfEventBase_
        .runInFbossEventBaseThreadAndWait([this]() {
          pfcDeadlockNotificationBottomHalfEventBase_.terminateLoopSoon();
        });
    pfcDeadlockNotificationBottomHalfThread_->join();
    pfcDeadlockNotificationBottomHalfThread_.reset();
  }
}

template <typename LockPolicyT>
void SaiSwitch::processLocalCapsuleSwitchIdsDelta(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  if (FLAGS_dual_stage_edsw_3q_2q) {
    /*
     * We run dual stage EDSWs in a de-facto single pipe mode. This
     * is done for 2-reasons,
     * 1. There is barely any east<->west (EDSW<->EDSW) traffic. So the
     * default 2-stage buffer partitioning is inefficient. The default
     * partitioning splits buffers into 2 to handle line rate traffic
     * on both pipes, while we only need line rate on one pipe.
     * 2. It allows for a longer cable len for EDSW<->FDSW segment,
     * since we can size the fabric buffer on EDSW to absorb more in-flight
     * traffic, while the EDSWs send flow control to FDSWs upon experiencing
     * congestion.
     */
    return;
  }
  SwitchID mySwitchId =
      static_cast<SwitchID>(platform_->getAsic()->getSwitchId().value());
  auto dsfNodesDelta = delta.getDsfNodesDelta();
  if (dsfNodesDelta.begin() == dsfNodesDelta.end() ||
      !delta.newState()->getClusterId(mySwitchId)) {
    return;
  }

  auto myDsfNode = delta.newState()->getDsfNodes()->getNodeIf(mySwitchId);
  CHECK(myDsfNode);
  /*
   * To avoid LLFC interrupts on EDSW side, we skip setting local capsule
   * for FDSWs in the BEEP layer.
   */
  auto fabricLevel = myDsfNode->getFabricLevel();
  if (fabricLevel && fabricLevel.value() == 1 &&
      myDsfNode->getClusterId() >= k2StageEdgePodClusterId) {
    return;
  }

  std::vector<SwitchID> newVal;
  CHECK(platform_->getAsic()->getSwitchId());
  if (dsfNodesDelta.getNew() &&
      dsfNodesDelta.getNew()->begin() != dsfNodesDelta.getNew()->end()) {
    newVal = delta.newState()->getIntraClusterSwitchIds(mySwitchId);
  }
  std::map<SwitchID, int> switchIdToNumCores;
  for (auto switchId : newVal) {
    auto dsfNode = delta.newState()->getDsfNodes()->getNodeIf(switchId);
    CHECK(dsfNode);
    const auto& hwAsic = getHwAsicForAsicType(dsfNode->getAsicType());
    switchIdToNumCores[switchId] = hwAsic.getNumCores();
  }
  managerTable_->switchManager().setLocalCapsuleSwitchIds(switchIdToNumCores);
}

template <typename LockPolicyT>
void SaiSwitch::processCreditRequestProfileDelta(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  auto dsfNodesDelta = delta.getDsfNodesDelta();
  if (getSwitchType() != cfg::SwitchType::VOQ ||
      dsfNodesDelta.begin() == dsfNodesDelta.end() || !dsfNodesDelta.getNew()) {
    return;
  }

  std::map<int32_t, int32_t> moduleIdToCreditRequestProfileParam;
  cfg::QueueScheduling expectedScheduling = cfg::QueueScheduling::INTERNAL;
  [[maybe_unused]] const auto& lock = lockPolicy.lock();
  for (const auto& [_, dsfNodes] : std::as_const(*dsfNodesDelta.getNew())) {
    for (const auto& [switchId, node] : std::as_const(*dsfNodes)) {
      auto scheduling = node->getScheduling();
      auto param = node->getSchedulingParam();
      int paramVal = 0;
      if (scheduling == cfg::QueueScheduling::STRICT_PRIORITY) {
        expectedScheduling = scheduling;
        paramVal = static_cast<int>(param.value().spPriority().value());
      } else if (
          scheduling == cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN ||
          scheduling == cfg::QueueScheduling::DEFICIT_ROUND_ROBIN) {
        expectedScheduling = scheduling;
        paramVal = param.value().wrrWeight().value();
      }
      if (scheduling != cfg::QueueScheduling::INTERNAL) {
        XLOG(DBG2) << "set credit request scheduling parameter of "
                   << node->getName() << " to " << paramVal;
        const auto& hwAsic = getHwAsicForAsicType(node->getAsicType());
        int numCores = hwAsic.getNumCores();
        for (auto core = switchId; core < switchId + numCores; core++) {
          moduleIdToCreditRequestProfileParam[core] = paramVal;
        }
      }
    }
  }
  XLOG(DBG2) << "set credit request profile scheduler "
             << apache::thrift::util::enumNameSafe(expectedScheduling);
  managerTable_->switchManager().setCreditRequestProfileSchedulerMode(
      expectedScheduling);
  managerTable_->switchManager().setModuleIdToCreditRequestProfileParam(
      moduleIdToCreditRequestProfileParam);
}

template <typename LockPolicyT>
void SaiSwitch::processDefaultDataPlanePolicyDelta(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  // process default qos policy, which is store in
  // SwitchState::switchSettingsMap::defaultDataPlaneQosPolicy
  auto qosDelta = delta.getDefaultDataPlaneQosPolicyDelta();
  auto& qosMapManager = managerTable_->qosMapManager();
  auto& switchManager = managerTable_->switchManager();
  if ((qosDelta.getOld() != qosDelta.getNew())) {
    [[maybe_unused]] const auto& lock = lockPolicy.lock();
    if (qosDelta.getOld() && qosDelta.getNew()) {
      if (*qosDelta.getOld() != *qosDelta.getNew()) {
        switchManager.clearQosPolicy();
        qosMapManager.changeQosMap(qosDelta.getOld(), qosDelta.getNew(), true);
        switchManager.setQosPolicy();
      }
    } else if (qosDelta.getNew()) {
      qosMapManager.addQosMap(qosDelta.getNew(), true);
      switchManager.setQosPolicy();
    } else if (qosDelta.getOld()) {
      switchManager.clearQosPolicy();
      qosMapManager.removeQosMap(qosDelta.getOld(), true);
    }
  }
}

template <typename LockPolicyT>
void SaiSwitch::processLinkStateChangeDelta(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  DeltaFunctions::forEachChanged(
      delta.getPortsDelta(),
      [&](const std::shared_ptr<Port>& oldPort,
          const std::shared_ptr<Port>& newPort) {
        [[maybe_unused]] const auto& lock = lockPolicy.lock();
        if (!oldPort->isEnabled() && !newPort->isEnabled()) {
          return;
        }
        auto id = newPort->getID();
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
          auto platformPort = platform_->getPort(id);
          platformPort->linkStatusChanged(
              newPort->isUp(), newPort->isEnabled());
        }

        if (oldPort->getLedPortExternalState() !=
            newPort->getLedPortExternalState()) {
          if (newPort->getLedPortExternalState().has_value()) {
            auto platformPort = platform_->getPort(id);
            platformPort->externalState(
                newPort->getLedPortExternalState().value());
          }
        }
      });
}

bool SaiSwitch::transactionsSupported() const {
  return true;
}

std::shared_ptr<SwitchState> SaiSwitch::constructSwitchStateWithFib() noexcept {
  auto state = std::make_shared<SwitchState>();

  const auto& switchStateRoutesMap =
      managerTable_->routeManager().getSwitchStateRoutesMap();
  const auto& virtualRouterManager = managerTable_->virtualRouterManager();

  // Create a new ForwardingInformationBaseMap for the new design
  auto fibsMap = std::make_shared<ForwardingInformationBaseMap>();

  // Group routes by router ID
  std::map<RouterID, std::vector<std::shared_ptr<RouteV4>>> routesV4;
  std::map<RouterID, std::vector<std::shared_ptr<RouteV6>>> routesV6;

  // Create a set of routerIds from each routeEntry in switchStateRoutesMap
  // Typically, there is only RouterID(0) but not making that assumption
  std::set<RouterID> routerIds;
  std::map<sai_object_id_t, RouterID> vrIdToRouterIdCache;
  std::for_each(
      switchStateRoutesMap.begin(),
      switchStateRoutesMap.end(),
      [&virtualRouterManager, &routerIds, &vrIdToRouterIdCache](
          const auto& routeEntry) {
        auto vrId = routeEntry.first.virtualRouterId();
        auto routerId = virtualRouterManager.getRouterID(vrId);
        vrIdToRouterIdCache[vrId] = routerId;
        routerIds.insert(routerId);
      });
  for (const auto& routerId : routerIds) {
    // Create FIB containers
    auto fibContainer =
        std::make_shared<ForwardingInformationBaseContainer>(routerId);
    fibsMap->updateForwardingInformationBaseContainer(fibContainer);
  }

  // Iterate through the switchStateRoutesMap
  for (const auto& [routeEntry, routeVariant] : switchStateRoutesMap) {
    RouterID routerId = vrIdToRouterIdCache.at(routeEntry.virtualRouterId());

    if (std::holds_alternative<std::shared_ptr<RouteV4>>(routeVariant)) {
      auto route = std::get<std::shared_ptr<RouteV4>>(routeVariant);
      if (route) {
        routesV4[routerId].push_back(route);
      }
    } else if (std::holds_alternative<std::shared_ptr<RouteV6>>(routeVariant)) {
      auto route = std::get<std::shared_ptr<RouteV6>>(routeVariant);
      if (route) {
        routesV6[routerId].push_back(route);
      }
    }
  }

  // Process each router ID with v4 routes
  for (const auto& [routerId, v4Routes] : routesV4) {
    // Create IPv4 FIB
    typename ForwardingInformationBaseV4::Base::NodeContainer v4Container;
    for (const auto& route : v4Routes) {
      RoutePrefix<folly::IPAddressV4> prefix(
          route->prefix().network(), route->prefix().mask());
      v4Container.emplace(prefix.str(), route);
    }
    auto fibV4 =
        std::make_shared<ForwardingInformationBaseV4>(std::move(v4Container));
    fibsMap->getNodeIf(routerId)->setFib<folly::IPAddressV4>(fibV4);
  }

  // Process each router ID with v6 routes
  for (const auto& [routerId, v6Routes] : routesV6) {
    // Create IPv6 FIB
    typename ForwardingInformationBaseV6::Base::NodeContainer v6Container;
    for (const auto& route : v6Routes) {
      RoutePrefix<folly::IPAddressV6> prefix(
          route->prefix().network(), route->prefix().mask());
      v6Container.emplace(prefix.str(), route);
    }
    auto fibV6 =
        std::make_shared<ForwardingInformationBaseV6>(std::move(v6Container));
    fibsMap->getNodeIf(routerId)->setFib<folly::IPAddressV6>(fibV6);
  }

  // Create FibInfo and set the fibsMap
  auto fibInfo = std::make_shared<FibInfo>();
  fibInfo->resetFibsMap(fibsMap);

  // Create MultiSwitchFibInfoMap and add the FibInfo
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  auto scopeResolver = platform_->scopeResolver();
  auto scope = scopeResolver->scope(fibInfo);
  fibInfoMap->updateFibInfo(fibInfo, scope);

  // Reset the fibsInfoMap in the state
  state->resetFibsInfoMap(fibInfoMap);
  state->publish();
  return state;
}

void SaiSwitch::preRollback(const StateDelta& delta) noexcept {
  try {
    CoarseGrainedLockPolicy lockPolicy(saiSwitchMutex_);

    // Remove any potentially added routes in the new State.
    // Imagine the scenario where a device supports 4 route entries in the hw.
    // Old state has route {A, B}, and new state has route {B, C, D, E, F}.
    // When new state is being applied, say route A is deleted and route C, D, E
    // is added, addition of route F would trigger TABLE_FULL error and sai
    // switch starts rollback.
    // Note that routes {B, C, D, E} are still in ASIC, and at step 3 we tried
    // to reapply old state, which leads to the addition of route A and causing
    // rollback failure. Routes C, D and E are not deleted until step 4 of
    // removing unclaimed entries.
    // To avoid such rollback failure, remove added routes in new state as step
    // 0. This would ensure enough room in the hardware for replaying old state.
    for (const auto& fibInfoDelta : delta.getFibsInfoDelta()) {
      for (const auto& routeDelta : fibInfoDelta.getFibsMapDelta()) {
        auto routerID = routeDelta.getOld() ? routeDelta.getOld()->getID()
                                            : routeDelta.getNew()->getID();
        processAddedDelta(
            routeDelta.getFibDelta<folly::IPAddressV4>(),
            managerTable_->routeManager(),
            lockPolicy,
            &SaiRouteManager::removeRouteForRollback<folly::IPAddressV4>,
            routerID);
        processAddedDelta(
            routeDelta.getFibDelta<folly::IPAddressV6>(),
            managerTable_->routeManager(),
            lockPolicy,
            &SaiRouteManager::removeRouteForRollback<folly::IPAddressV6>,
            routerID);
      }
    }
  } catch (const std::exception& ex) {
    // Rollback failed. Fail hard.
    XLOG(FATAL) << " Pre rollback failed with : " << ex.what();
  }
}

void SaiSwitch::rollbackPartialRoutes(const StateDelta& delta) noexcept {
  // This API called before the actual rollback

  // During state change, assuming there are state delta vector is of size 10
  // If deltas were of size 10 and delta from 0-5 succeeded and 6th failed,
  // HwSwitch state would still be goodKnownState but SaiSwitch would have
  // the routes from 0-5 and the partial applied state from the 6th delta
  // This API will be invoked to clear partially applied delta
  try {
    CoarseGrainedLockPolicy lockPolicy(saiSwitchMutex_);

    for (const auto& fibInfoDelta : delta.getFibsInfoDelta()) {
      for (const auto& routeDelta : fibInfoDelta.getFibsMapDelta()) {
        auto routerID = routeDelta.getOld() ? routeDelta.getOld()->getID()
                                            : routeDelta.getNew()->getID();
        // v6 routes first since they go after v4 during normal operation
        processRemovedRoutesDeltaInReverse<
            CoarseGrainedLockPolicy,
            folly::IPAddressV6>(
            routerID, routeDelta.getFibDelta<folly::IPAddressV6>(), lockPolicy);
        processAddedRoutesDeltaInReverse<
            CoarseGrainedLockPolicy,
            folly::IPAddressV6>(
            routerID,
            routeDelta.getFibDelta<folly::IPAddressV6>(),
            lockPolicy,
            delta.newState());
        processChangedRoutesDeltaInReverse<
            CoarseGrainedLockPolicy,
            folly::IPAddressV6>(
            routerID,
            routeDelta.getFibDelta<folly::IPAddressV6>(),
            lockPolicy,
            delta.newState());

        processRemovedRoutesDeltaInReverse<
            CoarseGrainedLockPolicy,
            folly::IPAddressV4>(
            routerID, routeDelta.getFibDelta<folly::IPAddressV4>(), lockPolicy);
        processAddedRoutesDeltaInReverse<
            CoarseGrainedLockPolicy,
            folly::IPAddressV4>(
            routerID,
            routeDelta.getFibDelta<folly::IPAddressV4>(),
            lockPolicy,
            delta.newState());
        processChangedRoutesDeltaInReverse<
            CoarseGrainedLockPolicy,
            folly::IPAddressV4>(
            routerID,
            routeDelta.getFibDelta<folly::IPAddressV4>(),
            lockPolicy,
            delta.newState());
      }
    }
  } catch (const std::exception& ex) {
    // Partial route Rollback failed. Fail hard.
    XLOG(FATAL) << " Partial route rollback failed with : " << ex.what();
  }
}

void SaiSwitch::rollback(const std::vector<StateDelta>& deltas) noexcept {
  const auto& knownGoodState = deltas.back().newState();
  auto curBootType = getBootType();
  // Attempt rollback
  // Detailed design is in the sai_switch_transactions wiki, but at a high
  // level the steps of the rollback are 0) Remove any added entries in the new
  // state (currently routes) 1) Clear out our internal data structures (stores,
  // managers) in SW, while throttling writes to HW 2) Reinit managers and
  // SaiStores. SaiStore* will now have all the HW state 3) Replay
  // StateDelta(emptySwitchState, delta.oldState()) to get us to the pre
  // transaction state 4) Clear out any remaining handles in SaiStore to flush
  // state left in HW due to the failed transaction Steps 2-4 are exactly the
  // same as what we do for warmboot and piggy back heavily on it for both code
  // reuse and correctness
  try {
    CoarseGrainedLockPolicy lockPolicy(saiSwitchMutex_);
    // unregister callbacks to prevent sdk call backs from interfering the
    // rollback
    auto preRollbackSwitchRunState = getSwitchRunState();
    switchRunStateChangedImplLocked(
        lockPolicy.lock(), SwitchRunState::ROLLBACK);
    auto pfcWatchdogRecoveryAction =
        knownGoodState->getPfcWatchdogRecoveryAction();
    bool pfcDeadlockEnabled = pfcDeadlockEnabled_;
    if (pfcDeadlockEnabled) {
      processPfcDeadlockNotificationCallback(
          std::nullopt /* ignored */, std::nullopt);
    }
    auto hwSwitchJson = toFollyDynamicLocked(lockPolicy.lock());
    {
      HwWriteBehaviorRAII writeBehavior{HwWriteBehavior::SKIP};
      if (platform_->getAsic()->isSupported(HwAsic::Feature::DEFAULT_VLAN)) {
        /*
         * TODO(pshaikh): introduce VLAN ID 1 in all configs across all
         * platforms which support default vlans.
         * TODO(pshaikh): remove default vlan from SwitchConfig, and
         * SwitchSettings for SAI, existing value of 4094 is legacy (non-SAI)
         * implementation
         */
        auto defaultVlan =
            managerTable_->switchManager().getDefaultVlanAdapterKey();
        saiStore_->get<SaiVlanTraits>().removeUnclaimedWarmbootHandlesIf(
            [defaultVlan](const auto& obj) {
              return obj->adapterKey() == defaultVlan;
            },
            true /* owned by adapter */);
      }
      managerTable_->reset(true /*skip switch manager reset*/);
    }
    // The work flow below is essentially a in memory warm boot,
    // so set the bootType to warm boot for duration of roll back. We
    // will restore it once we are done with roll back.
    bootType_ = BootType::WARM_BOOT;
    initStoreAndManagersLocked(
        lockPolicy.lock(),
        // We are being strict here in the sense of not allowing any HW
        // writes during this reinit of managers. However this does rely
        // on SaiStore being able to fetch exact state via get apis. If
        // gets are missing for some APIs we may choose to relax this to
        // WRITE as long as it does not effect forwarding. If it does
        // affect forwarding, we must fix for both transactions as well as
        // warmboot use case.
        HwWriteBehavior::FAIL,
        &hwSwitchJson[kAdapterKeys],
        &hwSwitchJson[kAdapterKey2AdapterHostKey]);
    for (const auto& delta : deltas) {
      stateChangedImplLocked(delta, lockPolicy);
    }
    saiStore_->printWarmbootHandles();
    saiStore_->removeUnexpectedUnclaimedWarmbootHandles();
    bootType_ = curBootType;
    for (const auto& value :
         apache::thrift::TEnumTraits<SwitchRunState>::values) {
      // transition through all switch run states until pre-rollback run state
      // is reached. this will register all appropriate callbacks which were
      // unregistered
      if (value > preRollbackSwitchRunState) {
        continue;
      }
      switchRunStateChangedImplLocked(lockPolicy.lock(), value);
    }
    if (pfcDeadlockEnabled) {
      processPfcDeadlockNotificationCallback(
          std::nullopt /* ignored */, pfcWatchdogRecoveryAction);
    }
  } catch (const std::exception& ex) {
    // Rollback failed. Fail hard.
    XLOG(FATAL) << " Roll back failed with : " << ex.what();
  }
}

bool SaiSwitch::l2LearningModeChangeProhibited() const {
  // We don't allow for l2 learning mode change after initial setting. This
  // means
  // - For cold boot prohibit changes after first config application
  // - For warmboot prohibit changes after we have applied the warm boot
  // state (in init). We still need to apply the initial changes in warmboot
  // to setup our internal data structures correctly
  auto l2LearningChangeProhibitedAfter = bootType_ == BootType::WARM_BOOT
      ? SwitchRunState::INITIALIZED
      : SwitchRunState::CONFIGURED;
  /*
   * Changing L2 learning mode on SAI is quite complex and I am
   *     inclined to not support it at all here (see prod plan below).
   * Consider,
   *
   * - On transition from SW to HW learning, we need to clear out
   * SwitchState MAC table and our FDB table of dynamic entries (to get
   * back to a state where we would be had we had HW learning mode).
   * Clearing out MAC table in SwitchState is easy enough. However we just
   * can't prune out the FDB entries in FDB manager, since that would
   * clear hw causing MACs to be unprogrammed. There are bunch of tricks
   * we can adopt to work around
   *                                  - Keep around the FDB entries but
   * mark them dead. This now complicates what we do when new FDB entries
   * get programmed with the same MAC (which we won;t know about since we
   * are now in HW learning mode) or when links go down, do we reset the
   * dead entries or not?
   *                                                      - Blow away the
   * FDB entries but blackhole writes to HW. For this we will have to
   * create a SAI blackholer since to one we have for native and block HW
   * writes. In addition we would have to block observers to FDB entry
   * reset, since we don't want ARP,NDP entries to observe this and start
   * shrinking ECMP groups. This is getting to be quite complex
   *                                                                                   -
   * On transition from HW to SW learning We need to traverse the hw FDB
   * entries and populate our MAC table. This is doable but needs a SAI
   * extension to either observe or iterate SAI FDB table updates in HW
   * learning mode. On the observer side things look clearer since we
   * would just observe what's already in HW.
   *
   * Considering all this, just prohibit transitions. For prod,
   * we will just deploy withSW L2 learning  everywhere. SW l2 learning
   * is a superset of HW l2 learning in terms of functionality, so for SAI
   * we will just standardize on that.
   */
  return getSwitchRunState() >= l2LearningChangeProhibitedAfter;
}

bool SaiSwitch::ecmpGroupSettingsChangeProhibited() const {
  // SAI_NEXT_HOP_GROUP_ATTR_SPLIT_HORIZON_ENABLE is CREATE_ONLY, so a later
  // change cannot reach groups already created from the old value. Honouring
  // one would mean recreating every next hop group and repointing every route
  // that references it, which we do not support. Prohibit the change instead,
  // on the same terms as l2 learning mode:
  // - cold boot, prohibit after the first config application
  // - warm boot, prohibit after the warm boot state has been applied in init
  auto ecmpGroupSettingsChangeProhibitedAfter = bootType_ == BootType::WARM_BOOT
      ? SwitchRunState::INITIALIZED
      : SwitchRunState::CONFIGURED;
  return getSwitchRunState() >= ecmpGroupSettingsChangeProhibitedAfter;
}

template <typename LockPolicyT, typename AddrT>
void SaiSwitch::processRemovedRoutesDelta(
    const RouterID& routerID,
    const auto& routesDelta,
    const LockPolicyT& lockPolicy) {
  if (!getRollbackInProgress_()) {
    // Normal processing order
    processRemovedDelta(
        routesDelta,
        managerTable_->routeManager(),
        lockPolicy,
        &SaiRouteManager::removeRoute<AddrT>,
        routerID);
  } else {
    processRemovedRoutesDeltaInReverse<LockPolicyT, AddrT>(
        routerID, routesDelta, lockPolicy);
  }
}

template <typename LockPolicyT, typename AddrT>
void SaiSwitch::processChangedAndAddedRoutesDelta(
    const RouterID& routerID,
    const auto& routesDelta,
    const LockPolicyT& lockPolicy,
    const std::shared_ptr<SwitchState>& state) {
  if (!getRollbackInProgress_()) {
    // Normal processing order: changed first, then added
    processChangedDelta(
        routesDelta,
        managerTable_->routeManager(),
        lockPolicy,
        &SaiRouteManager::changeRoute<AddrT>,
        routerID,
        state);
    processAddedDelta(
        routesDelta,
        managerTable_->routeManager(),
        lockPolicy,
        &SaiRouteManager::addRoute<AddrT>,
        routerID,
        state);
  } else {
    processAddedRoutesDeltaInReverse<LockPolicyT, AddrT>(
        routerID, routesDelta, lockPolicy, state);
    processChangedRoutesDeltaInReverse<LockPolicyT, AddrT>(
        routerID, routesDelta, lockPolicy, state);
  }
}

// Determine rollback index based on deltaApplicationBehavior
std::optional<int> getRollbackIndexFromDeltaApplicationBehavior(
    const std::vector<StateDelta>& deltas,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  std::optional<int> rollbackIndex = std::nullopt;
  if (deltaApplicationBehavior.has_value()) {
    const auto& app = deltaApplicationBehavior.value();
    switch (*app.mode()) {
      case DeltaApplicationMode::APPLY_ALL:
        // Normal mode - no rollback
        break;
      case DeltaApplicationMode::ROLLBACK:
        // Rollback at the end - use last delta index
        rollbackIndex = static_cast<int>(deltas.size()) - 1;
        break;
      case DeltaApplicationMode::ROLLBACK_AT_INDEX:
        // Rollback at specific index
        if (app.rollbackIndex().has_value()) {
          rollbackIndex = *app.rollbackIndex();
        }
        break;
      default:
        break;
    }
  }
  return rollbackIndex;
}

std::shared_ptr<SwitchState> SaiSwitch::stateChangedImpl(
    const std::vector<StateDelta>& deltas,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  FineGrainedLockPolicy lockPolicy(saiSwitchMutex_);

  // This is unlikely to happen but if it does, return current state
  if (deltas.size() == 0) {
    return getProgrammedState();
  }

  auto rollbackIndex = getRollbackIndexFromDeltaApplicationBehavior(
      deltas, deltaApplicationBehavior);
  setIntermediateState(getProgrammedState());
  int count = 0;
  std::shared_ptr<SwitchState> appliedState{nullptr};
  for (const auto& delta : deltas) {
    appliedState = stateChangedImplLocked(delta, lockPolicy);
    // if the current delta fails to apply, return the last successful state
    // HwSwitchHandler would rollback based on the response
    if (appliedState != delta.newState()) {
      XLOG(DBG2) << "Failed to apply " << count << " delta in  "
                 << deltas.size() << " deltas";
      return appliedState;
    }
    if (rollbackIndex.has_value() && rollbackIndex.value() == count) {
      throw FbossError(
          "Forced rollback for testing, size: ",
          deltas.size(),
          " at index: ",
          count);
    }
    // Save the current delta that applied cleanly
    setIntermediateState(appliedState);
    count++;
  }
  return appliedState;
}

template <typename LockPolicyT>
std::shared_ptr<SwitchState> SaiSwitch::stateChangedImplLocked(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  // Unsupported features
  checkUnsupportedDelta(
      delta.getTeFlowEntriesDelta(), managerTable_->teFlowEntryManager());
  // update switch settings first. In particular drain box first if drain ==
  // true
  processSwitchSettingsDrainStateChange(
      delta, cfg::SwitchDrainState::DRAINED, lockPolicy);
  processSwitchSettingsChangeSansDrained(delta, lockPolicy);
  processLocalCapsuleSwitchIdsDelta(delta, lockPolicy);
  processCreditRequestProfileDelta(delta, lockPolicy);

  if (platform_->getAsic()->isSupported(HwAsic::Feature::ARS)) {
    processFlowletSwitchingConfigAdded(delta, lockPolicy);
  }

  // process non-default qos policies, which are stored in
  // SwitchStatae::qosPolicyMaps
  processDelta(
      delta.getQosPoliciesDelta(),
      managerTable_->qosMapManager(),
      lockPolicy,
      &SaiQosMapManager::changeQosMap,
      &SaiQosMapManager::addQosMap,
      &SaiQosMapManager::removeQosMap,
      false);
  processDefaultDataPlanePolicyDelta(delta, lockPolicy);

  processRemovedDelta(
      delta.getMirrorOnDropReportsDelta(),
      managerTable_->tamManager(),
      lockPolicy,
      &SaiTamManager::removeMirrorOnDropReport);

  for (const auto& fibInfoDelta : delta.getFibsInfoDelta()) {
    for (const auto& routeDelta : fibInfoDelta.getFibsMapDelta()) {
      auto routerID = routeDelta.getOld() ? routeDelta.getOld()->getID()
                                          : routeDelta.getNew()->getID();
      processRemovedRoutesDelta<LockPolicyT, folly::IPAddressV6>(
          routerID, routeDelta.getFibDelta<folly::IPAddressV6>(), lockPolicy);
      processRemovedRoutesDelta<LockPolicyT, folly::IPAddressV4>(
          routerID, routeDelta.getFibDelta<folly::IPAddressV4>(), lockPolicy);
    }
  }

  for (const auto& vlanDelta : delta.getVlansDelta()) {
    processRemovedDelta(
        vlanDelta.getArpDelta(),
        managerTable_->neighborManager(),
        lockPolicy,
        &SaiNeighborManager::removeNeighbor<ArpEntry>);

    processRemovedDelta(
        vlanDelta.getNdpDelta(),
        managerTable_->neighborManager(),
        lockPolicy,
        &SaiNeighborManager::removeNeighbor<NdpEntry>);

    processRemovedDelta(
        vlanDelta.getMacDelta(),
        managerTable_->fdbManager(),
        lockPolicy,
        &SaiFdbManager::removeMac,
        vlanDelta.getNew() ? vlanDelta.getNew()->getID()
                           : vlanDelta.getOld()->getID());
  }

  // NOTE: Neighbor removals must be processed before changes/adds.
  // IntfDeltaValidator depends on this ordering to detect transient
  // multi-MAC states on PORT interfaces. See ValidateInterfaceDelta.cpp.
  auto processRemovedNeighborDeltaForIntfs =
      [this, &lockPolicy](const auto& intfsDelta) {
        for (const auto& intfDelta : intfsDelta) {
          processRemovedDelta(
              intfDelta.getArpEntriesDelta(),
              managerTable_->neighborManager(),
              lockPolicy,
              &SaiNeighborManager::removeNeighbor<ArpEntry>);

          processRemovedDelta(
              intfDelta.getNdpEntriesDelta(),
              managerTable_->neighborManager(),
              lockPolicy,
              &SaiNeighborManager::removeNeighbor<NdpEntry>);
        }
      };
  processRemovedNeighborDeltaForIntfs(delta.getIntfsDelta());
  processRemovedNeighborDeltaForIntfs(delta.getRemoteIntfsDelta());

  // Port RIFs are created based on system port - therefore during removal, both
  // local and remote RIFs should be removed before system port removal.
  processRemovedDelta(
      delta.getRemoteIntfsDelta(),
      managerTable_->routerInterfaceManager(),
      lockPolicy,
      &SaiRouterInterfaceManager::removeRemoteRouterInterface);

  processRemovedDelta(
      delta.getIntfsDelta(),
      managerTable_->routerInterfaceManager(),
      lockPolicy,
      &SaiRouterInterfaceManager::removeLocalRouterInterface);

  // Remove virtual routers after the routes and router interfaces using them
  processRemovedDelta(
      delta.getVrfsDelta(),
      managerTable_->virtualRouterManager(),
      lockPolicy,
      &SaiVirtualRouterManager::removeVrf);

  // Remove system ports (which may depend on local ports
  // before removing ports)
  processRemovedDelta(
      delta.getSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::removeSystemPort);
  processRemovedDelta(
      delta.getRemoteSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::removeSystemPort);
  processRemovedDelta(
      delta.getFabricLinkMonitoringSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::removeFabricLinkMonitoringSystemPort,
      delta.oldState());
  processRemovedDelta(
      delta.getPortsDelta(),
      managerTable_->portManager(),
      lockPolicy,
      &SaiPortManager::removePort);
  processChangedDelta(
      delta.getPortsDelta(),
      managerTable_->portManager(),
      lockPolicy,
      &SaiPortManager::changePort);
  processChangedDelta(
      delta.getSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::changeSystemPort);
  processChangedDelta(
      delta.getRemoteSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::changeSystemPort);
  processChangedDelta(
      delta.getFabricLinkMonitoringSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::changeFabricLinkMonitoringSystemPort);
  processChangedDelta(
      delta.getPortsDelta(),
      managerTable_->portManager(),
      lockPolicy,
      &SaiPortManager::loadPortQueuesForChangedPort);
  processAddedDelta(
      delta.getPortsDelta(),
      managerTable_->portManager(),
      lockPolicy,
      &SaiPortManager::addPort);
  processAddedDelta(
      delta.getSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::addSystemPort);
  processAddedDelta(
      delta.getRemoteSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::addSystemPort);
  processAddedDelta(
      delta.getFabricLinkMonitoringSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::addFabricLinkMonitoringSystemPort,
      delta.newState());
  processAddedDelta(
      delta.getPortsDelta(),
      managerTable_->portManager(),
      lockPolicy,
      &SaiPortManager::loadPortQueuesForAddedPort);

  // VOQ/Fabric switches require that the packets are not tagged with any
  // VLAN. Thus, no VLAN delta processing is needed for these switches
  if (!(getSwitchType() == cfg::SwitchType::FABRIC ||
        getSwitchType() == cfg::SwitchType::VOQ)) {
    processDelta(
        delta.getVlansDelta(),
        managerTable_->vlanManager(),
        lockPolicy,
        &SaiVlanManager::changeVlan,
        &SaiVlanManager::addVlan,
        &SaiVlanManager::removeVlan);
  } else {
    // Only DNX asics support switch reachability function
    processPortStateChangedForSwitchReachability(delta, lockPolicy);
  }

  {
    // this is specific for fabric/voq switches for now
    [[maybe_unused]] const auto& lock = lockPolicy.lock();
    fabricConnectivityManager_->stateUpdated(delta);
  }

  // LAGs
  processDelta(
      delta.getAggregatePortsDelta(),
      managerTable_->lagManager(),
      lockPolicy,
      &SaiLagManager::changeLag,
      &SaiLagManager::addLag,
      &SaiLagManager::removeLag);

  if (platform_->getAsic()->isSupported(HwAsic::Feature::BRIDGE_PORT_8021Q)) {
    // Add/Change bridge ports
    DeltaFunctions::forEachChanged(
        delta.getPortsDelta(),
        [&](const std::shared_ptr<Port>& oldPort,
            const std::shared_ptr<Port>& newPort) {
          auto portID = oldPort->getID();
          [[maybe_unused]] const auto& lock = lockPolicy.lock();
          if (managerTable_->lagManager().isLagMember(portID)) {
            // if port is member of lag, ignore it
            return;
          }
          managerTable_->portManager().changeBridgePort(oldPort, newPort);
        });

    DeltaFunctions::forEachAdded(
        delta.getPortsDelta(), [&](const std::shared_ptr<Port>& newPort) {
          auto portID = newPort->getID();
          [[maybe_unused]] const auto& lock = lockPolicy.lock();
          if (managerTable_->lagManager().isLagMember(portID)) {
            // if port is member of lag, ignore it
            return;
          }
          managerTable_->portManager().addBridgePort(newPort);
        });

    DeltaFunctions::forEachChanged(
        delta.getAggregatePortsDelta(),
        [&](const std::shared_ptr<AggregatePort>& oldAggPort,
            const std::shared_ptr<AggregatePort>& newAggPort) {
          [[maybe_unused]] const auto& lock = lockPolicy.lock();
          managerTable_->lagManager().changeBridgePort(oldAggPort, newAggPort);
        });

    DeltaFunctions::forEachAdded(
        delta.getAggregatePortsDelta(),
        [&](const std::shared_ptr<AggregatePort>& newAggPort) {
          [[maybe_unused]] const auto& lock = lockPolicy.lock();
          managerTable_->lagManager().addBridgePort(newAggPort);
        });
  }

  // Add virtual routers before the router interfaces using them
  processChangedDelta(
      delta.getVrfsDelta(),
      managerTable_->virtualRouterManager(),
      lockPolicy,
      &SaiVirtualRouterManager::changeVrf);
  processAddedDelta(
      delta.getVrfsDelta(),
      managerTable_->virtualRouterManager(),
      lockPolicy,
      &SaiVirtualRouterManager::addVrf);

  processChangedDelta(
      delta.getIntfsDelta(),
      managerTable_->routerInterfaceManager(),
      lockPolicy,
      &SaiRouterInterfaceManager::changeLocalRouterInterface);
  processAddedDelta(
      delta.getIntfsDelta(),
      managerTable_->routerInterfaceManager(),
      lockPolicy,
      &SaiRouterInterfaceManager::addLocalRouterInterface);

  processChangedDelta(
      delta.getRemoteIntfsDelta(),
      managerTable_->routerInterfaceManager(),
      lockPolicy,
      &SaiRouterInterfaceManager::changeRemoteRouterInterface);
  processAddedDelta(
      delta.getRemoteIntfsDelta(),
      managerTable_->routerInterfaceManager(),
      lockPolicy,
      &SaiRouterInterfaceManager::addRemoteRouterInterface);

  // For VOQ switches, neighbor tables live on port based RIFs.
  // NOTE: Per interface, the order is: change ARP, add ARP, change NDP,
  // add NDP. IntfDeltaValidator depends on this ordering (removals before
  // changes before adds) to detect transient multi-MAC states on PORT
  // interfaces. See ValidateInterfaceDelta.cpp.
  auto processNeighborChangedAndAddedDeltaForIntfs =
      [this, &lockPolicy](const auto& intfsDelta) {
        for (const auto& intfDelta : intfsDelta) {
          processChangedDelta(
              intfDelta.getArpEntriesDelta(),
              managerTable_->neighborManager(),
              lockPolicy,
              &SaiNeighborManager::changeNeighbor<ArpEntry>);

          processAddedDelta(
              intfDelta.getArpEntriesDelta(),
              managerTable_->neighborManager(),
              lockPolicy,
              &SaiNeighborManager::addNeighbor<ArpEntry>);

          processChangedDelta(
              intfDelta.getNdpEntriesDelta(),
              managerTable_->neighborManager(),
              lockPolicy,
              &SaiNeighborManager::changeNeighbor<NdpEntry>);

          processAddedDelta(
              intfDelta.getNdpEntriesDelta(),
              managerTable_->neighborManager(),
              lockPolicy,
              &SaiNeighborManager::addNeighbor<NdpEntry>);
        }
      };
  processNeighborChangedAndAddedDeltaForIntfs(delta.getIntfsDelta());
  processNeighborChangedAndAddedDeltaForIntfs(delta.getRemoteIntfsDelta());

  processAddedDelta(
      delta.getPortsDelta(),
      managerTable_->portManager(),
      lockPolicy,
      &SaiPortManager::addPortShelEnable);

  processChangedDelta(
      delta.getPortsDelta(),
      managerTable_->portManager(),
      lockPolicy,
      &SaiPortManager::changePortShelEnable);

  processAddedDelta(
      delta.getSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::addSystemPortShelPktDstEnable);

  processChangedDelta(
      delta.getSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::changeSystemPortShelPktDstEnable);

  processAddedDelta(
      delta.getRemoteSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::addSystemPortShelPktDstEnable);

  processChangedDelta(
      delta.getRemoteSystemPortsDelta(),
      managerTable_->systemPortManager(),
      lockPolicy,
      &SaiSystemPortManager::changeSystemPortShelPktDstEnable);

  for (const auto& vlanDelta : delta.getVlansDelta()) {
    processChangedDelta(
        vlanDelta.getArpDelta(),
        managerTable_->neighborManager(),
        lockPolicy,
        &SaiNeighborManager::changeNeighbor<ArpEntry>);
    processAddedDelta(
        vlanDelta.getArpDelta(),
        managerTable_->neighborManager(),
        lockPolicy,
        &SaiNeighborManager::addNeighbor<ArpEntry>);

    processChangedDelta(
        vlanDelta.getNdpDelta(),
        managerTable_->neighborManager(),
        lockPolicy,
        &SaiNeighborManager::changeNeighbor<NdpEntry>);
    processAddedDelta(
        vlanDelta.getNdpDelta(),
        managerTable_->neighborManager(),
        lockPolicy,
        &SaiNeighborManager::addNeighbor<NdpEntry>);

    const auto macVlanId = vlanDelta.getNew() ? vlanDelta.getNew()->getID()
                                              : vlanDelta.getOld()->getID();
    processChangedDelta(
        vlanDelta.getMacDelta(),
        managerTable_->fdbManager(),
        lockPolicy,
        &SaiFdbManager::changeMac,
        macVlanId);
    processAddedDelta(
        vlanDelta.getMacDelta(),
        managerTable_->fdbManager(),
        lockPolicy,
        &SaiFdbManager::addMac,
        macVlanId);
  }

  processDelta(
      delta.getSrv6TunnelsDelta(),
      managerTable_->srv6TunnelManager(),
      lockPolicy,
      &SaiSrv6TunnelManager::changeSrv6Tunnel,
      &SaiSrv6TunnelManager::addSrv6Tunnel,
      &SaiSrv6TunnelManager::removeSrv6Tunnel);

  processDelta(
      delta.getVxlanTunnelsDelta(),
      managerTable_->vxlanTunnelManager(),
      lockPolicy,
      &SaiVxlanTunnelManager::changeVxlanTunnel,
      &SaiVxlanTunnelManager::addVxlanTunnel,
      &SaiVxlanTunnelManager::removeVxlanTunnel);

  for (const auto& fibInfoDelta : delta.getFibsInfoDelta()) {
    for (const auto& routeDelta : fibInfoDelta.getFibsMapDelta()) {
      auto routerID = routeDelta.getOld() ? routeDelta.getOld()->getID()
                                          : routeDelta.getNew()->getID();
      processChangedAndAddedRoutesDelta<LockPolicyT, folly::IPAddressV6>(
          routerID,
          routeDelta.getFibDelta<folly::IPAddressV6>(),
          lockPolicy,
          delta.newState());
      processChangedAndAddedRoutesDelta<LockPolicyT, folly::IPAddressV4>(
          routerID,
          routeDelta.getFibDelta<folly::IPAddressV4>(),
          lockPolicy,
          delta.newState());
    }
  }
  {
    auto multiSwitchControlPlaneDelta = delta.getControlPlaneDelta();
    [[maybe_unused]] const auto& lock = lockPolicy.lock();
    managerTable_->hostifManager().processHostifDelta(
        multiSwitchControlPlaneDelta);
  }

  if (platform_->getAsic()->isSupported(HwAsic::Feature::SAI_MPLS_INSEGMENT)) {
    // Split so removal, which resolves nothing, does not take newState.
    auto labelDelta = delta.getLabelForwardingInformationBaseDelta();
    processRemovedDelta(
        labelDelta,
        managerTable_->inSegEntryManager(),
        lockPolicy,
        &SaiInSegEntryManager::processRemovedInSegEntry);
    processChangedDelta(
        labelDelta,
        managerTable_->inSegEntryManager(),
        lockPolicy,
        &SaiInSegEntryManager::processChangedInSegEntry,
        delta.newState());
    processAddedDelta(
        labelDelta,
        managerTable_->inSegEntryManager(),
        lockPolicy,
        &SaiInSegEntryManager::processAddedInSegEntry,
        delta.newState());
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  // UDF groups are processed prior to load balancer and ACL tables
  // Both require UDF groups to be created before referencing them
  processAddedDelta(
      delta.getUdfPacketMatcherDelta(),
      managerTable_->udfManager(),
      lockPolicy,
      &SaiUdfManager::addUdfMatch);
  processAddedDelta(
      delta.getUdfGroupDelta(),
      managerTable_->udfManager(),
      lockPolicy,
      &SaiUdfManager::addUdfGroup);
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  if (platform_->getAsic()->isSupported(HwAsic::Feature::SAI_UDF_HASH)) {
    // There're several constraints for load balancer and Udf objects.
    // 1. Udf Match needs to be processed before Udf Group (create, remove and
    // changed).
    // 2. In the case of removing Udf group: load balancer needs to remove
    // association with Udf Group.
    // 3. In the case of creating load balancer: Udf group needs to be created
    // first.
    // 4. In the case of changing load balancer: a. new Udf group needs to be
    // created, b. Load balancer starts to use new Udf group, c. Remove old
    // Udf group.
    processAddedDelta(
        delta.getLoadBalancersDelta(),
        managerTable_->switchManager(),
        lockPolicy,
        &SaiSwitchManager::addOrUpdateLoadBalancer);
    // TODO(zecheng): Process Udf Match changed
    // TODO(zecheng): Process Udf Group changed
    processChangedDelta(
        delta.getLoadBalancersDelta(),
        managerTable_->switchManager(),
        lockPolicy,
        &SaiSwitchManager::changeLoadBalancer);
    processRemovedDelta(
        delta.getLoadBalancersDelta(),
        managerTable_->switchManager(),
        lockPolicy,
        &SaiSwitchManager::removeLoadBalancer);
  } else {
    processDelta(
        delta.getLoadBalancersDelta(),
        managerTable_->switchManager(),
        lockPolicy,
        &SaiSwitchManager::changeLoadBalancer,
        &SaiSwitchManager::addOrUpdateLoadBalancer,
        &SaiSwitchManager::removeLoadBalancer);
  }
#else
  processDelta(
      delta.getLoadBalancersDelta(),
      managerTable_->switchManager(),
      lockPolicy,
      &SaiSwitchManager::changeLoadBalancer,
      &SaiSwitchManager::addOrUpdateLoadBalancer,
      &SaiSwitchManager::removeLoadBalancer);
#endif

  /*
   * Add/update mirrors before processing ACL, as ACLs with action
   * INGRESS/EGRESS Mirror rely on the Mirror being created.
   */
  processDelta(
      delta.getMirrorsDelta(),
      managerTable_->mirrorManager(),
      lockPolicy,
      &SaiMirrorManager::changeMirror,
      &SaiMirrorManager::addNode,
      &SaiMirrorManager::removeMirror);

  processChangedDelta(
      delta.getMirrorOnDropReportsDelta(),
      managerTable_->tamManager(),
      lockPolicy,
      &SaiTamManager::changeMirrorOnDropReport);
  processAddedDelta(
      delta.getMirrorOnDropReportsDelta(),
      managerTable_->tamManager(),
      lockPolicy,
      &SaiTamManager::addMirrorOnDropReport);

  processDelta(
      delta.getIpTunnelsDelta(),
      managerTable_->tunnelManager(),
      lockPolicy,
      &SaiTunnelManager::changeTunnel,
      &SaiTunnelManager::addTunnel,
      &SaiTunnelManager::removeTunnel);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  processDelta(
      delta.getMySidsDelta(),
      managerTable_->srv6MySidManager(),
      lockPolicy,
      &SaiSrv6MySidManager::changeMySidEntry,
      &SaiSrv6MySidManager::addMySidEntry,
      &SaiSrv6MySidManager::removeMySidEntry,
      delta.newState());
#endif

#if defined(TAJO_SDK_VERSION_1_42_8)
  FLAGS_enable_acl_table_group = false;
#endif
  if (FLAGS_enable_acl_table_group) {
    auto processAclTableGroups = [&](const auto& aclTableGroupDelta) {
      processDelta(
          aclTableGroupDelta,
          managerTable_->aclTableGroupManager(),
          lockPolicy,
          &SaiAclTableGroupManager::changedAclTableGroup,
          &SaiAclTableGroupManager::addAclTableGroup,
          &SaiAclTableGroupManager::removeAclTableGroup);
      if (aclTableGroupDelta.getNew()) {
        for (const auto& [_, tableGroupMap] : *aclTableGroupDelta.getNew()) {
          processAclTableGroupDelta(delta, *tableGroupMap, lockPolicy);
        }
      }
    };

    auto aclTableGroupsDelta = delta.getAclTableGroupsDelta();
    auto portAclTableGroupsDelta =
        MultiSwitchMapDelta<MultiSwitchAclTableGroupMap>(
            delta.oldState()->getPortAclTableGroups().get(),
            delta.newState()->getPortAclTableGroups().get());
    processAclTableGroups(aclTableGroupsDelta);
    processAclTableGroups(portAclTableGroupsDelta);
    // Program replacement tables and rebind ports before removing old tables,
    // so ports never reference a removed SAI object during one state update.
    processChangedDelta(
        delta.getPortsDelta(),
        managerTable_->portManager(),
        lockPolicy,
        &SaiPortManager::changeIngressAcl);
    processAddedDelta(
        delta.getPortsDelta(),
        managerTable_->portManager(),
        lockPolicy,
        &SaiPortManager::setIngressAcl);
    {
      [[maybe_unused]] const auto& lock = lockPolicy.lock();
      managerTable_->aclTableManager().removeObsoletePortBoundAclTables(
          delta.oldState(), delta.newState());
    }
  } else {
    std::set<cfg::AclTableQualifier> oldRequiredQualifiers{};
    std::set<cfg::AclTableQualifier> newRequiredQualifiers{};
    if (delta.getAclsDelta().getOld()) {
      oldRequiredQualifiers =
          delta.getAclsDelta().getOld()->requiredQualifiers();
    }
    if (delta.getAclsDelta().getNew()) {
      newRequiredQualifiers =
          delta.getAclsDelta().getNew()->requiredQualifiers();
    }
    bool aclTableUpdateSupport = platform_->getAsic()->isSupported(
        HwAsic::Feature::SAI_ACL_TABLE_UPDATE);
#if defined(TAJO_SDK_VERSION_1_42_8)
    aclTableUpdateSupport = false;
#endif
    if (!oldRequiredQualifiers.empty() &&
        oldRequiredQualifiers != newRequiredQualifiers &&
        aclTableUpdateSupport &&
        !managerTable_->aclTableManager()
             .areQualifiersSupportedInDefaultAclTable(newRequiredQualifiers)) {
      // qualifiers changed and default acl table doesn't support all of them,
      // remove default acl table and add a new one. table removal should
      // clear acl entries too
      managerTable_->switchManager().resetIngressAcl();
      managerTable_->aclTableManager().removeDefaultIngressAclTable();
      managerTable_->aclTableManager().addDefaultIngressAclTable();
      managerTable_->switchManager().setIngressAcl();
    }

    // Removals first: an entry taking over a priority that another entry is
    // vacating must not be added while the old one still holds it.
    processRemovedDelta(
        delta.getAclsDelta(),
        managerTable_->aclTableManager(),
        lockPolicy,
        &SaiAclTableManager::removeAclEntry,
        cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
        delta.newState());
    processChangedDelta(
        delta.getAclsDelta(),
        managerTable_->aclTableManager(),
        lockPolicy,
        &SaiAclTableManager::changedAclEntry,
        cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
        delta.newState());
    processAddedDelta(
        delta.getAclsDelta(),
        managerTable_->aclTableManager(),
        lockPolicy,
        &SaiAclTableManager::addAclEntry,
        cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
        delta.newState());
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  // ACLs are done processing. Remove UDF groups not required
  processRemovedDelta(
      delta.getUdfPacketMatcherDelta(),
      managerTable_->udfManager(),
      lockPolicy,
      &SaiUdfManager::removeUdfMatch);
  processRemovedDelta(
      delta.getUdfGroupDelta(),
      managerTable_->udfManager(),
      lockPolicy,
      &SaiUdfManager::removeUdfGroup);
#endif

  if (platform_->getAsic()->isSupported(HwAsic::Feature::ARS)) {
    processFlowletSwitchingConfigChanged(delta, lockPolicy);
  }

  processPfcWatchdogGlobalDelta(delta, lockPolicy);

  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::RESOURCE_USAGE_STATS)) {
    updateResourceUsage(lockPolicy);
  }

  // Process link state change delta and update the LED status
  processLinkStateChangeDelta(delta, lockPolicy);
  // Undrain switch last
  processSwitchSettingsDrainStateChange(
      delta, cfg::SwitchDrainState::UNDRAINED, lockPolicy);

  return delta.newState();
}

template <typename LockPolicyT>
void SaiSwitch::updateResourceUsage(const LockPolicyT& lockPolicy) {
  [[maybe_unused]] const auto& lock = lockPolicy.lock();

  try {
    // TODO - compute used resource stats from internal data structures and
    // populate them here

    /*
     * Loop through all tables and sum the resource used. This way,
     * irrespective of however many acl tables there are in the config,
     * the resource will be calculated properly
     */
    auto aclResourceUsage =
        managerTable_->aclTableManager().getAclResourceUsage();
    hwResourceStats_.acl_entries_free() = aclResourceUsage.first;
    hwResourceStats_.acl_counters_free() = aclResourceUsage.second;

    auto& switchApi = SaiApiTable::getInstance()->switchApi();
    hwResourceStats_.lpm_ipv4_free() = switchApi.getAttribute(
        saiSwitchId_, SaiSwitchTraits::Attributes::AvailableIpv4RouteEntry{});
    hwResourceStats_.lpm_ipv6_free() = switchApi.getAttribute(
        saiSwitchId_, SaiSwitchTraits::Attributes::AvailableIpv6RouteEntry{});
    hwResourceStats_.l3_ipv4_nexthops_free() = switchApi.getAttribute(
        saiSwitchId_, SaiSwitchTraits::Attributes::AvailableIpv4NextHopEntry{});
    hwResourceStats_.l3_ipv6_nexthops_free() = switchApi.getAttribute(
        saiSwitchId_, SaiSwitchTraits::Attributes::AvailableIpv6NextHopEntry{});
    hwResourceStats_.l3_ecmp_groups_free() = switchApi.getAttribute(
        saiSwitchId_,
        SaiSwitchTraits::Attributes::AvailableNextHopGroupEntry{});
    hwResourceStats_.l3_ecmp_group_members_free() = switchApi.getAttribute(
        saiSwitchId_,
        SaiSwitchTraits::Attributes::AvailableNextHopGroupMemberEntry{});
    hwResourceStats_.l3_ipv4_host_free() = switchApi.getAttribute(
        saiSwitchId_,
        SaiSwitchTraits::Attributes::AvailableIpv4NeighborEntry{});
    hwResourceStats_.l3_ipv6_host_free() = switchApi.getAttribute(
        saiSwitchId_,
        SaiSwitchTraits::Attributes::AvailableIpv6NeighborEntry{});
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
    // AvailableMySidEntry is only queryable when the SDK is initialized with
    // mySid stat support (sai_stats_support), which is config dependent and
    // independent of the static ASIC capability. Gate on FLAGS_srv6 so
    // non-SRv6 configs do not issue a get that would mark all resource stats
    // stale.
    if (FLAGS_srv6 &&
        platform_->getAsic()->isSupported(
            HwAsic::Feature::SRV6_MYSID_RESOURCE_COUNTER)) {
      hwResourceStats_.my_sid_entries_free() = switchApi.getAttribute(
          saiSwitchId_, SaiSwitchTraits::Attributes::AvailableMySidEntry{});
    }
#endif
    if (getSwitchType() == cfg::SwitchType::VOQ) {
      uint64_t sysPortsFree, voqsFree;
      saiCheckError(sai_object_type_get_availability(
          saiSwitchId_,
          SAI_OBJECT_TYPE_SYSTEM_PORT,
          0,
          nullptr,
          &sysPortsFree));
      hwResourceStats_.system_ports_free() = sysPortsFree;
      std::array<sai_attribute_t, 1> attr;
      attr[0].id = SAI_QUEUE_ATTR_TYPE;
      attr[0].value.u32 = SAI_QUEUE_TYPE_ALL;
      sai_object_type_get_availability(
          saiSwitchId_, SAI_OBJECT_TYPE_QUEUE, 1, attr.data(), &voqsFree);
      hwResourceStats_.voqs_free() = voqsFree;
    }
    hwResourceStats_.hw_table_stats_stale() = false;
  } catch (const SaiApiError& e) {
    XLOG(ERR) << " Failed to get resource usage hwResourceStats_: "
              << *e.message();
    hwResourceStats_.hw_table_stats_stale() = true;
  }
  // update stats for mirrors
  hwResourceStats_.mirrors_span() =
      managerTable_->mirrorManager().getMirrorsCount<SaiLocalMirrorTraits>();
  hwResourceStats_.mirrors_erspan() =
      managerTable_->mirrorManager()
          .getMirrorsCount<SaiEnhancedRemoteMirrorTraits>();
  hwResourceStats_.mirrors_sflow() =
      managerTable_->mirrorManager().getMirrorsCount<SaiSflowMirrorTraits>();
  hwResourceStats_.mirrors_max() = getPlatform()->getAsic()->getMaxMirrors();
  hwResourceStats_.mirrors_used() = *hwResourceStats_.mirrors_erspan() +
      *hwResourceStats_.mirrors_span() + *hwResourceStats_.mirrors_sflow();
  hwResourceStats_.mirrors_free() =
      *hwResourceStats_.mirrors_max() - *hwResourceStats_.mirrors_used();
}

void SaiSwitch::processSwitchSettingsDrainStateChangeLocked(
    const std::lock_guard<std::mutex>& lock,
    cfg::SwitchDrainState drainStateToProcess,
    const StateDelta& delta) {
  const auto switchSettingDelta = delta.getSwitchSettingsDelta();
  DeltaFunctions::forEachAdded(
      switchSettingDelta, [&](const auto& newSwitchSettings) {
        processSwitchSettingsChangeDrainedEntryLocked(
            lock,
            drainStateToProcess,
            std::make_shared<SwitchSettings>(),
            newSwitchSettings);
      });
  DeltaFunctions::forEachChanged(
      switchSettingDelta,
      [&](const auto& oldSwitchSettings, const auto& newSwitchSettings) {
        processSwitchSettingsChangeDrainedEntryLocked(
            lock, drainStateToProcess, oldSwitchSettings, newSwitchSettings);
      });
  DeltaFunctions::forEachRemoved(
      switchSettingDelta, [&](const auto& oldSwitchSettings) {
        processSwitchSettingsChangeDrainedEntryLocked(
            lock,
            drainStateToProcess,
            oldSwitchSettings,
            std::make_shared<SwitchSettings>());
      });
}

void SaiSwitch::processSwitchSettingsChangeDrainedEntryLocked(
    const std::lock_guard<std::mutex>& lock,
    cfg::SwitchDrainState drainStateToProcess,
    const std::shared_ptr<SwitchSettings>& oldSwitchSettings,
    const std::shared_ptr<SwitchSettings>& newSwitchSettings) {
  const auto oldVal = oldSwitchSettings->isSwitchDrained();
  const auto newVal = newSwitchSettings->isSwitchDrained();
  cfg::SwitchDrainState newDrainState = newVal
      ? cfg::SwitchDrainState::DRAINED
      : cfg::SwitchDrainState::UNDRAINED;
  if (oldVal != newVal && newDrainState == drainStateToProcess) {
    managerTable_->switchManager().setSwitchIsolate(newVal);
  }
}

void SaiSwitch::processSwitchSettingsChangeSansDrainedLocked(
    const std::lock_guard<std::mutex>& lock,
    const StateDelta& delta) {
  const auto switchSettingDelta = delta.getSwitchSettingsDelta();
  DeltaFunctions::forEachAdded(
      switchSettingDelta, [&](const auto& newSwitchSettings) {
        processSwitchSettingsChangeSansDrainedEntryLocked(
            lock, std::make_shared<SwitchSettings>(), newSwitchSettings);
      });
  DeltaFunctions::forEachChanged(
      switchSettingDelta,
      [&](const auto& oldSwitchSettings, const auto& newSwitchSettings) {
        processSwitchSettingsChangeSansDrainedEntryLocked(
            lock, oldSwitchSettings, newSwitchSettings);
      });
  DeltaFunctions::forEachRemoved(
      switchSettingDelta, [&](const auto& oldSwitchSettings) {
        processSwitchSettingsChangeSansDrainedEntryLocked(
            lock, oldSwitchSettings, std::make_shared<SwitchSettings>());
      });
}

void SaiSwitch::processSwitchSettingsChangeSansDrainedEntryLocked(
    const std::lock_guard<std::mutex>& /*lock*/,
    const std::shared_ptr<SwitchSettings>& oldSwitchSettings,
    const std::shared_ptr<SwitchSettings>& newSwitchSettings) {
  if (oldSwitchSettings->getL2LearningMode() !=
      newSwitchSettings->getL2LearningMode()) {
    XLOG(DBG3) << "Configuring L2LearningMode old: "
               << static_cast<int>(oldSwitchSettings->getL2LearningMode())
               << " new: "
               << static_cast<int>(newSwitchSettings->getL2LearningMode());
    managerTable_->bridgeManager().setL2LearningMode(
        newSwitchSettings->getL2LearningMode());
  }

  {
    const auto oldVal = oldSwitchSettings->getL2AgeTimerSeconds();
    const auto newVal = newSwitchSettings->getL2AgeTimerSeconds();
    if (oldVal != newVal) {
      XLOG(DBG3) << "Configuring l2AgeTimerSeconds old: "
                 << static_cast<int>(oldVal)
                 << " new: " << static_cast<int>(newVal);
      managerTable_->switchManager().setMacAgingSeconds(newVal);
    }
  }

  {
    const auto oldVal = oldSwitchSettings->isPtpTcEnable();
    const auto newVal = newSwitchSettings->isPtpTcEnable();
    if (oldVal != newVal) {
      XLOG(DBG3) << "Configuring ptpTcEnable old: " << oldVal
                 << " new: " << newVal;
      // cache the new status, used if the ports are not added yet
      managerTable_->switchManager().setPtpTcEnabled(newVal);
      // update already added ports
      managerTable_->portManager().setPtpTcEnable(newVal);
    }
  }

  {
    const auto oldVal = oldSwitchSettings->getMyMacs();
    const auto newVal = newSwitchSettings->getMyMacs();
    if (oldVal != newVal) {
      XLOG(DBG2) << "Configuring myMacs, num entries old: " << oldVal.size()
                 << " new: " << newVal.size();
      managerTable_->myMacManager().programMyMacs(newVal);
    }
  }

  {
    const auto oldVal = oldSwitchSettings->getEcmpGroupSettings();
    const auto newVal = newSwitchSettings->getEcmpGroupSettings();
    if (oldVal != newVal) {
      XLOG(DBG3) << "ecmpGroupSettings changed, updating next hop groups";
      managerTable_->nextHopGroupManager().setEcmpGroupSettings(newVal);
    }
  }

  {
    const auto oldMode = oldSwitchSettings->getPacketForwardingMode();
    const auto newMode = newSwitchSettings->getPacketForwardingMode();
    if (oldMode != newMode && newMode.has_value()) {
      XLOG(DBG3) << "Configuring packetForwardingMode old: "
                 << (oldMode.has_value()
                         ? apache::thrift::util::enumNameSafe(*oldMode)
                         : "none")
                 << " new: " << apache::thrift::util::enumNameSafe(*newMode);
      managerTable_->switchManager().setSwitchingMode(*newMode);
    }
  }

  {
    const auto oldVal = oldSwitchSettings->getForceTrafficOverFabric();
    const auto newVal = newSwitchSettings->getForceTrafficOverFabric();
    if (oldVal != newVal) {
      managerTable_->switchManager().setForceTrafficOverFabric(
          newVal.has_value() ? newVal.value() : false);
    }
  }

  {
    const auto oldVal = oldSwitchSettings->getCreditWatchdog();
    const auto newVal = newSwitchSettings->getCreditWatchdog();
    if (oldVal != newVal) {
      managerTable_->switchManager().setCreditWatchdog(
          newVal.has_value() ? newVal.value() : false);
    }
  }

  {
    auto oldVal = oldSwitchSettings->getReachabilityGroups();
    auto newVal = newSwitchSettings->getReachabilityGroups();
    if (oldVal != newVal) {
      managerTable_->switchManager().setReachabilityGroupList(newVal);
    }
  }

  {
    const auto oldSramGlobalXoffTh =
        oldSwitchSettings->getSramGlobalFreePercentXoffThreshold();
    const auto newSramGlobalXoffTh =
        newSwitchSettings->getSramGlobalFreePercentXoffThreshold();
    if (oldSramGlobalXoffTh != newSramGlobalXoffTh) {
      managerTable_->switchManager().setSramGlobalFreePercentXoffTh(
          newSramGlobalXoffTh.value_or(0));
    }

    const auto oldSramGlobalXonTh =
        oldSwitchSettings->getSramGlobalFreePercentXonThreshold();
    const auto newSramGlobalXonTh =
        newSwitchSettings->getSramGlobalFreePercentXonThreshold();
    if (oldSramGlobalXonTh != newSramGlobalXonTh) {
      managerTable_->switchManager().setSramGlobalFreePercentXonTh(
          newSramGlobalXonTh.value_or(0));
    }
  }
  {
    const auto oldLinkFlowControlCreditTh =
        oldSwitchSettings->getLinkFlowControlCreditThreshold();
    const auto newLinkFlowControlCreditTh =
        newSwitchSettings->getLinkFlowControlCreditThreshold();
    if (oldLinkFlowControlCreditTh != newLinkFlowControlCreditTh) {
      managerTable_->switchManager().setLinkFlowControlCreditTh(
          newLinkFlowControlCreditTh.value_or(0));
    }
  }
  {
    const auto oldVoqDramBoundTh =
        oldSwitchSettings->getVoqDramBoundThreshold();
    const auto newVoqDramBoundTh =
        newSwitchSettings->getVoqDramBoundThreshold();
    if (oldVoqDramBoundTh != newVoqDramBoundTh) {
      managerTable_->switchManager().setVoqDramBoundTh(
          newVoqDramBoundTh.value_or(0));
    }
  }

  {
    const auto oldConditionalEntropyRehashPeriodUS =
        oldSwitchSettings->getConditionalEntropyRehashPeriodUS();
    const auto newConditionalEntropyRehashPeriodUS =
        newSwitchSettings->getConditionalEntropyRehashPeriodUS();
    if (oldConditionalEntropyRehashPeriodUS !=
        newConditionalEntropyRehashPeriodUS) {
      managerTable_->switchManager().setConditionalEntropyRehashPeriodUS(
          newConditionalEntropyRehashPeriodUS.value_or(0));
    }
  }

  {
    const auto oldShelConfig = oldSwitchSettings->getSelfHealingEcmpLagConfig();
    const auto newShelConfig = newSwitchSettings->getSelfHealingEcmpLagConfig();
    if (oldShelConfig != newShelConfig) {
      managerTable_->switchManager().setShelConfig(newShelConfig);
    }
  }

  {
    const auto oldLocalVoqMaxExpectedLatencyNs =
        oldSwitchSettings->getLocalVoqMaxExpectedLatencyNsec();
    const auto newLocalVoqMaxExpectedLatencyNs =
        newSwitchSettings->getLocalVoqMaxExpectedLatencyNsec();
    if (oldLocalVoqMaxExpectedLatencyNs != newLocalVoqMaxExpectedLatencyNs) {
      managerTable_->switchManager().setLocalVoqMaxExpectedLatency(
          newLocalVoqMaxExpectedLatencyNs.value_or(0));
    }
  }

  {
    const auto oldRemoteL1VoqMaxExpectedLatencyNs =
        oldSwitchSettings->getRemoteL1VoqMaxExpectedLatencyNsec();
    const auto newRemoteL1VoqMaxExpectedLatencyNs =
        newSwitchSettings->getRemoteL1VoqMaxExpectedLatencyNsec();
    if (oldRemoteL1VoqMaxExpectedLatencyNs !=
        newRemoteL1VoqMaxExpectedLatencyNs) {
      managerTable_->switchManager().setRemoteL1VoqMaxExpectedLatency(
          newRemoteL1VoqMaxExpectedLatencyNs.value_or(0));
    }
  }

  {
    const auto oldRemoteL2VoqMaxExpectedLatencyNs =
        oldSwitchSettings->getRemoteL2VoqMaxExpectedLatencyNsec();
    const auto newRemoteL2VoqMaxExpectedLatencyNs =
        newSwitchSettings->getRemoteL2VoqMaxExpectedLatencyNsec();
    if (oldRemoteL2VoqMaxExpectedLatencyNs !=
        newRemoteL2VoqMaxExpectedLatencyNs) {
      managerTable_->switchManager().setRemoteL2VoqMaxExpectedLatency(
          newRemoteL2VoqMaxExpectedLatencyNs.value_or(0));
    }
  }

  {
    const auto oldVoqOutOfBoundsLatencyNs =
        oldSwitchSettings->getVoqOutOfBoundsLatencyNsec();
    const auto newVoqOutOfBoundsLatencyNs =
        newSwitchSettings->getVoqOutOfBoundsLatencyNsec();
    if (oldVoqOutOfBoundsLatencyNs != newVoqOutOfBoundsLatencyNs) {
      managerTable_->switchManager().setVoqOutOfBoundsLatency(
          newVoqOutOfBoundsLatencyNs.value_or(0));
    }
  }

  {
    const auto oldTcToRateLimitKbps = oldSwitchSettings->getTcToRateLimitKbps();
    const auto newTcToRateLimitKbps = newSwitchSettings->getTcToRateLimitKbps();
    if (oldTcToRateLimitKbps != newTcToRateLimitKbps) {
      managerTable_->switchManager().setTcRateLimitList(newTcToRateLimitKbps);
    }
  }

  {
    const auto oldPfcWatchdogTimerGranularity =
        oldSwitchSettings->getPfcWatchdogTimerGranularity();
    const auto newPfcWatchdogTimerGranularity =
        newSwitchSettings->getPfcWatchdogTimerGranularity();
    if (oldPfcWatchdogTimerGranularity != newPfcWatchdogTimerGranularity &&
        newPfcWatchdogTimerGranularity.has_value()) {
      managerTable_->switchManager().setPfcWatchdogTimerGranularity(
          *newPfcWatchdogTimerGranularity);
    }
  }

  {
    const auto oldFabricLinkMonitoringSystemPortOffset =
        oldSwitchSettings->getFabricLinkMonitoringSystemPortOffset();
    const auto newFabricLinkMonitoringSystemPortOffset =
        newSwitchSettings->getFabricLinkMonitoringSystemPortOffset();
    if (oldFabricLinkMonitoringSystemPortOffset !=
        newFabricLinkMonitoringSystemPortOffset) {
      managerTable_->systemPortManager()
          .setFabricLinkMonitoringSystemPortOffset(
              newFabricLinkMonitoringSystemPortOffset);
    }
  }
}

template <typename LockPolicyT>
void SaiSwitch::processSwitchSettingsChangeSansDrained(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  const auto switchSettingsDelta = delta.getSwitchSettingsDelta();
  const auto& oldSwitchSettings = switchSettingsDelta.getOld();
  const auto& newSwitchSettings = switchSettingsDelta.getNew();

  /*
   * SwitchSettings are mandatory and can thus only be modified.
   * Every field in SwitchSettings must always be set in new SwitchState.
   */
  CHECK(oldSwitchSettings);
  CHECK(newSwitchSettings);

  if (oldSwitchSettings != newSwitchSettings) {
    processSwitchSettingsChangeSansDrainedLocked(lockPolicy.lock(), delta);
  }
}

template <typename LockPolicyT>
void SaiSwitch::processSwitchSettingsDrainStateChange(
    const StateDelta& delta,
    cfg::SwitchDrainState drainStateToProcess,
    const LockPolicyT& lockPolicy) {
  const auto switchSettingsDelta = delta.getSwitchSettingsDelta();
  const auto& oldSwitchSettings = switchSettingsDelta.getOld();
  const auto& newSwitchSettings = switchSettingsDelta.getNew();

  /*
   * SwitchSettings are mandatory and can thus only be modified.
   * Every field in SwitchSettings must always be set in new SwitchState.
   */
  CHECK(oldSwitchSettings);
  CHECK(newSwitchSettings);

  if (oldSwitchSettings != newSwitchSettings) {
    processSwitchSettingsDrainStateChangeLocked(
        lockPolicy.lock(), drainStateToProcess, delta);
  }
}

void SaiSwitch::processPortStateChangedForSwitchReachabilityLocked(
    const std::lock_guard<std::mutex>& /*lock*/,
    const StateDelta& delta) {
  DeltaFunctions::forEachChanged(
      delta.getPortsDelta(), [&](const auto& oldPort, const auto& newPort) {
        if ((newPort->getPortType() == cfg::PortType::FABRIC_PORT) &&
            (newPort->getActiveState() != oldPort->getActiveState())) {
          // Forcing a switch reachability get from SAI/SDK once the
          // port active state change handling is complete to ensure
          // the switch reachability data is updated. This is needed
          // as there are some cases like the one captured in
          // CS00012399424, where a rechability change notification
          // is not sent to NOS when port active state changes.
          XLOG(DBG2) << "Port active state change on " << newPort->getName()
                     << " triggering switch reachability get!";
          setSwitchReachabilityChangePending();
          return;
        }
      });
}

template <typename LockPolicyT>
void SaiSwitch::processPortStateChangedForSwitchReachability(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  const auto portsDelta = delta.getPortsDelta();
  const auto& oldPorts = portsDelta.getOld();
  const auto& newPorts = portsDelta.getNew();
  if (oldPorts != newPorts) {
    processPortStateChangedForSwitchReachabilityLocked(
        lockPolicy.lock(), delta);
  }
}

bool SaiSwitch::isValidStateUpdate(const StateDelta& delta) const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return isValidStateUpdateLocked(lock, delta);
}

std::unique_ptr<TxPacket> SaiSwitch::allocatePacket(uint32_t size) const {
  getSwitchStats()->txPktAlloc();
  return std::make_unique<SaiTxPacket>(size);
}

bool SaiSwitch::sendPacketSwitchedAsync(
    std::unique_ptr<TxPacket> pkt) noexcept {
  return sendPacketSwitchedSync(std::move(pkt));
}

bool SaiSwitch::sendPacketOutOfPortAsync(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queueId) noexcept {
  return sendPacketOutOfPortSync(std::move(pkt), portID, queueId);
}

uint64_t SaiSwitch::getDeviceWatermarkBytes() const {
  std::lock_guard<std::mutex> locked(saiSwitchMutex_);
  return getDeviceWatermarkBytesLocked(locked);
}

uint64_t SaiSwitch::getDeviceWatermarkBytesLocked(
    const std::lock_guard<std::mutex>& /*lock*/) const {
  return managerTable_->bufferManager().getDeviceWatermarkBytes();
}

folly::F14FastMap<std::string, HwPortStats> SaiSwitch::getPortStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getPortStatsLocked(lock);
}

std::map<std::string, HwTrunkStats> SaiSwitch::getTrunkStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTable_->lagManager().getAllHwTrunkStats();
}

CpuPortStats SaiSwitch::getCpuPortStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::CPU_PORT)) {
    return CpuPortStats{};
  }
  auto& cpuStat = managerTable_->hostifManager().getCpuFb303Stats();
  auto cpuPortStats = cpuStat.getCpuPortStats();
  cpuPortStats.portStats_() = managerTable_->hostifManager().getCpuPortStats();
  return cpuPortStats;
}

folly::F14FastMap<std::string, HwPortStats> SaiSwitch::getPortStatsLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {
  folly::F14FastMap<std::string, HwPortStats> portStatsMap;
  auto& portIdStatsMap = managerTable_->portManager().getLastPortStats();
  for (auto& entry : portIdStatsMap) {
    portStatsMap.emplace(entry.second->portName(), entry.second->portStats());
  }
  return portStatsMap;
}

std::map<std::string, HwSysPortStats> SaiSwitch::getSysPortStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getSysPortStatsLocked(lock);
}

FabricReachabilityStats SaiSwitch::getFabricReachabilityStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getSwitchStats()->getFabricReachabilityStats();
}

std::map<std::string, HwSysPortStats> SaiSwitch::getSysPortStatsLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {
  std::map<std::string, HwSysPortStats> portStatsMap;
  auto& portIdStatsMap = managerTable_->systemPortManager().getLastPortStats();
  for (auto& entry : portIdStatsMap) {
    portStatsMap.emplace(entry.second->portName(), entry.second->portStats());
  }
  auto& cpuSysPortStats =
      managerTable_->hostifManager().getCpuSysPortFb303Stats();
  portStatsMap.emplace(cpuSysPortStats.portName(), cpuSysPortStats.portStats());
  return portStatsMap;
}

folly::F14FastMap<std::string, HwRouterInterfaceStats>
SaiSwitch::getRouterInterfaceStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getRouterInterfaceStatsLocked(lock);
}

folly::F14FastMap<std::string, HwRouterInterfaceStats>
SaiSwitch::getRouterInterfaceStatsLocked(
    const std::lock_guard<std::mutex>& /*lock*/) const {
  folly::F14FastMap<std::string, HwRouterInterfaceStats> rifStatsMap;
  const auto& statsMap =
      managerTable_->routerInterfaceManager().getRouterInterfaceStats();
  auto state = getProgrammedState();
  for (const auto& entry : statsMap) {
    auto intf = state->getInterfaces()->getNodeIf(entry.first);
    if (!intf) {
      // rif will get added into sai switch but programmed state is reflecting
      // old state until the entire switch state is fully applied.
      XLOG(WARNING) << "interface with id " << entry.first
                    << " not yet in programmed state";
      continue;
    }
    rifStatsMap.emplace(intf->getName(), entry.second);
  }
  return rifStatsMap;
}

std::map<PortID, phy::PhyInfo> SaiSwitch::updateAllPhyInfoImpl() {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return updateAllPhyInfoLocked();
}

std::map<int, cfg::PortState> SaiSwitch::getSysPortShelState() const {
  std::map<int, cfg::PortState> sysPortShelState;
  for (const auto& [sysPortId, portState] :
       std::as_const(concurrentIndices().sysPortShelState)) {
    sysPortShelState[static_cast<int>(sysPortId)] = portState;
  }
  return sysPortShelState;
}

std::map<PortID, phy::PhyInfo> SaiSwitch::updateAllPhyInfoLocked() {
  std::map<PortID, phy::PhyInfo> returnPhyParams;
  auto& portManager = managerTable_->portManager();
  bool readSerdesParams = false;
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  if ((now.count() - lastSerdesParamsReadTime_) >=
      FLAGS_serdes_params_poll_interval_s) {
    readSerdesParams = true;
    lastSerdesParamsReadTime_ = now.count();
  }

  for (const auto& portIdAndHandle : managerTable_->portManager()) {
    PortID portID = portIdAndHandle.first;
    if (portManager.getPortType(portID) == cfg::PortType::INTERFACE_PORT ||
        portManager.getPortType(portID) == cfg::PortType::FABRIC_PORT ||
        portManager.getPortType(portID) == cfg::PortType::MANAGEMENT_PORT ||
        portManager.getPortType(portID) == cfg::PortType::HYPER_PORT_MEMBER) {
      auto portHandle = portIdAndHandle.second.get();
      if (portHandle == nullptr) {
        XLOG(DBG3) << "PortHandle not found for port "
                   << static_cast<int>(portID);
        continue;
      }

      auto fb303PortStat = portManager.getLastPortStat(portID);
      if (fb303PortStat == nullptr) {
        XLOG(DBG3) << "fb303PortStat not found for port "
                   << static_cast<int>(portID);
        continue;
      }

      phy::PhyInfo lastPhyInfo;
      if (auto itr = lastPhyInfos_.find(portID); itr != lastPhyInfos_.end()) {
        lastPhyInfo = itr->second;
      }

      phy::PhyInfo phyParams;
      phyParams.state() = phy::PhyState();
      phyParams.stats() = phy::PhyStats();
      // LINE Side always exists
      phyParams.state()->line()->side() = phy::Side::LINE;
      phyParams.stats()->line()->side() = phy::Side::LINE;

      phyParams.state()->name() = fb303PortStat->portName();
      phyParams.state()->switchID() = getSaiSwitchId();
      // Global phy parameters
      phy::DataPlanePhyChip phyChip;
      auto chipType = getPlatform()->getAsic()->getDataPlanePhyChipType();
      phyChip.type() = chipType;
      bool isXphy = *phyChip.type() == phy::DataPlanePhyChipType::XPHY;
      phyParams.state()->phyChip() = phyChip;
      phyParams.state()->linkState() = portManager.isPortUp(portID);
      phyParams.state()->speed() = portManager.getSpeed(portID);

      if (isXphy) {
        phyParams.state()->system() = phy::PhySideState();
        phyParams.state()->system()->side() = phy::Side::SYSTEM;
        phyParams.stats()->system() = phy::PhySideStats();
        phyParams.stats()->system()->side() = phy::Side::SYSTEM;
      }

      phyParams.state()->line()->interfaceType() =
          getInterfaceType(portID, chipType);
      phyParams.state()->line()->medium() = portManager.getMedium(portID);
      // Update PMD Info
      phy::PmdState lastLinePmdState;
      auto lastState = lastPhyInfo.state();
      lastLinePmdState = *lastState->line()->pmd();
      phy::PmdStats lastLinePmdStats;
      auto lastStats = lastPhyInfo.stats();
      lastLinePmdStats = *lastStats->line()->pmd();
      updatePmdInfo(
          *phyParams.state()->line(),
          *phyParams.stats()->line(),
          portHandle->port,
          portHandle->serdes,
          lastLinePmdState,
          lastLinePmdStats,
          portID,
          readSerdesParams);
      if (isXphy) {
        CHECK(phyParams.state()->system().has_value());
        CHECK(phyParams.stats()->system().has_value());
        phy::PmdState lastSysPmdState;
        phy::PmdStats lastSysPmdStats;
        if (lastPhyInfo.state()->system().has_value()) {
          lastSysPmdState = *lastPhyInfo.state()->system()->pmd();
        }
        if (lastPhyInfo.stats()->system().has_value()) {
          lastSysPmdStats = *lastPhyInfo.stats()->system()->pmd();
        }
        updatePmdInfo(
            *phyParams.state()->system(),
            *phyParams.stats()->system(),
            portHandle->sysPort,
            portHandle->sysSerdes,
            lastSysPmdState,
            lastSysPmdStats,
            portID,
            readSerdesParams);
      }

#if defined(BRCM_PAI_SDK) && SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      // Only read on the PAI retimer. NPU / non-retimer platforms are
      // unaffected: no extra per-port SAI read and no change to their PhyInfo.
      // system() is populated above under the same isXphy condition.
      if (isXphy) {
        auto& phyPortMgr = managerTable_->portManager();
        phyParams.state()->line()->loopback() =
            phyPortMgr.getLoopbackMode(portHandle->port->adapterKey());
        if (portHandle->sysPort) {
          phyParams.state()->system()->loopback() =
              phyPortMgr.getLoopbackMode(portHandle->sysPort->adapterKey());
        }
      }
#endif

      // Update PCS Info
      updatePcsInfo(
          *(*phyParams.state()).line(),
          *(*phyParams.stats()).line(),
          portID,
          phy::Side::LINE,
          lastPhyInfo,
          fb303PortStat,
          *phyParams.state()->speed(),
          portHandle->port);

      // Update Reconciliation Sublayer (RS) Info
      updateRsInfo(
          *phyParams.state()->line(),
          portHandle->port,
          portID,
          *lastPhyInfo.state()->line());

      // PhyInfo update timestamp
      phyParams.state()->timeCollected() = now.count();
      phyParams.stats()->timeCollected() = now.count();
      returnPhyParams[portID] = phyParams;
    }
  }
  lastPhyInfos_ = returnPhyParams;
  return returnPhyParams;
}

void SaiSwitch::updatePmdInfo(
    phy::PhySideState& sideState,
    phy::PhySideStats& sideStats,
    std::shared_ptr<SaiPort> port,
    std::shared_ptr<SaiPortSerdes> serdes,
    [[maybe_unused]] phy::PmdState& lastPmdState,
    [[maybe_unused]] phy::PmdStats& lastPmdStats,
    [[maybe_unused]] PortID portID,
    bool readSerdesParams) {
  std::vector<uint32_t> pmdLanes;
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_PORT_GET_PMD_LANES)) {
    // HwLaneList might mean physical port list instead of pmd lane list on
    // TH4 So, use getPmdLaneList() to get the pmd lanes
    pmdLanes = managerTable_->portManager().getPmdLaneList(port->adapterKey());
  } else {
    pmdLanes = GET_ATTR(Port, HwLaneList, port->attributes());
  }
  uint32_t numPmdLanes = pmdLanes.size();
  if (!numPmdLanes) {
    return;
  }

  std::map<int, phy::LaneStats> laneStats;
  std::map<int, phy::LaneState> laneStates;
  auto eyeStatus =
      managerTable_->portManager().getPortEyeValues(port->adapterKey());
  // Collect eyeInfos for all lanes
  std::map<int, std::vector<phy::EyeInfo>> eyeInfos;
  for (int i = 0; i < eyeStatus.size(); i++) {
    std::vector<phy::EyeInfo> eyeInfo;
    phy::EyeInfo oneLaneEyeInfo;
    int width{0}, height{0};
    if (getPlatform()->getAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_ELBERT_8DD) {
      width = eyeStatus[i].right - eyeStatus[i].left;
      height = eyeStatus[i].up - eyeStatus[i].down;
    } else {
      width = eyeStatus[i].right + eyeStatus[i].left;
      height = eyeStatus[i].up + eyeStatus[i].down;
    }
    // Skip if both width and height are 0, it's likely a bad reading or the
    // eye reporting is not supported (On BCM PHYs eye is not supported for
    // PAM4 speeds)
    if (width == 0 && height == 0) {
      continue;
    }
    oneLaneEyeInfo.height() = height;
    oneLaneEyeInfo.width() = width;
    eyeInfos[eyeStatus[i].lane].push_back(oneLaneEyeInfo);
  }

  for (const auto& eyeInfo : eyeInfos) {
    auto laneId = eyeInfo.first;
    phy::LaneStats laneStat;
    if (laneStats.find(laneId) != laneStats.end()) {
      laneStat = laneStats[laneId];
    }

    laneStat.lane() = laneId;
    laneStat.eyes() = eyeInfo.second;
    laneStats[laneId] = laneStat;
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
  auto pmdSignalDetect = managerTable_->portManager().getRxSignalDetect(
      port->adapterKey(), numPmdLanes, portID);
  for (const auto& pmd : pmdSignalDetect) {
    auto laneId = pmd.lane;
    phy::LaneStats laneStat;
    phy::LaneState laneState;
    if (laneStats.find(laneId) != laneStats.end()) {
      laneStat = laneStats[laneId];
    }
    if (laneStates.find(laneId) != laneStates.end()) {
      laneState = laneStates[laneId];
    }
    laneState.lane() = laneId;
    laneStat.lane() = laneId;
    laneState.signalDetectLive() = pmd.value.current_status;
    laneState.signalDetectChanged() = pmd.value.changed;
    utility::updateSignalDetectChangedCount(
        pmd.value.changed, laneId, laneStat, lastPmdStats);
    laneStats[laneId] = laneStat;
    laneStates[laneId] = laneState;
  }

  auto pmdLockStatus = managerTable_->portManager().getRxLockStatus(
      port->adapterKey(), numPmdLanes, portID);
  for (const auto& pmd : pmdLockStatus) {
    auto laneId = pmd.lane;
    phy::LaneStats laneStat;
    phy::LaneState laneState;
    if (laneStats.find(laneId) != laneStats.end()) {
      laneStat = laneStats[laneId];
    }
    if (laneStates.find(laneId) != laneStates.end()) {
      laneState = laneStates[laneId];
    }
    laneState.lane() = laneId;
    laneStat.lane() = laneId;
    laneState.cdrLockLive() = pmd.value.current_status;
    laneState.cdrLockChanged() = pmd.value.changed;
    utility::updateCdrLockChangedCount(
        pmd.value.changed, laneId, laneStat, lastPmdStats);
    laneStats[laneId] = laneStat;
    laneStates[laneId] = laneState;
  }
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  auto pmdRxPPM =
      managerTable_->portManager().getRxPPM(port->adapterKey(), numPmdLanes);
  // In SAI spec, SNR is encoded in units of 1/256 dB
  constexpr auto snrScalingFactor = 256.0;
  for (const auto& pmd : pmdRxPPM) {
    auto laneId = pmd.lane;
    phy::LaneState laneState;
    if (laneStates.find(laneId) != laneStates.end()) {
      laneState = laneStates[laneId];
    }
    laneState.lane() = laneId;
    laneState.rxFrequencyPPM() = pmd.ppm;
    laneStates[laneId] = laneState;
  }

  auto pmdRxSNR = managerTable_->portManager().getRxSNR(
      port->adapterKey(), numPmdLanes, portID);
  for (const auto& pmd : pmdRxSNR) {
    auto laneId = pmd.lane;
    phy::LaneStats laneStat;
    if (laneStats.find(laneId) != laneStats.end()) {
      laneStat = laneStats[laneId];
    }
    laneStat.lane() = laneId;
    laneStat.snr() = pmd.snr / snrScalingFactor;
    laneStats[laneId] = laneStat;
  }
#endif

  // Serdes parameters need BRCM_SAI_SDK_GTE_13_0 plus the RX_SERDES_PARAMETERS
  // feature; where they are unavailable the TX FIR taps still read, but
  // nothing supplies a lane label for them, so fall back to the pmd lane list.
  const bool haveSerdesParams =
      managerTable_->portManager().rxSerdesParametersSupported();
  std::vector<phy::SerdesParameters> pmdSerdesParameters;
  std::vector<phy::TxSettings> pmdTxSettings;
  std::vector<int> txLaneLabels;
  if (readSerdesParams && serdes) {
    if (haveSerdesParams) {
      pmdSerdesParameters = managerTable_->portManager().getSerdesParameters(
          serdes->adapterKey(), portID, numPmdLanes);
    }
    pmdTxSettings = managerTable_->portManager().getTxSettings(
        serdes->adapterKey(), portID, numPmdLanes);
    if (!haveSerdesParams) {
      txLaneLabels.assign(pmdLanes.begin(), pmdLanes.end());
    }
  } else {
    // Use the previous state
    for (const auto& [laneId, laneState] : *lastPmdState.lanes()) {
      if (haveSerdesParams) {
        pmdSerdesParameters.push_back(*laneState.serdesParameters());
        pmdTxSettings.push_back(*laneState.txSettings());
        continue;
      }
      // txSettings is a non-optional field, so dereferencing an unset one
      // yields a zeroed struct rather than throwing; skip lanes never read.
      if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
              laneState.txSettings())) {
        continue;
      }
      pmdTxSettings.push_back(*laneState.txSettings());
      txLaneLabels.push_back(laneId);
    }
  }
  auto numEntries =
      haveSerdesParams ? pmdSerdesParameters.size() : pmdTxSettings.size();
  for (size_t l = 0; l < numEntries; l++) {
    auto laneId =
        haveSerdesParams ? *pmdSerdesParameters[l].lane() : txLaneLabels[l];
    phy::LaneState laneState;
    if (laneStates.find(laneId) != laneStates.end()) {
      laneState = laneStates[laneId];
    }
    laneState.lane() = laneId;
    if (haveSerdesParams) {
      laneState.serdesParameters() = pmdSerdesParameters[l];
    }
    if (l < pmdTxSettings.size()) {
      laneState.txSettings() = pmdTxSettings[l];
    }
    laneStates[laneId] = laneState;
  }
  for (const auto& laneStat : laneStats) {
    sideStats.pmd()->lanes()[laneStat.first] = laneStat.second;
  }
  // The hardware latches these per read, so OR-ing across lanes says whether
  // anything moved on this side since the previous updateAllPhyInfo. This runs
  // once per collection, so each event is published exactly once.
  bool signalDetectChanged = false;
  bool cdrLockChanged = false;
  for (const auto& laneState : laneStates) {
    sideState.pmd()->lanes()[laneState.first] = laneState.second;
    signalDetectChanged |=
        laneState.second.signalDetectChanged().value_or(false);
    cdrLockChanged |= laneState.second.cdrLockChanged().value_or(false);
  }
  managerTable_->portManager().updatePmdChangedFb303Counters(
      portID, *sideState.side(), signalDetectChanged, cdrLockChanged);
  auto swPort = getProgrammedState()->getPorts()->getNodeIf(portID);
  bool linkTrainingEnabled =
      swPort ? swPort->getLinkTraining().value_or(false) : false;
  if (linkTrainingEnabled) {
    sideState.pmd()->linkTrainingStatus() =
        managerTable_->portManager().getLinkTrainingStatus(
            port->adapterKey(), portID, linkTrainingEnabled);
  } else {
    sideState.pmd()->linkTrainingStatus()->linkTrainingEnabled() = false;
  }
}

void SaiSwitch::updatePcsInfo(
    phy::PhySideState& sideState,
    phy::PhySideStats& sideStats,
    PortID swPort,
    phy::Side side,
    phy::PhyInfo& lastPhyInfo,
    const HwPortFb303Stats* fb303PortStat,
    cfg::PortSpeed speed,
    std::shared_ptr<SaiPort> port) {
  auto fecMode = getPortFECMode(swPort);

  phy::PcsState pcsState;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
  if (auto pcsLinkStatus =
          managerTable_->portManager().getPcsRxLinkStatus(port->adapterKey())) {
    pcsState.pcsRxStatusLive() = pcsLinkStatus->current_status;
    pcsState.pcsRxStatusLatched() = pcsLinkStatus->changed;
  }
#endif

  if (utility::isReedSolomonFec(fecMode)) {
    phy::PcsStats pcsStats;
    phy::RsFecInfo rsFec;

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
    auto fecLanes = utility::reedSolomonFecLanes(speed);
    auto fecAmLock = managerTable_->portManager().getFecAlignmentLockStatus(
        port->adapterKey(), fecLanes);
    phy::RsFecState fecState;
    for (const auto& fecAm : fecAmLock) {
      // SDKs sometimes return data for more FEC lanes than the FEC block on
      // the port actually uses
      if (fecAm.lane < fecLanes) {
        phy::RsFecLaneState fecLaneState;
        fecLaneState.lane() = fecAm.lane;
        fecLaneState.fecAlignmentLockLive() = fecAm.value.current_status;
        fecLaneState.fecAlignmentLockChanged() = fecAm.value.changed;

        fecState.lanes()[fecAm.lane] = fecLaneState;
      }
    }
    pcsState.rsFecState() = fecState;
#endif

    rsFec.correctedCodewords() =
        *(fb303PortStat->portStats().fecCorrectableErrors());
    rsFec.uncorrectedCodewords() =
        *(fb303PortStat->portStats().fecUncorrectableErrors());
    rsFec.codewordStats() = *(fb303PortStat->portStats().fecCodewords_());

    phy::RsFecInfo lastRsFec;
    std::optional<phy::PcsStats> lastPcs;
    if (side == phy::Side::LINE) {
      if (auto pcs = lastPhyInfo.stats()->line()->pcs()) {
        lastPcs = *pcs;
      }
    } else if (auto sysSide = lastPhyInfo.stats()->system()) {
      if (sysSide->pcs().has_value()) {
        lastPcs = *sysSide->pcs();
      }
    }
    if (lastPcs.has_value() && lastPcs->rsFec().has_value()) {
      lastRsFec = *lastPcs->rsFec();
    }

    std::optional<uint64_t> correctedBitsFromHw = std::nullopt;
    std::optional<uint64_t> correctedSymbolsFromHw = std::nullopt;
    if (managerTable_->portManager().fecCorrectedBitsSupported(swPort)) {
      auto correctedBits = *(fb303PortStat->portStats().fecCorrectedBits_());
      if (correctedBits != hardware_stats_constants::STAT_UNINITIALIZED()) {
        correctedBitsFromHw = correctedBits;
      }
    }
    if (managerTable_->portManager().fecCorrectedSymbolsSupported(swPort)) {
      auto correctedSymbols =
          *(fb303PortStat->portStats().fecCorrectedSymbols_());
      if (correctedSymbols != hardware_stats_constants::STAT_UNINITIALIZED()) {
        correctedSymbolsFromHw = correctedSymbols;
      }
    }

    auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
    utility::updateCorrectedBitsAndPreFECBer(
        rsFec, /* current RsFecInfo to update */
        lastRsFec, /* previous RsFecInfo */
        correctedBitsFromHw, /* correctedBitsFromHw */
        correctedSymbolsFromHw, /* correctedSymbolsFromHw */
        now.count() -
            *lastPhyInfo.state()->timeCollected(), /* timeDeltaInSeconds */
        fecMode, /* operational FecMode */
        speed /* operational Speed */);
    utility::updateFecTail(
        rsFec, /* current RsFecInfo to update */
        lastRsFec, /* previous RsFecInfo */
        fecMode);
    pcsStats.rsFec() = rsFec;
    sideStats.pcs() = pcsStats;
  }

  sideState.pcs() = pcsState;
}

void SaiSwitch::updateRsInfo(
    phy::PhySideState& sideState,
    std::shared_ptr<SaiPort> port,
    PortID swPort,
    phy::PhySideState& lastState) {
  bool rsInfoSupported =
      platform_->getAsic()->isSupported(HwAsic::Feature::SAI_PORT_ERR_STATUS);
  auto errStatus =
      managerTable_->portManager().getPortErrStatus(port->adapterKey());
  phy::LinkFaultStatus faultStatus;
  for (auto& err : errStatus) {
    switch (err) {
      case SAI_PORT_ERR_STATUS_SIGNAL_LOCAL_ERROR:
        faultStatus.localFault() = true;
        break;
      case SAI_PORT_ERR_STATUS_REMOTE_FAULT_STATUS:
        faultStatus.remoteFault() = true;
        break;
      default:
        break;
    }
  }

  // Diff against the previous sample so a fault that asserts and clears
  // between two reads of PhyInfo is still observable. The first sample for a
  // port has nothing to compare against and reports no change.
  bool localFaultChanged = false;
  bool remoteFaultChanged = false;
  if (lastState.rs().has_value()) {
    const auto& lastFaultStatus = *lastState.rs()->faultStatus();
    localFaultChanged =
        *lastFaultStatus.localFault() != *faultStatus.localFault();
    remoteFaultChanged =
        *lastFaultStatus.remoteFault() != *faultStatus.remoteFault();
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  if (auto highCrcErrorRate = managerTable_->portManager().getHighCrcErrorRate(
          port->adapterKey(), swPort)) {
    rsInfoSupported = true;
    faultStatus.highCrcErrorRateLive() = highCrcErrorRate->current_status;
    if (highCrcErrorRate->changed) {
      if (lastState.rs().has_value()) {
        faultStatus.highCrcErrorRateChangedCount() =
            lastState.rs()
                ->faultStatus()
                ->highCrcErrorRateChangedCount()
                .value() +
            1;
      } else {
        faultStatus.highCrcErrorRateChangedCount() = 1;
      }
      managerTable_->portManager().updateLeakyBucketFb303Counter(
          swPort, *faultStatus.highCrcErrorRateChangedCount());
    }
  }
#endif

  if (rsInfoSupported) {
    phy::RsInfo rsInfo;
    rsInfo.faultStatus() = faultStatus;
    sideState.rs() = rsInfo;
    managerTable_->portManager().updateLinkFaultChangedFb303Counters(
        swPort, *sideState.side(), localFaultChanged, remoteFaultChanged);
  }
}

const std::map<PortID, FabricEndpoint>& SaiSwitch::getFabricConnectivity()
    const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getFabricConnectivityLocked(lock);
}

const std::map<PortID, FabricEndpoint>& SaiSwitch::getFabricConnectivityLocked(
    const std::lock_guard<std::mutex>& lock) const {
  return fabricConnectivityManager_->getConnectivityInfo();
}

std::vector<PortID> SaiSwitch::getSwitchReachability(SwitchID switchId) const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getSwitchReachabilityLocked(lock, switchId);
}

std::vector<PortID> SaiSwitch::getSwitchReachabilityLocked(
    const std::lock_guard<std::mutex>& lock,
    SwitchID switchId) const {
  return managerTable_->portManager().getFabricReachabilityForSwitch(switchId);
}

std::map<int64_t, FabricConnectivityManager::RemoteConnectionGroups>
SaiSwitch::getVirtualDeviceToRemoteConnectionGroups() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getVirtualDeviceToRemoteConnectionGroupsLocked(lock);
}

std::map<int64_t, FabricConnectivityManager::RemoteConnectionGroups>
SaiSwitch::getVirtualDeviceToRemoteConnectionGroupsLocked(
    const std::lock_guard<std::mutex>& lock) const {
  if (getSwitchType() != cfg::SwitchType::FABRIC) {
    return {};
  }
  auto lookupVirtualDeviceId = [this](PortID portId) {
    auto virtualDeviceId =
        platform_->getPlatformPort(portId)->getVirtualDeviceId();
    CHECK(virtualDeviceId.has_value());
    return *virtualDeviceId;
  };
  return fabricConnectivityManager_->getVirtualDeviceToRemoteConnectionGroups(
      lookupVirtualDeviceId);
}

void SaiSwitch::fetchL2Table(std::vector<L2EntryThrift>* l2Table, bool sdk)
    const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  fetchL2TableLocked(lock, l2Table, sdk);
}

void SaiSwitch::gracefulExitImpl() {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  gracefulExitLocked(lock);
}

void SaiSwitch::gracefulExitLocked(const std::lock_guard<std::mutex>& lock) {
  std::chrono::steady_clock::time_point begin =
      std::chrono::steady_clock::now();
  XLOG(DBG2) << "[Exit] Starting SAI Switch graceful exit";

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  const bool portLatchStatusSupported =
      platform_->getAsic()->isSupported(HwAsic::Feature::PORT_LATCH_STATUS);
  folly::dynamic latchStateJson = folly::dynamic::array;
  if (portLatchStatusSupported) {
    for (const auto& [portId, status] :
         managerTable_->portManager().snapshotExtOperStatusLatch()) {
      folly::dynamic portStatusJson = folly::dynamic::object;
      portStatusJson["portId"] = static_cast<int64_t>(portId);
      portStatusJson["currentStatus"] = status.current_status;
      portStatusJson["changed"] = status.changed;
      latchStateJson.push_back(std::move(portStatusJson));
    }
  }
#endif

  SaiSwitchTraits::Attributes::SwitchRestartWarm restartWarm{true};
  SaiApiTable::getInstance()->switchApi().setAttribute(
      saiSwitchId_, restartWarm);

#if CREDO_SDK_VERSION < CREDO_SDK_VERSION_0_9_0 && !defined(BRCM_PAI_SDK)
  SaiSwitchTraits::Attributes::SwitchPreShutdown preShutdown{true};
  SaiApiTable::getInstance()->switchApi().setAttribute(
      saiSwitchId_, preShutdown);
#endif
  if (platform_->getAsic()->isSupported(HwAsic::Feature::P4_WARMBOOT)) {
#if defined(TAJO_P4_WB_SDK)
    SaiSwitchTraits::Attributes::RestartIssu restartIssu{true};
    SaiApiTable::getInstance()->switchApi().setAttribute(
        saiSwitchId_, restartIssu);
#endif
  }
#if defined(TAJO_SAI_SDK)
  checkAndSetSdkDowngradeVersion();
#endif
  folly::dynamic follySwitchState = folly::dynamic::object;
  follySwitchState[kHwSwitch] = toFollyDynamicLocked(lock);
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  if (portLatchStatusSupported) {
    follySwitchState[kExtOperStatusLatchWarmBootState] =
        std::move(latchStateJson);
  }
#endif
  if (getSwitchType() == cfg::SwitchType::VOQ) {
    // SHEL callback already unregistered, hence safe to store in gracefulExit.
    follySwitchState[kSysPortShelState] =
        sysPortShelStateToFollyDynamicLocked(lock);
  }
  platform_->getWarmBootHelper()->storeHwSwitchWarmBootState(follySwitchState);
  std::chrono::steady_clock::time_point wbSaiSwitchWrite =
      std::chrono::steady_clock::now();
  XLOG(DBG2) << "[Exit] SaiSwitch warm boot state write time: "
             << std::chrono::duration_cast<std::chrono::duration<float>>(
                    wbSaiSwitchWrite - begin)
                    .count();
  managerTable_->switchManager().gracefulExit();
  std::chrono::steady_clock::time_point wbSdk =
      std::chrono::steady_clock::now();
  XLOG(DBG2) << "[Exit] Warm boot sdk time: "
             << std::chrono::duration_cast<std::chrono::duration<float>>(
                    wbSdk - wbSaiSwitchWrite)
                    .count();
  XLOG(DBG2) << " [Exit] SaiSwitch Graceful exit locked time: "
             << std::chrono::duration_cast<std::chrono::duration<float>>(
                    wbSdk - begin)
                    .count();
}

folly::dynamic SaiSwitch::toFollyDynamic() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return toFollyDynamicLocked(lock);
}

void SaiSwitch::switchRunStateChangedImpl(SwitchRunState newState) {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  switchRunStateChangedImplLocked(lock, newState);
}

void SaiSwitch::exitFatal() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  exitFatalLocked(lock);
}

bool SaiSwitch::isPortUp(PortID port) const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return isPortUpLocked(lock, port);
}

void SaiSwitch::clearPortStats(
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  auto& portManager = managerTable_->portManager();
  for (auto port : *ports) {
    std::lock_guard<std::mutex> lock(saiSwitchMutex_);
    portManager.clearStats(static_cast<PortID>(port));
  }
}

prbs::InterfacePrbsState SaiSwitch::getPortPrbsState(PortID portId) {
  return managerTable_->portManager().getPortPrbsState(portId);
}

std::vector<phy::PrbsLaneStats> SaiSwitch::getPortAsicPrbsStats(PortID portId) {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTable_->portManager().getPortAsicPrbsStats(portId);
}

void SaiSwitch::clearPortAsicPrbsStats(PortID portId) {
  managerTable_->portManager().clearPortAsicPrbsStats(portId);
}

void SaiSwitch::clearSignalDetectAndLockChangedStats(const PortID& portId) {
  auto phyItr = lastPhyInfos_.find(portId);
  if (phyItr == lastPhyInfos_.end()) {
    return;
  }

  const auto& lineLanes = phyItr->second.stats()->line()->pmd()->lanes();
  for (auto& [laneId, laneStat] : *lineLanes) {
    if (laneStat.signalDetectChangedCount().has_value()) {
      laneStat.signalDetectChangedCount() = 0;
    }
    if (laneStat.cdrLockChangedCount().has_value()) {
      laneStat.cdrLockChangedCount() = 0;
    }
  }
}

void SaiSwitch::triggerCableLengthMeasurement(
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  std::vector<PortID> portIds;
  portIds.reserve(ports->size());
  for (const auto port : *ports) {
    portIds.emplace_back(port);
  }
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  managerTable_->portManager().triggerCableLengthMeasurement(portIds);
}

void SaiSwitch::clearInterfacePhyCounters(
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  auto& portManager = managerTable_->portManager();
  for (auto port : *ports) {
    std::lock_guard<std::mutex> lock(saiSwitchMutex_);
    auto portId = static_cast<PortID>(port);
    portManager.clearInterfacePhyCounters(portId);
    clearSignalDetectAndLockChangedStats(portId);
  }
}

cfg::PortSpeed SaiSwitch::getPortMaxSpeed(PortID port) const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getPortMaxSpeedLocked(lock, port);
}

void SaiSwitch::linkStateChangedCallbackTopHalf(
    uint32_t count,
    const sai_port_oper_status_notification_t* operStatus) {
  std::vector<sai_port_oper_status_notification_t> operStatusTmp;
  operStatusTmp.resize(count);
  std::copy(operStatus, operStatus + count, operStatusTmp.data());
  linkStateBottomHalfEventBase_.runInFbossEventBaseThread(
      [this, operStatus = std::move(operStatusTmp)]() mutable {
        linkStateChangedCallbackBottomHalf(std::move(operStatus));
      });
}

void SaiSwitch::syncPortLinkState(PortID portId) {
  linkStateBottomHalfEventBase_.runInFbossEventBaseThread(
      [this, portId = portId]() mutable {
        linkStateChangedBottomHalf(portId);
      });
}
void SaiSwitch::linkStateChangedBottomHalf(const PortID& portId) {
  // Query SDK for port oper state using portId
  auto handle = managerTable_->portManager().getPortHandle(portId);
  auto saiPortId = handle->port->adapterKey();
  auto portOperStatus = SaiApiTable::getInstance()->portApi().getAttribute(
      saiPortId, SaiPortTraits::Attributes::OperStatus{});

  std::vector<sai_port_oper_status_notification_t> portStatus{};
  sai_port_oper_status_notification_t notification{};
  notification.port_id = saiPortId;
  notification.port_state = static_cast<sai_port_oper_status_t>(portOperStatus);
  portStatus.push_back(notification);

  linkStateChangedCallbackBottomHalf(portStatus);
}

void SaiSwitch::linkStateChangedCallbackBottomHalf(
    std::vector<sai_port_oper_status_notification_t> operStatus) {
  if (getSwitchRunState() == SwitchRunState::EXITING) {
    return;
  }
  // Store both link status AND port type from concurrentIndices_ to avoid
  // race condition. The port2PortType_ map in SaiPortManager is not thread-safe
  // (std::unordered_map), while concurrentIndices_->portSaiId2PortInfo is
  // thread-safe (folly::ConcurrentHashMap). During warm boot, the main thread
  // populates port2PortType_ via addPort(), but this thread (fbossSaiLnkScnBH)
  // may read from it before the write is visible due to lack of memory
  // barriers. By using portType from concurrentIndices_, we avoid this race
  // condition.
  std::map<PortID, std::pair<bool, cfg::PortType>> swPortId2StatusAndType;
  std::map<PortID, std::optional<AggregatePortID>> swPortId2DownAggPort;
  for (auto i = 0; i < operStatus.size(); i++) {
    bool up = utility::isPortOperUp(operStatus[i].port_state);

    // Look up SwitchState PortID by port sai id in ConcurrentIndices
    const auto portItr = concurrentIndices_->portSaiId2PortInfo.find(
        PortSaiId(operStatus[i].port_id));
    if (portItr == concurrentIndices_->portSaiId2PortInfo.cend()) {
      XLOG(WARNING)
          << "received port notification for port with unknown sai id: "
          << operStatus[i].port_id;
      continue;
    }
    PortID swPortId = portItr->second.portID;
    cfg::PortType portType = portItr->second.portType;

    std::optional<AggregatePortID> swAggPort{};
    const auto aggrItr = concurrentIndices_->memberPort2AggregatePortIds.find(
        PortSaiId(operStatus[i].port_id));
    if (aggrItr != concurrentIndices_->memberPort2AggregatePortIds.end()) {
      swAggPort = aggrItr->second;
    }

    XLOGF(
        DBG2,
        "Link state changed {} ({}): {}",
        swPortId,
        PortSaiId{operStatus[i].port_id},
        up ? "up" : "down");

    if (!up) {
      /*
       * Only link down are handled in the fast path. We let the
       * link up processing happen via the regular state change
       * mechanism. Reason for that is, post a link down
       * - We signal FDB entry, neighbor entry, next hop and next hop group
       *   that a link went down.
       * - Next hop group then shrinks the group based on which next hops are
       * affected.
       * - We now signal the callback (SwSwitch for wedge_agent, HwTest for hw
       * tests) for this link down state
       *    - SwSwitch in turn schedules a non coalescing port down state
       * update
       *    - Schedules a neighbor remove state update
       * - Meanwhile, if we get a port up event, we will just signal this upto
       * the SwSwitch and not handle this is in the fast path. Reason being,
       * post a link up the link is not immediately ready for packet handling,
       * so if we expand ECMP groups in the fast path, we will see some ms of
       * traffic loss. So we let the link up processing happen via switch
       * updates, which means that it will be queued behind the link down and
       * neighbor purge. So a ECMP group reexpansion would need both a link up
       * and neighbor add state update for expansion. At this point we are
       * guaranteed to have the link be ready for packet transmission, since
       * we already resolved neighbors over that link.
       */
      std::lock_guard<std::mutex> lock{saiSwitchMutex_};
      if (swAggPort) {
        // member of lag is gone down. unbundle it from LAG
        // once link comes back up LACP engine in SwSwitch will bundle it
        // again
        managerTable_->lagManager().disableMember(swAggPort.value(), swPortId);
        if (!managerTable_->lagManager().isLagUp(swAggPort.value())) {
          // remove fdb entries on LAG, this would remove neighbors, next hops
          // will point to drop and next hop group will shrink.
          managerTable_->fdbManager().handleLinkDown(
              SaiPortDescriptor(swAggPort.value()));
          if (!needL2EntryForNeighbor()) {
            // A neighbor on a port rif over an aggregate is keyed on the lag,
            // not on the member whose link went down, so the per member
            // notification below never matches it. With no fdb entries to fall
            // back on, this is the only thing that points its next hop at drop
            // and shrinks the next hop group.
            managerTable_->neighborManager().handleLinkDown(
                SaiPortDescriptor(swAggPort.value()));
          }
          // if min-link is enabled, neighbor caches in sw switch may not be
          // cleared and re-learned, when port flaps happen around the min-link
          // threshold. As a result, sai neighbor/nexthop object is not updated
          // and keep pointing to unresolved nexthop cpu port, see S519817 for
          // details. So, need to force trigger clearing neighbor cache
          // associated with the agg port here.
          swPortId2DownAggPort[swPortId] = swAggPort;
          XLOG(DBG2) << "link down for agg port " << swAggPort.value();
        }
      }
      managerTable_->fdbManager().handleLinkDown(SaiPortDescriptor(swPortId));
      if (!needL2EntryForNeighbor()) {
        XLOG(INFO) << "handleLinkDown: " << swPortId;
        // On VOQ switches or for neighbors on port rif, there are not FDB
        // entries. Rather we only have L3 ports which in turn are associated
        // with RIFs or type (system) port. Neighbors then tied to these RIFs.
        // So we need to directly signal Port RIF neighbors of a corresponding
        // link going down.
        managerTable_->neighborManager().handleLinkDown(
            SaiPortDescriptor(swPortId));
      }
    }
    swPortId2StatusAndType[swPortId] = std::make_pair(up, portType);
  }
  // Issue callbacks in a separate loop so fast link status change
  // processing is not at the mercy of what the callback (SwSwitch, HwTest)
  // does with the callback notification.
  for (const auto& [swPortId, statusAndType] : swPortId2StatusAndType) {
    std::optional<AggregatePortID> downAggPort;
    auto it = swPortId2DownAggPort.find(swPortId);
    if (it != swPortId2DownAggPort.end()) {
      downAggPort = it->second;
    }
    callback_->linkStateChanged(
        swPortId,
        statusAndType.first,
        statusAndType.second,
        std::nullopt,
        downAggPort);
  }
}

void SaiSwitch::txReadyStatusChangeCallbackTopHalf(SwitchSaiId switchId) {
  if (switchId != getSaiSwitchId()) {
    // On Multi-NPU system, each HwSwitch will independelty register TxReady
    // callback for ports on corresp ASIC. Thus, if the callback provides a
    // switchID not matching our own, this will point to an SDK bug.
    // Log error, but don't process.
    XLOG(ERR) << "SaiSwitch " << static_cast<long>(getSaiSwitchId())
              << " received TX Ready Status changed callback for SaiSwitch "
              << static_cast<long>(switchId);
    return;
  }

  // When txReadyStatusChangeOrFwIsolateCallbackBottomHalf runs, it queries for
  // active/inactive link status of all the links at that time.
  // Thus, if we txReadyStatusChangeOrFwIsolateCallbackBottomHalf is already
  // queued for run, we can skip scheduling another.
  // Note: txReadyStatusChangeOrFwIsolateCallbackBottomHalf sets changePending
  // to false before querying link active/inactive status, so avoid race.
  auto changePending = txReadyStatusChangePending_.wlock();
  if (!(*changePending)) {
    *changePending = true;
    txReadyStatusChangeBottomHalfEventBase_.runInFbossEventBaseThread(
        [this]() mutable {
          txReadyStatusChangeOrFwIsolateCallbackBottomHalf();
        });
  }
}

void SaiSwitch::txReadyStatusChangeOrFwIsolateCallbackBottomHalf(
    bool fwIsolated,
    const std::optional<uint32_t>& numActiveFabricPortsAtFwIsolate) {
  if (getSwitchRunState() == SwitchRunState::EXITING) {
    return;
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  {
    auto changePending = txReadyStatusChangePending_.wlock();
    *changePending = false;
  }

  std::vector<SaiPortTraits::AdapterKey> adapterKeys;
  for (const auto& [portSaiId, portInfo] :
       concurrentIndices_->portSaiId2PortInfo) {
    const auto& [portID, portType] = portInfo;
    if (portType == cfg::PortType::FABRIC_PORT) {
      adapterKeys.emplace_back(portSaiId);
    }
  }

  // Return early if no fabric port is initialized
  if (adapterKeys.empty()) {
    return;
  }

  auto& portApi = SaiApiTable::getInstance()->portApi();
  std::vector<SaiPortTraits::Attributes::TxReadyStatus> txReadyStatuses(
      adapterKeys.size());
  auto txReadyStatusesGot =
      portApi.bulkGetAttributes(adapterKeys, txReadyStatuses);
  if (adapterKeys.size() != txReadyStatusesGot.size()) {
    XLOG(ERR) << "TX Ready status queried for " << adapterKeys.size()
              << " Fabric ports; query returned status for: "
              << txReadyStatusesGot.size();
    return;
  }

  auto numActiveFabricPorts = 0, numInactiveFabricPorts = 0;
  std::map<PortID, bool> port2IsActive;
  for (auto tuple : boost::combine(adapterKeys, txReadyStatusesGot)) {
    auto portSaiId = tuple.get<0>();
    auto txReadyStatus = tuple.get<1>();

    const auto portItr = concurrentIndices_->portSaiId2PortInfo.find(portSaiId);
    CHECK(portItr != concurrentIndices_->portSaiId2PortInfo.cend());
    auto portID = portItr->second.portID;
    auto isActive = (txReadyStatus == SAI_PORT_HOST_TX_READY_STATUS_READY);

    XLOG(DBG4) << "Fabric Port SAI ID: " << std::hex
               << static_cast<long>(portSaiId) << std::dec
               << " PortID: " << static_cast<int>(portID)
               << (isActive ? " ACTIVE" : " INACTIVE");
    port2IsActive[portID] = isActive;

    if (isActive) {
      numActiveFabricPorts++;
    } else {
      numInactiveFabricPorts++;
    }
  }

  std::string logPrefix =
      fwIsolated ? "Firmware Isolate" : "TX Ready status change";
  XLOG(DBG2) << logPrefix << " callback received:: NumActiveFabricPorts: "
             << numActiveFabricPorts
             << " NumInactiveFabricPorts: " << numInactiveFabricPorts
             << " NumActiveFabricPortsAtFwIsolate: "
             << (numActiveFabricPortsAtFwIsolate.has_value()
                     ? folly::to<std::string>(
                           numActiveFabricPortsAtFwIsolate.value())
                     : "--");

  callback_->linkActiveStateChangedOrFwIsolated(
      port2IsActive, fwIsolated, numActiveFabricPortsAtFwIsolate);
#endif
}

void SaiSwitch::fwDisabledLinksCallbackBottomHalf(
    const std::vector<int32_t>& fwDisabledPortIds) {
  callback_->linkAdminStateChangedByFw(fwDisabledPortIds);
}

void SaiSwitch::linkConnectivityChanged(
    const std::map<PortID, multiswitch::FabricConnectivityDelta>&
        connectivityDelta) {
  if (getSwitchRunState() == SwitchRunState::EXITING) {
    return;
  }
  callback_->linkConnectivityChanged(connectivityDelta);
}

void SaiSwitch::switchReachabilityChangeTopHalf() {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::SWITCH_REACHABILITY_CHANGE_NOTIFY)) {
    // Callback handling and rest of the flow is unsupported!
    return;
  }
  setSwitchReachabilityChangePending();
}

std::set<PortID> SaiSwitch::getFabricReachabilityPortIds(
    const std::vector<sai_object_id_t>& switchIdAndFabricPortSaiIds) const {
  int64_t switchId = switchIdAndFabricPortSaiIds.at(0);
  if (switchIdAndFabricPortSaiIds.size() > 1) {
    XLOG(DBG4) << "SwitchID " << switchId << " reachable over "
               << switchIdAndFabricPortSaiIds.size() - 1 << " ports!";
  } else if (switchIdAndFabricPortSaiIds.size() == 1) {
    XLOG(DBG4) << "SwitchID " << switchId << " unreachable over fabric!";
  }
  // Index 0 has switchId and indices 1 onwards has fabric port SAI id,
  // need to find the PortID associated with these fabric port SAI ids.
  std::set<PortID> portIds{};
  for (auto idx = 1; idx < switchIdAndFabricPortSaiIds.size(); idx++) {
    const auto portItr = concurrentIndices_->portSaiId2PortInfo.find(
        static_cast<PortSaiId>(switchIdAndFabricPortSaiIds.at(idx)));
    if (portItr == concurrentIndices_->portSaiId2PortInfo.cend()) {
      XLOG(WARNING)
          << "Received port notification for port with unknown sai id: "
          << switchIdAndFabricPortSaiIds.at(idx);
      continue;
    }
    portIds.insert(portItr->second.portID);
  }
  return portIds;
}

void SaiSwitch::setSwitchReachabilityChangePending() {
  // UpdateStats (runs every second):
  //    - Acquires saiSwitchMutex_
  //    - Later, acquires SaiApiLock.
  //    - Queries stats, and releases both the locks.
  //    - Repeats the above for every object type.
  //
  // When SwitchReachability changes:
  //   - Acquires SaiApiLock
  //   - Queries SDK for reachability change: one query for every remote Switch.
  //
  // When several links flap:
  //   - SDK issues SwitchReachability callback often.
  //   - Each callback processing triggers multiple calls to SDK under
  //     SaiApiLock as outlined above.
  //
  // If SaiApiLock is acquired by SwitchReachability often:
  //  - updateStats holds saiSwitchMutex_ and waits for SaiApiLock.
  //  - Any state update (e.g. set PRBS) waits on saiSwitchMutex
  //
  // This wait can be considerable:
  //  - Without link flaps: set PRBS calls takes about 20 milisecond
  //  - With link flaps: set PRBS calls take > 10 seconds!
  //
  // Thus, don't process SwitchReachability in the BottomHalf. Instead:
  //  - SwitchReachability TopHalf signals switchReachabilityChangePending.
  //  - updateStats checks switchReachabilityChangePending in every run (every
  //    second) and process reachability if requested.

  auto changePending = switchReachabilityChangePending_.wlock();
  if (*changePending < FLAGS_max_unprocessed_switch_reachability_changes) {
    *changePending += 1;
  }
}

std::map<SwitchID, std::set<PortID>> SaiSwitch::getSwitchReachabilityChange() {
  std::map<SwitchID, std::set<PortID>> reachabilityInfo{};
  auto& switchApi = SaiApiTable::getInstance()->switchApi();

  int interfaceNodeCount{0};
  int reachableSwitchCount{0};
  // There is only one DSF Node map for the given SwitchID.
  // Thus, the 'outer for loop' runs only once and reachabilityInfo values are
  // never overwritten in below loops.
  for (const auto& [_, dsfNodes] :
       std::as_const(*getProgrammedState()->getDsfNodes())) {
    for (const auto& [switchId, node] : std::as_const(*dsfNodes)) {
      if (*node->toThrift().type() != cfg::DsfNodeType::INTERFACE_NODE) {
        // Switch reachability is only to interface nodes destinations
        reachabilityInfo[SwitchID(switchId)] = std::set<PortID>();
        continue;
      }
      // Keep track of the number of interface nodes
      interfaceNodeCount++;
      auto maxFabricPorts =
          getMaxNumberOfFabricPorts(*node->toThrift().asicType());
      std::vector<sai_object_id_t> output(maxFabricPorts + 1);
      // Requirement to have the switchId as first entry in the list,
      // the get() will return fabric ports over which the switchId is
      // reachable in indices 1 onwards.
      output.at(0) = switchId;
      // TODO: Use bulkGetAttribute instead of multiple getAttributes()
      auto switchIdAndFabricPortSaiIds = switchApi.getAttribute(
          saiSwitchId_,
          SaiSwitchTraits::Attributes::FabricRemoteReachablePortList{output});
      CHECK_EQ(switchIdAndFabricPortSaiIds.at(0), switchId);
      auto reachablePortIds =
          getFabricReachabilityPortIds(switchIdAndFabricPortSaiIds);
      if (reachablePortIds.size()) {
        reachableSwitchCount++;
      }
      reachabilityInfo[SwitchID(switchId)] = std::move(reachablePortIds);
    }
  }
  XLOG(DBG2) << "Got switch reachability from SDK, " << reachableSwitchCount
             << " switches reachable, "
             << (interfaceNodeCount - reachableSwitchCount)
             << " switches unreachable!";
  return reachabilityInfo;
}

void SaiSwitch::processSwitchReachabilityChange(
    const std::map<SwitchID, std::set<PortID>>& reachabilityInfo) {
  if (getSwitchRunState() == SwitchRunState::EXITING) {
    return;
  }
  callback_->switchReachabilityChanged(
      SwitchID(platform_->getAsic()->getSwitchId().value()), reachabilityInfo);
}

BootType SaiSwitch::getBootType() const {
  return bootType_;
}

const SaiManagerTable* SaiSwitch::managerTable() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTableLocked(lock);
}

SaiManagerTable* SaiSwitch::managerTable() {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTableLocked(lock);
}

// Begin Locked functions with actual SaiSwitch functionality

std::shared_ptr<SwitchState> SaiSwitch::getColdBootSwitchState() {
  auto state = std::make_shared<SwitchState>();

  auto switchId = platform_->getAsic()->getSwitchId()
      ? *platform_->getAsic()->getSwitchId()
      : 0;
  auto matcher =
      HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(switchId)}));
  auto scopeResolver = platform_->scopeResolver();
  if (platform_->getAsic()->isSupported(HwAsic::Feature::CPU_PORT)) {
    // get cpu queue settings
    auto cpu = std::make_shared<ControlPlane>();
    auto cpuQueues = managerTable_->hostifManager().getQueueSettings();
    cpu->resetQueues(cpuQueues);
    auto cpuVoqs = managerTable_->hostifManager().getVoqSettings();
    cpu->resetVoqs(cpuVoqs);
    auto multiSwitchControlPlane = std::make_shared<MultiControlPlane>();
    multiSwitchControlPlane->addNode(
        scopeResolver->scope(cpu).matcherString(), cpu);
    state->resetControlPlane(multiSwitchControlPlane);
  }
  // TODO(joseph5wu) We need to design how to restore xphy ports for the state
  // Temporarily skip resetPorts for ASIC_TYPE_ELBERT_8DD
  if (platform_->getAsic()->getAsicType() !=
      cfg::AsicType::ASIC_TYPE_ELBERT_8DD) {
    // reconstruct ports
    auto portMaps =
        managerTable_->portManager().reconstructPortsFromStore(getSwitchType());
    auto ports = std::make_shared<MultiSwitchPortMap>();
    if (FLAGS_hide_fabric_ports || FLAGS_hide_interface_ports) {
      for (const auto& portMap : std::as_const(*portMaps)) {
        for (const auto& [id, port] : std::as_const(*portMap.second)) {
          if ((port->getPortType() == cfg::PortType::FABRIC_PORT &&
               FLAGS_hide_fabric_ports) ||
              (port->getPortType() == cfg::PortType::INTERFACE_PORT &&
               FLAGS_hide_interface_ports)) {
            continue;
          }
          ports->addNode(port, scopeResolver->scope(port));
        }
      }
    } else {
      ports = std::move(portMaps);
    }
    state->resetPorts(ports);
  }

  // For VOQ switch, create system ports for existing egress ports
  if (getSwitchType() == cfg::SwitchType::VOQ) {
    CHECK(getSwitchId().has_value());
    auto multiSysPorts = std::make_shared<MultiSwitchSystemPortMap>();
    auto sysPorts = managerTable_->systemPortManager().constructSystemPorts(
        state->getPorts(),
        scopeResolver->switchIdToSwitchInfo(),
        getSwitchId().value());

    for (auto iter : std::as_const(*sysPorts)) {
      multiSysPorts->addNode(iter.second, scopeResolver->scope(iter.second));
    }
    state->resetSystemPorts(multiSysPorts);
  }

  auto multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
  auto switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setSwitchIdToSwitchInfo(
      scopeResolver->switchIdToSwitchInfo());
  multiSwitchSwitchSettings->addNode(matcher.matcherString(), switchSettings);

  if (platform_->getAsic()->isSupported(HwAsic::Feature::SWITCH_ISOLATE)) {
    CHECK(getSwitchId().has_value());
    // In practice, this will read and populate the value set during switch
    // create viz. DRAINED
    auto& switchApi = SaiApiTable::getInstance()->switchApi();
    auto switchIsolate = switchApi.getAttribute(
        saiSwitchId_, SaiSwitchTraits::Attributes::SwitchIsolate{});
    auto drainState = switchIsolate ? cfg::SwitchDrainState::DRAINED
                                    : cfg::SwitchDrainState::UNDRAINED;
    CHECK(drainState == cfg::SwitchDrainState::DRAINED)
        << " Switch must be drained on cold boot";
    XLOG(DBG2) << " On cold boot, switch came up:  "
               << (drainState == cfg::SwitchDrainState::DRAINED ? "DRAINED"
                                                                : "UNDRAINED");
    switchSettings->setActualSwitchDrainState(drainState);
  }

  state->resetSwitchSettings(multiSwitchSwitchSettings);
  state->publish();
  return state;
}

void SaiSwitch::restoreExtOperStatusLatchWarmBootBaseline(
    const std::optional<folly::dynamic>& latchWarmBootState,
    bool sdkSupportsLatchState) {
  if (!latchWarmBootState) {
    if (sdkSupportsLatchState) {
      XLOG(DBG2) << "Warmboot state is missing "
                 << kExtOperStatusLatchWarmBootState;
    }
    return;
  }
  if (!sdkSupportsLatchState) {
    XLOG(WARNING) << "Ignoring saved " << kExtOperStatusLatchWarmBootState
                  << " because this build cannot consume the latch state";
    return;
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  extOperStatusLatchWarmBootBaseline_.clear();
  for (const auto& portStatusJson : *latchWarmBootState) {
    extOperStatusLatchWarmBootBaseline_.emplace(
        PortID(portStatusJson["portId"].asInt()),
        sai_latch_status_t{
            portStatusJson["currentStatus"].asBool(),
            portStatusJson["changed"].asBool()});
  }
#endif
}

HwInitResult SaiSwitch::initLocked(
    const std::lock_guard<std::mutex>& lock,
    HwWriteBehavior behavior,
    Callback* callback,
    cfg::SwitchType switchType,
    std::optional<int64_t> switchId) noexcept {
  HwInitResult ret;
  ret.rib = std::make_unique<RoutingInformationBase>();
  ret.bootType = bootType_;
  std::unique_ptr<folly::dynamic> adapterKeysJson;
  std::unique_ptr<folly::dynamic> adapterKeys2AdapterHostKeysJson;
  std::optional<folly::dynamic> latchWarmBootState;
  const bool portLatchStatusSupported =
      platform_->getAsic()->isSupported(HwAsic::Feature::PORT_LATCH_STATUS);

  concurrentIndices_ = std::make_unique<ConcurrentIndices>();
  managerTable_ = std::make_unique<SaiManagerTable>(
      platform_, bootType_, switchType, switchId);
  saiSwitchId_ = managerTable_->switchManager().getSwitchSaiId();
  callback_ = callback;
  __gSaiIdToSwitch.insert_or_assign(saiSwitchId_, this);
  SaiApiTable::getInstance()->enableLogging(FLAGS_enable_sai_log);
  if (bootType_ == BootType::WARM_BOOT) {
    auto switchStateJson = platform_->getWarmBootHelper()->getWarmBootState();
    ret.switchState = std::make_shared<SwitchState>();
    if (platform_->getAsic()->isSupported(HwAsic::Feature::OBJECT_KEY_CACHE)) {
      adapterKeysJson = std::make_unique<folly::dynamic>(
          switchStateJson[kHwSwitch][kAdapterKeys]);
      const auto& switchKeysJson = (*adapterKeysJson)[saiObjectTypeToString(
          SaiSwitchTraits::ObjectType)];
      CHECK_EQ(1, switchKeysJson.size());
    }
    // adapter host keys may not be recoverable for all types of object, such
    // as next hop group.
    if (switchStateJson[kHwSwitch].find(kAdapterKey2AdapterHostKey) !=
        switchStateJson[kHwSwitch].items().end()) {
      adapterKeys2AdapterHostKeysJson = std::make_unique<folly::dynamic>(
          switchStateJson[kHwSwitch][kAdapterKey2AdapterHostKey]);
    }
    if (portLatchStatusSupported) {
      if (const auto latchState =
              switchStateJson.find(kExtOperStatusLatchWarmBootState);
          latchState != switchStateJson.items().end()) {
        latchWarmBootState = latchState->second;
      }
    }
    // Recover Shel Port State from HW State
    if (getSwitchType() == cfg::SwitchType::VOQ &&
        switchStateJson.find(kSysPortShelState) !=
            switchStateJson.items().end()) {
      reconstructSysPortShelStateLocked(
          lock,
          switchStateJson[kSysPortShelState],
          concurrentIndices_->sysPortShelState);
    }
  }
  initStoreAndManagersLocked(
      lock,
      behavior,
      adapterKeysJson.get(),
      adapterKeys2AdapterHostKeysJson.get());
  if (bootType_ == BootType::WARM_BOOT) {
    if (portLatchStatusSupported) {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
      restoreExtOperStatusLatchWarmBootBaseline(latchWarmBootState, true);
#else
      restoreExtOperStatusLatchWarmBootBaseline(latchWarmBootState, false);
#endif
    }
  } else {
    if (platform_->getAsic()->isSupported(HwAsic::Feature::SWITCH_ISOLATE)) {
      auto& switchApi = SaiApiTable::getInstance()->switchApi();
      auto isolated = switchApi.getAttribute(
          saiSwitchId_, SaiSwitchTraits::Attributes::SwitchIsolate{});
      XLOG(DBG2) << " Fabric switch on cold boot came up isolated : "
                 << (isolated ? "yes" : "no");
      CHECK(isolated);
    }
    ret.switchState = getColdBootSwitchState();
    ret.switchState->publish();
    setProgrammedState(ret.switchState);
    CHECK(ret.switchState->getSwitchSettings()->size());
    if (getPlatform()->getAsic()->isSupported(HwAsic::Feature::MAC_AGING)) {
      managerTable_->switchManager().setMacAgingSeconds(
          ret.switchState->getSwitchSettings()
              ->cbegin()
              ->second->getL2AgeTimerSeconds());
    }
  }
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0)
  auto& switchApi = SaiApiTable::getInstance()->switchApi();
  asicRevision_ = switchApi.getAttribute(
      saiSwitchId_, SaiSwitchTraits::Attributes::AsicRevision{});
  XLOG(DBG2) << "Asic revision: " << *asicRevision_;
  getSwitchStats()->asicRevision(*asicRevision_);
#endif
  ret.switchState->publish();
  return ret;
}

void SaiSwitch::setPortOwnershipToAdapter() {
  if ((getSwitchType() == cfg::SwitchType::FABRIC) ||
      (getSwitchType() == cfg::SwitchType::VOQ)) {
    // only do this for fabric or voq switches
    auto& switchApi = SaiApiTable::getInstance()->switchApi();
    auto fabricPorts = switchApi.getAttribute(
        saiSwitchId_, SaiSwitchTraits::Attributes::FabricPortList{});
    auto& portStore = saiStore_->get<SaiPortTraits>();
    std::vector<PortSaiId> portSaiIdsOwnedByAdapter;
    for (auto& fid : fabricPorts) {
      // Fabric ports are explicitly made to own by adapter because NOS
      // can't remove/add fabric ports.
      // Add to warm boot handles so object has a reference and
      // is preserved in Port store
      portStore.loadObjectOwnedByAdapter(
          PortSaiId(fid), true /* add to warm boot handles*/);
      if (FLAGS_hide_fabric_ports) {
        // Fabric port serdes are owned by NOS. As part of this flag,
        // we keep fabric ports explicitly disabled (for optimization) for
        // tests and hence don't create fabric port serdes either. In that
        // case, sdk will continue to load fabric port and serdes, but not
        // claimed by NOS since its created by adapter. Hence we need to
        // explicitly mark serdes objects as owned by adapter in that case.
        portSaiIdsOwnedByAdapter.emplace_back(fid);
      }
    }
    if (FLAGS_hide_interface_ports) {
      for (auto& iter : portStore.objects()) {
        auto saiPort = iter.second.lock();
        if (!saiPort) {
          continue;
        }
        auto portSaiId = saiPort->adapterKey();
        auto portType = SaiApiTable::getInstance()->portApi().getAttribute(
            portSaiId, SaiPortTraits::Attributes::Type{});
        bool isHyperPortMember = false;
#if defined(BRCM_SAI_SDK_DNX_GTE_14_0)
        isHyperPortMember = SaiApiTable::getInstance()->portApi().getAttribute(
            portSaiId, SaiPortTraits::Attributes::IsHyperPortMember{});
#endif
        if (portType == SAI_PORT_TYPE_LOGICAL && !isHyperPortMember) {
          portStore.loadObjectOwnedByAdapter(
              portSaiId, true /* add to warm boot handles*/);
          portSaiIdsOwnedByAdapter.emplace_back(portSaiId);
        }
      }
    }
    auto& portSerdesStore = saiStore_->get<SaiPortSerdesTraits>();
    std::optional<SaiPortTraits::Attributes::SerdesId> serdesAttr{};
    for (const auto& portSaiId : portSaiIdsOwnedByAdapter) {
      auto serdesId = SaiApiTable::getInstance()->portApi().getAttribute(
          portSaiId, serdesAttr);
      if (serdesId.has_value()) {
        portSerdesStore.loadObjectOwnedByAdapter(
            static_cast<PortSerdesSaiId>(serdesId.value()),
            true /* add to warm boot handles*/);
      }
    }
  }
}

void SaiSwitch::initStoreAndManagersLocked(
    const std::lock_guard<std::mutex>& /*lock*/,
    HwWriteBehavior behavior,
    const folly::dynamic* adapterKeys,
    const folly::dynamic* adapterKeys2AdapterHostKeys) {
  saiStore_ = std::make_unique<SaiStore>(saiSwitchId_);
  saiStore_->reload(adapterKeys, adapterKeys2AdapterHostKeys);
  managerTable_->createSaiTableManagers(
      saiStore_.get(), platform_, concurrentIndices_.get());
  /*
   * SwitchState does not have notion of AclTableGroup or AclTable today.
   * Thus, stateChanged() can not process aclTableGroupChanges or
   * aclTableChanges. Thus, we create single AclTableGroup for Ingress and a
   * single AclTable at its member during init(). Every AclEntry from
   * SwitchState is added to this AclTable by stateChanged() while processing
   * AclEntryChanges.
   *
   * During cold boot, addAclTableGroup()/addAclTable() populate
   * AclTableGroupManager/AclTableManager local data structures + program the
   * ASIC by calling SAI AclApi.
   *
   * During warm boot, SaiStore reload() reloads SAI objects for
   * AclTableGroup/AclTable in SaiStore. Thus,
   * addAclTableGroup()/addAclTable() only populate
   * AclTableGroupManager/AclTableManager local data structure.
   *
   * In future, SwitchState would be extended to carry AclTable, at that time,
   * the implementation would be on the following lines:
   *     - SwitchState AclTable configuration would include Stage
   *       e.g. ingress or egress.
   *     - For every stage supported for AclTable, SaiSwitch::init would
   *       pre-create an AclTableGroup.
   *     - statechanged() would contain AclTable delta processing which would
   *       add/remove/change AclTable and using 'stage', also update the
   *       corresponding Acl Table group member.
   *     - statechanged() would continue to carry AclEntry delta processing.
   */
  {
    HwWriteBehaviorRAII writeBehavior{behavior};
    if (getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::ACL_TABLE_GROUP)) {
      if (!FLAGS_enable_acl_table_group) {
        auto aclTableGroup =
            std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
        managerTable_->aclTableGroupManager().addAclTableGroup(aclTableGroup);

        managerTable_->aclTableManager().addDefaultIngressAclTable();
      }
    }

    managerTable_->debugCounterManager().setupDebugCounters();
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::COUNTER_REFRESH_INTERVAL)) {
      managerTable_->switchManager().setupCounterRefreshInterval();
    }
    if (platform_->getAsic()->isSupported(HwAsic::Feature::FABRIC_PORTS)) {
      setPortOwnershipToAdapter();
    }

    // during warm boot, all default system ports created for the platform
    // show up as unclaimed store objects. These obejcts are created once at
    // init and not need to be removed during warm boot. Change ownership for
    // them explicitly to adapter to remove it from unclaimed object
    if (getSwitchType() == cfg::SwitchType::VOQ) {
      managerTable_->bridgeManager().createBridgeHandle();
    }
  }
} // namespace facebook::fboss

void SaiSwitch::initLinkScanLocked(const std::lock_guard<std::mutex>& lock) {
  linkStateBottomHalfEventBase_.runInFbossEventBaseThread([=, this, &lock]() {
    auto& switchApi = SaiApiTable::getInstance()->switchApi();
    switchApi.registerPortStateChangeCallback(
        saiSwitchId_, __glinkStateChangedNotification);

    /* report initial link status after registering link scan call back.  link
     * state changes after reporting initial link state and before registering
     * link scan callback, could get lost. to mitigate this, register link
     * scan callback before reporting initial link state. doing this in
     * context of same thread/event base ensures initial link state always
     * preceedes any link state changes that may happen after registering link
     * scan call back are reported in order. */
    syncLinkStatesLocked(lock);
  });
}

void SaiSwitch::syncLinkStatesLocked(
    const std::lock_guard<std::mutex>& /* lock */) {
  for (const auto& portIdAndHandle : managerTable_->portManager()) {
    const auto& port = portIdAndHandle.second->port;
    auto operStatus = static_cast<sai_port_oper_status_t>(
        SaiApiTable::getInstance()->portApi().getAttribute(
            port->adapterKey(), SaiPortTraits::Attributes::OperStatus{}));
    XLOG(DBG2) << "Sending link state change notification for port "
               << portIdAndHandle.first << " with oper status: "
               << (utility::isPortOperUp(operStatus) ? "UP" : "DOWN");
    callback_->linkStateChanged(
        portIdAndHandle.first,
        utility::isPortOperUp(operStatus),
        managerTable_->portManager().getPortType(portIdAndHandle.first));
  }
}

void SaiSwitch::syncLinkStates() {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  linkStateBottomHalfEventBase_.runInFbossEventBaseThread(
      [=, this, &lock]() { syncLinkStatesLocked(lock); });
}

void SaiSwitch::initLinkConnectivityChangeLocked(
    const std::lock_guard<std::mutex>& lock) {
  linkConnectivityChangeBottomHalfEventBase_.runInFbossEventBaseThread(
      [this, &lock] { syncLinkConnectivityLocked(lock); });
}

void SaiSwitch::syncLinkConnectivity() {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  if (linkConnectivityChangeBottomHalfThread_) {
    linkConnectivityChangeBottomHalfEventBase_.runInFbossEventBaseThread(
        [this, &lock] { syncLinkConnectivityLocked(lock); });
  }
}
void SaiSwitch::syncLinkConnectivityLocked(
    const std::lock_guard<std::mutex>& lock) {
  auto connectivity = fabricConnectivityManager_->getConnectivityInfo();
  std::map<PortID, multiswitch::FabricConnectivityDelta> connectivityDelta;
  for (const auto& [port, connectivityInfo] : connectivity) {
    multiswitch::FabricConnectivityDelta delta;
    delta.newConnectivity() = connectivityInfo;
    connectivityDelta.insert({port, delta});
  }
  if (connectivityDelta.size()) {
    callback_->linkConnectivityChanged(connectivityDelta);
  }
}

void SaiSwitch::syncLinkActiveStates() {
  // Link active state is valid only for fabric ports
  if (platform_->getAsic()->isSupported(HwAsic::Feature::FABRIC_PORTS)) {
    std::lock_guard<std::mutex> lock(saiSwitchMutex_);
    txReadyStatusChangeBottomHalfEventBase_.runInFbossEventBaseThread(
        [=, this]() { txReadyStatusChangeOrFwIsolateCallbackBottomHalf(); });
  }
}

void SaiSwitch::syncSwitchReachability() {
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SWITCH_REACHABILITY_CHANGE_NOTIFY)) {
    std::lock_guard<std::mutex> lock(saiSwitchMutex_);
    setSwitchReachabilityChangePending();
  }
}

void SaiSwitch::initTxReadyStatusChangeLocked(
    const std::lock_guard<std::mutex>& /* lock */) {
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  txReadyStatusChangeBottomHalfEventBase_.runInFbossEventBaseThread(
      [=, this]() {
        auto& switchApi = SaiApiTable::getInstance()->switchApi();
        switchApi.registerTxReadyStatusChangeCallback(
            saiSwitchId_, __gTxReadyStatusChangeNotification);

        /*
         * If we query/process before registering the callback, then callbacks
         * after we query/process but before we register the callback could get
         * lost.
         *
         * Thus, query the initial state and process after registering the
         * callback.
         *
         * Moreover, query/process in the same context that registers/processes
         * callback to guarantee that we don't miss any callbacks and those are
         * always ordered.
         */
        txReadyStatusChangeOrFwIsolateCallbackBottomHalf();
      });
#endif
}

void SaiSwitch::initSwitchReachabilityChangeLocked(
    const std::lock_guard<std::mutex>& /* lock */) {
  /*
   * Query the initial state after registering the callbacks to avoid a
   * potentially missed callback and update.
   * More details in comments in setSwitchReachabilityChangePending.
   */
  setSwitchReachabilityChangePending();
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
void SaiSwitch::initSwitchAsicSdkHealthNotificationLocked(
    const std::lock_guard<std::mutex>& /* lock */) {
  CHECK(platform_->getAsic()->isSupported(
      HwAsic::Feature::SWITCH_ASIC_SDK_HEALTH_NOTIFY));
  CHECK(switchAsicSdkHealthNotificationBHThread_);
  auto& switchApi = SaiApiTable::getInstance()->switchApi();
  switchApi.registerSwitchAsicSdkHealthEventCallback(
      saiSwitchId_, __gSwitchAsicSdkHealthNotificationCallBack);

  // register for categories of asic sdk health events
  std::vector<sai_int32_t> categories{
      SAI_SWITCH_ASIC_SDK_HEALTH_CATEGORY_SW,
      SAI_SWITCH_ASIC_SDK_HEALTH_CATEGORY_FW,
      SAI_SWITCH_ASIC_SDK_HEALTH_CATEGORY_CPU_HW,
      SAI_SWITCH_ASIC_SDK_HEALTH_CATEGORY_ASIC_HW};
  SaiSwitchTraits::Attributes::RegFatalSwitchAsicSdkHealthCategory fatalEvents =
      categories;
  SaiSwitchTraits::Attributes::RegNoticeSwitchAsicSdkHealthCategory
      noticeEvents = categories;
  switchApi.setAttribute(saiSwitchId_, fatalEvents);
  switchApi.setAttribute(saiSwitchId_, noticeEvents);
}
#endif

bool SaiSwitch::isMissingSrcPortAllowed(HostifTrapSaiId hostifTrapSaiId) {
  static std::set<facebook::fboss::cfg::PacketRxReason> kAllowedRxReasons = {
      facebook::fboss::cfg::PacketRxReason::TTL_1,
      facebook::fboss::cfg::PacketRxReason::MPLS_TTL_1};
  const auto hostifTrapItr =
      concurrentIndices_->hostifTrapIds.find(hostifTrapSaiId);
  if (hostifTrapItr == concurrentIndices_->hostifTrapIds.cend()) {
    return false;
  }
  return (
      kAllowedRxReasons.find(hostifTrapItr->second) != kAllowedRxReasons.end());
}

// If the SAI impl supports reading intf type, get it from portManager
// otherwise just return the value from our platform config.
phy::InterfaceType SaiSwitch::getInterfaceType(
    PortID portID,
    phy::DataPlanePhyChipType chipType) const {
  if (platform_->getAsic()->isSupported(HwAsic::Feature::PORT_INTERFACE_TYPE)) {
    return managerTable_->portManager().getInterfaceType(portID);
  }

  auto platPort = platform_->getPort(portID);
  auto profileConfig =
      platPort->getPortProfileConfig(platPort->getCurrentProfile());
  if (chipType == phy::DataPlanePhyChipType::IPHY) {
    if (!profileConfig.iphy()->interfaceType()) {
      XLOG(WARNING) << "No interfaceType set for iphy on port " << portID;
      return phy::InterfaceType::NONE;
    }
    return *profileConfig.iphy()->interfaceType();
  }
  if (chipType == phy::DataPlanePhyChipType::XPHY) {
    // TODO: we should eventually support system side stat collection for xphy
    // as well.
    if (!profileConfig.xphyLine() ||
        !profileConfig.xphyLine()->interfaceType()) {
      XLOG(WARNING) << "No interfaceType set for xphy on port " << portID;
      return phy::InterfaceType::NONE;
    }
    return *profileConfig.xphyLine()->interfaceType();
  }
  XLOG(WARNING) << "Invalid chip type " << static_cast<int>(chipType)
                << " found for port " << portID;

  return phy::InterfaceType::NONE;
}

void SaiSwitch::packetRxCallback(
    SwitchSaiId /* switch_id */,
    sai_size_t buffer_size,
    const void* buffer,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  std::optional<PortSaiId> portSaiIdOpt;
  std::optional<LagSaiId> lagSaiIdOpt;
  std::optional<HostifTrapSaiId> hostifTrapSaiIdOpt;
  std::optional<uint8_t> hostifQueueIdOpt;
  std::optional<int32_t> packetTypeOpt;

  std::optional<sai_attr_id_t> packetTypeAttrId =
      SaiRxPacketTraits::Attributes::AttributePacketType()();

  for (uint32_t index = 0; index < attr_count; index++) {
    switch (attr_list[index].id) {
      case SAI_HOSTIF_PACKET_ATTR_INGRESS_PORT:
        portSaiIdOpt = attr_list[index].value.oid;
        break;
      case SAI_HOSTIF_PACKET_ATTR_INGRESS_LAG:
        lagSaiIdOpt = attr_list[index].value.oid;
        break;
      case SAI_HOSTIF_PACKET_ATTR_HOSTIF_TRAP_ID:
        hostifTrapSaiIdOpt = attr_list[index].value.oid;
        break;
      case SAI_HOSTIF_PACKET_ATTR_EGRESS_QUEUE_INDEX:
        hostifQueueIdOpt = attr_list[index].value.u8;
        break;
      default:
        if (packetTypeAttrId.has_value() &&
            attr_list[index].id == packetTypeAttrId.value()) {
          packetTypeOpt = attr_list[index].value.s32;
        } else {
          XLOG_EVERY_MS(DBG3, 5000)
              << "invalid attribute received: " << attr_list[index].id;
        }
    }
  }

  /*
   * for Tajo, the TTL 1 processing is done at egress and hence the
   * packet received to CPU does not carry source port attribute. Hence
   * set the source port as 0 and derive the vlan tag from the packet
   * and send it to sw switch for processing.
   */
  bool allowMissingSrcPort = hostifTrapSaiIdOpt.has_value() &&
      (platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_EBRO ||
       platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_P200) &&
      isMissingSrcPortAllowed(hostifTrapSaiIdOpt.value());

  if (!portSaiIdOpt && !allowMissingSrcPort) {
    XLOG(DBG5)
        << "discarded a packet with missing SAI_HOSTIF_PACKET_ATTR_INGRESS_PORT.";
    return;
  }

  if (portSaiIdOpt.has_value()) {
    auto iter = concurrentIndices_->memberPort2AggregatePortIds.find(
        portSaiIdOpt.value());

    if (iter != concurrentIndices_->memberPort2AggregatePortIds.end()) {
      // hack if SAI_HOSTIF_PACKET_ATTR_INGRESS_LAG is not set on packet on
      // lag! if port belongs to some aggregate port, process packet as if
      // coming from lag.
      auto [portSaiId, aggregatePortId] = *iter;
      std::ignore = portSaiId;
      for (auto entry : concurrentIndices_->aggregatePortIds) {
        auto [lagSaiId, swLagId] = entry;
        if (swLagId == aggregatePortId) {
          lagSaiIdOpt = lagSaiId;
          break;
        }
      }
    }
  }

  auto portSaiId = portSaiIdOpt.has_value() ? portSaiIdOpt.value()
                                            : PortSaiId(SAI_NULL_OBJECT_ID);
  auto packetRxReason = cfg::PacketRxReason::UNMATCHED;
  if (hostifTrapSaiIdOpt.has_value()) {
    const auto hostifTrapItr =
        concurrentIndices_->hostifTrapIds.find(hostifTrapSaiIdOpt.value());
    if (hostifTrapItr != concurrentIndices_->hostifTrapIds.cend()) {
      packetRxReason = hostifTrapItr->second;
    }
  }

  std::optional<uint8_t> queueId;
  if (platform_->getAsic()->isSupported(HwAsic::Feature::CPU_QUEUES)) {
    queueId = hostifQueueIdOpt.has_value() ? hostifQueueIdOpt.value() : 0;
  }

  // Convert SAI packet RX type to PacketType enum
  std::optional<PacketType> packetType;
  if (packetTypeOpt.has_value()) {
    packetType = getReceivedPacketType(packetTypeOpt.value());
  }

  if (!lagSaiIdOpt) {
    packetRxCallbackPort(
        buffer_size,
        buffer,
        portSaiId,
        allowMissingSrcPort,
        packetRxReason,
        queueId,
        packetType);
  } else {
    packetRxCallbackLag(
        buffer_size,
        buffer,
        lagSaiIdOpt.value(),
        portSaiId,
        allowMissingSrcPort,
        packetRxReason,
        queueId,
        packetType);
  }
}

void SaiSwitch::packetRxCallbackPort(
    sai_size_t buffer_size,
    const void* buffer,
    PortSaiId portSaiId,
    bool allowMissingSrcPort,
    cfg::PacketRxReason rxReason,
    std::optional<uint8_t> queueId,
    std::optional<PacketType> packetType) {
  PortID swPortId(0);
  std::optional<VlanID> swVlanId = processVlanUntaggedPackets()
      ? std::nullopt
      : std::make_optional(VlanID(0));
  auto swVlanIdStr = [swVlanId]() {
    return swVlanId.has_value()
        ? folly::to<std::string>(static_cast<int>(swVlanId.value()))
        : "None";
  };

  auto rxPacket = std::make_unique<SaiRxPacket>(
      buffer_size,
      (void*)((char*)(buffer)),
      PortID(0),
      swVlanId,
      rxReason,
      queueId,
      packetType);
  const auto portItr = concurrentIndices_->portSaiId2PortInfo.find(portSaiId);
  /*
   * When a packet is received with source port as cpu port, do the following:
   * 1) Check if a packet has a vlan tag and only one tag. If the packet is
   * not tagged or contains multiple vlan tags, log error and return.
   * 2) If a valid vlan is found, send the packet to Sw switch for further
   * processing.
   * NOTE: The port id is set to 0 since the sw switch uses the vlan id for
   * packet processing.
   *
   * For eg:, For LL V4, IP packets are sent to the asic first. Since the ARP
   * is not resolved, the packet will be punted back to CPU with the ingress
   * port set to CPU port. These packets are handled here by finding the
   * vlan id in the packet and sending it to SwSwitch.
   *
   * We use the cached cpu port id to avoid holding manager table locks in
   * the Rx path.
   */
  const bool isCpuPort =
      platform_->getAsic()->isSupported(HwAsic::Feature::CPU_PORT) &&
      (portSaiId == getCPUPortSaiId());
  if (!processVlanUntaggedPackets()) {
    // NPU switch: need vlan resolution
    if (isCpuPort ||
        (allowMissingSrcPort &&
         portItr == concurrentIndices_->portSaiId2PortInfo.cend())) {
      // CPU port or missing src port: extract vlan from packet
      folly::io::Cursor cursor(rxPacket->buf());
      EthHdr ethHdr{cursor};
      auto vlanTags = ethHdr.getVlanTags();
      if (vlanTags.size() != 1) {
        XLOG(ERR) << "RX packet on cpu port has no vlan tag "
                  << "or multiple vlan tags: " << ethHdr.printVlanTags();
        return;
      }
      swVlanId = VlanID(vlanTags[0].vid());
      XLOG(DBG6) << "Rx packet on cpu port. "
                 << "Found vlan from packet: "
                 << static_cast<int>(swVlanId.value());
    } else if (portItr == concurrentIndices_->portSaiId2PortInfo.cend()) {
      XLOG(DBG) << "RX packet had port with unknown sai id: 0x" << std::hex
                << portSaiId;
      return;
    } else {
      swPortId = portItr->second.portID;
      const auto vlanItr =
          concurrentIndices_->vlanIds.find(PortDescriptorSaiId(portSaiId));
      if (vlanItr == concurrentIndices_->vlanIds.cend()) {
        XLOG(ERR) << "RX packet had port in no known vlan: 0x" << std::hex
                  << portSaiId;
        return;
      }
      swVlanId = vlanItr->second;
    }
  } else {
    // VOQ / FABRIC switch: no vlan needed
    if (!isCpuPort) {
      if (portItr == concurrentIndices_->portSaiId2PortInfo.cend()) {
        XLOG(ERR) << "RX packet had port with unknown sai id: 0x" << std::hex
                  << portSaiId;
        return;
      }
      swPortId = portItr->second.portID;
      XLOG(DBG6) << "RX packet with sai id: 0x" << std::hex << portSaiId
                 << " portID: " << swPortId;
    }
  }

  /*
   * Set the correct vlan and port ID for the rx packet
   */
  rxPacket->setSrcPort(swPortId);
  rxPacket->setSrcVlan(swVlanId);

  XLOG(DBG6) << "Rx packet on port: " << swPortId << " vlan: " << swVlanIdStr()
             << " trap: " << packetRxReasonToString(rxReason) << " queue: "
             << (queueId.has_value() ? static_cast<uint16_t>(*queueId) : 0);

  folly::io::Cursor c0(rxPacket->buf());
  XLOG(DBG6) << PktUtil::hexDump(c0);
  callback_->packetReceived(std::move(rxPacket));
}

void SaiSwitch::packetRxCallbackLag(
    sai_size_t buffer_size,
    const void* buffer,
    LagSaiId lagSaiId,
    PortSaiId portSaiId,
    bool allowMissingSrcPort,
    cfg::PacketRxReason rxReason,
    std::optional<uint8_t> queueId,
    std::optional<PacketType> packetType) {
  AggregatePortID swAggPortId(0);
  PortID swPortId(0);
  std::optional<VlanID> swVlanId = processVlanUntaggedPackets()
      ? std::nullopt
      : std::make_optional(VlanID(0));
  auto swVlanIdStr = [&swVlanId]() {
    return swVlanId.has_value()
        ? folly::to<std::string>(static_cast<int>(swVlanId.value()))
        : "None";
  };
  auto rxPacket = std::make_unique<SaiRxPacket>(
      buffer_size, buffer, PortID(0), swVlanId, rxReason, queueId, packetType);

  folly::io::Cursor c0(rxPacket->buf());
  XLOG(DBG6) << PktUtil::hexDump(c0);

  const auto aggPortItr = concurrentIndices_->aggregatePortIds.find(lagSaiId);

  if (aggPortItr == concurrentIndices_->aggregatePortIds.end()) {
    // TODO: add counter to keep track of spurious rx packet
    XLOG(ERR) << "RX packet had lag with unknown sai id: 0x" << std::hex
              << lagSaiId;
    return;
  }
  swAggPortId = aggPortItr->second;
  // Resolve a vlan only where the platform has them. A lag carrying a router
  // interface of its own is in no vlan, and the frames it traps arrive
  // untagged, the same way they do for a port router interface.
  if (!processVlanUntaggedPackets()) {
    const auto vlanItr =
        concurrentIndices_->vlanIds.find(PortDescriptorSaiId(lagSaiId));
    if (vlanItr == concurrentIndices_->vlanIds.cend()) {
      XLOG(ERR) << "RX packet had lag in no known vlan: 0x" << std::hex
                << lagSaiId;
      return;
    }
    swVlanId = vlanItr->second;
  }
  rxPacket->setSrcAggregatePort(swAggPortId);
  rxPacket->setSrcVlan(swVlanId);

  auto swPortItr = concurrentIndices_->portSaiId2PortInfo.find(portSaiId);
  if (swPortItr == concurrentIndices_->portSaiId2PortInfo.cend() &&
      !allowMissingSrcPort) {
    XLOG(ERR) << "RX packet for lag has invalid port : 0x" << std::hex
              << portSaiId;
    return;
  }
  swPortId = swPortItr->second.portID;
  rxPacket->setSrcPort(swPortId);
  XLOG(DBG6) << "Rx packet on lag: " << swAggPortId << ", port: " << swPortId
             << " vlan: " << swVlanIdStr()
             << " trap: " << packetRxReasonToString(rxReason) << " queue: "
             << (queueId.has_value() ? static_cast<uint16_t>(*queueId) : 0);
  callback_->packetReceived(std::move(rxPacket));
}

bool SaiSwitch::isFeatureSetupLocked(
    FeaturesDesired feature,
    const std::lock_guard<std::mutex>& /*lock*/) const {
  if (!(feature & getFeaturesDesired())) {
    return false;
  }
  bool isConfigured = runState_ >= SwitchRunState::CONFIGURED;
  bool isInitialized = runState_ >= SwitchRunState::INITIALIZED;
  if (feature == FeaturesDesired::LINKSCAN_DESIRED) {
    return isConfigured || (bootType_ == BootType::WARM_BOOT && isInitialized);
  }
  if (feature == FeaturesDesired::PACKET_RX_DESIRED) {
    return isConfigured || (bootType_ == BootType::WARM_BOOT && isInitialized);
  }
  if (feature == FeaturesDesired::TAM_EVENT_NOTIFY_DESIRED) {
    return isConfigured;
  }
  CHECK(false) << " Unhandled feature " << feature;
  return false;
}

void SaiSwitch::unregisterCallbacksLocked(
    const std::lock_guard<std::mutex>& lock) noexcept {
  auto& switchApi = SaiApiTable::getInstance()->switchApi();
  if (isFeatureSetupLocked(FeaturesDesired::LINKSCAN_DESIRED, lock)) {
    switchApi.unregisterPortStateChangeCallback(saiSwitchId_);
  }
  if (isFeatureSetupLocked(FeaturesDesired::PACKET_RX_DESIRED, lock)) {
    switchApi.unregisterRxCallback(saiSwitchId_);
  }
  if (isFeatureSetupLocked(FeaturesDesired::TAM_EVENT_NOTIFY_DESIRED, lock)) {
#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
    switchApi.unregisterSwitchEventCallback(saiSwitchId_);
#else
    switchApi.unregisterTamEventCallback(saiSwitchId_);
#endif
  }

  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SWITCH_ASIC_SDK_HEALTH_NOTIFY)) {
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    switchApi.unregisterSwitchAsicSdkHealthEventCallback(saiSwitchId_);
#endif
  }

  if (platform_->getAsic()->isSupported(HwAsic::Feature::BRIDGE_PORT_8021Q)) {
    switchApi.unregisterFdbEventCallback(saiSwitchId_);
  }

  if (pfcDeadlockEnabled_) {
    switchApi.unregisterQueuePfcDeadlockNotificationCallback(saiSwitchId_);
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  if (isFeatureSetupLocked(FeaturesDesired::LINKSCAN_DESIRED, lock)) {
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::LINK_ACTIVE_INACTIVE_NOTIFY)) {
      switchApi.unregisterTxReadyStatusChangeCallback(saiSwitchId_);
    }
  }
#endif
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::VENDOR_SWITCH_NOTIFICATION)) {
    // Disable vendor switch interrupts before unregistering callback
    managerTable_->vendorSwitchManager().setVendorSwitchEventEnableState(
        false /*enable*/);
    switchApi.unregisterVendorSwitchEventNotifyCallback(saiSwitchId_);
  }
#endif
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::ASIC_RESET_NOTIFICATIONS)) {
    switchApi.unregisterSwitchHardResetNotifyCallback(saiSwitchId_);
  }
}

bool SaiSwitch::isValidStateUpdateLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    const StateDelta& delta) const {
  auto isValid = isStateUpdateValidMultiSwitch(
      delta,
      platform_->scopeResolver(),
      getSwitchID(),
      getPlatform()->getAsic());
  DeltaFunctions::forEachChanged(
      delta.getSwitchSettingsDelta(),
      [&](const std::shared_ptr<SwitchSettings>& oldSwitchSettings,
          const std::shared_ptr<SwitchSettings>& newSwitchSettings) {
        if (*oldSwitchSettings != *newSwitchSettings) {
          if (oldSwitchSettings->getL2LearningMode() !=
              newSwitchSettings->getL2LearningMode()) {
            if (l2LearningModeChangeProhibited()) {
              throw FbossError(
                  "Chaging L2 learning mode after initial config "
                  "application is not permitted");
            }
          }
          if (oldSwitchSettings->getEcmpGroupSettings() !=
              newSwitchSettings->getEcmpGroupSettings()) {
            if (ecmpGroupSettingsChangeProhibited()) {
              throw FbossError(
                  "Changing ecmpGroupSettings after initial config "
                  "application is not permitted");
            }
          }
        }
        if (newSwitchSettings->isQcmEnable()) {
          throw FbossError("QCM is not supported on SAI");
        }
      });

  auto qualifiersSupported =
      managerTable_->aclTableManager().getSupportedQualifierSet(
          cfg::AclStage::INGRESS);
  DeltaFunctions::forEachChanged(
      delta.getAclsDelta(),
      [qualifiersSupported, &isValid](
          const std::shared_ptr<AclEntry>& /*oldEntry*/,
          const std::shared_ptr<AclEntry>& newEntry) {
        auto qualifiersRequired = newEntry->getRequiredAclTableQualifiers();
        std::vector<cfg::AclTableQualifier> difference{
            qualifiersRequired.size()};
        auto iter = std::set_difference(
            qualifiersRequired.begin(),
            qualifiersRequired.end(),
            qualifiersSupported.begin(),
            qualifiersSupported.end(),
            difference.begin());
        difference.resize(iter - difference.begin());
        if (!difference.empty()) {
          XLOG(ERR) << "Invalid ACL qualifier used";
          isValid = false;
          return LoopAction::BREAK;
        }
        return LoopAction::CONTINUE;
      }

  );
  return isValid;
}

bool SaiSwitch::sendPacketSwitchedSync(std::unique_ptr<TxPacket> pkt) noexcept {
  folly::io::Cursor cursor(pkt->buf());
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SMAC_EQUALS_DMAC_CHECK_ENABLED)) {
    EthHdr ethHdr{cursor};
    if (ethHdr.getSrcMac() == ethHdr.getDstMac()) {
      auto* pktData = pkt->buf()->writableData();
      /* pktData[6]...pktData[11] is src mac */
      folly::MacAddress hackedMac{"fa:ce:b0:00:00:0c"};
      for (auto i = 0; i < folly::MacAddress::SIZE; i++) {
        pktData[folly::MacAddress::SIZE + i] = hackedMac.bytes()[i];
      }
      XLOG(DBG5) << "hacked packet as source and destination mac are same";
      cursor.reset(pkt->buf());
    }
  }

  if (FLAGS_check_tagged_tx &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::CPU_TX_PACKET_REQUIRES_VLAN_TAG)) {
    // use this in test to find no untagged packet is tx
    EthHdr ethHdr{cursor};
    CHECK_NE(ethHdr.size(), EthHdr::SIZE) << "tx packet is not tagged";
  }
  getSwitchStats()->txSent();

  XLOG(DBG6) << "sending packet with pipeline look up";
  XLOG(DBG6) << PktUtil::hexDump(cursor);
  SaiTxPacketTraits::Attributes::TxType txType(
      SAI_HOSTIF_TX_TYPE_PIPELINE_LOOKUP);
  SaiTxPacketTraits::TxAttributes attributes{
      txType, std::nullopt, std::nullopt, std::nullopt};
  SaiHostifApiPacket txPacket{
      reinterpret_cast<void*>(pkt->buf()->writableData()),
      pkt->buf()->length()};
  auto& hostifApi = SaiApiTable::getInstance()->hostifApi();
  auto rv = hostifApi.send(attributes, saiSwitchId_, txPacket);
  if (rv != SAI_STATUS_SUCCESS) {
    saiLogErrorEveryMs(
        5000, rv, SAI_API_HOSTIF, "failed to send packet with pipeline lookup");
  }
  return rv == SAI_STATUS_SUCCESS;
}

bool SaiSwitch::sendPacketOutOfPortSyncCommon(
    std::unique_ptr<TxPacket> pkt,
    const PortSaiId& portSaiId,
    std::optional<uint8_t> queueId,
    std::optional<int32_t> packetType) {
  SaiHostifApiPacket txPacket{
      reinterpret_cast<void*>(pkt->buf()->writableData()),
      pkt->buf()->length()};

  SaiTxPacketTraits::Attributes::TxType txType(
      SAI_HOSTIF_TX_TYPE_PIPELINE_BYPASS);
  SaiTxPacketTraits::Attributes::EgressPortOrLag egressPort(portSaiId);
  SaiTxPacketTraits::Attributes::EgressQueueIndex egressQueueIndex(
      queueId.value_or(0));
  std::optional<SaiTxPacketTraits::Attributes::PacketType> pktType;
  if (packetType.has_value()) {
    pktType = SaiTxPacketTraits::Attributes::PacketType(packetType.value());
  }
  SaiTxPacketTraits::TxAttributes attributes{
      txType, egressPort, egressQueueIndex, pktType};
  auto& hostifApi = SaiApiTable::getInstance()->hostifApi();
  auto rv = hostifApi.send(attributes, saiSwitchId_, txPacket);
  if (rv != SAI_STATUS_SUCCESS) {
    saiLogErrorEveryMs(
        5000,
        rv,
        SAI_API_HOSTIF,
        "failed to send packet on fabric port with pipeline bypass");
  }
  return rv == SAI_STATUS_SUCCESS;
}

bool SaiSwitch::sendPacketOutOfPortSync(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queueId) noexcept {
  auto portItr = concurrentIndices_->portSaiIds.find(portID);
  if (portItr == concurrentIndices_->portSaiIds.end()) {
    XLOG(ERR) << "Failed to send packet on invalid port: " << portID;
    return false;
  }

  // We should never send packets directly out of eventor port. Doing so may
  // trigger SDK or HW bugs.
  if (!FLAGS_allow_eventor_send_packet) {
    if (auto portInfoItr =
            concurrentIndices_->portSaiId2PortInfo.find(portItr->second);
        portInfoItr != concurrentIndices_->portSaiId2PortInfo.end()) {
      if (portInfoItr->second.portType == cfg::PortType::EVENTOR_PORT) {
        XLOG_EVERY_MS(WARNING, 5000)
            << "Rejecting packet sent to EVENTOR_PORT: " << portID;
        return false;
      }
    }
  }

  /* Strip vlan tag with pipeline bypass, for all asic types. */
  getSwitchStats()->txSent();
  folly::io::Cursor cursor(pkt->buf());
  EthHdr ethHdr{cursor};

  if (FLAGS_strip_vlan_for_pipeline_bypass && !ethHdr.getVlanTags().empty()) {
    CHECK_EQ(ethHdr.getVlanTags().size(), 1)
        << "found more than one vlan tags while sending packet";
    /* Strip vlans as pipeline bypass doesn't handle this */
    cursor.reset(pkt->buf());
    XLOG(DBG5) << "strip vlan for packet";
    XLOG(DBG5) << PktUtil::hexDump(cursor);

    constexpr auto kVlanTagSize = 4;
    auto ethPayload = pkt->buf()->clone();
    // trim DA(6), SA(6) & vlan (4)
    ethPayload->trimStart(
        folly::MacAddress::SIZE + folly::MacAddress::SIZE + kVlanTagSize);

    // trim rest of packet except DA(6), SA(6)
    auto totalLength = pkt->buf()->length();
    pkt->buf()->trimEnd(
        totalLength - folly::MacAddress::SIZE - folly::MacAddress::SIZE);

    // append to trimmed ethernet header remaining payload
    pkt->buf()->appendChain(std::move(ethPayload));
    pkt->buf()->coalesce();
    cursor.reset(pkt->buf());
    XLOG(DBG5) << "stripped vlan, new packet";
    XLOG(DBG5) << PktUtil::hexDump(cursor);
  }

  return sendPacketOutOfPortSyncCommon(
      std::move(pkt), portItr->second, queueId.value_or(0), std::nullopt);
}

void SaiSwitch::fetchL2TableLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    std::vector<L2EntryThrift>* l2Table,
    bool sdk) const {
  *l2Table = managerTable_->fdbManager().getL2Entries(sdk);
}

folly::dynamic SaiSwitch::toFollyDynamicLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {
  auto adapterKeys = saiStore_->adapterKeysFollyDynamic();
  // Need to provide full namespace scope for toFollyDynamic to disambiguate
  // from member SaiSwitch::toFollyDynamic
  auto switchKeys = folly::dynamic::array(
      facebook::fboss::toFollyDynamic<SaiSwitchTraits>(saiSwitchId_));
  adapterKeys[saiObjectTypeToString(SaiSwitchTraits::ObjectType)] = switchKeys;

  folly::dynamic hwSwitch = folly::dynamic::object;
  hwSwitch[kAdapterKeys] = adapterKeys;
  hwSwitch[kAdapterKey2AdapterHostKey] =
      saiStore_->adapterKeys2AdapterHostKeysFollyDynamic();
  return hwSwitch;
}

folly::dynamic SaiSwitch::sysPortShelStateToFollyDynamicLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {
  folly::dynamic shelState = folly::dynamic::object;
  for (const auto& [sysPortId, portState] :
       std::as_const(concurrentIndices().sysPortShelState)) {
    shelState[folly::to<std::string>(sysPortId)] = static_cast<int>(portState);
  }
  return shelState;
}

void SaiSwitch::reconstructSysPortShelStateLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    const folly::dynamic& shelStateJson,
    folly::ConcurrentHashMap<SystemPortID, cfg::PortState>& sysPortShelState) {
  for (const auto& [sysPortId, portState] : shelStateJson.items()) {
    auto sysPortIdInt = sysPortId.asInt();
    auto portStateInt = portState.asInt();
    sysPortShelState.insert_or_assign(
        static_cast<SystemPortID>(sysPortIdInt),
        static_cast<cfg::PortState>(portStateInt));
  }
}

bool SaiSwitch::isFullyInitialized() const {
  auto state = getSwitchRunState();
  return state >= SwitchRunState::INITIALIZED &&
      state != SwitchRunState::EXITING;
}
SwitchRunState SaiSwitch::getSwitchRunState() const {
  return runState_;
}

void SaiSwitch::switchRunStateChangedImplLocked(
    const std::lock_guard<std::mutex>& lock,
    SwitchRunState newState) {
  switch (newState) {
    case SwitchRunState::INITIALIZED: {
      auto& switchApi = SaiApiTable::getInstance()->switchApi();

      // FDB callback is only applicable if l2Learning mode is set.
      // L2 learning mode is set on Bridge port. Thus, enable the callback only
      // if Bridge prots are supported.
      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::BRIDGE_PORT_8021Q)) {
        switchApi.registerFdbEventCallback(saiSwitchId_, __gFdbEventCallback);
      }

      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::ASIC_RESET_NOTIFICATIONS)) {
        switchApi.registerSwitchHardResetNotifyCallback(
            saiSwitchId_, (sai_pointer_t)__gHardResetNotificationallback);
      }

    } break;
    case SwitchRunState::CONFIGURED: {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
      if (bootType_ == BootType::WARM_BOOT &&
          platform_->getAsic()->isSupported(
              HwAsic::Feature::PORT_LATCH_STATUS)) {
        auto updateLatchCounters =
            [this,
             baseline =
                 std::move(extOperStatusLatchWarmBootBaseline_)]() mutable {
              std::lock_guard<std::mutex> lock(saiSwitchMutex_);
              managerTable_->portManager()
                  .updateExtOperStatusLatchAfterWarmBoot(baseline);
            };
        if (linkStateBottomHalfThread_) {
          linkStateBottomHalfEventBase_.runInFbossEventBaseThread(
              [this,
               updateLatchCounters = std::move(updateLatchCounters)]() mutable {
                linkStateBottomHalfEventBase_.runAfterDelay(
                    std::move(updateLatchCounters),
                    kExtOperStatusLatchWarmBootCaptureDelayMs);
              });
        } else {
          updateLatchCounters();
        }
      }
#endif

      /*
       * SDK can notify FBOSS of an error condition via callback. For some
       * scenarios e.g. RX FIFO stuck, Firmware isolation etc., to further
       * debug, we may want to capture the system state as soon as the error
       * condition occurs. SDK issuing a callback to FBOSS and then relying on
       * FBOSS to collect system state info adds latency, and some state might
       * be lost. Thus, we follow a model where SDK dumps the state to a log
       * file before issuing callback.
       *
       * FBOSS programs the file path for SDK to write such state to. An error
       * callback can be issued as soon as it is registered. Thus, set the
       * file path before registering any error callback.
       */
      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::SDK_REGISTER_DUMP)) {
        setSdkRegDumpEnabledLocked(lock, !FLAGS_skip_sdk_reg_dump);
      }

      /*
       * Cold boot system init results in a lot of events like interrupts.
       * These needs to be cleared once system is up and running as it will
       * help ensure that tech support dumps done at a later point in time
       * will yield good data and we dont need to worry about events being
       * left over from cold boot system init. Ideally, we need to clear the
       * cold boot system init events once system is stable. However, it is
       * hard to define the criteria for system stability, hence the decision
       * to perform this init post cold boot and config complete.
       */
      if (bootType_ == BootType::COLD_BOOT &&
          platform_->getAsic()->isSupported(HwAsic::Feature::TECH_SUPPORT)) {
        initTechSupport();
      }

      if (getFeaturesDesired() & FeaturesDesired::LINKSCAN_DESIRED) {
        /*
         * Post warmboot synchronize hw link state with switch state
         * maintained at callback (SwSwitch or HwTest). Since the
         * callback_->linkStateChanged is called asynchronously, its possible
         * that prior to going down for warm boot there was a link event which
         * did not get communicated to up via callback_ before we received the
         * signal to do graceful exit. In such a case the switch state would
         * have a incorrect view of link state than the HW. Synchronize them.
         * Note that we dd this prior to registering our own linkscan callback
         * handler. This isn't required, but seems easier to reason about than
         * having interspersed callbacks from HW
         */
        /*
         * For cold boot, if port link state is changed before link scan is
         * registered, switch state will not reflect correct port status. as a
         * result invoke link scan call back for cold boot.
         */
        initLinkScanLocked(lock);
      }
      if (getFeaturesDesired() & FeaturesDesired::PACKET_RX_DESIRED) {
        auto& switchApi = SaiApiTable::getInstance()->switchApi();
        switchApi.registerRxCallback(saiSwitchId_, __gPacketRxCallback);
      }
      if (getFeaturesDesired() & FeaturesDesired::TAM_EVENT_NOTIFY_DESIRED) {
        auto& switchApi = SaiApiTable::getInstance()->switchApi();
#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
        switchApi.registerSwitchEventCallback(
            saiSwitchId_, (void*)__gSwitchEventCallback);
#else
        switchApi.registerTamEventCallback(saiSwitchId_, __gTamEventCallback);
#endif
      }
      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::SWITCH_ASIC_SDK_HEALTH_NOTIFY)) {
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
        initSwitchAsicSdkHealthNotificationLocked(lock);
#endif
      }

      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::LINK_ACTIVE_INACTIVE_NOTIFY)) {
        initTxReadyStatusChangeLocked(lock);
      }

      if (platform_->getAsic()->isSupported(HwAsic::Feature::FABRIC_PORTS)) {
        initLinkConnectivityChangeLocked(lock);
      }

      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::SWITCH_REACHABILITY_CHANGE_NOTIFY)) {
        initSwitchReachabilityChangeLocked(lock);
      }
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::VENDOR_SWITCH_NOTIFICATION)) {
        auto& switchApi = SaiApiTable::getInstance()->switchApi();
        switchApi.registerVendorSwitchEventNotifyCallback(
            saiSwitchId_, __gVendorSwitchEventNotificationCallback);
        // Enable vendor switch events now that callback is registered
        managerTable_->vendorSwitchManager().setVendorSwitchEventEnableState(
            true /*enable*/);
      }
#endif
    } break;
    case SwitchRunState::ROLLBACK:
      // rolling back
      unregisterCallbacksLocked(lock);
      break;
    default:
      break;
  }
  runState_ = newState;
}

void SaiSwitch::exitFatalLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {}

bool SaiSwitch::isPortUpLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    PortID /* port */) const {
  return true;
}

cfg::PortSpeed SaiSwitch::getPortMaxSpeedLocked(
    const std::lock_guard<std::mutex>& lock,
    PortID port) const {
  auto* managerTable = managerTableLocked(lock);
  return managerTable->portManager().getMaxSpeed(port);
}

const SaiManagerTable* SaiSwitch::managerTableLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {
  return managerTable_.get();
}

SaiManagerTable* SaiSwitch::managerTableLocked(
    const std::lock_guard<std::mutex>& /* lock */) {
  return managerTable_.get();
}

void SaiSwitch::fdbEventCallback(
    uint32_t count,
    const sai_fdb_event_notification_data_t* data) {
  XLOG(DBG4) << "Received " << count << " learn notifications";
  if (runState_ < SwitchRunState::CONFIGURED) {
    // receive learn events after switch is configured to prevent
    // fdb entries from being created against ports  which would
    // become members of lags later on.
    XLOG(WARNING)
        << "ignoring learn notifications as switch is not configured.";
    return;
  }
  /*
   * FDB event notifications can be parsed in several ways
   * 1) Deep copy of sai_fdb_event_notification_data_t. This includes
   * allocating memory for sai_attribute_t in the fdb event data and freeing
   * them in the fdb bottom half thread.
   * 2) Create a temporary struct FdbEventNotificationData which is a subset
   * of sai_fdb_event_notification_data_t. This will be used to store only the
   * necessary attributes and pass it on to fdb bottom half thread. 3) Avoid
   * creating temporary structs and create L2Entry which can be sent to sw
   * switch. The problem with this approach is we need to hold the sai switch
   * mutex in the callback thread. Also, this can cause deadlock when we used
   * the bridge port id to fetch the port id from the SDK. Going with option 2
   * for fdb event notifications.
   */
  std::vector<FdbEventNotificationData> fdbEventNotificationDataTmp;
  for (auto i = 0; i < count; i++) {
    BridgePortSaiId bridgePortSaiId{0};
    sai_uint32_t fdbMetaData = 0;
    for (auto j = 0; j < data[i].attr_count; j++) {
      switch (data[i].attr[j].id) {
        case SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID:
          bridgePortSaiId = data[i].attr[j].value.oid;
          break;
        case SAI_FDB_ENTRY_ATTR_META_DATA:
          fdbMetaData = data[i].attr[j].value.u32;
          break;
        default:
          break;
      }
    }
    fdbEventNotificationDataTmp.emplace_back(
        data[i].event_type, data[i].fdb_entry, bridgePortSaiId, fdbMetaData);
    XLOG(DBG4) << "Received FDB event: " << fdbEventToString(data[i].event_type)
               << " for bridge port: " << bridgePortSaiId;
  }
  fdbEventBottomHalfEventBase_.runInFbossEventBaseThread(
      [this,
       fdbNotifications = std::move(fdbEventNotificationDataTmp)]() mutable {
        fdbEventCallbackLockedBottomHalf(std::move(fdbNotifications));
      });
}

void SaiSwitch::fdbEventCallbackLockedBottomHalf(
    std::vector<FdbEventNotificationData> fdbNotifications) {
  if (getSwitchRunState() == SwitchRunState::EXITING) {
    return;
  }
  std::vector<std::pair<L2Entry, L2EntryUpdateType>> l2Entries;
  {
    auto lock = std::lock_guard<std::mutex>(saiSwitchMutex_);
    if (managerTable_->bridgeManager().getL2LearningMode() !=
        cfg::L2LearningMode::SOFTWARE) {
      // Some platforms call fdb callback even when mode is set to HW. In
      // keeping with our native SDK approach, don't send these events up.
      XLOG(DBG4) << "Ignoring FDB learn notification as mode is not software";
      return;
    }

    for (const auto& fdbNotification : fdbNotifications) {
      auto ditr =
          kL2AddrUpdateOperationsOfInterest.find(fdbNotification.eventType);
      if (ditr != kL2AddrUpdateOperationsOfInterest.end()) {
        auto updateTypeStr = fdbEventToString(ditr->first);
        auto l2Entry = getL2Entry(fdbNotification);
        if (l2Entry) {
          XLOG(DBG2) << "Received FDB " << updateTypeStr
                     << " notification for: " << l2Entry->str();
          l2Entries.emplace_back(l2Entry.value(), ditr->second);
        }
      }
    }
  }

  for (auto& l2Entry : l2Entries) {
    callback_->l2LearningUpdateReceived(l2Entry.first, l2Entry.second);
  }
}

std::optional<L2Entry> SaiSwitch::getL2Entry(
    const FdbEventNotificationData& fdbEvent) const {
  /*
   * Idea behind returning a optional here on spurious FDB event
   * (say with bogus port or vlan ID) vs throwing a exception
   * is to protect the application from crashes in face of
   * spurious events. This is similar to the philosophy we follow
   * for spurious linkstate change or packet RX callbacks.
   */
  if (!fdbEvent.bridgePortSaiId) {
    XLOG(ERR) << "Missing bridge port attribute in FDB event";
    return std::nullopt;
  }

  // We could recive learn event when bridge port is not present.
  // Ignore SDK error. Refer D74672820 for details.
  sai_object_id_t portOrLagSaiId = SAI_NULL_OBJECT_ID;
  try {
    portOrLagSaiId = SaiApiTable::getInstance()->bridgeApi().getAttribute(
        fdbEvent.bridgePortSaiId, SaiBridgePortTraits::Attributes::PortId{});
  } catch (const SaiApiError& e) {
    if (e.getSaiStatus() == SAI_STATUS_ITEM_NOT_FOUND) {
      XLOG(ERR)
          << "Bridge port is not found in SDK, may be we deleted it during port re-create";
      return std::nullopt;
    }
    throw;
  }

  L2Entry::L2EntryType entryType{L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING};
  auto mac = fromSaiMacAddress(fdbEvent.fdbEntry.mac_address);
  switch (fdbEvent.eventType) {
    // For learning events consider entry type as pending (else the
    // fdb event should not have been punted to us)
    // For Aging events, consider the entry as already validated (programmed)
    case SAI_FDB_EVENT_LEARNED:
      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::PENDING_L2_ENTRY)) {
        entryType = L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING;
      } else {
        entryType = L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED;
      }
      break;
    case SAI_FDB_EVENT_AGED:
      entryType = L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED;
      break;
    default:
      XLOG(ERR) << "Unexpected fdb event type: " << fdbEvent.eventType
                << " for " << mac;
      return std::nullopt;
  }

  // fdb learn event can happen over bridge port of lag or port
  auto portType = sai_object_type_query(portOrLagSaiId);
  if (portType != SAI_OBJECT_TYPE_PORT && portType != SAI_OBJECT_TYPE_LAG) {
    XLOG(ERR) << "FDB event for unexpected object type "
              << saiObjectTypeToString(portType);
    return std::nullopt;
  }
  PortDescriptorSaiId portDescSaiId = (portType == SAI_OBJECT_TYPE_PORT)
      ? PortDescriptorSaiId(PortSaiId(portOrLagSaiId))
      : PortDescriptorSaiId(LagSaiId(portOrLagSaiId));
  std::optional<PortDescriptor> portDesc{};
  switch (portDescSaiId.type()) {
    case PortDescriptorSaiId::PortType::PHYSICAL: {
      auto portItr = concurrentIndices_->portSaiId2PortInfo.find(
          portDescSaiId.phyPortID());
      if (portItr != concurrentIndices_->portSaiId2PortInfo.end()) {
        portDesc = PortDescriptor(portItr->second.portID);
      }
    } break;

    case PortDescriptorSaiId::PortType::AGGREGATE: {
      auto aggregatePortItr =
          concurrentIndices_->aggregatePortIds.find(portDescSaiId.aggPortID());
      if (aggregatePortItr != concurrentIndices_->aggregatePortIds.end()) {
        portDesc = PortDescriptor(aggregatePortItr->second);
      }
    }; break;
    case PortDescriptorSaiId::PortType::SYSTEM_PORT:
      XLOG(FATAL) << " Unexpected fdb event for sys port";
      break;
  }

  if (!portDesc) {
    XLOG(ERR) << " FDB event for " << mac
              << ", got non existent port id : " << portDescSaiId.str();
    return std::nullopt;
  }

  auto vlanItr = concurrentIndices_->vlanIds.find(portDescSaiId);
  if (vlanItr == concurrentIndices_->vlanIds.end()) {
    XLOG(ERR) << " FDB event for " << mac
              << " could not look up VLAN for : " << portDescSaiId.str();
    return std::nullopt;
  }

  auto classID = fdbEvent.fdbMetaData != 0
      ? std::optional<cfg::AclLookupClass>(
            cfg::AclLookupClass(fdbEvent.fdbMetaData))
      : std::nullopt;

  return L2Entry{
      fromSaiMacAddress(fdbEvent.fdbEntry.mac_address),
      vlanItr->second,
      portDesc.value(),
      entryType,
      classID};
}

template <
    typename Delta,
    typename Manager,
    typename LockPolicyT,
    typename... Args,
    typename ChangeFunc,
    typename AddedFunc,
    typename RemovedFunc>
void SaiSwitch::processDelta(
    Delta delta,
    Manager& manager,
    const LockPolicyT& lockPolicy,
    ChangeFunc changedFunc,
    AddedFunc addedFunc,
    RemovedFunc removedFunc,
    Args... args) {
  DeltaFunctions::forEachChanged(
      delta,
      [&](auto removed, auto added) {
        [[maybe_unused]] const auto& lock = lockPolicy.lock();
        (manager.*changedFunc)(removed, added, args...);
      },
      [&](auto added) {
        [[maybe_unused]] const auto& lock = lockPolicy.lock();
        (manager.*addedFunc)(added, args...);
      },
      [&](auto removed) {
        [[maybe_unused]] const auto& lock = lockPolicy.lock();
        (manager.*removedFunc)(removed, args...);
      });
}

template <
    typename Delta,
    typename Manager,
    typename LockPolicyT,
    typename... Args,
    typename ChangeFunc>
void SaiSwitch::processChangedDelta(
    Delta delta,
    Manager& manager,
    const LockPolicyT& lockPolicy,
    ChangeFunc changedFunc,
    Args... args) {
  DeltaFunctions::forEachChanged(delta, [&](auto added, auto removed) {
    [[maybe_unused]] const auto& lock = lockPolicy.lock();
    (manager.*changedFunc)(added, removed, args...);
  });
}

template <
    typename Delta,
    typename Manager,
    typename LockPolicyT,
    typename... Args,
    typename AddedFunc>
void SaiSwitch::processAddedDelta(
    Delta delta,
    Manager& manager,
    const LockPolicyT& lockPolicy,
    AddedFunc addedFunc,
    Args... args) {
  DeltaFunctions::forEachAdded(delta, [&](auto added) {
    [[maybe_unused]] const auto& lock = lockPolicy.lock();
    (manager.*addedFunc)(added, args...);
  });
}

template <
    typename Delta,
    typename Manager,
    typename LockPolicyT,
    typename... Args,
    typename RemovedFunc>
void SaiSwitch::processRemovedDelta(
    Delta delta,
    Manager& manager,
    const LockPolicyT& lockPolicy,
    RemovedFunc removedFunc,
    Args... args) {
  DeltaFunctions::forEachRemoved(delta, [&](auto removed) {
    [[maybe_unused]] const auto& lock = lockPolicy.lock();
    (manager.*removedFunc)(removed, args...);
  });
}

template <typename LockPolicyT, typename AddrT>
void SaiSwitch::processChangedRoutesDeltaInReverse(
    const RouterID& routerID,
    const auto& routesDelta,
    const LockPolicyT& lockPolicy,
    const std::shared_ptr<SwitchState>& state) {
  // During rollback, process changed routes in reverse order
  std::vector<
      std::pair<std::shared_ptr<Route<AddrT>>, std::shared_ptr<Route<AddrT>>>>
      changedRoutes;

  DeltaFunctions::forEachChanged(
      routesDelta,
      [&changedRoutes](
          const std::shared_ptr<Route<AddrT>>& oldRoute,
          const std::shared_ptr<Route<AddrT>>& newRoute) {
        changedRoutes.push_back({oldRoute, newRoute});
      });

  for (auto it = changedRoutes.rbegin(); it != changedRoutes.rend(); ++it) {
    [[maybe_unused]] const auto& lock = lockPolicy.lock();
    managerTable_->routeManager().changeRoute<AddrT>(
        it->first, it->second, routerID, state);
  }
}

template <typename LockPolicyT, typename AddrT>
void SaiSwitch::processAddedRoutesDeltaInReverse(
    const RouterID& routerID,
    const auto& routesDelta,
    const LockPolicyT& lockPolicy,
    const std::shared_ptr<SwitchState>& state) {
  // During rollback, process added routes in reverse order
  std::vector<std::shared_ptr<Route<AddrT>>> addedRoutes;

  DeltaFunctions::forEachAdded(
      routesDelta, [&addedRoutes](const std::shared_ptr<Route<AddrT>>& route) {
        addedRoutes.push_back(route);
      });

  for (auto it = addedRoutes.rbegin(); it != addedRoutes.rend(); ++it) {
    [[maybe_unused]] const auto& lock = lockPolicy.lock();
    managerTable_->routeManager().addRoute<AddrT>(*it, routerID, state);
  }
}

template <typename LockPolicyT, typename AddrT>
void SaiSwitch::processRemovedRoutesDeltaInReverse(
    const RouterID& routerID,
    const auto& routesDelta,
    const LockPolicyT& lockPolicy) {
  // During rollback, process removed routes in reverse order
  std::vector<std::shared_ptr<Route<AddrT>>> removedRoutes;

  DeltaFunctions::forEachRemoved(
      routesDelta,
      [&removedRoutes](const std::shared_ptr<Route<AddrT>>& route) {
        removedRoutes.push_back(route);
      });

  for (auto it = removedRoutes.rbegin(); it != removedRoutes.rend(); ++it) {
    [[maybe_unused]] const auto& lock = lockPolicy.lock();
    managerTable_->routeManager().removeRoute<AddrT>(*it, routerID);
  }
}

void SaiSwitch::dumpDebugState(const std::string& path) const {
  XLOG(INFO) << "generating debug dump at " << path;
  saiCheckError(sai_dbg_generate_dump(path.c_str()));
}

void SaiSwitch::setSdkRegDumpEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  setSdkRegDumpEnabledLocked(lock, enabled);
}

void SaiSwitch::setSdkRegDumpEnabledLocked(
    const std::lock_guard<std::mutex>& /*lock*/,
    bool enabled) {
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::SDK_REGISTER_DUMP)) {
    throw FbossError("SDK register dump is not supported on this device");
  }
  // The SDK dumps register/state logs to this path; an empty path disables the
  // dump. Computed here so this is the only place that builds the dump path.
  const std::string path = enabled
      ? folly::to<std::string>(
            FLAGS_sdk_reg_dump_path_prefix, "_", FLAGS_switchIndex, ".log")
      : std::string();
  // SAI expects a null-terminated char array, so copy the trailing '\0'.
  std::vector<int8_t> sdkRegDumpLogPathArray;
  std::copy(
      path.c_str(),
      path.c_str() + path.size() + 1,
      std::back_inserter(sdkRegDumpLogPathArray));
  std::optional<SaiSwitchTraits::Attributes::SdkRegDumpLogPath>
      sdkRegDumpLogPath{sdkRegDumpLogPathArray};
  auto& switchApi = SaiApiTable::getInstance()->switchApi();
  switchApi.setAttribute(saiSwitchId_, sdkRegDumpLogPath);
  if (enabled) {
    XLOG(INFO) << "Enabled SDK register/state dump to '" << path << "'";
  } else {
    XLOG(INFO) << "Disabled SDK register/state dump";
  }
}

std::string SaiSwitch::listCachedObjectsLocked(
    const std::vector<sai_object_type_t>& objects,
    const SaiStore* store,
    const FineGrainedLockPolicy& policy) const {
  std::string output;
  std::for_each(
      objects.begin(), objects.end(), [&output, &policy, store](auto objType) {
        auto lock = policy.lock();
        output += store->storeStr(objType);
      });
  return output;
}

std::string SaiSwitch::listObjectsLocked(
    const std::vector<sai_object_type_t>& objects,
    bool cached,
    const FineGrainedLockPolicy& policy) const {
  if (cached) {
    return listCachedObjectsLocked(objects, saiStore_.get(), policy);
  }

  /* We're making a change to ensure that the listObjectsLocked functionality
   * remains unchanged for Credo 0.7.2, We added
   * HwAsic::Feature::OBJECT_KEY_CACHE support for Credo Asic But for 0.7.2,
   * we want to avoid loading the adapterKeysJson file, and keep the
   * functionality as it was before.
   */
  bool useAdapterKeysJson =
      platform_->getAsic()->isSupported(HwAsic::Feature::OBJECT_KEY_CACHE);
#if CREDO_SDK_VERSION < CREDO_SDK_VERSION_0_9_0
  if (getPlatform()->getAsic()->getAsicType() ==
      cfg::AsicType::ASIC_TYPE_ELBERT_8DD) {
    useAdapterKeysJson = false;
  }
#endif

  auto reconstructStore = [this, objects, useAdapterKeysJson](
                              const std::lock_guard<std::mutex>& lock) {
    std::unique_ptr<SaiStore> directToHwStore{};

    directToHwStore = std::make_unique<SaiStore>(getSaiSwitchId());
    auto json = toFollyDynamicLocked(lock);
    std::unique_ptr<folly::dynamic> adapterKeysJson;
    if (useAdapterKeysJson) {
      adapterKeysJson = std::make_unique<folly::dynamic>(json[kAdapterKeys]);
    }

    std::unique_ptr<folly::dynamic> adapterKeys2AdapterHostKeysJson =
        std::make_unique<folly::dynamic>(json[kAdapterKey2AdapterHostKey]);
    directToHwStore->reload(
        adapterKeysJson.get(), adapterKeys2AdapterHostKeysJson.get(), objects);
    return directToHwStore;
  };

  auto directToHwStore = reconstructStore(policy.lock());
  return directToHwStore->storeStr(objects);
}

std::string SaiSwitch::listObjects(
    const std::vector<HwObjectType>& types,
    bool cached) const {
  bool listManagedObjects{false};
  std::vector<sai_object_type_t> objTypes;
  for (auto type : types) {
    switch (type) {
      case HwObjectType::PORT:
        objTypes.push_back(SAI_OBJECT_TYPE_PORT);
        objTypes.push_back(SAI_OBJECT_TYPE_PORT_SERDES);
        objTypes.push_back(SAI_OBJECT_TYPE_PORT_CONNECTOR);
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
        objTypes.push_back(SAI_OBJECT_TYPE_PORT_LLR_PROFILE);
#endif
        break;
      case HwObjectType::LAG:
        objTypes.push_back(SAI_OBJECT_TYPE_LAG);
        objTypes.push_back(SAI_OBJECT_TYPE_LAG_MEMBER);
        break;
      case HwObjectType::VIRTUAL_ROUTER:
        objTypes.push_back(SAI_OBJECT_TYPE_VIRTUAL_ROUTER);
        break;
      case HwObjectType::NEXT_HOP:
        objTypes.push_back(SAI_OBJECT_TYPE_NEXT_HOP);
        break;
      case HwObjectType::NEXT_HOP_GROUP:
        objTypes.push_back(SAI_OBJECT_TYPE_NEXT_HOP_GROUP);
        objTypes.push_back(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER);
        break;
      case HwObjectType::NEXT_HOP_GROUP_MEMBER:
        objTypes.push_back(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER);
        break;
      case HwObjectType::ROUTER_INTERFACE:
        objTypes.push_back(SAI_OBJECT_TYPE_ROUTER_INTERFACE);
        break;
      case HwObjectType::CPU_TRAP:
        objTypes.push_back(SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP);
        objTypes.push_back(SAI_OBJECT_TYPE_HOSTIF_TRAP);
        break;
      case HwObjectType::HASH:
        objTypes.push_back(SAI_OBJECT_TYPE_HASH);
        break;
      case HwObjectType::MIRROR:
        objTypes.push_back(SAI_OBJECT_TYPE_MIRROR_SESSION);
        break;
      case HwObjectType::QOS_MAP:
        objTypes.push_back(SAI_OBJECT_TYPE_QOS_MAP);
        break;
      case HwObjectType::QUEUE:
        objTypes.push_back(SAI_OBJECT_TYPE_QUEUE);
        objTypes.push_back(SAI_OBJECT_TYPE_WRED);
        break;
      case HwObjectType::SCHEDULER:
        objTypes.push_back(SAI_OBJECT_TYPE_SCHEDULER);
        break;
      case HwObjectType::L2_ENTRY:
        objTypes.push_back(SAI_OBJECT_TYPE_FDB_ENTRY);
        break;
      case HwObjectType::NEIGHBOR_ENTRY:
        objTypes.push_back(SAI_OBJECT_TYPE_NEIGHBOR_ENTRY);
        break;
      case HwObjectType::ROUTE_ENTRY:
        objTypes.push_back(SAI_OBJECT_TYPE_ROUTE_ENTRY);
        break;
      case HwObjectType::VLAN:
        objTypes.push_back(SAI_OBJECT_TYPE_VLAN);
        objTypes.push_back(SAI_OBJECT_TYPE_VLAN_MEMBER);
        break;
      case HwObjectType::BRIDGE:
        objTypes.push_back(SAI_OBJECT_TYPE_BRIDGE);
        objTypes.push_back(SAI_OBJECT_TYPE_BRIDGE_PORT);
        break;
      case HwObjectType::BUFFER:
        objTypes.push_back(SAI_OBJECT_TYPE_BUFFER_POOL);
        objTypes.push_back(SAI_OBJECT_TYPE_BUFFER_PROFILE);
        break;
      case HwObjectType::ACL:
        objTypes.push_back(SAI_OBJECT_TYPE_ACL_TABLE_GROUP);
        objTypes.push_back(SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER);
        objTypes.push_back(SAI_OBJECT_TYPE_ACL_TABLE);
        objTypes.push_back(SAI_OBJECT_TYPE_ACL_ENTRY);
        objTypes.push_back(SAI_OBJECT_TYPE_ACL_COUNTER);
        break;
      case HwObjectType::DEBUG_COUNTER:
        objTypes.push_back(SAI_OBJECT_TYPE_DEBUG_COUNTER);
        break;
      case HwObjectType::TELEMETRY:
        objTypes.push_back(SAI_OBJECT_TYPE_TAM_COLLECTOR);
        objTypes.push_back(SAI_OBJECT_TYPE_TAM_TRANSPORT);
        objTypes.push_back(SAI_OBJECT_TYPE_TAM_REPORT);
        objTypes.push_back(SAI_OBJECT_TYPE_TAM_EVENT_ACTION);
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
        objTypes.push_back(
            static_cast<sai_object_type_t>(
                SAI_OBJECT_TYPE_TAM_EVENT_AGING_GROUP));
#endif
        objTypes.push_back(SAI_OBJECT_TYPE_TAM_EVENT);
        objTypes.push_back(SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD);
        objTypes.push_back(SAI_OBJECT_TYPE_TAM);
        break;
      case HwObjectType::LABEL_ENTRY:
        objTypes.push_back(SAI_OBJECT_TYPE_INSEG_ENTRY);
        break;
      case HwObjectType::MACSEC:
        objTypes.push_back(SAI_OBJECT_TYPE_MACSEC);
        objTypes.push_back(SAI_OBJECT_TYPE_MACSEC_PORT);
        objTypes.push_back(SAI_OBJECT_TYPE_MACSEC_FLOW);
        objTypes.push_back(SAI_OBJECT_TYPE_MACSEC_SC);
        objTypes.push_back(SAI_OBJECT_TYPE_MACSEC_SA);
        break;
      case HwObjectType::SAI_MANAGED_OBJECTS:
        listManagedObjects = true;
        break;
      case HwObjectType::IPTUNNEL:
        objTypes.push_back(SAI_OBJECT_TYPE_TUNNEL);
        objTypes.push_back(SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY);
        break;
      case HwObjectType::SYSTEM_PORT:
        objTypes.push_back(SAI_OBJECT_TYPE_SYSTEM_PORT);
        break;
      case HwObjectType::FIRMWARE:
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
        objTypes.push_back(
            static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_FIRMWARE));
#endif
        break;
      case HwObjectType::SRV6:
        objTypes.push_back(SAI_OBJECT_TYPE_TUNNEL);
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
        objTypes.push_back(SAI_OBJECT_TYPE_SRV6_SIDLIST);
        objTypes.push_back(SAI_OBJECT_TYPE_MY_SID_ENTRY);
#endif
        break;
      case HwObjectType::VXLAN:
        objTypes.push_back(SAI_OBJECT_TYPE_TUNNEL);
        objTypes.push_back(SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY);
        objTypes.push_back(SAI_OBJECT_TYPE_TUNNEL_MAP);
        objTypes.push_back(SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY);
        break;
      case HwObjectType::SAMPLE_PACKET:
        objTypes.push_back(SAI_OBJECT_TYPE_SAMPLEPACKET);
        break;
      case HwObjectType::MY_MAC:
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
        objTypes.push_back(SAI_OBJECT_TYPE_MY_MAC);
#endif
        break;
    }
  }
  FineGrainedLockPolicy policy(saiSwitchMutex_);
  auto output = listObjectsLocked(objTypes, cached, policy);
  if (listManagedObjects) {
    listManagedObjectsLocked(output, policy);
  }
  return output;
}

void SaiSwitch::listManagedObjectsLocked(
    std::string& output,
    const FineGrainedLockPolicy& policy) const {
  auto listObjectsLocked = [&output](
                               const auto& objectMgr,
                               const std::lock_guard<std::mutex>& /*lock*/) {
    output += objectMgr.listManagedObjects();
  };
  output += "\nmanaged sai objects\n";
  listObjectsLocked(managerTable_->fdbManager(), policy.lock());
  listObjectsLocked(managerTable_->neighborManager(), policy.lock());
  listObjectsLocked(managerTable_->nextHopManager(), policy.lock());
  listObjectsLocked(managerTable_->nextHopGroupManager(), policy.lock());
}

uint32_t SaiSwitch::generateDeterministicSeed(
    LoadBalancerID loadBalancerID,
    folly::MacAddress platformMac) const {
  return utility::generateDeterministicSeed(loadBalancerID, platformMac, true);
}

phy::FecMode SaiSwitch::getPortFECMode(PortID portId) const {
  return managerTable_->portManager().getFECMode(portId);
}

void SaiSwitch::rollbackInTest(const StateDelta& delta) {
  std::vector<StateDelta> deltas;
  deltas.emplace_back(std::make_shared<SwitchState>(), delta.oldState());
  rollback(deltas);
  setProgrammedState(delta.oldState());
}

template <typename LockPolicyT>
void SaiSwitch::processAclTableGroupDelta(
    const StateDelta& delta,
    const AclTableGroupMap& aclTableGroupMap,
    const LockPolicyT& lockPolicy) {
  bool multipleAclTableSupport =
      platform_->getAsic()->isSupported(HwAsic::Feature::MULTIPLE_ACL_TABLES);
  for (const auto& [_, tableGroup] : aclTableGroupMap) {
    auto aclStage = tableGroup->getID();
    auto bindPoint = tableGroup->getBindPoint();
    auto oldAclTables =
        getAclTablesForBindPoint(delta.oldState(), aclStage, bindPoint);
    auto newAclTables =
        getAclTablesForBindPoint(delta.newState(), aclStage, bindPoint);
    auto aclTablesDelta =
        ThriftMapDelta<AclTableMap>(oldAclTables.get(), newAclTables.get());
    if (aclTablesDelta.getNew() && aclTablesDelta.getNew()->size() > 1 &&
        !multipleAclTableSupport) {
      throw FbossError(
          "multiple ACL tables configured, but platform only support one ACL table");
    }
    if (bindPoint == cfg::AclTableGroupBindPoint::PORT) {
      // Defer port-bound table removals until after ports are rebound, so no
      // port can reference a removed SAI ACL table.
      processChangedDelta(
          aclTablesDelta,
          managerTable_->aclTableManager(),
          lockPolicy,
          &SaiAclTableManager::changedAclTable,
          aclStage,
          delta.newState(),
          bindPoint);
      processAddedDelta(
          aclTablesDelta,
          managerTable_->aclTableManager(),
          lockPolicy,
          &SaiAclTableManager::addAclTable,
          aclStage,
          delta.newState(),
          bindPoint);
    } else {
      processDelta(
          aclTablesDelta,
          managerTable_->aclTableManager(),
          lockPolicy,
          &SaiAclTableManager::changedAclTable,
          &SaiAclTableManager::addAclTable,
          &SaiAclTableManager::removeAclTable,
          aclStage,
          delta.newState(),
          bindPoint);
    }

    if (aclTablesDelta.getNew()) {
      // Process delta for the entries of each table in the new state
      for (const auto& iter : std::as_const(*aclTablesDelta.getNew())) {
        auto table = iter.second;
        auto tableName = table->getID();
        processRemovedDelta(
            delta.getAclsDelta(aclStage, tableName),
            managerTable_->aclTableManager(),
            lockPolicy,
            &SaiAclTableManager::removeAclEntry,
            tableName,
            delta.newState());
        processChangedDelta(
            delta.getAclsDelta(aclStage, tableName),
            managerTable_->aclTableManager(),
            lockPolicy,
            &SaiAclTableManager::changedAclEntry,
            tableName,
            delta.newState());
        processAddedDelta(
            delta.getAclsDelta(aclStage, tableName),
            managerTable_->aclTableManager(),
            lockPolicy,
            &SaiAclTableManager::addAclEntry,
            tableName,
            delta.newState());
      }
    }
  }
}

void SaiSwitch::initialStateApplied() {
  managerTable_->fdbManager().removeUnclaimedDynanicEntries();
  managerTable_->hashManager().removeUnclaimedDefaultHash();
  // TODO(nivinl): Remove unclaimed WRED profiles to handle warm
  // boot transitions with the introduction of ECN probability
  // attribute. Remove this once binary with ECN probabilistic
  // marking is rolled out to prod.
  managerTable_->wredManager().removeUnclaimedWredProfile();
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
  // Sai spec 1.10.2 introduces the new attribute of Label for Acl counter.
  // Therefore, counters created before sai spec 1.10.2 will be treated as
  // unclaimed since they have no label (and SDK gives default values to the
  // label field).
  managerTable_->aclTableManager().removeUnclaimedAclCounter();
#endif
  if (bootType_ == BootType::WARM_BOOT) {
    XLOG(DBG2) << "Warm boot: removing unreferenced handles";
    saiStore_->printWarmbootHandles();
    if (FLAGS_check_wb_handles == true) {
      saiStore_->checkUnexpectedUnclaimedWarmbootHandles();
    } else {
      saiStore_->removeUnexpectedUnclaimedWarmbootHandles();
    }

    /* handle the case where warm boot happened before sw switch could be
     * notified and process link down event. this retains objects such as
     * static fdb entry and neighbor. reapplying warm boot state would expand
     * ecmp group to a port that is down. rectify that as quick as possible to
     * prevent drops. */
    std::vector<sai_port_oper_status_notification_t> portStatus{};
    for (auto entry : saiStore_->get<SaiPortTraits>()) {
      auto port = entry.second.lock();
      auto portOperStatus = SaiApiTable::getInstance()->portApi().getAttribute(
          port->adapterKey(), SaiPortTraits::Attributes::OperStatus{});
      sai_port_oper_status_notification_t notification{};
      notification.port_id = port->adapterKey();
      notification.port_state =
          static_cast<sai_port_oper_status_t>(portOperStatus);
      portStatus.push_back(notification);
    }
    linkStateChangedCallbackBottomHalf(std::move(portStatus));
  }
}

void SaiSwitch::processPfcDeadlockNotificationCallback(
    std::optional<cfg::PfcWatchdogRecoveryAction> /*oldRecoveryAction*/,
    std::optional<cfg::PfcWatchdogRecoveryAction> newRecoveryAction) {
  // Needed if PFC watchdog enabled status changes at device level
  if (newRecoveryAction.has_value() != pfcDeadlockEnabled_) {
    pfcDeadlockEnabled_ = newRecoveryAction.has_value();
    auto& switchApi = SaiApiTable::getInstance()->switchApi();
    if (pfcDeadlockEnabled_) {
      // Register the PFC deadlock recovery callback function
      switchApi.registerQueuePfcDeadlockNotificationCallback(
          saiSwitchId_, _gPfcDeadlockNotificationCallback);
    } else {
      // Unregister the PFC deadlock recovery callback function
      switchApi.unregisterQueuePfcDeadlockNotificationCallback(saiSwitchId_);
    }
    auto registration = pfcDeadlockEnabled_ ? "registered" : "unregistered";
    XLOG(DBG4) << "PFC deadlock notification callback " << registration;
  } else {
    XLOG(DBG4) << "PFC deadlock notification callback unchanged!";
  }
}

void SaiSwitch::processPfcDeadlockRecoveryAction(
    std::optional<cfg::PfcWatchdogRecoveryAction> recoveryAction) {
  int currentPfcDlrPacketAction =
      SaiApiTable::getInstance()->switchApi().getAttribute(
          saiSwitchId_, SaiSwitchTraits::Attributes::PfcDlrPacketAction{});
  std::optional<int> newPfcDlrPacketAction;
  auto recoveryActionConfig = cfg::PfcWatchdogRecoveryAction::NO_DROP;
  if (recoveryAction.has_value() &&
      (*recoveryAction == cfg::PfcWatchdogRecoveryAction::DROP)) {
    newPfcDlrPacketAction = SAI_PACKET_ACTION_DROP;
    recoveryActionConfig = *recoveryAction;
  } else {
    newPfcDlrPacketAction = SAI_PACKET_ACTION_FORWARD;
  }
  CHECK(newPfcDlrPacketAction.has_value());
  if (currentPfcDlrPacketAction != *newPfcDlrPacketAction) {
    SaiApiTable::getInstance()->switchApi().setAttribute(
        saiSwitchId_,
        SaiSwitchTraits::Attributes::PfcDlrPacketAction{
            *newPfcDlrPacketAction});
    XLOG(DBG2) << "PFC watchdog deadlock recovery action "
               << apache::thrift::util::enumNameSafe(recoveryActionConfig)
               << " programmed!";
  } else {
    XLOG(DBG4) << "PFC deadlock recovery packet action unchanged!";
  }
}

void SaiSwitch::processPfcWatchdogGlobalDeltaLocked(
    const StateDelta& delta,
    const std::lock_guard<std::mutex>& /* lock */) {
  auto oldRecoveryAction = delta.oldState()->getPfcWatchdogRecoveryAction();
  auto newRecoveryAction = delta.newState()->getPfcWatchdogRecoveryAction();

  if (oldRecoveryAction != newRecoveryAction) {
    processPfcDeadlockNotificationCallback(
        oldRecoveryAction, newRecoveryAction);
    processPfcDeadlockRecoveryAction(newRecoveryAction);
  }
}

template <typename LockPolicyT>
void SaiSwitch::processPfcWatchdogGlobalDelta(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  processPfcWatchdogGlobalDeltaLocked(delta, lockPolicy.lock());
}

template <typename LockPolicyT>
void SaiSwitch::processFlowletSwitchingConfigAdded(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  [[maybe_unused]] const auto& lock = lockPolicy.lock();
  [[maybe_unused]] const auto flowletSwitchingDelta =
      delta.getFlowletSwitchingConfigDelta();

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  const auto& oldFlowletConfig = flowletSwitchingDelta.getOld();
  const auto& newFlowletConfig = flowletSwitchingDelta.getNew();

  auto& switchManager = managerTable_->switchManager();
  auto& arsManager = managerTable_->arsManager();
  auto& arsProfileManager = managerTable_->arsProfileManager();
  auto& nextHopGroupManager = managerTable_->nextHopGroupManager();

  if (newFlowletConfig && !oldFlowletConfig) {
    XLOG(DBG2) << "Flowlet switching config is added";
    nextHopGroupManager.setPrimaryArsSwitchingMode(
        newFlowletConfig->getSwitchingMode());
    nextHopGroupManager.setMinWidthForArsVirtualGroup(
        newFlowletConfig->getMinWidthForArsVirtualGroup());
    // create the ARS profile object and attach to switch
    arsProfileManager.addArsProfile(newFlowletConfig);
    auto arsProfileHandlePtr = arsProfileManager.getArsProfileHandle();
    CHECK(arsProfileHandlePtr);
    switchManager.setArsProfile(arsProfileHandlePtr->arsProfile->adapterKey());

    // create the ARS object and attach to all ECMP groups
    arsManager.addArs(
        newFlowletConfig,
        delta.newState()->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS));
    auto arsHandlePtr = arsManager.getArsHandle();
    CHECK(arsHandlePtr);
    nextHopGroupManager.updateArsModeAll(newFlowletConfig);
  }
#endif
}

template <typename LockPolicyT>
void SaiSwitch::processFlowletSwitchingConfigChanged(
    const StateDelta& delta,
    const LockPolicyT& lockPolicy) {
  [[maybe_unused]] const auto& lock = lockPolicy.lock();
  [[maybe_unused]] const auto flowletSwitchingDelta =
      delta.getFlowletSwitchingConfigDelta();

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  const auto& oldFlowletConfig = flowletSwitchingDelta.getOld();
  const auto& newFlowletConfig = flowletSwitchingDelta.getNew();

  auto& switchManager = managerTable_->switchManager();
  auto& arsManager = managerTable_->arsManager();
  auto& arsProfileManager = managerTable_->arsProfileManager();
  auto& nextHopGroupManager = managerTable_->nextHopGroupManager();

  if (oldFlowletConfig && newFlowletConfig) {
    if (*oldFlowletConfig == *newFlowletConfig) {
      // SwitchSettings can change while the flowlet config stays put.
      if (delta.oldState()->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS) !=
          delta.newState()->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS)) {
        XLOG(DBG2) << "ARS split horizon changed, reprogramming ARS";
        arsManager.changeArs(
            oldFlowletConfig,
            newFlowletConfig,
            delta.newState()->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS));
      }
      XLOG(DBG5) << "Flowlet switching config is same";
      // flowlet is enabled here. lets walk through all ecmp objects to ensure
      // things look ok. For most purposes, this will be a no-op
      // One case this becomes useful is when a nhg went away and there are
      // other nhg's waiting to get flowlet resources, this will ensure they do
      // during a config update
      nextHopGroupManager.updateArsModeAll(newFlowletConfig);
      return;
    } else {
      XLOG(DBG2) << "Flowlet switching config is changed";
      // FlowletSwitchingConfig has both ARS_PROFILE and ARS info
      nextHopGroupManager.setPrimaryArsSwitchingMode(
          newFlowletConfig->getSwitchingMode());
      nextHopGroupManager.setMinWidthForArsVirtualGroup(
          newFlowletConfig->getMinWidthForArsVirtualGroup());
      arsProfileManager.changeArsProfile(oldFlowletConfig, newFlowletConfig);
      arsManager.changeArs(
          oldFlowletConfig,
          newFlowletConfig,
          delta.newState()->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS));
    }
  } else if (newFlowletConfig && !oldFlowletConfig) {
    nextHopGroupManager.updateArsModeAll(newFlowletConfig);
  } else if (oldFlowletConfig && !newFlowletConfig) {
    XLOG(DBG2) << "Flowlet switching config is removed";
    nextHopGroupManager.updateArsModeAll(newFlowletConfig);
    arsManager.removeArs(newFlowletConfig);
    switchManager.resetArsProfile();
    arsProfileManager.removeArsProfile();
    nextHopGroupManager.setPrimaryArsSwitchingMode(std::nullopt);
    nextHopGroupManager.setMinWidthForArsVirtualGroup(std::nullopt);
  }
#endif
}

void SaiSwitch::pfcDeadlockNotificationCallbackTopHalf(
    uint32_t count,
    const sai_queue_deadlock_notification_data_t* data) {
  std::vector<sai_queue_deadlock_notification_data_t> dataVec{};
  dataVec.reserve(count);
  std::copy(data, data + count, std::back_inserter(dataVec));
  pfcDeadlockNotificationBottomHalfEventBase_.runInEventBaseThread(
      [count, this, dataVec = std::move(dataVec)] {
        // create a new vector to hold the data and copy the data into it
        pfcDeadlockNotificationCallbackBottomHalf(count, dataVec.data());
      });
}

void SaiSwitch::pfcDeadlockNotificationCallbackBottomHalf(
    uint32_t count,
    const sai_queue_deadlock_notification_data_t* data) {
  if (getSwitchRunState() == SwitchRunState::EXITING) {
    return;
  }
  for (int idx = 0; idx < count; ++idx) {
    auto queueSaiId = static_cast<QueueSaiId>(data[idx].queue_id);
    XLOG(DBG2) << "queue_id " << data[idx].queue_id;
    auto queueInfo =
        managerTable_->queueManager().getQueueIndexAndPortSaiId(queueSaiId);
    if (!queueInfo.has_value()) {
      XLOG(ERR) << "PFC deadlock notification callback failure getting queue "
                   "ID/port SAI ID, port deadlock counter not incremented!";
      continue;
    }
    auto [qId, portSaiId] = *queueInfo;

    auto deadlockEvent = data[idx].event;
    const auto portItr = concurrentIndices_->portSaiId2PortInfo.find(portSaiId);
    if (portItr == concurrentIndices_->portSaiId2PortInfo.cend()) {
      XLOG(ERR) << "Unable to map Sai Port ID " << portSaiId << "in PFC"
                << " deadlock notification processing to a valid port!";
      continue;
    }
    PortID portId = portItr->second.portID;
    XLOG_EVERY_MS(WARNING, 5000)
        << "PFC deadlock notification callback invoked for qid: " << qId
        << ", on port: " << portId << ", with event: " << deadlockEvent
        << " and queueSaiId " << (int)(queueSaiId);
    std::lock_guard<std::mutex> locked(saiSwitchMutex_);
    switch (deadlockEvent) {
      case SAI_QUEUE_PFC_DEADLOCK_EVENT_TYPE_DETECTED:
        managerTable_->portManager().incrementPfcDeadlockCounter(portId);
        getSwitchStats()->pfcDeadlockDetectionCount(); // update HwSwitch stats
        break;
      case SAI_QUEUE_PFC_DEADLOCK_EVENT_TYPE_RECOVERED:
        managerTable_->portManager().incrementPfcRecoveryCounter(portId);
        getSwitchStats()->pfcDeadlockRecoveryCount(); // update HwSwitch stats
        break;
      default:
        XLOG(ERR) << "Unknown event " << deadlockEvent
                  << " in PFC deadlock notify callback";
    }
  }
}

void SaiSwitch::vendorSwitchEventNotificationCallback(
    sai_size_t bufferSize,
    const void* buffer,
    uint32_t eventType) {
  // The vendor switch event callback should at most increment a counter
  // and log the event, no additional processing expected. Hence not
  // splitting the callback to bottom / top half processing.
  managerTable_->vendorSwitchManager().vendorSwitchEventNotificationCallback(
      bufferSize, buffer, eventType);
}

TeFlowStats SaiSwitch::getTeFlowStats() const {
  // not implemented in SAI. Return empty stats
  return TeFlowStats();
}

HwFlowletStats SaiSwitch::getHwFlowletStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTable_->nextHopGroupManager().getHwFlowletStats();
}

std::vector<EcmpDetails> SaiSwitch::getAllEcmpDetails() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTable_->nextHopGroupManager().getAllEcmpDetails();
}

HwSwitchDropStats SaiSwitch::getSwitchDropStats() const {
  std::lock_guard<std::mutex> lk(saiSwitchMutex_);
  return managerTable_->switchManager().getSwitchDropStats();
}

HwSwitchDropBitmapStats SaiSwitch::getSwitchDropBitmapStats() const {
  std::lock_guard<std::mutex> lk(saiSwitchMutex_);
  return managerTable_->switchManager().getSwitchDropBitmapStats();
}

AclStats SaiSwitch::getAclStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTable_->aclTableManager().getAclStats();
}

cfg::SwitchingMode SaiSwitch::getFwdSwitchingMode(
    const RouteNextHopEntry& fwd) {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTable_->nextHopGroupManager().getNextHopGroupSwitchingMode(
      getNormalizedNextHops(getProgrammedState(), fwd));
}

HwSwitchWatermarkStats SaiSwitch::getSwitchWatermarkStats() const {
  std::lock_guard<std::mutex> lk(saiSwitchMutex_);
  return managerTable_->switchManager().getSwitchWatermarkStats();
}

HwSwitchPipelineStats SaiSwitch::getSwitchPipelineStats() const {
  std::lock_guard<std::mutex> lk(saiSwitchMutex_);
  return managerTable_->switchManager().getSwitchPipelineStats();
}

HwSwitchTemperatureStats SaiSwitch::getSwitchTemperatureStats() const {
  std::lock_guard<std::mutex> lk(saiSwitchMutex_);
  return managerTable_->switchManager().getSwitchTemperatureStats();
}

HwSwitchHardResetStats SaiSwitch::getHwSwitchHardResetStats() const {
  HwSwitchHardResetStats hardResetStats;
  hardResetStats.hard_reset_notification_received() =
      hardResetNotificationReceived_.load();
  return hardResetStats;
}

/*
 * On a FABRIC switch, from each virtual device, we want equal
 * number of connections to the peer devices. In absence of this
 * we get a asymmetry b/w sender and receiver, which can lead
 * to credit stalls/loss and generally poorer performance.
 * For illustration consider if the sender A has 4X100G
 * connecting to this virtual device, while the receiver B
 * has only 2X100G, in the trivial case this can lead to
 * sender overwhelming the receiver. Situation gets worse
 * if multiple senders have this asymmetry.
 *
 * To detect this, we prepare the following data structure
 * virtualDeviceId-> RemoteConnectionGroups
 * Where RemoteConnectionGroups is simply
 * map<numConnections, list<RemoteEndpoint (with num connections)>>
 * For symmetric topologies (prod use case), each virtual device should
 * have only a single RemoteConnectionGroup.
 */
void SaiSwitch::reportAsymmetricTopology() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  auto virtualDevice2RemoteConnectionGroups =
      getVirtualDeviceToRemoteConnectionGroupsLocked(lock);
  getSwitchStats()->virtualDevicesWithAsymmetricConnectivity(
      FabricConnectivityManager::virtualDevicesWithAsymmetricConnectivity(
          virtualDevice2RemoteConnectionGroups));
}

/*
 * Until later versions of the chip (B0), cable lengths seen by
 * each port group has a bearing on how efficiently FDR-in buffers on J3 are
 * utilized.
 * If all port groups see roughly the same cable length then they all dequeue
 * cells for a pkt to FDR-out buffers around the same time. If OTOH a port
 * groups sees substantially lower cable lengths, FDR-in corresponding
 * to that port group dequeues to FDR-out faster. This causes cells to
 * sit for longer in FDR-out buffers, leading to more stress on the
 * latter. Our cabling plans try to minimize this. Report the skew seen
 * here to corroborate that cabling was as desired
 */
void SaiSwitch::reportInterPortGroupCableSkew() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  if (getPlatform()->getAsic()->getAsicType() !=
          cfg::AsicType::ASIC_TYPE_JERICHO3 &&
      getPlatform()->getAsic()->getAsicType() !=
          cfg::AsicType::ASIC_TYPE_JERICHO4) {
    // Port group skew relevant only for J3/J4
    return;
  }
  std::map<PortID, uint32_t> portId2CableLen;
  auto& portIdStatsMap = managerTable_->portManager().getLastPortStats();
  for (auto& entry : portIdStatsMap) {
    if (entry.second->portStats().cableLengthMeters().has_value()) {
      portId2CableLen.insert(
          {entry.first, entry.second->portStats().cableLengthMeters().value()});
    }
  }
  auto portGroupSkew =
      getPlatform()->getAsic()->computePortGroupSkew(portId2CableLen);
  if (portGroupSkew) {
    getSwitchStats()->portGroupSkew(*portGroupSkew);
  }
}

std::shared_ptr<SwitchState> SaiSwitch::reconstructSwitchState() const {
  auto state = std::make_shared<SwitchState>();
  state->resetAclTableGroups(reconstructMultiSwitchAclTableGroupMap(
      cfg::AclTableGroupBindPoint::SWITCH));
  state->resetPortAclTableGroups(reconstructMultiSwitchAclTableGroupMap(
      cfg::AclTableGroupBindPoint::PORT));
  state->resetAcls(reconstructMultiSwitchAclMap());
  return state;
}

std::shared_ptr<MultiSwitchAclTableGroupMap>
SaiSwitch::reconstructMultiSwitchAclTableGroupMap(
    cfg::AclTableGroupBindPoint bindPoint) const {
  auto programmedState = getProgrammedState();
  const auto& aclTableGroups = bindPoint == cfg::AclTableGroupBindPoint::PORT
      ? programmedState->getPortAclTableGroups()
      : programmedState->getAclTableGroups();
  auto multiSwitchAclTableGroupMap =
      std::make_shared<MultiSwitchAclTableGroupMap>();
  for (const auto& [matcher, aclTableGroupMap] :
       std::as_const(*aclTableGroups)) {
    auto reconstructedAclTableGroupMap = std::make_shared<AclTableGroupMap>();
    for (const auto& [stage, aclTableGroup] :
         std::as_const(*aclTableGroupMap)) {
      auto reconstructedAclTableGroup =
          managerTable_->aclTableGroupManager().reconstructAclTableGroup(
              stage, aclTableGroup->getName());
      reconstructedAclTableGroup->setBindPoint(bindPoint);
      reconstructedAclTableGroupMap->addNode(reconstructedAclTableGroup);
    }
    multiSwitchAclTableGroupMap->addMapNode(
        reconstructedAclTableGroupMap, HwSwitchMatcher(matcher));
  }
  return multiSwitchAclTableGroupMap;
}

std::shared_ptr<MultiSwitchAclMap> SaiSwitch::reconstructMultiSwitchAclMap()
    const {
  auto reconstructedMultiSwitchAclMap = std::make_shared<MultiSwitchAclMap>();
  auto programmedState = getProgrammedState();
  for (const auto& [matcher, aclMap] :
       std::as_const(*programmedState->getAcls())) {
    for (const auto& [name, aclEntry] : std::as_const(*aclMap)) {
      auto reconstructedAclEntry =
          managerTable_->aclTableManager().reconstructAclEntry(
              cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
              name,
              aclEntry->getPriority());
      reconstructedMultiSwitchAclMap->addNode(
          reconstructedAclEntry, HwSwitchMatcher(matcher));
    }
  }
  return reconstructedMultiSwitchAclMap;
}

void SaiSwitch::injectSwitchReachabilityChangeNotification() {
  switchReachabilityChangeTopHalf();
}

HwResourceStats SaiSwitch::getResourceStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return hwResourceStats_;
}

HwSwitchCounterStats SaiSwitch::getHwSwitchCounterStats() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTable_->counterManager().getHwSwitchCounterStats();
}

// TODO: add support in SAI
bool SaiSwitch::getArsExhaustionStatus() {
  return false;
}

bool SaiSwitch::processVlanUntaggedPackets() const {
  return FLAGS_rx_vlan_untagged_packets ||
      (getSwitchType() == cfg::SwitchType::VOQ ||
       getSwitchType() == cfg::SwitchType::FABRIC) ||
      // P200 routes on port router interfaces and has no vlans, so the frames
      // it traps arrive untagged.
      asicType_ == cfg::AsicType::ASIC_TYPE_P200;
}

std::vector<FirmwareInfo> SaiSwitch::getAllFirmwareInfo() const {
  // Firmware is not supported/configured on all Platforms.
  // The platforms that support Firmware, support only single
  // Firmware today.
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  if (managerTable_->firmwareManager().isFirmwareEnabled()) {
    FirmwareInfo firmwareInfo;

    auto version = managerTable_->firmwareManager().getFirmwareVersion();
    CHECK(version.has_value());
    firmwareInfo.version() = version.value();

    auto opStatus = managerTable_->firmwareManager().getFirmwareOpStatus();
    CHECK(opStatus.has_value());
    firmwareInfo.opStatus() = opStatus.value();

    auto funcStatus = managerTable_->firmwareManager().getFirmwareFuncStatus();
    CHECK(funcStatus.has_value());
    firmwareInfo.funcStatus() = funcStatus.value();

    return {firmwareInfo};
  }

  return {};
}

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
void SaiSwitch::switchAsicSdkHealthNotificationTopHalf(
    SaiHealthNotification saiHealthNotification) {
  switchAsicSdkHealthNotificationBHEventBase_.runInFbossEventBaseThread(
      [this, notification = std::move(saiHealthNotification)]() {
        switchAsicSdkHealthNotificationBottomHalf(std::move(notification));
      });
}

void SaiSwitch::switchAsicSdkHealthNotificationBottomHalf(
    SaiHealthNotification saiHealthNotification) {
  if (getSwitchRunState() == SwitchRunState::EXITING) {
    return;
  }
  if (saiHealthNotification.asicError()) {
    getSwitchStats()->asicError();
  }
  if (saiHealthNotification.corrParityError()) {
    getSwitchStats()->corrParityError();
  }
  if (saiHealthNotification.uncorrParityError()) {
    getSwitchStats()->uncorrParityError();
  }
  switch (saiHealthNotification.getSeverity()) {
    case SAI_SWITCH_ASIC_SDK_HEALTH_SEVERITY_FATAL:
      XLOGF(FATAL, "{}", saiHealthNotification);
      break;

    case SAI_SWITCH_ASIC_SDK_HEALTH_SEVERITY_WARNING:
      XLOGF(WARNING, "{}", saiHealthNotification);
      break;

    case SAI_SWITCH_ASIC_SDK_HEALTH_SEVERITY_NOTICE:
      XLOGF(INFO, "{}", saiHealthNotification);
      break;
  }
}
#endif

} // namespace facebook::fboss
