/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/switch_asics/TajoAsic.h"

namespace facebook::fboss {

class G202xAsic : public TajoAsic {
 public:
  G202xAsic(
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
  cfg::AsicType getAsicType() const override {
    return cfg::AsicType::ASIC_TYPE_G202X;
  }
  phy::DataPlanePhyChipType getDataPlanePhyChipType() const override {
    return phy::DataPlanePhyChipType::IPHY;
  }
  cfg::PortSpeed getMaxPortSpeed() const override {
    return cfg::PortSpeed::EIGHTHUNDREDG;
  }
  std::set<cfg::StreamType> getQueueStreamTypes(
      cfg::PortType portType) const override;
  int getDefaultNumPortQueues(
      cfg::StreamType streamType,
      cfg::PortType /*portType*/) const override;
  uint32_t getMaxLabelStackDepth() const override {
    return 3;
  }
  uint64_t getMMUSizeBytes() const override {
    return 256 * 1024 * 1024;
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
    // Concept of reserved bytes does not apply to GB
    return 0;
  }
  std::optional<cfg::MMUScalingFactor> getDefaultScalingFactor(
      cfg::StreamType /*streamType*/,
      bool /*cpu*/) const override {
    // Concept of scaling factor does not apply returning the same value TH3
    return cfg::MMUScalingFactor::TWO;
  }
  bool scalingFactorBasedDynamicThresholdSupported() const override {
    return true;
  }
  int getBufferDynThreshFromScalingFactor(
      cfg::MMUScalingFactor scalingFactor) const override {
    return HwAsic::getBufferDynThreshFromScalingFactor(scalingFactor);
  }
  const std::map<cfg::PortType, cfg::PortLoopbackMode>& desiredLoopbackModes()
      const override;
  int getMaxNumLogicalPorts() const override {
    // 8*32 + 2 CPU
    return 258;
  }

  uint16_t getMirrorTruncateSize() const override {
    return 220;
  }
  uint32_t getMaxWideEcmpSize() const override {
    return 128;
  }
  uint32_t getMaxLagMemberSize() const override {
    return 512;
  }
  int getSflowPortIDOffset() const override {
    return 0;
  }
  uint32_t getSflowShimHeaderSize() const override {
    return 9;
  }
  std::optional<uint32_t> getPortSerdesPreemphasis() const override {
    return 50;
  }
  uint32_t getPacketBufferUnitSize() const override {
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
    return 1024;
  }
  std::optional<uint32_t> getMaxMySidEntries() const override {
    return 2048;
  }
  std::optional<uint32_t> getMaxRouteCounters() const override {
    return 4096;
  }
  std::optional<uint32_t> getMaxSrv6EcmpNextHops() const override {
    return 7800;
  }
  std::optional<uint32_t> getMaxSrv6SingleNextHops() const override {
    return 3000;
  }
  std::optional<uint32_t> getMaxSrv6UcmpDistinctWeights() const override {
    return 8;
  }
  std::optional<uint32_t> getMaxEcmpMembers() const override {
    /*
     * G202x has two level ECMP. Each level owns one resolution stage of the
     * Leaba indirection table, which holds one line per NHG/ECMP member:
     *
     *   usable(stage) = INDIRECTION_TABLE_SIZE
     *                     - wcmp_base(stage)
     *                     - max_mp_group_size
     *
     * INDIRECTION_TABLE_SIZE is 10240 (resolution_traits_*.h) and the bases
     * come from the GID limits in our ASIC config JSON, which the SDK reads in
     * sai_config_parser.cpp:
     *
     *   l2_service_port_gid_limit 1024   next_hop_gid_limit 512
     *   prefix_object_gid_limit      0   ip_hosts_limit     512
     *   max_mp_group_size          128
     *
     * Level 1 (WCMP0), base = l2 service port + prefix object GID limits:
     *        10240 - (1024 + 0) - 128 = 9088 members
     *
     * Level 2 (WCMP1), base = next hop GID limit + IP hosts limit:
     *        10240 - (512 + 512) - 128 = 9088 members  (SDK 25.5)
     *
     * Together 9088 + 9088 = 18176. Empirically the last 4 are not usable
     * (test passed at 18172, failed at 18173) - unexplained, same offset on
     * every SDK, tracked in MT-803.
     *
     * SDK 26.2 costs one more member. Both SDKs reserve an internal "ip
     * collapse drop" next hop GID, but they charge it to different budgets.
     * With next_hop_gid_limit = L:
     *   25.5: the drop GID is carved out of L, so only L-1 next hop GIDs are
     *         usable and get_wcmp_table_base() returns L.
     *   26.2: the drop GID is reserved on top of L, so all L are usable and
     *         get_wcmp_table_base() returns get_internal_gid_limit() = L+1.
     * The higher base costs one level 2 line: 10240 - 1025 - 128 = 9087, so
     * 9088 + 9087 - 4 = 18171.
     */
    return 18171;
  }
  uint32_t getNumCores() const override {
    return 12;
  }
  uint32_t getStaticQueueLimitBytes() const override {
    return 512 * 1024 * getPacketBufferUnitSize();
  }
  uint32_t getNumMemoryBuffers() const override {
    return 1;
  }

  std::optional<uint32_t> getMaxNdpTableSize() const override {
    return 512;
  }

  std::optional<uint32_t> getMaxArpTableSize() const override {
    return 512;
  }

  std::optional<uint32_t> getMaxUnifiedNeighborTableSize() const override {
    return 512;
  }

  cfg::Range64 getReservedEncapIndexRange() const override;

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

 private:
  bool isSupportedFabric(Feature feature) const;
  bool isSupportedNonFabric(Feature feature) const;
};

} // namespace facebook::fboss
