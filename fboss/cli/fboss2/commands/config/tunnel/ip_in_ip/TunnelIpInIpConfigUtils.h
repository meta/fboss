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

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

/*
 * Shared parsing + apply helpers for the IP-in-IP tunnel config commands.
 *
 * The encap and decap subcommands differ only in:
 *   - the tunnelType they stamp on the tunnel (IP_IN_IP_ENCAP vs
 *     IP_IN_IP_DECAP), and
 *   - the set of attributes they accept (termination-type is decap-only).
 *
 * Everything else — token parsing, IPv6/mask/mode validation, and writing the
 * attributes onto the staged cfg::IpInIpTunnel — is identical, so it lives
 * here and is shared by both subcommand handlers. The attribute-name
 * constants and helpers here are also reused by the delete-side utils
 * (TunnelIpInIpDeleteUtils).
 */
namespace facebook::fboss::tunnel_utils {

// Attribute names accepted on the config command line.
inline constexpr std::string_view kAttrDestination = "destination";
inline constexpr std::string_view kAttrSource = "source";
inline constexpr std::string_view kAttrTtlMode = "ttl-mode";
inline constexpr std::string_view kAttrDscpMode = "dscp-mode";
inline constexpr std::string_view kAttrEcnMode = "ecn-mode";
inline constexpr std::string_view kAttrTerminationType = "termination-type";
inline constexpr std::string_view kAttrUnderlayIntfId = "underlay-intf-id";

// Sub-keyword that follows destination/source to introduce a prefix length.
inline constexpr std::string_view kMaskKeyword = "mask";

std::string toLower(const std::string& s);

// "encap" or "decap" for the given tunnel direction.
std::string directionName(TunnelType type);

cfg::TunnelMode parseTunnelMode(const std::string& s);
cfg::TunnelTerminationType parseTerminationType(const std::string& s);

/*
 * Parse "<tunnel-id> [<attr> <value> ...]" into a tunnel ID and a flat map of
 * attribute -> value. Only attributes present in allowedAttrs are accepted;
 * masks are stored as "<attr>-mask". Throws std::invalid_argument on any
 * malformed input (missing ID, unknown attr, missing value, bad IPv6, etc.).
 */
void parseTunnelConfigArgs(
    const std::vector<std::string>& v,
    const std::unordered_set<std::string>& allowedAttrs,
    std::string& tunnelId,
    std::map<std::string, std::string>& attrs);

// Shell completion for the grammar parseTunnelConfigArgs() accepts.
std::vector<std::string> completeTunnelConfigArgs(
    const std::vector<std::string>& typed,
    const std::unordered_set<std::string>& allowedAttrs);

/*
 * Find-or-create the tunnel with id `tunnelId` in `swConfig`, stamp it with
 * `tunnelType`, write each attribute from `attrs` onto it, and return one
 * human-readable "<attr>=<value>" string per applied attribute.
 *
 * Throws std::invalid_argument if the tunnel already exists with a different
 * direction (encap vs decap), if underlay-intf-id names an interface that is
 * not present in the staged config, or if the post-apply tunnel would be
 * missing a field the agent hard-requires (destination and underlay-intf-id
 * for both directions, source for encap and for p2p-terminated decap).
 */
std::vector<std::string> applyTunnelConfig(
    cfg::SwitchConfig& swConfig,
    TunnelType tunnelType,
    const std::string& tunnelId,
    const std::map<std::string, std::string>& attrs);

/*
 * Full handler body shared by the encap and decap config subcommands: apply
 * `attrs` to the tunnel in the active ConfigSession's staged agent config,
 * persist the session, and return the user-facing result message.
 */
std::string configureTunnel(
    TunnelType tunnelType,
    const std::string& tunnelId,
    const std::map<std::string, std::string>& attrs);

/*
 * Common arg type for the encap/decap config subcommands: a tunnel ID plus
 * parsed (attr, value) pairs. The direction-specific leaf classes only
 * supply their allowed-attribute set.
 */
class TunnelIpInIpConfigArgsBase
    : public utils::BaseObjectArgType<std::string> {
 public:
  TunnelIpInIpConfigArgsBase(
      std::vector<std::string> v,
      const std::unordered_set<std::string>& allowedAttrs) {
    parseTunnelConfigArgs(v, allowedAttrs, tunnelId_, attrs_);
    data_ = std::move(v);
  }

  const std::string& getTunnelId() const {
    return tunnelId_;
  }

  const std::map<std::string, std::string>& getAttrs() const {
    return attrs_;
  }

  bool hasAttrs() const {
    return !attrs_.empty();
  }

  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;

 private:
  std::string tunnelId_;
  std::map<std::string, std::string> attrs_;
};

} // namespace facebook::fboss::tunnel_utils
