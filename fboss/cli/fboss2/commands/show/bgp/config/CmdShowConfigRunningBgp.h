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

#include <folly/json/dynamic.h>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/config/CmdShowConfigTraits.h"

namespace facebook::fboss {

class CmdShowConfigRunningBgp : public CmdHandler<
                                    CmdShowConfigRunningBgp,
                                    CmdShowConfigRunningBgpTraits> {
 public:
  using RetType = CmdShowConfigRunningBgpTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(RetType& bgpConfig, std::ostream& out = std::cout);

  /*
   * Canned, synthetic model for the CLI reference wiki. Every value here is
   * invented.
   *
   * This header is built into the open-source FBOSS distribution and the
   * rendered sample is published to the CLI reference wiki, so it must not
   * carry a real switch's routing configuration - community values, policy or
   * peer-group names, and AS numbers are all Business Confidential. AS numbers
   * are sample values from the RFC 6996 private-use range and every name is
   * obviously an example.
   *
   * The KEYS, by contrast, are the real thrift field names: the internal
   * implementation renders BgpConfig through
   * thrift::to_dynamic(..., PORTABLE), so these are what an operator greps for
   * in real output. `policies` is a BgpPolicies struct keyed by
   * bgp_policy_statements, and PeerGroup spells its fields
   * remote_as_4_byte / ingress_policy_name / egress_policy_name.
   *
   * No switch is contacted.
   */
  static RetType sampleModel() {
    return folly::dynamic::object(
        "bgp_setting_config",
        folly::dynamic::object("enable_egress_queue_backpressure", true)(
            "features",
            folly::dynamic::array("sample_feature_a", "sample_feature_b")))(
        "communities",
        folly::dynamic::array(
            folly::dynamic::object("name", "AS65108.AGGREGATE.GLOBAL")(
                "description", "Global public aggregates")(
                "communities", folly::dynamic::array("65108:814")),
            folly::dynamic::object("name", "AS65108.DEFAULT")(
                "description", "Default routes")(
                "communities", folly::dynamic::array("65108:337"))))(
        "localprefs",
        folly::dynamic::array(
            folly::dynamic::object("name", "SAMPLE_LOCALPREF_100")(
                "localpref", 100),
            folly::dynamic::object("name", "SAMPLE_LOCALPREF_20")(
                "localpref", 20)))(
        "peer_groups",
        folly::dynamic::array(
            folly::dynamic::object("name", "SAMPLE_UPLINK_GROUP")(
                "remote_as_4_byte", 65221)(
                "ingress_policy_name", "SAMPLE_UPLINK_IN")(
                "egress_policy_name", "SAMPLE_UPLINK_OUT")))(
        "policies",
        folly::dynamic::object(
            "bgp_policy_statements",
            folly::dynamic::array(
                folly::dynamic::object("name", "SAMPLE_UPLINK_IN")(
                    "policy_version", "1")(
                    "policy_entries",
                    folly::dynamic::array(
                        folly::dynamic::object("name", "SAMPLE_ACCEPT_TERM")(
                            "term_miss_action", "NEXT_TERM"))))));
  }
};

} // namespace facebook::fboss
