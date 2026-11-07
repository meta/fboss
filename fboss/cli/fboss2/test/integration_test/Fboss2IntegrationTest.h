// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <folly/dynamic.h>
#include <folly/json.h>
#include <folly/json/dynamic.h>
#include <gtest/gtest.h>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace facebook::fboss {

class PlatformMapping;

/**
 * Fboss2IntegrationTest is the base class for CLI end-to-end tests.
 *
 * Unlike the link tests, CLI tests don't need to be concerned with the SAI
 * or what SAI we're using, since we're testing the CLI and the CLI is
 * largely platform independent.
 *
 * The tests run against a live FBOSS agent and execute actual CLI commands
 * by directly invoking the CLI library code (not spawning a subprocess).
 */
class Fboss2IntegrationTest : public ::testing::Test {
 public:
  /**
   * Represents the result of a CLI command execution.
   */
  struct Result {
    int exitCode;
    std::string stdout;
    std::string stderr;
  };

  /**
   * Represents a network interface from the output of 'show interface'.
   */
  struct Interface {
    std::string name;
    std::string status;
    std::string speed;
    std::optional<int> vlan;
    int mtu{};
    std::vector<std::string> addresses;
    std::string description;
  };

  ~Fboss2IntegrationTest() override = default;

 protected:
  void SetUp() override;
  void TearDown() override;

  /**
   * Run a CLI command with the given arguments.
   * The --fmt json flag is automatically prepended.
   * @param args The command arguments (e.g., {"show", "interface"})
   * @return Result with exit code, stdout, and stderr
   */
  Result runCli(const std::vector<std::string>& args) const;

  /**
   * Run a CLI command and parse the JSON output.
   * Throws if the command fails or output is not valid JSON.
   * @param args The command arguments
   * @return Parsed JSON as folly::dynamic
   */
  folly::dynamic runCliJson(const std::vector<std::string>& args) const;

  /**
   * Fetch the agent's running config via thrift and return it parsed.
   */
  folly::dynamic getRunningConfig() const;

  /**
   * Read a top-level field from the running config's "sw" object.
   *
   * The return type is controlled by the template parameter:
   *   getSwConfigField<int>("arpTimeoutSeconds")
   *   getSwConfigField<bool>("enableLldp")
   *   getSwConfigField<std::string>("loadBalancerPoolName")
   *   getSwConfigField<std::string>("optionalField", "default")
   */
  template <typename T>
  T getSwConfigField(const std::string& field) const {
    auto result = getSwConfigFieldOpt<T>(field);
    if (!result) {
      throw std::runtime_error(
          "Running config 'sw' missing field '" + field + "'");
    }
    return std::move(*result);
  }

  template <typename T>
  T getSwConfigField(const std::string& field, T defaultValue) const {
    return getSwConfigFieldOpt<T>(field).value_or(std::move(defaultValue));
  }

  /**
   * Fetch the agent's switch state for the given thrift path via
   * getCurrentStateJSON and return it parsed.
   */
  folly::dynamic getSwitchState(const std::string& path) const;

  /**
   * Read a field from switchSettingsMap in the agent's programmed switch
   * state.  Returns a map of SwitchIdList -> value for every entry that
   * contains the field, so callers can verify consistency across all
   * switches in multi-switch mode.
   *
   * The value type is controlled by the template parameter:
   *   getSwitchSettingsField<int>("arpTimeout")
   *   getSwitchSettingsField<bool>("qcmEnable")
   *   getSwitchSettingsField<std::string>("switchDrainState")
   */
  template <typename T>
  std::map<std::string, T> getSwitchSettingsField(
      const std::string& field) const {
    auto settingsMap = getSwitchState("switchSettingsMap");
    std::map<std::string, T> result;
    for (const auto& [switchIdList, settings] : settingsMap.items()) {
      if (settings.isObject() && settings.count(field)) {
        std::string key = switchIdList.asString();
        if constexpr (std::is_same_v<T, bool>) {
          result[key] = settings[field].asBool();
        } else if constexpr (std::is_integral_v<T>) {
          result[key] = static_cast<T>(settings[field].asInt());
        } else if constexpr (std::is_same_v<T, std::string>) {
          result[key] = settings[field].asString();
        } else if constexpr (std::is_same_v<T, double>) {
          result[key] = settings[field].asDouble();
        } else {
          static_assert(
              !sizeof(T), "Unsupported type for getSwitchSettingsField");
        }
      }
    }
    if (result.empty()) {
      throw std::runtime_error(
          "switchSettingsMap has no entry with field '" + field + "'");
    }
    return result;
  }

  /**
   * Assert that a switchSettingsMap field has the expected value across all
   * switch IDs. Fails the test with a descriptive message if any entry
   * differs.
   */
  template <typename T>
  void verifySwitchSettingsField(const std::string& field, const T& expected)
      const {
    auto values = getSwitchSettingsField<T>(field);
    for (const auto& [switchIdList, value] : values) {
      EXPECT_EQ(value, expected)
          << "switchSettingsMap[" << switchIdList << "]." << field << " = "
          << value << ", expected " << expected;
    }
  }

  /**
   * Return the L3 interfaceId for the L3 interface associated with the named
   * port. Looks up the agent's getAllInterfaces() and matches on portNames.
   * @throws std::runtime_error if no interface is associated with the port.
   */
  int getInterfaceIdForPort(const std::string& portName) const;

  /**
   * Find a (vlanId, portName) pair where vlanId appears in sw.vlans and the
   * port is a member of that VLAN via sw.vlanPorts. Returns nullopt on
   * switches configured in a port-based / L3-routed style with no L2 VLAN
   * memberships — tests that need L2-VLAN commands (e.g. 'config vlan <id>
   * port <name> taggingMode', 'config vlan <id> static-mac ...') should
   * GTEST_SKIP when this returns nullopt.
   */
  std::optional<std::pair<int, std::string>> findConfiguredVlanPort() const;

  /**
   * Run a shell command and return the result.
   * @param args Command and arguments
   * @return Result with exit code, stdout, and stderr
   */
  Result runCmd(const std::vector<std::string>& args) const;

  /**
   * Get information about a specific interface.
   * @param interfaceName The interface name (e.g., "eth1/1/1")
   * @return Interface object with the interface details
   */
  Interface getInterfaceInfo(const std::string& interfaceName) const;

  /**
   * Get all interfaces from the system.
   * @return Map of interface name to Interface object
   */
  std::map<std::string, Interface> getAllInterfaces() const;

  /**
   * Pick a random INTERFACE_PORT for testing and return its name.
   *
   * Candidates come from the agent's getAllPortInfo() and are restricted to
   * INTERFACE_PORT ports, so the returned port is always L3-resolvable via
   * getInterfaceIdForPort(). (Other port types such as MANAGEMENT_PORT may
   * carry a virtual L3 interface whose member ports the agent omits from
   * getAllInterfaces(), which would break port->interface lookups.) Ports that
   * are operationally up are strongly preferred — if at least one is up, a
   * random up port is chosen; otherwise a random INTERFACE_PORT is chosen.
   * Throws if no INTERFACE_PORT exists.
   *
   * The selection is randomized (thread-local mt19937) to reduce the chance
   * of piling test load onto the same port across back-to-back runs.
   *
   * Callers that need the full Interface object (vlan/addresses/description)
   * should pass the returned name to getInterfaceInfo().
   */
  std::string getRandomInterfacePortName() const;

  /**
   * Find the first ethernet interface that has a non-zero MTU reported by
   * 'show interface'. This implies the interface has a configured L3
   * cfg::Interface entry in the agent, making it suitable for MTU tests.
   * Returns std::nullopt if no such interface exists (test should GTEST_SKIP).
   */
  std::optional<Interface> findFirstEthInterfaceWithMtu() const;

  /**
   * Commit the current configuration session.
   */
  void commitConfig() const;

  /**
   * Get the MTU of a kernel interface using 'ip -json link'.
   * @param vlanId The VLAN ID (interface name will be fboss<vlanId>)
   * @return MTU value
   * @throws std::runtime_error if interface not found
   */
  int getKernelInterfaceMtu(int vlanId) const;

  /**
   * Running port info returned from a direct Thrift call to the agent.
   */
  struct PortRunningInfo {
    int64_t speedMbps{0};
    std::string profileId;
  };

  /**
   * Poll getInterfaceInfo() until condition(info) is true or timeout expires.
   * Returns the last observed Interface so callers can assert on the value.
   */
  Interface waitForInterfaceInfo(
      const std::string& interfaceName,
      const std::function<bool(const Interface&)>& condition,
      std::chrono::seconds timeout = std::chrono::seconds(30),
      std::chrono::seconds interval = std::chrono::seconds(2)) const;

  /**
   * Poll getKernelInterfaceMtu() until condition(mtu) is true or timeout
   * expires. Returns the last observed MTU value.
   */
  int waitForKernelMtu(
      int vlanId,
      const std::function<bool(int)>& condition,
      std::chrono::seconds timeout = std::chrono::seconds(30),
      std::chrono::seconds interval = std::chrono::seconds(2)) const;

  /**
   * Query the agent directly via getAllPortInfo() and return the running
   * speed and profileID for the named port.
   * @param portName The port name (e.g., "eth1/1/1")
   * @throws std::runtime_error if the port is not found
   */
  PortRunningInfo getPortRunningInfo(const std::string& portName) const;

  /**
   * Poll getPortRunningInfo() until condition(info) returns true or timeout
   * expires. Returns the last observed PortRunningInfo so callers can assert
   * on the final value even when the condition is not met.
   * @param portName  The port name (e.g., "eth1/1/1")
   * @param condition Predicate evaluated on each poll result
   * @param timeout   Max time to wait (default: 30 s)
   * @param interval  Sleep between polls (default: 2 s)
   */
  PortRunningInfo waitForPortRunningInfo(
      const std::string& portName,
      const std::function<bool(const PortRunningInfo&)>& condition,
      std::chrono::seconds timeout = std::chrono::seconds(30),
      std::chrono::seconds interval = std::chrono::seconds(2)) const;

  /**
   * A port currently present in the agent, keyed elsewhere by logical id.
   */
  struct PresentPort {
    std::string name;
    std::string profileId; // enum name string
  };

  /**
   * A creatable-port scenario derived from the platform mapping: an absent
   * subport whose controlling port is present. `controllingName` is the present
   * controlling port; `subportName` is the absent INTERFACE_PORT to create.
   * `narrowProfile` is a profile the controlling port supports that does NOT
   * subsume the subport AND that the subport itself supports -- so applying it
   * to both ports narrows the controlling port and creates the freed subport in
   * a single command. `subportProfile` is any profile the subport supports.
   */
  struct CreatableCandidate {
    std::string controllingName;
    std::string controllingProfile; // controlling port's current profile
    std::string subportName;
    std::string subportProfile; // a profile the absent subport supports
    std::string narrowProfile; // frees subport and is supported by it
  };

  /**
   * Build a PlatformMapping from the running agent (via getPlatformMapping).
   */
  PlatformMapping fetchPlatformMapping() const;

  /**
   * Ports currently present in the agent (subsumed/removed ports are absent),
   * keyed by logical id.
   */
  std::map<int32_t, PresentPort> fetchPresentPorts() const;

  /**
   * Enum-name strings of every profile the platform port `id` supports.
   */
  std::set<std::string> supportedProfileNames(
      const PlatformMapping& mapping,
      int32_t id) const;

  /**
   * A present controlling port C that currently subsumes an absent subport S,
   * and a narrower profile C supports that does NOT subsume S and that S also
   * supports -- so applying that profile to both C and S in one command narrows
   * C and creates S. Returns nullopt if no such scenario exists on this
   * platform.
   */
  std::optional<CreatableCandidate> findFreeableCandidate() const;

  /**
   * Poll until the named port disappears from getAllPortInfo (i.e. it was
   * removed from the config). Returns false if it is still present after the
   * timeout.
   */
  bool waitForPortAbsent(const std::string& portName) const;

  /**
   * Poll getRunningConfig() until condition(config) is true or timeout
   * expires. Thrift exceptions during a poll are swallowed and treated as
   * condition-not-met — the agent may still be coming back up after a
   * commit-triggered restart. Returns the last successfully fetched config
   * (or an empty object if none was fetched) so callers can assert on the
   * observed value when the condition was not met in time.
   */
  folly::dynamic waitForRunningConfig(
      const std::function<bool(const folly::dynamic&)>& condition,
      std::chrono::seconds timeout = std::chrono::seconds(30),
      std::chrono::seconds interval = std::chrono::seconds(2)) const;

  /**
   * Look up the ndp sub-object for the L3 interface with the given intfID in
   * a running config (as returned by getRunningConfig()). Returns an empty
   * object if the interface or its ndp block is absent. Obtain the intfID
   * via getInterfaceIdForPort().
   */
  static folly::dynamic getNdpConfig(
      const folly::dynamic& runningConfig,
      int intfID);

  /**
   * Discard any pending config session by deleting session files.
   * This ensures each test starts with a fresh session based on current HEAD.
   */
  void discardSession() const;

  /**
   * Wait until the FBOSS agent is responsive (ready to serve thrift requests).
   * Polls 'show interface' until it succeeds or timeout is reached.
   * Use this after triggering a warmboot or coldboot restart.
   * @param timeout Maximum time to wait (default: 180 seconds)
   */
  void waitForAgentReady(
      std::chrono::seconds timeout = std::chrono::seconds(180)) const;

  /**
   * Bounds of the VLAN ID window tests may create in. See pickUnusedVlanId().
   */
  static constexpr int kTestVlanMin = 2000;
  static constexpr int kTestVlanMax = 2099;

  /**
   * Pick a VLAN ID that is unused in the running config and safe to create on
   * any platform. Returns 0 if nothing is available (caller should GTEST_SKIP).
   *
   * Creating a VLAN also creates a backing cfg::Interface, and TunManager
   * gives every L3 interface its own Linux routing table. On platforms without
   * enable_1to1_intf_route_table_mapping only tables 1-253 exist, and an
   * interface that maps outside that range aborts the agent unrecoverably
   * (T284228086).
   *
   * Rather than reproduce TunManager's mapping here, the window is kept narrow
   * enough that the answer is the same however the agent is configured: the
   * 2000 interface range starts at table 1, so [2000, 2099] can only ever
   * consume tables 1-100 — well inside 1-253, and clear of the tables the
   * 3000 (101+) and 4000 (201+) ranges use. Under
   * enable_1to1_intf_route_table_mapping the same IDs map to tables 2000-2099,
   * which are equally fine.
   *
   * Candidates are also rejected when the ID is already in use as an intfID by
   * a different VLAN — VlanManager would then allocate 5000 + vlanId instead,
   * which is out of range under both mappings.
   */
  int pickUnusedVlanId() const;

  /**
   * Remove a VLAN and its backing interface from the running config and
   * commit. A no-op when the VLAN is not present.
   *
   * Tests that create a VLAN must call this, otherwise the TUN interface the
   * agent created for it survives in the kernel across agent restarts.
   */
  void deleteVlanIfPresent(int vlanId) const;

  /**
   * Ensure a VLAN + backing cfg::Interface exists in the staged config, then
   * return that interface's intfID for use as the IP-in-IP tunnel underlay.
   *
   * If the VLAN is already present, the existing interface's intfID is
   * returned and the staged config is left unchanged. Otherwise the VLAN and
   * a barebone interface are inserted via VlanManager, the staged config is
   * persisted, and the new intfID is returned.
   *
   * Defaults to a VLAN chosen by pickUnusedVlanId().
   */
  int ensureUnderlayIntfId(std::optional<int> vlanId = std::nullopt) const;

  /**
   * Return the first IPv6 address (without prefix length) configured on the
   * interface with the given intfID. If the interface has no IPv6 address
   * (e.g. one freshly created by ensureUnderlayIntfId), a documentation IPv6
   * (RFC 3849) is returned as a safe stand-in — the agent does not validate
   * that dstIp exists on the underlay interface.
   */
  std::string findIpv6OnIntf(int intfId) const;

 private:
  template <typename T>
  std::optional<T> getSwConfigFieldOpt(const std::string& field) const {
    auto config = getRunningConfig();
    if (!config.isObject() || !config.count("sw")) {
      return std::nullopt;
    }
    const auto& sw = config["sw"];
    if (!sw.isObject() || !sw.count(field)) {
      return std::nullopt;
    }
    if constexpr (std::is_same_v<T, bool>) {
      return sw[field].asBool();
    } else if constexpr (std::is_integral_v<T>) {
      return static_cast<T>(sw[field].asInt());
    } else if constexpr (std::is_same_v<T, std::string>) {
      return sw[field].asString();
    } else if constexpr (std::is_same_v<T, double>) {
      return sw[field].asDouble();
    } else {
      static_assert(!sizeof(T), "Unsupported type for getSwConfigField");
    }
    return std::nullopt;
  }

  Interface parseInterfaceJson(const folly::dynamic& data) const;

  /**
   * Execute a CLI command by parsing args and running the command.
   * Captures stdout/stderr and returns the result.
   */
  Result executeCliCommand(const std::vector<std::string>& args) const;
};
} // namespace facebook::fboss
