// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <map>

#include "fboss/agent/ValidateInterfaceDelta.h"
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {
class StateDelta;
class SwitchIdScopeResolver;
class HwAsic;
class HwAsicTable;
class HwSwitchHandler;
class SwitchStats;
class Port;
class ResourceAccountant;

bool hasValidPortQueues(
    const std::shared_ptr<Port>& port,
    bool isEcnProbabilisticMarkingSupported);

bool isStateUpdateValidCommon(
    const StateDelta& delta,
    const HwAsicTable* hwAsicTable,
    const SwitchIdScopeResolver* resolver);
bool isStateUpdateValidMultiSwitch(
    const StateDelta& delta,
    const SwitchIdScopeResolver* resolver,
    const std::map<SwitchID, const HwAsic*>& hwAsics);

bool isStateUpdateValidMultiSwitch(
    const StateDelta& delta,
    const SwitchIdScopeResolver* resolver,
    SwitchID switchID,
    const HwAsic* asic);

class StateUpdateValidator {
 public:
  bool isValidUpdate(const StateDelta& delta, SwitchStats* stats);
  bool isValidUpdateDryRun(const StateDelta& delta, SwitchStats* stats);
  StateUpdateValidator(
      const cfg::AgentRunMode& runMode,
      const HwSwitchHandler* hwSwitchHandler,
      const HwAsicTable* asicTable,
      const SwitchIdScopeResolver* scopeResolver);

  void stateChanged(const StateDelta& delta);
  void updateRejected(const StateDelta& delta);

  const ResourceAccountant* getResourceAccountant() const {
    return resourceAccountant_.get();
  }

 private:
  bool isEcmpWidthUpdateValid(const StateDelta& delta) const;
  bool isLlrConfigUpdateValid(const StateDelta& delta) const;
  bool isCbfcSenderUpdateValid(const StateDelta& delta) const;
  bool isValidUpdateCommon(const StateDelta& delta);
  bool isValidUpdateMultiSwitch(const StateDelta& delta) const;

  cfg::AgentRunMode runMode_;
  const HwSwitchHandler* hwSwitchHandler_;
  const HwAsicTable* asicTable_;
  const SwitchIdScopeResolver* scopeResolver_;
  std::unique_ptr<ResourceAccountant> resourceAccountant_;
  IntfDeltaValidator intfDeltaValidator_;
};

} // namespace facebook::fboss
