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

#include <memory>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {
class SwitchState;

} // namespace facebook::fboss

namespace facebook::fboss::utility {

cfg::AggregatePortMember makePortMember(int32_t port, cfg::LacpPortRate rate);
void addAggPort(
    int key,
    const std::vector<int32_t>& ports,
    cfg::SwitchConfig* config,
    cfg::LacpPortRate rate = cfg::LacpPortRate::FAST,
    double minLinkPercentage = 1.0,
    cfg::AggregatePortType aggregatePortType = cfg::AggregatePortType::LAG_PORT,
    std::optional<std::string> aggPortName = std::nullopt);

/*
 * addAggPort for platforms whose router interfaces are bound to a port rather
 * than to a vlan, i.e. Chenab. Meant to be called from initialConfig, so the
 * aggregate port and its router interface exist from cold boot.
 *
 * Rather than re-homing the members onto a shared vlan, the aggregate takes a
 * router interface of its own: the first member's port router interface is
 * rebound to the aggregate, keeping its interface id, mac and addresses, and
 * the other members' interfaces are dropped. The members stay in no vlan.
 *
 * Building the trunk as part of the initial config, rather than layering it
 * onto an already applied port config, is what makes this work on Chenab. A
 * member port cannot hold a router interface of its own when it joins a LAG,
 * and the adapter defers router interface teardown, so there is no single
 * delta that can take a port from having its own interface to being a LAG
 * member.
 *
 * Expects one port router interface per member port, as
 * onePortPerInterfaceConfig produces on such platforms.
 */
void addAggPortWithRouterInterface(
    int key,
    const std::vector<int32_t>& ports,
    cfg::SwitchConfig* config,
    cfg::LacpPortRate rate = cfg::LacpPortRate::FAST,
    double minLinkPercentage = 1.0);

std::shared_ptr<SwitchState> enableTrunkPorts(
    std::shared_ptr<SwitchState> curState);
std::shared_ptr<SwitchState> disableTrunkPorts(
    std::shared_ptr<SwitchState> curState);
std::shared_ptr<SwitchState> setTrunkMinLinkCount(
    std::shared_ptr<SwitchState> curState,
    uint8_t minlinks);
std::shared_ptr<SwitchState> disableTrunkPort(
    std::shared_ptr<SwitchState> curState,
    const AggregatePortID& aggId,
    const facebook::fboss::PortID& portId);

} // namespace facebook::fboss::utility
