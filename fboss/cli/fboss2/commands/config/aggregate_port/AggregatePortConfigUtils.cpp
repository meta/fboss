// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/config/aggregate_port/AggregatePortConfigUtils.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace facebook::fboss::aggregate_port_utils {
namespace {

constexpr std::string_view kKey = "key";
constexpr std::string_view kDescription = "description";
constexpr std::string_view kType = "type";
constexpr std::string_view kMinLinkCount = "minimum-capacity-link-count";
constexpr std::string_view kMinToUpLinkCount =
    "minimum-capacity-to-up-link-count";

constexpr std::string_view kActivity = "activity";
constexpr std::string_view kRate = "rate";
constexpr std::string_view kPriority = "priority";
constexpr std::string_view kHoldTimerMultiplier = "hold-timer-multiplier";

const std::unordered_set<std::string_view> kAggregatePortAttributes = {
    kKey,
    kDescription,
    kType,
    kMinLinkCount,
    kMinToUpLinkCount,
};

const std::unordered_set<std::string_view> kMemberAttributes = {
    kActivity,
    kRate,
    kPriority,
    kHoldTimerMultiplier,
};

template <typename T>
T parseInteger(
    const std::string& attribute,
    const std::string& value,
    int64_t minimum,
    int64_t maximum) {
  auto parsed = folly::tryTo<int64_t>(value);
  if (parsed.hasError() || parsed.value() < minimum ||
      parsed.value() > maximum) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid value '{}' for {}. Expected an integer in [{}, {}]",
            value,
            attribute,
            minimum,
            maximum));
  }
  return static_cast<T>(parsed.value());
}

std::string lowerCopy(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
  return value;
}

cfg::MinimumCapacity linkCountCapacity(
    const std::string& attribute,
    const std::string& value) {
  cfg::MinimumCapacity capacity;
  capacity.linkCount() = parseInteger<int8_t>(
      attribute, value, 1, std::numeric_limits<int8_t>::max());
  return capacity;
}

size_t effectiveMinimum(
    const cfg::MinimumCapacity& capacity,
    size_t memberCount,
    const std::string& fieldName) {
  switch (capacity.getType()) {
    case cfg::MinimumCapacity::Type::linkCount: {
      auto count = capacity.get_linkCount();
      if (count <= 0) {
        throw std::invalid_argument(
            fmt::format("{} link count must be positive", fieldName));
      }
      return static_cast<size_t>(count);
    }
    case cfg::MinimumCapacity::Type::linkPercentage: {
      auto percentage = capacity.get_linkPercentage();
      if (!std::isfinite(percentage) || percentage <= 0 || percentage > 1) {
        throw std::invalid_argument(
            fmt::format("{} link percentage must be in (0, 1]", fieldName));
      }
      return static_cast<size_t>(std::ceil(percentage * memberCount));
    }
    case cfg::MinimumCapacity::Type::__EMPTY__:
      throw std::invalid_argument(
          fmt::format("{} must specify link count or percentage", fieldName));
  }
  throw std::invalid_argument(fmt::format("Invalid {}", fieldName));
}

void validateHysteresis(
    const cfg::AggregatePort& aggregatePort,
    size_t memberCount) {
  if (!aggregatePort.minimumCapacityToUp().has_value()) {
    return;
  }
  const auto& minimum = *aggregatePort.minimumCapacity();
  const auto& minimumToUp = *aggregatePort.minimumCapacityToUp();
  bool lower = false;
  if (minimum.getType() == cfg::MinimumCapacity::Type::linkCount &&
      minimumToUp.getType() == cfg::MinimumCapacity::Type::linkCount) {
    lower = minimumToUp.get_linkCount() < minimum.get_linkCount();
  } else if (
      minimum.getType() == cfg::MinimumCapacity::Type::linkPercentage &&
      minimumToUp.getType() == cfg::MinimumCapacity::Type::linkPercentage) {
    lower = minimumToUp.get_linkPercentage() < minimum.get_linkPercentage();
  } else if (memberCount != 0) {
    lower = effectiveMinimum(minimumToUp, memberCount, "minimumCapacityToUp") <
        effectiveMinimum(minimum, memberCount, "minimumCapacity");
  } else {
    throw std::invalid_argument(
        fmt::format(
            "Aggregate-port '{}' cannot compare mixed count and percentage thresholds without member ports",
            *aggregatePort.name()));
  }
  if (lower) {
    throw std::invalid_argument(
        fmt::format(
            "Aggregate-port '{}' minimumCapacityToUp must not be lower than minimumCapacity",
            *aggregatePort.name()));
  }
}

} // namespace

bool isAggregatePortAttribute(const std::string& token) {
  return kAggregatePortAttributes.count(token) != 0;
}

bool isMemberAttribute(const std::string& token) {
  return kMemberAttributes.count(token) != 0;
}

cfg::AggregatePort* findAggregatePort(
    std::vector<cfg::AggregatePort>& aggregatePorts,
    const std::string& name) {
  auto it = std::find_if(
      aggregatePorts.begin(), aggregatePorts.end(), [&](const auto& port) {
        return *port.name() == name;
      });
  return it == aggregatePorts.end() ? nullptr : &*it;
}

const cfg::AggregatePort* findAggregatePort(
    const std::vector<cfg::AggregatePort>& aggregatePorts,
    const std::string& name) {
  auto it = std::find_if(
      aggregatePorts.begin(), aggregatePorts.end(), [&](const auto& port) {
        return *port.name() == name;
      });
  return it == aggregatePorts.end() ? nullptr : &*it;
}

ResolvedPort resolvePort(
    const cfg::SwitchConfig& swConfig,
    const std::string& token) {
  for (const auto& port : *swConfig.ports()) {
    if (port.name().has_value() && *port.name() == token) {
      return {*port.logicalID(), *port.name()};
    }
  }

  auto parsed = folly::tryTo<int32_t>(token);
  if (parsed.hasValue()) {
    for (const auto& port : *swConfig.ports()) {
      if (*port.logicalID() == parsed.value()) {
        return {
            *port.logicalID(),
            port.name().has_value() ? *port.name()
                                    : fmt::format("port{}", *port.logicalID())};
      }
    }
  }

  throw std::invalid_argument(
      fmt::format("Port '{}' not found in configuration", token));
}

void applyAggregatePortAttributes(
    cfg::AggregatePort& aggregatePort,
    const std::map<std::string, std::string>& attributes) {
  for (const auto& [attribute, value] : attributes) {
    if (attribute == kKey) {
      aggregatePort.key() = parseInteger<int16_t>(
          attribute, value, 0, std::numeric_limits<int16_t>::max());
    } else if (attribute == kDescription) {
      aggregatePort.description() = value;
    } else if (attribute == kType) {
      auto lower = lowerCopy(value);
      if (lower == "lag") {
        aggregatePort.aggregatePortType() = cfg::AggregatePortType::LAG_PORT;
      } else if (lower == "hyper") {
        aggregatePort.aggregatePortType() = cfg::AggregatePortType::HYPER_PORT;
      } else {
        throw std::invalid_argument(
            fmt::format(
                "Invalid aggregate-port type '{}'. Use lag or hyper", value));
      }
    } else if (attribute == kMinLinkCount) {
      aggregatePort.minimumCapacity() = linkCountCapacity(attribute, value);
    } else if (attribute == kMinToUpLinkCount) {
      aggregatePort.minimumCapacityToUp() = linkCountCapacity(attribute, value);
    } else {
      throw std::invalid_argument(
          fmt::format("Unknown aggregate-port attribute '{}'", attribute));
    }
  }
}

void applyMemberAttributes(
    cfg::AggregatePortMember& member,
    const std::map<std::string, std::string>& attributes) {
  for (const auto& [attribute, value] : attributes) {
    if (attribute == kActivity) {
      auto lower = lowerCopy(value);
      if (lower == "active") {
        member.activity() = cfg::LacpPortActivity::ACTIVE;
      } else if (lower == "passive") {
        member.activity() = cfg::LacpPortActivity::PASSIVE;
      } else {
        throw std::invalid_argument(
            fmt::format(
                "Invalid LACP activity '{}'. Use active or passive", value));
      }
    } else if (attribute == kRate) {
      auto lower = lowerCopy(value);
      if (lower == "fast") {
        member.rate() = cfg::LacpPortRate::FAST;
      } else if (lower == "slow") {
        member.rate() = cfg::LacpPortRate::SLOW;
      } else {
        throw std::invalid_argument(
            fmt::format("Invalid LACP rate '{}'. Use fast or slow", value));
      }
    } else if (attribute == kPriority) {
      member.priority() = parseInteger<int32_t>(attribute, value, 0, 65535);
    } else if (attribute == kHoldTimerMultiplier) {
      member.holdTimerMultiplier() = parseInteger<int16_t>(
          attribute, value, 1, std::numeric_limits<int16_t>::max());
    } else {
      throw std::invalid_argument(
          fmt::format(
              "Unknown aggregate-port member attribute '{}'", attribute));
    }
  }
}

void validateAggregatePorts(
    const cfg::SwitchConfig& swConfig,
    const std::vector<cfg::AggregatePort>& aggregatePorts) {
  std::unordered_set<int32_t> configuredPortIds;
  for (const auto& port : *swConfig.ports()) {
    configuredPortIds.insert(*port.logicalID());
  }

  std::unordered_set<std::string> names;
  std::unordered_set<int16_t> keys;
  std::unordered_map<int32_t, std::string> memberOwners;

  for (const auto& aggregatePort : aggregatePorts) {
    const auto& name = *aggregatePort.name();
    if (name.empty() || !names.insert(name).second) {
      throw std::invalid_argument(
          fmt::format("Duplicate or empty aggregate-port name '{}'", name));
    }

    auto key = *aggregatePort.key();
    if (key < 0 || !keys.insert(key).second) {
      throw std::invalid_argument(
          fmt::format("Duplicate or invalid aggregate-port key {}", key));
    }
    for (const auto& member : *aggregatePort.memberPorts()) {
      auto portId = *member.memberPortID();
      if (!configuredPortIds.count(portId)) {
        throw std::invalid_argument(
            fmt::format(
                "Aggregate-port '{}' references unknown port ID {}",
                name,
                portId));
      }
      auto [it, inserted] = memberOwners.emplace(portId, name);
      if (!inserted) {
        throw std::invalid_argument(
            fmt::format(
                "Port ID {} is already a member of aggregate-port '{}'",
                portId,
                it->second));
      }
      if (*member.priority() < 0 || *member.priority() > 65535) {
        throw std::invalid_argument(
            fmt::format("Port ID {} has invalid LACP priority", portId));
      }
      if (*member.holdTimerMultiplier() <= 0) {
        throw std::invalid_argument(
            fmt::format(
                "Port ID {} has invalid hold timer multiplier", portId));
      }
    }

    auto memberCount = aggregatePort.memberPorts()->size();
    effectiveMinimum(
        *aggregatePort.minimumCapacity(), memberCount, "minimumCapacity");
    if (aggregatePort.minimumCapacityToUp().has_value()) {
      effectiveMinimum(
          *aggregatePort.minimumCapacityToUp(),
          memberCount,
          "minimumCapacityToUp");
    }
    validateHysteresis(aggregatePort, memberCount);
  }

  for (const auto& interface : *swConfig.interfaces()) {
    if (!interface.aggregatePortID().has_value()) {
      continue;
    }
    auto aggregatePort = std::find_if(
        aggregatePorts.begin(), aggregatePorts.end(), [&](const auto& port) {
          return *port.key() == *interface.aggregatePortID();
        });
    if (aggregatePort == aggregatePorts.end()) {
      throw std::invalid_argument(
          fmt::format(
              "Router interface {} references aggregate-port key {}, which does not exist",
              *interface.intfID(),
              *interface.aggregatePortID()));
    }
    if (aggregatePort->memberPorts()->empty()) {
      throw std::invalid_argument(
          fmt::format(
              "Router interface {} references aggregate-port '{}' with no member ports",
              *interface.intfID(),
              *aggregatePort->name()));
    }
  }
}

} // namespace facebook::fboss::aggregate_port_utils
