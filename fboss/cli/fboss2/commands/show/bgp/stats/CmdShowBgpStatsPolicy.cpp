/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/stats/CmdShowBgpStatsPolicy.h"

#include <fmt/core.h>
#include <iostream>

#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"

namespace facebook::fboss {

CmdShowBgpStatsPolicy::RetType CmdShowBgpStatsPolicy::queryClient(
    const HostInfo& hostInfo) {
  TPolicyStats policy_stats;
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  client->sync_getPolicyStats(policy_stats);
  return policy_stats;
}

void CmdShowBgpStatsPolicy::printOutput(
    const RetType& policy_stats,
    std::ostream& out) {
  out << "BGP policy statistics:";
  for (const auto& stats : policy_stats.policy_statement_stats().value()) {
    const auto statement_prefix_count =
        folly::copy(stats.prefix_hit_count().value());
    out << fmt::format("\n Policy Name: {}", stats.name().value());
    out << fmt::format(
        "\n\t Number of executions: {}",
        folly::copy(stats.num_of_runs().value()));
    out << fmt::format(
        "\n\t Number of hits: {} prefixes", statement_prefix_count);
    out << fmt::format(
        "\n\t Average time of execution: {}us",
        folly::copy(stats.avg_time().value()));
    out << fmt::format(
        "\n\t Maximum time of execution: {}us",
        folly::copy(stats.max_time().value()));

    out << "\n\t Term  Description                                        Hits/Misses";

    int term_number = 1, hit_count_so_far = 0;
    for (const auto& term_stats : stats.term_stats().value()) {
      const auto& term_prefix_count =
          folly::copy(term_stats.prefix_hit_count().value());
      out << fmt::format(
          "\n\t {:<5} {:<48} {:>6}/{:<6}",
          term_number,
          term_stats.description().value(),
          term_prefix_count,
          (statement_prefix_count - term_prefix_count - hit_count_so_far));

      ++term_number;
      hit_count_so_far += term_prefix_count;
    }
    if (hit_count_so_far <= statement_prefix_count) {
      out << fmt::format(
          "\n\t {:<5} {:<48} {:>6}/0\n",
          term_number,
          "Default deny (Implicit)",
          statement_prefix_count - hit_count_so_far);
    }
  }
}

std::string_view CmdShowBgpStatsPolicyTraits::description() {
  return "Displays per-policy execution statistics: for each configured policy statement, how many times it ran, how many prefixes it matched, and its average and maximum execution time in microseconds, followed by a per-term breakdown of hits and misses. Terms are numbered in evaluation order, and the misses column for a term is the prefixes that reached the policy but had already been consumed by an earlier term or fell through past this one - so hits accumulate down the list rather than each row being independent. A trailing 'Default deny (Implicit)' row accounts for everything no explicit term matched; a large count there usually means the policy is not matching what its author intended. A policy with zero executions is configured but never invoked on this switch. Use the timing columns to find a policy that is expensive enough to slow convergence.";
}

CmdShowBgpStatsPolicy::RetType CmdShowBgpStatsPolicy::sampleModel() {
  using facebook::neteng::routing::policy::thrift::TPolicyStatementStats;
  using facebook::neteng::routing::policy::thrift::TPolicyTermStats;

  auto term = [](const std::string& description, int64_t prefixHits) {
    TPolicyTermStats stats;
    stats.description() = description;
    stats.prefix_hit_count() = prefixHits;
    return stats;
  };

  // An origination policy that has matched nothing on this switch, showing the
  // implicit default-deny row printOutput appends.
  TPolicyStatementStats originate;
  originate.name() = "SAMPLE_ORIGINATE_LOCAL";
  originate.num_of_runs() = 0;
  originate.prefix_hit_count() = 0;
  originate.avg_time() = 0;
  originate.max_time() = 0;
  originate.term_stats() = {term(
      "(TYPE-1) Unconditionally originate the route and attach IBN tags", 0)};

  // An ingress policy that is actively matching, so the per-term hits and the
  // running miss count are both non-trivial.
  TPolicyStatementStats propagate;
  propagate.name() = "SAMPLE_UPLINK_IN";
  propagate.num_of_runs() = 1464;
  propagate.prefix_hit_count() = 1464;
  propagate.avg_time() = 3;
  propagate.max_time() = 41;
  propagate.term_stats() = {
      term("Accept default route", 1),
      term("Accept rack private prefixes", 1341)};

  RetType stats;
  stats.policy_statement_stats() = {originate, propagate};
  return stats;
}

} // namespace facebook::fboss
