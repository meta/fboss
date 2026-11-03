// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/neighbor/CmdConfigProtocolBgpNeighbor.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/neighbor/CmdDeleteProtocolBgpNeighbor.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

using namespace ::testing;

namespace facebook::fboss {

// Deleting only touches the BGP side of ConfigSession, which seeds from
// thrift schema defaults when neither a staged session nor a system
// bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpNeighborTest).
class CmdDeleteBgpNeighborTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteBgpNeighborTestFixture()
      : CmdConfigTestBase("bgp_neighbor_delete_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string configure(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpNeighbor cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpNeighborConfig(tokens));
  }

  std::string del(const std::vector<std::string>& tokens) {
    CmdDeleteProtocolBgpNeighbor cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpNeighborRef(tokens));
  }

  const std::vector<bgp::thrift::BgpPeer>& peers() {
    return *ConfigSession::getInstance().getBgpConfig().peers();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpNeighborRef (arg) validation
// ==============================================================================

TEST_F(CmdDeleteBgpNeighborTestFixture, argValidation) {
  // Address normalization mirrors the config command.
  EXPECT_EQ(BgpNeighborRef({"2001:DB8::1"}).peerAddr(), "2001:db8::1");
  EXPECT_EQ(BgpNeighborRef({"2001:db8::/64"}).peerAddr(), "2001:db8::/64");
  EXPECT_EQ(BgpNeighborRef({"10.0.0.1/24"}).peerAddr(), "10.0.0.0/24");

  // Invalid: empty, extra tokens, bad address.
  EXPECT_THROW(BgpNeighborRef({}), std::invalid_argument);
  EXPECT_THROW(BgpNeighborRef({"10.0.0.1", "10.0.0.2"}), std::invalid_argument);
  EXPECT_THROW(BgpNeighborRef({"not-an-ip"}), std::invalid_argument);
}

// ==============================================================================
// queryClient
// ==============================================================================

TEST_F(CmdDeleteBgpNeighborTestFixture, deleteExistingNeighbor) {
  configure({"10.0.0.1", "remote-asn", "65000"});
  configure({"10.0.0.2", "remote-asn", "65001"});
  ASSERT_EQ(peers().size(), 2);

  auto result = del({"10.0.0.1"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted BGP neighbor 10.0.0.1"));
  // The other neighbor survives untouched.
  ASSERT_EQ(peers().size(), 1);
  EXPECT_EQ(*peers()[0].peer_addr(), "10.0.0.2");
  EXPECT_EQ(peers()[0].remote_as_4_byte().value_or(0), 65001);
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(CmdDeleteBgpNeighborTestFixture, deleteByAnySpelling) {
  configure({"2001:db8::1", "remote-asn", "65000"});
  ASSERT_EQ(peers().size(), 1);
  // A different spelling of the same address deletes the same peer.
  auto result = del({"2001:DB8:0:0::1"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_TRUE(peers().empty());
}

TEST_F(CmdDeleteBgpNeighborTestFixture, deleteByEquivalentPrefixSpelling) {
  // bgpd masks dynamic-peer prefixes, so 10.0.0.1/24 and 10.0.0.2/24 name the
  // same neighbor; deleting via either spelling must find it.
  configure({"10.0.0.1/24", "remote-asn", "65000"});
  ASSERT_EQ(peers().size(), 1);
  EXPECT_EQ(*peers()[0].peer_addr(), "10.0.0.0/24");
  auto result = del({"10.0.0.2/24"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_TRUE(peers().empty());
}

// Delete mirrors add: an absent target is a success with a warning, never an
// error, so a replayed script stays idempotent.
TEST_F(CmdDeleteBgpNeighborTestFixture, deleteUnknownNeighborWarns) {
  auto result = del({"192.0.2.99"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Warning: BGP neighbor 192.0.2.99 does not exist; nothing to "
          "delete"));
  EXPECT_THAT(result, Not(HasSubstr("Error:")));
  // Nothing changed, so nothing is staged.
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after a no-op delete";
}

TEST_F(CmdDeleteBgpNeighborTestFixture, deleteTwiceIsIdempotent) {
  configure({"10.0.0.1", "remote-asn", "65000"});
  EXPECT_THAT(del({"10.0.0.1"}), HasSubstr("Successfully deleted"));
  EXPECT_TRUE(peers().empty());

  auto again = del({"10.0.0.1"});
  EXPECT_THAT(
      again,
      HasSubstr(
          "Warning: BGP neighbor 10.0.0.1 does not exist; nothing to delete"));
  EXPECT_THAT(again, Not(HasSubstr("Error:")));
  EXPECT_TRUE(peers().empty());
}

TEST_F(CmdDeleteBgpNeighborTestFixture, deleteUnknownLeavesOthersIntact) {
  configure({"10.0.0.1", "remote-asn", "65000"});
  auto result = del({"10.0.0.99"});
  EXPECT_THAT(result, HasSubstr("does not exist"));
  ASSERT_EQ(peers().size(), 1);
  EXPECT_EQ(*peers()[0].peer_addr(), "10.0.0.1");
}

} // namespace facebook::fboss
