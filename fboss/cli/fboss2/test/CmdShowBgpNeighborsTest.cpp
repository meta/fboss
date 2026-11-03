/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/reflection/testing.h> // NOLINT(misc-include-cleaner)
#include <memory>
#include <sstream>
#include <string>
#include <string_view> // NOLINT(misc-include-cleaner)
#include <vector>
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/CmdShowBgpNeighbors.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/session_id/CmdBgpNeighborsSessionId.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#ifndef IS_OSS
// Avoid EXPECT_THRIFT_EQ clash with <thrift/lib/cpp2/reflection/testing.h>
#undef EXPECT_THRIFT_EQ
#include "nettools/common/TestUtils.h"
#endif

using namespace ::testing;
using facebook::neteng::fboss::bgp::thrift::TBgpAddPathNegotiated;
using facebook::neteng::fboss::bgp::thrift::TBgpPeer;
using facebook::neteng::fboss::bgp::thrift::TBgpSession;
using facebook::neteng::fboss::bgp::thrift::TBgpSessionDetail;
using facebook::neteng::fboss::bgp_attr::AddPath;
using facebook::neteng::fboss::bgp_attr::AdvertiseLinkBandwidth;
using facebook::neteng::fboss::bgp_attr::ReceiveLinkBandwidth;
using facebook::neteng::fboss::bgp_attr::TBgpAfi;

// Constants for queried IPs
const std::string kLookableIP = "1.2.3.4";

namespace facebook::fboss {

class CmdShowBgpNeighborsTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<TBgpSession> establishedNeighborSession_;
  std::vector<TBgpSession> idleNeighborSession_;
  std::vector<TBgpSession> activeNeighborSession_;
  std::vector<TBgpSession> allNeighborSessions_;
  std::vector<std::string> lookableIp_;
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    populateMockNeighbors();
    establishedNeighborSession_ = getEstablishedNeighborSession();
    allNeighborSessions_ = getAllNeighborSessions();
    idleNeighborSession_ = getIdleNeighborSession();
    activeNeighborSession_ = getActiveNeighborSession();

    lookableIp_ = {kLookableIP};
  }

 private:
  TBgpSession kNeighborSession1, kNeighborSession2, kNeighborSession3,
      kNeighborSession4;
  void populateMockNeighbors() {
    //
    // Populate kNeighborSession1
    //
    // kNeighborSession1 {peerAddr 1.2.3.4, remoteBgpId 123456 /* 64.226.1.0 */}
    // TBgpPeerState::ESTABLISHED;
    //
    kNeighborSession1.my_addr() = "5.6.7.8";
    kNeighborSession1.peer_addr() = "1.2.3.4";
    kNeighborSession1.prepolicy_rcvd_prefix_count() = 10;
    kNeighborSession1.postpolicy_sent_prefix_count() = 2;
    kNeighborSession1.description() = "abc001.d001.e01.fgh1";
    kNeighborSession1.uptime() = 847295;
    kNeighborSession1.reset_time() = 729000;
    kNeighborSession1.num_resets() = 20;
    kNeighborSession1.last_reset_reason() = "MANUAL_STOP";
    kNeighborSession1.advertise_link_bandwidth() =
        AdvertiseLinkBandwidth::DISABLE;
    kNeighborSession1.receive_link_bandwidth() = ReceiveLinkBandwidth::ACCEPT;
    kNeighborSession1.link_bandwidth_bps() = 1.25e+10;

    TBgpPeer peer;
    peer.local_as_4_byte() = 2001;
    peer.remote_as_4_byte() = 0xfffffff0; // 4294967280
    peer.hold_time() = 15;
    peer.peer_state() = TBgpPeerState::ESTABLISHED;
    peer.lastResetHoldTimer() = 1635278860724;
    peer.lastSentKeepAlive() = 1635278859716;
    peer.lastRcvdKeepAlive() = 1635278860724;

    TBgpSessionDetail details;
    details.confed_peer() = true;
    details.remote_bgp_id() = 123456; // 64.226.1.0
    details.rr_client() = false;
    details.connect_mode() = TBgpSessionConnectMode::PASSIVE_ACTIVE;
    details.peer_port() = 45824;
    details.local_router_id() = "2.3.5.6";
    details.local_port() = 58283;
    details.ipv4_unicast() = true;
    details.ipv6_unicast() = false;
    details.gr_restart_time() = 120;
    details.gr_remote_restart_time() = 60;
    details.eor_sent_time() = 1635271191576;
    details.eor_received_time() = 1635271195614;
    details.num_of_flaps() = 20;

    details.sent_update_announcements_ipv4() = 8;
    details.sent_update_announcements_ipv6() = 7;
    details.sent_update_withdrawals() = 6;

    details.recv_update_announcements_ipv4() = 5;
    details.recv_update_announcements_ipv6() = 4;
    details.recv_update_withdrawals() = 3;

    details.enforce_first_as_rejects() = 71;

    kNeighborSession1.peer() = peer;
    kNeighborSession1.details() = details;
    kNeighborSession1.rib_version() = 12345;

    //
    // Populate kNeighborSession2
    //
    // kNeighborSession2 has the same remote peerAddress as kNeighborSession1,
    // but different remoteBgpId 999999999 or 255.201.154.59
    //
    // kNeighborSession2 {peerAddr 1.2.3.4, remoteBgpId 999999999 /*
    // 255.201.154.59 */}
    // TBgpPeerState::ESTABLISHED;
    //

    kNeighborSession2 = kNeighborSession1;
    if (kNeighborSession2.details()) { // avoid linter
      kNeighborSession2.details()->remote_bgp_id() =
          999999999; // 255.201.154.59
    }

    //
    // Populate IDLE kNeighborSession3
    //
    // This kNeighborSession3 peer major information will be different from the
    // first two kNeighborSessions.
    // The minor details will be the same to make this peer information
    // complete.
    //
    // kNeighborSession3 {peerAddr 101.102.103.104, remoteBgpId 1010987903 /*
    // 127.115.66.60 */ } TBgpPeerState::IDLE;
    // AS number = 4008636128
    //
    kNeighborSession3 = kNeighborSession1;
    kNeighborSession3.peer_addr() = "101.102.103.104";
    kNeighborSession3.description() = "def001.k001.e01.fgh1";
    kNeighborSession3.peer()->remote_as_4_byte() = 0xeeeeeee0; // 4008636128
    kNeighborSession3.peer()->peer_state() = TBgpPeerState::IDLE;
    kNeighborSession3.rib_version() = 0; // IDLE peers don't show RIB version
    if (kNeighborSession3.details()) {
      kNeighborSession3.details()->remote_bgp_id() =
          1010987903; // 127.115.66.60
    }

    //
    // Populate ACTIVE kNeighborSession4
    //
    // This kNeighborSession4 peer major information will be different from the
    // first three kNeighborSessions.
    //
    // kNeighborSession4 {peerAddr 105.106.107.108, remoteBgpId 1010987904 /*
    // 128.115.66.60*/ } TBgpPeerState::ACTIVE;
    // AS number = 4008636129
    //
    kNeighborSession4 = kNeighborSession1;
    kNeighborSession4.peer_addr() = "105.106.107.108";
    kNeighborSession4.description() = "ghi001.j001.k01.ll1";
    kNeighborSession4.peer()->remote_as_4_byte() = 0xeeeeeee1; // 4008636129
    kNeighborSession4.peer()->peer_state() = TBgpPeerState::ACTIVE;
    kNeighborSession4.reset_time() = 50000;
    kNeighborSession4.num_resets() = 10;
    kNeighborSession4.rib_version() = 0; // ACTIVE peers don't show RIB version
    if (kNeighborSession4.details()) {
      kNeighborSession4.details()->remote_bgp_id() =
          1010987904; // 128.115.66.60
      kNeighborSession4.details()->active_time() = 2777;
    }
  }
  std::vector<TBgpSession> getEstablishedNeighborSession() {
    return {kNeighborSession1};
  }
  std::vector<TBgpSession> getIdleNeighborSession() {
    return {kNeighborSession3};
  }

  std::vector<TBgpSession> getActiveNeighborSession() {
    return {kNeighborSession4};
  }

  std::vector<TBgpSession> getAllNeighborSessions() {
    return {
        kNeighborSession1,
        kNeighborSession2,
        kNeighborSession3,
        kNeighborSession4};
  }
};

TEST_F(CmdShowBgpNeighborsTestFixture, queryClient) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getBgpNeighbors(_, _))
      .WillOnce(
          Invoke([&](std::vector<TBgpSession>& neighbors,
                     std::unique_ptr<std::vector<std::string>> queriedIp) {
            neighbors = establishedNeighborSession_;
            queriedIp = std::make_unique<std::vector<std::string>>(lookableIp_);
          }));
  auto results = CmdShowBgpNeighbors().queryClient(localhost(), lookableIp_);
  EXPECT_THRIFT_EQ_VECTOR(results, establishedNeighborSession_);
}

TEST_F(CmdShowBgpNeighborsTestFixture, printOutputWithoutAddCapabilities) {
  std::stringstream ss;
  CmdShowBgpNeighbors().printOutput(establishedNeighborSession_, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP neighbor is 1.2.3.4, remote AS 4294967280, Confed Peer: configured\n"
      "  Description: abc001.d001.e01.fgh1\n"
      "  BGP version 4, remote router ID 64.226.1.0\n"
      "  RIB version: 12345\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is ESTABLISHED, up for 0h 14m 7s\n"
      "  Flapped 20 times, last flap 0h 12m 9s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: DISABLED\n\n"
      " Prefix Telemetry    Sent  Rcvd  Accepted \n"
      "-----------------------------------------------\n"
      " Current prefixes    2     10    0        \n"
      " Announcements IPv4  8     5     -        \n"
      " Announcements IPv6  7     4     -        \n"
      " Withdrawals         6     3     -        \n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 1.2.3.4, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "EOR Sent at 2021-10-26 10:59:51.576 PDT\n"
      "EOR Received at 2021-10-26 10:59:55.614 PDT\n\n";

  EXPECT_EQ(output, expectedOutput);
}

// With non-zero per-type message counters, the detail view renders the
// data-plane (socket) + control-plane (AdjRib) counters table. Header presence
// is asserted via substrings; per-row values are asserted via ContainsRegex
// (spaces collapsed to " +") so the checks stay robust to utils::Table
// locale-dependent column widths while still pinning that each value lands in
// its own column. The four columns per row use DISTINCT values so a Tx/Rx swap
// or a Socket/AdjRib column crossing is caught (equal values would hide it).
TEST_F(CmdShowBgpNeighborsTestFixture, printOutputMessageCounters) {
  auto sessions = establishedNeighborSession_;
  auto& d = sessions[0].details().ensure();
  d.socket_tx_open_msgs() = 1;
  d.socket_tx_update_msgs() = 7;
  d.socket_tx_keepalive_msgs() = 3;
  d.socket_tx_notification_msgs() = 2;
  d.socket_tx_route_refresh_msgs() = 4;
  d.socket_tx_eor_msgs() = 5;
  d.socket_rx_open_msgs() = 10;
  d.socket_rx_update_msgs() = 70;
  d.socket_rx_keepalive_msgs() = 30;
  d.socket_rx_notification_msgs() = 20;
  d.socket_rx_route_refresh_msgs() = 40;
  d.socket_rx_eor_msgs() = 50;
  d.adjrib_sent_update_msgs() = 6;
  d.adjrib_sent_eor_msgs() = 4;
  d.adjrib_recv_update_msgs() = 60;
  d.adjrib_recv_eor_msgs() = 40;

  std::stringstream ss;
  CmdShowBgpNeighbors().printOutput(sessions, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Socket Tx"));
  EXPECT_THAT(output, HasSubstr("Socket Rx"));
  EXPECT_THAT(output, HasSubstr("AdjRib Sent"));
  EXPECT_THAT(output, HasSubstr("AdjRib Recv"));

  // Socket-only row: AdjRib columns render "-" for non-UPDATE/EoR PDU types.
  EXPECT_THAT(output, ContainsRegex("open +1 +10 +- +-"));
  // AdjRib-bearing rows: all four columns distinct so column order is verified.
  EXPECT_THAT(output, ContainsRegex("update +7 +70 +6 +60"));
  EXPECT_THAT(output, ContainsRegex("end-of-rib +5 +50 +4 +40"));
}

TEST_F(
    CmdShowBgpNeighborsTestFixture,
    printOutputWithoutAddCapabilitiesBothAsnField) {
  // TODO(michaeluw): T113736668 deprecate this when we deprecate i32 asns field
  // both field present
  std::stringstream ss;
  auto peer = establishedNeighborSession_.at(0).peer();
  peer->local_as() = 1111;
  peer->remote_as() = 2222;
  CmdShowBgpNeighbors().printOutput(establishedNeighborSession_, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP neighbor is 1.2.3.4, remote AS 4294967280, Confed Peer: configured\n"
      "  Description: abc001.d001.e01.fgh1\n"
      "  BGP version 4, remote router ID 64.226.1.0\n"
      "  RIB version: 12345\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is ESTABLISHED, up for 0h 14m 7s\n"
      "  Flapped 20 times, last flap 0h 12m 9s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: DISABLED\n\n"
      " Prefix Telemetry    Sent  Rcvd  Accepted \n"
      "-----------------------------------------------\n"
      " Current prefixes    2     10    0        \n"
      " Announcements IPv4  8     5     -        \n"
      " Announcements IPv6  7     4     -        \n"
      " Withdrawals         6     3     -        \n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 1.2.3.4, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "EOR Sent at 2021-10-26 10:59:51.576 PDT\n"
      "EOR Received at 2021-10-26 10:59:55.614 PDT\n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(
    CmdShowBgpNeighborsTestFixture,
    printOutputWithoutAddCapabilitiesOldAsnField) {
  // TODO(michaeluw): T113736668 deprecate this when we deprecate i32 asns field
  std::stringstream ss;

  TBgpPeer peer;
  peer.local_as() = 2001;
  peer.remote_as() = 6000;
  peer.hold_time() = 15;
  peer.peer_state() = TBgpPeerState::ESTABLISHED;
  peer.lastResetHoldTimer() = 1635278860724;
  peer.lastSentKeepAlive() = 1635278859716;
  peer.lastRcvdKeepAlive() = 1635278860724;

  establishedNeighborSession_.at(0).peer() = peer;

  CmdShowBgpNeighbors().printOutput(establishedNeighborSession_, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP neighbor is 1.2.3.4, remote AS 6000, Confed Peer: configured\n"
      "  Description: abc001.d001.e01.fgh1\n"
      "  BGP version 4, remote router ID 64.226.1.0\n"
      "  RIB version: 12345\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is ESTABLISHED, up for 0h 14m 7s\n"
      "  Flapped 20 times, last flap 0h 12m 9s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: DISABLED\n\n"
      " Prefix Telemetry    Sent  Rcvd  Accepted \n"
      "-----------------------------------------------\n"
      " Current prefixes    2     10    0        \n"
      " Announcements IPv4  8     5     -        \n"
      " Announcements IPv6  7     4     -        \n"
      " Withdrawals         6     3     -        \n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 1.2.3.4, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "EOR Sent at 2021-10-26 10:59:51.576 PDT\n"
      "EOR Received at 2021-10-26 10:59:55.614 PDT\n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpNeighborsTestFixture, printOutputWithAddCapabilities) {
  TBgpAddPathNegotiated pathNegotiations;
  pathNegotiations.afi() = TBgpAfi::AFI_IPV4;
  pathNegotiations.add_path() = AddPath::BOTH;

  auto& session = establishedNeighborSession_.at(0);
  session.details()->add_path_capabilities() = {pathNegotiations};
  session.peer()->add_path() = AddPath::BOTH;

  std::stringstream ss;
  CmdShowBgpNeighbors().printOutput(establishedNeighborSession_, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP neighbor is 1.2.3.4, remote AS 4294967280, Confed Peer: configured\n"
      "  Description: abc001.d001.e01.fgh1\n"
      "  BGP version 4, remote router ID 64.226.1.0\n"
      "  RIB version: 12345\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is ESTABLISHED, up for 0h 14m 7s\n"
      "  Flapped 20 times, last flap 0h 12m 9s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: Configured - BOTH, Negotiated - AFI_IPV4 BOTH\n\n"
      " Prefix Telemetry    Sent  Rcvd  Accepted \n"
      "-----------------------------------------------\n"
      " Current prefixes    2     10    0        \n"
      " Announcements IPv4  8     5     -        \n"
      " Announcements IPv6  7     4     -        \n"
      " Withdrawals         6     3     -        \n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 1.2.3.4, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "EOR Sent at 2021-10-26 10:59:51.576 PDT\n"
      "EOR Received at 2021-10-26 10:59:55.614 PDT\n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(
    CmdShowBgpNeighborsTestFixture,
    printOutputIdleNeighborWithAddCapabilities) {
  std::stringstream ss;
  CmdShowBgpNeighbors().printOutput(idleNeighborSession_, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP neighbor is 101.102.103.104, remote AS 4008636128, Confed Peer: configured\n"
      "  Description: def001.k001.e01.fgh1\n"
      "  BGP version 4, remote router ID 127.115.66.60\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is IDLE\n"
      "  Flapped 20 times, last went down 0h 12m 9s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: DISABLED\n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 101.102.103.104, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(
    CmdShowBgpNeighborsTestFixture,
    printOutputActiveNeighborWithAddCapabilities) {
  std::stringstream ss;
  CmdShowBgpNeighbors().printOutput(activeNeighborSession_, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP neighbor is 105.106.107.108, remote AS 4008636129, Confed Peer: configured\n"
      "  Description: ghi001.j001.k01.ll1\n"
      "  BGP version 4, remote router ID 128.115.66.60\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is ACTIVE\n"
      "  The TCP socket has been up for 0h 0m 2s.\n"
      "  Flapped 10 times, last went down 0h 0m 50s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: DISABLED\n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 105.106.107.108, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpNeighborsTestFixture, printAllNeighbors) {
  std::stringstream ss;
  CmdShowBgpNeighbors().printOutput(allNeighborSessions_, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "------------------------------------------------------------------\n"
      "neighbor 1 of 4\n"
      "BGP neighbor is 1.2.3.4, remote AS 4294967280, Confed Peer: configured\n"
      "  Description: abc001.d001.e01.fgh1\n"
      "  BGP version 4, remote router ID 64.226.1.0\n"
      "  RIB version: 12345\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is ESTABLISHED, up for 0h 14m 7s\n"
      "  Flapped 20 times, last flap 0h 12m 9s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: DISABLED\n"
      "\n"
      " Prefix Telemetry    Sent  Rcvd  Accepted \n"
      "-----------------------------------------------\n"
      " Current prefixes    2     10    0        \n"
      " Announcements IPv4  8     5     -        \n"
      " Announcements IPv6  7     4     -        \n"
      " Withdrawals         6     3     -        \n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 1.2.3.4, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "EOR Sent at 2021-10-26 10:59:51.576 PDT\n"
      "EOR Received at 2021-10-26 10:59:55.614 PDT\n"
      "------------------------------------------------------------------\n"
      "\n"
      "------------------------------------------------------------------\n"
      "neighbor 2 of 4\n"
      "BGP neighbor is 1.2.3.4, remote AS 4294967280, Confed Peer: configured\n"
      "  Description: abc001.d001.e01.fgh1\n"
      "  BGP version 4, remote router ID 255.201.154.59\n"
      "  RIB version: 12345\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is ESTABLISHED, up for 0h 14m 7s\n"
      "  Flapped 20 times, last flap 0h 12m 9s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: DISABLED\n"
      "\n"
      " Prefix Telemetry    Sent  Rcvd  Accepted \n"
      "-----------------------------------------------\n"
      " Current prefixes    2     10    0        \n"
      " Announcements IPv4  8     5     -        \n"
      " Announcements IPv6  7     4     -        \n"
      " Withdrawals         6     3     -        \n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 1.2.3.4, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "EOR Sent at 2021-10-26 10:59:51.576 PDT\n"
      "EOR Received at 2021-10-26 10:59:55.614 PDT\n"
      "------------------------------------------------------------------\n"
      "\n"
      "------------------------------------------------------------------\n"
      "neighbor 3 of 4\n"
      "BGP neighbor is 101.102.103.104, remote AS 4008636128, Confed Peer: configured\n"
      "  Description: def001.k001.e01.fgh1\n"
      "  BGP version 4, remote router ID 127.115.66.60\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is IDLE\n"
      "  Flapped 20 times, last went down 0h 12m 9s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: DISABLED\n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 101.102.103.104, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "------------------------------------------------------------------\n"
      "\n"
      "------------------------------------------------------------------\n"
      "neighbor 4 of 4\n"
      "BGP neighbor is 105.106.107.108, remote AS 4008636129, Confed Peer: configured\n"
      "  Description: ghi001.j001.k01.ll1\n"
      "  BGP version 4, remote router ID 128.115.66.60\n"
      "  Hold time is 15s, KeepAlive interval is 5s\n"
      "  BGP state is ACTIVE\n"
      "  The TCP socket has been up for 0h 0m 2s.\n"
      "  Flapped 10 times, last went down 0h 0m 50s ago\n"
      "  Last reset reason: MANUAL_STOP\n"
      "  UCMP Link Bandwidth:\n"
      "    Advertise: DISABLE\n"
      "    Receive  : ACCEPT\n"
      "    Value    : 99Gbps\n"
      "  Neighbor Capabilities:\n"
      "    Multiprotocol IPv4 Unicast: negotiated\n"
      "    Graceful Restart: received, peer restart-time is 60s\n"
      "    Graceful Restart: sent, local restart-time is 120s\n"
      "    Add Path: DISABLED\n"
      "\n"
      "Number of enforce-first-as validation rejections: 71\n"
      "Local AS is 2001, local router ID 2.3.5.6\n"
      "Local TCP address is 5.6.7.8, local port is 44003\n"
      "Remote TCP address is 105.106.107.108, remote port is 179\n"
      "TCP connection-mode is PASSIVE_ACTIVE\n"
      "Number of session terminations: 20\n"
      "HoldTimer last reset at 2021-10-26 13:07:40.724 PDT\n"
      "KeepAlive last received at 2021-10-26 13:07:39.716 PDT\n"
      "KeepAlive last sent at 2021-10-26 13:07:40.724 PDT\n"
      "------------------------------------------------------------------\n"
      "\n";
  EXPECT_EQ(output, expectedOutput);
}

// Unit-test the "Legacy v4 NLRI encoding" line in the Neighbor Capabilities
// block: it is shown only when the session uses legacy v4-unicast encoding.
TEST(PrintBgpCapabilitiesTest, LegacyV4NlriEncoding) {
  TBgpSessionDetail details;
  details.ipv4_unicast() = true;
  details.ipv6_unicast() = false;
  details.rr_client() = false;

  details.legacy_v4_nlri_encoding() = true;
  {
    std::ostringstream out;
    printBgpCapabilities(details, out);
    EXPECT_THAT(
        out.str(),
        HasSubstr(
            "Legacy v4 NLRI encoding (RFC 4271 classic NLRI + NEXT_HOP): yes"));
  }

  details.legacy_v4_nlri_encoding() = false;
  {
    std::ostringstream out;
    printBgpCapabilities(details, out);
    EXPECT_THAT(out.str(), Not(HasSubstr("Legacy v4 NLRI encoding")));
  }
}

TEST_F(CmdShowBgpNeighborsTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpNeighborsTraits::description().empty());
  EXPECT_EQ(CmdShowBgpNeighbors::sampleModel().size(), 2);

  // Render the sample the way the wiki generator does; a property-only check
  // would still pass on a sample missing a field printOutput reads via
  // .value().
  std::stringstream ss;
  CmdShowBgpNeighbors().printOutput(CmdShowBgpNeighbors::sampleModel(), ss);
  const std::string output = ss.str();

  // One established peer with the rich render, one listen range with the
  // sparse one - the two cases description() contrasts.
  EXPECT_THAT(output, HasSubstr("neighbor 1 of 2"));
  EXPECT_THAT(output, HasSubstr("BGP state is ESTABLISHED"));
  EXPECT_THAT(output, HasSubstr("Prefix Telemetry"));
  EXPECT_THAT(output, HasSubstr("neighbor 2 of 2"));
  EXPECT_THAT(output, HasSubstr("BGP state is IDLE"));
}

TEST_F(CmdShowBgpNeighborsTestFixture, sessionIdWikiDocHooks) {
  EXPECT_FALSE(CmdBgpNeighborsSessionIdTraits::description().empty());

  // A session ID selects one session, so unlike the parent command's sample
  // there is no second entry and no 'neighbor N of M' separator.
  const auto model = CmdBgpNeighborsSessionId::sampleModel();
  ASSERT_EQ(model.size(), 1);

  std::stringstream ss;
  CmdBgpNeighborsSessionId().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("BGP state is ESTABLISHED"));
  EXPECT_THAT(output, HasSubstr("Prefix Telemetry"));
  EXPECT_THAT(output, Not(HasSubstr("neighbor 1 of")));
}
} // namespace facebook::fboss
