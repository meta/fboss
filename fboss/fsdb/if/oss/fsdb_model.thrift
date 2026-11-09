namespace py3 neteng.fboss
namespace py neteng.fboss.fsdb_model
namespace cpp2 facebook.fboss.fsdb
namespace go facebook.fboss.fsdb

include "fboss/agent/agent_config.thrift"
include "fboss/agent/agent_info.thrift"
include "fboss/agent/agent_stats.thrift"
include "fboss/agent/switch_state.thrift"
include "fboss/agent/switch_reachability.thrift"
include "fboss/qsfp_service/if/qsfp_state.thrift"
include "fboss/qsfp_service/if/qsfp_stats.thrift"
include "fboss/fsdb/if/fsdb_common.thrift"
include "fboss/fsdb/if/te_srv6_agent/te_srv6_agent_stats.thrift"
include "fboss/lib/phy/phy.thrift"
include "fboss/platform/sensor_service/sensor_service_stats.thrift"
include "neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/bgp_config.thrift"
include "configerator/structs/neteng/bgp_policy/thrift/rib_policy.thrift"
include "neteng/fboss/bgp/if/bgp_route_types.thrift"
include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

// mirrors of structs fboss/fsdb/if/facebook/fsdb_model.thrift with oss only portions
struct AgentData {
  1: agent_config.AgentConfig config;
  2: switch_state.SwitchState switchState;
  4: map<string, fsdb_common.FsdbSubscriptionState> fsdbSubscriptions = {};
  5: map<
    i64,
    switch_reachability.SwitchReachability
  > dsfSwitchReachability = {};
  6: optional agent_info.AgentInfo agentInfo;
  // ASIC side (iphy) PHY state, keyed by port name. Counterpart to the
  // external PHY state qsfp_service publishes in QsfpState.phyStates.
  7: map<string, phy.PhyState> iPhyStates = {};
}

struct BgpData {
  1: bgp_config.BgpConfig config;
  2: optional rib_policy.TRouteAttributePolicy routeAttributePolicy;
  3: optional rib_policy.TPathSelectionPolicy pathSelectionPolicy;
  4: optional rib_policy.TRouteFilterPolicy routeFilterPolicy;
  // key is str() of folly::CIDRNetwork, which matches the prefix in TRibEntry exactly
  5: optional map<string, bgp_route_types.TRibEntry> ribMap;
  6: optional bgp_route_types.TPartialDrainState partialDrainState;
  /*
   * Compact, deduplicated form of BGP RIB entries (prefix -> paths). See
   * bgp_route_types.TCanonicalRibState for the encoding.
   */
  7: optional bgp_route_types.TCanonicalRibState canonicalRib;
}

@thrift.DeprecatedUnvalidatedAnnotations{items = {"thriftpath.root": "1"}}
struct FsdbOperStateRoot {
  1: AgentData agent;
  2: BgpData bgp;
  4: qsfp_state.QsfpServiceData qsfp_service;
}

@thrift.DeprecatedUnvalidatedAnnotations{items = {"thriftpath.root": "1"}}
struct FsdbOperStatsRoot {
  1: agent_stats.AgentStats agent;
  3: qsfp_stats.QsfpStats qsfp_service;
  4: sensor_service_stats.SensorServiceStats sensor_service;
  5: te_srv6_agent_stats.TeSrv6AgentStats te_srv6_agent;
}
