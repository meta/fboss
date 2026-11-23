// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"
#include "fboss/agent/hw/sai/switch/SaiTunnelManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/state/IpTunnel.h"
#include "fboss/agent/types.h"

#include <optional>
#include <string>

using namespace facebook::fboss;

class AclTableManagerRedirectTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN | SetupStage::INTERFACE;
    ManagerTestBase::SetUp();
    intf0_ = testInterfaces[0];
  }

  int kPriority() {
    return 1;
  }

  uint8_t kDscp() {
    return 10;
  }

  std::shared_ptr<IpTunnel> makeEncapTunnel(
      const std::string& tunnelId = "encapTunnel0") {
    auto tunnel = std::make_shared<IpTunnel>(tunnelId);
    tunnel->setType(TunnelType::IP_IN_IP_ENCAP);
    tunnel->setUnderlayIntfId(InterfaceID(intf0_.id));
    tunnel->setTTLMode(cfg::TunnelMode::PIPE);
    tunnel->setDscpMode(cfg::TunnelMode::PIPE);
    tunnel->setEcnMode(cfg::TunnelMode::UNIFORM);
    tunnel->setTunnelTermType(cfg::TunnelTerminationType::P2P);
    tunnel->setSrcIP(folly::IPAddressV6("2401:db00::1"));
    tunnel->setDstIP(folly::IPAddressV6("2401:db00:ffff::1"));
    return tunnel;
  }

  cfg::RedirectNextHop tunnelRedirectNextHop() const {
    cfg::RedirectNextHop nh;
    nh.ip() = "2401:db00:ffff::1";
    nh.tunnelType() = TunnelType::IP_IN_IP_ENCAP;
    nh.tunnelId() = "encapTunnel0";
    return nh;
  }

  cfg::RedirectNextHop plainRedirectNextHop() const {
    cfg::RedirectNextHop nh;
    nh.ip() = "2401:db00::2";
    return nh;
  }

  std::shared_ptr<AclEntry> makeRedirectEntry(
      const cfg::RedirectNextHop& nh,
      std::optional<bool> enabled) {
    auto aclEntry =
        std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
    aclEntry->setDscp(kDscp());
    if (enabled.has_value()) {
      aclEntry->setEnabled(*enabled);
    }
    cfg::RedirectToNextHopAction redirectAction;
    redirectAction.redirectNextHops() = {nh};
    MatchAction matchAction;
    matchAction.setRedirectToNextHop(
        std::make_pair(redirectAction, MatchAction::NextHopSet()));
    aclEntry->setAclAction(matchAction);
    return aclEntry;
  }

  AclEntrySaiId addEntry(const std::shared_ptr<AclEntry>& aclEntry) {
    return saiManagerTable->aclTableManager().addAclEntry(
        aclEntry,
        cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
        nullptr /*state*/);
  }

  bool enabledInHw(AclEntrySaiId aclEntryId) {
    return saiApiTable->aclApi().getAttribute(
        aclEntryId, SaiAclEntryTraits::Attributes::Enabled{});
  }

  AclEntrySaiId entryId() {
    auto aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
        cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
    return saiManagerTable->aclTableManager()
        .getAclEntryHandle(aclTableHandle, kPriority(), "AclEntry1")
        ->aclEntry->adapterKey();
  }

  TestInterface intf0_;
};

TEST_F(AclTableManagerRedirectTest, addAclEntryWithTunnelEncapRedirect) {
  auto swTunnel = makeEncapTunnel("encapTunnel0");
  saiManagerTable->tunnelManager().addTunnel(swTunnel);

  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());

  cfg::RedirectNextHop redirectNh;
  redirectNh.ip() = "2401:db00:ffff::1";
  redirectNh.tunnelType() = TunnelType::IP_IN_IP_ENCAP;
  redirectNh.tunnelId() = "encapTunnel0";

  cfg::RedirectToNextHopAction redirectAction;
  redirectAction.redirectNextHops() = {redirectNh};

  MatchAction matchAction;
  matchAction.setRedirectToNextHop(
      std::make_pair(redirectAction, MatchAction::NextHopSet()));
  aclEntry->setAclAction(matchAction);

  AclEntrySaiId aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  auto aclEntryHandle = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority(), std::string("AclEntry1"));
  ASSERT_TRUE(aclEntryHandle);
  ASSERT_TRUE(aclEntryHandle->tunnelEncapNextHop);

  auto redirectOid =
      saiApiTable->aclApi()
          .getAttribute(
              aclEntryId, SaiAclEntryTraits::Attributes::ActionRedirect())
          .getData();
  EXPECT_EQ(redirectOid, aclEntryHandle->tunnelEncapNextHop->adapterKey());
}

TEST_F(AclTableManagerRedirectTest, removeAclEntryWithTunnelEncapRedirect) {
  auto swTunnel = makeEncapTunnel("encapTunnel0");
  saiManagerTable->tunnelManager().addTunnel(swTunnel);

  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());

  cfg::RedirectNextHop redirectNh;
  redirectNh.ip() = "2401:db00:ffff::1";
  redirectNh.tunnelType() = TunnelType::IP_IN_IP_ENCAP;
  redirectNh.tunnelId() = "encapTunnel0";

  cfg::RedirectToNextHopAction redirectAction;
  redirectAction.redirectNextHops() = {redirectNh};

  MatchAction matchAction;
  matchAction.setRedirectToNextHop(
      std::make_pair(redirectAction, MatchAction::NextHopSet()));
  aclEntry->setAclAction(matchAction);

  saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto& nhStore = saiStore->get<SaiTunnelEncapNextHopTraits>();
  EXPECT_EQ(nhStore.size(), 1);

  saiManagerTable->aclTableManager().removeAclEntry(
      aclEntry, cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());

  auto aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  auto aclEntryHandle = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority(), std::string("AclEntry1"));
  EXPECT_FALSE(aclEntryHandle);
  EXPECT_EQ(nhStore.size(), 0);
}

TEST_F(AclTableManagerRedirectTest, aclEntryRedirectMissingTunnel) {
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());

  cfg::RedirectNextHop redirectNh;
  redirectNh.ip() = "2401:db00:ffff::1";
  redirectNh.tunnelType() = TunnelType::IP_IN_IP_ENCAP;
  redirectNh.tunnelId() = "nonExistentTunnel";

  cfg::RedirectToNextHopAction redirectAction;
  redirectAction.redirectNextHops() = {redirectNh};

  MatchAction matchAction;
  matchAction.setRedirectToNextHop(
      std::make_pair(redirectAction, MatchAction::NextHopSet()));
  aclEntry->setAclAction(matchAction);

  EXPECT_THROW(
      saiManagerTable->aclTableManager().addAclEntry(
          aclEntry,
          cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
          nullptr /*state*/),
      FbossError);
}

// An entry software disabled, e.g. a redirect whose nexthop is unresolved,
// must be programmed with admin state off so lower priority entries match.
TEST_F(AclTableManagerRedirectTest, disabledAclEntryProgrammedDisabled) {
  auto aclEntryId = addEntry(makeRedirectEntry(plainRedirectNextHop(), false));
  EXPECT_FALSE(enabledInHw(aclEntryId));
}

TEST_F(
    AclTableManagerRedirectTest,
    disabledTunnelEncapRedirectProgrammedDisabled) {
  saiManagerTable->tunnelManager().addTunnel(makeEncapTunnel("encapTunnel0"));
  auto aclEntryId = addEntry(makeRedirectEntry(tunnelRedirectNextHop(), false));
  EXPECT_FALSE(enabledInHw(aclEntryId));
}

TEST_F(AclTableManagerRedirectTest, aclEntryEnabledByDefaultAndWhenSet) {
  auto unsetId =
      addEntry(makeRedirectEntry(plainRedirectNextHop(), std::nullopt));
  EXPECT_TRUE(enabledInHw(unsetId));
  saiManagerTable->aclTableManager().removeAclEntry(
      makeRedirectEntry(plainRedirectNextHop(), std::nullopt),
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());

  auto enabledId = addEntry(makeRedirectEntry(plainRedirectNextHop(), true));
  EXPECT_TRUE(enabledInHw(enabledId));
}

// Nexthop resolution changes flip the software enabled state; the HW admin
// state must follow in both directions.
TEST_F(AclTableManagerRedirectTest, aclEntryEnabledStateFollowsChanges) {
  auto disabled = makeRedirectEntry(plainRedirectNextHop(), false);
  auto enabled = makeRedirectEntry(plainRedirectNextHop(), true);
  const auto table = cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE();
  addEntry(disabled);
  EXPECT_FALSE(enabledInHw(entryId()));

  saiManagerTable->aclTableManager().changedAclEntry(
      disabled, enabled, table, nullptr /*state*/);
  EXPECT_TRUE(enabledInHw(entryId()));

  saiManagerTable->aclTableManager().changedAclEntry(
      enabled, disabled, table, nullptr /*state*/);
  EXPECT_FALSE(enabledInHw(entryId()));
}
