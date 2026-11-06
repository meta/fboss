// Copyright 2004-present Facebook. All Rights Reserved.
#pragma once

#include <folly/futures/Future.h>

#include "common/fb303/cpp/FacebookBase2.h"

#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"
#include "fboss/mka_service/handlers/MacsecHandler.h"
#include "fboss/qsfp_service/PortManager.h"
#include "fboss/qsfp_service/TransceiverManager.h"
#include "fboss/qsfp_service/diag/PaiDiagShell.h"
#include "fboss/qsfp_service/fsdb/QsfpFsdbSubscriber.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

DECLARE_string(sak_list_warmboot_config);
DECLARE_int32(phy_service_macsec_port);
DECLARE_bool(port_manager_mode);

namespace facebook {
namespace fboss {

class QsfpServiceHandler
    : public facebook::fboss::QsfpServiceSvIf,
      public ::facebook::fb303::FacebookBase2DeprecationMigration {
 public:
  QsfpServiceHandler(
      std::unique_ptr<TransceiverManager> tcvrManager,
      std::unique_ptr<PortManager> portManager,
      std::shared_ptr<mka::MacsecHandler> handler);
  ~QsfpServiceHandler() override;

  void init();
  facebook::fb303::cpp2::fb_status getStatus() override;
  TransceiverType getType(int32_t idx) override;
  /*
   * Returns all qsfp information for the transceiver
   */
  void getTransceiverInfo(
      std::map<int32_t, TransceiverInfo>& info,
      std::unique_ptr<std::vector<int32_t>> ids) override;

  /*
   * Returns all qsfp information for the transceiver
   */
  void getPortStateMachineState(
      std::map<int32_t, PortStateMachineState>& info,
      std::unique_ptr<std::vector<int32_t>> ids) override;

  /*
   * Returns all qsfp information for the transceiver
   */
  void getPortStateMachineStateFromPortNames(
      std::map<std::string, PortStateMachineState>& info,
      std::unique_ptr<std::vector<std::string>> portNames) override;

  /*
   * Returns portName to configured media interface
   */
  void getPortMediaInterface(
      std::map<std::string, MediaInterfaceCode>& portMediaInterface) override;

  /*
   * Returns qsfp config validation information for a transceiver.
   *
   * When getConfigString is true, the map is populated with stringified JSONs
   * of non-validated transceiver configurations. Otherwise, when
   * getConfigString is false, the map is populated with strings representing
   * the reason validation failed as applicable.
   */
  void getTransceiverConfigValidationInfo(
      std::map<int32_t, std::string>& info,
      std::unique_ptr<std::vector<int32_t>> ids,
      bool getConfigString) override;

  /*
   * Returns raw DOM page data for each passed in transceiver.
   */
  void getTransceiverRawDOMData(
      std::map<int32_t, RawDOMData>& info,
      std::unique_ptr<std::vector<int32_t>> ids) override;

  /*
   * Returns a union of the two raw DOM data format for each passed in
   * transceiver.
   */
  void getTransceiverDOMDataUnion(
      std::map<int32_t, DOMDataUnion>& info,
      std::unique_ptr<std::vector<int32_t>> ids) override;

  /*
   * Store port status information and return relevant transceiver map.
   */
  void syncPorts(
      std::map<int32_t, TransceiverInfo>& info,
      std::unique_ptr<std::map<int32_t, PortStatus>> ports) override;

  /*
   * Return a pointer to the transceiver manager.
   */
  TransceiverManager* getTransceiverManager() const {
    return tcvrManager_.get();
  }

  PortManager* getPortManager() const {
    return portManager_.get();
  }

  void resetTransceiver(
      std::unique_ptr<std::vector<std::string>> portNames,
      ResetType resetType,
      ResetAction resetAction) override;

  void pauseRemediation(
      int32_t timeout,
      std::unique_ptr<std::vector<std::string>> portList) override;

  void unpauseRemediation(
      std::unique_ptr<std::vector<std::string>> portList) override;

  void getRemediationUntilTime(
      std::map<std::string, int32_t>& info,
      std::unique_ptr<std::vector<std::string>> portList) override;

  void readTransceiverRegister(
      std::map<int32_t, ReadResponse>& response,
      std::unique_ptr<ReadRequest> request) override;

  void writeTransceiverRegister(
      std::map<int32_t, WriteResponse>& response,
      std::unique_ptr<WriteRequest> request) override;

  void getPortTransceiverIDs(
      std::map<std::string, std::vector<int32_t>>& portTransceiverIds) override;

  void getTransceiverInfoByPortName(
      std::map<std::string, TransceiverInfo>& info,
      std::unique_ptr<std::vector<std::string>> portNames) override;

  /*
   * Thrift call servicing routine for programming one PHY port
   */
  void programXphyPort(int32_t portId, cfg::PortProfileID portProfileId)
      override;

  void getXphyInfo(phy::PhyInfo& response, int32_t portID) override;

  /*
   * Handle: PortID
   * Change the PRBS setting on a port. Useful when debugging a link
   * down or flapping issue.
   */
  void setPortPrbs(
      int32_t portId,
      phy::PortComponent component,
      std::unique_ptr<phy::PortPrbsState> state) override;

  /*
   * Handle: Interface name
   * Change the PRBS setting on a port. Useful when debugging a link
   * down or flapping issue.
   */
  void setInterfacePrbs(
      std::unique_ptr<std::string> portName,
      phy::PortComponent component,
      std::unique_ptr<prbs::InterfacePrbsState> state) override;

  /*
   * Get the PRBS state on a port
   */
  void getInterfacePrbsState(
      prbs::InterfacePrbsState& prbsState,
      std::unique_ptr<std::string> portName,
      phy::PortComponent component) override;

  /*
   * Get the PRBS settings on all interfaces.
   */
  void getAllInterfacePrbsStates(
      std::map<std::string, prbs::InterfacePrbsState>& prbsStates,
      phy::PortComponent component) override;

  /*
   * Get the PRBS stats on an interface. Useful when debugging a link
   * down or flapping issue.
   */
  void getInterfacePrbsStats(
      phy::PrbsStats& response,
      std::unique_ptr<std::string> portName,
      phy::PortComponent component) override;

  /*
   * Get the PRBS stats on all interfaces.
   */
  void getAllInterfacePrbsStats(
      std::map<std::string, phy::PrbsStats>& prbsStats,
      phy::PortComponent component) override;

  /*
   * Get the PRBS stats on a port. Useful when debugging a link
   * down or flapping issue.
   */
  void getPortPrbsStats(
      phy::PrbsStats& response,
      int32_t portId,
      phy::PortComponent component) override;

  void clearInterfacePrbsStats(
      std::unique_ptr<std::string> portName,
      phy::PortComponent component) override;

  void bulkClearInterfacePrbsStats(
      std::unique_ptr<std::vector<std::string>> interfaces,
      phy::PortComponent component) override;

  void dumpTransceiverI2cLog(std::unique_ptr<std::string> portName) override;

  void getPortsRequiringOpticsFwUpgrade(
      std::map<std::string, FirmwareUpgradeData>& ports) override;

  void triggerAllOpticsFwUpgrade(
      std::map<std::string, FirmwareUpgradeData>& ports) override;

  void triggerOpticsFwUpgrade(
      std::map<std::string, FirmwareUpgradeData>& ports,
      std::unique_ptr<std::vector<std::string>> interfaces) override;

  /*
   * Get the list of supported PRBS polynomials for the given port and
   * prbs component
   */
  void getSupportedPrbsPolynomials(
      std::vector<prbs::PrbsPolynomial>& prbsCapabilities,
      std::unique_ptr<std::string> portName,
      phy::PortComponent component) override;

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
  void clearPortPrbsStats(int32_t portId, phy::PortComponent component)
      override;

  void getMacsecCapablePorts(std::vector<int32_t>& ports) override;

  void listHwObjects(
      std::string& out,
      std::unique_ptr<std::vector<HwObjectType>> hwObjects,
      bool cached) override;

  bool getSdkState(std::unique_ptr<std::string> fileName) override;

  void getPortInfo(std::string& out, std::unique_ptr<std::string> portName)
      override;

  void setPortLoopbackState(
      std::unique_ptr<std::string> portName,
      phy::PortComponent component,
      bool setLoopback,
      phy::LoopbackMode mode) override;

  void setPortAdminState(
      std::unique_ptr<std::string> portName,
      phy::PortComponent component,
      bool setAdminUp) override;

  void setInterfaceTxRx(
      std::vector<phy::TxRxEnableResponse>& txRxEnableResponse,
      std::unique_ptr<std::vector<phy::TxRxEnableRequest>> txRxEnableRequests)
      override;

  void saiPhyRegisterAccess(
      std::string& out,
      std::unique_ptr<std::string> portName,
      bool opRead,
      int phyAddr,
      int devId,
      int regOffset,
      int data) override;

  void saiPhySerdesRegisterAccess(
      std::string& out,
      std::unique_ptr<std::string> portName,
      bool opRead,
      int16_t mdioAddr,
      phy::Side side,
      int serdesLane,
      int64_t regOffset,
      int64_t data) override;

  /**
   * Start a streaming PAI Diag Shell session for Broadcom Agera3 retimers.
   * Mirrors SaiHandler::startDiagShell. Returns the first prompt + a stream
   * of subsequent shell output. Thread-safe: only one client at a time.
   * Use the PAI shell's `s <switch_id>` command to switch chip context.
   */
  apache::thrift::ResponseAndServerStream<std::string, std::string>
  startPaiDiagShell() override;

  /**
   * Send keystrokes/commands to an active streaming PAI Diag Shell session.
   */
  void producePaiDiagShellInput(
      std::unique_ptr<std::string> input,
      std::unique_ptr<ClientInformation> client) override;

  /**
   * One-shot PAI diag command: connect, send command, read output, disconnect.
   */
  void paiDiagCmd(
      std::string& result,
      std::unique_ptr<std::string> input,
      std::unique_ptr<ClientInformation> client) override;

  void phyConfigCheckHw(std::string& out, std::unique_ptr<std::string> portName)
      override;

  void publishLinkSnapshots(
      std::unique_ptr<std::vector<std::string>> portNames) override;

  void getInterfacePhyInfo(
      std::map<std::string, phy::PhyInfo>& phyInfos,
      std::unique_ptr<std::vector<std::string>> portNames) override;

  void getAllInterfacePhyInfo(
      std::map<std::string, phy::PhyInfo>& phyInfos) override;

  void getSymbolErrorHistogram(
      CdbDatapathSymErrHistogram& symErr,
      std::unique_ptr<std::string> portName) override;

  void getAllPortSupportedProfiles(
      std::map<std::string, std::vector<cfg::PortProfileID>>&
          supportedPortProfiles,
      bool checkOptics) override;

  void refreshStateMachines();

  void gracefulExit();

  PhyManager* getPhyManager() const;

  std::optional<std::string> getPortNameByPortId(const PortID& portId) const;

  void setOverrideAgentPortStatusForTesting(
      bool up,
      bool enabled,
      bool clearOnly = false);

  void setOverrideAgentConfigAppliedInfoForTesting(ConfigAppliedInfo config);

  std::optional<PortID> getPortIdByPortName(
      const std::string& portNameStr) const;

  void programXphyPort(PortID portId, cfg::PortProfileID portProfileId);

  void updateAllXphyPortsStats();

  void programXphyPortPrbs(
      PortID portID,
      phy::Side side,
      const phy::PortPrbsState& prbs);

  phy::PortPrbsState getXphyPortPrbs(const PortID& portId, phy::Side side);

#if FOLLY_HAS_COROUTINES
  folly::coro::Task<bool> co_sakInstallRx(
      std::unique_ptr<mka::MKASak> sak,
      std::unique_ptr<mka::MKASci> sciToAdd) override;

  folly::coro::Task<bool> co_sakInstallTx(
      std::unique_ptr<mka::MKASak> sak) override;

  folly::coro::Task<bool> co_sakDeleteRx(
      std::unique_ptr<mka::MKASak> sak,
      std::unique_ptr<mka::MKASci> sciToRemove) override;

  folly::coro::Task<bool> co_sakDelete(
      std::unique_ptr<mka::MKASak> sak) override;

  folly::coro::Task<std::unique_ptr<mka::MKASakHealthResponse>>
  co_sakHealthCheck(std::unique_ptr<mka::MKASak> sak) override;

  folly::coro::Task<std::unique_ptr<mka::MacsecPortPhyMap>>
  co_macsecGetPhyPortInfo(
      std::unique_ptr<std::vector<std::string>> portNames) override;

  folly::coro::Task<PortOperState> co_macsecGetPhyLinkInfo(
      std::unique_ptr<std::string> portName) override;

  folly::coro::Task<std::unique_ptr<phy::PhyInfo>> co_getPhyInfo(
      std::unique_ptr<std::string> portName) override;

  folly::coro::Task<bool> co_deleteAllSc(
      std::unique_ptr<std::string> portName) override;

  folly::coro::Task<bool> co_setupMacsecState(
      std::unique_ptr<std::vector<std::string>> portList,
      bool macsecDesired,
      bool dropUnencrypted) override;

  folly::coro::Task<std::unique_ptr<std::map<std::string, MacsecStats>>>
  co_getAllMacsecPortStats(bool readFromHw) override;

  folly::coro::Task<std::unique_ptr<std::map<std::string, MacsecStats>>>
  co_getMacsecPortStats(
      std::unique_ptr<std::vector<std::string>> portNames,
      bool readFromHw) override;

#endif

  QsfpServiceRunState getQsfpServiceRunState() override;

 private:
  // Forbidden copy constructor and assignment operator
  QsfpServiceHandler(QsfpServiceHandler const&) = delete;
  QsfpServiceHandler& operator=(QsfpServiceHandler const&) = delete;

  void validateHandler() const;

  std::unique_ptr<TransceiverManager> tcvrManager_{nullptr};
  std::unique_ptr<PortManager> portManager_{nullptr};
  std::shared_ptr<mka::MacsecHandler> macsecHandler_;

  std::unique_ptr<QsfpFsdbSubscriber> fsdbSubscriber_;

  // PAI Diag Shell access for retimer chips. Lazy-initialized on first use,
  // because we need a SaiPhyManager-owned SaiSwitch to be ready first.
  // Only one shell session at a time across the process; switch chip context
  // with the PAI shell's `s <switch_id>` command.
  void ensurePaiDiagShell();
  std::unique_ptr<StreamingPaiDiagShell> paiDiagShell_;
  std::unique_ptr<PaiDiagCmdServer> paiDiagCmdServer_;
  std::mutex paiDiagCmdLock_;
  std::mutex paiDiagInitLock_;
};
} // namespace fboss
} // namespace facebook
