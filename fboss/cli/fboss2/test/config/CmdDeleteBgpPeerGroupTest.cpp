// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/peer-group/CmdConfigProtocolBgpPeerGroup.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/peer-group/CmdDeleteProtocolBgpPeerGroup.h"
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
// CmdConfigBgpPeerGroupTest).
class CmdDeleteBgpPeerGroupTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteBgpPeerGroupTestFixture()
      : CmdConfigTestBase("bgp_peer_group_delete_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string configure(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPeerGroup cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpPeerGroupConfig(tokens));
  }

  std::string del(const std::vector<std::string>& tokens) {
    CmdDeleteProtocolBgpPeerGroup cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpPeerGroupRef(tokens));
  }

  const std::vector<bgp::thrift::PeerGroup>& groups() {
    return ConfigSession::getInstance().getBgpConfig().peer_groups().ensure();
  }

  // Reference a peer-group from a neighbor the way a committed config would,
  // without pulling the neighbor command (and its whole dispatch table) into
  // this test binary.
  void addPeerReferencing(
      const std::string& peerAddr,
      const std::string& groupName) {
    bgp::thrift::BgpPeer peer;
    peer.local_addr() = "10.0.0.1";
    peer.peer_addr() = peerAddr;
    peer.peer_group_name() = groupName;
    ConfigSession::getInstance().getBgpConfig().peers()->push_back(
        std::move(peer));
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpPeerGroupRef (arg) validation
// ==============================================================================

TEST_F(CmdDeleteBgpPeerGroupTestFixture, argValidation) {
  EXPECT_EQ(BgpPeerGroupRef({"SPINE"}).groupName(), "SPINE");

  // Invalid: empty, empty name, extra tokens.
  EXPECT_THROW(BgpPeerGroupRef({}), std::invalid_argument);
  EXPECT_THROW(BgpPeerGroupRef({""}), std::invalid_argument);
  EXPECT_THROW(BgpPeerGroupRef({"SPINE", "LEAF"}), std::invalid_argument);
}

// ==============================================================================
// queryClient
// ==============================================================================

TEST_F(CmdDeleteBgpPeerGroupTestFixture, deleteExistingGroup) {
  configure({"SPINE", "remote-asn", "65000"});
  configure({"LEAF", "remote-asn", "65001"});
  ASSERT_EQ(groups().size(), 2);

  auto result = del({"SPINE"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted BGP peer-group SPINE"));
  // The other group survives untouched.
  ASSERT_EQ(groups().size(), 1);
  EXPECT_EQ(*groups()[0].name(), "LEAF");
  EXPECT_EQ(groups()[0].remote_as_4_byte().value_or(0), 65001);
  EXPECT_TRUE(sessionFileExists());
}

// Delete mirrors add: an absent target is a success with a warning, never an
// error, so a replayed script stays idempotent.
TEST_F(CmdDeleteBgpPeerGroupTestFixture, deleteUnknownGroupWarns) {
  auto result = del({"NO-SUCH-GROUP"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Warning: BGP peer-group NO-SUCH-GROUP does not exist; nothing to "
          "delete"));
  EXPECT_THAT(result, Not(HasSubstr("Error:")));
  // Nothing changed, so nothing is staged.
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after a no-op delete";
}

TEST_F(CmdDeleteBgpPeerGroupTestFixture, deleteTwiceIsIdempotent) {
  configure({"SPINE", "remote-asn", "65000"});
  EXPECT_THAT(del({"SPINE"}), HasSubstr("Successfully deleted"));
  EXPECT_TRUE(groups().empty());

  auto again = del({"SPINE"});
  EXPECT_THAT(
      again,
      HasSubstr(
          "Warning: BGP peer-group SPINE does not exist; nothing to delete"));
  EXPECT_THAT(again, Not(HasSubstr("Error:")));
  EXPECT_TRUE(groups().empty());
}

TEST_F(CmdDeleteBgpPeerGroupTestFixture, deleteUnknownLeavesOthersIntact) {
  configure({"SPINE", "remote-asn", "65000"});
  auto result = del({"LEAF"});
  EXPECT_THAT(result, HasSubstr("does not exist"));
  ASSERT_EQ(groups().size(), 1);
  EXPECT_EQ(*groups()[0].name(), "SPINE");
}

TEST_F(CmdDeleteBgpPeerGroupTestFixture, deleteReferencedGroupRejected) {
  configure({"SPINE", "remote-asn", "65000"});
  addPeerReferencing("10.0.0.2", "SPINE");
  addPeerReferencing("10.0.0.3", "SPINE");

  auto result = del({"SPINE"});
  EXPECT_THAT(result, HasSubstr("still referenced"));
  // The refusal names every referencing neighbor so the user can act on it.
  EXPECT_THAT(result, HasSubstr("10.0.0.2"));
  EXPECT_THAT(result, HasSubstr("10.0.0.3"));
  ASSERT_EQ(groups().size(), 1);
  EXPECT_EQ(*groups()[0].name(), "SPINE");
  EXPECT_EQ(groups()[0].remote_as_4_byte().value_or(0), 65000);
}

TEST_F(CmdDeleteBgpPeerGroupTestFixture, deleteUnreferencedGroupSucceeds) {
  configure({"SPINE", "remote-asn", "65000"});
  configure({"LEAF", "remote-asn", "65001"});
  // A neighbor referencing SPINE must not block deleting LEAF.
  addPeerReferencing("10.0.0.2", "SPINE");

  auto result = del({"LEAF"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted BGP peer-group LEAF"));
  ASSERT_EQ(groups().size(), 1);
  EXPECT_EQ(*groups()[0].name(), "SPINE");
}

TEST_F(CmdDeleteBgpPeerGroupTestFixture, deleteThenRecreateStartsClean) {
  configure({"SPINE", "remote-asn", "65000"});
  auto result = del({"SPINE"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_TRUE(groups().empty());

  configure({"SPINE"});
  ASSERT_EQ(groups().size(), 1);
  // The recreated group starts clean — no fields leak across the
  // delete/recreate boundary.
  EXPECT_FALSE(groups()[0].remote_as_4_byte().has_value());
}

} // namespace facebook::fboss
