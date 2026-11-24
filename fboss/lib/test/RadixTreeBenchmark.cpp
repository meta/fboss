// Copyright 2004-present Facebook. All Rights Reserved.

#include <folly/Benchmark.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <set>
#include <vector>
#include "common/base/Random.h"
#include "fboss/lib/RadixTree.h"
#include "fboss/lib/test/PyRadixWrapper.h"

using namespace std;
using namespace folly;
using namespace facebook;
using namespace facebook::network;

DEFINE_int32(
    insert_count,
    10000,
    "The number of inserts to performed on each insert iteration");
DEFINE_int32(
    erase_count,
    1000,
    "The number of elements to erase on each erase iteration");
DEFINE_int32(
    lookup_count,
    5000,
    "The number of elements to look up on each lookup iteration");
namespace {
set<Prefix4> insertSet4;
set<Prefix4> eraseSet4;
set<Prefix4> exactMatchSet4;
set<Prefix4> longestMatchSet4;
set<Prefix6> insertSet6;
set<Prefix6> eraseSet6;
set<Prefix6> exactMatchSet6;
set<Prefix6> longestMatchSet6;
vector<int> valueSet;

// V4 Benchmarks
template <typename TREE>
void setupTree4(TREE& tree) {
  auto count = 0;
  for (auto pfx : insertSet4) {
    tree.insert(pfx.ip, pfx.mask, valueSet[count++]);
  }
}

BENCHMARK(PyRadixInsert4) {
  PyRadixWrapper<IPAddressV4, int> pyrtree;
  setupTree4(pyrtree);
}

BENCHMARK_RELATIVE(RadixTreeInsert4) {
  RadixTree<IPAddressV4, int> rtree;
  setupTree4(rtree);
}

BENCHMARK(PyRadixErase4) {
  PyRadixWrapper<IPAddressV4, int> pyrtree;
  BENCHMARK_SUSPEND {
    setupTree4(pyrtree);
  }
  for (auto pfx : eraseSet4) {
    pyrtree.erase(pfx.ip, pfx.mask);
  }
}

BENCHMARK_RELATIVE(RadixTreeErase4) {
  RadixTree<IPAddressV4, int> rtree;
  BENCHMARK_SUSPEND {
    setupTree4(rtree);
  }
  for (auto pfx : eraseSet4) {
    rtree.erase(pfx.ip, pfx.mask);
  }
}

BENCHMARK(PyRadixExactMatch4) {
  PyRadixWrapper<IPAddressV4, int> pyrtree;
  BENCHMARK_SUSPEND {
    setupTree4(pyrtree);
  }
  for (auto pfx : exactMatchSet4) {
    pyrtree.exactMatch(pfx.ip, pfx.mask);
  }
}

BENCHMARK_RELATIVE(RadixTreeExactMatch4) {
  RadixTree<IPAddressV4, int> rtree;
  BENCHMARK_SUSPEND {
    setupTree4(rtree);
  }
  for (auto pfx : exactMatchSet4) {
    rtree.exactMatch(pfx.ip, pfx.mask);
  }
}

BENCHMARK(PyRadixLongestMatch4) {
  PyRadixWrapper<IPAddressV4, int> pyrtree;
  BENCHMARK_SUSPEND {
    setupTree4(pyrtree);
  }
  for (auto pfx : longestMatchSet4) {
    pyrtree.longestMatch(pfx.ip, pfx.mask);
  }
}

BENCHMARK_RELATIVE(RadixTreeLongestMatch4) {
  RadixTree<IPAddressV4, int> rtree;
  BENCHMARK_SUSPEND {
    setupTree4(rtree);
  }
  for (auto pfx : longestMatchSet4) {
    rtree.longestMatch(pfx.ip, pfx.mask);
  }
}

// V6 benchmarks

template <typename TREE>
void setupTree6(TREE& tree) {
  auto count = 0;
  for (auto pfx : insertSet6) {
    tree.insert(pfx.ip, pfx.mask, valueSet[count++]);
  }
}

BENCHMARK(PyRadixInsert6) {
  PyRadixWrapper<IPAddressV6, int> pyrtree;
  setupTree6(pyrtree);
}

BENCHMARK_RELATIVE(RadixTreeInsert6) {
  RadixTree<IPAddressV6, int> rtree;
  setupTree6(rtree);
}

BENCHMARK(PyRadixErase6) {
  PyRadixWrapper<IPAddressV6, int> pyrtree;
  BENCHMARK_SUSPEND {
    setupTree6(pyrtree);
  }
  for (auto pfx : eraseSet6) {
    pyrtree.erase(pfx.ip, pfx.mask);
  }
}

BENCHMARK_RELATIVE(RadixTreeErase6) {
  RadixTree<IPAddressV6, int> rtree;
  BENCHMARK_SUSPEND {
    setupTree6(rtree);
  }
  for (auto pfx : eraseSet6) {
    rtree.erase(pfx.ip, pfx.mask);
  }
}

BENCHMARK(PyRadixExactMatch6) {
  PyRadixWrapper<IPAddressV6, int> pyrtree;
  BENCHMARK_SUSPEND {
    setupTree6(pyrtree);
  }
  for (auto pfx : exactMatchSet6) {
    pyrtree.exactMatch(pfx.ip, pfx.mask);
  }
}

BENCHMARK_RELATIVE(RadixTreeExactMatch6) {
  RadixTree<IPAddressV6, int> rtree;
  BENCHMARK_SUSPEND {
    setupTree6(rtree);
  }
  for (auto pfx : exactMatchSet6) {
    rtree.exactMatch(pfx.ip, pfx.mask);
  }
}

BENCHMARK(PyRadixLongestMatch6) {
  PyRadixWrapper<IPAddressV6, int> pyrtree;
  BENCHMARK_SUSPEND {
    setupTree6(pyrtree);
  }
  for (auto pfx : longestMatchSet6) {
    pyrtree.longestMatch(pfx.ip, pfx.mask);
  }
}

BENCHMARK_RELATIVE(RadixTreeLongestMatch6) {
  RadixTree<IPAddressV6, int> rtree;
  BENCHMARK_SUSPEND {
    setupTree6(rtree);
  }
  for (auto pfx : longestMatchSet6) {
    rtree.longestMatch(pfx.ip, pfx.mask);
  }
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
  vector<Prefix4> inserted4;
  while (insertSet4.size() < FLAGS_insert_count) {
    auto mask = folly::Random::rand32(32);
    auto ip = IPAddressV4::fromLongHBO(folly::Random::rand32());
    ip = ip.mask(mask);
    if (insertSet4.insert(Prefix4(ip, mask)).second) {
      valueSet.push_back(ip.toLong());
      inserted4.emplace_back(ip, mask);
    }
  }
  while (eraseSet4.size() < FLAGS_erase_count) {
    auto index = folly::Random::rand32(FLAGS_insert_count - 1);
    eraseSet4.insert(inserted4[index]);
  }
  while (exactMatchSet4.size() < FLAGS_lookup_count) {
    auto index = folly::Random::rand32(FLAGS_insert_count - 1);
    CHECK(index < FLAGS_insert_count);
    exactMatchSet4.insert(inserted4[index]);
  }
  for (auto pfx : exactMatchSet4) {
    auto newMask = pfx.mask ? folly::Random::rand32(pfx.mask) : pfx.mask;
    auto newIp = pfx.ip.mask(newMask);
    longestMatchSet4.insert(Prefix4(newIp, newMask));
  }

  // Generate random V6 prefixes
  vector<Prefix6> inserted6;
  while (insertSet6.size() < FLAGS_insert_count) {
    auto mask = folly::Random::rand32(128);
    ByteArray16 ba;
    *(uint64_t*)(ba.data()) = folly::Random::rand64();
    *(uint64_t*)(&ba[8]) = folly::Random::rand64();
    auto ip = IPAddressV6(ba);
    ip = ip.mask(mask);
    if (insertSet6.insert(Prefix6(ip, mask)).second) {
      inserted6.emplace_back(ip, mask);
    }
  }
  while (eraseSet6.size() < FLAGS_erase_count) {
    auto index = folly::Random::rand32(FLAGS_insert_count - 1);
    eraseSet6.insert(inserted6[index]);
  }
  while (exactMatchSet6.size() < FLAGS_lookup_count) {
    auto index = folly::Random::rand32(FLAGS_insert_count - 1);
    CHECK(index < FLAGS_insert_count);
    exactMatchSet6.insert(inserted6[index]);
  }
  for (auto pfx : exactMatchSet6) {
    auto newMask = pfx.mask ? folly::Random::rand32(pfx.mask) : pfx.mask;
    auto newIp = pfx.ip.mask(newMask);
    longestMatchSet6.insert(Prefix6(newIp, newMask));
  }
  runBenchmarks();
}
