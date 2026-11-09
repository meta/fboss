namespace cpp2 facebook.fboss
namespace go neteng.fboss.qsfp
namespace py neteng.fboss.qsfp
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.qsfp

include "fboss/agent/if/ctrl.thrift"
include "fboss/agent/if/common.thrift"
include "fboss/agent/if/fboss.thrift"
include "fboss/qsfp_service/if/transceiver.thrift"
include "fboss/agent/switch_config.thrift"
include "fboss/mka_service/if/mka_structs.thrift"
include "fboss/agent/hw/hardware_stats.thrift"
include "fboss/lib/phy/phy.thrift"
include "fboss/lib/phy/prbs.thrift"

service QsfpService extends phy.FbossCommonPhyCtrl {
  transceiver.QsfpServiceRunState getQsfpServiceRunState();

  transceiver.TransceiverType getType(1: i32 idx);

  /*
   * Get all information about a transceiver
   */
  map<i32, transceiver.TransceiverInfo> getTransceiverInfo(
    1: list<i32> idx,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get state information about a port from ids.
   */
  map<i32, transceiver.PortStateMachineState> getPortStateMachineState(
    1: list<i32> idx,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get state information about a port
   */
  map<
    string,
    transceiver.PortStateMachineState
  > getPortStateMachineStateFromPortNames(1: list<string> portNames) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Get config validation status of a transceiver
   */
  map<i32, string> getTransceiverConfigValidationInfo(
    1: list<i32> idx,
    2: bool getConfigString,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Do a raw read on the data for a specific transceiver.
   */
  map<i32, transceiver.RawDOMData> getTransceiverRawDOMData(
    1: list<i32> idx,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Read on the raw DOM data for a specific transceiver as a union.
   */
  map<i32, transceiver.DOMDataUnion> getTransceiverDOMDataUnion(
    1: list<i32> idx,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Tell the qsfp service about the status of ports, retrieve transceiver information
   * for each of these ports.
   */
  map<i32, transceiver.TransceiverInfo> syncPorts(
    1: map<i32, ctrl.PortStatus> ports,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Reset a select transceiver
   * portName: ethx/y/z
   * resetType: HARD_RESET
   * resetAction: RESET_THEN_CLEAR
   * Throw when trying to hard reset a SFF8472 transceiver (not supported)
   */
  void resetTransceiver(
    1: list<string> portNames,
    2: transceiver.ResetType resetType,
    3: transceiver.ResetAction resetAction,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Qsfp service has an internal remediation loop and may potentially perform
   * interruptive operation to modules that carry no active(up) link. However
   * it may cause some confusion for debugging. This function is to tell
   * qsfp-service to pause auto remediation for the specified amount of seconds.
   */
  void pauseRemediation(1: i32 timeout, 2: list<string> portList);

  /*
  * Qsfp service has an internal remediation loop and may potentially perform
  * interruptive operation to modules that carry no active(up) link. However
  * it may cause some confusion for debugging. This function is to tell
  * qsfp-service to unpause remediation.
  */
  void unpauseRemediation(1: list<string> portList);

  /*
   * Qsfp service has an internal remediation loop and may potentially perform
   * interruptive operation to modules that carry no active(up) link. However
   * it may cause some confusion for debugging. This function is to tell
   * what is the currently pause remediation expiration time set to.
   */
  map<string, i32> getRemediationUntilTime(1: list<string> portList);

  /*
  * Perform a raw register read for the specified transceivers
  */
  map<i32, transceiver.ReadResponse> readTransceiverRegister(
    1: transceiver.ReadRequest request,
  ) throws (1: fboss.FbossBaseError error);

  /*
  * Perform a raw register write for the specified transceivers
  */
  map<i32, transceiver.WriteResponse> writeTransceiverRegister(
    1: transceiver.WriteRequest request,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * MACSEC capable port ids
  */
  list<i32> getMacsecCapablePorts() throws (1: fboss.FbossBaseError error);

  /*
   * Thrift call to program one PHY port. This call will be made by agent to
   * qsfp_service
   */
  void programXphyPort(
    1: i32 portId,
    2: switch_config.PortProfileID portProfileId,
  );

  phy.PhyInfo getXphyInfo(1: i32 portID) throws (1: fboss.FbossBaseError error);

  /*
   * Change the PRBS setting on a port. Useful when debugging a link
   * down or flapping issue.
   */
  void setPortPrbs(
    1: i32 portId,
    2: phy.PortComponent component,
    3: phy.PortPrbsState state,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get the PRBS stats on a port. Useful when debugging a link
   * down or flapping issue.
   */
  phy.PrbsStats getPortPrbsStats(
    1: i32 portId,
    2: phy.PortComponent component,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Clear the PRBS stats counter on a port. Useful when debugging a link
   * down or flapping issue.
   * This clearPortPrbsStats will result in:
   * 1. reset ber (due to reset accumulated error count if implemented)
   * 2. reset maxBer
   * 3. reset numLossOfLock to 0
   * 4. set timeLastCleared to now
   * 5. set timeLastLocked to timeLastCollect if locked else epoch
   * 6. locked status not changed
   * 7. timeLastCollect not changed
   */
  void clearPortPrbsStats(
    1: i32 portId,
    2: phy.PortComponent component,
  ) throws (1: fboss.FbossBaseError error);

  string listHwObjects(
    1: list<ctrl.HwObjectType> objects,
    2: bool cached,
  ) throws (1: fboss.FbossBaseError error);

  bool getSdkState(1: string fileName) throws (1: fboss.FbossBaseError error);

  string getPortInfo(1: string portName) throws (1: fboss.FbossBaseError error);

  // OUTPUT is only supported for TRANSCEIVER_SYSTEM and TRANSCEIVER_LINE.
  void setPortLoopbackState(
    1: string portName,
    2: phy.PortComponent component,
    3: bool setLoopback = false,
    4: phy.LoopbackMode mode = phy.LoopbackMode.INPUT,
  ) throws (1: fboss.FbossBaseError error);

  void setPortAdminState(
    1: string portName,
    2: phy.PortComponent component,
    3: bool setAdminUp = true,
  ) throws (1: fboss.FbossBaseError error);

  transceiver.CdbDatapathSymErrHistogram getSymbolErrorHistogram(
    1: string portName,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Returns a map of all platform software port names to the list of port
   * profile Id supported by that port. If checkOptics is set True then it will
   * exclude the port profiles which optics does not support
   */
  map<string, list<switch_config.PortProfileID>> getAllPortSupportedProfiles(
    1: bool checkOptics = true,
  ) throws (1: fboss.FbossBaseError error);

  string saiPhyRegisterAccess(
    1: string portName,
    2: bool opRead = true,
    3: i32 phyAddr,
    4: i32 devId,
    5: i32 regOffset,
    6: i32 data,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * PAI Diag Shell access for Broadcom Agera3 retimer chips.
   *
   * Provides bcmsh-style interactive access to the PAI library's built-in
   * diag shell (brcm_pai_diag.c). Mirrors the SaiCtrl service's startDiagShell
   * /produceDiagShellInput/diagCmd pattern used by wedge_agent for ASIC NPU.
   *
   * The PAI shell's `s <switch_id>` command lets you switch chip context at
   * runtime, so a single shell session can hop across all 32 retimers.
   */
  string, stream<string> startPaiDiagShell() throws (
    1: fboss.FbossBaseError error,
  );

  void producePaiDiagShellInput(
    1: string input,
    2: common.ClientInformation client,
  ) throws (1: fboss.FbossBaseError error);

  string paiDiagCmd(
    1: string input,
    2: common.ClientInformation client,
  ) throws (1: fboss.FbossBaseError error);

  string saiPhySerdesRegisterAccess(
    1: string portName,
    2: bool opRead = true,
    3: i16 mdioAddr,
    4: phy.Side side,
    5: i32 serdesLane,
    6: i64 regOffset,
    7: i64 data,
  ) throws (1: fboss.FbossBaseError error);

  string phyConfigCheckHw(1: string portName) throws (
    1: fboss.FbossBaseError error,
  );

  bool sakInstallRx(
    1: mka_structs.MKASak sak,
    2: mka_structs.MKASci sciToAdd,
  ) throws (1: mka_structs.MKAServiceException ex);

  bool sakInstallTx(1: mka_structs.MKASak sak) throws (
    1: mka_structs.MKAServiceException ex,
  );

  bool sakDeleteRx(
    1: mka_structs.MKASak sak,
    2: mka_structs.MKASci sciToRemove,
  ) throws (1: mka_structs.MKAServiceException ex);

  bool sakDelete(1: mka_structs.MKASak sak) throws (
    1: mka_structs.MKAServiceException ex,
  );

  mka_structs.MKASakHealthResponse sakHealthCheck(
    1: mka_structs.MKASak sak,
  ) throws (1: mka_structs.MKAServiceException ex);

  mka_structs.MacsecPortPhyMap macsecGetPhyPortInfo(
    1: list<string> portNames = [],
  ) throws (1: fboss.FbossBaseError error);

  ctrl.PortOperState macsecGetPhyLinkInfo(1: string portName) throws (
    1: fboss.FbossBaseError error,
  );

  phy.PhyInfo getPhyInfo(1: string portName) throws (
    1: fboss.FbossBaseError error,
  );

  bool deleteAllSc(1: string portName) throws (1: fboss.FbossBaseError error);

  bool setupMacsecState(
    1: list<string> portList,
    2: bool macsecDesired = false,
    3: bool dropUnencrypted = false,
  ) throws (1: fboss.FbossBaseError error);

  map<string, hardware_stats.MacsecStats> getAllMacsecPortStats(
    1: bool readFromHw = false,
  ) throws (1: fboss.FbossBaseError error);

  map<string, hardware_stats.MacsecStats> getMacsecPortStats(
    1: list<string> portNames,
    2: bool readFromHw = false,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get the PRBS settings on all interfaces.
   */
  map<string, prbs.InterfacePrbsState> getAllInterfacePrbsStates(
    1: phy.PortComponent component,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get the PRBS stats on all interfaces.
   */
  map<string, phy.PrbsStats> getAllInterfacePrbsStats(
    1: phy.PortComponent component,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Bulk clear the PRBS stats counters on interfaces.
   */
  void bulkClearInterfacePrbsStats(
    1: list<string> interfaces,
    2: phy.PortComponent component,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Dump the i2c log for a transceiver
   */
  void dumpTransceiverI2cLog(1: string portName) throws (
    1: fboss.FbossBaseError error,
  );

  map<
    string,
    transceiver.FirmwareUpgradeData
  > getPortsRequiringOpticsFwUpgrade() throws (1: fboss.FbossBaseError error);

  map<
    string,
    transceiver.FirmwareUpgradeData
  > triggerAllOpticsFwUpgrade() throws (1: fboss.FbossBaseError error);

  map<string, transceiver.FirmwareUpgradeData> triggerOpticsFwUpgrade(
    1: list<string> interfaces,
  ) throws (1: fboss.FbossBaseError error);

  map<string, transceiver.MediaInterfaceCode> getPortMediaInterface() throws (
    1: fboss.FbossBaseError error,
  );

  map<string, list<i32>> getPortTransceiverIDs() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Get transceiver information for specific ports by name.
   * Returns a map keyed by port name to TransceiverInfo.
   * This avoids the need for multiple RPCs (getPortInfo + getTransceiverInfo)
   * when querying transceiver details for specific ports.
   */
  map<string, transceiver.TransceiverInfo> getTransceiverInfoByPortName(
    1: list<string> portNames,
  ) throws (1: fboss.FbossBaseError error);
}
