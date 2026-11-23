// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/IPAddressV4.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"

#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <thrift/lib/cpp2/reflection/testing.h>
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "fboss/cli/fboss2/commands/show/route/CmdShowRouteDetails.h"
#include "fboss/cli/fboss2/commands/show/route/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

facebook::bgp::nsf_policy::NsfTeWeightEncoding createFpfEncoding() {
  facebook::bgp::nsf_policy::NsfFpfL2TeWeightEncoding fpfEncoding;
  fpfEncoding.rack_id() = 8;
  fpfEncoding.spine_id() = 16;
  fpfEncoding.remote_rack_capacity() = 8;

  facebook::bgp::nsf_policy::NsfTeWeightEncoding encoding;
  encoding.fpf_l2_encoding() = fpfEncoding;
  return encoding;
}

facebook::bgp::nsf_policy::NsfTeWeightEncoding createL2Encoding() {
  facebook::bgp::nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = facebook::bgp::nsf_policy::NsfL2TeWeightEncoding();
  return encoding;
}

facebook::bgp::bgp_policy::BgpPolicies createFpfPolicies() {
  auto encoding = createFpfEncoding();

  facebook::bgp::bgp_policy::LbwExtCommunityAction lbwAction;
  lbwAction.type() =
      facebook::bgp::bgp_policy::LbwExtCommunityActionType::DECODE_ALL;
  lbwAction.encoding_scheme() = encoding;

  facebook::bgp::bgp_policy::BgpPolicyAction action;
  action.lbw_ext_community_action() = lbwAction;

  facebook::bgp::bgp_policy::BgpPolicyTerm term;
  term.policy_action_entries()->push_back(action);

  facebook::bgp::bgp_policy::BgpPolicyStatement statement;
  statement.name() = "SAMPLE_GAR";
  statement.policy_version() = "1";
  statement.policy_entries()->push_back(term);

  facebook::bgp::bgp_policy::BgpPolicies policies;
  policies.bgp_policy_statements()->push_back(statement);
  return policies;
}

std::string createStandaloneFpfPolicyConfig() {
  facebook::bgp::thrift::BgpPolicyConfig config;
  config.policies() = createFpfPolicies();
  return apache::thrift::SimpleJSONSerializer::serialize<std::string>(config);
}

/*
 * Set up test data
 */
std::vector<RouteDetails> createRouteEntries() {
  std::vector<RouteDetails> routeEntries;
  RouteDetails routeEntry1, routeEntry2;

  // routeEntry1
  folly::IPAddressV6 ip1_1("2401:db00::");
  network::thrift::BinaryAddress binaryAddr1 =
      facebook::network::toBinaryAddress(ip1_1);

  IpPrefix ipPrefix1;
  ipPrefix1.ip() = binaryAddr1;
  ipPrefix1.prefixLength() = 32;
  routeEntry1.dest() = ipPrefix1;

  folly::IPAddressV6 ip1_2("2401:db00:e32f:8fc::2");
  network::thrift::BinaryAddress binaryAddr1_2 =
      facebook::network::toBinaryAddress(ip1_2);

  MplsAction mplsAction1_1;
  mplsAction1_1.action() = MplsActionCode::SWAP;
  mplsAction1_1.swapLabel() = 1;

  NextHopThrift nextHop1_1;
  nextHop1_1.address() = binaryAddr1_2;
  nextHop1_1.weight() = 1;
  nextHop1_1.mplsAction() = mplsAction1_1;
  nextHop1_1.role() = NextHopRole::BACKUP;

  ClientAndNextHops clAndNxthops1;
  clAndNxthops1.clientId() = 0;
  clAndNxthops1.nextHops()->emplace_back(nextHop1_1);
  clAndNxthops1.adminDistance() = AdminDistance::EBGP;
  clAndNxthops1.isPreferred() = true;
  clAndNxthops1.classID() = cfg::AclLookupClass::DST_CLASS_L3_DPR;
  routeEntry1.nextHopMulti()->emplace_back(clAndNxthops1);

  routeEntry1.action() = "Nexthops";
  routeEntry1.isConnected() = false;
  routeEntry1.adminDistance() = AdminDistance::EBGP;

  network::thrift::BinaryAddress binaryAddr1_3 =
      facebook::network::toBinaryAddress(ip1_2);
  binaryAddr1_3.ifName() = "Port-Channel304";

  MplsAction mplsAction1_2;
  mplsAction1_2.action() = MplsActionCode::PUSH;
  std::vector<MplsLabel> pushLabels = {2, 3};
  mplsAction1_2.pushLabels() = pushLabels;

  NextHopThrift nextHop1_2;
  nextHop1_2.address() = binaryAddr1_3;
  nextHop1_2.weight() = 1;
  nextHop1_2.mplsAction() = mplsAction1_2;
  nextHop1_2.role() = NextHopRole::BACKUP;
  NetworkTopologyInformation topologyInfo;
  topologyInfo.rack_id() = 5;
  topologyInfo.spine_id() = 17;
  topologyInfo.remote_rack_capacity() = 3;
  nextHop1_2.topologyInfo() = topologyInfo;
  routeEntry1.nextHops()->emplace_back(nextHop1_2);
  auto resolvedNextHop1_2 = nextHop1_2;
  resolvedNextHop1_2.weight() = 2;
  routeEntry1.resolvedNextHops() = {resolvedNextHop1_2};
  routeEntry1.classID() = cfg::AclLookupClass::DST_CLASS_L3_DPR;
  routeEntry1.resolvedNextHopSetID() = 100;
  routeEntry1.normalizedResolvedNextHopSetID() = 200;

  // routeEntry2
  folly::IPAddressV4 ip2_1("176.161.6.0");
  network::thrift::BinaryAddress binaryAddr2_1 =
      facebook::network::toBinaryAddress(ip2_1);

  IpPrefix ipPrefix2;
  ipPrefix2.ip() = binaryAddr2_1;
  ipPrefix2.prefixLength() = 32;
  routeEntry2.dest() = ipPrefix2;

  folly::IPAddressV4 ip2_2("240.161.6.0");
  network::thrift::BinaryAddress binaryAddr2_2 =
      facebook::network::toBinaryAddress(ip2_2);

  IfAndIP ifAndIp2;
  ifAndIp2.interfaceID() = 0;
  ifAndIp2.ip() = binaryAddr2_2;
  routeEntry2.fwdInfo()->emplace_back(ifAndIp2);

  ClientAndNextHops clAndNxthops2;
  clAndNxthops2.clientId() = 1;
  clAndNxthops2.nextHopAddrs()->emplace_back(binaryAddr2_2);
  clAndNxthops2.adminDistance() = AdminDistance::DIRECTLY_CONNECTED;
  clAndNxthops2.isPreferred() = true;
  clAndNxthops2.counterID() = "counter0";
  routeEntry2.nextHopMulti()->emplace_back(clAndNxthops2);

  routeEntry2.action() = "Nexthops";
  routeEntry2.isConnected() = true;
  routeEntry2.adminDistance() = AdminDistance::DIRECTLY_CONNECTED;
  routeEntry2.counterID() = "counter0";

  routeEntries.emplace_back(routeEntry1);
  routeEntries.emplace_back(routeEntry2);

  // routeEntry3 — SRv6 nexthop
  RouteDetails routeEntry3;

  folly::IPAddressV6 ip3_1("fc00::");
  network::thrift::BinaryAddress binaryAddr3_1 =
      facebook::network::toBinaryAddress(ip3_1);

  IpPrefix ipPrefix3;
  ipPrefix3.ip() = binaryAddr3_1;
  ipPrefix3.prefixLength() = 48;
  routeEntry3.dest() = ipPrefix3;

  folly::IPAddressV6 ip3_nh("2001:db8::1");
  network::thrift::BinaryAddress binaryAddr3_nh =
      facebook::network::toBinaryAddress(ip3_nh);

  folly::IPAddressV6 sid3_1("fc00:1::");
  folly::IPAddressV6 sid3_2("fc00:2::");
  network::thrift::BinaryAddress binarySid3_1 =
      facebook::network::toBinaryAddress(sid3_1);
  network::thrift::BinaryAddress binarySid3_2 =
      facebook::network::toBinaryAddress(sid3_2);

  NextHopThrift nextHop3;
  nextHop3.address() = binaryAddr3_nh;
  nextHop3.weight() = 1;
  nextHop3.srv6SegmentList() = {binarySid3_1, binarySid3_2};

  routeEntry3.nextHops()->emplace_back(nextHop3);
  routeEntry3.resolvedNextHops() = {nextHop3};
  routeEntry3.action() = "Nexthops";
  routeEntry3.isConnected() = false;

  routeEntries.emplace_back(routeEntry3);

  // routeEntry4 — named NHG route
  RouteDetails routeEntry4;

  folly::IPAddressV6 ip4_1("2800:2::");
  network::thrift::BinaryAddress binaryAddr4_1 =
      facebook::network::toBinaryAddress(ip4_1);

  IpPrefix ipPrefix4;
  ipPrefix4.ip() = binaryAddr4_1;
  ipPrefix4.prefixLength() = 64;
  routeEntry4.dest() = ipPrefix4;

  network::thrift::BinaryAddress binaryAddr4_nh =
      facebook::network::toBinaryAddress(folly::IPAddress("1.1.1.10"));

  NextHopThrift nextHop4;
  nextHop4.address() = binaryAddr4_nh;
  nextHop4.weight() = 1;

  ClientAndNextHops clAndNxthops4;
  clAndNxthops4.clientId() = 0;
  clAndNxthops4.nextHops()->emplace_back(nextHop4);
  clAndNxthops4.adminDistance() = AdminDistance::EBGP;
  clAndNxthops4.isPreferred() = true;
  NamedRouteDestination namedDest4Client;
  namedDest4Client.nextHopGroup_ref() = "nhg1";
  clAndNxthops4.namedRouteDestination() = namedDest4Client;
  routeEntry4.nextHopMulti()->emplace_back(clAndNxthops4);

  routeEntry4.nextHops()->emplace_back(nextHop4);
  routeEntry4.action() = "Nexthops";
  routeEntry4.isConnected() = false;
  routeEntry4.adminDistance() = AdminDistance::EBGP;

  NamedRouteDestination namedDest4;
  namedDest4.nextHopGroup_ref() = "nhg1";
  routeEntry4.namedRouteDestination() = namedDest4;

  routeEntries.emplace_back(routeEntry4);
  return routeEntries;
}

cli::ShowRouteDetailsModel createRouteModel() {
  cli::ShowRouteDetailsModel model;
  cli::RouteDetailEntry entry1, entry2;

  // entry1
  entry1.ip() = "2401:db00::";
  entry1.prefixLength() = 32;
  entry1.action() = "Nexthops";

  cli::MplsActionInfo mplsActionInfo1_1;
  mplsActionInfo1_1.action() = "SWAP";
  mplsActionInfo1_1.swapLabel() = 1;

  cli::NextHopInfo nextHopInfo1_1;
  nextHopInfo1_1.addr() = "2401:db00:e32f:8fc::2";
  nextHopInfo1_1.weight() = 1;
  nextHopInfo1_1.mplsAction() = mplsActionInfo1_1;
  nextHopInfo1_1.isBackup() = true;

  cli::ClientAndNextHops clnAndNxtHops1;
  clnAndNxtHops1.clientId() = 0;
  clnAndNxtHops1.nextHops()->emplace_back(nextHopInfo1_1);
  clnAndNxtHops1.adminDistance() = "20";
  clnAndNxtHops1.isPreferred() = true;
  clnAndNxtHops1.counterID() = "None";
  clnAndNxtHops1.classID() = "DST_CLASS_L3_DPR(20)";
  entry1.nextHopMulti()->emplace_back(clnAndNxtHops1);

  entry1.isConnected() = false;
  entry1.adminDistance() = "20";

  cli::MplsActionInfo mplsActionInfo1_2;
  mplsActionInfo1_2.action() = "PUSH";
  mplsActionInfo1_2.pushLabels() = {2, 3};

  cli::NextHopInfo nextHopInfo1_2;
  nextHopInfo1_2.addr() = "2401:db00:e32f:8fc::2";
  nextHopInfo1_2.weight() = 1;
  nextHopInfo1_2.mplsAction() = mplsActionInfo1_2;
  nextHopInfo1_2.ifName() = "Port-Channel304";
  nextHopInfo1_2.isBackup() = true;
  nextHopInfo1_2.preNormalizationWeight() = 2;
  NetworkTopologyInformation topologyInfo;
  topologyInfo.rack_id() = 5;
  topologyInfo.spine_id() = 17;
  topologyInfo.remote_rack_capacity() = 3;
  nextHopInfo1_2.topologyInfo() = topologyInfo;

  entry1.nextHops()->emplace_back(nextHopInfo1_2);
  entry1.counterID() = "None";
  entry1.classID() = "DST_CLASS_L3_DPR(20)";
  entry1.overridenEcmpMode() = "None";
  entry1.resolvedNextHopSetID() = 100;
  entry1.normalizedResolvedNextHopSetID() = 200;

  // entry 2
  entry2.ip() = "176.161.6.0";
  entry2.prefixLength() = 32;
  entry2.action() = "Nexthops";

  cli::NextHopInfo nextHopInfo2_1;
  nextHopInfo2_1.addr() = "240.161.6.0";
  nextHopInfo2_1.weight() = 0;

  cli::ClientAndNextHops clnAndNxtHops2;
  clnAndNxtHops2.clientId() = 1;
  clnAndNxtHops2.nextHops()->emplace_back(nextHopInfo2_1);
  clnAndNxtHops2.adminDistance() = "0";
  clnAndNxtHops2.isPreferred() = true;
  clnAndNxtHops2.counterID() = "counter0";
  clnAndNxtHops2.classID() = "None";
  entry2.nextHopMulti()->emplace_back(clnAndNxtHops2);

  entry2.isConnected() = true;
  entry2.adminDistance() = "0";

  cli::NextHopInfo nextHopInfo2_2;
  nextHopInfo2_2.addr() = "240.161.6.0";
  nextHopInfo2_2.interfaceID() = 0;

  entry2.nextHops()->emplace_back(nextHopInfo2_2);
  entry2.counterID() = "counter0";
  entry2.classID() = "None";
  entry2.overridenEcmpMode() = "None";

  // entry3 — SRv6
  cli::RouteDetailEntry entry3;
  entry3.ip() = "fc00::";
  entry3.prefixLength() = 48;
  entry3.action() = "Nexthops";
  entry3.isConnected() = false;
  entry3.adminDistance() = "None";
  entry3.counterID() = "None";
  entry3.classID() = "None";
  entry3.overridenEcmpMode() = "None";

  cli::NextHopInfo nextHopInfo3;
  nextHopInfo3.addr() = "2001:db8::1";
  nextHopInfo3.weight() = 1;
  nextHopInfo3.srv6SegmentList() = {"fc00:1::", "fc00:2::"};

  entry3.nextHops()->emplace_back(nextHopInfo3);

  // entry4 — named NHG
  cli::RouteDetailEntry entry4;
  entry4.ip() = "2800:2::";
  entry4.prefixLength() = 64;
  entry4.action() = "Nexthops";
  entry4.isConnected() = false;
  entry4.adminDistance() = "20";
  entry4.counterID() = "None";
  entry4.classID() = "None";
  entry4.overridenEcmpMode() = "None";
  entry4.namedNextHopGroup() = "nhg1";

  cli::NextHopInfo nextHopInfo4;
  nextHopInfo4.addr() = "1.1.1.10";
  nextHopInfo4.weight() = 1;

  cli::ClientAndNextHops clnAndNxtHops4;
  clnAndNxtHops4.clientId() = 0;
  clnAndNxtHops4.nextHops()->emplace_back(nextHopInfo4);
  clnAndNxtHops4.adminDistance() = "20";
  clnAndNxtHops4.isPreferred() = true;
  clnAndNxtHops4.counterID() = "None";
  clnAndNxtHops4.classID() = "None";
  clnAndNxtHops4.namedNextHopGroup() = "nhg1";
  entry4.nextHopMulti()->emplace_back(clnAndNxtHops4);

  entry4.nextHops()->emplace_back(nextHopInfo4);

  model.routeEntries() = {entry1, entry2, entry3, entry4};
  model.nsfTeWeightEncoding() = createFpfEncoding();

  return model;
}

class CmdShowRouteDetailsTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<facebook::fboss::RouteDetails> routeEntries;
  cli::ShowRouteDetailsModel normalizedModel;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    routeEntries = createRouteEntries();
    normalizedModel = createRouteModel();
  }

  void setupFpfPolicy(bool standalonePolicy = true) {
    setupMockedBgpServer();
    if (standalonePolicy) {
      EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
          .WillOnce([](std::string& config) {
            config = createStandaloneFpfPolicyConfig();
          });
      EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_)).Times(0);
      return;
    }
    EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
        .WillOnce([](std::string& config) { config.clear(); });
    EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_))
        .WillOnce([](facebook::bgp::thrift::BgpConfig& config) {
          config.policies() = createFpfPolicies();
        });
  }
};

TEST_F(CmdShowRouteDetailsTestFixture, queryClient) {
  setupMockedAgentServer();
  setupFpfPolicy();
  EXPECT_CALL(getMockAgent(), getRouteTableDetails(_))
      .WillOnce(Invoke([&](auto& entries) { entries = routeEntries; }));
  // Mock the calls made by populateAggregatePortMap and populateVlanPortMap
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](auto& entries) { entries.clear(); }));
  EXPECT_CALL(getMockAgent(), getAggregatePortTable(_))
      .WillOnce(Invoke([&](auto& entries) { entries.clear(); }));

  auto cmd = CmdShowRouteDetails();
  CmdShowRouteDetailsTraits::ObjectArgType queriedEntries;
  auto model = cmd.queryClient(localhost(), queriedEntries);

  EXPECT_THRIFT_EQ(normalizedModel, model);
}

TEST_F(CmdShowRouteDetailsTestFixture, preNormalizationWeightEquivalence) {
  const struct {
    int32_t preNormalizationWeight;
    int32_t postNormalizationWeight;
    bool showPreNormalizationWeight;
  } cases[] = {
      {0, 0, false},
      {0, 1, false},
      {1, 0, false},
      {1, 1, false},
      {2, 2, false},
      {0, 2, true},
      {2, 0, true},
      {1, 2, true},
      {2, 1, true},
  };
  for (const auto& testCase : cases) {
    SCOPED_TRACE(
        testing::Message() << "pre=" << testCase.preNormalizationWeight
                           << ", post=" << testCase.postNormalizationWeight);
    routeEntries[0].resolvedNextHops()->at(0).weight() =
        testCase.preNormalizationWeight;
    routeEntries[0].nextHops()->at(0).weight() =
        testCase.postNormalizationWeight;

    auto expectedModel = normalizedModel;
    expectedModel.nsfTeWeightEncoding().reset();
    auto& expectedNextHop =
        expectedModel.routeEntries()->at(0).nextHops()->at(0);
    expectedNextHop.weight() = testCase.postNormalizationWeight;
    expectedNextHop.preNormalizationWeight().reset();
    if (testCase.showPreNormalizationWeight) {
      expectedNextHop.preNormalizationWeight() =
          testCase.preNormalizationWeight;
    }

    auto cmd = CmdShowRouteDetails();
    const auto model = cmd.createModel(
        routeEntries, CmdShowRouteDetailsTraits::ObjectArgType());
    EXPECT_THRIFT_EQ(expectedModel, model);

    std::stringstream output;
    cmd.printOutput(model, output);
    EXPECT_THAT(output.str(), Not(HasSubstr("weight 0")));
    EXPECT_THAT(output.str(), Not(HasSubstr("pre-normalization weight: 0")));
    if (testCase.showPreNormalizationWeight) {
      EXPECT_THAT(output.str(), HasSubstr("(pre-normalization weight:"));
    } else {
      EXPECT_THAT(output.str(), Not(HasSubstr("(pre-normalization weight:")));
    }
  }
}

TEST_F(
    CmdShowRouteDetailsTestFixture,
    showPreNormalizationWeightsForWholeRoute) {
  auto& route = routeEntries[0];
  auto unchangedNextHop = route.nextHops()->at(0);
  unchangedNextHop.address() =
      facebook::network::toBinaryAddress(folly::IPAddress("2001:db8::2"));
  unchangedNextHop.weight() = 1;
  auto ecmpNextHop = unchangedNextHop;
  ecmpNextHop.address() =
      facebook::network::toBinaryAddress(folly::IPAddress("2001:db8::3"));
  ecmpNextHop.weight() = 0;
  route.nextHops()->push_back(unchangedNextHop);
  route.nextHops()->push_back(ecmpNextHop);
  route.resolvedNextHops()->push_back(unchangedNextHop);
  route.resolvedNextHops()->push_back(ecmpNextHop);

  auto cmd = CmdShowRouteDetails();
  auto model =
      cmd.createModel(routeEntries, CmdShowRouteDetailsTraits::ObjectArgType());
  const std::vector<int32_t> expectedPreNormalizationWeights{2, 1, 0};
  std::vector<int32_t> preNormalizationWeights;
  for (const auto& nextHop : *model.routeEntries()->at(0).nextHops()) {
    ASSERT_TRUE(nextHop.preNormalizationWeight().has_value());
    preNormalizationWeights.push_back(*nextHop.preNormalizationWeight());
  }
  EXPECT_EQ(preNormalizationWeights, expectedPreNormalizationWeights);
  // Other routes with unchanged weights do not get annotations.
  EXPECT_FALSE(model.routeEntries()
                   ->at(2)
                   .nextHops()
                   ->at(0)
                   .preNormalizationWeight()
                   .has_value());

  std::stringstream output;
  cmd.printOutput(model, output);
  EXPECT_THAT(
      output.str(), HasSubstr("weight 1 (pre-normalization weight: 2)"));
  EXPECT_THAT(
      output.str(), HasSubstr("weight 1 (pre-normalization weight: 1)"));
  EXPECT_THAT(output.str(), Not(HasSubstr("weight 0")));
  EXPECT_THAT(output.str(), Not(HasSubstr("pre-normalization weight: 0")));

  route.resolvedNextHops()->at(0).weight() = 1;
  route.resolvedNextHops()->at(1).weight() = 0;
  route.resolvedNextHops()->at(2).weight() = 1;
  model =
      cmd.createModel(routeEntries, CmdShowRouteDetailsTraits::ObjectArgType());
  for (const auto& nextHop : *model.routeEntries()->at(0).nextHops()) {
    EXPECT_FALSE(nextHop.preNormalizationWeight().has_value());
  }
}

TEST_F(CmdShowRouteDetailsTestFixture, queryNetworkEntries) {
  setupMockedAgentServer();
  setupFpfPolicy(false);
  EXPECT_CALL(getMockAgent(), getRouteTableDetails(_))
      .WillOnce(Invoke([&](auto& entries) { entries = routeEntries; }));
  // Mock the calls made by populateAggregatePortMap and populateVlanPortMap
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](auto& entries) { entries.clear(); }));
  EXPECT_CALL(getMockAgent(), getAggregatePortTable(_))
      .WillOnce(Invoke([&](auto& entries) { entries.clear(); }));

  auto cmd = CmdShowRouteDetails();
  std::vector<std::string> entries = {
      "2401:db00::/32", "176.161.6.0/32", "fc00::/48", "2800:2::/64"};
  CmdShowRouteDetailsTraits::ObjectArgType queriedEntries(entries);
  auto model = cmd.queryClient(localhost(), queriedEntries);

  EXPECT_THRIFT_EQ(normalizedModel, model);
}

TEST_F(CmdShowRouteDetailsTestFixture, queryIpRouteEntries) {
  setupMockedAgentServer();
  setupFpfPolicy();
  EXPECT_CALL(getMockAgent(), getRouteTableDetails(_))
      .WillOnce(Invoke([&](auto& entries) { entries = routeEntries; }));
  EXPECT_CALL(getMockAgent(), getIpRouteDetails(_, _, _))
      .WillOnce(
          Invoke([&](auto& entry, auto, auto) { entry = routeEntries[0]; }));
  // Mock the calls made by populateAggregatePortMap and populateVlanPortMap
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](auto& entries) { entries.clear(); }));
  EXPECT_CALL(getMockAgent(), getAggregatePortTable(_))
      .WillOnce(Invoke([&](auto& entries) { entries.clear(); }));

  auto cmd = CmdShowRouteDetails();
  std::vector<std::string> entries = {
      "2401:db00::/32", "176.161.6.0/32", "1.1.1.1"};
  CmdShowRouteDetailsTraits::ObjectArgType queriedEntries(entries);
  auto model = cmd.queryClient(localhost(), queriedEntries);

  // "1.1.1.1" resolves to "2401:db00::/32" via getIpRouteDetails, so only
  // entry1 and entry2 are returned (fc00::/48 is not in the queried set).
  cli::ShowRouteDetailsModel expectedModel;
  expectedModel.routeEntries() = {
      normalizedModel.routeEntries().value()[0],
      normalizedModel.routeEntries().value()[1]};
  expectedModel.nsfTeWeightEncoding() = createFpfEncoding();
  EXPECT_THRIFT_EQ(expectedModel, model);
}

TEST_F(CmdShowRouteDetailsTestFixture, printOutput) {
  std::stringstream ss;
  CmdShowRouteDetails().printOutput(normalizedModel, ss);

  std::string output = ss.str();
  std::string expectOutput = R"(
Network Address: 2401:db00::/32
> Client: BGPD (Admin Distance: 20)
      Nexthops:
        2401:db00:e32f:8fc::2 weight 1 MPLS -> SWAP : 1 (BACKUP)
      Counter Id: None
      Class Id: DST_CLASS_L3_DPR(20)
  Action: Nexthops
  Forwarding via:
    2401:db00:e32f:8fc::2 dev Port-Channel304 weight 1 (pre-normalization weight: 2) MPLS -> PUSH : {2,3} rack 5 spine id 17 remote weight 3 (BACKUP)
  Overridden ECMP mode: None
  Resolved NextHop Set ID: 100
  Normalized Resolved NextHop Set ID: 200

Network Address: 176.161.6.0/32 (connected)
> Client: STATIC_ROUTE (Admin Distance: 0)
      Nexthops:
        240.161.6.0 weight 1
      Counter Id: counter0
      Class Id: None
  Action: Nexthops
  Forwarding via:
    (i/f 0) 240.161.6.0 weight 1
  Overridden ECMP mode: None

Network Address: fc00::/48
  Action: Nexthops
  Forwarding via:
    2001:db8::1 weight 1 SRv6 SID List [fc00:1::,fc00:2::]
  Overridden ECMP mode: None

Network Address: 2800:2::/64
> Client: BGPD (Admin Distance: 20)
      Named Next Hop Group: nhg1
      Nexthops:
        1.1.1.10 weight 1
      Counter Id: None
      Class Id: None
  Action: Nexthops
  Forwarding via:
    1.1.1.10 weight 1
  Overridden ECMP mode: None
)";
  EXPECT_EQ(output, expectOutput);
}

TEST_F(CmdShowRouteDetailsTestFixture, printOutputUsesNsfEncodingScheme) {
  normalizedModel.nsfTeWeightEncoding() = createL2Encoding();

  std::stringstream ss;
  CmdShowRouteDetails().printOutput(normalizedModel, ss);

  EXPECT_THAT(ss.str(), HasSubstr("rack 5 remote weight 3"));
  EXPECT_THAT(ss.str(), Not(HasSubstr("spine id 17")));
}

TEST_F(CmdShowRouteDetailsTestFixture, printOutputHandlesMissingFpfSpineId) {
  normalizedModel.routeEntries()
      ->at(0)
      .nextHops()
      ->at(0)
      .topologyInfo()
      ->spine_id()
      .reset();

  std::stringstream ss;
  CmdShowRouteDetails().printOutput(normalizedModel, ss);

  // Still FPF-encoded, but spine_id was cleared: the nexthop line keeps its
  // other FPF fields and drops the spine identifier that would otherwise print.
  EXPECT_THAT(ss.str(), HasSubstr("rack 5 remote weight 3"));
  EXPECT_THAT(ss.str(), Not(HasSubstr("spine id 17")));
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowRouteDetailsTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowRouteDetailsTraits::description().empty());
  EXPECT_FALSE(CmdShowRouteDetails::sampleModel().routeEntries()->empty());
}

namespace {
cli::NextHopInfo makeTopologyNextHop(
    const std::string& addr,
    const NetworkTopologyInformation& topologyInfo) {
  cli::NextHopInfo nhInfo;
  nhInfo.addr() = addr;
  nhInfo.weight() = 1;
  nhInfo.topologyInfo() = topologyInfo;
  return nhInfo;
}

// Build a single-route model with the given nexthops on one BGP client, so
// printOutput exercises both the per-nexthop topology string (client Nexthops
// section) and the per-STSW/plane summary (Forwarding via section). The
// address->topology map consumed by the summary is derived from the nexthops
// themselves, so each nexthop's topology is specified exactly once.
cli::ShowRouteDetailsModel makeTopologyModel(
    const std::string& prefix,
    int prefixLength,
    const std::vector<cli::NextHopInfo>& nextHops) {
  cli::ShowRouteDetailsModel model;
  cli::RouteDetailEntry entry;
  entry.ip() = prefix;
  entry.prefixLength() = prefixLength;
  entry.action() = "Nexthops";
  entry.isConnected() = false;
  entry.overridenEcmpMode() = "None";

  cli::ClientAndNextHops client;
  client.clientId() = 0;
  client.adminDistance() = "20";
  client.isPreferred() = true;
  client.counterID() = "None";
  client.classID() = "None";
  client.nextHops() = nextHops;
  entry.nextHopMulti()->emplace_back(client);

  entry.nextHops() = nextHops;

  std::map<std::string, NetworkTopologyInformation> nhToTopoInfo;
  for (const auto& nextHop : nextHops) {
    if (nextHop.topologyInfo().has_value()) {
      nhToTopoInfo[nextHop.addr().value()] = nextHop.topologyInfo().value();
    }
  }
  entry.nhAddressToTopologyInfo() = nhToTopoInfo;
  model.routeEntries() = {entry};
  return model;
}
} // namespace

TEST_F(CmdShowRouteDetailsTestFixture, printOutputFpfTopology) {
  NetworkTopologyInformation topo3;
  topo3.spine_id() = 3;
  topo3.remote_rack_capacity() = 6;
  NetworkTopologyInformation topo5;
  topo5.spine_id() = 5;
  topo5.remote_rack_capacity() = 8;

  auto model = makeTopologyModel(
      "2401:db00:1::",
      64,
      {makeTopologyNextHop("2401:db00:1::1", topo3),
       makeTopologyNextHop("2401:db00:1::2", topo3),
       makeTopologyNextHop("2401:db00:1::3", topo5)});
  // spine id is only decoded (and printed) under the FPF L2 encoding.
  model.nsfTeWeightEncoding() = createFpfEncoding();

  std::stringstream ss;
  CmdShowRouteDetails().printOutput(model, ss);
  std::string expectOutput = R"(
Network Address: 2401:db00:1::/64
> Client: BGPD (Admin Distance: 20)
      Nexthops:
        2401:db00:1::1 weight 1 spine id 3 remote weight 6
        2401:db00:1::2 weight 1 spine id 3 remote weight 6
        2401:db00:1::3 weight 1 spine id 5 remote weight 8
      Counter Id: None
      Class Id: None
  Action: Nexthops
  Forwarding via:
    2401:db00:1::1 weight 1 spine id 3 remote weight 6
    2401:db00:1::2 weight 1 spine id 3 remote weight 6
    2401:db00:1::3 weight 1 spine id 5 remote weight 8
  Paths per stsw:
    Stsw 3: 2
    Stsw 5: 1
  Overridden ECMP mode: None
)";
  EXPECT_EQ(ss.str(), expectOutput);
}

TEST_F(CmdShowRouteDetailsTestFixture, printOutputNsfTopology) {
  NetworkTopologyInformation topo;
  topo.rack_id() = 2;
  topo.plane_id() = 1;
  topo.remote_rack_capacity() = 6;
  topo.spine_capacity() = 34;
  topo.local_rack_capacity() = 6;

  auto model = makeTopologyModel(
      "2402:db00::",
      64,
      {makeTopologyNextHop("2402:db00::1", topo),
       makeTopologyNextHop("2402:db00::2", topo)});
  model.nsfTeWeightEncoding() = createL2Encoding();

  std::stringstream ss;
  CmdShowRouteDetails().printOutput(model, ss);
  std::string expectOutput = R"(
Network Address: 2402:db00::/64
> Client: BGPD (Admin Distance: 20)
      Nexthops:
        2402:db00::1 weight 1 rack 2 plane 1 remote weight 6 spine weight 34 local weight 6
        2402:db00::2 weight 1 rack 2 plane 1 remote weight 6 spine weight 34 local weight 6
      Counter Id: None
      Class Id: None
  Action: Nexthops
  Forwarding via:
    2402:db00::1 weight 1 rack 2 plane 1 remote weight 6 spine weight 34 local weight 6
    2402:db00::2 weight 1 rack 2 plane 1 remote weight 6 spine weight 34 local weight 6
  Paths per plane:
    Plane 1: 2
  Overridden ECMP mode: None
)";
  EXPECT_EQ(ss.str(), expectOutput);
}

TEST_F(CmdShowRouteDetailsTestFixture, printOutputNsfTopologyWithSpineId) {
  // spine_id set under a non-FPF encoding: the per-nexthop line and the
  // per-plane/stsw summary must agree on the encoding as the discriminator.
  NetworkTopologyInformation topo;
  topo.rack_id() = 2;
  topo.plane_id() = 1;
  topo.spine_id() = 7;
  topo.remote_rack_capacity() = 6;

  auto model = makeTopologyModel(
      "2403:db00::", 64, {makeTopologyNextHop("2403:db00::1", topo)});
  model.nsfTeWeightEncoding() = createL2Encoding();

  std::stringstream ss;
  CmdShowRouteDetails().printOutput(model, ss);
  std::string expectOutput = R"(
Network Address: 2403:db00::/64
> Client: BGPD (Admin Distance: 20)
      Nexthops:
        2403:db00::1 weight 1 rack 2 plane 1 remote weight 6
      Counter Id: None
      Class Id: None
  Action: Nexthops
  Forwarding via:
    2403:db00::1 weight 1 rack 2 plane 1 remote weight 6
  Paths per plane:
    Plane 1: 1
  Overridden ECMP mode: None
)";
  EXPECT_EQ(ss.str(), expectOutput);
}

TEST_F(CmdShowRouteDetailsTestFixture, printOutputFpfTopologyIgnoresPlaneId) {
  NetworkTopologyInformation spineTopo;
  spineTopo.spine_id() = 3;
  NetworkTopologyInformation planeTopo;
  planeTopo.plane_id() = 1;

  auto model = makeTopologyModel(
      "2404:db00::",
      64,
      {makeTopologyNextHop("2404:db00::1", spineTopo),
       makeTopologyNextHop("2404:db00::2", planeTopo)});
  model.nsfTeWeightEncoding() = createFpfEncoding();

  std::stringstream ss;
  CmdShowRouteDetails().printOutput(model, ss);

  EXPECT_THAT(ss.str(), HasSubstr("Paths per stsw:"));
  EXPECT_THAT(ss.str(), HasSubstr("Stsw 3: 1"));
  EXPECT_THAT(ss.str(), Not(HasSubstr("Paths per plane")));
  EXPECT_THAT(ss.str(), Not(HasSubstr("Plane 1")));
}

} // namespace facebook::fboss
