/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

include "fboss/agent/hw/config/asic_config_v2.thrift"

package "facebook.com/fboss/configs/platforms/generic/forwarding_stack"

namespace cpp2 facebook.fboss.configgen

enum ServiceType {
  // Generate an FBOSS AgentConfig.
  AGENT = 1,
}

enum ConfigProfileType {
  // Production-like baseline configuration.
  DEFAULT = 1,
  // Configuration used by FBOSS hardware tests.
  HW_TEST = 2,
  // Configuration used by FBOSS hardware benchmarks.
  BENCHMARK = 3,
  // Configuration used by FBOSS link tests.
  LINK_TEST = 4,
}

// Uses an existing ASIC config file instead of a generated ASIC-v3 config.
struct AsicConfigOverrideOption {
  // Path to the ASIC config file. Relative to the FBOSS root by default.
  // Checked-in manifests must use relative paths; absolute paths are for
  // local manifests that refer to files outside the FBOSS root.
  1: string asicConfigFile;

  // Format of asicConfigFile. Required.
  2: asic_config_v2.AsicConfigType asicConfigType;

  // Whether asicConfigFile is relative to the FBOSS root.
  3: bool relativeToFbossRoot = true;
}

struct ConfigOption {
  // Selects the generation behavior and forms the base output filename.
  1: ConfigProfileType profile;

  // Optional output-name suffix used to declare more than one config for the
  // same profile. The output is <profile>[_<variant>].conf.
  2: optional string variant;

  // Selects a named ASIC-v3 config variant for this output. When omitted, the
  // generator uses the profile name as the ASIC config variant.
  3: optional string asicConfigVariant;

  // Selects the extensionless CSV filename under
  // forwarding_stack/link_topology/. When set, the topology determines the
  // enabled port graph, port profiles, and expected LLDP peers.
  4: optional string linkTopologyVariant;

  // Selects a directory under variants/ whose generated platform mapping
  // artifacts should be used for this output.
  5: optional string platformMappingVariant;

  // Uses an existing ASIC config file for this output. Cannot be combined with
  // asicConfigVariant.
  6: optional AsicConfigOverrideOption asicConfigOverride;
}

struct ConfigGenerationSpec {
  // Declares the config outputs for each supported service. Every ConfigOption
  // in a service's list produces one generated config.
  1: map<ServiceType, list<ConfigOption>> serviceToOptions = {};
}
