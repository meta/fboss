// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/platform/platform_manager/PmUnitIdentity.h"

using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::platform_manager;

namespace {
EepromContents makeIdprom(
    const std::string& productName,
    const std::string& productionState,
    const std::string& productionSubState,
    const std::string& variantIndicator) {
  EepromContents idprom;
  idprom.productName() = productName;
  idprom.productionState() = productionState;
  idprom.productionSubState() = productionSubState;
  idprom.variantIndicator() = variantIndicator;
  return idprom;
}

PmUnitVersion makeVersion(
    int16_t productionState,
    int16_t productionSubState,
    int16_t respinVariantIndicator) {
  PmUnitVersion version;
  version.productionState() = productionState;
  version.productionSubState() = productionSubState;
  version.respinVariantIndicator() = respinVariantIndicator;
  return version;
}
} // namespace

TEST(PmUnitIdentityTest, ConfiguredPmUnitNameWinsOverIdprom) {
  SlotTypeConfig slotTypeConfig;
  slotTypeConfig.pmUnitName() = "SCM";

  const PmUnitIdentity expected{"SCM", makeVersion(4, 2, 10)};
  EXPECT_EQ(
      identifyPmUnit(slotTypeConfig, makeIdprom("SCM_V2", "4", "2", "10")),
      expected);
}

TEST(PmUnitIdentityTest, IdpromNamesPmUnitWhenSlotTypeDoesNot) {
  const PmUnitIdentity expected{"NETLAKE", makeVersion(4, 2, 2)};
  EXPECT_EQ(
      identifyPmUnit(SlotTypeConfig{}, makeIdprom("NETLAKE", "4", "2", "2")),
      expected);
}

// An unprogrammed field leaves the PmUnit on its default config.
TEST(PmUnitIdentityTest, UnparseableVersionIsAbsent) {
  const PmUnitIdentity expected{"NETLAKE", std::nullopt};
  EXPECT_EQ(
      identifyPmUnit(SlotTypeConfig{}, makeIdprom("NETLAKE", "4", "", "2")),
      expected);
}
