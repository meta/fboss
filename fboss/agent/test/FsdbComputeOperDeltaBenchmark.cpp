// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/Benchmark.h>
#include <folly/init/Init.h>

#include "fboss/agent/FsdbHelper.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/RouteScaleGenerators.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/fsdb/common/Utils.h"

namespace facebook::fboss {

namespace {
template <typename RouteGeneratorT>
struct BenchmarkHelperForRouteScale {
  std::shared_ptr<SwitchState> getState() {
    auto cfg = testConfigA();
    auto handle = createTestHandle(&cfg);

    RouteGeneratorT generator(
        handle->getSw()->getState(), handle->getSw()->needL2EntryForNeighbor());
    handle->getSw()->updateStateBlocking(
        "update 1", [=](const std::shared_ptr<SwitchState>& state) {
          return generator.resolveNextHops(state);
        });
    auto rid = RouterID(0);
    auto client = ClientID::BGPD;
    auto routeChunks = generator.getThriftRoutes();
    auto updater = handle->getSw()->getRouteUpdater();
    for (const auto& routeChunk : routeChunks) {
      std::for_each(
          routeChunk.begin(),
          routeChunk.end(),
          [&updater, client, rid](const auto& route) {
            updater.addRoute(rid, client, route);
          });
      updater.program();
    }
    return handle->getSw()->getState();
  }
};
} // namespace

#define DEFINE_BENCHMARK(SCALE)                                     \
  BENCHMARK(SCALE, numIters) {                                      \
    BenchmarkHelperForRouteScale<utility::SCALE##Generator> helper; \
    std::shared_ptr<SwitchState> state{};                           \
    BENCHMARK_SUSPEND {                                             \
      state = helper.getState();                                    \
    }                                                               \
    for (size_t n = 0; n < numIters; ++n) {                         \
      fsdb::computeOperDelta(                                       \
          std::make_shared<SwitchState>(),                          \
          state,                                                    \
          fsdbAgentDataSwitchStateRootPath(),                       \
          true);                                                    \
    }                                                               \
  }

DEFINE_BENCHMARK(RSWRouteScale);

DEFINE_BENCHMARK(FSWRouteScale);

DEFINE_BENCHMARK(THAlpmRouteScale);

DEFINE_BENCHMARK(HgridDuRouteScale);

DEFINE_BENCHMARK(HgridUuRouteScale);

DEFINE_BENCHMARK(AnticipatedRouteScale);

} // namespace facebook::fboss

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
