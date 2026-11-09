/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/fb303counters/CmdShowFb303Counters.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/TApplicationException.h>
#include <algorithm>
#include <stdexcept>

#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"

namespace facebook::fboss {

namespace {

using CountersBySource =
    std::vector<std::pair<std::string, std::map<std::string, int64_t>>>;

// Service name -> thrift port. Ports are deliberately not part of the command
// surface; callers name a service.
//
// The platform entries (led..platform_manager) answer fb303 only in internal
// builds: their handler comes from ServiceFrameworkLight in
// fboss/platform/helpers/facebook/Init.cpp, and the OSS runThriftService
// installs none. agent, qsfp and fsdb serve fb303 from their own thrift
// handlers and so work in either build. bgpd's handler only inherits the fb303
// methods without implementing them; OSS bgpd multiplexes fb303::BaseService
// in front of it (neteng/fboss/bgp/cpp/MainOSS.cpp).
const std::vector<std::pair<std::string, uint16_t>>& serviceTable() {
  static const std::vector<std::pair<std::string, uint16_t>> kServices = {
      {"agent", 5909},
      {"fsdb", 5908},
      {"qsfp", 5910},
      {"led", 5930},
      {"sensor", 5970},
      {"data_corral", 5971},
      {"fan", 5972},
      // Only runs on platforms with rack power hardware (RS485/Modbus); on
      // others the connect is refused.
      {"rackmon", 5973},
      {"platform_manager", 5975},
      {"bgp", 6909},
  };
  return kServices;
}

std::string knownServices() {
  std::string names;
  for (const auto& [name, _] : serviceTable()) {
    if (!names.empty()) {
      names.append(", ");
    }
    names.append(name);
  }
  return names;
}

uint16_t portForService(const std::string& service) {
  for (const auto& [name, port] : serviceTable()) {
    if (name == service) {
      return port;
    }
  }
  throw std::invalid_argument(
      "unknown --service '" + service +
      "', expected one of: " + knownServices());
}

// getRegexCounters full-matches the whole counter name, so a bare substring
// such as 'link_state\.flap' matches nothing and returns an empty map with no
// error. Wrap so --regex searches the way grep does. Anchors still behave:
// ".*(^foo$).*" is equivalent to "^foo$", since the surrounding ".*" can match
// empty.
std::string searchPattern(const std::string& regex) {
  return ".*(" + regex + ").*";
}

std::map<std::string, int64_t> fetch(
    apache::thrift::Client<FbossCtrl>& client,
    const std::string& regex) {
  std::map<std::string, int64_t> counters;
  if (regex.empty()) {
    client.sync_getCounters(counters);
  } else {
    // Filter server side so the switch does not serialize the full set.
    client.sync_getRegexCounters(counters, searchPattern(regex));
  }
  return counters;
}

// In multi-switch mode the ASIC counters live in the HwAgent processes while
// SwAgent keeps the software side, so querying only SwAgent returns half the
// picture. In mono mode SwAgent also binds the HwAgent port and serves both,
// so there is nothing to fan out to.
CountersBySource fetchAgent(
    const HostInfo& hostInfo,
    const std::string& regex) {
  CountersBySource results;
  // Isolate the SwAgent query the way each HwAgent query is isolated. SwAgent
  // holds only the software side, so letting a transient failure there abort
  // the call would discard the very ASIC counters this fan-out exists for.
  try {
    auto swClient =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    results.emplace_back("swagent", fetch(*swClient, regex));
  } catch (const std::exception& ex) {
    XLOG(ERR) << "swagent unreachable: " << ex.what();
  }

  // Both of these ask SwAgent over thrift (getMultiSwitchRunState), so neither
  // can answer when SwAgent is itself the thing that is down. Guard them
  // together: on failure keep whatever SwAgent counters we already have rather
  // than letting the throw discard them.
  int numHwSwitches = 0;
  try {
    if (!utils::isMultiSwitchEnabled(hostInfo)) {
      return results;
    }
    numHwSwitches = utils::getNumHwSwitches(hostInfo);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "could not read multi-switch state: " << ex.what();
    return results;
  }
  for (int i = 0; i < numHwSwitches; ++i) {
    auto label = "hwagent" + std::to_string(i);
    try {
      auto hwClient =
          utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo, i);
      results.emplace_back(label, fetch(*hwClient, regex));
    } catch (const std::exception& ex) {
      // Report rather than skip silently: a missing HwAgent is often the thing
      // being investigated.
      XLOG(ERR) << label << " unreachable: " << ex.what();
    }
  }
  return results;
}

CountersBySource fetchService(
    const HostInfo& hostInfo,
    const std::string& service,
    const std::string& regex) {
  if (service == "agent") {
    return fetchAgent(hostInfo, regex);
  }
  // Thrift dispatches by method name and getCounters/getRegexCounters are
  // fb303 methods, so one client type reaches every service; only the port
  // differs. FbossCtrl is used because it is already linked here.
  auto options =
      utils::ConnectionOptions(hostInfo.getIp().str(), portForService(service));
  try {
    auto client = utils::tryCreateEncryptedClient<FbossCtrl>(options);
    if (!client) {
      throw std::runtime_error("client creation returned null");
    }
    return {{service, fetch(*client, regex)}};
  } catch (const apache::thrift::TApplicationException& ex) {
    // An application error is a reply, so the service is up. UNKNOWN_METHOD
    // means it has no fb303 handler, e.g. an OSS bgpd that predates one.
    const std::string reason =
        ex.getType() == apache::thrift::TApplicationException::UNKNOWN_METHOD
        ? " is running but does not serve fb303 counters: "
        : " is running but failed the counter query: ";
    throw std::runtime_error(
        service + " on port " + std::to_string(portForService(service)) +
        reason + ex.what());
  } catch (const std::exception& ex) {
    // A refused connect is expected for a service that is not running on this
    // platform -- rackmon needs rack power hardware. Report it plainly rather
    // than letting a raw thrift exception escape queryClient.
    throw std::runtime_error(
        service + " unreachable on port " +
        std::to_string(portForService(service)) + ": " + ex.what());
  }
}

} // namespace

CmdShowFb303Counters::RetType CmdShowFb303Counters::createModel(
    const CountersBySource& bySource) {
  RetType model;
  for (const auto& [source, counters] : bySource) {
    for (const auto& [name, value] : counters) {
      cli::Fb303CounterEntry entry;
      entry.source() = source;
      entry.name() = name;
      entry.value() = value;
      model.counters()->push_back(std::move(entry));
    }
  }
  return model;
}

CmdShowFb303Counters::RetType CmdShowFb303Counters::queryClient(
    const HostInfo& hostInfo) {
  auto localOptions = CmdLocalOptions::getInstance();
  auto service = localOptions->getLocalOption(
      "show_fb303-counters", kFb303CountersServiceOpt);
  auto regex = localOptions->getLocalOption(
      "show_fb303-counters", kFb303CountersRegexOpt);
  // The default lives solely in the LocalOptions entry in the traits, which is
  // also what --help reports; CLI11 applies it, so no fallback is needed here.
  return createModel(fetchService(hostInfo, service, regex));
}

void CmdShowFb303Counters::printOutput(
    const RetType& model,
    std::ostream& out) {
  utils::Table table;
  table.setHeader({"Source", "Counter", "Value"});
  for (const auto& entry : *model.counters()) {
    table.addRow(
        {*entry.source(), *entry.name(), std::to_string(*entry.value())});
  }
  out << table << std::endl;
}

std::string_view CmdShowFb303CountersTraits::description() {
  return "Dumps raw fb303 counters from a FBOSS service, unlike the curated per-object views such as `show interface counters`. Select the process with --service (agent, fsdb, qsfp, led, sensor, data_corral, fan, rackmon, platform_manager, bgp) and narrow the result with --regex, which is applied on the switch so it does not serialize its whole counter set. With --service agent on a multi-switch platform the ASIC counters live in the HwAgent processes rather than SwAgent, so those are collected too and each row is labelled with the process it came from. Note the platform services expose counters only in internal builds.";
}

CmdShowFb303Counters::RetType CmdShowFb303Counters::sampleModel() {
  CountersBySource bySource = {
      {"swagent",
       {{"eth1/1/1.link_state.flap.sum", 3},
        {"eth1/1/1.link_state.flap.sum.60", 0}}},
      {"hwagent0",
       {{"eth1/1/1.fec_uncorrectable_errors.sum", 41},
        {"eth1/1/1.fec_uncorrectable_errors.sum.60", 0}}},
  };
  return createModel(bySource);
}

// Explicit template instantiation
template void
CmdHandler<CmdShowFb303Counters, CmdShowFb303CountersTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowFb303Counters, CmdShowFb303CountersTraits>::getValidFilters();

} // namespace facebook::fboss
