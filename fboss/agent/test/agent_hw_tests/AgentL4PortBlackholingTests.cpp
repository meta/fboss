/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"

#include "fboss/agent/packet/PktFactory.h"
#include "fboss/lib/CommonUtils.h"

#include "fboss/agent/test/gen-cpp2/production_features_types.h"

#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"

#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/IPAddress.h>
#include "fboss/agent/TxPacket.h"

using folly::IPAddress;
using folly::IPAddressV6;

namespace facebook::fboss {

class AgentL4PortBlackHolingTest : public AgentHwTest {
 private:
  int kNumL4Ports() const {
    return pow(2, 16) - 1;
  }
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_FORWARDING};
  }
  void pumpTraffic(bool isV6) {
    auto srcIp = IPAddress(isV6 ? "1001::1" : "101.101.0.1");
    auto dstIp = IPAddress(isV6 ? "2001::1" : "201.101.0.1");
    auto vlanId = getVlanIDForTx();
    auto mac = getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    // Router MAC as src would flap in the L2 table under SW learning.
    auto srcMac = utility::MacAddressGenerator().get(mac.u64HBO() + 1);
    enum class Dir { SRC_PORT, DST_PORT };
    for (auto l4Port = 1; l4Port <= kNumL4Ports(); ++l4Port) {
      for (auto dir : {Dir::SRC_PORT, Dir::DST_PORT}) {
        auto pkt = utility::makeUDPTxPacket(
            getSw(),
            vlanId,
            srcMac,
            mac,
            srcIp,
            dstIp,
            dir == Dir::SRC_PORT ? l4Port : 1,
            dir == Dir::DST_PORT ? l4Port : 1);
        sendPacketSwitchedAsync(std::move(pkt));
      }
    }
  }

 protected:
  void pumpTrafficAndVerify(PortID portId, bool isV6) {
    int numL4Ports = kNumL4Ports();
    auto original = *getLatestPortStats(portId).outUnicastPkts_();
    pumpTraffic(isV6);

    WITH_RETRIES_N_TIMED(90, std::chrono::milliseconds(1000), {
      auto newPortStats = getLatestPortStats(portId);
      auto current = *newPortStats.outUnicastPkts_();
      XLOGF(
          INFO,
          "Checking current port outPkts ({}) - "
          "original port outPkts: ({}) ==  "
          "2 * number of l4 ports ({})",
          current,
          original,
          2 * numL4Ports);
      EXPECT_EVENTUALLY_EQ(current - original, 2 * numL4Ports);
    });
  }

  void runTest() {
    auto setup = [=, this]() {
      const RouterID kRid{0};
      resolveNeighborAndProgramRoutes(
          utility::EcmpSetupAnyNPorts6(
              getProgrammedState(), getSw()->needL2EntryForNeighbor(), kRid),
          1);
      resolveNeighborAndProgramRoutes(
          utility::EcmpSetupAnyNPorts4(
              getProgrammedState(), getSw()->needL2EntryForNeighbor(), kRid),
          1);
    };
    auto verify = [=, this]() {
      PortID portId;
      if (FLAGS_hyper_port) {
        portId = masterLogicalHyperPortIds()[0];
      } else {
        portId = masterLogicalInterfacePortIds()[0];
      }
      // Sweep all L4 ports for both address families in a single setup/verify
      // cycle, with per-family before/after snapshots.
      {
        SCOPED_TRACE("v6");
        pumpTrafficAndVerify(portId, true /* isV6 */);
      }
      {
        SCOPED_TRACE("v4");
        pumpTrafficAndVerify(portId, false /* isV6 */);
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(AgentL4PortBlackHolingTest, udp) {
  runTest();
}

} // namespace facebook::fboss
