// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/switch_asics/BroadcomXgsAsic.h"

DECLARE_int32(teFlow_gid);

namespace facebook::fboss {

class Tomahawk4Asic : public BroadcomXgsAsic {
 public:
  using BroadcomXgsAsic::BroadcomXgsAsic;
  bool isSupported(Feature) const override;
  cfg::AsicType getAsicType() const override {
    return cfg::AsicType::ASIC_TYPE_TOMAHAWK4;
  }
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
    return cfg::PortSpeed::FOURHUNDREDG;
  }
  int getDefaultNumPortQueues(
      cfg::StreamType streamType,
      cfg::PortType portType) const override;
  uint32_t getMaxLabelStackDepth() const override {
    // one VC label and 8 tunnel labels, same as tomahawk3
    return 9;
  }
  uint64_t getMMUSizeBytes() const override {
    return 2 * 234606 * 254;
  }
  uint64_t getSramSizeBytes() const override {
    // No HBM!
    return getMMUSizeBytes();
  }
  uint32_t getMMUCellSize() const {
    return 254;
  }
  uint32_t getNumCellsAvailable(PlatformType /*platformType*/) const override {
    return 223662;
  }
  std::optional<uint64_t> getDefaultReservedBytes(
      cfg::StreamType /*streamType*/,
      cfg::PortType portType) const override {
    /* TODO: Mimicking TH3 size here, figure out the defaults for TH4*/
    return portType == cfg::PortType::CPU_PORT ? 1778 : 0;
  }
  std::optional<cfg::MMUScalingFactor> getDefaultScalingFactor(
      cfg::StreamType /*streamType*/,
      bool /*cpu*/) const override {
    /* TODO: Mimicking TH3 size here, figure out the defaults for TH4*/
    return cfg::MMUScalingFactor::TWO;
  }

  int getNumLanesPerPhysicalPort() const override;

  int getDefaultACLGroupID() const override;

  int getDefaultTeFlowGroupID() const override;

  int getStationID(int intfId) const override;

  int getMaxNumLogicalPorts() const override {
    return 272;
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
    return 254;
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
    // CS00012328553
    return 2048;
  }
  std::optional<uint32_t> getMaxEcmpMembers() const override {
    // CS00012330051
    return 56000;
  }
  uint32_t getStaticQueueLimitBytes() const override {
    // Per ITM buffers limits the queue size
    return getMMUSizeBytes() / 2;
  }
  uint32_t getNumMemoryBuffers() const override {
    return 2;
  }
  std::optional<uint32_t> getMaxAclTables() const override {
    return 5;
  }
  std::optional<uint32_t> getMaxAclEntries() const override {
    // Max ACL entries per ACL table
    return 256;
  }
  std::optional<uint32_t> getMaxNdpTableSize() const override {
    return 8192;
  }

  std::optional<uint32_t> getMaxVrfs() const override {
    return 8192;
  }

  std::optional<uint32_t> getMaxArpTableSize() const override {
    return 16384;
  }
  std::optional<uint32_t> getMaxArsGroups() const override;
  std::optional<uint32_t> getArsBaseIndex() const override;
  std::optional<uint32_t> getMaxArsWidth() const override;

  cfg::L2LearningMode getDefaultL2LearningMode() const override {
    return cfg::L2LearningMode::SOFTWARE;
  }
};

} // namespace facebook::fboss
