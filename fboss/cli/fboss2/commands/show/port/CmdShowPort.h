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

#include <folly/json/json.h>
#include <thrift/lib/cpp/transport/TTransportException.h>
#include <string_view>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/port/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

#include <unistd.h>
#include <algorithm>

namespace facebook::fboss {

using utils::Table;
using PeerDrainState = std::map<int64_t, cfg::SwitchDrainState>;
using PortIdToInfo = std::map<int32_t, facebook::fboss::PortInfoThrift>;
using PortNameToInfo = std::map<std::string, facebook::fboss::PortInfoThrift>;

struct CmdShowPortTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST;
  using ObjectArgType = utils::PortList;
  using RetType = cli::ShowPortModel;
  static constexpr bool ALLOW_FILTERING = true;
  static constexpr bool ALLOW_AGGREGATION = true;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

struct Endpoint {
  bool isAttached;
  std::string expectedSwitchName;
  std::string attachedSwitchName;
  std::string attachedRemotePortName;
};

struct PeerInfo {
  std::unordered_map<std::string, Endpoint> fabPort2Peer;
  std::unordered_set<std::string> allPeers;
};

struct PeerPortInfo {
  bool drainedOrDown{false};
  std::optional<uint64_t> cableLenMeters;
};
class CmdShowPort : public CmdHandler<CmdShowPort, CmdShowPortTraits> {
 public:
  using ObjectArgType = CmdShowPortTraits::ObjectArgType;
  using RetType = CmdShowPortTraits::RetType;

  std::unordered_map<std::string, std::vector<std::string>>
  getAcceptedFilterValues();

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& queriedPorts);

  RetType createModel(
      const std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries,
      const std::map<int32_t, facebook::fboss::TransceiverInfo>&
          transceiverEntries,
      const ObjectArgType& queriedPorts,
      const std::map<std::string, facebook::fboss::HwPortStats>& portStats,
      const std::unordered_map<std::string, Endpoint>& portToPeer,
      const std::unordered_map<std::string, cfg::SwitchDrainState>&
          peerDrainStates,
      const std::unordered_map<std::string, PeerPortInfo>& peerPortInfo,
      const std::vector<std::string>& drainedInterfaces,
      const std::map<std::string, int64_t>& fb303Counters = {});

  void printOutput(const RetType& model, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();

 private:
  std::chrono::seconds peerTimeout = std::chrono::seconds(1);

  PeerInfo getFabPortPeerInfo(const auto& hostInfo) const;

  PeerDrainState asyncGetDrainState(
      std::shared_ptr<apache::thrift::Client<FbossCtrl>> client) const;
  std::unordered_map<std::string, cfg::SwitchDrainState> getPeerDrainStates(
      const PeerInfo& peerInfo);

  PortIdToInfo asyncGetPortInfo(
      std::shared_ptr<apache::thrift::Client<FbossCtrl>> client) const;
  std::unordered_map<std::string, PortNameToInfo> getPeerToPorts(
      const std::unordered_set<std::string>& hosts);
  std::unordered_map<std::string, PeerPortInfo> getPeerPortInfo(
      const PeerInfo& peerInfo);

  std::unordered_map<
      std::string,
      std::shared_ptr<apache::thrift::Client<FbossCtrl>>>
      clients_;
};

} // namespace facebook::fboss
