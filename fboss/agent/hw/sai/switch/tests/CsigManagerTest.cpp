// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiTamManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/hw/switch_asics/FakeAsic.h"
#include "fboss/agent/platforms/sai/SaiFakePlatform.h"

extern "C" {
#include "fboss/agent/hw/sai/api/fake/saifakeextensions.h"
}

using namespace facebook::fboss;

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 1) && !defined(CHENAB_SAI_SDK)
class CsigManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::BLANK;
    ManagerTestBase::SetUp();
    // FakeManager starts at zero; SAI reserves that object ID for null.
    auto fs = FakeSai::getInstance();
    const auto nullTam = fs->tamManager.create(
        std::vector<sai_object_id_t>{}, std::vector<sai_int32_t>{});
    fs->tamManager.remove(nullTam);
    const auto nullInt = fs->tamIntManager.create();
    fs->tamIntManager.remove(nullInt);
  }

  SaiTamIntTraits::CreateAttributes profile(
      sai_int32_t type,
      sai_int32_t signal = SAI_CSIG_SIGNAL_TYPE_ABW) {
    using A = SaiTamIntTraits::Attributes;
    SaiTamIntTraits::CreateAttributes attrs;
    std::get<A::Type>(attrs) = type;
    std::get<A::DeviceId>(attrs) = 0;
    std::get<A::IntPresenceType>(attrs) = SAI_TAM_INT_PRESENCE_TYPE_ETH_TYPE;
    std::get<A::Inline>(attrs) = true;
    std::get<A::EthTypeCodePoint>(attrs) =
        type == SAI_TAM_INT_TYPE_CSIG_COMPACT ? 0x9900 : 0x9901;
    std::get<A::CsigSignalType>(attrs) = signal;
    if (signal == SAI_CSIG_SIGNAL_TYPE_ABW ||
        signal == SAI_CSIG_SIGNAL_TYPE_ABWC) {
      std::get<std::optional<A::BwTimeInterval>>(attrs) = 128;
    }
    if (type == SAI_TAM_INT_TYPE_CSIG_COMPACT) {
      std::get<A::QuantBands>(attrs) = 1;
      std::get<std::optional<A::QuantBandUint64RangeList>>(attrs) =
          std::vector<sai_u64_range_t>{{0, 1000}};
    } else {
      std::get<A::QuantBands>(attrs) = 1048576;
      std::get<std::optional<A::WideQuantBaseValue>>(attrs) = 0;
      std::get<std::optional<A::WideQuantStepValue>>(attrs) = 1024;
    }
    std::get<std::optional<A::CsigDBit>>(attrs) = false;
    return attrs;
  }

  std::vector<sai_object_id_t> bindings() {
    return saiApiTable->switchApi().getAttribute(
        saiManagerTable->switchManager().getSwitchSaiId(),
        SaiSwitchTraits::Attributes::TamObject{});
  }

  void reloadWithRecoveredBindings() {
    saiStore->exitForWarmBoot();
    saiManagerTable->tamManager().gracefulExit();
    saiStore->reload();
  }
};

namespace {
class CsigUnsupportedAsic : public FakeAsic {
 public:
  using FakeAsic::FakeAsic;
  bool isSupported(Feature feature) const override {
    return feature != Feature::CSIG && FakeAsic::isSupported(feature);
  }
};

class CsigUnsupportedPlatform : public SaiFakePlatform {
 public:
  using SaiFakePlatform::SaiFakePlatform;
  HwAsic* getAsic() const override {
    return asic_.get();
  }

 private:
  void setupAsic(
      std::optional<int64_t> switchId,
      const cfg::SwitchInfo& switchInfo,
      std::optional<HwAsic::FabricNodeRole> /*fabricNodeRole*/) override {
    asic_ = std::make_unique<CsigUnsupportedAsic>(switchId, switchInfo);
  }
  std::unique_ptr<CsigUnsupportedAsic> asic_;
};
} // namespace

class CsigUnsupportedAsicTest : public CsigManagerTest {
 protected:
  std::unique_ptr<SaiPlatform> createSaiPlatformForTest(
      std::unique_ptr<PlatformProductInfo> productInfo) override {
    return std::make_unique<CsigUnsupportedPlatform>(std::move(productInfo));
  }
};

TEST_F(CsigUnsupportedAsicTest, RejectsProfilesBeforeProgramming) {
  const auto fs = FakeSai::getInstance();
  auto& manager = saiManagerTable->tamManager();
  EXPECT_THROW(
      manager.programCsigProfiles(
          {profile(SAI_TAM_INT_TYPE_CSIG_COMPACT)}, true),
      FbossError);
  EXPECT_EQ(manager.getCsigHandle(), nullptr);
  EXPECT_TRUE(fs->tamIntManager.map().empty());
  EXPECT_TRUE(fs->tamManager.map().empty());
}

TEST_F(CsigManagerTest, RejectsEmptyUpdateProfiles) {
  const auto fs = FakeSai::getInstance();
  auto& manager = saiManagerTable->tamManager();
  EXPECT_THROW(manager.programCsigProfiles({}, true), FbossError);
  EXPECT_EQ(manager.getCsigHandle(), nullptr);
  EXPECT_TRUE(fs->tamIntManager.map().empty());
  EXPECT_TRUE(fs->tamManager.map().empty());
}

TEST_F(CsigManagerTest, RejectsProfilesOnLegacySdk) {
  const auto fs = FakeSai::getInstance();
  auto& manager = saiManagerTable->tamManager();
  fake_sai_set_legacy_csig_tag_action(true);
  EXPECT_THROW(
      manager.programCsigProfiles(
          {profile(SAI_TAM_INT_TYPE_CSIG_COMPACT)}, true),
      FbossError);
  fake_sai_set_legacy_csig_tag_action(false);
  EXPECT_EQ(manager.getCsigHandle(), nullptr);
  EXPECT_TRUE(fs->tamIntManager.map().empty());
  EXPECT_TRUE(fs->tamManager.map().empty());
}

TEST_F(CsigManagerTest, CompactProfile) {
  using A = SaiTamIntTraits::Attributes;
  auto attrs = profile(SAI_TAM_INT_TYPE_CSIG_COMPACT);
  std::get<std::optional<A::BwTimeInterval>>(attrs) = 256;
  std::get<A::QuantBands>(attrs) = 2;
  std::get<std::optional<A::QuantBandId>>(attrs) =
      std::vector<sai_uint16_t>{7, 3};
  std::get<std::optional<A::QuantBandUint64RangeList>>(attrs) =
      std::vector<sai_u64_range_t>{
          {0, 1ULL << 32}, {(1ULL << 32) + 1, 1ULL << 40}};
  saiManagerTable->tamManager().programCsigProfiles({attrs}, true);
  const auto* handle = saiManagerTable->tamManager().getCsigHandle();
  ASSERT_NE(handle, nullptr);
  ASSERT_EQ(handle->profiles.size(), 1);
  ASSERT_NE(handle->tam, nullptr);
  const auto id = handle->profiles[0]->adapterKey();
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(id, A::Type{}),
      SAI_TAM_INT_TYPE_CSIG_COMPACT);
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(id, A::CsigSignalType{}),
      SAI_CSIG_SIGNAL_TYPE_ABW);
  EXPECT_EQ(saiApiTable->tamApi().getAttribute(id, A::BwTimeInterval{}), 256);
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(id, A::QuantBandId{}),
      (std::vector<sai_uint16_t>{7, 3}));
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(id, A::QuantBandUint64RangeList{}),
      (std::vector<sai_u64_range_t>{
          {0, 1ULL << 32}, {(1ULL << 32) + 1, 1ULL << 40}}));
  EXPECT_EQ(
      bindings(), (std::vector<sai_object_id_t>{handle->tam->adapterKey()}));
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(
          handle->tam->adapterKey(), SaiTamTraits::Attributes::IntObjectList{}),
      (std::vector<sai_object_id_t>{id}));
}

TEST_F(CsigManagerTest, EventAndCsigBindings) {
  auto& manager = saiManagerTable->tamManager();
  manager.programCsigProfiles({profile(SAI_TAM_INT_TYPE_CSIG_COMPACT)}, true);
  const auto csigTam = manager.getCsigHandle()->tam;

  SaiTamTraits::CreateAttributes attrs{
      std::vector<sai_object_id_t>{100},
      std::vector<sai_int32_t>{
          SAI_TAM_BIND_POINT_TYPE_SWITCH, SAI_TAM_BIND_POINT_TYPE_PORT},
      std::vector<sai_object_id_t>{}};
  auto eventTam = saiStore->get<SaiTamTraits>().setObject(attrs, attrs);

  folly::F14FastMap<std::string, std::unique_ptr<SaiEventTamHandle>>
      eventTamHandles;
  auto eventTamHandle = std::make_unique<SaiEventTamHandle>();
  eventTamHandle->tam = eventTam;
  eventTamHandles.emplace("event", std::move(eventTamHandle));
  auto csigHandle = std::make_unique<SaiCsigTamHandle>();
  csigHandle->tam = csigTam;

  auto& switchManager = saiManagerTable->switchManager();
  switchManager.setTamObject(
      getSwitchTamIds(eventTamHandles, csigHandle.get()));
  EXPECT_EQ(
      bindings(),
      (std::vector<sai_object_id_t>{
          eventTam->adapterKey(), csigTam->adapterKey()}));

  csigHandle.reset();
  switchManager.setTamObject(
      getSwitchTamIds(eventTamHandles, csigHandle.get()));
  EXPECT_EQ(bindings(), (std::vector<sai_object_id_t>{eventTam->adapterKey()}));
  switchManager.setTamObject({});
}

TEST_F(CsigManagerTest, WideProfileChangesAndPassthrough) {
  using A = SaiTamIntTraits::Attributes;
  auto attrs = profile(SAI_TAM_INT_TYPE_CSIG_WIDE, SAI_CSIG_SIGNAL_TYPE_DELAY);
  std::get<std::optional<A::CsigDBit>>(attrs) = true;
  auto& manager = saiManagerTable->tamManager();
  manager.programCsigProfiles({attrs}, true);
  const auto id = manager.getCsigHandle()->profiles[0]->adapterKey();
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(id, A::Type{}),
      SAI_TAM_INT_TYPE_CSIG_WIDE);
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(id, A::CsigSignalType{}),
      SAI_CSIG_SIGNAL_TYPE_DELAY);
  EXPECT_EQ(saiApiTable->tamApi().getAttribute(id, A::QuantBands{}), 1048576);
  EXPECT_EQ(saiApiTable->tamApi().getAttribute(id, A::WideQuantBaseValue{}), 0);
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(id, A::WideQuantStepValue{}), 1024);
  EXPECT_TRUE(saiApiTable->tamApi().getAttribute(id, A::CsigDBit{}));
  std::get<std::optional<A::WideQuantStepValue>>(attrs) = 2048;
  manager.programCsigProfiles({attrs}, true);
  EXPECT_EQ(manager.getCsigHandle()->profiles[0]->adapterKey(), id);
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(id, A::WideQuantStepValue{}), 2048);
  manager.programCsigProfiles({attrs}, false);
  EXPECT_EQ(manager.getCsigHandle()->tam, nullptr);
  EXPECT_TRUE(bindings().empty());
  manager.clearCsig();
  EXPECT_EQ(manager.getCsigHandle(), nullptr);
  EXPECT_TRUE(FakeSai::getInstance()->tamIntManager.map().empty());
  EXPECT_TRUE(FakeSai::getInstance()->tamManager.map().empty());
}

TEST_F(CsigManagerTest, ReplacesSwitchTamBindings) {
  auto& manager = saiManagerTable->tamManager();
  manager.programCsigProfiles({profile(SAI_TAM_INT_TYPE_CSIG_COMPACT)}, true);
  const auto csig = manager.getCsigHandle()->tam->adapterKey();
  SaiTamTraits::CreateAttributes attrs{
      std::vector<sai_object_id_t>{100},
      std::vector<sai_int32_t>{SAI_TAM_BIND_POINT_TYPE_SWITCH},
      std::vector<sai_object_id_t>{}};
  const auto other = saiApiTable->tamApi().create<SaiTamTraits>(attrs, 0);
  auto& switchManager = saiManagerTable->switchManager();
  switchManager.setTamObject({other});
  EXPECT_EQ(bindings(), (std::vector<sai_object_id_t>{other}));
  switchManager.setTamObject({other, csig});
  EXPECT_EQ(bindings(), (std::vector<sai_object_id_t>{other, csig}));
  switchManager.setTamObject({});
  EXPECT_TRUE(bindings().empty());
  manager.clearCsig();
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(
          other, SaiTamTraits::Attributes::EventObjectList{}),
      (std::vector<sai_object_id_t>{100}));
  saiApiTable->tamApi().remove(other);
}

TEST_F(CsigManagerTest, ReplacesRecoveredBindingAfterProfileChange) {
  auto& manager = saiManagerTable->tamManager();
  manager.programCsigProfiles({profile(SAI_TAM_INT_TYPE_CSIG_COMPACT)}, true);
  const auto oldTam = manager.getCsigHandle()->tam->adapterKey();
  reloadWithRecoveredBindings();

  manager.programCsigProfiles(
      {profile(SAI_TAM_INT_TYPE_CSIG_WIDE, SAI_CSIG_SIGNAL_TYPE_DELAY)}, true);

  const auto newTam = manager.getCsigHandle()->tam->adapterKey();
  EXPECT_NE(newTam, oldTam);
  EXPECT_EQ(bindings(), (std::vector<sai_object_id_t>{newTam}));
}

TEST_F(CsigManagerTest, ClearsRecoveredBinding) {
  auto& manager = saiManagerTable->tamManager();
  manager.programCsigProfiles({profile(SAI_TAM_INT_TYPE_CSIG_COMPACT)}, true);
  reloadWithRecoveredBindings();
  manager.clearCsig();
  EXPECT_TRUE(bindings().empty());
}

TEST_F(CsigManagerTest, UnsupportedFormatsAndSignals) {
  auto attrs = profile(SAI_TAM_INT_TYPE_CSIG_WIDE, SAI_CSIG_SIGNAL_TYPE_DELAY);
  auto fs = FakeSai::getInstance();
  fs->csigCapabilities.tagTypes = {SAI_TAM_INT_TYPE_CSIG_COMPACT};
  EXPECT_THROW(
      saiManagerTable->tamManager().programCsigProfiles({attrs}, true),
      FbossError);
  EXPECT_TRUE(fs->tamIntManager.map().empty());
  fs->csigCapabilities.tagTypes.push_back(SAI_TAM_INT_TYPE_CSIG_WIDE);
  fs->csigCapabilities.signals = {SAI_CSIG_SIGNAL_TYPE_ABW};
  EXPECT_THROW(
      saiManagerTable->tamManager().programCsigProfiles({attrs}, true),
      FbossError);
  EXPECT_TRUE(fs->tamIntManager.map().empty());
}

TEST_F(CsigManagerTest, UnsupportedTrimMarking) {
  using A = SaiTamIntTraits::Attributes;
  auto attrs = profile(SAI_TAM_INT_TYPE_CSIG_WIDE, SAI_CSIG_SIGNAL_TYPE_DELAY);
  auto fs = FakeSai::getInstance();
  fs->csigCapabilities.dBit = false;
  std::get<std::optional<A::CsigDBit>>(attrs) = true;
  EXPECT_THROW(
      saiManagerTable->tamManager().programCsigProfiles({attrs}, true),
      FbossError);
  EXPECT_TRUE(fs->tamIntManager.map().empty());
  std::get<std::optional<A::CsigDBit>>(attrs) = false;
  saiManagerTable->tamManager().programCsigProfiles({attrs}, true);
  const auto id =
      saiManagerTable->tamManager().getCsigHandle()->profiles[0]->adapterKey();
  EXPECT_FALSE(saiApiTable->tamApi().getAttribute(id, A::CsigDBit{}));
}

TEST_F(CsigManagerTest, OptionalCustomBandIds) {
  using A = SaiTamIntTraits::Attributes;
  auto attrs = profile(SAI_TAM_INT_TYPE_CSIG_COMPACT);
  std::get<std::optional<A::QuantBandId>>(attrs) = std::vector<sai_uint16_t>{7};
  auto fs = FakeSai::getInstance();
  fs->csigCapabilities.customBandIds = false;
  EXPECT_THROW(
      saiManagerTable->tamManager().programCsigProfiles({attrs}, true),
      FbossError);
  EXPECT_TRUE(fs->tamIntManager.map().empty());
  std::get<std::optional<A::QuantBandId>>(attrs).reset();
  saiManagerTable->tamManager().programCsigProfiles({attrs}, true);
  const auto id =
      saiManagerTable->tamManager().getCsigHandle()->profiles[0]->adapterKey();
  std::optional<A::QuantBandId> ids;
  EXPECT_FALSE(saiApiTable->tamApi().getAttribute(id, ids).has_value());
}

TEST_F(CsigManagerTest, UnsupportedBandwidthInterval) {
  auto attrs = profile(SAI_TAM_INT_TYPE_CSIG_COMPACT);
  std::get<std::optional<SaiTamIntTraits::Attributes::BwTimeInterval>>(attrs) =
      129;
  EXPECT_THROW(
      saiManagerTable->tamManager().programCsigProfiles({attrs}, true),
      FbossError);
  EXPECT_TRUE(FakeSai::getInstance()->tamIntManager.map().empty());
}

TEST_F(CsigManagerTest, BandwidthTimerCapacity) {
  using A = SaiTamIntTraits::Attributes;
  auto compact = profile(SAI_TAM_INT_TYPE_CSIG_COMPACT);
  auto relative =
      profile(SAI_TAM_INT_TYPE_CSIG_COMPACT, SAI_CSIG_SIGNAL_TYPE_ABWC);
  std::get<std::optional<A::BwTimeInterval>>(relative) = 256;
  auto wide = profile(SAI_TAM_INT_TYPE_CSIG_WIDE);
  std::get<std::optional<A::BwTimeInterval>>(wide) = 512;
  EXPECT_THROW(
      saiManagerTable->tamManager().programCsigProfiles(
          {compact, relative, wide}, true),
      FbossError);
  EXPECT_TRUE(FakeSai::getInstance()->tamIntManager.map().empty());
}

TEST_F(CsigManagerTest, SignalsShareBandwidthTimer) {
  auto compact = profile(SAI_TAM_INT_TYPE_CSIG_COMPACT);
  auto relative =
      profile(SAI_TAM_INT_TYPE_CSIG_COMPACT, SAI_CSIG_SIGNAL_TYPE_ABWC);
  FakeSai::getInstance()
      ->switchManager.get(saiManagerTable->switchManager().getSwitchSaiId())
      .maxCsigTimers = 1;
  saiManagerTable->tamManager().programCsigProfiles({compact, relative}, true);
  ASSERT_EQ(saiManagerTable->tamManager().getCsigHandle()->profiles.size(), 2);
  const auto id =
      saiManagerTable->tamManager().getCsigHandle()->profiles[1]->adapterKey();
  EXPECT_EQ(
      saiApiTable->tamApi().getAttribute(
          id, SaiTamIntTraits::Attributes::CsigSignalType{}),
      SAI_CSIG_SIGNAL_TYPE_ABWC);
}

TEST_F(CsigManagerTest, WarmbootReload) {
  auto attrs = profile(SAI_TAM_INT_TYPE_CSIG_WIDE, SAI_CSIG_SIGNAL_TYPE_NQD);
  auto& manager = saiManagerTable->tamManager();
  manager.programCsigProfiles({attrs}, true);
  const auto id = manager.getCsigHandle()->profiles[0]->adapterKey();
  const auto tam = manager.getCsigHandle()->tam->adapterKey();
  saiStore->exitForWarmBoot();
  manager.gracefulExit();
  saiStore->reload();
  manager.programCsigProfiles({attrs}, true);
  EXPECT_EQ(manager.getCsigHandle()->profiles[0]->adapterKey(), id);
  EXPECT_EQ(manager.getCsigHandle()->tam->adapterKey(), tam);
  EXPECT_EQ(bindings(), (std::vector<sai_object_id_t>{tam}));
}

TEST_F(CsigManagerTest, RemovesRecoveredBindingForPassthrough) {
  auto attrs = profile(SAI_TAM_INT_TYPE_CSIG_WIDE, SAI_CSIG_SIGNAL_TYPE_DELAY);
  auto& manager = saiManagerTable->tamManager();
  manager.programCsigProfiles({attrs}, true);
  reloadWithRecoveredBindings();
  manager.programCsigProfiles({attrs}, false);
  EXPECT_TRUE(bindings().empty());
  EXPECT_EQ(manager.getCsigHandle()->tam, nullptr);
}
#endif
