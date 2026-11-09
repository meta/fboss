// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/peer-group/CmdConfigProtocolBgpPeerGroup.h"
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

// The peer-group dispatcher only touches the BGP side of ConfigSession, which
// seeds from thrift schema defaults when neither a staged session nor a
// system bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpNeighborTest).
class CmdConfigBgpPeerGroupTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigBgpPeerGroupTestFixture()
      : CmdConfigTestBase("bgp_peer_group_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string run(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPeerGroup cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpPeerGroupConfig(tokens));
  }

  const std::vector<bgp::thrift::PeerGroup>& groups() {
    return ConfigSession::getInstance().getBgpConfig().peer_groups().ensure();
  }

  bgp::thrift::BgpConfig& config() {
    return ConfigSession::getInstance().getBgpConfig();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpPeerGroupConfig (arg) validation
// ==============================================================================

TEST_F(CmdConfigBgpPeerGroupTestFixture, argValidation) {
  // Bare create.
  EXPECT_EQ(BgpPeerGroupConfig({"SPINE"}).groupName(), "SPINE");
  EXPECT_TRUE(BgpPeerGroupConfig({"SPINE"}).attr().empty());

  // Single-token attribute.
  auto singleTok = BgpPeerGroupConfig({"SPINE", "remote-asn", "65000"});
  EXPECT_EQ(singleTok.attr(), "remote-asn");
  EXPECT_EQ(singleTok.values(), std::vector<std::string>({"65000"}));

  // Two-token attribute wins longest-prefix match.
  auto twoTok = BgpPeerGroupConfig({"SPINE", "timers", "hold-time", "30"});
  EXPECT_EQ(twoTok.attr(), "timers hold-time");
  EXPECT_EQ(twoTok.values(), std::vector<std::string>({"30"}));

  // Invalid: empty, empty name, unknown attribute, incomplete group.
  EXPECT_THROW(BgpPeerGroupConfig({}), std::invalid_argument);
  EXPECT_THROW(BgpPeerGroupConfig({""}), std::invalid_argument);
  EXPECT_THROW(
      BgpPeerGroupConfig({"SPINE", "no-such-attr", "1"}),
      std::invalid_argument);
  EXPECT_THROW(BgpPeerGroupConfig({"SPINE", "timers"}), std::invalid_argument);
}

// ==============================================================================
// Attribute handlers
// ==============================================================================

TEST_F(CmdConfigBgpPeerGroupTestFixture, bareCreate) {
  auto result = run({"SPINE"});
  EXPECT_THAT(result, HasSubstr("Successfully created BGP peer-group SPINE"));
  ASSERT_EQ(groups().size(), 1);
  EXPECT_EQ(*groups()[0].name(), "SPINE");
  // PeerGroup's only non-optional field is `name`; nothing else is seeded.
  EXPECT_FALSE(groups()[0].local_addr().has_value());
  EXPECT_TRUE(sessionFileExists());

  run({"LEAF"});
  ASSERT_EQ(groups().size(), 2);
  EXPECT_EQ(*groups()[1].name(), "LEAF");
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, bareCreateExisting) {
  run({"SPINE", "remote-asn", "65000"});
  // A bare re-create of an existing group is a no-op: it must say so rather
  // than claim a creation, and leave the group untouched.
  auto result = run({"SPINE"});
  EXPECT_THAT(result, HasSubstr("BGP peer-group SPINE already exists"));
  EXPECT_THAT(result, Not(HasSubstr("Successfully created")));
  ASSERT_EQ(groups().size(), 1);
  EXPECT_EQ(groups()[0].remote_as_4_byte().value_or(0), 65000);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, setRemoteAsn) {
  auto result = run({"SPINE", "remote-asn", "65000"});
  EXPECT_THAT(result, HasSubstr("65000"));
  ASSERT_EQ(groups().size(), 1);
  EXPECT_EQ(groups()[0].remote_as_4_byte().value_or(0), 65000);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, timersAccumulate) {
  run({"SPINE", "timers", "hold-time", "90"});
  run({"SPINE", "timers", "keepalive", "30"});
  run({"SPINE", "timers", "out-delay", "5"});
  run({"SPINE", "timers", "withdraw-unprog-delay", "10"});
  run({"SPINE", "graceful-restart", "restart-time", "120"});
  ASSERT_EQ(groups().size(), 1);
  const auto& timers = *groups()[0].bgp_peer_timers();
  EXPECT_EQ(*timers.hold_time_seconds(), 90);
  EXPECT_EQ(*timers.keep_alive_seconds(), 30);
  EXPECT_EQ(*timers.out_delay_seconds(), 5);
  EXPECT_EQ(*timers.withdraw_unprog_delay_seconds(), 10);
  EXPECT_EQ(timers.graceful_restart_seconds().value_or(0), 120);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, addPathMerge) {
  run({"SPINE", "add-path", "receive", "true"});
  EXPECT_EQ(groups()[0].add_path().value_or(AddPath(0)), AddPath::RECEIVE);

  run({"SPINE", "add-path", "send", "true"});
  EXPECT_EQ(groups()[0].add_path().value_or(AddPath(0)), AddPath::BOTH);

  run({"SPINE", "add-path", "receive", "false"});
  EXPECT_EQ(groups()[0].add_path().value_or(AddPath(0)), AddPath::SEND);

  run({"SPINE", "add-path", "send", "false"});
  EXPECT_FALSE(groups()[0].add_path().has_value());
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, passive) {
  // is_passive=true is PASSIVE_ONLY in bgpd, false is PASSIVE_ACTIVE (listen
  // and connect); the CLI exposes the flag rather than connect-mode names.
  run({"SPINE", "passive", "true"});
  EXPECT_TRUE(groups()[0].is_passive().value_or(false));

  run({"SPINE", "passive", "false"});
  EXPECT_FALSE(groups()[0].is_passive().value_or(true));

  auto result = run({"SPINE", "passive", "BOTH"});
  EXPECT_THAT(result, HasSubstr("Invalid"));
  EXPECT_FALSE(groups()[0].is_passive().value_or(true));
  EXPECT_THROW(
      run({"SPINE", "connect-mode", "PASSIVE"}), std::invalid_argument);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, boolAndStringAttributes) {
  run({"SPINE", "rr-client", "true"});
  run({"SPINE", "confed-peer", "false"});
  run({"SPINE", "redistribute-peer", "true"});
  run({"SPINE", "enhanced-route-refresh", "true"});
  run({"SPINE", "route-refresh", "true"});
  run({"SPINE", "remove-private-as", "true"});
  run({"SPINE", "enforce-first-as", "false"});
  run({"SPINE", "afi", "disable-ipv4-afi", "true"});
  run({"SPINE", "afi", "disable-ipv6-afi", "false"});
  run({"SPINE", "afi", "ipv4-over-ipv6-nh", "true"});
  run({"SPINE", "graceful-restart", "stateful-ha", "true"});
  run({"SPINE", "peer-tag", "FSW"});
  run({"SPINE", "ingress-policy", "IN_POLICY"});
  run({"SPINE", "egress-policy", "OUT_POLICY"});
  run({"SPINE", "local-asn", "64512"});
  run({"SPINE", "next-hop-self", "true"});
  run({"SPINE", "description", "spine", "group"});

  ASSERT_EQ(groups().size(), 1);
  const auto& group = groups()[0];
  EXPECT_TRUE(group.is_rr_client().value_or(false));
  EXPECT_FALSE(group.is_confed_peer().value_or(true));
  EXPECT_TRUE(group.is_redistribute_peer().value_or(false));
  EXPECT_TRUE(group.enhanced_route_refresh().value_or(false));
  EXPECT_TRUE(group.route_refresh().value_or(false));
  EXPECT_TRUE(group.remove_private_as().value_or(false));
  EXPECT_FALSE(group.enforce_first_as().value_or(true));
  EXPECT_TRUE(group.disable_ipv4_afi().value_or(false));
  EXPECT_FALSE(group.disable_ipv6_afi().value_or(true));
  EXPECT_TRUE(group.v4_over_v6_nexthop().value_or(false));
  EXPECT_TRUE(group.enable_stateful_ha().value_or(false));
  EXPECT_EQ(group.peer_tag().value_or(""), "FSW");
  EXPECT_EQ(group.ingress_policy_name().value_or(""), "IN_POLICY");
  EXPECT_EQ(group.egress_policy_name().value_or(""), "OUT_POLICY");
  EXPECT_EQ(group.local_as_4_byte().value_or(0), 64512);
  EXPECT_TRUE(group.next_hop_self().value_or(false));
  // Multi-token description is re-joined.
  EXPECT_EQ(group.description().value_or(""), "spine group");
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, routeLimits) {
  run({"SPINE", "max-route", "pre-filter", "45000"});
  run({"SPINE", "max-route", "post-filter", "50000"});
  run({"SPINE", "max-route", "post-warning-threshold", "48000"});
  // Parity attributes carried over from the deleted per-attribute commands.
  run({"SPINE", "max-route", "pre-warning-threshold", "40000"});
  run({"SPINE", "max-route", "pre-warning-only", "true"});
  run({"SPINE", "max-route", "post-warning-only", "false"});
  const auto& group = groups()[0];
  EXPECT_EQ(*group.pre_filter()->max_routes(), 45000);
  EXPECT_EQ(*group.post_filter()->max_routes(), 50000);
  EXPECT_EQ(*group.post_filter()->warning_limit(), 48000);
  EXPECT_EQ(*group.pre_filter()->warning_limit(), 40000);
  EXPECT_TRUE(*group.pre_filter()->warning_only());
  EXPECT_FALSE(*group.post_filter()->warning_only());
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, linkBandwidthAttributes) {
  // Same spellings and validation as the neighbor command: bgpd resolves the
  // link-bandwidth trio peer > group.
  run({"SPINE", "link-bandwidth", "10G"});
  EXPECT_EQ(groups()[0].link_bandwidth_bps().value_or(""), "10G");
  run({"SPINE", "link-bandwidth", "1.5G"});
  EXPECT_EQ(groups()[0].link_bandwidth_bps().value_or(""), "1.5G");
  run({"SPINE", "link-bandwidth", "auto"});
  EXPECT_EQ(groups()[0].link_bandwidth_bps().value_or(""), "auto");
  EXPECT_THAT(run({"SPINE", "link-bandwidth", "10Q"}), HasSubstr("Invalid"));
  EXPECT_THAT(run({"SPINE", "link-bandwidth", "10T"}), HasSubstr("Invalid"));
  EXPECT_EQ(groups()[0].link_bandwidth_bps().value_or(""), "auto");

  run({"SPINE", "advertise-lbw", "1"});
  run({"SPINE", "receive-lbw", "ACCEPT"}); // enum value name accepted
  const auto& group = groups()[0];
  EXPECT_EQ(
      static_cast<int>(group.advertise_link_bandwidth().value_or(
          facebook::neteng::fboss::bgp_attr::AdvertiseLinkBandwidth::DISABLE)),
      1);
  EXPECT_EQ(
      static_cast<int>(group.receive_link_bandwidth().value_or(
          facebook::neteng::fboss::bgp_attr::ReceiveLinkBandwidth::DISABLE)),
      1);
  EXPECT_THAT(
      run({"SPINE", "advertise-lbw", "99"}), HasSubstr("not a valid mode"));
  EXPECT_THAT(
      run({"SPINE", "receive-lbw", "SOMETIMES"}),
      HasSubstr("not a valid mode"));
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, ttlSecurityHopsRange) {
  // bgpd throws at config load outside kMin/kMaxTtlSecurityHops (1..255).
  EXPECT_THAT(run({"SPINE", "ttl-security-hops", "0"}), HasSubstr("Error"));
  EXPECT_THAT(run({"SPINE", "ttl-security-hops", "256"}), HasSubstr("Error"));
  EXPECT_THAT(run({"SPINE", "ttl-security-hops", "x"}), HasSubstr("Error"));
  EXPECT_TRUE(groups().empty())
      << "rejected first attribute must not leave a half-created group";
  run({"SPINE", "ttl-security-hops", "1"});
  EXPECT_EQ(groups()[0].ttl_security_hops().value_or(0), 1);
  run({"SPINE", "ttl-security-hops", "255"});
  EXPECT_EQ(groups()[0].ttl_security_hops().value_or(0), 255);
  EXPECT_THAT(run({"SPINE", "ttl-security-hops", "256"}), HasSubstr("Error"));
  EXPECT_EQ(groups()[0].ttl_security_hops().value_or(0), 255);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, valueValidation) {
  // ASN must fit in 4 bytes (RFC 6793).
  EXPECT_THAT(run({"SPINE", "remote-asn", "4294967296"}), HasSubstr("Invalid"));
  EXPECT_THAT(
      run({"SPINE", "local-asn", "18446744073709551615"}),
      HasSubstr("Invalid"));
  // The rejected value implicitly created SPINE; the rollback must remove it,
  // not just skip persisting it.
  EXPECT_TRUE(groups().empty())
      << "rejected first attribute must not leave a half-created group";
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, wrongValueCountRejected) {
  // Known attribute, but no value: parses fine, handler rejects at runtime.
  EXPECT_THAT(run({"SPINE", "remote-asn"}), HasSubstr("requires"));
  EXPECT_THAT(run({"SPINE", "timers", "hold-time"}), HasSubstr("requires"));
  EXPECT_THAT(run({"SPINE", "description"}), HasSubstr("requires"));
  // Too many values for single-value attributes.
  EXPECT_THAT(
      run({"SPINE", "remote-asn", "65000", "65001"}), HasSubstr("requires"));
  EXPECT_THAT(
      run({"SPINE", "rr-client", "true", "false"}), HasSubstr("requires"));
  EXPECT_TRUE(groups().empty())
      << "rejected first attribute must not leave a half-created group";
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(
    CmdConfigBgpPeerGroupTestFixture,
    rejectedFirstAttributeDoesNotMaterializePeerGroups) {
  EXPECT_FALSE(config().peer_groups().has_value());
  EXPECT_THAT(run({"SPINE", "remote-asn", "invalid"}), HasSubstr("Invalid"));
  EXPECT_FALSE(config().peer_groups().has_value());
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, overwriteSameAttributeLastWins) {
  run({"SPINE", "remote-asn", "65000"});
  run({"SPINE", "remote-asn", "65001"});
  ASSERT_EQ(groups().size(), 1);
  EXPECT_EQ(groups()[0].remote_as_4_byte().value_or(0), 65001);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, namedGroupsAreDistinct) {
  run({"SPINE", "remote-asn", "65000"});
  run({"LEAF", "remote-asn", "65001"});
  ASSERT_EQ(groups().size(), 2);
  // Re-referencing an existing group by name updates it, not appends —
  // and mutating one field preserves the group's other fields and leaves
  // the sibling group untouched.
  run({"SPINE", "local-asn", "64512"});
  ASSERT_EQ(groups().size(), 2);
  EXPECT_EQ(groups()[0].remote_as_4_byte().value_or(0), 65000);
  EXPECT_EQ(groups()[0].local_as_4_byte().value_or(0), 64512);
  EXPECT_EQ(groups()[1].remote_as_4_byte().value_or(0), 65001);
  EXPECT_FALSE(groups()[1].local_as_4_byte().has_value());
}

// ==============================================================================
// Inherited values: bgpd applies a group's timers to its members as a whole
// struct, so the first timer attribute must seed the rest from the global
// defaults instead of leaving them at the thrift default of 0.
// ==============================================================================

TEST_F(CmdConfigBgpPeerGroupTestFixture, timersSeededFromGlobal) {
  config().hold_time() = 180;
  run({"SPINE", "graceful-restart", "restart-time", "60"});

  ASSERT_EQ(groups().size(), 1);
  const auto& timers = *groups()[0].bgp_peer_timers();
  EXPECT_EQ(*timers.hold_time_seconds(), 180);
  EXPECT_EQ(*timers.keep_alive_seconds(), 60);
  EXPECT_EQ(*timers.out_delay_seconds(), 0);
  EXPECT_EQ(*timers.withdraw_unprog_delay_seconds(), 0);
  EXPECT_EQ(timers.graceful_restart_seconds().value_or(0), 60);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, timersNotReseededOncePresent) {
  run({"SPINE", "timers", "hold-time", "90"});
  run({"SPINE", "timers", "keepalive", "30"});
  // Seeding happens once: a later global change does not leak into a group
  // that already carries its own timers, and other fields are preserved.
  config().hold_time() = 180;
  run({"SPINE", "timers", "out-delay", "2"});

  const auto& timers = *groups()[0].bgp_peer_timers();
  EXPECT_EQ(*timers.hold_time_seconds(), 90);
  EXPECT_EQ(*timers.keep_alive_seconds(), 30);
  EXPECT_EQ(*timers.out_delay_seconds(), 2);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, routeLimitSeededUnlimited) {
  // A group with no RouteLimit imposes none on its members (bgpd skips the
  // check when the struct is absent), so the struct created for a
  // warning-only/threshold edit must keep that: max_routes 0 = unlimited,
  // not the 12000 thrift default, which would be a hard cap with teardown.
  run({"SPINE", "max-route", "pre-warning-only", "true"});
  const auto& pre = *groups()[0].pre_filter();
  EXPECT_EQ(*pre.max_routes(), 0);
  EXPECT_EQ(*pre.warning_limit(), 0);
  EXPECT_TRUE(*pre.warning_only());
  EXPECT_FALSE(groups()[0].post_filter().has_value());

  run({"SPINE", "max-route", "post-warning-threshold", "30000"});
  const auto& post = *groups()[0].post_filter();
  EXPECT_EQ(*post.max_routes(), 0);
  EXPECT_EQ(*post.warning_limit(), 30000);
  EXPECT_FALSE(*post.warning_only());
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, routeLimitNotReseededOncePresent) {
  run({"SPINE", "max-route", "pre-filter", "45000"});
  run({"SPINE", "max-route", "pre-warning-threshold", "30000"});
  EXPECT_EQ(*groups()[0].pre_filter()->max_routes(), 45000);
  EXPECT_EQ(*groups()[0].pre_filter()->warning_limit(), 30000);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, timerRanges) {
  // Hold time: 0 or 3..65535 (bgpd refuses 1-2; the OPEN field is 16 bits).
  EXPECT_THAT(run({"SPINE", "timers", "hold-time", "1"}), HasSubstr("Error"));
  EXPECT_THAT(run({"SPINE", "timers", "hold-time", "2"}), HasSubstr("Error"));
  EXPECT_THAT(
      run({"SPINE", "timers", "hold-time", "65536"}), HasSubstr("Error"));
  EXPECT_TRUE(groups().empty())
      << "rejected first attribute must not leave a half-created group";
  run({"SPINE", "timers", "hold-time", "0"});
  EXPECT_EQ(*groups()[0].bgp_peer_timers()->hold_time_seconds(), 0);
  run({"SPINE", "timers", "hold-time", "3"});
  EXPECT_EQ(*groups()[0].bgp_peer_timers()->hold_time_seconds(), 3);
  run({"SPINE", "timers", "hold-time", "65535"});
  EXPECT_EQ(*groups()[0].bgp_peer_timers()->hold_time_seconds(), 65535);

  // Graceful-restart time: 0..4095 (12-bit field, RFC 4724).
  EXPECT_THAT(
      run({"SPINE", "graceful-restart", "restart-time", "4096"}),
      HasSubstr("Error"));
  EXPECT_FALSE(groups()[0].bgp_peer_timers()->graceful_restart_seconds());
  run({"SPINE", "graceful-restart", "restart-time", "4095"});
  EXPECT_EQ(
      groups()[0].bgp_peer_timers()->graceful_restart_seconds().value_or(0),
      4095);
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, rejectedValueLeavesGroupUntouched) {
  run({"SPINE", "remote-asn", "65000"});
  // A rejected timer must not leave behind the timers struct that was seeded
  // for it: that would silently pin the group's members to the current
  // global hold time.
  EXPECT_THAT(run({"SPINE", "timers", "hold-time", "2"}), HasSubstr("Error"));
  ASSERT_EQ(groups().size(), 1);
  EXPECT_FALSE(groups()[0].bgp_peer_timers().has_value());
  EXPECT_EQ(groups()[0].remote_as_4_byte().value_or(0), 65000);
  // Likewise a rejected route-limit value must not leave a seeded RouteLimit.
  EXPECT_THAT(
      run({"SPINE", "max-route", "pre-warning-threshold", "-1"}),
      HasSubstr("Error"));
  EXPECT_FALSE(groups()[0].pre_filter().has_value());
  EXPECT_THAT(run({"SPINE", "ttl-security-hops", "0"}), HasSubstr("Error"));
  EXPECT_FALSE(groups()[0].ttl_security_hops().has_value());
}

// ==============================================================================
// Reject paths — the error is surfaced and nothing is persisted
// ==============================================================================

TEST_F(CmdConfigBgpPeerGroupTestFixture, invalidValuesRejected) {
  EXPECT_THAT(run({"SPINE", "rr-client", "maybe"}), HasSubstr("Invalid"));
  EXPECT_THAT(
      run({"SPINE", "timers", "hold-time", "-1"}), HasSubstr("non-negative"));
  EXPECT_THAT(
      run({"SPINE", "remote-asn", "not-a-number"}), HasSubstr("Invalid"));
  EXPECT_THAT(
      run({"SPINE", "max-route", "pre-filter", "-5"}),
      HasSubstr("non-negative"));
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(CmdConfigBgpPeerGroupTestFixture, unsupportedAttributesRejected) {
  // These attributes have no per-peer-group thrift field, so they are not in
  // the dispatch table and are refused at parse time as unknown attributes.
  EXPECT_THROW(run({"SPINE", "peer-port", "1179"}), std::invalid_argument);
  EXPECT_THROW(
      run({"SPINE", "afi", "ipv4-labeled-unicast", "true"}),
      std::invalid_argument);
  EXPECT_THROW(
      run({"SPINE", "afi", "ipv6-labeled-unicast", "true"}),
      std::invalid_argument);
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

} // namespace facebook::fboss
