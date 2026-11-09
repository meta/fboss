/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/SetupThrift.h"
#include "fboss/agent/AgentConfig.h"
#include "fboss/lib/ThriftMethodRateLimitSetup.h"

#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp2/async/MultiplexAsyncProcessor.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>

#include <stdexcept>
#include <variant>

#include "fboss/lib/ThriftServiceUtils.h"
#include "fboss/platform/helpers/PlatformThriftAcceptor.h"
#include "fboss/platform/helpers/PlatformThriftAcceptorUtil.h"

DEFINE_int32(thrift_idle_timeout, 60, "Thrift idle timeout in seconds.");
// Programming 16K routes can take 20+ seconds
DEFINE_int32(
    thrift_task_expire_timeout,
    30,
    "Thrift task expire timeout in seconds.");

// TODO: remove after netcastle changes are in place
DEFINE_bool(
    thrift_test_utils_thrift_handler,
    false,
    "Enable thrift handler for HW tests");

DEFINE_bool(
    hw_agent_for_testing,
    false,
    "Whether to prepare hw agent for testing. This includes "
    "1) Enable thrift handler for HW tests, "
    "2) Consume config file created by sw agent with overrides.");

DEFINE_bool(
    thrift_rate_limit_shadow_mode,
    true,
    "Run thrift rate limit in shadow mode");

DEFINE_bool(
    agent_enable_thrift_acceptor,
    false,
    "If set, install a connection-level acceptor that admits Thrift "
    "connections only from loopback or --agent_trusted_subnets and rejects all "
    "others. Off by default to preserve existing behavior.");

DEFINE_string(
    agent_trusted_subnets,
    "",
    "Comma-separated CIDR subnets, in addition to loopback (which is always "
    "permitted), allowed to connect when --agent_enable_thrift_acceptor is "
    "set. FBOSS control traffic is IPv6-only; e.g. \"2001:db8::/32\".");

namespace {
// The worst performance of programming acceptable route scale is 28s across
// our platforms in production. Hence setting the queue timeout to 30s to give
// small headroom.
constexpr auto kThriftServerQueueTimeout = std::chrono::seconds(30);
} // namespace

namespace facebook::fboss {
void markThriftMethodsInternalAndBypassLimits(
    apache::thrift::ThriftServer& server,
    const std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>&
        interfaces) {
  auto internalMethods = server.getInternalMethods();
  auto bypassMethods = server.getMethodsBypassMaxRequestsLimit();
  std::vector<std::string> methods;
  for (const auto& interface : interfaces) {
    const auto metadata = interface->createMethodMetadata();
    const auto* methodMap =
        std::get_if<apache::thrift::AsyncProcessorFactory::MethodMetadataMap>(
            &metadata);
    if (methodMap == nullptr) {
      throw std::logic_error("Thrift interface requires method metadata");
    }
    methods.reserve(methods.size() + methodMap->size());
    for (const auto& [method, _] : *methodMap) {
      methods.push_back(method);
    }
  }
  internalMethods.insert(methods.begin(), methods.end());
  bypassMethods.insert(methods.begin(), methods.end());
  server.setInternalMethods(std::move(internalMethods));
  server.setMethodsBypassMaxRequestsLimit(
      {bypassMethods.begin(), bypassMethods.end()});
}

std::unique_ptr<apache::thrift::ThriftServer> setupThriftServer(
    folly::EventBase& eventBase,
    const std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>&
        handlers,
    const std::vector<int>& ports,
    bool setupSSL) {
  // Start the thrift server
  auto server = std::make_unique<apache::thrift::ThriftServer>();
  ThriftServiceUtils::setPreferredEventBaseBackend(*server);

  server->setTaskExpireTime(
      std::chrono::milliseconds(FLAGS_thrift_task_expire_timeout * 1000));
  server->getEventBaseManager()->setEventBase(&eventBase, false);

  auto handler =
      std::make_shared<apache::thrift::MultiplexAsyncProcessorFactory>(
          handlers);

  server->setInterface(handler);

  server->setQueueTimeout(kThriftServerQueueTimeout);
  server->setSocketQueueTimeout(kThriftServerQueueTimeout);

  // Furthermore, if a request is already being processed, thrift expects
  // that to complete within JOIN_TIMEOUT as well or else Thrift server
  // will crash with FATAL error. Thrift library does not provide any API
  // to disable this mechanism.
  // Thus, set JOIN TIMEOUT to a very large value so it never kicks in.
  // This value is chosen to be > wrapper script timeout.
  // Note: this API must be invoked during thrift server setup i.e. before the
  // first serve().
  //
  // TODO: refactor BGP => Agent thrift timeout, wrapper script timeout
  // and JOIN TIMEOUT to a single source of truth in configerator.
  server->setWorkersJoinTimeout(std::chrono::seconds(140));

  if (setupSSL) {
    serverSSLSetup(*server);
  }

  setupThriftModules();

  std::vector<folly::SocketAddress> addresses;
  for (auto port : ports) {
    folly::SocketAddress address;
    address.setFromLocalPort(port);
    addresses.push_back(address);
  }
  server->setAddresses(addresses);

  // The agent FbossCtrl Thrift server binds all interfaces. Internal builds
  // authenticate connections via SSL + ThriftAclCheckerModule; the OSS build
  // has no such auth. When enabled, admit only loopback plus configured
  // trusted subnets, rejecting off-box peers at accept time before any RPC
  // dispatches.
  if (FLAGS_agent_enable_thrift_acceptor) {
    auto trustedSubnets =
        platform::helpers::parseTrustedSubnets(FLAGS_agent_trusted_subnets);
    XLOG(INFO) << "Thrift connection acceptor enabled: admitting loopback + "
               << trustedSubnets.size() << " trusted subnet(s)";
    server->setAcceptorFactory(
        std::make_shared<platform::helpers::PlatformThriftAcceptorFactory>(
            server.get(), std::move(trustedSubnets)));
  }
  server->setIdleTimeout(std::chrono::seconds(FLAGS_thrift_idle_timeout));

  std::map<std::string, double> method2QpsLimit = {};
  try {
    auto config = AgentConfig::fromDefaultFile();
    for (const auto& item : *config->thrift.thriftApiToRateLimitInQps()) {
      XLOG(DBG2) << "set rate limit " << item.second << " qps to "
                 << "thrift method " << item.first;
      method2QpsLimit[item.first] = item.second;
    }
  } catch (const std::exception&) {
    XLOG(ERR) << "cannot load thrift rate limit settings from agent config";
  }
  installThriftMethodRateLimit(
      *server, method2QpsLimit, FLAGS_thrift_rate_limit_shadow_mode);
  return server;
}
} // namespace facebook::fboss
