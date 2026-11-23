// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include <atomic>

#include <fboss/fsdb/oper/DeltaValue.h>
#include <fboss/fsdb/oper/SubscriptionCommon.h>
#include <fboss/fsdb/oper/SubscriptionServeQueue.h>
#include <fboss/thrift_cow/storage/Storage.h>
#include <folly/Expected.h>
#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Sleep.h>
#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <chrono>
#include "fboss/fsdb/if/gen-cpp2/fsdb_common_types.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"

namespace facebook::fboss::fsdb {

// Used to pass client specific parameters per subscription
struct SubscriptionStorageParams {
  explicit SubscriptionStorageParams(
      std::optional<std::chrono::seconds> heartbeatInterval = std::nullopt,
      std::optional<uint64_t> serveIntervalMs = std::nullopt) {
    if (heartbeatInterval.has_value()) {
      heartbeatInterval_ = heartbeatInterval.value();
    }
    if (serveIntervalMs.has_value()) {
      serveIntervalMs_ = serveIntervalMs.value();
    }
  }

  std::optional<std::chrono::seconds> heartbeatInterval_;
  // Requested serve interval in ms, converted from the seconds the client sent.
  // The storage rounds it up to a whole tick and clamps it to the default.
  std::optional<uint64_t> serveIntervalMs_;
};

/*
 * This object
 */
template <typename Root, typename Impl>
class SubscribableStorage {
 public:
  // TODO: more flexibility here for all forward iterator types
  using RootT = Root;
  using ConcretePath = std::vector<std::string>;
  using PathIter = std::vector<std::string>::const_iterator;
  using ExtPathIter = std::vector<OperPathElem>::const_iterator;

  template <typename T>
  using Result = folly::Expected<T, StorageError>;

  virtual ~SubscribableStorage() {}

  /*
   * New apis, start/stop + subscribe.
   */

  void start() {
    static_cast<Impl*>(this)->start_impl();
  }
  void stop() {
    static_cast<Impl*>(this)->stop_impl();
  }

  template <typename Path>
  folly::coro::AsyncGenerator<DeltaValue<typename Path::DataT>&&> subscribe(
      SubscriptionIdentifier&& subscriber,
      Path&& path)
    requires(std::is_same_v<typename Path::RootT, RootT>)
  {
    return this->template subscribe<typename Path::DataT, typename Path::TC>(
        std::move(subscriber), path.begin(), path.end());
  }

  template <typename T, typename TC>
  folly::coro::AsyncGenerator<DeltaValue<T>&&> subscribe(
      SubscriptionIdentifier&& subscriber,
      const ConcretePath& path) {
    return this->template subscribe<T, TC>(
        std::move(subscriber), path.begin(), path.end());
  }

  template <typename T, typename TC>
  folly::coro::AsyncGenerator<DeltaValue<T>&&>
  subscribe(SubscriptionIdentifier&& subscriber, PathIter begin, PathIter end) {
    return static_cast<Impl*>(this)->template subscribe_impl<T, TC>(
        std::move(subscriber), begin, end);
  }

  template <typename Path>
  folly::coro::AsyncGenerator<DeltaValue<OperState>&&> subscribe_encoded(
      SubscriptionIdentifier&& subscriber,
      Path&& path,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt)
    requires(std::is_same_v<typename std::remove_cvref_t<Path>::RootT, RootT>)
  {
    return this->subscribe_encoded(
        std::move(subscriber),
        path.begin(),
        path.end(),
        protocol,
        std::move(subscriptionParams));
  }
  folly::coro::AsyncGenerator<DeltaValue<OperState>&&> subscribe_encoded(
      SubscriptionIdentifier&& subscriber,
      const ConcretePath& path,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return this->subscribe_encoded(
        std::move(subscriber),
        path.begin(),
        path.end(),
        protocol,
        std::move(subscriptionParams));
  }

  folly::coro::AsyncGenerator<DeltaValue<OperState>&&> subscribe_encoded(
      SubscriptionIdentifier&& subscriber,
      PathIter begin,
      PathIter end,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return static_cast<Impl*>(this)->subscribe_encoded_impl(
        std::move(subscriber), begin, end, protocol, subscriptionParams);
  }

  folly::coro::AsyncGenerator<std::vector<DeltaValue<TaggedOperState>>&&>
  subscribe_encoded_extended(
      SubscriptionIdentifier&& subscriber,
      std::vector<ExtendedOperPath> paths,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return static_cast<Impl*>(this)->subscribe_encoded_extended_impl(
        std::move(subscriber), std::move(paths), protocol, subscriptionParams);
  }

  template <typename Path>
  SubscriptionStreamReader<SubscriptionServeQueueElement<OperDelta>>
  subscribe_delta(
      SubscriptionIdentifier&& subscriber,
      Path&& path,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt)
    requires(std::is_same_v<typename std::remove_cvref_t<Path>::RootT, RootT>)
  {
    return this->subscribe_delta(
        std::move(subscriber),
        path.begin(),
        path.end(),
        protocol,
        std::move(subscriptionParams));
  }

  SubscriptionStreamReader<SubscriptionServeQueueElement<OperDelta>>
  subscribe_delta(
      SubscriptionIdentifier&& subscriber,
      const ConcretePath& path,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return this->subscribe_delta(
        std::move(subscriber),
        path.begin(),
        path.end(),
        protocol,
        std::move(subscriptionParams));
  }

  SubscriptionStreamReader<SubscriptionServeQueueElement<OperDelta>>
  subscribe_delta(
      SubscriptionIdentifier&& subscriber,
      PathIter begin,
      PathIter end,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return static_cast<Impl*>(this)->subscribe_delta_impl(
        std::move(subscriber), begin, end, protocol, subscriptionParams);
  }

  SubscriptionStreamReader<
      SubscriptionServeQueueElement<std::vector<TaggedOperDelta>>>
  subscribe_delta_extended(
      SubscriptionIdentifier&& subscriber,
      std::vector<ExtendedOperPath> paths,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return static_cast<Impl*>(this)->subscribe_delta_extended_impl(
        std::move(subscriber), std::move(paths), protocol, subscriptionParams);
  }

  template <typename Path>
  SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
  subscribe_patch(
      SubscriptionIdentifier&& subscriber,
      Path&& path,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt)
    requires(std::is_same_v<typename std::remove_cvref_t<Path>::RootT, RootT>)
  {
    return this->subscribe_patch(
        std::move(subscriber),
        path.begin(),
        path.end(),
        std::move(subscriptionParams));
  }
  SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
  subscribe_patch(
      SubscriptionIdentifier&& subscriber,
      const ConcretePath& path,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return this->subscribe_patch(
        std::move(subscriber),
        path.begin(),
        path.end(),
        std::move(subscriptionParams));
  }
  SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
  subscribe_patch(
      SubscriptionIdentifier&& subscriber,
      PathIter begin,
      PathIter end,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    RawOperPath rawPath;
    rawPath.path() = std::vector<std::string>(begin, end);
    return this->subscribe_patch(
        std::move(subscriber),
        std::move(rawPath),
        std::move(subscriptionParams));
  }
  SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
  subscribe_patch(
      SubscriptionIdentifier&& subscriber,
      RawOperPath rawPath,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return subscribe_patch(
        std::move(subscriber),
        std::map<SubscriptionKey, RawOperPath>{{0, std::move(rawPath)}},
        std::move(subscriptionParams));
  }
  SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
  subscribe_patch(
      SubscriptionIdentifier&& subscriber,
      std::map<SubscriptionKey, RawOperPath> rawPaths,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return static_cast<Impl*>(this)->subscribe_patch_impl(
        std::move(subscriber),
        std::move(rawPaths),
        std::move(subscriptionParams));
  }
  SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
  subscribe_patch_extended(
      SubscriptionIdentifier&& subscriber,
      std::map<SubscriptionKey, ExtendedOperPath> rawPaths,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt) {
    return static_cast<Impl*>(this)->subscribe_patch_extended_impl(
        std::move(subscriber),
        std::move(rawPaths),
        std::move(subscriptionParams));
  }

  // Append paths to an already-created patch subscription (located by its
  // SubscriptionIdentifier). Added paths get a full-state initial sync then
  // incremental patches on the existing stream. Returns an FsdbErrorCode on
  // failure (unknown id, non-patch sub, publisher-root mismatch, colliding
  // key).
  std::optional<FsdbErrorCode> add_patch_subscription_path(
      SubscriptionIdentifier&& id,
      std::map<SubscriptionKey, RawOperPath> newPaths,
      std::optional<StreamRevision> streamRevision = std::nullopt) {
    return static_cast<Impl*>(this)->add_patch_subscription_path_impl(
        std::move(id), std::move(newPaths), streamRevision);
  }
  std::optional<FsdbErrorCode> add_patch_subscription_path(
      SubscriptionIdentifier&& id,
      SubscriptionKey key,
      RawOperPath newPath,
      std::optional<StreamRevision> streamRevision = std::nullopt) {
    return add_patch_subscription_path(
        std::move(id),
        std::map<SubscriptionKey, RawOperPath>{{key, std::move(newPath)}},
        streamRevision);
  }

  // Append additional extended (wildcard/regex) paths to an already-created
  // patch subscription. Same semantics as add_patch_subscription_path but
  // accepts ExtendedOperPath, avoiding the raw→extended conversion.
  std::optional<FsdbErrorCode> add_extended_patch_subscription_path(
      SubscriptionIdentifier&& id,
      std::map<SubscriptionKey, ExtendedOperPath> newPaths,
      std::optional<StreamRevision> streamRevision = std::nullopt) {
    return static_cast<Impl*>(this)->add_extended_patch_subscription_path_impl(
        std::move(id), std::move(newPaths), streamRevision);
  }
  std::optional<FsdbErrorCode> add_extended_patch_subscription_path(
      SubscriptionIdentifier&& id,
      SubscriptionKey key,
      ExtendedOperPath newPath,
      std::optional<StreamRevision> streamRevision = std::nullopt) {
    return add_extended_patch_subscription_path(
        std::move(id),
        std::map<SubscriptionKey, ExtendedOperPath>{{key, std::move(newPath)}},
        streamRevision);
  }

  // wrapper calls to underlying storage

  template <typename Path>
  Result<typename Path::DataT> get(const Path& path) const
    requires(std::is_same_v<typename Path::RootT, RootT>)
  {
    return this->template get<typename Path::DataT>(path.begin(), path.end());
  }
  template <typename T>
  Result<T> get(const ConcretePath& path) const {
    return this->template get<T>(path.begin(), path.end());
  }
  template <typename T>
  Result<T> get(PathIter begin, PathIter end) const {
    return static_cast<Impl const*>(this)->template get_impl<T>(begin, end);
  }

  template <typename Path>
  Result<OperState> get_encoded(const Path& path, OperProtocol protocol) const
    requires(std::is_same_v<typename Path::RootT, Root>)
  {
    return this->get_encoded(path.begin(), path.end(), protocol);
  }
  Result<OperState> get_encoded(const ConcretePath& path, OperProtocol protocol)
      const {
    return this->get_encoded(path.begin(), path.end(), protocol);
  }
  Result<OperState>
  get_encoded(PathIter begin, PathIter end, OperProtocol protocol) const {
    return static_cast<Impl const*>(this)->get_encoded_impl(
        begin, end, protocol);
  }

  Result<std::vector<TaggedOperState>> get_encoded_extended(
      ExtPathIter begin,
      ExtPathIter end,
      OperProtocol protocol) const {
    return static_cast<Impl const*>(this)->get_encoded_extended_impl(
        begin, end, protocol);
  }

  template <typename Path>
  std::optional<StorageError> set(const Path& path, typename Path::DataT value)
    requires(std::is_same_v<typename Path::RootT, RootT>)
  {
    return this->set(path.begin(), path.end(), std::move(value));
  }

  template <typename T>
  std::optional<StorageError> set(const ConcretePath& path, T&& value) {
    return this->set(path.begin(), path.end(), std::forward<T>(value));
  }
  template <typename T>
  std::optional<StorageError> set(PathIter begin, PathIter end, T&& value) {
    return static_cast<Impl*>(this)->set_impl(
        begin, end, std::forward<T>(value));
  }

  template <typename Path>
  std::optional<StorageError> set_encoded(
      const Path& path,
      const OperState& state)
    requires(std::is_same_v<typename Path::RootT, Root>)
  {
    return this->set_encoded(path.begin(), path.end(), state);
  }
  std::optional<StorageError> set_encoded(
      const ConcretePath& path,
      const OperState& state) {
    return this->set_encoded(path.begin(), path.end(), state);
  }
  std::optional<StorageError>
  set_encoded(PathIter begin, PathIter end, const OperState& state) {
    return static_cast<Impl*>(this)->set_encoded_impl(begin, end, state);
  }

  template <typename Path>
  void remove(const Path& path)
    requires(std::is_same_v<typename Path::RootT, RootT>)
  {
    this->remove(path.begin(), path.end());
  }
  void remove(const ConcretePath& path) {
    this->remove(path.begin(), path.end());
  }
  void remove(PathIter begin, PathIter end) {
    static_cast<Impl*>(this)->remove_impl(begin, end);
  }

  std::optional<StorageError> patch(Patch&& patch) {
    return static_cast<Impl*>(this)->patch_impl(std::move(patch));
  }

  std::optional<StorageError> patch(const fsdb::OperDelta& delta) {
    return static_cast<Impl*>(this)->patch_impl(delta);
  }

  std::optional<StorageError> patch(const fsdb::TaggedOperState& state) {
    return static_cast<Impl*>(this)->patch_impl(state);
  }
};

} // namespace facebook::fboss::fsdb
