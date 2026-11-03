// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/switch_asics/RamonAsic.h"
#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/FbossError.h"

namespace facebook::fboss {
bool RamonAsic::isSupported(Feature feature) const {
  switch (feature) {
    case HwAsic::Feature::REMOVE_PORTS_FOR_COLDBOOT:
    case HwAsic::Feature::FABRIC_PORTS:
    case HwAsic::Feature::LINK_TRAINING:
    case HwAsic::Feature::SAI_PORT_SERDES_FIELDS_RESET:
    case HwAsic::Feature::FABRIC_TX_QUEUES:
    case HwAsic::Feature::PMD_RX_LOCK_STATUS:
    case HwAsic::Feature::PMD_RX_SIGNAL_DETECT:
    case HwAsic::Feature::FEC:
    case HwAsic::Feature::FEC_CORRECTED_BITS:
    case HwAsic::Feature::PORT_FABRIC_ISOLATE:
    case HwAsic::Feature::SAI_FEC_COUNTERS:
    case HwAsic::Feature::SWITCH_DROP_STATS:
    case HwAsic::Feature::RX_LANE_SQUELCH_ENABLE:
    case HwAsic::Feature::WARMBOOT:
    case HwAsic::Feature::TELEMETRY_AND_MONITORING:
    case HwAsic::Feature::SAI_PORT_SERDES_PROGRAMMING:
    case HwAsic::Feature::SAI_ECMP_HASH_ALGORITHM:
    case HwAsic::Feature::CPU_QUEUE_WATERMARK_STATS:
    case HwAsic::Feature::SWITCH_ISOLATE:
      return true;
    case HwAsic::Feature::ARS_ALTERNATE_MEMBERS:
    case HwAsic::Feature::ARS_FUTURE_PORT_LOAD:
    case HwAsic::Feature::VIRTUAL_ARS_GROUP:
    case HwAsic::Feature::CUT_THROUGH_FORWARDING:
    case HwAsic::Feature::SRV6_MYSID_DISCARD_COUNTER:
    case HwAsic::Feature::SRV6_MYSID_RESOURCE_COUNTER:
    case HwAsic::Feature::SRV6_BINDING_USID_UA_USD:
    case HwAsic::Feature::PBR_ACL:
    case HwAsic::Feature::DEVICE_WATERMARK_SUPPORT:
    case HwAsic::Feature::SWITCH_CUSTOM_DROP_BITMAP_SUPPORT:
    case HwAsic::Feature::SWITCH_DROP_REASON_LIST_SUPPORT:
    case HwAsic::Feature::SAI_SERDES_PRECODING:
    case HwAsic::Feature::ECMP_RANDOM_SPRAY_HIERARCHICAL_LEVEL:
    case HwAsic::Feature::ACL_DST_IPV6_WORD_QUALIFIERS:
    case HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::PORT_LATCH_STATUS:
    default:
      return false;
  }
}

std::set<cfg::StreamType> RamonAsic::getQueueStreamTypes(
    cfg::PortType portType) const {
  switch (portType) {
    case cfg::PortType::CPU_PORT:
    case cfg::PortType::INTERFACE_PORT:
    case cfg::PortType::MANAGEMENT_PORT:
    case cfg::PortType::RECYCLE_PORT:
    case cfg::PortType::EVENTOR_PORT:
    case cfg::PortType::HYPER_PORT:
    case cfg::PortType::HYPER_PORT_MEMBER:
      break;
    case cfg::PortType::FABRIC_PORT:
      return {cfg::StreamType::FABRIC_TX};
  }
  throw FbossError(
      "Ramon Asic does not support port type: ",
      apache::thrift::util::enumNameSafe(portType));
}

int RamonAsic::getDefaultNumPortQueues(
    cfg::StreamType /* streamType */,
    cfg::PortType portType) const {
  if (portType != cfg::PortType::FABRIC_PORT) {
    throw FbossError("Only fabric ports expected on Ramon asic");
  }
  // On Ramons we use a single fabric queue for all
  // traffic
  return 1;
}

std::optional<uint64_t> RamonAsic::getDefaultReservedBytes(
    cfg::StreamType /* streamType */,
    cfg::PortType /* portType */) const {
  throw FbossError("Ramon doesn't support queue feature");
}
std::optional<cfg::MMUScalingFactor> RamonAsic::getDefaultScalingFactor(
    cfg::StreamType /* streamType */,
    bool /* cpu */) const {
  throw FbossError("Ramon doesn't support queue feature");
}

uint32_t RamonAsic::getMaxMirrors() const {
  return 0;
}
uint16_t RamonAsic::getMirrorTruncateSize() const {
  throw FbossError("Ramon doesn't support mirror feature");
}

uint32_t RamonAsic::getMaxLabelStackDepth() const {
  throw FbossError("Ramon doesn't support label feature");
};
uint64_t RamonAsic::getMMUSizeBytes() const {
  throw FbossError("Ramon doesn't support MMU feature");
};
uint64_t RamonAsic::getSramSizeBytes() const {
  throw FbossError("Ramon doesn't support MMU feature");
};
int RamonAsic::getMaxNumLogicalPorts() const {
  throw FbossError("Ramon doesn't support logical ports feature");
}
uint32_t RamonAsic::getMaxWideEcmpSize() const {
  throw FbossError("Ramon doesn't support ecmp feature");
}
uint32_t RamonAsic::getMaxLagMemberSize() const {
  throw FbossError("Ramon doesn't support lag feature");
}
uint32_t RamonAsic::getPacketBufferUnitSize() const {
  throw FbossError("Ramon doesn't support MMU feature");
}
uint32_t RamonAsic::getPacketBufferDescriptorSize() const {
  throw FbossError("Ramon doesn't support MMU feature");
}
uint32_t RamonAsic::getMaxVariableWidthEcmpSize() const {
  return 512;
}
uint32_t RamonAsic::getMaxEcmpSize() const {
  return 4096;
}

uint32_t RamonAsic::getNumCores() const {
  throw FbossError("Num cores API not supported by RamonAsic");
}

bool RamonAsic::scalingFactorBasedDynamicThresholdSupported() const {
  throw FbossError("Ramon doesn't support MMU feature");
}
int RamonAsic::getBufferDynThreshFromScalingFactor(
    cfg::MMUScalingFactor /* scalingFactor */) const {
  throw FbossError("Ramon doesn't support MMU feature");
}
uint32_t RamonAsic::getStaticQueueLimitBytes() const {
  throw FbossError("Ramon doesn't support MMU feature");
}
uint32_t RamonAsic::getNumMemoryBuffers() const {
  throw FbossError("Ramon doesn't support MMU feature");
}

const std::map<cfg::PortType, cfg::PortLoopbackMode>&
RamonAsic::desiredLoopbackModes() const {
  static const std::map<cfg::PortType, cfg::PortLoopbackMode> kLoopbackMode = {
      {cfg::PortType::FABRIC_PORT, cfg::PortLoopbackMode::MAC}};
  return kLoopbackMode;
}
}; // namespace facebook::fboss
