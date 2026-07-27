package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

include "fboss/qsfp_service/if/transceiver.thrift"

struct ShowTransceiverModel {
  /* key: portName, value: TransceiverDetail */
  1: map<string, TransceiverDetail> transceivers;
}

struct TransceiverDetail {
  1: string name;
  2: optional bool isUp;
  3: bool isPresent;
  4: string vendor;
  5: string serial;
  6: string partNumber;
  7: double temperature;
  8: double voltage;
  9: list<double> currentMA;
  10: list<double> txPower;
  11: list<double> rxPower;
  12: list<double> rxSnr;
  13: string appFwVer;
  14: string dspFwVer;
  15: string validationStatus;
  16: string notValidatedReason;
  17: transceiver.FlagLevels tempFlags;
  18: transceiver.FlagLevels vccFlags;
  19: transceiver.MediaInterfaceCode mediaInterface;
  20: i32 transceiverID;
  /* Logical port ID of the agent port; unset for bypass modules */
  21: optional i32 portID;
}
