namespace cpp2 facebook.fboss
namespace go neteng.fboss.test_ctrl
namespace py neteng.fboss.test_ctrl
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.test_ctrl

include "fboss/agent/if/fboss.thrift"
include "common/network/if/Address.thrift"
include "fboss/agent/if/ctrl.thrift"
include "fboss/agent/switch_config.thrift"

service TestCtrl extends ctrl.FbossCtrl {
  void gracefullyRestartService(
    1: string serviceName,
    2: i32 delayInSeconds = 0,
  );

  void ungracefullyRestartService(1: string serviceName);

  // When Agent is stopped, Test server will exit.
  // Impleneting Agent start will then require the client to login to the
  // device or need a separate thrift server.
  // We avoid it by implementing restartWithDelay API as below:
  //     - Schedule a restart of Agent after delayInSeconds.
  //     - Stop Agent
  //
  // Multi-switch: stops sw_agent only (not hw_agent), to trigger
  // subscriber-side DSF GR via FSDB publisher disconnect. Use
  // gracefullyRestartService for sw+hw.
  void gracefullyRestartServiceWithDelay(
    1: string serviceName,
    2: i32 delayInSeconds,
  );

  void addNeighbor(
    1: i32 interfaceID,
    2: Address.BinaryAddress ip,
    3: string mac,
    4: i32 portID,
  );

  // Apply the specified drain state on every NPU
  void setSwitchDrainState(1: switch_config.SwitchDrainState switchDrainState);

  // Set SHEL state for a NIF port of a VOQ Switch
  void setSelfHealingLagState(1: i32 portId, -1: bool enable) throws (
    1: fboss.FbossBaseError error,
  );

  // Set conditional entropy rehash for a NIF port of a VOQ Switch
  void setConditionalEntropyRehash(1: i32 portId, -1: bool enable) throws (
    1: fboss.FbossBaseError error,
  );
}
