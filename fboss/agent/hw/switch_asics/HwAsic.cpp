/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <folly/Conv.h>
#include <folly/Range.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <cctype>
#include <tuple>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/switch_asics/Agera3PhyAsic.h"
#include "fboss/agent/hw/switch_asics/Chenab2Asic.h"
#include "fboss/agent/hw/switch_asics/ChenabAsic.h"
#include "fboss/agent/hw/switch_asics/CredoPhyAsic.h"
#include "fboss/agent/hw/switch_asics/EbroAsic.h"
#include "fboss/agent/hw/switch_asics/FakeAsic.h"
#include "fboss/agent/hw/switch_asics/G202xAsic.h"
#include "fboss/agent/hw/switch_asics/Jericho2Asic.h"
#include "fboss/agent/hw/switch_asics/Jericho3Asic.h"
#include "fboss/agent/hw/switch_asics/Jericho4Asic.h"
#include "fboss/agent/hw/switch_asics/MockAsic.h"
#include "fboss/agent/hw/switch_asics/P200Asic.h"
#include "fboss/agent/hw/switch_asics/Qumran4DAsic.h"
#include "fboss/agent/hw/switch_asics/Ramon3Asic.h"
#include "fboss/agent/hw/switch_asics/RamonAsic.h"
#include "fboss/agent/hw/switch_asics/Tomahawk3Asic.h"
#include "fboss/agent/hw/switch_asics/Tomahawk4Asic.h"
#include "fboss/agent/hw/switch_asics/Tomahawk5Asic.h"
#include "fboss/agent/hw/switch_asics/Tomahawk6Asic.h"
#include "fboss/agent/hw/switch_asics/TomahawkAsic.h"
#include "fboss/agent/hw/switch_asics/TomahawkUltra1Asic.h"
#include "fboss/agent/hw/switch_asics/YubaAsic.h"

DEFINE_int32(acl_gid, -1, "Content aware processor group ID for ACLs");
DEFINE_int32(teFlow_gid, -1, "Exact Match group ID for TeFlows");

namespace {
constexpr auto kDefaultACLGroupID = 128;
constexpr auto kDefaultTeFlowGroupID = 1;
constexpr auto kDefaultDropEgressID = 100000;

std::optional<int> leadingInt(folly::StringPiece s) noexcept {
  size_t n = 0;
  while (n < s.size() && std::isdigit(static_cast<unsigned char>(s[n]))) {
    ++n;
  }
  if (n == 0) {
    return std::nullopt;
  }
  const auto parsed = folly::tryTo<int>(s.subpiece(0, n));
  return parsed.hasValue() ? std::optional<int>(*parsed) : std::nullopt;
}

struct SaiSdkOrdering {
  int major;
  int minor;
  // An early access drop precedes its line's GA, so order EA below GA.
  bool generallyAvailable;

  auto key() const {
    return std::tie(major, minor, generallyAvailable);
  }
};

std::optional<SaiSdkOrdering> parseSaiSdk(folly::StringPiece version) noexcept {
  const auto dot = version.find('.');
  if (dot == folly::StringPiece::npos) {
    return std::nullopt;
  }
  const auto major = leadingInt(version.subpiece(0, dot));
  const auto minorField = version.subpiece(dot + 1);
  const auto minor = leadingInt(minorField);
  if (!major.has_value() || !minor.has_value()) {
    return std::nullopt;
  }
  return SaiSdkOrdering{
      *major, *minor, minorField.find("_ea") == folly::StringPiece::npos};
}

std::optional<SaiSdkOrdering> parseSaiSdk(
    const std::optional<facebook::fboss::cfg::SdkVersion>&
        sdkVersion) noexcept {
  if (!sdkVersion.has_value() || !sdkVersion->saiSdk().has_value()) {
    return std::nullopt;
  }
  return parseSaiSdk(folly::StringPiece{sdkVersion->saiSdk().value()});
}
} // namespace

namespace facebook::fboss {

HwAsic::HwAsic(
    std::optional<int64_t> switchId,
    const cfg::SwitchInfo& switchInfo,
    std::optional<cfg::SdkVersion> sdkVersion,
    std::unordered_set<cfg::SwitchType> supportedModes)
    : switchType_(*switchInfo.switchType()),
      switchId_(switchId),
      switchIndex_(*switchInfo.switchIndex()),
      systemPortRanges_(*switchInfo.systemPortRanges()),
      sdkVersion_(sdkVersion) {
  CHECK(switchInfo.switchMac().has_value());
  asicMac_ = folly::MacAddress(*switchInfo.switchMac());
  if (!supportedModes.contains(switchType_)) {
    throw std::runtime_error(
        folly::to<std::string>("Unsupported Mode: ", switchType_));
  }
  if (switchInfo.globalSystemPortOffset()) {
    globalSystemPortOffset_ = *switchInfo.globalSystemPortOffset();
  }
  if (switchInfo.localSystemPortOffset()) {
    localSystemPortOffset_ = *switchInfo.localSystemPortOffset();
  }
  if (switchInfo.inbandPortId()) {
    inbandPortId_ = *switchInfo.inbandPortId();
  }
}
/*
 * Default Content Aware Processor group ID for ACLs
 */
int HwAsic::getDefaultACLGroupID() const {
  if (FLAGS_acl_gid > 0) {
    return FLAGS_acl_gid;
  } else {
    return kDefaultACLGroupID;
  }
}

int HwAsic::getBufferDynThreshFromScalingFactor(
    cfg::MMUScalingFactor scalingFactor) const {
  switch (scalingFactor) {
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
    case cfg::MMUScalingFactor::ONE:
      return 0;
    case cfg::MMUScalingFactor::TWO:
      return 1;
    case cfg::MMUScalingFactor::FOUR:
      return 2;
    case cfg::MMUScalingFactor::EIGHT:
      return 3;
    case cfg::MMUScalingFactor::ONE_32768TH:
    case cfg::MMUScalingFactor::ONE_HUNDRED_TWENTY_EIGHT:
      throw FbossError(
          "Unsupported scaling factor: ",
          apache::thrift::util::enumNameSafe(scalingFactor));
  }
  throw FbossError(
      "Unknown scaling factor: ",
      apache::thrift::util::enumNameSafe(scalingFactor));
}

uint32_t HwAsic::getNumCellsAvailable(PlatformType platformType) const {
  throw FbossError(
      getAsicTypeStr(),
      " ASIC does not support num cells available for platform: ",
      apache::thrift::util::enumNameSafe(platformType));
}

uint32_t HwAsic::getSaiPhysicalLaneId(
    PlatformType platformType,
    cfg::PortType portType,
    uint32_t chipId,
    uint32_t logicalLane) const {
  throw FbossError(
      getAsicTypeStr(),
      " ASIC does not support SAI physical lane ID for platform: ",
      apache::thrift::util::enumNameSafe(platformType),
      " port type: ",
      apache::thrift::util::enumNameSafe(portType),
      " chip: ",
      chipId,
      " lane: ",
      logicalLane);
}

std::vector<HwAsic::InternalSystemPortConfig>
HwAsic::getInternalSystemPortConfig(
    const CpuPortCoreAndPortIndex& /*cpuPortsCoreAndPortIdx*/) const {
  return {};
}

std::unique_ptr<HwAsic> HwAsic::makeAsic(
    int64_t switchId,
    const cfg::SwitchInfo& switchInfo,
    std::optional<cfg::SdkVersion> sdkVersion,
    std::optional<HwAsic::FabricNodeRole> fabricNodeRole) {
  switch (*switchInfo.asicType()) {
    case cfg::AsicType::ASIC_TYPE_FAKE:
      return std::make_unique<FakeAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_FAKE_NO_WARMBOOT:
      return std::make_unique<FakeAsicNoWarmboot>(
          switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_MOCK:
      return std::make_unique<MockAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_TRIDENT2:
      throw FbossError("Unsupported ASIC type: ", *switchInfo.asicType());
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK:
      return std::make_unique<TomahawkAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
      return std::make_unique<Tomahawk3Asic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
      return std::make_unique<Tomahawk4Asic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
      return std::make_unique<Tomahawk5Asic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      return std::make_unique<Tomahawk6Asic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1:
      return std::make_unique<TomahawkUltra1Asic>(
          switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_ELBERT_8DD:
      return std::make_unique<CredoPhyAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_EBRO:
      return std::make_unique<EbroAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_P200:
      return std::make_unique<P200Asic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_YUBA:
      return std::make_unique<YubaAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_G202X:
      return std::make_unique<G202xAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_CHENAB:
      return std::make_unique<ChenabAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_CHENAB2:
      return std::make_unique<Chenab2Asic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_JERICHO2:
      return std::make_unique<Jericho2Asic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_JERICHO3:
      return std::make_unique<Jericho3Asic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_JERICHO4:
      return std::make_unique<Jericho4Asic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
      return std::make_unique<Qumran4DAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_RAMON:
      return std::make_unique<RamonAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_RAMON3:
      CHECK(fabricNodeRole.has_value());
      return std::make_unique<Ramon3Asic>(
          switchId, switchInfo, sdkVersion, fabricNodeRole.value());
    case cfg::AsicType::ASIC_TYPE_AGERA3:
      return std::make_unique<Agera3PhyAsic>(switchId, switchInfo, sdkVersion);
    case cfg::AsicType::ASIC_TYPE_GARONNE:
    case cfg::AsicType::ASIC_TYPE_SANDIA_PHY:
      throw FbossError("Unexcepted asic type: ", *switchInfo.asicType());
  };
  throw FbossError("Unexcepted asic type: ", *switchInfo.asicType());
}
/*
 * Default Content Aware Processor group ID for TeFlows
 */
int HwAsic::getDefaultTeFlowGroupID() const {
  if (FLAGS_teFlow_gid > 0) {
    return FLAGS_teFlow_gid;
  } else {
    return kDefaultTeFlowGroupID;
  }
}

/*
 * station entry id for vlan interface
 */
int HwAsic::getStationID(int intfID) const {
  return intfID;
}

int HwAsic::getDefaultDropEgressID() const {
  return kDefaultDropEgressID;
}

std::vector<cfg::AsicType> HwAsic::getAllHwAsicList() {
  std::vector<cfg::AsicType> result;
  for (auto asicType : apache::thrift::TEnumTraits<cfg::AsicType>::values) {
    result.push_back(asicType);
  }
  return result;
}

cfg::Range64 HwAsic::getReservedEncapIndexRange() const {
  throw FbossError(
      "Reserved encap idx range unimplemented for: ", getAsicTypeStr());
}

HwAsic::RecyclePortInfo HwAsic::getRecyclePortInfo(
    InterfaceNodeRole /* intfRole */) const {
  throw FbossError("Recycle port info unimplemented for: ", getAsicTypeStr());
}

uint32_t HwAsic::getMaxSwitchId() const {
  throw FbossError("Max switchId unimplemented for: ", getAsicTypeStr());
}

bool HwAsic::saiSdkAtLeast(folly::StringPiece minVersion) const {
  const auto configured = parseSaiSdk(getSdkVersion());
  const auto minimum = parseSaiSdk(minVersion);
  if (!configured.has_value() || !minimum.has_value()) {
    return false;
  }
  return configured->key() >= minimum->key();
}

bool HwAsic::AcceptedValues::accepts(uint32_t value) const {
  if (isRange()) {
    const auto& [minValue, maxValue] = asRange();
    return (value >= minValue) && (value <= maxValue);
  }
  const auto& values = asValues();
  return std::find(values.begin(), values.end(), value) != values.end();
}

std::string HwAsic::AcceptedValues::str() const {
  if (isRange()) {
    const auto& [minValue, maxValue] = asRange();
    return fmt::format("[{}, {}]", minValue, maxValue);
  }
  return fmt::format("{{{}}}", fmt::join(asValues(), ", "));
}

cfg::Range64 HwAsic::makeRange(int64_t min, int64_t max) {
  cfg::Range64 kRange;
  kRange.minimum() = min;
  kRange.maximum() = max;
  return kRange;
}
std::string HwAsic::getAsicTypeStr() const {
  return apache::thrift::util::enumNameSafe(getAsicType());
}

const std::map<cfg::PortType, cfg::PortLoopbackMode>&
HwAsic::desiredLoopbackModes() const {
  static const std::map<cfg::PortType, cfg::PortLoopbackMode> kLoopbackMode = {
      {cfg::PortType::INTERFACE_PORT, cfg::PortLoopbackMode::MAC}};
  return kLoopbackMode;
}

cfg::PortLoopbackMode HwAsic::getDesiredLoopbackMode(
    cfg::PortType portType) const {
  const auto loopbackModeMap = desiredLoopbackModes();
  auto itr = loopbackModeMap.find(portType);
  if (itr != loopbackModeMap.end()) {
    return itr->second;
  }
  throw FbossError("Unable to find the portType ", portType);
}

uint32_t HwAsic::getMaxPorts() const {
  return 2048;
}

uint32_t HwAsic::getVirtualDevices() const {
  return 1;
}

std::optional<uint32_t> HwAsic::computePortGroupSkew(
    const std::map<PortID, uint32_t>& portId2cableLen) const {
  throw FbossError(
      "Derived class must override getPortGroupSkew, where applicable");
}

HwAsic::FabricNodeRole HwAsic::getFabricNodeRole() const {
  throw FbossError(
      "Derived class must override getFabricNodeRole, where applicable");
}

const std::set<uint16_t>& HwAsic::getL1FabricPortsToConnectToL2() const {
  throw FbossError(
      "Derived class must override getL1FabricPortsToConnectToL2, where applicable");
}

uint64_t HwAsic::getCpuPortEgressPoolSize() const {
  throw FbossError("cpu port egress pool size is not available");
}

bool HwAsic::portMtuSupported(cfg::PortType portType) const {
  switch (portType) {
    case cfg::PortType::INTERFACE_PORT:
    case cfg::PortType::CPU_PORT:
    case cfg::PortType::RECYCLE_PORT:
    case cfg::PortType::MANAGEMENT_PORT:
    case cfg::PortType::EVENTOR_PORT:
    case cfg::PortType::HYPER_PORT:
      return true;
    case cfg::PortType::FABRIC_PORT:
    case cfg::PortType::HYPER_PORT_MEMBER:
      return false;
  };
  throw FbossError("Unexcepted port type: ", portType);
}
} // namespace facebook::fboss
