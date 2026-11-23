// Copyright 2004-present Facebook. All Rights Reserved.

#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>
#include "fboss/led_service/LedUtils.h"
#include "fboss/lib/led/LedIO.h"

namespace facebook::fboss {

class LedTests : public ::testing::Test {
 protected:
  void SetUp() override {
    mkdir(blueBasePath_.c_str(), 0777);
    blueFd_ = open(bluePath_.c_str(), O_RDWR | O_CREAT, 0777);
    CHECK(blueFd_);

    mkdir(yellowBasePath_.c_str(), 0777);
    yellowFd_ = open(yellowPath_.c_str(), O_RDWR | O_CREAT, 0777);
    CHECK(yellowFd_);

    const std::string content = "1";
    folly::writeFile(content, blueMaxBrightness_.c_str());
    folly::writeFile(content, yellowMaxBrightness_.c_str());
    // We turn the LEDs on. Expect that during initialization (after
    // LedIO constructor is called) that the LEDs are turned off.
    folly::writeFile(content, bluePath_.c_str());
    folly::writeFile(content, yellowPath_.c_str());
  }

  void TearDown() override {
    close(blueFd_);
    close(yellowFd_);
  }

  char ReadLedFile(int fd) {
    constexpr auto len = 1;
    char readBuf;

    // Reset to the beginning of the file before reading it
    int retVal = ::lseek(fd, 0, SEEK_SET);
    EXPECT_EQ(retVal, 0);
    retVal = ::read(fd, &readBuf, len);
    EXPECT_EQ(retVal, len);

    return readBuf;
  }

  // Verify that both the blue and yellow LED files contain a single 0 and the
  // current color is Off
  void VerifyLedOff() {
    EXPECT_EQ('0', ReadLedFile(blueFd_));
    EXPECT_EQ('0', ReadLedFile(yellowFd_));
    EXPECT_EQ(led::LedColor::OFF, led_->getLedState().ledColor().value());
    VerifyYellowBlink("");
    VerifyBlueBlink("");
  }

  // Verify that the blue file contains a single 1, the yellow file
  // contains a single 0, and the current color is Blue
  void VerifyBlueOn(const std::string& blink) {
    EXPECT_EQ('1', ReadLedFile(blueFd_));
    EXPECT_EQ('0', ReadLedFile(yellowFd_));
    EXPECT_EQ(led::LedColor::BLUE, led_->getLedState().ledColor().value());
    VerifyYellowBlink("");
    VerifyBlueBlink(blink);
  }

  // Verify that the yellow file contains a single 1, the blue file
  // contains a single 0, and the current color is Yellow
  void VerifyYellowOn(const std::string& blink) {
    EXPECT_EQ('0', ReadLedFile(blueFd_));
    EXPECT_EQ('1', ReadLedFile(yellowFd_));
    EXPECT_EQ(led::LedColor::YELLOW, led_->getLedState().ledColor().value());
    VerifyYellowBlink(blink);
    VerifyBlueBlink("");
  }

  void VerifyYellowBlink(const std::string& ledBlinkSpeed) {
    std::string yellow;
    folly::readFile(yellowTrigger_.c_str(), yellow);
    if (ledBlinkSpeed.empty()) {
      EXPECT_EQ("none", yellow);
    } else {
      EXPECT_EQ("timer", yellow);
      folly::readFile(yellowDelayOn_.c_str(), yellow);
      EXPECT_EQ(ledBlinkSpeed, yellow);
    }
  }

  void VerifyBlueBlink(const std::string& ledBlinkSpeed) {
    std::string blue;
    folly::readFile(blueTrigger_.c_str(), blue);
    if (ledBlinkSpeed.empty()) {
      EXPECT_EQ("none", blue);
    } else {
      EXPECT_EQ("timer", blue);
      folly::readFile(blueDelayOn_.c_str(), blue);
      EXPECT_EQ(ledBlinkSpeed, blue);
    }
  }

  folly::test::TemporaryDirectory tmpDir_;
  std::string blueBasePath_ = tmpDir_.path().string() + "/invalidBlueLed";
  std::string yellowBasePath_ = tmpDir_.path().string() + "/invalidYellowLed";
  std::string bluePath_ = blueBasePath_ + "/brightness";
  std::string yellowPath_ = yellowBasePath_ + "/brightness";
  std::string blueMaxBrightness_ = blueBasePath_ + "/max_brightness";
  std::string yellowMaxBrightness_ = yellowBasePath_ + "/max_brightness";
  std::string blueDelayOn_ = blueBasePath_ + "/delay_on";
  std::string yellowDelayOn_ = yellowBasePath_ + "/delay_on";
  std::string blueDelayOff_ = blueBasePath_ + "/delay_off";
  std::string yellowDelayOff_ = yellowBasePath_ + "/delay_off";
  std::string blueTrigger_ = blueBasePath_ + "/trigger";
  std::string yellowTrigger_ = yellowBasePath_ + "/trigger";
  int blueFd_;
  int yellowFd_;
  std::unique_ptr<LedIO> led_;
};

TEST_F(LedTests, TestInvalidLedPath) {
  LedMapping ledMapping;
  ledMapping.id() = 0;
  EXPECT_THROW(
      std::unique_ptr<LedIO> led = std::make_unique<LedIO>(ledMapping),
      LedIOError);
}

TEST_F(LedTests, testFakeLed) {
  LedMapping ledMapping;
  ledMapping.id() = 0;
  ledMapping.bluePath() = blueBasePath_;
  ledMapping.yellowPath() = yellowBasePath_;
  // Instantiating Led object will write 0 to both blue and yellow LED
  // files and set current color to Off
  led_ = std::make_unique<LedIO>(ledMapping);

  // Expect LEDs to be off after initialization
  VerifyLedOff();

  // Since current color is Off, setting it to Off again will be noop
  led_->setLedState(
      utility::constructLedState(led::LedColor::OFF, led::Blink::OFF));
  VerifyLedOff();

  // Change current color from Off to Blue
  led_->setLedState(
      utility::constructLedState(led::LedColor::BLUE, led::Blink::OFF));
  VerifyBlueOn("");

  // Since current color is Blue, setting it to Blue again will be noop
  led_->setLedState(
      utility::constructLedState(led::LedColor::BLUE, led::Blink::OFF));
  VerifyBlueOn("");

  // Change current color from Blue to Off
  led_->setLedState(
      utility::constructLedState(led::LedColor::OFF, led::Blink::OFF));
  VerifyLedOff();

  // Change current color from Off to Yellow
  led_->setLedState(
      utility::constructLedState(led::LedColor::YELLOW, led::Blink::OFF));
  VerifyYellowOn("");

  // Since current color is Yellow, setting it to Yellow again will be noop
  led_->setLedState(
      utility::constructLedState(led::LedColor::YELLOW, led::Blink::OFF));
  VerifyYellowOn("");
}

TEST_F(LedTests, TestBlink) {
  LedMapping ledMapping;
  ledMapping.id() = 0;
  ledMapping.bluePath() = blueBasePath_;
  ledMapping.yellowPath() = yellowBasePath_;
  // Instantiating Led object will write 0 to both blue and yellow LED
  // files and set current color to Off

  led_ = std::make_unique<LedIO>(ledMapping);
  led_->setLedState(
      utility::constructLedState(led::LedColor::YELLOW, led::Blink::SLOW));
  VerifyYellowOn(kLedBlinkSlow);

  led_->setLedState(
      utility::constructLedState(led::LedColor::BLUE, led::Blink::SLOW));
  VerifyBlueOn(kLedBlinkSlow);

  led_->setLedState(
      utility::constructLedState(led::LedColor::YELLOW, led::Blink::FAST));
  VerifyYellowOn(kLedBlinkFast);

  led_->setLedState(
      utility::constructLedState(led::LedColor::BLUE, led::Blink::FAST));
  VerifyBlueOn(kLedBlinkFast);
}

} // namespace facebook::fboss
