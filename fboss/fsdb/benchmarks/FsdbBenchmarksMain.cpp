// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/Init.h>
#include <gtest/gtest.h>

#include <sys/resource.h>

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG3; default:async=true");

inline bool listBenchmarks(int* argc, char** argv) {
  for (int i = 0; i < *argc; i++) {
    if (strcmp(argv[i], "--bm_list") == 0) {
      return true;
    }
  }
  return false;
}

inline int64_t timevalToUsec(const timeval& tv) {
  const int64_t kUsecPerSecond = 1000000;

  return (int64_t(tv.tv_sec) * kUsecPerSecond) + tv.tv_usec;
}

int main(int argc, char* argv[]) {
  struct rusage startUsage, endUsage;

  getrusage(RUSAGE_SELF, &startUsage);

  FLAGS_bm_max_iters = 2;

  if (!listBenchmarks(&argc, argv)) {
    testing::InitGoogleTest(&argc, argv);
  }

  folly::Init init(&argc, &argv, false);

  folly::runBenchmarks();

  getrusage(RUSAGE_SELF, &endUsage);
  auto cpuTime =
      (timevalToUsec(endUsage.ru_stime) - timevalToUsec(startUsage.ru_stime)) +
      (timevalToUsec(endUsage.ru_utime) - timevalToUsec(startUsage.ru_utime));

  folly::dynamic rusageJson = folly::dynamic::object;
  rusageJson["cpu_time_usec"] = cpuTime;
  rusageJson["max_rss"] = endUsage.ru_maxrss;
  std::cout << toPrettyJson(rusageJson) << std::endl;

  return 0;
}
