// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/switch_asics/CredoPhyAsic.h"
#include "fboss/agent/FbossError.h"

namespace facebook::fboss {
bool CredoPhyAsic::isSupported(Feature feature) const {
  switch (feature) {
    case HwAsic::Feature::MACSEC:
    case HwAsic::Feature::REMOVE_PORTS_FOR_COLDBOOT:
    case HwAsic::Feature::EMPTY_ACL_MATCHER:
    case HwAsic::Feature::PORT_EYE_VALUES:
    case HwAsic::Feature::FEC:
    case HwAsic::Feature::XPHY_PORT_STATE_TOGGLE:
    case HwAsic::Feature::PMD_RX_SIGNAL_DETECT:
    case HwAsic::Feature::PMD_RX_LOCK_STATUS:
    case HwAsic::Feature::FEC_AM_LOCK_STATUS:
    case HwAsic::Feature::PCS_RX_LINK_STATUS:
    case HwAsic::Feature::WARMBOOT:
    case HwAsic::Feature::OBJECT_KEY_CACHE:
    case HwAsic::Feature::XPHY_SERDES_TX_FIR_POST3:
    case HwAsic::Feature::ZERO_SDK_WRITE_WARMBOOT:
      return true;
    case HwAsic::Feature::ARS_ALTERNATE_MEMBERS:
    case HwAsic::Feature::ARS_FUTURE_PORT_LOAD:
    case HwAsic::Feature::VIRTUAL_ARS_GROUP:
    case HwAsic::Feature::CUT_THROUGH_FORWARDING:
    case HwAsic::Feature::SRV6_MYSID_DISCARD_COUNTER:
    case HwAsic::Feature::SRV6_MYSID_RESOURCE_COUNTER:
    case HwAsic::Feature::SRV6_BINDING_USID_UA_USD:
    case HwAsic::Feature::SRV6_DECAP_TUNNEL:
    case HwAsic::Feature::PBR_ACL:
    case HwAsic::Feature::DEVICE_WATERMARK_SUPPORT:
    case HwAsic::Feature::SWITCH_CUSTOM_DROP_BITMAP_SUPPORT:
    case HwAsic::Feature::SWITCH_DROP_REASON_LIST_SUPPORT:
    case HwAsic::Feature::SAI_SERDES_PRECODING:
    case HwAsic::Feature::ECMP_RANDOM_SPRAY_HIERARCHICAL_LEVEL:
    case HwAsic::Feature::ACL_DST_IPV6_WORD_QUALIFIERS:
    case HwAsic::Feature::ACL_DST_IP_WITH_MASK:
    case HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::PORT_LATCH_STATUS:
    default:
      return false;
  }
  return false;
}

std::set<cfg::StreamType> CredoPhyAsic::getQueueStreamTypes(
    cfg::PortType /*portType*/) const {
  throw FbossError("CredoPhyAsic doesn't support queue feature");
}
int CredoPhyAsic::getDefaultNumPortQueues(
    cfg::StreamType /* streamType */,
    cfg::PortType /*portType*/) const {
  throw FbossError("CredoPhyAsic doesn't support queue feature");
}
std::optional<uint64_t> CredoPhyAsic::getDefaultReservedBytes(
    cfg::StreamType /* streamType */,
    cfg::PortType /* portType */) const {
  throw FbossError("CredoPhyAsic doesn't support queue feature");
}
std::optional<cfg::MMUScalingFactor> CredoPhyAsic::getDefaultScalingFactor(
    cfg::StreamType /* streamType */,
    bool /* cpu */) const {
  throw FbossError("CredoPhyAsic doesn't support queue feature");
}

uint32_t CredoPhyAsic::getMaxMirrors() const {
  throw FbossError("CredoPhyAsic doesn't support mirror feature");
}
uint16_t CredoPhyAsic::getMirrorTruncateSize() const {
  throw FbossError("CredoPhyAsic doesn't support mirror feature");
}

uint32_t CredoPhyAsic::getMaxLabelStackDepth() const {
  throw FbossError("CredoPhyAsic doesn't support label feature");
};
uint64_t CredoPhyAsic::getMMUSizeBytes() const {
  throw FbossError("CredoPhyAsic doesn't support MMU feature");
};
uint64_t CredoPhyAsic::getSramSizeBytes() const {
  throw FbossError("CredoPhyAsic doesn't support MMU feature");
}
int CredoPhyAsic::getMaxNumLogicalPorts() const {
  throw FbossError("CredoPhyAsic doesn't support logical ports feature");
}
uint32_t CredoPhyAsic::getMaxWideEcmpSize() const {
  throw FbossError("CredoPhyAsic doesn't support ecmp feature");
}
uint32_t CredoPhyAsic::getMaxLagMemberSize() const {
  throw FbossError("CredoPhyAsic doesn't support lag feature");
}
uint32_t CredoPhyAsic::getPacketBufferUnitSize() const {
  throw FbossError("CredoPhyAsic doesn't support MMU feature");
}
uint32_t CredoPhyAsic::getPacketBufferDescriptorSize() const {
  throw FbossError("CredoPhyAsic doesn't support MMU feature");
}
uint32_t CredoPhyAsic::getMaxVariableWidthEcmpSize() const {
  return 512;
}
uint32_t CredoPhyAsic::getMaxEcmpSize() const {
  return 4096;
}

uint32_t CredoPhyAsic::getNumCores() const {
  throw FbossError("Num cores API not supported");
}

bool CredoPhyAsic::scalingFactorBasedDynamicThresholdSupported() const {
  throw FbossError("CredoPhyAsic doesn't support MMU feature");
}
int CredoPhyAsic::getBufferDynThreshFromScalingFactor(
    cfg::MMUScalingFactor /* scalingFactor */) const {
  throw FbossError("CredoPhyAsic doesn't support MMU feature");
}
uint32_t CredoPhyAsic::getStaticQueueLimitBytes() const {
  throw FbossError("CredoPhyAsic doesn't support MMU feature");
}
uint32_t CredoPhyAsic::getNumMemoryBuffers() const {
  throw FbossError("CredoPhyAsic doesn't support MMU feature");
}
int CredoPhyAsic::getMidPriCpuQueueId() const {
  throw FbossError("Credo ASIC does not support cpu queue");
}
int CredoPhyAsic::getHiPriCpuQueueId() const {
  throw FbossError("Credo ASIC does not support cpu queue");
}
std::optional<uint32_t> CredoPhyAsic::getMaxArsGroups() const {
  return std::nullopt;
}

std::optional<uint32_t> CredoPhyAsic::getArsBaseIndex() const {
  return std::nullopt;
}
}; // namespace facebook::fboss
