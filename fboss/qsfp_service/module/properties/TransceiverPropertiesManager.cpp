// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/qsfp_service/module/properties/TransceiverPropertiesManager.h"

#include <folly/FileUtil.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>

#include "fboss/agent/FbossError.h"
#include "fboss/qsfp_service/module/properties/TransceiverPropertiesDefault.h"

namespace facebook::fboss {

namespace {
int32_t getMediaInterfaceUnionValue(const MediaInterfaceUnion& u) {
  switch (u.getType()) {
    case MediaInterfaceUnion::Type::smfCode:
      return static_cast<int32_t>(*u.smfCode());
    case MediaInterfaceUnion::Type::activeCuCode:
      return static_cast<int32_t>(*u.activeCuCode());
    case MediaInterfaceUnion::Type::passiveCuCode:
      return static_cast<int32_t>(*u.passiveCuCode());
    case MediaInterfaceUnion::Type::extendedSpecificationComplianceCode:
      return static_cast<int32_t>(*u.extendedSpecificationComplianceCode());
    case MediaInterfaceUnion::Type::ethernet10GComplianceCode:
      return static_cast<int32_t>(*u.ethernet10GComplianceCode());
    case MediaInterfaceUnion::Type::__EMPTY__:
      return 0;
    default:
      throw FbossError("Unsupported MediaInterfaceUnion type");
  }
}
} // namespace

TransceiverPropertiesManager& TransceiverPropertiesManager::instance() {
  static TransceiverPropertiesManager inst;
  return inst;
}

void TransceiverPropertiesManager::initDefault() {
  initFromJson(kDefaultTransceiverPropertiesJson);
}

void TransceiverPropertiesManager::init(const std::string& configPath) {
  std::string configJson;
  if (!folly::readFile(configPath.c_str(), configJson)) {
    throw FbossError(
        "Failed to read transceiver properties config from ", configPath);
  }
  initFromJson(configJson);
  auto& self = instance();
  if (self.initialized_) {
    XLOG(INFO) << "Loaded transceiver properties config with "
               << self.config_.smfTransceivers()->size() << " SMF entries from "
               << configPath;
  }
}

void TransceiverPropertiesManager::initFromJson(const std::string& configJson) {
  auto& self = instance();
  try {
    folly::json::serialization_opts opts;
    opts.allow_json5_experimental = true;
    auto dynamic = folly::parseJson(configJson, opts);
    facebook::thrift::from_dynamic(
        self.config_,
        dynamic,
        facebook::thrift::dynamic_format::PORTABLE,
        facebook::thrift::format_adherence::LENIENT);
  } catch (const std::exception& ex) {
    throw FbossError(
        "Failed to parse transceiver properties config: ", ex.what());
  }

  // Validate required fields in each entry
  for (const auto& [codeValue, props] : *self.config_.smfTransceivers()) {
    const auto& adv = *props.firstApplicationAdvertisement();
    if (getMediaInterfaceUnionValue(*adv.mediaInterfaceCode()) == 0) {
      throw FbossError(
          "Missing or zero mediaInterfaceCode in "
          "firstApplicationAdvertisement for code ",
          codeValue);
    }
    if (adv.hostStartLanes()->empty()) {
      throw FbossError(
          "Missing or empty hostStartLanes in "
          "firstApplicationAdvertisement for code ",
          codeValue);
    }
    if (*adv.hostInterfaceCode() == ActiveCuHostInterfaceCode::UNKNOWN) {
      throw FbossError(
          "Missing or zero hostInterfaceCode in "
          "firstApplicationAdvertisement for code ",
          codeValue);
    }
  }

  // Build indexes from smfTransceivers
  self.codeToPropsMap_.clear();
  self.smfDerivationMap_.clear();
  self.mediaLaneCodeToMediaInterfaceCodeMap_.clear();
  self.mediaLaneCodeAndHostLanesToMediaInterfaceCodeMap_.clear();

  for (const auto& [codeValue, props] : *self.config_.smfTransceivers()) {
    self.codeToPropsMap_[codeValue] = &props;

    const auto& adv = *props.firstApplicationAdvertisement();
    auto smfByte = static_cast<uint8_t>(
        getMediaInterfaceUnionValue(*adv.mediaInterfaceCode()));
    SmfCandidate candidate{
        .codeValue = codeValue,
        .hostStartLanesCount = adv.hostStartLanes()->size(),
        .hostInterfaceCode = static_cast<uint8_t>(*adv.hostInterfaceCode()),
        .smfLength = props.smfLength().has_value()
            ? std::optional<int32_t>(*props.smfLength())
            : std::nullopt,
        .maxNumBanks = adv.maxNumBanks().value_or(1)};
    self.smfDerivationMap_[smfByte].push_back(candidate);
  }

  // Validate speedChangeTransitions reference valid speed combination keys
  for (const auto& [codeValue, props] : *self.config_.smfTransceivers()) {
    for (const auto& transition : *props.speedChangeTransitions()) {
      if (transition.size() != 2) {
        throw FbossError(
            "Invalid speedChangeTransition for code ",
            codeValue,
            ": expected 2 elements, got ",
            transition.size());
      }
      for (const auto& desc : transition) {
        bool found = false;
        for (const auto& combo : *props.supportedSpeedCombinations()) {
          if (*combo.combinationName() == desc) {
            found = true;
            break;
          }
        }
        if (!found) {
          throw FbossError(
              "speedChangeTransition references unknown speed "
              "combination '",
              desc,
              "' for code ",
              codeValue);
        }
      }
    }
  }

  // Build mediaLaneCode → MediaInterfaceCode reverse map
  for (const auto& [_, props] : *self.config_.smfTransceivers()) {
    for (const auto& combo : *props.supportedSpeedCombinations()) {
      for (const auto& port : *combo.ports()) {
        auto mediaLaneCode = getMediaInterfaceUnionValue(*port.mediaLaneCode());
        auto mediaInterfaceCode =
            static_cast<MediaInterfaceCode>(*port.mediaInterfaceCode());
        if (mediaInterfaceCode != MediaInterfaceCode::UNKNOWN &&
            mediaLaneCode != 0) {
          self.mediaLaneCodeToMediaInterfaceCodeMap_.try_emplace(
              mediaLaneCode, mediaInterfaceCode);
          self.mediaLaneCodeAndHostLanesToMediaInterfaceCodeMap_.try_emplace(
              std::make_pair(mediaLaneCode, *port.hostLanes()->count()),
              mediaInterfaceCode);
        }
      }
    }
  }

  self.initialized_ = true;
}

const SmfTransceiverProperties& TransceiverPropertiesManager::getProperties(
    MediaInterfaceCode code) {
  return getProperties(static_cast<int32_t>(code));
}

const SmfTransceiverProperties& TransceiverPropertiesManager::getProperties(
    int32_t codeValue) {
  auto& self = instance();
  if (!self.initialized_) {
    throw FbossError("TransceiverPropertiesManager not initialized");
  }
  auto it = self.codeToPropsMap_.find(codeValue);
  if (it == self.codeToPropsMap_.end()) {
    throw FbossError("Unknown MediaInterfaceCode: ", codeValue);
  }
  return *it->second;
}

std::vector<MediaInterfaceCode> TransceiverPropertiesManager::getKnownCodes() {
  auto& self = instance();
  if (!self.initialized_) {
    throw FbossError("TransceiverPropertiesManager not initialized");
  }
  std::vector<MediaInterfaceCode> result;
  for (const auto& [codeValue, _] : *self.config_.smfTransceivers()) {
    result.push_back(static_cast<MediaInterfaceCode>(codeValue));
  }
  return result;
}

bool TransceiverPropertiesManager::isKnown(MediaInterfaceCode code) {
  return isKnown(static_cast<int32_t>(code));
}

bool TransceiverPropertiesManager::isKnown(int32_t codeValue) {
  auto& self = instance();
  if (!self.initialized_) {
    return false;
  }
  return self.codeToPropsMap_.count(codeValue) > 0;
}

bool TransceiverPropertiesManager::isInitialized() {
  return instance().initialized_;
}

int TransceiverPropertiesManager::getNumHostLanes(MediaInterfaceCode code) {
  return *getProperties(code).numHostLanes();
}

int TransceiverPropertiesManager::getNumMediaLanes(MediaInterfaceCode code) {
  return *getProperties(code).numMediaLanes();
}

bool TransceiverPropertiesManager::getDoesNotSupportVdm(
    MediaInterfaceCode code) {
  const auto& props = getProperties(code);
  if (props.diagnosticCapabilitiesExceptions().has_value()) {
    return *props.diagnosticCapabilitiesExceptions()->doesNotSupportVdm();
  }
  return false;
}

bool TransceiverPropertiesManager::getDoesNotSupportRxOutputControl(
    MediaInterfaceCode code) {
  const auto& props = getProperties(code);
  if (props.diagnosticCapabilitiesExceptions().has_value()) {
    return *props.diagnosticCapabilitiesExceptions()
                ->doesNotSupportRxOutputControl();
  }
  return false;
}

std::vector<int> TransceiverPropertiesManager::getExpectedMediaLanes(
    MediaInterfaceCode code,
    const std::vector<int>& hostLanes,
    cfg::PortSpeed speed) {
  const auto& props = getProperties(code);
  if (hostLanes.empty()) {
    return {};
  }
  int startHostLane = *std::min_element(hostLanes.begin(), hostLanes.end());
  int numHostLanes = static_cast<int>(hostLanes.size());

  for (const auto& combo : *props.supportedSpeedCombinations()) {
    for (const auto& port : *combo.ports()) {
      if (*port.hostLanes()->start() == startHostLane &&
          *port.hostLanes()->count() == numHostLanes &&
          static_cast<cfg::PortSpeed>(*port.speed()) == speed) {
        std::vector<int> mediaLanes;
        int startMedia = *port.mediaLanes()->start();
        int numMedia = *port.mediaLanes()->count();
        mediaLanes.reserve(numMedia);
        for (int i = 0; i < numMedia; i++) {
          mediaLanes.push_back(startMedia + i);
        }
        return mediaLanes;
      }
    }
  }
  return {};
}

const SpeedCombination& TransceiverPropertiesManager::findSpeedCombination(
    MediaInterfaceCode code,
    const std::string& description) {
  const auto& props = getProperties(code);
  for (const auto& combo : *props.supportedSpeedCombinations()) {
    if (*combo.combinationName() == description) {
      return combo;
    }
  }
  throw FbossError(
      "SpeedCombination '",
      description,
      "' not found for MediaInterfaceCode ",
      static_cast<int32_t>(code));
}

MediaInterfaceCode TransceiverPropertiesManager::deriveSmfCode(
    uint8_t smfByte,
    const std::vector<int>& hostStartLanes,
    uint8_t hostInterfaceCode,
    int smfLength,
    int maxNumBanks) {
  auto& self = instance();
  if (!self.initialized_) {
    throw FbossError("TransceiverPropertiesManager not initialized");
  }
  auto it = self.smfDerivationMap_.find(smfByte);
  if (it == self.smfDerivationMap_.end()) {
    return MediaInterfaceCode::UNKNOWN;
  }

  auto numStartLanes = hostStartLanes.size();
  for (const auto& candidate : it->second) {
    if (candidate.hostStartLanesCount != numStartLanes) {
      continue;
    }
    if (candidate.hostInterfaceCode != hostInterfaceCode) {
      continue;
    }
    if (candidate.maxNumBanks != maxNumBanks) {
      continue;
    }
    if (!candidate.smfLength.has_value() || smfLength == *candidate.smfLength) {
      return static_cast<MediaInterfaceCode>(candidate.codeValue);
    }
  }
  return MediaInterfaceCode::UNKNOWN;
}

MediaInterfaceCode
TransceiverPropertiesManager::mediaLaneCodeToMediaInterfaceCode(
    uint8_t mediaLaneCodeByte,
    std::optional<int> numHostLanes) {
  auto& self = instance();
  if (!self.initialized_) {
    throw FbossError("TransceiverPropertiesManager not initialized");
  }
  if (numHostLanes) {
    auto widthIt = self.mediaLaneCodeAndHostLanesToMediaInterfaceCodeMap_.find(
        std::make_pair(static_cast<int32_t>(mediaLaneCodeByte), *numHostLanes));
    if (widthIt !=
        self.mediaLaneCodeAndHostLanesToMediaInterfaceCodeMap_.end()) {
      return widthIt->second;
    }
  }
  auto it = self.mediaLaneCodeToMediaInterfaceCodeMap_.find(
      static_cast<int32_t>(mediaLaneCodeByte));
  if (it == self.mediaLaneCodeToMediaInterfaceCodeMap_.end()) {
    throw FbossError(
        "Unknown media lane code byte: ", static_cast<int>(mediaLaneCodeByte));
  }
  return it->second;
}

void TransceiverPropertiesManager::reset() {
  auto& self = instance();
  self.config_ = TransceiverPropertiesConfig{};
  self.initialized_ = false;
  self.codeToPropsMap_.clear();
  self.smfDerivationMap_.clear();
  self.mediaLaneCodeToMediaInterfaceCodeMap_.clear();
  self.mediaLaneCodeAndHostLanesToMediaInterfaceCodeMap_.clear();
}

} // namespace facebook::fboss
