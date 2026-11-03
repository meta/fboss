// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/platforms/common/utils/BcmYamlConfig.h"

#include "fboss/agent/FbossError.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "folly/gen/String.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>

namespace {
constexpr auto kHSDKBcmDeviceKey = "bcm_device";
constexpr auto kHSDKDevice0Key = "0";
constexpr auto kHSDKBcmDeviceGlobalKey = "global";
constexpr auto kHSDKDeviceKey = "device";
constexpr auto kHSDKTMTHDConfigKey = "TM_THD_CONFIG";

constexpr auto kHSDKCoresMapKey = "PC_PM_CORE";
constexpr auto kRxLaneMapKey = "RX_LANE_MAP";
constexpr auto kTxLaneMapKey = "TX_LANE_MAP";
constexpr auto kRxPolaritySwapKey = "RX_POLARITY_FLIP";
constexpr auto kTxPolaritySwapKey = "TX_POLARITY_FLIP";

constexpr auto kHSDKL3ALPMState = "l3_alpm_template";
constexpr auto kHSDKIs128ByteIpv6Enabled = "ipv6_lpm_128b_enable";
constexpr auto kHSDKIsPriorityKeyUsedInRxCosqMapping =
    "rx_cosq_mapping_management_mode";
constexpr auto kHSDKThresholsModeKey = "THRESHOLD_MODE";

struct CoreKey {
  int pmId_, coreIndex_;

  CoreKey(int pmId = 0, int coreIndex = 0)
      : pmId_(pmId), coreIndex_(coreIndex) {}

  bool operator==(const CoreKey& rhs) const {
    return pmId_ == rhs.pmId_ && coreIndex_ == rhs.coreIndex_;
  }
};
} // namespace

namespace YAML {
template <>
struct convert<CoreKey> {
  static constexpr auto pmIDKey = "PC_PM_ID";
  static constexpr auto coreIndexKey = "CORE_INDEX";

  static Node encode(const CoreKey& rhs) {
    Node node;
    node[pmIDKey] = rhs.pmId_;
    node[coreIndexKey] = rhs.coreIndex_;
    return node;
  }

  static bool decode(const Node& node, CoreKey& rhs) {
    if (!node.IsMap() || node.size() != 2) {
      return false;
    }

    rhs.pmId_ = node[pmIDKey].as<int>();
    rhs.coreIndex_ = node[coreIndexKey].as<int>();
    return true;
  }
};
} // namespace YAML

namespace facebook::fboss {

using apache::thrift::can_throw;

void BcmYamlConfig::setBaseConfig(const std::string& baseConfig) {
  configStr_ = baseConfig;
  nodes_ = YAML::LoadAll(configStr_);
  for (auto yamlNode : nodes_) {
    if (auto bcmDeviceNode = yamlNode[kHSDKBcmDeviceKey]) {
      if (auto deviceNode = bcmDeviceNode[kHSDKDevice0Key]) {
        if (auto globalNode = deviceNode[kHSDKBcmDeviceGlobalKey]) {
          globalNode_ = globalNode;
        }
      }
    } else if (auto deviceNode = yamlNode[kHSDKDeviceKey]) {
      if (auto deviceSubNode = deviceNode[kHSDKDevice0Key]) {
        if (auto thresholdNode = deviceSubNode[kHSDKTMTHDConfigKey]) {
          thresholdNode_ = thresholdNode;
        }
        if (auto coresNode = deviceSubNode[kHSDKCoresMapKey]) {
          coreMapNode_ = coresNode;
        }
      }
    }
  }
}

void BcmYamlConfig::modifyCoreMaps(
    const std::map<phy::DataPlanePhyChip, std::vector<phy::PinConfig>>&
        pinMapping) {
  dirty_ = true;
  for (auto& [chip, pins] : pinMapping) {
    auto coreNum = folly::copy(chip.physicalID().value());
    int rxMap = 0, txMap = 0, rxPolaritySwap = 0, txPolaritySwap = 0;
    for (auto pin = pins.rbegin(); pin != pins.rend(); pin++) {
      if (!pin->laneMap().has_value() || !pin->polaritySwap().has_value()) {
        throw FbossError(
            "LaneMap and PolaritySwap information is required for dynamic bcm config");
      }
      // for lane map, each hex digit represents 1 lane, hence bit shift by 4
      rxMap = (rxMap << 4) + can_throw(*pin->laneMap()->rx());
      txMap = (txMap << 4) + can_throw(*pin->laneMap()->tx());
      // for pn swap, each bit represents 1 lane
      rxPolaritySwap =
          (rxPolaritySwap << 1) | can_throw(*pin->polaritySwap()->rx());
      txPolaritySwap =
          (txPolaritySwap << 1) | can_throw(*pin->polaritySwap()->tx());
    }
    auto node = coreMapNode_[CoreKey(coreNum + 1, 0)];
    node[kRxLaneMapKey] = fmt::format("0x{:08X}", rxMap);
    node[kTxLaneMapKey] = fmt::format("0x{:08X}", txMap);
    node[kRxPolaritySwapKey] = fmt::format("0x{:02X}", rxPolaritySwap);
    node[kTxPolaritySwapKey] = fmt::format("0x{:02X}", txPolaritySwap);
  }
}

void BcmYamlConfig::setGlobalProperty(
    const std::string& name,
    const std::string& value) {
  // A default-constructed YAML::Node is null but still "truthy", so test
  // IsMap() rather than the node itself to tell "global section present" from
  // "absent".
  if (globalNode_.IsMap()) {
    // Skip a no-op write so getConfig() does not force a needless YAML re-emit.
    if (auto existing = getConfigValue<std::string>(globalNode_, name);
        existing && *existing == value) {
      return;
    }
    dirty_ = true;
    globalNode_[name] = value;
    return;
  }
  // No bcm_device.0.global section exists yet. Capture a live reference to
  // bcm_device.0 (reusing an existing bcm_device document if there is one, so
  // we don't emit a second document with a duplicate bcm_device key) and vivify
  // the global section under it. This mirrors modifyCoreMaps(): mutating a live
  // sub-node reference is what reliably attaches to nodes_ and emits.
  YAML::Node dev0Node;
  bool found = false;
  for (auto yamlNode : nodes_) {
    if (auto bcmDeviceNode = yamlNode[kHSDKBcmDeviceKey]) {
      dev0Node = bcmDeviceNode[kHSDKDevice0Key];
      found = true;
      break;
    }
  }
  if (!found) {
    // Append a new document that already has bcm_device.0 as maps, so only the
    // (new) global section is vivified below.
    nodes_.push_back(YAML::Load("bcm_device:\n  0: {}\n"));
    dev0Node = nodes_.back()[kHSDKBcmDeviceKey][kHSDKDevice0Key];
  }
  dirty_ = true;
  globalNode_ = dev0Node[kHSDKBcmDeviceGlobalKey];
  globalNode_[name] = value;
}

std::optional<std::string> BcmYamlConfig::getMmuState() const {
  return getConfigValue<std::string>(thresholdNode_, kHSDKThresholsModeKey);
}

bool BcmYamlConfig::is128ByteIpv6Enabled() const {
  auto state = getConfigValue<int>(globalNode_, kHSDKIs128ByteIpv6Enabled);
  if (!state || *state != 1) {
    return false;
  }
  return true;
}

bool BcmYamlConfig::isAlpmEnabled() const {
  auto state = getConfigValue<int>(globalNode_, kHSDKL3ALPMState);
  if (!state || *state == 0) {
    return false;
  }
  // 1: combined mode, 2: parallel mode. But both are alpm enabled
  return true;
}

bool BcmYamlConfig::isPriorityKeyUsedInRxCosqMapping() const {
  auto state =
      getConfigValue<int>(globalNode_, kHSDKIsPriorityKeyUsedInRxCosqMapping);
  if (!state || *state == 0) {
    return false;
  }
  return true;
}

void BcmYamlConfig::reloadConfig() {
  dirty_ = false;
  YAML::Emitter out;
  for (auto& yamlNode : nodes_) {
    out << yamlNode;
  }
  configStr_ = out.c_str();
}

void BcmYamlConfig::dumpConfig(const std::string& dumpFile) {
  folly::writeFile(getConfig(), dumpFile.data());
}

std::string BcmYamlConfig::getConfig() {
  if (dirty_) {
    reloadConfig();
  }
  return configStr_;
}

std::string BcmYamlConfig::loadFromFile(const std::string& path) {
  std::string yamlCfg;
  if (!folly::readFile(path.c_str(), yamlCfg)) {
    throw FbossError("unable to read Broadcom Yaml config file ", path);
  }
  return yamlCfg;
}
} // namespace facebook::fboss
