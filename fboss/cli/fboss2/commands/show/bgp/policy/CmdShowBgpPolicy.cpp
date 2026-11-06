/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/policy/CmdShowBgpPolicy.h"

#include <utility>

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <folly/json/json.h>
#include <thrift/lib/cpp2/FieldRef.h>
#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"

namespace facebook::fboss {

CmdShowBgpPolicy::RetType CmdShowBgpPolicy::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& policyNames) {
  const auto loaded = getLoadedBgpPolicy(hostInfo);
  const auto* source = loaded.source == BgpPolicySource::POLICY_ARTIFACT
      ? "BGP policy"
      : "BGP config";
  if (policyNames.empty()) {
    return folly::dynamic::object("source", source)(
        "policy",
        facebook::thrift::to_dynamic(
            loaded.config, facebook::thrift::dynamic_format::PORTABLE));
  }

  folly::dynamic policy;
  if (auto policies = loaded.config.policies();
      policies &&
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          policies->bgp_policy_statements())) {
    for (const auto& statement : policies->bgp_policy_statements().value()) {
      if (*statement.name() == policyNames.front()) {
        policy = facebook::thrift::to_dynamic(
            statement, facebook::thrift::dynamic_format::PORTABLE);
        break;
      }
    }
  }

  if (policy.isNull()) {
    std::cerr << "Policy '" << policyNames.front() << "' not found"
              << std::endl;
    return folly::dynamic::object;
  }

  return folly::dynamic::object("source", source)("policy", std::move(policy));
}

void CmdShowBgpPolicy::printOutput(
    const RetType& policyConfig,
    std::ostream& out) {
  if (policyConfig.empty()) {
    return;
  }
  out << "Policy source: " << policyConfig["source"].asString() << '\n';
  out << folly::toPrettyJson(policyConfig["policy"]) << '\n';
}

template void CmdHandler<CmdShowBgpPolicy, CmdShowBgpPolicyTraits>::run();

} // namespace facebook::fboss
