// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/client/cgo/FsdbCgoPubSubWrapper.h"

#include <folly/String.h>
#include <folly/init/Init.h>
#include <folly/init/Phase.h>
#include <folly/logging/xlog.h>
#include <folly/synchronization/CallOnce.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <chrono>
#include <limits>
#include <stdexcept>

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/fsdb/client/FsdbStreamClient.h"
#include "fboss/fsdb/if/FsdbModel.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_types.h"
#include "fboss/lib/thrift_service_client/ConnectionOptions.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"

namespace facebook::fboss::fsdb {

namespace {
// Worst-case shutdown latency for waitFor*.
constexpr auto kPollInterval = std::chrono::milliseconds(100);
// Bounds producer-side backpressure on a full queue. Past this, the FSDB
// callback drops the update with an error log instead of stalling indefinitely.
constexpr auto kEnqueueTimeout = std::chrono::milliseconds(100);
// Subscribers here reach remote switches over the management network, so allow
// more slack than on-switch clients: 6 missed 5s state heartbeats, and 3 missed
// stats heartbeats at the 30s binary default.
constexpr auto kStateChunkTimeoutSec = "30";
constexpr auto kStatChunkTimeoutSec = "90";

void applyChunkTimeoutDefaults() {
  gflags::SetCommandLineOptionWithMode(
      "fsdb_state_chunk_timeout",
      kStateChunkTimeoutSec,
      gflags::SET_FLAG_IF_DEFAULT);
  gflags::SetCommandLineOptionWithMode(
      "fsdb_stat_chunk_timeout",
      kStatChunkTimeoutSec,
      gflags::SET_FLAG_IF_DEFAULT);
}

// Polls `shuttingDown` every kPollInterval until first item or shutdown,
// then non-blocking-drain up to `maxCount`. Empty vector on shutdown.
template <typename T, typename Queue>
std::vector<T> waitAndDrain(
    Queue& queue,
    std::atomic<bool>& shuttingDown,
    int maxCount,
    size_t batchHint) {
  if (maxCount <= 0) {
    return {};
  }
  T update;
  while (!shuttingDown.load(std::memory_order_acquire)) {
    if (queue.try_dequeue_for(update, kPollInterval)) {
      break;
    }
  }
  if (shuttingDown.load(std::memory_order_acquire)) {
    return {};
  }
  std::vector<T> updates;
  updates.reserve(std::min(static_cast<size_t>(maxCount), batchHint));
  updates.emplace_back(std::move(update));
  while (static_cast<int>(updates.size()) < maxCount &&
         queue.try_dequeue(update)) {
    updates.emplace_back(std::move(update));
  }
  return updates;
}

// Deserialize a portMaps blob into (portName, portId, portOperState) per port.
// portId is read from the PortFields field, not the (switch-local) map key.
template <typename Buf>
std::vector<std::tuple<std::string, int32_t, bool>> decodePortMaps(
    const Buf& contents) {
  using PortMaps = std::
      map<std::string, std::map<int16_t, facebook::fboss::state::PortFields>>;
  auto portMaps =
      apache::thrift::BinarySerializer::deserialize<PortMaps>(contents);
  std::vector<std::tuple<std::string, int32_t, bool>> ports;
  for (const auto& [switchId, portMap] : portMaps) {
    for (const auto& [mapKey, portFields] : portMap) {
      ports.emplace_back(
          *portFields.portName(),
          *portFields.portId(),
          *portFields.portOperState());
    }
  }
  return ports;
}

// GR-hold means temporarily down but attempting to restore, so report it as
// CONNECTING rather than DISCONNECTED.
int32_t toConnectionState(fsdb::SubscriptionState state) {
  switch (state) {
    case fsdb::SubscriptionState::CONNECTED:
      return FSDB_CONNECTION_CONNECTED;
    case fsdb::SubscriptionState::DISCONNECTED_GR_HOLD:
      return FSDB_CONNECTION_CONNECTING;
    case fsdb::SubscriptionState::DISCONNECTED:
    case fsdb::SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED:
    case fsdb::SubscriptionState::CANCELLED:
      return FSDB_CONNECTION_DISCONNECTED;
  }
  return FSDB_CONNECTION_DISCONNECTED;
}
} // namespace

FsdbCgoPubSubWrapper::FsdbCgoPubSubWrapper(const std::string& clientId)
    : clientId_(clientId) {
  XLOG(INFO) << "FsdbCgoPubSubWrapper created for client: " << clientId_;

  applyChunkTimeoutDefaults();
  XLOG(INFO) << "FSDB chunk timeouts: state=" << FLAGS_fsdb_state_chunk_timeout
             << "s stats=" << FLAGS_fsdb_stat_chunk_timeout << "s";

  pubSubMgr_ = std::make_unique<FsdbPubSubManager>(clientId_);
}

FsdbCgoPubSubWrapper::~FsdbCgoPubSubWrapper() {
  XLOG(INFO) << "FsdbCgoPubSubWrapper destructor for client: " << clientId_;

  // Stop the callback thread before freeing the queues/map it touches.
  shuttingDown_.store(true, std::memory_order_release);
  pubSubMgr_.reset();
}

void FsdbCgoPubSubWrapper::subscribeToOperState_portMaps(
    std::optional<int> serverPort,
    const std::optional<std::string>& host) {
  if (stateSubscribed_) {
    XLOG(WARNING) << "Already subscribed to portMaps state";
    return;
  }

  thriftpath::RootThriftPath<FsdbOperStateRoot> rootPath;
  auto path = rootPath.agent().switchState().portMaps();

  auto subscriptionStateCb = [this](
                                 fsdb::SubscriptionState oldState,
                                 fsdb::SubscriptionState newState,
                                 std::optional<bool> /*initialSyncHasData*/) {
    XLOG(INFO) << "FSDB state subscription changed from "
               << subscriptionStateToString(oldState) << " to "
               << subscriptionStateToString(newState);
    const int32_t mapped = toConnectionState(newState);
    if (mapped == FSDB_CONNECTION_CONNECTED) {
      // Stream up. Don't clobber DATA_RECEIVED, which processPortMapsUpdate may
      // have already set on the same callback thread.
      if (portMapsConnState_.load(std::memory_order_acquire) <
          FSDB_CONNECTION_CONNECTED) {
        portMapsConnState_.store(
            FSDB_CONNECTION_CONNECTED, std::memory_order_release);
      }
    } else {
      portMapsConnState_.store(mapped, std::memory_order_release);
    }
  };

  auto operStateCb = [this](fsdb::OperState&& operState) {
    processPortMapsUpdate(std::move(operState));
  };

  // Attempting first connect until the state-change callback reports otherwise.
  portMapsConnState_.store(
      FSDB_CONNECTION_CONNECTING, std::memory_order_release);

  try {
    if (serverPort.has_value()) {
      const std::string dstHost = host.value_or("::1");
      utils::ConnectionOptions connOptions(dstHost, *serverPort);
      pubSubMgr_->addStatePathSubscription(
          path.tokens(),
          std::move(subscriptionStateCb),
          std::move(operStateCb),
          std::move(connOptions));

      XLOG(INFO) << "Successfully subscribed to portMaps state on " << dstHost
                 << ":" << *serverPort;
    } else {
      pubSubMgr_->addStatePathSubscription(
          path.tokens(),
          std::move(subscriptionStateCb),
          std::move(operStateCb));

      XLOG(INFO) << "Successfully subscribed to portMaps state (default port)";
    }

    stateSubscribed_ = true;
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to subscribe to portMaps state: " << e.what();
    throw std::runtime_error(
        std::string("Failed to subscribe to portMaps state: ") + e.what());
  }
}

void FsdbCgoPubSubWrapper::subscribeStatsPath(
    const std::vector<std::string>& path,
    std::optional<int> serverPort,
    const std::optional<std::string>& host) {
  if (statsSubscribed_) {
    XLOG(WARNING) << "Already subscribed to a stats path; ignoring";
    return;
  }
  if (path.empty()) {
    throw std::runtime_error("subscribeStatsPath: empty path");
  }

  const std::string pathStr = folly::join(".", path);

  auto subscriptionStateCb = [pathStr](
                                 fsdb::SubscriptionState oldState,
                                 fsdb::SubscriptionState newState,
                                 std::optional<bool> /*initialSyncHasData*/) {
    XLOG(INFO) << "FSDB stats subscription (path=" << pathStr
               << ") changed from " << subscriptionStateToString(oldState)
               << " to " << subscriptionStateToString(newState);
  };

  auto operStatsCb = [this, pathStr](fsdb::OperState&& operState) {
    if (!operState.contents()) {
      XLOG(WARNING) << "Empty OperState for stats path " << pathStr;
      return;
    }
    const int32_t protocol = static_cast<int32_t>(*operState.protocol());
    enqueueStats(pathStr, std::move(*operState.contents()), protocol);
  };

  try {
    if (serverPort.has_value()) {
      const std::string dstHost = host.value_or("::1");
      utils::ConnectionOptions connOptions(dstHost, *serverPort);
      pubSubMgr_->addStatPathSubscription(
          path,
          std::move(subscriptionStateCb),
          std::move(operStatsCb),
          std::move(connOptions));

      XLOG(INFO) << "Successfully subscribed to stats path " << pathStr
                 << " on " << dstHost << ":" << *serverPort;
    } else {
      pubSubMgr_->addStatPathSubscription(
          path, std::move(subscriptionStateCb), std::move(operStatsCb));

      XLOG(INFO) << "Successfully subscribed to stats path " << pathStr
                 << " (default port)";
    }

    statsSubscribed_ = true;
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to subscribe to stats path " << pathStr << ": "
              << e.what();
    throw std::runtime_error(
        std::string("Failed to subscribe to stats path: ") + e.what());
  }
}

std::vector<std::tuple<std::string, int32_t, bool>>
FsdbCgoPubSubWrapper::waitForStateUpdates(int maxCount) {
  if (!stateSubscribed_.load()) {
    throw std::runtime_error("No active state subscription");
  }
  constexpr size_t kStateBatchHint = 128;
  auto updates = waitAndDrain<std::tuple<std::string, int32_t, bool>>(
      stateQueue_, shuttingDown_, maxCount, kStateBatchHint);
  XLOG(DBG2) << "Returning " << updates.size() << " state updates";
  return updates;
}

std::vector<std::tuple<std::string, folly::fbstring, int32_t>>
FsdbCgoPubSubWrapper::waitForStatsUpdates(int maxCount) {
  if (!statsSubscribed_.load()) {
    throw std::runtime_error("No active stats subscription");
  }
  constexpr size_t kStatsBatchHint = 16;
  auto updates =
      waitAndDrain<std::tuple<std::string, folly::fbstring, int32_t>>(
          statsQueue_, shuttingDown_, maxCount, kStatsBatchHint);
  XLOG(DBG2) << "Returning " << updates.size() << " stats updates";
  return updates;
}

void FsdbCgoPubSubWrapper::enqueueState(
    const std::string& key,
    int32_t portId,
    bool portOperState) {
  if (shuttingDown_.load(std::memory_order_acquire)) {
    return;
  }
  XLOG(DBG2) << "Received state update for key: " << key
             << ", portId: " << portId
             << ", portOperState: " << (portOperState ? "UP" : "DOWN");

  if (!stateQueue_.try_enqueue_for(
          std::make_tuple(key, portId, portOperState), kEnqueueTimeout)) {
    XLOG(ERR) << "stateQueue_ full after " << kEnqueueTimeout.count()
              << "ms; dropping update for key: " << key;
  }
}

void FsdbCgoPubSubWrapper::enqueueStats(
    const std::string& key,
    folly::fbstring&& contents,
    int32_t protocol) {
  if (shuttingDown_.load(std::memory_order_acquire)) {
    return;
  }
  XLOG(DBG2) << "Received stats update for key: " << key << " ("
             << contents.size() << " bytes, protocol=" << protocol << ")";

  if (!statsQueue_.try_enqueue_for(
          std::make_tuple(key, std::move(contents), protocol),
          kEnqueueTimeout)) {
    XLOG(ERR) << "statsQueue_ full after " << kEnqueueTimeout.count()
              << "ms; dropping update for key: " << key;
  }
}

void FsdbCgoPubSubWrapper::subscribeStatePath(
    const std::vector<std::string>& path,
    std::optional<int> serverPort,
    const std::optional<std::string>& host) {
  if (statePathSubscribed_) {
    XLOG(WARNING) << "Already subscribed to a state path; ignoring";
    return;
  }
  if (path.empty()) {
    throw std::runtime_error("subscribeStatePath: empty path");
  }

  const std::string pathStr = folly::join(".", path);

  auto subscriptionStateCb = [pathStr](
                                 fsdb::SubscriptionState oldState,
                                 fsdb::SubscriptionState newState,
                                 std::optional<bool> /*initialSyncHasData*/) {
    XLOG(INFO) << "FSDB state-path subscription (path=" << pathStr
               << ") changed from " << subscriptionStateToString(oldState)
               << " to " << subscriptionStateToString(newState);
  };

  auto operStateCb = [this, pathStr](fsdb::OperState&& operState) {
    if (!operState.contents()) {
      XLOG(WARNING) << "Empty OperState for state path " << pathStr;
      return;
    }
    const int32_t protocol = static_cast<int32_t>(*operState.protocol());
    enqueueStatePath(pathStr, std::move(*operState.contents()), protocol);
  };

  try {
    if (serverPort.has_value()) {
      const std::string dstHost = host.value_or("::1");
      utils::ConnectionOptions connOptions(dstHost, *serverPort);
      pubSubMgr_->addStatePathSubscription(
          path,
          std::move(subscriptionStateCb),
          std::move(operStateCb),
          std::move(connOptions));

      XLOG(INFO) << "Successfully subscribed to state path " << pathStr
                 << " on " << dstHost << ":" << *serverPort;
    } else {
      pubSubMgr_->addStatePathSubscription(
          path, std::move(subscriptionStateCb), std::move(operStateCb));

      XLOG(INFO) << "Successfully subscribed to state path " << pathStr
                 << " (default port)";
    }

    statePathSubscribed_ = true;
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to subscribe to state path " << pathStr << ": "
              << e.what();
    throw std::runtime_error(
        std::string("Failed to subscribe to state path: ") + e.what());
  }
}

std::vector<std::tuple<std::string, int32_t, bool>>
FsdbCgoPubSubWrapper::getPortSnapshot(
    std::optional<int> serverPort,
    const std::optional<std::string>& host) {
  thriftpath::RootThriftPath<FsdbOperStateRoot> rootPath;
  auto path = rootPath.agent().switchState().portMaps();

  utils::ConnectionOptions connOptions = serverPort.has_value()
      ? utils::ConnectionOptions(host.value_or("::1"), *serverPort)
      : utils::ConnectionOptions::defaultOptions<FsdbService>();
  auto client =
      utils::createPlaintextClient<FsdbService>(std::move(connOptions));
  if (!client) {
    throw std::runtime_error("getPortSnapshot: failed to create FSDB client");
  }

  OperGetRequest req;
  req.path()->raw() = path.tokens();
  req.protocol() = OperProtocol::BINARY;

  OperState result;
  client->sync_getOperState(result, req);
  if (!result.contents()) {
    return {};
  }
  return decodePortMaps(*result.contents());
}

std::vector<std::tuple<std::string, folly::fbstring, int32_t>>
FsdbCgoPubSubWrapper::waitForStatePathUpdates(int maxCount) {
  if (!statePathSubscribed_.load()) {
    throw std::runtime_error("No active state-path subscription");
  }
  constexpr size_t kStatePathBatchHint = 128;
  auto updates =
      waitAndDrain<std::tuple<std::string, folly::fbstring, int32_t>>(
          statePathQueue_, shuttingDown_, maxCount, kStatePathBatchHint);
  XLOG(DBG2) << "Returning " << updates.size() << " state-path updates";
  return updates;
}

void FsdbCgoPubSubWrapper::enqueueStatePath(
    const std::string& key,
    folly::fbstring&& contents,
    int32_t protocol) {
  if (shuttingDown_.load(std::memory_order_acquire)) {
    return;
  }
  XLOG(DBG2) << "Received state-path update for key: " << key << " ("
             << contents.size() << " bytes, protocol=" << protocol << ")";

  if (!statePathQueue_.try_enqueue_for(
          std::make_tuple(key, std::move(contents), protocol),
          kEnqueueTimeout)) {
    XLOG(ERR) << "statePathQueue_ full after " << kEnqueueTimeout.count()
              << "ms; dropping update for key: " << key;
  }
}

void FsdbCgoPubSubWrapper::processPortMapsUpdate(fsdb::OperState&& operState) {
  if (!operState.contents()) {
    XLOG(WARNING) << "Received empty OperState for portMaps";
    return;
  }

  portMapsConnState_.store(
      FSDB_CONNECTION_DATA_RECEIVED, std::memory_order_release);

  try {
    // Enqueue an update only for a new port or one whose state changed.
    for (const auto& [portName, portId, currentState] :
         decodePortMaps(*operState.contents())) {
      auto it = portName2OperState_.find(portName);
      if (it == portName2OperState_.end() || it->second != currentState) {
        enqueueState(portName, portId, currentState);
        portName2OperState_[portName] = currentState;
      }
    }
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to deserialize and process portMaps: " << e.what();
  }
}

// ============================================================================
// C API for CGO (Go<->C++ interop)
// ============================================================================
// These functions provide a C-compatible interface that Go can call via CGO

extern "C" {

/**
 * Create a new FsdbCgoPubSubWrapper instance
 * @param clientId Client identifier string
 * @return Opaque handle to the wrapper, or nullptr on failure
 */
FsdbWrapperHandle FOLLY_NULLABLE CreateFsdbWrapper(const char* clientId) {
  if (!clientId) {
    return nullptr;
  }

  try {
    auto* wrapper = new FsdbCgoPubSubWrapper(std::string(clientId));
    return static_cast<FsdbWrapperHandle>(wrapper);
  } catch (const std::exception&) {
    return nullptr;
  }
}

/**
 * Destroy a FsdbCgoPubSubWrapper instance
 * @param handle Opaque handle to the wrapper
 */
void DestroyFsdbWrapper(FsdbWrapperHandle handle) {
  if (!handle) {
    return;
  }

  auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
  delete wrapper;
}

// Wakes blocked WaitFor* within ~100 ms. Call before Destroy. Returns 0/1.
int32_t ShutdownFsdbWrapper(FsdbWrapperHandle handle) {
  if (!handle) {
    return 0;
  }
  try {
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    wrapper->shutdown();
    return 0;
  } catch (const std::exception& e) {
    XLOG(ERR) << "ShutdownFsdbWrapper error: " << e.what();
    return 1;
  }
}

// NULL or empty host => localhost; non-empty host => remote.
static std::optional<std::string> hostToOptional(const char* host) {
  if (host && host[0] != '\0') {
    return std::string(host);
  }
  return std::nullopt;
}

// Negative server_port => default FSDB port.
static std::optional<int> portToOptional(int32_t serverPort) {
  return serverPort >= 0 ? std::optional<int>(serverPort) : std::nullopt;
}

// A default port (server_port < 0) routes to the host-less path, dropping host.
static void warnIfHostIgnored(const char* fn, const char* host, int32_t port) {
  if (host && host[0] != '\0' && port < 0) {
    XLOG(WARNING) << fn << ": host '" << host
                  << "' ignored because server_port < 0; pass an explicit port "
                     "to reach a remote host";
  }
}

static std::vector<std::string> tokensToPath(
    const char** path_tokens,
    int32_t num_tokens) {
  std::vector<std::string> path;
  path.reserve(num_tokens);
  for (int32_t i = 0; i < num_tokens; ++i) {
    if (path_tokens[i] == nullptr) {
      throw std::runtime_error(
          "tokensToPath: null token at index " + std::to_string(i));
    }
    path.emplace_back(path_tokens[i]);
  }
  return path;
}

void SubscribeToPortMaps(
    FsdbWrapperHandle handle,
    const char* host,
    int32_t server_port) {
  if (!handle) {
    return;
  }
  try {
    warnIfHostIgnored("SubscribeToPortMaps", host, server_port);
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    wrapper->subscribeToOperState_portMaps(
        portToOptional(server_port), hostToOptional(host));
  } catch (const std::exception&) {
    // Subscription failed - error already logged
  }
}

void SubscribeToStats(
    FsdbWrapperHandle handle,
    const char** path_tokens,
    int32_t num_tokens,
    const char* host,
    int32_t server_port) {
  if (!handle || !path_tokens || num_tokens <= 0) {
    return;
  }
  try {
    warnIfHostIgnored("SubscribeToStats", host, server_port);
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    wrapper->subscribeStatsPath(
        tokensToPath(path_tokens, num_tokens),
        portToOptional(server_port),
        hostToOptional(host));
  } catch (const std::exception&) {
    // Subscription failed - error already logged
  }
}

void SubscribeToState(
    FsdbWrapperHandle handle,
    const char** path_tokens,
    int32_t num_tokens,
    const char* host,
    int32_t server_port) {
  if (!handle || !path_tokens || num_tokens <= 0) {
    return;
  }
  try {
    warnIfHostIgnored("SubscribeToState", host, server_port);
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    wrapper->subscribeStatePath(
        tokensToPath(path_tokens, num_tokens),
        portToOptional(server_port),
        hostToOptional(host));
  } catch (const std::exception&) {
    // Subscription failed - error already logged
  }
}

// Returns ports written (>=0), or -1 on error. See header for lifetime of the
// borrowed port_name pointers.
int32_t GetPortSnapshot(
    FsdbWrapperHandle handle,
    const char* host,
    int32_t server_port,
    FsdbPortStateUpdate* out,
    int32_t max_count) {
  if (!handle || !out || max_count <= 0) {
    return -1;
  }
  try {
    warnIfHostIgnored("GetPortSnapshot", host, server_port);
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);

    // Stash on wrapper so c_str() pointers stay live across the C boundary.
    wrapper->lastSnapshot_ = wrapper->getPortSnapshot(
        portToOptional(server_port), hostToOptional(host));

    if (static_cast<int32_t>(wrapper->lastSnapshot_.size()) > max_count) {
      XLOG(WARNING) << "GetPortSnapshot: " << wrapper->lastSnapshot_.size()
                    << " ports exceed caller buffer (" << max_count
                    << "); truncating";
    }
    int32_t i = 0;
    for (const auto& [portName, portId, operState] : wrapper->lastSnapshot_) {
      if (i >= max_count) {
        break;
      }
      out[i].port_name = portName.c_str();
      out[i].port_id = portId;
      out[i].oper_state = operState ? 1 : 0;
      ++i;
    }
    return i;
  } catch (const std::exception& e) {
    XLOG(ERR) << "GetPortSnapshot error: " << e.what();
    return -1;
  }
}

// Returns the portMaps connection state (a FsdbConnectionState value).
int32_t GetConnectionState(FsdbWrapperHandle handle) {
  if (!handle) {
    return FSDB_CONNECTION_DISCONNECTED;
  }
  try {
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    return wrapper->getConnectionState();
  } catch (const std::exception&) {
    return FSDB_CONNECTION_DISCONNECTED;
  }
}

/**
 * Check if state subscription is active
 * @param handle Opaque handle to the wrapper
 * @return 1 if active, 0 otherwise
 */
int32_t HasStateSubscription(FsdbWrapperHandle handle) {
  if (!handle) {
    return 0;
  }

  try {
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    return wrapper->hasStateSubscription() ? 1 : 0;
  } catch (const std::exception&) {
    return 0;
  }
}

/**
 * Check if stats subscription is active
 * @param handle Opaque handle to the wrapper
 * @return 1 if active, 0 otherwise
 */
int32_t HasStatsSubscription(FsdbWrapperHandle handle) {
  if (!handle) {
    return 0;
  }

  try {
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    return wrapper->hasStatsSubscription() ? 1 : 0;
  } catch (const std::exception&) {
    return 0;
  }
}

int32_t HasStatePathSubscription(FsdbWrapperHandle handle) {
  if (!handle) {
    return 0;
  }
  try {
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    return wrapper->hasStatePathSubscription() ? 1 : 0;
  } catch (const std::exception&) {
    return 0;
  }
}

/**
 * Get client ID
 * @param handle Opaque handle to the wrapper
 * @return Client ID string (do not free - owned by wrapper), or nullptr on
 * failure
 */
const char* FOLLY_NULLABLE GetClientId(FsdbWrapperHandle handle) {
  if (!handle) {
    return nullptr;
  }

  try {
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    // Return pointer to internal string - valid until wrapper is destroyed
    return wrapper->getClientId().c_str();
  } catch (const std::exception&) {
    return nullptr;
  }
}

int32_t FsdbCgoAbiVersion() {
  return FSDB_CGO_ABI_VERSION;
}

int32_t FsdbSetFlag(const char* name, const char* value) {
  if (!name || !value) {
    return 1;
  }
  try {
    // SetCommandLineOption returns the new value on success, empty on failure
    // (unknown flag name OR value couldn't be parsed for the flag's type).
    const std::string result = gflags::SetCommandLineOption(name, value);
    if (result.empty()) {
      XLOG(ERR) << "FsdbSetFlag: failed to set flag '" << name << "' = '"
                << value << "' (unknown flag or invalid value)";
      return 1;
    }
    return 0;
  } catch (const std::exception& e) {
    XLOG(ERR) << "FsdbSetFlag: exception setting '" << name
              << "': " << e.what();
    return 1;
  }
}

// ABI-checked folly::Init bootstrap. Idempotent. 0=ok, 1=caught exception,
// 2=ABI mismatch.
int32_t FsdbInit(int32_t consumer_abi_version) {
  if (consumer_abi_version != FSDB_CGO_ABI_VERSION) {
    XLOG(ERR) << "FsdbInit: ABI mismatch — consumer compiled against version "
              << consumer_abi_version << ", library is version "
              << FSDB_CGO_ABI_VERSION
              << ". Aborting init; do not call any other entry point.";
    return 2;
  }
  try {
    static folly::once_flag initFlag;
    folly::call_once(initFlag, []() {
      // Check if folly is already initialized (e.g. by test framework)
      if (folly::get_process_phase() != folly::ProcessPhase::Init) {
        XLOG(INFO) << "FsdbInit: Folly already initialized, skipping";
        return;
      }
      static std::unique_ptr<folly::Init> follyInit;
      int argc = 1;
      const char* argv0 = "fsdb_cgo";
      char* mutableArgv0 = const_cast<char*>(argv0);
      char** argv = &mutableArgv0;
      follyInit = std::make_unique<folly::Init>(&argc, &argv, false);
      XLOG(INFO) << "FsdbInit: Folly runtime initialized";
    });
    return 0;
  } catch (const std::exception& e) {
    XLOG(ERR) << "FsdbInit failed: " << e.what();
    return 1;
  }
}

// Returns count, 0 on shutdown, -1 on error. Surplus stays queued.
int32_t WaitForStateUpdates(
    FsdbWrapperHandle handle,
    FsdbPortStateUpdate* out,
    int32_t max_count) {
  if (!handle || !out || max_count <= 0) {
    return -1;
  }

  try {
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    if (!wrapper->hasStateSubscription()) {
      return -1;
    }

    // Stash on wrapper so c_str() pointers stay live across the C boundary.
    wrapper->lastStateUpdates_ = wrapper->waitForStateUpdates(max_count);

    int32_t i = 0;
    for (const auto& [portName, portId, operState] :
         wrapper->lastStateUpdates_) {
      out[i].port_name = portName.c_str();
      out[i].port_id = portId;
      out[i].oper_state = operState ? 1 : 0;
      ++i;
    }
    return i;
  } catch (const std::exception& e) {
    XLOG(ERR) << "WaitForStateUpdates error: " << e.what();
    return -1;
  }
}

int32_t WaitForStatsUpdates(
    FsdbWrapperHandle handle,
    FsdbStatsUpdate* out,
    int32_t max_count) {
  if (!handle || !out || max_count <= 0) {
    return -1;
  }

  try {
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    if (!wrapper->hasStatsSubscription()) {
      return -1;
    }

    wrapper->lastStatsUpdates_ = wrapper->waitForStatsUpdates(max_count);

    int32_t i = 0;
    for (const auto& [key, contents, protocol] : wrapper->lastStatsUpdates_) {
      if (contents.size() >
          static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        XLOG(ERR) << "Stats update for key '" << key << "' is "
                  << contents.size() << " bytes (>INT32_MAX); dropping";
        continue;
      }
      out[i].key = key.c_str();
      out[i].data = reinterpret_cast<const uint8_t*>(contents.data());
      out[i].data_len = static_cast<int32_t>(contents.size());
      out[i].protocol = protocol;
      ++i;
    }
    return i;
  } catch (const std::exception& e) {
    XLOG(ERR) << "WaitForStatsUpdates error: " << e.what();
    return -1;
  }
}

/**
 * Free the state updates buffer (invalidates borrowed pointers from
 * WaitForStateUpdates)
 * @param handle Opaque handle to the wrapper
 */
void FreeStateUpdates(FsdbWrapperHandle handle) {
  if (!handle) {
    return;
  }
  auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
  wrapper->lastStateUpdates_.clear();
}

/**
 * Free the stats updates buffer (invalidates borrowed pointers from
 * WaitForStatsUpdates)
 * @param handle Opaque handle to the wrapper
 */
void FreeStatsUpdates(FsdbWrapperHandle handle) {
  if (!handle) {
    return;
  }
  auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
  wrapper->lastStatsUpdates_.clear();
}

int32_t WaitForStatePathUpdates(
    FsdbWrapperHandle handle,
    FsdbStatePathUpdate* out,
    int32_t max_count) {
  if (!handle || !out || max_count <= 0) {
    return -1;
  }
  try {
    auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
    if (!wrapper->hasStatePathSubscription()) {
      return -1;
    }

    wrapper->lastStatePathUpdates_ =
        wrapper->waitForStatePathUpdates(max_count);

    int32_t i = 0;
    for (const auto& [key, contents, protocol] :
         wrapper->lastStatePathUpdates_) {
      if (contents.size() >
          static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        XLOG(ERR) << "State-path update for key '" << key << "' is "
                  << contents.size() << " bytes (>INT32_MAX); dropping";
        continue;
      }
      out[i].key = key.c_str();
      out[i].data = reinterpret_cast<const uint8_t*>(contents.data());
      out[i].data_len = static_cast<int32_t>(contents.size());
      out[i].protocol = protocol;
      ++i;
    }
    return i;
  } catch (const std::exception& e) {
    XLOG(ERR) << "WaitForStatePathUpdates error: " << e.what();
    return -1;
  }
}

void FreeStatePathUpdates(FsdbWrapperHandle handle) {
  if (!handle) {
    return;
  }
  auto* wrapper = static_cast<FsdbCgoPubSubWrapper*>(handle);
  wrapper->lastStatePathUpdates_.clear();
}

} // extern "C"

} // namespace facebook::fboss::fsdb
