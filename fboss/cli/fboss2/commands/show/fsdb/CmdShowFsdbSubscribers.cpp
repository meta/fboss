/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/fsdb/CmdShowFsdbSubscribers.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <folly/coro/BlockingWait.h>
#include <unistd.h>
#include "fboss/cli/fboss2/commands/show/fsdb/CmdShowFsdbUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/fsdb/if/gen-cpp2/FsdbService.h"

namespace facebook::fboss {

namespace {

// Most timestamp fields on OperSubscriberInfo are milliseconds-since-epoch
// (set via getCurrentTimeMs() in fboss/fsdb/oper/Subscription.h).
// "subscribedSince" is the historical exception and stores plain seconds.
// Helpers are shared with CmdShowFsdbPublishers via fsdb_cli_format in
// CmdShowFsdbUtils.h.

void printSubscriberDetail(
    const fsdb::OperSubscriberInfo& subscriber,
    std::ostream& out) {
  out << "" << std::endl;
  out << fmt::format(
             "Subscriber Id:                  {}",
             subscriber.subscriberId().value())
      << std::endl;
  out << fmt::format(
             "Type:                           {}",
             apache::thrift::util::enumNameSafe(
                 folly::copy(subscriber.type().value())))
      << std::endl;
  out << fmt::format(
             "Path:                           {}",
             utils::getSubscriptionPathStr(subscriber))
      << std::endl;
  out << fmt::format(
             "isStats:                        {}",
             folly::copy(subscriber.isStats().value()))
      << std::endl;
  out << fmt::format(
             "Subscription UID:               {}",
             fsdb_cli_format::optFieldToString<int64_t>(
                 subscriber.subscriptionUid()))
      << std::endl;
  out << fmt::format(
             "Serve Interval:                 {}",
             fsdb_cli_format::serveIntervalToString(
                 subscriber.serveIntervalSec()))
      << std::endl;

  std::string subscribedSince = "--";
  if (apache::thrift::get_pointer(subscriber.subscribedSince())) {
    subscribedSince = fsdb_cli_format::epochSecondsAsLocalTime(
        *apache::thrift::get_pointer(subscriber.subscribedSince()));
  }
  out << fmt::format("Subscribed Since:               {}", subscribedSince)
      << std::endl;

  out << fmt::format(
             "Initial Sync Completed At:      {}",
             subscriber.initialSyncCompletedAt().has_value()
                 ? fsdb_cli_format::epochMillisAsLocalTime(
                       subscriber.initialSyncCompletedAt().value())
                 : "--")
      << std::endl;
  out << fmt::format(
             "Last Update Enqueued At:        {}",
             subscriber.lastUpdateEnqueuedAt().has_value()
                 ? fsdb_cli_format::epochMillisAsLocalTime(
                       subscriber.lastUpdateEnqueuedAt().value())
                 : "--")
      << std::endl;
  out << fmt::format(
             "Last Update Written At:         {}",
             subscriber.lastUpdateWrittenAt().has_value()
                 ? fsdb_cli_format::epochMillisAsLocalTime(
                       subscriber.lastUpdateWrittenAt().value())
                 : "--")
      << std::endl;
  out << fmt::format(
             "Last Enqueued Update Pub At:    {}",
             subscriber.lastEnqueuedUpdatePublishedAt().has_value()
                 ? fsdb_cli_format::epochMillisAsLocalTime(
                       subscriber.lastEnqueuedUpdatePublishedAt().value())
                 : "--")
      << std::endl;
  out << fmt::format(
             "Last Heartbeat Sent At:         {}",
             subscriber.lastHeartbeatSentAt().has_value()
                 ? fsdb_cli_format::epochMillisAsLocalTime(
                       subscriber.lastHeartbeatSentAt().value())
                 : "--")
      << std::endl;
  out << fmt::format(
             "Staleness (vs lastWritten):     {}",
             subscriber.lastUpdateWrittenAt().has_value()
                 ? fsdb_cli_format::stalenessFromMillis(
                       subscriber.lastUpdateWrittenAt().value())
                 : "--")
      << std::endl;
  out << fmt::format(
             "Num Updates Served:             {}",
             fsdb_cli_format::optFieldToString<int64_t>(
                 subscriber.numUpdatesServed()))
      << std::endl;
  out << fmt::format(
             "Enqueued Data Size (bytes):     {}",
             fsdb_cli_format::optFieldToString<int64_t>(
                 subscriber.enqueuedDataSize()))
      << std::endl;
  out << fmt::format(
             "Served Data Size (bytes):       {}",
             fsdb_cli_format::optFieldToString<int64_t>(
                 subscriber.servedDataSize()))
      << std::endl;
  out << fmt::format(
             "Subscription Chunks Coalesced:  {}",
             fsdb_cli_format::optFieldToString<int32_t>(
                 subscriber.subscriptionChunksCoalesced()))
      << std::endl;
  out << fmt::format(
             "Subscription Queue Watermark:   {}",
             fsdb_cli_format::optFieldToString<int32_t>(
                 subscriber.subscriptionQueueWatermark()))
      << std::endl;
}

} // namespace

std::string_view CmdShowFsdbSubscriberTraits::description() {
  return "Displays the clients currently subscribed to FSDB: each subscriber's ID, subscription type (PATH/PATCH/DELTA), the raw path, whether it's a stats (vs state) subscription, when it subscribed, and its queue watermark. Use it to see who is consuming FSDB data.";
}

CmdShowFsdbSubscribers::RetType CmdShowFsdbSubscribers::sampleModel() {
  fsdb::SubscriberIdToOperSubscriberInfos result;

  fsdb::OperSubscriberInfo sub1;
  sub1.subscriberId() = "bgpd";
  sub1.type() = fsdb::PubSubType::PATCH;
  fsdb::RawOperPath path1;
  path1.path() = {"agent", "switchState", "interfaceMaps"};
  sub1.paths() = {{0, path1}};
  sub1.isStats() = false;
  sub1.subscribedSince() = 1700000000;
  sub1.subscriptionQueueWatermark() = 0;
  result["bgpd"] = {sub1};

  fsdb::OperSubscriberInfo sub2;
  sub2.subscriberId() = "fan_service";
  sub2.type() = fsdb::PubSubType::PATH;
  fsdb::OperPath path2;
  path2.raw() = {"qsfp_service", "state", "tcvrStates"};
  sub2.path() = path2;
  sub2.isStats() = false;
  sub2.subscribedSince() = 1700000000;
  sub2.subscriptionQueueWatermark() = 1;
  result["fan_service"] = {sub2};

  fsdb::OperSubscriberInfo sub3;
  sub3.subscriberId() = "netstate";
  sub3.type() = fsdb::PubSubType::PATH;
  fsdb::OperPath path3;
  path3.raw() = {"agent", "hwPortStats"};
  sub3.path() = path3;
  sub3.isStats() = true;
  sub3.subscribedSince() = 1700000000;
  sub3.subscriptionQueueWatermark() = 0;
  sub3.serveIntervalSec() = 2;
  result["netstate"] = {sub3};

  return result;
}

CmdShowFsdbSubscribers::RetType CmdShowFsdbSubscribers::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& fsdbClientid) {
  auto client = utils::createClient<
      apache::thrift::Client<facebook::fboss::fsdb::FsdbService>>(hostInfo);

  fsdb::SubscriberIds subscribers(fsdbClientid.begin(), fsdbClientid.end());

  fsdb::SubscriberIdToOperSubscriberInfos subInfos;
  if (subscribers.empty()) {
    client->sync_getAllOperSubscriberInfos(subInfos);
  } else {
    client->sync_getOperSubscriberInfos(subInfos, subscribers);
  }
  return subInfos;
}

void CmdShowFsdbSubscribers::printOutput(
    const RetType& result,
    std::ostream& out) {
  if (CmdGlobalOptions::getInstance()->isDetailed()) {
    for (const auto& subscriberInfo : result) {
      for (const auto& subscriber : subscriberInfo.second) {
        printSubscriberDetail(subscriber, out);
      }
    }
    return;
  }

  utils::Table table;
  table.setHeader(
      {"Subscribers Id",
       "Type",
       "Raw Path",
       "isStats",
       "Serve Interval",
       "Subscribed Since",
       "QueueWatermark"});
  for (const auto& subscriberInfo : result) {
    for (const auto& subscriber : subscriberInfo.second) {
      std::string subscriberId =
          folly::to<std::string>(subscriber.subscriberId().value());
      auto subscriberType = apache::thrift::util::enumNameSafe(
          folly::copy(subscriber.type().value()));
      std::string subscriberPath = utils::getSubscriptionPathStr(subscriber);
      std::string subscriberIsStats =
          folly::to<std::string>(folly::copy(subscriber.isStats().value()));
      std::string queueWatermark = "--";
      if (subscriber.subscriptionQueueWatermark().has_value()) {
        queueWatermark =
            fmt::format("{}", subscriber.subscriptionQueueWatermark().value());
      }

      std::string subscribedSince = "--";
      if (apache::thrift::get_pointer(subscriber.subscribedSince())) {
        subscribedSince = fsdb_cli_format::epochSecondsAsLocalTime(
            *apache::thrift::get_pointer(subscriber.subscribedSince()));
      }

      table.addRow(
          {subscriberId,
           subscriberType,
           subscriberPath,
           subscriberIsStats,
           fsdb_cli_format::serveIntervalToString(
               subscriber.serveIntervalSec()),
           subscribedSince,
           queueWatermark});
    }
  }
  out << table << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdShowFsdbSubscribers, CmdShowFsdbSubscriberTraits>::run();

} // namespace facebook::fboss
