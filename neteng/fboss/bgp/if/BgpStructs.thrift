/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

namespace cpp2 facebook.nettools.bgplib
namespace go nettools.bgplib.if.BgpStructs
namespace py nettools.bgplib.BgpStructs
namespace py3 nettools.bgplib
namespace py.asyncio nettools.bgplib_asyncio.BgpStructs
namespace java.swift com.facebook.swift.nettools.bgplib

include "common/network/if/Address.thrift"
include "thrift/annotation/hack.thrift"

@hack.NamePrefix{prefix = "BgpStructs_"}
@hack.LegacyOmitPrefixInNameString
package "facebook.com/nettools/bgplib"

namespace hack ""

exception InvalidAddress {
  1: string prefix;
}

enum BgpUpdateType {
  BU_UPDATE = 1,
  BU_WITHDRAW = 2,
  BU_ENDOFRIB = 3,
}

enum BgpUpdateAfi {
  AFI_IPv4 = 1,
  AFI_IPv6 = 2,
  AFI_LS = 16388,
}

enum BgpUpdateSafi {
  SAFI_UNICAST = 1,
  SAFI_LABELED_UNICAST = 4,
  SAFI_LS = 71,
}

enum BgpAttrOrigin {
  BGP_ORIGIN_IGP = 0,
  BGP_ORIGIN_EGP = 1,
  BGP_ORIGIN_INCOMPLETE = 2,
}

enum BgpNotifErrCode {
  BN_MSG_HDR_ERR = 1,
  BN_OPEN_MSG_ERR = 2,
  BN_UPDATE_MSG_ERR = 3,
  BN_HOLD_TIMER_EXPIRED = 4,
  BN_FSM_ERROR = 5,
  BN_CEASE = 6,
  /* RFC 7313 */
  BN_ROUTE_REFRESH_MSG_ERR = 7,
}

enum BgpNotifMsgHdrErrSubCode {
  BN_MH_CONNECTION_NOT_SYNCHRONIZED = 1,
  BN_MH_BAD_MSG_LEN = 2,
  BN_MH_BAD_MSG_TYPE = 3,
}

enum BgpNotifOpenMsgErrSubCode {
  BN_OM_UNSPECIFIC = 0,
  BN_OM_UNSUPPORTED_VERSION_NUMBER = 1,
  BN_OM_BAD_PEER_AS = 2,
  BN_OM_BAG_BGP_IDENTIFIER = 3,
  BN_OM_UNSUPPORTED_OPTIONAL_PARAM = 4,
  BN_OM_AUTHENTICATION_FAILURE_DEPRECATED = 5,
  BN_OM_UNACCEPTABLE_HOLD_TIME = 6,
  BN_OM_UNSUPPORTED_CAPABILITY = 7,
}

enum BgpNotifUpdateMsgErrSubCode {
  BN_UM_UNSPECIFIC = 0,
  BN_UM_MALFORMED_ATTRIBUTE_LIST = 1,
  BN_UM_UNRECOGNIZED_WELL_KNOWN_ATTR = 2,
  BN_UM_MISSING_WELL_KNOWN_ATTR = 3,
  BN_UM_ATTR_FLAGS_ERR = 4,
  BN_UM_ATTR_LEN_ERR = 5,
  BN_UM_INVALID_ORIGIN_ATTR = 6,
  BN_UM_DEPRECATED = 7,
  BN_UM_INVALID_NEXT_HOP_ATTR = 8,
  BN_UM_OPTIONAL_ATTR_ERROR = 9,
  BN_UM_INVALID_NETWORK_FIELD = 10,
  BN_UM_MALFORMED_AS_PATH = 11,
}

enum BgpNotifCeaseErrSubCode {
  /* RFC 4486 */
  BN_CEASE_MAX_PREFIX_LIMIT = 1,
  BN_CEASE_ADMIN_SHUTDOWN = 2,
  BN_CEASE_PEER_DE_CONFIGURED = 3,
  BN_CEASE_ADMIN_RESET = 4,
  BN_CEASE_CONN_REJECTED = 5,
  BN_CEASE_CONFIG_CHANGE = 6,
  BN_CEASE_CONN_COLLISION_RES = 7,
  BN_CEASE_OUT_OF_RESOURCES = 8,
}

enum BgpNotificationRouteRefreshErrSubCode {
  /* RFC 7313 */
  BN_INVALID_MSG_LEN = 1,
}

enum BgpOpenMsgParam {
  BO_PARAM_CAPABILITIES = 2,
}

enum BgpCapability {
  CAPA_MP_EXT = 1, // RFC 2858
  CAPA_ROUTE_REFRESH = 2, // RFC 2918
  CAPA_EXT_NH_ENCODING = 5, // RFC 5549
  CAPA_GR = 64, // RFC 4724 (Graceful Restart)
  CAPA_AS4_BYTE = 65, // RFC 6793
  CAPA_ADD_PATH = 69, // RFC 7911
  CAPA_ENHANCED_ROUTE_REFRESH = 70, // RFC 7313
}

//
// BGP Route Refresh message Subtypes
// RFC 7313
//
enum BgpRouteRefreshMessageSubtype {
  ROUTE_REFRESH_REQUEST = 0,
  BEGINNING_OF_ROUTE_REFRESH = 1,
  END_OF_ROUTE_REFRESH = 2,
}

struct BgpAttrAsPathSegment {
  // Only one of the two will have elements in one path segment
  1: set<i64> asSet; // unsigned 32 bit integer
  2: list<i64> asSequence; // unsigned 32 bit integer
  3: list<i64> asConfedSequence; // unsigned 32 bit integer
  4: set<i64> asConfedSet; // unsigned 32 bit integer
}

struct BgpAttrAggregator {
  1: i64 asn; // unsigned 32 bit integer
  2: string ip;
}

struct BgpAttrCommunity {
  1: i32 asn = 0; // unsigned 16 bit integer
  2: i32 value = 0; // unsigned 16 bit integer
}

struct BgpAttrExtCommunity {
  1: i64 firstWord; // First 4 bytes (uint32_t)
  2: i64 secondWord; // Next/Last 4 bytes (uint32_t)
}

struct BgpAttrLargeCommunity {
  1: i64 asn; // 4 bytes unsigned int
  2: i64 localData1; // 4 bytes unsigned int
  3: i64 localData2; /// 4 bytes unsigned int
}

struct BgpPathNonStandardData {
  1: optional i64 nhWeight;
  2: bool installToFib;
}

// Following is the list of all BgpAttributes. Their thrift-id matches with
// their type code in BGP-4 protocol. MP_REACH_NLRI and MP_UNREACH_NLRI are
// exception to this list of attributes
struct BgpAttributes {
  // ORIGIN
  1: BgpAttrOrigin origin = 0;
  // AS_PATH
  2: list<BgpAttrAsPathSegment> asPath;
  // NEXT_HOP
  3: string nexthop = "";
  // MULTI_EXIT_DESCRIMINATOR
  4: i64 med = 0; // Unsigned 32 bit integer
  // LOCAL_PREF
  // optional is to distinguish localPref not set VS localPref = 0
  5: optional i64 localPref; // unsigned 32 bit integer
  // ATOMIC_AGGREGATE
  6: bool atomicAggregate = 0; // Default false
  // AGGREGATOR
  7: BgpAttrAggregator aggregator;
  // COMMUNITIES
  8: list<BgpAttrCommunity> communities;
  // ORIGINATOR_ID
  9: i64 originatorId = 0; // unsigned 32 bit integer (NOB)
  // CLUSTER_LIST
  10: list<i64> clusterList; // unsigned 32 bit integers (NOB)
  // EXTENDED_COMMUNITIES
  16: list<BgpAttrExtCommunity> extCommunities;
  // DEPRECATED
  // 17: list<i16> aggregateBitmap; // DEPRECATED
  18: list<BgpAttrLargeCommunity> largeCommunities;
  19: optional BgpPathNonStandardData pathNonStandardData;
  // keys to this map are the field names of an encoding
  // scheme defined in nsf_policy.thrift
  20: optional map<string, i64> topologyInfo;
  // Flag to indicate if med is set in bgp attributes
  21: bool isMedSet = false;
  // Local Weight (Will be only used locally for best path selection)
  22: i32 weight;
}

// BGP Graceful Restart capability attributes
struct BgpGrCapability {
  1: BgpUpdateAfi afi;
  2: BgpUpdateSafi safi;
  3: bool forwardingState = 0;
}

enum BgpAddPathSendRec {
  RECEIVE = 1,
  SEND = 2,
  BOTH = 3,
}

struct BgpAddPathCapability {
  1: BgpUpdateAfi afi;
  2: BgpUpdateSafi safi;
  3: BgpAddPathSendRec sor;
}

// BGP Extended Next Hop Encoding Capability. Sec 4 in RFC 5549
struct BgpExtNHEncodingCapability {
  1: BgpUpdateAfi nlriAfi;
  2: BgpUpdateSafi nlriSafi;
  3: BgpUpdateAfi nhAfi;
}

// BGP Capabilities. Supported capabilities are
// 1. MP Extension
// 2. 4-octet ASN
struct BgpCapabilities {
  1: bool mpExtV4Unicast = 0; // MP Extension v4 unicast, RFC 2858
  2: bool mpExtV6Unicast = 0; // MP Extension v6 unicast, RFC 2858
  3: bool mpExtV4LU = 0; // MP Extension v4 Labelled Unicast
  4: bool mpExtV6LU = 0; // MP Extension v6 Labelled Unicast

  5: bool as4byte = 0; // Support for 4-octet AS number, AS4_PATH,
  // AS4_AGGREGATOR, RFC 6793
  6: i64 asn = 0; // Variable to hold 4-octet ASN if as4byte is set

  7: bool gracefulRestart = 0; // BGP Graceful Restart, RFC 4724
  8: bool isRestarting = 0;
  9: i16 restartTime = 0;
  10: list<BgpGrCapability> grCapabilities;
  11: bool mpExtLs = 0; // MP Extension BGP LS, RFC 7752
  12: bool mpExtExist = 0; // MP Extension exist
  13: list<BgpAddPathCapability> addPathCapabilities;
  14: list<BgpExtNHEncodingCapability> extNHEncodingCapabilities; // RFC 5549
  15: bool enhancedRouteRefresh; // RFC 7313
  16: bool routeRefresh; // RFC 2918
  17: bool grNotification; // GR Notification (N) bit, RFC 8538
}

// Structure to hold the parsed BgpUpdate
struct BgpUpdate {
  1: BgpUpdateType type;
  2: BgpUpdateAfi afi; // Type of prefix (v4 or v6)
  3: BgpUpdateSafi safi;
  4: string prefix = "";
  5: string peerIp = ""; // Router Ip (BGP Speaker's ip)
  6: BgpAttributes attrs; // Bgp attributes
  7: list<i32> labels; // Label stack associated with route
}

//
// The following defines new BgpUpdate format that allows
// for better structured IP prefix data and better information
// packing
//

// IPPrefix possibly with MPLS label stack
// TODO: Think of a better name?
struct RiggedIPPrefix {
  1: Address.IPPrefix prefix;
  // in labeled unicast, labels are part of prefix...
  2: list<i32> labels;
  3: optional i32 pathId;
}

// BGP NLRI information. Labels may be empty. The prefixes
// must be of the same AFI. The nexthop must be in same AFI
// as the prefixes. It could be empty for withdraw messages.
// NOTE: this part could be extracted either from UPDATE
// message's advertised IPv4 routes (classical NLRI) or
// from the MP_REACH_NLRI attribute.
struct BgpNlri {
  1: BgpUpdateAfi afi;
  2: BgpUpdateSafi safi;
  3: list<RiggedIPPrefix> prefixes;
  // this is either BGP NEXT_HOP or NEXT_HOP from the
  // MP_REACH_NLRI attribute. Empty for withdrawn routes.
  4: Address.BinaryAddress nexthop;
}

/*
 * Variant of BgpUpdate that holds multiple prefixes.
 *
 * Notice that we generate separate BgpNlri for each AFI/SAFI. We store them as
 * a vector, since in any case we need to process them all(there is no selective
 * processing).
 */
struct BgpUpdate2 {
  /*
   * All BGP attributes except NEXT_HOP and MP_REACH/UNREACH
   * NEXT_HOP is stored in v4Nexthop
   */
  2: BgpAttributes attrs;

  /*
   * There could be multiple NLRIs in one UPDATE message.
   * For example - v4 and v6 MP_REACH_NLRI are possible.
   * Or combination of classic v4 + MP v6
   */
  3: BgpNlri mpAnnounced;

  /*
   * NOTE: nexthop is not used for withdrawn, but we use afi/safi
   */
  4: BgpNlri mpWithdrawn;

  /*
   * TODO: legacy version of v4 announcement and withdrawn.
   * Retire the thrift field once all clients moves to new data structures.
   */
  5: list<Address.IPPrefix> v4Announced;
  6: list<Address.IPPrefix> v4Withdrawn;
  // The next-hop to use with v4Announced
  7: Address.BinaryAddress v4Nexthop;

  /*
   * New version of v4 announced/withdrawn used by BGP daemon.
   */
  10: list<RiggedIPPrefix> v4Announced2;
  11: list<RiggedIPPrefix> v4Withdrawn2;
}

// Indicates end of RIB for given AFI/SAFI
struct BgpEndOfRib {
  // Is this an MP_ EoR or Classical EoR
  1: bool isMpEor;
  // The following are used when isMpEor == true
  2: BgpUpdateAfi afi;
  3: BgpUpdateSafi safi;
}

struct Update2EorWrapper {
  1: optional BgpUpdate2 update2;
  2: optional BgpEndOfRib eor;
}
//
// End of BgpUpdate2 section
//

// Structure to hold the parsed BgpOpenMsg
struct BgpOpenMsg {
  // field 1: has been removed
  2: byte version; // BGP version number
  3: i64 asn; // Autonomous System number (16/32 bit unsigned integer)
  4: i32 holdTime; // 16 bit unsigned integer
  5: i64 bgpID; // 32 bit unsigned integer, BGP Identifier
  6: BgpCapabilities capabilities;
}

// Structure to hold the parsed BgpNotification
struct BgpNotification {
  // field 1: has been removed
  2: BgpNotifErrCode errCode;
  3: i16 errSubCode; // Unsigned 8 bit integer
  // String representation of error subcode in context of errCode
  4: string errSubCodeStr;
  5: string data; // binary data associated with BgpNotification
}

// Structure to hold parsed BgpKeepAlive message
struct BgpKeepAlive {
  // dummy placeholder, always empty
  1: string _empty;
}

// Structure to hold parsed BgpRouteRefresh message
// RFC 7313(https://datatracker.ietf.org/doc/html/rfc7313#section-3.2)
struct BgpRouteRefresh {
  1: BgpUpdateAfi afi;
  2: BgpRouteRefreshMessageSubtype msgSubType;
  3: BgpUpdateSafi safi;
}

// Label-Switched Path
struct LspRoute {
  1: string prefix;
  2: string nexthop;
  3: list<i32> labels;
}
