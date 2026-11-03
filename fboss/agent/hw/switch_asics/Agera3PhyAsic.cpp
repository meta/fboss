// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/switch_asics/Agera3PhyAsic.h"
#include "fboss/agent/FbossError.h"

namespace facebook::fboss {
bool Agera3PhyAsic::isSupported(Feature feature) const {
  switch (feature) {
    case HwAsic::Feature::SAI_PORT_SERDES_PROGRAMMING:
    case HwAsic::Feature::SAI_SERDES_PRECODING:
    case HwAsic::Feature::SAI_SERDES_RX_REACH:
    case HwAsic::Feature::PMD_RX_SIGNAL_DETECT:
    case HwAsic::Feature::PMD_RX_LOCK_STATUS:
    // Enable the FEC-monitor read paths for the Agera3 retimer. FEC mode
    // RS544 is programmed on the LINE side only, via the platform mapping
    // (xphyLine.fec = RS544_2N); xphySystem stays NONE, and SaiPortManager
    // selects FEC per-side, so only the line port enters FEC-monitor mode.
    // Line-side-only is REQUIRED: Agera3 firmware rejects a both-side FEC
    // monitor and fails port-create ("Both side FEC monitor can not be
    // enabled", agera3_cfg_seq.c). The retimer is monitor-only (it does not
    // terminate FEC); getXphyInfo exposes corrected/uncorrectable codewords
    // (SAI_FEC_COUNTERS), pre-FEC BER (SAI_FEC_CORRECTED_BITS) and FEC
    // alignment lock (FEC_AM_LOCK_STATUS). Collection is exception-guarded
    // per-xphy in SaiPhyManager::collectXphyStats.
    case HwAsic::Feature::FEC:
    case HwAsic::Feature::SAI_FEC_COUNTERS:
    case HwAsic::Feature::SAI_FEC_CORRECTED_BITS:
    case HwAsic::Feature::SAI_FEC_CODEWORDS_STATS:
    case HwAsic::Feature::FEC_AM_LOCK_STATUS:
    // Per-lane RX SNR. PAI 4.1-only (SAI 1.18.1): the read is gated in
    // SaiPortManager::rxSNRSupported() by
    // (BRCM_PAI_SDK && SAI_API_VERSION >= 1.18.1), so it stays off on
    // PAI 4.0 which does not implement SAI_PORT_ATTR_RX_SNR.
    case HwAsic::Feature::RX_SNR:
    case HwAsic::Feature::WARMBOOT:
    case HwAsic::Feature::OBJECT_KEY_CACHE:
      return true;
    case HwAsic::Feature::MACSEC:
    case HwAsic::Feature::REMOVE_PORTS_FOR_COLDBOOT:
    case HwAsic::Feature::EMPTY_ACL_MATCHER:
    case HwAsic::Feature::PORT_EYE_VALUES:
    case HwAsic::Feature::XPHY_PORT_STATE_TOGGLE:
    case HwAsic::Feature::XPHY_SERDES_TX_FIR_POST3:
    case HwAsic::Feature::PCS_RX_LINK_STATUS:
    case HwAsic::Feature::ARS_ALTERNATE_MEMBERS:
    case HwAsic::Feature::ARS_FUTURE_PORT_LOAD:
    case HwAsic::Feature::VIRTUAL_ARS_GROUP:
    case HwAsic::Feature::CUT_THROUGH_FORWARDING:
    case HwAsic::Feature::SRV6_MYSID_DISCARD_COUNTER:
    case HwAsic::Feature::SRV6_MYSID_RESOURCE_COUNTER:
    case HwAsic::Feature::SRV6_BINDING_USID_UA_USD:
    case HwAsic::Feature::PBR_ACL:
    case HwAsic::Feature::DEVICE_WATERMARK_SUPPORT:
    case HwAsic::Feature::ECN_PROBABILISTIC_MARKING:
    case HwAsic::Feature::SWITCH_CUSTOM_DROP_BITMAP_SUPPORT:
    case HwAsic::Feature::SWITCH_DROP_REASON_LIST_SUPPORT:
    case HwAsic::Feature::ECMP_RANDOM_SPRAY_HIERARCHICAL_LEVEL:
    case HwAsic::Feature::ACL_DST_IPV6_WORD_QUALIFIERS:
    case HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::PORT_LATCH_STATUS:
      return false;
    default:
      return false;
  }
}

std::set<cfg::StreamType> Agera3PhyAsic::getQueueStreamTypes(
    cfg::PortType /*portType*/) const {
  throw FbossError("Agera3PhyAsic doesn't support queue feature");
}
int Agera3PhyAsic::getDefaultNumPortQueues(
    cfg::StreamType /* streamType */,
    cfg::PortType /*portType*/) const {
  throw FbossError("Agera3PhyAsic doesn't support queue feature");
}
std::optional<uint64_t> Agera3PhyAsic::getDefaultReservedBytes(
    cfg::StreamType /* streamType */,
    cfg::PortType /* portType */) const {
  throw FbossError("Agera3PhyAsic doesn't support queue feature");
}
std::optional<cfg::MMUScalingFactor> Agera3PhyAsic::getDefaultScalingFactor(
    cfg::StreamType /* streamType */,
    bool /* cpu */) const {
  throw FbossError("Agera3PhyAsic doesn't support queue feature");
}

uint32_t Agera3PhyAsic::getMaxMirrors() const {
  throw FbossError("Agera3PhyAsic doesn't support mirror feature");
}
uint16_t Agera3PhyAsic::getMirrorTruncateSize() const {
  throw FbossError("Agera3PhyAsic doesn't support mirror feature");
}

uint32_t Agera3PhyAsic::getMaxLabelStackDepth() const {
  throw FbossError("Agera3PhyAsic doesn't support label feature");
};
uint64_t Agera3PhyAsic::getMMUSizeBytes() const {
  throw FbossError("Agera3PhyAsic doesn't support MMU feature");
};
uint64_t Agera3PhyAsic::getSramSizeBytes() const {
  throw FbossError("Agera3PhyAsic doesn't support MMU feature");
}
int Agera3PhyAsic::getMaxNumLogicalPorts() const {
  throw FbossError("Agera3PhyAsic doesn't support logical ports feature");
}
uint32_t Agera3PhyAsic::getMaxWideEcmpSize() const {
  throw FbossError("Agera3PhyAsic doesn't support ecmp feature");
}
uint32_t Agera3PhyAsic::getMaxLagMemberSize() const {
  throw FbossError("Agera3PhyAsic doesn't support lag feature");
}
uint32_t Agera3PhyAsic::getPacketBufferUnitSize() const {
  throw FbossError("Agera3PhyAsic doesn't support MMU feature");
}
uint32_t Agera3PhyAsic::getPacketBufferDescriptorSize() const {
  throw FbossError("Agera3PhyAsic doesn't support MMU feature");
}

uint32_t Agera3PhyAsic::getMaxVariableWidthEcmpSize() const {
  throw FbossError("Agera3PhyAsic doesn't support ecmp feature");
}
uint32_t Agera3PhyAsic::getMaxEcmpSize() const {
  throw FbossError("Agera3PhyAsic doesn't support ecmp feature");
}

uint32_t Agera3PhyAsic::getNumCores() const {
  throw FbossError("Num cores API not supported");
}

bool Agera3PhyAsic::scalingFactorBasedDynamicThresholdSupported() const {
  throw FbossError("Agera3PhyAsic doesn't support MMU feature");
}
int Agera3PhyAsic::getBufferDynThreshFromScalingFactor(
    cfg::MMUScalingFactor /* scalingFactor */) const {
  throw FbossError("Agera3PhyAsic doesn't support MMU feature");
}
uint32_t Agera3PhyAsic::getStaticQueueLimitBytes() const {
  throw FbossError("Agera3PhyAsic doesn't support MMU feature");
}
uint32_t Agera3PhyAsic::getNumMemoryBuffers() const {
  throw FbossError("Agera3PhyAsic doesn't support MMU feature");
}
int Agera3PhyAsic::getMidPriCpuQueueId() const {
  throw FbossError("Agera3PhyAsic does not support cpu queue");
}
int Agera3PhyAsic::getHiPriCpuQueueId() const {
  throw FbossError("Agera3PhyAsic does not support cpu queue");
}
std::optional<uint32_t> Agera3PhyAsic::getMaxArsGroups() const {
  return std::nullopt;
}

std::optional<uint32_t> Agera3PhyAsic::getArsBaseIndex() const {
  return std::nullopt;
}
}; // namespace facebook::fboss
