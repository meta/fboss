// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <optional>

#include <fboss/agent/state/Thrifty.h>
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/MirrorOnDropReport.h"
#include "folly/IPAddress.h"
#include "folly/MacAddress.h"

namespace facebook::fboss {

MirrorOnDropReport::MirrorOnDropReport(
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
    std::map<cfg::MirrorOnDropAgingGroup, int32_t> agingGroupAgingIntervalUsecs,
    std::optional<int32_t> samplingRate,
    std::optional<int32_t> dropPacketRateThreshold,
    std::optional<int32_t> aggregateDropPacketRateThreshold)
    : ThriftStructNode<MirrorOnDropReport, state::MirrorOnDropReportFields>() {
  set<switch_state_tags::name>(name);
  if (mirrorPortId.has_value()) {
    set<switch_state_tags::mirrorPortId>(mirrorPortId.value());
  }
  set<switch_state_tags::localSrcIp>(network::toBinaryAddress(localSrcIp));
  set<switch_state_tags::localSrcPort>(localSrcPort);
  set<switch_state_tags::collectorIp>(network::toBinaryAddress(collectorIp));
  set<switch_state_tags::collectorPort>(collectorPort);
  set<switch_state_tags::mtu>(mtu);
  set<switch_state_tags::truncateSize>(truncateSize);
  set<switch_state_tags::dscp>(dscp);
  set<switch_state_tags::switchMac>(switchMac);
  set<switch_state_tags::firstInterfaceMac>(firstInterfaceMac);
  set<switch_state_tags::modEventToConfigMap>(modEventToConfigMap);
  set<switch_state_tags::agingGroupAgingIntervalUsecs>(
      agingGroupAgingIntervalUsecs);
  set<switch_state_tags::isResolved>(false);
  if (samplingRate.has_value()) {
    set<switch_state_tags::samplingRate>(samplingRate.value());
  }
  if (dropPacketRateThreshold.has_value()) {
    set<switch_state_tags::dropPacketRateThreshold>(
        dropPacketRateThreshold.value());
  }
  if (aggregateDropPacketRateThreshold.has_value()) {
    set<switch_state_tags::aggregateDropPacketRateThreshold>(
        aggregateDropPacketRateThreshold.value());
  }
}

std::string MirrorOnDropReport::getID() const {
  return get<switch_state_tags::name>()->cref();
}

std::optional<PortID> MirrorOnDropReport::getMirrorPortId() const {
  if (auto portId = get<switch_state_tags::mirrorPortId>()) {
    return PortID(portId->cref());
  }
  return std::nullopt;
}

void MirrorOnDropReport::setMirrorPortId(PortID portId) {
  set<switch_state_tags::mirrorPortId>(portId);
}

folly::IPAddress MirrorOnDropReport::getLocalSrcIp() const {
  return network::toIPAddress(get<switch_state_tags::localSrcIp>()->toThrift());
}

int16_t MirrorOnDropReport::getLocalSrcPort() const {
  return get<switch_state_tags::localSrcPort>()->cref();
}

folly::IPAddress MirrorOnDropReport::getCollectorIp() const {
  return network::toIPAddress(
      get<switch_state_tags::collectorIp>()->toThrift());
}

int16_t MirrorOnDropReport::getCollectorPort() const {
  return get<switch_state_tags::collectorPort>()->cref();
}

int16_t MirrorOnDropReport::getMtu() const {
  return get<switch_state_tags::mtu>()->cref();
}

int16_t MirrorOnDropReport::getTruncateSize() const {
  return get<switch_state_tags::truncateSize>()->cref();
}

uint8_t MirrorOnDropReport::getDscp() const {
  return get<switch_state_tags::dscp>()->cref();
}

std::string MirrorOnDropReport::getSwitchMac() const {
  return get<switch_state_tags::switchMac>()->cref();
}

std::string MirrorOnDropReport::getFirstInterfaceMac() const {
  return get<switch_state_tags::firstInterfaceMac>()->cref();
}

std::map<int8_t, cfg::MirrorOnDropEventConfig>
MirrorOnDropReport::getModEventToConfigMap() const {
  return get<switch_state_tags::modEventToConfigMap>()->toThrift();
}

std::map<cfg::MirrorOnDropAgingGroup, int32_t>
MirrorOnDropReport::getAgingGroupAgingIntervalUsecs() const {
  return get<switch_state_tags::agingGroupAgingIntervalUsecs>()->toThrift();
}

std::optional<int32_t> MirrorOnDropReport::getSamplingRate() const {
  if (auto rate = get<switch_state_tags::samplingRate>()) {
    return rate->cref();
  }
  return std::nullopt;
}

std::optional<int32_t> MirrorOnDropReport::getDropPacketRateThreshold() const {
  if (auto threshold = get<switch_state_tags::dropPacketRateThreshold>()) {
    return threshold->cref();
  }
  return std::nullopt;
}

std::optional<int32_t> MirrorOnDropReport::getAggregateDropPacketRateThreshold()
    const {
  if (auto threshold =
          get<switch_state_tags::aggregateDropPacketRateThreshold>()) {
    return threshold->cref();
  }
  return std::nullopt;
}

bool MirrorOnDropReport::isResolved() const {
  return get<switch_state_tags::isResolved>()->cref();
}

void MirrorOnDropReport::setIsResolved(bool resolved) {
  set<switch_state_tags::isResolved>(resolved);
}

std::optional<folly::MacAddress> MirrorOnDropReport::getResolvedCollectorMac()
    const {
  if (auto mac = get<switch_state_tags::resolvedCollectorMac>()) {
    return folly::MacAddress(mac->cref());
  }
  return std::nullopt;
}

void MirrorOnDropReport::setResolvedCollectorMac(folly::MacAddress mac) {
  set<switch_state_tags::resolvedCollectorMac>(mac.toString());
}

std::optional<cfg::PortDescriptor> MirrorOnDropReport::getResolvedEgressPort()
    const {
  if (auto port = get<switch_state_tags::resolvedEgressPort>()) {
    return port->toThrift();
  }
  return std::nullopt;
}

void MirrorOnDropReport::setResolvedEgressPort(
    const cfg::PortDescriptor& portDesc) {
  set<switch_state_tags::resolvedEgressPort>(portDesc);
}

template struct ThriftStructNode<
    MirrorOnDropReport,
    state::MirrorOnDropReportFields>;

} // namespace facebook::fboss
