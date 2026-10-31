// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/ValidateStateUpdate.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/HwSwitchHandler.h"
#include "fboss/agent/ResourceAccountant.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/AclTableGroupMap.h"
#include "fboss/agent/state/AclTableMap.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchSettings.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/ScopeGuard.h>

using facebook::fboss::DeltaFunctions::forEachChanged;
using std::shared_ptr;

namespace facebook::fboss {

bool hasValidPortQueues(
    const shared_ptr<Port>& port,
    bool isEcnProbabilisticMarkingSupported) {
  constexpr auto kDefaultProbability = 100;
  for (const auto& portQueue : *port->getPortQueues()) {
    const auto& aqms = portQueue->get<ctrl_if_tags::aqms>();
    if (!aqms) {
      continue;
    }
    for (const auto& entry : std::as_const(*aqms)) {
      // THRIFT_COPY
      auto behavior = entry->cref<switch_config_tags::behavior>()->toThrift();
      auto detection = entry->cref<switch_config_tags::detection>()->toThrift();
      if (behavior == cfg::QueueCongestionBehavior::ECN) {
        auto probability = *detection.linear()->probability();
        if (isEcnProbabilisticMarkingSupported) {
          // Probability must be >0 and <=100
          if (probability <= 0 || probability > 100) {
            XLOG(ERR) << "Port " << port->getID() << " queue "
                      << portQueue->getID()
                      << " has invalid ECN probability: " << probability
                      << " (must be >0 and <=100)";
            return false;
          }
        } else {
          // Must be exactly 100%
          if (probability != kDefaultProbability) {
            XLOG(ERR) << "Port " << port->getID() << " queue "
                      << portQueue->getID()
                      << " has invalid ECN probability: " << probability
                      << " (must be exactly 100% when ECN probabilistic "
                      << "marking is not supported)";
            return false;
          }
        }
      }
    }
  }
  return true;
}

bool StateUpdateValidator::isEcmpWidthUpdateValid(
    const StateDelta& delta) const {
  bool isValid = true;
  // ecmpWidth drives route normalization and HW ECMP sizing; it cannot change
  // under a running agent. Reject any change to an already-set width, including
  // clearing it (set->unset), since clearing makes the effective width fall
  // back to FLAGS_ecmp_width and would desync forwarding state normalized
  // against the old width. unset->set is intentionally allowed: it is the
  // warm-boot upgrade path where a pre-migration state (no ecmpWidth) adopts
  // the flag-derived value, preserving the effective width. On a coldboot the
  // width is a first time add (not a change), so this lambda does not fire and
  // it applies cleanly.
  forEachChanged(
      delta.getSwitchSettingsDelta(),
      [&isValid](
          const shared_ptr<SwitchSettings>& oldSettings,
          const shared_ptr<SwitchSettings>& newSettings) {
        auto oldEcmpWidth = oldSettings->getEcmpWidth();
        auto newEcmpWidth = newSettings->getEcmpWidth();
        if (oldEcmpWidth.has_value() && oldEcmpWidth != newEcmpWidth) {
          XLOG(ERR) << "ecmpWidth cannot change on a running agent ("
                    << *oldEcmpWidth << " -> "
                    << (newEcmpWidth.has_value() ? std::to_string(*newEcmpWidth)
                                                 : "unset")
                    << "); a coldboot is required to change ECMP width";
          isValid = false;
        }
      });
  return isValid;
}

bool StateUpdateValidator::isLlrConfigUpdateValid(
    const StateDelta& delta) const {
  bool isValid = true;
  // An LLR profile cannot be retuned or unbound under a running agent. Draining
  // the port first does not help: the SDK refuses the rebind even with the port
  // admin disabled and both LLR modes cleared beforehand (CS00012478409).
  //
  // The rule enforced here is not "coldboot only": a StateDelta carries no boot
  // type, and no validator in this file reads one. It is the first bind on a
  // port that is still down, which is the shape a coldboot takes, since ports
  // exist carrying cfg::PortState::DISABLED before config is applied. A runtime
  // update of that same shape is therefore accepted too. That is safe -- the
  // port is down on the old side and SaiPortManager holds the enable until the
  // profile is attached -- but it does mean a first bind can land without a
  // coldboot.
  //
  // Everything else needs a coldboot: retuning a bound profile, unbinding one,
  // or binding onto a port that is already up, where the attach would land on
  // a port hardware considers enabled.
  forEachChanged(
      delta.getPortsDelta(),
      [&isValid](
          const shared_ptr<Port>& oldPort, const shared_ptr<Port>& newPort) {
        if (!llrConfigChanged(oldPort, newPort) ||
            (!oldPort->getLlrConfigName().has_value() &&
             !oldPort->isEnabled())) {
          return;
        }
        XLOG(ERR) << "LLR config on port " << newPort->getID()
                  << " cannot change on a running agent; a coldboot is required"
                  << " to change LLR config";
        isValid = false;
      });
  return isValid;
}

bool StateUpdateValidator::isCbfcSenderUpdateValid(
    const StateDelta& delta) const {
  bool isValid = true;
  // The CBFC sender credit size, packet overhead and credit limit are set once,
  // when a port takes on CBFC: on a coldboot, or as the first bind on a running
  // agent. On TU1 each size or overhead write makes the SDK recompute the port
  // credit limit, so changing them on a live port briefly resets the sender.
  // Changing them needs a coldboot, and so does removing CBFC from the port,
  // as for LLR.
  forEachChanged(
      delta.getPortsDelta(),
      [&isValid](
          const shared_ptr<Port>& oldPort, const shared_ptr<Port>& newPort) {
        if (!oldPort->getCbfcConfigName().has_value()) {
          return;
        }
        if (!newPort->getCbfcConfigName().has_value()) {
          XLOG(ERR) << "CBFC cannot be removed from port " << newPort->getID()
                    << " on a running agent; a coldboot is required";
          isValid = false;
          return;
        }
        if (oldPort->getCbfcSenderCreditSize() ==
                newPort->getCbfcSenderCreditSize() &&
            oldPort->getCbfcSenderPacketOverhead() ==
                newPort->getCbfcSenderPacketOverhead() &&
            oldPort->getCbfcSenderCreditLimit() ==
                newPort->getCbfcSenderCreditLimit()) {
          return;
        }
        XLOG(ERR) << "CBFC sender credit size, packet overhead or credit limit"
                  << " on port " << newPort->getID()
                  << " cannot change on a running agent; a coldboot is"
                  << " required to change them";
        isValid = false;
      });
  return isValid;
}
namespace {
std::string linkUpHoldoffStr(const shared_ptr<facebook::fboss::Port>& port) {
  auto holdoffTimeMs = port->getPortUpHoldoffTimeMs();
  return holdoffTimeMs.has_value() ? std::to_string(*holdoffTimeMs) + "ms"
                                   : "no hold";
}

bool hasSameLinkUpHoldoff(
    const shared_ptr<SwitchState>& state,
    const SwitchIdScopeResolver* resolver,
    SwitchID switchId) {
  shared_ptr<Port> firstPort;
  for (const auto& portMap : std::as_const(*state->getPorts())) {
    for (const auto& port : std::as_const(*portMap.second)) {
      if (port.second->getPortType() != cfg::PortType::INTERFACE_PORT) {
        continue;
      }
      // Each ASIC programs its own SWITCH_WIDE hold, so only compare ports
      // that belong to this switch.
      if (resolver && !resolver->scope(port.second).has(switchId)) {
        continue;
      }
      if (!firstPort) {
        firstPort = port.second;
        continue;
      }
      if (port.second->getPortUpHoldoffTimeMs() !=
          firstPort->getPortUpHoldoffTimeMs()) {
        XLOG(ERR) << "portUpHoldoffTimeMs is a switch wide timer on this ASIC, "
                  << "but port " << port.second->getName() << " asks for "
                  << linkUpHoldoffStr(port.second) << " while port "
                  << firstPort->getName() << " asks for "
                  << linkUpHoldoffStr(firstPort) << " on switch " << switchId;
        return false;
      }
    }
  }
  return true;
}

// A hold-off the SDK will not take fails the set at the ASIC, so reject it
// while the update can still be turned away. 0 is always accepted: it is how a
// config asks for no hold at all.
bool hasAcceptedLinkUpHoldoff(
    const shared_ptr<SwitchState>& state,
    const HwAsic::AcceptedValues& acceptedMs,
    const SwitchIdScopeResolver* resolver,
    SwitchID switchId) {
  for (const auto& portMap : std::as_const(*state->getPorts())) {
    for (const auto& port : std::as_const(*portMap.second)) {
      // Each ASIC enforces its own accepted values, so only check ports that
      // belong to this switch.
      if (resolver && !resolver->scope(port.second).has(switchId)) {
        continue;
      }
      auto holdoffTimeMs = port.second->getPortUpHoldoffTimeMs();
      if (!holdoffTimeMs.has_value() || *holdoffTimeMs == 0) {
        continue;
      }
      if (!acceptedMs.accepts(*holdoffTimeMs)) {
        XLOG(ERR) << "portUpHoldoffTimeMs " << *holdoffTimeMs << "ms on port "
                  << port.second->getName()
                  << " is not a value this ASIC accepts, " << acceptedMs.str()
                  << "ms on switch " << switchId;
        return false;
      }
    }
  }
  return true;
}

// Two entries at one priority are indistinguishable to hardware unless the
// platform carries the ACL entry name down as well, which only the PBR-capable
// ASICs do.
bool hasUniqueAclPriorities(
    const AclMap& acls,
    const std::string& tableName,
    bool sharedPbrPriorityAllowed) {
  std::unordered_map<int, std::string> nameByPriority;
  for (const auto& [name, entry] : std::as_const(acls)) {
    auto priority = entry->getPriority();
    if (sharedPbrPriorityAllowed && priority == FLAGS_pbr_acl_priority) {
      continue;
    }
    auto [itr, inserted] = nameByPriority.emplace(priority, name);
    if (!inserted) {
      XLOG(ERR) << "Acl table " << tableName << " has entries " << itr->second
                << " and " << name << " at the same priority " << priority;
      return false;
    }
  }
  return true;
}

bool hasUniqueAclPriorities(
    const StateDelta& delta,
    const HwAsicTable* hwAsicTable) {
  const auto& oldState = delta.oldState();
  const auto& newState = delta.newState();
  if (oldState->getAcls() == newState->getAcls() &&
      oldState->getAclTableGroups() == newState->getAclTableGroups()) {
    return true;
  }
  const bool pbrAclSupported =
      hwAsicTable->isFeatureSupportedOnAllAsic(HwAsic::Feature::PBR_ACL);
  bool isValid = true;
  if (const auto& aclTableGroups = newState->getAclTableGroups()) {
    for (const auto& [_matcher, groupMap] : std::as_const(*aclTableGroups)) {
      for (const auto& [_stage, group] : std::as_const(*groupMap)) {
        const auto& tableMap = group->getAclTableMap();
        if (!tableMap) {
          continue;
        }
        for (const auto& [tableName, table] : std::as_const(*tableMap)) {
          const bool sharedPbrPriorityAllowed = pbrAclSupported &&
              tableName ==
                  cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE();
          if (const auto& aclMap = table->getAclMap()) {
            isValid = isValid &&
                hasUniqueAclPriorities(
                          *aclMap, tableName, sharedPbrPriorityAllowed);
          }
        }
      }
    }
  }
  if (const auto& acls = newState->getAcls()) {
    for (const auto& [matcher, aclMap] : std::as_const(*acls)) {
      isValid = isValid &&
          hasUniqueAclPriorities(
                    *aclMap, matcher, false /*sharedPbrPriorityAllowed*/);
    }
  }
  return isValid;
}
} // namespace

bool isStateUpdateValidCommon(
    const StateDelta& delta,
    const HwAsicTable* hwAsicTable,
    const SwitchIdScopeResolver* resolver) {
  bool isValid = true;
  bool isEcnProbabilisticMarkingSupported =
      hwAsicTable->isFeatureSupportedOnAllAsic(
          HwAsic::Feature::ECN_PROBABILISTIC_MARKING);
  bool isEmptyAclMatcherSupported = hwAsicTable->isFeatureSupportedOnAllAsic(
      HwAsic::Feature::EMPTY_ACL_MATCHER);

  forEachChanged(
      delta.getAclsDelta(),
      [&](const shared_ptr<AclEntry>& /* oldAcl */,
          const shared_ptr<AclEntry>& newAcl) {
        isValid =
            isValid && (isEmptyAclMatcherSupported || newAcl->hasMatcher());
        if (!isValid) {
          XLOG(ERR) << "Changed acl " << newAcl->getID() << " has no matcher";
        }
        return isValid ? LoopAction::CONTINUE : LoopAction::BREAK;
      },
      [&](const shared_ptr<AclEntry>& addAcl) {
        isValid =
            isValid && (isEmptyAclMatcherSupported || addAcl->hasMatcher());
        if (!isValid) {
          XLOG(ERR) << "Newly added acl " << addAcl->getID()
                    << " has no matcher";
        }
        return isValid ? LoopAction::CONTINUE : LoopAction::BREAK;
      },
      [&](const shared_ptr<AclEntry>& /* delAcl */) {});

  isValid = isValid && hasUniqueAclPriorities(delta, hwAsicTable);

  forEachChanged(
      delta.getPortsDelta(),
      [&](const shared_ptr<Port>& /* oldport */,
          const shared_ptr<Port>& newport) {
        isValid = isValid &&
            hasValidPortQueues(newport, isEcnProbabilisticMarkingSupported);
        if (!isValid) {
          XLOG(ERR) << "Changed port " << newport->getID()
                    << " has invalid queues";
        }

        return isValid ? LoopAction::CONTINUE : LoopAction::BREAK;
      },
      [&](const shared_ptr<Port>& addport) {
        isValid = isValid &&
            hasValidPortQueues(addport, isEcnProbabilisticMarkingSupported);
        if (!isValid) {
          XLOG(ERR) << "Newly added port " << addport->getID()
                    << " has invalid queues";
        }
        return isValid ? LoopAction::CONTINUE : LoopAction::BREAK;
      },
      [&](const shared_ptr<Port>& /* delport */) {});

  for (const auto& [switchId, asic] : hwAsicTable->getHwAsics()) {
    if (asic->isSupported(HwAsic::Feature::SWITCH_WIDE_LINK_UP_DEBOUNCE) &&
        !hasSameLinkUpHoldoff(delta.newState(), resolver, switchId)) {
      isValid = false;
    }
  }

  for (const auto& [switchId, asic] : hwAsicTable->getHwAsics()) {
    auto acceptedMs = asic->getAcceptedLinkUpHoldoffTimeMs();
    if (acceptedMs.has_value() &&
        !hasAcceptedLinkUpHoldoff(
            delta.newState(), *acceptedMs, resolver, switchId)) {
      isValid = false;
    }
  }

  // Ensure only one sflow mirror session is configured
  std::set<std::string> ingressMirrors;
  for (auto mniter : std::as_const(*(delta.newState()->getPorts()))) {
    for (auto iter : std::as_const(*mniter.second)) {
      auto port = iter.second;
      if (port && port->getIngressMirror().has_value()) {
        auto ingressMirror = delta.newState()->getMirrors()->getNodeIf(
            port->getIngressMirror().value());
        if (ingressMirror && ingressMirror->type() == Mirror::Type::SFLOW) {
          ingressMirrors.insert(port->getIngressMirror().value());
        }
      }
    }
  }

  if (ingressMirrors.size() > 1) {
    XLOG(ERR) << "Only one sflow mirror can be configured across all ports";
    isValid = false;
  }

  return isValid;
}

bool isStateUpdateValidMultiSwitch(
    const StateDelta& delta,
    const SwitchIdScopeResolver* resolver,
    const std::map<SwitchID, const HwAsic*>& hwAsics) {
  auto globalQosDelta = delta.getDefaultDataPlaneQosPolicyDelta();
  auto isValid = true;
  if (globalQosDelta.getNew()) {
    auto& newPolicy = globalQosDelta.getNew();
    bool hasDscpMap =
        newPolicy->getDscpMap()->get<switch_state_tags::from>()->size() > 0;
    auto pcpMap = newPolicy->getPcpMap();
    bool hasPcpMap = pcpMap.has_value() && !pcpMap->empty();
    bool hasTcToQueue = newPolicy->getTrafficClassToQueueId()->size() > 0;

    if ((!hasDscpMap && !hasPcpMap) || !hasTcToQueue) {
      XLOG(ERR)
          << " Either DSCP to TC or PCP to TC map, along with TC to Queue map, must be provided in valid qos policies";
      return false;
    }
    /*
     * Not adding a check for expMap even though we don't support
     * MPLS QoS yet. Unfortunately, SwSwitch implicitly sets a exp map
     * even if the config doesn't have one. So no point in warning/failing
     * on what could be just system generated behavior.
     * TODO: see if we can stop doing this at SwSwitch layre
     */
  }

  if (delta.oldState()->getSwitchSettings()->size() &&
      delta.newState()->getSwitchSettings()->empty()) {
    throw FbossError("Switch settings cannot be removed from SwitchState");
  }

  // Only single watchdog recovery action is supported.
  // TODO - Add support for per port watchdog recovery action
  std::shared_ptr<Port> firstPort;
  std::optional<cfg::PfcWatchdogRecoveryAction> recoveryAction{};
  for (const auto& portMap : std::as_const(*delta.newState()->getPorts())) {
    for (const auto& port : std::as_const(*portMap.second)) {
      if (port.second->getPfc().has_value() &&
          port.second->getPfc()->watchdog().has_value()) {
        auto pfcWd = port.second->getPfc()->watchdog().value();
        if (!recoveryAction.has_value()) {
          recoveryAction = *pfcWd.recoveryAction();
          firstPort = port.second;
        } else if (*recoveryAction != *pfcWd.recoveryAction()) {
          // Error: All ports should have the same recovery action configured
          XLOG(ERR) << "PFC watchdog deadlock recovery action on "
                    << port.second->getName() << " conflicting with "
                    << firstPort->getName();
          isValid = false;
        }
      }
    }
  }

  DeltaFunctions::forEachChanged(
      delta.getMirrorsDelta(),
      [&](const std::shared_ptr<Mirror>& /* oldMirror */,
          const std::shared_ptr<Mirror>& newMirror) {
        if (!newMirror->getTruncate()) {
          // the loop mainly verifies truncate
          return LoopAction::CONTINUE;
        }
        auto scope = resolver->scope(newMirror);
        for (auto switchId : scope.switchIds()) {
          auto itr = hwAsics.find(switchId);
          if (itr == hwAsics.end()) {
            XLOG(ERR) << "ASIC not found for the switch " << switchId;
            isValid = false;
            return LoopAction::BREAK;
          }
          if (!itr->second->isSupported(
                  HwAsic::Feature::MIRROR_PACKET_TRUNCATION)) {
            XLOG(ERR)
                << "Mirror packet truncation is not supported on the switch "
                << switchId;
            isValid = false;
            return LoopAction::BREAK;
          }
        }
        return LoopAction::CONTINUE;
      });

  std::map<SwitchID, uint32_t> switchId2Mirrors;
  for (const auto& [matcherStr, mirrors] :
       std::as_const(*(delta.newState()->getMirrors()))) {
    if (!isValid) {
      break;
    }
    HwSwitchMatcher matcher(matcherStr);
    for (const auto& switchId : matcher.switchIds()) {
      if (switchId2Mirrors.find(switchId) == switchId2Mirrors.end()) {
        switchId2Mirrors[switchId] = 0;
      }
      switchId2Mirrors[switchId] += mirrors->size();
      auto itr = hwAsics.find(switchId);
      if (itr == hwAsics.end()) {
        XLOG(ERR) << "ASIC not found for the switch " << switchId;
        isValid = false;
      }
      if (itr->second->getMaxMirrors() < switchId2Mirrors[switchId]) {
        XLOG(ERR) << "Number of mirrors configured "
                  << switchId2Mirrors[switchId]
                  << " is higher on  platform for switch " << switchId
                  << " than the max supported " << itr->second->getMaxMirrors();
        isValid = false;
      }
    }
  }

  return isValid;
}

bool isStateUpdateValidMultiSwitch(
    const StateDelta& delta,
    const SwitchIdScopeResolver* resolver,
    SwitchID switchID,
    const HwAsic* asic) {
  return isStateUpdateValidMultiSwitch(delta, resolver, {{switchID, asic}});
}

StateUpdateValidator::StateUpdateValidator(
    const cfg::AgentRunMode& runMode,
    const HwSwitchHandler* hwSwitchHandler,
    const HwAsicTable* asicTable,
    const SwitchIdScopeResolver* scopeResolver)
    : runMode_(runMode),
      hwSwitchHandler_(hwSwitchHandler),
      asicTable_(asicTable),
      scopeResolver_(scopeResolver) {
  resourceAccountant_ =
      std::make_unique<ResourceAccountant>(asicTable_, scopeResolver_);
}

bool StateUpdateValidator::isValidUpdate(
    const StateDelta& delta,
    SwitchStats* stats) {
  lastRejectionRequiredApplyMethod_.reset();
  if (!isEcmpWidthUpdateValid(delta)) {
    XLOG(ERR) << "State update is not valid.";
    lastRejectionRequiredApplyMethod_ =
        thrift::ConfigApplyMethod::DISRUPTIVE_RESTART;
    return false;
  }

  if (!isLlrConfigUpdateValid(delta)) {
    XLOG(ERR) << "State update is not valid.";
    lastRejectionRequiredApplyMethod_ =
        thrift::ConfigApplyMethod::DISRUPTIVE_RESTART;
    return false;
  }

  if (!isCbfcSenderUpdateValid(delta)) {
    XLOG(ERR) << "State update is not valid.";
    return false;
  }

  if (!resourceAccountant_->isValidUpdate(delta)) {
    stats->resourceAccountantRejectedUpdates();
    XLOG(ERR) << "State updated rejected by resource accountant.";
    return false;
  }

  if (!isValidUpdateCommon(delta)) {
    XLOG(ERR) << "State update is not valid.";
    return false;
  }

  switch (runMode_) {
    case cfg::AgentRunMode::MONO:
      if (!hwSwitchHandler_->isValidStateUpdate(delta)) {
        XLOG(ERR) << "State update is not valid.";
        return false;
      }
      break;
    case cfg::AgentRunMode::MULTI_SWITCH:
      if (!isValidUpdateMultiSwitch(delta)) {
        XLOG(ERR) << "State update is not valid.";
        return false;
      }
      break;
  }
  return true;
}

bool StateUpdateValidator::isValidUpdateCommon(const StateDelta& delta) {
  if (!isStateUpdateValidCommon(delta, asicTable_, scopeResolver_)) {
    return false;
  }
  if (!intfDeltaValidator_.isValidDelta(delta)) {
    return false;
  }
  return true;
}

bool StateUpdateValidator::isValidUpdateMultiSwitch(
    const StateDelta& delta) const {
  return isStateUpdateValidMultiSwitch(
      delta, scopeResolver_, asicTable_->getHwAsics());
}

bool StateUpdateValidator::isValidUpdateDryRun(
    const StateDelta& delta,
    SwitchStats* stats) {
  auto resourceAccountant =
      std::make_unique<ResourceAccountant>(*resourceAccountant_);
  auto intfDeltaValidator = intfDeltaValidator_;
  SCOPE_EXIT {
    resourceAccountant_ = std::move(resourceAccountant);
    intfDeltaValidator_ = std::move(intfDeltaValidator);
  };
  return isValidUpdate(delta, stats);
}

void StateUpdateValidator::stateChanged(const StateDelta& delta) {
  resourceAccountant_->stateChanged(delta);
}

void StateUpdateValidator::updateRejected(const StateDelta& delta) {
  /* reconstruct the resource account to reset resources accounted in earlier
   * deltas */
  reset(delta.oldState());
}

void StateUpdateValidator::reset(const std::shared_ptr<SwitchState>& state) {
  resourceAccountant_ =
      std::make_unique<ResourceAccountant>(asicTable_, scopeResolver_);
  resourceAccountant_->stateChanged(
      StateDelta(std::make_shared<SwitchState>(), state));
  intfDeltaValidator_.updateRejected(
      StateDelta(std::make_shared<SwitchState>(), state));
}

} // namespace facebook::fboss
