// Copyright 2004-present Facebook. All Rights Reserved.
//
//
#include "fboss/platform/helpers/Init.h"
#include "fboss/platform/helpers/InitCli.h"
#include "fboss/platform/helpers/PlatformThriftAcceptor.h"
#include "fboss/platform/helpers/PlatformThriftAcceptorUtil.h"

#include <folly/init/Init.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp2/async/MultiplexAsyncProcessor.h>

#include "common/fb303/cpp/FacebookBase2.h"
#include "fboss/lib/ThriftServiceUtils.h"

DEFINE_bool(
    platform_enable_thrift_acceptor,
    false,
    "If set, install a connection-level acceptor that admits Thrift "
    "connections only from loopback or --platform_trusted_subnets and rejects "
    "all others. Off by default to preserve existing OSS behavior.");

DEFINE_string(
    platform_trusted_subnets,
    "",
    "Comma-separated CIDR subnets, in addition to loopback (which is always "
    "permitted), allowed to connect when --platform_enable_thrift_acceptor is "
    "set. FBOSS control traffic is IPv6-only; e.g. \"2001:db8::/32\".");

namespace facebook::fboss::platform::helpers {

void init(int* argc, char*** argv) {
  folly::unsafe_unscoped_init(argc, argv, true);
}

void initCli(int* argc, char*** argv, const std::string&) {
  folly::unsafe_unscoped_init(argc, argv, true);
}

std::string getBuildVersion() {
  return "Not implemented";
}

std::string getBuildSummary() {
  return "Not implemented";
}

void runThriftService(
    std::shared_ptr<apache::thrift::ThriftServer> server,
    std::shared_ptr<apache::thrift::ServerInterface> handler,
    const std::string& serviceName,
    uint32_t port) {
  // Setup thrift server
  facebook::fboss::ThriftServiceUtils::setPreferredEventBaseBackend(*server);
  server->setPort(port);

  // The platform services already write counters to fbData from common code
  // (sensor readings, fan RPM, platform_manager exploration timings), but
  // internally the fb303 handler that serves them is installed by
  // ServiceFrameworkLight, which has no OSS equivalent. Without one those
  // counters are written and then unreachable. Multiplex FacebookBase2 in so
  // getCounters/getRegexCounters resolve, mirroring what HwAgentMain does
  // under IS_OSS. Pushed last so the service's own handler wins any
  // method-name overlap.
  std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>> handlers{
      handler, std::make_shared<facebook::fb303::FacebookBase2>(serviceName)};
  server->setInterface(
      std::make_shared<apache::thrift::MultiplexAsyncProcessorFactory>(
          std::move(handlers)));

  server->setAllowPlaintextOnLoopback(true);

  auto evb = server->getEventBaseManager()->getEventBase();
  SignalHandler signalHandler(evb, server);

  if (FLAGS_platform_enable_thrift_acceptor) {
    auto trustedSubnets = parseTrustedSubnets(FLAGS_platform_trusted_subnets);
    XLOG(INFO) << "Thrift connection acceptor enabled: admitting loopback + "
               << trustedSubnets.size() << " trusted subnet(s)";
    server->setAcceptorFactory(
        std::make_shared<PlatformThriftAcceptorFactory>(
            server.get(), std::move(trustedSubnets)));
  }

  server->serve();
}

} // namespace facebook::fboss::platform::helpers
