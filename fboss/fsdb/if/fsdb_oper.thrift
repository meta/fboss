package "facebook.com/fboss/fsdb"

namespace py3 neteng.fboss
namespace py neteng.fboss.fsdb_oper
namespace py.asyncio neteng.fboss.asyncio.fsdb_oper
namespace cpp2 facebook.fboss.fsdb
namespace go facebook.fboss.fsdb_oper

include "fboss/fsdb/if/fsdb_common.thrift"
include "fboss/thrift_cow/patch.thrift"
include "thrift/annotation/cpp.thrift"

@cpp.Type{name = "::folly::fbstring"}
typedef binary fbbinary

/*
 * Generic types for interacting w/ OperState
 */

union OperPathElem {
  1: bool any;
  2: string regex;
  3: string raw;
}

// TODO: replace w/ RawOperPath
struct OperPath {
  1: list<string> raw;
}

struct RawOperPath {
  1: list<string> path;
}

struct ExtendedOperPath {
  1: list<OperPathElem> path;
}

union Path {
  // TODO: remove in favor of rawPath
  1: OperPath operPath;
  2: RawOperPath rawPath;
  3: list<ExtendedOperPath> extendedPaths;
  4: map<SubscriptionKey, RawOperPath> rawPaths;
}

enum OperProtocol {
  BINARY = 1,
  SIMPLE_JSON = 2,
  COMPACT = 3,
}

// Client-specified alias identifying a subscription path (used as a map key).
typedef i32 SubscriptionKey

// Revision token round-tripped via addPatchSubscriptionPaths so a subscriber can
// correlate the patches it receives with the add-paths request that took effect.
typedef i64 StreamRevision

struct OperMetadata {
  // lastConfirmedAt measured in seconds since epoch
  1: optional i64 lastConfirmedAt;
  // timestamp in msec since epoch when publisher enqueued last update
  2: optional i64 lastPublishedAt;
  // timestamp in msec since epoch when this update was served to subscriber
  3: optional i64 lastServedAt;
  // Echoed back from the most recent addPatchSubscriptionPaths request (its
  // lastStreamRevision), set after a live add so a subscriber knows it took
  // effect.
  4: optional StreamRevision streamRevision;
}

struct OperState {
  1: optional fbbinary contents;
  2: OperProtocol protocol;
  3: optional OperMetadata metadata;
  4: bool isHeartbeat = false;
}

struct TaggedOperState {
  1: RawOperPath path;
  2: OperState state;
}

struct OperDeltaUnit {
  1: OperPath path;
  2: optional fbbinary oldState;
  3: optional fbbinary newState;
}

struct OperDelta {
  1: list<OperDeltaUnit> changes;
  2: OperProtocol protocol;
  3: optional OperMetadata metadata;
}

struct TaggedOperDelta {
  1: RawOperPath path;
  2: OperDelta delta;
}

struct OperPubRequest {
  1: OperPath path;
  2: fsdb_common.PublisherId publisherId;
}

struct OperPubInitResponse {}

struct OperPubFinalResponse {}

struct OperSubRequest {
  1: OperPath path;
  2: OperProtocol protocol = OperProtocol.BINARY;
  3: fsdb_common.SubscriberId subscriberId;
  // Forcefully subscribe even if there is already a subscriber with the same SubscriberId
  4: bool forceSubscribe = false;
  5: optional i64 heartbeatInterval;
  // Requested serve cadence in seconds. Rounded up to a whole serve tick and
  // clamped to the storage's default interval; absent means the default.
  6: optional i32 serveIntervalSec;
}

struct OperSubInitResponse {
  // Server-assigned unique id for this subscription, echoed back in
  // AddPatchSubscriptionPathsRequest to target it. Optional for backward
  // compatibility with older servers that do not populate it.
  1: optional i64 subscriptionUid;
}

// types to support extended subscription api
struct OperSubRequestExtended {
  1: list<ExtendedOperPath> paths;
  2: OperProtocol protocol = OperProtocol.BINARY;
  3: fsdb_common.SubscriberId subscriberId;
  // Forcefully subscribe even if there is already a subscriber with the same SubscriberId
  4: bool forceSubscribe = false;
  5: optional i64 heartbeatInterval;
  // Requested serve cadence in seconds. Rounded up to a whole serve tick and
  // clamped to the storage's default interval; absent means the default.
  6: optional i32 serveIntervalSec;
}

struct OperSubPathUnit {
  1: list<TaggedOperState> changes;
}

struct OperSubDeltaUnit {
  1: list<TaggedOperDelta> changes;
}

enum PubSubType {
  PATH = 0,
  DELTA = 1,
  PATCH = 2,
}

struct PubRequest {
  1: RawOperPath path;
  2: fsdb_common.ClientId clientId;
}

struct SubRequest {
  1: map<SubscriptionKey, RawOperPath> paths;
  3: fsdb_common.ClientId clientId;
  // Forcefully subscribe even if there is already a subscriber with the same SubscriberId
  4: bool forceSubscribe = false;
  5: optional i64 heartbeatInterval;
  6: map<SubscriptionKey, ExtendedOperPath> extPaths;
  // Requested serve cadence in seconds. Rounded up to a whole serve tick and
  // clamped to the storage's default interval; absent means the default.
  7: optional i32 serveIntervalSec;
}

// Request to append paths to an already-created patch subscription, located by
// subscriptionUid alone (a non-zero uid is globally unique). clientId carries
// the requester's identity but is not used to locate the subscription.
struct AddPatchSubscriptionPathsRequest {
  1: fsdb_common.ClientId clientId;
  2: i64 subscriptionUid;
  3: map<SubscriptionKey, RawOperPath> paths;
  // Extended (wildcard/regex) paths. Mutually exclusive with field 3 (paths):
  // set exactly one of paths or extPaths per request.
  4: map<SubscriptionKey, ExtendedOperPath> extPaths;
  // Last stream revision the client has observed. The server stashes it and
  // echoes it back as OperMetadata.streamRevision on subsequent patches.
  5: StreamRevision lastStreamRevision;
}

struct AddPatchSubscriptionPathsResponse {}

struct Patch {
  1: list<string> basePath;
  2: patch.PatchNode patch;
  3: OperMetadata metadata;
  4: OperProtocol protocol = OperProtocol.COMPACT;
}

struct Heartbeat {
  1: optional OperMetadata metadata;
}

union PublisherMessage {
  1: Patch patch;
  2: Heartbeat heartbeat;
}

struct SubscriberChunk {
  // SubscriptionKey is specified by the client, it is an alias to the path
  // For each path we have a list of patches because of the existence of wildcard
  // paths. For non wildcard subs, there will always be only one patch per key
  1: map<SubscriptionKey, list<Patch>> patchGroups;
}

union SubscriberMessage {
  1: SubscriberChunk chunk;
  2: Heartbeat heartbeat;
}
