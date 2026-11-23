// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <optional>

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>

#include <fboss/agent/state/Thrifty.h>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeBase.h"

namespace facebook::fboss {

USE_THRIFT_COW(MirrorOnDropReport);

class MirrorOnDropReport : public ThriftStructNode<
                               MirrorOnDropReport,
                               state::MirrorOnDropReportFields> {
 public:
  using BaseT =
      ThriftStructNode<MirrorOnDropReport, state::MirrorOnDropReportFields>;

  MirrorOnDropReport(
      const std::string& name,
      std::optional<PortID> mirrorPortId,
      folly::IPAddress localSrcIp,
      int16_t localSrcPort,
      folly::IPAddress collectorIp,
      int16_t collectorPort,
      int16_t mtu,
      int16_t truncateSize,
      uint8_t dscp,
      std::string switchMac,
      std::string firstInterfaceMac,
      std::map<int8_t, cfg::MirrorOnDropEventConfig> modEventToConfigMap,
      std::map<cfg::MirrorOnDropAgingGroup, int32_t>
          agingGroupAgingIntervalUsecs,
      std::optional<int32_t> samplingRate = std::nullopt,
      std::optional<int32_t> dropPacketRateThreshold = std::nullopt,
      std::optional<int32_t> aggregateDropPacketRateThreshold = std::nullopt);

  std::string getID() const;
  std::optional<PortID> getMirrorPortId() const;
  void setMirrorPortId(PortID portId);
  folly::IPAddress getLocalSrcIp() const;
  int16_t getLocalSrcPort() const;
  folly::IPAddress getCollectorIp() const;
  int16_t getCollectorPort() const;
  int16_t getMtu() const;
  int16_t getTruncateSize() const;
  uint8_t getDscp() const;
  std::string getSwitchMac() const;
  std::string getFirstInterfaceMac() const;
  std::map<int8_t, cfg::MirrorOnDropEventConfig> getModEventToConfigMap() const;
  std::map<cfg::MirrorOnDropAgingGroup, int32_t>
  getAgingGroupAgingIntervalUsecs() const;
  std::optional<int32_t> getSamplingRate() const;
  std::optional<int32_t> getDropPacketRateThreshold() const;
  std::optional<int32_t> getAggregateDropPacketRateThreshold() const;

  // Resolved fields accessors
  bool isResolved() const;
  void setIsResolved(bool resolved);
  std::optional<folly::MacAddress> getResolvedCollectorMac() const;
  void setResolvedCollectorMac(folly::MacAddress mac);
  std::optional<cfg::PortDescriptor> getResolvedEgressPort() const;
  void setResolvedEgressPort(const cfg::PortDescriptor& portDesc);

 private:
  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
