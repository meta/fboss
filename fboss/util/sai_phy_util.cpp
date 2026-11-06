// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/init/Init.h>
#include "fboss/qsfp_service/lib/QsfpClient.h"

using namespace facebook::fboss;
using namespace apache::thrift;

DEFINE_bool(phy_port_info, false, "Print PHY port SAI info, use with --port");
DEFINE_string(port, "", "Switch port");
DEFINE_bool(
    set_phy_loopback,
    false,
    "Set PHY port in loopback mode, use with --port --side");
DEFINE_bool(
    clear_phy_loopback,
    false,
    "Clear PHY port loopback mode, use with --port --side");
DEFINE_bool(
    set_admin_up,
    false,
    "Set PHY port Admin Up, use with --port --side");
DEFINE_bool(
    set_admin_down,
    false,
    "Set PHY port Admin Down, use with --port --side");
DEFINE_string(side, "line", "line side or system side");
DEFINE_bool(
    phy_cfg_check,
    false,
    "Check PHY config  in Hardware and report, use with --port");
DEFINE_bool(
    read_reg,
    false,
    "Read PHY register, use with --port --mdio --dev --offset");
DEFINE_bool(
    write_reg,
    false,
    "Write PHY register, use with --port --mdio --dev --offset --val");
DEFINE_int32(mdio, -1, "PHY Mdio address");
DEFINE_int32(dev, -1, "Clause45 Device Id for register access");
DEFINE_int32(offset, -1, "Register offset");
DEFINE_int32(val, -1, "Value to be written");
DEFINE_bool(
    read_serdes_reg,
    false,
    "Read PHY register, use with --port --mdio --side --lane --serdes_offset");
DEFINE_bool(
    write_serdes_reg,
    false,
    "Write PHY register, use with --port --mdio --side --lane --serdes_reg --serdes_val");
DEFINE_int32(lane, -1, "Serdes Lane id");
DEFINE_int64(serdes_reg, -1, "Serdes Register offset");
DEFINE_int64(serdes_val, -1, "Serdes Value to be written");
DEFINE_bool(
    create_port,
    false,
    "Create a PHY port, use with --sw_port --profile");
DEFINE_int32(sw_port, -1, "Software port id");
DEFINE_int32(profile, -1, "Profile Id value");

void printPhyPortInfo(QsfpServiceAsyncClient* qsfpHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }
  std::string output;
  qsfpHandler->sync_getPortInfo(output, FLAGS_port);

  printf("%s\n", output.c_str());
}

void setPhyLoopback(QsfpServiceAsyncClient* qsfpHandler, bool setLoopback) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }
  if (FLAGS_side != "line" && FLAGS_side != "host") {
    printf("side should be either line or host\n");
    return;
  }

  phy::PortComponent component = (FLAGS_side == "line")
      ? phy::PortComponent::GB_LINE
      : phy::PortComponent::GB_SYSTEM;

  qsfpHandler->sync_setPortLoopbackState(
      FLAGS_port, component, setLoopback, phy::LoopbackMode::INPUT);
  printf(
      "Port Loopback %s got %s\n",
      FLAGS_port.c_str(),
      (setLoopback ? "Set" : "Clear"));
}

void setPhyAdminState(QsfpServiceAsyncClient* qsfpHandler, bool adminUp) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }
  if (FLAGS_side != "line" && FLAGS_side != "host") {
    printf("side should be either line or host\n");
    return;
  }

  phy::PortComponent component = (FLAGS_side == "line")
      ? phy::PortComponent::GB_LINE
      : phy::PortComponent::GB_SYSTEM;

  qsfpHandler->sync_setPortAdminState(FLAGS_port, component, adminUp);
  printf(
      "Port %s Admin state set to %s\n",
      FLAGS_port.c_str(),
      (adminUp ? "Up" : "Down"));
}

void saiPhyRegisterAccess(QsfpServiceAsyncClient* qsfpHandler, bool opRead) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }
  if (FLAGS_mdio == -1 || FLAGS_dev == -1 || FLAGS_offset == -1) {
    printf("MDIO address, Device Id and Register offset is required\n");
    return;
  }
  if (!opRead && FLAGS_val == -1) {
    printf("Register value is required\n");
    return;
  }

  std::string output;
  qsfpHandler->sync_saiPhyRegisterAccess(
      output,
      FLAGS_port,
      opRead,
      FLAGS_mdio,
      FLAGS_dev,
      FLAGS_offset,
      FLAGS_val);

  printf("%s\n", output.c_str());
}

void saiPhySerdesRegisterAccess(
    QsfpServiceAsyncClient* qsfpHandler,
    bool opRead) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }
  if (FLAGS_mdio == -1 || FLAGS_lane == -1 || FLAGS_serdes_reg == -1) {
    printf("MDIO address, Serdes lane and Register offset are required\n");
    return;
  }
  if (!opRead && FLAGS_serdes_val == -1) {
    printf("Serdes Register value is required\n");
    return;
  }

  std::string output;
  qsfpHandler->sync_saiPhySerdesRegisterAccess(
      output,
      FLAGS_port,
      opRead,
      FLAGS_mdio,
      (FLAGS_side == "line") ? phy::Side::LINE : phy::Side::SYSTEM,
      FLAGS_lane,
      FLAGS_serdes_reg,
      FLAGS_serdes_val);

  printf("%s\n", output.c_str());
}

void phyConfigCheckHw(QsfpServiceAsyncClient* qsfpHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }

  std::string output;
  qsfpHandler->sync_phyConfigCheckHw(output, FLAGS_port);
  printf("%s\n", output.c_str());
}

void saiPhyCreatePort(QsfpServiceAsyncClient* qsfpHandler) {
  if (FLAGS_sw_port == -1) {
    printf("Port name is required\n");
    return;
  }
  if (FLAGS_profile == -1) {
    printf("Profile Id value is required\n");
    return;
  }

  qsfpHandler->sync_programXphyPort(
      FLAGS_sw_port, cfg::PortProfileID(FLAGS_profile));
  printf("SW Port %d got create\n", FLAGS_sw_port);
}

int main(int argc, char* argv[]) {
  const folly::Init init(&argc, &argv, true);
  gflags::SetCommandLineOptionWithMode(
      "minloglevel", "0", gflags::SET_FLAGS_DEFAULT);
  folly::EventBase evb;
  std::unique_ptr<facebook::fboss::QsfpServiceAsyncClient> client =
      QsfpClient::createClient(&evb).getVia(&evb);

  if (FLAGS_phy_port_info) {
    printPhyPortInfo(client.get());
    return 0;
  }
  if (FLAGS_set_phy_loopback) {
    setPhyLoopback(client.get(), true);
    return 0;
  }
  if (FLAGS_clear_phy_loopback) {
    setPhyLoopback(client.get(), false);
    return 0;
  }
  if (FLAGS_set_admin_up) {
    setPhyAdminState(client.get(), true);
    return 0;
  }
  if (FLAGS_set_admin_down) {
    setPhyAdminState(client.get(), false);
    return 0;
  }
  if (FLAGS_read_reg) {
    saiPhyRegisterAccess(client.get(), true);
    return 0;
  }
  if (FLAGS_write_reg) {
    saiPhyRegisterAccess(client.get(), false);
    return 0;
  }
  if (FLAGS_read_serdes_reg) {
    saiPhySerdesRegisterAccess(client.get(), true);
    return 0;
  }
  if (FLAGS_write_serdes_reg) {
    saiPhySerdesRegisterAccess(client.get(), false);
    return 0;
  }
  if (FLAGS_phy_cfg_check) {
    phyConfigCheckHw(client.get());
    return 0;
  }
  if (FLAGS_create_port) {
    saiPhyCreatePort(client.get());
    return 0;
  }
  return 0;
}
