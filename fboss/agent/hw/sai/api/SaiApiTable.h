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

#include "fboss/agent/hw/sai/api/AclApi.h"
#include "fboss/agent/hw/sai/api/ArsApi.h"
#include "fboss/agent/hw/sai/api/ArsProfileApi.h"
#include "fboss/agent/hw/sai/api/BridgeApi.h"
#include "fboss/agent/hw/sai/api/BufferApi.h"
#include "fboss/agent/hw/sai/api/CounterApi.h"
#include "fboss/agent/hw/sai/api/DebugCounterApi.h"
#include "fboss/agent/hw/sai/api/FdbApi.h"
#include "fboss/agent/hw/sai/api/FirmwareApi.h"
#include "fboss/agent/hw/sai/api/HashApi.h"
#include "fboss/agent/hw/sai/api/HostifApi.h"
#include "fboss/agent/hw/sai/api/IsolationGroupApi.h"
#include "fboss/agent/hw/sai/api/LagApi.h"
#include "fboss/agent/hw/sai/api/MacsecApi.h"
#include "fboss/agent/hw/sai/api/MirrorApi.h"
#include "fboss/agent/hw/sai/api/MplsApi.h"
#include "fboss/agent/hw/sai/api/MyMacApi.h"
#include "fboss/agent/hw/sai/api/NeighborApi.h"
#include "fboss/agent/hw/sai/api/NextHopApi.h"
#include "fboss/agent/hw/sai/api/NextHopGroupApi.h"
#include "fboss/agent/hw/sai/api/PortApi.h"
#include "fboss/agent/hw/sai/api/QosMapApi.h"
#include "fboss/agent/hw/sai/api/QueueApi.h"
#include "fboss/agent/hw/sai/api/RouteApi.h"
#include "fboss/agent/hw/sai/api/RouterInterfaceApi.h"
#include "fboss/agent/hw/sai/api/SamplePacketApi.h"
#include "fboss/agent/hw/sai/api/SchedulerApi.h"
#include "fboss/agent/hw/sai/api/Srv6Api.h"
#include "fboss/agent/hw/sai/api/SwitchApi.h"
#include "fboss/agent/hw/sai/api/SwitchPipelineApi.h"
#include "fboss/agent/hw/sai/api/SystemPortApi.h"
#include "fboss/agent/hw/sai/api/TamApi.h"
#include "fboss/agent/hw/sai/api/TamEventAgingGroupApi.h"
#include "fboss/agent/hw/sai/api/TunnelApi.h"
#include "fboss/agent/hw/sai/api/UdfApi.h"
#include "fboss/agent/hw/sai/api/VendorSwitchApi.h"
#include "fboss/agent/hw/sai/api/VirtualChannelApi.h"
#include "fboss/agent/hw/sai/api/VirtualRouterApi.h"
#include "fboss/agent/hw/sai/api/VlanApi.h"
#include "fboss/agent/hw/sai/api/WredApi.h"

#include <memory>

namespace facebook::fboss {

class SaiApiTable {
 public:
  SaiApiTable() = default;
  ~SaiApiTable();
  SaiApiTable(const SaiApiTable& other) = delete;
  SaiApiTable& operator=(const SaiApiTable& other) = delete;

  // Static function for getting the SaiApiTable folly::Singleton
  static std::shared_ptr<SaiApiTable> getInstance();

  /*
   * This constructs each individual SAI API object based on the input apis list
   * which queries the Adapter for the api implementation and thus renders them
   * ready for use by the Adapter Host.
   */
  void queryApis(
      sai_service_method_table_t* serviceMethodTable,
      const std::set<sai_api_t>& desiredApis);

  const AclApi& aclApi() const;

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  const ArsApi& arsApi() const;

  const ArsProfileApi& arsProfileApi() const;
#endif

  const BridgeApi& bridgeApi() const;

  const BufferApi& bufferApi() const;

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  const CounterApi& counterApi() const;
#endif

  const DebugCounterApi& debugCounterApi() const;

  const FdbApi& fdbApi() const;

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  const FirmwareApi& firmwareApi() const;
#endif

  const HashApi& hashApi() const;

  const HostifApi& hostifApi() const;

  const IsolationGroupApi& isolationGroupApi() const;

  const MirrorApi& mirrorApi() const;

  const MplsApi& mplsApi() const;

  const NextHopApi& nextHopApi() const;

  const NextHopGroupApi& nextHopGroupApi() const;

  const NeighborApi& neighborApi() const;

  const PortApi& portApi() const;

  const QosMapApi& qosMapApi() const;

  const QueueApi& queueApi() const;

  const RouteApi& routeApi() const;

  const RouterInterfaceApi& routerInterfaceApi() const;

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  const MyMacApi& myMacApi() const;
#endif

  const SamplePacketApi& samplePacketApi() const;

  const SchedulerApi& schedulerApi() const;

  const SwitchApi& switchApi() const;

  const SystemPortApi& systemPortApi() const;

  const UdfApi& udfApi() const;

#if defined(SAI_CBFC_SUPPORTED)
  const VirtualChannelApi& virtualChannelApi() const;
#endif

  const VirtualRouterApi& virtualRouterApi() const;

  const VlanApi& vlanApi() const;

  const WredApi& wredApi() const;

  const TamApi& tamApi() const;

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  const TamEventAgingGroupApi& tamEventAgingGroupApi() const;
#endif

  const TunnelApi& tunnelApi() const;

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  const Srv6Api& srv6Api() const;
#endif

  const LagApi& lagApi() const;

  const MacsecApi& macsecApi() const;

#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  const VendorSwitchApi& vendorSwitchApi() const;

  const SwitchPipelineApi& switchPipelineApi() const;
#endif

  template <typename SaiApiT>
  SaiApiT& getApi() {
    return *std::get<std::unique_ptr<SaiApiT>>(apis_);
  }

  template <typename SaiApiT>
  const SaiApiT& getApi() const {
    return *std::get<std::unique_ptr<SaiApiT>>(apis_);
  }

  void enableLogging(const std::string& logLevelStr) const;
  const auto& allApis() const {
    return apis_;
  }

  const std::set<sai_api_t>& getFullApiList() const;

 private:
  std::tuple<
      std::unique_ptr<AclApi>,
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      std::unique_ptr<ArsApi>,
      std::unique_ptr<ArsProfileApi>,
#endif
      std::unique_ptr<BridgeApi>,
      std::unique_ptr<BufferApi>,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      std::unique_ptr<CounterApi>,
#endif
      std::unique_ptr<DebugCounterApi>,
      std::unique_ptr<FdbApi>,
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      std::unique_ptr<FirmwareApi>,
#endif
      std::unique_ptr<HashApi>,
      std::unique_ptr<HostifApi>,
      std::unique_ptr<IsolationGroupApi>,
      std::unique_ptr<NextHopApi>,
      std::unique_ptr<NextHopGroupApi>,
      std::unique_ptr<MirrorApi>,
      std::unique_ptr<MplsApi>,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      std::unique_ptr<MyMacApi>,
#endif
      std::unique_ptr<NeighborApi>,
      std::unique_ptr<PortApi>,
      std::unique_ptr<QosMapApi>,
      std::unique_ptr<QueueApi>,
      std::unique_ptr<RouteApi>,
      std::unique_ptr<RouterInterfaceApi>,
      std::unique_ptr<SamplePacketApi>,
      std::unique_ptr<SchedulerApi>,
      std::unique_ptr<SwitchApi>,
      std::unique_ptr<SystemPortApi>,
      std::unique_ptr<UdfApi>,
#if defined(SAI_CBFC_SUPPORTED)
      std::unique_ptr<VirtualChannelApi>,
#endif
      std::unique_ptr<VirtualRouterApi>,
      std::unique_ptr<VlanApi>,
      std::unique_ptr<WredApi>,
      std::unique_ptr<TamApi>,
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      std::unique_ptr<TamEventAgingGroupApi>,
#endif
      std::unique_ptr<TunnelApi>,
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      std::unique_ptr<Srv6Api>,
#endif
      std::unique_ptr<LagApi>,
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
      std::unique_ptr<VendorSwitchApi>,
      std::unique_ptr<SwitchPipelineApi>,
#endif
      std::unique_ptr<MacsecApi>>
      apis_;
  bool apisQueried_{false};

  template <typename SaiApiPtr>
  void initApiIfDesired(SaiApiPtr& api, const std::set<sai_api_t>& desiredApis);
};

} // namespace facebook::fboss
