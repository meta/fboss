// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include <set>

namespace facebook::fboss {

class FakeAsic : public HwAsic {
 public:
  FakeAsic(
      std::optional<int64_t> switchId,
      cfg::SwitchInfo switchInfo,
      std::optional<cfg::SdkVersion> sdkVersion = std::nullopt)
      : HwAsic(
            switchId,
            switchInfo,
            sdkVersion,
            {cfg::SwitchType::NPU,
             cfg::SwitchType::VOQ,
             cfg::SwitchType::FABRIC}) {}

  bool isSupported(Feature feature) const override {
    switch (feature) {
      case Feature::HSDK:
      case Feature::OBJECT_KEY_CACHE:
      case Feature::RESOURCE_USAGE_STATS:
      case Feature::PKTIO:
      case Feature::WEIGHTED_NEXTHOPGROUP_MEMBER:
      case Feature::SAI_WEIGHTED_NEXTHOPGROUP_MEMBER:
      case Feature::INGRESS_FIELD_PROCESSOR_FLEX_COUNTER:
      case Feature::OBM_COUNTERS:
      case Feature::PTP_TC:
      case Feature::PTP_TC_PCS:
      case Feature::EGRESS_QUEUE_FLEX_COUNTER:
      case Feature::NON_UNICAST_HASH:
      // Can be removed once CS00012110063 is resolved
      case Feature::SAI_PORT_SPEED_CHANGE:
      case Feature::SAI_MPLS_LABEL_LOOKUP_FAIL_COUNTER: // TODO(pshaikh):
                                                        // support in fake
      case HwAsic::Feature::LINK_TRAINING:
      case HwAsic::Feature::SAI_PORT_VCO_CHANGE:
      case HwAsic::Feature::LINK_INACTIVE_BASED_ISOLATE:
      case HwAsic::Feature::SWITCH_ISOLATE:
      case HwAsic::Feature::ANY_TRAP_DROP_COUNTER:
      case HwAsic::Feature::LINK_ACTIVE_INACTIVE_NOTIFY:
      case HwAsic::Feature::PORT_MTU_ERROR_TRAP:
      case HwAsic::Feature::NO_RX_REASON_TRAP:
      case HwAsic::Feature::SDK_REGISTER_DUMP:
      case HwAsic::Feature::ECMP_RANDOM_SPRAY_HIERARCHICAL_LEVEL:
      case HwAsic::Feature::TEMPERATURE_MONITORING:
      case HwAsic::Feature::SWITCH_WIDE_LINK_UP_DEBOUNCE:
      case HwAsic::Feature::SRV6_BINDING_USID_UA_USD:
      case HwAsic::Feature::PORT_LATCH_STATUS:
        return false;

      default:
        return true;
    }
  }
  cfg::AsicType getAsicType() const override {
    return cfg::AsicType::ASIC_TYPE_FAKE;
  }
  std::string getVendor() const override {
    return "fake";
  }
  phy::DataPlanePhyChipType getDataPlanePhyChipType() const override {
    return phy::DataPlanePhyChipType::IPHY;
  }
  cfg::PortSpeed getMaxPortSpeed() const override {
    return cfg::PortSpeed::HUNDREDG;
  }
  std::set<cfg::StreamType> getQueueStreamTypes(
      cfg::PortType portType) const override {
    switch (portType) {
      case cfg::PortType::CPU_PORT:
        return {cfg::StreamType::MULTICAST};
      case cfg::PortType::INTERFACE_PORT:
      case cfg::PortType::MANAGEMENT_PORT:
      case cfg::PortType::RECYCLE_PORT:
      case cfg::PortType::EVENTOR_PORT:
      case cfg::PortType::HYPER_PORT:
      case cfg::PortType::HYPER_PORT_MEMBER:
        return {cfg::StreamType::UNICAST};
      case cfg::PortType::FABRIC_PORT:
        return {cfg::StreamType::FABRIC_TX};
    }
    throw FbossError(
        "Fake ASIC does not support:",
        apache::thrift::util::enumNameSafe(portType));
  }
  int getDefaultNumPortQueues(
      cfg::StreamType streamType,
      cfg::PortType portType) const override {
    if (portType == cfg::PortType::CPU_PORT) {
      // Match fake SAI's supported CPU queue count.
      return 8;
    }
    return streamType == cfg::StreamType::UNICAST ? 8 : 10;
  }
  uint32_t getMaxLabelStackDepth() const override {
    // Copying TH3's max label stack depth
    return 9;
  }
  uint64_t getMMUSizeBytes() const override {
    // Fake MMU size
    return 64 * 1024 * 1024;
  }

  uint64_t getSramSizeBytes() const override {
    // No HBM!
    return getMMUSizeBytes();
  }

  uint32_t getMaxMirrors() const override {
    return 4;
  }
  std::optional<uint64_t> getDefaultReservedBytes(
      cfg::StreamType /*streamType*/,
      cfg::PortType portType) const override {
    // Mimicking TH
    return portType == cfg::PortType::CPU_PORT ? 1664 : 0;
  }
  std::optional<cfg::MMUScalingFactor> getDefaultScalingFactor(
      cfg::StreamType /*streamType*/,
      bool /*cpu*/) const override {
    // Mimicking TH
    return cfg::MMUScalingFactor::TWO;
  }
  int getMaxNumLogicalPorts() const override {
    return 134;
  }
  uint16_t getMirrorTruncateSize() const override {
    return 0;
  }
  uint32_t getMaxWideEcmpSize() const override {
    return 512;
  }
  uint32_t getMaxLagMemberSize() const override {
    return 256;
  }
  uint32_t getSflowShimHeaderSize() const override {
    return 0;
  }
  std::optional<uint32_t> getPortSerdesPreemphasis() const override {
    return std::nullopt;
  }
  uint32_t getPacketBufferUnitSize() const override {
    // Mimicking TH
    return 208;
  }
  uint32_t getPacketBufferDescriptorSize() const override {
    return 0;
  }
  uint32_t getMaxEcmpSize() const override {
    return 512;
  }
  std::optional<uint32_t> getMaxEcmpGroups() const override {
    return 4;
  }
  std::optional<uint32_t> getMaxEcmpMembers() const override {
    return 128;
  }
  std::optional<uint32_t> getMaxMySidEntries() const override {
    return 8;
  }
  std::optional<uint32_t> getMaxSrv6EcmpNextHops() const override {
    return 16;
  }
  std::optional<uint32_t> getMaxSrv6SingleNextHops() const override {
    return 8;
  }
  std::optional<uint32_t> getMaxNdpTableSize() const override {
    return 8192;
  }
  std::optional<uint32_t> getMaxArpTableSize() const override {
    return 8192;
  }
  AsicVendor getAsicVendor() const override {
    return HwAsic::AsicVendor::ASIC_VENDOR_FAKE;
  }
  uint32_t getMaxVariableWidthEcmpSize() const override {
    return 128;
  }
  uint32_t getNumCores() const override {
    return 0;
  }
  bool scalingFactorBasedDynamicThresholdSupported() const override {
    return true;
  }
  int getBufferDynThreshFromScalingFactor(
      cfg::MMUScalingFactor scalingFactor) const override {
    // Mimicking TH
    switch (scalingFactor) {
      case cfg::MMUScalingFactor::ONE:
        return 0;
      case cfg::MMUScalingFactor::EIGHT:
        return 3;
      case cfg::MMUScalingFactor::ONE_128TH:
        return -7;
      case cfg::MMUScalingFactor::ONE_64TH:
        return -6;
      case cfg::MMUScalingFactor::ONE_32TH:
        return -5;
      case cfg::MMUScalingFactor::ONE_16TH:
        return -4;
      case cfg::MMUScalingFactor::ONE_8TH:
        return -3;
      case cfg::MMUScalingFactor::ONE_QUARTER:
        return -2;
      case cfg::MMUScalingFactor::ONE_HALF:
        return -1;
      case cfg::MMUScalingFactor::TWO:
        return 1;
      case cfg::MMUScalingFactor::FOUR:
        return 2;
      case cfg::MMUScalingFactor::ONE_HUNDRED_TWENTY_EIGHT:
      case cfg::MMUScalingFactor::ONE_32768TH:
        // Unsupported
        throw FbossError(
            "Unsupported scaling factor : ",
            apache::thrift::util::enumNameSafe(scalingFactor));
    }
    CHECK(0) << "Should never get here";
    return -1;
  }
  uint32_t getStaticQueueLimitBytes() const override {
    return 0;
  }
  cfg::Range64 getReservedEncapIndexRange() const override {
    return makeRange(1000, 2000);
  }
  HwAsic::RecyclePortInfo getRecyclePortInfo(
      InterfaceNodeRole /* intfRole */) const override {
    return {
        .coreId = 0,
        .corePortIndex = 1,
        .speedMbps = 10000 // 10G
    };
  }
  uint32_t getNumMemoryBuffers() const override {
    // Divisor in SaiBufferManager::getIngressPoolDivisor(), called every stats
    // update; 0 made the hw agent SIGFPE and crash loop.
    return 1;
  }
  int getMidPriCpuQueueId() const override {
    return 2;
  }
  int getHiPriCpuQueueId() const override {
    return 7;
  }
  std::optional<uint32_t> getMaxArsGroups() const override {
    return 4;
  }
  std::optional<uint32_t> getArsBaseIndex() const override {
    return std::nullopt;
  }
  std::vector<int16_t> getCbfcSenderCreditSizes() const override {
    return {32, 56};
  }
  std::vector<int16_t> getCbfcSenderPacketOverheads() const override {
    return {0, 16, 32, 48, 64, 80};
  }
};

/**
 * FakeAsicNoWarmboot is a FakeAsic that does not support warmboot.
 * This is useful for testing cold boot scenarios.
 */
class FakeAsicNoWarmboot : public FakeAsic {
 public:
  using FakeAsic::FakeAsic;

  bool isSupported(Feature feature) const override {
    if (feature == Feature::WARMBOOT) {
      return false;
    }
    return FakeAsic::isSupported(feature);
  }

  cfg::AsicType getAsicType() const override {
    return cfg::AsicType::ASIC_TYPE_FAKE_NO_WARMBOOT;
  }
};

} // namespace facebook::fboss
