// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/client/FsdbSubManagerBase.h"
#include "fboss/fsdb/common/Utils.h"

namespace facebook::fboss::fsdb {

FsdbSubManagerBase::FsdbSubManagerBase(
    fsdb::SubscriptionOptions opts,
    utils::ConnectionOptions serverOptions,
    folly::EventBase* reconnectEvb,
    folly::EventBase* subscriberEvb)
    : opts_(std::move(opts)),
      connectionOptions_(std::move(serverOptions)),
      reconnectEvbThread_(
          reconnectEvb ? nullptr
                       : std::make_unique<folly::ScopedEventBaseThread>(
                             kReconnectThread)),
      subscriberEvbThread_(
          subscriberEvb ? nullptr
                        : std::make_unique<folly::ScopedEventBaseThread>(
                              kSubscriberThread)),
      reconnectEvb_(
          reconnectEvbThread_ ? reconnectEvbThread_->getEventBase()
                              : reconnectEvb),
      subscriberEvb_(
          subscriberEvbThread_ ? subscriberEvbThread_->getEventBase()
                               : subscriberEvb) {}

FsdbSubManagerBase::~FsdbSubManagerBase() {
  stop();
}

void FsdbSubManagerBase::stop() {
  if (subscriber_) {
    subscriber_.reset();
  }
  if (extSubscriber_) {
    extSubscriber_.reset();
  }
}

void FsdbSubManagerBase::reconnect(bool noGR) {
  if (subscriber_) {
    subscriber_->reconnect(noGR);
  } else if (extSubscriber_) {
    extSubscriber_->reconnect(noGR);
  }
}

const std::string& FsdbSubManagerBase::clientId() const {
  return opts_.clientId_;
}

std::optional<SubscriptionInfo> FsdbSubManagerBase::getInfo() {
  if (subscriber_) {
    return subscriber_->getInfo();
  } else if (extSubscriber_) {
    return extSubscriber_->getInfo();
  }
  return std::nullopt;
}

bool FsdbSubManagerBase::supportsLiveAddPath() const {
  if (subscriber_) {
    return subscriber_->serverSupportsLiveAddPath();
  } else if (extSubscriber_) {
    return extSubscriber_->serverSupportsLiveAddPath();
  }
  return false;
}

SubscriptionKey FsdbSubManagerBase::addPathImpl(
    const std::vector<std::string>& pathTokens) {
  CHECK(extSubscribePaths_.empty()) << "Cannot mix extended and raw paths";
  CHECK(!subscriber_) << "Cannot add paths after subscribed";
  CHECK(!extSubscriber_) << "Cannot add paths after subscribed";
  auto key = nextKey_++;
  RawOperPath p;
  p.path() = pathTokens;
  auto res = subscribePaths_.insert_or_assign(key, std::move(p));
  CHECK(res.second) << "Duplicate path added";
  return key;
}

SubscriptionKey FsdbSubManagerBase::addPathToLiveSubscriptionImpl(
    const std::vector<std::string>& pathTokens) {
  CHECK(extSubscribePaths_.empty()) << "Cannot mix extended and raw paths";
  CHECK(subscriber_)
      << "addPathToLiveSubscription requires an active raw-path subscription";
  // Client-side validation: reject invalid input synchronously (before
  // consuming a key or staging the path) so the caller gets an immediate,
  // retryable error. The live-extend RPC is best-effort; server rejections
  // surface via logs.
  if (pathTokens.empty()) {
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "addPathToLiveSubscription: path must not be empty");
  }
  auto key = nextKey_;
  RawOperPath p;
  p.path() = pathTokens;
  auto err = subscriber_->addPaths({{key, p}});
  if (err.has_value()) {
    throw Utils::createFsdbException(
        err.value(), "addPathToLiveSubscription failed");
  }
  subscribePaths_.insert_or_assign(key, std::move(p));
  ++nextKey_;
  return key;
}

SubscriptionKey FsdbSubManagerBase::addExtendedPathImpl(
    const std::vector<OperPathElem>& pathTokens) {
  CHECK(subscribePaths_.empty()) << "Cannot mix extended and raw paths";
  CHECK(!subscriber_) << "Cannot add paths after subscribed";
  CHECK(!extSubscriber_) << "Cannot add paths after subscribed";
  auto key = nextKey_++;
  ExtendedOperPath p;
  p.path() = pathTokens;
  auto res = extSubscribePaths_.insert_or_assign(key, std::move(p));
  CHECK(res.second) << "Duplicate path added";
  return key;
}

SubscriptionKey FsdbSubManagerBase::addExtendedPathToLiveSubscriptionImpl(
    const std::vector<OperPathElem>& pathTokens) {
  CHECK(subscribePaths_.empty()) << "Cannot mix extended and raw paths";
  CHECK(extSubscriber_)
      << "addExtendedPathToLiveSubscription requires an active extended-path subscription";
  // Client-side validation: reject obviously-invalid input synchronously,
  // before consuming a SubscriptionKey or staging the path, so the caller gets
  // an immediate, retryable error. The live-extend RPC itself is best-effort
  // and async, so server-side rejections surface via logs + the reconnect
  // merge, not through this call's return value.
  if (pathTokens.empty()) {
    throw Utils::createFsdbException(
        FsdbErrorCode::INVALID_REQUEST,
        "addExtendedPathToLiveSubscription: path must not be empty");
  }
  auto key = nextKey_;
  ExtendedOperPath p;
  p.path() = pathTokens;
  auto err = extSubscriber_->addPaths({{key, p}});
  if (err.has_value()) {
    // Don't consume the key or stage the path on failure, so the caller can
    // retry without leaking a key or a rejected path.
    throw Utils::createFsdbException(
        err.value(), "addExtendedPathToLiveSubscription failed");
  }
  extSubscribePaths_.insert_or_assign(key, std::move(p));
  ++nextKey_;
  return key;
}

void FsdbSubManagerBase::subscribeImpl(
    std::function<void(SubscriberChunk&&)> chunkHandler,
    std::optional<SubscriptionStateChangeCb> subscriptionStateChangeCb,
    std::optional<FsdbStreamHeartbeatCb> heartbeatCb) {
  CHECK(!subscriber_) << "Cannot subscribe twice";
  CHECK(!extSubscriber_) << "Cannot subscribe twice";
  if (!subscribePaths_.empty()) {
    subscriber_ = std::make_unique<FsdbPatchSubscriber>(
        SubscriptionOptions(opts_),
        subscribePaths_,
        subscriberEvb_,
        reconnectEvb_,
        std::move(chunkHandler),
        std::move(subscriptionStateChangeCb),
        std::nullopt,
        std::move(heartbeatCb));
    subscriber_->setConnectionOptions(connectionOptions_);
  } else {
    CHECK(!extSubscribePaths_.empty()) << "No paths to subscribe to";
    extSubscriber_ = std::make_unique<FsdbExtPatchSubscriber>(
        SubscriptionOptions(opts_),
        extSubscribePaths_,
        subscriberEvb_,
        reconnectEvb_,
        std::move(chunkHandler),
        std::move(subscriptionStateChangeCb),
        std::nullopt,
        std::move(heartbeatCb));
    extSubscriber_->setConnectionOptions(connectionOptions_);
  }
}

} // namespace facebook::fboss::fsdb
