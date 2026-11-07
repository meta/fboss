// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

namespace facebook::fboss::utils {

namespace {

std::map<int32_t, PortInfoThrift> makePorts(
    const std::map<int32_t, std::string>& idToName) {
  std::map<int32_t, PortInfoThrift> ports;
  for (const auto& [id, name] : idToName) {
    PortInfoThrift port;
    port.portId() = id;
    port.name() = name;
    ports.emplace(id, std::move(port));
  }
  return ports;
}

std::vector<std::string> names(
    const std::vector<const PortInfoThrift*>& ports) {
  std::vector<std::string> result;
  result.reserve(ports.size());
  for (const auto* port : ports) {
    result.push_back(port->name().value());
  }
  return result;
}

} // namespace

TEST(InterfaceSelectionTest, isQueriedInterface) {
  EXPECT_TRUE(isQueriedInterface("eth1/1/1", {}));
  EXPECT_TRUE(isQueriedInterface("eth1/1/1", {"eth1/2/1", "eth1/1/1"}));
  EXPECT_FALSE(isQueriedInterface("eth1/1/1", {"eth1/2/1"}));
}

TEST(InterfaceSelectionTest, getQueriedPortsSortsInFrontPanelOrder) {
  // Port IDs deliberately disagree with the front panel order, and the
  // unparseable name falls back to its ID after every front panel port.
  const auto ports = makePorts({
      {1, "eth1/10/1"},
      {2, "fab1/1/1"},
      {3, "eth1/2/1"},
      {4, "cpu"},
      {5, "eth1/2/5"},
  });

  EXPECT_EQ(
      names(getQueriedPorts(ports, {})),
      (std::vector<std::string>{
          "eth1/2/1", "eth1/2/5", "eth1/10/1", "fab1/1/1", "cpu"}));
}

TEST(InterfaceSelectionTest, getQueriedPortsFiltersByName) {
  const auto ports = makePorts({
      {1, "eth1/10/1"},
      {2, "eth1/2/1"},
      {3, "eth1/3/1"},
  });

  // Command line order does not matter, and unknown names are ignored.
  EXPECT_EQ(
      names(getQueriedPorts(ports, {"eth1/10/1", "eth9/9/9", "eth1/2/1"})),
      (std::vector<std::string>{"eth1/2/1", "eth1/10/1"}));
}

TEST(InterfaceSelectionTest, getQueriedInterfaceNames) {
  const std::vector<std::string> ifNames{
      "eth1/10/1", "fab1/1/1", "zzz", "eth1/2/1", "aaa"};

  EXPECT_EQ(
      getQueriedInterfaceNames(ifNames, {}),
      (std::vector<std::string>{
          "eth1/2/1", "eth1/10/1", "fab1/1/1", "aaa", "zzz"}));
  EXPECT_EQ(
      getQueriedInterfaceNames(ifNames, {"zzz", "eth1/10/1"}),
      (std::vector<std::string>{"eth1/10/1", "zzz"}));
}

} // namespace facebook::fboss::utils
