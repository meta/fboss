// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/String.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/neighbor/CmdConfigProtocolBgpNeighbor.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

using namespace ::testing;
using facebook::neteng::fboss::bgp_attr::AddPath;

namespace facebook::fboss {

// The neighbor dispatcher only touches the BGP side of ConfigSession, which
// seeds from thrift schema defaults when neither a staged session nor a
// system bgpcpp.conf exists — so no seed agent config is needed (mirrors
// BgpConfigSessionTest).
class CmdConfigBgpNeighborTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigBgpNeighborTestFixture()
      : CmdConfigTestBase("bgp_neighbor_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string run(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpNeighbor cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpNeighborConfig(tokens));
  }

  const std::vector<bgp::thrift::BgpPeer>& peers() {
    return *ConfigSession::getInstance().getBgpConfig().peers();
  }

  bgp::thrift::BgpConfig& config() {
    return ConfigSession::getInstance().getBgpConfig();
  }

  // Stage a peer group directly in the session config (the peer-group command
  // is out of scope here); returns a reference valid until the next insert.
  bgp::thrift::PeerGroup& addPeerGroup(const std::string& name) {
    auto& groups = config().peer_groups().ensure();
    groups.emplace_back();
    groups.back().name() = name;
    return groups.back();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpNeighborConfig (arg) validation
// ==============================================================================

TEST_F(CmdConfigBgpNeighborTestFixture, argValidation) {
  // Bare create.
  EXPECT_EQ(BgpNeighborConfig({"10.0.0.1"}).peerAddr(), "10.0.0.1");
  EXPECT_TRUE(BgpNeighborConfig({"10.0.0.1"}).attr().empty());

  // Address normalization.
  EXPECT_EQ(BgpNeighborConfig({"2001:DB8::1"}).peerAddr(), "2001:db8::1");

  // Prefix notation (passive listening sessions); host bits are cleared the
  // way bgpd keys dynamic peers, so spellings that differ only there agree.
  EXPECT_EQ(BgpNeighborConfig({"2001:db8::/64"}).peerAddr(), "2001:db8::/64");
  EXPECT_EQ(BgpNeighborConfig({"10.0.0.1/24"}).peerAddr(), "10.0.0.0/24");
  EXPECT_EQ(
      BgpNeighborConfig({"2001:db8::1234/64"}).peerAddr(), "2001:db8::/64");

  // Single-token attribute.
  auto singleTok = BgpNeighborConfig({"10.0.0.1", "remote-asn", "65000"});
  EXPECT_EQ(singleTok.attr(), "remote-asn");
  EXPECT_EQ(singleTok.values(), std::vector<std::string>({"65000"}));

  // Two-token attribute wins longest-prefix match.
  auto twoTok = BgpNeighborConfig({"10.0.0.1", "timers", "hold-time", "30"});
  EXPECT_EQ(twoTok.attr(), "timers hold-time");
  EXPECT_EQ(twoTok.values(), std::vector<std::string>({"30"}));

  // Invalid: empty, bad address, unknown attribute, incomplete group.
  EXPECT_THROW(BgpNeighborConfig({}), std::invalid_argument);
  EXPECT_THROW(BgpNeighborConfig({"not-an-ip"}), std::invalid_argument);
  EXPECT_THROW(
      BgpNeighborConfig({"10.0.0.1", "no-such-attr", "1"}),
      std::invalid_argument);
  EXPECT_THROW(
      BgpNeighborConfig({"10.0.0.1", "timers"}), std::invalid_argument);
}

// ==============================================================================
// Attribute handlers
// ==============================================================================

TEST_F(CmdConfigBgpNeighborTestFixture, bareCreate) {
  auto result = run({"10.0.0.1"});
  EXPECT_THAT(result, HasSubstr("Successfully created BGP neighbor 10.0.0.1"));
  ASSERT_EQ(peers().size(), 1);
  EXPECT_EQ(*peers()[0].peer_addr(), "10.0.0.1");
  // bgpd aborts at config load on unparseable (empty) address fields, so a
  // new peer must be born with parseable any-addresses of the right family.
  EXPECT_EQ(*peers()[0].local_addr(), "0.0.0.0");
  EXPECT_EQ(*peers()[0].next_hop4(), "0.0.0.0");
  EXPECT_EQ(*peers()[0].next_hop6(), "::");
  EXPECT_TRUE(sessionFileExists());

  run({"2001:db8::1"});
  ASSERT_EQ(peers().size(), 2);
  EXPECT_EQ(*peers()[1].local_addr(), "::");
}

TEST_F(CmdConfigBgpNeighborTestFixture, bareCreateExisting) {
  run({"10.0.0.1", "remote-asn", "65000"});
  // A bare re-create of an existing neighbor is a no-op: it must say so
  // rather than claim a creation, and leave the neighbor untouched.
  auto result = run({"10.0.0.1"});
  EXPECT_THAT(result, HasSubstr("BGP neighbor 10.0.0.1 already exists"));
  EXPECT_THAT(result, Not(HasSubstr("Successfully created")));
  ASSERT_EQ(peers().size(), 1);
  EXPECT_EQ(peers()[0].remote_as_4_byte().value_or(0), 65000);
}

TEST_F(CmdConfigBgpNeighborTestFixture, setRemoteAsn) {
  auto result = run({"10.0.0.1", "remote-asn", "65000"});
  EXPECT_THAT(result, HasSubstr("65000"));
  ASSERT_EQ(peers().size(), 1);
  EXPECT_EQ(peers()[0].remote_as_4_byte().value_or(0), 65000);
}

TEST_F(CmdConfigBgpNeighborTestFixture, timersAccumulate) {
  run({"10.0.0.1", "timers", "hold-time", "90"});
  run({"10.0.0.1", "timers", "keepalive", "30"});
  run({"10.0.0.1", "graceful-restart", "restart-time", "120"});
  ASSERT_EQ(peers().size(), 1);
  const auto& timers = *peers()[0].bgp_peer_timers();
  EXPECT_EQ(*timers.hold_time_seconds(), 90);
  EXPECT_EQ(*timers.keep_alive_seconds(), 30);
  EXPECT_EQ(timers.graceful_restart_seconds().value_or(0), 120);
}

TEST_F(CmdConfigBgpNeighborTestFixture, addPathMerge) {
  run({"10.0.0.1", "add-path", "receive", "true"});
  EXPECT_EQ(peers()[0].add_path().value_or(AddPath(0)), AddPath::RECEIVE);

  run({"10.0.0.1", "add-path", "send", "true"});
  EXPECT_EQ(peers()[0].add_path().value_or(AddPath(0)), AddPath::BOTH);

  run({"10.0.0.1", "add-path", "receive", "false"});
  EXPECT_EQ(peers()[0].add_path().value_or(AddPath(0)), AddPath::SEND);

  run({"10.0.0.1", "add-path", "send", "false"});
  EXPECT_FALSE(peers()[0].add_path().has_value());
}

TEST_F(CmdConfigBgpNeighborTestFixture, passive) {
  // is_passive is the only session-mode knob bgpd has: true = listen only,
  // false = listen and connect. The CLI exposes the flag as-is.
  run({"10.0.0.1", "passive", "true"});
  EXPECT_TRUE(peers()[0].is_passive().value_or(false));

  run({"10.0.0.1", "passive", "false"});
  EXPECT_FALSE(peers()[0].is_passive().value_or(true));

  EXPECT_THAT(run({"10.0.0.1", "passive", "BOTH"}), HasSubstr("Invalid"));
  EXPECT_FALSE(peers()[0].is_passive().value_or(true));
}

TEST_F(CmdConfigBgpNeighborTestFixture, boolAndStringAttributes) {
  run({"10.0.0.1", "rr-client", "true"});
  run({"10.0.0.1", "confed-peer", "false"});
  run({"10.0.0.1", "redistribute-peer", "true"});
  run({"10.0.0.1", "enhanced-route-refresh", "true"});
  run({"10.0.0.1", "route-refresh", "true"});
  run({"10.0.0.1", "remove-private-as", "true"});
  run({"10.0.0.1", "enforce-first-as", "false"});
  run({"10.0.0.1", "afi", "disable-ipv4-afi", "true"});
  run({"10.0.0.1", "afi", "disable-ipv6-afi", "false"});
  run({"10.0.0.1", "afi", "ipv4-over-ipv6-nh", "true"});
  run({"10.0.0.1", "graceful-restart", "stateful-ha", "true"});
  run({"10.0.0.1", "peer-group", "MY-GROUP"});
  run({"10.0.0.1", "peer-tag", "FSW"});
  run({"10.0.0.1", "ingress-policy", "IN_POLICY"});
  run({"10.0.0.1", "egress-policy", "OUT_POLICY"});
  run({"10.0.0.1", "bind-addr", "address", "10.0.0.2"});
  run({"10.0.0.1", "local-asn", "64512"});
  run({"10.0.0.1", "description", "spine", "peer"});

  ASSERT_EQ(peers().size(), 1);
  const auto& peer = peers()[0];
  EXPECT_TRUE(peer.is_rr_client().value_or(false));
  EXPECT_FALSE(peer.is_confed_peer().value_or(true));
  EXPECT_TRUE(peer.is_redistribute_peer().value_or(false));
  EXPECT_TRUE(peer.enhanced_route_refresh().value_or(false));
  EXPECT_TRUE(peer.route_refresh().value_or(false));
  EXPECT_TRUE(peer.remove_private_as().value_or(false));
  EXPECT_FALSE(peer.enforce_first_as().value_or(true));
  EXPECT_TRUE(peer.disable_ipv4_afi().value_or(false));
  EXPECT_FALSE(peer.disable_ipv6_afi().value_or(true));
  EXPECT_TRUE(peer.v4_over_v6_nexthop().value_or(false));
  EXPECT_TRUE(peer.enable_stateful_ha().value_or(false));
  EXPECT_EQ(peer.peer_group_name().value_or(""), "MY-GROUP");
  EXPECT_EQ(peer.peer_tag().value_or(""), "FSW");
  EXPECT_EQ(peer.ingress_policy_name().value_or(""), "IN_POLICY");
  EXPECT_EQ(peer.egress_policy_name().value_or(""), "OUT_POLICY");
  EXPECT_EQ(*peer.local_addr(), "10.0.0.2");
  EXPECT_EQ(peer.local_as_4_byte().value_or(0), 64512);
  // Multi-token description is re-joined.
  EXPECT_EQ(peer.description().value_or(""), "spine peer");
}

TEST_F(CmdConfigBgpNeighborTestFixture, parityAttributes) {
  // Attributes carried over from the deleted per-attribute peer commands.
  run({"10.0.0.1", "next-hop4", "10.1.1.1"});
  run({"10.0.0.1", "next-hop6", "2001:db8::a"});
  run({"10.0.0.1", "next-hop-self", "true"});
  run({"10.0.0.1", "peer-id", "test-peer:v6:1"});
  run({"10.0.0.1", "type", "BGP_MONITOR"});
  run({"10.0.0.1", "link-bandwidth", "100G"});
  EXPECT_EQ(peers()[0].link_bandwidth_bps().value_or(""), "100G");
  // Every spelling bgpd's own parser takes must be accepted, or an existing
  // config could not be recreated through the CLI.
  run({"10.0.0.1", "link-bandwidth", "1.5G"});
  EXPECT_EQ(peers()[0].link_bandwidth_bps().value_or(""), "1.5G");
  run({"10.0.0.1", "link-bandwidth", "150g"});
  EXPECT_EQ(peers()[0].link_bandwidth_bps().value_or(""), "150g");
  run({"10.0.0.1", "link-bandwidth", "auto"});
  EXPECT_EQ(peers()[0].link_bandwidth_bps().value_or(""), "auto");
  run({"10.0.0.1", "link-bandwidth", "100G"});
  run({"10.0.0.1", "advertise-lbw", "1"});
  run({"10.0.0.1", "receive-lbw", "ACCEPT"}); // enum value name accepted
  run(
      {"10.0.0.1",
       "advertise-lbw",
       "true"}); // bool alias (old peer cmd compat)
  run({"10.0.0.1", "timers", "withdraw-unprog-delay", "5"});
  run({"10.0.0.1", "max-route", "pre-warning-threshold", "100"});
  run({"10.0.0.1", "max-route", "pre-warning-only", "true"});
  run({"10.0.0.1", "max-route", "post-warning-only", "false"});

  ASSERT_EQ(peers().size(), 1);
  const auto& peer = peers()[0];
  EXPECT_EQ(*peer.next_hop4(), "10.1.1.1");
  EXPECT_EQ(*peer.next_hop6(), "2001:db8::a");
  EXPECT_TRUE(peer.next_hop_self().value_or(false));
  EXPECT_EQ(peer.peer_id().value_or(""), "test-peer:v6:1");
  EXPECT_EQ(peer.type().value_or(""), "BGP_MONITOR");
  EXPECT_EQ(peer.link_bandwidth_bps().value_or(""), "100G");
  EXPECT_EQ(
      static_cast<int>(peer.advertise_link_bandwidth().value_or(
          facebook::neteng::fboss::bgp_attr::AdvertiseLinkBandwidth::DISABLE)),
      1);
  EXPECT_EQ(
      static_cast<int>(peer.receive_link_bandwidth().value_or(
          facebook::neteng::fboss::bgp_attr::ReceiveLinkBandwidth::DISABLE)),
      1);
  EXPECT_EQ(*peer.bgp_peer_timers()->withdraw_unprog_delay_seconds(), 5);
  EXPECT_EQ(*peer.pre_filter()->warning_limit(), 100);
  EXPECT_TRUE(*peer.pre_filter()->warning_only());
  EXPECT_FALSE(*peer.post_filter()->warning_only());
}

TEST_F(CmdConfigBgpNeighborTestFixture, valueValidation) {
  // ASN must fit in 4 bytes (RFC 6793).
  EXPECT_THAT(
      run({"10.0.0.1", "remote-asn", "4294967296"}), HasSubstr("Invalid"));
  EXPECT_THAT(
      run({"10.0.0.1", "local-asn", "18446744073709551615"}),
      HasSubstr("Invalid"));
  // IP-valued attributes validate the address and its family.
  EXPECT_THAT(
      run({"10.0.0.1", "bind-addr", "address", "not-an-ip"}),
      HasSubstr("Invalid"));
  EXPECT_THAT(run({"10.0.0.1", "next-hop4", "2001:db8::a"}), HasSubstr("IPv4"));
  EXPECT_THAT(run({"10.0.0.1", "next-hop6", "10.1.1.1"}), HasSubstr("IPv6"));
  // Enum-valued attributes reject non-member modes but accept value names
  // and true/false aliases (compat with the replaced boolean peer commands).
  EXPECT_THAT(
      run({"10.0.0.1", "advertise-lbw", "99"}), HasSubstr("not a valid mode"));
  // link-bandwidth rejects exactly what bgpd's parser rejects.
  EXPECT_THAT(run({"10.0.0.1", "link-bandwidth", "10Q"}), HasSubstr("Invalid"));
  EXPECT_THAT(run({"10.0.0.1", "link-bandwidth", "G10"}), HasSubstr("Invalid"));
  EXPECT_THAT(
      run({"10.0.0.1", "link-bandwidth", "1.2.3G"}), HasSubstr("Invalid"));
  EXPECT_THAT(run({"10.0.0.1", "link-bandwidth", "G"}), HasSubstr("Invalid"));
  EXPECT_THAT(run({"10.0.0.1", "link-bandwidth", "10T"}), HasSubstr("Invalid"));
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(CmdConfigBgpNeighborTestFixture, routeLimits) {
  run({"10.0.0.1", "max-route", "pre-filter", "45000"});
  run({"10.0.0.1", "max-route", "post-filter", "50000"});
  run({"10.0.0.1", "max-route", "post-warning-threshold", "48000"});
  const auto& peer = peers()[0];
  EXPECT_EQ(*peer.pre_filter()->max_routes(), 45000);
  EXPECT_EQ(*peer.post_filter()->max_routes(), 50000);
  EXPECT_EQ(*peer.post_filter()->warning_limit(), 48000);
}

TEST_F(CmdConfigBgpNeighborTestFixture, normalizedAddressesShareOnePeer) {
  run({"2001:DB8::1", "remote-asn", "65000"});
  run({"2001:db8:0:0::1", "timers", "hold-time", "30"});
  ASSERT_EQ(peers().size(), 1);
  EXPECT_EQ(*peers()[0].peer_addr(), "2001:db8::1");
}

TEST_F(CmdConfigBgpNeighborTestFixture, prefixSpellingsShareOnePeer) {
  // bgpd masks a dynamic peer's prefix at load, so these are one neighbor to
  // it; a second CLI spelling must update that peer, not create a twin.
  run({"10.0.0.1/24", "remote-asn", "65000"});
  run({"10.0.0.2/24", "passive", "true"});
  ASSERT_EQ(peers().size(), 1);
  EXPECT_EQ(*peers()[0].peer_addr(), "10.0.0.0/24");
  EXPECT_EQ(peers()[0].remote_as_4_byte().value_or(0), 65000);
  EXPECT_TRUE(peers()[0].is_passive().value_or(false));
}

// ==============================================================================
// Inherited values: bgpd resolves timers / route limits per STRUCT, so the
// first field set on a peer must not silently zero the rest of the struct.
// ==============================================================================

TEST_F(CmdConfigBgpNeighborTestFixture, timersSeededFromPeerGroup) {
  auto& group = addPeerGroup("SPINE");
  bgp::thrift::BgpPeerTimers groupTimers;
  groupTimers.hold_time_seconds() = 90;
  groupTimers.keep_alive_seconds() = 30;
  groupTimers.out_delay_seconds() = 5;
  groupTimers.graceful_restart_seconds() = 120;
  group.bgp_peer_timers() = groupTimers;

  run({"10.0.0.1", "peer-group", "SPINE"});
  run({"10.0.0.1", "graceful-restart", "restart-time", "60"});

  ASSERT_EQ(peers().size(), 1);
  const auto& timers = *peers()[0].bgp_peer_timers();
  EXPECT_EQ(*timers.hold_time_seconds(), 90);
  EXPECT_EQ(*timers.keep_alive_seconds(), 30);
  EXPECT_EQ(*timers.out_delay_seconds(), 5);
  EXPECT_EQ(timers.graceful_restart_seconds().value_or(0), 60);
}

TEST_F(CmdConfigBgpNeighborTestFixture, timersSeededFromGlobalWithoutGroup) {
  config().hold_time() = 180;
  run({"10.0.0.1", "graceful-restart", "restart-time", "60"});

  const auto& timers = *peers()[0].bgp_peer_timers();
  EXPECT_EQ(*timers.hold_time_seconds(), 180);
  EXPECT_EQ(*timers.keep_alive_seconds(), 60);
  EXPECT_EQ(*timers.out_delay_seconds(), 0);
  EXPECT_EQ(timers.graceful_restart_seconds().value_or(0), 60);
}

TEST_F(CmdConfigBgpNeighborTestFixture, timersNotReseededOncePresent) {
  auto& group = addPeerGroup("SPINE");
  bgp::thrift::BgpPeerTimers groupTimers;
  groupTimers.hold_time_seconds() = 90;
  groupTimers.keep_alive_seconds() = 30;
  group.bgp_peer_timers() = groupTimers;

  run({"10.0.0.1", "peer-group", "SPINE"});
  run({"10.0.0.1", "timers", "hold-time", "45"});
  run({"10.0.0.1", "timers", "out-delay", "2"});

  const auto& timers = *peers()[0].bgp_peer_timers();
  EXPECT_EQ(*timers.hold_time_seconds(), 45);
  EXPECT_EQ(*timers.keep_alive_seconds(), 30);
  EXPECT_EQ(*timers.out_delay_seconds(), 2);
}

TEST_F(CmdConfigBgpNeighborTestFixture, routeLimitSeededFromPeerGroup) {
  auto& group = addPeerGroup("SPINE");
  bgp::thrift::RouteLimit limit;
  limit.max_routes() = 45000;
  limit.warning_limit() = 40000;
  group.pre_filter() = limit;

  run({"10.0.0.1", "peer-group", "SPINE"});
  run({"10.0.0.1", "max-route", "pre-warning-only", "true"});

  const auto& pre = *peers()[0].pre_filter();
  EXPECT_EQ(*pre.max_routes(), 45000);
  EXPECT_EQ(*pre.warning_limit(), 40000);
  EXPECT_TRUE(*pre.warning_only());
  // post_filter was not touched and stays inherited.
  EXPECT_FALSE(peers()[0].post_filter().has_value());
}

TEST_F(CmdConfigBgpNeighborTestFixture, routeLimitSeededUnlimitedWithoutGroup) {
  // Neither the peer nor its group carries a RouteLimit: bgpd applies no cap
  // at all, so the struct created for a warning-only/threshold edit must keep
  // that (max_routes 0 = unlimited) rather than the 12000 thrift default.
  run({"10.0.0.1", "max-route", "pre-warning-only", "true"});
  const auto& pre = *peers()[0].pre_filter();
  EXPECT_EQ(*pre.max_routes(), 0);
  EXPECT_EQ(*pre.warning_limit(), 0);
  EXPECT_TRUE(*pre.warning_only());
  EXPECT_FALSE(peers()[0].post_filter().has_value());

  run({"10.0.0.1", "max-route", "post-warning-threshold", "30000"});
  const auto& post = *peers()[0].post_filter();
  EXPECT_EQ(*post.max_routes(), 0);
  EXPECT_EQ(*post.warning_limit(), 30000);
  EXPECT_FALSE(*post.warning_only());

  // A group with no struct of its own is the same as no group.
  addPeerGroup("SPINE");
  run({"10.0.0.2", "peer-group", "SPINE"});
  run({"10.0.0.2", "max-route", "pre-warning-threshold", "30000"});
  EXPECT_EQ(*peers()[1].pre_filter()->max_routes(), 0);
  EXPECT_EQ(*peers()[1].pre_filter()->warning_limit(), 30000);
}

TEST_F(CmdConfigBgpNeighborTestFixture, routeLimitNotReseededOncePresent) {
  run({"10.0.0.1", "max-route", "pre-filter", "45000"});
  run({"10.0.0.1", "max-route", "pre-warning-threshold", "30000"});
  EXPECT_EQ(*peers()[0].pre_filter()->max_routes(), 45000);
  EXPECT_EQ(*peers()[0].pre_filter()->warning_limit(), 30000);
}

TEST_F(CmdConfigBgpNeighborTestFixture, ttlSecurityHopsRange) {
  // bgpd throws at config load outside kMin/kMaxTtlSecurityHops (1..255).
  EXPECT_THAT(run({"10.0.0.1", "ttl-security-hops", "0"}), HasSubstr("Error"));
  EXPECT_THAT(
      run({"10.0.0.1", "ttl-security-hops", "256"}), HasSubstr("Error"));
  EXPECT_THAT(run({"10.0.0.1", "ttl-security-hops", "x"}), HasSubstr("Error"));
  EXPECT_TRUE(peers().empty())
      << "rejected first attribute must not leave a half-created peer";
  run({"10.0.0.1", "ttl-security-hops", "1"});
  EXPECT_EQ(peers()[0].ttl_security_hops().value_or(0), 1);
  run({"10.0.0.1", "ttl-security-hops", "255"});
  EXPECT_EQ(peers()[0].ttl_security_hops().value_or(0), 255);
}

TEST_F(CmdConfigBgpNeighborTestFixture, addPathMergesWithPeerGroupValue) {
  addPeerGroup("SPINE").add_path() = AddPath::SEND;
  run({"10.0.0.1", "peer-group", "SPINE"});

  // Enabling the other direction keeps the group's SEND.
  run({"10.0.0.1", "add-path", "receive", "true"});
  EXPECT_EQ(peers()[0].add_path().value_or(AddPath(0)), AddPath::BOTH);

  // Turning receive back off leaves the peer at the group's value explicitly.
  run({"10.0.0.1", "add-path", "receive", "false"});
  EXPECT_EQ(peers()[0].add_path().value_or(AddPath(0)), AddPath::SEND);
}

TEST_F(CmdConfigBgpNeighborTestFixture, addPathCannotDisableGroupDirection) {
  // The enum has no disabled value and an unset peer field inherits the
  // group, so "send false" here would be a no-op reported as success.
  addPeerGroup("SPINE").add_path() = AddPath::SEND;
  run({"10.0.0.1", "peer-group", "SPINE"});

  auto result = run({"10.0.0.1", "add-path", "send", "false"});
  EXPECT_THAT(result, HasSubstr("Error"));
  EXPECT_THAT(result, HasSubstr("SPINE"));
  EXPECT_FALSE(peers()[0].add_path().has_value());
}

TEST_F(CmdConfigBgpNeighborTestFixture, addPathNarrowsGroupBothToOneDirection) {
  addPeerGroup("SPINE").add_path() = AddPath::BOTH;
  run({"10.0.0.1", "peer-group", "SPINE"});

  // Dropping one direction of BOTH is representable: the peer gets an
  // explicit RECEIVE that overrides the group.
  run({"10.0.0.1", "add-path", "send", "false"});
  EXPECT_EQ(peers()[0].add_path().value_or(AddPath(0)), AddPath::RECEIVE);
  // ... but the remaining direction cannot be switched off (see above).
  EXPECT_THAT(
      run({"10.0.0.1", "add-path", "receive", "false"}), HasSubstr("Error"));
  EXPECT_EQ(peers()[0].add_path().value_or(AddPath(0)), AddPath::RECEIVE);
}

TEST_F(CmdConfigBgpNeighborTestFixture, timerRanges) {
  // Hold time: 0 or 3..65535 (bgpd refuses 1-2; the OPEN field is 16 bits).
  EXPECT_THAT(
      run({"10.0.0.1", "timers", "hold-time", "1"}), HasSubstr("Error"));
  EXPECT_THAT(
      run({"10.0.0.1", "timers", "hold-time", "2"}), HasSubstr("Error"));
  EXPECT_THAT(
      run({"10.0.0.1", "timers", "hold-time", "65536"}), HasSubstr("Error"));
  EXPECT_FALSE(sessionFileExists());
  run({"10.0.0.1", "timers", "hold-time", "0"});
  EXPECT_EQ(*peers()[0].bgp_peer_timers()->hold_time_seconds(), 0);
  run({"10.0.0.1", "timers", "hold-time", "3"});
  EXPECT_EQ(*peers()[0].bgp_peer_timers()->hold_time_seconds(), 3);
  run({"10.0.0.1", "timers", "hold-time", "65535"});
  EXPECT_EQ(*peers()[0].bgp_peer_timers()->hold_time_seconds(), 65535);

  // Graceful restart: 0..4095 (12-bit RFC 4724 field).
  EXPECT_THAT(
      run({"10.0.0.1", "graceful-restart", "restart-time", "4096"}),
      HasSubstr("Error"));
  run({"10.0.0.1", "graceful-restart", "restart-time", "4095"});
  EXPECT_EQ(
      peers()[0].bgp_peer_timers()->graceful_restart_seconds().value_or(0),
      4095);
}

TEST_F(CmdConfigBgpNeighborTestFixture, rejectedValueLeavesPeerUntouched) {
  run({"10.0.0.1", "remote-asn", "65000"});
  // A rejected timer must not leave a seeded timers struct behind.
  EXPECT_THAT(
      run({"10.0.0.1", "timers", "hold-time", "1"}), HasSubstr("Error"));
  EXPECT_FALSE(peers()[0].bgp_peer_timers().has_value());
  // Likewise a rejected route-limit value must not leave a seeded RouteLimit.
  EXPECT_THAT(
      run({"10.0.0.1", "max-route", "pre-warning-threshold", "-1"}),
      HasSubstr("Error"));
  EXPECT_FALSE(peers()[0].pre_filter().has_value());
  EXPECT_EQ(peers().size(), 1);
}

TEST_F(CmdConfigBgpNeighborTestFixture, distinctAddressesCreateDistinctPeers) {
  run({"10.0.0.1", "remote-asn", "65000"});
  run({"10.0.0.2", "remote-asn", "65001"});
  EXPECT_EQ(peers().size(), 2);
}

// ==============================================================================
// Help text — generated from the dispatch table and wired into `--help`
// ==============================================================================

TEST_F(CmdConfigBgpNeighborTestFixture, attributeHelpListsEveryAttribute) {
  const auto help = bgpNeighborAttributeHelp();
  const auto names = bgpNeighborAttributeNames();
  ASSERT_FALSE(names.empty());
  for (const auto& name : names) {
    // One line per attribute: indented name, then its value placeholder.
    EXPECT_THAT(help, HasSubstr("\n  " + name + " ")) << name;
    // ... and every listed spelling is one the parser accepts.
    std::vector<std::string> tokens = {"10.0.0.1"};
    folly::split(' ', name, tokens);
    tokens.emplace_back("x");
    EXPECT_EQ(BgpNeighborConfig(tokens).attr(), name);
  }
  // Spot-check the value shapes users most need to see.
  EXPECT_THAT(help, HasSubstr("passive"));
  EXPECT_THAT(help, HasSubstr("<true|false>"));
  EXPECT_THAT(help, HasSubstr("timers hold-time"));
  EXPECT_THAT(help, HasSubstr("0 or 3-65535"));
  EXPECT_THAT(help, HasSubstr("graceful-restart restart-time"));
  EXPECT_THAT(help, HasSubstr("0-4095"));
  EXPECT_THAT(help, HasSubstr("| auto"));
  EXPECT_THAT(help, HasSubstr("not supported"));
  EXPECT_THAT(help, Not(HasSubstr("connect-mode")));
}

TEST_F(CmdConfigBgpNeighborTestFixture, cliHelpFooterCarriesAttributeList) {
  CLI::App app;
  std::vector<std::string> args;
  CmdConfigProtocolBgpNeighborTraits::addCliArg(app, args);
  EXPECT_EQ(app.get_footer(), bgpNeighborAttributeHelp());
  EXPECT_THAT(app.help(), HasSubstr("timers hold-time"));
}

// ==============================================================================
// Reject paths — the error is surfaced and nothing is persisted
// ==============================================================================

TEST_F(CmdConfigBgpNeighborTestFixture, invalidValuesRejected) {
  EXPECT_THAT(run({"10.0.0.1", "rr-client", "maybe"}), HasSubstr("Invalid"));
  EXPECT_THAT(
      run({"10.0.0.1", "timers", "hold-time", "-1"}),
      HasSubstr("non-negative"));
  EXPECT_THAT(
      run({"10.0.0.1", "remote-asn", "not-a-number"}), HasSubstr("Invalid"));
  EXPECT_THAT(
      run({"10.0.0.1", "max-route", "pre-filter", "-5"}),
      HasSubstr("non-negative"));
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(CmdConfigBgpNeighborTestFixture, unsupportedAttributesRejected) {
  // No thrift representation (or not per-neighbor) — refuse instead of
  // persisting dead config.
  EXPECT_THAT(
      run({"10.0.0.1", "peer-port", "1179"}), HasSubstr("not supported"));
  EXPECT_THAT(
      run({"10.0.0.1", "bind-addr", "port", "1179"}),
      HasSubstr("not supported"));
  EXPECT_THAT(
      run({"10.0.0.1", "afi", "ipv4-labeled-unicast", "true"}),
      HasSubstr("not supported"));
  EXPECT_THAT(
      run({"10.0.0.1", "afi", "ipv6-labeled-unicast", "true"}),
      HasSubstr("not supported"));
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

} // namespace facebook::fboss
