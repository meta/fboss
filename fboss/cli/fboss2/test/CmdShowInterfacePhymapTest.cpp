// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/reflection/testing.h>
#include <sstream>

#include "fboss/cli/fboss2/commands/show/interface/phymap/CmdShowInterfacePhymap.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {

mka::MacsecPortPhyMap makePortsPhyMap() {
  mka::MacsecPortPhyInfo phyInfo;
  phyInfo.slotId() = 1;
  phyInfo.mdioId() = 2;
  phyInfo.phyId() = 3;
  phyInfo.saiSwitchId() = 4;
  phyInfo.portName() = "eth1/1/1";

  mka::MacsecPortPhyMap portsPhyMap;
  portsPhyMap.macsecPortPhyMap() = {{1, phyInfo}};
  return portsPhyMap;
}

} // namespace

class CmdShowInterfacePhymapTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowInterfacePhymapTestFixture, noXphyPlatform) {
  setupMockedAgentServer();
  EXPECT_CALL(getQsfpService(), getMacsecCapablePorts(_))
      .WillOnce([](auto& ports) { ports.clear(); });
  EXPECT_CALL(getQsfpService(), macsecGetPhyPortInfo(_, _)).Times(0);

  auto model = CmdShowInterfacePhymap().queryClient(localhost(), {});
  EXPECT_TRUE(model.portsPhyMap()->macsecPortPhyMap()->empty());

  std::stringstream output;
  CmdShowInterfacePhymap().printOutput(model, output);
  EXPECT_EQ("No Phy port map for this platform\n", output.str());
}

TEST_F(CmdShowInterfacePhymapTestFixture, xphyPlatformQueriesPhyMap) {
  setupMockedAgentServer();
  const std::vector<std::string> queriedIfs = {"eth1/1/1"};
  const auto expectedPortsPhyMap = makePortsPhyMap();

  EXPECT_CALL(getQsfpService(), getMacsecCapablePorts(_))
      .WillOnce([](auto& ports) { ports = {1}; });
  EXPECT_CALL(
      getQsfpService(),
      macsecGetPhyPortInfo(_, Pointee(ElementsAre("eth1/1/1"))))
      .WillOnce([&expectedPortsPhyMap](auto& portsPhyMap, auto&&) {
        portsPhyMap = expectedPortsPhyMap;
      });

  auto model = CmdShowInterfacePhymap().queryClient(localhost(), queriedIfs);

  EXPECT_THRIFT_EQ(expectedPortsPhyMap, model.portsPhyMap().value());
}

} // namespace facebook::fboss
