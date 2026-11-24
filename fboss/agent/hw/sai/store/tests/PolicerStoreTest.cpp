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

TEST_F(PolicerStoreTest, createLoadAndReplaceByAttributes) {
  auto& store = saiStore->get<SaiPolicerTraits>();
  auto policer = store.setObject(attributes(), attributes());
  auto id = policer->adapterKey();
  EXPECT_EQ(policer->adapterHostKey(), attributes());
  EXPECT_EQ(policer->attributes(), attributes());
  auto loaded = createObj<SaiPolicerTraits>(id);
  EXPECT_EQ(loaded.adapterHostKey(), attributes());
  EXPECT_EQ(loaded.attributes(), attributes());
  loaded.release();

  auto updatedAttributes = attributes(2000, 200);
  auto updated = store.setObject(updatedAttributes, updatedAttributes);
  EXPECT_NE(updated->adapterKey(), id);
  EXPECT_EQ(updated->attributes(), updatedAttributes);
  EXPECT_EQ(
      saiApiTable->policerApi().getAttribute(
          id, SaiPolicerTraits::Attributes::Cir{}),
      1000);
  EXPECT_EQ(
      saiApiTable->policerApi().getAttribute(
          id, SaiPolicerTraits::Attributes::Cbs{}),
      100);
}

TEST_F(
    PolicerStoreTest,
    sharedAttributesSurviveReloadWithoutSerializedHostKeys) {
  auto& store = saiStore->get<SaiPolicerTraits>();
  auto first = store.setObject(attributes(), attributes());
  auto second = store.setObject(attributes(), attributes());
  auto id = first->adapterKey();
  EXPECT_EQ(first, second);
  auto keys = saiStore->adapterKeysFollyDynamic();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload(&keys);
  auto& recovered = reloaded.get<SaiPolicerTraits>();
  auto reclaimed = recovered.setObject(attributes(), attributes());
  EXPECT_EQ(reclaimed->adapterKey(), id);
  EXPECT_EQ(reclaimed->adapterHostKey(), attributes());
  auto updatedAttributes = attributes(2000, 200);
  auto updated = recovered.setObject(updatedAttributes, updatedAttributes);
  EXPECT_NE(updated->adapterKey(), id);
  EXPECT_EQ(updated->attributes(), updatedAttributes);
  EXPECT_EQ(
      saiApiTable->policerApi().getAttribute(
          id, SaiPolicerTraits::Attributes::Cir{}),
      1000);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
TEST_F(PolicerStoreTest, labelDistinguishesPolicersWithIdenticalSettings) {
  using Label = SaiPolicerTraits::Attributes::Label;
  auto firstAttrs = attributes();
  auto secondAttrs = firstAttrs;
  std::get<Label>(firstAttrs) = Label{SaiCharArray32{"first"}};
  std::get<Label>(secondAttrs) = Label{SaiCharArray32{"second"}};

  auto& store = saiStore->get<SaiPolicerTraits>();
  auto first = store.setObject(firstAttrs, firstAttrs);
  auto second = store.setObject(secondAttrs, secondAttrs);
  const auto firstId = first->adapterKey();
  const auto secondId = second->adapterKey();
  EXPECT_NE(first, second);
  EXPECT_NE(firstId, secondId);
  EXPECT_EQ(store.setObject(firstAttrs, firstAttrs), first);
  EXPECT_EQ(store.setObject(secondAttrs, secondAttrs), second);

  auto keys = saiStore->adapterKeysFollyDynamic();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload(&keys);
  auto& recovered = reloaded.get<SaiPolicerTraits>();
  auto firstRecovered = recovered.setObject(firstAttrs, firstAttrs);
  auto secondRecovered = recovered.setObject(secondAttrs, secondAttrs);
  EXPECT_EQ(firstRecovered->adapterKey(), firstId);
  EXPECT_EQ(secondRecovered->adapterKey(), secondId);
}
#endif

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
  auto first = policers.setObject(attributes(), attributes());
  auto second =
      policers.setObject(attributes(2000, 200), attributes(2000, 200));
  auto firstAttrs = mirrorAttributes(
      SaiEnhancedRemoteMirrorTraits::Attributes::Policer{first->adapterKey()});
  auto secondAttrs = mirrorAttributes(
      SaiEnhancedRemoteMirrorTraits::Attributes::Policer{second->adapterKey()});
  SaiEnhancedRemoteMirrorTraits::AdapterHostKey key{
      SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE,
      42,
      folly::IPAddress{"10.0.0.1"},
      folly::IPAddress{"10.0.0.2"}};
  auto& mirrors = saiStore->get<SaiEnhancedRemoteMirrorTraits>();
  auto firstMirror = mirrors.setObject(key, firstAttrs);
  auto secondMirror = mirrors.setObject(key, secondAttrs);
  auto firstId = firstMirror->adapterKey();
  auto secondId = secondMirror->adapterKey();
  EXPECT_EQ(firstId, secondId);
  auto keys = saiStore->adapterKeysFollyDynamic();
  auto names = saiStore->adapterKeys2AdapterHostKeysFollyDynamic();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload(&keys, &names);
  auto& recovered = reloaded.get<SaiEnhancedRemoteMirrorTraits>();
  ASSERT_NE(recovered.get(key), nullptr);
  EXPECT_EQ(recovered.get(key)->adapterKey(), firstId);
  EXPECT_EQ(recovered.get(key)->attributes(), secondAttrs);
  EXPECT_EQ(recovered.size(), 1);
  reloaded.removeUnexpectedUnclaimedWarmbootHandles();
  EXPECT_TRUE(fs->mirrorManager.map().empty());
  EXPECT_TRUE(fs->policerManager.map().empty());
}

TEST_F(PolicerStoreTest, greAttributeUpdatesReuseObject) {
  using Attr = SaiEnhancedRemoteMirrorTraits::Attributes;
  auto policer =
      saiStore->get<SaiPolicerTraits>().setObject(attributes(), attributes());
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
  auto first = policers.setObject(attributes(), attributes());
  auto second =
      policers.setObject(attributes(2000, 200), attributes(2000, 200));
  auto firstAttrs = sflowMirrorAttributes(Attr::Policer{first->adapterKey()});
  auto secondAttrs = sflowMirrorAttributes(Attr::Policer{second->adapterKey()});
  SaiSflowMirrorTraits::AdapterHostKey key{
      SAI_MIRROR_SESSION_TYPE_SFLOW,
      42,
      folly::IPAddress{"10.0.0.1"},
      folly::IPAddress{"10.0.0.2"},
      1234,
      6343};
  auto& mirrors = saiStore->get<SaiSflowMirrorTraits>();
  auto firstMirror = mirrors.setObject(key, firstAttrs);
  auto secondMirror = mirrors.setObject(key, secondAttrs);
  auto firstId = firstMirror->adapterKey();
  auto secondId = secondMirror->adapterKey();
  EXPECT_EQ(firstId, secondId);
  auto keys = saiStore->adapterKeysFollyDynamic();
  auto names = saiStore->adapterKeys2AdapterHostKeysFollyDynamic();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload(&keys, &names);
  auto& recovered = reloaded.get<SaiSflowMirrorTraits>();
  ASSERT_NE(recovered.get(key), nullptr);
  EXPECT_EQ(recovered.get(key)->adapterKey(), firstId);
  EXPECT_EQ(recovered.get(key)->attributes(), secondAttrs);
  EXPECT_EQ(recovered.size(), 1);
  reloaded.removeUnexpectedUnclaimedWarmbootHandles();
  EXPECT_TRUE(fs->mirrorManager.map().empty());
  EXPECT_TRUE(fs->policerManager.map().empty());
}

TEST_F(PolicerStoreTest, mirrorStoreBindAndUnbind) {
  auto policer =
      saiStore->get<SaiPolicerTraits>().setObject(attributes(), attributes());
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
