// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/ladakh800bcls/Ladakh800bclsBspPlatformMapping.h"
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/lib/bsp/BspPlatformMapping.h"
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"

using namespace facebook::fboss;
using namespace apache::thrift;

namespace {
constexpr auto kJsonBspPlatformMappingStr = R"(
{
  "pimMapping": {
    "1": {
      "pimID": 1,
      "tcvrMapping": {
        "1": {
          "tcvrId": 1,
          "accessControl": {
            "controllerId": "1",
            "type": 1,
            "reset": {
              "sysfsPath": "/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_reset_1",
              "mask": 1,
              "gpioOffset": 0,
              "resetHoldHi": 1
            },
            "presence": {
              "sysfsPath": "/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_present_1",
              "mask": 1,
              "gpioOffset": 0,
              "presentHoldHi": 0
            },
            "gpioChip": ""
          },
          "io": {
            "controllerId": "1",
            "type": 1,
            "devicePath": "/run/devmap/xcvrs/xcvr_io_1"
          },
          "tcvrLaneToLedId": {
            "1": 1,
            "2": 1,
            "3": 1,
            "4": 1
          }
        },
        "2": {
          "tcvrId": 2,
          "accessControl": {
            "controllerId": "2",
            "type": 1,
            "reset": {
              "sysfsPath": "/run/devmap/xcvrs/xcvr_ctrl_2/xcvr_reset_2",
              "mask": 1,
              "gpioOffset": 0,
              "resetHoldHi": 1
            },
            "presence": {
              "sysfsPath": "/run/devmap/xcvrs/xcvr_ctrl_2/xcvr_present_2",
              "mask": 1,
              "gpioOffset": 0,
              "presentHoldHi": 0
            },
            "gpioChip": ""
          },
          "io": {
            "controllerId": "2",
            "type": 1,
            "devicePath": "/run/devmap/xcvrs/xcvr_io_2"
          },
          "tcvrLaneToLedId": {
            "1": 2,
            "2": 2,
            "3": 2,
            "4": 2
          }
        }
      },
      "phyMapping": {
        "0": {
          "phyId": 0,
          "phyIOControllerId": 2,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_2_1/rtm_reset_2"
        },
        "1": {
          "phyId": 1,
          "phyIOControllerId": 3,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_3_1/rtm_reset_3"
        },
        "2": {
          "phyId": 2,
          "phyIOControllerId": 4,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_4_1/rtm_reset_4"
        },
        "3": {
          "phyId": 3,
          "phyIOControllerId": 1,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_1_1/rtm_reset_1"
        },
        "4": {
          "phyId": 4,
          "phyIOControllerId": 5,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_5_1/rtm_reset_5"
        },
        "5": {
          "phyId": 5,
          "phyIOControllerId": 7,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_7_1/rtm_reset_7"
        },
        "6": {
          "phyId": 6,
          "phyIOControllerId": 8,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_8_1/rtm_reset_8"
        },
        "7": {
          "phyId": 7,
          "phyIOControllerId": 6,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_6_1/rtm_reset_6"
        },
        "8": {
          "phyId": 8,
          "phyIOControllerId": 10,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_10_1/rtm_reset_10"
        },
        "9": {
          "phyId": 9,
          "phyIOControllerId": 12,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_12_1/rtm_reset_12"
        },
        "10": {
          "phyId": 10,
          "phyIOControllerId": 11,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_11_1/rtm_reset_11"
        },
        "11": {
          "phyId": 11,
          "phyIOControllerId": 9,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_9_1/rtm_reset_9"
        },
        "12": {
          "phyId": 12,
          "phyIOControllerId": 13,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_13_1/rtm_reset_13"
        },
        "13": {
          "phyId": 13,
          "phyIOControllerId": 15,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_15_1/rtm_reset_15"
        },
        "14": {
          "phyId": 14,
          "phyIOControllerId": 16,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_16_1/rtm_reset_16"
        },
        "15": {
          "phyId": 15,
          "phyIOControllerId": 14,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_l_14_1/rtm_reset_14"
        },
        "16": {
          "phyId": 16,
          "phyIOControllerId": 18,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_2_1/rtm_reset_18"
        },
        "17": {
          "phyId": 17,
          "phyIOControllerId": 19,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_3_1/rtm_reset_19"
        },
        "18": {
          "phyId": 18,
          "phyIOControllerId": 20,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_4_1/rtm_reset_20"
        },
        "19": {
          "phyId": 19,
          "phyIOControllerId": 17,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_1_1/rtm_reset_17"
        },
        "20": {
          "phyId": 20,
          "phyIOControllerId": 21,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_5_1/rtm_reset_21"
        },
        "21": {
          "phyId": 21,
          "phyIOControllerId": 23,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_7_1/rtm_reset_23"
        },
        "22": {
          "phyId": 22,
          "phyIOControllerId": 24,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_8_1/rtm_reset_24"
        },
        "23": {
          "phyId": 23,
          "phyIOControllerId": 22,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_6_1/rtm_reset_22"
        },
        "24": {
          "phyId": 24,
          "phyIOControllerId": 26,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_10_1/rtm_reset_26"
        },
        "25": {
          "phyId": 25,
          "phyIOControllerId": 28,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_12_1/rtm_reset_28"
        },
        "26": {
          "phyId": 26,
          "phyIOControllerId": 27,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_11_1/rtm_reset_27"
        },
        "27": {
          "phyId": 27,
          "phyIOControllerId": 25,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_9_1/rtm_reset_25"
        },
        "28": {
          "phyId": 28,
          "phyIOControllerId": 29,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_13_1/rtm_reset_29"
        },
        "29": {
          "phyId": 29,
          "phyIOControllerId": 31,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_15_1/rtm_reset_31"
        },
        "30": {
          "phyId": 30,
          "phyIOControllerId": 32,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_16_1/rtm_reset_32"
        },
        "31": {
          "phyId": 31,
          "phyIOControllerId": 30,
          "phyAddr": 0,
          "phyCoreId": 0,
          "pimId": 1,
          "phyResetPath": "/run/devmap/rtms/rtm_ctrl_mdio_r_14_1/rtm_reset_30"
        }
      },
      "phyIOControllers": {
        "1": {
          "controllerId": 1,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_1",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_1/reset_bus"
        },
        "2": {
          "controllerId": 2,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_2",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_2/reset_bus"
        },
        "3": {
          "controllerId": 3,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_3",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_3/reset_bus"
        },
        "4": {
          "controllerId": 4,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_4",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_4/reset_bus"
        },
        "5": {
          "controllerId": 5,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_5",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_5/reset_bus"
        },
        "6": {
          "controllerId": 6,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_6",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_6/reset_bus"
        },
        "7": {
          "controllerId": 7,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_7",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_7/reset_bus"
        },
        "8": {
          "controllerId": 8,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_8",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_8/reset_bus"
        },
        "9": {
          "controllerId": 9,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_9",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_9/reset_bus"
        },
        "10": {
          "controllerId": 10,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_10",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_10/reset_bus"
        },
        "11": {
          "controllerId": 11,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_11",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_11/reset_bus"
        },
        "12": {
          "controllerId": 12,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_12",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_12/reset_bus"
        },
        "13": {
          "controllerId": 13,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_13",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_13/reset_bus"
        },
        "14": {
          "controllerId": 14,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_14",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_14/reset_bus"
        },
        "15": {
          "controllerId": 15,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_15",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_15/reset_bus"
        },
        "16": {
          "controllerId": 16,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_l_16",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_l_16/reset_bus"
        },
        "17": {
          "controllerId": 17,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_1",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_1/reset_bus"
        },
        "18": {
          "controllerId": 18,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_2",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_2/reset_bus"
        },
        "19": {
          "controllerId": 19,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_3",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_3/reset_bus"
        },
        "20": {
          "controllerId": 20,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_4",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_4/reset_bus"
        },
        "21": {
          "controllerId": 21,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_5",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_5/reset_bus"
        },
        "22": {
          "controllerId": 22,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_6",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_6/reset_bus"
        },
        "23": {
          "controllerId": 23,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_7",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_7/reset_bus"
        },
        "24": {
          "controllerId": 24,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_8",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_8/reset_bus"
        },
        "25": {
          "controllerId": 25,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_9",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_9/reset_bus"
        },
        "26": {
          "controllerId": 26,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_10",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_10/reset_bus"
        },
        "27": {
          "controllerId": 27,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_11",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_11/reset_bus"
        },
        "28": {
          "controllerId": 28,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_12",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_12/reset_bus"
        },
        "29": {
          "controllerId": 29,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_13",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_13/reset_bus"
        },
        "30": {
          "controllerId": 30,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_14",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_14/reset_bus"
        },
        "31": {
          "controllerId": 31,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_15",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_15/reset_bus"
        },
        "32": {
          "controllerId": 32,
          "type": 1,
          "devicePath": "/run/devmap/mdio-busses/mdio_bus_io_r_16",
          "resetPath": "/run/devmap/mdio-busses/mdio_bus_ctrl_r_16/reset_bus"
        }
      },
      "ledMapping": {
        "1": {
          "id": 1,
          "bluePath": "/sys/class/leds/port1_led1:blue:status",
          "yellowPath": "/sys/class/leds/port1_led1:amber:status",
          "transceiverId": 1
        },
        "2": {
          "id": 2,
          "bluePath": "/sys/class/leds/port2_led1:blue:status",
          "yellowPath": "/sys/class/leds/port2_led1:amber:status",
          "transceiverId": 2
        }
      }
    }
  }
}
)";

static BspPlatformMappingThrift buildLadakh800bclsPlatformMapping(
    const std::string& platformMappingStr) {
  return apache::thrift::SimpleJSONSerializer::deserialize<
      BspPlatformMappingThrift>(platformMappingStr);
}

} // namespace

namespace facebook {
namespace fboss {

Ladakh800bclsBspPlatformMapping::Ladakh800bclsBspPlatformMapping()
    : BspPlatformMapping(
          buildLadakh800bclsPlatformMapping(kJsonBspPlatformMappingStr)) {}

Ladakh800bclsBspPlatformMapping::Ladakh800bclsBspPlatformMapping(
    const std::string& platformMappingStr)
    : BspPlatformMapping(
          buildLadakh800bclsPlatformMapping(platformMappingStr)) {}

} // namespace fboss
} // namespace facebook
