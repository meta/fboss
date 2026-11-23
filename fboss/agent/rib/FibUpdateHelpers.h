/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/types.h"

#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RouteUpdater.h"
#include "fboss/agent/state/StateDelta.h"

#include <memory>
#include <vector>

namespace facebook::fboss {

class SwitchState;
class SwitchIdScopeResolver;

StateDelta ribToSwitchStateUpdate(
    const SwitchIdScopeResolver* resolver,
    const std::vector<RibVrfFib>& vrfFibs,
    const NextHopIDManager* nextHopIDManager,
    const MySidTable& mySidTable,
    void* cookie);

StateDelta noopFibUpdate(
    const SwitchIdScopeResolver* resolver,
    const std::vector<RibVrfFib>& vrfFibs,
    const NextHopIDManager* nextHopIDManager,
    const MySidTable& mySidTable,
    void* cookie);
} // namespace facebook::fboss
