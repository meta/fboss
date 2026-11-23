// Copyright 2004-present Facebook. All Rights Reserved.

#include <gtest/gtest.h>

#include "fboss/lib/fpga/MinipackSystemContainer.h"
#include "fboss/lib/fpga/facebook/tests/FakeFpgaDevice.h"
#include "fboss/lib/fpga/tests/BaseSystemContainerTests.h"

namespace {
constexpr auto kFakePhysicalAddr = 0xfdf00000;
constexpr auto kFakeSize = 1024 * 1024;
constexpr auto kPimOffset = 0x40000;
constexpr auto kPimStride = 0x8000;
constexpr auto kLedRegisterOffset = 0x0310;
constexpr auto kLedRegisterStride = 0x4;
constexpr auto kPimStart = 2;
constexpr auto kNumPims = 8;
constexpr auto kMinipackPimNumLeds = 16;
} // namespace

namespace facebook::fboss {

class MinipackSystemContainerTests : public BaseSystemContainerTests {
 protected:
  const std::unordered_map<int, int> kPimToNumLeds = {
      {2, kMinipackPimNumLeds},
      {3, kMinipackPimNumLeds},
      {4, kMinipackPimNumLeds},
      {5, kMinipackPimNumLeds},
      {6, kMinipackPimNumLeds},
      {7, kMinipackPimNumLeds},
      {8, kMinipackPimNumLeds},
      {9, kMinipackPimNumLeds},
  };
  const std::unordered_map<int, MultiPimPlatformPimContainer::PimType>
      kPimToPimType = {
          {2, MultiPimPlatformPimContainer::PimType::MINIPACK_16Q},
          {3, MultiPimPlatformPimContainer::PimType::MINIPACK_16Q},
          {4, MultiPimPlatformPimContainer::PimType::MINIPACK_16O},
          {5, MultiPimPlatformPimContainer::PimType::MINIPACK_16O},
          {6, MultiPimPlatformPimContainer::PimType::MINIPACK_16O},
          {7, MultiPimPlatformPimContainer::PimType::MINIPACK_16O},
          {8, MultiPimPlatformPimContainer::PimType::MINIPACK_16Q},
          {9, MultiPimPlatformPimContainer::PimType::MINIPACK_16Q},
      };
  std::unique_ptr<MinipackSystemContainer> systemContainer_;

  void SetUp() override {
    // Use fake fpga device

    systemContainer_ = std::make_unique<MinipackSystemContainer>(
        std::make_unique<FakeFpgaDevice>(kFakePhysicalAddr, kFakeSize));

    // Hold a reference for validation. memory regions will write to the fake
    // device
    fpgaDevice_ =
        dynamic_cast<FakeFpgaDevice*>(systemContainer_->getFpgaDevice());

    // Set pim types
    static constexpr auto kMinipack16QPimVal = 0xA3000000;
    static constexpr auto kMinipack16OPimVal = 0xA5000000;
    static constexpr uint32_t kFacebookFpgaPimTypeReg = 0x0;
    for (int pim = kPimStart; pim < kPimStart + kNumPims; pim++) {
      fpgaDevice_->write(
          getFpgaPimOffset(pim) + kFacebookFpgaPimTypeReg,
          kPimToPimType.at(pim) ==
                  MultiPimPlatformPimContainer::PimType::MINIPACK_16Q
              ? kMinipack16QPimVal
              : kMinipack16OPimVal);
    }
  }

  uint32_t getFpgaPimOffset(int pim) const {
    static constexpr uint32_t kFacebookFpgaPimBase = 0x40000;
    static constexpr uint32_t kFacebookFpgaPimSize = 0x8000;
    // To avoid ambiguity, we explicitly decided the pim number starts from 2.
    return kFacebookFpgaPimBase + kFacebookFpgaPimSize * (pim - 2);
  }

  uint32_t getLedAddress(int pim, int led) const override {
    auto pimOffset = kPimOffset + kPimStride * (pim - 2);
    return pimOffset + kLedRegisterOffset + kLedRegisterStride * led;
  }
};

TEST_F(MinipackSystemContainerTests, TestLeds) {
  testPimLeds(
      systemContainer_.get(),
      kNumPims,
      kPimStart,
      kPimToNumLeds,
      MinipackLed::Color::BLUE,
      MinipackLed::Color::OFF);
}

TEST_F(MinipackSystemContainerTests, TestPimTypes) {
  testPimTypes(systemContainer_.get(), kPimToPimType);
}

TEST_F(MinipackSystemContainerTests, TestPimTypeErrorOnUnreadablePim) {
  constexpr auto kFailingPim = 5;
  fpgaDevice_->write(getFpgaPimOffset(kFailingPim), 0xFFFFFFFF);

  try {
    systemContainer_->getPimType(kFailingPim);
    FAIL() << "Expected PimTypeError";
  } catch (const PimTypeError& e) {
    EXPECT_EQ(e.getPim(), kFailingPim);
    EXPECT_EQ(e.getPimTypeReg(), 0xFF000000);
    EXPECT_EQ(e.getReason(), PimTypeError::Reason::UNREADABLE);
  }
}

TEST_F(MinipackSystemContainerTests, TestPimTypeErrorOnZeroPimTypeReg) {
  constexpr auto kFailingPim = 5;
  fpgaDevice_->write(getFpgaPimOffset(kFailingPim), 0);

  try {
    systemContainer_->getPimType(kFailingPim);
    FAIL() << "Expected PimTypeError";
  } catch (const PimTypeError& e) {
    EXPECT_EQ(e.getPim(), kFailingPim);
    EXPECT_EQ(e.getReason(), PimTypeError::Reason::UNREADABLE);
  }
}
} // namespace facebook::fboss
