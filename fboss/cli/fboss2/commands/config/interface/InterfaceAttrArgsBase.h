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

#include <cctype>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

// Single source of truth for the lldp-expected-* attribute names and their
// LLDPTag mapping, shared by `config interface` and `delete interface`.
inline const std::vector<std::pair<std::string, cfg::LLDPTag>>&
lldpAttrToTag() {
  static const std::vector<std::pair<std::string, cfg::LLDPTag>> kAttrToTag = {
      {"lldp-expected-value", cfg::LLDPTag::PORT},
      {"lldp-expected-chassis", cfg::LLDPTag::CHASSIS},
      {"lldp-expected-ttl", cfg::LLDPTag::TTL},
      {"lldp-expected-port-desc", cfg::LLDPTag::PORT_DESC},
      {"lldp-expected-system-name", cfg::LLDPTag::SYSTEM_NAME},
      {"lldp-expected-system-desc", cfg::LLDPTag::SYSTEM_DESC},
  };
  return kAttrToTag;
}

// Maps an lldp-expected-* attribute name to its LLDPTag enum value.
// Returns nullopt if attr is not a recognised lldp-expected-* attribute.
inline std::optional<cfg::LLDPTag> lldpTagForAttr(const std::string& attr) {
  for (const auto& [name, tag] : lldpAttrToTag()) {
    if (name == attr) {
      return tag;
    }
  }
  return std::nullopt;
}

// lldp-expected-* attribute names, in declaration order.
inline std::vector<std::string> lldpAttrNames() {
  std::vector<std::string> names;
  names.reserve(lldpAttrToTag().size());
  for (const auto& [name, tag] : lldpAttrToTag()) {
    names.push_back(name);
  }
  return names;
}

/*
 * Common base for CLI argument types that parse a token list of the form
 *   <port-list> [<attr> [<value>] ...]
 * into an InterfaceList plus (attribute, value) pairs.
 *
 * The generic token parsing lives in utils::MultiArgsConfigType; this layer
 * only adds the interface-specific pieces: resolving the leading object names
 * into an InterfaceList and exposing it, and pinning objectKind to "interface".
 * Concrete arg types (e.g. config / delete interface) pass their known /
 * valueless attribute sets and error-message nouns through the constructor.
 */
class InterfaceAttrArgsBase : public utils::MultiArgsConfigType {
 public:
  /* Get the resolved interfaces. */
  const utils::InterfaceList& getInterfaces() const {
    return interfaces_;
  }

  /* Implicit conversion to InterfaceList so that child commands can accept
   * const InterfaceList& without knowing about the concrete arg type. */
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ operator const utils::InterfaceList&() const {
    return interfaces_;
  }

 protected:
  InterfaceAttrArgsBase(
      std::unordered_set<std::string> knownAttrs,
      std::unordered_set<std::string> valuelessAttrs,
      std::string attrKind,
      std::string validAttrs)
      : utils::MultiArgsConfigType(
            utils::MultiArgsConfigType::Spec{
                std::move(knownAttrs),
                std::move(valuelessAttrs),
                "interface",
                std::move(attrKind),
                std::move(validAttrs)}),
        interfaces_(std::vector<std::string>{}) {}

  utils::InterfaceList interfaces_;
};

} // namespace facebook::fboss
