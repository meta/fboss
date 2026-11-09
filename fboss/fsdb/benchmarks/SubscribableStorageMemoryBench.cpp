// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <fmt/format.h>
#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <folly/logging/LogLevel.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/xlog.h>
#include <glog/logging.h>

#include <fboss/fsdb/oper/instantiations/FsdbNaivePeriodicSubscribableStorage.h>
#include <fboss/thrift_cow/storage/tests/CowStorageBenchHelper.h>
#include <fboss/thrift_cow/storage/tests/TestDataFactory.h>

DEFINE_string(
    bm_role,
    "MaxScale",
    "Device role whose agent-stats scale to build. One of the roles in "
    "FsdbStatsDataFactory::getRoleScale: Minimal, MaxScale, RTSW, FTSW, STSW, "
    "RSW, FSW, SSW, XSW, MA, FA, RDSW, FDSW, SDSW, EDSW, RGSW.");

DEFINE_int32(
    bm_subscribers,
    5,
    "Total subscribers, spread round-robin over the occupied buckets. Held "
    "constant across the bucket-count cases so their deltas isolate the cost "
    "of a bucket; vary it to measure the cost of a subscription instead.");

namespace facebook::fboss::fsdb::test {

namespace {

constexpr std::chrono::milliseconds kTick{2000};
constexpr std::chrono::milliseconds kDefaultInterval{10000};
// A disabled tick is modelled as tick == default interval, which is what
// resolveServeTickMs() returns when the gflag is 0, and yields a single bucket.
constexpr std::chrono::milliseconds tickFor(bool enableTick) {
  return enableTick ? kTick : kDefaultInterval;
}
constexpr size_t defaultBucketFor(bool enableTick) {
  // Unsigned: a tick larger than the default interval would wrap to SIZE_MAX.
  static_assert(kTick <= kDefaultInterval, "tick must not exceed the default");
  return static_cast<size_t>(kDefaultInterval / tickFor(enableTick)) - 1;
}

struct Scenario {
  bool enableTick;
  size_t fastBuckets;
};

using StorageT = FsdbNaivePeriodicSubscribableStatsStorage;
using PatchReader =
    SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>;

test_data::RoleSelector roleFromFlag() {
  auto role = test_data::statsRoleFromString(FLAGS_bm_role);
  CHECK(role.has_value()) << "unknown --bm_role: " << FLAGS_bm_role;
  return *role;
}

FsdbOperStatsRoot makeRoot(int update) {
  test_data::FsdbStatsDataFactory dataGen(roleFromFlag());
  auto seed = dataGen.getStateUpdate(update, false);
  auto root = facebook::fboss::thrift_cow::
      deserialize<apache::thrift::type_class::structure, FsdbOperStatsRoot>(
          *seed.state()->protocol(),
          apache::thrift::can_throw(*seed.state()->contents()));
  for (auto& [_, stats] : *root.agent()->hwPortStats()) {
    *stats.timestamp_() += update;
  }
  return root;
}

std::unique_ptr<StorageT> buildStorage(
    const FsdbOperStatsRoot& root,
    bool enableTick) {
  auto storage = std::make_unique<StorageT>(
      root,
      NaivePeriodicSubscribableStorageBase::StorageParams(kDefaultInterval)
          .setServeTickInterval(tickFor(enableTick)));
  storage->setConvertToIDPaths(true);
  return storage;
}

PatchReader addSub(
    StorageT& storage,
    std::optional<uint32_t> serveIntervalMs,
    std::string subId) {
  RawOperPath rawPath;
  rawPath.path() = std::vector<std::string>{};
  SubscriptionStorageParams subParams(std::nullopt, serveIntervalMs);
  return storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId(std::move(subId))),
      rawPath,
      subParams);
}

// Today every subscriber sits in the default bucket, so that is the zero point
// every case is measured against.
std::vector<size_t> occupiedBuckets(const Scenario& sc) {
  const auto defaultBucket = defaultBucketFor(sc.enableTick);
  // Fast buckets are 0..fastBuckets-1 and the default bucket is the last one,
  // so anything at or past it would repeat an index and make the caller
  // publish and serve that bucket twice, inflating the per-bucket cost this
  // benchmark exists to report.
  CHECK_LE(sc.fastBuckets, defaultBucket)
      << "fastBuckets=" << sc.fastBuckets << " overlaps default bucket "
      << defaultBucket;
  std::vector<size_t> buckets{defaultBucket};
  for (size_t bucket = 0; bucket < sc.fastBuckets; ++bucket) {
    buckets.push_back(bucket);
  }
  return buckets;
}

// Readers must stay alive for the subscriptions to stay registered.
std::vector<PatchReader> addSubscribers(
    StorageT& storage,
    const Scenario& sc,
    const std::vector<size_t>& buckets) {
  std::vector<PatchReader> readers;
  readers.reserve(FLAGS_bm_subscribers);
  const auto tickMs = tickFor(sc.enableTick).count();
  for (int i = 0; i < FLAGS_bm_subscribers; ++i) {
    const auto bucket = buckets[static_cast<size_t>(i) % buckets.size()];
    const auto intervalMs = static_cast<uint32_t>((bucket + 1) * tickMs);
    readers.push_back(
        addSub(storage, intervalMs, fmt::format("mem_sub_{}", i)));
  }
  return readers;
}

std::optional<int64_t> measureStatsStorageFootprint(const Scenario& sc) {
  auto bytesBefore = thrift_cow::test::getJemallocAllocatedBytes();
  if (bytesBefore == 0) {
    return std::nullopt;
  }

  const auto buckets = occupiedBuckets(sc);
  auto storage = buildStorage(makeRoot(0), sc.enableTick);
  auto readers = addSubscribers(*storage, sc, buckets);

  // Publish only where subscribers landed: the serve loop releases the baseline
  // of any bucket that has none.
  StorageT::ConcretePath rootPath;
  int update = 1;
  for (auto bucket : buckets) {
    CHECK(!storage->set(rootPath, makeRoot(update++)).has_value());
    storage->publishCurrentState(bucket);
  }
  // The serve loop refreshes the GET slot at the end of every tick, so without
  // this lastPublishedState_ would pin the initial root for the whole
  // measurement and inflate every case by one tree.
  storage->refreshPublishedStateForReads();
  CHECK(!storage->set(rootPath, makeRoot(update)).has_value());

  auto bytesAfter = thrift_cow::test::getJemallocAllocatedBytes();
  return bytesAfter == 0 ? std::nullopt
                         : std::optional<int64_t>(bytesAfter - bytesBefore);
}

void reportCounters(
    folly::UserCounters& counters,
    const std::vector<int64_t>& samples) {
  if (samples.empty()) {
    return;
  }
  int64_t sum = 0;
  int64_t maxV = *std::max_element(samples.begin(), samples.end());
  for (auto v : samples) {
    sum += v;
  }
  double avg = static_cast<double>(sum) / static_cast<double>(samples.size());

  double variance = 0.0;
  if (samples.size() > 1) {
    for (auto v : samples) {
      double diff = static_cast<double>(v) - avg;
      variance += diff * diff;
    }
    variance /= static_cast<double>(samples.size() - 1);
  }
  double stddev = std::sqrt(variance);

  counters["avg_allocated_KB"] = folly::UserMetric(avg / 1024.0);
  counters["max_allocated_KB"] = folly::UserMetric(maxV / 1024.0);
  counters["stddev_allocated_KB"] = folly::UserMetric(stddev / 1024.0);
}

void fsdb_stats_storage(
    folly::UserCounters& counters,
    unsigned /* iters */,
    bool enableTick,
    size_t fastBuckets) {
  const Scenario sc{enableTick, fastBuckets};
  // Discard a warm-up iteration so one-time init isn't charged to a sample.
  measureStatsStorageFootprint(sc);

  std::vector<int64_t> samples;
  samples.reserve(FLAGS_bm_memory_iters);
  for (int i = 0; i < FLAGS_bm_memory_iters; i++) {
    auto sample = measureStatsStorageFootprint(sc);
    if (sample.has_value() && *sample > 0) {
      samples.push_back(*sample);
    }
  }
  if (samples.empty()) {
    XLOG(WARNING) << "no usable samples; jemalloc is required for this "
                  << "benchmark and the reported counters will be absent";
    return;
  }
  reportCounters(counters, samples);
}

// Serving is the one place a peak could hide: publishCurrentState hands the
// bucket's previous root to a local while the slot takes the new one, and
// serveSubscriptions then builds a patch per subscriber. subscriptions_ is
// protected, so driving a real serve needs a subclass.
class ServeProbeStorage : public StorageT {
 public:
  using StorageT::StorageT;

  struct Peak {
    int64_t afterPublish{0};
    int64_t afterServe{0};
    int64_t afterRelease{0};
    // How long the GET slot stays stale: the serve loop refreshes it only
    // after every due bucket has been served.
    int64_t staleWindowUs{0};
    // Freed when the GET slot catches up, i.e. the tree the lagging
    // lastPublishedState_ was the sole holder of once the bucket moved on.
    int64_t staleGetSlot{0};
  };

  // Serves every occupied bucket back to back, which is what the loop does on
  // a tick where their cadences coincide.
  Peak serveBucketsSampled(const std::vector<size_t>& buckets) {
    const auto base = thrift_cow::test::getJemallocAllocatedBytes();
    Peak peak;
    const auto windowStart = std::chrono::steady_clock::now();
    for (auto bucket : buckets) {
      {
        auto [oldRoot, newRoot, metadataServer] =
            this->publishCurrentState(bucket);
        peak.afterPublish = std::max(
            peak.afterPublish,
            thrift_cow::test::getJemallocAllocatedBytes() - base);
        this->subscriptions_.serveSubscriptions(
            oldRoot, newRoot, metadataServer, bucket);
        peak.afterServe = std::max(
            peak.afterServe,
            thrift_cow::test::getJemallocAllocatedBytes() - base);
      }
      // oldRoot/newRoot are gone here, so anything still held is retained.
      peak.afterRelease = std::max(
          peak.afterRelease,
          thrift_cow::test::getJemallocAllocatedBytes() - base);
    }

    // Every bucket has now moved past the version the GET slot still points
    // at, which is the window the serve loop sits in until it refreshes.
    peak.staleWindowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - windowStart)
                             .count();
    const auto beforeRefresh = thrift_cow::test::getJemallocAllocatedBytes();
    this->refreshPublishedStateForReads();
    peak.staleGetSlot =
        beforeRefresh - thrift_cow::test::getJemallocAllocatedBytes();
    return peak;
  }
};

std::optional<ServeProbeStorage::Peak> measureServePeak(const Scenario& sc) {
  if (thrift_cow::test::getJemallocAllocatedBytes() == 0) {
    return std::nullopt;
  }
  const auto buckets = occupiedBuckets(sc);
  // Constructed inline for the same reason buildStorage is: StorageParams
  // holds metricPrefix_ as a reference to the defaulted "fsdb" temporary,
  // which dies at the end of the full-expression. Factoring this into a
  // helper that returns StorageParams by value would leave it dangling
  // before the storage ctor snapshots it.
  ServeProbeStorage storage(
      makeRoot(0),
      NaivePeriodicSubscribableStorageBase::StorageParams(kDefaultInterval)
          .setServeTickInterval(tickFor(sc.enableTick)));
  storage.setConvertToIDPaths(true);
  auto readers = addSubscribers(storage, sc, buckets);

  // Reach steady state first: every occupied bucket holds a baseline and the
  // GET slot has caught up, so the sampled serve measures only the delta.
  StorageT::ConcretePath rootPath;
  int update = 1;
  for (auto bucket : buckets) {
    CHECK(!storage.set(rootPath, makeRoot(update++)).has_value());
    storage.publishCurrentState(bucket);
  }
  storage.refreshPublishedStateForReads();
  CHECK(!storage.set(rootPath, makeRoot(update)).has_value());

  return storage.serveBucketsSampled(buckets);
}

void fsdb_serve_peak(
    folly::UserCounters& counters,
    unsigned /* iters */,
    bool enableTick,
    size_t fastBuckets) {
  const Scenario sc{enableTick, fastBuckets};
  measureServePeak(sc);

  std::vector<int64_t> pub, srv, rel, stale, windowUs;
  for (int i = 0; i < FLAGS_bm_memory_iters; i++) {
    if (auto p = measureServePeak(sc)) {
      pub.push_back(p->afterPublish);
      srv.push_back(p->afterServe);
      rel.push_back(p->afterRelease);
      stale.push_back(p->staleGetSlot);
      windowUs.push_back(p->staleWindowUs);
    }
  }
  if (pub.empty()) {
    XLOG(WARNING) << "no usable samples; jemalloc is required for this "
                  << "benchmark and the reported counters will be absent";
    return;
  }
  auto avgKB = [](const std::vector<int64_t>& v) {
    int64_t sum = 0;
    for (auto x : v) {
      sum += x;
    }
    return folly::UserMetric(
        static_cast<double>(sum) / static_cast<double>(v.size()) / 1024.0);
  };
  counters["publish_peak_KB"] = avgKB(pub);
  counters["serve_peak_KB"] = avgKB(srv);
  counters["retained_KB"] = avgKB(rel);
  counters["stale_getslot_KB"] = avgKB(stale);
  int64_t wsum = 0;
  for (auto v : windowUs) {
    wsum += v;
  }
  counters["stale_window_us"] = folly::UserMetric(
      static_cast<double>(wsum) / static_cast<double>(windowUs.size()));
}

} // namespace

BENCHMARK_COUNTERS_NAME_PARAM(
    fsdb_stats_storage,
    counters,
    tick_disabled,
    false,
    0);

BENCHMARK_COUNTERS_NAME_PARAM(
    fsdb_stats_storage,
    counters,
    default_only,
    true,
    0);

BENCHMARK_COUNTERS_NAME_PARAM(
    fsdb_stats_storage,
    counters,
    default_plus_1fast,
    true,
    1);

BENCHMARK_COUNTERS_NAME_PARAM(
    fsdb_stats_storage,
    counters,
    default_plus_2fast,
    true,
    2);

BENCHMARK_COUNTERS_NAME_PARAM(
    fsdb_stats_storage,
    counters,
    default_plus_3fast,
    true,
    3);

BENCHMARK_COUNTERS_NAME_PARAM(
    fsdb_stats_storage,
    counters,
    default_plus_4fast,
    true,
    4);

BENCHMARK_COUNTERS_NAME_PARAM(
    fsdb_serve_peak,
    counters,
    tick_disabled,
    false,
    0);

BENCHMARK_COUNTERS_NAME_PARAM(fsdb_serve_peak, counters, default_only, true, 0);

BENCHMARK_COUNTERS_NAME_PARAM(
    fsdb_serve_peak,
    counters,
    default_plus_1fast,
    true,
    1);

BENCHMARK_COUNTERS_NAME_PARAM(
    fsdb_serve_peak,
    counters,
    default_plus_4fast,
    true,
    4);

} // namespace facebook::fboss::fsdb::test

int main(int argc, char* argv[]) {
  folly::Init init(&argc, &argv);
  // Suppress serve/stats log spew so the benchmark table stays readable.
  google::SetStderrLogging(google::GLOG_ERROR);
  folly::LoggerDB::get().setLevel(".", folly::LogLevel::ERR);

  if (FLAGS_bm_subscribers <= 0 || FLAGS_bm_memory_iters <= 0) {
    std::fprintf(
        stderr, "--bm_subscribers and --bm_memory_iters must be positive\n");
    return 1;
  }

  folly::runBenchmarks();
  return 0;
}
