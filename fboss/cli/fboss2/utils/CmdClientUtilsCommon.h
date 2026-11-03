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

#include <folly/executors/GlobalExecutor.h>
#include <thrift/lib/cpp2/async/PooledRequestChannel.h>
#include <thrift/lib/cpp2/async/RocketClientChannel.h>

#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss::utils {

static auto constexpr kConnTimeout = 1000;
static auto constexpr kRecvTimeout = 45000;
static auto constexpr kSendTimeout = 5000;

template <typename T>
std::unique_ptr<T> createClient(const HostInfo& hostInfo);

template <typename T>
std::unique_ptr<T> createClient(const HostInfo& hostInfo, int switchIndex);

template <typename T>
std::unique_ptr<T> createClient(
    const HostInfo& hostInfo,
    const std::chrono::milliseconds& timeout);

// Some clients do not require info about host info
template <typename T>
std::unique_ptr<T> createClient();

template <typename Client>
std::unique_ptr<Client> createPlaintextClient(
    const HostInfo& hostInfo,
    const int port) {
  // A direct RocketClientChannel exposes its EventBase to sync callers. When
  // clients created on one thread are used concurrently from worker threads,
  // those callers can try to drive the same EventBase and abort. The pooled
  // channel dispatches I/O onto a managed EventBase instead. This is also the
  // pattern used by fboss/agent/SetupThrift.h and
  // fboss/agent/mnpu/SplitAgentThriftSyncerClient.cpp. Supply a callback
  // executor as those implementations do so future_ and callback RPCs remain
  // supported; synchronous and semifuture_ RPCs use inline-safe callbacks.
  // This helper has no caller-owned callback executor, so use the process-wide
  // CPU executor as a safe default. If a caller needs callback thread affinity,
  // add an overload accepting a caller-owned
  // std::shared_ptr<folly::ScopedEventBaseThread>&, following SetupThrift.h.
  auto channel = apache::thrift::PooledRequestChannel::newChannel(
      folly::getGlobalCPUExecutor().get(),
      [addr = folly::SocketAddress(hostInfo.getIp(), port)](
          folly::EventBase& eventBase) {
        auto socket =
            folly::AsyncSocket::newSocket(&eventBase, addr, kConnTimeout);
        socket->setSendTimeout(kSendTimeout);
        auto rocketChannel =
            apache::thrift::RocketClientChannel::newChannel(std::move(socket));
        rocketChannel->setTimeout(kRecvTimeout);
        return rocketChannel;
      });
  return std::make_unique<Client>(std::move(channel));
}

} // namespace facebook::fboss::utils
