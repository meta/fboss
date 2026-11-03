/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/api/SwitchApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

using namespace facebook::fboss;

class SwitchApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    switchApi = std::make_unique<SwitchApi>();
    switchId = SwitchSaiId{fs->switchManager.create(FakeSwitch())};
  }
  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<SwitchApi> switchApi;
  SwitchSaiId switchId;
};

TEST_F(SwitchApiTest, setGetInit) {
  SaiSwitchTraits::Attributes::InitSwitch init{true};
  switchApi->setAttribute(switchId, init);
  SaiSwitchTraits::Attributes::InitSwitch blank{false};
  EXPECT_TRUE(switchApi->getAttribute(switchId, blank));
}

TEST_F(SwitchApiTest, getNumPorts) {
  SaiSwitchTraits::Attributes::PortNumber pn;
  // expect the one global cpu port
  EXPECT_EQ(switchApi->getAttribute(switchId, pn), 1);
  fs->portManager.create(FakePort{{0}, 100000});
  fs->portManager.create(FakePort{{1}, 25000});
  fs->portManager.create(FakePort{{2}, 25000});
  fs->portManager.create(FakePort{{3}, 25000});
  // expect 4 created ports plus global cpu port
  EXPECT_EQ(switchApi->getAttribute(switchId, pn), 5);
}

TEST_F(SwitchApiTest, setNumPorts) {
  SaiSwitchTraits::Attributes::PortNumber pn{100};
  EXPECT_THROW(switchApi->setAttribute(switchId, pn), SaiApiError);
}

TEST_F(SwitchApiTest, testGetPortIds) {
  fs->portManager.create(FakePort{{0}, 100000});
  fs->portManager.create(FakePort{{1}, 25000});
  fs->portManager.create(FakePort{{2}, 25000});
  fs->portManager.create(FakePort{{3}, 25000});
  SaiSwitchTraits::Attributes::PortNumber pn;
  auto numPorts = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::PortNumber());
  std::vector<sai_object_id_t> v;
  v.resize(numPorts);
  SaiSwitchTraits::Attributes::PortList pl(v);
  auto portIds = switchApi->getAttribute(switchId, pl);
  EXPECT_EQ(portIds.size(), numPorts);
}

TEST_F(SwitchApiTest, setPortList) {
  SaiSwitchTraits::Attributes::PortList portList{
      std::vector<sai_object_id_t>{}};
  EXPECT_THROW(switchApi->setAttribute(switchId, portList), SaiApiError);
}

TEST_F(SwitchApiTest, testSetMac) {
  folly::MacAddress newSrcMac("DE:AD:BE:EF:42:42");
  SaiSwitchTraits::Attributes::SrcMac ma(newSrcMac);
  switchApi->setAttribute(switchId, ma);
  SaiSwitchTraits::Attributes::SrcMac blank;
  EXPECT_EQ(switchApi->getAttribute(switchId, blank), newSrcMac);
}

TEST_F(SwitchApiTest, testSetHwInfo) {
  SaiSwitchTraits::Attributes::HwInfo hw = std::vector<int8_t>(1, 41);
  switchApi->setAttribute(switchId, hw);
  SaiSwitchTraits::Attributes::HwInfo blank;
  auto hwGot = switchApi->getAttribute(switchId, blank);
  EXPECT_EQ(hwGot.size(), 1);
}
TEST_F(SwitchApiTest, getDefaultVlanId) {
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::DefaultVlanId()),
      0);
}

TEST_F(SwitchApiTest, setDefaultVlanId) {
  EXPECT_THROW(
      try {
        switchApi->setAttribute(
            switchId, SaiSwitchTraits::Attributes::DefaultVlanId(42));
      } catch (const SaiApiError& e) {
        EXPECT_EQ(e.getSaiStatus(), SAI_STATUS_INVALID_PARAMETER);
        throw;
      },
      SaiApiError);
}

TEST_F(SwitchApiTest, getCpuPort) {
  auto cpuPort =
      switchApi->getAttribute(switchId, SaiSwitchTraits::Attributes::CpuPort{});
  EXPECT_EQ(cpuPort, 0);
}

TEST_F(SwitchApiTest, setCpuPort) {
  SaiSwitchTraits::Attributes::CpuPort cpuPort{1};
  EXPECT_THROW(switchApi->setAttribute(switchId, cpuPort), SaiApiError);
}

TEST_F(SwitchApiTest, setGetShellEnable) {
  SaiSwitchTraits::Attributes::SwitchShellEnable shell{true};
  switchApi->setAttribute(switchId, shell);
  SaiSwitchTraits::Attributes::SwitchShellEnable blank{false};
  EXPECT_TRUE(switchApi->getAttribute(switchId, blank));
}

TEST_F(SwitchApiTest, getHashIds) {
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpHash{}),
      1234);
  EXPECT_EQ(
      switchApi->getAttribute(switchId, SaiSwitchTraits::Attributes::LagHash{}),
      1234);
}

TEST_F(SwitchApiTest, setHashIds) {
  SaiSwitchTraits::Attributes::EcmpHash ecmpHash{1};
  EXPECT_THROW(switchApi->setAttribute(switchId, ecmpHash), SaiApiError);
}

TEST_F(SwitchApiTest, setGetHashSeeds) {
  SaiSwitchTraits::Attributes::EcmpDefaultHashSeed ecmpSeed{42};
  switchApi->setAttribute(switchId, ecmpSeed);
  SaiSwitchTraits::Attributes::LagDefaultHashSeed lagSeed{24};
  switchApi->setAttribute(switchId, lagSeed);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpDefaultHashSeed{}),
      42);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::LagDefaultHashSeed{}),
      24);
}

TEST_F(SwitchApiTest, setGetHashAlgorithms) {
  SaiSwitchTraits::Attributes::EcmpDefaultHashAlgorithm ecmpAlgorithm{
      SAI_HASH_ALGORITHM_CRC_CCITT};
  switchApi->setAttribute(switchId, ecmpAlgorithm);
  SaiSwitchTraits::Attributes::LagDefaultHashAlgorithm lagAlgorithm{
      SAI_HASH_ALGORITHM_XOR};
  switchApi->setAttribute(switchId, lagAlgorithm);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpDefaultHashAlgorithm{}),
      SAI_HASH_ALGORITHM_CRC_CCITT);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::LagDefaultHashAlgorithm{}),
      SAI_HASH_ALGORITHM_XOR);
}

TEST_F(SwitchApiTest, setGetEcmpV4Hash) {
  SaiSwitchTraits::Attributes::EcmpHashV4 ecmpHash{42};
  switchApi->setAttribute(switchId, ecmpHash);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpHashV4{}),
      42);
}

TEST_F(SwitchApiTest, setGetEcmpV6Hash) {
  SaiSwitchTraits::Attributes::EcmpHashV6 ecmpHash{42};
  switchApi->setAttribute(switchId, ecmpHash);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpHashV6{}),
      42);
}

TEST_F(SwitchApiTest, setGetLagV4Hash) {
  SaiSwitchTraits::Attributes::LagHashV4 lagHash{24};
  switchApi->setAttribute(switchId, lagHash);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::LagHashV4{}),
      24);
}

TEST_F(SwitchApiTest, setGetLagV6Hash) {
  SaiSwitchTraits::Attributes::LagHashV6 lagHash{24};
  switchApi->setAttribute(switchId, lagHash);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::LagHashV6{}),
      24);
}

TEST_F(SwitchApiTest, setGetWarmRestart) {
  SaiSwitchTraits::Attributes::SwitchRestartWarm restartWarm{true};
  switchApi->setAttribute(switchId, restartWarm);
  SaiSwitchTraits::Attributes::SwitchRestartWarm blank{false};
  EXPECT_TRUE(switchApi->getAttribute(switchId, blank));
}

TEST_F(SwitchApiTest, setGetSetQosMaps) {
  SaiSwitchTraits::Attributes::QosDscpToTcMap dscpToTc{42};
  SaiSwitchTraits::Attributes::QosTcToQueueMap tcToQueue{43};
  switchApi->setAttribute(switchId, dscpToTc);
  switchApi->setAttribute(switchId, tcToQueue);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::QosDscpToTcMap{}),
      42);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::QosTcToQueueMap{}),
      43);
}

TEST_F(SwitchApiTest, getAclEntryMinimumPriority) {
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AclEntryMinimumPriority()),
      0);
}

TEST_F(SwitchApiTest, setAclEntryMinimumPriority) {
  EXPECT_THROW(
      switchApi->setAttribute(
          switchId, SaiSwitchTraits::Attributes::AclEntryMinimumPriority(42)),
      SaiApiError);
}

TEST_F(SwitchApiTest, getAclEntryMaximumPriority) {
  EXPECT_NE(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AclEntryMaximumPriority()),
      0);
}

TEST_F(SwitchApiTest, setAclEntryMaximumPriority) {
  EXPECT_THROW(
      switchApi->setAttribute(
          switchId, SaiSwitchTraits::Attributes::AclEntryMaximumPriority(42)),
      SaiApiError);
}

TEST_F(SwitchApiTest, setGetMacAgingTime) {
  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::MacAgingTime{42});
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::MacAgingTime{}),
      42);
}

TEST_F(SwitchApiTest, getDstUserMetaDataRange) {
  EXPECT_EQ(
      switchApi
          ->getAttribute(
              switchId, SaiSwitchTraits::Attributes::FdbDstUserMetaDataRange())
          .min,
      0);
  EXPECT_EQ(
      switchApi
          ->getAttribute(
              switchId,
              SaiSwitchTraits::Attributes::RouteDstUserMetaDataRange())
          .min,
      0);
  EXPECT_EQ(
      switchApi
          ->getAttribute(
              switchId,
              SaiSwitchTraits::Attributes::NeighborDstUserMetaDataRange())
          .min,
      0);
  EXPECT_EQ(
      switchApi
          ->getAttribute(
              switchId, SaiSwitchTraits::Attributes::PortUserMetaDataRange())
          .min,
      0);
}

TEST_F(SwitchApiTest, setDstUserMetaDataRange) {
  sai_u32_range_t u32Range;
  u32Range.min = 0;
  u32Range.max = 42;

  EXPECT_THROW(
      switchApi->setAttribute(
          switchId,
          SaiSwitchTraits::Attributes::FdbDstUserMetaDataRange(u32Range)),
      SaiApiError);
  EXPECT_THROW(
      switchApi->setAttribute(
          switchId,
          SaiSwitchTraits::Attributes::RouteDstUserMetaDataRange(u32Range)),
      SaiApiError);
  EXPECT_THROW(
      switchApi->setAttribute(
          switchId,
          SaiSwitchTraits::Attributes::NeighborDstUserMetaDataRange(u32Range)),
      SaiApiError);
  EXPECT_THROW(
      switchApi->setAttribute(
          switchId,
          SaiSwitchTraits::Attributes::PortUserMetaDataRange(u32Range)),
      SaiApiError);
}

TEST_F(SwitchApiTest, getUseEcnThresholds) {
  EXPECT_FALSE(switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::UseEcnThresholds()));
}

TEST_F(SwitchApiTest, setUseEcnThresholds) {
  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::UseEcnThresholds{true});

  EXPECT_TRUE(switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::UseEcnThresholds()));

  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::UseEcnThresholds{false});

  EXPECT_FALSE(switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::UseEcnThresholds{}));
}

TEST_F(SwitchApiTest, ExtensionAttributes) {
  /* Toggle LED */
  SaiSwitchTraits::Attributes::LedReset reset0{std::vector<sai_uint32_t>{0, 0}};
  SaiSwitchTraits::Attributes::LedReset reset1{std::vector<sai_uint32_t>{1, 0}};
  switchApi->setAttribute(switchId, reset0);
  switchApi->setAttribute(switchId, reset1);

  SaiSwitchTraits::Attributes::LedReset gotReset0 =
      switchApi->getAttribute(switchId, reset0);
  SaiSwitchTraits::Attributes::LedReset gotReset1 =
      switchApi->getAttribute(switchId, reset1);
  EXPECT_EQ(gotReset0, reset0);
  EXPECT_EQ(gotReset1, reset1);

  SaiSwitchTraits::Attributes::LedReset set0{std::vector<sai_uint32_t>{0, 1}};
  SaiSwitchTraits::Attributes::LedReset set1{std::vector<sai_uint32_t>{1, 1}};
  switchApi->setAttribute(switchId, set0);
  switchApi->setAttribute(switchId, set1);

  SaiSwitchTraits::Attributes::LedReset gotSet0 =
      switchApi->getAttribute(switchId, set0);
  SaiSwitchTraits::Attributes::LedReset gotSet1 =
      switchApi->getAttribute(switchId, set1);
  EXPECT_EQ(gotSet0, set0);
  EXPECT_EQ(gotSet1, set1);

  /* Program LED state */
  SaiSwitchTraits::Attributes::Led ledPgm0{
      std::vector<sai_uint32_t>{0, 1, 100, 0xab}};
  SaiSwitchTraits::Attributes::Led ledPgm1{
      std::vector<sai_uint32_t>{1, 1, 100, 0xcd}};
  switchApi->setAttribute(switchId, ledPgm0);
  switchApi->setAttribute(switchId, ledPgm1);

  SaiSwitchTraits::Attributes::Led gotledPgm0 = switchApi->getAttribute(
      switchId,
      SaiSwitchTraits::Attributes::Led{
          std::vector<sai_uint32_t>{0, 1, 100, 0}});
  SaiSwitchTraits::Attributes::Led gotledPgm1 = switchApi->getAttribute(
      switchId,
      SaiSwitchTraits::Attributes::Led{
          std::vector<sai_uint32_t>{1, 1, 100, 0}});
  EXPECT_EQ(ledPgm0, gotledPgm0);
  EXPECT_EQ(ledPgm1, gotledPgm1);

  SaiSwitchTraits::Attributes::Led ledDt0{
      std::vector<sai_uint32_t>{0, 0, 118, 0xabcd}};
  SaiSwitchTraits::Attributes::Led ledDt1{
      std::vector<sai_uint32_t>{1, 0, 118, 0xdcba}};
  switchApi->setAttribute(switchId, ledDt0);
  switchApi->setAttribute(switchId, ledDt1);

  SaiSwitchTraits::Attributes::Led gotledDt0 = switchApi->getAttribute(
      switchId,
      SaiSwitchTraits::Attributes::Led{
          std::vector<sai_uint32_t>{0, 0, 118, 0}});
  SaiSwitchTraits::Attributes::Led gotledDt1 = switchApi->getAttribute(
      switchId,
      SaiSwitchTraits::Attributes::Led{
          std::vector<sai_uint32_t>{1, 0, 118, 0}});
  EXPECT_EQ(ledDt0, gotledDt0);
  EXPECT_EQ(ledDt1, gotledDt1);
}

TEST_F(SwitchApiTest, getAvailableRoutes) {
  EXPECT_GT(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AvailableIpv4RouteEntry{}),
      0);
  EXPECT_GT(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AvailableIpv6RouteEntry{}),
      0);
}

TEST_F(SwitchApiTest, getAvailableNexthops) {
  EXPECT_GT(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AvailableIpv4NextHopEntry{}),
      0);
  EXPECT_GT(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AvailableIpv6NextHopEntry{}),
      0);
}

TEST_F(SwitchApiTest, getAvailableNexthopGroups) {
  EXPECT_GT(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AvailableNextHopGroupEntry{}),
      0);
  EXPECT_GT(
      switchApi->getAttribute(
          switchId,
          SaiSwitchTraits::Attributes::AvailableNextHopGroupMemberEntry{}),
      0);
}

TEST_F(SwitchApiTest, getAvailableNeighbor) {
  EXPECT_GT(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AvailableIpv4NeighborEntry{}),
      0);
  EXPECT_GT(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AvailableIpv6NeighborEntry{}),
      0);
}

TEST_F(SwitchApiTest, getIngressAcl) {
  EXPECT_FALSE(switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::IngressAcl()));
}

TEST_F(SwitchApiTest, setIngressAcl) {
  switchApi->setAttribute(switchId, SaiSwitchTraits::Attributes::IngressAcl{1});

  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::IngressAcl()),
      1);
}

TEST_F(SwitchApiTest, egressPoolSize) {
  SaiSwitchTraits::Attributes::EgressPoolAvailableSize sz{};
  EXPECT_EQ(switchApi->getAttribute(switchId, sz), 1'000'000);
}

TEST_F(SwitchApiTest, setCounterRefreshInterval) {
  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::CounterRefreshInterval{20});
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::CounterRefreshInterval()),
      20);
}

TEST_F(SwitchApiTest, setArsProfile) {
  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::ArsProfile{50});

  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::ArsProfile()),
      50);
}

TEST_F(SwitchApiTest, testSetFirmwareFile) {
  SaiSwitchTraits::Attributes::FirmwarePathName fwPath =
      std::vector<int8_t>{'f', 'i', 'l', 'e'};
  switchApi->setAttribute(switchId, fwPath);
  auto fwPathGot = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::FirmwarePathName{});

  EXPECT_EQ(fwPathGot.size(), 4);
  EXPECT_EQ(fwPathGot[0], 'f');
  EXPECT_EQ(fwPathGot[1], 'i');
  EXPECT_EQ(fwPathGot[2], 'l');
  EXPECT_EQ(fwPathGot[3], 'e');
}

TEST_F(SwitchApiTest, testFwLoadMethod) {
  SaiSwitchTraits::Attributes::FirmwareLoadMethod method{
      SAI_SWITCH_FIRMWARE_LOAD_METHOD_INTERNAL};
  switchApi->setAttribute(switchId, method);

  auto gotMethod = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::FirmwareLoadMethod{});
  EXPECT_EQ(method, gotMethod);
}

TEST_F(SwitchApiTest, testFwLoadType) {
  SaiSwitchTraits::Attributes::FirmwareLoadType loadType{
      SAI_SWITCH_FIRMWARE_LOAD_TYPE_AUTO};
  switchApi->setAttribute(switchId, loadType);

  auto gotLoadType = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::FirmwareLoadType{});
  EXPECT_EQ(loadType, gotLoadType);
}

TEST_F(SwitchApiTest, testWarmRecover) {
  SaiSwitchTraits::Attributes::WarmRecover warmRecover{true};
  switchApi->setAttribute(switchId, warmRecover);

  auto gotWarmRecover = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::WarmRecover{});
  EXPECT_EQ(warmRecover, gotWarmRecover);
}

TEST_F(SwitchApiTest, testAccessBus) {
  SaiSwitchTraits::Attributes::HardwareAccessBus bus{
      SAI_SWITCH_HARDWARE_ACCESS_BUS_MDIO};
  switchApi->setAttribute(switchId, bus);

  auto gotBus = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::HardwareAccessBus{});
  EXPECT_EQ(bus, gotBus);
}

TEST_F(SwitchApiTest, testPlatformContext) {
  SaiSwitchTraits::Attributes::PlatformContext context{0x1122334455667788};
  switchApi->setAttribute(switchId, context);

  auto gotContext = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::PlatformContext{});
  EXPECT_EQ(context, gotContext);
}

TEST_F(SwitchApiTest, testProfileId) {
  SaiSwitchTraits::Attributes::SwitchProfileId prof{1};
  switchApi->setAttribute(switchId, prof);

  auto gotProf = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::SwitchProfileId{});
  EXPECT_EQ(prof, gotProf);
}

TEST_F(SwitchApiTest, testFwStatus) {
  auto fwStatus = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::FirmwareStatus{});
  EXPECT_EQ(fwStatus, true);
}

TEST_F(SwitchApiTest, testFwVersion) {
  auto fwMajor = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::FirmwareMajorVersion{});
  EXPECT_EQ(fwMajor, 0);

  auto fwMinor = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::FirmwareMinorVersion{});
  EXPECT_EQ(fwMinor, 0);
}

TEST_F(SwitchApiTest, testSwitchId) {
  SaiSwitchTraits::Attributes::SwitchId swId{1};
  switchApi->setAttribute(switchId, swId);

  auto gotId = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::SwitchId{});
  EXPECT_EQ(swId, gotId);
}

TEST_F(SwitchApiTest, testMaxSysCores) {
  SaiSwitchTraits::Attributes::MaxSystemCores sysCores{1};
  switchApi->setAttribute(switchId, sysCores);

  auto gotCores = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::MaxSystemCores{});
  EXPECT_EQ(sysCores, gotCores);
}

TEST_F(SwitchApiTest, testSysPortCfgList) {
  SaiSwitchTraits::Attributes::SysPortConfigList cfgList =
      std::vector<sai_system_port_config_t>{{
          1, // port_id
          0, // switch_id
          10, // attached_core_index
          0, // attached_core_port_index
          100000, // speed - 100G in mbps
      }};
  switchApi->setAttribute(switchId, cfgList);
  auto gotList = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::SysPortConfigList{});
  EXPECT_EQ(gotList.size(), 1);
}

TEST_F(SwitchApiTest, testSwitchType) {
  SaiSwitchTraits::Attributes::SwitchType swType{SAI_SWITCH_TYPE_VOQ};
  switchApi->setAttribute(switchId, swType);

  auto gotType = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::SwitchType{});
  EXPECT_EQ(swType, gotType);
}

TEST_F(SwitchApiTest, testReadFn) {
  SaiSwitchTraits::Attributes::RegisterReadFn readFunc{0};
  switchApi->setAttribute(switchId, readFunc);

  auto gotFn = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::RegisterReadFn{});
  EXPECT_EQ(readFunc, gotFn);
}

TEST_F(SwitchApiTest, testWriteFn) {
  SaiSwitchTraits::Attributes::RegisterWriteFn writeFunc{0};
  switchApi->setAttribute(switchId, writeFunc);

  auto gotFn = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::RegisterWriteFn{});
  EXPECT_EQ(writeFunc, gotFn);
}

TEST_F(SwitchApiTest, getAllStats) {
  auto stats =
      switchApi->getStats<SaiSwitchTraits>(switchId, SAI_STATS_MODE_READ);
  EXPECT_EQ(stats.size(), SaiSwitchTraits::CounterIdsToRead.size());
}

TEST_F(SwitchApiTest, getSomeStats) {
  auto stats = switchApi->getStats<SaiSwitchTraits>(
      switchId, {SAI_SWITCH_STAT_GLOBAL_DROP}, SAI_STATS_MODE_READ);
  EXPECT_EQ(stats.size(), 1);
}

TEST_F(SwitchApiTest, setGetCreditWatchdog) {
  EXPECT_TRUE(switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::CreditWd{}));
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::CreditWdTimer{}),
      500);
  SaiSwitchTraits::Attributes::CreditWd wd{false};
  switchApi->setAttribute(switchId, wd);
  SaiSwitchTraits::Attributes::CreditWd blank{true};
  EXPECT_FALSE(switchApi->getAttribute(switchId, blank));
  SaiSwitchTraits::Attributes::CreditWdTimer wdTimer{10};
  switchApi->setAttribute(switchId, wdTimer);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::CreditWdTimer{}),
      10);
}

TEST_F(SwitchApiTest, setGetPfcDlrPacketAction) {
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PfcDlrPacketAction{}),
      SAI_PACKET_ACTION_DROP);
  SaiSwitchTraits::Attributes::PfcDlrPacketAction pktAction{
      SAI_PACKET_ACTION_FORWARD};
  switchApi->setAttribute(switchId, pktAction);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PfcDlrPacketAction{}),
      SAI_PACKET_ACTION_FORWARD);
}

TEST_F(SwitchApiTest, testPfcMonitorEnable) {
  // Test default value
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PfcMonitorEnable{}),
      false);

  // Test setting to true
  SaiSwitchTraits::Attributes::PfcMonitorEnable pfcMonitorEnable{true};
  switchApi->setAttribute(switchId, pfcMonitorEnable);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PfcMonitorEnable{}),
      true);

  // Test setting to false
  SaiSwitchTraits::Attributes::PfcMonitorEnable pfcMonitorDisable{false};
  switchApi->setAttribute(switchId, pfcMonitorDisable);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PfcMonitorEnable{}),
      false);
}

TEST_F(SwitchApiTest, testCablePropagationDelayMeasurement) {
  // Test default value
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId,
          SaiSwitchTraits::Attributes::CablePropagationDelayMeasurement{}),
      false);

  // Test setting to true
  SaiSwitchTraits::Attributes::CablePropagationDelayMeasurement
      cablePropagationDelayMeasurementEnable{true};
  switchApi->setAttribute(switchId, cablePropagationDelayMeasurementEnable);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId,
          SaiSwitchTraits::Attributes::CablePropagationDelayMeasurement{}),
      true);

  // Test setting to false
  SaiSwitchTraits::Attributes::CablePropagationDelayMeasurement
      cablePropagationDelayMeasurementDisable{false};
  switchApi->setAttribute(switchId, cablePropagationDelayMeasurementDisable);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId,
          SaiSwitchTraits::Attributes::CablePropagationDelayMeasurement{}),
      false);
}

TEST_F(SwitchApiTest, testPortCl72RetryEnable) {
  // Test default value
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PortCl72RetryEnable{}),
      false);

  // Test setting to true
  SaiSwitchTraits::Attributes::PortCl72RetryEnable portCl72RetryEnable{true};
  switchApi->setAttribute(switchId, portCl72RetryEnable);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PortCl72RetryEnable{}),
      true);

  // Test setting to false
  SaiSwitchTraits::Attributes::PortCl72RetryEnable portCl72RetryDisable{false};
  switchApi->setAttribute(switchId, portCl72RetryDisable);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PortCl72RetryEnable{}),
      false);
}

TEST_F(SwitchApiTest, testAclDropSnoopDisabled) {
  EXPECT_FALSE(switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::AclDropSnoopDisabled{}));

  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::AclDropSnoopDisabled{true});
  EXPECT_TRUE(switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::AclDropSnoopDisabled{}));

  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::AclDropSnoopDisabled{false});
  EXPECT_FALSE(switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::AclDropSnoopDisabled{}));
}

TEST_F(SwitchApiTest, getPacketDropTypeIngressList) {
  const std::vector<sai_int32_t> dropTypes{1, 7, 42};
  fs->switchManager.get(switchId).setPacketDropTypeIngressList(dropTypes);

  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PacketDropTypeIngressList{}),
      dropTypes);
}

TEST_F(SwitchApiTest, getEmptyPacketDropTypeIngressList) {
  EXPECT_TRUE(switchApi
                  ->getAttribute(
                      switchId,
                      SaiSwitchTraits::Attributes::PacketDropTypeIngressList{})
                  .empty());
}

TEST_F(SwitchApiTest, setPacketDropTypeIngressList) {
  SaiSwitchTraits::Attributes::PacketDropTypeIngressList dropTypes{
      std::vector<sai_int32_t>{1}};
  EXPECT_THROW(switchApi->setAttribute(switchId, dropTypes), SaiApiError);
}

TEST_F(SwitchApiTest, getPacketDropTypeEgressList) {
  const std::vector<sai_int32_t> dropTypes{2, 13};
  fs->switchManager.get(switchId).setPacketDropTypeEgressList(dropTypes);

  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::PacketDropTypeEgressList{}),
      dropTypes);
}

TEST_F(SwitchApiTest, getEmptyPacketDropTypeEgressList) {
  EXPECT_TRUE(
      switchApi
          ->getAttribute(
              switchId, SaiSwitchTraits::Attributes::PacketDropTypeEgressList{})
          .empty());
}

TEST_F(SwitchApiTest, setPacketDropTypeEgressList) {
  SaiSwitchTraits::Attributes::PacketDropTypeEgressList dropTypes{
      std::vector<sai_int32_t>{2}};
  EXPECT_THROW(switchApi->setAttribute(switchId, dropTypes), SaiApiError);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
TEST_F(SwitchApiTest, testLinkUpDebounceTimeout) {
  // Defaults to no debounce
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::LinkUpDebounceTimeout{}),
      0);

  constexpr sai_uint32_t kTimeoutUs = 50000;
  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::LinkUpDebounceTimeout{kTimeoutUs});
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::LinkUpDebounceTimeout{}),
      kTimeoutUs);

  // Back to no debounce
  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::LinkUpDebounceTimeout{0});
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::LinkUpDebounceTimeout{}),
      0);
}
#endif

TEST_F(SwitchApiTest, testSwitchingMode) {
  // Test default value (STORE_AND_FORWARD)
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::SwitchingMode{}),
      SAI_SWITCH_SWITCHING_MODE_STORE_AND_FORWARD);

  // Test setting to CUT_THROUGH
  SaiSwitchTraits::Attributes::SwitchingMode cutThrough{
      SAI_SWITCH_SWITCHING_MODE_CUT_THROUGH};
  switchApi->setAttribute(switchId, cutThrough);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::SwitchingMode{}),
      SAI_SWITCH_SWITCHING_MODE_CUT_THROUGH);

  // Test setting back to STORE_AND_FORWARD
  SaiSwitchTraits::Attributes::SwitchingMode storeAndForward{
      SAI_SWITCH_SWITCHING_MODE_STORE_AND_FORWARD};
  switchApi->setAttribute(switchId, storeAndForward);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::SwitchingMode{}),
      SAI_SWITCH_SWITCHING_MODE_STORE_AND_FORWARD);
}
