#include "fboss/fsdb/server/Server.h"
#include <fb303/ThreadCachedServiceData.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>

namespace facebook::fboss::fsdb {

void setVersionString() {}

void startThriftServer(
    std::shared_ptr<apache::thrift::ThriftServer> server,
    std::shared_ptr<ServiceHandler> handler) {
  facebook::fb303::ThreadCachedServiceData::get()->startPublishThread(
      std::chrono::milliseconds(1000));

  auto evbThread =
      std::make_shared<folly::ScopedEventBaseThread>("fsdbSigHandlerThread");
  SignalHandler signalHandler(
      evbThread->getEventBase(), [server]() { server->stop(); });
  server->serve();
}

} // namespace facebook::fboss::fsdb
