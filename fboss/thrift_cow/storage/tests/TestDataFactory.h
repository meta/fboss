// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <folly/IPAddress.h>
#include <map>
#include <optional>
#include <string>
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/fsdb/if/FsdbModel.h"
#include "fboss/fsdb/tests/gen-cpp2-thriftpath/thriftpath_test.h" // @manual=//fboss/fsdb/tests:thriftpath_test_thrift-cpp2-thriftpath
#include "fboss/thrift_cow/storage/tests/SwitchStateBuilders.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_route_types_types.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace {
constexpr int kDefaultMapSize = 1 * 1000;
}

namespace facebook::fboss::test_data {

using facebook::fboss::fsdb::OperProtocol;
using facebook::fboss::fsdb::OperState;
using facebook::fboss::fsdb::OtherStruct;
using facebook::fboss::fsdb::TaggedOperState;
using facebook::fboss::fsdb::TestEnum;
using facebook::fboss::fsdb::TestStruct;

// Import switch state types for FIB data
using facebook::fboss::AdminDistance;
using facebook::fboss::ClientID;
using facebook::fboss::NextHopThrift;
using facebook::fboss::RouteForwardAction;
using facebook::fboss::state::FibContainerFields;
using facebook::fboss::state::RouteFields;
using facebook::fboss::state::RouteNextHopEntry;
using facebook::fboss::state::RouteNextHopsMulti;
using facebook::fboss::state::RoutePrefix;
using facebook::fboss::state::SwitchState;
using facebook::network::thrift::BinaryAddress;

// Import AgentStats and related types
using facebook::fboss::AgentStats;
using facebook::fboss::HwPortStats;
using facebook::fboss::HwSysPortStats;
using facebook::fboss::IOStats;
using facebook::fboss::phy::LaneStats;
using facebook::fboss::phy::PcsStats;
using facebook::fboss::phy::PhySideStats;
using facebook::fboss::phy::PhyStats;
using facebook::fboss::phy::PmdStats;
using facebook::fboss::phy::RsFecInfo;
using facebook::fboss::phy::Side;

// Import BGP types
using TRibEntry = neteng::fboss::bgp::thrift::TRibEntry;

// Import SwitchStateScale from SwitchStateBuilders
using facebook::fboss::fsdb::test::SwitchStateScale;

enum RoleSelector {
  Minimal = 0,
  MaxScale = 1,
  RTSW = 2,
  FTSW = 3,
  STSW = 4,
  RSW = 5,
  FSW = 6,
  SSW = 7,
  XSW = 8,
  MA = 9,
  FA = 10,
  RDSW = 11,
  FDSW = 12,
  SDSW = 13,
  EDSW = 14,
  RUSW = 15,
  RGSW = 16,
  GTSW = 17,
};

// RUSW and GTSW are absent deliberately: neither has an entry in
// FsdbStatsDataFactory::getRoleScale, which falls back to the Minimal (1,1,1)
// scale instead of failing, so accepting them by name would report a
// single-port measurement as if it were that role's.
inline std::optional<RoleSelector> statsRoleFromString(
    const std::string& name) {
  static const std::map<std::string, RoleSelector> kRoles{
      {"Minimal", Minimal},
      {"MaxScale", MaxScale},
      {"RTSW", RTSW},
      {"FTSW", FTSW},
      {"STSW", STSW},
      {"RSW", RSW},
      {"FSW", FSW},
      {"SSW", SSW},
      {"XSW", XSW},
      {"MA", MA},
      {"FA", FA},
      {"RDSW", RDSW},
      {"FDSW", FDSW},
      {"SDSW", SDSW},
      {"EDSW", EDSW},
      {"RGSW", RGSW}};
  auto it = kRoles.find(name);
  return it == kRoles.end() ? std::nullopt
                            : std::optional<RoleSelector>(it->second);
}

struct AgentStatsScale {
  int hwPortStatsCount{0};
  int phyStatsCount{0};
  int sysPortStatsCount{0};

  // NEW fields
  int asicCount{1};
  int sysPortShelStateCount{0};
  int asicTempCount{0};
  bool hasHwResourceStats{true};
  bool hasHwAsicErrors{true};
  bool hasCpuPortStats{true};
  bool hasSwitchDropStats{true};
  bool hasSwitchWatermarkStats{true};
  bool hasFabricReachabilityStats{true};
  bool hasSwitchPipelineStats{true};
  bool hasFabricOverdrainPct{false};
  bool hasFlowletStats{false};
};

struct BgpRibMapScale {
  int ribV4EntryCount;
  int ribV6EntryCount;
  int bestPathsPerEntry;
  int communitiesPerPath;
  int asPathSegments;
  int extCommunitiesPerPath;
  // When isFPF is true the generator emits bgpData.canonicalRib()
  // (best-path-only) instead of ribMap, sized from numPods x numPrefixesPerPod.
  // Defaulted so existing aggregate initializers are unaffected.
  bool isFPF{false};
  int numPods{0};
  int numPrefixesPerPod{0};
};

class IDataGenerator {
 public:
  virtual ~IDataGenerator() = default;

  virtual TaggedOperState getStateUpdate(int version, bool minimal) = 0;

  // Set a path filter to only populate the subtree at the given path.
  // Path tokens should be the FSDB path components, e.g.
  // {"agent", "switchState", "fibsMap"}.
  void setPathFilter(std::vector<std::string> pathTokens) {
    pathFilter_ = std::move(pathTokens);
  }

 protected:
  std::vector<std::string> pathFilter_;
};

class TestDataFactory : public IDataGenerator {
 public:
  using RootT = TestStruct;

  explicit TestDataFactory(
      RoleSelector selector,
      int scaleFactor = kDefaultMapSize)
      : selector_(selector), scaleFactor_(scaleFactor) {}

  TaggedOperState getStateUpdate(int version, bool minimal) override;

 protected:
  OtherStruct
  buildMinimalTestData(int version, int key, std::vector<std::string>& path);

  TestStruct buildTestData(int version, std::vector<std::string>& /* path */);

  RoleSelector selector_;
  int scaleFactor_;
  OperProtocol protocol_{OperProtocol::COMPACT};
};

class FsdbStateDataFactory : public IDataGenerator {
 public:
  using RootT = fsdb::FsdbOperStateRoot;

  explicit FsdbStateDataFactory(RoleSelector selector) : selector_(selector) {}

  TaggedOperState getStateUpdate(int version, bool minimal) override;

 protected:
  fsdb::FsdbOperStateRoot buildFsdbOperStateRoot(int version);
  SwitchState buildSwitchState(int version);
  FibContainerFields buildFibData(int version);
  SwitchStateScale getRoleScale(RoleSelector role);

  RouteFields createRouteFields(
      const std::string& prefix,
      const std::vector<NextHopThrift>& nexthops);
  NextHopThrift createNextHop(
      const std::string& address,
      const std::string& ifName = "eth0",
      int32_t weight = 1);
  std::vector<NextHopThrift> createNextHops(
      int count,
      bool isV6 = false,
      const std::string& baseIf = "eth");
  BinaryAddress createBinaryAddress(const folly::IPAddress& addr);

  // Helper methods for remote system ports and remote interfaces
  void populateRemoteSystemPortsAndInterfaces(SwitchState& switchState);

  RoleSelector selector_;
  OperProtocol protocol_{OperProtocol::COMPACT};
};

class FsdbStatsDataFactory : public IDataGenerator {
 public:
  using RootT = fsdb::FsdbOperStatsRoot;

  explicit FsdbStatsDataFactory(RoleSelector selector) : selector_(selector) {}

  TaggedOperState getStateUpdate(int version, bool minimal) override;

 protected:
  fsdb::FsdbOperStatsRoot buildFsdbOperStatsRoot();
  AgentStats buildAgentStats();
  AgentStatsScale getRoleScale(RoleSelector role);

  // Helper methods to create AgentStats structures
  HwPortStats createHwPortStats(
      const std::string& portName,
      int64_t baseTimestamp,
      int hwPortNum);
  PhyStats createPhyStats(int64_t baseTimestamp, int phyPortNum);
  HwSysPortStats createSysPortStats(
      const std::string& portName,
      int64_t baseTimestamp,
      int sysPortNum);

  // Helper methods for internal structures
  RsFecInfo createRsFecInfo(int portIndex);
  PhySideStats createPhySideStats(Side side, int portIndex);
  PcsStats createPcsStats(int portIndex);
  PmdStats createPmdStats(int portIndex);
  LaneStats createLaneStats(int16_t laneId, int portIndex);
  IOStats createIOStats();

  RoleSelector selector_;
  OperProtocol protocol_{OperProtocol::COMPACT};
};

class BgpRibMapDataGenerator : public IDataGenerator {
 public:
  using RootT = fsdb::FsdbOperStateRoot;

  explicit BgpRibMapDataGenerator(RoleSelector selector)
      : selector_(selector) {}

  // Construct with an explicit BgpRibMapScale that overrides the per-role
  // table. Used by callers that want parameterized scales (e.g. GTSW with
  // configurable prefix count and paths per entry).
  BgpRibMapDataGenerator(RoleSelector selector, BgpRibMapScale overrideScale)
      : selector_(selector), overrideScale_(overrideScale) {}

  TaggedOperState getStateUpdate(int version, bool minimal) override;

  // Builds a GTSW-shaped scale (V6-heavy, 13 communities, 1 AS segment,
  // 0 ext communities) sized by `prefixScale` total prefixes and `paths`
  // best-paths per entry. ~10% V4 / 90% V6 split.
  static BgpRibMapScale makeGtswScale(int prefixScale, int paths);

  // Builds an FPF (GPU fabric) canonicalRib-shaped scale matching a captured
  // GTSW canonicalRib: rib_entries = numPods * numPrefixesPerPod (all V6 /64),
  // 1 community list of 13, numPods 2-ASN as_path lists, 1 ext-community list,
  // no cluster lists.
  static BgpRibMapScale
  makeGtswScale(bool isFPF, int numPods, int numPrefixesPerPod);

 protected:
  fsdb::FsdbOperStateRoot buildFsdbOperStateRoot(int version);
  fsdb::BgpData buildBgpData(int version);
  BgpRibMapScale getScale(RoleSelector role);

  // Helper methods to create BGP structures
  TRibEntry buildTRibEntry(
      const BgpRibMapScale& scale,
      int index,
      bool isV6,
      int version);
  facebook::neteng::fboss::bgp_attr::TIpPrefix
  createPrefix(int index, bool isV6, int keySet);
  neteng::fboss::bgp::thrift::TBgpPath createBgpPath(
      int entryIndex,
      int pathIndex,
      int numCommunities,
      int numAsPathSegments,
      int numExtCommunities,
      int asPathVersion);
  std::string createPrefixKey(
      const facebook::neteng::fboss::bgp_attr::TIpPrefix& prefix);

  // Helpers for the FPF canonicalRib path (scale.isFPF == true).
  neteng::fboss::bgp::thrift::TCanonicalRibState buildCanonicalRib(
      const BgpRibMapScale& scale,
      int version);
  neteng::fboss::bgp::thrift::TBgpAttrDict buildAttrDict(
      const BgpRibMapScale& scale);
  neteng::fboss::bgp::thrift::TBgpDedupedPath
  buildDedupedBestPath(int index, int podIdx, const BgpRibMapScale& scale);
  // Distinct V6 /64 prefix mirroring the DNE injector's expand_prefix_range
  // (base 5000:dd::/64). Distinct for every non-negative index.
  facebook::neteng::fboss::bgp_attr::TIpPrefix createFpfPrefix(int index);

  RoleSelector selector_;
  std::optional<BgpRibMapScale> overrideScale_;
  OperProtocol protocol_{OperProtocol::COMPACT};
};

} // namespace facebook::fboss::test_data
