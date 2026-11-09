// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

// CPU benchmark for the stats serve loop, modelling the realistic FSDB stats
// scenario: a publisher writing full agent stats every time unit, a default
// subscriber on full agent stats served every `default_units` units, and zero
// or more sub-default-interval subscribers.
//
// A publish cannot be pruned to a subscriber's path -- the COW freeze walk has
// to visit the whole tree to keep the published-parent invariant -- so a fast
// subscriber on a narrow path still pays for a full tree publish on each of its
// ticks. Separating that from the cost of serving the subscribed data is what
// the narrow/wide pairs are for.
//
// Heavy root generation happens before the measured region; a bounded pool of
// distinct roots is cycled so memory stays bounded while each publish is a real
// change.

#include <sys/resource.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <folly/init/Init.h>
#include <folly/logging/LogLevel.h>
#include <folly/logging/LoggerDB.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <fboss/fsdb/oper/instantiations/FsdbNaivePeriodicSubscribableStorage.h>
#include <fboss/thrift_cow/storage/tests/TestDataFactory.h>

DEFINE_string(
    bm_role,
    "MaxScale",
    "Device role whose agent-stats scale to build. One of the roles in "
    "FsdbStatsDataFactory::getRoleScale: Minimal, MaxScale, RTSW, FTSW, STSW, "
    "RSW, FSW, SSW, XSW, MA, FA, RDSW, FDSW, SDSW, EDSW, RGSW. Raise "
    "--unit_ms with the role: a serve that outlasts the tick measures "
    "saturation, not per-serve cost.");
DEFINE_int32(updates, 200, "Number of stats publishes per case");
DEFINE_int32(trials, 3, "Repetitions per case; reported CPU is the mean");
DEFINE_int32(pool_size, 30, "Number of distinct pre-generated roots to cycle");
DEFINE_int32(
    unit_ms,
    20,
    "One time unit (publish + fast serve interval) in ms; 20ms/100ms serve "
    "intervals extrapolate 100x to prod's 2s/10s");
DEFINE_int32(
    default_units,
    5,
    "Default serve interval, in time units (prod is 10s/2s = 5x fast)");

namespace facebook::fboss::fsdb::test {

namespace {

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
  *root.agent()->threadHeartBeatMiss() += update;
  for (auto& [_, stats] : *root.agent()->hwPortStats()) {
    *stats.timestamp_() += update;
    *stats.inDiscards_() += update;
  }
  return root;
}

// A disabled tick is modelled as tick == default interval, which is what
// resolveServeTickMs() returns when the gflag is 0, and yields a single bucket.
std::unique_ptr<StorageT> buildStorage(
    const FsdbOperStatsRoot& root,
    bool enableTick) {
  // StorageParams::metricPrefix_ is a reference to the defaulted temporary that
  // must outlive the base ctor's snapshot; keep the construction inline.
  auto storage = std::make_unique<StorageT>(
      root,
      NaivePeriodicSubscribableStorageBase::StorageParams(
          std::chrono::milliseconds(FLAGS_unit_ms * FLAGS_default_units),
          std::chrono::seconds(5),
          false)
          .setServeTickInterval(
              std::chrono::milliseconds(
                  enableTick ? FLAGS_unit_ms
                             : FLAGS_unit_ms * FLAGS_default_units)));
  storage->setConvertToIDPaths(true);
  return storage;
}

PatchReader addPatchSub(
    StorageT& storage,
    std::optional<uint32_t> serveIntervalMs,
    std::string subId,
    std::vector<std::string> path) {
  RawOperPath rawPath;
  rawPath.path() = std::move(path);
  SubscriptionStorageParams subParams(std::nullopt, serveIntervalMs);
  return storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId(std::move(subId))),
      rawPath,
      subParams);
}

// agent/hwPortStats/<any>/inDiscards_ -- a wildcard the publisher touches on
// every update, so it resolves against the whole port map each publish.
PatchReader addWildcardSub(StorageT& storage, std::string subId) {
  ExtendedOperPath path;
  OperPathElem agent;
  agent.raw() = "agent";
  OperPathElem portStats;
  portStats.raw() = "hwPortStats";
  OperPathElem anyPort;
  anyPort.any() = true;
  OperPathElem counter;
  counter.raw() = "inDiscards_";
  path.path() = {agent, portStats, anyPort, counter};
  return storage.subscribe_patch_extended(
      SubscriptionIdentifier(SubscriberId(std::move(subId))),
      {{0, std::move(path)}});
}

double processCpuSeconds() {
  struct rusage ru{};
  getrusage(RUSAGE_SELF, &ru);
  auto toSec = [](const timeval& tv) {
    return static_cast<double>(tv.tv_sec) +
        static_cast<double>(tv.tv_usec) / 1e6;
  };
  return toSec(ru.ru_utime) + toSec(ru.ru_stime);
}

struct Case {
  const char* name;
  bool enableTick;
  // Buckets, other than the default one, to put an interval subscriber in.
  std::vector<size_t> fastBuckets;
  std::vector<std::string> fastPath;
  // Unsubscribe the default subscriber once initial sync has completed, so the
  // measured window carries the interval lane alone. Requesting a slower
  // interval cannot achieve this: normalizeServeIntervalMs clamps every request
  // to the default interval, so no subscriber can be slower than the default.
  bool dropSlowAfterSync{false};
  // publishAndAddPaths registers into every bucket's store on each publish, so
  // a wildcard in the default bucket may be charged to the fast lane's ticks.
  bool slowWildcardSub{false};
};

double runCase(const Case& c, const std::vector<FsdbOperStatsRoot>& pool) {
  auto storage = buildStorage(pool.front(), c.enableTick);
  storage->start();
  std::optional<PatchReader> slowSub =
      addPatchSub(*storage, std::nullopt, "agent_full", {"agent"});
  std::optional<PatchReader> wildcardSub;
  if (c.slowWildcardSub) {
    wildcardSub = addWildcardSub(*storage, "agent_indiscards_wildcard");
  }
  std::vector<PatchReader> fastSubs;
  fastSubs.reserve(c.fastBuckets.size());
  for (auto bucket : c.fastBuckets) {
    fastSubs.push_back(addPatchSub(
        *storage,
        static_cast<uint32_t>((bucket + 1) * FLAGS_unit_ms),
        fmt::format("interval_sub_b{}", bucket),
        c.fastPath));
  }

  auto settle =
      std::chrono::milliseconds(2 * FLAGS_unit_ms * FLAGS_default_units);
  // This is a wall-clock CPU benchmark, not a test; the sleeps model real
  // publish/serve cadence and are intentional.
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(settle);
  if (c.dropSlowAfterSync) {
    slowSub.reset();
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(settle);
  }

  StorageT::ConcretePath rootPath;
  auto before = processCpuSeconds();
  for (int i = 1; i <= FLAGS_updates; ++i) {
    storage->set(rootPath, pool[i % pool.size()]);
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_unit_ms));
  }
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(settle);
  auto cpu = processCpuSeconds() - before;
  storage->stop();
  return cpu;
}

struct Result {
  double mean{0.0};
  double lo{0.0};
  double hi{0.0};
};

Result runTrials(const Case& c, const std::vector<FsdbOperStatsRoot>& pool) {
  std::vector<double> samples;
  samples.reserve(FLAGS_trials);
  for (int t = 0; t < FLAGS_trials; ++t) {
    samples.push_back(runCase(c, pool));
  }
  Result r;
  r.lo = *std::min_element(samples.begin(), samples.end());
  r.hi = *std::max_element(samples.begin(), samples.end());
  for (auto v : samples) {
    r.mean += v;
  }
  r.mean /= static_cast<double>(samples.size());
  return r;
}

} // namespace

} // namespace facebook::fboss::fsdb::test

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::fsdb::test;

  // Suppress serve/stats log spew so the benchmark table stays readable.
  google::SetStderrLogging(google::GLOG_ERROR);
  folly::LoggerDB::get().setLevel(".", folly::LogLevel::ERR);

  if (FLAGS_pool_size <= 0 || FLAGS_updates <= 0 || FLAGS_trials <= 0) {
    std::fprintf(
        stderr, "--pool_size, --updates and --trials must be positive\n");
    return 1;
  }

  std::vector<FsdbOperStatsRoot> pool;
  pool.reserve(FLAGS_pool_size);
  for (int i = 0; i < FLAGS_pool_size; ++i) {
    pool.push_back(makeRoot(i));
  }

  const std::vector<std::string> kNarrow{"agent", "threadHeartBeatMiss"};
  const std::vector<std::string> kWide{"agent"};
  // Bucket 4 is the default bucket and already holds the default subscriber, so
  // occupying 0..3 is what makes every bucket busy.
  const std::vector<size_t> kFastOnly{0};
  const std::vector<size_t> kAllFast{0, 1, 2, 3};
  const std::vector<Case> cases = {
      // Baseline every ratio below is measured against.
      {.name = "tick disabled (as landed)",
       .enableTick = false,
       .fastPath = kNarrow},
      // Cost of the tick loop alone, before any fast subscriber exists.
      {.name = "default only", .enableTick = true, .fastPath = kNarrow},
      // A fast subscriber on one scalar: nearly all of it is extra publishes.
      {.name = "interval sub, narrow path",
       .enableTick = true,
       .fastBuckets = kFastOnly,
       .fastPath = kNarrow},
      // Same subscriber on the whole tree; the gap above it is the serve cost.
      {.name = "interval sub, WIDE path",
       .enableTick = true,
       .fastBuckets = kFastOnly,
       .fastPath = kWide},
      // Default subscriber dropped post-sync, leaving the interval lane alone.
      {.name = "initial-sync only slow + narrow fast",
       .enableTick = true,
       .fastBuckets = kFastOnly,
       .fastPath = kNarrow,
       .dropSlowAfterSync = true},
      // Every bucket busy, so they collide on shared multiples of the tick.
      {.name = "all buckets occupied, narrow",
       .enableTick = true,
       .fastBuckets = kAllFast,
       .fastPath = kNarrow},
      // Worst case the feature admits; size capacity against this one.
      {.name = "all buckets occupied, WIDE",
       .enableTick = true,
       .fastBuckets = kAllFast,
       .fastPath = kWide},
      // What a wildcard costs on its own, with no fast lane to charge it to.
      {.name = "wildcard slow, no fast",
       .enableTick = true,
       .fastPath = kNarrow,
       .slowWildcardSub = true},
      // Against "interval sub, narrow path": is the wildcard billed to the
      // fast lane's ticks?
      {.name = "wildcard slow + narrow fast",
       .enableTick = true,
       .fastBuckets = kFastOnly,
       .fastPath = kNarrow,
       .slowWildcardSub = true},
  };

  std::printf(
      "\nrealistic stats scenario: publish every %dms, default serve every "
      "%dms, fast serve every %dms, %d publishes, %d trials\n",
      FLAGS_unit_ms,
      FLAGS_unit_ms * FLAGS_default_units,
      FLAGS_unit_ms,
      FLAGS_updates,
      FLAGS_trials);
  std::printf("default sub: full agent, in the default (slowest) bucket\n");
  std::printf("interval sub count and path vary by case\n\n");
  std::printf(
      "%-38s %9s %9s %9s %8s\n", "case", "cpu_sec", "min", "max", "vs_base");
  std::optional<double> base;
  for (const auto& c : cases) {
    auto r = runTrials(c, pool);
    if (!base.has_value()) {
      base = r.mean;
    }
    std::printf(
        "%-38s %9.4f %9.4f %9.4f %7.2fx\n",
        c.name,
        r.mean,
        r.lo,
        r.hi,
        r.mean / *base);
  }
  return 0;
}
