include "thrift/annotation/thrift.thrift"
include "thrift/annotation/hack.thrift"

@hack.NamePrefix{prefix = "NetengFbossPlatformFanService_"}
@hack.LegacyAlwaysIncludeNamePrefixInProcessor
@hack.LegacyOmitPrefixInNameString
@thrift.AllowLegacyMissingUris
package;

namespace cpp2 facebook.fboss.platform.fan_service
namespace py3 neteng.fboss.platform.fan_service

// Holds latest status of Fans
// `fanFailed`: Whether fan is in failure state. Either it's missing, unaccessible, unable to be programmed.
// `pwmToProgram`: Last pwm value computed to program based on the latest sensors/optics data.
// `rpm`: Last rpm value observed.
// `lastSuccessfulAccessTime`: Last time the fan was successfully accessed.
struct FanStatus {
  1: bool fanFailed;
  2: float pwmToProgram;
  3: i64 lastSuccessfulAccessTime;
  4: optional i32 rpm;
}

struct FanStatusesResponse {
  1: map<string /*fanName*/, FanStatus> fanStatuses;
}

service FanService {
  FanStatusesResponse getFanStatuses();
}
