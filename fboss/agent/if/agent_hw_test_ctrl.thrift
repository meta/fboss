namespace cpp2 facebook.fboss.utility
namespace go neteng.fboss.agent_hw_test_ctrl
namespace py neteng.fboss.agent_hw_test_ctrl
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.agent_hw_test_ctrl

include "thrift/annotation/cpp.thrift"
include "fboss/agent/switch_state.thrift"
include "fboss/agent/switch_config.thrift"
include "fboss/agent/if/ctrl.thrift"
include "fboss/agent/if/mpls.thrift"
include "fboss/lib/phy/phy.thrift"
include "common/network/if/Address.thrift"
include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

struct NeighborInfo {
  1: bool exists;
  2: bool isProgrammedToCpu;
  3: optional i32 classId;
}

struct CIDRNetwork {
  1: string IPAddress;
  @cpp.Type{name = "uint8_t"}
  2: byte mask;
}

struct RouteInfo {
  1: bool exists;
  2: bool isProgrammedToCpu;
  3: bool isMultiPath;
  4: bool isRouteUnresolvedToClassId;
  5: optional i32 classId;
  6: bool isProgrammedToDrop;
}

struct PortInfo {
  1: i32 loopbackMode;
}

// HW-side view of a port's UEC LLR binding (UE Spec 1.0.2 section 5.1).
// profileId is sourced from the SaiPortManager's port handle (the create-time
// adapter key), not from a port getAttribute -- the port-side LLR getters can
// return NOT_SUPPORTED and silently default on current SDK drops. The frame
// actions are read back from the profile object itself.
struct PortLlrInfo {
  1: bool hasProfile;
  2: i64 profileId;
  3: switch_config.LlrFrameAction initFrameAction;
  4: switch_config.LlrFrameAction flushFrameAction;
}

// HW-side view of a port's CBFC virtual channels (UE Spec 1.0.2 section 5.2).
// Sourced from SaiVirtualChannelManager's handle -- the create-time adapter
// keys -- rather than from the port's QOS_VIRTUAL_CHANNEL_LIST, which can
// return NOT_SUPPORTED and silently default on current SDK drops. The
// per-VC values are read back from the objects themselves.
struct VcInfo {
  1: i64 vcId;
  2: byte index;
  3: bool senderEnable;
  4: bool receiverEnable;
  5: bool hasCreditProfile;
  6: i64 creditProfileId;
  7: i64 reservedCreditSize;
  // Read-only, derived by hardware from the MMU carving. Nothing in FBOSS
  // sets these; reading them is the point of the test. Optional because SDK
  // support varies -- unset means the SDK refused the read, which is itself a
  // result worth reporting rather than an error.
  8: optional i64 receiverNativeCreditLimit;
}

// The sender credit limit is deliberately absent: brcm-sai 16.0_ea_odp rejects
// a GET of SAI_PORT_ATTR_CBFC_SENDER_CREDIT_LIMIT, so it can be written but
// never read back.
struct PortVcInfo {
  1: list<VcInfo> virtualChannels;
  2: optional i64 receiverNativeCreditSize;
  3: optional i64 receiverNativePacketOverhead;
  4: optional i64 receiverNativeTotalCredits;
  // Read from the SDK sender table, so they show what hardware holds.
  5: optional i64 senderCreditSize;
  6: optional i64 senderPacketOverhead;
  // The port's TC_TO_VC map read back from hardware; empty when no map is
  // bound, unset when the SDK refuses the read.
  7: optional map<i16, i16> trafficClassToVcId;
  // The port's QUEUE_TO_VC map read back from hardware; same conventions.
  8: optional map<i16, i16> queueToVcId;
}

struct AggPortInfo {
  1: bool isPresent;
  2: i32 numMembers;
  3: i32 numActiveMembers;
}

struct AclStatCountInfo {
  1: i32 aclEntryCount;
  2: i32 aclStatCount;
  3: i32 counterCount;
}

service AgentHwTestCtrl {
  // acl utils begin
  i32 getDefaultAclTableNumAclEntries();

  i32 getAclTableNumAclEntries(1: string name);

  bool isDefaultAclTableEnabled();

  bool isAclTableEnabled(1: string name);

  bool isAclEntrySame(
    1: switch_state.AclEntryFields aclEntry,
    2: string aclTableName,
  );

  bool areAllAclEntriesEnabled();

  bool isStatProgrammedInDefaultAclTable(
    1: list<string> aclEntryNames,
    2: string counterName,
    3: list<switch_config.CounterType> types,
  );

  bool isStatProgrammedInAclTable(
    1: list<string> aclEntryNames,
    2: string counterName,
    3: list<switch_config.CounterType> types,
    4: string tableName,
  );

  AclStatCountInfo getDefaultAclTableStatCountInfo();

  bool isAclStatDeleted(1: string statName);

  bool isMirrorProgrammed(1: switch_state.MirrorFields mirror);

  bool isPortMirrored(1: i32 port, 2: string mirror, 3: bool ingress);

  bool isPortSampled(1: i32 port, 2: string mirror, 3: bool ingress);

  bool isAclEntryMirrored(1: string aclEntry,2: string mirror,3: bool ingress,);

  bool verifyResolvedMirror(1: switch_state.MirrorFields mirror);

  bool verifyUnResolvedMirror(1: switch_state.MirrorFields mirror);

  bool verifyPortMirrorDestination(
    1: i32 port,
    2: i32 flags,
    3: i64 mirrorDestID,
  );

  bool verifyPortNoMirrorDestination(1: i32 port, 2: i32 flags);

  list<i64> getAllMirrorDestinations();

  bool isMirrorSflowTunnelEnabled(1: i64 destination);
  // neighbor utils
  NeighborInfo getNeighborInfo(1: ctrl.IfAndIP neighbor);

  i32 getHwEcmpSize(1: CIDRNetwork prefix, 2: i32 routerID, 3: i32 sizeInSw);
  map<i32, i32> getEcmpWeights(1: CIDRNetwork prefix, 2: i32 routerID);

  void injectFecError(1: list<i32> hwPorts, 2: bool injectCorrectable);

  void injectSwitchReachabilityChangeNotification();

  // route utils
  RouteInfo getRouteInfo(1: ctrl.IpPrefix prefix, 2: i32 routerID);
  bool isRouteHit(1: ctrl.IpPrefix prefix);
  void clearRouteHit(1: ctrl.IpPrefix prefix);
  bool isRouteToNexthop(
    1: ctrl.IpPrefix prefix,
    2: Address.BinaryAddress address,
    3: i32 routerID,
  );
  bool isProgrammedInHw(
    1: i32 intfID,
    2: ctrl.IpPrefix prefix,
    3: mpls.MplsLabelStack labelStack,
    4: i32 refCount,
  );

  // port utils
  list<PortInfo> getPortInfo(1: list<i32> portIds);
  PortLlrInfo getPortLlrInfo(1: i32 port);
  PortVcInfo getPortVcInfo(1: i32 port);
  bool verifyPortLedStatus(1: i32 port, 2: bool status);
  bool verifyPGSettings(1: i32 port, 2: bool pfcEnabled);

  list<AggPortInfo> getAggPortInfo(1: list<i32> aggPortIds);
  i32 getNumAggPorts();
  bool verifyPktFromAggPort(1: i32 aggPortId);

  //tam utils
  void triggerParityError();

  // AQM utils
  i32 getEgressSharedPoolLimitBytes();

  // print diag
  void printDiagCmd(1: string cmd);

  switch_config.SwitchingMode getFwdSwitchingMode(
    1: switch_state.RouteNextHopEntry routeNextHopEntry,
  );

  // PtcTc utils
  bool getPtpTcEnabled();

  // Switching mode utils
  i32 getSwitchingModeFromHw();

  void clearInterfacePhyCounters(1: list<i32> portIds);

  bool validateUdfConfig(1: string udfGroupName, 2: string udfPackeMatchName);
  bool validateRemoveUdfGroup(1: string udfGroupName, 2: i32 udfGroupId);
  bool validateRemoveUdfPacketMatcher(
    1: string udfPackeMatchName,
    2: i32 udfPacketMatcherId,
  );
  i32 getHwUdfGroupId(1: string udfGroupName);

  i32 getHwUdfPacketMatcherId(1: string udfPacketMatchName);
  bool validateUdfAclRoceOpcodeConfig(1: switch_state.SwitchState curState);
  bool validateUdfIdsInQset(1: i32 aclGroupId, 2: bool isSet);

  // PFC utils
  bool getPfcEnabled(1: i32 portId, 2: bool rx);
  bool pfcWatchdogProgrammingMatchesConfig(
    1: i32 portId,
    2: bool watchdogEnabled,
    3: switch_config.PfcWatchdog watchdog,
  );
  i32 getPfcWatchdogRecoveryAction(1: i32 portId);

  // Te flow utils
  i32 getNumTeFlowEntries();
  bool checkSwHwTeFlowMatch(1: switch_state.TeFlowEntryFields flowEntryFields);
  bool verifyEcmpForFlowletSwitchingHandler(
    1: CIDRNetwork ip,
    2: switch_state.SwitchSettingsFields settings,
    3: bool flowletEnable,
  );

  bool verifyPortFlowletConfig(
    1: CIDRNetwork prefix,
    2: switch_config.PortFlowletConfig cfg,
    3: bool flowletEnable,
  );

  bool validateFlowSetTable(1: bool expectFlowsetSizeZero);

  bool verifyEcmpForNonFlowlet(
    1: CIDRNetwork prefix,
    2: switch_state.SwitchSettingsFields settings,
    3: bool expectFlowsetFree,
  );

  // vlan utils
  map<i32, i32> getVlanToNumPorts();

  // acl table group utils
  bool isAclTableGroupEnabled(1: i32 aclStage);

  // port profile utils — returns list of mismatch descriptions (empty = pass)
  list<string> verifyPortProfile(
    1: i32 portId,
    2: switch_config.PortProfileID profileId,
    3: phy.ProfileSideConfig profileConfig,
    4: list<phy.PinConfig> pinConfigs,
  );

  phy.FecMode getPortFECMode(1: i32 portId);

  bool rxSignalDetectSupportedInSdk();
  bool rxLockStatusSupportedInSdk();
  bool pcsRxLinkStatusSupportedInSdk();
  bool fecAlignmentLockSupportedInSdk();

  // Log capture utils. Logs emitted HW-side (e.g. drop-reason WARNINGs) are
  // produced in the HwAgent process in multi-switch mode; these let a test
  // capture and read them over RPC, working in both mono and multi-switch.
  // installLogCapture() must be called before the log is emitted.
  void installLogCapture();
  list<string> getMatchingLogMessages(1: string substring);

  // fb303 cross-process utils for multi-switch testing
  map<string, i64> getFb303RegexCounters(1: string regex);
  i64 getFb303Counter(1: string key);
}
