// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <folly/IPAddressV4.h>
#include <cstdint>

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include "fboss/agent/AddressUtil.h"

#include "fboss/cli/fboss2/commands/show/ndp/CmdShowNdp.h"
#include "fboss/cli/fboss2/commands/show/ndp/gen-cpp2/model_types.h"

using namespace ::testing;

namespace facebook::fboss {

/*
 * Set up test data
 */
std::vector<facebook::fboss::NdpEntryThrift> createNdpEntries() {
  facebook::fboss::NdpEntryThrift ndpEntry1;
  folly::IPAddressV6 ipv6_1("fe80::526b:4bff:fe28:8fb0");
  network::thrift::BinaryAddress binaryAddr1 =
      facebook::network::toBinaryAddress(ipv6_1);

  ndpEntry1.ip() = binaryAddr1;
  ndpEntry1.mac() = "50:6b:4b:28:8f:b0";
  ndpEntry1.port() = 46;
  ndpEntry1.vlanName() = "downlinks";
  ndpEntry1.vlanID() = 2000;
  ndpEntry1.interfaceID() = 2000;
  ndpEntry1.state() = "REACHABLE";
  ndpEntry1.ttl() = 45013;
  ndpEntry1.classID() = 0;
  ndpEntry1.resolvedSince() = 0;

  fboss::NdpEntryThrift ndpEntry2;
  folly::IPAddressV6 ipv6_2("fe80::464c:a8ff:fee4:1c3f");
  network::thrift::BinaryAddress binaryAddr2 =
      facebook::network::toBinaryAddress(ipv6_2);

  ndpEntry2.ip() = binaryAddr2;
  ndpEntry2.mac() = "44:4c:a8:e4:1c:3f";
  ndpEntry2.port() = 102;
  ndpEntry2.vlanName() = "uplink_1";
  ndpEntry2.vlanID() = 4001;
  ndpEntry2.interfaceID() = 4001;
  ndpEntry2.state() = "REACHABLE";
  ndpEntry2.ttl() = 21045;
  ndpEntry2.classID() = 0;

  std::vector<fboss::NdpEntryThrift> entries{ndpEntry1, ndpEntry2};
  return entries;
}

std::map<int32_t, facebook::fboss::PortInfoThrift> createPortThriftEntries() {
  std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries;

  PortInfoThrift portEntry1;
  portEntry1.portId() = 46;
  portEntry1.name() = "eth1/1/1";

  PortInfoThrift portEntry2;
  portEntry2.portId() = 102;
  portEntry2.name() = "eth2/1/1";

  portEntries[folly::copy(portEntry1.portId().value())] = portEntry1;
  portEntries[folly::copy(portEntry2.portId().value())] = portEntry2;

  return portEntries;
}

class CmdShowNdpTestFixture : public testing::Test {
 public:
  std::vector<fboss::NdpEntryThrift> ndpEntries;
  std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries;
  folly::IPAddressV4 hostIp;

  void SetUp() override {
    ndpEntries = createNdpEntries();
    portEntries = createPortThriftEntries();
    hostIp = folly::IPAddressV4::tryFromString("127.0.0.1").value();
  }
};

TEST_F(CmdShowNdpTestFixture, createModel) {
  auto cmd = CmdShowNdp();
  CmdShowNdpTraits::ObjectArgType queriedEntries;
  auto model = cmd.createModel(ndpEntries, queriedEntries, portEntries, {});
  auto entries = model.ndpEntries().value();

  EXPECT_EQ(entries.size(), 2);

  EXPECT_EQ(entries[0].ip().value(), "fe80::526b:4bff:fe28:8fb0");
  EXPECT_EQ(entries[0].mac().value(), "50:6b:4b:28:8f:b0");
  EXPECT_EQ(entries[0].port().value(), "eth1/1/1");
  EXPECT_EQ(entries[0].vlanName().value(), "downlinks");
  EXPECT_EQ(entries[0].vlanID().value(), 2000);
  EXPECT_EQ(entries[0].state().value(), "REACHABLE");
  EXPECT_EQ(entries[0].ttl().value(), 45013);
  EXPECT_EQ(entries[0].classID().value(), 0);
  EXPECT_EQ(entries[0].resolvedSince().value(), "1969-12-31 16:00:00");

  EXPECT_EQ(entries[1].ip().value(), "fe80::464c:a8ff:fee4:1c3f");
  EXPECT_EQ(entries[1].mac().value(), "44:4c:a8:e4:1c:3f");
  EXPECT_EQ(entries[1].port().value(), "eth2/1/1");
  EXPECT_EQ(entries[1].vlanName().value(), "uplink_1");
  EXPECT_EQ(entries[1].vlanID().value(), 4001);
  EXPECT_EQ(entries[1].state().value(), "REACHABLE");
  EXPECT_EQ(entries[1].ttl().value(), 21045);
  EXPECT_EQ(entries[1].classID().value(), 0);
  EXPECT_EQ(entries[1].resolvedSince().value(), "--");
}

TEST_F(CmdShowNdpTestFixture, printOutput) {
  auto cmd = CmdShowNdp();
  CmdShowNdpTraits::ObjectArgType queriedEntries;
  auto model = cmd.createModel(ndpEntries, queriedEntries, portEntries, {});

  std::stringstream ss;
  cmd.printOutput(model, ss);

  std::string output = ss.str();
  std::string expectOutput =
      " IP Address                 MAC Address        Interface  VLAN/InterfaceID  State      TTL    CLASSID  Voq Switch  Resolved Since      \n"
      "-------------------------------------------------------------------------------------------------------------------------------------------------\n"
      " fe80::526b:4bff:fe28:8fb0  50:6b:4b:28:8f:b0  eth1/1/1   downlinks (2000)  REACHABLE  45013  0        --          1969-12-31 16:00:00 \n"
      " fe80::464c:a8ff:fee4:1c3f  44:4c:a8:e4:1c:3f  eth2/1/1   uplink_1 (4001)   REACHABLE  21045  0        --          --                  \n\n";

  EXPECT_EQ(output, expectOutput);
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowNdpTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowNdpTraits::description().empty());
  EXPECT_FALSE(CmdShowNdp::sampleModel().ndpEntries()->empty());
}

} // namespace facebook::fboss
