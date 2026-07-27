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

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <folly/IPAddress.h>
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <re2/re2.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant> // NOLINT(misc-include-cleaner)

#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/cli/fboss2/CmdGlobalOptions.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/gen-cpp2/cli_types.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/utils/CmdClientUtils.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/PrbsUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/fsdb/if/gen-cpp2/FsdbService.h" // NOLINT(misc-include-cleaner)
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"

namespace facebook::fboss::utils {

// Returns true if the CLI is running locally on an FBOSS switch,
// false if running on a dev server.
// This is determined by checking for the presence of /etc/netwhoami,
// which is only present on FBOSS switches.
bool isRunningOnSwitch();

class CommunityList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ CommunityList(std::vector<std::string> v)
      : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_COMMUNITY_LIST;
};

class IPList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ IPList() : BaseObjectArgType() {}
  /* implicit */ IPList(std::vector<std::string> v) : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
};

class CIDRNetwork : public BaseObjectArgType<folly::CIDRNetwork> {
 public:
  /* implicit */ CIDRNetwork() : BaseObjectArgType() {}
  /* implicit */ CIDRNetwork(std::vector<std::string> v) {
    data_.reserve(v.size());
    for (const auto& network : v) {
      auto parsed = folly::IPAddress::tryCreateNetwork(network);
      if (parsed.hasError()) {
        throw std::runtime_error(
            fmt::format("Unable to parse network {}", network));
      }
      data_.push_back(parsed.value());
    }
  }

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_CIDR_NETWORK;
};

class IPV6List : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ IPV6List(std::vector<std::string> v) : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IPV6_LIST;
};

/**
 * Port names for commands. Accepts regular port names like 'eth1/5/3'
 * and aggregate port names like 'Port-Channel1'.
 * Validation against actual ports/aggregate ports happens later in the
 * command implementation.
 */
class PortList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ PortList() : BaseObjectArgType() {}
  /* implicit */ PortList(std::vector<std::string> ports) {
    // Basic validation - just check that ports are non-empty
    for (auto const& port : ports) {
      if (port.empty()) {
        throw std::invalid_argument("Port name cannot be empty");
      }
    }
    // deduplicate ports while ensuring order
    std::set<std::string> uniquePorts(ports.begin(), ports.end());
    data_ = std::vector<std::string>(uniquePorts.begin(), uniquePorts.end());
  }

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST;
};

/**
 * Whether input port name conforms to the required pattern
 * 'switchName:moduleNum/port/subport' For example,
 * rdsw001.n001.z004.snc1:eth1/5/3 will be parsed to five parts:
 * rdsw001.n001.z004.snc1 (switch name), eth(module name),
 * 1(module number), 5(port number), 3(subport number). Error will be
 * thrown if the port name is not valid.
 */
class SystemPortList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ SystemPortList() : BaseObjectArgType() {}
  /* implicit */ SystemPortList(const std::vector<std::string>& ports) {
    static const RE2 exp("([^:]+):([a-z]+)(\\d+)/(\\d+)/(\\d)");
    for (auto const& port : ports) {
      if (!RE2::FullMatch(port, exp)) {
        throw std::invalid_argument(
            folly::to<std::string>(
                "Invalid port name: ",
                port,
                "\nPort name must match 'switch:moduleNum/port/subport' pattern"));
      }
    }
    // deduplicate ports while ensuring order
    std::set<std::string> uniquePorts(ports.begin(), ports.end());
    data_ = std::vector<std::string>(uniquePorts.begin(), uniquePorts.end());
  }

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_SYSTEM_PORT_LIST;
};

class Message : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ Message(std::vector<std::string> v) : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;
};

// Custom type for VLAN ID argument with validation
class VlanIdValue : public BaseObjectArgType<int32_t> {
 public:
  /* implicit */ VlanIdValue( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v) {
    if (v.empty()) {
      throw std::invalid_argument("VLAN ID is required");
    }
    if (v.size() != 1) {
      throw std::invalid_argument(
          "Expected single VLAN ID, got: " + folly::join(", ", v));
    }

    try {
      int32_t vlanId = folly::to<int32_t>(v[0]);
      // VLAN IDs are typically 1-4094 (0 and 4095 are reserved)
      if (vlanId < kVlanIdMin || vlanId > kVlanIdMax) {
        throw std::invalid_argument(
            fmt::format(
                "VLAN ID must be between {} and {} inclusive, got: {}",
                kVlanIdMin,
                kVlanIdMax,
                vlanId));
      }
      data_.push_back(vlanId);
    } catch (const folly::ConversionError&) {
      throw std::invalid_argument("Invalid VLAN ID: " + v[0]);
    }
  }

  int32_t getVlanId() const {
    return data_[0];
  }

  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_VLAN_ID;
};

class VipInjectorID : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ VipInjectorID( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v)
      : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_VIP_INJECTOR_ID;
};

class PeerIdList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ PeerIdList(std::vector<std::string> v)
      : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PEERID_LIST;
};

class DebugLevel : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ DebugLevel(std::vector<std::string> v)
      : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_DEBUG_LEVEL;
};

class PrbsComponent : public BaseObjectArgType<phy::PortComponent> {
 public:
  /* implicit */ PrbsComponent(std::vector<std::string> components) {
    // existing helper function from PrbsUtils.h
    data_ = prbsComponents(components, false /* returnAllIfEmpty */);
  }

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_PRBS_COMPONENT;
};

class PhyChipType : public BaseObjectArgType<phy::DataPlanePhyChipType> {
 public:
  /* implicit */ PhyChipType(std::vector<std::string> chipTypes) {
    if (chipTypes.empty()) {
      iphyIncluded = true;
      xphyIncluded = true;
    } else {
      if (std::find(chipTypes.begin(), chipTypes.end(), "asic") !=
          chipTypes.end()) {
        iphyIncluded = true;
      }
      if (std::find(chipTypes.begin(), chipTypes.end(), "xphy") !=
          chipTypes.end()) {
        xphyIncluded = true;
      }
    }
  }

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_PHY_CHIP_TYPE;
  bool iphyIncluded{false};
  bool xphyIncluded{false};
};

class PrbsState : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ PrbsState(std::vector<std::string> args)
      : BaseObjectArgType(args) {
    if (args.empty()) {
      throw std::invalid_argument(
          "Incomplete command, expecting state <PRBSXX>");
    }
    auto state = folly::gen::from(args) |
        folly::gen::mapped([](const std::string& s) {
                   return boost::to_upper_copy(s);
                 }) |
        folly::gen::as<std::vector>();
    enabled = (state[0] != "OFF");
    if (enabled) {
      polynomial = apache::thrift::util::enumValueOrThrow<
          facebook::fboss::prbs::PrbsPolynomial>(state[0]);
    }
    if (state.size() <= 1) {
      // None of the generator or checker args are passed, therefore set both
      // the generator and checker
      generator = enabled;
      checker = enabled;
    } else {
      std::unordered_set<std::string> stateSet(state.begin() + 1, state.end());
      if (stateSet.find("GENERATOR") != stateSet.end()) {
        generator = enabled;
      }
      if (stateSet.find("CHECKER") != stateSet.end()) {
        checker = enabled;
      }
    }
  }

  bool enabled;
  facebook::fboss::prbs::PrbsPolynomial polynomial;
  std::optional<bool> generator;
  std::optional<bool> checker;
  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_PRBS_STATE;
};

class PortState : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ PortState(std::vector<std::string> v) : BaseObjectArgType(v) {
    if (v.empty()) {
      throw std::runtime_error(
          "Incomplete command, expecting 'state <enable|disable>'");
    }
    if (v.size() != 1) {
      throw std::runtime_error(
          folly::to<std::string>(
              "Unexpected state '",
              folly::join<std::string, std::vector<std::string>>(" ", v),
              "', expecting 'enable|disable'"));
    }

    portState = getPortState(v[0]);
  }
  bool portState;
  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_PORT_STATE;

 private:
  bool getPortState(std::string& v) {
    auto state = boost::to_upper_copy(v);
    if (state == "ENABLE") {
      return true;
    }
    if (state == "DISABLE") {
      return false;
    }
    throw std::runtime_error(
        folly::to<std::string>(
            "Unexpected state '", v, "', expecting 'enable|disable'"));
  }
};

class FanPwm : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ FanPwm(std::vector<std::string> v) : BaseObjectArgType(v) {
    if (v.empty()) {
      throw std::runtime_error(
          "Incomplete command, expecting 'fanhold <disable>|0|1|..|100'");
    }
    if (v.size() != 1) {
      throw std::runtime_error(
          folly::to<std::string>(
              "Unexpected fanhold '",
              folly::join<std::string, std::vector<std::string>>(" ", v),
              "', expecting 'disable|0|1|...|100'"));
    }

    pwm = getPwm(v[0]);
  }
  std::optional<int> pwm;
  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_FAN_PWM;

 private:
  std::optional<int> getPwm(std::string& v) {
    auto state = boost::to_upper_copy(v);
    if (state == "DISABLE") {
      return std::nullopt;
    }
    auto asint = folly::tryTo<int>(state);
    if (asint.hasValue()) {
      int value = asint.value();
      if (value >= 0 && value <= 100) {
        return value;
      }
    }

    throw std::runtime_error(
        folly::to<std::string>(
            "Unexpected fanhold '", v, "', expecting 'disable|0|1|...|100'"));
  }
};

class LinkDirection : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ LinkDirection(std::vector<std::string> v) {
    if (v.empty()) {
      throw std::runtime_error("Incomplete command, expecting '<system|line>'");
    }

    direction = getLinkDirection(v);
  }
  phy::Direction direction;
  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_LINK_DIRECTION;

 private:
  phy::Direction getLinkDirection(std::vector<std::string>& v) {
    if (std::find(v.begin(), v.end(), "line") != v.end()) {
      return phy::Direction::RECEIVE;
    } else if (std::find(v.begin(), v.end(), "system") != v.end()) {
      return phy::Direction::TRANSMIT;
    } else {
      throw std::runtime_error(
          folly::to<std::string>(
              "Unexpected direction '", v[0], "', expecting 'system|line'"));
    }
  }
};

class FsdbPath : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ FsdbPath(std::vector<std::string> fsdbPath) {
    if (fsdbPath.size() > 1) {
      throw std::runtime_error(
          "Argument for fsdb path needs to be a single string starting with /, path items separated by /\n"
          "Slashes inside path tokens such as eth2/1/1 need to be escaped"
          "For example \"/agent/switchState/ports/eth2\\/1\\/1\"");
    } else if (fsdbPath.size() == 1) {
      auto pathString = fsdbPath[0];
      // Split by SLASH, ignoring ones escaped by a BACK_SLASH
      boost::split_regex(
          fsdbPath, pathString, boost::basic_regex("(?<!\\\\)/"));
      folly::gen::from(fsdbPath) |
          // prefix SLASH is enforced to differentiate b/w path and subcommand
          // names, split will give an extranous empty string at the start of
          // path so filter that out
          folly::gen::filter([](const std::string& in) { return in != ""; }) |
          // unescape strings for things like eth2\/1\/1
          folly::gen::map([](const std::string& in) {
            auto strCopy = in;
            const RE2 re("\\\\/");
            RE2::GlobalReplace(&strCopy, re, "/");
            return strCopy;
          }) |
          folly::gen::appendTo(data_);
      // TODO: validate path matches model here
    }
  }
  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_FSDB_PATH;
};

class FsdbClientId : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ FsdbClientId(std::vector<std::string> fsdbClientId)
      : BaseObjectArgType(fsdbClientId) {
    // if there is no input, the default value will be given inside each command
  }

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_FSDB_CLIENT_ID;
};

class HwObjectList : public BaseObjectArgType<HwObjectType> {
 public:
  /* implicit */ HwObjectList() : BaseObjectArgType() {}
  /* implicit */ HwObjectList(std::vector<std::string> hwObjectTypeArgList) {
    for (auto const& hwObjectTypeArg : hwObjectTypeArgList) {
      HwObjectType hwObjectType =
          apache::thrift::util::enumValueOrThrow<HwObjectType>(hwObjectTypeArg);
      data_.push_back(hwObjectType);
    }
  }

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_HW_OBJECT_LIST;
};

class MirrorList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ MirrorList() : BaseObjectArgType() {}
  /* implicit */ MirrorList(std::vector<std::string> v)
      : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MIRROR_LIST;
};

class RevisionList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ RevisionList() : BaseObjectArgType() {}
  /* implicit */ RevisionList(const std::vector<std::string>& v);

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_REVISION_LIST;
};

std::vector<int32_t> getPortIDList(
    const std::vector<std::string>& ifList,
    std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries);
std::string getAddrStr(network::thrift::BinaryAddress addr);
std::string getAdminDistanceStr(AdminDistance adminDistance);
const std::string removeFbDomains(const std::string& host);
std::string getSpeedGbps(int64_t speedMbps);
std::string getl2EntryTypeStr(L2EntryType l2EntryType);
// Renders an ingress port class ID for display, e.g. "Restricted". Returns
// "--" when unset.
std::string getAclLookupClassPortStr(
    const std::optional<cfg::AclLookupClassPort>& lookupClassPort);

/**
 * Whether the first port name is smaller than or equal to the second port
 * name. For example, eth1/5/3 will be parsed to four parts: eth(module name),
 * 1(module number), 5(port number), 3(subport number). The two number will
 * first be compared by the alphabetical order of their module names. If the
 * module namea are the same, then the order will be decied by the numerical
 * values of their module number, port number, subport number in order.
 * Therefore, eth420/5/1 comes before fab3/9/1 as the module name of the
 * former one is alphabetically smaller. However, eth420/5/1 comes after
 * eth1/5/3 as the module number 420 is larger than 1. Accordingly, eth1/5/3
 * comes after eth1/5/1 as its subport is larger. With input array
 * ["eth1/5/1", "eth1/5/2", "eth1/5/3", "fab402/20/1", "eth1/10/1",
 * "eth1/4/1"], the expected returned order of this comparator will be
 * ["eth1/4/1", "eth1/5/1", "eth1/5/2", "eth1/5/3",  "eth1/10/1",
 * "fab402/20/1"].
 * @param nameA    The first port name
 * @param nameB    The second port name
 * @return         true if the first port name is smaller or equal to the
 * second port name
 */
bool comparePortName(
    const std::basic_string<char>& nameA,
    const std::basic_string<char>& nameB);

struct PortNameParts {
  std::string moduleName;
  int moduleNum{0};
  int portNum{0};
  int subportNum{0};

  auto tie() const {
    return std::tie(moduleName, moduleNum, portNum, subportNum);
  }
  bool operator<(const PortNameParts& other) const {
    return tie() < other.tie();
  }
};

/*
 * Parses a "moduleNum/port/subport" interface name (e.g. eth1/5/3). Returns
 * std::nullopt for names that don't follow the pattern, rather than throwing
 * the way comparePortName does.
 */
std::optional<PortNameParts> parsePortName(const std::string& name);

/*
 * Orders interfaces for display. Interface names are free-form -- SVIs are
 * named arbitrary things like "downlinks" and may be unset -- so names that
 * parse are ordered by their numeric module/port/subport components (front
 * panel order) and anything else falls back to the numeric port/interface ID.
 * Never throws.
 */
bool comparePortNameOrId(
    const std::string& nameA,
    int32_t idA,
    const std::string& nameB,
    int32_t idB);

bool compareSystemPortName(
    const std::basic_string<char>& nameA,
    const std::basic_string<char>& nameB);

std::optional<std::string> getMyHostname(const std::string& hostname);

std::string escapeDoubleQuotes(const std::string& cmd);
std::string getCmdToRun(const std::string& hostname, const std::string& cmd);
std::string runCmd(const std::string& cmd);
std::vector<std::string> getBgpDrainedInterafces(const HostInfo& hostInfo);
std::string getBgpSwitchDrainState(const HostInfo& hostInfo);

std::string getSubscriptionPathStr(const fsdb::OperSubscriberInfo& subscriber);
std::map<int16_t, std::vector<std::string>> getSwitchIndicesForInterfaces(
    const HostInfo& hostInfo,
    const std::vector<std::string>& interfaces);
Table::StyledCell styledBer(double ber);
Table::StyledCell styledFecTail(int tail);

cfg::SwitchType getSwitchType(apache::thrift::Client<FbossCtrl>& client);
cfg::SwitchType getSwitchType(
    std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo);
bool isVoqOrFabric(cfg::SwitchType switchType);
bool isFabricSwitch(cfg::SwitchType switchType);

std::map<std::string, FabricEndpoint> getFabricEndpoints(
    const HostInfo& hostInfo);

std::map<std::string, int64_t> getAgentFb303RegexCounters(
    const HostInfo& hostInfo,
    const std::string& regex);

std::unordered_map<std::string, std::vector<std::string>>
getCachedSwSwitchReachabilityInfo(
    const HostInfo& hostInfo,
    const std::vector<std::string>& switchNames);

std::unordered_map<std::string, std::vector<std::string>>
getUncachedSwitchReachabilityInfo(
    const HostInfo& hostInfo,
    const std::vector<std::string>& switchNames);

} // namespace facebook::fboss::utils
