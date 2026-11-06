/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/api/LoggingUtil.h"

#include "fboss/lib/TupleUtils.h"

#include <folly/Singleton.h>

extern "C" {
#include <sai.h>
}

namespace {
struct singleton_tag_type {};

template <typename SaiApiPtr>
sai_api_t getSaiApiType(SaiApiPtr& /* api */) {
  using SaiApiT = typename std::pointer_traits<SaiApiPtr>::element_type;
  return SaiApiT::ApiType;
}
} // namespace

using facebook::fboss::SaiApiTable;
static folly::Singleton<SaiApiTable, singleton_tag_type> saiApiTableSingleton{};
std::shared_ptr<SaiApiTable> SaiApiTable::getInstance() {
  return saiApiTableSingleton.try_get();
}

namespace facebook::fboss {

const std::set<sai_api_t>& SaiApiTable::getFullApiList() const {
  static std::set<sai_api_t> fullApiList;
  if (fullApiList.empty()) {
    // Generate fullApiList_ based on the ApiT in apis_ tuple
    tupleForEach(
        [this](auto& api) { fullApiList.insert(getSaiApiType(api)); }, apis_);
  }
  return fullApiList;
}

SaiApiTable::~SaiApiTable() {
  sai_api_uninitialize();
}

template <typename SaiApiPtr>
void SaiApiTable::initApiIfDesired(
    SaiApiPtr& /* api */,
    const std::set<sai_api_t>& desiredApis) {
  using SaiApiT = typename std::pointer_traits<SaiApiPtr>::element_type;
  if (auto it = desiredApis.find(SaiApiT::ApiType); it != desiredApis.end()) {
    std::get<std::unique_ptr<SaiApiT>>(apis_) = std::make_unique<SaiApiT>();
  }
}

void SaiApiTable::queryApis(
    sai_service_method_table_t* serviceMethodTable,
    const std::set<sai_api_t>& desiredApis) {
  if (apisQueried_) {
    return;
  }
  // Initialize the SAI API
  auto rv = sai_api_initialize(0, serviceMethodTable);
  saiCheckError(rv, "Unable to initialize sai api");
  apisQueried_ = true;
  const auto& apis = SaiApiTable::getInstance()->allApis();
  tupleForEach(
      [this, &desiredApis](auto& api) { initApiIfDesired(api, desiredApis); },
      apis);
}

const AclApi& SaiApiTable::aclApi() const {
  return getApi<AclApi>();
}

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
const ArsApi& SaiApiTable::arsApi() const {
  return getApi<ArsApi>();
}

const ArsProfileApi& SaiApiTable::arsProfileApi() const {
  return getApi<ArsProfileApi>();
}
#endif

const BridgeApi& SaiApiTable::bridgeApi() const {
  return getApi<BridgeApi>();
}

const BufferApi& SaiApiTable::bufferApi() const {
  return getApi<BufferApi>();
}

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
const CounterApi& SaiApiTable::counterApi() const {
  return getApi<CounterApi>();
}
#endif

const DebugCounterApi& SaiApiTable::debugCounterApi() const {
  return getApi<DebugCounterApi>();
}

const FdbApi& SaiApiTable::fdbApi() const {
  return getApi<FdbApi>();
}

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
const FirmwareApi& SaiApiTable::firmwareApi() const {
  return getApi<FirmwareApi>();
}
#endif

const HashApi& SaiApiTable::hashApi() const {
  return getApi<HashApi>();
}

const HostifApi& SaiApiTable::hostifApi() const {
  return getApi<HostifApi>();
}

const IsolationGroupApi& SaiApiTable::isolationGroupApi() const {
  return getApi<IsolationGroupApi>();
}

const MirrorApi& SaiApiTable::mirrorApi() const {
  return getApi<MirrorApi>();
}

const MplsApi& SaiApiTable::mplsApi() const {
  return getApi<MplsApi>();
}

const NextHopApi& SaiApiTable::nextHopApi() const {
  return getApi<NextHopApi>();
}

const NextHopGroupApi& SaiApiTable::nextHopGroupApi() const {
  return getApi<NextHopGroupApi>();
}

const NeighborApi& SaiApiTable::neighborApi() const {
  return getApi<NeighborApi>();
}

const PortApi& SaiApiTable::portApi() const {
  return getApi<PortApi>();
}

const QosMapApi& SaiApiTable::qosMapApi() const {
  return getApi<QosMapApi>();
}

const QueueApi& SaiApiTable::queueApi() const {
  return getApi<QueueApi>();
}

const RouteApi& SaiApiTable::routeApi() const {
  return getApi<RouteApi>();
}

const RouterInterfaceApi& SaiApiTable::routerInterfaceApi() const {
  return getApi<RouterInterfaceApi>();
}

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
const MyMacApi& SaiApiTable::myMacApi() const {
  return getApi<MyMacApi>();
}
#endif

const SamplePacketApi& SaiApiTable::samplePacketApi() const {
  return getApi<SamplePacketApi>();
}

const SchedulerApi& SaiApiTable::schedulerApi() const {
  return getApi<SchedulerApi>();
}

const SwitchApi& SaiApiTable::switchApi() const {
  return getApi<SwitchApi>();
}

const SystemPortApi& SaiApiTable::systemPortApi() const {
  return getApi<SystemPortApi>();
}

const UdfApi& SaiApiTable::udfApi() const {
  return getApi<UdfApi>();
}

#if defined(SAI_CBFC_SUPPORTED)
const VirtualChannelApi& SaiApiTable::virtualChannelApi() const {
  return getApi<VirtualChannelApi>();
}
#endif

const VirtualRouterApi& SaiApiTable::virtualRouterApi() const {
  return getApi<VirtualRouterApi>();
}

const VlanApi& SaiApiTable::vlanApi() const {
  return getApi<VlanApi>();
}

const WredApi& SaiApiTable::wredApi() const {
  return getApi<WredApi>();
}

const TamApi& SaiApiTable::tamApi() const {
  return getApi<TamApi>();
}

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
const TamEventAgingGroupApi& SaiApiTable::tamEventAgingGroupApi() const {
  return getApi<TamEventAgingGroupApi>();
}
#endif

const TunnelApi& SaiApiTable::tunnelApi() const {
  return getApi<TunnelApi>();
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
const Srv6Api& SaiApiTable::srv6Api() const {
  return getApi<Srv6Api>();
}
#endif

const LagApi& SaiApiTable::lagApi() const {
  return getApi<LagApi>();
}

const MacsecApi& SaiApiTable::macsecApi() const {
  return getApi<MacsecApi>();
}

#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
const VendorSwitchApi& SaiApiTable::vendorSwitchApi() const {
  return getApi<VendorSwitchApi>();
}

const SwitchPipelineApi& SaiApiTable::switchPipelineApi() const {
  return getApi<SwitchPipelineApi>();
}
#endif

void SaiApiTable::enableLogging(const std::string& logLevelStr) const {
  auto logLevel = saiLogLevelFromString(logLevelStr);
  for (uint32_t api = SAI_API_UNSPECIFIED; api < SAI_API_MAX; api++) {
    auto rv = sai_log_set((sai_api_t)api, logLevel);
    // Log a error but continue on if we can't set log level
    // for a api type. A adaptor may not support debug logging
    // for a particular API (e.g. in cases where we have had
    // to add stubs for a yet to be supported API). Make this
    // a non fatal condition.
    saiLogError(rv, (sai_api_t)api, "Failed to set debug log for api");
  }
}

} // namespace facebook::fboss
