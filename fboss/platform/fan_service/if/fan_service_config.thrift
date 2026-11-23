include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace cpp2 facebook.fboss.platform.fan_service
namespace py3 neteng.fboss.platform.fan_service

const string ZONE_TYPE_MAX = "ZONE_TYPE_MAX";
const string ZONE_TYPE_MIN = "ZONE_TYPE_MIN";
const string ZONE_TYPE_AVG = "ZONE_TYPE_AVG";
const string OPTIC_TYPE_100_GENERIC = "OPTIC_TYPE_100_GENERIC";
const string OPTIC_TYPE_200_GENERIC = "OPTIC_TYPE_200_GENERIC";
const string OPTIC_TYPE_400_GENERIC = "OPTIC_TYPE_400_GENERIC";
const string OPTIC_TYPE_800_GENERIC = "OPTIC_TYPE_800_GENERIC";
const string OPTIC_TYPE_800_ZR = "OPTIC_TYPE_800_ZR";
const string OPTIC_TYPE_1600_GENERIC = "OPTIC_TYPE_1600_GENERIC";
const string OPTIC_AGGREGATION_TYPE_MAX = "OPTIC_AGGREGATION_TYPE_MAX";
const string OPTIC_AGGREGATION_TYPE_PID = "OPTIC_AGGREGATION_TYPE_PID";
const string SENSOR_PWM_CALC_TYPE_FOUR_LINEAR_TABLE = "SENSOR_PWM_CALC_TYPE_FOUR_LINEAR_TABLE";
const string SENSOR_PWM_CALC_TYPE_PID = "SENSOR_PWM_CALC_TYPE_PID";
const string SENSOR_PWM_CALC_TYPE_INCREMENTAL_PID = "SENSOR_PWM_CALC_TYPE_INCREMENTAL_PID";
const string OPTIC_AGGREGATION_TYPE_INCREMENTAL_PID = "OPTIC_AGGREGATION_TYPE_INCREMENTAL_PID";

struct Zone {
  1: string zoneType;
  2: string zoneName;
  3: list<string> sensorNames;
  4: list<string> fanNames;
  5: i32 slope;
}

# If the read temperature exceeds the specified temperature,
# set the PWM to the specified value.
typedef map<i32, i16> TempToPwmMap

# PID specific settings
# setPoint : Target set point, affecting ki based calculation
# kp : Proportional gain in PID method
# ki : Integral gain in PID method
# kd : Derivative gain in PID method
# negHysteresis : the maximum downward PID value change per control
# posHysteresis : the maximum upward PID value change per control
struct PidSetting {
  1: float kp;
  2: float ki;
  3: float kd;
  4: float setPoint;
  5: float negHysteresis;
  6: float posHysteresis;
}

struct Optic {
  1: string opticName;
  4: string aggregationType;
  5: map<string /* optic_type */, TempToPwmMap> tempToPwmMaps;
  6: map<string /* optic_type */, PidSetting> pidSettings;
}

struct Gpio {
  1: string path;
  2: i32 lineIndex;
  3: i16 desiredValue;
}

struct Fan {
  1: string fanName;
  2: string rpmSysfsPath;
  3: string pwmSysfsPath;
  4: optional string presenceSysfsPath;
  5: optional string goodLedSysfsPath;
  6: optional string failLedSysfsPath;
  7: i32 pwmMin;
  8: i32 pwmMax;
  9: i32 fanPresentVal;
  10: i32 fanMissingVal;
  11: optional Gpio presenceGpio;
  12: optional i32 rpmMin;
  13: optional i32 rpmMax;
}

struct Watchdog {
  1: string sysfsPath;
  2: i32 value;
}

struct Sensor {
  1: string sensorName;
  6: string pwmCalcType;
  8: TempToPwmMap normalUpTable;
  9: TempToPwmMap normalDownTable;
  10: TempToPwmMap failUpTable;
  11: TempToPwmMap failDownTable;
  12: PidSetting pidSetting;
  13: optional TempToPwmMap twoRotorsFailUpTable;
  14: optional TempToPwmMap twoRotorsFailDownTable;
}

struct ControlInterval {
  1: i32 sensorReadInterval;
  2: i32 pwmUpdateInterval;
}

struct OvertempEntry {
  1: string sensorName;
  2: float overtempThreshold;
  3: optional i32 slidingWindowSize;
}

struct ShutdownCondition {
  1: i32 numOvertempSensorForShutdown;
  2: list<OvertempEntry> conditions;
}

struct FanServiceConfig {
  1: optional ControlInterval controlInterval;
  2: string shutdownCmd;
  3: list<Zone> zones;
  4: list<Sensor> sensors;
  5: list<Optic> optics;
  6: list<Fan> fans;
  7: optional Watchdog watchdog;
  11: i16 pwmBoostOnNumDeadFan;
  12: i16 pwmBoostOnNumDeadSensor;
  13: i16 pwmBoostOnNoQsfpAfterInSec;
  14: i16 pwmBoostValue;
  15: i16 pwmTransitionValue;
  16: i16 pwmUpperThreshold;
  17: i16 pwmLowerThreshold;
  18: optional ShutdownCondition shutdownCondition;
}
