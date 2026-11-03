// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {
namespace {

constexpr auto kSwAgentService = "fboss_sw_agent.service";
constexpr auto kFibStatePath = "fibsInfoMap";
constexpr int16_t kRouteClient = static_cast<int16_t>(ClientID::BGPD);
constexpr uint32_t kRouteCount = 8192;
constexpr size_t kRouteChunkSize = 1024;
constexpr size_t kRequestThreadCount = 32;
constexpr size_t kMinimumRequestsPerThread = 4;
constexpr size_t kMaximumRequestsPerThread = 32;
constexpr size_t kTargetSerializedBytes = 512 << 20;
constexpr size_t kMinimumPeakGrowth = 128 << 20;
constexpr size_t kMaximumRetainedPercent = 50;
constexpr auto kRssSampleInterval = std::chrono::milliseconds(20);
constexpr auto kReclaimPollInterval = std::chrono::seconds(1);
constexpr auto kReclaimTimeout = std::chrono::seconds(25);

struct RouteBatch {
  std::vector<UnicastRoute> routes;
  std::vector<IpPrefix> prefixes;
};

class AgentMemoryReclamationTest : public Fboss2IntegrationTest {
 protected:
  void restartSwAgent() const {
    const auto result =
        runCmd({"/usr/bin/systemctl", "restart", kSwAgentService});
    if (result.exitCode != 0) {
      throw std::runtime_error(
          fmt::format(
              "Failed to restart {}: {}", kSwAgentService, result.stderr));
    }
    waitForAgentReady();
  }

  std::unique_ptr<apache::thrift::Client<FbossCtrl>> createAgentClient() const {
    return utils::createClient<apache::thrift::Client<FbossCtrl>>(
        HostInfo("localhost"));
  }

  int64_t getSwAgentPid() const {
    std::error_code error;
    for (const auto& entry :
         std::filesystem::directory_iterator("/proc", error)) {
      const auto pidText = entry.path().filename().string();
      if (pidText.empty() ||
          !std::all_of(pidText.begin(), pidText.end(), [](unsigned char c) {
            return std::isdigit(c);
          })) {
        continue;
      }

      std::ifstream comm{entry.path() / "comm"};
      std::string processName;
      if (std::getline(comm, processName) && processName == "fboss_sw_agent") {
        return std::stoll(pidText);
      }
    }
    throw std::runtime_error(
        fmt::format("Unable to find a running {} process", kSwAgentService));
  }

  size_t readResidentBytes(int64_t pid) const {
    std::ifstream input{fmt::format("/proc/{}/smaps_rollup", pid)};
    std::string line;
    while (std::getline(input, line)) {
      size_t rssKb{0};
      if (std::sscanf(line.c_str(), "Rss: %zu kB", &rssKb) == 1) {
        return rssKb << 10;
      }
    }
    throw std::runtime_error(
        fmt::format("Unable to read RSS for fboss_sw_agent PID {}", pid));
  }

  size_t getFibStateSize() const {
    auto client = createAgentClient();
    std::string stateJson;
    client->sync_getCurrentStateJSON(stateJson, kFibStatePath);
    return stateJson.size();
  }

  RouteBatch makeDropRoutes() const {
    RouteBatch batch;
    batch.routes.reserve(kRouteCount);
    batch.prefixes.reserve(kRouteCount);
    for (uint32_t i = 0; i < kRouteCount; ++i) {
      IpPrefix prefix;
      prefix.ip() = network::toBinaryAddress(
          folly::IPAddress(fmt::format("2001:db8:ffff:{:x}::", i)));
      prefix.prefixLength() = 64;

      UnicastRoute route;
      route.dest() = prefix;
      route.action() = RouteForwardAction::DROP;
      route.adminDistance() = AdminDistance::EBGP;
      batch.routes.emplace_back(std::move(route));
      batch.prefixes.emplace_back(std::move(prefix));
    }
    return batch;
  }

  void programRoutes(const std::vector<UnicastRoute>& routes) const {
    auto client = createAgentClient();
    for (size_t begin = 0; begin < routes.size(); begin += kRouteChunkSize) {
      const auto end = std::min(begin + kRouteChunkSize, routes.size());
      std::vector<UnicastRoute> chunk{
          routes.begin() + begin, routes.begin() + end};
      client->sync_addUnicastRoutes(kRouteClient, chunk);
    }
  }

  void unprogramRoutes(const std::vector<IpPrefix>& prefixes) const {
    auto client = createAgentClient();
    for (size_t begin = 0; begin < prefixes.size(); begin += kRouteChunkSize) {
      const auto end = std::min(begin + kRouteChunkSize, prefixes.size());
      std::vector<IpPrefix> chunk{
          prefixes.begin() + begin, prefixes.begin() + end};
      client->sync_deleteUnicastRoutes(kRouteClient, chunk);
    }
  }

  static size_t requestsPerThread(size_t serializedStateBytes) {
    const auto bytesPerRound =
        std::max<size_t>(serializedStateBytes, 1) * kRequestThreadCount;
    const auto requests =
        (kTargetSerializedBytes + bytesPerRound - 1) / bytesPerRound;
    return std::clamp(
        requests, kMinimumRequestsPerThread, kMaximumRequestsPerThread);
  }

  std::future<size_t> startStateReader(
      const std::shared_future<void>& startSignal,
      size_t requestCount) const {
    return std::async(
        std::launch::async, [this, startSignal, requestCount]() mutable {
          auto client = createAgentClient();
          startSignal.wait();
          size_t serializedBytes{0};
          for (size_t i = 0; i < requestCount; ++i) {
            std::string stateJson;
            client->sync_getCurrentStateJSON(stateJson, kFibStatePath);
            serializedBytes += stateJson.size();
          }
          return serializedBytes;
        });
  }

  size_t waitForStateReaders(
      int64_t pid,
      std::vector<std::future<size_t>>& readers,
      size_t initialPeak) const {
    auto peak = initialPeak;
    while (true) {
      peak = std::max(peak, readResidentBytes(pid));
      auto pending = std::find_if(
          readers.begin(), readers.end(), [](std::future<size_t>& reader) {
            return reader.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready;
          });
      if (pending == readers.end()) {
        return peak;
      }
      pending->wait_for(kRssSampleInterval);
    }
  }

  size_t waitForMemoryReclamation(
      int64_t pid,
      size_t baseline,
      size_t maximumRetainedGrowth) const {
    auto current = readResidentBytes(pid);
    const auto deadline = std::chrono::steady_clock::now() + kReclaimTimeout;
    while (growthFromBaseline(baseline, current) > maximumRetainedGrowth &&
           std::chrono::steady_clock::now() < deadline) {
      std::promise<void> delay;
      delay.get_future().wait_for(kReclaimPollInterval);
      current = readResidentBytes(pid);
    }
    return current;
  }

  static size_t growthFromBaseline(size_t baseline, size_t current) {
    return current > baseline ? current - baseline : 0;
  }
};

TEST_F(AgentMemoryReclamationTest, ReclaimsRssAfterRouteStateSerialization) {
  if (folly::kIsSanitizeAddress) {
    GTEST_SKIP()
        << "ASAN replaces the process allocator, so RSS reclamation does not "
           "measure jemalloc behavior";
  }

  restartSwAgent();
  const auto swAgentPid = getSwAgentPid();
  getFibStateSize();
  const auto baseline = readResidentBytes(swAgentPid);

  auto routeBatch = makeDropRoutes();
  bool routesProgrammed = true;
  auto cleanup = folly::makeGuard([&] {
    if (!routesProgrammed) {
      return;
    }
    try {
      unprogramRoutes(routeBatch.prefixes);
    } catch (const std::exception& error) {
      XLOG(ERR) << "Failed to clean up memory-test routes: " << error.what();
    }
  });
  programRoutes(routeBatch.routes);

  const auto populatedFibBytes = getFibStateSize();
  const auto requestCount = requestsPerThread(populatedFibBytes);
  std::promise<void> startPromise;
  const auto startSignal = startPromise.get_future().share();
  std::vector<std::future<size_t>> readers;
  readers.reserve(kRequestThreadCount);
  for (size_t i = 0; i < kRequestThreadCount; ++i) {
    readers.emplace_back(startStateReader(startSignal, requestCount));
  }

  auto peak = readResidentBytes(swAgentPid);
  startPromise.set_value();
  peak = waitForStateReaders(swAgentPid, readers, peak);

  size_t totalSerializedBytes{0};
  for (auto& reader : readers) {
    totalSerializedBytes += reader.get();
  }

  peak = std::max(peak, readResidentBytes(swAgentPid));
  unprogramRoutes(routeBatch.prefixes);
  routesProgrammed = false;
  peak = std::max(peak, readResidentBytes(swAgentPid));
  const auto peakGrowth = growthFromBaseline(baseline, peak);
  const auto maximumRetainedGrowth = peakGrowth * kMaximumRetainedPercent / 100;
  const auto finalRss =
      waitForMemoryReclamation(swAgentPid, baseline, maximumRetainedGrowth);
  const auto retainedGrowth = growthFromBaseline(baseline, finalRss);
  const auto retainedPercent =
      peakGrowth == 0 ? 100 : retainedGrowth * 100 / peakGrowth;

  RecordProperty("sw_agent_pid", std::to_string(swAgentPid));
  RecordProperty("routes", std::to_string(kRouteCount));
  RecordProperty("fib_state_bytes", std::to_string(populatedFibBytes));
  RecordProperty("request_threads", std::to_string(kRequestThreadCount));
  RecordProperty("requests_per_thread", std::to_string(requestCount));
  RecordProperty(
      "total_serialized_bytes", std::to_string(totalSerializedBytes));
  RecordProperty("baseline_rss", std::to_string(baseline));
  RecordProperty("peak_rss", std::to_string(peak));
  RecordProperty("peak_growth", std::to_string(peakGrowth));
  RecordProperty("final_rss", std::to_string(finalRss));
  RecordProperty("retained_growth", std::to_string(retainedGrowth));
  RecordProperty("retained_percent", std::to_string(retainedPercent));
  RecordProperty(
      "retained_percent_limit", std::to_string(kMaximumRetainedPercent));

  XLOG(INFO) << "sw_agent_pid=" << swAgentPid << ", routes=" << kRouteCount
             << ", fib_state_bytes=" << populatedFibBytes
             << ", request_threads=" << kRequestThreadCount
             << ", requests_per_thread=" << requestCount
             << ", total_serialized_bytes=" << totalSerializedBytes
             << ", baseline_rss=" << baseline << ", peak_rss=" << peak
             << ", peak_growth=" << peakGrowth << ", final_rss=" << finalRss
             << ", retained_growth=" << retainedGrowth
             << ", retained_percent=" << retainedPercent
             << ", retained_percent_limit=" << kMaximumRetainedPercent;

  EXPECT_GE(totalSerializedBytes, kTargetSerializedBytes);
  ASSERT_GE(peakGrowth, kMinimumPeakGrowth)
      << "Workload did not create enough RSS growth to test reclamation";
  EXPECT_LE(retainedPercent, kMaximumRetainedPercent);
}

} // namespace
} // namespace facebook::fboss
