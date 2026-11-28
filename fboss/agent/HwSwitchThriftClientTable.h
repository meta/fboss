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
#include <string>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include "fboss/agent/if/gen-cpp2/FbossHwCtrl.h"
#include "fboss/agent/types.h"

DECLARE_int32(hwswitch_query_timeout);

namespace facebook::fboss {
std::unique_ptr<apache::thrift::Client<FbossHwCtrl>> createFbossHwClient(
    int16_t port,
    std::shared_ptr<folly::ScopedEventBaseThread> evbThread);
class HwSwitchThriftClientTable {
 public:
  HwSwitchThriftClientTable(
      int16_t basePort,
      const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo);
  virtual ~HwSwitchThriftClientTable() = default;
  apache::thrift::Client<FbossHwCtrl>* getClient(SwitchID switchId);

  std::optional<std::map<::std::int64_t, FabricEndpoint>> getFabricReachability(
      SwitchID switchId);
  std::map<::std::string, FabricEndpoint> getFabricConnectivity(
      SwitchID switchId);

  void clearHwPortStats(SwitchID switchId, std::vector<int32_t>& ports);
  void clearAllHwPortStats(SwitchID switchId);
  void getHwL2Table(SwitchID switchId, std::vector<L2EntryThrift>& l2Table);
  std::string diagCmd(
      SwitchID switchId,
      const std::string& cmd,
      const ClientInformation& clientInfo);

  std::vector<FirmwareInfo> getAllFirmwareInfo(SwitchID switchId);

  std::string getHwDebugDump(SwitchID switchId);
  void setSdkRegDumpEnabled(SwitchID switchId, bool enabled);
  // prbs related APIs
  std::vector<phy::PrbsLaneStats> getPortAsicPrbsStats(
      const SwitchID& switchId,
      const PortID& portId);
  void clearPortAsicPrbsStats(const SwitchID& switchId, const PortID& portId);
  std::vector<prbs::PrbsPolynomial> getPortPrbsPolynomials(
      const SwitchID& switchId,
      const PortID& portId);
  prbs::InterfacePrbsState getPortPrbsState(
      const SwitchID& switchId,
      const PortID& portId);

  virtual state::SwitchState getProgrammedState(const SwitchID& switchId);
  virtual SwitchRunState getHwSwitchRunState(const SwitchID& switchId);
  virtual BootType getHwSwitchBootType(const SwitchID& switchId);

 private:
  std::map<
      SwitchID,
      std::pair<
          std::unique_ptr<apache::thrift::Client<FbossHwCtrl>>,
          std::shared_ptr<folly::ScopedEventBaseThread>>>
      clientInfos_;
};
} // namespace facebook::fboss
