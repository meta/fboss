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

#include <fmt/format.h>
#include <folly/Conv.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * Value parsing helpers shared by the BGP config dispatchers
 * (`config protocol bgp global` / `config protocol bgp neighbor`).
 *
 * All parsers return std::nullopt on invalid input instead of throwing so a
 * handler can reject the value with a user-facing message and leave the
 * session unpersisted.
 */
namespace facebook::fboss::bgpcli {

// Outcome of an attribute handler: on failure the message is returned to the
// user and the session is NOT persisted, so a rejected value never lands on
// disk.
struct Result {
  bool ok;
  std::string message;
};

inline Result ok(std::string message) {
  return Result{true, std::move(message)};
}

inline Result err(std::string message) {
  return Result{false, std::move(message)};
}

inline std::optional<bool> parseBool(const std::string& value) {
  if (value == "true" || value == "1" || value == "yes") {
    return true;
  }
  if (value == "false" || value == "0" || value == "no") {
    return false;
  }
  return std::nullopt;
}

template <typename T>
std::optional<T> parseInt(const std::string& value) {
  try {
    return folly::to<T>(value);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Parse a non-negative value that must fit in int32 (used for second-valued
// timers and min-routes).
inline std::optional<int32_t> parseNonNegInt32(const std::string& value) {
  auto parsed = parseInt<int64_t>(value);
  if (!parsed || *parsed < 0 || *parsed > std::numeric_limits<int32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<int32_t>(*parsed);
}

// Parse a non-negative int64 (used for route limits).
inline std::optional<int64_t> parseNonNegInt64(const std::string& value) {
  auto parsed = parseInt<int64_t>(value);
  if (!parsed || *parsed < 0) {
    return std::nullopt;
  }
  return parsed;
}

// Parse a 4-byte ASN (RFC 6793): an unsigned value in [0, 2^32-1]. The thrift
// fields are i64, so an unchecked uint64 parse would let an out-of-range ASN
// wrap or exceed the protocol range and be persisted.
inline std::optional<int64_t> parseAsn4Byte(const std::string& value) {
  auto parsed = parseInt<uint64_t>(value);
  if (!parsed || *parsed > std::numeric_limits<uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<int64_t>(*parsed);
}

// ---- constructor-time token parsing ----------------------------------------
// Unlike the value parsers above, this runs while constructing a command's
// ObjectArgType, where throwing std::invalid_argument IS the framework's
// error channel — the message is surfaced to the user as the parse error.

// The `<list-name> [<keyword> <member-name>]` prefix, for the *delete*
// command of a policy list whose members are keyed objects (prefix-list
// entries): the list name, the optional member selected by `keyword`, and
// where any remaining tokens begin.
//
// Only delete parses this shape by hand. The config side reaches the same
// grammar through CLI11 subcommand nesting instead — the member keyword is a
// registered subcommand there, so each level gets its own already-split token
// vector and never calls this helper. Lists whose members are a flat string
// list in the daemon (as-path-list `regex`, community-list `community`) have
// no member level at all: their optional `<keyword> <value>` selector is a
// plain attribute-style pair, parsed inline by their own delete command.
struct ListMemberSelector {
  std::string listName;
  std::optional<std::string> memberName;
  // Index of the first token after the parsed prefix (== tokens.size() when
  // nothing follows). The delete dispatcher rejects any tail.
  size_t restStart;
};

// Parse the prefix. `objectName` is the list flavor for messages (e.g.
// "community-list"), `memberKeyword` selects the nested member (e.g.
// `community`), and `usage` is the whole-command usage line thrown when no
// tokens were given. A second token other than `memberKeyword` is left to
// the caller (restStart == 1), which delete rejects as an unexpected token.
inline ListMemberSelector parseListMemberSelector(
    const std::vector<std::string>& tokens,
    std::string_view objectName,
    std::string_view memberKeyword,
    std::string_view usage) {
  if (tokens.empty()) {
    throw std::invalid_argument(std::string(usage));
  }
  if (tokens[0].empty()) {
    throw std::invalid_argument(
        fmt::format("Error: {} name must not be empty", objectName));
  }
  ListMemberSelector selector{tokens[0], std::nullopt, 1};
  if (tokens.size() > 1 && tokens[1] == memberKeyword) {
    if (tokens.size() < 3 || tokens[2].empty()) {
      throw std::invalid_argument(
          fmt::format("Error: `{}` requires a <name>", memberKeyword));
    }
    selector.memberName = tokens[2];
    selector.restStart = 3;
  }
  return selector;
}

} // namespace facebook::fboss::bgpcli
