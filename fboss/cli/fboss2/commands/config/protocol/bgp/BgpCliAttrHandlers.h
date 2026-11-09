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

#include <fmt/core.h>
#include <folly/IPAddress.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliValueParsers.h"
#include "fmt/format.h"
#include "thrift/lib/cpp2/gen/module_types_h.h"

/**
 * Generic, target-typed per-attribute handler factories shared by every
 * `config protocol bgp ...` dispatcher.
 *
 * Each factory owns one value SHAPE (a bool, a bounded int, a 4-byte ASN, an
 * IP address, a seconds-valued timer, ...): it validates the CLI tokens,
 * produces the usage and rejection text for that shape, and on success calls
 * the caller-supplied setter with an already-parsed value. They are templated
 * on the target type T so the same logic mutates any BGP thrift object
 * (BgpPeer, PeerGroup, AsPathList, PrefixListEntry, BgpPolicyTerm, ...).
 *
 * A dispatcher's registry is therefore one line per attribute -- key, shape,
 * setter -- with no handler bodies inline. If an attribute seems to need a
 * hand-written handler, that means its value shape has no factory yet, and the
 * fix is to add the factory here.
 */
namespace facebook::fboss::bgpcli {

using Tokens = std::vector<std::string>;

// A per-attribute handler mutating a typed thrift target T in place.
template <typename T>
using AttrHandler = std::function<Result(T&, const Tokens&)>;

// A single-token string value (names, regex patterns, policy names).
template <typename T>
AttrHandler<T> stringAttr(
    std::string_view name,
    std::string_view valueName,
    std::function<void(T&, const std::string&)> set) {
  return [name, valueName, set = std::move(set)](
             T& target, const Tokens& values) -> Result {
    if (values.size() != 1) {
      return err(fmt::format("Error: {} requires <{}>", name, valueName));
    }
    set(target, values[0]);
    return ok(fmt::format("Successfully set {} to: {}", name, values[0]));
  };
}

// A free-text string value that may span multiple CLI tokens (descriptions);
// the tokens are re-joined with single spaces.
template <typename T>
AttrHandler<T> joinedStringAttr(
    std::string_view name,
    std::function<void(T&, const std::string&)> set) {
  return
      [name, set = std::move(set)](T& target, const Tokens& values) -> Result {
        if (values.empty()) {
          return err(fmt::format("Error: {} requires <string>", name));
        }
        std::string joined = values[0];
        for (size_t i = 1; i < values.size(); ++i) {
          joined += " " + values[i];
        }
        set(target, joined);
        return ok(fmt::format("Successfully set {} to: {}", name, joined));
      };
}

// A true/false value (accepts the parseBool spellings).
template <typename T>
AttrHandler<T> boolAttr(
    std::string_view name,
    std::function<void(T&, bool)> set) {
  return
      [name, set = std::move(set)](T& target, const Tokens& values) -> Result {
        if (values.size() != 1) {
          return err(fmt::format("Error: {} requires <true|false>", name));
        }
        auto enable = parseBool(values[0]);
        if (!enable) {
          return err(
              fmt::format(
                  "Error: Invalid {} value '{}'; expected true or false",
                  name,
                  values[0]));
        }
        set(target, *enable);
        return ok(
            fmt::format(
                "Successfully {} {}", *enable ? "enabled" : "disabled", name));
      };
}

// A bounded integer value in [minValue, maxValue]. `valueDesc` documents the
// accepted range in both the usage and the rejection message; it is taken by
// value for the same dangling-temporary reason as enumAttr's.
template <typename T>
AttrHandler<T> intAttr(
    std::string_view name,
    std::string valueDesc,
    int32_t minValue,
    int32_t maxValue,
    std::function<void(T&, int32_t)> set) {
  return [name,
          valueDesc = std::move(valueDesc),
          minValue,
          maxValue,
          set = std::move(set)](T& target, const Tokens& values) -> Result {
    if (values.size() != 1) {
      return err(fmt::format("Error: {} requires <{}>", name, valueDesc));
    }
    auto parsed = parseInt<int64_t>(values[0]);
    if (!parsed || *parsed < minValue || *parsed > maxValue) {
      return err(
          fmt::format(
              "Error: Invalid {} value '{}'; expected {}",
              name,
              values[0],
              valueDesc));
    }
    set(target, static_cast<int32_t>(*parsed));
    return ok(fmt::format("Successfully set {} to: {}", name, values[0]));
  };
}

// A non-negative integer value in [0, maxValue], for the thrift fields that
// hold an unsigned quantity in a signed i64 (local-pref, med, ...). Unlike
// intAttr the accepted range is DERIVED from maxValue for both the usage and
// the rejection message, so the two cannot drift apart. The setter takes
// int64_t to match those fields directly, with no cast at the call site.
template <typename T>
AttrHandler<T> uintAttr(
    std::string_view name,
    int64_t maxValue,
    std::function<void(T&, int64_t)> set) {
  return [name, maxValue, set = std::move(set)](
             T& target, const Tokens& values) -> Result {
    auto parsed = values.size() == 1 ? parseInt<int64_t>(values[0])
                                     : std::optional<int64_t>();
    if (!parsed || *parsed < 0 || *parsed > maxValue) {
      return err(fmt::format("Error: {} requires <0-{}>", name, maxValue));
    }
    set(target, *parsed);
    return ok(fmt::format("Successfully set {} to: {}", name, values[0]));
  };
}

// A 4-byte ASN value (RFC 6793). The thrift fields are i64, so an unchecked
// parse would let an out-of-range ASN wrap or exceed the protocol range.
template <typename T>
AttrHandler<T> asnAttr(
    std::string_view name,
    std::function<void(T&, int64_t)> set) {
  return
      [name, set = std::move(set)](T& target, const Tokens& values) -> Result {
        if (values.size() != 1) {
          return err(fmt::format("Error: {} requires <asn>", name));
        }
        auto asn = parseAsn4Byte(values[0]);
        if (!asn) {
          return err(
              fmt::format(
                  "Error: Invalid {} value '{}'; expected an unsigned 4-byte "
                  "ASN",
                  name,
                  values[0]));
        }
        set(target, *asn);
        return ok(fmt::format("Successfully set {} to: {}", name, *asn));
      };
}

// A validated IP address value, optionally restricted to one family, stored
// normalized.
template <typename T>
AttrHandler<T> ipAttr(
    std::string_view name,
    std::function<void(T&, const std::string&)> set,
    std::optional<bool> requireV6 = std::nullopt) {
  return [name, set = std::move(set), requireV6](
             T& target, const Tokens& values) -> Result {
    if (values.size() != 1) {
      return err(fmt::format("Error: {} requires <ip-address>", name));
    }
    auto addr = folly::IPAddress::tryFromString(values[0]);
    if (!addr.hasValue()) {
      return err(
          fmt::format("Error: Invalid {} address '{}'", name, values[0]));
    }
    if (requireV6.has_value() && addr->isV6() != *requireV6) {
      return err(
          fmt::format(
              "Error: {} requires an IPv{} address, got '{}'",
              name,
              *requireV6 ? 6 : 4,
              values[0]));
    }
    set(target, addr->str());
    return ok(fmt::format("Successfully set {} to: {}", name, addr->str()));
  };
}

// The protocol range of a second-valued timer: 0 (disabled) is always
// accepted, any other value must lie in [min, max]. The default is "anything
// non-negative that fits in int32"; callers narrow it where the wire format or
// the daemon does (a 16-bit BGP hold time, a 12-bit graceful-restart time).
struct SecondsRange {
  int32_t min = 0;
  int32_t max = std::numeric_limits<int32_t>::max();
};

// A second-valued timer: 0 or a value within `range`.
template <typename T>
AttrHandler<T> secondsAttr(
    std::string_view name,
    std::function<void(T&, int32_t)> set,
    SecondsRange range = {}) {
  return [name, set = std::move(set), range](
             T& target, const Tokens& values) -> Result {
    if (values.size() != 1) {
      return err(fmt::format("Error: {} requires <seconds>", name));
    }
    auto seconds = parseNonNegInt32(values[0]);
    if (!seconds) {
      return err(
          fmt::format(
              "Error: {} must be a non-negative integer that fits in "
              "int32, got '{}'",
              name,
              values[0]));
    }
    if (*seconds != 0 && (*seconds < range.min || *seconds > range.max)) {
      return err(
          fmt::format(
              "Error: {} must be 0 or between {} and {} seconds, got {}",
              name,
              range.min,
              range.max,
              *seconds));
    }
    set(target, *seconds);
    return ok(
        fmt::format("Successfully set {} to: {} seconds", name, *seconds));
  };
}

// A non-negative int64 route-count value (RouteLimit fields).
template <typename T>
AttrHandler<T> routeCountAttr(
    std::string_view name,
    std::function<void(T&, int64_t)> set) {
  return
      [name, set = std::move(set)](T& target, const Tokens& values) -> Result {
        if (values.size() != 1) {
          return err(fmt::format("Error: {} requires <value>", name));
        }
        auto limit = parseNonNegInt64(values[0]);
        if (!limit) {
          return err(
              fmt::format(
                  "Error: {} must be a non-negative integer, got '{}'",
                  name,
                  values[0]));
        }
        set(target, *limit);
        return ok(fmt::format("Successfully set {} to: {}", name, *limit));
      };
}

// An attribute the CLI must refuse: either the thrift has no field for it
// (persisting it would stage dead config the daemon ignores) or the knob is
// not settable at this level. The reason is surfaced to the user instead.
template <typename T>
AttrHandler<T> rejectedAttr(std::string_view name, std::string_view reason) {
  return [name, reason](T& /* target */, const Tokens& /* values */) -> Result {
    return err(fmt::format("Error: {} is not supported: {}", name, reason));
  };
}

// The spelling bgpd's link-bandwidth parser accepts (parseLinkBandwidthBps in
// its policy utils): a decimal number with at most one '.', an optional
// case-insensitive K/M/G multiplier as the last character, or the literal
// "auto" (bgpd derives the value from the port speed). nullopt if bgpd would
// reject it.
inline std::optional<std::string> parseLinkBandwidth(const std::string& raw) {
  std::string v = folly::trimWhitespace(raw).str();
  if (v == "auto") {
    return v;
  }
  if (std::count(v.begin(), v.end(), '.') > 1) {
    return std::nullopt;
  }
  std::string number = v;
  const size_t suffixAt = v.find_first_not_of(".0123456789");
  if (suffixAt != std::string::npos) {
    const char suffix =
        static_cast<char>(std::toupper(static_cast<unsigned char>(v.back())));
    if (suffixAt != v.size() - 1 ||
        (suffix != 'K' && suffix != 'M' && suffix != 'G')) {
      return std::nullopt;
    }
    number.pop_back();
  }
  if (number.empty() || number == ".") {
    return std::nullopt;
  }
  try {
    std::stod(number);
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return v;
}

// A link-bandwidth value in the spelling bgpd accepts (see
// parseLinkBandwidth). The thrift field is a string that bgpd re-parses, so
// the CLI validates with the same rules rather than a stricter subset -- an
// existing config's "1.5G", "150g" or "auto" must round-trip through the CLI.
template <typename T>
AttrHandler<T> bitRateAttr(
    std::string_view name,
    std::function<void(T&, const std::string&)> set) {
  return
      [name, set = std::move(set)](T& target, const Tokens& values) -> Result {
        if (values.size() != 1) {
          return err(
              fmt::format(
                  "Error: {} requires <bits-per-second>[K|M|G] or auto", name));
        }
        auto v = parseLinkBandwidth(values[0]);
        if (!v) {
          return err(
              fmt::format(
                  "Error: Invalid {} value '{}'; expected a number with an "
                  "optional K/M/G suffix (e.g. 100G, 1.5G) or auto",
                  name,
                  values[0]));
        }
        set(target, *v);
        return ok(fmt::format("Successfully set {} to: {}", name, *v));
      };
}

// A value drawn from a fixed set of names, mapped to an enum by `lookup`
// (returns std::nullopt for an unrecognized name). `valueDesc` documents the
// accepted names in both the usage and the rejection message; it is taken by
// value because call sites build it with fmt::format, and a string_view
// capture would dangle once that temporary dies.
template <typename T, typename Enum>
AttrHandler<T> enumAttr(
    std::string_view name,
    std::string valueDesc,
    std::function<std::optional<Enum>(const std::string&)> lookup,
    std::function<void(T&, Enum)> set) {
  return [name,
          valueDesc = std::move(valueDesc),
          lookup = std::move(lookup),
          set = std::move(set)](T& target, const Tokens& values) -> Result {
    if (values.size() != 1) {
      return err(fmt::format("Error: {} requires <{}>", name, valueDesc));
    }
    auto parsed = lookup(values[0]);
    if (!parsed) {
      return err(
          fmt::format(
              "Error: Invalid {} value '{}'; expected {}",
              name,
              values[0],
              valueDesc));
    }
    set(target, *parsed);
    return ok(fmt::format("Successfully set {} to: {}", name, values[0]));
  };
}

// A thrift enum attribute identified by its generated metadata rather than a
// hand-written lookup: accepts the enum value name (e.g. BEST_PATH), the
// integer value, or true/false as aliases for 1/0 (the per-attribute commands
// this replaced took booleans, so existing usage keeps working). Prefer
// enumAttr when only a documented subset of the enum is valid; use this when
// the whole enum is.
template <typename T, typename EnumT>
AttrHandler<T> thriftEnumAttr(
    std::string_view name,
    std::function<void(T&, EnumT)> set) {
  return [name, set = std::move(set)](
             T& target, const Tokens& values) -> Result {
    if (values.size() != 1) {
      return err(fmt::format("Error: {} requires <mode>", name));
    }
    EnumT mode;
    bool valid = false;
    if (auto parsed = parseInt<int32_t>(values[0])) {
      mode = static_cast<EnumT>(*parsed);
      valid = apache::thrift::TEnumTraits<EnumT>::findName(mode) != nullptr;
    } else if (auto enable = parseBool(values[0])) {
      mode = static_cast<EnumT>(*enable ? 1 : 0);
      valid = true;
    } else {
      valid = apache::thrift::TEnumTraits<EnumT>::findValue(values[0], &mode);
    }
    if (!valid) {
      std::string names;
      for (auto n : apache::thrift::TEnumTraits<EnumT>::names) {
        names += names.empty() ? "" : ", ";
        names += std::string(n);
      }
      return err(
          fmt::format(
              "Error: {} value '{}' is not a valid mode; expected one of: "
              "{}",
              name,
              values[0],
              names));
    }
    set(target, mode);
    return ok(
        fmt::format(
            "Successfully set {} to: {}", name, static_cast<int32_t>(mode)));
  };
}

} // namespace facebook::fboss::bgpcli
