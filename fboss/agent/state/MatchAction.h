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

#include <folly/FBString.h>
#include <folly/json/dynamic.h>
#include <optional>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/RouteNextHopEntry.h"

namespace facebook::fboss {

class MatchAction {
 public:
  /*
   * In config, we will get rid of `sendToCPU` and use `CPUTrafficPolicyConfig`
   * to decide whether we need to send to cpu queue or regular port queue.
   * In agent code, we still need to keep this `sendToCPU` logic for acl, thus,
   * SendToQueue.second is the old `sendToCPU` value.
   */
  using SendToQueue = std::pair<cfg::QueueMatchAction, bool>;
  using SetDscp = cfg::SetDscpMatchAction;
  using MacsecFlow = cfg::MacsecFlowAction;
  using NextHopSet = RouteNextHopEntry::NextHopSet;
  using RedirectToNextHopAction =
      std::pair<cfg::RedirectToNextHopAction, NextHopSet>;
  using SetTc = std::pair<cfg::SetTcAction, bool>;
  using UserDefinedTrap = cfg::UserDefinedTrapAction;
  using EcmpHashAction = cfg::SetEcmpHashAction;

  MatchAction() = default;

  MatchAction(const MatchAction& action)

      = default;

  std::optional<SendToQueue> getSendToQueue() const {
    return sendToQueue_;
  }

  void setSendToQueue(const SendToQueue& sendToQueue) {
    sendToQueue_ = sendToQueue;
  }

  bool isSendToCpuQueue() const {
    return sendToQueue_ && sendToQueue_.value().second;
  }

  std::optional<cfg::TrafficCounter> getTrafficCounter() const {
    return trafficCounter_;
  }

  void setTrafficCounter(const cfg::TrafficCounter& trafficCounter) {
    trafficCounter_ = trafficCounter;
  }

  std::optional<SetDscp> getSetDscp() const {
    return setDscp_;
  }

  void setSetDscp(const SetDscp& setDscp) {
    setDscp_ = setDscp;
  }

  std::optional<MacsecFlow> getMacsecFlow() const {
    return macsecFlow_;
  }

  void setMacsecFlow(const MacsecFlow& macsecFlow) {
    macsecFlow_ = macsecFlow;
  }

  std::optional<std::string> getIngressMirror() const {
    return ingressMirror_;
  }

  void setIngressMirror(const std::string& mirror) {
    ingressMirror_ = mirror;
  }

  void unsetIngressMirror() {
    ingressMirror_.reset();
  }

  std::optional<std::string> getEgressMirror() const {
    return egressMirror_;
  }

  void unsetEgressMirror() {
    egressMirror_.reset();
  }

  void setEgressMirror(const std::string& mirror) {
    egressMirror_ = mirror;
  }

  std::optional<cfg::ToCpuAction> getToCpuAction() const {
    return toCpuAction_;
  }

  void setToCpuAction(const cfg::ToCpuAction& toCpuAction) {
    toCpuAction_ = toCpuAction;
  }

  const std::optional<RedirectToNextHopAction>& getRedirectToNextHop() const {
    return redirectToNextHop_;
  }

  void setRedirectToNextHop(const RedirectToNextHopAction& redirectToNextHop) {
    redirectToNextHop_ = redirectToNextHop;
  }

  std::optional<int64_t> getRedirectNextHopGroupId() const {
    return redirectNextHopGroupId_;
  }

  void setRedirectNextHopGroupId(int64_t redirectNextHopGroupId) {
    redirectNextHopGroupId_ = redirectNextHopGroupId;
  }

  const std::optional<SetTc>& getSetTc() const {
    return setTc_;
  }

  void setSetTc(const SetTc& setTc) {
    setTc_ = setTc;
  }

  const std::optional<UserDefinedTrap>& getUserDefinedTrap() const {
    return userDefinedTrap_;
  }

  void setUserDefinedTrap(const UserDefinedTrap& userDefinedTrap) {
    userDefinedTrap_ = userDefinedTrap;
  }

  std::optional<cfg::FlowletAction> getFlowletAction() const {
    return flowletAction_;
  }

  void setFlowletAction(const cfg::FlowletAction& flowletAction) {
    flowletAction_ = flowletAction;
  }

  const std::optional<cfg::SetEcmpHashAction>& getEcmpHashAction() const {
    return setEcmpHashAction_;
  }

  void setEcmpHashAction(const cfg::SetEcmpHashAction& setEcmpHashAction) {
    setEcmpHashAction_ = setEcmpHashAction;
  }

  bool operator==(const MatchAction& action) const {
    return std::tie(
               sendToQueue_,
               ingressMirror_,
               egressMirror_,
               trafficCounter_,
               setDscp_,
               toCpuAction_,
               macsecFlow_,
               redirectToNextHop_,
               redirectNextHopGroupId_,
               setTc_,
               userDefinedTrap_,
               flowletAction_,
               setEcmpHashAction_) ==
        std::tie(
               action.sendToQueue_,
               action.ingressMirror_,
               action.egressMirror_,
               action.trafficCounter_,
               action.setDscp_,
               action.toCpuAction_,
               action.macsecFlow_,
               action.redirectToNextHop_,
               action.redirectNextHopGroupId_,
               action.setTc_,
               action.userDefinedTrap_,
               action.flowletAction_,
               action.setEcmpHashAction_);
  }

  MatchAction& operator=(const MatchAction& action) {
    std::tie(
        sendToQueue_,
        ingressMirror_,
        egressMirror_,
        trafficCounter_,
        setDscp_,
        toCpuAction_,
        macsecFlow_,
        redirectToNextHop_,
        redirectNextHopGroupId_,
        setTc_,
        userDefinedTrap_,
        flowletAction_,
        setEcmpHashAction_) =
        std::tie(
            action.sendToQueue_,
            action.ingressMirror_,
            action.egressMirror_,
            action.trafficCounter_,
            action.setDscp_,
            action.toCpuAction_,
            action.macsecFlow_,
            action.redirectToNextHop_,
            action.redirectNextHopGroupId_,
            action.setTc_,
            action.userDefinedTrap_,
            action.flowletAction_,
            action.setEcmpHashAction_);
    return *this;
  }

  state::MatchAction toThrift() const;
  static MatchAction fromThrift(state::MatchAction const& ma);

 private:
  std::optional<SendToQueue> sendToQueue_{std::nullopt};
  std::optional<cfg::TrafficCounter> trafficCounter_{std::nullopt};
  std::optional<SetDscp> setDscp_{std::nullopt};
  std::optional<std::string> ingressMirror_{std::nullopt};
  std::optional<std::string> egressMirror_{std::nullopt};
  std::optional<cfg::ToCpuAction> toCpuAction_{std::nullopt};
  std::optional<MacsecFlow> macsecFlow_{std::nullopt};
  std::optional<RedirectToNextHopAction> redirectToNextHop_{std::nullopt};
  std::optional<int64_t> redirectNextHopGroupId_{std::nullopt};
  std::optional<SetTc> setTc_{std::nullopt};
  std::optional<UserDefinedTrap> userDefinedTrap_{std::nullopt};
  std::optional<cfg::FlowletAction> flowletAction_{std::nullopt};
  std::optional<cfg::SetEcmpHashAction> setEcmpHashAction_{std::nullopt};
};

} // namespace facebook::fboss
