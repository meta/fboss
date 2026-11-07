// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/single/MonolithicHwSwitchHandler.h"

#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/Platform.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/HwSwitchFb303Stats.h"

namespace facebook::fboss {

MonolithicHwSwitchHandler::MonolithicHwSwitchHandler(
    Platform* platform,
    const SwitchID& switchId,
    const cfg::SwitchInfo& info,
    SwSwitch* /*sw*/)
    : HwSwitchHandler(switchId, info),
      platform_(platform),
      hw_(platform_->getHwSwitch()) {}

void MonolithicHwSwitchHandler::exitFatal() const {
  return hw_->exitFatal();
}

std::unique_ptr<TxPacket> MonolithicHwSwitchHandler::allocatePacket(
    uint32_t size) const {
  return hw_->allocatePacket(size);
}

bool MonolithicHwSwitchHandler::sendPacketOutOfPortAsync(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queue) noexcept {
  return hw_->sendPacketOutOfPortAsync(std::move(pkt), portID, queue);
}

bool MonolithicHwSwitchHandler::sendPacketSwitchedSync(
    std::unique_ptr<TxPacket> pkt) noexcept {
  return hw_->sendPacketSwitchedSync(std::move(pkt));
}

bool MonolithicHwSwitchHandler::sendPacketSwitchedAsync(
    std::unique_ptr<TxPacket> pkt) noexcept {
  return hw_->sendPacketSwitchedAsync(std::move(pkt));
}

bool MonolithicHwSwitchHandler::sendPacketOutOfPortSyncForPktType(
    std::unique_ptr<TxPacket> pkt,
    const PortID& portID,
    PacketType packetType) noexcept {
  return hw_->sendPacketOutOfPortSyncForPktType(
      std::move(pkt), portID, packetType);
}

bool MonolithicHwSwitchHandler::isValidStateUpdate(
    const StateDelta& delta) const {
  return hw_->isValidStateUpdate(delta);
}

void MonolithicHwSwitchHandler::unregisterCallbacks() {
  hw_->unregisterCallbacks();
}

void MonolithicHwSwitchHandler::gracefulExit() {
  hw_->gracefulExit();
}

folly::dynamic MonolithicHwSwitchHandler::toFollyDynamic() const {
  return hw_->toFollyDynamic();
}

void MonolithicHwSwitchHandler::onHwInitialized(HwSwitchCallback* callback) {
  platform_->onHwInitialized(callback);
}

bool MonolithicHwSwitchHandler::transactionsSupported(
    std::optional<cfg::SdkVersion> /*sdkVersion*/) const {
  // TODO use sdk version to determine if transactions are supported
  // This can be done after sdk version is populated in all tests
  return hw_->transactionsSupported();
}

folly::F14FastMap<std::string, HwPortStats>
MonolithicHwSwitchHandler::getPortStats() const {
  return hw_->getPortStats();
}

std::map<std::string, HwSysPortStats>
MonolithicHwSwitchHandler::getSysPortStats() const {
  return hw_->getSysPortStats();
}

void MonolithicHwSwitchHandler::updateStats() {
  return hw_->updateStats();
}

HwSwitchDropStats MonolithicHwSwitchHandler::getSwitchDropStats() const {
  return hw_->getSwitchDropStats();
}

HwSwitchWatermarkStats MonolithicHwSwitchHandler::getSwitchWatermarkStats()
    const {
  return hw_->getSwitchWatermarkStats();
}

HwSwitchPipelineStats MonolithicHwSwitchHandler::getSwitchPipelineStats()
    const {
  return hw_->getSwitchPipelineStats();
}
HwSwitchTemperatureStats MonolithicHwSwitchHandler::getSwitchTemperatureStats()
    const {
  return hw_->getSwitchTemperatureStats();
}

HwSwitchHardResetStats MonolithicHwSwitchHandler::getHwSwitchHardResetStats()
    const {
  return hw_->getHwSwitchHardResetStats();
}

void MonolithicHwSwitchHandler::updateAllPhyInfo() {
  hw_->updateAllPhyInfo();
}

std::map<PortID, phy::PhyInfo> MonolithicHwSwitchHandler::getAllPhyInfo()
    const {
  return hw_->getAllPhyInfo();
}

uint64_t MonolithicHwSwitchHandler::getDeviceWatermarkBytes() const {
  return hw_->getDeviceWatermarkBytes();
}

HwFlowletStats MonolithicHwSwitchHandler::getHwFlowletStats() const {
  return hw_->getHwFlowletStats();
}

HwSwitchFb303Stats* MonolithicHwSwitchHandler::getSwitchStats() const {
  return hw_->getSwitchStats();
}

void MonolithicHwSwitchHandler::clearPortStats(
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  hw_->clearPortStats(ports);
}

std::vector<phy::PrbsLaneStats> MonolithicHwSwitchHandler::getPortAsicPrbsStats(
    PortID portId) {
  return hw_->getPortAsicPrbsStats(portId);
}

void MonolithicHwSwitchHandler::clearPortAsicPrbsStats(PortID portId) {
  hw_->clearPortAsicPrbsStats(portId);
}

std::vector<prbs::PrbsPolynomial>
MonolithicHwSwitchHandler::getPortPrbsPolynomials(int32_t portId) {
  return hw_->getPortPrbsPolynomials(portId);
}

prbs::InterfacePrbsState MonolithicHwSwitchHandler::getPortPrbsState(
    PortID portId) {
  return hw_->getPortPrbsState(portId);
}

void MonolithicHwSwitchHandler::switchRunStateChanged(SwitchRunState newState) {
  hw_->switchRunStateChanged(newState);
}

std::vector<EcmpDetails> MonolithicHwSwitchHandler::getAllEcmpDetails() const {
  return hw_->getAllEcmpDetails();
}

cfg::SwitchingMode MonolithicHwSwitchHandler::getFwdSwitchingMode(
    const RouteNextHopEntry& fwd) {
  return hw_->getFwdSwitchingMode(fwd);
}

CpuPortStats MonolithicHwSwitchHandler::getCpuPortStats() const {
  return hw_->getCpuPortStats();
}

std::map<PortID, FabricEndpoint>
MonolithicHwSwitchHandler::getFabricConnectivity() const {
  return hw_->getFabricConnectivity();
}

std::vector<PortID> MonolithicHwSwitchHandler::getSwitchReachability(
    SwitchID switchId) const {
  return hw_->getSwitchReachability(switchId);
}

std::string MonolithicHwSwitchHandler::getDebugDump() const {
  return hw_->getDebugDump();
}

void MonolithicHwSwitchHandler::setSdkRegDumpEnabled(bool enabled) {
  hw_->setSdkRegDumpEnabled(enabled);
}

void MonolithicHwSwitchHandler::fetchL2Table(
    std::vector<L2EntryThrift>* l2Table,
    bool sdk) const {
  hw_->fetchL2Table(l2Table, sdk);
}

std::string MonolithicHwSwitchHandler::listObjects(
    const std::vector<HwObjectType>& types,
    bool cached) const {
  return hw_->listObjects(types, cached);
}

FabricReachabilityStats MonolithicHwSwitchHandler::getFabricReachabilityStats()
    const {
  return hw_->getFabricReachabilityStats();
}

bool MonolithicHwSwitchHandler::needL2EntryForNeighbor(
    const cfg::SwitchConfig* /*config*/) const {
  return hw_->needL2EntryForNeighbor();
}

void MonolithicHwSwitchHandler::initialStateApplied() {
  hw_->initialStateApplied();
}

std::pair<fsdb::OperDelta, HwSwitchStateUpdateStatus>
MonolithicHwSwitchHandler::stateChanged(
    std::vector<fsdb::OperDelta> deltas,
    bool transaction,
    const std::shared_ptr<SwitchState>& /*oldState*/,
    const std::shared_ptr<SwitchState>& /*newState*/,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  auto operResult = transaction
      ? hw_->stateChangedTransaction(
            deltas,
            HwWriteBehaviorRAII(hwWriteBehavior),
            deltaApplicationBehavior)
      : hw_->stateChanged(deltas, HwWriteBehaviorRAII(hwWriteBehavior));
  /*
   * For monolithic, return success for update since SwSwitch should not
   * do rollback for partial update failure. In monolithic SwSwitch
   * transitions the state returned by HwSwitch to applied state.
   */
  return {
      operResult, HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_SUCCEEDED};
}

multiswitch::StateOperDelta MonolithicHwSwitchHandler::getNextStateOperDelta(
    std::unique_ptr<multiswitch::StateOperDelta> /*prevOperResult*/,
    int64_t /*lastUpdateSeqNum*/) {
  throw FbossError("Not supported");
}

// no action to take for monolithic
void MonolithicHwSwitchHandler::notifyHwSwitchDisconnected() {}

SwitchRunState MonolithicHwSwitchHandler::getHwSwitchRunState() {
  return hw_->getRunState();
}

AclStats MonolithicHwSwitchHandler::getAclStats() const {
  return hw_->getAclStats();
}

void MonolithicHwSwitchHandler::getHwStats(
    multiswitch::HwSwitchStats& hwStats) const {
  auto now = duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch());
  hwStats.timestamp() = now.count();

  hwStats.hwPortStats() = getPortStats();
  hwStats.hwTrunkStats() = hw_->getTrunkStats();
  hwStats.sysPortStats() = getSysPortStats();
  hwStats.switchDropStats() = getSwitchDropStats();
  hwStats.switchDropBitmapStats() = hw_->getSwitchDropBitmapStats();
  hwStats.fabricReachabilityStats() = getFabricReachabilityStats();
  hwStats.switchWatermarkStats() = getSwitchWatermarkStats();
  hwStats.switchPipelineStats() = getSwitchPipelineStats();
  hwStats.switchTemperatureStats() = getSwitchTemperatureStats();

  if (auto hwSwitchStats = getSwitchStats()) {
    hwStats.hwAsicErrors() = hwSwitchStats->getHwAsicErrors();
  }
  for (auto& [portId, phyInfoPerPort] : getAllPhyInfo()) {
    hwStats.phyInfo()->emplace(portId, phyInfoPerPort);
  }
  hwStats.flowletStats() = getHwFlowletStats();
  hwStats.cpuPortStats() = getCpuPortStats();
  hwStats.aclStats() = getAclStats();
  // update global stats
  hwStats.fb303GlobalStats() = hw_->getSwitchStats()->getAllFb303Stats();
  hwStats.hwResourceStats() = hw_->getResourceStats();
  hwStats.arsExhausted() = hw_->getArsExhaustionStatus();
  hwStats.sysPortShelState() = hw_->getSysPortShelState();
  hwStats.hwRouterInterfaceStats() = hw_->getRouterInterfaceStats();
  hwStats.hardResetStats() = hw_->getHwSwitchHardResetStats();
  hwStats.counterStats() = hw_->getHwSwitchCounterStats();
}

} // namespace facebook::fboss
