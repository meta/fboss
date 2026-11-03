/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/MatchAction.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"

namespace facebook::fboss {

state::MatchAction MatchAction::toThrift() const {
  auto matchAction = state::MatchAction();
  if (sendToQueue_.has_value()) {
    auto toQueue = state::SendToQueue();
    toQueue.action() = sendToQueue_->first;
    toQueue.sendToCPU() = sendToQueue_->second;
    matchAction.sendToQueue() = toQueue;
  }
  matchAction.trafficCounter().from_optional(trafficCounter_);
  matchAction.setDscp().from_optional(setDscp_);
  matchAction.ingressMirror().from_optional(ingressMirror_);
  matchAction.egressMirror().from_optional(egressMirror_);
  matchAction.toCpuAction().from_optional(toCpuAction_);
  matchAction.macsecFlow().from_optional(macsecFlow_);
  if (redirectToNextHop_.has_value()) {
    auto redirectToNextHop = state::RedirectToNextHopAction();
    redirectToNextHop.action() = redirectToNextHop_->first;
    for (const auto& nh : redirectToNextHop_->second) {
      redirectToNextHop.resolvedNexthops()->push_back(nh.toThrift());
    }
    matchAction.redirectToNextHop() = redirectToNextHop;
  }
  matchAction.redirectNextHopGroupId().from_optional(redirectNextHopGroupId_);
  if (setTc_.has_value()) {
    auto setTc = state::SetTc();
    setTc.action() = setTc_->first;
    setTc.sendToCPU() = setTc_->second;
    matchAction.setTc() = setTc;
  }
  matchAction.userDefinedTrap().from_optional(userDefinedTrap_);
  matchAction.flowletAction().from_optional(flowletAction_);
  matchAction.ecmpHashAction().from_optional(setEcmpHashAction_);
  return matchAction;
}

MatchAction MatchAction::fromThrift(state::MatchAction const& ma) {
  auto matchAction = MatchAction();
  if (auto sendToQueue = ma.sendToQueue()) {
    auto toQueue = SendToQueue();
    toQueue.first = sendToQueue->action().value();
    toQueue.second = folly::copy(sendToQueue->sendToCPU().value());
    matchAction.sendToQueue_ = toQueue;
  }
  matchAction.trafficCounter_ = ma.trafficCounter().to_optional();
  matchAction.setDscp_ = ma.setDscp().to_optional();
  matchAction.ingressMirror_ = ma.ingressMirror().to_optional();
  matchAction.egressMirror_ = ma.egressMirror().to_optional();
  matchAction.toCpuAction_ = ma.toCpuAction().to_optional();
  matchAction.macsecFlow_ = ma.macsecFlow().to_optional();
  if (auto redirectToNextHop = ma.redirectToNextHop()) {
    auto redirectAction = RedirectToNextHopAction();
    redirectAction.first = redirectToNextHop->action().value();
    for (const auto& nh : redirectToNextHop->resolvedNexthops().value()) {
      redirectAction.second.insert(
          util::fromThrift(nh, true /* allowV6NonLinkLocal */));
    }
    matchAction.redirectToNextHop_ = redirectAction;
  }
  matchAction.redirectNextHopGroupId_ =
      ma.redirectNextHopGroupId().to_optional();
  if (auto setTc = ma.setTc()) {
    auto setTcVal = SetTc();
    setTcVal.first = setTc->action().value();
    setTcVal.second = folly::copy(setTc->sendToCPU().value());
    matchAction.setTc_ = setTcVal;
  }
  matchAction.userDefinedTrap_ = ma.userDefinedTrap().to_optional();
  matchAction.flowletAction_ = ma.flowletAction().to_optional();
  matchAction.setEcmpHashAction_ = ma.ecmpHashAction().to_optional();
  return matchAction;
}

} // namespace facebook::fboss
