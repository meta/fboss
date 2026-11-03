/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

// Action level required for config changes to take effect.
// Used to track the highest impact action needed when committing config
// changes. The levels are generic across services; how a level is carried out
// for a given service (agent warmboot vs plain bgpd restart, ...) is decided by
// FbossServiceUtil::restartService() from the (service, level) pair.
enum ConfigActionLevel {
  // Can be applied with reloadConfig() if the service supports it - default
  HITLESS = 0,
  // Restart the service gracefully, retaining state where the service can:
  // an agent warmboot, or a plain restart for daemons with no warmboot (bgpd).
  SERVICE_RESTART = 1,
  // Restart the service disruptively: an agent coldboot (clears ASIC state).
  DISRUPTIVE_SERVICE_RESTART = 2,
}

// Identifier for different services that can be configured
enum ServiceType {
  AGENT = 1,
  BGP = 2, // The bgpd (BGP++) routing daemon
}

// Metadata stored alongside the session configuration file.
// This metadata tracks state that needs to persist across CLI invocations
// within a single config session.
struct ConfigSessionMetadata {
  // Maps each service to the required action level for pending config changes.
  // Services not in this map default to HITLESS.
  1: map<ServiceType, ConfigActionLevel> action;
  // List of CLI commands executed in this session, in chronological order.
  // Each entry is the full command string (e.g., "config interface eth1/1/1 mtu 9000").
  2: list<string> commands;
  // Git commit SHA that this session is based on. Used to detect if someone
  // else committed changes while this session was in progress.
  3: string base;
  // Validated COOP config paths keyed by service. These let recovery commands
  // use the last known active config when a service is unavailable. Optional
  // for compatibility with metadata written before config path discovery.
  4: optional map<ServiceType, string> currentConfigPaths;
}
