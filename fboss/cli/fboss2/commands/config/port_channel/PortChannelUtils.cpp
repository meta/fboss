/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/port_channel/PortChannelUtils.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/CppAttributes.h>
#include <folly/String.h>

#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

namespace {

// Value bounds for the member attributes (table rows below).
constexpr int32_t kMinMemberPriority = 0;
constexpr int32_t kMaxMemberPriority = 65535; // LACP port priority is 16-bit
constexpr int32_t kMinHoldTimerMultiplier = 1;
constexpr int32_t kMaxHoldTimerMultiplier = 32767; // thrift i16

// The thrift default for AggregatePort.minimumCapacity is ALL_LINKS
// (linkPercentage = 1).
constexpr double kDefaultMinimumLinkPercentage = 1.0;

// Two-token member attribute names, derived from the single-token constants
// so the spellings cannot drift apart.
const std::string kMemberAttrLacpRate = fmt::format(
    "{} {}",
    kPortChannelMemberKeywordLacp,
    kPortChannelMemberAttrLacpRate);
const std::string kMemberAttrLacpMode = fmt::format(
    "{} {}",
    kPortChannelMemberKeywordLacp,
    kPortChannelMemberAttrLacpMode);
const std::string kMemberAttrLacpHoldTimer = fmt::format(
    "{} {}",
    kPortChannelMemberKeywordLacp,
    kPortChannelMemberAttrLacpHoldTimer);

// ---- per-value-shape factories ---------------------------------------------
// Each factory owns one value SHAPE (a free-form string, a bounded int, a
// fixed token set mapped to an enum): it produces the usage text, the
// validation, the set and the reset for that shape, so a table row is one
// line -- name, bounds/tokens, getter, setter, default. If an attribute
// seems to need a hand-written row, its value shape has no factory yet (see
// minimumLinksAttr below for the union-valued exception).

// A free-form string value.
template <typename T>
PortChannelAttrOps<T> stringAttr(
    std::string_view name,
    const std::function<std::string(const T&)>& get,
    const std::function<void(T&, const std::string&)>& set,
    std::string defaultValue) {
  PortChannelAttrOps<T> ops;
  ops.usage = fmt::format("{} <string>", name);
  ops.validate = [](const std::string&) {};
  ops.set = [name, get, set](
                T& target, const std::string& value) -> PortChannelAttrResult {
    bool changed = get(target) != value;
    set(target, value);
    return {changed, fmt::format("{}='{}'", name, value)};
  };
  ops.reset = [name, get, set, defaultValue = std::move(defaultValue)](
                  T& target) -> PortChannelAttrResult {
    bool changed = get(target) != defaultValue;
    set(target, defaultValue);
    return {changed, std::string(name)};
  };
  return ops;
}

// A bounded integer value in [min, max].
template <typename T>
PortChannelAttrOps<T> boundedIntAttr(
    std::string_view name,
    int32_t min,
    int32_t max,
    const std::function<int32_t(const T&)>& get,
    const std::function<void(T&, int32_t)>& set,
    int32_t defaultValue) {
  PortChannelAttrOps<T> ops;
  ops.usage = fmt::format("{} <{}-{}>", name, min, max);
  ops.validate = [name, min, max](const std::string& value) {
    parseIntInRange(value, name, min, max);
  };
  ops.set = [name, min, max, get, set](
                T& target, const std::string& value) -> PortChannelAttrResult {
    int32_t parsed = parseIntInRange(value, name, min, max);
    bool changed = get(target) != parsed;
    set(target, parsed);
    return {changed, fmt::format("{}={}", name, parsed)};
  };
  ops.reset =
      [name, get, set, defaultValue](T& target) -> PortChannelAttrResult {
    bool changed = get(target) != defaultValue;
    set(target, defaultValue);
    return {changed, std::string(name)};
  };
  return ops;
}

// A value drawn from a fixed set of (lowercase) tokens mapped to an enum.
template <typename T, typename Enum>
PortChannelAttrOps<T> enumTokenAttr(
    std::string_view name,
    std::initializer_list<std::pair<std::string_view, Enum>> tokens,
    const std::function<Enum(const T&)>& get,
    const std::function<void(T&, Enum)>& set,
    Enum defaultValue) {
  std::vector<std::pair<std::string, Enum>> tokenMap;
  std::vector<std::string> tokenNames;
  for (const auto& [token, value] : tokens) {
    tokenMap.emplace_back(std::string(token), value);
    tokenNames.emplace_back(token);
  }
  auto parse = [name, tokenMap, tokenNames](const std::string& value) -> Enum {
    const std::string lower = toLowerCopy(value);
    for (const auto& [token, mapped] : tokenMap) {
      if (lower == token) {
        return mapped;
      }
    }
    throw std::invalid_argument(
        fmt::format(
            "Invalid {} value '{}': expected {}",
            name,
            value,
            folly::join(" or ", tokenNames)));
  };
  PortChannelAttrOps<T> ops;
  ops.usage = fmt::format("{} <{}>", name, folly::join("|", tokenNames));
  ops.validate = [parse](const std::string& value) { parse(value); };
  ops.set = [name, parse, get, set](
                T& target, const std::string& value) -> PortChannelAttrResult {
    Enum parsed = parse(value);
    bool changed = get(target) != parsed;
    set(target, parsed);
    return {changed, fmt::format("{}={}", name, toLowerCopy(value))};
  };
  ops.reset =
      [name, get, set, defaultValue](T& target) -> PortChannelAttrResult {
    bool changed = get(target) != defaultValue;
    set(target, defaultValue);
    return {changed, std::string(name)};
  };
  return ops;
}

// minimum-links is the one attribute no factory covers: it writes a thrift
// union (MinimumCapacity), whose set arm is linkCount but whose default is
// the OTHER arm (linkPercentage = 1, ALL_LINKS), so set and reset do not
// share a getter/setter pair.
PortChannelAttrOps<cfg::AggregatePort> minimumLinksAttr() {
  PortChannelAttrOps<cfg::AggregatePort> ops;
  ops.usage = fmt::format(
      "{} <{}-{}>",
      kPortChannelAttrMinimumLinks,
      kPortChannelMinLinkCount,
      kPortChannelMaxLinkCount);
  ops.validate = [](const std::string& value) {
    parseIntInRange(
        value,
        kPortChannelAttrMinimumLinks,
        kPortChannelMinLinkCount,
        kPortChannelMaxLinkCount);
  };
  ops.set = [](cfg::AggregatePort& portChannel,
               const std::string& value) -> PortChannelAttrResult {
    int32_t count = parseIntInRange(
        value,
        kPortChannelAttrMinimumLinks,
        kPortChannelMinLinkCount,
        kPortChannelMaxLinkCount);
    auto& minimumCapacity = *portChannel.minimumCapacity();
    bool changed =
        minimumCapacity.getType() != cfg::MinimumCapacity::Type::linkCount ||
        minimumCapacity.get_linkCount() != count;
    if (changed) {
      minimumCapacity.set_linkCount(static_cast<int8_t>(count));
    }
    return {changed, fmt::format("{}={}", kPortChannelAttrMinimumLinks, count)};
  };
  ops.reset = [](cfg::AggregatePort& portChannel) -> PortChannelAttrResult {
    auto& minimumCapacity = *portChannel.minimumCapacity();
    bool changed = minimumCapacity.getType() !=
            cfg::MinimumCapacity::Type::linkPercentage ||
        minimumCapacity.get_linkPercentage() != kDefaultMinimumLinkPercentage;
    if (changed) {
      minimumCapacity.set_linkPercentage(kDefaultMinimumLinkPercentage);
    }
    return {changed, std::string(kPortChannelAttrMinimumLinks)};
  };
  return ops;
}

} // namespace

// ---- the attribute tables ----------------------------------------------
// One line per attribute: table key, value shape, getter, setter, default.
// There are no handler bodies here by design -- if an attribute appears to
// need one, its value shape is missing a factory.

const PortChannelAttrTable& portChannelAttrTable() {
  static const PortChannelAttrTable kTable = {
      {std::string(kPortChannelAttrDescription),
       stringAttr<cfg::AggregatePort>(
           kPortChannelAttrDescription,
           [](const cfg::AggregatePort& p) { return *p.description(); },
           [](cfg::AggregatePort& p, const std::string& v) {
             p.description() = v;
           },
           "")},
      {std::string(kPortChannelAttrMinimumLinks), minimumLinksAttr()},
  };
  return kTable;
}

const PortChannelMemberAttrTable& portChannelMemberAttrTable() {
  static const PortChannelMemberAttrTable kTable = {
      {std::string(kPortChannelMemberAttrPriority),
       boundedIntAttr<cfg::AggregatePortMember>(
           kPortChannelMemberAttrPriority,
           kMinMemberPriority,
           kMaxMemberPriority,
           [](const cfg::AggregatePortMember& m) { return *m.priority(); },
           [](cfg::AggregatePortMember& m, int32_t v) { m.priority() = v; },
           kPortChannelDefaultMemberPriority)},
      {kMemberAttrLacpRate,
       enumTokenAttr<cfg::AggregatePortMember, cfg::LacpPortRate>(
           kMemberAttrLacpRate,
           {{kLacpRateSlow, cfg::LacpPortRate::SLOW},
            {kLacpRateFast, cfg::LacpPortRate::FAST}},
           [](const cfg::AggregatePortMember& m) { return *m.rate(); },
           [](cfg::AggregatePortMember& m, cfg::LacpPortRate v) {
             m.rate() = v;
           },
           cfg::LacpPortRate::FAST)},
      {kMemberAttrLacpMode,
       enumTokenAttr<cfg::AggregatePortMember, cfg::LacpPortActivity>(
           kMemberAttrLacpMode,
           {{kLacpModeActive, cfg::LacpPortActivity::ACTIVE},
            {kLacpModePassive, cfg::LacpPortActivity::PASSIVE}},
           [](const cfg::AggregatePortMember& m) { return *m.activity(); },
           [](cfg::AggregatePortMember& m, cfg::LacpPortActivity v) {
             m.activity() = v;
           },
           cfg::LacpPortActivity::ACTIVE)},
      {kMemberAttrLacpHoldTimer,
       boundedIntAttr<cfg::AggregatePortMember>(
           kMemberAttrLacpHoldTimer,
           kMinHoldTimerMultiplier,
           kMaxHoldTimerMultiplier,
           [](const cfg::AggregatePortMember& m) {
             return static_cast<int32_t>(*m.holdTimerMultiplier());
           },
           [](cfg::AggregatePortMember& m, int32_t v) {
             m.holdTimerMultiplier() = static_cast<int16_t>(v);
           },
           cfg::switch_config_constants::DEFAULT_LACP_HOLD_TIMER_MULTIPLIER())},
  };
  return kTable;
}

std::unordered_set<std::string> portChannelAttrNameSet() {
  std::unordered_set<std::string> names;
  for (const auto& [name, ops] : portChannelAttrTable()) {
    names.insert(name);
  }
  return names;
}

std::string validPortChannelAttrs() {
  std::vector<std::string> names;
  for (const auto& [name, ops] : portChannelAttrTable()) {
    names.push_back(name);
  }
  return folly::join(", ", names);
}

std::optional<std::pair<std::string, size_t>> matchPortChannelMemberAttr(
    const std::vector<std::string>& tokens,
    size_t start) {
  const auto& table = portChannelMemberAttrTable();
  // Attributes may span two tokens; match the longest prefix first.
  if (start + 1 < tokens.size()) {
    std::string twoToken = fmt::format(
        "{} {}", toLowerCopy(tokens[start]), toLowerCopy(tokens[start + 1]));
    if (table.find(twoToken) != table.end()) {
      return std::make_pair(std::move(twoToken), size_t{2});
    }
  }
  if (start < tokens.size()) {
    std::string oneToken = toLowerCopy(tokens[start]);
    if (table.find(oneToken) != table.end()) {
      return std::make_pair(std::move(oneToken), size_t{1});
    }
  }
  return std::nullopt;
}

bool isPortChannelMemberAttrStart(const std::string& token) {
  const std::string lower = toLowerCopy(token);
  for (const auto& [name, ops] : portChannelMemberAttrTable()) {
    if (name.substr(0, name.find(' ')) == lower) {
      return true;
    }
  }
  return false;
}

std::string portChannelAttrSetGrammar() {
  std::vector<std::string> usages;
  for (const auto& [name, ops] : portChannelAttrTable()) {
    usages.push_back(ops.usage);
  }
  return folly::join(" | ", usages);
}

std::string portChannelAttrDeleteGrammar() {
  std::vector<std::string> names;
  for (const auto& [name, ops] : portChannelAttrTable()) {
    names.push_back(name);
  }
  return folly::join("|", names);
}

std::string portChannelMemberSetGrammar() {
  std::string grammar = fmt::format(
      "{} <interface> [...] | {} <interface> [...]",
      kPortChannelMemberOpAdd,
      kPortChannelMemberOpRemove);
  for (const auto& [name, ops] : portChannelMemberAttrTable()) {
    grammar += fmt::format(" | <interface> {}", ops.usage);
  }
  return grammar;
}

std::string portChannelMemberDeleteGrammar() {
  std::string grammar = "<interface> [...]";
  for (const auto& [name, ops] : portChannelMemberAttrTable()) {
    grammar += fmt::format(" | <interface> {}", name);
  }
  return grammar;
}

std::string portChannelMemberSetUsage() {
  return "Expected: " + portChannelMemberSetGrammar();
}

std::string portChannelMemberDeleteUsage() {
  return "Expected: " + portChannelMemberDeleteGrammar();
}

std::string toLowerCopy(const std::string& s) {
  std::string lower = s;
  folly::toLowerAscii(lower);
  return lower;
}

int32_t parseIntInRange(
    const std::string& value,
    std::string_view what,
    int32_t min,
    int32_t max) {
  int32_t parsed = 0;
  try {
    parsed = folly::to<int32_t>(value);
  } catch (const folly::ConversionError&) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid {} value '{}': expected an integer between {} and {}",
            what,
            value,
            min,
            max));
  }
  if (parsed < min || parsed > max) {
    throw std::invalid_argument(
        fmt::format(
            "{} value {} is out of range [{}, {}]", what, parsed, min, max));
  }
  return parsed;
}

int16_t parsePortChannelKey(const std::string& name) {
  if (name.size() > kPortChannelMaxNameLen) {
    throw std::invalid_argument(
        fmt::format(
            "Port-channel name '{}' is longer than {} characters",
            name,
            kPortChannelMaxNameLen));
  }

  size_t digitStart = name.size();
  while (digitStart > 0 &&
         std::isdigit(static_cast<unsigned char>(name[digitStart - 1]))) {
    --digitStart;
  }
  if (digitStart == name.size() || digitStart == 0) {
    throw std::invalid_argument(
        fmt::format(
            "Port-channel name '{}' must end in a numeric ID between {} and "
            "{} (e.g. port-channel100)",
            name,
            kPortChannelMinKey,
            kPortChannelMaxKey));
  }

  int32_t key = 0;
  try {
    key = folly::to<int32_t>(name.substr(digitStart));
  } catch (const folly::ConversionError&) {
    throw std::invalid_argument(
        fmt::format("Port-channel name '{}' has an invalid numeric ID", name));
  }
  if (key < kPortChannelMinKey || key > kPortChannelMaxKey) {
    throw std::invalid_argument(
        fmt::format(
            "Port-channel ID {} is out of range [{}, {}]",
            key,
            kPortChannelMinKey,
            kPortChannelMaxKey));
  }
  return static_cast<int16_t>(key);
}

cfg::AggregatePort* FOLLY_NULLABLE
findPortChannel(cfg::SwitchConfig& swConfig, const std::string& name) {
  for (auto& aggPort : *swConfig.aggregatePorts()) {
    if (*aggPort.name() == name) {
      return &aggPort;
    }
  }
  return nullptr;
}

cfg::AggregatePortMember* FOLLY_NULLABLE
findPortChannelMember(cfg::AggregatePort& portChannel, int32_t portId) {
  for (auto& member : *portChannel.memberPorts()) {
    if (*member.memberPortID() == portId) {
      return &member;
    }
  }
  return nullptr;
}

cfg::Port* resolveMemberPort(const std::string& interfaceName) {
  // Throws when the name is not found in the configuration.
  utils::InterfaceList interfaces({interfaceName});
  cfg::Port* port = interfaces[0].getPort();
  if (!port) {
    throw std::invalid_argument(
        fmt::format(
            "Interface '{}' is not a physical port and cannot be a "
            "port-channel member",
            interfaceName));
  }
  return port;
}

std::optional<int32_t> portChannelMemberVlan(
    const cfg::SwitchConfig& swConfig,
    const cfg::AggregatePort& portChannel) {
  for (const auto& member : *portChannel.memberPorts()) {
    for (const auto& port : *swConfig.ports()) {
      if (*port.logicalID() == *member.memberPortID()) {
        return *port.ingressVlan();
      }
    }
  }
  return std::nullopt;
}

std::vector<int32_t> interfacesBoundToPortChannel(
    const cfg::SwitchConfig& swConfig,
    int16_t key) {
  std::vector<int32_t> intfIds;
  for (const auto& intf : *swConfig.interfaces()) {
    if (intf.aggregatePortID().has_value() && *intf.aggregatePortID() == key) {
      intfIds.push_back(*intf.intfID());
    }
  }
  return intfIds;
}

std::optional<std::string> portChannelForMember(
    const cfg::SwitchConfig& swConfig,
    int32_t portId) {
  for (const auto& aggPort : *swConfig.aggregatePorts()) {
    for (const auto& member : *aggPort.memberPorts()) {
      if (*member.memberPortID() == portId) {
        return *aggPort.name();
      }
    }
  }
  return std::nullopt;
}

std::vector<std::string> removePortChannelMembers(
    cfg::AggregatePort& portChannel,
    const std::vector<std::string>& memberNames) {
  // Deduplicate before the emptiness check below: repeated names must not
  // inflate the removal count past the real number of distinct members.
  std::vector<int32_t> portIds;
  std::vector<std::string> removed;
  for (const auto& name : memberNames) {
    int32_t portId = *resolveMemberPort(name)->logicalID();
    if (!findPortChannelMember(portChannel, portId)) {
      throw std::invalid_argument(
          fmt::format(
              "Interface '{}' is not a member of port-channel '{}'",
              name,
              *portChannel.name()));
    }
    if (std::find(portIds.begin(), portIds.end(), portId) == portIds.end()) {
      portIds.push_back(portId);
      removed.push_back(name);
    }
  }

  auto& members = *portChannel.memberPorts();
  if (portIds.size() >= members.size()) {
    // An aggregate port with no members crashes the agent when applied
    // (SaiLagManager::addLag requires at least one subport).
    throw std::invalid_argument(
        fmt::format(
            "Removing all members would leave port-channel '{}' empty; "
            "delete it instead with: delete port-channel {}",
            *portChannel.name(),
            *portChannel.name()));
  }

  members.erase(
      std::remove_if(
          members.begin(),
          members.end(),
          [&portIds](const cfg::AggregatePortMember& member) {
            return std::find(
                       portIds.begin(),
                       portIds.end(),
                       *member.memberPortID()) != portIds.end();
          }),
      members.end());

  return removed;
}

} // namespace facebook::fboss
