/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/EnumUtils.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/LlrConfig.h"
#include "fboss/agent/state/PortFlowletConfig.h"
#include "fboss/agent/state/PortPgConfig.h"
#include "fboss/agent/state/PortQueue.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/mka_service/if/gen-cpp2/mka_structs_types.h"

#include <map>
#include <string>
#include <vector>

namespace facebook::fboss {

class SwitchState;

using PortFlowletCfgPtr = std::shared_ptr<PortFlowletCfg>;
using LlrConfigPtr = std::shared_ptr<LlrConfig>;

struct PortFields {
  struct VlanInfo {
    explicit VlanInfo(bool emitTags, bool emitPriorityTags)
        : tagged(emitTags), priorityTagged(emitPriorityTags) {}
    bool operator==(const VlanInfo& other) const {
      return tagged == other.tagged && priorityTagged == other.priorityTagged;
    }
    bool operator!=(const VlanInfo& other) const {
      return !(*this == other);
    }
    state::VlanInfo toThrift() const;
    static VlanInfo fromThrift(state::VlanInfo const&);
    bool tagged;
    bool priorityTagged;
  };

  using VlanMembership = std::map<VlanID, VlanInfo>;
  using LLDPValidations = std::map<cfg::LLDPTag, std::string>;
  using NeighborReachability = std::vector<cfg::PortNeighbor>;

  enum class OperState {
    DOWN = 0,
    UP = 1,
  };

  enum class ActiveState {
    INACTIVE = 0,
    ACTIVE = 1,
  };

  struct MKASakKey {
    bool operator<(const MKASakKey& r) const {
      return std::tie(*sci.macAddress(), *sci.port(), associationNum) <
          std::tie(*r.sci.macAddress(), *r.sci.port(), r.associationNum);
    }
    bool operator==(const MKASakKey& r) const {
      return std::tie(*sci.macAddress(), *sci.port(), associationNum) ==
          std::tie(*r.sci.macAddress(), *r.sci.port(), r.associationNum);
    }
    mka::MKASci sci;
    int associationNum;
  };
  using RxSaks = std::map<MKASakKey, mka::MKASak>;

  static state::RxSak rxSakToThrift(
      const MKASakKey& sakKey,
      const mka::MKASak& sak);
  static std::pair<MKASakKey, mka::MKASak> rxSakFromThrift(state::RxSak rxSak);
};

USE_THRIFT_COW(Port);

RESOLVE_STRUCT_MEMBER(Port, switch_state_tags::flowletConfig, PortFlowletCfg);
RESOLVE_STRUCT_MEMBER(Port, switch_state_tags::llrConfig, LlrConfig);
/*
 * Port stores state about one of the physical ports on the switch.
 */
class Port : public ThriftStructNode<Port, state::PortFields> {
 public:
  using BaseT = ThriftStructNode<Port, state::PortFields>;
  using BaseT::modify;
  using LegacyFields = PortFields;
  using VlanInfo = PortFields::VlanInfo;
  using VlanMembership = PortFields::VlanMembership;
  using OperState = PortFields::OperState;
  using ActiveState = PortFields::ActiveState;
  using LLDPValidations = PortFields::LLDPValidations;
  using NeighborReachability = PortFields::NeighborReachability;
  using MKASakKey = PortFields::MKASakKey;
  using RxSaks = PortFields::RxSaks;
  using PfcPriorityList = thrift_cow::detail::ReferenceWrapper<
      const std::shared_ptr<thrift_cow::ThriftListNode<
          apache::thrift::type_class::list<
              apache::thrift::type_class::integral>,
          std::vector<short>>>>;

  Port(PortID id, const std::string& name);

  PortID getID() const {
    return PortID(cref<switch_state_tags::portId>()->cref());
  }

  const std::string& getName() const {
    return cref<switch_state_tags::portName>()->cref();
  }

  void setName(const std::string& name) {
    set<switch_state_tags::portName>(name);
  }

  const std::string& getDescription() const {
    return cref<switch_state_tags::portDescription>()->cref();
  }

  void setDescription(const std::string& description) {
    set<switch_state_tags::portDescription>(description);
  }

  phy::PortPrbsState getAsicPrbs() const {
    // THRIFT_COPY
    return cref<switch_state_tags::asicPrbs>()->toThrift();
  }

  void setAsicPrbs(phy::PortPrbsState prbsState) {
    set<switch_state_tags::asicPrbs>(prbsState);
  }

  phy::PortPrbsState getGbSystemPrbs() const {
    // THRIFT_COPY
    return cref<switch_state_tags::gbSystemPrbs>()->toThrift();
  }

  void setGbSystemPrbs(phy::PortPrbsState prbsState) {
    set<switch_state_tags::gbSystemPrbs>(prbsState);
  }

  phy::PortPrbsState getGbLinePrbs() const {
    // THRIFT_COPY
    return cref<switch_state_tags::gbLinePrbs>()->toThrift();
  }

  void setGbLinePrbs(phy::PortPrbsState prbsState) {
    set<switch_state_tags::gbLinePrbs>(prbsState);
  }

  cfg::PortState getAdminState() const {
    return nameToEnum<cfg::PortState>(
        cref<switch_state_tags::portState>()->cref());
  }

  cfg::PortType getPortType() const {
    return cref<switch_state_tags::portType>()->cref();
  }
  void setPortType(cfg::PortType portType) {
    set<switch_state_tags::portType>(portType);
  }

  cfg::PortDrainState getPortDrainState() const {
    return cref<switch_state_tags::drainState>()->cref();
  }
  void setPortDrainState(cfg::PortDrainState drainState) {
    set<switch_state_tags::drainState>(drainState);
  }

  void setAdminState(cfg::PortState adminState) {
    set<switch_state_tags::portState>(enumToName(adminState));
  }

  std::optional<phy::LinkFaultStatus> getIPhyLinkFaultStatus() const {
    if (auto iPhyLinkFaultStatus =
            cref<switch_state_tags::iPhyLinkFaultStatus>()) {
      // THRIFT_COPY
      return iPhyLinkFaultStatus->toThrift();
    }
    return std::nullopt;
  }

  void setIPhyLinkFaultStatus(std::optional<phy::LinkFaultStatus> faultStatus) {
    if (!faultStatus) {
      ref<switch_state_tags::iPhyLinkFaultStatus>().reset();
      return;
    }
    set<switch_state_tags::iPhyLinkFaultStatus>(faultStatus.value());
  }

  OperState getOperState() const {
    return cref<switch_state_tags::portOperState>()->cref() ? OperState::UP
                                                            : OperState::DOWN;
  }

  void setOperState(bool isUp) {
    set<switch_state_tags::portOperState>(isUp);
  }

  std::optional<ActiveState> getActiveState() const {
    if (auto portActiveState = cref<switch_state_tags::portActiveState>()) {
      return portActiveState->cref() ? ActiveState::ACTIVE
                                     : ActiveState::INACTIVE;
    }

    return std::nullopt;
  }

  void setActiveState(std::optional<bool> isActive) {
    if (!isActive) {
      ref<switch_state_tags::portActiveState>().reset();
      return;
    }
    set<switch_state_tags::portActiveState>(isActive.value());
  }

  bool isEnabled() const {
    return getAdminState() == cfg::PortState::ENABLED;
  }

  bool isDrained() const {
    return getPortDrainState() == cfg::PortDrainState::DRAINED;
  }

  bool isUp() const {
    return cref<switch_state_tags::portOperState>()->cref();
  }

  std::optional<bool> isActive() const {
    if (auto portActiveState = cref<switch_state_tags::portActiveState>()) {
      return portActiveState->cref();
    }
    return std::nullopt;
  }

  std::optional<mka::MKASak> getTxSak() const {
    if (auto txSak = cref<switch_state_tags::txSecureAssociationKey>()) {
      // THRIFT_COPY
      return txSak->toThrift();
    }
    return std::nullopt;
  }
  void setTxSak(std::optional<mka::MKASak> txSak) {
    if (!txSak) {
      ref<switch_state_tags::txSecureAssociationKey>().reset();
      return;
    }
    set<switch_state_tags::txSecureAssociationKey>(txSak.value());
  }
  // THRIFT_COPY
  RxSaks getRxSaksMap() const {
    RxSaks rxSecureAssociationKeys;
    for (const auto& rxSak :
         *(safe_cref<switch_state_tags::rxSecureAssociationKeys>())) {
      rxSecureAssociationKeys.emplace(
          PortFields::rxSakFromThrift(rxSak->toThrift()));
    }
    return rxSecureAssociationKeys;
  }
  void setRxSaksMap(const RxSaks& rxSecureAssociationKeys) {
    std::vector<state::RxSak> rxSaks{};
    for (const auto& [mkaSakKey, mkaSak] : rxSecureAssociationKeys) {
      rxSaks.push_back(PortFields::rxSakToThrift(mkaSakKey, mkaSak));
    }
    setRxSaks(rxSaks);
  }

  bool getMacsecDesired() const {
    return cref<switch_state_tags::macsecDesired>()->cref();
  }
  void setMacsecDesired(bool macsecDesired) {
    set<switch_state_tags::macsecDesired>(macsecDesired);
    if (!macsecDesired) {
      setRxSaksMap({});
      setTxSak(std::nullopt);
    }
  }

  bool getDropUnencrypted() const {
    return cref<switch_state_tags::dropUnencrypted>()->cref();
  }
  void setDropUnencrypted(bool dropUnencrypted) {
    set<switch_state_tags::dropUnencrypted>(dropUnencrypted);
  }

  /**
   * Tells you Oper+Admin state of port. Will be UP only if both admin and
   * oper state is UP.
   */
  bool isPortUp() const {
    return isEnabled() && isUp();
  }

  VlanMembership getVlans() const {
    // TODO(zecheng): vlanMembership are stored as map<string, VlanInfo>, which
    // is not ideal since we should use vlanId.
    // Keeping the interface same for now, until vlanMembership is moved to
    // map<VlanId, VlanInfo>.
    VlanMembership vlanMembership;
    // THRIFT_COPY
    for (const auto& [vlanString, vlanInfo] :
         cref<switch_state_tags::vlanMemberShips>()->toThrift()) {
      vlanMembership.emplace(
          VlanID(folly::to<uint32_t>(vlanString)),
          VlanInfo::fromThrift(vlanInfo));
    }
    return vlanMembership;
  }
  void setVlans(const VlanMembership& vlans) {
    std::map<std::string, state::VlanInfo> vlanMembership;
    for (const auto& [vlanId, vlanInfo] : vlans) {
      vlanMembership.emplace(
          folly::to<std::string>(vlanId), vlanInfo.toThrift());
    }
    set<switch_state_tags::vlanMemberShips>(vlanMembership);
  }

  void addVlan(VlanID id, bool tagged, bool priorityTagged) {
    auto vlanMembership = getVlans();
    vlanMembership.emplace(
        std::make_pair(id, VlanInfo(tagged, priorityTagged)));
    setVlans(vlanMembership);
  }

  auto getPortQueues() const {
    return safe_cref<switch_state_tags::queues>();
  }
  void resetPortQueues(QueueConfig& queues) {
    // TODO(zecheng): change type to ThriftListNode
    std::vector<PortQueueFields> queuesThrift{};
    for (const auto& queue : queues) {
      queuesThrift.push_back(queue->toThrift());
    }
    set<switch_state_tags::queues>(std::move(queuesThrift));
  }

  auto getPortPgConfigs() const {
    return safe_cref<switch_state_tags::pgConfigs>();
  }

  void resetPgConfigs(std::optional<PortPgConfigs>& pgConfigs) {
    if (!pgConfigs) {
      safe_ref<switch_state_tags::pgConfigs>().reset();
      return;
    }
    std::vector<state::PortPgFields> pgConfigThrift{};
    for (const auto& pgConfig : pgConfigs.value()) {
      pgConfigThrift.push_back(pgConfig->toThrift());
    }
    set<switch_state_tags::pgConfigs>(std::move(pgConfigThrift));
  }

  void resetPgConfigs(std::vector<state::PortPgFields> pgConfigs) {
    set<switch_state_tags::pgConfigs>(pgConfigs);
  }

  auto getVirtualChannels() const {
    return safe_cref<switch_state_tags::virtualChannels>();
  }

  void setVirtualChannels(
      const std::optional<std::vector<state::PortVcFields>>& virtualChannels) {
    if (!virtualChannels) {
      ref<switch_state_tags::virtualChannels>().reset();
      return;
    }
    set<switch_state_tags::virtualChannels>(*virtualChannels);
  }

  std::optional<std::string> getCbfcConfigName() const {
    if (auto name = cref<switch_state_tags::cbfcConfigName>()) {
      return name->toThrift();
    }
    return std::nullopt;
  }

  void setCbfcConfigName(const std::optional<std::string>& name) {
    if (!name) {
      ref<switch_state_tags::cbfcConfigName>().reset();
      return;
    }
    set<switch_state_tags::cbfcConfigName>(name.value());
  }

  std::optional<int64_t> getCbfcSenderCreditLimit() const {
    if (auto limit = cref<switch_state_tags::cbfcSenderCreditLimit>()) {
      return limit->toThrift();
    }
    return std::nullopt;
  }

  void setCbfcSenderCreditLimit(const std::optional<int64_t>& limit) {
    if (!limit) {
      ref<switch_state_tags::cbfcSenderCreditLimit>().reset();
      return;
    }
    set<switch_state_tags::cbfcSenderCreditLimit>(limit.value());
  }

  std::optional<int16_t> getCbfcSenderCreditSize() const {
    if (auto size = cref<switch_state_tags::cbfcSenderCreditSize>()) {
      return size->toThrift();
    }
    return std::nullopt;
  }

  void setCbfcSenderCreditSize(const std::optional<int16_t>& size) {
    if (!size) {
      ref<switch_state_tags::cbfcSenderCreditSize>().reset();
      return;
    }
    set<switch_state_tags::cbfcSenderCreditSize>(size.value());
  }

  std::optional<int16_t> getCbfcSenderPacketOverhead() const {
    if (auto overhead = cref<switch_state_tags::cbfcSenderPacketOverhead>()) {
      return overhead->toThrift();
    }
    return std::nullopt;
  }

  void setCbfcSenderPacketOverhead(const std::optional<int16_t>& overhead) {
    if (!overhead) {
      ref<switch_state_tags::cbfcSenderPacketOverhead>().reset();
      return;
    }
    set<switch_state_tags::cbfcSenderPacketOverhead>(overhead.value());
  }

  VlanID getIngressVlan() const {
    return VlanID(cref<switch_config_tags::ingressVlan>()->cref());
  }
  void setIngressVlan(VlanID id) {
    set<switch_config_tags::ingressVlan>(id);
  }

  cfg::PortSpeed getSpeed() const {
    // TODO(zecheng): change storage type when ready
    return nameToEnum<cfg::PortSpeed>(
        cref<switch_state_tags::portSpeed>()->cref());
  }
  void setSpeed(cfg::PortSpeed speed) {
    // TODO(zecheng): change storage type when ready
    set<switch_state_tags::portSpeed>(enumToName(speed));
  }

  cfg::PortProfileID getProfileID() const {
    // TODO(zecheng): change storage type when ready
    return nameToEnum<cfg::PortProfileID>(
        cref<switch_state_tags::portProfileID>()->cref());
  }
  void setProfileId(cfg::PortProfileID profileID) {
    // TODO(zecheng): change storage type when ready
    set<switch_state_tags::portProfileID>(enumToName(profileID));
  }

  cfg::PortPause getPause() const {
    cfg::PortPause portPause;
    portPause.tx() = cref<switch_state_tags::txPause>()->cref();
    portPause.rx() = cref<switch_state_tags::rxPause>()->cref();
    return portPause;
  }
  void setPause(cfg::PortPause pause) {
    set<switch_state_tags::txPause>(*pause.tx());
    set<switch_state_tags::rxPause>(*pause.rx());
  }
  std::optional<cfg::PortPfc> getPfc() const {
    if (auto pfc = cref<switch_state_tags::pfc>()) {
      // THRIFT_COPY
      return pfc->toThrift();
    }
    return std::nullopt;
  }
  void setPfc(std::optional<cfg::PortPfc>& pfc) {
    if (!pfc) {
      ref<switch_state_tags::pfc>().reset();
      return;
    }
    set<switch_state_tags::pfc>(pfc.value());
  }

  std::vector<PfcPriority> getPfcPriorities() const {
    std::vector<PfcPriority> pfcPriorities{};
    if (safe_cref<switch_state_tags::pfcPriorities>()) {
      for (auto pri : *safe_cref<switch_state_tags::pfcPriorities>()) {
        pfcPriorities.emplace_back(static_cast<PfcPriority>(pri->cref()));
      }
    }
    return pfcPriorities;
  }
  void setPfcPriorities(std::optional<std::vector<int16_t>>& pri) {
    if (!pri) {
      ref<switch_state_tags::pfcPriorities>().reset();
      return;
    }
    set<switch_state_tags::pfcPriorities>(pri.value());
  }

  int32_t getMaxFrameSize() const {
    return cref<switch_state_tags::maxFrameSize>()->cref();
  }
  void setMaxFrameSize(int32_t maxFrameSize) {
    set<switch_state_tags::maxFrameSize>(maxFrameSize);
  }

  cfg::PortLoopbackMode getLoopbackMode() const {
    // TODO(zecheng): change storage type when ready
    return nameToEnum<cfg::PortLoopbackMode>(
        cref<switch_state_tags::portLoopbackMode>()->cref());
  }
  void setLoopbackMode(cfg::PortLoopbackMode loopbackMode) {
    // TODO(zecheng): change storage type when ready
    set<switch_state_tags::portLoopbackMode>(enumToName(loopbackMode));
  }

  int64_t getSflowIngressRate() const {
    return cref<switch_state_tags::sFlowIngressRate>()->cref();
  }
  void setSflowIngressRate(int64_t ingressRate) {
    set<switch_state_tags::sFlowIngressRate>(ingressRate);
  }

  int64_t getSflowEgressRate() const {
    return cref<switch_state_tags::sFlowEgressRate>()->cref();
  }
  void setSflowEgressRate(int64_t egressRate) {
    set<switch_state_tags::sFlowEgressRate>(egressRate);
  }

  std::optional<cfg::SampleDestination> getSampleDestination() const {
    // TODO(zecheng): change storage type when ready
    if (auto sampleDestination = cref<switch_state_tags::sampleDest>()) {
      return nameToEnum<cfg::SampleDestination>(sampleDestination->cref());
    }
    return std::nullopt;
  }

  void setSampleDestination(
      const std::optional<cfg::SampleDestination>& sampleDest) {
    if (!sampleDest) {
      ref<switch_state_tags::sampleDest>().reset();
      return;
    }
    set<switch_state_tags::sampleDest>(enumToName(sampleDest.value()));
  }

  std::optional<std::string> getIngressMirror() const {
    if (auto ingressMirror = cref<switch_state_tags::ingressMirror>()) {
      return ingressMirror->cref();
    }
    return std::nullopt;
  }

  void setIngressMirror(const std::optional<std::string>& mirror) {
    if (!mirror) {
      ref<switch_state_tags::ingressMirror>().reset();
      return;
    }
    set<switch_state_tags::ingressMirror>(mirror.value());
  }

  std::optional<std::string> getEgressMirror() const {
    if (auto egressMirror = cref<switch_state_tags::egressMirror>()) {
      return egressMirror->cref();
    }
    return std::nullopt;
  }

  void setEgressMirror(const std::optional<std::string>& mirror) {
    if (!mirror) {
      ref<switch_state_tags::egressMirror>().reset();
      return;
    }
    set<switch_state_tags::egressMirror>(mirror.value());
  }

  std::optional<std::string> getQosPolicy() const {
    if (auto qosPolicy = cref<switch_state_tags::qosPolicy>()) {
      return qosPolicy->cref();
    }
    return std::nullopt;
  }

  void setQosPolicy(const std::optional<std::string>& qosPolicy) {
    if (!qosPolicy) {
      ref<switch_state_tags::qosPolicy>().reset();
      return;
    }
    set<switch_state_tags::qosPolicy>(qosPolicy.value());
  }

  LLDPValidations getLLDPValidations() const {
    // THRIFT_COPY
    return safe_cref<switch_state_tags::expectedLLDPValues>()->toThrift();
  }

  auto getExpectedNeighborValues() const {
    return safe_cref<switch_state_tags::expectedNeighborReachability>();
  }

  void setExpectedLLDPValues(LLDPValidations vals) {
    set<switch_state_tags::expectedLLDPValues>(vals);
  }

  void setExpectedNeighborReachability(const NeighborReachability& vals) {
    set<switch_state_tags::expectedNeighborReachability>(vals);
  }

  std::vector<cfg::AclLookupClass> getLookupClassesToDistributeTrafficOn()
      const {
    // THRIFT_COPY
    return cref<switch_state_tags::lookupClassesToDistrubuteTrafficOn>()
        ->toThrift();
  }

  void setLookupClassesToDistributeTrafficOn(
      const std::vector<cfg::AclLookupClass>&
          lookupClassesToDistrubuteTrafficOn) {
    set<switch_state_tags::lookupClassesToDistrubuteTrafficOn>(
        lookupClassesToDistrubuteTrafficOn);
  }

  phy::ProfileSideConfig getProfileConfig() const {
    // THRIFT_COPY
    return cref<switch_state_tags::profileConfig>()->toThrift();
  }
  void setProfileConfig(const phy::ProfileSideConfig& profileCfg) {
    set<switch_state_tags::profileConfig>(profileCfg);
  }

  std::vector<phy::PinConfig> getPinConfigs() const {
    // THRIFT_COPY
    return cref<switch_state_tags::pinConfigs>()->toThrift();
  }
  void resetPinConfigs(std::vector<phy::PinConfig> pinCfgs) {
    set<switch_state_tags::pinConfigs>(pinCfgs);
  }

  std::optional<phy::ProfileSideConfig> getLineProfileConfig() const {
    if (auto lineProfileConfig = cref<switch_state_tags::lineProfileConfig>()) {
      // THRIFT_COPY
      return lineProfileConfig->toThrift();
    }
    return std::nullopt;
  }
  void setLineProfileConfig(const phy::ProfileSideConfig& profileCfg) {
    set<switch_state_tags::lineProfileConfig>(profileCfg);
  }

  std::optional<std::vector<phy::PinConfig>> getLinePinConfigs() const {
    if (auto linePinConfigs = cref<switch_state_tags::linePinConfigs>()) {
      // THRIFT_COPY
      return linePinConfigs->toThrift();
    }
    return std::nullopt;
  }
  void resetLinePinConfigs(std::vector<phy::PinConfig> pinCfgs) {
    set<switch_state_tags::linePinConfigs>(pinCfgs);
  }

  InterfaceID getInterfaceID() const;
  std::vector<int32_t> getInterfaceIDs() const {
    return safe_cref<switch_state_tags::interfaceIDs>()->toThrift();
  }

  void setInterfaceIDs(const std::vector<int32_t>& interfaceIDs) {
    set<switch_state_tags::interfaceIDs>(interfaceIDs);
  }

  std::optional<std::string> getFlowletConfigName() const {
    if (auto name = cref<switch_state_tags::flowletConfigName>()) {
      return name->cref();
    }
    return std::nullopt;
  }

  void setFlowletConfigName(const std::optional<std::string>& name) {
    if (!name) {
      ref<switch_state_tags::flowletConfigName>().reset();
      return;
    }
    set<switch_state_tags::flowletConfigName>(name.value());
  }

  std::optional<PortFlowletCfgPtr> getPortFlowletConfig() const {
    if (auto flowletConfigPtr = safe_cref<switch_state_tags::flowletConfig>()) {
      return flowletConfigPtr;
    }
    return std::nullopt;
  }

  void setPortFlowletConfig(PortFlowletCfgPtr flowletConfigPtr) {
    ref<switch_state_tags::flowletConfig>() = flowletConfigPtr;
  }

  std::optional<std::string> getLlrConfigName() const {
    if (auto name = cref<switch_state_tags::llrConfigName>()) {
      return name->cref();
    }
    return std::nullopt;
  }

  void setLlrConfigName(const std::optional<std::string>& name) {
    if (!name) {
      ref<switch_state_tags::llrConfigName>().reset();
      return;
    }
    set<switch_state_tags::llrConfigName>(name.value());
  }

  std::optional<LlrConfigPtr> getLlrConfig() const {
    if (auto llrConfigPtr = safe_cref<switch_state_tags::llrConfig>()) {
      return llrConfigPtr;
    }
    return std::nullopt;
  }

  void setLlrConfig(LlrConfigPtr llrConfigPtr) {
    ref<switch_state_tags::llrConfig>() = llrConfigPtr;
  }

  void setLedPortExternalState(std::optional<PortLedExternalState> state) {
    if (state.has_value()) {
      set<switch_state_tags::portLedExternalState>(*state);
    } else {
      ref<switch_state_tags::portLedExternalState>().reset();
    }
  }

  std::optional<PortLedExternalState> getLedPortExternalState() {
    if (auto state = cref<switch_state_tags::portLedExternalState>()) {
      return state->toThrift();
    }
    return std::nullopt;
  }

  bool getRxLaneSquelch() const {
    return cref<switch_state_tags::rxLaneSquelch>()->cref();
  }

  void setRxLaneSquelch(bool rxLaneSquelch) {
    set<switch_state_tags::rxLaneSquelch>(rxLaneSquelch);
  }

  bool getZeroPreemphasis() const {
    return cref<switch_state_tags::zeroPreemphasis>()->cref();
  }

  void setZeroPreemphasis(bool zeroPreemphasis) {
    set<switch_state_tags::zeroPreemphasis>(zeroPreemphasis);
  }

  std::optional<std::string> getSerdesCustomCollection() const {
    if (auto value = cref<switch_state_tags::serdesCustomCollection>()) {
      return value->cref();
    }
    return std::nullopt;
  }

  void setSerdesCustomCollection(
      const std::optional<std::string>& serdesCustomCollection) {
    if (serdesCustomCollection.has_value()) {
      set<switch_state_tags::serdesCustomCollection>(*serdesCustomCollection);
    } else {
      ref<switch_state_tags::serdesCustomCollection>().reset();
    }
  }

  std::optional<bool> getTTLDisableDecrement() const {
    if (auto value = cref<switch_state_tags::disableTTLDecrement>()) {
      return value->toThrift();
    }
    return std::nullopt;
  }

  void setTTLDisableDecrement(std::optional<bool> disableTTLDecrement) {
    if (disableTTLDecrement.has_value()) {
      set<switch_state_tags::disableTTLDecrement>(*disableTTLDecrement);
    } else {
      ref<switch_state_tags::disableTTLDecrement>().reset();
    }
  }

  std::optional<bool> getTxEnable() const {
    if (auto value = cref<switch_state_tags::txEnable>()) {
      return value->toThrift();
    }
    return std::nullopt;
  }

  void setTxEnable(std::optional<bool> txEnable) {
    if (txEnable.has_value()) {
      set<switch_state_tags::txEnable>(*txEnable);
    } else {
      ref<switch_state_tags::txEnable>().reset();
    }
  }

  std::optional<bool> getResetQueueCreditBalance() const {
    if (auto value = cref<switch_state_tags::resetQueueCreditBalance>()) {
      return value->toThrift();
    }
    return std::nullopt;
  }

  void setResetQueueCreditBalance(std::optional<bool> resetQueueCreditBalance) {
    if (resetQueueCreditBalance.has_value()) {
      set<switch_state_tags::resetQueueCreditBalance>(*resetQueueCreditBalance);
    } else {
      ref<switch_state_tags::resetQueueCreditBalance>().reset();
    }
  }

  std::vector<PortError> getActiveErrors() const {
    return safe_cref<switch_state_tags::activeErrors>()->toThrift();
  }
  void setActiveErrors(const std::set<PortError>& errors);

  void addError(PortError error);
  void removeError(PortError error);

  Port* modify(std::shared_ptr<SwitchState>* state);

  void setScope(const cfg::Scope& scope) {
    set<ctrl_if_tags::scope>(scope);
  }

  cfg::Scope getScope() const {
    return cref<ctrl_if_tags::scope>()->cref();
  }

  std::optional<int32_t> getReachabilityGroupId() const {
    if (auto reachabilityGroupId =
            cref<switch_state_tags::reachabilityGroupId>()) {
      return reachabilityGroupId->toThrift();
    }
    return std::nullopt;
  }

  void setReachabilityGroupId(std::optional<int32_t> reachabilityGroupId) {
    if (!reachabilityGroupId) {
      ref<switch_state_tags::reachabilityGroupId>().reset();
    } else {
      set<switch_state_tags::reachabilityGroupId>(*reachabilityGroupId);
    }
  }

  bool getConditionalEntropyRehash() const {
    return cref<switch_state_tags::conditionalEntropyRehash>()->cref();
  }
  void setConditionalEntropyRehash(bool conditionalEntropyRehash) {
    set<switch_state_tags::conditionalEntropyRehash>(conditionalEntropyRehash);
  }

  std::optional<bool> getFecErrorDetectEnable() const {
    if (auto fecErrorDetectEnable =
            cref<switch_state_tags::fecErrorDetectEnable>()) {
      return fecErrorDetectEnable->cref();
    }
    return std::nullopt;
  }

  void setFecErrorDetectEnable(std::optional<bool> fecErrorDetectEnable) {
    if (!fecErrorDetectEnable.has_value()) {
      ref<switch_state_tags::fecErrorDetectEnable>().reset();
    } else {
      set<switch_state_tags::fecErrorDetectEnable>(
          fecErrorDetectEnable.value());
    }
  }

  std::optional<bool> getSelfHealingECMPLagEnable() const {
    if (auto selfHealingECMPLagEnable =
            cref<switch_state_tags::selfHealingECMPLagEnable>()) {
      return selfHealingECMPLagEnable->cref();
    }
    return std::nullopt;
  }

  void setSelfHealingECMPLagEnable(
      std::optional<bool> selfHealingECMPLagEnable) {
    if (!selfHealingECMPLagEnable.has_value()) {
      ref<switch_state_tags::selfHealingECMPLagEnable>().reset();
    } else {
      set<switch_state_tags::selfHealingECMPLagEnable>(
          selfHealingECMPLagEnable.value());
    }
  }

  std::optional<bool> getDesiredSelfHealingECMPLagEnable() const {
    if (auto desiredSelfHealingECMPLagEnable =
            cref<switch_state_tags::desiredSelfHealingECMPLagEnable>()) {
      return desiredSelfHealingECMPLagEnable->cref();
    }
    return std::nullopt;
  }

  void setDesiredSelfHealingECMPLagEnable(
      std::optional<bool> desiredSelfHealingECMPLagEnable) {
    if (!desiredSelfHealingECMPLagEnable.has_value()) {
      ref<switch_state_tags::desiredSelfHealingECMPLagEnable>().reset();
    } else {
      set<switch_state_tags::desiredSelfHealingECMPLagEnable>(
          desiredSelfHealingECMPLagEnable.value());
    }
  }

  /** @brief Get inter-packet gap state bits */
  std::optional<int32_t> getInterPacketGapBits() const {
    if (auto interPacketGapBits =
            cref<switch_state_tags::interPacketGapBits>()) {
      return interPacketGapBits->cref();
    }
    return std::nullopt;
  }

  /** @brief Set inter-packet gap state bits */
  void setInterPacketGapBits(std::optional<int32_t> interPacketGapBits) {
    if (!interPacketGapBits.has_value()) {
      ref<switch_state_tags::interPacketGapBits>().reset();
    } else {
      set<switch_state_tags::interPacketGapBits>(interPacketGapBits.value());
    }
  }

  /** @brief Get AM idles state */
  std::optional<bool> getAmIdles() const {
    if (auto amIdles = cref<switch_state_tags::amIdles>()) {
      return amIdles->cref();
    }
    return std::nullopt;
  }

  /** @brief Set AM idles state */
  void setAmIdles(std::optional<bool> amIdles) {
    if (!amIdles.has_value()) {
      ref<switch_state_tags::amIdles>().reset();
    } else {
      set<switch_state_tags::amIdles>(amIdles.value());
    }
  }

  /** @brief Get Cable Length Measurement (CLM) enable state */
  std::optional<bool> getClmEnable() const {
    if (auto clmEnable = cref<switch_state_tags::clmEnable>()) {
      return clmEnable->cref();
    }
    return std::nullopt;
  }

  /** @brief Set Cable Length Measurement (CLM) enable state */
  void setClmEnable(std::optional<bool> clmEnable) {
    if (!clmEnable.has_value()) {
      ref<switch_state_tags::clmEnable>().reset();
    } else {
      set<switch_state_tags::clmEnable>(clmEnable.value());
    }
  }

  /** @brief Get link training enable state */
  std::optional<bool> getLinkTraining() const {
    if (auto linkTraining = cref<switch_state_tags::linkTraining>()) {
      return linkTraining->cref();
    }
    return std::nullopt;
  }

  /** @brief Set link training enable state */
  void setLinkTraining(std::optional<bool> linkTraining) {
    if (!linkTraining.has_value()) {
      ref<switch_state_tags::linkTraining>().reset();
    } else {
      set<switch_state_tags::linkTraining>(linkTraining.value());
    }
  }

  std::optional<bool> getTxPrecoding() const {
    if (auto txPrecoding = cref<switch_state_tags::txPrecoding>()) {
      return txPrecoding->cref();
    }
    return std::nullopt;
  }

  void setTxPrecoding(std::optional<bool> txPrecoding) {
    if (!txPrecoding.has_value()) {
      ref<switch_state_tags::txPrecoding>().reset();
    } else {
      set<switch_state_tags::txPrecoding>(txPrecoding.value());
    }
  }

  std::optional<bool> getRxPrecoding() const {
    if (auto rxPrecoding = cref<switch_state_tags::rxPrecoding>()) {
      return rxPrecoding->cref();
    }
    return std::nullopt;
  }

  void setRxPrecoding(std::optional<bool> rxPrecoding) {
    if (!rxPrecoding.has_value()) {
      ref<switch_state_tags::rxPrecoding>().reset();
    } else {
      set<switch_state_tags::rxPrecoding>(rxPrecoding.value());
    }
  }

  /** @brief Get who notices link status changes: the SDK or the ASIC */
  std::optional<cfg::LinkScanMode> getLinkScanMode() const {
    if (auto linkScanMode = cref<switch_state_tags::linkScanMode>()) {
      return linkScanMode->toThrift();
    }
    return std::nullopt;
  }

  /** @brief Set who notices link status changes: the SDK or the ASIC */
  void setLinkScanMode(std::optional<cfg::LinkScanMode> linkScanMode) {
    if (!linkScanMode.has_value()) {
      ref<switch_state_tags::linkScanMode>().reset();
    } else {
      set<switch_state_tags::linkScanMode>(linkScanMode.value());
    }
  }

  std::optional<cfg::CsigTagAction> getCsigTagAction() const {
    if (auto csigTagAction = cref<switch_state_tags::csigTagAction>()) {
      return csigTagAction->toThrift();
    }
    return std::nullopt;
  }

  void setCsigTagAction(std::optional<cfg::CsigTagAction> csigTagAction) {
    if (!csigTagAction.has_value()) {
      ref<switch_state_tags::csigTagAction>().reset();
    } else {
      set<switch_state_tags::csigTagAction>(csigTagAction.value());
    }
  }

  std::optional<int16_t> getCsigLinkLocator() const {
    if (auto csigLinkLocator = cref<switch_state_tags::csigLinkLocator>()) {
      return csigLinkLocator->toThrift();
    }
    return std::nullopt;
  }

  void setCsigLinkLocator(std::optional<int16_t> csigLinkLocator) {
    if (!csigLinkLocator.has_value()) {
      ref<switch_state_tags::csigLinkLocator>().reset();
    } else {
      set<switch_state_tags::csigLinkLocator>(csigLinkLocator.value());
    }
  }

  std::optional<cfg::AclLookupClassPort> getUserMetaData() const {
    if (auto userMetaData = cref<switch_state_tags::userMetaData>()) {
      return userMetaData->cref();
    }
    return std::nullopt;
  }

  void setUserMetaData(std::optional<cfg::AclLookupClassPort> userMetaData) {
    if (!userMetaData.has_value()) {
      ref<switch_state_tags::userMetaData>().reset();
    } else {
      set<switch_state_tags::userMetaData>(userMetaData.value());
    }
  }

  std::optional<std::string> getIngressAclTableName() const {
    if (auto name = cref<switch_state_tags::ingressAclTableName>()) {
      return name->cref();
    }
    return std::nullopt;
  }

  void setIngressAclTableName(const std::optional<std::string>& name) {
    if (!name) {
      ref<switch_state_tags::ingressAclTableName>().reset();
      return;
    }
    set<switch_state_tags::ingressAclTableName>(*name);
  }

  std::optional<int32_t> getPortSwitchId() const {
    if (auto portSwitchId = cref<switch_state_tags::portSwitchId>()) {
      return portSwitchId->cref();
    }
    return std::nullopt;
  }

  void setPortSwitchId(std::optional<int32_t> portSwitchId) {
    if (!portSwitchId.has_value()) {
      ref<switch_state_tags::portSwitchId>().reset();
    } else {
      set<switch_state_tags::portSwitchId>(portSwitchId.value());
    }
  }

  /** @brief Get hold timer (ms) before reporting a link-down event */
  std::optional<int32_t> getPortDownHoldoffTimeMs() const {
    if (auto v = cref<switch_state_tags::portDownHoldoffTimeMs>()) {
      return v->cref();
    }
    return std::nullopt;
  }

  /** @brief Set hold timer (ms) before reporting a link-down event */
  void setPortDownHoldoffTimeMs(std::optional<int32_t> portDownHoldoffTimeMs) {
    if (!portDownHoldoffTimeMs.has_value()) {
      ref<switch_state_tags::portDownHoldoffTimeMs>().reset();
    } else {
      set<switch_state_tags::portDownHoldoffTimeMs>(
          portDownHoldoffTimeMs.value());
    }
  }

  /** @brief Get hold timer (ms) before reporting a link-up event */
  std::optional<int32_t> getPortUpHoldoffTimeMs() const {
    if (auto v = cref<switch_state_tags::portUpHoldoffTimeMs>()) {
      return v->cref();
    }
    return std::nullopt;
  }

  /** @brief Set hold timer (ms) before reporting a link-up event */
  void setPortUpHoldoffTimeMs(std::optional<int32_t> portUpHoldoffTimeMs) {
    if (!portUpHoldoffTimeMs.has_value()) {
      ref<switch_state_tags::portUpHoldoffTimeMs>().reset();
    } else {
      set<switch_state_tags::portUpHoldoffTimeMs>(portUpHoldoffTimeMs.value());
    }
  }

 private:
  auto getRxSaks() const {
    return safe_cref<switch_state_tags::rxSecureAssociationKeys>();
  }
  void setRxSaks(const std::vector<state::RxSak>& rxSaks) {
    set<switch_state_tags::rxSecureAssociationKeys>(rxSaks);
  }

  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};

/*
 * Whether the two ports resolve to different LLR config. The profile is
 * compared by content, not by node identity: ThriftConfigApplier rebuilds every
 * node in the LlrConfig map whenever any profile changes, so a port whose own
 * profile is untouched can still end up pointing at a new node holding
 * identical config.
 */
bool llrConfigChanged(
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort);

} // namespace facebook::fboss
