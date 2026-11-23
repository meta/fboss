/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/QueueConfigUtils.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <re2/re2.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace facebook::fboss::utils {

namespace {

// The one attribute that takes more than a single value token.
constexpr std::string_view kAttrRateLimit = "rate-limit";
constexpr std::string_view kRateUnitKbps = "kbps";
constexpr std::string_view kRateUnitPps = "pps";
// Consumes every remaining token into the AQM sub-grammar.
constexpr std::string_view kAttrAqm = "active-queue-management";
constexpr std::string_view kAttrAqmShort = "aqm";

// Uppercase and turn dashes into underscores so users can type
// "strict-priority" for the thrift enum STRICT_PRIORITY.
std::string toUpper(const std::string& value) {
  std::string result = value;
  std::transform(
      result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return c == '-' ? '_' : std::toupper(c);
      });
  return result;
}

template <typename EnumT>
std::string getValidEnumValues() {
  std::vector<std::string> names;
  for (auto value : apache::thrift::TEnumTraits<EnumT>::values) {
    names.push_back(apache::thrift::util::enumNameSafe(value));
  }
  return folly::join(", ", names);
}

template <typename EnumT>
EnumT parseThriftEnum(const std::string& attr, const std::string& value) {
  EnumT result{};
  if (!apache::thrift::TEnumTraits<EnumT>::findValue(toUpper(value), &result)) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid {}: '{}'. Valid values are: {}",
            attr,
            value,
            getValidEnumValues<EnumT>()));
  }
  return result;
}

std::string getValidSchedulingTypes() {
  return getValidEnumValues<cfg::QueueScheduling>() +
      " (or short names: WRR, SP, DRR)";
}

int32_t parseNonNegativeInt(const std::string& attr, const std::string& value) {
  auto parsed = folly::tryTo<int32_t>(value);
  if (!parsed.hasValue()) {
    throw std::invalid_argument(
        fmt::format("{} must be an integer, got: {}", attr, value));
  }
  if (parsed.value() < 0) {
    throw std::invalid_argument(
        fmt::format("{} must be non-negative, got: {}", attr, value));
  }
  return parsed.value();
}

// How many value tokens the attribute at v[i] consumes. rate-limit's <min> is
// optional -- `rate-limit <unit> <max>` is the form `config copp queue`
// shipped and still accepts -- so the choice between two and three is made by
// lookahead. No attribute name is a bare integer, which is what makes
// "the token after <max> parses as an integer" an unambiguous test for the
// three-token form.
size_t attrValueCount(const std::vector<std::string>& v, size_t i) {
  const auto& attr = v[i];
  if (attr == kAttrRateLimit) {
    return (i + 3 < v.size() && folly::tryTo<int32_t>(v[i + 3]).hasValue()) ? 3
                                                                            : 2;
  }
  return 1;
}

// Message for an attribute whose value tokens ran out, naming the shape
// rate-limit expects rather than the generic "requires a value".
std::string attrArityError(const std::string& attr) {
  if (attr == kAttrRateLimit) {
    return fmt::format(
        "'{}' requires <{}|{}> [<min>] <max>",
        kAttrRateLimit,
        kRateUnitKbps,
        kRateUnitPps);
  }
  return fmt::format("Attribute '{}' requires a value.", attr);
}

// Parses a <min> <max> pair, rejecting an inverted range: nothing downstream
// re-checks it, so the agent would hand it to the ASIC as given.
cfg::Range parseRange(
    const std::string& attr,
    const std::string& minToken,
    const std::string& maxToken) {
  cfg::Range range;
  range.minimum() = parseNonNegativeInt(attr, minToken);
  range.maximum() = parseNonNegativeInt(attr, maxToken);
  if (*range.minimum() > *range.maximum()) {
    throw std::invalid_argument(
        fmt::format(
            "{} minimum ({}) must not exceed maximum ({})",
            attr,
            *range.minimum(),
            *range.maximum()));
  }
  return range;
}

// rate-limit <kbps|pps> [<min>] <max> -> PortQueue.portQueueRate. The thrift
// field is a union, so setting one unit clears a rate previously configured in
// the other rather than leaving two conflicting rates behind. An omitted <min>
// means 0.
void applyRateLimit(
    cfg::PortQueue& queue,
    const std::vector<std::string>& values) {
  const auto& unit = values[0];
  if (unit != kRateUnitKbps && unit != kRateUnitPps) {
    throw std::invalid_argument(
        fmt::format(
            "rate-limit unit must be '{}' or '{}', got '{}'",
            kRateUnitKbps,
            kRateUnitPps,
            unit));
  }
  const auto attrLabel = fmt::format("{} {}", kAttrRateLimit, unit);
  auto range = values.size() == 3 ? parseRange(attrLabel, values[1], values[2])
                                  : parseRange(attrLabel, "0", values[1]);
  cfg::PortQueueRate rate;
  if (unit == kRateUnitKbps) {
    rate.kbitsPerSec() = range;
  } else {
    rate.pktsPerSec() = range;
  }
  queue.portQueueRate() = rate;
}

using LinearSetter = void (*)(cfg::LinearQueueCongestionDetection&, int32_t);

const std::map<std::string, LinearSetter>& linearSetters() {
  static const std::map<std::string, LinearSetter> kSetters = {
      {"minimum-length",
       [](cfg::LinearQueueCongestionDetection& l, int32_t v) {
         l.minimumLength() = v;
       }},
      {"maximum-length",
       [](cfg::LinearQueueCongestionDetection& l, int32_t v) {
         l.maximumLength() = v;
       }},
      {"probability",
       [](cfg::LinearQueueCongestionDetection& l, int32_t v) {
         l.probability() = v;
       }},
  };
  return kSetters;
}

std::string getValidLinearAttrs() {
  std::vector<std::string> keys;
  for (const auto& [key, setter] : linearSetters()) {
    keys.push_back(key);
  }
  return folly::join(", ", keys);
}

std::optional<cfg::QueueScheduling> parseScheduling(const std::string& value) {
  std::string upperValue = toUpper(value);

  static const std::map<std::string, cfg::QueueScheduling> shortNames = {
      {"WRR", cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN},
      {"SP", cfg::QueueScheduling::STRICT_PRIORITY},
      {"DRR", cfg::QueueScheduling::DEFICIT_ROUND_ROBIN},
  };

  auto it = shortNames.find(upperValue);
  if (it != shortNames.end()) {
    return it->second;
  }

  cfg::QueueScheduling scheduling{};
  if (apache::thrift::TEnumTraits<cfg::QueueScheduling>::findValue(
          upperValue, &scheduling)) {
    return scheduling;
  }
  return std::nullopt;
}

// The AQM argument stream is a flat list of <attribute> <value> pairs; the
// detection/linear nesting only exists in the thrift output, not the input.
// congestion-behavior is applied by the caller (see findCongestionBehavior),
// so here it is accepted and skipped; detection asserts the (only supported)
// linear type, and the rest set fields on the linear detection struct.
void parseAqmAttributes(
    const std::vector<std::string>& aqmArgs,
    cfg::ActiveQueueManagement& aqm) {
  if (aqmArgs.empty()) {
    throw std::invalid_argument(
        "active-queue-management requires sub-attributes: "
        "congestion-behavior <value> or detection linear <attr> <value> ...");
  }

  cfg::LinearQueueCongestionDetection linear;
  if (aqm.detection()->linear().has_value()) {
    linear = *aqm.detection()->linear();
  }
  bool sawDetectionArgs = false;

  for (size_t i = 0; i < aqmArgs.size(); i += 2) {
    const auto& attr = aqmArgs[i];
    if (i + 1 >= aqmArgs.size()) {
      throw std::invalid_argument(fmt::format("'{}' requires a value", attr));
    }
    const auto& value = aqmArgs[i + 1];

    // congestion-behavior is parsed and applied by the caller
    // (findCongestionBehavior + selectOrCreateAqm); skip it here.
    if (attr == "congestion-behavior") {
      continue;
    }

    if (attr == "detection") {
      if (toUpper(value) != "LINEAR") {
        throw std::invalid_argument(
            fmt::format(
                "Invalid detection type: '{}'. Currently supported: linear",
                value));
      }
      sawDetectionArgs = true;
    } else if (auto it = linearSetters().find(attr);
               it != linearSetters().end()) {
      it->second(linear, parseNonNegativeInt(attr, value));
      sawDetectionArgs = true;
    } else {
      throw std::invalid_argument(
          fmt::format(
              "Unknown active-queue-management attribute: '{}'. Valid "
              "attributes are: congestion-behavior, detection, {}",
              attr,
              getValidLinearAttrs()));
    }
  }

  // Only touch detection when the user actually supplied detection/linear
  // args; otherwise a bare `congestion-behavior <x>` would clobber an existing
  // detection with an empty linear struct.
  if (sawDetectionArgs) {
    aqm.detection() = cfg::QueueCongestionDetection();
    aqm.detection()->linear() = linear;
  }
}

// Parses the congestion-behavior named in the AQM arg stream (the key the
// caller uses to target the matching AQM list entry). Returns nullopt when the
// user did not name one. This is the single place the behavior is parsed, so it
// also rejects a second, possibly conflicting, congestion-behavior token.
std::optional<cfg::QueueCongestionBehavior> findCongestionBehavior(
    const std::vector<std::string>& aqmArgs) {
  std::optional<cfg::QueueCongestionBehavior> behavior;
  for (size_t i = 0; i + 1 < aqmArgs.size(); i += 2) {
    if (aqmArgs[i] == "congestion-behavior") {
      if (behavior.has_value()) {
        throw std::invalid_argument(
            "congestion-behavior may only be specified once");
      }
      behavior = parseThriftEnum<cfg::QueueCongestionBehavior>(
          aqmArgs[i], aqmArgs[i + 1]);
    }
  }
  return behavior;
}

// Picks the AQM list entry a config edit should modify, keyed by behavior. A
// port queue may carry two AQM entries (one ECN, one EARLY_DROP); selecting by
// behavior lets them coexist instead of clobbering aqms[0]. Returns the
// existing entry for this behavior, or appends and returns a new one.
cfg::ActiveQueueManagement& selectOrCreateAqm(
    cfg::PortQueue& queue,
    cfg::QueueCongestionBehavior behavior) {
  auto& aqms = queue.aqms().ensure();
  for (auto& aqm : aqms) {
    if (*aqm.behavior() == behavior) {
      return aqm;
    }
  }
  return aqms.emplace_back();
}

} // namespace

namespace {
const std::vector<std::string>& queueAttrNames() {
  static const std::vector<std::string> kNames = {
      "name",
      "reserved-bytes",
      "shared-bytes",
      "max-dynamic-shared-bytes",
      "weight",
      "scaling-factor",
      "scheduling",
      "stream-type",
      "buffer-pool-name",
      std::string(kAttrRateLimit),
      std::string(kAttrAqm),
  };
  return kNames;
}
} // namespace

const std::string& validQueueAttrs() {
  static const std::string kAttrs = folly::join(", ", queueAttrNames());
  return kAttrs;
}

std::vector<std::string> completeQueueAttrs(
    const std::vector<std::string>& typed,
    size_t minObjects) {
  // Everything after active-queue-management is its own grammar.
  for (size_t i = minObjects; i < typed.size(); ++i) {
    if (typed[i] == kAttrAqm || typed[i] == kAttrAqmShort) {
      std::vector<std::string> aqmTyped(typed.begin() + i + 1, typed.end());
      std::vector<std::string> attrs = {"congestion-behavior", "detection"};
      for (const auto& [key, _] : linearSetters()) {
        attrs.push_back(key);
      }
      return completion::completeAttrGrammar(
          aqmTyped,
          {.attrs = std::move(attrs),
           .values = {
               {"congestion-behavior",
                completion::enumNames<cfg::QueueCongestionBehavior>()},
               {"detection", {"linear"}},
           }});
    }
  }
  auto scheduling = completion::enumNames<cfg::QueueScheduling>();
  scheduling.insert(scheduling.end(), {"WRR", "SP", "DRR"});
  return completion::completeAttrGrammar(
      typed,
      {.attrs = queueAttrNames(),
       .values =
           {
               {"scaling-factor",
                completion::enumNames<cfg::MMUScalingFactor>()},
               {"scheduling", std::move(scheduling)},
               {"stream-type", completion::enumNames<cfg::StreamType>()},
               {std::string(kAttrRateLimit),
                {std::string(kRateUnitKbps), std::string(kRateUnitPps)}},
           },
       .minObjects = minObjects});
}

void walkQueueAttributes(
    const std::vector<std::string>& v,
    size_t begin,
    std::vector<std::pair<std::string, std::vector<std::string>>>& attributes,
    std::vector<std::string>& aqmAttributes) {
  for (size_t i = begin; i < v.size();) {
    const auto& attr = v[i];
    if (attr == kAttrAqm || attr == kAttrAqmShort) {
      // Everything after the keyword is the AQM sub-arg stream; an empty tail
      // would otherwise parse as "no edit" and silently succeed.
      if (i + 1 >= v.size()) {
        throw std::invalid_argument(
            "active-queue-management requires sub-attributes");
      }
      aqmAttributes.assign(v.begin() + i + 1, v.end());
      break;
    }
    const size_t count = attrValueCount(v, i);
    if (i + count >= v.size()) {
      throw std::invalid_argument(attrArityError(attr));
    }
    // A repeated attribute is last-wins, and the commands echo every token
    // back as if all of them applied. Refuse it rather than lie.
    const bool seen = std::any_of(
        attributes.begin(), attributes.end(), [&attr](const auto& kv) {
          return kv.first == attr;
        });
    if (seen) {
      throw std::invalid_argument(
          fmt::format("'{}' given more than once", attr));
    }
    attributes.emplace_back(
        attr,
        std::vector<std::string>(v.begin() + i + 1, v.begin() + i + 1 + count));
    i += 1 + count;
  }
}

std::optional<cfg::StreamType> findStreamTypeAttr(
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        attributes) {
  for (const auto& [attr, values] : attributes) {
    if (attr == "stream-type") {
      return parseThriftEnum<cfg::StreamType>(attr, values.front());
    }
  }
  return std::nullopt;
}

QueueIdAndAttributes::QueueIdAndAttributes(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Expected: <queue-id> <attr> <value> [<attr> <value> ...] where "
        "<attr> is one of: " +
        validQueueAttrs());
  }

  // Parse the queue ID (first argument). The true upper bound is ASIC
  // dependent; kMaxQueueId is the shared arbitrary-but-high limit.
  queueId_ = folly::to<int16_t>(v[0]);
  if (queueId_ < 0 || queueId_ > kMaxQueueId) {
    throw std::invalid_argument(
        fmt::format(
            "Queue ID must be between 0 and {}, got: {}",
            kMaxQueueId,
            queueId_));
  }
  data_.push_back(v[0]);

  walkQueueAttributes(v, 1, attributes_, aqmAttributes_);
  // data_ already holds v[0]; the walk validated the rest, echo them in order.
  data_.insert(data_.end(), v.begin() + 1, v.end());
}

void applyPortQueueConfig(
    cfg::PortQueue& queue,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        attributes,
    const std::vector<std::string>& aqmArgs) {
  if (attributes.empty() && aqmArgs.empty()) {
    throw std::invalid_argument(
        "At least one attribute is required: " + validQueueAttrs());
  }

  for (const auto& [attr, values] : attributes) {
    // Scalar attributes take exactly one value token -- the parser enforces
    // the per-attribute count -- while the multi-token ones read `values`
    // whole.
    const auto& value = values.front();
    if (attr == "name") {
      if (value.empty()) {
        throw std::invalid_argument("name <string> must be non-empty");
      }
      queue.name() = value;
    } else if (attr == "reserved-bytes") {
      queue.reservedBytes() = parseNonNegativeInt(attr, value);
    } else if (attr == "shared-bytes") {
      queue.sharedBytes() = parseNonNegativeInt(attr, value);
    } else if (attr == "max-dynamic-shared-bytes") {
      // The dynamic counterpart of shared-bytes, used when scaling-factor
      // (alpha) drives the threshold rather than a static byte count.
      queue.maxDynamicSharedBytes() = parseNonNegativeInt(attr, value);
    } else if (attr == "weight") {
      auto weight = parseNonNegativeInt(attr, value);
      // The SAI scheduler profile stores the WRR weight as a uint8
      // (SAI_SCHEDULER_ATTR_SCHEDULING_WEIGHT), so anything above 255 would
      // silently truncate at apply time.
      if (weight > 255) {
        throw std::invalid_argument(
            fmt::format("weight must be in [0, 255], got {}", weight));
      }
      queue.weight() = weight;
    } else if (attr == "scaling-factor") {
      queue.scalingFactor() =
          parseThriftEnum<cfg::MMUScalingFactor>(attr, value);
    } else if (attr == "scheduling") {
      auto scheduling = parseScheduling(value);
      if (!scheduling) {
        throw std::invalid_argument(
            "Invalid scheduling: '" + value +
            "'. Valid values are: " + getValidSchedulingTypes());
      }
      queue.scheduling() = *scheduling;
    } else if (attr == "stream-type") {
      queue.streamType() = parseThriftEnum<cfg::StreamType>(attr, value);
    } else if (attr == "buffer-pool-name") {
      queue.bufferPoolName() = value;
    } else if (attr == kAttrRateLimit) {
      applyRateLimit(queue, values);
    } else {
      throw std::invalid_argument(
          "Unknown attribute: '" + attr +
          "'. Valid attributes are: " + validQueueAttrs());
    }
  }

  if (!aqmArgs.empty()) {
    // congestion-behavior is the key that identifies which AQM policy on the
    // queue this edit targets. Without it the target is ambiguous (a queue can
    // hold both an ECN and an EARLY_DROP entry) and creating an entry would
    // silently commit behavior=EARLY_DROP (thrift enum default 0). Require it.
    auto behavior = findCongestionBehavior(aqmArgs);
    if (!behavior.has_value()) {
      throw std::invalid_argument(
          "active-queue-management requires 'congestion-behavior "
          "<ECN|EARLY_DROP>' to identify which AQM policy to edit");
    }
    auto& aqm = selectOrCreateAqm(queue, *behavior);
    aqm.behavior() = *behavior;
    parseAqmAttributes(aqmArgs, aqm);
  }
}

QueueConfigName::QueueConfigName(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument("Queue config name is required");
  }
  if (v.size() != 1) {
    throw std::invalid_argument(
        "Expected a single queue config name, got: " + folly::join(", ", v));
  }
  const auto& name = v[0];
  static const re2::RE2 kValidNamePattern("^[a-zA-Z][a-zA-Z0-9_-]{0,63}$");
  if (!re2::RE2::FullMatch(name, kValidNamePattern)) {
    throw std::invalid_argument(
        "Invalid queue config name: '" + name +
        "'. Name must start with a letter, contain only alphanumeric "
        "characters, underscores, or hyphens, and be 1-64 characters long.");
  }
  data_.push_back(name);
}

std::vector<cfg::PortQueue>& queueConfigListForWrite(
    cfg::SwitchConfig& switchConfig,
    const QueueConfigName& name) {
  if (name.isDefault()) {
    return *switchConfig.defaultPortQueues();
  }
  return (*switchConfig.portQueueConfigs())[name.getName()];
}

std::vector<cfg::PortQueue>* findQueueConfigList(
    cfg::SwitchConfig& switchConfig,
    const QueueConfigName& name) {
  if (name.isDefault()) {
    return &*switchConfig.defaultPortQueues();
  }
  auto& configs = *switchConfig.portQueueConfigs();
  auto it = configs.find(name.getName());
  return it == configs.end() ? nullptr : &it->second;
}

std::vector<std::string> portsUsingQueueConfig(
    const cfg::SwitchConfig& switchConfig,
    const QueueConfigName& name) {
  std::vector<std::string> portNames;
  if (name.isDefault()) {
    return portNames;
  }
  for (const auto& port : *switchConfig.ports()) {
    if (port.portQueueConfigName().has_value() &&
        *port.portQueueConfigName() == name.getName()) {
      // Port::name is optional; switch_config.thrift documents the fallback as
      // "port-<logicalID>", so use that rather than reporting an empty name.
      if (port.name().has_value()) {
        portNames.push_back(*port.name());
      } else {
        portNames.push_back(fmt::format("port-{}", *port.logicalID()));
      }
    }
  }
  return portNames;
}

} // namespace facebook::fboss::utils
