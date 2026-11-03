/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/lib/phy/SaiPhyRetimer.h"

#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/api/SwitchApi.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortUtils.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include <fmt/format.h>

DEFINE_bool(
    enable_xphy_global_lock,
    false,
    "Enable/disable global lock for all XPHY chips");

namespace facebook::fboss::phy {

namespace {

#if defined(BRCM_PAI_SDK)
// Global mutex fallback, used when the enable_xphy_global_lock gflag is set.
std::mutex gPaiGlobalMutex;

// pai_lock_callback/pai_unlock_callback are invoked by PAI across a C ABI
// boundary, so exceptions must not propagate out of them.
sai_status_t pai_lock_callback(uint64_t platform_context) {
  try {
    if (FLAGS_enable_xphy_global_lock) {
      gPaiGlobalMutex.lock();
      XLOG(DBG5) << "[PAI-SYNC] LOCK acquired mode=GLOBAL mutex="
                 << static_cast<void*>(&gPaiGlobalMutex);
    } else {
      if (platform_context == 0) {
        XLOG(ERR) << "[PAI-SYNC] lock callback invoked with a null context.";
        return SAI_STATUS_FAILURE;
      }
      auto* retimer = reinterpret_cast<SaiPhyRetimer*>(platform_context);
      retimer->getPaiMutex().lock();
      XLOG(DBG5) << "[PAI-SYNC] LOCK acquired mode=PER-XPHY xphy:"
                 << retimer->getPhyAddr()
                 << " mutex=" << static_cast<void*>(&retimer->getPaiMutex());
    }
  } catch (const std::exception& e) {
    XLOG(ERR) << "[PAI-SYNC] lock callback failed: " << e.what();
    return SAI_STATUS_FAILURE;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t pai_unlock_callback(uint64_t platform_context) {
  try {
    if (FLAGS_enable_xphy_global_lock) {
      gPaiGlobalMutex.unlock();
      XLOG(DBG5) << "[PAI-SYNC] UNLOCK released mode=GLOBAL mutex="
                 << static_cast<void*>(&gPaiGlobalMutex);
    } else {
      if (platform_context == 0) {
        // May happen if a preceding lock failed; warn rather than silently
        // succeed so an unexpected unlock is visible.
        XLOG(WARNING)
            << "[PAI-SYNC] unlock callback invoked with a null context.";
        return SAI_STATUS_SUCCESS;
      }
      auto* retimer = reinterpret_cast<SaiPhyRetimer*>(platform_context);
      retimer->getPaiMutex().unlock();
      XLOG(DBG5) << "[PAI-SYNC] UNLOCK released mode=PER-XPHY xphy:"
                 << retimer->getPhyAddr()
                 << " mutex=" << static_cast<void*>(&retimer->getPaiMutex());
    }
  } catch (const std::exception& e) {
    XLOG(ERR) << "[PAI-SYNC] unlock callback failed: " << e.what();
    return SAI_STATUS_FAILURE;
  }
  return SAI_STATUS_SUCCESS;
}
#endif

/*
 * This is a static function for reading a Phy register. This function will be
 * passed to the SAI layer. The SAI driver will use this function to read a
 * phy register. The SAI will call the function with platform context. We are
 * passing the platform context as the SaiPhyRetimer object pointer during
 * create switch. When SAI calls this function with platform context then this
 * function will use that context to get correct SaiPhyRetimer object and then
 * call that object's XphyIO's readRegister function
 */
sai_status_t saiPhyRetimerSwitchRegisterRead(
    uint64_t platform_context,
    uint32_t /*device_addr*/,
    uint32_t start_reg_addr,
    uint32_t number_of_registers,
    uint32_t* reg_val) {
  SaiPhyRetimer* retimerObj =
      reinterpret_cast<SaiPhyRetimer*>(platform_context);
  CHECK(retimerObj);

  XLOG(DBG5) << __func__ << ":"
             << " phyAddr=" << retimerObj->getPhyAddr()
             << " deviceAddr=" << SaiPhyRetimer::getDeviceAddress()
             << " start_reg_addr=" << start_reg_addr
             << " number_of_registers=" << number_of_registers;

  // Finally call that object's XphyIO read register function
  for (uint32_t i = 0; i < number_of_registers; i++) {
    uint16_t value = retimerObj->getXphyIO()->readRegister(
        retimerObj->getPhyAddr(),
        SaiPhyRetimer::getDeviceAddress(),
        static_cast<uint16_t>(start_reg_addr + i));
    reg_val[i] = value;
  }

  return SAI_STATUS_SUCCESS;
}

/*
 * This is a static function for writing a Phy register. This function will be
 * passed to the SAI layer. The SAI driver will use this function to write a
 * phy register. The SAI will call the function with platform context. We are
 * passing the platform context as the SaiPhyRetimer object pointer during
 * create switch. When SAI calls this function with platform context then this
 * function will use that context to get correct SaiPhyRetimer object and then
 * call that object's XphyIO's writeRegister function
 */
sai_status_t saiPhyRetimerSwitchRegisterWrite(
    uint64_t platform_context,
    uint32_t /*device_addr*/,
    uint32_t start_reg_addr,
    uint32_t number_of_registers,
    const uint32_t* reg_val) {
  // Find the SaiPhyRetimer object from platform context passed by SAI
  SaiPhyRetimer* retimerObj =
      reinterpret_cast<SaiPhyRetimer*>(platform_context);
  CHECK(retimerObj);

  XLOG(DBG5) << __func__ << ":"
             << " phyAddr=" << retimerObj->getPhyAddr()
             << " deviceAddr=" << SaiPhyRetimer::getDeviceAddress()
             << " start_reg_addr=" << start_reg_addr
             << " number_of_registers=" << number_of_registers;
  // Finally call that object's XphyIO write register function
  for (uint32_t i = 0; i < number_of_registers; i++) {
    retimerObj->getXphyIO()->writeRegister(
        retimerObj->getPhyAddr(),
        SaiPhyRetimer::getDeviceAddress(),
        static_cast<uint16_t>(start_reg_addr + i),
        static_cast<uint16_t>(reg_val[i]));
  }

  return SAI_STATUS_SUCCESS;
}
} // namespace

bool SaiPhyRetimer::isSupported(Feature feature) const {
  switch (feature) {
    case Feature::PRBS:
    case Feature::PRBS_STATS:
      return false;
    case Feature::PORT_STATS:
      // Stats come from SaiPhyManager's getAllPhyInfo() path, not
      // ExternalPhy::getPortStats(); this flag gates the stats test infra.
      return true;
    case Feature::PORT_INFO:
      // getPortInfo() returns real PhyInfo by delegating to
      // SaiSwitch::getAllPhyInfo() (the same builder
      // SaiPhyManager::getPhyInfo() uses), so PORT_INFO is supported.
      return true;
    case Feature::LOOPBACK:
    case Feature::MACSEC:
      return false;
    default:
      return true;
  }
}

void SaiPhyRetimer::dumpImpl() const {
  // An utility to make sure the firmware is loaded successfully
  auto switchId = getSwitchId();
  auto fwStatus = SaiApiTable::getInstance()->switchApi().getAttribute(
      switchId, SaiSwitchTraits::Attributes::FirmwareStatus{});
  const auto& fw = fwVersionImpl();
  if (auto versionStr = fw.versionStr()) {
    XLOG(INFO) << "Firmware status for " << name_ << ", switchId=" << switchId
               << ", version=" << *versionStr
               << ", status=" << (fwStatus ? "running" : "not_running");
  } else {
    throw FbossError(
        "Unable to get firmware version for ", name_, ", switchId=", switchId);
  }
}

PhyFwVersion SaiPhyRetimer::fwVersionImpl() const {
  auto switchId = getSwitchId();
  phy::PhyFwVersion fw;
  fw.version() = SaiApiTable::getInstance()->switchApi().getAttribute(
      switchId, SaiSwitchTraits::Attributes::FirmwareMajorVersion{});
  fw.versionStr() = fmt::format("{}", *fw.version());
  if (auto versionStr = fw.versionStr()) {
    XLOG(DBG5) << "fwVersionImpl switchId=" << switchId
               << ", version=" << *versionStr;
  }
  return fw;
}

void* SaiPhyRetimer::getRegisterReadFuncPtr() {
  XLOG(DBG5) << __func__ << ": Returning read function pointer";
  return reinterpret_cast<void*>(saiPhyRetimerSwitchRegisterRead);
}

void* SaiPhyRetimer::getRegisterWriteFuncPtr() {
  XLOG(DBG5) << __func__ << ": Returning write function pointer";
  return reinterpret_cast<void*>(saiPhyRetimerSwitchRegisterWrite);
}

SaiSwitchTraits::CreateAttributes SaiPhyRetimer::getSwitchAttributes(
    bool warmboot) {
  XLOG(DBG5) << __func__ << ": Starting for xphyID=" << xphyID_;

  SaiSwitchTraits::Attributes::InitSwitch initSwitch(true);

  // Set HwInfo as XPHY ID
  // Encoding as 4-byte little-endian integer
  std::vector<int8_t> hwInfoVec(4);
  uint32_t phyId = static_cast<uint32_t>(xphyID_);
  hwInfoVec[0] = phyId & 0xFF;
  hwInfoVec[1] = (phyId >> 8) & 0xFF;
  hwInfoVec[2] = (phyId >> 16) & 0xFF;
  hwInfoVec[3] = (phyId >> 24) & 0xFF;
  XLOG(DBG2) << __func__ << ": Encoded phyId=" << phyId << " into hwInfoVec";
  std::optional<SaiSwitchTraits::Attributes::HwInfo> hwInfo(hwInfoVec);

  // Pass the static register read function
  std::optional<SaiSwitchTraits::Attributes::RegisterReadFn> regReadFn(
      getRegisterReadFuncPtr());

  // Pass the static register write function
  std::optional<SaiSwitchTraits::Attributes::RegisterWriteFn> regWriteFn(
      getRegisterWriteFuncPtr());

  // We are assuming this firmware is part of the SDK and dont need an external
  // fw
  std::optional<SaiSwitchTraits::Attributes::FirmwarePathName> fwPathName =
      std::nullopt;

  std::optional<SaiSwitchTraits::Attributes::FirmwareLoadMethod> fwLoadMethod(
      SAI_SWITCH_FIRMWARE_LOAD_METHOD_INTERNAL);

  std::optional<SaiSwitchTraits::Attributes::FirmwareLoadType> fwLoadType(
      warmboot ? SAI_SWITCH_FIRMWARE_LOAD_TYPE_SKIP
               : SAI_SWITCH_FIRMWARE_LOAD_TYPE_AUTO);
  std::optional<SaiSwitchTraits::Attributes::WarmRecover> warmRecover;
  if (warmboot) {
    warmRecover = true;
  }

  // Phy is accessible on MDIO
  std::optional<SaiSwitchTraits::Attributes::HardwareAccessBus> hwAccessBus(
      SAI_SWITCH_HARDWARE_ACCESS_BUS_MDIO);

  // Pass this object pointer as platform context to driver. The driver will
  // use this context while calling read/write register function and there we
  // will use this context to identify this object again
  std::optional<SaiSwitchTraits::Attributes::PlatformContext> context(
      reinterpret_cast<sai_uint64_t>(this));

  std::optional<SaiSwitchTraits::Attributes::SwitchType> switchType(
      SAI_SWITCH_TYPE_PHY);

  // Set profile ID to XPHY ID so each retimer gets unique profile id
  uint32_t profileIdValue = static_cast<uint32_t>(xphyID_);
  XLOG(DBG5) << __func__ << ": Setting profileId=" << profileIdValue;
  std::optional<SaiSwitchTraits::Attributes::SwitchProfileId> profileId(
      profileIdValue);

  // Mandatory attributes for Sai
  std::optional<SaiSwitchTraits::Attributes::SwitchId> switchId(0);
  std::optional<SaiSwitchTraits::Attributes::MaxSystemCores> maxSysCores(0);
  std::vector<sai_system_port_config_t> sysPortConfigVec;
  std::optional<SaiSwitchTraits::Attributes::SysPortConfigList>
      sysPortConfigList(sysPortConfigVec);

#if defined(BRCM_PAI_SDK)
  XLOG(INFO) << "[PAI-SYNC] BRCM_PAI_SDK is COMPILED IN. Installing PAI "
             << "sync lock/unlock callbacks for xphy:" << getPhyAddr()
             << " (xphyID=" << xphyID_ << "), mode="
             << (FLAGS_enable_xphy_global_lock ? "GLOBAL" : "PER-XPHY")
             << ", platform_context(this)=" << static_cast<const void*>(this)
             << ", paiMutex=" << static_cast<void*>(&paiMutex_);
#else
  XLOG(WARNING) << "[PAI-SYNC] BRCM_PAI_SDK is NOT compiled in; PAI sync "
                << "lock/unlock callbacks will NOT be passed to PAI for xphy:"
                << getPhyAddr() << " (xphyID=" << xphyID_
                << "). PAI access is UNPROTECTED.";
#endif

  return {
      initSwitch,
      hwInfo, // hardware info
      std::nullopt, // srcMac,
      std::nullopt, // shell
      std::nullopt, // ecmp hash v4
      std::nullopt, // ecmp hash v6
      std::nullopt, // lag hash v4
      std::nullopt, // lag hash v6
      std::nullopt, // ecmp hash seed
      std::nullopt, // lag hash seed
      std::nullopt, // ecmp hash algo
      std::nullopt, // lag hash algo
      std::nullopt, // restart warm
      std::nullopt, // qos dscp to tc map
      std::nullopt, // qos tc to queue map
      std::nullopt, // qos exp to tc map
      std::nullopt, // qos tc to exp map
      std::nullopt, // macAgingTime
      std::nullopt, // ingress acl
      std::nullopt, // egress acl
      std::nullopt, // aclFieldList
      std::nullopt, // tam object list
      std::nullopt, // use Ecn Thresholds
      std::nullopt, // counter refresh interval
      std::nullopt, // Firmware core to use
      fwPathName, // Firmware path name
      std::nullopt, // Firmware log file
      fwLoadMethod, // Firmware load method
      fwLoadType, // Firmware load type
      hwAccessBus, // Hardware access bus
      context, // Platform context
      profileId, // Switch profile id (set to XPHY ID)
      switchId, // Switch id
      maxSysCores, // Max system cores
      sysPortConfigList, // System port config list
      switchType, // Switch type
      regReadFn, // Register read function
      regWriteFn, // Register write function
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      std::nullopt, // Max ECMP member count
      std::nullopt, // ECMP member count
#endif
      std::nullopt, // dllPath
      std::nullopt, // restartIssu
      std::nullopt, // Switch Isolate
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      std::nullopt, // Credit Watchdog
      std::nullopt, // Credit Watchdog Timer
#endif
      std::nullopt, // Max Cores
      std::nullopt, // PFC DLR Packet Action
      std::nullopt, // route no implicit meta data
      std::nullopt, // route allow implicit meta data
      std::nullopt, // multi-stage local switch ids
      std::nullopt, // Local VoQ latency bin min
      std::nullopt, // Local VoQ latency bin max
      std::nullopt, // Level1 VoQ latency bin min
      std::nullopt, // Level1 VoQ latency bin max
      std::nullopt, // Level2 VoQ latency bin min
      std::nullopt, // Level2 VoQ latency bin max
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      std::nullopt, // ArsProfile
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      std::nullopt, // Port PTP mode
#endif
      std::nullopt, // Reachability Group List
      std::nullopt, // Delay Drop Cong Threshold
      std::nullopt, // Fabric link level control threshold
      std::nullopt, // SRAM free percent XOFF threshold
      std::nullopt, // SRAM free percent XON threshold
      std::nullopt, // No acls for traps
      std::nullopt, // Max system port ids
      std::nullopt, // Max local system port ids
      std::nullopt, // Max system ports
      std::nullopt, // Max voqs
      std::nullopt, // Fabric CLLFC TX credit threshold
      std::nullopt, // VOQ DRAM bound threshold
      std::nullopt, // ConditionalEntropyRehashPeriodUS
      std::nullopt, // Shel Source IP
      std::nullopt, // Shel Destination IP
      std::nullopt, // Shel Source MAC
      std::nullopt, // Shel Periodic Interval
      std::nullopt, // MaxSwitchId
      std::nullopt, // Sflow aggr number of samples
      std::nullopt, // SDK Register dump log path
      std::nullopt, // SDK dump rate limit window
      std::nullopt, // Firmware Object list
      std::nullopt, // tc rate limit list
      std::nullopt, // PFC watchdog timer granularity
      std::nullopt, // disable sll and hll timeout
      std::nullopt, // credit request profile scheduler mode
      std::nullopt, // module id to credit request profile param list
#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
      std::nullopt, // range list of local scope system port ids
#endif
      std::nullopt, // enable PFC monitoring for the switch
      std::nullopt, // enable cable propagation delay measurement
      std::nullopt, // enable CL72 link training retry
      std::nullopt, // disable snoop on ingress ACL drop
      std::nullopt, // switching mode
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
      std::nullopt, // link up debounce timeout
#endif
#if defined(BRCM_PAI_SDK)
      reinterpret_cast<sai_pointer_t>(pai_lock_callback), // user sync_lock
      reinterpret_cast<sai_pointer_t>(pai_unlock_callback), // user sync_unlock
      warmRecover, // Warm recover
#endif
  };
}

namespace {

// Convert LaneID vector to uint32_t vector for SAI
std::vector<uint32_t> toLaneVector(const std::vector<LaneID>& lanes) {
  std::vector<uint32_t> saiLanes;
  saiLanes.reserve(lanes.size());
  for (auto lane : lanes) {
    saiLanes.push_back(lane);
  }
  return saiLanes;
}

// Format lane vector as string for logging
std::string formatLanes(const std::vector<uint32_t>& lanes) {
  return fmt::format("{}", fmt::join(lanes, " "));
}

// Extract optional vector attribute from serdes
template <typename AttrType>
std::vector<sai_uint32_t> extractSerdesAttr(
    const SaiPortSerdesTraits::CreateAttributes& attrs) {
  const auto& opt = std::get<std::optional<AttrType>>(attrs);
  if (opt.has_value()) {
    return opt.value().value();
  }
  return {};
}

// Build TxSettings for a specific lane index from serdes TX values
std::optional<TxSettings> buildTxSettings(
    size_t laneIdx,
    const std::vector<sai_uint32_t>& txPre1,
    const std::vector<sai_uint32_t>& txPre2,
    const std::vector<sai_uint32_t>& txPre3,
    const std::vector<sai_uint32_t>& txMain,
    const std::vector<sai_uint32_t>& txPost1,
    const std::vector<sai_uint32_t>& txPost2,
    const std::vector<sai_uint32_t>& txPost3) {
  TxSettings tx;
  // Initialize all fields to 0
  tx.pre() = 0;
  tx.pre2() = 0;
  tx.pre3() = 0;
  tx.main() = 0;
  tx.post() = 0;
  tx.post2() = 0;
  tx.post3() = 0;

  // Fill with actual values from serdes
  if (laneIdx < txPre1.size()) {
    tx.pre() = static_cast<int16_t>(txPre1[laneIdx]);
  }
  if (laneIdx < txPre2.size()) {
    tx.pre2() = static_cast<int16_t>(txPre2[laneIdx]);
  }
  if (laneIdx < txPre3.size()) {
    tx.pre3() = static_cast<int16_t>(txPre3[laneIdx]);
  }
  if (laneIdx < txMain.size()) {
    tx.main() = static_cast<int16_t>(txMain[laneIdx]);
  }
  if (laneIdx < txPost1.size()) {
    tx.post() = static_cast<int16_t>(txPost1[laneIdx]);
  }
  if (laneIdx < txPost2.size()) {
    tx.post2() = static_cast<int16_t>(txPost2[laneIdx]);
  }
  if (laneIdx < txPost3.size()) {
    tx.post3() = static_cast<int16_t>(txPost3[laneIdx]);
  }
  return tx;
}

// Get FEC mode from port attributes, defaulting to NONE if not set
sai_port_fec_mode_t getSaiFecFromPort(
    const SaiPortTraits::CreateAttributes& attrs) {
  const auto& fecOpt =
      std::get<std::optional<SaiPortTraits::Attributes::FecMode>>(attrs);
  if (fecOpt.has_value()) {
    return static_cast<sai_port_fec_mode_t>(fecOpt.value().value());
  }
  return SAI_PORT_FEC_MODE_NONE;
}

IpModulation getModulation(cfg::PortSpeed speed, size_t numLanes) {
  auto perLaneSpeed = static_cast<int>(speed) / numLanes;
  return (perLaneSpeed >= 50000) ? IpModulation::PAM4 : IpModulation::NRZ;
}

// Helper to get serdes TX attributes directly from SAI hardware
std::vector<sai_uint32_t> getSerdesAttrFromHw(
    const PortSerdesSaiId& serdesSaiId,
    sai_port_serdes_attr_t attrId,
    size_t numLanes) {
  std::vector<sai_uint32_t> result(numLanes);
  try {
    switch (attrId) {
      case SAI_PORT_SERDES_ATTR_TX_FIR_PRE1:
        return SaiApiTable::getInstance()->portApi().getAttribute(
            serdesSaiId, SaiPortSerdesTraits::Attributes::TxFirPre1{result});
      case SAI_PORT_SERDES_ATTR_TX_FIR_PRE2:
        return SaiApiTable::getInstance()->portApi().getAttribute(
            serdesSaiId, SaiPortSerdesTraits::Attributes::TxFirPre2{result});
      case SAI_PORT_SERDES_ATTR_TX_FIR_PRE3:
        return SaiApiTable::getInstance()->portApi().getAttribute(
            serdesSaiId, SaiPortSerdesTraits::Attributes::TxFirPre3{result});
      case SAI_PORT_SERDES_ATTR_TX_FIR_MAIN:
        return SaiApiTable::getInstance()->portApi().getAttribute(
            serdesSaiId, SaiPortSerdesTraits::Attributes::TxFirMain{result});
      case SAI_PORT_SERDES_ATTR_TX_FIR_POST1:
        return SaiApiTable::getInstance()->portApi().getAttribute(
            serdesSaiId, SaiPortSerdesTraits::Attributes::TxFirPost1{result});
      case SAI_PORT_SERDES_ATTR_TX_FIR_POST2:
        return SaiApiTable::getInstance()->portApi().getAttribute(
            serdesSaiId, SaiPortSerdesTraits::Attributes::TxFirPost2{result});
      case SAI_PORT_SERDES_ATTR_TX_FIR_POST3:
        return SaiApiTable::getInstance()->portApi().getAttribute(
            serdesSaiId, SaiPortSerdesTraits::Attributes::TxFirPost3{result});
      default:
        return {};
    }
  } catch (const SaiApiError& e) {
    XLOG(DBG3) << "Failed to get serdes attribute " << attrId
               << " from HW: " << e.what();
    return {};
  }
}

} // namespace

PhyPortConfig SaiPhyRetimer::getConfigOnePort(
    const std::vector<LaneID>& sysLanes,
    const std::vector<LaneID>& lineLanes,
    cfg::PortProfileID profileID,
    bool readFromHw) {
  if (!platform_) {
    throw FbossError("Platform is null for xphyID=", xphyID_);
  }
  auto* saiSwitch = static_cast<SaiSwitch*>(platform_->getHwSwitch());
  if (!saiSwitch) {
    throw FbossError("SaiSwitch is null for xphyID=", xphyID_);
  }

  auto* saiStore = saiSwitch->getSaiStore();
  auto& portStore = saiStore->get<SaiPortTraits>();
  auto& serdesStore = saiStore->get<SaiPortSerdesTraits>();

  auto sysSaiLanes = toLaneVector(sysLanes);
  auto lineSaiLanes = toLaneVector(lineLanes);

  auto sysPort = portStore.get(SaiPortTraits::AdapterHostKey{sysSaiLanes});
  if (!sysPort) {
    throw FbossError(
        "System port not found for xphyID=",
        xphyID_,
        ", lanes=[",
        formatLanes(sysSaiLanes),
        "]");
  }

  auto linePort = portStore.get(SaiPortTraits::AdapterHostKey{lineSaiLanes});
  if (!linePort) {
    throw FbossError(
        "Line port not found for xphyID=",
        xphyID_,
        ", lanes=[",
        formatLanes(lineSaiLanes),
        "]");
  }

  cfg::PortSpeed sysSpeed, lineSpeed;
  if (readFromHw) {
    sysSpeed = static_cast<cfg::PortSpeed>(
        SaiApiTable::getInstance()->portApi().getAttribute(
            sysPort->adapterKey(), SaiPortTraits::Attributes::Speed{}));
    lineSpeed = static_cast<cfg::PortSpeed>(
        SaiApiTable::getInstance()->portApi().getAttribute(
            linePort->adapterKey(), SaiPortTraits::Attributes::Speed{}));
  } else {
    sysSpeed = static_cast<cfg::PortSpeed>(
        GET_ATTR(Port, Speed, sysPort->attributes()));
    lineSpeed = static_cast<cfg::PortSpeed>(
        GET_ATTR(Port, Speed, linePort->attributes()));
  }

  CHECK(sysSpeed == lineSpeed)
      << "System/line speed mismatch: " << static_cast<int>(sysSpeed) << " vs "
      << static_cast<int>(lineSpeed);

  PhyPortConfig config;
  config.profile.speed = sysSpeed;

  // Helper to build PhySideConfig (lane configs with TX settings)
  const bool post3Supported = platform_->getAsic()->isSupported(
      HwAsic::Feature::XPHY_SERDES_TX_FIR_POST3);
  auto buildPhySideConfig =
      [&serdesStore, readFromHw, post3Supported](
          std::shared_ptr<SaiObject<SaiPortTraits>> portObj,
          const std::vector<uint32_t>& lanes) -> PhySideConfig {
    PhySideConfig side;

    auto serdes = serdesStore.get(
        SaiPortSerdesTraits::AdapterHostKey{portObj->adapterKey()});

    std::vector<sai_uint32_t> txPre1, txPre2, txPre3, txMain, txPost1, txPost2,
        txPost3;
    if (serdes) {
      if (readFromHw) {
        auto serdesSaiId = serdes->adapterKey();
        size_t numLanes = lanes.size();
        txPre1 = getSerdesAttrFromHw(
            serdesSaiId, SAI_PORT_SERDES_ATTR_TX_FIR_PRE1, numLanes);
        txPre2 = getSerdesAttrFromHw(
            serdesSaiId, SAI_PORT_SERDES_ATTR_TX_FIR_PRE2, numLanes);
        txPre3 = getSerdesAttrFromHw(
            serdesSaiId, SAI_PORT_SERDES_ATTR_TX_FIR_PRE3, numLanes);
        txMain = getSerdesAttrFromHw(
            serdesSaiId, SAI_PORT_SERDES_ATTR_TX_FIR_MAIN, numLanes);
        txPost1 = getSerdesAttrFromHw(
            serdesSaiId, SAI_PORT_SERDES_ATTR_TX_FIR_POST1, numLanes);
        txPost2 = getSerdesAttrFromHw(
            serdesSaiId, SAI_PORT_SERDES_ATTR_TX_FIR_POST2, numLanes);
        if (post3Supported) {
          txPost3 = getSerdesAttrFromHw(
              serdesSaiId, SAI_PORT_SERDES_ATTR_TX_FIR_POST3, numLanes);
        }
      } else {
        const auto& attrs = serdes->attributes();
        txPre1 = extractSerdesAttr<SaiPortSerdesTraits::Attributes::TxFirPre1>(
            attrs);
        txPre2 = extractSerdesAttr<SaiPortSerdesTraits::Attributes::TxFirPre2>(
            attrs);
        txPre3 = extractSerdesAttr<SaiPortSerdesTraits::Attributes::TxFirPre3>(
            attrs);
        txMain = extractSerdesAttr<SaiPortSerdesTraits::Attributes::TxFirMain>(
            attrs);
        txPost1 =
            extractSerdesAttr<SaiPortSerdesTraits::Attributes::TxFirPost1>(
                attrs);
        txPost2 =
            extractSerdesAttr<SaiPortSerdesTraits::Attributes::TxFirPost2>(
                attrs);
        if (post3Supported) {
          txPost3 =
              extractSerdesAttr<SaiPortSerdesTraits::Attributes::TxFirPost3>(
                  attrs);
        }
      }
    }

    for (size_t i = 0; i < lanes.size(); ++i) {
      LaneConfig laneConfig;
      if (auto tx = buildTxSettings(
              i, txPre1, txPre2, txPre3, txMain, txPost1, txPost2, txPost3)) {
        laneConfig.tx = *tx;
      }
      side.lanes[LaneID(lanes[i])] = laneConfig;
    }
    return side;
  };

  config.config.system = buildPhySideConfig(sysPort, sysSaiLanes);
  config.config.line = buildPhySideConfig(linePort, lineSaiLanes);

  // Helper to build ProfileSideConfig (FEC, numLanes, modulation)
  auto buildProfileSideConfig =
      [readFromHw, profileID](
          std::shared_ptr<SaiObject<SaiPortTraits>> portObj,
          cfg::PortSpeed speed) -> ProfileSideConfig {
    ProfileSideConfig side;

    sai_port_fec_mode_t fecValue;
    size_t numLanes;

    if (readFromHw) {
      fecValue = static_cast<sai_port_fec_mode_t>(
          SaiApiTable::getInstance()->portApi().getAttribute(
              portObj->adapterKey(), SaiPortTraits::Attributes::FecMode{}));
      auto expectedLanes =
          GET_ATTR(Port, HwLaneList, portObj->adapterHostKey());
      std::vector<uint32_t> hwLanes(expectedLanes.size());
      hwLanes = SaiApiTable::getInstance()->portApi().getAttribute(
          portObj->adapterKey(),
          SaiPortTraits::Attributes::HwLaneList{hwLanes});
      numLanes = hwLanes.size();
    } else {
      fecValue = getSaiFecFromPort(portObj->attributes());
      numLanes = GET_ATTR(Port, HwLaneList, portObj->adapterHostKey()).size();
    }

    side.fec() = utility::getFecModeFromSaiFecMode(fecValue, profileID);
    side.numLanes() = numLanes;
    side.modulation() = getModulation(speed, numLanes);

    return side;
  };

  config.profile.system = buildProfileSideConfig(sysPort, sysSpeed);
  config.profile.line = buildProfileSideConfig(linePort, lineSpeed);

  XLOG(DBG2) << "getConfigOnePort" << (readFromHw ? "FromHw" : "")
             << " completed: speed="
             << apache::thrift::util::enumNameSafe(config.profile.speed)
             << ", sys.lanes=" << config.profile.system.numLanes().value()
             << ", line.lanes=" << config.profile.line.numLanes().value();

  return config;
}

PhyInfo SaiPhyRetimer::getPortInfo(
    const std::vector<LaneID>& /* sysLanes */,
    const std::vector<LaneID>& lineLanes,
    PhyInfo& /* lastPhyInfo */) {
  if (!platform_) {
    throw FbossError("Platform is null for xphyID=", xphyID_);
  }
  auto* saiSwitch = static_cast<SaiSwitch*>(platform_->getHwSwitch());
  if (!saiSwitch) {
    throw FbossError("SaiSwitch is null for xphyID=", xphyID_);
  }

  // Reuse SaiSwitch's PhyInfo (updateAllPhyInfo/getAllPhyInfo), like
  // SaiPhyManager::getPhyInfo(). It's keyed by SwitchState PortID, so map this
  // xphy's line lanes -> SAI port (by AdapterHostKey) -> owning PortID.
  auto& portStore = saiSwitch->getSaiStore()->get<SaiPortTraits>();
  const auto lineSaiLanes = toLaneVector(lineLanes);
  auto linePort = portStore.get(SaiPortTraits::AdapterHostKey{lineSaiLanes});
  if (!linePort) {
    throw FbossError(
        "Line port not found for xphyID=",
        xphyID_,
        ", lanes=[",
        formatLanes(lineSaiLanes),
        "]");
  }
  const auto linePortSaiId = linePort->adapterKey();

  // SaiSwitch already indexes SAI port id -> PortID; use it instead of scanning
  // every port handle.
  const auto& portSaiId2PortInfo =
      saiSwitch->concurrentIndices().portSaiId2PortInfo;
  const auto portInfoIt = portSaiId2PortInfo.find(linePortSaiId);
  if (portInfoIt == portSaiId2PortInfo.end()) {
    throw FbossError(
        "No PortID found for xphyID=",
        xphyID_,
        " line lanes=[",
        formatLanes(lineSaiLanes),
        "]");
  }
  const auto portID = portInfoIt->second.portID;

  saiSwitch->updateAllPhyInfo();
  auto allPhyInfo = saiSwitch->getAllPhyInfo();
  auto it = allPhyInfo.find(portID);
  if (it == allPhyInfo.end()) {
    throw FbossError(
        "PhyInfo not found for xphyID=", xphyID_, " portID=", portID);
  }
  auto phyInfo = std::move(it->second);

  // SaiSwitch sets phyChip.type() but not phyChip.name(); fill the name from
  // the platform mapping (XPHY chip whose physicalID == xphyID_).
  auto& phyChip = *phyInfo.state()->phyChip();
  if (phyChip.name()->empty()) {
    for (const auto& [chipName, dpChip] : platformMapping_->getChips()) {
      if (dpChip.type().value() == DataPlanePhyChipType::XPHY &&
          dpChip.physicalID().value() == static_cast<int32_t>(xphyID_)) {
        phyChip.name() = dpChip.name().value();
        break;
      }
    }
  }
  return phyInfo;
}

} // namespace facebook::fboss::phy
