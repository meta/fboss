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

#include <bits/types/struct_timeval.h>
#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <folly/stop_watch.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace facebook::fboss::utils {

// MTU bounds constants
constexpr int kMtuMin = 68;
constexpr int kMtuMax = 9416;

// 802.1Q VLAN id bounds. 0 and 4095 are reserved.
constexpr int32_t kVlanIdMin = 1;
constexpr int32_t kVlanIdMax = 4094;

// Queue-id ceiling shared by the qos config/delete commands. Arbitrary but
// high; the true maximum is ASIC-dependent.
constexpr int16_t kMaxQueueId = 128;

struct LocalOption {
  std::string name;
  std::string helpMsg;
  std::optional<std::string> defaultValue{std::nullopt};
  bool isFlag{false};
};

struct CmdLogInfo {
  std::string CmdName;
  std::string Duration;
  std::string Arguments;
  std::string UserInfo;
  std::string ExitStatus;
};

enum class ObjectArgTypeId : uint8_t {
  OBJECT_ARG_TYPE_ID_UNINITIALIZE = 0,
  OBJECT_ARG_TYPE_ID_NONE,
  OBJECT_ARG_TYPE_ID_COMMUNITY_LIST,
  OBJECT_ARG_TYPE_ID_IP_LIST, // IPv4 and/or IPv6
  OBJECT_ARG_TYPE_ID_IPV6_LIST,
  OBJECT_ARG_TYPE_ID_CIDR_NETWORK,
  OBJECT_ARG_TYPE_ID_PORT_LIST,
  OBJECT_ARG_TYPE_ID_MESSAGE,
  OBJECT_ARG_TYPE_ID_PEERID_LIST, // BGP peer id
  OBJECT_ARG_TYPE_ID_VIP_INJECTOR_ID,
  OBJECT_ARG_TYPE_ID_AREA_LIST,
  OBJECT_ARG_TYPE_ID_NODE_LIST,
  OBJECT_ARG_TYPE_ID_HW_OBJECT_LIST,
  OBJECT_ARG_TYPE_ID_SWITCH_NAME_LIST,
  OBJECT_ARG_TYPE_DEBUG_LEVEL,
  OBJECT_ARG_TYPE_PRBS_COMPONENT,
  OBJECT_ARG_TYPE_PRBS_STATE,
  OBJECT_ARG_TYPE_PORT_STATE,
  OBJECT_ARG_TYPE_FSDB_PATH,
  OBJECT_ARG_TYPE_AS_SEQUENCE,
  OBJECT_ARG_TYPE_LOCAL_PREFERENCE,
  OBJECT_ARG_TYPE_PHY_CHIP_TYPE,
  OBJECT_ARG_TYPE_FSDB_CLIENT_ID,
  OBJECT_ARG_TYPE_ID_SYSTEM_PORT_LIST,
  OBJECT_ARG_TYPE_ID_MIRROR_LIST,
  OBJECT_ARG_TYPE_LINK_DIRECTION,
  OBJECT_ARG_TYPE_MTU,
  OBJECT_ARG_TYPE_ID_INTERFACE_LIST,
  OBJECT_ARG_TYPE_ID_REVISION_LIST,
  OBJECT_ARG_TYPE_VLAN_ID,
  // Don't add new values here. Use Traits::addCliArg() instead (see
  // args-validation.mdx). This enum is legacy and will be removed once
  // all existing commands are migrated.
};

template <typename T>
class BaseObjectArgType {
 public:
  BaseObjectArgType() = default;
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BaseObjectArgType(std::vector<T> v) : data_(std::move(v)) {}
  using iterator = typename std::vector<T>::iterator;
  using const_iterator = typename std::vector<T>::const_iterator;
  using size_type = typename std::vector<T>::size_type;

  std::vector<T> data() const {
    return data_;
  }

  T& operator[](int index) {
    return data_[index];
  }

  const T& operator[](int index) const {
    return data_[index];
  }
  iterator begin() {
    return data_.begin();
  }
  iterator end() {
    return data_.end();
  }
  const_iterator begin() const {
    return data_.begin();
  }
  const_iterator end() const {
    return data_.end();
  }

  size_type size() const {
    return data_.size();
  }

  bool empty() const {
    return data_.empty();
  }

  std::vector<T> data_;
  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_UNINITIALIZE;
};

class NoneArgType : public BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ NoneArgType(std::vector<std::string> v)
      : BaseObjectArgType(std::move(v)) {}

  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
};

/*
 * Common base for CLI argument types that parse a token list of the form
 *   <object-names> [<attr> [<value>] ...]
 * into a leading list of object names plus (attribute, value) pairs. Tokens
 * before the first known attribute name are object names; valueful attributes
 * consume the following token, valueless attributes do not.
 *
 * The per-command grammar is supplied as data (a Spec) rather than via virtual
 * hooks: which attribute names exist, which are valueless, and the nouns used
 * in error messages. parseTokens() carries no command- or object-specific
 * terms, so it can be reused by any `config <object> ...` /
 * `delete <object> ...` argument type. Concrete arg types pass their Spec
 * through the constructor (see InterfaceAttrArgsBase for an example).
 */
class MultiArgsConfigType : public BaseObjectArgType<std::string> {
 public:
  // Per-command grammar. Attribute sets are stored lowercase; lookups
  // lowercase the input, so matching is case-insensitive.
  struct Spec {
    std::unordered_set<std::string> knownAttrs;
    std::unordered_set<std::string> valuelessAttrs; // subset of knownAttrs
    std::string objectKind; // e.g. "interface" -- used in error messages
    std::string attrKind; // e.g. "attribute" / "delete attribute"
    std::string validAttrs; // human-readable list, for error messages
  };

  /* (attribute, value) pairs. value is empty for valueless attributes. */
  const std::vector<std::pair<std::string, std::string>>& getAttributes()
      const {
    return attributes_;
  }

  /* Whether any attributes were provided. */
  bool hasAttributes() const {
    return !attributes_.empty();
  }

 protected:
  explicit MultiArgsConfigType(Spec spec) : spec_(std::move(spec)) {}

  bool isKnownAttribute(const std::string& s) const {
    return spec_.knownAttrs.count(toLower(s)) > 0;
  }

  bool isValuelessAttribute(const std::string& s) const {
    return spec_.valuelessAttrs.count(toLower(s)) > 0;
  }

  /*
   * Splits v into object names and attribute/value pairs (stored into
   * attributes_, names lowercased). Returns the leading object names so the
   * caller can resolve/validate them.
   */
  std::vector<std::string> parseTokens(const std::vector<std::string>& v) {
    if (v.empty()) {
      throw std::invalid_argument(
          fmt::format("No {} name provided", spec_.objectKind));
    }

    // Tokens before the first known attribute name are object names.
    size_t attrStart = v.size();
    for (size_t i = 0; i < v.size(); ++i) {
      if (isKnownAttribute(v[i])) {
        attrStart = i;
        break;
      }
    }

    // Must have at least one object name.
    if (attrStart == 0) {
      throw std::invalid_argument(
          fmt::format(
              "No {} name provided. First token '{}' is an attribute name.",
              spec_.objectKind,
              v[0]));
    }

    std::vector<std::string> objectNames(v.begin(), v.begin() + attrStart);

    // Parse attribute-value pairs (valueless attributes consume no value
    // token).
    for (size_t i = attrStart; i < v.size();) {
      const std::string& attr = v[i];
      if (!isKnownAttribute(attr)) {
        throw std::invalid_argument(
            fmt::format(
                "Unknown {} '{}'. Valid attributes are: {}",
                spec_.attrKind,
                attr,
                spec_.validAttrs));
      }

      std::string attrLower = toLower(attr);

      if (isValuelessAttribute(attrLower)) {
        attributes_.emplace_back(std::move(attrLower), "");
        ++i;
        continue;
      }

      if (i + 1 >= v.size()) {
        throw std::invalid_argument(
            fmt::format("Missing value for attribute '{}'", attr));
      }

      const std::string& value = v[i + 1];

      // Check if "value" is actually another attribute name (user forgot the
      // value).
      if (isKnownAttribute(value)) {
        throw std::invalid_argument(
            fmt::format(
                "Missing value for attribute '{}'. Got another attribute '{}' instead.",
                attr,
                value));
      }

      attributes_.emplace_back(std::move(attrLower), value);
      i += 2;
    }

    return objectNames;
  }

  static std::string toLower(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower;
  }

  Spec spec_;
  std::vector<std::pair<std::string, std::string>> attributes_;
};

template <typename T, size_t N, size_t... Indices>
auto arrayToTupleImpl(
    const std::array<T, N>& v,
    std::index_sequence<Indices...>) {
  return std::make_tuple(v[Indices]...);
}

// convert the first N elements of an array to a tuple
template <size_t N, size_t M, typename T>
auto arrayToTuple(const std::array<T, M>& v) {
  static_assert(N <= M);
  return arrayToTupleImpl(v, std::make_index_sequence<N>());
}

// returns tuple(value) if TargetT == std::monostate, otherwise empty tuple()
template <typename TargetT, typename T>
auto tupleValueIfNotMonostate(const T& value) {
  // backward compatibility
  if constexpr (std::is_same_v<TargetT, std::monostate>) {
    return std::make_tuple();
  }
  // NoneArgType indicates OBJECT_ARG_TYPE_ID_NONE
  else if constexpr (std::is_same_v<TargetT, NoneArgType>) {
    return std::make_tuple();
  } else {
    return std::make_tuple(value);
  }
}

template <typename UnfilteredTypes, typename Tuple, std::size_t... Indices>
auto filterTupleMonostatesImpl(Tuple tup, std::index_sequence<Indices...>) {
  return std::tuple_cat(
      tupleValueIfNotMonostate<std::tuple_element_t<Indices, UnfilteredTypes>>(
          std::get<Indices>(tup))...);
}

// Filter out all std::monostates from a tuple. the passed UnfilteredTypes
// will indicate which indices need to be removed
template <typename UnfilteredTypes, typename Tuple, std::size_t... Indices>
auto filterTupleMonostates(Tuple tup) {
  static_assert(
      std::tuple_size_v<UnfilteredTypes> == std::tuple_size_v<Tuple>,
      "Passed tuple must be the same size as passed type");
  return filterTupleMonostatesImpl<UnfilteredTypes>(
      tup, std::make_index_sequence<std::tuple_size_v<UnfilteredTypes>>());
}

// API to retrieve host related information
std::pair<std::string, folly::IPAddress> getCanonicalNameAndIPFromHost(
    const std::string& hostname);
const std::string getOobNameFromHost(const std::string& host);
std::vector<std::string> getHostsInSmcTier(const std::string& parentTierName);
std::vector<std::string> getHostsFromFile(const std::string& filename);

// Common util method
timeval splitFractionalSecondsFromTimer(const long& timer);
const std::string parseTimeToTimeStamp(const long& timeToParse);
const std::string formatBandwidth(float bandwidthBytesPerSecond);
long getEpochFromDuration(const int64_t& duration);
const std::string getDurationStr(folly::stop_watch<>& watch);
const std::string getPrettyElapsedTime(const int64_t& start_time);

// ASCII-lowercased copy of a string, for case-insensitive keyword matching.
std::string toLowerCopy(const std::string& s);

std::string getUserInfo();
std::string getUnixname();

void setLogLevel(const std::string& logLevelStr);
void logUsage(const CmdLogInfo& cmdLogInfo);

} // namespace facebook::fboss::utils
