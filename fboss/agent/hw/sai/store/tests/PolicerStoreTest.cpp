// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include "fboss/agent/hw/sai/api/PolicerApi.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class PolicerStoreTest : public SaiStoreTest {
 public:
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
        SAI_PACKET_ACTION_DROP};
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

  SaiSflowMirrorTraits::CreateAttributes sflowMirrorAttributes(
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
};

TEST_F(PolicerStoreTest, createLoadAndUpdateByName) {
  auto& store = saiStore->get<SaiPolicerTraits>();
  auto policer = store.setObject("mirror0", attributes());
  auto id = policer->adapterKey();
  EXPECT_EQ(policer->adapterHostKey(), "mirror0");
  EXPECT_EQ(policer->attributes(), attributes());
  auto loaded = createObj<SaiPolicerTraits>(id, "mirror0");
  EXPECT_EQ(loaded.adapterHostKey(), "mirror0");
  EXPECT_EQ(loaded.attributes(), attributes());
  loaded.release();

  auto updated = store.setObject("mirror0", attributes(2000, 200));
  EXPECT_EQ(updated->adapterKey(), id);
  EXPECT_EQ(
      saiApiTable->policerApi().getAttribute(
          id, SaiPolicerTraits::Attributes::Cir{}),
      2000);
  EXPECT_EQ(
      saiApiTable->policerApi().getAttribute(
          id, SaiPolicerTraits::Attributes::Cbs{}),
      200);
}

TEST_F(PolicerStoreTest, independentSameRateNamesSurviveSerializedReload) {
  auto& store = saiStore->get<SaiPolicerTraits>();
  auto first = store.setObject("mirror0", attributes());
  auto second = store.setObject("mirror1", attributes());
  auto firstId = first->adapterKey();
  auto secondId = second->adapterKey();
  EXPECT_NE(firstId, secondId);
  EXPECT_EQ(first->adapterHostKeyToFollyDynamic().asString(), "mirror0");
  EXPECT_EQ(
      SaiObject<SaiPolicerTraits>::follyDynamicToAdapterHostKey(
          second->adapterHostKeyToFollyDynamic()),
      "mirror1");
  auto keys = saiStore->adapterKeysFollyDynamic();
  auto names = saiStore->adapterKeys2AdapterHostKeysFollyDynamic();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload(&keys, &names);
  auto& recovered = reloaded.get<SaiPolicerTraits>();
  ASSERT_NE(recovered.get("mirror0"), nullptr);
  ASSERT_NE(recovered.get("mirror1"), nullptr);
  EXPECT_EQ(recovered.get("mirror0")->adapterKey(), firstId);
  EXPECT_EQ(recovered.get("mirror1")->adapterKey(), secondId);
  auto updated = recovered.setObject("mirror0", attributes(2000, 200));
  EXPECT_EQ(updated->adapterKey(), firstId);
  EXPECT_EQ(
      saiApiTable->policerApi().getAttribute(
          secondId, SaiPolicerTraits::Attributes::Cir{}),
      1000);
}

TEST_F(PolicerStoreTest, greUnboundMirrorReusesObjectAfterReload) {
  using Attr = SaiEnhancedRemoteMirrorTraits::Attributes;
  auto attrs = mirrorAttributes();
  std::get<std::optional<Attr::SampleRate>>(attrs).reset();
  auto key = detail::adapterHostKey<SaiEnhancedRemoteMirrorTraits>(
      MirrorSaiId{0}, attrs);
  auto mirror =
      saiStore->get<SaiEnhancedRemoteMirrorTraits>().setObject(key, attrs);
  auto id = mirror->adapterKey();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload();
  auto& mirrors = reloaded.get<SaiEnhancedRemoteMirrorTraits>();
  auto loaded = mirrors.get(key);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->adapterKey(), id);
  EXPECT_EQ(loaded->adapterHostKey(), key);
  EXPECT_EQ(mirrors.setObject(key, attrs)->adapterKey(), id);
  EXPECT_EQ(mirrors.size(), 1);
}

TEST_F(PolicerStoreTest, grePolicerUpdatesReuseObjectAfterReload) {
  auto& policers = saiStore->get<SaiPolicerTraits>();
  auto first = policers.setObject("mirror0", attributes());
  auto second = policers.setObject("mirror1", attributes());
  auto firstAttrs = mirrorAttributes(
      SaiEnhancedRemoteMirrorTraits::Attributes::Policer{first->adapterKey()});
  auto secondAttrs = mirrorAttributes(
      SaiEnhancedRemoteMirrorTraits::Attributes::Policer{second->adapterKey()});
  auto firstKey = detail::adapterHostKey<SaiEnhancedRemoteMirrorTraits>(
      MirrorSaiId{0}, firstAttrs);
  auto secondKey = detail::adapterHostKey<SaiEnhancedRemoteMirrorTraits>(
      MirrorSaiId{0}, secondAttrs);
  EXPECT_EQ(firstKey, secondKey);
  auto& mirrors = saiStore->get<SaiEnhancedRemoteMirrorTraits>();
  auto firstMirror = mirrors.setObject(firstKey, firstAttrs);
  auto secondMirror = mirrors.setObject(secondKey, secondAttrs);
  auto firstId = firstMirror->adapterKey();
  auto secondId = secondMirror->adapterKey();
  EXPECT_EQ(firstId, secondId);
  auto keys = saiStore->adapterKeysFollyDynamic();
  auto names = saiStore->adapterKeys2AdapterHostKeysFollyDynamic();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload(&keys, &names);
  auto& recovered = reloaded.get<SaiEnhancedRemoteMirrorTraits>();
  ASSERT_NE(recovered.get(firstKey), nullptr);
  EXPECT_EQ(recovered.get(firstKey)->adapterKey(), firstId);
  EXPECT_EQ(recovered.get(firstKey)->attributes(), secondAttrs);
  EXPECT_EQ(recovered.size(), 1);
  reloaded.removeUnexpectedUnclaimedWarmbootHandles();
  EXPECT_TRUE(fs->mirrorManager.map().empty());
  EXPECT_TRUE(fs->policerManager.map().empty());
}

TEST_F(PolicerStoreTest, greAttributeUpdatesReuseObject) {
  using Attr = SaiEnhancedRemoteMirrorTraits::Attributes;
  auto policer =
      saiStore->get<SaiPolicerTraits>().setObject("mirror0", attributes());
  auto attrs = mirrorAttributes(Attr::Policer{policer->adapterKey()});
  auto key = detail::adapterHostKey<SaiEnhancedRemoteMirrorTraits>(
      MirrorSaiId{0}, attrs);
  auto& mirrors = saiStore->get<SaiEnhancedRemoteMirrorTraits>();
  auto mirror = mirrors.setObject(key, attrs);
  auto id = mirror->adapterKey();
  std::get<Attr::DstMacAddress>(attrs) =
      Attr::DstMacAddress{folly::MacAddress{"00:11:22:33:44:77"}};
  std::get<std::optional<Attr::Ttl>>(attrs) = Attr::Ttl{128};
  std::get<std::optional<Attr::SampleRate>>(attrs) = Attr::SampleRate{100};
  auto updatedKey = detail::adapterHostKey<SaiEnhancedRemoteMirrorTraits>(
      MirrorSaiId{0}, attrs);
  auto updated = mirrors.setObject(updatedKey, attrs);
  EXPECT_EQ(updated->adapterKey(), id);
  auto readback = createObj<SaiEnhancedRemoteMirrorTraits>(id);
  EXPECT_EQ(readback.attributes(), attrs);
  readback.release();
  auto keys = saiStore->adapterKeysFollyDynamic();
  auto names = saiStore->adapterKeys2AdapterHostKeysFollyDynamic();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload(&keys, &names);
  auto& recovered = reloaded.get<SaiEnhancedRemoteMirrorTraits>();
  auto loaded = recovered.get(key);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->adapterKey(), id);
  EXPECT_EQ(loaded->attributes(), attrs);
  auto reused = recovered.setObject(updatedKey, attrs);
  EXPECT_EQ(reused->adapterKey(), id);
  EXPECT_EQ(recovered.size(), 1);
  reused.reset();
  loaded.reset();
  reloaded.removeUnexpectedUnclaimedWarmbootHandles();
  EXPECT_TRUE(fs->mirrorManager.map().empty());
  EXPECT_TRUE(fs->policerManager.map().empty());
}

TEST_F(PolicerStoreTest, sflowUnboundMirrorReusesObjectAfterReload) {
  using Attr = SaiSflowMirrorTraits::Attributes;
  auto omittedAttrs = sflowMirrorAttributes();
  auto attrs = sflowMirrorAttributes(Attr::Policer{SAI_NULL_OBJECT_ID});
  auto key =
      detail::adapterHostKey<SaiSflowMirrorTraits>(MirrorSaiId{0}, attrs);
  EXPECT_EQ(
      key,
      detail::adapterHostKey<SaiSflowMirrorTraits>(
          MirrorSaiId{0}, omittedAttrs));
  auto mirror = saiStore->get<SaiSflowMirrorTraits>().setObject(key, attrs);
  auto id = mirror->adapterKey();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload();
  auto& mirrors = reloaded.get<SaiSflowMirrorTraits>();
  auto loaded = mirrors.get(key);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->adapterKey(), id);
  EXPECT_EQ(loaded->adapterHostKey(), key);
  EXPECT_EQ(mirrors.setObject(key, omittedAttrs)->adapterKey(), id);
  EXPECT_EQ(mirrors.size(), 1);
}

TEST_F(PolicerStoreTest, sflowPolicerUpdatesReuseObjectAfterReload) {
  using Attr = SaiSflowMirrorTraits::Attributes;
  auto& policers = saiStore->get<SaiPolicerTraits>();
  auto first = policers.setObject("sflow0", attributes());
  auto second = policers.setObject("sflow1", attributes());
  auto firstAttrs = sflowMirrorAttributes(Attr::Policer{first->adapterKey()});
  auto secondAttrs = sflowMirrorAttributes(Attr::Policer{second->adapterKey()});
  auto firstKey =
      detail::adapterHostKey<SaiSflowMirrorTraits>(MirrorSaiId{0}, firstAttrs);
  auto secondKey =
      detail::adapterHostKey<SaiSflowMirrorTraits>(MirrorSaiId{0}, secondAttrs);
  ASSERT_EQ(firstKey, secondKey);
  auto& mirrors = saiStore->get<SaiSflowMirrorTraits>();
  auto firstMirror = mirrors.setObject(firstKey, firstAttrs);
  auto secondMirror = mirrors.setObject(secondKey, secondAttrs);
  auto firstId = firstMirror->adapterKey();
  auto secondId = secondMirror->adapterKey();
  EXPECT_EQ(firstId, secondId);
  auto keys = saiStore->adapterKeysFollyDynamic();
  auto names = saiStore->adapterKeys2AdapterHostKeysFollyDynamic();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload(&keys, &names);
  auto& recovered = reloaded.get<SaiSflowMirrorTraits>();
  ASSERT_NE(recovered.get(firstKey), nullptr);
  EXPECT_EQ(recovered.get(firstKey)->adapterKey(), firstId);
  EXPECT_EQ(recovered.get(firstKey)->attributes(), secondAttrs);
  EXPECT_EQ(recovered.size(), 1);
  reloaded.removeUnexpectedUnclaimedWarmbootHandles();
  EXPECT_TRUE(fs->mirrorManager.map().empty());
  EXPECT_TRUE(fs->policerManager.map().empty());
}

TEST_F(PolicerStoreTest, mirrorStoreBindAndUnbind) {
  auto policer =
      saiStore->get<SaiPolicerTraits>().setObject("mirror0", attributes());
  auto attrs = mirrorAttributes();
  auto key = detail::adapterHostKey<SaiEnhancedRemoteMirrorTraits>(
      MirrorSaiId{0}, attrs);
  auto mirror =
      saiStore->get<SaiEnhancedRemoteMirrorTraits>().setObject(key, attrs);
  auto id = mirror->adapterKey();
  mirror->setOptionalAttribute(
      SaiEnhancedRemoteMirrorTraits::Attributes::Policer{
          policer->adapterKey()});
  EXPECT_EQ(
      saiApiTable->mirrorApi().getAttribute(
          id, SaiEnhancedRemoteMirrorTraits::Attributes::Policer{}),
      policer->adapterKey());
  mirror->setOptionalAttribute(
      SaiEnhancedRemoteMirrorTraits::Attributes::Policer{SAI_NULL_OBJECT_ID});
  EXPECT_EQ(
      saiApiTable->mirrorApi().getAttribute(
          id, SaiEnhancedRemoteMirrorTraits::Attributes::Policer{}),
      SAI_NULL_OBJECT_ID);
  policer.reset();
  EXPECT_TRUE(fs->policerManager.map().empty());
}
