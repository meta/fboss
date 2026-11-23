/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss::configgen {

struct LinkTopologyEntry {
  // Required local endpoint whose profile and transceiver metadata are
  // described by this entry.
  std::string portA;

  // Optional second local endpoint. An omitted value represents an external
  // or otherwise unmodeled peer and does not produce an LLDP expectation. A
  // value equal to portA represents a physical loopback; any other value
  // represents a connection between two local ports.
  std::optional<std::string> portB;

  // Concrete profile applied to portA and to portB when portB is present.
  cfg::PortProfileID profile;

  // Whether the represented connection uses pluggable transceivers. For a
  // two-port entry, this value applies to both local endpoints.
  bool hasTransceiver;

  bool operator==(const LinkTopologyEntry&) const = default;
};

// Entries form a disjoint set of local endpoints: a port may occur in only
// one entry, except that portA and portB may match to describe a self-loop.
using LinkTopology = std::vector<LinkTopologyEntry>;

// Parses the canonical four-column link-topology CSV. An empty portB describes
// a single local endpoint whose peer is not modeled by the topology.
LinkTopology parseLinkTopologyCsv(
    std::string_view contents,
    std::string_view sourceName);

LinkTopology loadLinkTopologyCsv(const std::filesystem::path& path);

// Loads <platformDirectory>/forwarding_stack/link_topology/<variant>.csv.
LinkTopology loadLinkTopologyVariant(
    const std::filesystem::path& platformDirectory,
    std::string_view variant);

} // namespace facebook::fboss::configgen
