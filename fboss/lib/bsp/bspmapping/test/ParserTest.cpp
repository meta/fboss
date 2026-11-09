// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/bspmapping/Parser.h"
#include <folly/Range.h>
#include <gtest/gtest.h>

using namespace ::testing;

TEST(ParserTest, GetTransceiverConfigRowFromCsvLine) {
  // First line taken from Montblanc_BspMapping.csv
  std::string line =
      "1,1 2 3 4,1,1,CPLD,/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_reset_1,1,1,/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_present_1,1,0,1,I2C,/run/devmap/xcvrs/xcvr_io_1,1,/sys/class/leds/port1_led1:blue:status,/sys/class/leds/port1_led1:yellow:status";
  auto transceiverConfigRow =
      facebook::fboss::Parser::getTransceiverConfigRowFromCsvLine(line);
  EXPECT_EQ(transceiverConfigRow.tcvrId().value(), 1);
  EXPECT_TRUE(
      apache::thrift::get_pointer(transceiverConfigRow.tcvrLaneIdList()) !=
      nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(transceiverConfigRow.tcvrLaneIdList().value()),
      std::vector<int>({1, 2, 3, 4}));
  EXPECT_EQ(transceiverConfigRow.pimId().value(), 1);
  EXPECT_EQ(transceiverConfigRow.accessCtrlId().value(), "1");
  EXPECT_EQ(
      transceiverConfigRow.accessCtrlType().value(),
      facebook::fboss::ResetAndPresenceAccessType::CPLD);
  EXPECT_EQ(
      transceiverConfigRow.resetPath().value(),
      "/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_reset_1");
  EXPECT_EQ(transceiverConfigRow.resetMask().value(), 1);
  EXPECT_EQ(transceiverConfigRow.resetHoldHi().value(), 1);
  EXPECT_EQ(
      transceiverConfigRow.presentPath().value(),
      "/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_present_1");
  EXPECT_EQ(transceiverConfigRow.presentMask().value(), 1);
  EXPECT_EQ(transceiverConfigRow.presentHoldHi().value(), 0);
  EXPECT_EQ(transceiverConfigRow.ioCtrlId().value(), "1");
  EXPECT_EQ(
      transceiverConfigRow.ioCtrlType().value(),
      facebook::fboss::TransceiverIOType::I2C);
  EXPECT_EQ(
      transceiverConfigRow.ioPath().value(), "/run/devmap/xcvrs/xcvr_io_1");
  EXPECT_TRUE(
      apache::thrift::get_pointer(transceiverConfigRow.ledId()) != nullptr);
  EXPECT_EQ(apache::thrift::can_throw(transceiverConfigRow.ledId().value()), 1);
  EXPECT_TRUE(
      apache::thrift::get_pointer(transceiverConfigRow.ledBluePath()) !=
      nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(transceiverConfigRow.ledBluePath().value()),
      "/sys/class/leds/port1_led1:blue:status");
  EXPECT_TRUE(
      apache::thrift::get_pointer(transceiverConfigRow.ledYellowPath()) !=
      nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(transceiverConfigRow.ledYellowPath().value()),
      "/sys/class/leds/port1_led1:yellow:status");
}

TEST(ParserTest, GetTransceiverConfigRowFromCsvLineNullCorrect) {
  // First line taken from Meru400bfu_BspMapping.csv
  std::string line =
      "1,,1,accessController-1,CPLD,/sys/bus/i2c/devices/2-0032/cpld_qsfpdd_port_config_0,1,0,/sys/bus/i2c/devices/2-0032/cpld_qsfpdd_port_status_0,2,0,ioController-1,I2C,/dev/i2c-21,,,,";
  auto transceiverConfigRow =
      facebook::fboss::Parser::getTransceiverConfigRowFromCsvLine(line);
  EXPECT_EQ(transceiverConfigRow.tcvrId().value(), 1);
  EXPECT_TRUE(
      apache::thrift::get_pointer(transceiverConfigRow.tcvrLaneIdList()) ==
      nullptr);
  EXPECT_EQ(transceiverConfigRow.pimId().value(), 1);
  EXPECT_EQ(transceiverConfigRow.accessCtrlId().value(), "accessController-1");
  EXPECT_EQ(
      transceiverConfigRow.accessCtrlType().value(),
      facebook::fboss::ResetAndPresenceAccessType::CPLD);
  EXPECT_EQ(
      transceiverConfigRow.resetPath().value(),
      "/sys/bus/i2c/devices/2-0032/cpld_qsfpdd_port_config_0");
  EXPECT_EQ(transceiverConfigRow.resetMask().value(), 1);
  EXPECT_EQ(transceiverConfigRow.resetHoldHi().value(), 0);
  EXPECT_EQ(
      transceiverConfigRow.presentPath().value(),
      "/sys/bus/i2c/devices/2-0032/cpld_qsfpdd_port_status_0");
  EXPECT_EQ(transceiverConfigRow.presentMask().value(), 2);
  EXPECT_EQ(transceiverConfigRow.presentHoldHi().value(), 0);
  EXPECT_EQ(transceiverConfigRow.ioCtrlId().value(), "ioController-1");
  EXPECT_EQ(
      transceiverConfigRow.ioCtrlType().value(),
      facebook::fboss::TransceiverIOType::I2C);
  EXPECT_EQ(transceiverConfigRow.ioPath().value(), "/dev/i2c-21");
  EXPECT_TRUE(
      apache::thrift::get_pointer(transceiverConfigRow.ledId()) == nullptr);
  EXPECT_TRUE(
      apache::thrift::get_pointer(transceiverConfigRow.ledBluePath()) ==
      nullptr);
  EXPECT_TRUE(
      apache::thrift::get_pointer(transceiverConfigRow.ledYellowPath()) ==
      nullptr);
}

TEST(ParserTest, GetTransceiverConfigRowFromCsvLineThrowsOnMalformedLine) {
  // First line taken from Meru400bfu_BspMapping.csv, but it's missing 3 fields
  // for LED attributes.
  std::string line =
      "1,,1,accessController-1,CPLD,/sys/bus/i2c/devices/2-0032/cpld_qsfpdd_port_config_0,1,0,/sys/bus/i2c/devices/2-0032/cpld_qsfpdd_port_status_0,2,0,ioController-1,I2C,/dev/i2c-21";
  EXPECT_THROW(
      facebook::fboss::Parser::getTransceiverConfigRowFromCsvLine(line),
      std::runtime_error);
}

TEST(ParserTest, GetTransceiverConfigRowsFromCsvTest) {
  auto transceivers = facebook::fboss::Parser::getTransceiverConfigRowsFromCsv(
      folly::StringPiece(
          "fboss/lib/bsp/bspmapping/test/test_data/test_example.csv"));
  EXPECT_EQ(transceivers.size(), 2);
}

TEST(ParserTest, GetBspPlatformMappingFromCsvTest) {
  auto bspPlatformMapping =
      facebook::fboss::Parser::getBspPlatformMappingFromCsv(
          folly::StringPiece(
              "fboss/lib/bsp/bspmapping/test/test_data/test_example.csv"));
  EXPECT_EQ(bspPlatformMapping.pimMapping().value().size(), 1);
  EXPECT_TRUE(
      bspPlatformMapping.pimMapping().value().find(1) !=
      bspPlatformMapping.pimMapping().value().end());
  EXPECT_EQ(bspPlatformMapping.pimMapping().value().at(1).pimID().value(), 1);
  auto tcvrMapping =
      bspPlatformMapping.pimMapping().value().at(1).tcvrMapping().value();
  EXPECT_EQ(tcvrMapping.size(), 1);
  EXPECT_TRUE(tcvrMapping.find(1) != tcvrMapping.end());
  EXPECT_EQ(tcvrMapping.at(1).tcvrId().value(), 1);
  EXPECT_EQ(
      tcvrMapping.at(1).accessControl().value().controllerId().value(), "1");
  EXPECT_EQ(
      tcvrMapping.at(1).accessControl().value().type().value(),
      facebook::fboss::ResetAndPresenceAccessType::CPLD);

  EXPECT_TRUE(
      apache::thrift::get_pointer(tcvrMapping.at(1)
                                      .accessControl()
                                      .value()
                                      .reset()
                                      .value()
                                      .sysfsPath()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(tcvrMapping.at(1)
                                    .accessControl()
                                    .value()
                                    .reset()
                                    .value()
                                    .sysfsPath()
                                    .value()),
      "/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_reset_1");
  EXPECT_TRUE(
      apache::thrift::get_pointer(
          tcvrMapping.at(1).accessControl().value().reset().value().mask()) !=
      nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(tcvrMapping.at(1)
                                    .accessControl()
                                    .value()
                                    .reset()
                                    .value()
                                    .mask()
                                    .value()),
      1);
  EXPECT_TRUE(
      apache::thrift::get_pointer(tcvrMapping.at(1)
                                      .accessControl()
                                      .value()
                                      .reset()
                                      .value()
                                      .gpioOffset()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(tcvrMapping.at(1)
                                    .accessControl()
                                    .value()
                                    .reset()
                                    .value()
                                    .gpioOffset()
                                    .value()),
      0);
  EXPECT_TRUE(
      apache::thrift::get_pointer(tcvrMapping.at(1)
                                      .accessControl()
                                      .value()
                                      .reset()
                                      .value()
                                      .resetHoldHi()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(tcvrMapping.at(1)
                                    .accessControl()
                                    .value()
                                    .reset()
                                    .value()
                                    .resetHoldHi()
                                    .value()),
      1);

  EXPECT_TRUE(
      apache::thrift::get_pointer(tcvrMapping.at(1)
                                      .accessControl()
                                      .value()
                                      .presence()
                                      .value()
                                      .sysfsPath()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(tcvrMapping.at(1)
                                    .accessControl()
                                    .value()
                                    .presence()
                                    .value()
                                    .sysfsPath()
                                    .value()),
      "/run/devmap/cplds/JANGA_SMB_CPLD/xcvr_present_1");
  EXPECT_TRUE(
      apache::thrift::get_pointer(tcvrMapping.at(1)
                                      .accessControl()
                                      .value()
                                      .presence()
                                      .value()
                                      .mask()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(tcvrMapping.at(1)
                                    .accessControl()
                                    .value()
                                    .presence()
                                    .value()
                                    .mask()
                                    .value()),
      1);
  EXPECT_TRUE(
      apache::thrift::get_pointer(tcvrMapping.at(1)
                                      .accessControl()
                                      .value()
                                      .presence()
                                      .value()
                                      .gpioOffset()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(tcvrMapping.at(1)
                                    .accessControl()
                                    .value()
                                    .presence()
                                    .value()
                                    .gpioOffset()
                                    .value()),
      0);
  EXPECT_TRUE(
      apache::thrift::get_pointer(tcvrMapping.at(1)
                                      .accessControl()
                                      .value()
                                      .presence()
                                      .value()
                                      .presentHoldHi()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(tcvrMapping.at(1)
                                    .accessControl()
                                    .value()
                                    .presence()
                                    .value()
                                    .presentHoldHi()
                                    .value()),
      1);

  EXPECT_TRUE(
      apache::thrift::get_pointer(
          tcvrMapping.at(1).accessControl().value().gpioChip()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(
          tcvrMapping.at(1).accessControl().value().gpioChip().value()),
      "");

  EXPECT_EQ(tcvrMapping.at(1).io().value().controllerId().value(), "1");
  EXPECT_EQ(
      tcvrMapping.at(1).io().value().type().value(),
      facebook::fboss::TransceiverIOType::I2C);
  EXPECT_EQ(
      tcvrMapping.at(1).io().value().devicePath().value(),
      "/run/devmap/xcvrs/xcvr_io_1");

  EXPECT_EQ(tcvrMapping.at(1).tcvrLaneToLedId().value().size(), 4);
  std::map<int, int> expectedLaneToLedId = {{1, 1}, {2, 1}, {3, 1}, {4, 1}};
  EXPECT_EQ(tcvrMapping.at(1).tcvrLaneToLedId().value(), expectedLaneToLedId);

  EXPECT_TRUE(bspPlatformMapping.pimMapping()
                  .value()
                  .at(1)
                  .phyMapping()
                  .value()
                  .empty());
  EXPECT_TRUE(bspPlatformMapping.pimMapping()
                  .value()
                  .at(1)
                  .phyIOControllers()
                  .value()
                  .empty());

  EXPECT_EQ(
      bspPlatformMapping.pimMapping().value().at(1).ledMapping().value().size(),
      1);
  auto ledMapping =
      bspPlatformMapping.pimMapping().value().at(1).ledMapping().value().at(1);

  EXPECT_EQ(ledMapping.id().value(), 1);
  EXPECT_TRUE(apache::thrift::get_pointer(ledMapping.bluePath()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(ledMapping.bluePath().value()),
      "/sys/class/leds/port1_led1:blue:status");
  EXPECT_TRUE(apache::thrift::get_pointer(ledMapping.yellowPath()) != nullptr);
  EXPECT_EQ(
      apache::thrift::can_throw(ledMapping.yellowPath().value()),
      "/sys/class/leds/port1_led1:yellow:status");
  EXPECT_EQ(ledMapping.transceiverId().value(), 1);
}
