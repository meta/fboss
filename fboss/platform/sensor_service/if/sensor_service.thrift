namespace cpp2 facebook.fboss.platform.sensor_service
namespace go neteng.fboss.platform.sensor_service
namespace py neteng.fboss.platform.sensor_service
namespace py3 neteng.fboss.platform.sensor_service
namespace py.asyncio neteng.fboss.platform.asyncio.sensor_service

include "fboss/agent/if/fboss.thrift"
include "fboss/platform/sensor_service/if/sensor_config.thrift"
include "fboss/platform/reboot_cause_finder/if/reboot_cause_config.thrift"
include "thrift/annotation/hack.thrift"
include "thrift/annotation/thrift.thrift"

@hack.NamePrefix{prefix = "NetengFbossPlatformSensorService_"}
@hack.LegacyAlwaysIncludeNamePrefixInProcessor
@hack.LegacyOmitPrefixInNameString
@thrift.AllowLegacyMissingUris
package;

// SensorData contains the observed data of a sensor device's output.
//
// `name`: Name of the sensor device
//
// `value`: Lastest value of sensor read. Not set if the last read failed.
//
// `timeStamp`: Lastest timestamp of sensor read. Not set if the last read failed.
//
// `thresholds`: Threshold of the sensors
//
// `sensorType`: Enum which Represent the type of the sensor

struct SensorData {
  1: string name;
  2: optional float value;
  3: optional i64 timeStamp;
  4: sensor_config.Thresholds thresholds;
  5: sensor_config.SensorType sensorType;
  6: string slotPath;
  7: string sysfsPath;
}

@hack.Attributes{
  attributes = [
    "Oncalls('fboss_platform')",
    "JSEnum(shape('flow_enum' => false))",
    "GraphQLEnum('XFBFruType')",
    "GraphQLUnprefixedNamingScheme",
    "SelfDescriptive",
    "RelayFlowEnum",
  ],
}
enum FruType {
  // TODO: Add more FRU types
  ALL = 0,
  SMB = 1,
  SCM = 2,
  PEM = 3,
  PSU = 4,
}

// Send back sensor data based on request
struct SensorReadResponse {
  1: i64 timeStamp;
  2: list<SensorData> sensorData;
}

// Response for `getRebootHistory`.
//
// This is a best-effort read of a directory written by a separate process, so
// it never fails: a missing directory, an unreadable or malformed record, or
// more records than the cap all yield a partial `records` with the problem
// described in `warnings`. Callers should treat an empty list as "nothing to
// show", not as an error.
//
// `records`: successfully parsed records, newest `detectedAtMs` first.
//
// `skippedCount`: records dropped for a reason this call could count, namely
// those over the cap and those that failed to read or parse. It is a lower
// bound, not a total: a directory scan that stops partway, or a failure
// caught by the outer handler, leaves records uncounted. Non-zero means
// `records` is incomplete; zero does not prove it is complete, so check
// `warnings` too.
//
// `warnings`: human-readable reasons, deduplicated and capped. Diagnostic
// only; do not parse.
struct RebootHistoryResponse {
  1: list<reboot_cause_config.RebootCauseRecord> records;
  2: i32 skippedCount;
  3: list<string> warnings;
}

service SensorServiceThrift {
  SensorReadResponse getSensorValuesByNames(
    1: list<string> sensorNames,
  ) throws (1: fboss.FbossBaseError error);
  // Best-effort; see RebootHistoryResponse. Does not throw.
  RebootHistoryResponse getRebootHistory();
}
