// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/core.h>

#include "fboss/agent/hw/test/HwTestThriftHandler.h"

#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"
#include "fboss/agent/hw/bcm/BcmTrunkTable.h"
#include "fboss/agent/hw/bcm/tests/BcmTrunkUtils.h"
#include "fboss/agent/hw/test/HwPortUtils.h"
#include "fboss/agent/hw/test/PhyCapabilities.h"

extern "C" {
#include <bcm/port.h>
}

using facebook::fboss::cfg::PortLoopbackMode;

namespace {
facebook::fboss::cfg::PortLoopbackMode bcmToFbLoopbackMode(
    bcm_port_loopback_t inMode) {
  switch (inMode) {
    case BCM_PORT_LOOPBACK_NONE:
      return facebook::fboss::cfg::PortLoopbackMode::NONE;
    case BCM_PORT_LOOPBACK_PHY:
      return facebook::fboss::cfg::PortLoopbackMode::PHY;
    case BCM_PORT_LOOPBACK_MAC:
      return facebook::fboss::cfg::PortLoopbackMode::MAC;
    default:
      CHECK(0) << "Should never reach here";
  }
}
} // namespace

namespace facebook {
namespace fboss {
namespace utility {

void HwTestThriftHandler::injectFecError(
    [[maybe_unused]] std::unique_ptr<std::vector<int>> hwPorts,
    [[maybe_unused]] bool injectCorrectable) {
  // not implemented in bcm
  return;
}

void HwTestThriftHandler::getPortInfo(
    ::std::vector<::facebook::fboss::utility::PortInfo>& portInfos,
    std::unique_ptr<::std::vector<::std::int32_t>> portIds) {
  for (const auto& portId : *portIds) {
    PortInfo portInfo;
    int loopbackMode;
    auto rv = bcm_port_loopback_get(
        0, static_cast<bcm_port_t>(portId), &loopbackMode);
    bcmCheckError(rv, "Failed to get loopback mode for port:", portId);
    portInfo.loopbackMode() = static_cast<int>(
        bcmToFbLoopbackMode(static_cast<bcm_port_loopback_t>(loopbackMode)));
    portInfos.push_back(portInfo);
  }
  return;
}

void HwTestThriftHandler::getPortLlrInfo(
    ::facebook::fboss::utility::PortLlrInfo& /*portLlrInfo*/,
    int32_t /*port*/) {
  // LLR is a SAI-only feature today; no BCM-native implementation.
  throw FbossError("getPortLlrInfo is not implemented in bcm");
}

void HwTestThriftHandler::getPortVcInfo(
    ::facebook::fboss::utility::PortVcInfo& /*portVcInfo*/,
    int32_t /*port*/) {
  // CBFC is a SAI-only feature today; no BCM-native implementation.
  throw FbossError("getPortVcInfo is not implemented in bcm");
}

bool HwTestThriftHandler::verifyPortLedStatus(int /*portId*/, bool /*status*/) {
  return true;
}

bool HwTestThriftHandler::verifyPGSettings(int portId, bool pfcEnabled) {
  PortPgConfigs portPgsHw;

  auto swPort = hwSwitch_->getProgrammedState()->getPorts()->getNodeIf(portId);
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch_);
  auto bcmPort = bcmSwitch->getPortTable()->getBcmPort(portId);

  portPgsHw = bcmPort->getCurrentProgrammedPgSettings();
  const auto bufferPoolHwPtr = bcmPort->getCurrentIngressPoolSettings();

  auto swPgConfig = swPort->getPortPgConfigs();

  if (swPort->getPortPgConfigs()->size() != portPgsHw.size()) {
    return false;
  }

  int i = 0;
  // both vectors are sorted to start with lowest pg id
  for (const auto& pgConfig : std::as_const(*swPgConfig)) {
    auto id = pgConfig->cref<switch_state_tags::id>()->cref();
    if (id != portPgsHw[i]->getID()) {
      return false;
    }

    cfg::MMUScalingFactor scalingFactor;
    if (auto scalingFactorStr =
            pgConfig->cref<switch_state_tags::scalingFactor>()) {
      scalingFactor =
          nameToEnum<cfg::MMUScalingFactor>(scalingFactorStr->cref());
      if (scalingFactor != *portPgsHw[i]->getScalingFactor()) {
        XLOG(DBG2) << "Scaling factor mismatch for pg " << id;
        return false;
      }
    } else {
      if (std::nullopt != portPgsHw[i]->getScalingFactor()) {
        XLOG(DBG2) << "Scaling factor mismatch for pg " << id;
        return false;
      }
    }
    if (pgConfig->cref<switch_state_tags::resumeOffsetBytes>()->cref() !=
        portPgsHw[i]->getResumeOffsetBytes().value()) {
      XLOG(DBG2) << "Resume offset mismatch for pg " << id;
      return false;
    }
    if (pgConfig->cref<switch_state_tags::minLimitBytes>()->cref() !=
        portPgsHw[i]->getMinLimitBytes()) {
      XLOG(DBG2) << "Min limit mismatch for pg " << id;
      return false;
    }

    int pgHeadroom = 0;
    // for pgs with headroom, lossless mode + pfc should be enabled
    if (auto pgHdrmOpt =
            pgConfig->cref<switch_state_tags::headroomLimitBytes>()) {
      pgHeadroom = pgHdrmOpt->cref();
    }
    if (pgHeadroom != portPgsHw[i]->getHeadroomLimitBytes()) {
      XLOG(DBG2) << "Headroom mismatch for pg " << id;
      return false;
    }
    const auto bufferPool =
        pgConfig->cref<switch_state_tags::bufferPoolConfig>();
    if (bufferPool->cref<common_if_tags::sharedBytes>()->cref() !=
        (*bufferPoolHwPtr).getSharedBytes()) {
      XLOG(DBG2) << "Shared bytes mismatch for pg " << id;
      return false;
    }
    if (bufferPool->safe_cref<common_if_tags::headroomBytes>()->toThrift() !=
        *((*bufferPoolHwPtr).getHeadroomBytes())) {
      XLOG(DBG2) << "Headroom bytes mismatch for pg " << id;
      return false;
    }
    // we are in lossless mode if headroom > 0, else lossless mode = 0
    auto expectedLosslessMode = pgHeadroom > 0 ? 1 : 0;
    if (bcmPort->getProgrammedPgLosslessMode(id) != expectedLosslessMode) {
      XLOG(DBG2) << "Lossless mode mismatch for pg " << id;
      return false;
    }

    auto expectedPfcStatus = pfcEnabled ? 1 : 0;
    if (bcmPort->getProgrammedPfcStatusInPg(id) != expectedPfcStatus) {
      XLOG(DBG2) << "PFC mismatch for pg " << id;
      return false;
    }
    i++;
  }
  return true;
}

void HwTestThriftHandler::getAggPortInfo(
    ::std::vector<::facebook::fboss::utility::AggPortInfo>& aggPortInfos,
    std::unique_ptr<::std::vector<::std::int32_t>> aggPortIds) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch_);
  auto trunkTable = bcmSwitch->getTrunkTable();
  auto aggPortTable = hwSwitch_->getProgrammedState()->getAggregatePorts();
  for (const auto& portId : *aggPortIds) {
    AggPortInfo aggPortInfo;
    AggregatePortID aggPortId = AggregatePortID(portId);
    auto aggPort = aggPortTable->getNodeIf(aggPortId);
    try {
      trunkTable->getBcmTrunkId(aggPortId);
      aggPortInfo.isPresent() = true;
      aggPortInfo.numActiveMembers() = utility::getBcmTrunkMemberCount(
          0,
          trunkTable->getBcmTrunkId(aggPortId),
          aggPort->sortedSubports().size());
      aggPortInfo.numMembers() = aggPort->sortedSubports().size();

    } catch (const std::exception&) {
      XLOG(DBG2) << "Lag handle not found for port " << aggPortId;
      aggPortInfo.isPresent() = false;
    }
    aggPortInfos.push_back(aggPortInfo);
  }
  return;
}

int HwTestThriftHandler::getNumAggPorts() {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch_);
  return bcmSwitch->getTrunkTable()->numTrunkPorts();
}

bool HwTestThriftHandler::verifyPktFromAggPort(int aggPortId) {
  AggregatePortID aggregatePortID(aggPortId);
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch_);
  bool usePktIO = bcmSwitch->usePKTIO();
  BcmPacketT bcmPacket;
  bcm_pkt_t bcmPkt;
  auto bcmTrunkId = bcmSwitch->getTrunkTable()->getBcmTrunkId(aggregatePortID);
  bcmPacket.usePktIO = usePktIO;
  if (!usePktIO) {
    bcmPacket.ptrUnion.pkt = &bcmPkt;
    BCM_PKT_ONE_BUF_SETUP(&bcmPkt, nullptr, 0);
    bcmPkt.src_trunk = bcmTrunkId;
    bcmPkt.flags |= BCM_PKT_F_TRUNK;
    bcmPkt.unit = 0;
    FbBcmRxPacket fbRxPkt(bcmPacket, bcmSwitch);
    if (!fbRxPkt.isFromAggregatePort()) {
      return false;
    }
    if (fbRxPkt.getSrcAggregatePort() != aggregatePortID) {
      return false;
    }
  }
  return true;
}

void HwTestThriftHandler::clearInterfacePhyCounters(
    std::unique_ptr<::std::vector<::std::int32_t>> portIds) {
  hwSwitch_->clearInterfacePhyCounters(
      std::make_unique<std::vector<int32_t>>(std::move(*portIds)));
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
