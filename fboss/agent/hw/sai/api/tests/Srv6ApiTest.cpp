// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/api/Srv6Api.h"
#include "fboss/agent/hw/sai/api/SaiObjectApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
using namespace facebook::fboss;

class Srv6ApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    srv6Api = std::make_unique<Srv6Api>();
  }

  Srv6SidListSaiId createSrv6SidList(sai_int32_t type) {
    SaiSrv6SidListTraits::Attributes::Type typeAttr{type};
    SaiSrv6SidListTraits::CreateAttributes a{
        typeAttr, std::nullopt, std::nullopt};
    return srv6Api->create<SaiSrv6SidListTraits>(a, 0);
  }

  SaiMySidEntryTraits::MySidEntry createMySidEntryKey(
      const folly::IPAddressV6& sid = folly::IPAddressV6("2001:db8::1"),
      sai_object_id_t switchId = 0,
      sai_object_id_t vrId = 0,
      uint8_t locatorBlockLen = 48,
      uint8_t locatorNodeLen = 16,
      uint8_t functionLen = 20,
      uint8_t argsLen = 0) {
    return SaiMySidEntryTraits::MySidEntry(
        switchId,
        vrId,
        locatorBlockLen,
        locatorNodeLen,
        functionLen,
        argsLen,
        sid);
  }

  void createMySidEntry(
      const SaiMySidEntryTraits::MySidEntry& entry,
      sai_int32_t endpointBehavior = SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E) {
    SaiMySidEntryTraits::Attributes::EndpointBehavior behavior{
        endpointBehavior};
    SaiMySidEntryTraits::CreateAttributes attrs{
        behavior,
        SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USP,
        SAI_NULL_OBJECT_ID,
        0,
        SAI_PACKET_ACTION_FORWARD};
    srv6Api->create<SaiMySidEntryTraits>(entry, attrs);
  }

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<Srv6Api> srv6Api;
};

TEST_F(Srv6ApiTest, createSrv6SidList) {
  auto id = createSrv6SidList(SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);
  EXPECT_EQ(
      srv6Api->getAttribute(id, SaiSrv6SidListTraits::Attributes::Type{}),
      SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);
}

TEST_F(Srv6ApiTest, removeSrv6SidList) {
  auto id = createSrv6SidList(SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);
  srv6Api->remove(id);
  EXPECT_THROW(
      srv6Api->getAttribute(id, SaiSrv6SidListTraits::Attributes::Type{}),
      std::exception);
}

TEST_F(Srv6ApiTest, setSrv6SidListNextHop) {
  auto id = createSrv6SidList(SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);
  srv6Api->setAttribute(id, SaiSrv6SidListTraits::Attributes::NextHopId{42});
  EXPECT_EQ(
      srv6Api->getAttribute(id, SaiSrv6SidListTraits::Attributes::NextHopId{}),
      42);
}

TEST_F(Srv6ApiTest, getSrv6SidListAttributes) {
  auto id = createSrv6SidList(SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);
  EXPECT_EQ(
      srv6Api->getAttribute(id, SaiSrv6SidListTraits::Attributes::Type{}),
      SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);
  EXPECT_EQ(
      srv6Api->getAttribute(id, SaiSrv6SidListTraits::Attributes::NextHopId{}),
      SAI_NULL_OBJECT_ID);
}

// MySidEntry tests

TEST_F(Srv6ApiTest, createMySidEntryBasic) {
  auto entry = createMySidEntryKey();
  createMySidEntry(entry);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E);
}

TEST_F(Srv6ApiTest, programMySidEntryWithAllAttributes) {
  auto entry = createMySidEntryKey();
  SaiMySidEntryTraits::Attributes::EndpointBehavior behavior{
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X};
  SaiMySidEntryTraits::Attributes::EndpointBehaviorFlavor flavor{
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD};
  SaiMySidEntryTraits::Attributes::NextHopId nextHop{42};
  SaiMySidEntryTraits::Attributes::Vrf vrf{10};
  SaiMySidEntryTraits::Attributes::PacketAction packetAction{
      SAI_PACKET_ACTION_TRAP};
  SaiMySidEntryTraits::Attributes::TunnelId tunnelId{55};
  SaiMySidEntryTraits::CreateAttributes attrs{
      behavior, flavor, nextHop, vrf, packetAction};
  srv6Api->create<SaiMySidEntryTraits>(entry, attrs);
  srv6Api->setAttribute(entry, tunnelId);

  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::EndpointBehaviorFlavor{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::NextHopId{}),
      42);
  EXPECT_EQ(
      srv6Api->getAttribute(entry, SaiMySidEntryTraits::Attributes::Vrf{}), 10);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_TRAP);
  EXPECT_EQ(
      srv6Api->getAttribute(entry, SaiMySidEntryTraits::Attributes::TunnelId{}),
      55);
}

TEST_F(Srv6ApiTest, removeMySidEntry) {
  auto entry = createMySidEntryKey();
  createMySidEntry(entry);
  srv6Api->remove(entry);
  EXPECT_THROW(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      std::exception);
}

TEST_F(Srv6ApiTest, setMySidEntryEndpointBehavior) {
  auto entry = createMySidEntryKey();
  createMySidEntry(entry, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E);
  srv6Api->setAttribute(
      entry,
      SaiMySidEntryTraits::Attributes::EndpointBehavior{
          SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X});
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X);
}

TEST_F(Srv6ApiTest, setMySidEntryEndpointBehaviorFlavor) {
  auto entry = createMySidEntryKey();
  createMySidEntry(entry);
  srv6Api->setAttribute(
      entry,
      SaiMySidEntryTraits::Attributes::EndpointBehaviorFlavor{
          SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD});
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::EndpointBehaviorFlavor{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD);
}

TEST_F(Srv6ApiTest, setMySidEntryNextHopId) {
  auto entry = createMySidEntryKey();
  createMySidEntry(entry);
  srv6Api->setAttribute(entry, SaiMySidEntryTraits::Attributes::NextHopId{42});
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::NextHopId{}),
      42);
  srv6Api->setAttribute(entry, SaiMySidEntryTraits::Attributes::NextHopId{99});
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::NextHopId{}),
      99);
}

TEST_F(Srv6ApiTest, setMySidEntryVrf) {
  auto entry = createMySidEntryKey();
  createMySidEntry(entry);
  srv6Api->setAttribute(entry, SaiMySidEntryTraits::Attributes::Vrf{10});
  EXPECT_EQ(
      srv6Api->getAttribute(entry, SaiMySidEntryTraits::Attributes::Vrf{}), 10);
}

TEST_F(Srv6ApiTest, setMySidEntryPacketAction) {
  auto entry = createMySidEntryKey();
  createMySidEntry(entry);
  srv6Api->setAttribute(
      entry,
      SaiMySidEntryTraits::Attributes::PacketAction{SAI_PACKET_ACTION_TRAP});
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_TRAP);
  srv6Api->setAttribute(
      entry,
      SaiMySidEntryTraits::Attributes::PacketAction{SAI_PACKET_ACTION_DROP});
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_DROP);
}

TEST_F(Srv6ApiTest, getMySidEntryDefaultAttributes) {
  auto entry = createMySidEntryKey();
  createMySidEntry(entry);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::EndpointBehaviorFlavor{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USP);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::NextHopId{}),
      SAI_NULL_OBJECT_ID);
  EXPECT_EQ(
      srv6Api->getAttribute(entry, SaiMySidEntryTraits::Attributes::Vrf{}),
      SAI_NULL_OBJECT_ID);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_FORWARD);
}

TEST_F(Srv6ApiTest, mySidEntryCount) {
  uint32_t count = getObjectCount<SaiMySidEntryTraits>(0);
  EXPECT_EQ(count, 0);
  auto entry1 = createMySidEntryKey(folly::IPAddressV6("2001:db8::1"));
  createMySidEntry(entry1);
  count = getObjectCount<SaiMySidEntryTraits>(0);
  EXPECT_EQ(count, 1);
  auto entry2 = createMySidEntryKey(folly::IPAddressV6("2001:db8::2"));
  createMySidEntry(entry2);
  count = getObjectCount<SaiMySidEntryTraits>(0);
  EXPECT_EQ(count, 2);
}

TEST_F(Srv6ApiTest, createMultipleMySidEntries) {
  auto entry1 = createMySidEntryKey(folly::IPAddressV6("2001:db8::1"));
  auto entry2 = createMySidEntryKey(folly::IPAddressV6("2001:db8::2"));
  auto entry3 = createMySidEntryKey(folly::IPAddressV6("2001:db8::3"));
  createMySidEntry(entry1, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E);
  createMySidEntry(entry2, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X);
  createMySidEntry(entry3, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_DX6);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry1, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry2, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X);
  EXPECT_EQ(
      srv6Api->getAttribute(
          entry3, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_DX6);
}

TEST_F(Srv6ApiTest, duplicateMySidEntryCreate) {
  auto entry = createMySidEntryKey();
  createMySidEntry(entry);
  EXPECT_THROW(createMySidEntry(entry), std::exception);
}

TEST_F(Srv6ApiTest, removeNonexistentMySidEntry) {
  auto entry = createMySidEntryKey();
  EXPECT_THROW(srv6Api->remove(entry), std::exception);
}

TEST_F(Srv6ApiTest, mySidEntryDifferentKeyFields) {
  auto entry1 = createMySidEntryKey(
      folly::IPAddressV6("2001:db8::1"), 0, 0, 48, 16, 20, 0);
  auto entry2 = createMySidEntryKey(
      folly::IPAddressV6("2001:db8::1"), 0, 0, 32, 16, 20, 0);
  createMySidEntry(entry1);
  createMySidEntry(entry2);
  uint32_t count = getObjectCount<SaiMySidEntryTraits>(0);
  EXPECT_EQ(count, 2);
}

TEST_F(Srv6ApiTest, mySidEntryEquality) {
  auto entry1 = createMySidEntryKey(
      folly::IPAddressV6("2001:db8::1"), 0, 0, 48, 16, 20, 0);
  auto entry2 = createMySidEntryKey(
      folly::IPAddressV6("2001:db8::1"), 0, 0, 48, 16, 20, 0);
  EXPECT_EQ(entry1, entry2);
}

TEST_F(Srv6ApiTest, mySidEntryInequality) {
  auto entry1 = createMySidEntryKey(folly::IPAddressV6("2001:db8::1"));
  auto entry2 = createMySidEntryKey(folly::IPAddressV6("2001:db8::2"));
  EXPECT_NE(entry1, entry2);
}

TEST_F(Srv6ApiTest, mySidEntryAccessors) {
  folly::IPAddressV6 sid("2001:db8::1");
  auto entry = createMySidEntryKey(sid, 10, 20, 48, 16, 20, 4);
  EXPECT_EQ(entry.switchId(), 10);
  EXPECT_EQ(entry.vrId(), 20);
  EXPECT_EQ(entry.locatorBlockLen(), 48);
  EXPECT_EQ(entry.locatorNodeLen(), 16);
  EXPECT_EQ(entry.functionLen(), 20);
  EXPECT_EQ(entry.argsLen(), 4);
  EXPECT_EQ(entry.sid(), sid);
}

TEST_F(Srv6ApiTest, mySidEntryFromObjectKey) {
  folly::IPAddressV6 sid("2001:db8::1");
  auto entry = createMySidEntryKey(sid, 0, 0, 48, 16, 20, 0);
  sai_object_key_t objKey;
  objKey.key.my_sid_entry = *entry.entry();
  SaiMySidEntryTraits::MySidEntry fromKey(objKey);
  EXPECT_EQ(entry, fromKey);
}

TEST_F(Srv6ApiTest, mySidEntryCountAfterRemove) {
  auto entry1 = createMySidEntryKey(folly::IPAddressV6("2001:db8::1"));
  auto entry2 = createMySidEntryKey(folly::IPAddressV6("2001:db8::2"));
  createMySidEntry(entry1);
  createMySidEntry(entry2);
  EXPECT_EQ(getObjectCount<SaiMySidEntryTraits>(0), 2u);
  srv6Api->remove(entry1);
  EXPECT_EQ(getObjectCount<SaiMySidEntryTraits>(0), 1u);
  srv6Api->remove(entry2);
  EXPECT_EQ(getObjectCount<SaiMySidEntryTraits>(0), 0u);
}

TEST_F(Srv6ApiTest, formatMySidEntryAttributes) {
  SaiMySidEntryTraits::Attributes::EndpointBehavior behavior{
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E};
  std::string expected("EndpointBehavior: 0");
  EXPECT_EQ(expected, fmt::format("{}", behavior));
}
#endif
