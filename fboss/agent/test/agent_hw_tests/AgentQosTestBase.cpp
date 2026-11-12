/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/agent_hw_tests/AgentQosTestBase.h"

#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"

namespace facebook::fboss {

void AgentQosTestBase::verifyDscpQueueMapping(
    const std::map<int, std::vector<uint8_t>>& queueToDscp) {
  auto setup = [=, this]() {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    resolveNeighborAndProgramRoutes(ecmpHelper, kDefaultEcmpWidth);
  };

  auto verify = [=, this]() {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto portId = ecmpHelper.ecmpPortDescriptorAt(0).phyPortID();
    std::optional<SystemPortID> sysPortId;
    if (getSw()->getSwitchInfoTable().haveVoqSwitches()) {
      auto switchId = switchIdForPort(portId);
      sysPortId = getSystemPortID(portId, getProgrammedState(), switchId);
    }
    for (bool frontPanel : {false, true}) {
      XLOG(DBG2) << "verify send packets "
                 << (frontPanel ? "out of port" : "switched");
      if (frontPanel) {
        learnSrcMacOnEcmpTxPortIfPending(kDefaultEcmpWidth);
      }
      utility::sendPktAndVerifyQueueHit(
          queueToDscp,
          getSw(),
          [this, frontPanel](int dscp) { sendPacket(dscp, frontPanel); },
          portId,
          sysPortId);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

void AgentQosTestBase::sendPacket(uint8_t dscp, bool frontPanel) {
  auto vlanId = getVlanIDForTx();
  auto intfMac =
      getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
  auto txPacket = utility::makeUDPTxPacket(
      getSw(),
      vlanId,
      srcMac, // src mac
      intfMac, // dst mac
      folly::IPAddressV6(kTestSrcIpV6), // src ip
      folly::IPAddressV6(kTestDstIpV6), // dst ip
      kTestSrcPort, // l4 src port
      kTestDstPort, // l4 dst port
      dscp << 2, // shifted by 2 bits for ECN
      255 // ttl
  );
  // port is in LB mode, so it will egress and immediately loop back.
  // Since it is not re-written, it should hit the pipeline as if it
  // ingressed on the port, and be properly queued.
  if (frontPanel) {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto outPort =
        ecmpHelper.ecmpPortDescriptorAt(kDefaultEcmpWidth).phyPortID();
    getSw()->sendPacketOutOfPortAsync(std::move(txPacket), outPort);
  } else {
    sendPacketSwitchedAsync(std::move(txPacket));
  }
}

} // namespace facebook::fboss
