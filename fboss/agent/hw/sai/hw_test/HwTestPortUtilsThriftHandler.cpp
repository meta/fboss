// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/core.h>

#include "fboss/agent/hw/sai/api/QosMapApi.h"
#include "fboss/agent/hw/sai/api/SaiApiError.h"
#include "fboss/agent/hw/sai/switch/SaiLagManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualChannelManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/HwPortUtils.h"
#include "fboss/agent/hw/test/HwTestPortUtils.h"
#include "fboss/agent/hw/test/HwTestThriftHandler.h"
#include "fboss/agent/hw/test/PhyCapabilities.h"
#include "fboss/agent/platforms/common/utils/Wedge100LedUtils.h"

namespace facebook {
namespace fboss {
namespace utility {

namespace {
SaiPortTraits::AdapterKey getPortAdapterKey(const HwSwitch* hw, PortID port) {
  auto saiSwitch = static_cast<const SaiSwitch*>(hw);
  auto handle = saiSwitch->managerTable()->portManager().getPortHandle(port);
  CHECK(handle);
  return handle->port->adapterKey();
}

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
cfg::LlrFrameAction saiLlrFrameActionToCfg(sai_int32_t action) {
  switch (action) {
    case SAI_LLR_FRAME_ACTION_DISCARD:
      return cfg::LlrFrameAction::DISCARD;
    case SAI_LLR_FRAME_ACTION_BLOCK:
      return cfg::LlrFrameAction::BLOCK;
    case SAI_LLR_FRAME_ACTION_BEST_EFFORT:
      return cfg::LlrFrameAction::BEST_EFFORT;
  }
  throw FbossError("Unknown SAI LLR frame action: ", action);
}
#endif
} // namespace

void HwTestThriftHandler::injectFecError(
    std::unique_ptr<std::vector<int>> hwPorts,
    bool injectCorrectable) {
  utility::injectFecError(std::move(*hwPorts), hwSwitch_, injectCorrectable);
}

void HwTestThriftHandler::getPortInfo(
    ::std::vector<::facebook::fboss::utility::PortInfo>& portInfos,
    std::unique_ptr<::std::vector<::std::int32_t>> portIds) {
  for (const auto& portId : *portIds) {
    PortInfo portInfo;
    auto key = getPortAdapterKey(hwSwitch_, PortID(portId));
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    SaiPortTraits::Attributes::PortLoopbackMode loopbackMode;
    SaiApiTable::getInstance()->portApi().getAttribute(key, loopbackMode);
    portInfo.loopbackMode() = loopbackMode.value();
#else
    SaiPortTraits::Attributes::InternalLoopbackMode internalLoopbackMode;
    SaiApiTable::getInstance()->portApi().getAttribute(
        key, internalLoopbackMode);
    portInfo.loopbackMode() = internalLoopbackMode.value();
#endif
    portInfos.push_back(portInfo);
  }
  return;
}

void HwTestThriftHandler::getPortLlrInfo(
    [[maybe_unused]] PortLlrInfo& portLlrInfo,
    [[maybe_unused]] int32_t port) {
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  auto saiSwitch = static_cast<const SaiSwitch*>(hwSwitch_);
  auto handle =
      saiSwitch->managerTable()->portManager().getPortHandle(PortID(port));
  CHECK(handle);
  if (!handle->llrProfile) {
    portLlrInfo.hasProfile() = false;
    return;
  }
  portLlrInfo.hasProfile() = true;
  // Adapter key of the profile the manager created/reclaimed -- a reliable
  // create-time OID, unlike the port's LLR_PROFILE getAttribute.
  auto profileKey = handle->llrProfile->adapterKey();
  portLlrInfo.profileId() = static_cast<int64_t>(profileKey);
  // Read the frame actions back from the profile object in hardware. This get
  // path is the same one warm-boot store reload depends on, so it is exercised
  // on every restart of an LLR-bound switch.
  portLlrInfo.initFrameAction() = saiLlrFrameActionToCfg(
      SaiApiTable::getInstance()->portApi().getAttribute(
          profileKey,
          SaiPortLlrProfileTraits::Attributes::InitLlrFrameAction{}));
  portLlrInfo.flushFrameAction() = saiLlrFrameActionToCfg(
      SaiApiTable::getInstance()->portApi().getAttribute(
          profileKey,
          SaiPortLlrProfileTraits::Attributes::FlushLlrFrameAction{}));
#else
  throw FbossError("LLR requires SAI 1.18 or newer");
#endif
}

void HwTestThriftHandler::getPortVcInfo(
    [[maybe_unused]] ::facebook::fboss::utility::PortVcInfo& portVcInfo,
    [[maybe_unused]] int32_t port) {
#if defined(SAI_CBFC_SUPPORTED)
  auto saiSwitch = static_cast<const SaiSwitch*>(hwSwitch_);
  auto* handle = saiSwitch->managerTable()
                     ->virtualChannelManager()
                     .getVirtualChannelHandle(PortID(port));
  auto portHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(PortID(port));
  CHECK(portHandle);
  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto portKey = portHandle->port->adapterKey();

  // Read-only, derived by hardware from the MMU carving. These are the values
  // the credit sizing question turns on, and nothing in FBOSS writes them.
  //
  // Each read is tolerated individually: SDK support for these varies and is
  // exactly what we are here to discover, so one NOT IMPLEMENTED attribute
  // must not cost us the readings that did work. An unset field means the SDK
  // refused, which is a result rather than a failure.
  auto tryRead = [](auto&& field, auto&& read) {
    try {
      field = read();
    } catch (const SaiApiError& e) {
      XLOG(WARN) << "CBFC attribute not readable on this SDK: " << e.what();
    }
  };

  tryRead(portVcInfo.receiverNativeCreditSize(), [&] {
    return portApi.getAttribute(
        portKey, SaiPortTraits::Attributes::CbfcReceiverNativeCreditSize{});
  });
  tryRead(portVcInfo.receiverNativePacketOverhead(), [&] {
    return portApi.getAttribute(
        portKey, SaiPortTraits::Attributes::CbfcReceiverNativePacketOverhead{});
  });
  tryRead(portVcInfo.receiverNativeTotalCredits(), [&] {
    return portApi.getAttribute(
        portKey, SaiPortTraits::Attributes::CbfcReceiverNativeTotalCredits{});
  });
  tryRead(portVcInfo.senderCreditSize(), [&] {
    return portApi.getAttribute(
        portKey, SaiPortTraits::Attributes::CbfcSenderCreditSize{});
  });
  tryRead(portVcInfo.senderPacketOverhead(), [&] {
    return portApi.getAttribute(
        portKey, SaiPortTraits::Attributes::CbfcSenderPacketOverhead{});
  });

  auto& qosMapApi = SaiApiTable::getInstance()->qosMapApi();
  auto readVcMap = [&](auto attr, auto keyOf) {
    std::map<int16_t, int16_t> vcMap;
    auto mapId = portApi.getAttribute(portKey, attr);
    if (mapId == SAI_NULL_OBJECT_ID) {
      return vcMap;
    }
    for (const auto& entry : qosMapApi.getAttribute(
             QosMapSaiId(mapId),
             SaiQosMapTraits::Attributes::MapToValueList{})) {
      vcMap[keyOf(entry.key)] = entry.value.vc;
    }
    return vcMap;
  };
  tryRead(portVcInfo.trafficClassToVcId(), [&] {
    return readVcMap(
        SaiPortTraits::Attributes::QosTcToVcMap{},
        [](const sai_qos_map_params_t& key) { return key.tc; });
  });
  tryRead(portVcInfo.queueToVcId(), [&] {
    return readVcMap(
        SaiPortTraits::Attributes::QosQueueToVcMap{},
        [](const sai_qos_map_params_t& key) { return key.queue_index; });
  });

  if (!handle) {
    return;
  }
  auto& vcApi = SaiApiTable::getInstance()->virtualChannelApi();
  for (const auto& vc : handle->virtualChannels) {
    auto vcKey = vc->adapterKey();
    ::facebook::fboss::utility::VcInfo vcInfo;
    vcInfo.vcId() = static_cast<int64_t>(vcKey);
    vcInfo.index() =
        vcApi.getAttribute(vcKey, SaiVirtualChannelTraits::Attributes::Index{});
    vcInfo.senderEnable() = vcApi.getAttribute(
        vcKey, SaiVirtualChannelTraits::Attributes::CbfcSenderEnable{});
    vcInfo.receiverEnable() = vcApi.getAttribute(
        vcKey, SaiVirtualChannelTraits::Attributes::CbfcReceiverEnable{});
    tryRead(vcInfo.receiverNativeCreditLimit(), [&] {
      return vcApi.getAttribute(
          vcKey,
          SaiVirtualChannelTraits::Attributes::CbfcReceiverNativeCreditLimit{});
    });
    auto profileId = vcApi.getAttribute(
        vcKey, SaiVirtualChannelTraits::Attributes::CbfcSenderCreditProfile{});
    vcInfo.hasCreditProfile() = profileId != SAI_NULL_OBJECT_ID;
    vcInfo.creditProfileId() = static_cast<int64_t>(profileId);
    if (*vcInfo.hasCreditProfile()) {
      vcInfo.reservedCreditSize() = vcApi.getAttribute(
          CbfcCreditProfileSaiId(profileId),
          SaiCbfcCreditProfileTraits::Attributes::ReservedCreditSize{});
    }
    portVcInfo.virtualChannels()->push_back(std::move(vcInfo));
  }
#else
  throw FbossError("CBFC requires SAI 1.19 or a Broadcom 16.0 SDK");
#endif
}

bool HwTestThriftHandler::verifyPortLedStatus(int portId, bool status) {
  SaiPlatform* platform = static_cast<SaiPlatform*>(hwSwitch_->getPlatform());
  SaiPlatformPort* platformPort = platform->getPort(PortID(portId));
  uint32_t currentVal = platformPort->getCurrentLedState();
  uint32_t expectedVal = 0;
  switch (platform->getType()) {
    case PlatformType::PLATFORM_WEDGE100: {
      expectedVal = static_cast<uint32_t>(Wedge100LedUtils::getExpectedLEDState(
          platform->getLaneCount(platformPort->getCurrentProfile()),
          status,
          status));
      return currentVal == expectedVal;
    }
    default:
      throw FbossError("Unsupported platform type");
  }
}

bool HwTestThriftHandler::verifyPGSettings(int portId, bool pfcEnabled) {
  auto swPort = hwSwitch_->getProgrammedState()->getPorts()->getNodeIf(portId);
  auto swPgConfig = swPort->getPortPgConfigs();
  SaiPlatform* platform = static_cast<SaiPlatform*>(hwSwitch_->getPlatform());

  auto portHandle = static_cast<const SaiSwitch*>(hwSwitch_)
                        ->managerTable()
                        ->portManager()
                        .getPortHandle(PortID(swPort->getID()));
  // Ensure that both SW and HW has the same number of PG IDs
  if (portHandle->configuredIngressPriorityGroups.size() !=
      swPort->getPortPgConfigs()->size()) {
    XLOG(DBG2) << "Number of PGs mismatch for port " << swPort->getName()
               << " hw size: "
               << portHandle->configuredIngressPriorityGroups.size()
               << " sw size: " << swPort->getPortPgConfigs()->size();
    return false;
  }
  for (const auto& pgConfig : std::as_const(*swPgConfig)) {
    auto id = pgConfig->cref<switch_state_tags::id>()->cref();
    auto iter = portHandle->configuredIngressPriorityGroups.find(
        static_cast<IngressPriorityGroupID>(id));
    if (iter == portHandle->configuredIngressPriorityGroups.end()) {
      XLOG(DBG2) << "Priority group config canot be found for PG id " << id
                 << " on port " << swPort->getName();
      return false;
    }
    auto bufferProfile = iter->second.bufferProfile;
    if (auto resumeBytesOpt =
            pgConfig->cref<switch_state_tags::resumeBytes>()) {
      auto want = resumeBytesOpt->cref();
      auto got = SaiApiTable::getInstance()->bufferApi().getAttribute(
          bufferProfile->adapterKey(), SaiBufferProfileAttributes::XonTh{});
      if (got != want) {
        XLOG(DBG2) << "Resume threshold mismatch for pg " << id
                   << ", got=" << got << ", want=" << want;
        return false;
      }
    }
    if (auto resumeOffsetBytesOpt =
            pgConfig->cref<switch_state_tags::resumeOffsetBytes>()) {
      auto want = resumeOffsetBytesOpt->cref();
      auto got = SaiApiTable::getInstance()->bufferApi().getAttribute(
          bufferProfile->adapterKey(),
          SaiBufferProfileAttributes::XonOffsetTh{});
      if (got != want) {
        XLOG(DBG2) << "Resume offset mismatch for pg " << id << ", got=" << got
                   << ", want=" << want;
        return false;
      }
    }
    {
      auto want = pgConfig->cref<switch_state_tags::minLimitBytes>()->cref();
      auto got = SaiApiTable::getInstance()->bufferApi().getAttribute(
          bufferProfile->adapterKey(),
          SaiBufferProfileAttributes::ReservedBytes{});
      if (got != want) {
        XLOG(DBG2) << "Min limit mismatch for pg " << id << ", got=" << got
                   << ", want=" << want;
        return false;
      }
    }
    if (auto pgHdrmOpt =
            pgConfig->cref<switch_state_tags::headroomLimitBytes>()) {
      auto want = pgHdrmOpt->cref();
      auto got = SaiApiTable::getInstance()->bufferApi().getAttribute(
          bufferProfile->adapterKey(), SaiBufferProfileAttributes::XoffTh{});
      if (got != want) {
        XLOG(DBG2) << "Headroom mismatch for pg " << id << ", got=" << got
                   << ", want=" << want;
        return false;
      }
    }

    // Buffer pool configs
    const auto bufferPool =
        pgConfig->cref<switch_state_tags::bufferPoolConfig>();
    {
      auto want = bufferPool->cref<common_if_tags::headroomBytes>()->cref() *
          static_cast<const SaiSwitch*>(hwSwitch_)
              ->getPlatform()
              ->getAsic()
              ->getNumMemoryBuffers();
      auto got = SaiApiTable::getInstance()->bufferApi().getAttribute(
          static_cast<const SaiSwitch*>(hwSwitch_)
              ->managerTable()
              ->bufferManager()
              .getIngressBufferPoolHandle()
              ->bufferPool->adapterKey(),
          SaiBufferPoolTraits::Attributes::XoffSize{});
      if (got != want) {
        XLOG(DBG2) << "Headroom mismatch for buffer pool, got=" << got
                   << ", want=" << want;
        return false;
      }
      if (platform->getAsic()->isSupported(
              HwAsic::Feature::RESERVED_BYTES_FOR_BUFFER_POOL)) {
        auto shared = bufferPool->cref<common_if_tags::sharedBytes>()->cref() *
            static_cast<const SaiSwitch*>(hwSwitch_)
                ->getPlatform()
                ->getAsic()
                ->getNumMemoryBuffers();
        auto reserved =
            bufferPool->cref<common_if_tags::reservedBytes>()->cref() *
            static_cast<const SaiSwitch*>(hwSwitch_)
                ->getPlatform()
                ->getAsic()
                ->getNumMemoryBuffers();
        want += shared + reserved;
        got = SaiApiTable::getInstance()->bufferApi().getAttribute(
            static_cast<const SaiSwitch*>(hwSwitch_)
                ->managerTable()
                ->bufferManager()
                .getIngressBufferPoolHandle()
                ->bufferPool->adapterKey(),
            SaiBufferPoolTraits::Attributes::Size{});
        if (got != want) {
          XLOG(DBG2) << "Shared size mismatch for buffer pool, got=" << got
                     << ", want=" << want;
          return false;
        }
      }
    }

    // Port PFC configurations
    if (SaiApiTable::getInstance()->portApi().getAttribute(
            portHandle->port->adapterKey(),
            SaiPortTraits::Attributes::PriorityFlowControlMode{}) ==
        SAI_PORT_PRIORITY_FLOW_CONTROL_MODE_COMBINED) {
      auto hwPfcEnabled = SaiApiTable::getInstance()->portApi().getAttribute(
                              portHandle->port->adapterKey(),
                              SaiPortTraits::Attributes::PriorityFlowControl{})
          ? 1
          : 0;
      if (hwPfcEnabled != pfcEnabled) {
        XLOG(DBG2) << "PFC mismatch for port " << swPort->getName();
        return false;
      }
    } else {
#if !defined(TAJO_SDK)
      auto hwPfcEnabled =
          SaiApiTable::getInstance()->portApi().getAttribute(
              portHandle->port->adapterKey(),
              SaiPortTraits::Attributes::PriorityFlowControlTx{})
          ? 1
          : 0;
      if (hwPfcEnabled != pfcEnabled) {
        XLOG(DBG2) << "PFC mismatch for port " << swPort->getName();
        return false;
      }
#else
      XLOG(DBG2) << "Flow control mode SEPARATE unsupported!";
      return false;
#endif
    }
  }
  return true;
}

void HwTestThriftHandler::getAggPortInfo(
    ::std::vector<::facebook::fboss::utility::AggPortInfo>& aggPortInfos,
    std::unique_ptr<::std::vector<::std::int32_t>> aggPortIds) {
  auto saiSwitch = static_cast<const SaiSwitch*>(hwSwitch_);
  auto& lagManager = saiSwitch->managerTable()->lagManager();
  for (const auto& portId : *aggPortIds) {
    AggPortInfo aggPortInfo;
    AggregatePortID aggPortId = AggregatePortID(portId);
    try {
      lagManager.getLagHandle(aggPortId);
      aggPortInfo.isPresent() = true;
      aggPortInfo.numMembers() = lagManager.getLagMemberCount(aggPortId);
      aggPortInfo.numActiveMembers() =
          lagManager.getActiveMemberCount(aggPortId);

    } catch (const std::exception&) {
      XLOG(DBG2) << "Lag handle not found for port " << aggPortId;
      aggPortInfo.isPresent() = false;
    }
    aggPortInfos.push_back(aggPortInfo);
  }
  return;
}

int HwTestThriftHandler::getNumAggPorts() {
  auto saiSwitch = static_cast<const SaiSwitch*>(hwSwitch_);
  return saiSwitch->managerTable()->lagManager().getLagCount();
}

void HwTestThriftHandler::clearInterfacePhyCounters(
    std::unique_ptr<::std::vector<::std::int32_t>> portIds) {
  hwSwitch_->clearInterfacePhyCounters(
      std::make_unique<std::vector<int32_t>>(std::move(*portIds)));
}

bool HwTestThriftHandler::verifyPktFromAggPort(int aggPortId) {
  std::array<char, 8> data{};
  // TODO (T159867926): Set the right queue ID once the vendor
  // set the right queue ID in the rx callback.
  auto rxPacket = std::make_unique<SaiRxPacket>(
      data.size(),
      data.data(),
      AggregatePortID(aggPortId),
      VlanID(1),
      cfg::PacketRxReason::UNMATCHED,
      0 /* queue Id */);
  return rxPacket->isFromAggregatePort();
}

void HwTestThriftHandler::verifyPortProfile(
    std::vector<std::string>& result,
    int32_t portId,
    cfg::PortProfileID profileId,
    std::unique_ptr<phy::ProfileSideConfig> profileConfig,
    std::unique_ptr<std::vector<phy::PinConfig>> pinConfigs) {
  auto platform = hwSwitch_->getPlatform();
  auto tryVerify = [&](const std::string& name, auto&& fn) {
    try {
      fn();
    } catch (const std::exception& ex) {
      result.push_back(
          fmt::format("{} failed on port {}: {}", name, portId, ex.what()));
    }
  };
  tryVerify("verifyInterfaceMode", [&]() {
    ::facebook::fboss::utility::verifyInterfaceMode(
        PortID(portId), profileId, platform, *profileConfig);
  });
  tryVerify("verifyTxSettting", [&]() {
    ::facebook::fboss::utility::verifyTxSettting(
        PortID(portId), profileId, platform, *pinConfigs);
  });
  tryVerify("verifyRxSettting", [&]() {
    ::facebook::fboss::utility::verifyRxSettting(
        PortID(portId), profileId, platform, *pinConfigs);
  });
  tryVerify("verifyFec", [&]() {
    ::facebook::fboss::utility::verifyFec(
        PortID(portId), profileId, platform, *profileConfig);
  });
}

phy::FecMode HwTestThriftHandler::getPortFECMode(int32_t portId) {
  return hwSwitch_->getPortFECMode(PortID(portId));
}

bool HwTestThriftHandler::rxSignalDetectSupportedInSdk() {
  return ::facebook::fboss::rxSignalDetectSupportedInSdk();
}

bool HwTestThriftHandler::rxLockStatusSupportedInSdk() {
  return ::facebook::fboss::rxLockStatusSupportedInSdk();
}

bool HwTestThriftHandler::pcsRxLinkStatusSupportedInSdk() {
  return ::facebook::fboss::pcsRxLinkStatusSupportedInSdk();
}

bool HwTestThriftHandler::fecAlignmentLockSupportedInSdk() {
  return ::facebook::fboss::fecAlignmentLockSupportedInSdk();
}

} // namespace utility
} // namespace fboss
} // namespace facebook
