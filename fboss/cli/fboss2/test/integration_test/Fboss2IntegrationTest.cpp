// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

#include <CLI/App.hpp>
#include <CLI/Error.hpp>
#include <fmt/format.h>
#include <folly/String.h>
#include <folly/Subprocess.h>
#include <folly/init/Init.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/Init.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <folly/IPAddressV6.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/gen-cpp2/platform_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/CmdArgsLists.h"
#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdInitUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

namespace fs = std::filesystem;

namespace facebook::fboss {

void Fboss2IntegrationTest::SetUp() {
  XLOG(INFO) << "Fboss2IntegrationTest::SetUp - starting CLI test";
  // Discard any stale session from previous runs to ensure we start fresh
  discardSession();
  // Prior tests may have triggered a warm/coldboot; wait out any residual
  // agent-initializing state before the test body starts talking to it.
  waitForAgentReady();
}

void Fboss2IntegrationTest::TearDown() {
  XLOG(INFO) << "Fboss2IntegrationTest::TearDown - cleaning up CLI test";
}

void Fboss2IntegrationTest::discardSession() const {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): HOME is read-only in practice
  const char* home = std::getenv("HOME");
  if (home == nullptr) {
    XLOG(WARN) << "HOME environment variable not set, cannot discard session";
    return;
  }
  fs::path sessionDir = fs::path(home) / ".fboss2";
  fs::path sessionConfig = sessionDir / "agent.conf";
  fs::path sessionMetadata = sessionDir / "cli_metadata.json";

  std::error_code ec;
  if (fs::exists(sessionConfig, ec)) {
    XLOG(INFO) << "Discarding session config: " << sessionConfig.string();
    fs::remove(sessionConfig, ec);
    if (ec) {
      XLOG(WARN) << "Failed to remove session config: " << ec.message();
    }
  }
  if (fs::exists(sessionMetadata, ec)) {
    XLOG(INFO) << "Discarding session metadata: " << sessionMetadata.string();
    fs::remove(sessionMetadata, ec);
    if (ec) {
      XLOG(WARN) << "Failed to remove session metadata: " << ec.message();
    }
  }

  // Reset the in-memory singleton so the next CLI command starts a fresh
  // session from the current system config, not stale in-process state.
  ConfigSession::resetInstance();
}

Fboss2IntegrationTest::Result Fboss2IntegrationTest::executeCliCommand(
    const std::vector<std::string>& args) const {
  // CLI11 add_option() appends to the vector it's bound to rather than
  // replacing it. CmdArgsLists is a process-wide singleton, so without this
  // reset args from a previous executeCliCommand() call would bleed into the
  // current parse and typically cause nonsensical positional arg values.
  CmdArgsLists::getInstance()->clear();

  // CmdGlobalOptions and CmdLocalOptions are also process-wide singletons
  // and CLI11 leaves bound variables untouched when the option isn't passed,
  // so a --filter / --aggregate or a per-command option (e.g. show route
  // --clientID) from a previous executeCliCommand() would apply to every
  // later command. Reset them before each parse.
  std::string emptyOption;
  CmdGlobalOptions::getInstance()->setFilterInput(emptyOption);
  CmdGlobalOptions::getInstance()->setAggregateInput(emptyOption);
  CmdLocalOptions::getInstance()->clear();

  // Create a new CLI::App for this command
  CLI::App app{"FBOSS CLI Test"};
  utils::initApp(app);

  // Build argv-style argument list
  // Prepend program name and --fmt json
  std::vector<std::string> fullArgs = {"fboss2", "--fmt", "json"};
  fullArgs.insert(fullArgs.end(), args.begin(), args.end());

  // Convert to argc/argv format
  std::vector<char*> argv;
  argv.reserve(fullArgs.size());
  for (auto& arg : fullArgs) {
    argv.push_back(arg.data());
  }

  // Redirect stdout and stderr to capture output
  std::stringstream capturedStdout;
  std::stringstream capturedStderr;
  std::streambuf* oldStdout = std::cout.rdbuf(capturedStdout.rdbuf());
  std::streambuf* oldStderr = std::cerr.rdbuf(capturedStderr.rdbuf());

  int exitCode = 0;
  try {
    // Parse and execute the command
    app.parse(static_cast<int>(argv.size()), argv.data());
  } catch (const CLI::ParseError& e) {
    exitCode = app.exit(e);
  } catch (const std::exception& e) {
    capturedStderr << "Error: " << e.what() << "\n";
    exitCode = 1;
  }

  // Restore original stdout/stderr
  std::cout.rdbuf(oldStdout);
  std::cerr.rdbuf(oldStderr);

  return Result{exitCode, capturedStdout.str(), capturedStderr.str()};
}

Fboss2IntegrationTest::Result Fboss2IntegrationTest::runCli(
    const std::vector<std::string>& args) const {
  XLOG(INFO) << "Running CLI command: " << folly::join(" ", args);
  return executeCliCommand(args);
}

folly::dynamic Fboss2IntegrationTest::runCliJson(
    const std::vector<std::string>& args) const {
  auto result = runCli(args);
  EXPECT_EQ(result.exitCode, 0) << "CLI command failed: " << result.stderr;

  if (result.exitCode != 0 || result.stdout.empty()) {
    return folly::dynamic::object();
  }
  return folly::parseJson(result.stdout);
}

folly::dynamic Fboss2IntegrationTest::getRunningConfig() const {
  HostInfo hostInfo("localhost");
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  std::string configStr;
  client->sync_getRunningConfig(configStr);
  return folly::parseJson(configStr);
}

std::string Fboss2IntegrationTest::getAgentOption(
    const std::string& name) const {
  HostInfo hostInfo("localhost");
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  std::string value;
  client->sync_getOption(value, name);
  return value;
}

folly::dynamic Fboss2IntegrationTest::getSwitchState(
    const std::string& path) const {
  HostInfo hostInfo("localhost");
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  std::string stateJson;
  client->sync_getCurrentStateJSON(stateJson, path);
  return folly::parseJson(stateJson);
}

int Fboss2IntegrationTest::getInterfaceIdForPort(
    const std::string& portName) const {
  HostInfo hostInfo("localhost");
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  std::map<int32_t, InterfaceDetail> intfDetails;
  client->sync_getAllInterfaces(intfDetails);
  for (const auto& [intfId, detail] : intfDetails) {
    for (const auto& name : *detail.portNames()) {
      if (name == portName) {
        return *detail.interfaceId();
      }
    }
  }
  throw std::runtime_error(
      fmt::format("No L3 interface found for port {}", portName));
}

std::optional<std::pair<int, std::string>>
Fboss2IntegrationTest::findConfiguredVlanPort() const {
  auto config = getRunningConfig();
  const auto& sw = config["sw"];
  std::set<int> knownVlans;
  if (sw.count("vlans")) {
    for (const auto& v : sw["vlans"]) {
      if (v.count("id")) {
        knownVlans.insert(v["id"].asInt());
      }
    }
  }
  if (!sw.count("vlanPorts") || knownVlans.empty()) {
    return std::nullopt;
  }
  std::map<int, std::string> logicalToName;
  if (sw.count("ports")) {
    for (const auto& p : sw["ports"]) {
      if (p.count("logicalID") && p.count("name")) {
        logicalToName[p["logicalID"].asInt()] = p["name"].asString();
      }
    }
  }
  for (const auto& vp : sw["vlanPorts"]) {
    if (!vp.count("vlanID") || !vp.count("logicalPort")) {
      continue;
    }
    int vlanId = vp["vlanID"].asInt();
    if (!knownVlans.count(vlanId)) {
      continue;
    }
    auto it = logicalToName.find(vp["logicalPort"].asInt());
    if (it == logicalToName.end()) {
      continue;
    }
    // Prefer ports whose names start with "eth" (skip loopback/fabric).
    if (it->second.rfind("eth", 0) == 0) {
      return std::make_pair(vlanId, it->second);
    }
  }
  return std::nullopt;
}

Fboss2IntegrationTest::Result Fboss2IntegrationTest::runCmd(
    const std::vector<std::string>& args) const {
  XLOG(INFO) << "Running command: " << folly::join(" ", args);

  folly::Subprocess::Options options;
  options.pipeStdout();
  options.pipeStderr();

  folly::Subprocess proc(args, options);
  auto [stdout, stderr] = proc.communicate();
  auto exitStatus = proc.wait().exitStatus();

  return Result{exitStatus, stdout, stderr};
}

Fboss2IntegrationTest::Interface Fboss2IntegrationTest::parseInterfaceJson(
    const folly::dynamic& data) const {
  Fboss2IntegrationTest::Interface intf;
  intf.name = data["name"].asString();
  intf.status = data["status"].asString();
  intf.speed = data["speed"].asString();
  if (data.count("vlan") && !data["vlan"].isNull()) {
    intf.vlan = static_cast<int>(data["vlan"].asInt());
  }
  if (data.count("mtu")) {
    intf.mtu = static_cast<int>(data["mtu"].asInt());
  }

  // Parse prefixes
  if (data.count("prefixes")) {
    for (const auto& prefix : data["prefixes"]) {
      intf.addresses.push_back(
          fmt::format(
              "{}/{}",
              prefix["ip"].asString(),
              prefix["prefixLength"].asInt()));
    }
  }

  intf.description = data.getDefault("description", "").asString();
  return intf;
}

std::map<std::string, Fboss2IntegrationTest::Interface>
Fboss2IntegrationTest::getAllInterfaces() const {
  auto result = runCli({"show", "interface"});
  XLOG(DBG2) << "getAllInterfaces: exitCode=" << result.exitCode
             << " stdout.size=" << result.stdout.size()
             << " stderr=" << result.stderr;

  if (result.exitCode != 0 || result.stdout.empty()) {
    return {};
  }

  auto json = folly::parseJson(result.stdout);
  std::map<std::string, Fboss2IntegrationTest::Interface> interfaces;
  int totalIntfs = 0;
  int withVlan = 0;

  // JSON has a host key containing the interfaces
  for (const auto& [host, hostData] : json.items()) {
    if (!hostData.isObject() || !hostData.count("interfaces")) {
      continue;
    }
    for (const auto& intfData : hostData["interfaces"]) {
      ++totalIntfs;
      auto intf = parseInterfaceJson(intfData);
      if (intf.vlan.has_value() && *intf.vlan > 1) {
        ++withVlan;
      }
      interfaces[intf.name] = intf;
    }
  }

  XLOG(DBG2) << "getAllInterfaces: total=" << totalIntfs
             << " withVlan>1=" << withVlan;

  return interfaces;
}

Fboss2IntegrationTest::Interface Fboss2IntegrationTest::getInterfaceInfo(
    const std::string& interfaceName) const {
  auto json = runCliJson({"show", "interface", interfaceName});

  XLOG(DBG2) << "getInterfaceInfo JSON: " << folly::toPrettyJson(json);

  for (const auto& [host, hostData] : json.items()) {
    XLOG(DBG2) << "  Host: " << host << ", isObject: " << hostData.isObject()
               << ", hasInterfaces: " << hostData.count("interfaces");
    if (!hostData.isObject() || !hostData.count("interfaces")) {
      continue;
    }
    for (const auto& intfData : hostData["interfaces"]) {
      XLOG(DBG2) << "    Interface: " << intfData["name"].asString();
      if (intfData["name"].asString() == interfaceName) {
        return parseInterfaceJson(intfData);
      }
    }
  }

  throw std::runtime_error(
      fmt::format("Interface {} not found", interfaceName));
}

std::string Fboss2IntegrationTest::getRandomInterfacePortName() const {
  // Retry with backoff to handle the window where the agent is processing a
  // config reload after a preceding commit. Agent reloads can take up to ~30s.
  constexpr int kMaxRetries = 60;
  constexpr auto kRetryDelay = std::chrono::milliseconds(1000);

  // Select from the agent's port list rather than 'show interface' so we only
  // ever pick an INTERFACE_PORT. Other port types (e.g. MANAGEMENT_PORT) can
  // still carry an L3 interface, but that interface is virtual and the agent
  // omits its member ports from getAllInterfaces(); selecting one would make
  // getInterfaceIdForPort() throw "No L3 interface found for port".
  for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
    HostInfo hostInfo("localhost");
    auto client =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    std::map<int32_t, PortInfoThrift> portEntries;
    client->sync_getAllPortInfo(portEntries);

    std::vector<std::string> upCandidates;
    std::vector<std::string> allCandidates;
    for (const auto& [portId, portInfo] : portEntries) {
      if (portInfo.portType() != cfg::PortType::INTERFACE_PORT) {
        continue;
      }
      allCandidates.push_back(*portInfo.name());
      if (portInfo.operState() == PortOperState::UP) {
        upCandidates.push_back(*portInfo.name());
      }
    }
    const auto& pool = !upCandidates.empty() ? upCandidates : allCandidates;
    if (!pool.empty()) {
      thread_local std::mt19937 rng{std::random_device{}()};
      std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
      const auto& chosenName = pool[dist(rng)];
      XLOG(INFO) << "Selected test interface " << chosenName
                 << " (pool=" << (!upCandidates.empty() ? "up" : "all")
                 << ", size=" << pool.size() << ")";
      // Callers that need vlan/addresses/description can pass this name to
      // getInterfaceInfo().
      return chosenName;
    }
    if (attempt + 1 < kMaxRetries) {
      XLOG(WARN) << "getRandomInterfacePortName: no INTERFACE_PORT found "
                    "(attempt "
                 << (attempt + 1) << "/" << kMaxRetries
                 << "), retrying in 1s...";
      // Polling backoff: the port list is populated asynchronously by the
      // agent, with no event/future to await, so a short delay between retries
      // is the appropriate pattern here.
      // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
      std::this_thread::sleep_for(kRetryDelay);
    }
  }
  throw std::runtime_error("No INTERFACE_PORT found in getAllPortInfo");
}

std::optional<Fboss2IntegrationTest::Interface>
Fboss2IntegrationTest::findFirstEthInterfaceWithMtu() const {
  auto interfaces = getAllInterfaces();

  for (const auto& [name, intf] : interfaces) {
    if (name.rfind("eth", 0) == 0 && intf.vlan.has_value() && *intf.vlan > 1 &&
        intf.mtu > 0) {
      return intf;
    }
  }

  return std::nullopt;
}

void Fboss2IntegrationTest::commitConfig() const {
  auto result = runCli({"config", "session", "commit"});
  if (result.exitCode == 0) {
    return;
  }
  // When a commit triggers an agent restart (warmboot / coldboot to apply the
  // new config), the thrift call can lose its connection mid-flight. The
  // commit itself may have been accepted before the restart — callers should
  // confirm the actual outcome via getRunningConfig() / waitForRunningConfig().
  // Treat the disconnect signatures as expected: log, wait for the agent to
  // come back, and return without failing the test here.
  static constexpr std::array<std::string_view, 3> kRestartSignatures = {
      "Channel got EOF",
      "Connection refused",
      "Socket not open",
  };
  for (auto sig : kRestartSignatures) {
    if (result.stderr.find(sig) != std::string::npos) {
      XLOG(INFO)
          << "Commit dropped connection (likely agent restart): " << sig
          << ". Waiting for agent to come back; caller must verify via running config.";
      waitForAgentReady();
      return;
    }
  }
  ASSERT_EQ(result.exitCode, 0) << "Failed to commit config: " << result.stderr;
}

void Fboss2IntegrationTest::waitForAgentReady(
    std::chrono::seconds timeout) const {
  // Fast path — if the agent is already responsive, skip the 5s grace.
  {
    auto quick = executeCliCommand({"show", "interface"});
    if (quick.exitCode == 0) {
      XLOG(DBG1) << "Agent already ready; skipping wait";
      return;
    }
  }
  XLOG(INFO) << "Waiting for agent to become ready (timeout: "
             << timeout.count() << "s)...";
  // Wait 5s before the first poll to give the agent time to shut down and
  // start coming back up. Polling immediately after a restart can interfere
  // with the initialization sequence.
  XLOG(INFO) << "  Waiting 5s before first poll...";
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(std::chrono::seconds(5));
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    auto result = executeCliCommand({"show", "interface"});
    if (result.exitCode == 0) {
      XLOG(INFO) << "Agent is ready";
      return;
    }
    // Two expected failure modes during restart:
    // 1. "Connection refused"  — process not yet listening
    // 2. "switch is still initializing" — process up, HW init in progress
    // Both resolve naturally once the agent finishes warmboot/coldboot (~50s).
    XLOG(DBG1) << "Agent not ready: " << result.stderr;
    XLOG(INFO) << "Agent not ready yet, retrying in 10s...";
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(std::chrono::seconds(10));
  }
  FAIL() << "Agent did not become ready within " << timeout.count()
         << " seconds";
}

int Fboss2IntegrationTest::getKernelInterfaceMtu(int vlanId) const {
  auto kernelIntf = fmt::format("fboss{}", vlanId);
  // Use full path to ip command since folly::Subprocess doesn't search PATH
  auto result = runCmd({"/usr/sbin/ip", "-json", "link", "show", kernelIntf});

  if (result.exitCode != 0) {
    // Interface does not exist on this platform (e.g. VLAN has no L3 RIF)
    // — return 0 so callers can treat it as "skip kernel-side check".
    // Callers relying on the "if (mtu > 0)" guard pattern work this way.
    return 0;
  }

  auto json = folly::parseJson(result.stdout);
  if (json.empty()) {
    throw std::runtime_error(
        fmt::format("No data returned for kernel interface {}", kernelIntf));
  }

  return static_cast<int>(json[0]["mtu"].asInt());
}

Fboss2IntegrationTest::Interface Fboss2IntegrationTest::waitForInterfaceInfo(
    const std::string& interfaceName,
    const std::function<bool(const Interface&)>& condition,
    std::chrono::seconds timeout,
    std::chrono::seconds interval) const {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  Interface last{};
  do {
    last = getInterfaceInfo(interfaceName);
    if (condition(last)) {
      return last;
    }
    XLOG(DBG1) << "Condition not yet met for interface " << interfaceName
               << ", retrying in " << interval.count() << "s...";
    /* sleep override */ std::this_thread::sleep_for(interval);
  } while (std::chrono::steady_clock::now() < deadline);
  XLOG(INFO) << "Timeout waiting for condition on interface " << interfaceName;
  return last;
}

int Fboss2IntegrationTest::waitForKernelMtu(
    int vlanId,
    const std::function<bool(int)>& condition,
    std::chrono::seconds timeout,
    std::chrono::seconds interval) const {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  int last = 0;
  do {
    last = getKernelInterfaceMtu(vlanId);
    if (condition(last)) {
      return last;
    }
    XLOG(DBG1) << "Kernel MTU condition not yet met for fboss" << vlanId
               << " (mtu=" << last << "), retrying in " << interval.count()
               << "s...";
    /* sleep override */ std::this_thread::sleep_for(interval);
  } while (std::chrono::steady_clock::now() < deadline);
  XLOG(INFO) << "Timeout waiting for kernel MTU condition on fboss" << vlanId
             << " — last observed: " << last;
  return last;
}

Fboss2IntegrationTest::PortRunningInfo
Fboss2IntegrationTest::getPortRunningInfo(const std::string& portName) const {
  HostInfo hostInfo("localhost");
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  std::map<int32_t, PortInfoThrift> portEntries;
  client->sync_getAllPortInfo(portEntries);
  for (const auto& [portId, portInfo] : portEntries) {
    if (*portInfo.name() == portName) {
      return PortRunningInfo{*portInfo.speedMbps(), *portInfo.profileID()};
    }
  }
  throw std::runtime_error(
      fmt::format("Port '{}' not found in getAllPortInfo", portName));
}

Fboss2IntegrationTest::PortRunningInfo
Fboss2IntegrationTest::waitForPortRunningInfo(
    const std::string& portName,
    const std::function<bool(const PortRunningInfo&)>& condition,
    std::chrono::seconds timeout,
    std::chrono::seconds interval) const {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  PortRunningInfo last{};
  do {
    last = getPortRunningInfo(portName);
    if (condition(last)) {
      return last;
    }
    XLOG(DBG1) << "Condition not yet met for port " << portName
               << " (profile=" << last.profileId << ", speed=" << last.speedMbps
               << " Mbps)"
               << ", retrying in " << interval.count() << "s...";
    /* sleep override */ std::this_thread::sleep_for(interval);
  } while (std::chrono::steady_clock::now() < deadline);
  XLOG(INFO) << "Timeout waiting for condition on port " << portName
             << " — last observed: profile=" << last.profileId
             << ", speed=" << last.speedMbps << " Mbps";
  return last;
}

PlatformMapping Fboss2IntegrationTest::fetchPlatformMapping() const {
  HostInfo hostInfo("localhost");
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  cfg::PlatformMapping thriftMapping;
  client->sync_getPlatformMapping(thriftMapping);
  return PlatformMapping(thriftMapping);
}

std::map<int32_t, Fboss2IntegrationTest::PresentPort>
Fboss2IntegrationTest::fetchPresentPorts() const {
  HostInfo hostInfo("localhost");
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  std::map<int32_t, PortInfoThrift> portEntries;
  client->sync_getAllPortInfo(portEntries);
  std::map<int32_t, PresentPort> present;
  for (const auto& [id, info] : portEntries) {
    present[id] = PresentPort{*info.name(), *info.profileID()};
  }
  return present;
}

std::set<std::string> Fboss2IntegrationTest::supportedProfileNames(
    const PlatformMapping& mapping,
    int32_t id) const {
  std::set<std::string> names;
  const auto& entry = mapping.getPlatformPort(id);
  for (const auto& [profileId, unused] : *entry.supportedProfiles()) {
    names.insert(apache::thrift::util::enumNameSafe(profileId));
  }
  return names;
}

std::optional<Fboss2IntegrationTest::CreatableCandidate>
Fboss2IntegrationTest::findFreeableCandidate() const {
  auto mapping = fetchPlatformMapping();
  auto present = fetchPresentPorts();
  auto groups = utility::getSubsidiaryPortIDs(mapping.getPlatformPorts());
  for (const auto& [controlling, unused] : groups) {
    int32_t cid = static_cast<int32_t>(controlling);
    auto pit = present.find(cid);
    if (pit == present.end()) {
      continue;
    }
    cfg::PortProfileID currentProfile;
    if (!apache::thrift::TEnumTraits<cfg::PortProfileID>::findValue(
            pit->second.profileId.c_str(), &currentProfile)) {
      continue;
    }
    const auto& cEntry = mapping.getPlatformPort(cid);
    for (auto sp : mapping.getSubsumedPorts(controlling, currentProfile)) {
      int32_t sid = static_cast<int32_t>(sp);
      if (present.find(sid) != present.end()) {
        continue; // S must be absent
      }
      const auto& subMapping = mapping.getPlatformPort(sid);
      if (subMapping.mapping()->portType() != cfg::PortType::INTERFACE_PORT) {
        continue;
      }
      auto subProfiles = supportedProfileNames(mapping, sid);
      if (subProfiles.empty()) {
        continue;
      }
      // Find a profile C supports that no longer subsumes S and that S also
      // supports -- so one command can narrow C and create S at that profile.
      for (const auto& [narrowId, unused2] : *cEntry.supportedProfiles()) {
        auto narrowName = apache::thrift::util::enumNameSafe(narrowId);
        if (narrowName == pit->second.profileId) {
          continue;
        }
        if (subProfiles.find(narrowName) == subProfiles.end()) {
          continue; // S must support the profile we apply to it
        }
        const auto freed = mapping.getSubsumedPorts(controlling, narrowId);
        if (std::find(freed.begin(), freed.end(), sp) != freed.end()) {
          continue; // still subsumes S
        }
        return CreatableCandidate{
            pit->second.name,
            pit->second.profileId,
            *subMapping.mapping()->name(),
            *subProfiles.begin(),
            std::move(narrowName)};
      }
    }
  }
  return std::nullopt;
}

bool Fboss2IntegrationTest::waitForPortAbsent(
    const std::string& portName) const {
  for (int attempt = 0; attempt < 15; ++attempt) {
    try {
      getPortRunningInfo(portName);
    } catch (const std::runtime_error&) {
      return true;
    }
    /* sleep override */ std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  return false;
}

folly::dynamic Fboss2IntegrationTest::waitForRunningConfig(
    const std::function<bool(const folly::dynamic&)>& condition,
    std::chrono::seconds timeout,
    std::chrono::seconds interval) const {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  folly::dynamic last = folly::dynamic::object();
  bool everFetched = false;
  do {
    try {
      last = getRunningConfig();
      everFetched = true;
      if (condition(last)) {
        return last;
      }
      XLOG(DBG1) << "Running-config condition not yet met, retrying in "
                 << interval.count() << "s...";
    } catch (const std::exception& e) {
      XLOG(DBG1) << "Failed to fetch running config (" << e.what()
                 << "), retrying in " << interval.count() << "s...";
    }
    /* sleep override */ std::this_thread::sleep_for(interval);
  } while (std::chrono::steady_clock::now() < deadline);
  XLOG(WARN) << "Timed out waiting for running-config condition after "
             << timeout.count() << "s"
             << (everFetched ? "" : " (config never successfully fetched)")
             << "; returning last observed config";
  return last;
}

folly::dynamic Fboss2IntegrationTest::getNdpConfig(
    const folly::dynamic& runningConfig,
    int intfID) {
  if (!runningConfig.isObject() || !runningConfig.count("sw")) {
    return folly::dynamic::object();
  }
  const auto& sw = runningConfig["sw"];
  if (!sw.count("interfaces")) {
    return folly::dynamic::object();
  }
  for (const auto& iface : sw["interfaces"]) {
    if (iface.count("intfID") && iface["intfID"].asInt() == intfID) {
      return iface.count("ndp") ? iface["ndp"] : folly::dynamic::object();
    }
  }
  return folly::dynamic::object();
}

int Fboss2IntegrationTest::pickUnusedVlanId() const {
  auto config = getRunningConfig();
  if (!config.isObject() || !config.count("sw")) {
    return 0;
  }
  const auto& sw = config["sw"];

  std::set<int> usedVlans;
  std::set<int> usedIntfIds;
  auto collect =
      [&sw](const char* array, const char* field, std::set<int>& into) {
        if (!sw.count(array)) {
          return;
        }
        for (const auto& item : sw[array]) {
          if (item.count(field)) {
            into.insert(item[field].asInt());
          }
        }
      };
  if (sw.count("defaultVlan")) {
    usedVlans.insert(sw["defaultVlan"].asInt());
  }
  collect("vlans", "id", usedVlans);
  collect("vlanPorts", "vlanID", usedVlans);
  collect("interfaces", "vlanID", usedVlans);
  collect("ports", "ingressVlan", usedVlans);
  collect("interfaces", "intfID", usedIntfIds);

  for (int candidate = kTestVlanMin; candidate <= kTestVlanMax; ++candidate) {
    if (!usedVlans.count(candidate) && !usedIntfIds.count(candidate)) {
      return candidate;
    }
  }
  return 0;
}

void Fboss2IntegrationTest::deleteVlanIfPresent(int vlanId) const {
  auto config = getRunningConfig();
  if (!config.isObject() || !config.count("sw") ||
      !config["sw"].count("vlans")) {
    return;
  }
  const auto& vlans = config["sw"]["vlans"];
  const bool present = std::any_of(
      vlans.begin(), vlans.end(), [vlanId](const folly::dynamic& vlan) {
        return vlan.count("id") && vlan["id"].asInt() == vlanId;
      });
  if (!present) {
    return;
  }

  auto result = runCli({"delete", "vlan", std::to_string(vlanId)});
  if (result.exitCode != 0) {
    XLOG(WARN) << "delete vlan " << vlanId
               << " failed, discarding session: " << result.stderr;
    discardSession();
    return;
  }
  commitConfig();
  XLOG(INFO) << "Removed VLAN " << vlanId << " and its backing interface";
}

int Fboss2IntegrationTest::ensureUnderlayIntfId(
    std::optional<int> vlanId) const {
  const int vid = vlanId.value_or(pickUnusedVlanId());
  if (vid == 0) {
    throw std::runtime_error(
        fmt::format(
            "No VLAN ID free in [{}, {}] for the tunnel underlay",
            kTestVlanMin,
            kTestVlanMax));
  }
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  auto [created, vlan] = VlanManager::createVlan(swConfig, VlanID(vid));
  // Pointer into swConfig.vlans() — do not hold across further mutations.
  (void)vlan;

  // VlanManager either created a new cfg::Interface for this VLAN or left
  // an existing one in place. Either way, look it up by vlanID.
  for (const auto& intf : *swConfig.interfaces()) {
    if (*intf.vlanID() == vid) {
      if (created) {
        XLOG(INFO) << "Created VLAN " << vid << " with interface "
                   << *intf.intfID() << " for tunnel underlay";
        session.saveConfig();
      }
      return *intf.intfID();
    }
  }
  throw std::runtime_error(
      fmt::format(
          "VlanManager did not produce a backing interface for VLAN {}", vid));
}

std::string Fboss2IntegrationTest::findIpv6OnIntf(int intfId) const {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  for (const auto& intf : *swConfig.interfaces()) {
    if (*intf.intfID() != intfId) {
      continue;
    }
    for (const auto& addr : *intf.ipAddresses()) {
      auto slashPos = addr.find('/');
      std::string ip =
          (slashPos != std::string::npos) ? addr.substr(0, slashPos) : addr;
      if (folly::IPAddressV6::validate(ip)) {
        return ip;
      }
    }
    break;
  }
  // Test devices (e.g. NH-4010-F lab units) may have a virtual interface with
  // no configured addresses. Fall back to a documentation IPv6 (RFC 3849) so
  // tests still exercise the CLI path; the agent does not validate that dstIp
  // exists on the underlay interface.
  return "2001:db8::1";
}

} // namespace facebook::fboss

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG2; default:async=true");

int main(int argc, char* argv[]) {
  // Initialize gtest first so it consumes --gtest_* flags before folly::init
  ::testing::InitGoogleTest(&argc, argv);

  // Initialize folly singletons before using CLI components
  folly::Init init(&argc, &argv, /* removeFlags = */ false);

  return RUN_ALL_TESTS();
}
