// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/hw/switch_asics/BroadcomXgsAsic.h"

namespace facebook::fboss {

class TomahawkUltra1Asic : public BroadcomXgsAsic {
 public:
  using BroadcomXgsAsic::BroadcomXgsAsic;
  bool isSupported(Feature) const override;
  bool useAllOnesForUdfL2TypeAny() const override;
  cfg::AsicType getAsicType() const override {
    return cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1;
  }
  const std::map<cfg::PortType, cfg::PortLoopbackMode>& desiredLoopbackModes()
      const override;
  phy::DataPlanePhyChipType getDataPlanePhyChipType() const override {
    return phy::DataPlanePhyChipType::IPHY;
  }
  AsicMode getAsicMode() const override {
    static const AsicMode asicMode = std::getenv("BCM_SIM_PATH")
        ? AsicMode::ASIC_MODE_SIM
        : AsicMode::ASIC_MODE_HW;
    return asicMode;
  }
  cfg::PortSpeed getMaxPortSpeed() const override {
    return cfg::PortSpeed::EIGHTHUNDREDG;
  }
  int getDefaultNumPortQueues(
      cfg::StreamType streamType,
      cfg::PortType portType) const override;
  int getBasePortQueueId(cfg::StreamType streamType, cfg::PortType portType)
      const override;
  uint32_t getMaxLabelStackDepth() const override {
    return 9;
  }
  uint64_t getMMUSizeBytes() const override {
    return 59000 * 56;
  }
  uint64_t getSramSizeBytes() const override {
    return getMMUSizeBytes();
  }
  uint32_t getMMUCellSize() const {
    return 56;
  }
  uint32_t getNumCellsAvailable(PlatformType /*platformType*/) const override {
    // TODO: 59000 is from TOO, actual number will come soon.
    return 59000;
  }
  std::optional<uint64_t> getDefaultReservedBytes(
      cfg::StreamType /*streamType*/,
      cfg::PortType portType) const override {
    return portType == cfg::PortType::CPU_PORT ? 1778 : 0;
  }
  std::optional<cfg::MMUScalingFactor> getDefaultScalingFactor(
      cfg::StreamType /*streamType*/,
      bool /*cpu*/) const override {
    return cfg::MMUScalingFactor::TWO;
  }

  int getMaxNumLogicalPorts() const override {
    return 341;
  }
  uint16_t getMirrorTruncateSize() const override {
    return 204;
  }

  uint32_t getMaxWideEcmpSize() const override {
    return 128;
  }
  uint32_t getMaxLagMemberSize() const override {
    return 64;
  }
  uint32_t getPacketBufferUnitSize() const override {
    return 56;
  }
  uint32_t getPacketBufferDescriptorSize() const override {
    return 48;
  }
  uint32_t getMaxVariableWidthEcmpSize() const override {
    return 512;
  }
  uint32_t getMaxEcmpSize() const override {
    return 4096;
  }
  std::optional<uint32_t> getMaxEcmpGroups() const override {
    return 4096;
  }
  std::optional<uint32_t> getMaxEcmpMembers() const override {
    return 64000;
  }
  uint32_t getStaticQueueLimitBytes() const override {
    return getMMUSizeBytes() / 2;
  }
  uint32_t getNumMemoryBuffers() const override {
    return 1;
  }
  std::optional<uint32_t> getMaxArsGroups() const override;
  std::optional<uint32_t> getArsBaseIndex() const override;
  // brcm-sai 16.0_ea_odp maps only these, though the SDK sender table takes
  // more credit sizes.
  std::vector<int16_t> getCbfcSenderCreditSizes() const override {
    return {32, 56};
  }
  std::vector<int16_t> getCbfcSenderPacketOverheads() const override {
    return {0, 16, 32, 48, 64, 80};
  }
};

} // namespace facebook::fboss
