// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/switch_asics/Ramon3Asic.h"
#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/FbossError.h"

namespace facebook::fboss {
bool Ramon3Asic::isSupported(Feature feature) const {
  switch (feature) {
    case HwAsic::Feature::REMOVE_PORTS_FOR_COLDBOOT:
    case HwAsic::Feature::FABRIC_PORTS:
    case HwAsic::Feature::LINK_TRAINING:
    case HwAsic::Feature::PMD_RX_LOCK_STATUS:
    case HwAsic::Feature::PMD_RX_SIGNAL_DETECT:
    case HwAsic::Feature::FEC:
    case HwAsic::Feature::FEC_CORRECTED_BITS:
    case HwAsic::Feature::PORT_FABRIC_ISOLATE:
    case HwAsic::Feature::SWITCH_DROP_STATS:
    case HwAsic::Feature::SAI_CONFIGURE_SIX_TAP:
    case HwAsic::Feature::TELEMETRY_AND_MONITORING:
    case HwAsic::Feature::WARMBOOT:
    case HwAsic::Feature::SAI_PORT_ERR_STATUS:
    case HwAsic::Feature::SAI_FEC_COUNTERS:
    case HwAsic::Feature::SAI_FEC_CORRECTED_BITS:
    case HwAsic::Feature::DTL_WATERMARK_COUNTER:
    case HwAsic::Feature::ZERO_SDK_WRITE_WARMBOOT:
    case HwAsic::Feature::SAI_PRBS:
    case HwAsic::Feature::PORT_SERDES_ZERO_PREEMPHASIS:
    case HwAsic::Feature::LINK_ACTIVE_INACTIVE_NOTIFY:
    case HwAsic::Feature::FABRIC_LINK_DOWN_CELL_DROP_COUNTER:
    case HwAsic::Feature::CRC_ERROR_DETECT:
    case HwAsic::Feature::SAI_ECMP_HASH_ALGORITHM:
    case HwAsic::Feature::DATA_CELL_FILTER:
    case HwAsic::Feature::SWITCH_REACHABILITY_CHANGE_NOTIFY:
    case HwAsic::Feature::CPU_QUEUE_WATERMARK_STATS:
    case HwAsic::Feature::FEC_ERROR_DETECT_ENABLE:
    case HwAsic::Feature::EGRESS_POOL_AVAILABLE_SIZE_ATTRIBUTE_SUPPORTED:
    case HwAsic::Feature::SDK_REGISTER_DUMP:
    case HwAsic::Feature::RX_SERDES_PARAMETERS:
    case HwAsic::Feature::TEMPERATURE_MONITORING:
    case HwAsic::Feature::VENDOR_SWITCH_NOTIFICATION:
    case HwAsic::Feature::TECH_SUPPORT:
    case HwAsic::Feature::FABRIC_INTER_CELL_JITTER_WATERMARK:
    case HwAsic::Feature::MAC_TRANSMIT_DATA_QUEUE_WATERMARK:
    case HwAsic::Feature::FABRIC_LINK_MONITORING:
    case HwAsic::Feature::CPU_PORT:
    case HwAsic::Feature::SWITCH_ISOLATE:
      return true;
    case HwAsic::Feature::SAI_PORT_SERDES_FIELDS_RESET:
    case HwAsic::Feature::FABRIC_TX_QUEUES:
    case HwAsic::Feature::RX_LANE_SQUELCH_ENABLE:
    case HwAsic::Feature::SAI_PORT_SERDES_PROGRAMMING:
      return getAsicMode() != AsicMode::ASIC_MODE_SIM;
    // Dual stage L1 (FE13) fabric features
    case HwAsic::Feature::CABLE_PROPOGATION_DELAY:
      return fabricNodeRole_ == FabricNodeRole::DUAL_STAGE_L1;
    case HwAsic::Feature::ARS_ALTERNATE_MEMBERS:
    case HwAsic::Feature::CPU_PORT_EGRESS_BUFFER_POOL:
    case HwAsic::Feature::SAI_SERDES_RX_REACH:
    case HwAsic::Feature::SAI_SERDES_PRECODING:
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
    case HwAsic::Feature::ECMP_RANDOM_SPRAY_HIERARCHICAL_LEVEL:
    case HwAsic::Feature::ACL_DST_IPV6_WORD_QUALIFIERS:
    // TODO(nivinl): Reported from SDK 16.x, enable once that is in
    // production.
    case HwAsic::Feature::RX_PACKET_TYPE:
    case HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::PORT_LATCH_STATUS:
    default:
      return false;
  }
}

std::set<cfg::StreamType> Ramon3Asic::getQueueStreamTypes(
    cfg::PortType portType) const {
  switch (portType) {
    case cfg::PortType::CPU_PORT:
      // Fabric switches support CPU port for packet send/receive,
      // but don't have CPU queues
      return {};
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
      "Ramon3 Asic does not support port type: ",
      apache::thrift::util::enumNameSafe(portType));
}

int Ramon3Asic::getDefaultNumPortQueues(
    cfg::StreamType /* streamType */,
    cfg::PortType portType) const {
  if (portType == cfg::PortType::CPU_PORT) {
    // Fabric switches support CPU port for packet send/receive,
    // but don't have CPU queues
    return 0;
  }
  if (portType != cfg::PortType::FABRIC_PORT) {
    throw FbossError(
        "Ramon3 Asic does not support port type: ",
        apache::thrift::util::enumNameSafe(portType));
  }
  // On Ramons we use a single fabric queue for all traffic
  return 1;
}
std::optional<uint64_t> Ramon3Asic::getDefaultReservedBytes(
    cfg::StreamType /* streamType */,
    cfg::PortType /* portType */) const {
  throw FbossError("Ramon3 doesn't support queue feature");
}
std::optional<cfg::MMUScalingFactor> Ramon3Asic::getDefaultScalingFactor(
    cfg::StreamType /* streamType */,
    bool /* cpu */) const {
  throw FbossError("Ramon3 doesn't support queue feature");
}

uint32_t Ramon3Asic::getMaxMirrors() const {
  return 0;
}
uint16_t Ramon3Asic::getMirrorTruncateSize() const {
  throw FbossError("Ramon3 doesn't support mirror feature");
}

uint32_t Ramon3Asic::getMaxLabelStackDepth() const {
  throw FbossError("Ramon3 doesn't support label feature");
};
uint64_t Ramon3Asic::getMMUSizeBytes() const {
  throw FbossError("Ramon3 doesn't support MMU feature");
};
uint64_t Ramon3Asic::getSramSizeBytes() const {
  throw FbossError("Ramon3 doesn't support MMU feature");
}
int Ramon3Asic::getMaxNumLogicalPorts() const {
  throw FbossError("Ramon3 doesn't support logical ports feature");
}
uint32_t Ramon3Asic::getMaxWideEcmpSize() const {
  throw FbossError("Ramon3 doesn't support ecmp feature");
}
uint32_t Ramon3Asic::getMaxLagMemberSize() const {
  throw FbossError("Ramon3 doesn't support lag feature");
}
uint32_t Ramon3Asic::getPacketBufferUnitSize() const {
  throw FbossError("Ramon3 doesn't support MMU feature");
}
uint32_t Ramon3Asic::getPacketBufferDescriptorSize() const {
  throw FbossError("Ramon3 doesn't support MMU feature");
}
uint32_t Ramon3Asic::getMaxVariableWidthEcmpSize() const {
  return 512;
}
uint32_t Ramon3Asic::getMaxEcmpSize() const {
  return 4096;
}

uint32_t Ramon3Asic::getNumCores() const {
  return 2;
}

bool Ramon3Asic::scalingFactorBasedDynamicThresholdSupported() const {
  throw FbossError("Ramon3 doesn't support MMU feature");
}
int Ramon3Asic::getBufferDynThreshFromScalingFactor(
    cfg::MMUScalingFactor /* scalingFactor */) const {
  throw FbossError("Ramon3 doesn't support MMU feature");
}
uint32_t Ramon3Asic::getStaticQueueLimitBytes() const {
  throw FbossError("Ramon3 doesn't support MMU feature");
}
uint32_t Ramon3Asic::getNumMemoryBuffers() const {
  throw FbossError("Ramon3 doesn't support MMU feature");
}

const std::map<cfg::PortType, cfg::PortLoopbackMode>&
Ramon3Asic::desiredLoopbackModes() const {
  static const std::map<cfg::PortType, cfg::PortLoopbackMode> kLoopbackMode = {
      {cfg::PortType::FABRIC_PORT, cfg::PortLoopbackMode::MAC}};
  return kLoopbackMode;
}

uint32_t Ramon3Asic::getVirtualDevices() const {
  return 2;
}

const std::set<uint16_t>& Ramon3Asic::getL1BaseFabricPortsToConnectToL2() {
  static const std::set<uint16_t> l1BaseFabricPortsToConnectToL2 = {
      64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,
      79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,
      94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108,
      109, 110, 111, 112, 113, 114, 116, 117, 118, 120, 121, 122, 123, 124, 125,
      126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140,
      141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155,
      156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170,
      171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185,
      186, 187, 188, 189, 190, 191, 320, 321, 322, 323, 324, 325, 326, 327, 328,
      329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343,
      344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358,
      359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 370, 371, 372, 374, 375,
      376, 377, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388, 389, 390,
      391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405,
      406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420,
      421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435,
      436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447};

  return l1BaseFabricPortsToConnectToL2;
}

const std::set<uint16_t>& Ramon3Asic::getL1FabricPortsToConnectToL2() const {
  return l1FabricPortsToConnectToL2_;
}

}; // namespace facebook::fboss
