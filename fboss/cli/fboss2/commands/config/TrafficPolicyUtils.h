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

#include <string>
#include <string_view>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

/*
 * The CPU and dataplane traffic policies hold the same TrafficPolicyConfig and
 * accept the same MatchActions; they differ only in which SwitchConfig field
 * owns them. So the grammar, the upsert, the guards and the apply/delete logic
 * all live here once, and each command supplies a PolicyKind.
 *
 * Which policy a rule belongs to changes behavior, not just labeling:
 * ThriftConfigApplier::updateAclsImpl looks the rule up in the CPU policy
 * first and only falls back to the dataplane one, and it threads the result
 * into setSendToQueue/setSetTc as `isCoppAcl`. The same queue id therefore
 * means a CPU queue under one policy and a port queue under the other, so a
 * rule cannot simply carry its actions on the AclEntry.
 */
namespace facebook::fboss::traffic_policy {

enum class PolicyKind {
  Cpu, // sw.cpuTrafficPolicy.trafficPolicy
  DataPlane, // sw.dataPlaneTrafficPolicy
};

// Human name for messages and errors: "copp" / "data-plane".
std::string_view policyName(PolicyKind kind);

/*
 * The policy for `kind`, creating the optional field (and, for Cpu, the
 * enclosing CPUTrafficPolicyConfig) if it is unset.
 */
cfg::TrafficPolicyConfig& policyFor(
    cfg::SwitchConfig& swConfig,
    PolicyKind kind);

/*
 * The MatchAction attached to `matcher` in `policy`, appending a MatchToAction
 * for it if none exists yet.
 *
 * Note the append: matchToAction order is ACL priority (updateAclsImpl walks
 * the list assigning cpuPriority++/dataPriority++), so a matcher created here
 * lands at the end and therefore at the lowest precedence within its policy.
 */
cfg::MatchAction& upsertMatcher(
    cfg::TrafficPolicyConfig& policy,
    const std::string& matcher);

/*
 * Throw if `matcher` already has an entry in the *other* policy. A rule
 * matched by both resolves to the CPU one and the dataplane entry is dead
 * config, so refuse rather than write something that will never take effect.
 */
void assertNotInOtherPolicy(
    const cfg::SwitchConfig& swConfig,
    const std::string& matcher,
    PolicyKind kind);

// Comma-separated action keywords, for --help and error text.
std::string actionKeysCsv();

// Throw std::invalid_argument unless `actionType` names a known action. Lets
// arg classes reject a bad keyword at parse time while still checking against
// the one action table.
void validateActionType(std::string_view actionType);

// Help text for both `config <policy> traffic-policy` leaves.
std::string configHelpText();

/*
 * Argument for `config <copp|data-plane> traffic-policy match <rule> action
 * <type> [<value>...]`. Both verbs parse identically, so they share this; the
 * action keyword and its values are validated later by applyAction, against
 * the one action table.
 */
class TrafficPolicyArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ TrafficPolicyArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getMatcherName() const {
    return matcherName_;
  }

  const std::vector<std::string>& getActionTokens() const {
    return actionTokens_;
  }

 private:
  std::string matcherName_;
  std::vector<std::string> actionTokens_;
};

/*
 * Argument for `delete <copp|data-plane> traffic-policy match <rule>`: one
 * non-empty matcher name. Nothing about it is policy-specific, so both delete
 * chains share it.
 */
class MatcherName : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ MatcherName( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getName() const {
    return data_[0];
  }
};

/*
 * Argument for `... match <rule> action <type>`: one action keyword, checked
 * against the same action table the config side uses, so the two verbs cannot
 * drift apart on which keywords exist.
 */
class MatchActionArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ MatchActionArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getActionType() const {
    return data_[0];
  }
};

/*
 * Apply `action <type> [<value>]` to the matcher, or remove that one action
 * from it. Both validate the action keyword and its value, and both throw
 * std::invalid_argument on a bad keyword/value and std::runtime_error when the
 * delete target does not exist. Returns a human-readable summary.
 */
std::string applyAction(
    cfg::SwitchConfig& swConfig,
    PolicyKind kind,
    const std::string& matcher,
    const std::vector<std::string>& actionTokens);

std::string deleteAction(
    cfg::SwitchConfig& swConfig,
    PolicyKind kind,
    const std::string& matcher,
    const std::string& actionType);

} // namespace facebook::fboss::traffic_policy
