// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include "fboss/agent/hw/sai/api/PolicerApi.h"
#include "fboss/agent/hw/sai/api/MirrorApi.h"
#include "fboss/agent/hw/sai/api/SaiObjectApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

using namespace facebook::fboss;

class PolicerApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    sai_api_initialize(0, nullptr);
    policerApi = std::make_unique<PolicerApi>();
    mirrorApi = std::make_unique<MirrorApi>();
  }

  SaiPolicerTraits::CreateAttributes attributes(
      uint64_t cir = 1000,
      uint64_t cbs = 100) const {
    return {
        SAI_METER_TYPE_PACKETS,
        SAI_POLICER_MODE_SR_TCM,
        SAI_POLICER_COLOR_SOURCE_BLIND,
        cir,
        cbs,
        0,
        SAI_PACKET_ACTION_FORWARD,
        SAI_PACKET_ACTION_DROP,
        SAI_PACKET_ACTION_DROP,
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
        SaiCharArray32{},
#endif
    };
  }

  SaiEnhancedRemoteMirrorTraits::CreateAttributes mirrorAttributes(
      std::optional<SaiEnhancedRemoteMirrorTraits::Attributes::Policer>
          policer = std::nullopt) const {
    return {
        SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE,
        42,
        SAI_ERSPAN_ENCAPSULATION_TYPE_MIRROR_L3_GRE_TUNNEL,
        16,
        folly::IPAddress{"10.0.0.1"},
        folly::IPAddress{"10.0.0.2"},
        folly::MacAddress{"00:11:22:33:44:55"},
        folly::MacAddress{"00:11:22:33:44:66"},
        0x88be,
        4,
        64,
        0,
        1,
        std::nullopt,
        policer};
  }

  SaiSflowMirrorTraits::CreateAttributes sflowAttributes(
      std::optional<SaiSflowMirrorTraits::Attributes::Policer> policer =
          std::nullopt) const {
    return {
        SAI_MIRROR_SESSION_TYPE_SFLOW,
        42,
        16,
        folly::IPAddress{"10.0.0.1"},
        folly::IPAddress{"10.0.0.2"},
        folly::MacAddress{"00:11:22:33:44:55"},
        folly::MacAddress{"00:11:22:33:44:66"},
        1234,
        6343,
        4,
        64,
        0,
        1,
        std::nullopt,
        policer};
  }

  std::unique_ptr<PolicerApi> policerApi;
  std::unique_ptr<MirrorApi> mirrorApi;
};

TEST_F(PolicerApiTest, createAndGetAttributes) {
  auto expected = attributes(uint64_t{1} << 40, uint64_t{1} << 36);
  auto id = policerApi->create<SaiPolicerTraits>(expected, 0);
  EXPECT_NE(id, PolicerSaiId{SAI_NULL_OBJECT_ID});
  EXPECT_EQ(
      policerApi->getAttribute(id, SaiPolicerTraits::CreateAttributes{}),
      expected);
  EXPECT_TRUE(FakeSai::getInstance()->policerManager.exists(id));
  policerApi->remove(id);
  EXPECT_FALSE(FakeSai::getInstance()->policerManager.exists(id));
  EXPECT_THROW(
      policerApi->getAttribute(id, SaiPolicerTraits::Attributes::Cir{}),
      SaiApiError);
}

TEST_F(PolicerApiTest, updateRateAndBurst) {
  auto id = policerApi->create<SaiPolicerTraits>(attributes(), 0);
  policerApi->setAttribute(id, SaiPolicerTraits::Attributes::Cir{2000});
  policerApi->setAttribute(id, SaiPolicerTraits::Attributes::Cbs{200});
  EXPECT_EQ(
      policerApi->getAttribute(id, SaiPolicerTraits::Attributes::Cir{}), 2000);
  EXPECT_EQ(
      policerApi->getAttribute(id, SaiPolicerTraits::Attributes::Cbs{}), 200);
  EXPECT_EQ(
      policerApi->getAttribute(id, SaiPolicerTraits::Attributes::Pbs{}), 0);
  EXPECT_THROW(
      policerApi->setAttribute(
          id, SaiPolicerTraits::Attributes::Type{SAI_METER_TYPE_BYTES}),
      SaiApiError);
  EXPECT_THROW(
      policerApi->setAttribute(
          id, SaiPolicerTraits::Attributes::Mode{SAI_POLICER_MODE_TR_TCM}),
      SaiApiError);
  policerApi->remove(id);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
TEST_F(PolicerApiTest, omittedLabelDefaultsToEmpty) {
  sai_policer_api_t* rawApi{};
  ASSERT_EQ(
      sai_api_query(SAI_API_POLICER, reinterpret_cast<void**>(&rawApi)),
      SAI_STATUS_SUCCESS);
  using Attr = SaiPolicerTraits::Attributes;
  const std::array required{
      *Attr::Type{SAI_METER_TYPE_PACKETS}.saiAttr(),
      *Attr::Mode{SAI_POLICER_MODE_SR_TCM}.saiAttr(),
  };
  sai_object_id_t id{};
  ASSERT_EQ(
      rawApi->create_policer(&id, 0, required.size(), required.data()),
      SAI_STATUS_SUCCESS);
  EXPECT_EQ(
      policerApi->getAttribute(PolicerSaiId{id}, Attr::Label{}),
      SaiCharArray32{});
  policerApi->remove(PolicerSaiId{id});
}

TEST_F(PolicerApiTest, labelRoundTrip) {
  using Label = SaiPolicerTraits::Attributes::Label;
  const SaiCharArray32 initial{"initial-policer"};
  auto attrs = attributes();
  std::get<Label>(attrs) = Label{initial};
  auto id = policerApi->create<SaiPolicerTraits>(attrs, 0);
  EXPECT_EQ(policerApi->getAttribute(id, Label{}), initial);

  const SaiCharArray32 updated{"updated"};
  policerApi->setAttribute(id, Label{updated});
  EXPECT_EQ(policerApi->getAttribute(id, Label{}), updated);
  policerApi->remove(id);
}
#endif

TEST_F(PolicerApiTest, mirrorDefaultsToNoPolicer) {
  using Policer = SaiEnhancedRemoteMirrorTraits::Attributes::Policer;
  for (const auto& policer :
       {std::optional<Policer>{},
        std::optional<Policer>{Policer{SAI_NULL_OBJECT_ID}}}) {
    auto gre = mirrorApi->create<SaiEnhancedRemoteMirrorTraits>(
        mirrorAttributes(policer), 0);
    auto sflow =
        mirrorApi->create<SaiSflowMirrorTraits>(sflowAttributes(policer), 0);
    for (const auto& id : {gre, sflow}) {
      EXPECT_EQ(mirrorApi->getAttribute(id, Policer{}), SAI_NULL_OBJECT_ID);
      mirrorApi->remove(id);
    }
  }
}

TEST_F(PolicerApiTest, createBoundMirrorAndUnbind) {
  auto policer = policerApi->create<SaiPolicerTraits>(attributes(), 0);
  using Policer = SaiEnhancedRemoteMirrorTraits::Attributes::Policer;
  auto gre = mirrorApi->create<SaiEnhancedRemoteMirrorTraits>(
      mirrorAttributes(Policer{policer}), 0);
  auto sflow = mirrorApi->create<SaiSflowMirrorTraits>(
      sflowAttributes(Policer{policer}), 0);
  for (const auto& mirror : {gre, sflow}) {
    EXPECT_EQ(mirrorApi->getAttribute(mirror, Policer{}), policer);
    EXPECT_THROW(policerApi->remove(policer), SaiApiError);
    mirrorApi->setAttribute(mirror, Policer{SAI_NULL_OBJECT_ID});
    EXPECT_EQ(mirrorApi->getAttribute(mirror, Policer{}), SAI_NULL_OBJECT_ID);
    mirrorApi->remove(mirror);
  }
  policerApi->remove(policer);
}

TEST_F(PolicerApiTest, bindAndReplacePolicer) {
  auto first = policerApi->create<SaiPolicerTraits>(attributes(), 0);
  auto second = policerApi->create<SaiPolicerTraits>(attributes(), 0);
  EXPECT_NE(first, second);
  auto mirror =
      mirrorApi->create<SaiEnhancedRemoteMirrorTraits>(mirrorAttributes(), 0);
  mirrorApi->setAttribute(
      mirror, SaiEnhancedRemoteMirrorTraits::Attributes::Policer{first});
  EXPECT_EQ(
      mirrorApi->getAttribute(
          mirror, SaiEnhancedRemoteMirrorTraits::Attributes::Policer{}),
      first);
  mirrorApi->setAttribute(
      mirror, SaiEnhancedRemoteMirrorTraits::Attributes::Policer{second});
  EXPECT_EQ(
      mirrorApi->getAttribute(
          mirror, SaiEnhancedRemoteMirrorTraits::Attributes::Policer{}),
      second);
  policerApi->remove(first);
  EXPECT_THROW(policerApi->remove(second), SaiApiError);
  mirrorApi->remove(mirror);
  policerApi->remove(second);
}

TEST_F(PolicerApiTest, rejectInvalidBinding) {
  EXPECT_THROW(
      mirrorApi->create<SaiEnhancedRemoteMirrorTraits>(
          mirrorAttributes(999999), 0),
      SaiApiError);
  auto mirror =
      mirrorApi->create<SaiEnhancedRemoteMirrorTraits>(mirrorAttributes(), 0);
  EXPECT_THROW(
      mirrorApi->setAttribute(
          mirror, SaiEnhancedRemoteMirrorTraits::Attributes::Policer{999999}),
      SaiApiError);
  EXPECT_EQ(
      mirrorApi->getAttribute(
          mirror, SaiEnhancedRemoteMirrorTraits::Attributes::Policer{}),
      SAI_NULL_OBJECT_ID);
  mirrorApi->remove(mirror);
}

TEST_F(PolicerApiTest, enumeratePolicers) {
  auto first = policerApi->create<SaiPolicerTraits>(attributes(), 0);
  auto second = policerApi->create<SaiPolicerTraits>(attributes(), 0);
  auto keys = getObjectKeys<SaiPolicerTraits>(0);
  EXPECT_NE(std::find(keys.begin(), keys.end(), first), keys.end());
  EXPECT_NE(std::find(keys.begin(), keys.end(), second), keys.end());
  policerApi->remove(first);
  policerApi->remove(second);
}
