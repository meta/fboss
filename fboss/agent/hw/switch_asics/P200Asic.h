// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/hw/sai/impl/util.h"
#include "fboss/agent/hw/switch_asics/TajoAsic.h"

namespace facebook::fboss {

class P200Asic : public TajoAsic {
 public:
  P200Asic(
      std::optional<int64_t> switchId,
      cfg::SwitchInfo switchInfo,
      std::optional<cfg::SdkVersion> sdkVersion = std::nullopt)
      : TajoAsic(switchId, switchInfo, sdkVersion, {cfg::SwitchType::NPU}) {
    HwAsic::setDefaultStreamType(cfg::StreamType::UNICAST);
  }
  bool isSupported(Feature feature) const override {
    return getSwitchType() != cfg::SwitchType::FABRIC
        ? isSupportedNonFabric(feature)
        : isSupportedFabric(feature);
  }
  const std::map<cfg::PortType, cfg::PortLoopbackMode>& desiredLoopbackModes()
      const override;
  cfg::AsicType getAsicType() const override {
    return cfg::AsicType::ASIC_TYPE_P200;
  }
  std::vector<InternalSystemPortConfig> getInternalSystemPortConfig(
      const CpuPortCoreAndPortIndex& cpuPortsCoreAndPortIdx) const override;
  phy::DataPlanePhyChipType getDataPlanePhyChipType() const override {
    return phy::DataPlanePhyChipType::IPHY;
  }
  cfg::PortSpeed getMaxPortSpeed() const override {
    return cfg::PortSpeed::HUNDREDG;
  }
  std::set<cfg::StreamType> getQueueStreamTypes(
      cfg::PortType portType) const override;
  int getDefaultNumPortQueues(
      cfg::StreamType streamType,
      cfg::PortType /*portType*/) const override;
  uint32_t getMaxLabelStackDepth() const override {
    return 9;
  }
  uint64_t getMMUSizeBytes() const override {
    return 88 * 1024 * 1024;
  }
  uint64_t getSramSizeBytes() const override {
    // No HBM!
    return getMMUSizeBytes();
  }
  uint32_t getMaxMirrors() const override {
    // TODO - verify this
    return 4;
  }
  std::optional<uint64_t> getDefaultReservedBytes(
      cfg::StreamType /*streamType*/,
      cfg::PortType /*portType*/) const override {
    // Concept of reserved bytes does not apply to P200
    return 0;
  }
  std::optional<cfg::MMUScalingFactor> getDefaultScalingFactor(
      cfg::StreamType /*streamType*/,
      bool /*cpu*/) const override {
    // Concept of scaling factor does not apply to P200; returning a default
    return cfg::MMUScalingFactor::TWO;
  }
  int getMaxNumLogicalPorts() const override {
    // 256 physical lanes + cpu
    return 257;
  }
  uint16_t getMirrorTruncateSize() const override {
    return 343;
  }
  uint32_t getMaxWideEcmpSize() const override {
    return 128;
  }
  uint32_t getMaxLagMemberSize() const override {
    return 512;
  }
  int getSflowPortIDOffset() const override {
    return 1000;
  }
  uint32_t getSflowShimHeaderSize() const override {
    return 9;
  }
  std::optional<uint32_t> getPortSerdesPreemphasis() const override {
    return 50;
  }
  uint32_t getPacketBufferUnitSize() const override {
    // P200 buffer unit size is 512 bytes, different than other ASIC families
    return 512;
  }
  uint32_t getPacketBufferDescriptorSize() const override {
    return 40;
  }
  uint32_t getMaxVariableWidthEcmpSize() const override {
    return 512;
  }
  uint32_t getMaxEcmpSize() const override {
    return 512;
  }
  std::optional<uint32_t> getMaxEcmpGroups() const override {
    // MT-697
    // fbsource/third-party/tp2/leaba-sdk/1.42.8/sdk-1.42.8/sai/src/sai_device.h
    // MAX_NEXT_HOPS = 4096
    return 4096;
  }
  std::optional<uint32_t> getMaxEcmpMembers() const override {
    // MT-697
    // fbsource/third-party/tp2/leaba-sdk/1.42.8/sdk-1.42.8/sai/src/sai_device.h
    // MAX_NEXT_HOP_GROUP_MEMBERS = 32768
    return 32768;
  }
  std::optional<uint32_t> getMaxNdpTableSize() const override {
    return 88465;
  }
  std::optional<uint32_t> getMaxArpTableSize() const override {
    return 176881;
  }
  uint32_t getNumCores() const override {
    return 12;
  }
  uint32_t getStaticQueueLimitBytes() const override {
    return 16000 * getPacketBufferUnitSize();
  }
  uint32_t getNumMemoryBuffers() const override {
    return 1;
  }

  // P200 has one egress pool. A queue with reservedBytes must bind to a pool
  // that already has a pool-level reserve, and that reserve cannot be changed
  // after a buffer profile is bound (S1SW-41991). Vendor asked for 20MB at
  // creation. SAI also reserves 5MB for control traffic and 2MB for TC7 on
  // this same pool. The PFC test flag (~1.5MB) is replaced with the 50MB
  // Palladium2 egress pool from SAI hw/pfc/test_pfc_tx.py before the pool
  // is created. That size holds the reserves.
  uint64_t getEgressBufferPoolReservedBytes() const override {
    return 20 * 1024 * 1024; // 20 MB
  }
  uint64_t getInternalEgressBufferPoolReservedBytes() const override {
    return 7 * 1024 * 1024; // 7 MB
  }
  uint64_t getSaiPfcEgressBufferPoolBytes() const override {
    return 50 * 1024 * 1024; // 50 MB
  }

  std::vector<prbs::PrbsPolynomial> getSupportedPrbsPolynomials()
      const override {
    return {
        prbs::PrbsPolynomial::PRBS9,
        prbs::PrbsPolynomial::PRBS11,
        prbs::PrbsPolynomial::PRBS13,
        prbs::PrbsPolynomial::PRBS15,
        prbs::PrbsPolynomial::PRBS31,
    };
  }

  cfg::Range64 getReservedEncapIndexRange() const override;

 private:
  bool isSupportedFabric(Feature feature) const;
  bool isSupportedNonFabric(Feature feature) const;
};

} // namespace facebook::fboss
