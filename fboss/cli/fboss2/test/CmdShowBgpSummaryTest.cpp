/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/ScopeGuard.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/reflection/testing.h> // NOLINT(misc-include-cleaner)
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/show/bgp/summary/CmdShowBgpSummary.h"
#include "fboss/cli/fboss2/commands/show/bgp/summary/gen-cpp2/bgp_summary_types.h"
#include "fboss/cli/fboss2/test/CmdBgpTestUtils.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#ifndef IS_OSS
// Avoid EXPECT_THRIFT_EQ clash with <thrift/lib/cpp2/reflection/testing.h>
#undef EXPECT_THRIFT_EQ
#include "nettools/common/TestUtils.h"
#endif

using namespace ::testing;
using facebook::neteng::fboss::bgp::thrift::TBgpDrainState;
using facebook::neteng::fboss::bgp::thrift::TBgpLocalConfig;
using facebook::neteng::fboss::bgp::thrift::TBgpSession;

namespace facebook::fboss {
// Constants for TBgpLocalConfig
const int kRouterId = 1234;
const int kLocalAS = 6000;
const int kLocalConfedAs = 700;
const bool kUCMPWeights = false;
const bool kEnableUpdateGroup = true;

// Constants for TBgpDrainState
const std::vector<std::string> kDrainedInterfaces = {"eth1/1/1", "eth1/2/1"};

// Constants for the global summary getters. kProcessUptimeSeconds is chosen so
// formatUptime renders the deterministic "1d 1h 1m 1s" string:
// 90061 = 1*86400 + 1*3600 + 1*60 + 1.
const int64_t kProcessUptimeSeconds = 90061;
const int64_t kTotalPrefixCount = 42;
const int64_t kRibVersion = 7;

class CmdShowBgpSummaryTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<TBgpSession> sessions_;
  TBgpLocalConfig localConfig_;
  TBgpDrainState drainState_;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    sessions_ = getQueriedSession();
    localConfig_ = getQueriedConfig();
    drainState_ = getQueriedDrainState();
  }

  std::vector<TBgpSession> getQueriedSession() {
    TBgpSession establishedSession;

    auto establishedPeer = establishedSession.peer();
    establishedPeer->peer_state() = kEstablishedPeerState;
    establishedPeer->remote_as_4_byte() = kEstablishedRemoteAS;
    establishedPeer->graceful() = kEstablishedIsGraceful;

    establishedSession.prepolicy_rcvd_prefix_count() =
        kEstablishedPrePolicyRcvdPrefixCount;
    establishedSession.postpolicy_sent_prefix_count() =
        kEstablishedPostPolicySentPrefixCount;

    establishedSession.peer_addr() = kEstablishedPeerAddress;
    establishedSession.uptime() = kUptime;
    establishedSession.reset_time() = kEstablishedDowntime;
    establishedSession.eor_received() = true;
    establishedSession.eor_sent() = true;
    establishedSession.num_resets() = kEstablishedNumResets;
    establishedSession.description() = kEstablishedDescription;
    establishedSession.peer_bgp_id() = kEstablishedRemoteBgpId;

    TBgpSession idleSession;

    auto idlePeer = idleSession.peer();
    idlePeer->peer_state() = kIdlePeerState;
    idlePeer->remote_as_4_byte() = kIdleRemoteAS;
    idlePeer->graceful() = kIdleIsGraceful;

    idleSession.prepolicy_rcvd_prefix_count() = kIdlePrePolicyRcvdPrefixCount;
    idleSession.postpolicy_sent_prefix_count() = kIdlePostPolicySentPrefixCount;

    idleSession.peer_addr() = kIdlePeerAddress;
    idleSession.uptime() = kUptime;
    idleSession.reset_time() = kIdleDowntime;
    idleSession.eor_received() = false;
    idleSession.eor_sent() = false;
    idleSession.num_resets() = kIdleNumResets;
    idleSession.description() = kIdleDescription;
    idleSession.peer_bgp_id() = kIdleRemoteBgpId;

    return {establishedSession, idleSession};
  }

  TBgpLocalConfig getQueriedConfig() {
    TBgpLocalConfig config;

    config.my_router_id() = kRouterId;
    config.local_as_4_byte() = kLocalAS;
    config.local_confed_as_4_byte() = kLocalConfedAs;
    config.program_ucmp_weights() = kUCMPWeights;
    config.enable_update_group() = kEnableUpdateGroup;

    return config;
  }

  TBgpDrainState getQueriedDrainState() {
    TBgpDrainState drain_state;

    drain_state.drain_state() =
        facebook::bgp::bgp_policy::DrainState::UNDRAINED;
    drain_state.drained_interfaces() = kDrainedInterfaces;

    return drain_state;
  }

  cli::ShowBgpSummaryModel getModelBgpSummary() {
    cli::ShowBgpSummaryModel model;

    model.sessions() = getQueriedSession();
    model.config() = getQueriedConfig();
    model.drain_state() = getQueriedDrainState();
    model.process_uptime_seconds() = kProcessUptimeSeconds;
    model.total_prefix_count() = kTotalPrefixCount;
    model.rib_version() = kRibVersion;

    return model;
  }

  cli::ShowBgpSummaryModel getModelBgpSummaryOldAsn() {
    // TODO(michaeluw): T113736668 deprecate this when we deprecate i32 asns
    // field. To be safe, build from scratch instead of manipulating result of
    // getModelBgpSummary
    cli::ShowBgpSummaryModel model;

    TBgpLocalConfig config;

    config.my_router_id() = kRouterId;
    config.local_as() = kLocalAS;
    config.local_confed_as() = kLocalConfedAs;
    config.program_ucmp_weights() = kUCMPWeights;
    config.enable_update_group() = kEnableUpdateGroup;

    model.config() = std::move(config);

    TBgpSession establishedSession;

    auto establishedPeer = establishedSession.peer();
    establishedPeer->peer_state() = kEstablishedPeerState;
    establishedPeer->remote_as() = kEstablishedRemoteAS;
    establishedPeer->graceful() = kEstablishedIsGraceful;

    establishedSession.prepolicy_rcvd_prefix_count() =
        kEstablishedPrePolicyRcvdPrefixCount;
    establishedSession.postpolicy_sent_prefix_count() =
        kEstablishedPostPolicySentPrefixCount;

    establishedSession.peer_addr() = kEstablishedPeerAddress;
    establishedSession.uptime() = kUptime;
    establishedSession.reset_time() = kEstablishedDowntime;
    establishedSession.eor_received() = true;
    establishedSession.eor_sent() = true;
    establishedSession.num_resets() = kEstablishedNumResets;
    establishedSession.description() = kEstablishedDescription;
    establishedSession.peer_bgp_id() = kEstablishedRemoteBgpId;

    TBgpSession idleSession;

    auto idlePeer = idleSession.peer();
    idlePeer->peer_state() = kIdlePeerState;
    idlePeer->remote_as() = kIdleRemoteAS;
    idlePeer->graceful() = kIdleIsGraceful;

    idleSession.prepolicy_rcvd_prefix_count() = kIdlePrePolicyRcvdPrefixCount;
    idleSession.postpolicy_sent_prefix_count() = kIdlePostPolicySentPrefixCount;

    idleSession.peer_addr() = kIdlePeerAddress;
    idleSession.uptime() = kUptime;
    idleSession.reset_time() = kIdleDowntime;
    idleSession.num_resets() = kIdleNumResets;
    idleSession.description() = kIdleDescription;
    idleSession.peer_bgp_id() = kIdleRemoteBgpId;

    model.sessions() = {std::move(establishedSession), std::move(idleSession)};

    TBgpDrainState drainState;

    drainState.drain_state() = facebook::bgp::bgp_policy::DrainState::DRAINED;
    drainState.drained_interfaces() = kDrainedInterfaces;
    model.drain_state() = std::move(drainState);

    model.process_uptime_seconds() = kProcessUptimeSeconds;
    model.total_prefix_count() = kTotalPrefixCount;
    model.rib_version() = kRibVersion;

    return model;
  }
};

TEST_F(CmdShowBgpSummaryTestFixture, queryClient) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getBgpSessions(_))
      .WillOnce(Invoke([&](auto& entries) { entries = sessions_; }));

  EXPECT_CALL(getMockBgp(), getBgpLocalConfig(_))
      .WillOnce(Invoke([&](auto& entries) { entries = localConfig_; }));

  EXPECT_CALL(getMockBgp(), getDrainState(_))
      .WillOnce(Invoke([&](auto& entries) { entries = drainState_; }));

  EXPECT_CALL(getMockBgp(), getProcessUptimeSeconds())
      .WillOnce(Return(kProcessUptimeSeconds));
  EXPECT_CALL(getMockBgp(), getNumPrefixes())
      .WillOnce(Return(kTotalPrefixCount));
  EXPECT_CALL(getMockBgp(), getRibVersion()).WillOnce(Return(kRibVersion));

  auto results = CmdShowBgpSummary().queryClient(localhost());
  EXPECT_THRIFT_EQ(results, getModelBgpSummary());
}

// Backward compatibility: an older bgpd that predates getProcessUptimeSeconds()
// / getNumPrefixes() makes those thrift calls throw. queryClient must swallow
// the error and leave process_uptime_seconds / total_prefix_count unset, while
// still returning the always-available rib_version.
TEST_F(CmdShowBgpSummaryTestFixture, queryClientOlderDaemonMissingGetters) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getBgpSessions(_))
      .WillOnce(Invoke([&](auto& entries) { entries = sessions_; }));

  EXPECT_CALL(getMockBgp(), getBgpLocalConfig(_))
      .WillOnce(Invoke([&](auto& entries) { entries = localConfig_; }));

  EXPECT_CALL(getMockBgp(), getDrainState(_))
      .WillOnce(Invoke([&](auto& entries) { entries = drainState_; }));

  EXPECT_CALL(getMockBgp(), getRibVersion()).WillOnce(Return(kRibVersion));

  // Simulate the older daemon: the first guarded getter throws, so the CLI
  // never reaches getNumPrefixes(). The thrown type is normalized to a thrift
  // exception across the RPC boundary; queryClient catches it as
  // std::exception.
  EXPECT_CALL(getMockBgp(), getProcessUptimeSeconds())
      .WillOnce(Throw(std::runtime_error("unimplemented on older daemon")));

  auto results = CmdShowBgpSummary().queryClient(localhost());

  EXPECT_FALSE(results.process_uptime_seconds().has_value());
  EXPECT_FALSE(results.total_prefix_count().has_value());
  EXPECT_EQ(kRibVersion, folly::copy(results.rib_version().value()));
}

TEST(CmdShowBgpSummaryFormatUptimeTest, trimsLeadingZeroUnits) {
  // Only seconds elapsed: minutes/hours/days are omitted.
  EXPECT_EQ("0s", CmdShowBgpSummary::formatUptime(0));
  EXPECT_EQ("45s", CmdShowBgpSummary::formatUptime(45));
  // Minutes elapsed: hours/days omitted, seconds kept.
  EXPECT_EQ("5m 30s", CmdShowBgpSummary::formatUptime(330));
  // Hours elapsed: days omitted; interior zero units kept so it stays
  // unambiguous.
  EXPECT_EQ("1h 1m 1s", CmdShowBgpSummary::formatUptime(3661));
  EXPECT_EQ("3h 0m 5s", CmdShowBgpSummary::formatUptime(10805));
  // Days elapsed: all units shown.
  EXPECT_EQ("1d 0h 0m 0s", CmdShowBgpSummary::formatUptime(86400));
  EXPECT_EQ("1d 1h 1m 1s", CmdShowBgpSummary::formatUptime(90061));
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutput) {
  std::stringstream ss;
  CmdShowBgpSummary().printOutput(getModelBgpSummary(), ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000, Confed ASN - 700\n"
      "BGP Switch Drain State: UNDRAINED\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is ENABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 0, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent, UG - Update Group, UGPS - Update Group Peer State\n\n"
      " Peer     AS    State  GR  PR  PA  PS  UG  UGPS  Uptime    Downtime  Description           Session ID  Flaps \n"
      "----------------------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   0   2   -   -     0h 0m 1s            leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0   -   -               0h 0m 2s  leaf07.p099.f01.lab0  6.7.8.9     33    \n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputWhenConfedAsnIsZero) {
  std::stringstream ss;
  auto model = getModelBgpSummary();
  model.config()->local_confed_as_4_byte() = 0; // zero confed asn
  CmdShowBgpSummary().printOutput(model, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000\n"
      "BGP Switch Drain State: UNDRAINED\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is ENABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 0, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent, UG - Update Group, UGPS - Update Group Peer State\n\n"
      " Peer     AS    State  GR  PR  PA  PS  UG  UGPS  Uptime    Downtime  Description           Session ID  Flaps \n"
      "----------------------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   0   2   -   -     0h 0m 1s            leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0   -   -               0h 0m 2s  leaf07.p099.f01.lab0  6.7.8.9     33    \n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputWithOldAsnField) {
  // TODO(michaeluw): deprecate this when we deprecate i32 asns field
  // Test with only old field present
  // s T113736668
  std::stringstream ss;
  CmdShowBgpSummary().printOutput(getModelBgpSummaryOldAsn(), ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000, Confed ASN - 700\n"
      "BGP Switch Drain State: DRAINED\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is ENABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 0, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent, UG - Update Group, UGPS - Update Group Peer State\n\n"
      " Peer     AS    State  GR  PR  PA  PS  UG  UGPS  Uptime    Downtime  Description           Session ID  Flaps \n"
      "----------------------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   0   2   -   -     0h 0m 1s            leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0   -   -               0h 0m 2s  leaf07.p099.f01.lab0  6.7.8.9     33    \n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputBothAsnFieldPresent) {
  // TODO(michaeluw): deprecate this when we deprecate i32 asns field
  // Test with only new and old field present
  // s T113736668
  std::stringstream ss;
  auto model = getModelBgpSummary();
  model.config()->local_as() = 123;
  model.config()->local_confed_as() = 123;
  CmdShowBgpSummary().printOutput(model, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000, Confed ASN - 700\n"
      "BGP Switch Drain State: UNDRAINED\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is ENABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 0, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent, UG - Update Group, UGPS - Update Group Peer State\n\n"
      " Peer     AS    State  GR  PR  PA  PS  UG  UGPS  Uptime    Downtime  Description           Session ID  Flaps \n"
      "----------------------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   0   2   -   -     0h 0m 1s            leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0   -   -               0h 0m 2s  leaf07.p099.f01.lab0  6.7.8.9     33    \n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputUpdateGroupDisabled) {
  // When update-group is disabled, the UG/UGPS columns and their acronyms are
  // omitted entirely.
  std::stringstream ss;
  auto model = getModelBgpSummary();
  model.config()->enable_update_group() = false;
  CmdShowBgpSummary().printOutput(model, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000, Confed ASN - 700\n"
      "BGP Switch Drain State: UNDRAINED\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is DISABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 0, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent\n\n"
      " Peer     AS    State  GR  PR  PA  PS  Uptime    Downtime  Description           Session ID  Flaps \n"
      "----------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   0   2   0h 0m 1s            leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0             0h 0m 2s  leaf07.p099.f01.lab0  6.7.8.9     33    \n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputWithUpdateGroupAndPaValues) {
  // Update-group id / peer-state and the accepted-prefix count render their
  // actual values (UG/UGPS columns and PA column).
  std::stringstream ss;
  auto model = getModelBgpSummary();
  model.sessions()[0].update_group_id() = 5;
  model.sessions()[0].peer_state_update_group() = "Converged";
  model.sessions()[0].postpolicy_rcvd_prefix_count() = 2;
  model.sessions()[1].update_group_id() = 7;
  model.sessions()[1].peer_state_update_group() = "Pending";
  CmdShowBgpSummary().printOutput(model, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000, Confed ASN - 700\n"
      "BGP Switch Drain State: UNDRAINED\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is ENABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 2, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent, UG - Update Group, UGPS - Update Group Peer State\n\n"
      " Peer     AS    State  GR  PR  PA  PS  UG  UGPS       Uptime    Downtime  Description           Session ID  Flaps \n"
      "---------------------------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   2   2   5   Converged  0h 0m 1s            leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0   7   Pending              0h 0m 2s  leaf07.p099.f01.lab0  6.7.8.9     33    \n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputNoDowntimeColumn) {
  // When no peer has a downtime value, the Downtime column is omitted.
  std::stringstream ss;
  auto model = getModelBgpSummary();
  // Idle peer with num_resets == 0 has never been established -> no downtime.
  model.sessions()[1].num_resets() = 0;
  CmdShowBgpSummary().printOutput(model, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000, Confed ASN - 700\n"
      "BGP Switch Drain State: UNDRAINED\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is ENABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 0, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent, UG - Update Group, UGPS - Update Group Peer State\n\n"
      " Peer     AS    State  GR  PR  PA  PS  UG  UGPS  Uptime    Description           Session ID  Flaps \n"
      "-----------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   0   2   -   -     0h 0m 1s  leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0   -   -               leaf07.p099.f01.lab0  6.7.8.9     0     \n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputDrainStateNotSet) {
  // An unset drain state must print "N/A" rather than crash on .value().
  std::stringstream ss;
  auto model = getModelBgpSummary();
  model.drain_state()->drain_state().reset();
  CmdShowBgpSummary().printOutput(model, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000, Confed ASN - 700\n"
      "BGP Switch Drain State: N/A\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is ENABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 0, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent, UG - Update Group, UGPS - Update Group Peer State\n\n"
      " Peer     AS    State  GR  PR  PA  PS  UG  UGPS  Uptime    Downtime  Description           Session ID  Flaps \n"
      "----------------------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   0   2   -   -     0h 0m 1s            leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0   -   -               0h 0m 2s  leaf07.p099.f01.lab0  6.7.8.9     33    \n\n";

  EXPECT_EQ(output, expectedOutput);
}

#ifndef IS_OSS
// Meta-internal drain states (WARM_DRAINED, SOFT_DRAINED) not available in OSS
TEST_F(CmdShowBgpSummaryTestFixture, printOutputWithDifferentDrainStates) {
  std::stringstream ss;
  auto model = getModelBgpSummary();
  model.drain_state()->drain_state() =
      bgp::bgp_policy::DrainState::WARM_DRAINED;
  CmdShowBgpSummary().printOutput(model, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000, Confed ASN - 700\n"
      "BGP Switch Drain State: WARM_DRAINED\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is ENABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 0, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent, UG - Update Group, UGPS - Update Group Peer State\n\n"
      " Peer     AS    State  GR  PR  PA  PS  UG  UGPS  Uptime    Downtime  Description           Session ID  Flaps \n"
      "----------------------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   0   2   -   -     0h 0m 1s            leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0   -   -               0h 0m 2s  leaf07.p099.f01.lab0  6.7.8.9     33    \n\n";

  EXPECT_EQ(output, expectedOutput);

  model.drain_state()->drain_state() =
      bgp::bgp_policy::DrainState::SOFT_DRAINED;
  ss.str("");
  ss.clear();
  CmdShowBgpSummary().printOutput(model, ss);
  output = ss.str();

  expectedOutput =
      "BGP summary information for VRF default\n"
      "BGP is up for 1d 1h 1m 1s\n"
      "Router ID - 210.4.0.0, Local ASN - 6000, Confed ASN - 700\n"
      "BGP Switch Drain State: SOFT_DRAINED\n"
      "UCMP weight programming is DISABLED\n"
      "Update group is ENABLED\n"
      "Peers: UP - 1, TOTAL - 2\n"
      "Peer Groups Configured - 0\n"
      "EOR sent (UP peers) - 1/1\n"
      "EORs received (UP peers) - 1/1\n"
      "Paths: Received - 2, Accepted - 0, Sent - 2\n"
      "Loc-RIB Prefix Count: 42\n"
      "RIB Version: 7\n"
      "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent, UG - Update Group, UGPS - Update Group Peer State\n\n"
      " Peer     AS    State  GR  PR  PA  PS  UG  UGPS  Uptime    Downtime  Description           Session ID  Flaps \n"
      "----------------------------------------------------------------------------------------------------------------------------\n"
      " 1.2.3.4  1     ESTA   Y   2   0   2   -   -     0h 0m 1s            leaf01.p099.f01.lab0  4.3.2.1     9     \n"
      " 2.3.4.5  4651  IDLE   N   0   0   0   -   -               0h 0m 2s  leaf07.p099.f01.lab0  6.7.8.9     33    \n\n";

  EXPECT_EQ(output, expectedOutput);
}
#endif // IS_OSS

TEST_F(CmdShowBgpSummaryTestFixture, printOutputPrePolicyPrefixDrops) {
  // A non-zero pre-policy dropped count surfaces the PRD column.
  std::stringstream ss;
  auto model = getModelBgpSummary();
  model.sessions()[0].prepolicy_rcvd_dropped_prefix_count() = 50;
  CmdShowBgpSummary().printOutput(model, ss);
  std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("PRD"));
  EXPECT_THAT(output, HasSubstr("50"));
  EXPECT_THAT(output, HasSubstr("PRD - Prefixes Received Dropped"));
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputNoPrefixDrops) {
  // With no dropped routes, the PRD column and legend are omitted.
  std::stringstream ss;
  auto model = getModelBgpSummary();
  CmdShowBgpSummary().printOutput(model, ss);
  std::string output = ss.str();

  EXPECT_THAT(output, Not(HasSubstr("PRD")));
}

using SortColumn = CmdShowBgpSummary::SortColumn;

// Peers deliberately tie on some columns and differ on others, so a
// multi-column sort has something to break ties on.
class CmdShowBgpSummarySortTestFixture : public CmdHandlerTestBase {
 public:
  static TBgpSession makeSession(
      const std::string& peerAddr,
      uint32_t remoteAs,
      int64_t rcvdPrefixes,
      int64_t numResets) {
    TBgpSession session;
    session.peer()->peer_state() = kEstablishedPeerState;
    session.peer()->remote_as_4_byte() = remoteAs;
    session.peer()->graceful() = true;
    session.peer_addr() = peerAddr;
    session.prepolicy_rcvd_prefix_count() = rcvdPrefixes;
    session.postpolicy_rcvd_prefix_count() = rcvdPrefixes;
    session.postpolicy_sent_prefix_count() = 0;
    session.uptime() = 0;
    session.reset_time() = 0;
    session.num_resets() = numResets;
    session.description() = "";
    session.peer_bgp_id() = "0.0.0.0";
    return session;
  }

  std::vector<TBgpSession> sessions() {
    return {
        makeSession("10.0.0.3", 200, 9, 1),
        makeSession("10.0.0.1", 100, 100, 3),
        makeSession("10.0.0.2", 200, 9, 2),
    };
  }

  std::vector<std::string> sortedPeers(const std::string& sortBy) {
    auto input = sessions();
    TBgpLocalConfig config;
    TBgpDrainState drainState;
    const auto model = CmdShowBgpSummary().createModel(
        input,
        config,
        drainState,
        std::nullopt,
        std::nullopt,
        kRibVersion,
        CmdShowBgpSummary::parseSortKeys(sortBy));

    std::vector<std::string> peers;
    for (const auto& session : model.sessions().value()) {
      peers.emplace_back(session.peer_addr().value());
    }
    return peers;
  }
};

TEST_F(CmdShowBgpSummarySortTestFixture, defaultsToPeerAddressOrder) {
  const std::vector<std::string> expected = {
      "10.0.0.1", "10.0.0.2", "10.0.0.3"};
  EXPECT_EQ(sortedPeers(""), expected);
}

TEST_F(CmdShowBgpSummarySortTestFixture, sortsNumericallyNotLexically) {
  // 9 must come before 100, which a string sort would reverse.
  const std::vector<std::string> expected = {
      "10.0.0.2", "10.0.0.3", "10.0.0.1"};
  EXPECT_EQ(sortedPeers("pr"), expected);
}

TEST_F(CmdShowBgpSummarySortTestFixture, appliesKeysInPriorityOrder) {
  // AS ascending, then flaps descending within the two peers sharing AS 200.
  const std::vector<std::string> expected = {
      "10.0.0.1", "10.0.0.2", "10.0.0.3"};
  EXPECT_EQ(sortedPeers("as,flaps:desc"), expected);
}

TEST_F(CmdShowBgpSummarySortTestFixture, tiesFallBackToPeerAddress) {
  // Both AS-200 peers tie on the only requested key, so they keep peer order.
  const std::vector<std::string> expected = {
      "10.0.0.2", "10.0.0.3", "10.0.0.1"};
  EXPECT_EQ(sortedPeers("as:desc"), expected);
}

TEST(CmdShowBgpSummaryParseSortKeysTest, parsesMultipleKeysAndDirections) {
  const auto keys =
      CmdShowBgpSummary::parseSortKeys(" PR:desc , peer:asc ,flaps");
  ASSERT_EQ(keys.size(), 3);
  EXPECT_EQ(keys[0].column, SortColumn::PR);
  EXPECT_TRUE(keys[0].descending);
  EXPECT_EQ(keys[1].column, SortColumn::PEER);
  EXPECT_FALSE(keys[1].descending);
  EXPECT_EQ(keys[2].column, SortColumn::FLAPS);
  EXPECT_FALSE(keys[2].descending);
}

TEST(CmdShowBgpSummaryParseSortKeysTest, emptySpecYieldsNoKeys) {
  EXPECT_TRUE(CmdShowBgpSummary::parseSortKeys("").empty());
}

TEST(CmdShowBgpSummaryParseSortKeysTest, rejectsUnknownColumn) {
  EXPECT_THROW(
      CmdShowBgpSummary::parseSortKeys("peer,bogus"), std::invalid_argument);
}

TEST(CmdShowBgpSummaryParseSortKeysTest, rejectsUnknownDirection) {
  EXPECT_THROW(
      CmdShowBgpSummary::parseSortKeys("peer:sideways"), std::invalid_argument);
}

TEST_F(CmdShowBgpSummaryTestFixture, queryClientHonorsSortByOption) {
  CmdLocalOptions::getInstance()->setLocalOption(
      "show_bgp_summary", "--sort-by", "pr:desc");
  SCOPE_EXIT {
    CmdLocalOptions::getInstance()->setLocalOption(
        "show_bgp_summary", "--sort-by", "");
  };

  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getBgpSessions(_)).WillOnce([&](auto& entries) {
    entries = sessions_;
  });
  EXPECT_CALL(getMockBgp(), getBgpLocalConfig(_)).WillOnce([&](auto& entries) {
    entries = localConfig_;
  });
  EXPECT_CALL(getMockBgp(), getDrainState(_)).WillOnce([&](auto& entries) {
    entries = drainState_;
  });
  EXPECT_CALL(getMockBgp(), getProcessUptimeSeconds())
      .WillOnce(Return(kProcessUptimeSeconds));
  EXPECT_CALL(getMockBgp(), getNumPrefixes())
      .WillOnce(Return(kTotalPrefixCount));
  EXPECT_CALL(getMockBgp(), getRibVersion()).WillOnce(Return(kRibVersion));

  const auto results = CmdShowBgpSummary().queryClient(localhost());

  // The established peer received prefixes and the idle one did not, so
  // descending PR puts it first - the reverse of the peer-address default.
  ASSERT_EQ(results.sessions().value().size(), 2);
  EXPECT_EQ(
      results.sessions().value()[0].peer_addr().value(),
      kEstablishedPeerAddress);
}

TEST_F(CmdShowBgpSummaryTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpSummaryTraits::description().empty());
  EXPECT_FALSE(CmdShowBgpSummary::sampleModel().sessions()->empty());

  // Render the sample the way the wiki generator does. A property-only check
  // would still pass on a sample missing a field printOutput reads via
  // .value().
  std::stringstream ss;
  CmdShowBgpSummary().printOutput(CmdShowBgpSummary::sampleModel(), ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Router ID - 192.0.2.1, Local ASN - 65108"));
  EXPECT_THAT(output, HasSubstr("Peers: UP - 4, TOTAL - 4"));
  EXPECT_THAT(output, HasSubstr("Peer Groups Configured - 1"));
  EXPECT_THAT(output, HasSubstr("EOR sent (UP peers) - 4/4"));
  EXPECT_THAT(output, HasSubstr("EORs received (UP peers) - 4/4"));
  EXPECT_THAT(output, HasSubstr("192.0.2.11"));
  // The listen range is deliberately disjoint from the established peers, so
  // the example cannot be misread as a range that already has sessions in it.
  EXPECT_THAT(output, HasSubstr("198.51.100.0/24"));
  EXPECT_THAT(output, Not(HasSubstr("192.0.2.0/24")));
  // description() tells readers the three conditional columns are absent from
  // this example, so pin that they really are.
  EXPECT_THAT(output, Not(HasSubstr("PRD")));
  EXPECT_THAT(output, Not(HasSubstr("UGPS")));
  EXPECT_THAT(output, Not(HasSubstr("Downtime")));
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputEoRCountsOnlyEstablishedPeers) {
  auto model = getModelBgpSummary();
  // IDLE peers never contribute to the numerator or denominator.
  model.sessions()[1].eor_received() = true;
  model.sessions()[1].eor_sent() = true;
  std::stringstream ss;
  CmdShowBgpSummary().printOutput(model, ss);
  const auto output = ss.str();

  EXPECT_THAT(output, HasSubstr("EOR sent (UP peers) - 1/1\n"));
  EXPECT_THAT(output, HasSubstr("EORs received (UP peers) - 1/1\n"));
  EXPECT_THAT(output, Not(HasSubstr(" EOR Received  EOR Sent ")));
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputPendingEoRIsZeroNotNA) {
  auto model = getModelBgpSummary();
  model.sessions()[0].eor_received() = false;
  model.sessions()[0].eor_sent() = false;
  std::stringstream ss;
  CmdShowBgpSummary().printOutput(model, ss);
  const auto output = ss.str();

  EXPECT_THAT(output, HasSubstr("EOR sent (UP peers) - 0/1\n"));
  EXPECT_THAT(output, HasSubstr("EORs received (UP peers) - 0/1\n"));
}

TEST_F(CmdShowBgpSummaryTestFixture, printOutputOldDaemonEoRUnavailable) {
  auto model = getModelBgpSummary();
  for (auto& session : *model.sessions()) {
    session.eor_received().reset();
    session.eor_sent().reset();
  }
  std::stringstream ss;
  CmdShowBgpSummary().printOutput(model, ss);
  const auto output = ss.str();

  EXPECT_THAT(output, HasSubstr("EOR sent (UP peers) - N/A\n"));
  EXPECT_THAT(output, HasSubstr("EORs received (UP peers) - N/A\n"));
}

} // namespace facebook::fboss
