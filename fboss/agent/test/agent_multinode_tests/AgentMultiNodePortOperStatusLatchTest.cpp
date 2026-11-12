// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/hw/StatsConstants.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeUtils.h"
#include "fboss/agent/test/thrift_client_utils/ThriftClientUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

DECLARE_bool(enable_lacp);

namespace facebook::fboss {
namespace {

constexpr size_t kLinkUpRetries{60};
constexpr auto kLinkUpRetryInterval{std::chrono::seconds(5)};
constexpr size_t kPortDownRetries{40};
constexpr auto kPortDownRetryInterval{std::chrono::milliseconds(250)};
constexpr int kHwAgentThriftPort{5931};
constexpr int32_t kRestartDelaySeconds{30};

struct InterfaceLink {
  std::string localPortName;
  std::string targetSwitch;
  std::string targetPortName;
};

std::vector<InterfaceLink> findExpectedInterfaceLinks(
    const cfg::SwitchConfig& localConfig) {
  std::vector<InterfaceLink> links;
  for (const auto& port : *localConfig.ports()) {
    if (*port.state() != cfg::PortState::ENABLED ||
        *port.portType() != cfg::PortType::INTERFACE_PORT) {
      continue;
    }

    const auto& lldpValues = *port.expectedLLDPValues();
    const auto targetSwitch = lldpValues.find(cfg::LLDPTag::SYSTEM_NAME);
    const auto targetPort = lldpValues.find(cfg::LLDPTag::PORT);
    if (targetSwitch == lldpValues.end() || targetPort == lldpValues.end()) {
      continue;
    }

    links.push_back(
        InterfaceLink{*port.name(), targetSwitch->second, targetPort->second});
  }
  return links;
}

bool interfacePortIsUp(
    const std::string& switchName,
    const std::string& portName) {
  const auto portInfos = utility::getPortIdToPortInfo(switchName);
  return std::any_of(
      portInfos.begin(), portInfos.end(), [&](const auto& idAndPortInfo) {
        const auto& portInfo = idAndPortInfo.second;
        return portInfo.name().value() == portName &&
            portInfo.portType().value() == cfg::PortType::INTERFACE_PORT &&
            portInfo.operState().value() == PortOperState::UP;
      });
}

bool interfaceLinkIsUp(
    const std::string& localSwitch,
    const std::string& remoteSwitch,
    const InterfaceLink& link) {
  return interfacePortIsUp(localSwitch, link.localPortName) &&
      interfacePortIsUp(remoteSwitch, link.targetPortName);
}

std::optional<InterfaceLink> findUpExpectedInterfaceLink(
    const std::string& localSwitch,
    const std::vector<InterfaceLink>& links) {
  for (const auto& link : links) {
    if (interfaceLinkIsUp(localSwitch, link.targetSwitch, link)) {
      return link;
    }
  }
  return std::nullopt;
}

std::string getInterfacePortOperState(
    const std::string& switchName,
    const std::string& portName) {
  try {
    const auto portInfos = utility::getPortIdToPortInfo(switchName);
    const auto portInfo = std::find_if(
        portInfos.begin(), portInfos.end(), [&](const auto& idAndPortInfo) {
          return idAndPortInfo.second.name().value() == portName &&
              idAndPortInfo.second.portType().value() ==
              cfg::PortType::INTERFACE_PORT;
        });
    if (portInfo == portInfos.end()) {
      return "missing";
    }
    return fmt::format(
        "admin={},oper={}",
        static_cast<int>(portInfo->second.adminState().value()),
        static_cast<int>(portInfo->second.operState().value()));
  } catch (const std::exception& ex) {
    return fmt::format("unavailable: {}", ex.what());
  }
}

std::string getExpectedInterfaceLinkStateSummary(
    const std::string& localSwitch,
    const std::vector<InterfaceLink>& links) {
  std::string summary;
  for (const auto& link : links) {
    if (!summary.empty()) {
      summary += "; ";
    }
    summary += fmt::format(
        "{}:{} ({}) <-> {}:{} ({})",
        localSwitch,
        link.localPortName,
        getInterfacePortOperState(localSwitch, link.localPortName),
        link.targetSwitch,
        link.targetPortName,
        getInterfacePortOperState(link.targetSwitch, link.targetPortName));
  }
  return summary;
}

std::optional<int32_t> getInterfacePortId(
    const std::string& switchName,
    const std::string& portName) {
  const auto portInfos = utility::getPortIdToPortInfo(switchName);
  const auto portInfo = std::find_if(
      portInfos.begin(), portInfos.end(), [&](const auto& idAndPortInfo) {
        return idAndPortInfo.second.name().value() == portName &&
            idAndPortInfo.second.portType().value() ==
            cfg::PortType::INTERFACE_PORT;
      });
  if (portInfo == portInfos.end()) {
    return std::nullopt;
  }
  return portInfo->first;
}

bool waitForInterfacePortState(
    const std::string& switchName,
    const std::string& portName,
    PortOperState expectedState) {
  return checkWithRetryErrorReturn(
      [&]() {
        const auto portInfos = utility::getPortIdToPortInfo(switchName);
        const auto portInfo = std::find_if(
            portInfos.begin(), portInfos.end(), [&](const auto& idAndPortInfo) {
              return idAndPortInfo.second.name().value() == portName &&
                  idAndPortInfo.second.portType().value() ==
                  cfg::PortType::INTERFACE_PORT;
            });
        return portInfo != portInfos.end() &&
            portInfo->second.operState().value() == expectedState;
      },
      kPortDownRetries,
      kPortDownRetryInterval,
      true /* retry on exception */);
}

bool waitForInterfacePortAdminState(
    const std::string& switchName,
    const std::string& portName,
    PortAdminState expectedState) {
  return checkWithRetryErrorReturn(
      [&]() {
        const auto portInfos = utility::getPortIdToPortInfo(switchName);
        const auto portInfo = std::find_if(
            portInfos.begin(), portInfos.end(), [&](const auto& idAndPortInfo) {
              return idAndPortInfo.second.name().value() == portName &&
                  idAndPortInfo.second.portType().value() ==
                  cfg::PortType::INTERFACE_PORT;
            });
        return portInfo != portInfos.end() &&
            portInfo->second.adminState().value() == expectedState;
      },
      kPortDownRetries,
      kPortDownRetryInterval,
      true /* retry on exception */);
}

std::string getInterfacePortStateSummary(
    SwSwitch* sw,
    const cfg::SwitchConfig& localConfig,
    const std::string& localSwitch) {
  const auto localPortInfos = utility::getPortIdToPortInfo(localSwitch);
  auto* lldpMgr = sw->getLldpMgr();
  std::string summary;
  for (const auto& port : *localConfig.ports()) {
    if (*port.state() != cfg::PortState::ENABLED ||
        *port.portType() != cfg::PortType::INTERFACE_PORT) {
      continue;
    }

    if (!summary.empty()) {
      summary += "; ";
    }
    const auto portInfo = std::find_if(
        localPortInfos.begin(),
        localPortInfos.end(),
        [&](const auto& idAndPortInfo) {
          return idAndPortInfo.second.name().value() == *port.name();
        });
    if (portInfo == localPortInfos.end()) {
      summary +=
          fmt::format("{}: missing from live port inventory", *port.name());
      continue;
    }

    size_t neighborCount{0};
    if (lldpMgr != nullptr) {
      neighborCount =
          lldpMgr->getDB()->getNeighbors(PortID(portInfo->first)).size();
    }
    summary += fmt::format(
        "{}(id={}, admin={}, oper={}, lldp_neighbors={})",
        *port.name(),
        portInfo->first,
        static_cast<int>(portInfo->second.adminState().value()),
        static_cast<int>(portInfo->second.operState().value()),
        neighborCount);
  }
  return summary;
}

class AdminEnabledPortGuard {
 public:
  AdminEnabledPortGuard(std::string switchName, int32_t portId)
      : switchName_(std::move(switchName)), portId_(portId) {}

  ~AdminEnabledPortGuard() {
    try {
      utility::adminEnablePort(switchName_, portId_);
    } catch (const std::exception& ex) {
      ADD_FAILURE() << "Failed to re-enable port " << portId_ << " on "
                    << switchName_ << ": " << ex.what();
    }
  }

 private:
  const std::string switchName_;
  const int32_t portId_;
};

bool isAgentUnavailable(const std::string& switchName) {
  try {
    utility::getAgentAliveSinceEpoch(switchName);
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

bool waitForAgentUnavailable(const std::string& switchName) {
  return checkWithRetryErrorReturn(
      [&]() { return isAgentUnavailable(switchName); },
      80 /* num retries */,
      std::chrono::milliseconds(250) /* sleep between retries */,
      true /* retry on exception */);
}

bool isHwAgentUnavailable(const std::string& switchName) {
  try {
    utility::getHwAgentThriftClient(switchName, kHwAgentThriftPort)
        ->sync_getHwSwitchRunState();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

bool waitForHwAgentUnavailable(const std::string& switchName) {
  return checkWithRetryErrorReturn(
      [&]() { return isHwAgentUnavailable(switchName); },
      80 /* num retries */,
      std::chrono::milliseconds(250) /* sleep between retries */,
      true /* retry on exception */);
}

} // namespace

class AgentMultiNodePortOperStatusLatchTest : public AgentHwTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& /* ensemble */) const override {
    return *AgentConfig::fromFile(FLAGS_config)->thrift.sw();
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {};
  }

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_lacp = true;
    FLAGS_enable_lldp = true;
    FLAGS_publish_stats_to_fsdb = true;
  }
};

TEST_F(
    AgentMultiNodePortOperStatusLatchTest,
    linkDownDuringWarmBootIsPublished) {
  const auto localConfig = getSw()->getConfig();
  const auto localSwitch = *localConfig.hostname();
  const auto links = findExpectedInterfaceLinks(localConfig);
  ASSERT_FALSE(links.empty())
      << "No enabled INTERFACE_PORT has expected SYSTEM_NAME and PORT LLDP "
         "values; configured port states: "
      << getInterfacePortStateSummary(getSw(), localConfig, localSwitch);

  std::optional<InterfaceLink> upLink;
  const auto linkIsUp = checkWithRetryErrorReturn(
      [&]() {
        upLink = findUpExpectedInterfaceLink(localSwitch, links);
        return upLink.has_value();
      },
      kLinkUpRetries,
      kLinkUpRetryInterval,
      true /* retry on exception */);
  ASSERT_TRUE(linkIsUp)
      << "No configured interface link became UP; link states: "
      << getExpectedInterfaceLinkStateSummary(localSwitch, links)
      << "; local LLDP state: "
      << getInterfacePortStateSummary(getSw(), localConfig, localSwitch);
  const auto& remoteLink = *upLink;
  const auto& targetSwitch = remoteLink.targetSwitch;

  const auto localPortId =
      getInterfacePortId(localSwitch, remoteLink.localPortName);
  ASSERT_TRUE(localPortId)
      << "Local interface port is missing from the live port inventory: "
      << remoteLink.localPortName;
  AdminEnabledPortGuard restoreLocalPort(localSwitch, *localPortId);

  utility::adminDisablePort(localSwitch, *localPortId);
  ASSERT_TRUE(waitForInterfacePortState(
      targetSwitch, remoteLink.targetPortName, PortOperState::DOWN))
      << targetSwitch << ":" << remoteLink.targetPortName
      << " did not transition DOWN after disabling " << localSwitch << ":"
      << remoteLink.localPortName;
  utility::adminEnablePort(localSwitch, *localPortId);
  ASSERT_TRUE(waitForInterfacePortState(
      targetSwitch, remoteLink.targetPortName, PortOperState::UP));

  auto restartFuture = std::async(std::launch::async, [&]() {
    return utility::restartServiceForSwitches(
        {targetSwitch},
        [&](const std::string& switchName) {
          utility::triggerGracefulAgentRestart(
              switchName, kRestartDelaySeconds);
        },
        utility::getAgentAliveSinceEpoch,
        utility::verifySwSwitchRunState,
        SwitchRunState::CONFIGURED);
  });

  ASSERT_TRUE(waitForAgentUnavailable(targetSwitch))
      << "Target agent did not detach for warmboot";
  ASSERT_TRUE(waitForHwAgentUnavailable(targetSwitch))
      << "Target HW agent did not detach for warmboot";
  utility::adminDisablePort(localSwitch, *localPortId);
  ASSERT_TRUE(waitForInterfacePortAdminState(
      localSwitch, remoteLink.localPortName, PortAdminState::DISABLED));
  utility::adminEnablePort(localSwitch, *localPortId);
  ASSERT_TRUE(waitForInterfacePortAdminState(
      localSwitch, remoteLink.localPortName, PortAdminState::ENABLED));
  ASSERT_TRUE(isAgentUnavailable(targetSwitch))
      << "Target agent restarted before the local port flap completed";
  ASSERT_EQ(
      std::future_status::timeout,
      restartFuture.wait_for(std::chrono::seconds(0)))
      << "Target agent restart completed before the link flap was published";
  ASSERT_TRUE(restartFuture.get()) << "Target agent warmboot failed";
  ASSERT_TRUE(waitForInterfacePortState(
      localSwitch, remoteLink.localPortName, PortOperState::UP))
      << "Local link did not return UP after target agent warmboot";
  ASSERT_TRUE(waitForInterfacePortState(
      targetSwitch, remoteLink.targetPortName, PortOperState::UP))
      << "Target link did not return UP after target agent warmboot";

  const auto targetCounters = utility::getCounterNameToCount(targetSwitch);
  const auto latchCounterName = fmt::format(
      "{}.{}", remoteLink.targetPortName, kLinkDownDuringWarmBoot().str());
  const auto latchCounter = targetCounters.find(latchCounterName);
  ASSERT_NE(latchCounter, targetCounters.end())
      << "Missing scalar fb303 counter " << latchCounterName;
  EXPECT_EQ(1, latchCounter->second);
}

} // namespace facebook::fboss
