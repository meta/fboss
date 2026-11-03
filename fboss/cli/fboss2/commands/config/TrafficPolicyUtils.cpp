/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/TrafficPolicyUtils.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/IPAddress.h>
#include <folly/String.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace facebook::fboss::traffic_policy {

namespace {

constexpr std::string_view kActionSendToQueue = "send-to-queue";
constexpr std::string_view kActionSetDscp = "set-dscp";
constexpr std::string_view kActionSetTc = "set-tc";
constexpr std::string_view kActionMirrorIngress = "mirror-ingress";
constexpr std::string_view kActionMirrorEgress = "mirror-egress";
constexpr std::string_view kActionCounter = "counter";
constexpr std::string_view kActionTrapToCpu = "trap-to-cpu";
constexpr std::string_view kActionCopyToCpu = "copy-to-cpu";
constexpr std::string_view kActionRedirect = "redirect";
constexpr std::string_view kActionRedirectNexthopKeyword = "nexthop";
constexpr std::string_view kActionUserDefinedTrap = "user-defined-trap";
constexpr std::string_view kActionFlowlet = "flowlet";
constexpr std::string_view kActionEcmpHash = "ecmp-hash";

constexpr int64_t kQueueIdMax = 32767; // i16 QueueMatchAction
constexpr int64_t kDscpMax = 63; // 6-bit codepoint
// SetTcAction.tcValue is a thrift byte. Deliberately not capped at 7: CPU
// policies in shipped configs use tc 9 alongside CPU queue 9, so an "8 traffic
// classes" range would reject real configuration. The per-ASIC cap is enforced
// by the agent at apply time.
constexpr int64_t kTcMax = 127;

int64_t parseIntInRange(
    std::string_view key,
    const std::string& s,
    int64_t min,
    int64_t max) {
  int64_t parsed = 0;
  try {
    parsed = folly::to<int64_t>(s);
  } catch (const std::exception&) {
    throw std::invalid_argument(
        fmt::format("Value for '{}' must be an integer, got '{}'", key, s));
  }
  if (parsed < min || parsed > max) {
    throw std::invalid_argument(
        fmt::format(
            "Value for '{}' out of range [{}, {}], got {}",
            key,
            min,
            max,
            parsed));
  }
  return parsed;
}

std::string requireName(std::string_view key, const std::string& name) {
  if (name.empty()) {
    throw std::invalid_argument(
        fmt::format("Action '{}' requires a non-empty name", key));
  }
  return name;
}

// A malformed address would otherwise reach the agent, where
// AclNexthopHandler::resolveActionNexthops constructs a folly::IPAddress from
// it mid-apply and throws there instead of here.
std::string requireIpAddress(std::string_view key, const std::string& ip) {
  try {
    (void)folly::IPAddress{ip};
  } catch (const std::exception& e) {
    throw std::invalid_argument(
        fmt::format(
            "Action '{}' expects an IP address, got '{}': {}",
            key,
            ip,
            e.what()));
  }
  return ip;
}

// trap-to-cpu and copy-to-cpu are separate keywords over one toCpuAction
// field, so clearing it unconditionally would let a delete of either keyword
// wipe the other one's action and still report success.
bool resetToCpuAction(cfg::MatchAction& ma, cfg::ToCpuAction which) {
  if (ma.toCpuAction().has_value() && *ma.toCpuAction() == which) {
    ma.toCpuAction().reset();
    return true;
  }
  return false;
}

// One action keyword: its value arity, a form string for --help and errors, a
// setter, and a reset used by the delete path. Keeping set and reset in the
// same row is what stops the two verbs drifting apart.
struct ActionRow {
  std::string_view key;
  // Exactly this many tokens must follow the action keyword. Every action has
  // a fixed arity, and the setters below index their values positionally, so
  // there is deliberately no optional-value form.
  std::size_t requiredExtraArgsNum;
  std::string_view form;
  void (*set)(cfg::MatchAction&, const std::vector<std::string>&);
  // Reports whether the action was present, and clears it.
  bool (*reset)(cfg::MatchAction&);
};

const std::vector<ActionRow>& actionRows() {
  static const std::vector<ActionRow> kRows = {
      {kActionSendToQueue,
       1,
       "<queue-id>",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         cfg::QueueMatchAction q;
         q.queueId() = static_cast<int16_t>(
             parseIntInRange(kActionSendToQueue, v[0], 0, kQueueIdMax));
         ma.sendToQueue() = q;
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.sendToQueue().has_value();
         ma.sendToQueue().reset();
         return had;
       }},
      {kActionSetDscp,
       1,
       "<0-63>",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         cfg::SetDscpMatchAction d;
         d.dscpValue() = static_cast<int8_t>(
             parseIntInRange(kActionSetDscp, v[0], 0, kDscpMax));
         ma.setDscp() = d;
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.setDscp().has_value();
         ma.setDscp().reset();
         return had;
       }},
      {kActionSetTc,
       1,
       "<0-127>",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         cfg::SetTcAction t;
         t.tcValue() = static_cast<int8_t>(
             parseIntInRange(kActionSetTc, v[0], 0, kTcMax));
         ma.setTc() = t;
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.setTc().has_value();
         ma.setTc().reset();
         return had;
       }},
      {kActionMirrorIngress,
       1,
       "<mirror-name>",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         ma.ingressMirror() = requireName(kActionMirrorIngress, v[0]);
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.ingressMirror().has_value();
         ma.ingressMirror().reset();
         return had;
       }},
      {kActionMirrorEgress,
       1,
       "<mirror-name>",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         ma.egressMirror() = requireName(kActionMirrorEgress, v[0]);
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.egressMirror().has_value();
         ma.egressMirror().reset();
         return had;
       }},
      {kActionCounter,
       1,
       "<counter-name>",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         ma.counter() = requireName(kActionCounter, v[0]);
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.counter().has_value();
         ma.counter().reset();
         return had;
       }},
      {kActionTrapToCpu,
       0,
       "",
       [](cfg::MatchAction& ma, const std::vector<std::string>&) {
         ma.toCpuAction() = cfg::ToCpuAction::TRAP;
       },
       [](cfg::MatchAction& ma) {
         return resetToCpuAction(ma, cfg::ToCpuAction::TRAP);
       }},
      {kActionCopyToCpu,
       0,
       "",
       [](cfg::MatchAction& ma, const std::vector<std::string>&) {
         ma.toCpuAction() = cfg::ToCpuAction::COPY;
       },
       [](cfg::MatchAction& ma) {
         return resetToCpuAction(ma, cfg::ToCpuAction::COPY);
       }},
      {kActionRedirect,
       2,
       "nexthop <ip>",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         if (v[0] != kActionRedirectNexthopKeyword) {
           throw std::invalid_argument(
               fmt::format(
                   "Action 'redirect' expects keyword '{}', got '{}'",
                   kActionRedirectNexthopKeyword,
                   v[0]));
         }
         cfg::RedirectToNextHopAction rd;
         cfg::RedirectNextHop nh;
         nh.ip() = requireIpAddress(kActionRedirect, v[1]);
         rd.redirectNextHops()->push_back(std::move(nh));
         ma.redirectToNextHop() = std::move(rd);
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.redirectToNextHop().has_value();
         ma.redirectToNextHop().reset();
         return had;
       }},
      {kActionUserDefinedTrap,
       1,
       "<queue-id>",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         cfg::UserDefinedTrapAction t;
         t.queueId() = static_cast<int16_t>(
             parseIntInRange(kActionUserDefinedTrap, v[0], 0, kQueueIdMax));
         ma.userDefinedTrap() = t;
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.userDefinedTrap().has_value();
         ma.userDefinedTrap().reset();
         return had;
       }},
      {kActionFlowlet,
       1,
       "forward|disable",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         if (v[0] == "forward") {
           ma.flowletAction() = cfg::FlowletAction::FORWARD;
         } else if (v[0] == "disable") {
           ma.flowletAction() = cfg::FlowletAction::DISABLE;
         } else {
           throw std::invalid_argument(
               fmt::format(
                   "Action 'flowlet' expects 'forward' or 'disable', got '{}'",
                   v[0]));
         }
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.flowletAction().has_value();
         ma.flowletAction().reset();
         return had;
       }},
      {kActionEcmpHash,
       1,
       "flowlet-quality|per-packet-quality|fixed-assignment|per-packet-random",
       [](cfg::MatchAction& ma, const std::vector<std::string>& v) {
         static const std::vector<
             std::pair<std::string_view, cfg::SwitchingMode>>
             kModes = {
                 {"flowlet-quality", cfg::SwitchingMode::FLOWLET_QUALITY},
                 {"per-packet-quality", cfg::SwitchingMode::PER_PACKET_QUALITY},
                 {"fixed-assignment", cfg::SwitchingMode::FIXED_ASSIGNMENT},
                 {"per-packet-random", cfg::SwitchingMode::PER_PACKET_RANDOM},
             };
         for (const auto& [name, mode] : kModes) {
           if (v[0] == name) {
             cfg::SetEcmpHashAction a;
             a.switchingMode() = mode;
             ma.ecmpHashAction() = a;
             return;
           }
         }
         std::vector<std::string> names;
         names.reserve(kModes.size());
         for (const auto& [name, mode] : kModes) {
           names.emplace_back(name);
         }
         throw std::invalid_argument(
             fmt::format(
                 "Action 'ecmp-hash' expects one of: {}; got '{}'",
                 folly::join(", ", names),
                 v[0]));
       },
       [](cfg::MatchAction& ma) {
         bool had = ma.ecmpHashAction().has_value();
         ma.ecmpHashAction().reset();
         return had;
       }},
  };
  return kRows;
}

const ActionRow* findActionRow(std::string_view key) {
  for (const auto& row : actionRows()) {
    if (row.key == key) {
      return &row;
    }
  }
  return nullptr;
}

const ActionRow& requireActionRow(std::string_view key) {
  const auto* row = findActionRow(key);
  if (row == nullptr) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid action-type '{}': must be one of: {}",
            key,
            actionKeysCsv()));
  }
  return *row;
}

} // namespace

namespace {
constexpr std::string_view kSubCmdMatch = "match";
constexpr std::string_view kSubCmdAction = "action";
} // namespace

std::string configHelpText() {
  static const std::string kText =
      "match <rule-name> action <action-type> [<value>] where <action-type> "
      "is one of: " +
      actionKeysCsv() +
      ". trap-to-cpu and copy-to-cpu take no value; "
      "'redirect' takes 'nexthop <ip>'.";
  return kText;
}

TrafficPolicyArgs::TrafficPolicyArgs(std::vector<std::string> v) {
  if (v.size() < 4) {
    throw std::invalid_argument(
        fmt::format(
            "Expected {} <rule-name> {} <action-type> [<value>], got {} "
            "argument(s)",
            kSubCmdMatch,
            kSubCmdAction,
            v.size()));
  }
  if (v[0] != kSubCmdMatch) {
    throw std::invalid_argument(
        fmt::format(
            "Expected '{}' as first token, got '{}'", kSubCmdMatch, v[0]));
  }
  if (v[1].empty()) {
    throw std::invalid_argument("rule-name must not be empty");
  }
  // The delete command models this grammar as subcommand tree nodes, where
  // CLI11 classifies a bare token naming a child subcommand ("action") before
  // filling positionals. A rule with that name would be configurable here but
  // impossible to delete, so reject it up front.
  if (v[1] == kSubCmdAction) {
    throw std::invalid_argument(
        fmt::format(
            "rule-name must not be '{}': it collides with the '{}' "
            "subcommand of the delete command tree",
            kSubCmdAction,
            kSubCmdAction));
  }
  if (v[2] != kSubCmdAction) {
    throw std::invalid_argument(
        fmt::format(
            "Expected '{}' between rule name and action type, got '{}'",
            kSubCmdAction,
            v[2]));
  }
  matcherName_ = v[1];
  actionTokens_ = std::vector<std::string>(v.begin() + 3, v.end());
  data_ = std::move(v);
}

MatcherName::MatcherName(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument("matcher-name is required");
  }
  if (v.size() != 1) {
    throw std::invalid_argument(
        fmt::format("Expected exactly one matcher-name, got {}", v.size()));
  }
  if (v[0].empty()) {
    throw std::invalid_argument("matcher-name must not be empty");
  }
  data_ = std::move(v);
}

MatchActionArg::MatchActionArg(std::vector<std::string> v) {
  if (v.size() != 1) {
    throw std::invalid_argument(
        fmt::format("Expected exactly one action-type, got {}", v.size()));
  }
  validateActionType(v[0]);
  data_ = std::move(v);
}

std::string_view policyName(PolicyKind kind) {
  return kind == PolicyKind::Cpu ? "copp" : "data-plane";
}

cfg::TrafficPolicyConfig& policyFor(
    cfg::SwitchConfig& swConfig,
    PolicyKind kind) {
  if (kind == PolicyKind::DataPlane) {
    if (!swConfig.dataPlaneTrafficPolicy()) {
      swConfig.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig{};
    }
    return *swConfig.dataPlaneTrafficPolicy();
  }
  if (!swConfig.cpuTrafficPolicy()) {
    swConfig.cpuTrafficPolicy() = cfg::CPUTrafficPolicyConfig{};
  }
  auto& cpu = *swConfig.cpuTrafficPolicy();
  if (!cpu.trafficPolicy()) {
    cpu.trafficPolicy() = cfg::TrafficPolicyConfig{};
  }
  return *cpu.trafficPolicy();
}

cfg::MatchAction& upsertMatcher(
    cfg::TrafficPolicyConfig& policy,
    const std::string& matcher) {
  auto& list = *policy.matchToAction();
  auto it = std::find_if(
      list.begin(), list.end(), [&](const cfg::MatchToAction& mta) {
        return *mta.matcher() == matcher;
      });
  if (it == list.end()) {
    cfg::MatchToAction fresh;
    fresh.matcher() = matcher;
    list.push_back(std::move(fresh));
    it = std::prev(list.end());
  }
  return *it->action();
}

void assertNotInOtherPolicy(
    const cfg::SwitchConfig& swConfig,
    const std::string& matcher,
    PolicyKind kind) {
  const std::vector<cfg::MatchToAction>* other = nullptr;
  if (kind == PolicyKind::Cpu) {
    if (auto dataPlane = swConfig.dataPlaneTrafficPolicy()) {
      other = &*dataPlane->matchToAction();
    }
  } else if (auto cpu = swConfig.cpuTrafficPolicy()) {
    if (auto cpuPolicy = cpu->trafficPolicy()) {
      other = &*cpuPolicy->matchToAction();
    }
  }
  if (other == nullptr) {
    return;
  }
  auto it = std::find_if(
      other->begin(), other->end(), [&](const cfg::MatchToAction& mta) {
        return *mta.matcher() == matcher;
      });
  if (it != other->end()) {
    throw std::runtime_error(
        fmt::format(
            "Rule '{}' already has actions under the {} traffic policy. A rule "
            "resolves to one policy only (the CPU one wins), so the {} entry "
            "would never take effect; delete the other one first",
            matcher,
            policyName(
                kind == PolicyKind::Cpu ? PolicyKind::DataPlane
                                        : PolicyKind::Cpu),
            policyName(kind)));
  }
}

std::string actionKeysCsv() {
  std::string out;
  for (const auto& row : actionRows()) {
    if (!out.empty()) {
      out += ", ";
    }
    out += row.key;
  }
  return out;
}

void validateActionType(std::string_view actionType) {
  requireActionRow(actionType);
}

std::string applyAction(
    cfg::SwitchConfig& swConfig,
    PolicyKind kind,
    const std::string& matcher,
    const std::vector<std::string>& actionTokens) {
  if (actionTokens.empty()) {
    throw std::invalid_argument(
        fmt::format("Expected an action-type, one of: {}", actionKeysCsv()));
  }
  const auto& row = requireActionRow(actionTokens[0]);
  std::vector<std::string> values(actionTokens.begin() + 1, actionTokens.end());
  if (values.size() != row.requiredExtraArgsNum) {
    throw std::invalid_argument(
        fmt::format(
            "Action '{}' expects '{}', got {} value token(s)",
            row.key,
            row.form,
            values.size()));
  }

  assertNotInOtherPolicy(swConfig, matcher, kind);
  auto& action = upsertMatcher(policyFor(swConfig, kind), matcher);
  row.set(action, values);

  return fmt::format(
      "Set {} traffic-policy action '{}'{}{} on matcher '{}'",
      policyName(kind),
      row.key,
      values.empty() ? "" : " ",
      folly::join(" ", values),
      matcher);
}

std::string deleteAction(
    cfg::SwitchConfig& swConfig,
    PolicyKind kind,
    const std::string& matcher,
    const std::string& actionType) {
  const auto& row = requireActionRow(actionType);

  auto& policy = policyFor(swConfig, kind);
  auto& list = *policy.matchToAction();
  auto it = std::find_if(
      list.begin(), list.end(), [&](const cfg::MatchToAction& mta) {
        return *mta.matcher() == matcher;
      });
  if (it == list.end()) {
    throw std::runtime_error(
        fmt::format(
            "No {} traffic-policy entry for matcher '{}'",
            policyName(kind),
            matcher));
  }
  // Deleting an action that was never set is reported, not an error. That is
  // the behaviour the copp verb already shipped, and it keeps the command
  // safe to re-run from automation.
  bool wasPresent = row.reset(*it->action());

  // An entry with no actions left is dead weight, and worse than that:
  // checkTrafficPolicyAclsExistInConfig still requires the AclEntry it names
  // to exist, so an empty leftover keeps a constraint alive for no benefit.
  // Check this even when the requested action was already absent: an empty
  // entry can pre-exist (e.g. from a hand-edited config) and should not be
  // left behind either.
  bool erased = false;
  if (*it->action() == cfg::MatchAction{}) {
    list.erase(it);
    erased = true;
  }

  if (!wasPresent && !erased) {
    return fmt::format(
        "Action '{}' is already absent for matcher '{}'", row.key, matcher);
  }
  if (!wasPresent) {
    return fmt::format(
        "Action '{}' was already absent for matcher '{}'; removed the empty "
        "{} traffic-policy entry",
        row.key,
        matcher,
        policyName(kind));
  }
  return fmt::format(
      "Successfully deleted action '{}' from {} traffic-policy matcher '{}'",
      row.key,
      policyName(kind),
      matcher);
}

} // namespace facebook::fboss::traffic_policy
