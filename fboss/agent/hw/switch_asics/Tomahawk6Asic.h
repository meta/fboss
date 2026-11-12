// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/switch_asics/BroadcomXgsAsic.h"

namespace facebook::fboss {

class Tomahawk6Asic : public BroadcomXgsAsic {
 public:
  using BroadcomXgsAsic::BroadcomXgsAsic;
  bool isSupported(Feature) const override;
  cfg::AsicType getAsicType() const override {
    return cfg::AsicType::ASIC_TYPE_TOMAHAWK6;
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
    return 500000 * getMMUCellSize();
  }
  uint64_t getSramSizeBytes() const override {
    // No HBM!
    return getMMUSizeBytes();
  }
  uint32_t getMMUCellSize() const {
    return 420;
  }
  uint32_t getNumCellsAvailable(PlatformType /*platformType*/) const override {
    return 629986;
  }
  uint32_t getSaiPhysicalLaneId(
      PlatformType platformType,
      cfg::PortType portType,
      uint32_t chipId,
      uint32_t logicalLane) const override {
    const auto laneId = BroadcomXgsAsic::getSaiPhysicalLaneId(
        platformType, portType, chipId, logicalLane);
    // TH6 management port lane numbering is one-based relative to the generic
    // BCM XGS formula: core 64 maps to lanes 514-517.
    return portType == cfg::PortType::MANAGEMENT_PORT ? laneId + 1 : laneId;
  }
  std::optional<uint64_t> getDefaultReservedBytes(
      cfg::StreamType /*streamType*/,
      cfg::PortType portType) const override {
    return portType == cfg::PortType::CPU_PORT ? (24 * getMMUCellSize()) : 0;
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
    // TODO: update numbers if necessary
    return 128;
  }
  uint32_t getMaxLagMemberSize() const override {
    // TODO: update numbers if necessary
    return 64;
  }
  uint32_t getPacketBufferUnitSize() const override {
    return 420;
  }
  uint32_t getPacketBufferDescriptorSize() const override {
    // TODO: update numbers if necessary
    return 48;
  }
  uint32_t getMaxVariableWidthEcmpSize() const override {
    // TODO: update numbers if necessary
    return 512;
  }
  uint32_t getMaxEcmpSize() const override {
    // TODO: update numbers if necessary
    return 4096;
  }
  std::optional<uint32_t> getMaxEcmpGroups() const override {
    return 4096;
  }
  std::optional<uint32_t> getMaxEcmpMembers() const override {
    return 128000;
  }
  std::optional<AcceptedValues> getAcceptedLinkUpHoldoffTimeMs()
      const override {
    return AcceptedValues::range(2, 500);
  }
  uint32_t getStaticQueueLimitBytes() const override {
    return getMMUSizeBytes();
  }
  uint32_t getNumMemoryBuffers() const override {
    return 1;
  }
  std::optional<uint32_t> getMaxNdpTableSize() const override {
    return 8192;
  }
  std::optional<uint32_t> getMaxVrfs() const override {
    return 8192;
  }
  std::optional<uint32_t> getMaxRoutes() const override {
    return 125000;
  }
  std::optional<uint32_t> getMaxArsGroups() const override;
  std::optional<uint32_t> getArsBaseIndex() const override;
  std::optional<uint32_t> getMaxArsWidth() const override;

  cfg::L2LearningMode getDefaultL2LearningMode() const override {
    return cfg::L2LearningMode::SOFTWARE;
  }
};

} // namespace facebook::fboss
