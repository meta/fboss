// Copyright 2004-present Facebook. All Rights Reserved.
#include "fboss/qsfp_service/QsfpServer.h"
#include <thrift/lib/cpp2/server/ThriftServer.h>
#include "fboss/qsfp_service/QsfpServiceHandler.h"

#include <folly/init/Init.h>

namespace facebook::fboss {

void setVersionInfo() {}

int qsfpServiceInit(int* argc, char*** argv) {
  folly::unsafe_unscoped_init(argc, argv, true);
  return 0;
}

int doServerLoop(
    std::shared_ptr<apache::thrift::ThriftServer> thriftServer,
    std::shared_ptr<QsfpServiceHandler>) {
  thriftServer->setup();
  folly::EventBase* evb = thriftServer->getEventBaseManager()->getEventBase();
  evb->loopForever();
  return 0;
}

void stopQsfpServerGracefully(
    std::shared_ptr<apache::thrift::ThriftServer> thriftServer) {
  thriftServer->stopListening();
  thriftServer->stop();
}

} // namespace facebook::fboss
