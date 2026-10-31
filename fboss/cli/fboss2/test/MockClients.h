// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdint>

#include <folly/io/async/AsyncSocket.h>

#include <fboss/cli/fboss2/options/SSLPolicy.h>
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/FbossHwCtrl.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/commands/show/hwagent/CmdShowHwAgentStatus.h"
#include "fboss/fsdb/if/gen-cpp2/FsdbService.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#ifndef IS_OSS
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#endif

namespace facebook::fboss {

#ifndef IS_OSS
using namespace facebook::neteng::fboss::bgp::thrift;
#endif
extern std::vector<facebook::fboss::ArpEntryThrift> createArpEntries();

class MockFsdb : public apache::thrift::ServiceHandler<fsdb::FsdbService> {
 public:
  MOCK_METHOD(
      void,
      sync_getAllOperPublisherInfos,
      (fsdb::PublisherIdToOperPublisherInfo&));
  MOCK_METHOD(
      void,
      sync_getOperPublisherInfos,
      (fsdb::PublisherIdToOperPublisherInfo&,
       std::unique_ptr<fsdb::PublisherIds> ids));
  MOCK_METHOD(
      void,
      sync_getAllOperSubscriberInfos,
      (fsdb::SubscriberIdToOperSubscriberInfos&));
  MOCK_METHOD(
      void,
      sync_getOperSubscriberInfos,
      (fsdb::SubscriberIdToOperSubscriberInfos&,
       std::unique_ptr<fsdb::SubscriberIds> ids));
};

class MockAgentCounters : public AgentCountersIf {
 public:
  using FbSwHwAgentCounters = struct SwHwAgentCounters;
  using hostInfo = facebook::fboss::HostInfo;
  MOCK_METHOD(void, getAgentCounters, (hostInfo, int, FbSwHwAgentCounters&));
};

class MockFbossCtrlAgent : public apache::thrift::ServiceHandler<FbossCtrl> {
 public:
  MOCK_METHOD(void, reloadConfig, ());
  MOCK_METHOD(void, getAclTableGroup, (AclTableThrift&));
  MOCK_METHOD(void, getNdpTable, (std::vector<NdpEntryThrift>&));
  MOCK_METHOD(void, getMyMacs, (std::vector<cfg::MacAndVlan>&));

  MOCK_METHOD(
      void,
      getArpTable,
      (std::vector<facebook::fboss::ArpEntryThrift>&));
  MOCK_METHOD(
      int32_t,
      flushNeighborEntries,
      (std::unique_ptr<std::vector<IfAndIP>>));

  using PortInfoMap = std::map<int32_t, facebook::fboss::PortInfoThrift>&;
  using PortStatusMap = std::map<int32_t, facebook::fboss::PortStatus>&;
  using Out = std::string&;
  using Ports = std::unique_ptr<std::vector<int32_t>>;
  using PortNames = std::unique_ptr<std::vector<std::string>>;
  using HwObjects = std::unique_ptr<std::vector<HwObjectType>>;
  using HwAgentStatusMap =
      std::map<int16_t, facebook::fboss::HwAgentEventSyncStatus>&;
  using SwitchIndicesForInterfaces =
      std::map<int16_t, std::vector<std::string>>&;
  using RouteCounters = std::map<std::string, HwSwitchCounter>&;
  MOCK_METHOD(void, startPktCapture, (std::unique_ptr<CaptureInfo>));
  MOCK_METHOD(void, stopPktCapture, (std::unique_ptr<std::string>));
  MOCK_METHOD(void, getAllPortInfo, (PortInfoMap));
  MOCK_METHOD(void, getProductInfo, (ProductInfo&));
  MOCK_METHOD(BootType, getBootType, ());
  MOCK_METHOD(
      void,
      getAllCpuPortStats,
      ((std::map<int32_t, facebook::fboss::CpuPortStats>&)));
  MOCK_METHOD(void, getRouteCounters, (RouteCounters));
  MOCK_METHOD(void, getPortStatus, (PortStatusMap, Ports));
  MOCK_METHOD(void, getHwAgentConnectionStatus, (HwAgentStatusMap));
  MOCK_METHOD(void, getMultiSwitchRunState, (MultiSwitchRunState&));
  MOCK_METHOD(
      void,
      getRegexExportedValues,
      ((std::map<std::string, std::string>&), std::unique_ptr<std::string>));
  MOCK_METHOD(void, listHwObjects, (Out, HwObjects, bool));
  MOCK_METHOD(SSLType, getSSLPolicy, ());
  MOCK_METHOD(void, setPortState, (int32_t, bool));
  MOCK_METHOD(
      void,
      getSwitchIdToSwitchInfo,
      ((std::map<int64_t, cfg::SwitchInfo>&)));
  MOCK_METHOD(
      void,
      getFabricConnectivity,
      ((std::map<std::string, FabricEndpoint>&)));
  MOCK_METHOD(
      void,
      getAggregatePortTable,
      (std::vector<facebook::fboss::AggregatePortThrift>&));
  MOCK_METHOD(
      void,
      getSwitchIndicesForInterfaces,
      (SwitchIndicesForInterfaces, PortNames));
  MOCK_METHOD(
      void,
      getRouteTableDetails,
      (std::vector<facebook::fboss::RouteDetails>&));
  MOCK_METHOD(
      void,
      getRouteTable,
      (std::vector<facebook::fboss::UnicastRoute>&));
  MOCK_METHOD(
      void,
      getRouteTableByClient,
      (std::vector<facebook::fboss::UnicastRoute>&, int16_t));
  MOCK_METHOD3(
      getIpRouteDetails,
      void(
          facebook::fboss::RouteDetails&,
          std::unique_ptr<facebook::network::thrift::Address>,
          int32_t));
  /* This unit test is a special case because the thrift spec for
  getRegexCounters uses "thread = eb".  This requires a pretty ugly mock
  definition and call to work */
  MOCK_METHOD2(
      async_eb_getRegexCounters,
      void(
          apache::thrift::HandlerCallbackPtr<
              std::unique_ptr<std::map<std::string, int64_t>>>,
          std::unique_ptr<std::string> regex));
  MOCK_METHOD(
      void,
      getTeFlowTableDetails,
      (std::vector<facebook::fboss::TeFlowDetails>&));
  MOCK_METHOD(void, getCurrentStateJSON, (Out, std::unique_ptr<std::string>));
  MOCK_METHOD(void, getRunningConfig, (std::string&));
  MOCK_METHOD(
      void,
      getAllEcmpDetails,
      (std::vector<facebook::fboss::EcmpDetails>&));
  MOCK_METHOD(void, getConfigAppliedInfo, (ConfigAppliedInfo&));
  MOCK_METHOD(void, getPlatformMapping, (cfg::PlatformMapping&));
  using InterfaceDetailMap =
      std::map<int32_t, facebook::fboss::InterfaceDetail>&;
  MOCK_METHOD(void, getAllInterfaces, (InterfaceDetailMap));
  MOCK_METHOD(void, getNextHopGroups, (std::vector<NextHopGroup>&, bool));
  MOCK_METHOD(
      void,
      getNamedNextHopGroups,
      (std::vector<NextHopGroup>&,
       std::unique_ptr<std::vector<std::string>>,
       bool));
};

class MockFbossHwCtrlAgent
    : public apache::thrift::ServiceHandler<FbossHwCtrl> {
 public:
  using SwitchState = state::SwitchState&;
  using Ports = std::unique_ptr<std::vector<int32_t>>;

  MOCK_METHOD(void, getProgrammedState, (SwitchState));
  MOCK_METHOD(BootType, getBootType, ());
  MOCK_METHOD(void, triggerCableLengthMeasurement, (Ports));
};

class MockFbossQsfpService
    : public apache::thrift::ServiceHandler<QsfpService> {
 public:
  using MacsecPortPhyMap = mka::MacsecPortPhyMap&;
  using PortNames = std::unique_ptr<std::vector<std::string>>;
  using transceiverEntries =
      std::map<int32_t, facebook::fboss::TransceiverInfo>&;
  MOCK_METHOD2(
      getTransceiverInfo,
      void(transceiverEntries, std::unique_ptr<std::vector<int32_t>>));
  MOCK_METHOD3(
      getTransceiverConfigValidationInfo,
      void(
          std::map<int32_t, std::string>&,
          std::unique_ptr<std::vector<int32_t>>,
          bool));
  MOCK_METHOD2(
      getAllPortSupportedProfiles,
      void(std::map<std::string, std::vector<cfg::PortProfileID>>&, bool));
  MOCK_METHOD(void, getMacsecCapablePorts, (std::vector<int32_t>&));
  MOCK_METHOD(void, macsecGetPhyPortInfo, (MacsecPortPhyMap, PortNames));
  MOCK_METHOD2(
      getSymbolErrorHistogram,
      void(CdbDatapathSymErrHistogram&, std::unique_ptr<std::string>));
  MOCK_METHOD1(
      getPortMediaInterface,
      void(std::map<std::string, MediaInterfaceCode>&));
  MOCK_METHOD(
      void,
      setPortAdminState,
      (std::unique_ptr<std::string>, phy::PortComponent, bool));
};

#ifdef IS_OSS
class TBgpServiceSvIf {
  // Stub because the Thrift model for BgpService is not open source yet.
 public:
  virtual ~TBgpServiceSvIf() {}
  virtual void getRunningConfig(std::string&) = 0;
};
#endif

class MockFbossBgpService : public TBgpServiceSvIf {
 public:
  MOCK_METHOD(void, getRunningConfig, (std::string&));
};
} // namespace facebook::fboss
