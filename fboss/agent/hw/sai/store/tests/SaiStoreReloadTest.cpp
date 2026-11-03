/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class SaiStoreReloadTest : public SaiStoreTest {
 protected:
  SaiPortTraits::CreateAttributes makeAttrs(
      uint32_t lane,
      uint32_t speed,
      std::optional<bool> adminStateOpt = true) {
    std::vector<uint32_t> lanes;
    lanes.push_back(lane);
    return SaiPortTraits::CreateAttributes{
        lanes,        speed,        adminStateOpt, std::nullopt,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
        std::nullopt, std::nullopt,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
        std::nullopt, // Port Fabric Isolate
#endif
        std::nullopt, std::nullopt, std::nullopt,  std::nullopt, std::nullopt,
        std::nullopt, std::nullopt, std::nullopt,  std::nullopt, std::nullopt,
        std::nullopt, // TAM object
        std::nullopt, // Ingress Mirror Session
        std::nullopt, // Egress Mirror Session
        std::nullopt, // Ingress Sample Packet
        std::nullopt, // Egress Sample Packet
        std::nullopt, // Ingress mirror sample session
        std::nullopt, // Egress mirror sample session
        std::nullopt, // PRBS Polynomial
        std::nullopt, // PRBS Config
        std::nullopt, // Ingress macsec acl
        std::nullopt, // Egress macsec acl
        std::nullopt, // System Port Id
        std::nullopt, // PTP Mode
        std::nullopt, // PFC Mode
        std::nullopt, // PFC Priorities
#if !defined(TAJO_SDK)
        std::nullopt, // PFC Rx Priorities
        std::nullopt, // PFC Tx Priorities
#endif
        std::nullopt, // TC to Priority Group map
        std::nullopt, // PFC Priority to Queue map
        std::nullopt, // PFC Priority to Priority Group map
#if defined(SAI_CBFC_SUPPORTED)
        std::nullopt, // TC to VC map
        std::nullopt, // Queue to VC map
        std::nullopt, // CBFC sender credit size
        std::nullopt, // CBFC sender packet overhead
        std::nullopt, // CBFC sender credit limit
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        std::nullopt, // Inter Frame Gap
#endif
        std::nullopt, // Link Training Enable
        std::nullopt, // FDR Enable
        std::nullopt, // Rx Lane Squelch Enable
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
        std::nullopt, // PFC Deadlock Detection Interval
        std::nullopt, // PFC Deadlock Recovery Interval
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
        std::nullopt, // ARS enable
        std::nullopt, // ARS scaling factor
        std::nullopt, // ARS port load past weight
        std::nullopt, // ARS port load future weight
#endif
        std::nullopt, // Reachability Group
        std::nullopt, // CondEntropyRehashEnable
        std::nullopt, // CondEntropyRehashPeriodUS
        std::nullopt, // CondEntropyRehashSeed
        std::nullopt, // ShelEnable
        std::nullopt, // FecErrorDetectEnable
        std::nullopt, // AmIdles
        std::nullopt, // FabricSystemPort
        std::nullopt, // StaticModuleId
        std::nullopt, // IsHyperPortMember
        std::nullopt, // HyperPortMemberList
        std::nullopt, // PfcMonitorDirection
        std::nullopt, // QosDot1pToTcMap
        std::nullopt, // QosTcAndColorToDot1pMap
        std::nullopt, // QosIngressBufferProfileList
        std::nullopt, // QosEgressBufferProfileList
        std::nullopt, // CablePropagationDelayMediaType
        std::nullopt, // LinkScanMode
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
        std::nullopt, // LlrModeLocal
        std::nullopt, // LlrModeRemote
        std::nullopt, // LlrProfile
#endif
        std::nullopt, // PfcPauseDurationOverride
        std::nullopt, // Ingress ACL
        std::nullopt, // IsolationGroup
        std::nullopt, // Metadata
        std::nullopt, // CsigTagAction
        std::nullopt, // CsigLinkLocator
    };
  }

  PortSaiId createPort(uint32_t lane) {
    SaiPortTraits::CreateAttributes c = makeAttrs(lane, 100000);
    return saiApiTable->portApi().create<SaiPortTraits>(c, 0);
  }

  HostifTrapGroupSaiId createTrapGroup(uint32_t queue) {
    SaiHostifTrapGroupTraits::CreateAttributes c{queue, std::nullopt};
    return saiApiTable->hostifApi().create<SaiHostifTrapGroupTraits>(c, 0);
  }

  HostifTrapSaiId createTrap(int32_t trapType) {
    SaiHostifTrapTraits::CreateAttributes c{
        trapType, SAI_PACKET_ACTION_TRAP, std::nullopt, std::nullopt};
    return saiApiTable->hostifApi().create<SaiHostifTrapTraits>(c, 0);
  }

  void SetUp() override {
    SaiStoreTest::SetUp();
    for (auto i = 1; i <= 64; i++) {
      createPort(i);
    }
    createTrapGroup(2);
    createTrapGroup(3);
  }
};

TEST_F(SaiStoreReloadTest, reload) {
  SaiStore s(0);
  s.reload();
  EXPECT_EQ(s.get<SaiHostifTrapGroupTraits>().size(), 2);
  EXPECT_EQ(s.get<SaiPortTraits>().size(), 64);
  EXPECT_EQ(s.get<SaiFdbTraits>().size(), 0);
}

TEST_F(SaiStoreReloadTest, reloadSelective) {
  SaiStore s(0);
  s.reload();

  folly::dynamic hwSwitch = folly::dynamic::object;

  auto adapterKeys = s.adapterKeysFollyDynamic();
  auto adapterKeys2AdapterHostKeys =
      s.adapterKeys2AdapterHostKeysFollyDynamic();

  SaiStore s1(0);
  s1.reload(&adapterKeys, &adapterKeys2AdapterHostKeys, {SAI_OBJECT_TYPE_PORT});
  // reloading only ports
  EXPECT_EQ(s1.get<SaiHostifTrapGroupTraits>().size(), 0);
  EXPECT_EQ(s1.get<SaiPortTraits>().size(), 64);
  EXPECT_EQ(s1.get<SaiFdbTraits>().size(), 0);
}
