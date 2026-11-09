// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "fboss/agent/if/gen-cpp2/ctrl_types.h"

#include "fboss/cli/fboss2/commands/show/interface/traffic/CmdShowInterfaceTraffic.h"
#include "fboss/cli/fboss2/commands/show/interface/traffic/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "folly/Utility.h"

using namespace ::testing;

namespace facebook::fboss {

std::map<int32_t, facebook::fboss::PortInfoThrift> createFakePortInfo() {
  std::map<int32_t, PortInfoThrift> portMap;

  PortInfoThrift portEntry1;
  portEntry1.portId() = 1;
  portEntry1.name() = "eth1/1/1";

  portEntry1.operState() = facebook::fboss::PortOperState::UP;
  portEntry1.description() = "u-001: fsw001.p001 (F=spine:L=d-051)";
  portEntry1.speedMbps() = 100000;

  PortCounters inputCounter;
  PortCounters outputCounter;
  PortErrors inputErrors;
  PortErrors outputErrors;
  inputErrors.errors() = 0;
  inputErrors.discards() = 0;
  outputErrors.errors() = 0;
  outputErrors.discards() = 0;

  inputCounter.errors() = inputErrors;
  outputCounter.errors() = outputErrors;

  portEntry1.input() = inputCounter;
  portEntry1.output() = outputCounter;

  PortInfoThrift portEntry2;
  portEntry2.portId() = 2;
  portEntry2.name() = "eth2/1/1";

  portEntry2.operState() = facebook::fboss::PortOperState::UP;
  portEntry2.description() = "u-001: fsw002.p001 (F=spine:L=d-051)";
  portEntry2.speedMbps() = 200000;

  PortCounters inputCounter2;
  PortCounters outputCounter2;
  PortErrors inputErrors2;
  PortErrors outputErrors2;
  inputErrors2.errors() = 100;
  inputErrors2.discards() = 0;
  outputErrors2.errors() = 100;
  outputErrors2.discards() = 0;

  inputCounter2.errors() = inputErrors2;
  outputCounter2.errors() = outputErrors2;

  portEntry2.input() = inputCounter2;
  portEntry2.output() = outputCounter2;

  PortInfoThrift portEntry3;
  portEntry3.portId() = 3;
  portEntry3.name() = "eth3/1/1";

  portEntry3.operState() = facebook::fboss::PortOperState::UP;
  portEntry3.description() = "u-001: fsw003.p001 (F=spine:L=d-051)";
  portEntry3.speedMbps() = 400000;

  PortCounters inputCounter3;
  PortCounters outputCounter3;
  PortErrors inputErrors3;
  PortErrors outputErrors3;
  inputErrors3.errors() = 0;
  inputErrors3.discards() = 100;
  outputErrors3.errors() = 0;
  outputErrors3.discards() = 100;

  inputCounter3.errors() = inputErrors3;
  outputCounter3.errors() = outputErrors3;

  portEntry3.input() = inputCounter3;
  portEntry3.output() = outputCounter3;

  portMap[folly::copy(portEntry1.portId().value())] = portEntry1;
  portMap[folly::copy(portEntry2.portId().value())] = portEntry2;
  portMap[folly::copy(portEntry3.portId().value())] = portEntry3;

  return portMap;
}

std::map<std::string, int64_t> createFakePortCounters() {
  std::map<std::string, int64_t> portCounters{
      {"eth1/1/1.in_bytes.rate.60", 1234567890},
      {"eth1/1/1.in_unicast_pkts.rate.60", 1000},
      {"eth1/1/1.in_multicast_pkts.rate.60", 2000},
      {"eth1/1/1.in_broadcast_pkts.rate.60", 3000},
      {"eth1/1/1.out_bytes.rate.60", 9876543210},
      {"eth1/1/1.out_unicast_pkts.rate.60", 4000},
      {"eth1/1/1.out_multicast_pkts.rate.60", 5000},
      {"eth1/1/1.out_broadcast_pkts.rate.60", 6000},

      {"eth2/1/1.in_bytes.rate.60", 1234567890},
      {"eth2/1/1.in_unicast_pkts.rate.60", 1000},
      {"eth2/1/1.in_multicast_pkts.rate.60", 2000},
      {"eth2/1/1.in_broadcast_pkts.rate.60", 3000},
      {"eth2/1/1.out_bytes.rate.60", 9876543210},
      {"eth2/1/1.out_unicast_pkts.rate.60", 4000},
      {"eth2/1/1.out_multicast_pkts.rate.60", 5000},
      {"eth2/1/1.out_broadcast_pkts.rate.60", 6000},

      {"eth3/1/1.in_bytes.rate.60", 1234567890},
      {"eth3/1/1.in_unicast_pkts.rate.60", 1000},
      {"eth3/1/1.in_multicast_pkts.rate.60", 2000},
      {"eth3/1/1.in_broadcast_pkts.rate.60", 3000},
      {"eth3/1/1.out_bytes.rate.60", 9876543210},
      {"eth3/1/1.out_unicast_pkts.rate.60", 4000},
      {"eth3/1/1.out_multicast_pkts.rate.60", 5000},
      {"eth3/1/1.out_broadcast_pkts.rate.60", 6000},
  };
  return portCounters;
}

class CmdShowInterfaceTrafficTestFixture : public CmdHandlerTestBase {
 public:
  std::map<int32_t, facebook::fboss::PortInfoThrift> portInfo;
  std::map<std::string, int64_t> intCounters;
  std::vector<std::string> queriedIfs;
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    portInfo = createFakePortInfo();
    intCounters = createFakePortCounters();
  }
};

TEST_F(CmdShowInterfaceTrafficTestFixture, createModel) {
  auto cmd = CmdShowInterfaceTraffic();
  auto model = cmd.createModel(portInfo, intCounters, queriedIfs);
  auto errorCounters = model.error_counters().value();
  auto trafficCounters = model.traffic_counters().value();

  EXPECT_EQ(errorCounters.size(), 1);
  EXPECT_EQ(trafficCounters.size(), 3);

  EXPECT_EQ(trafficCounters[0].peerIf().value(), "fsw001.p001");
  EXPECT_EQ(trafficCounters[1].peerIf().value(), "fsw002.p001");
  EXPECT_EQ(trafficCounters[2].peerIf().value(), "fsw003.p001");

  EXPECT_NEAR(trafficCounters[0].inPct().value(), 9.87654, 0.0001);
  EXPECT_NEAR(trafficCounters[0].outPct().value(), 79.0123, 0.0001);
  EXPECT_NEAR(trafficCounters[1].inPct().value(), 4.93827, 0.0001);
  EXPECT_NEAR(trafficCounters[1].outPct().value(), 39.5062, 0.0001);
  EXPECT_NEAR(trafficCounters[2].inPct().value(), 2.46914, 0.0001);
  EXPECT_NEAR(trafficCounters[2].outPct().value(), 19.7531, 0.0001);
}

TEST_F(CmdShowInterfaceTrafficTestFixture, printOutput) {
  auto cmd = CmdShowInterfaceTraffic();
  auto model = cmd.createModel(portInfo, intCounters, queriedIfs);

  std::stringstream ss;
  cmd.printOutput(model, ss);

  std::string output = ss.str();

  std::string expectedOutput =
      "ERRORS 1 interfaces, watch for any incrementing counters:\n"
      " Interface Name  Peer         Status  FCS  Align  Symbol  Rx   Runts  Giants  Tx  \n"
      "---------------------------------------------------------------------------------------------\n"
      " eth2/1/1        fsw002.p001  up      0    0      0       100  0      0       100 \n\n"
      " Interface Name  Peer         Intvl  InMbps    InPct  InKpps  OutMbps    OutPct  OutKpps \n"
      "---------------------------------------------------------------------------------------------------\n"
      " eth1/1/1        fsw001.p001  0:60   9876.54   9.88%  6.00    79012.35   79.01%  15.00   \n"
      " eth2/1/1        fsw002.p001  0:60   9876.54   4.94%  6.00    79012.35   39.51%  15.00   \n"
      " eth3/1/1        fsw003.p001  0:60   9876.54   2.47%  6.00    79012.35   19.75%  15.00   \n"
      " Total           --           --     29629.63  4.23%  18.00   237037.04  33.86%  45.00   \n\n";

  EXPECT_EQ(expectedOutput, output);
}

TEST_F(CmdShowInterfaceTrafficTestFixture, extractExpectedPort) {
  auto cmd = CmdShowInterfaceTraffic();
  // Meta description conventions keep their parsed peer labels.
  EXPECT_EQ(cmd.extractExpectedPort(""), "--");
  EXPECT_EQ(
      cmd.extractExpectedPort("u-001: fsw001.p001 (F=spine:L=d-051)"),
      "fsw001.p001");
  EXPECT_EQ(cmd.extractExpectedPort("rsw001.ssw001.s001"), "ssw001.s001");
  EXPECT_EQ(cmd.extractExpectedPort("fsw001.s001.p1"), "fsw001.s001");
  EXPECT_EQ(cmd.extractExpectedPort("host.port"), "port");
  // Free-form descriptions that lack the expected tokens must not index
  // past the split result; they are shown verbatim or fall back
  // to the generic dotted form.
  EXPECT_EQ(
      cmd.extractExpectedPort("Port11.lab-tgen:T=us:U=lab:TGEN=ssw_mimic"),
      "lab-tgen:T=us:U=lab:TGEN=ssw_mimic");
  EXPECT_EQ(cmd.extractExpectedPort("ssw_mimic"), "ssw_mimic");
  EXPECT_EQ(cmd.extractExpectedPort("fsw-only-no-dot"), "fsw-only-no-dot");
  EXPECT_EQ(
      cmd.extractExpectedPort("peer-switch:T=us:U=lab:B=po301"),
      "peer-switch:T=us:U=lab:B=po301");
  EXPECT_EQ(cmd.extractExpectedPort("uplink"), "uplink");
}

TEST_F(CmdShowInterfaceTrafficTestFixture, createModelFreeFormDescription) {
  PortInfoThrift port;
  port.portId() = 4;
  port.name() = "eth4/1/1";
  port.operState() = facebook::fboss::PortOperState::UP;
  port.description() = "Port11.lab-tgen:T=us:U=lab:TGEN=ssw_mimic";
  port.speedMbps() = 100000;
  portInfo[4] = port;
  intCounters["eth4/1/1.in_bytes.rate.60"] = 1234567890;
  intCounters["eth4/1/1.out_bytes.rate.60"] = 9876543210;

  auto cmd = CmdShowInterfaceTraffic();
  auto model = cmd.createModel(portInfo, intCounters, queriedIfs);
  const auto trafficCounters = model.traffic_counters().value();
  ASSERT_EQ(trafficCounters.size(), 4);
  EXPECT_EQ(
      trafficCounters[3].peerIf().value(),
      "lab-tgen:T=us:U=lab:TGEN=ssw_mimic");
}

// A box with every front panel port down and no transceivers reports zero for
// every rate counter, so isInterestingTraffic() filters out every row and the
// Total is computed over an empty set. Totals must read 0.00%.
TEST_F(CmdShowInterfaceTrafficTestFixture, printOutputNoInterestingTraffic) {
  auto cmd = CmdShowInterfaceTraffic();
  auto model = cmd.createModel(portInfo, {} /* intCounters */, queriedIfs);

  EXPECT_TRUE(model.traffic_counters()->empty());

  std::stringstream ss;
  cmd.printOutput(model, ss);

  const std::string output = ss.str();
  EXPECT_NE(output.find("Total"), std::string::npos);
  EXPECT_NE(output.find("0.00%"), std::string::npos);
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowInterfaceTrafficTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowInterfaceTrafficTraits::description().empty());
  EXPECT_FALSE(
      CmdShowInterfaceTraffic::sampleModel().traffic_counters()->empty());
}
} // namespace facebook::fboss
