/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/api/FdbApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <gtest/gtest.h>

#include <set>

using namespace facebook::fboss;

class FdbApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    fdbApi = std::make_unique<FdbApi>();
  }
  SaiFdbTraits::FdbEntry makeFdbEntry(
      folly::MacAddress mac = folly::MacAddress{"42:42:42:42:42:42"}) {
    return SaiFdbTraits::FdbEntry(0, 1000, mac);
  }

  void createFdbEntry(
      folly::MacAddress mac,
      sai_object_id_t bridgePortId,
      sai_fdb_entry_type_t type) {
    fdbApi->create<SaiFdbTraits>(
        makeFdbEntry(mac),
        SaiFdbTraits::CreateAttributes{type, bridgePortId, std::nullopt});
  }

  std::set<folly::MacAddress> remainingMacs() const {
    std::set<folly::MacAddress> macs;
    for (const auto& [key, _] : fs->fdbManager.map()) {
      macs.insert(std::get<folly::MacAddress>(key));
    }
    return macs;
  }

  const folly::MacAddress kPort1Dynamic{"02:00:00:00:00:01"};
  const folly::MacAddress kPort1Static{"02:00:00:00:00:02"};
  const folly::MacAddress kPort2Dynamic{"02:00:00:00:00:03"};
  static constexpr sai_object_id_t kBridgePort1{24};
  static constexpr sai_object_id_t kBridgePort2{25};

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<FdbApi> fdbApi;
};

TEST_F(FdbApiTest, serDeserv) {
  folly::MacAddress mac{"42:42:42:42:42:42"};
  SaiFdbTraits::FdbEntry f(0, 1000, mac);
  EXPECT_EQ(f, SaiFdbTraits::FdbEntry::fromFollyDynamic(f.toFollyDynamic()));
}

TEST_F(FdbApiTest, create) {
  folly::MacAddress mac{"42:42:42:42:42:42"};
  SaiFdbTraits::CreateAttributes c{SAI_FDB_ENTRY_TYPE_DYNAMIC, 24, 42};
  fdbApi->create<SaiFdbTraits>(makeFdbEntry(mac), c);
  EXPECT_EQ(
      fdbApi->getAttribute(
          makeFdbEntry(mac), SaiFdbTraits::Attributes::Metadata{}),
      42);
}

TEST_F(FdbApiTest, setGetMetadata) {
  folly::MacAddress mac{"42:42:42:42:42:42"};
  SaiFdbTraits::CreateAttributes c{
      SAI_FDB_ENTRY_TYPE_DYNAMIC, 24, std::nullopt};
  fdbApi->create<SaiFdbTraits>(makeFdbEntry(mac), c);
  EXPECT_EQ(
      fdbApi->getAttribute(
          makeFdbEntry(mac), SaiFdbTraits::Attributes::Metadata{}),
      0);

  fdbApi->setAttribute(
      makeFdbEntry(mac), SaiFdbTraits::Attributes::Metadata{42});
  EXPECT_EQ(
      fdbApi->getAttribute(
          makeFdbEntry(mac), SaiFdbTraits::Attributes::Metadata{}),
      42);
}

TEST_F(FdbApiTest, flushDynamicEntriesOnBridgePort) {
  createFdbEntry(kPort1Dynamic, kBridgePort1, SAI_FDB_ENTRY_TYPE_DYNAMIC);
  createFdbEntry(kPort1Static, kBridgePort1, SAI_FDB_ENTRY_TYPE_STATIC);
  createFdbEntry(kPort2Dynamic, kBridgePort2, SAI_FDB_ENTRY_TYPE_DYNAMIC);

  fdbApi->flush(
      SwitchSaiId{0},
      BridgePortSaiId{kBridgePort1},
      SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC);

  const std::set<folly::MacAddress> expected{kPort1Static, kPort2Dynamic};
  EXPECT_EQ(remainingMacs(), expected);
}

TEST_F(FdbApiTest, flushDynamicEntriesOnAllBridgePorts) {
  createFdbEntry(kPort1Dynamic, kBridgePort1, SAI_FDB_ENTRY_TYPE_DYNAMIC);
  createFdbEntry(kPort1Static, kBridgePort1, SAI_FDB_ENTRY_TYPE_STATIC);
  createFdbEntry(kPort2Dynamic, kBridgePort2, SAI_FDB_ENTRY_TYPE_DYNAMIC);

  fdbApi->flush(SwitchSaiId{0}, std::nullopt, SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC);

  const std::set<folly::MacAddress> expected{kPort1Static};
  EXPECT_EQ(remainingMacs(), expected);
}
