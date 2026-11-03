// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

namespace facebook::fboss {

// Default transceiver properties config.
// This is the source of truth for all known transceiver types.
// To add a new transceiver, add an entry here keyed by its
// MediaInterfaceCode integer value.
// Note: hex literals (e.g. 0x77) are supported via JSON5 parsing.
constexpr auto kDefaultTransceiverPropertiesJson = R"({
  "smfTransceivers": {
    "4": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x18},
        "hostStartLanes": [0],
        "hostInterfaceCode": 0x0F
      },
      "numHostLanes": 4,
      "numMediaLanes": 4,
      "displayName": "FR4_200G",
      "diagnosticCapabilitiesExceptions": {
        "doesNotSupportVdm": true,
        "doesNotSupportRxOutputControl": true
      },
      "supportedSpeedCombinations": [
        {
          "combinationName": "1x200G-FR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4}
          ]
        },
        {
          "combinationName": "1x100G-CWDM4",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x10}, "mediaInterfaceCode": 1}
          ]
        }
      ],
      "speedChangeTransitions": [
        ["1x200G-FR4", "1x100G-CWDM4"]
      ]
    },
    "5": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x1D},
        "hostStartLanes": [0],
        "hostInterfaceCode": 0x50
      },
      "smfLength": 2000,
      "numHostLanes": 8,
      "numMediaLanes": 4,
      "displayName": "FR4_400G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "1x400G-FR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5}
          ]
        },
        {
          "combinationName": "1x200G-FR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4}
          ]
        },
        {
          "combinationName": "1x100G-CWDM4",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x10}, "mediaInterfaceCode": 1}
          ]
        }
      ],
      "speedChangeTransitions": [
        ["1x400G-FR4", "1x200G-FR4"]
      ]
    },
    "6": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x1E},
        "hostStartLanes": [0],
        "hostInterfaceCode": 0x50
      },
      "numHostLanes": 8,
      "numMediaLanes": 4,
      "displayName": "LR4_400G_10KM",
      "supportedSpeedCombinations": [
        {
          "combinationName": "1x400G-LR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1E}, "mediaInterfaceCode": 6}
          ]
        },
        {
          "combinationName": "1x200G-FR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4}
          ]
        },
        {
          "combinationName": "1x100G-CWDM4",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x10}, "mediaInterfaceCode": 1}
          ]
        }
      ],
      "speedChangeTransitions": [
        ["1x400G-LR4", "1x200G-FR4"]
      ]
    },
    "14": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x1C},
        "hostStartLanes": [0],
        "hostInterfaceCode": 0x50
      },
      "numHostLanes": 8,
      "numMediaLanes": 4,
      "displayName": "DR4_400G",
      "diagnosticCapabilitiesExceptions": {
        "doesNotSupportVdm": true,
        "doesNotSupportRxOutputControl": true
      },
      "supportedSpeedCombinations": [
        {
          "combinationName": "1x400G-DR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1C}, "mediaInterfaceCode": 14}
          ]
        }
      ]
    },
    "11": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x1D},
        "hostStartLanes": [0, 4],
        "hostInterfaceCode": 0x50
      },
      "smfLength": 3000,
      "numHostLanes": 8,
      "numMediaLanes": 8,
      "displayName": "FR4_2x400G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "2x400G-FR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5}
          ]
        },
        {
          "combinationName": "2x200G-FR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4}
          ]
        },
        {
          "combinationName": "400G-FR4+200G-FR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4}
          ]
        },
        {
          "combinationName": "200G-FR4+400G-FR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5}
          ]
        },
        {
          "combinationName": "8x106.25G-FR1",
          "ports": [
            {"speed": 106250, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3}
          ]
        },
        {
          "combinationName": "8x100G-FR1",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}}
          ]
        },
        {
          "combinationName": "2x100G-CWDM4",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x10}, "mediaInterfaceCode": 1},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x10}, "mediaInterfaceCode": 1}
          ]
        },
        {
          "combinationName": "1x800G-2FR4",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 8}, "mediaLaneCode": {"smfCode": 0xC1}, "mediaInterfaceCode": 15}
          ]
        },
        {
          "combinationName": "4x200G-FR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 2}, "mediaLanes": {"start": 0, "count": 2}, "mediaLaneCode": {"smfCode": 0xC0}, "mediaInterfaceCode": 37},
            {"speed": 200000, "hostLanes": {"start": 2, "count": 2}, "mediaLanes": {"start": 2, "count": 2}, "mediaLaneCode": {"smfCode": 0xC0}, "mediaInterfaceCode": 37},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 2}, "mediaLanes": {"start": 4, "count": 2}, "mediaLaneCode": {"smfCode": 0xC0}, "mediaInterfaceCode": 37},
            {"speed": 200000, "hostLanes": {"start": 6, "count": 2}, "mediaLanes": {"start": 6, "count": 2}, "mediaLaneCode": {"smfCode": 0xC0}, "mediaInterfaceCode": 37}
          ]
        }
      ],
      "speedChangeTransitions": [
        ["2x400G-FR4", "2x200G-FR4"],
        ["2x400G-FR4", "400G-FR4+200G-FR4"],
        ["2x400G-FR4", "200G-FR4+400G-FR4"],
        ["2x400G-FR4", "4x200G-FR4"],
      ]
    },
    "13": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x1C},
        "hostStartLanes": [0, 4],
        "hostInterfaceCode": 0x50
      },
      "numHostLanes": 8,
      "numMediaLanes": 8,
      "displayName": "DR4_2x400G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "2x400G-DR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1C}, "mediaInterfaceCode": 14},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1C}, "mediaInterfaceCode": 14}
          ]
        },
        {
          "combinationName": "4x200G-DR2",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 2}, "mediaLanes": {"start": 0, "count": 2}, "mediaLaneCode": {"smfCode": 0x98}, "mediaInterfaceCode": 34},
            {"speed": 200000, "hostLanes": {"start": 2, "count": 2}, "mediaLanes": {"start": 2, "count": 2}, "mediaLaneCode": {"smfCode": 0x98}, "mediaInterfaceCode": 34},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 2}, "mediaLanes": {"start": 4, "count": 2}, "mediaLaneCode": {"smfCode": 0x98}, "mediaInterfaceCode": 34},
            {"speed": 200000, "hostLanes": {"start": 6, "count": 2}, "mediaLanes": {"start": 6, "count": 2}, "mediaLaneCode": {"smfCode": 0x98}, "mediaInterfaceCode": 34}
          ]
        },
        {
          "combinationName": "8x100G-DR1",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28}
          ]
        }
      ]
    },
    "17": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x1D},
        "hostStartLanes": [0, 4],
        "hostInterfaceCode": 0x50
      },
      "smfLength": 500,
      "numHostLanes": 8,
      "numMediaLanes": 8,
      "displayName": "FR4_LITE_2x400G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "2x400G-FR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5}
          ]
        },
        {
          "combinationName": "2x200G-FR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4}
          ]
        },
        {
          "combinationName": "400G-FR4+200G-FR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4}
          ]
        },
        {
          "combinationName": "200G-FR4+400G-FR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x18}, "mediaInterfaceCode": 4},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5}
          ]
        },
        {
          "combinationName": "8x106.25G-FR1",
          "ports": [
            {"speed": 106250, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3}
          ]
        },
        {
          "combinationName": "8x100G-FR1",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}}
          ]
        },
        {
          "combinationName": "2x100G-CWDM4",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x10}, "mediaInterfaceCode": 1},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x10}, "mediaInterfaceCode": 1}
          ]
        },
        {
          "combinationName": "1x800G-2FR4",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 8}, "mediaLaneCode": {"smfCode": 0xC1}, "mediaInterfaceCode": 15}
          ]
        }
      ],
      "speedChangeTransitions": [
        ["2x400G-FR4", "2x200G-FR4"],
        ["2x400G-FR4", "400G-FR4+200G-FR4"],
        ["2x400G-FR4", "200G-FR4+400G-FR4"],
      ]
    },
    "20": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x1E},
        "hostStartLanes": [0, 4],
        "hostInterfaceCode": 0x50
      },
      "numHostLanes": 8,
      "numMediaLanes": 8,
      "displayName": "LR4_2x400G_10KM",
      "supportedSpeedCombinations": [
        {
          "combinationName": "2x400G-LR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1E}, "mediaInterfaceCode": 6},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1E}, "mediaInterfaceCode": 6}
          ]
        },
        {
          "combinationName": "2x200G-LR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x19}, "mediaInterfaceCode": 21},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x19}, "mediaInterfaceCode": 21}
          ]
        },
        {
          "combinationName": "400G-LR4+200G-LR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1E}, "mediaInterfaceCode": 6},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x19}, "mediaInterfaceCode": 21}
          ]
        },
        {
          "combinationName": "200G-LR4+400G-LR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x19}, "mediaInterfaceCode": 21},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1E}, "mediaInterfaceCode": 6}
          ]
        },
        {
          "combinationName": "2x100G-CWDM4",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x10}, "mediaInterfaceCode": 1},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x10}, "mediaInterfaceCode": 1}
          ]
        },
        {
          "combinationName": "1x800G-2LR4",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 8}, "mediaLaneCode": {"smfCode": 0xC1}, "mediaInterfaceCode": 15}
          ]
        }
      ],
      "speedChangeTransitions": [
        ["2x400G-LR4", "2x200G-LR4"],
        ["2x400G-LR4", "400G-LR4+200G-LR4"],
        ["2x400G-LR4", "200G-LR4+400G-LR4"],
      ]
    },
    "23": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x77},
        "hostStartLanes": [0, 4],
        "hostInterfaceCode": 0x82
      },
      "smfLength": 500,
      "numHostLanes": 8,
      "numMediaLanes": 8,
      "displayName": "DR4_2x800G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "2x800G-DR4",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x77}, "mediaInterfaceCode": 22},
            {"speed": 800000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x77}, "mediaInterfaceCode": 22}
          ]
        },
        {
          "combinationName": "2x400G-DR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1C}, "mediaInterfaceCode": 14},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1C}, "mediaInterfaceCode": 14}
          ]
        },
        {
          "combinationName": "8x100G-DR1",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28}
          ]
        },
        {
          "combinationName": "8x200G-DR1",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24}
          ]
        },
        {
          "combinationName": "4x400G-DR2",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 2}, "mediaLanes": {"start": 0, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 2, "count": 2}, "mediaLanes": {"start": 2, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 2}, "mediaLanes": {"start": 4, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 6, "count": 2}, "mediaLanes": {"start": 6, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27}
          ]
        }
      ]
    },
    "25": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x15},
        "hostStartLanes": [0, 1, 2, 3, 4, 5, 6, 7],
        "hostInterfaceCode": 0x20
      },
      "numHostLanes": 8,
      "numMediaLanes": 8,
      "displayName": "FR4_LPO_2x400G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "8x106.25G-FR1",
          "ports": [
            {"speed": 106250, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3},
            {"speed": 106250, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}, "mediaInterfaceCode": 3}
          ]
        },
        {
          "combinationName": "8x100G-FR1",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}}
          ]
        },
        {
          "combinationName": "2x400G-FR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1D}, "mediaInterfaceCode": 5}
          ]
        },
        {
          "combinationName": "1x800G-2FR4",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 8}, "mediaLaneCode": {"smfCode": 0xC1}, "mediaInterfaceCode": 15}
          ]
        }
      ]
    },
    "26": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x68},
        "hostStartLanes": [0],
        "hostInterfaceCode": 0x51
      },
      "numHostLanes": 8,
      "numMediaLanes": 1,
      "displayName": "ZR_800G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "800G-ZR",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x68}, "mediaInterfaceCode": 26}
          ]
        },
        {
          "combinationName": "800G-ZR-OIF-ZRA",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x6C}, "mediaInterfaceCode": 26}
          ]
        },
        {
          "combinationName": "800G-ZR-Vendor-Custom",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0xF7}, "mediaInterfaceCode": 26}
          ]
        },
        {
          "combinationName": "3x200G-ZR-FlexO-6e-DPO",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 2}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x6A}, "mediaInterfaceCode": 26},
            {"speed": 200000, "hostLanes": {"start": 2, "count": 2}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x6A}, "mediaInterfaceCode": 26},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 2}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x6A}, "mediaInterfaceCode": 26}
          ]
        }
      ]
    },
    "30": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x79},
        "hostStartLanes": [0, 4],
        "hostInterfaceCode": 0x82
      },
      "smfLength": 3000,
      "numHostLanes": 8,
      "numMediaLanes": 8,
      "displayName": "FR4_2x800G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "2x800G-FR4",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x79}, "mediaInterfaceCode": 31},
            {"speed": 800000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x79}, "mediaInterfaceCode": 31}
          ]
        },
        {
          "combinationName": "4x400G-FR2",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 2}, "mediaLanes": {"start": 0, "count": 2}, "mediaLaneCode": {"smfCode": 0xC2}, "mediaInterfaceCode": 32},
            {"speed": 400000, "hostLanes": {"start": 2, "count": 2}, "mediaLanes": {"start": 2, "count": 2}, "mediaLaneCode": {"smfCode": 0xC2}, "mediaInterfaceCode": 32},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 2}, "mediaLanes": {"start": 4, "count": 2}, "mediaLaneCode": {"smfCode": 0xC2}, "mediaInterfaceCode": 32},
            {"speed": 400000, "hostLanes": {"start": 6, "count": 2}, "mediaLanes": {"start": 6, "count": 2}, "mediaLaneCode": {"smfCode": 0xC2}, "mediaInterfaceCode": 32}
          ]
        },
        {
          "combinationName": "8x200G-FR1",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0xC4}, "mediaInterfaceCode": 33},
            {"speed": 200000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0xC4}, "mediaInterfaceCode": 33},
            {"speed": 200000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0xC4}, "mediaInterfaceCode": 33},
            {"speed": 200000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0xC4}, "mediaInterfaceCode": 33},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0xC4}, "mediaInterfaceCode": 33},
            {"speed": 200000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0xC4}, "mediaInterfaceCode": 33},
            {"speed": 200000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0xC4}, "mediaInterfaceCode": 33},
            {"speed": 200000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0xC4}, "mediaInterfaceCode": 33}
          ]
        },
        {
          "combinationName": "8x100G-FR1",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}},
            {"speed": 100000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x15}}
          ]
        }
      ],
      "speedChangeTransitions": [
        ["2x800G-FR4", "4x400G-FR2"]
      ]
    },
    "35": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x77},
        "hostStartLanes": [0],
        "hostInterfaceCode": 0x52
      },
      "smfLength": 500,
      "numHostLanes": 8,
      "numMediaLanes": 4,
      "displayName": "DR4_800G_GEARBOX",
      "supportedSpeedCombinations": [
        {
          "combinationName": "1x800G-DR4",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x77}, "mediaInterfaceCode": 22},
          ]
        },
        {
          "combinationName": "2x400G-DR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 2, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            ]
        },
        {
          "combinationName": "4x200G-DR4",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 2}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 2, "count": 2}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 2}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 6, "count": 2}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            ]
        }
      ]
    },
    "36": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x77},
        "hostStartLanes": [0, 4],
        "hostInterfaceCode": 0x82,
        "maxNumBanks": 4
      },
      "smfLength": 500,
      "numHostLanes": 32,
      "numMediaLanes": 32,
      "displayName": "DR4_8x800G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "2x800G-DR4",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x77}, "mediaInterfaceCode": 22},
            {"speed": 800000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x77}, "mediaInterfaceCode": 22}
          ]
        },
        {
          "combinationName": "4x400G-DR2",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 2}, "mediaLanes": {"start": 0, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 2, "count": 2}, "mediaLanes": {"start": 2, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 2}, "mediaLanes": {"start": 4, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 6, "count": 2}, "mediaLanes": {"start": 6, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27}
          ]
        },
        {
          "combinationName": "8x200G-DR1",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24}
          ]
        },
        {
          "combinationName": "2x400G-DR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1C}, "mediaInterfaceCode": 14},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1C}, "mediaInterfaceCode": 14}
          ]
        },
        {
          "combinationName": "8x100G-DR1",
          "ports": [
            {"speed": 100000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28},
            {"speed": 100000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x14}, "mediaInterfaceCode": 28}
          ]
        }
      ]
    },
    "38": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x56},
        "hostStartLanes": [0],
        "hostInterfaceCode": 0x52
      },
      "smfLength": 500,
      "numHostLanes": 8,
      "numMediaLanes": 8,
      "displayName": "DR8_800G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "1x800G-DR8",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 8}, "mediaLaneCode": {"smfCode": 0x56}, "mediaInterfaceCode": 38}
          ]
        },
        {
          "combinationName": "2x400G-DR4",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x1C}, "mediaInterfaceCode": 14},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x1C}, "mediaInterfaceCode": 14}
          ]
        },
        {
          "combinationName": "4x200G-DR2",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 2}, "mediaLanes": {"start": 0, "count": 2}, "mediaLaneCode": {"smfCode": 0x98}, "mediaInterfaceCode": 34},
            {"speed": 200000, "hostLanes": {"start": 2, "count": 2}, "mediaLanes": {"start": 2, "count": 2}, "mediaLaneCode": {"smfCode": 0x98}, "mediaInterfaceCode": 34},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 2}, "mediaLanes": {"start": 4, "count": 2}, "mediaLaneCode": {"smfCode": 0x98}, "mediaInterfaceCode": 34},
            {"speed": 200000, "hostLanes": {"start": 6, "count": 2}, "mediaLanes": {"start": 6, "count": 2}, "mediaLaneCode": {"smfCode": 0x98}, "mediaInterfaceCode": 34}
          ]
        }
      ],
      "speedChangeTransitions": [
        ["1x800G-DR8", "2x400G-DR4"],
        ["2x400G-DR4", "4x200G-DR2"]
      ]
    },
    "39": {
      "firstApplicationAdvertisement": {
        "mediaInterfaceCode": {"smfCode": 0x73},
        "hostStartLanes": [0, 1, 2, 3, 4, 5, 6, 7],
        "hostInterfaceCode": 0x80
      },
      "smfLength": 500,
      "numHostLanes": 8,
      "numMediaLanes": 8,
      "displayName": "DR8_1600G",
      "supportedSpeedCombinations": [
        {
          "combinationName": "1x1600G-DR8",
          "ports": [
            {"speed": 1600000, "hostLanes": {"start": 0, "count": 8}, "mediaLanes": {"start": 0, "count": 8}, "mediaLaneCode": {"smfCode": 0x7F}, "mediaInterfaceCode": 39}
          ]
        },
        {
          "combinationName": "2x800G-DR4",
          "ports": [
            {"speed": 800000, "hostLanes": {"start": 0, "count": 4}, "mediaLanes": {"start": 0, "count": 4}, "mediaLaneCode": {"smfCode": 0x77}, "mediaInterfaceCode": 22},
            {"speed": 800000, "hostLanes": {"start": 4, "count": 4}, "mediaLanes": {"start": 4, "count": 4}, "mediaLaneCode": {"smfCode": 0x77}, "mediaInterfaceCode": 22}
          ]
        },
        {
          "combinationName": "4x400G-DR2",
          "ports": [
            {"speed": 400000, "hostLanes": {"start": 0, "count": 2}, "mediaLanes": {"start": 0, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 2, "count": 2}, "mediaLanes": {"start": 2, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 4, "count": 2}, "mediaLanes": {"start": 4, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27},
            {"speed": 400000, "hostLanes": {"start": 6, "count": 2}, "mediaLanes": {"start": 6, "count": 2}, "mediaLaneCode": {"smfCode": 0x75}, "mediaInterfaceCode": 27}
          ]
        },
        {
          "combinationName": "8x200G-DR1",
          "ports": [
            {"speed": 200000, "hostLanes": {"start": 0, "count": 1}, "mediaLanes": {"start": 0, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 1, "count": 1}, "mediaLanes": {"start": 1, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 2, "count": 1}, "mediaLanes": {"start": 2, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 3, "count": 1}, "mediaLanes": {"start": 3, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 4, "count": 1}, "mediaLanes": {"start": 4, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 5, "count": 1}, "mediaLanes": {"start": 5, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 6, "count": 1}, "mediaLanes": {"start": 6, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24},
            {"speed": 200000, "hostLanes": {"start": 7, "count": 1}, "mediaLanes": {"start": 7, "count": 1}, "mediaLaneCode": {"smfCode": 0x73}, "mediaInterfaceCode": 24}
          ]
        }
      ]
    }
  }
})";

} // namespace facebook::fboss
