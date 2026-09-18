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

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <folly/CppAttributes.h>

#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss {

/*
 * Attribute and keyword names shared by the `config port-channel` and
 * `delete port-channel` command families, so the two cannot drift apart.
 */
inline constexpr std::string_view kPortChannelAttrDescription = "description";
inline constexpr std::string_view kPortChannelAttrMinimumLinks =
    "minimum-links";
inline constexpr std::string_view kPortChannelMemberOpAdd = "add";
inline constexpr std::string_view kPortChannelMemberOpRemove = "remove";
inline constexpr std::string_view kPortChannelMemberAttrPriority = "priority";
inline constexpr std::string_view kPortChannelMemberKeywordLacp = "lacp";
inline constexpr std::string_view kPortChannelMemberAttrLacpRate = "rate";
inline constexpr std::string_view kPortChannelMemberAttrLacpMode = "mode";
inline constexpr std::string_view kPortChannelMemberAttrLacpHoldTimer =
    "hold-timer";
inline constexpr std::string_view kLacpRateSlow = "slow";
inline constexpr std::string_view kLacpRateFast = "fast";
inline constexpr std::string_view kLacpModeActive = "active";
inline constexpr std::string_view kLacpModePassive = "passive";

/*
 * Default LACP port priority assigned to newly added members. This is the
 * IEEE 802.1AX default and the value used throughout production configs
 * (cfg::AggregatePortMember.priority has no thrift default).
 */
inline constexpr int32_t kPortChannelDefaultMemberPriority = 32768;

/*
 * cfg::AggregatePort.key is an i16 consumed as a uint16_t AggregatePortID;
 * restrict CLI-created port-channels to the positive i16 range.
 */
inline constexpr int32_t kPortChannelMinKey = 1;
inline constexpr int32_t kPortChannelMaxKey = 32767;

/*
 * SwitchConfig.aggregatePorts[*].name becomes the SAI LAG label, which is
 * truncated to 32 characters (see SaiLagManager::addLag).
 */
inline constexpr size_t kPortChannelMaxNameLen = 32;

/*
 * The minimumCapacity link count is a thrift `byte` (int8).
 */
inline constexpr int32_t kPortChannelMinLinkCount = 1;
inline constexpr int32_t kPortChannelMaxLinkCount = 127;

/*
 * Result of applying one attribute handler to its target.
 */
struct PortChannelAttrResult {
  // Whether the target actually changed (drives the "No changes" replies).
  bool changed;
  // User-facing rendering of what was applied, e.g. "minimum-links=2" for a
  // set or "lacp rate" for a reset.
  std::string applied;
};

/*
 * One row of a port-channel attribute table: everything the config and
 * delete command families need to know about a single attribute of Target
 * (cfg::AggregatePort or cfg::AggregatePortMember). Rows are built by
 * per-value-shape factories in PortChannelUtils.cpp -- a bounded int, a
 * fixed token set mapped to an enum, a free-form string -- so an attribute
 * is one table line: name, shape, getter/setter, default. Keeping set and
 * reset in the same row is what keeps `config` and `delete` from drifting
 * apart.
 */
template <typename Target>
struct PortChannelAttrOps {
  // Value grammar for usage strings, e.g. "priority <0-65535>".
  std::string usage;
  // Validates a CLI value without a target (throws std::invalid_argument),
  // so arg parsing can reject bad values before any config is loaded.
  std::function<void(const std::string&)> validate;
  // Parses + assigns the value ("config ... <attr> <value>").
  std::function<PortChannelAttrResult(Target&, const std::string&)> set;
  // Restores the attribute's default ("delete ... <attr>").
  std::function<PortChannelAttrResult(Target&)> reset;
};

using PortChannelAttrTable =
    std::map<std::string, PortChannelAttrOps<cfg::AggregatePort>, std::less<>>;
using PortChannelMemberAttrTable = std::
    map<std::string, PortChannelAttrOps<cfg::AggregatePortMember>, std::less<>>;

/*
 * The attribute tables: one line per attribute of a port-channel and of a
 * port-channel member. Both the config and delete command families dispatch
 * through these.
 */
const PortChannelAttrTable& portChannelAttrTable();
const PortChannelMemberAttrTable& portChannelMemberAttrTable();

/*
 * Attribute names of portChannelAttrTable() as a lowercase set (for
 * MultiArgsConfigType specs) and as a comma-joined list (for error text).
 */
std::unordered_set<std::string> portChannelAttrNameSet();
std::string validPortChannelAttrs();

/*
 * Longest-prefix match of tokens[start..] against the member attribute
 * table (attributes may span two tokens, e.g. "lacp rate"). Returns the
 * matched table key and the number of tokens consumed, or std::nullopt.
 * Matching is case-insensitive.
 */
std::optional<std::pair<std::string, size_t>> matchPortChannelMemberAttr(
    const std::vector<std::string>& tokens,
    size_t start);

/*
 * Whether the token starts a member attribute (the first word of any member
 * table key, e.g. "priority" or "lacp"): used to split interface names from
 * attributes when parsing member commands.
 */
bool isPortChannelMemberAttrStart(const std::string& token);

/*
 * Grammar generated from portChannelAttrTable(), for the config-side
 * ("description <string> | minimum-links <1-127>") and delete-side
 * ("description|minimum-links") port-channel commands' help text.
 */
std::string portChannelAttrSetGrammar();
std::string portChannelAttrDeleteGrammar();

/*
 * Grammar generated from the member attribute table, for the config-side
 * ("add <interface> [...] | ... | <interface> priority <0-65535> | ...") and
 * delete-side ("<interface> [...] | <interface> priority | ...") member
 * commands. The *Usage variants prefix "Expected: " for error messages.
 */
std::string portChannelMemberSetGrammar();
std::string portChannelMemberDeleteGrammar();
std::string portChannelMemberSetUsage();
std::string portChannelMemberDeleteUsage();

/*
 * Returns an ASCII-lowercased copy of the string, for case-insensitive
 * keyword matching.
 */
std::string toLowerCopy(const std::string& s);

/*
 * Parses an integer CLI value and validates it against [min, max]. Throws
 * std::invalid_argument (naming `what`) when the value is not an integer or
 * is out of range.
 */
int32_t parseIntInRange(
    const std::string& value,
    std::string_view what,
    int32_t min,
    int32_t max);

/*
 * Validates a port-channel name and derives the AggregatePort key from its
 * trailing digits (e.g. "port-channel100" -> 100). Throws
 * std::invalid_argument when the name has no numeric suffix, the suffix is
 * outside [kPortChannelMinKey, kPortChannelMaxKey], or the name is longer
 * than kPortChannelMaxNameLen.
 */
int16_t parsePortChannelKey(const std::string& name);

/*
 * Finds the aggregate port with the given name. Returns nullptr when absent.
 */
cfg::AggregatePort* FOLLY_NULLABLE
findPortChannel(cfg::SwitchConfig& swConfig, const std::string& name);

/*
 * Finds the member entry for the given logical port ID within an aggregate
 * port. Returns nullptr when the port is not a member.
 */
cfg::AggregatePortMember* FOLLY_NULLABLE
findPortChannelMember(cfg::AggregatePort& portChannel, int32_t portId);

/*
 * Resolves an interface name (e.g. "eth1/5/1") to its cfg::Port in the
 * session config. Throws std::invalid_argument when the name does not
 * resolve to a physical port.
 */
cfg::Port* resolveMemberPort(const std::string& interfaceName);

/*
 * The ingress VLAN a port-channel's members share: the ingressVlan of the
 * first member whose port is in the config (0 for a routed LAG, whose members
 * are in no VLAN), or std::nullopt when the port-channel has no resolvable
 * members. The agent binds a LAG to the VLAN of its first member and assumes
 * all members share it (SaiLagManager::addLag,
 * ThriftConfigApplier::getAggregatePortInterfaceIDs).
 */
std::optional<int32_t> portChannelMemberVlan(
    const cfg::SwitchConfig& swConfig,
    const cfg::AggregatePort& portChannel);

/*
 * IDs of the port router interfaces bound to the port-channel with the given
 * key (cfg::Interface.aggregatePortID). Deleting a port-channel they still
 * reference makes the agent reject the config on apply.
 */
std::vector<int32_t> interfacesBoundToPortChannel(
    const cfg::SwitchConfig& swConfig,
    int16_t key);

/*
 * Name of the port-channel (if any) that the given logical port is already a
 * member of.
 */
std::optional<std::string> portChannelForMember(
    const cfg::SwitchConfig& swConfig,
    int32_t portId);

/*
 * Removes the given member interfaces from a port-channel. Duplicate names
 * are removed once. Throws std::invalid_argument when a name is not a
 * member, or when the removal would leave the port-channel with no members
 * (an aggregate port without members crashes the agent when applied; delete
 * the port-channel instead). Returns the list of removed interface names.
 */
std::vector<std::string> removePortChannelMembers(
    cfg::AggregatePort& portChannel,
    const std::vector<std::string>& memberNames);

} // namespace facebook::fboss
