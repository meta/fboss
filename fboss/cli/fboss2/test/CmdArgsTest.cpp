// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <fboss/cli/fboss2/utils/CmdUtils.h>
#include <fboss/cli/fboss2/utils/LoopbackUtils.h>
#include <fboss/cli/fboss2/utils/PrbsUtils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace ::testing;

namespace facebook::fboss {
namespace {

std::vector<std::string> getPortList() {
  return {"eth1/1/1", "eth1/20/3", "eth1/32/4"};
}

std::vector<std::string> getPrbsComponent() {
  return {
      "asic",
      "xphy_system",
      "xphy_line",
      "transceiver_system",
      "transceiver_line"};
}

utils::PortList doTypingImplicitPortList() {
  return getPortList();
}

utils::PrbsComponent doTypingImplicitPrbsComponent() {
  return getPrbsComponent();
}

std::tuple<utils::PortList, utils::PrbsComponent> doTypingImplicitTuple() {
  return std::make_tuple(getPortList(), getPrbsComponent());
}

template <typename TypedArgs, typename UnTypedArgs>
TypedArgs doTyping(UnTypedArgs& unTypedArgs) {
  return unTypedArgs;
}

} // namespace

TEST(CmdArgsTest, doTypingImplicitSingle) {
  auto typedPortList = doTypingImplicitPortList();
  auto typedPrbsComponent = doTypingImplicitPrbsComponent();
  EXPECT_EQ(typeid(typedPortList), typeid(utils::PortList));
  EXPECT_EQ(typeid(typedPrbsComponent), typeid(utils::PrbsComponent));
  static_assert(std::is_same_v<utils::PortList, decltype(typedPortList)>);
  static_assert(
      std::is_same_v<utils::PrbsComponent, decltype(typedPrbsComponent)>);
}

TEST(CmdArgsTest, doTypingImplicitTuple) {
  auto typedArgs = doTypingImplicitTuple();
  static_assert(std::is_same_v<
                utils::PortList,
                std::tuple_element_t<0, decltype(typedArgs)>>);
  static_assert(std::is_same_v<
                utils::PrbsComponent,
                std::tuple_element_t<1, decltype(typedArgs)>>);
}

TEST(CmdArgsTest, doTypingTemplating) {
  using TypedArgsTest = std::tuple<utils::PortList, utils::PrbsComponent>;
  auto untypedArgs = std::make_tuple(getPortList(), getPrbsComponent());
  auto typedArgs = doTyping<TypedArgsTest>(untypedArgs);
  static_assert(std::is_same_v<
                utils::PortList,
                std::tuple_element_t<0, decltype(typedArgs)>>);
  static_assert(std::is_same_v<
                utils::PrbsComponent,
                std::tuple_element_t<1, decltype(typedArgs)>>);
}

TEST(CmdArgsTest, PortList) {
  // PortList accepts any non-empty string. Validation against actual
  // ports/aggregate ports happens later in the command implementation.

  // test valid arguments
  ASSERT_NO_THROW(utils::PortList({"eth1/5/1"}));
  ASSERT_NO_THROW(utils::PortList({"eth1/5/1", "eth1/5/2"}));
  ASSERT_NO_THROW(utils::PortList({"Port-Channel1"}));
  ASSERT_NO_THROW(utils::PortList({"eth1/5/1", "Port-Channel1"}));

  // test port data
  auto twoPorts = utils::PortList({"eth1/5/1", "eth1/5/2"});
  EXPECT_THAT(twoPorts.data(), ElementsAre("eth1/5/1", "eth1/5/2"));

  auto duplicatePorts = utils::PortList({"eth1/5/1", "eth1/5/1"});
  EXPECT_THAT(duplicatePorts.data(), ElementsAre("eth1/5/1"));

  auto multiPorts = utils::PortList({"eth1/5/1", "eth1/5/9", "eth1/5/1"});
  EXPECT_THAT(multiPorts.data(), ElementsAre("eth1/5/1", "eth1/5/9"));

  // test aggregate port
  auto aggPort = utils::PortList({"Port-Channel1"});
  EXPECT_THAT(aggPort.data(), ElementsAre("Port-Channel1"));

  // test mixed ports and aggregate ports
  auto mixedPorts = utils::PortList({"eth1/5/1", "Port-Channel1", "eth1/5/2"});
  EXPECT_THAT(
      mixedPorts.data(), ElementsAre("Port-Channel1", "eth1/5/1", "eth1/5/2"));

  // test invalid arguments - empty string should throw
  ASSERT_THROW(utils::PortList({""}), std::invalid_argument);
}

TEST(CmdArgsTest, FsdbPath) {
  // test valid arguments
  EXPECT_NO_THROW(utils::FsdbPath({"/agent/config"}));

  // test parsed results
  auto fsdbPath = utils::FsdbPath({"/agent/config"});
  EXPECT_THAT(fsdbPath.data(), ElementsAre("agent", "config"));

  fsdbPath = utils::FsdbPath({"/agent/switchState/portMap/eth2\\/1\\/1"});
  EXPECT_THAT(
      fsdbPath.data(),
      ElementsAre("agent", "switchState", "portMap", "eth2/1/1"));

  // empty is no throw
  EXPECT_NO_THROW(utils::FsdbPath({}));

  // multiple items is a throw
  EXPECT_THROW(utils::FsdbPath({"invalid", "args"}), std::runtime_error);
}

TEST(CmdArgsTest, HwObjectList) {
  // test valid arguments
  ASSERT_NO_THROW(utils::HwObjectList({"PORT"}));
  EXPECT_THAT(
      utils::HwObjectList({"PORT"}).data(), ElementsAre(HwObjectType::PORT));
  ASSERT_NO_THROW(utils::HwObjectList({"NEXT_HOP_GROUP_MEMBER"}));
  EXPECT_THAT(
      utils::HwObjectList({"NEXT_HOP_GROUP_MEMBER"}).data(),
      ElementsAre(HwObjectType::NEXT_HOP_GROUP_MEMBER));
  ASSERT_NO_THROW(utils::HwObjectList({"LAG", "MIRROR"}));
  EXPECT_THAT(
      utils::HwObjectList({"LAG", "MIRROR"}).data(),
      ElementsAre(HwObjectType::LAG, HwObjectType::MIRROR));
  ASSERT_NO_THROW(utils::HwObjectList({"SAMPLE_PACKET"}));
  EXPECT_THAT(
      utils::HwObjectList({"SAMPLE_PACKET"}).data(),
      ElementsAre(HwObjectType::SAMPLE_PACKET));
  ASSERT_NO_THROW(utils::HwObjectList({"MY_MAC"}));
  EXPECT_THAT(
      utils::HwObjectList({"MY_MAC"}).data(),
      ElementsAre(HwObjectType::MY_MAC));

  // test invalid arguments
  ASSERT_THROW(utils::HwObjectList({"M"}), std::out_of_range);
  ASSERT_THROW(utils::HwObjectList({""}), std::out_of_range);
  ASSERT_THROW(
      utils::HwObjectList({"QUEUE", "VLAN", "AAA"}), std::out_of_range);
}

TEST(CmdArgsTest, CIDRNetwork) {
  auto args = utils::CIDRNetwork({"2401:db00:e01e:2105::24/110", "::/0"});
  EXPECT_THAT(
      args.data(),
      ElementsAre(
          folly::CIDRNetwork("2401:db00:e01e:2105::", 110),
          folly::CIDRNetwork("::", 0)));
}

TEST(CmdArgsTest, LoopbackComponentActionComponents) {
  using loopback_utils::LoopbackComponentAction;

  // The component vocabulary is shared with `prbs <component>`.
  EXPECT_EQ(
      LoopbackComponentAction({"asic", "enable"}).component(),
      phy::PortComponent::ASIC);
  EXPECT_EQ(
      LoopbackComponentAction({"xphy_system", "enable"}).component(),
      phy::PortComponent::GB_SYSTEM);
  EXPECT_EQ(
      LoopbackComponentAction({"xphy_line", "enable"}).component(),
      phy::PortComponent::GB_LINE);
  EXPECT_EQ(
      LoopbackComponentAction({"transceiver_system", "enable"}).component(),
      phy::PortComponent::TRANSCEIVER_SYSTEM);
  EXPECT_EQ(
      LoopbackComponentAction({"transceiver_line", "enable"}).component(),
      phy::PortComponent::TRANSCEIVER_LINE);
}

TEST(CmdArgsTest, LoopbackComponentActionEnableDisable) {
  using loopback_utils::LoopbackComponentAction;

  EXPECT_TRUE(LoopbackComponentAction({"xphy_line", "enable"}).enable());
  EXPECT_FALSE(LoopbackComponentAction({"xphy_line", "disable"}).enable());

  // Both tokens are case insensitive, and componentName() is normalized.
  auto mixedCase = LoopbackComponentAction({"XPHY_Line", "ENABLE"});
  EXPECT_EQ(mixedCase.component(), phy::PortComponent::GB_LINE);
  EXPECT_TRUE(mixedCase.enable());
  EXPECT_EQ(mixedCase.componentName(), "xphy_line");
}

TEST(CmdArgsTest, LoopbackComponentActionDirection) {
  using loopback_utils::LoopbackComponentAction;

  EXPECT_EQ(
      LoopbackComponentAction({"transceiver_line", "enable"}).direction(),
      phy::LoopbackMode::INPUT);
  auto output =
      LoopbackComponentAction({"transceiver_line", "OUTPUT", "enable"});
  EXPECT_EQ(output.direction(), phy::LoopbackMode::OUTPUT);
  EXPECT_EQ(output.component(), phy::PortComponent::TRANSCEIVER_LINE);
  EXPECT_TRUE(output.enable());
  EXPECT_EQ(
      LoopbackComponentAction({"transceiver_system", "input", "disable"})
          .direction(),
      phy::LoopbackMode::INPUT);
  EXPECT_THROW(
      LoopbackComponentAction({"transceiver_line", "sideways", "enable"}),
      std::exception);
}

TEST(CmdArgsTest, LoopbackActionDirection) {
  using loopback_utils::LoopbackAction;

  auto defaulted = LoopbackAction({"system", "enable"});
  EXPECT_EQ(defaulted.mode(), "system");
  EXPECT_EQ(defaulted.direction(), phy::LoopbackMode::INPUT);
  EXPECT_TRUE(defaulted.enable());

  auto output = LoopbackAction({"line", "output", "disable"});
  EXPECT_EQ(output.mode(), "line");
  EXPECT_EQ(output.direction(), phy::LoopbackMode::OUTPUT);
  EXPECT_FALSE(output.enable());

  EXPECT_TRUE(LoopbackAction({"disable"}).isDisableAll());
  EXPECT_THROW(LoopbackAction({"line", "sideways", "enable"}), std::exception);
  EXPECT_THROW(
      LoopbackAction({"line", "output", "enable", "extra"}), std::exception);
}

TEST(CmdArgsTest, LoopbackComponentActionInvalid) {
  using loopback_utils::LoopbackComponentAction;

  // Wrong number of tokens.
  EXPECT_THROW(
      LoopbackComponentAction(std::vector<std::string>{}), std::exception);
  EXPECT_THROW(LoopbackComponentAction({"xphy_line"}), std::exception);
  EXPECT_THROW(
      LoopbackComponentAction({"xphy_line", "enable", "extra"}),
      std::exception);

  // Unknown component, including the pre-migration syntax which used a bare
  // side name as the first token.
  EXPECT_THROW(LoopbackComponentAction({"xphy", "enable"}), std::exception);
  EXPECT_THROW(LoopbackComponentAction({"line", "enable"}), std::exception);
  EXPECT_THROW(LoopbackComponentAction({"", "enable"}), std::exception);

  // Unknown action.
  EXPECT_THROW(LoopbackComponentAction({"xphy_line", "on"}), std::exception);
  EXPECT_THROW(LoopbackComponentAction({"xphy_line", "none"}), std::exception);
}

TEST(CmdArgsTest, PrbsComponentRequiredForWrites) {
  EXPECT_THROW(
      utils::PrbsComponent(std::vector<std::string>{}), std::runtime_error);
  EXPECT_THROW(
      prbsComponents({}, false /* returnAllIfEmpty */), std::runtime_error);
}

TEST(CmdArgsTest, PrbsComponentDefaultsToAllForShow) {
  const std::vector<phy::PortComponent> expected = {
      phy::PortComponent::ASIC,
      phy::PortComponent::GB_SYSTEM,
      phy::PortComponent::GB_LINE,
      phy::PortComponent::TRANSCEIVER_SYSTEM,
      phy::PortComponent::TRANSCEIVER_LINE};
  EXPECT_EQ(prbsComponents({}, true /* returnAllIfEmpty */), expected);
}

} // namespace facebook::fboss
