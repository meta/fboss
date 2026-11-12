/**
 * Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/bcm/BcmCinter.h"

#include <array>
#include <iomanip>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

#include <fmt/core.h>
#include <gflags/gflags.h>

#include <folly/FileUtil.h>
#include <folly/MapUtil.h>
#include <folly/Singleton.h>
#include <folly/String.h>

extern "C" {
#if (defined(IS_OPENNSA))
#define BCM_WARM_BOOT_SUPPORT
#endif
}

DEFINE_string(
    cint_file,
    "/var/facebook/logs/fboss/sdk/bcm_cinter.log",
    "File to dump equivalent cint commands for each wrapped SDK api");

DEFINE_bool(enable_bcm_cinter, false, "Whether to enable bcm cinter");

DEFINE_bool(gen_tx_cint, false, "Generate cint for packet TX calls");

DEFINE_int32(
    cinter_log_timeout,
    100,
    "Log timeout value in milliseconds. Logger will periodically"
    "flush logs even if the buffer is not full");

DEFINE_int32(pbmp_word_max, 8, "Max pbmp width as suggested by Vendor.");

DEFINE_bool(enable_fdr_cinter, false, "Enable logging for fdr set calls.");

using folly::join;
using folly::to;
using std::array;
using std::make_move_iterator;
using std::pair;
using std::string;
using std::tie;
using std::vector;

namespace {

folly::Singleton<facebook::fboss::BcmCinter> _bcmCinter;

template <typename T>
vector<string> makeParamVec(T&& arg) {
  return {to<string>(std::forward<T>(arg))};
}

template <typename T, typename... Args>
vector<string> makeParamVec(T&& first, Args&&... args) {
  vector<string> params = makeParamVec(std::forward<T>(first));
  auto remParams = makeParamVec(std::forward<Args>(args)...);
  params.insert(params.end(), remParams.begin(), remParams.end());
  return params;
}

template <typename... Args>
string makeParamStr(Args&&... args) {
  return join(", ", makeParamVec(std::forward<Args>(args)...));
}

template <typename T>
string getCintVar(
    folly::Synchronized<std::map<T, std::string>>& varMap,
    T key,
    std::optional<std::string> default_val = std::nullopt) {
  return varMap.withRLock([&](auto& vars) {
    return folly::get_default(vars, key, default_val.value_or(to<string>(key)));
  });
}

} // namespace

namespace facebook::fboss {

BcmCinter::BcmCinter() {
  if (FLAGS_enable_bcm_cinter) {
    asyncLogger_ = std::make_unique<AsyncLogger>(
        FLAGS_cint_file, FLAGS_cinter_log_timeout, AsyncLogger::BCM_CINTER);

    asyncLogger_->startFlushThread();
    setupGlobals();
  }
}

BcmCinter::~BcmCinter() {
  if (FLAGS_enable_bcm_cinter) {
    asyncLogger_->forceFlush();
    asyncLogger_->stopFlushThread();
  }
}

std::shared_ptr<BcmCinter> BcmCinter::getInstance() {
  return _bcmCinter.try_get();
}

void BcmCinter::setupGlobals() {
  array<string, 39> globals = {
      "_shr_pbmp_t pbmp",
      "_shr_rx_reasons_t reasons",
      "bcm_cosq_gport_discard_t discard",
      "bcm_field_qset_t qset",
      "bcm_field_aset_t aset",
      "bcm_field_group_config_t group_config",
      "bcm_l3_ecmp_member_t member",
      "bcm_l3_ecmp_member_t member_array[64]",
      "bcm_if_t intf_array[64]",
      "bcm_l2_station_t station",
      "bcm_l3_egress_ecmp_t ecmp",
      "bcm_l3_egress_t l3_egress",
      "bcm_l3_ingress_t l3_ingress",
      "bcm_if_t ing_intf_id",
      "bcm_vlan_control_vlan_t vlan_ctrl",
      "bcm_l3_host_t l3_host",
      "bcm_l3_intf_t l3_intf",
      "bcm_l3_route_t l3_route",
      "bcm_mirror_destination_t mirror_dest",
      "bcm_mpls_egress_label_t label_array[10]",
      "bcm_mpls_tunnel_switch_t switch_info",
      "bcm_pkt_t *pkt",
      "bcm_qos_map_t qos_map_entry",
      "bcm_rx_cfg_t rx_cfg",
      "bcm_rx_cosq_mapping_t cosqMap",
      "bcm_trunk_info_t trunk_info",
      "bcm_trunk_member_t trunk_member",
      "bcm_trunk_t tid",
      "bcm_cosq_bst_profile_t cosq_bst_profile",
      "int num_labels",
      "int rv",
      "int station_id",
      "bcm_port_resource_t resource",
      "bcm_port_resource_t resources[4]",
      "bcm_port_phy_tx_t tx",
      "bcm_field_flexctr_config_t flexctr_config",
      "bcm_flexctr_action_t flexctr_action",
      "bcm_flexctr_trigger_t flexctr_trigger",
      "bcm_field_hint_t hint",
  };
  writeCintLines(std::move(globals));
#ifdef INCLUDE_PKTIO
  array<string, 4> pktioGlobals = {
      "bcm_pktio_pkt_t* pktio_pkt",
      "void *pktio_data",
      "uint8 *pktio_bytes",
      "uint32 pktio_len",
  };
  writeCintLines(std::move(pktioGlobals));
#endif
  array<string, 1> fdrGlobals = {
      "bcm_port_fdr_config_t fdr_config",
  };
  writeCintLines(std::move(fdrGlobals));
}

template <typename C>
void BcmCinter::writeCintLines(C&& lines) {
  auto constexpr kSeparator = ";\n";
  auto numLines = lines.size();
  auto cint = join(kSeparator, std::forward<C>(lines)) + kSeparator;
  if (FLAGS_enable_bcm_cinter) {
    asyncLogger_->appendLog(cint.c_str(), cint.size());
  }
  linesWritten_ += numLines;
}

void BcmCinter::writeCintLine(const std::string& cint) {
  writeCintLines(array<string, 1>{cint});
}

inline std::string BcmCinter::getCurrentTimestamp() const {
  auto now = std::chrono::system_clock::now();
  auto timer = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  localtime_r(&timer, &tm);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %T");
  return oss.str();
}

vector<string> BcmCinter::wrapFunc(const string& funcCall) const {
  vector<string> cintLines = {
      to<string>("// ", getCurrentTimestamp()),
      to<string>("rv = ", funcCall),
      // NOTE this is approximate line number in particular it
      // will point to line number just before the lines written
      // in setting up args and making this function call.
      to<string>(
          "if (rv) { printf(\"\\nError around line ",
          linesWritten_.load(),
          " : ",
          funcCall.substr(0, funcCall.find('(')),
          ": %d -> %s\", rv, bcm_errmsg(rv));  }")};
  return cintLines;
}

vector<string> BcmCinter::cintForQset(const bcm_field_qset_t& qset) const {
  vector<string> cintLines = {"BCM_FIELD_QSET_INIT(qset)"};
  for (auto qual = 0; qual < bcmFieldQualifyCount; ++qual) {
    if (BCM_FIELD_QSET_TEST(qset, qual)) {
      cintLines.emplace_back(
          to<string>("BCM_FIELD_QSET_ADD(qset, ", qual, ")"));
    }
  }
  return cintLines;
}

vector<string> BcmCinter::cintForAset(const bcm_field_aset_t& aset) const {
  vector<string> cintLines = {"BCM_FIELD_ASET_INIT(aset)"};
  for (auto act = 0; act < bcmFieldActionCount; ++act) {
    if (BCM_FIELD_ASET_TEST(aset, act)) {
      cintLines.emplace_back(to<string>("BCM_FIELD_ASET_ADD(aset, ", act, ")"));
    }
  }
  return cintLines;
}

vector<string> BcmCinter::cintForFpGroupConfig(
    const bcm_field_group_config_t* group_config) const {
  vector<string> cintLines = {
      "bcm_field_group_config_t_init(&group_config)",
      to<string>("group_config.flags=", group_config->flags),
      to<string>("group_config.priority=", group_config->priority),
      to<string>("group_config.group=", group_config->group),
      to<string>("group_config.hintid=", group_config->hintid),
      to<string>("group_config.mode=", group_config->mode)};
  auto cintForFn = cintForQset(group_config->qset);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  cintLines.emplace_back(to<string>("group_config.qset=qset"));
  cintForFn = cintForAset(group_config->aset);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  cintLines.emplace_back(to<string>("group_config.aset=aset"));
  return cintLines;
}

vector<string> BcmCinter::cintForCosQDiscard(
    const bcm_cosq_gport_discard_t& discard) const {
  vector<string> cintLines = {
      "bcm_cosq_gport_discard_t_init(&discard)",
      to<string>("discard.flags=", discard.flags),
      to<string>("discard.min_thresh=", discard.min_thresh),
      to<string>("discard.max_thresh=", discard.max_thresh),
      to<string>("discard.drop_probability=", discard.drop_probability)};
  return cintLines;
}

string BcmCinter::getNextTmpV6Var() {
  return to<string>("tmpV6_", ++tmpV6Created_);
}

string BcmCinter::getNextTmpMacVar() {
  return to<string>("tmpMac_", ++tmpMacCreated_);
}

string BcmCinter::getNextTmpStatArray() {
  return to<string>("tmpStatArray_", ++tmpStatArrayCreated_);
}

string BcmCinter::getNextTmpTrunkMemberArray() {
  return to<string>("tmpTrunkMemberArray_", ++tmpTrunkMemberArrayCreated_);
}

string BcmCinter::getNextTmpPortBitmapVar() {
  return to<string>("tmpPbmp_", ++tmpPortBitmapCreated_);
}

string BcmCinter::getNextTmpReasonsVar() {
  return to<string>("tmpReasons_", ++tmpReasonsCreated_);
}

string BcmCinter::getNextFieldEntryVar() {
  return to<string>("fieldEntry_", ++fieldEntryCreated_);
}

string BcmCinter::getNextQosMapVar() {
  return to<string>("qosMap_", ++qosMapCreated_);
}

string BcmCinter::getNextL3IntfIdVar() {
  return to<string>("l3Intf_", ++l3IntfCreated_);
}

string BcmCinter::getNextStatVar() {
  return to<string>("stat_", ++statCreated_);
}

string BcmCinter::getNextPriorityToPgVar() {
  return to<string>("priorityToPg_", ++tmpProrityToPgCreated_);
}

string BcmCinter::getNextPfcPriToPgVar() {
  return to<string>("pfcPriToPg_", ++tmpPfcPriToPgCreated_);
}

string BcmCinter::getNextEthertypeVar() {
  return to<string>("etherType_", ++tmpEthertype_);
}

string BcmCinter::getNextPfcPriToQueueVar() {
  return to<string>("pfcPriToQueue_", ++tmpPfcPriToQueueCreated_);
}

string BcmCinter::getNextPgConfigVar() {
  return to<string>("pgConfig_", ++tmpPgConfigCreated_);
}

string BcmCinter::getNextPptConfigVar() {
  return to<string>("pptConfig_", ++tmpPptConfigCreated_);
}

string BcmCinter::getNextPtConfigVar() {
  return to<string>("ptConfig_", ++tmpPtConfigCreated_);
}

string BcmCinter::getNextPtConfigArrayVar() {
  return to<string>("ptConfigArray_", ++tmpPtConfigArrayCreated_);
}

string BcmCinter::getNextMirrorDestIdVar() {
  return to<string>("mirrorDestId_", ++mirrorDestIdCreated_);
}

string BcmCinter::getNextRangeVar() {
  return to<string>("range_", ++rangeCreated_);
}

string BcmCinter::getNextStgVar() {
  return to<string>("stg_", ++stgCreated_);
}

string BcmCinter::getNextTimeInterfaceVar() {
  return to<string>("timeInterface_", ++tmpTimeInterfaceCreated_);
}

string BcmCinter::getNextTimeSpecVar() {
  return to<string>("timeSpec_", ++tmpTimeSpecCreated_);
}

string BcmCinter::getNextStateCounterVar() {
  return to<string>("stateCounter_", ++tmpStateCounterCreated_);
}

pair<string, string> BcmCinter::cintForIp6(const bcm_ip6_t in) {
  array<string, 16> bytes;
  for (auto i = 0; i < 16; ++i) {
    bytes[i] = to<string>(in[i]);
  }
  auto v6VarName = getNextTmpV6Var();
  return make_pair(
      to<string>("bcm_ip6_t ", v6VarName, " = ", " {", join(", ", bytes), "}"),
      v6VarName);
}

pair<string, string> BcmCinter::cintForMac(const bcm_mac_t in) {
  array<string, 6> bytes;
  for (auto i = 0; i < 6; ++i) {
    bytes[i] = to<string>(in[i]);
  }
  auto macVarName = getNextTmpMacVar();
  return make_pair(
      to<string>("bcm_mac_t ", macVarName, " = ", " {", join(", ", bytes), "}"),
      macVarName);
}

pair<string, string> BcmCinter::cintForStatArray(
    const bcm_field_stat_t* statArray,
    int statSize) {
  std::vector<string> stats;
  for (int i = 0; i < statSize; ++i) {
    stats.push_back(to<string>(statArray[i]));
  }
  auto statArrayName = getNextTmpStatArray();
  return make_pair(
      to<string>(
          "bcm_field_stat_t ",
          statArrayName,
          "[] =",
          " {",
          join(", ", stats),
          "}"),
      statArrayName);
}

vector<string> BcmCinter::cintForL3Ecmp(const bcm_l3_egress_ecmp_t& ecmp) {
  vector<string> cintLines = {
      "bcm_l3_egress_ecmp_t_init(&ecmp)",
      to<string>("ecmp.flags=", ecmp.flags),
      to<string>("ecmp.max_paths=", ecmp.max_paths),
      to<string>("ecmp.ecmp_intf=", getCintVar(l3IntfIdVars, ecmp.ecmp_intf)),
      to<string>("ecmp.ecmp_group_flags=", ecmp.ecmp_group_flags),
      to<string>("ecmp.dynamic_mode=", ecmp.dynamic_mode),
      to<string>("ecmp.dynamic_size=", ecmp.dynamic_size),
      to<string>("ecmp.dynamic_age=", ecmp.dynamic_age)};

  return cintLines;
}

vector<string> BcmCinter::cintForL3Route(const bcm_l3_route_t& l3_route) {
  vector<string> cintLines;

  if (l3_route.l3a_flags & BCM_L3_IP6) {
    string cintV6, v6Var, cintMask, v6Mask;
    tie(cintV6, v6Var) = cintForIp6(l3_route.l3a_ip6_net);
    tie(cintMask, v6Mask) = cintForIp6(l3_route.l3a_ip6_mask);
    cintLines = {
        cintV6,
        cintMask,
        "bcm_l3_route_t_init(&l3_route)",
        to<string>("l3_route.l3a_vrf = ", l3_route.l3a_vrf),
        to<string>(
            "l3_route.l3a_intf = ",
            getCintVar(l3IntfIdVars, l3_route.l3a_intf)),
        to<string>("l3_route.l3a_ip6_net = ", v6Var),
        to<string>("l3_route.l3a_ip6_mask = ", v6Mask),
        to<string>("l3_route.l3a_lookup_class = ", l3_route.l3a_lookup_class),
        to<string>("l3_route.l3a_flags = ", l3_route.l3a_flags)};

  } else {
    cintLines = {
        "bcm_l3_route_t_init(&l3_route)",
        to<string>("l3_route.l3a_vrf = ", l3_route.l3a_vrf),
        to<string>(
            "l3_route.l3a_intf = ",
            getCintVar(l3IntfIdVars, l3_route.l3a_intf)),
        to<string>("l3_route.l3a_subnet = ", l3_route.l3a_subnet),
        to<string>("l3_route.l3a_ip_mask = ", l3_route.l3a_ip_mask),
        to<string>("l3_route.l3a_lookup_class = ", l3_route.l3a_lookup_class),
        to<string>("l3_route.l3a_flags = ", l3_route.l3a_flags)};
  }
  return cintLines;
}

vector<string> BcmCinter::cintForL3Intf(const bcm_l3_intf_t& l3_intf) {
  vector<string> cintLines;
  string cintMac, macVar;
  tie(cintMac, macVar) = cintForMac(l3_intf.l3a_mac_addr);
  cintLines = {
      cintMac,
      "bcm_l3_intf_t_init(&l3_intf)",
      to<string>("l3_intf.l3a_vrf = ", l3_intf.l3a_vrf),
      to<string>("l3_intf.l3a_vid = ", l3_intf.l3a_vid),
      to<string>("l3_intf.l3a_mtu = ", l3_intf.l3a_mtu),
      to<string>("l3_intf.l3a_mac_addr = ", macVar),
      to<string>("l3_intf.l3a_flags = ", l3_intf.l3a_flags)};
  if (l3_intf.l3a_flags & BCM_L3_WITH_ID) {
    cintLines.push_back(
        to<string>("l3_intf.l3a_intf_id = ", l3_intf.l3a_intf_id));
  }
  return cintLines;
}

vector<string> BcmCinter::cintForMirrorDestination(
    const bcm_mirror_destination_t* mirror_dest) {
  vector<string> cintLines{
      to<string>("bcm_mirror_destination_t_init(&mirror_dest)"),
      to<string>("mirror_dest.gport = ", mirror_dest->gport),
      to<string>("mirror_dest.flags = ", mirror_dest->flags),
      to<string>("mirror_dest.tos = ", mirror_dest->tos),
  };
  if (!(mirror_dest->flags & BCM_MIRROR_DEST_TUNNEL_IP_GRE) &&
      !(mirror_dest->flags & BCM_MIRROR_DEST_TUNNEL_SFLOW)) {
    return cintLines;
  }
  cintLines.push_back(
      to<string>("mirror_dest.version = ", mirror_dest->version));
  cintLines.push_back(
      to<string>("mirror_dest.gre_protocol = ", mirror_dest->gre_protocol));
  cintLines.push_back(to<string>("mirror_dest.ttl = ", mirror_dest->ttl));
  cintLines.push_back(
      to<string>("mirror_dest.udp_src_port = ", mirror_dest->udp_src_port));
  cintLines.push_back(
      to<string>("mirror_dest.udp_dst_port = ", mirror_dest->udp_dst_port));
  cintLines.push_back(
      to<string>("mirror_dest.flow_label = ", mirror_dest->flow_label));
  cintLines.push_back(to<string>("mirror_dest.tpid = ", mirror_dest->tpid));
  cintLines.push_back(
      to<string>("mirror_dest.vlan_id = ", mirror_dest->vlan_id));
  cintLines.push_back(
      to<string>("mirror_dest.stat_id = ", mirror_dest->stat_id));
  cintLines.push_back(
      to<string>("mirror_dest.stat_id2 = ", mirror_dest->stat_id2));

  cintLines.push_back(
      to<string>("mirror_dest.niv_src_vif = ", mirror_dest->niv_src_vif));
  cintLines.push_back(
      to<string>("mirror_dest.niv_dst_vif = ", mirror_dest->niv_dst_vif));
  cintLines.push_back(
      to<string>("mirror_dest.niv_flags = ", mirror_dest->niv_flags));
  cintLines.push_back(
      to<string>("mirror_dest.pkt_prio = ", mirror_dest->pkt_prio));
  cintLines.push_back(
      to<string>(
          "mirror_dest.sample_rate_dividend = ",
          mirror_dest->sample_rate_dividend));
  cintLines.push_back(
      to<string>(
          "mirror_dest.sample_rate_divisor = ",
          mirror_dest->sample_rate_divisor));

  cintLines.push_back(
      to<string>("mirror_dest.int_pri = ", mirror_dest->int_pri));
  cintLines.push_back(
      to<string>("mirror_dest.etag_src_vid = ", mirror_dest->etag_src_vid));
  cintLines.push_back(
      to<string>("mirror_dest.etag_dst_vid = ", mirror_dest->etag_dst_vid));
  cintLines.push_back(
      to<string>(
          "mirror_dest.egress_sample_rate_dividend = ",
          mirror_dest->egress_sample_rate_dividend));
  cintLines.push_back(
      to<string>(
          "mirror_dest.egress_sample_rate_divisor = ",
          mirror_dest->egress_sample_rate_divisor));
  cintLines.push_back(
      to<string>(
          "mirror_dest.recycle_context = ", mirror_dest->recycle_context));
  cintLines.push_back(
      to<string>(
          "mirror_dest.packet_copy_size = ", mirror_dest->packet_copy_size));
  cintLines.push_back(
      to<string>(
          "mirror_dest.egress_packet_copy_size = ",
          mirror_dest->egress_packet_copy_size));
  cintLines.push_back(to<string>("mirror_dest.df = ", mirror_dest->df));
  cintLines.push_back(
      to<string>("mirror_dest.truncate = ", mirror_dest->truncate));
  cintLines.push_back(
      to<string>("mirror_dest.template_id = ", mirror_dest->template_id));
  cintLines.push_back(
      to<string>(
          "mirror_dest.observation_domain = ",
          mirror_dest->observation_domain));
  cintLines.push_back(
      to<string>(
          "mirror_dest.is_recycle_strict_priority = ",
          mirror_dest->is_recycle_strict_priority));
  cintLines.push_back(to<string>("mirror_dest.flags2 = ", mirror_dest->flags2));
  cintLines.push_back(to<string>("mirror_dest.vni = ", mirror_dest->vni));
  cintLines.push_back(
      to<string>("mirror_dest.gre_seq_number = ", mirror_dest->gre_seq_number));
  cintLines.push_back(
      to<string>(
          "mirror_dest.initial_seq_number = ",
          mirror_dest->initial_seq_number));
  cintLines.push_back(
      to<string>("mirror_dest.meta_data = ", mirror_dest->meta_data));
  cintLines.push_back(
      to<string>("mirror_dest.ext_stat_valid = ", mirror_dest->ext_stat_valid));
  cintLines.push_back(
      to<string>("mirror_dest.ipfix_version = ", mirror_dest->ipfix_version));
  cintLines.push_back(
      to<string>("mirror_dest.psamp_epoch = ", mirror_dest->psamp_epoch));
  cintLines.push_back(to<string>("mirror_dest.cosq = ", mirror_dest->cosq));
  cintLines.push_back(to<string>("mirror_dest.cfi = ", mirror_dest->cfi));
  cintLines.push_back(
      to<string>("mirror_dest.drop_group_bmp = ", mirror_dest->drop_group_bmp));
  cintLines.push_back(
      to<string>(
          "mirror_dest.encap_truncate_profile_id = ",
          mirror_dest->encap_truncate_profile_id));
  cintLines.push_back(
      to<string>("mirror_dest.duplicate_pri = ", mirror_dest->duplicate_pri));
  cintLines.push_back(
      to<string>("mirror_dest.ip_proto = ", mirror_dest->ip_proto));
  cintLines.push_back(
      to<string>("mirror_dest.switch_id = ", mirror_dest->switch_id));

  string srcMac, dstMac;
  string srcMacName, dstMacName;
  tie(srcMac, srcMacName) = cintForMac(mirror_dest->src_mac);
  tie(dstMac, dstMacName) = cintForMac(mirror_dest->dst_mac);
  cintLines.push_back(srcMac);
  cintLines.push_back(to<string>("mirror_dest.src_mac = ", srcMacName));
  cintLines.push_back(dstMac);
  cintLines.push_back(to<string>("mirror_dest.dst_mac = ", dstMacName));

  if (mirror_dest->version == 4) {
    cintLines.push_back(
        to<string>("mirror_dest.src_addr = ", mirror_dest->src_addr));
    cintLines.push_back(
        to<string>("mirror_dest.dst_addr = ", mirror_dest->dst_addr));
  } else {
    string cintSrcV6Addr, cintDstV6Addr;
    string cintSrcV6Name, cintDstV6Name;
    tie(cintSrcV6Addr, cintSrcV6Name) = cintForIp6(mirror_dest->src6_addr);
    tie(cintDstV6Addr, cintDstV6Name) = cintForIp6(mirror_dest->dst6_addr);
    cintLines.push_back(cintSrcV6Addr);
    cintLines.push_back(to<string>("mirror_dest.src6_addr = ", cintSrcV6Name));
    cintLines.push_back(cintDstV6Addr);
    cintLines.push_back(to<string>("mirror_dest.dst6_addr = ", cintDstV6Name));
  }
  return cintLines;
}

vector<string> BcmCinter::cintForL3Host(const bcm_l3_host_t& l3_host) {
  vector<string> cintLines;
  if (l3_host.l3a_flags & BCM_L3_IP6) {
    string cintV6, v6Var;
    tie(cintV6, v6Var) = cintForIp6(l3_host.l3a_ip6_addr);
    cintLines = {
        cintV6,
        "bcm_l3_host_t_init(&l3_host)",
        to<string>("l3_host.l3a_vrf = ", l3_host.l3a_vrf),
        to<string>(
            "l3_host.l3a_intf = ", getCintVar(l3IntfIdVars, l3_host.l3a_intf)),
        to<string>("l3_host.l3a_flags = ", l3_host.l3a_flags),
        to<string>("l3_host.l3a_ip6_addr = ", v6Var)};

  } else {
    cintLines = {
        "bcm_l3_host_t_init(&l3_host)",
        to<string>("l3_host.l3a_vrf = ", l3_host.l3a_vrf),
        to<string>(
            "l3_host.l3a_intf = ", getCintVar(l3IntfIdVars, l3_host.l3a_intf)),
        to<string>("l3_host.l3a_flags = ", l3_host.l3a_flags),
        to<string>("l3_host.l3a_ip_addr = ", l3_host.l3a_ip_addr)};
  }
  return cintLines;
}

std::vector<std::string> BcmCinter::cintForLabelsArray(
    int num_labels,
    bcm_mpls_egress_label_t* label_array) {
  vector<string> cintLines;
  for (auto i = 0; i < num_labels; i++) {
    cintLines.push_back(
        to<string>("bcm_mpls_egress_label_t_init(&label_array[", i, "])"));
    cintLines.push_back(
        to<string>("label_array[", i, "].label = ", label_array[i].label));
    cintLines.push_back(
        to<string>("label_array[", i, "].flags = ", label_array[i].flags));
    cintLines.push_back(
        to<string>(
            "label_array[", i, "].qos_map_id = ", label_array[i].qos_map_id));
  }
  return cintLines;
}

std::vector<std::string> BcmCinter::cintForMplsTunnelSwitch(
    const bcm_mpls_tunnel_switch_t& switch_info) {
  vector<string> cintLines;
  cintLines.push_back(
      to<string>("bcm_mpls_tunnel_switch_t_init(&switch_info)"));
  cintLines.push_back(to<string>("switch_info.label = ", switch_info.label));
  cintLines.push_back(to<string>("switch_info.flags = ", switch_info.flags));
  cintLines.push_back(to<string>("switch_info.port = ", switch_info.port));
  cintLines.push_back(to<string>("switch_info.action = ", switch_info.action));
  cintLines.push_back(
      to<string>(
          "switch_info.egress_if = ",
          getCintVar(l3IntfIdVars, switch_info.egress_if)));
  return cintLines;
}

vector<string> BcmCinter::cintForPortPhyTimesyncConfig(
    bcm_port_phy_timesync_config_t* conf,
    const string& pptConfigVar) {
  string timecodeVar = getNextTimeSpecVar();
  auto cintLines = cintForTimeSpec(conf->original_timecode, timecodeVar);

  vector<string> structCint = {
      to<string>("bcm_port_phy_timesync_config_t ", pptConfigVar),
      to<string>("bcm_port_phy_timesync_config_t_init(&", pptConfigVar, ")"),
      to<string>(pptConfigVar + ".capabilities=", conf->capabilities),
      to<string>(pptConfigVar + ".validity_mask=", conf->validity_mask),
      to<string>(pptConfigVar + ".flags=", conf->flags),
      to<string>(pptConfigVar + ".itpid=", conf->itpid),
      to<string>(pptConfigVar + ".otpid=", conf->otpid),
      to<string>(pptConfigVar + ".otpid2=", conf->otpid),
      to<string>(pptConfigVar + ".timer_adjust.mode=", conf->timer_adjust.mode),
      to<string>(
          pptConfigVar + ".timer_adjust.delta=", conf->timer_adjust.delta),
      // Inband control
      to<string>(
          pptConfigVar + ".inband_control.flags=", conf->inband_control.flags),
      to<string>(
          pptConfigVar + ".inband_control.resv0_id=",
          conf->inband_control.resv0_id),
      to<string>(
          pptConfigVar + ".inband_control.timer_mode=",
          conf->inband_control.timer_mode),
      to<string>(pptConfigVar + ".gmode=", conf->gmode),
      // Framesync
      to<string>(pptConfigVar + ".framesync.mode=", conf->framesync.mode),
      to<string>(
          pptConfigVar + ".framesync.length_threshold=",
          conf->framesync.length_threshold),
      to<string>(
          pptConfigVar + ".framesync.event_offset=",
          conf->framesync.event_offset),
      // syncout
      to<string>(pptConfigVar + ".syncout.mode=", conf->syncout.mode),
      to<string>(
          pptConfigVar + ".syncout.pulse_1_length=",
          conf->syncout.pulse_1_length),
      to<string>(
          pptConfigVar + ".syncout.pulse_2_length=",
          conf->syncout.pulse_2_length),
      to<string>(pptConfigVar + ".syncout.interval=", conf->syncout.interval),
      to<string>(
          pptConfigVar + ".syncout.syncout_ts=", conf->syncout.syncout_ts),
      to<string>(pptConfigVar + ".ts_divider=", conf->ts_divider),
      to<string>(pptConfigVar + ".original_timecode=", timecodeVar),
      to<string>(
          pptConfigVar + ".tx_timestamp_offset=", conf->tx_timestamp_offset),
      to<string>(
          pptConfigVar + ".rx_timestamp_offset=", conf->rx_timestamp_offset),
      to<string>(pptConfigVar + ".rx_link_delay=", conf->rx_link_delay),
      to<string>(pptConfigVar + ".tx_sync_mode=", conf->tx_sync_mode),
      to<string>(
          pptConfigVar + ".tx_delay_request_mode=",
          conf->tx_delay_request_mode),
      to<string>(
          pptConfigVar + ".tx_pdelay_request_mode=",
          conf->tx_pdelay_request_mode),
      to<string>(
          pptConfigVar + ".tx_pdelay_response_mode=",
          conf->tx_pdelay_response_mode),
      to<string>(pptConfigVar + ".rx_sync_mode=", conf->rx_sync_mode),
      to<string>(
          pptConfigVar + ".rx_delay_request_mode=",
          conf->rx_delay_request_mode),
      to<string>(
          pptConfigVar + ".rx_pdelay_request_mode=",
          conf->rx_pdelay_request_mode),
      to<string>(
          pptConfigVar + ".rx_pdelay_response_mode=",
          conf->rx_pdelay_response_mode),
      // mpls control
      to<string>(
          pptConfigVar + ".mpls_control.flags=", conf->mpls_control.flags),
      to<string>(
          pptConfigVar + ".mpls_control.special_label=",
          conf->mpls_control.special_label),
      to<string>(pptConfigVar + ".mpls_control.size=", conf->mpls_control.size),
      to<string>(pptConfigVar + ".sync_freq=", conf->sync_freq),
      to<string>(pptConfigVar + ".phy_1588_dpll_k1=", conf->phy_1588_dpll_k1),
      to<string>(pptConfigVar + ".phy_1588_dpll_k2=", conf->phy_1588_dpll_k2),
      to<string>(pptConfigVar + ".phy_1588_dpll_k3=", conf->phy_1588_dpll_k3),
      to<string>(
          pptConfigVar + ".phy_1588_dpll_loop_filter=0x",
          conf->phy_1588_dpll_loop_filter),
      to<string>(
          pptConfigVar + ".phy_1588_dpll_ref_phase=0x",
          conf->phy_1588_dpll_ref_phase),
      to<string>(
          pptConfigVar + ".phy_1588_dpll_ref_phase_delta=",
          conf->phy_1588_dpll_ref_phase_delta)};
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(structCint.begin()),
      make_move_iterator(structCint.end()));
  // Mpls control labels
  for (int i = 0; i < conf->mpls_control.size; i++) {
    string labelHeader =
        to<string>(pptConfigVar, ".mpls_control.labels[", i, "].");
    vector<string> additionalCint = {
        to<string>(labelHeader, "value=", conf->mpls_control.labels[i].value),
        to<string>(labelHeader, "mask=", conf->mpls_control.labels[i].mask),
        to<string>(labelHeader, "flags=", conf->mpls_control.labels[i].flags)};
    cintLines.insert(
        cintLines.end(),
        make_move_iterator(additionalCint.begin()),
        make_move_iterator(additionalCint.end()));
  }
  return cintLines;
} // namespace facebook::fboss

vector<string> BcmCinter::cintForPortTimesyncConfig(
    const bcm_port_timesync_config_t& conf,
    const string& ppConfigVar) {
  string cintMac, macVar;
  tie(cintMac, macVar) = cintForMac(conf.src_mac_addr);
  vector<string> cintLines = {
      cintMac,
      to<string>("bcm_port_timesync_config_t ", ppConfigVar),
      to<string>("bcm_port_timesync_config_t_init(&", ppConfigVar, ")"),
      to<string>(ppConfigVar + ".flags=", conf.flags),
      to<string>(ppConfigVar + ".pkt_drop=", conf.pkt_drop),
      to<string>(ppConfigVar + ".pkt_tocpu=", conf.pkt_tocpu),
      to<string>(ppConfigVar + ".mpls_min_label=", conf.mpls_min_label),
      to<string>(ppConfigVar + ".mpls_max_label=", conf.mpls_max_label),
      to<string>(ppConfigVar + ".src_mac_addr=", macVar),
      to<string>(ppConfigVar + ".user_trap_id=", conf.user_trap_id)};
  return cintLines;
}

vector<string> BcmCinter::cintForPortTimesyncConfigArray(
    int config_count,
    bcm_port_timesync_config_t* config_array,
    const string& ppConfigArrayVar) {
  vector<string> configNames;
  vector<string> cintLines;
  for (int i = 0; i < config_count; ++i) {
    string ppConfigVar = getNextPtConfigVar();
    configNames.push_back(ppConfigVar);
    auto structCint = cintForPortTimesyncConfig(config_array[i], ppConfigVar);
    cintLines.insert(
        cintLines.end(),
        make_move_iterator(structCint.begin()),
        make_move_iterator(structCint.end()));
  }
  // Define the config array
  cintLines.push_back(
      to<string>(
          "bcm_port_timesync_config_t ",
          ppConfigArrayVar,
          "[] =",
          " {",
          join(", ", configNames),
          "}"));
  return cintLines;
}

vector<string> BcmCinter::cintForTimeSpec(
    const bcm_time_spec_s& timeSpec,
    const string& timeSpecVar) {
  vector<string> cintLines = {
      to<string>("bcm_time_spec_t ", timeSpecVar),
      to<string>(timeSpecVar + ".isnegative=", timeSpec.isnegative),
      to<string>(timeSpecVar + ".seconds=0x", timeSpec.seconds),
      to<string>(timeSpecVar + ".nanoseconds=", timeSpec.nanoseconds)};
  return cintLines;
}

vector<string> BcmCinter::cintForTimeInterface(
    bcm_time_interface_t* intf,
    const string& timeInterfaceVar) {
  vector<string> cintLines;
  // drift
  string driftVar = getNextTimeSpecVar();
  auto driftCint = cintForTimeSpec(intf->drift, driftVar);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(driftCint.begin()),
      make_move_iterator(driftCint.end()));
  // offset
  string offsetVar = getNextTimeSpecVar();
  auto offsetCint = cintForTimeSpec(intf->offset, offsetVar);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(offsetCint.begin()),
      make_move_iterator(offsetCint.end()));
  // accuracy
  string accuracyVar = getNextTimeSpecVar();
  auto accuracyCint = cintForTimeSpec(intf->accuracy, accuracyVar);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(accuracyCint.begin()),
      make_move_iterator(accuracyCint.end()));
  // ntpOffset
  string ntpOffsetVar = getNextTimeSpecVar();
  auto ntpOffsetCint = cintForTimeSpec(intf->ntp_offset, ntpOffsetVar);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(ntpOffsetCint.begin()),
      make_move_iterator(ntpOffsetCint.end()));
  // bsTime
  string bsTimeVar = getNextTimeSpecVar();
  auto bsTimeCint = cintForTimeSpec(intf->bs_time, bsTimeVar);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(bsTimeCint.begin()),
      make_move_iterator(bsTimeCint.end()));

  vector<string> structLines = {
      to<string>("bcm_time_interface_t ", timeInterfaceVar),
      to<string>("bcm_time_interface_t_init(&", timeInterfaceVar, ")"),
      to<string>(timeInterfaceVar + ".flags=", intf->flags),
      to<string>(timeInterfaceVar + ".id=", intf->id),
      to<string>(timeInterfaceVar + ".drift=", driftVar),
      to<string>(timeInterfaceVar + ".offset=", offsetVar),
      to<string>(timeInterfaceVar + ".accuracy=", accuracyVar),
      to<string>(timeInterfaceVar + ".heartbeat_hz=", intf->heartbeat_hz),
      to<string>(timeInterfaceVar + ".clk_resolution=", intf->clk_resolution),
      to<string>(timeInterfaceVar + ".bitclock_hz=", intf->bitclock_hz),
      to<string>(timeInterfaceVar + ".status=", intf->status),
      to<string>(timeInterfaceVar + ".ntp_offset=", ntpOffsetVar),
      to<string>(timeInterfaceVar + ".bs_time=", bsTimeVar),
  };
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(structLines.begin()),
      make_move_iterator(structLines.end()));
  return cintLines;
}

vector<string> BcmCinter::cintForFlexctrConfig(
    bcm_field_flexctr_config_t* flexctr_cfg) {
  vector<string> cintLines = {
      to<string>(
          "flexctr_config.flexctr_action_id=",
          getCintVar(statCounterIdVars, flexctr_cfg->flexctr_action_id)),
      to<string>("flexctr_config.counter_index=", flexctr_cfg->counter_index)};
  return cintLines;
}

vector<string> BcmCinter::cintForFlexCtrTrigger(
    bcm_flexctr_trigger_t& flexctr_trigger) {
  vector<string> cintLines = {
      to<string>("flexctr_trigger.trigger_type=", flexctr_trigger.trigger_type),
      to<string>("flexctr_trigger.period_num=", flexctr_trigger.period_num),
      to<string>("flexctr_trigger.interval=", flexctr_trigger.interval),
      to<string>("flexctr_trigger.object=", flexctr_trigger.object),
      to<string>(
          "flexctr_trigger.object_value_start=",
          flexctr_trigger.object_value_start),
      to<string>(
          "flexctr_trigger.object_value_stop=",
          flexctr_trigger.object_value_stop),
      to<string>(
          "flexctr_trigger.object_value_mask=",
          flexctr_trigger.object_value_mask),
      to<string>(
          "flexctr_trigger.interval_shift=", flexctr_trigger.interval_shift),
      to<string>(
          "flexctr_trigger.interval_size=", flexctr_trigger.interval_size),
      to<string>("flexctr_trigger.object_id=", flexctr_trigger.object_id),
      to<string>(
          "flexctr_trigger.action_count_stop=",
          flexctr_trigger.action_count_stop),
      to<string>("flexctr_trigger.flags=", flexctr_trigger.flags)};
  return cintLines;
}

vector<string> BcmCinter::cintForFlexCtrValueOperation(
    bcm_flexctr_value_operation_t* operation,
    const string& varname) {
  vector<string> cintLines = {
      to<string>("flexctr_action.", varname, ".select=", operation->select),
      to<string>("flexctr_action.", varname, ".type=", operation->type),
  };
  for (int i = 0; i < BCM_FLEXCTR_OPERATION_OBJECT_SIZE; i++) {
    vector<string> operationCint = {
        to<string>(
            "flexctr_action.",
            varname,
            ".object[",
            i,
            "]=",
            operation->object[i]),
        to<string>(
            "flexctr_action.",
            varname,
            ".quant_id[",
            i,
            "]=",
            operation->quant_id[i]),
        to<string>(
            "flexctr_action.",
            varname,
            ".mask_size[",
            i,
            "]=",
            operation->mask_size[i]),
        to<string>(
            "flexctr_action.",
            varname,
            ".shift[",
            i,
            "]=",
            operation->shift[i]),
        to<string>(
            "flexctr_action.",
            varname,
            ".object_id[",
            i,
            "]=",
            operation->object_id[i]),
    };
    cintLines.insert(
        cintLines.end(),
        make_move_iterator(operationCint.begin()),
        make_move_iterator(operationCint.end()));
  }

  return cintLines;
}

vector<string> BcmCinter::cintForFlexCtrAction(
    bcm_flexctr_action_t* flexctr_action) {
  string pbmp;
  vector<string> cintLines, pbmpCint;
  tie(pbmp, pbmpCint) = cintForPortBitmap(flexctr_action->ports);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(pbmpCint.begin()),
      make_move_iterator(pbmpCint.end()));
  vector<string> structLines = {
      "bcm_flexctr_action_t_init(&flexctr_action)",
      to<string>("flexctr_action.flags=", flexctr_action->flags),
      to<string>("flexctr_action.source=", flexctr_action->source),
      to<string>("flexctr_action.ports=", pbmp),
      to<string>("flexctr_action.hint=", flexctr_action->hint),
      to<string>(
          "flexctr_action.drop_count_mode=", flexctr_action->drop_count_mode),
      to<string>(
          "flexctr_action.exception_drop_count_enable=",
          flexctr_action->exception_drop_count_enable),
      to<string>(
          "flexctr_action.egress_mirror_count_enable=",
          flexctr_action->egress_mirror_count_enable),
      to<string>("flexctr_action.mode=", flexctr_action->mode),
      to<string>("flexctr_action.hint_ext=", flexctr_action->hint_ext),
      to<string>("flexctr_action.index_num=", flexctr_action->index_num),
  };
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(structLines.begin()),
      make_move_iterator(structLines.end()));
  // index_operation
  for (int i = 0; i < BCM_FLEXCTR_OPERATION_OBJECT_SIZE; i++) {
    vector<string> indexOperationCint = {
        to<string>(
            "flexctr_action.index_operation.object[",
            i,
            "]=",
            flexctr_action->index_operation.object[i]),
        to<string>(
            "flexctr_action.index_operation.quant_id[",
            i,
            "]=",
            flexctr_action->index_operation.quant_id[i]),
        to<string>(
            "flexctr_action.index_operation.mask_size[",
            i,
            "]=",
            flexctr_action->index_operation.mask_size[i]),
        to<string>(
            "flexctr_action.index_operation.shift[",
            i,
            "]=",
            flexctr_action->index_operation.shift[i]),
        to<string>(
            "flexctr_action.index_operation.object_id[",
            i,
            "]=",
            flexctr_action->index_operation.object_id[i]),
    };
    cintLines.insert(
        cintLines.end(),
        make_move_iterator(indexOperationCint.begin()),
        make_move_iterator(indexOperationCint.end()));
  }
  // operation_a
  vector<string> operationACint =
      cintForFlexCtrValueOperation(&flexctr_action->operation_a, "operation_a");
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(operationACint.begin()),
      make_move_iterator(operationACint.end()));
  // operation_b
  vector<string> operationBCint = operationACint =
      cintForFlexCtrValueOperation(&flexctr_action->operation_b, "operation_b");
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(operationBCint.begin()),
      make_move_iterator(operationBCint.end()));
  // Flexctr_trigger
  vector<string> triggerCint = cintForFlexCtrTrigger(flexctr_action->trigger);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(triggerCint.begin()),
      make_move_iterator(triggerCint.end()));
  cintLines.push_back(to<string>("flexctr_action.trigger=flexctr_trigger"));
  structLines = {
      to<string>("flexctr_action.hint_type=", flexctr_action->hint_type),
      to<string>("flexctr_action.base_pool_id=", flexctr_action->base_pool_id),
      to<string>("flexctr_action.base_index=", flexctr_action->base_index),
  };
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(structLines.begin()),
      make_move_iterator(structLines.end()));
  return cintLines;
}

int BcmCinter::bcm_cosq_init(int unit) {
  writeCintLines(wrapFunc(to<string>("bcm_cosq_init(", unit, ")")));
  return 0;
}

int BcmCinter::bcm_cosq_gport_sched_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    int mode,
    int weight) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_cosq_gport_sched_set(",
          makeParamStr(unit, gport, cosq, mode, weight),
          ")")));
  return 0;
}

int BcmCinter::bcm_cosq_control_set(
    int unit,
    bcm_gport_t port,
    bcm_cos_queue_t cosq,
    bcm_cosq_control_t type,
    int arg) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_cosq_control_set(",
          makeParamStr(unit, port, cosq, type, arg),
          ")")));
  return 0;
}

int BcmCinter::bcm_cosq_port_profile_set(
    int unit,
    bcm_gport_t port,
    bcm_cosq_profile_type_t profile_type,
    int profile_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_cosq_port_profile_set(",
          makeParamStr(unit, port, profile_type, profile_id),
          ")")));
  return 0;
}

int BcmCinter::bcm_cosq_gport_bandwidth_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    uint32 kbits_sec_min,
    uint32 kbits_sec_max,
    uint32 flags) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_cosq_gport_bandwidth_set(",
          makeParamStr(unit, gport, cosq, kbits_sec_min, kbits_sec_max, flags),
          ")")));
  return 0;
}

int BcmCinter::bcm_cosq_gport_discard_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_cosq_gport_discard_t* discard) {
  auto cint = cintForCosQDiscard(*discard);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_cosq_gport_discard_set(",
          makeParamStr(unit, gport, cosq, "&discard"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_cosq_gport_mapping_set(
    int unit,
    bcm_gport_t ing_port,
    bcm_cos_t priority,
    uint32_t flags,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_cosq_gport_mapping_set(",
          makeParamStr(unit, ing_port, priority, flags, gport, cosq),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_init(int unit) {
  writeCintLines(wrapFunc(to<string>("bcm_field_init(", unit, ")")));
  return 0;
}

int BcmCinter::bcm_cosq_pfc_class_config_profile_set(
    int unit,
    int profile_index,
    int count,
    bcm_cosq_pfc_class_map_config_t* config_array) {
  string arrayVarName = getNextPfcPriToQueueVar();
  vector<string> cintLines;
  string argVarArray = to<string>(
      "bcm_cosq_pfc_class_map_config_t ", arrayVarName, "[", count, "]");
  cintLines.push_back(argVarArray);
  for (int i = 0; i < count; ++i) {
    cintLines.push_back(
        to<string>(
            "bcm_cosq_pfc_class_map_config_t_init(&",
            arrayVarName,
            "[",
            i,
            "])"));
    cintLines.push_back(
        to<string>(
            arrayVarName, "[", i, "].pfc_enable=", config_array[i].pfc_enable));
    cintLines.push_back(
        to<string>(
            arrayVarName,
            "[",
            i,
            "].pfc_optimized=",
            config_array[i].pfc_optimized));
    cintLines.push_back(
        to<string>(
            arrayVarName,
            "[",
            i,
            "].cos_list_bmp=",
            config_array[i].cos_list_bmp));
  }
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_cosq_pfc_class_config_profile_set(",
          makeParamStr(unit, profile_index, count, arrayVarName),
          ")"));
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cintLines));
  return 0;
}

int BcmCinter::bcm_cosq_priority_group_pfc_priority_mapping_profile_set(
    int unit,
    int profile_index,
    int array_count,
    int* pg_array) {
  std::vector<string> argValues;
  for (int i = 0; i < array_count; ++i) {
    argValues.push_back(to<string>(pg_array[i]));
  }
  string argVarArrayName = getNextPfcPriToPgVar();
  string argVarArray = to<string>(
      "int ", argVarArrayName, "[] =", " {", join(", ", argValues), "}");
  vector<string> cint = {argVarArray};
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_cosq_priority_group_pfc_priority_mapping_profile_set(",
          makeParamStr(unit, profile_index, array_count, argVarArrayName),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator((cintForFn.end())));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_cosq_priority_group_mapping_profile_set(
    int unit,
    int profile_index,
    bcm_cosq_priority_group_mapping_profile_type_t type,
    int array_count,
    int* arg) {
  std::vector<string> argValues;
  for (int i = 0; i < array_count; ++i) {
    argValues.push_back(to<string>(arg[i]));
  }
  string argVarArrayName = getNextPriorityToPgVar();
  string argVarArray = to<string>(
      "int ", argVarArrayName, "[] =", " {", join(", ", argValues), "}");
  vector<string> cint = {argVarArray};
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_cosq_priority_group_mapping_profile_set(",
          makeParamStr(unit, profile_index, type, array_count, argVarArrayName),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator((cintForFn.end())));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_group_create_id(
    int unit,
    bcm_field_qset_t groupQset,
    int pri,
    bcm_field_group_t group) {
  auto cint = cintForQset(groupQset);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_field_group_create_id(",
          makeParamStr(unit, "qset", pri, group),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_group_config_create(
    int unit,
    bcm_field_group_config_t* group_config) {
  auto cint = cintForFpGroupConfig(group_config);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_field_group_config_create(",
          makeParamStr(unit, "group_config"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_group_destroy(int unit, bcm_field_group_t group) {
  writeCintLines(wrapFunc(
      to<string>("bcm_field_group_destroy(", makeParamStr(unit, group), ")")));
  return 0;
}

int BcmCinter::bcm_field_entry_create(
    int unit,
    bcm_field_group_t group,
    bcm_field_entry_t* entry) {
  string entryVar = getNextFieldEntryVar();
  writeCintLine(to<string>("bcm_field_entry_t ", entryVar));
  {
    fieldEntryVars.wlock()->emplace(*entry, entryVar);
  }
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_entry_create(",
          makeParamStr(unit, group, to<string>("&", entryVar)),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_entry_create_id(
    int unit,
    bcm_field_group_t group,
    bcm_field_entry_t entry) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_entry_create(", makeParamStr(unit, group, entry), ")")));
  return 0;
}

int BcmCinter::bcm_l3_enable_set(int unit, int enable) {
  writeCintLines(wrapFunc(
      to<string>("bcm_l3_enable_set(", makeParamStr(unit, enable), ")")));
  return 0;
}

int BcmCinter::bcm_field_entry_install(int unit, bcm_field_entry_t entry) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_entry_install(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry)),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_entry_reinstall(int unit, bcm_field_entry_t entry) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_entry_reinstall(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry)),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_entry_prio_set(
    int unit,
    bcm_field_entry_t entry,
    int prio) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_entry_prio_set(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), prio),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_entry_enable_set(
    int unit,
    bcm_field_entry_t entry,
    int enable) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_entry_enable_set(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), enable),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_stat_create(
    int unit,
    bcm_field_group_t group,
    int nstat,
    bcm_field_stat_t* stat_arr,
    int* stat_id) {
  string statArrayCint, statArrayVar, statIdVar, statIdVarCint;
  statIdVar = getNextStatVar();
  {
    statIdVars.wlock()->emplace(*stat_id, statIdVar);
  }
  statIdVarCint = to<string>("int ", statIdVar);
  tie(statArrayCint, statArrayVar) = cintForStatArray(stat_arr, nstat);
  vector<string> cint = {statIdVarCint, statArrayCint};
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_field_stat_create(",
          makeParamStr(
              unit, group, nstat, statArrayVar, to<string>("&", statIdVar)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_stat_destroy(int unit, int stat_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_stat_destroy(",
          makeParamStr(unit, getCintVar(statIdVars, stat_id)),
          ")")));
  {
    statIdVars.wlock()->erase(stat_id);
  }
  return 0;
}

int BcmCinter::bcm_field_entry_stat_attach(
    int unit,
    bcm_field_entry_t entry,
    int stat_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_entry_stat_attach(",
          makeParamStr(
              unit,
              getCintVar(fieldEntryVars, entry),
              getCintVar(statIdVars, stat_id)),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_entry_stat_detach(
    int unit,
    bcm_field_entry_t entry,
    int stat_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_entry_stat_detach(",
          makeParamStr(
              unit,
              getCintVar(fieldEntryVars, entry),
              getCintVar(statIdVars, stat_id)),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_action_add(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action,
    uint32 param0,
    uint32 param1) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_action_add(",
          makeParamStr(
              unit, getCintVar(fieldEntryVars, entry), action, param0, param1),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_action_remove(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_action_remove(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), action),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_action_delete(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action,
    uint32 param0,
    uint32 param1) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_action_delete(",
          makeParamStr(
              unit, getCintVar(fieldEntryVars, entry), action, param0, param1),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_DstIp(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip_t data,
    bcm_ip_t mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_DstIp",
          "(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_DstIp6(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t data,
    bcm_ip6_t mask) {
  return bcmFieldQualifyIp6(unit, entry, data, mask, Dir::DST);
}

int BcmCinter::bcm_field_qualify_SrcIp6(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t data,
    bcm_ip6_t mask) {
  return bcmFieldQualifyIp6(unit, entry, data, mask, Dir::SRC);
}

int BcmCinter::bcmFieldQualifyIp6(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t data,
    bcm_ip6_t mask,
    Dir dir) {
  string cintV6, v6Var, cintMask, v6Mask;
  tie(cintV6, v6Var) = cintForIp6(data);
  tie(cintMask, v6Mask) = cintForIp6(mask);
  vector<string> cint = {cintV6, cintMask};
  auto cintForFn = wrapFunc(
      to<string>(
          dir == Dir::SRC ? "bcm_field_qualify_SrcIp6"
                          : "bcm_field_qualify_DstIp6",
          "(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), v6Var, v6Mask),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_qualify_L4DstPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t data,
    bcm_l4_port_t mask) {
  return bcmFieldQualifyL4Port(unit, entry, data, mask, Dir::DST);
}

int BcmCinter::bcm_field_qualify_L4SrcPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t data,
    bcm_l4_port_t mask) {
  return bcmFieldQualifyL4Port(unit, entry, data, mask, Dir::SRC);
}

int BcmCinter::bcmFieldQualifyL4Port(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t data,
    bcm_l4_port_t mask,
    Dir dir) {
  writeCintLines(wrapFunc(
      to<string>(
          dir == Dir::SRC ? "bcm_field_qualify_L4SrcPort"
                          : "bcm_field_qualify_L4DstPort",
          "(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_DstMac(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t mac,
    bcm_mac_t macMask) {
  return bcmFieldQualifyMac(unit, entry, mac, macMask, Dir::DST);
}

int BcmCinter::bcm_field_qualify_SrcMac(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t mac,
    bcm_mac_t macMask) {
  return bcmFieldQualifyMac(unit, entry, mac, macMask, Dir::SRC);
}

int BcmCinter::bcmFieldQualifyMac(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t mac,
    bcm_mac_t macMask,
    Dir dir) {
  string cintMac, macVar, cintMask, macMaskVar;
  tie(cintMac, macVar) = cintForMac(mac);
  tie(cintMask, macMaskVar) = cintForMac(macMask);
  vector<string> cint = {cintMac, cintMask};
  auto cintForFn = wrapFunc(
      to<string>(
          dir == Dir::SRC ? "bcm_field_qualify_SrcMac"
                          : "bcm_field_qualify_DstMac",
          "(",
          makeParamStr(
              unit, getCintVar(fieldEntryVars, entry), macVar, macMaskVar),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_qualify_DstClassL2(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_DstClassL2",
          "(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_DstClassL3(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_DstClassL3",
          "(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_PacketRes(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_PacketRes",
          "(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_OuterVlanId(
    int unit,
    bcm_field_entry_t entry,
    bcm_vlan_t data,
    bcm_vlan_t mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_OuterVlanId",
          "(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_Ttl(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_Ttl(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_IpType(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpType_t type) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_IpType(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), type),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_EtherType(
    int unit,
    bcm_field_entry_t entry,
    uint16 data,
    uint16 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_EtherType(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_IpProtocol(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_IpProtocol(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_DSCP(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_DSCP(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_TcpControl(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_TcpControl(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_SrcPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t data_modid,
    bcm_module_t mask_modid,
    bcm_port_t data_port,
    bcm_port_t mask_port) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_SrcPort(",
          makeParamStr(
              unit,
              getCintVar(fieldEntryVars, entry),
              data_modid,
              mask_modid,
              data_port,
              mask_port),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_DstPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t data_modid,
    bcm_module_t mask_modid,
    bcm_port_t data_port,
    bcm_port_t mask_port) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_DstPort(",
          makeParamStr(
              unit,
              getCintVar(fieldEntryVars, entry),
              data_modid,
              mask_modid,
              data_port,
              mask_port),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_IpFrag(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpFrag_t frag_info) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_IpFrag(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), frag_info),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_IcmpTypeCode(
    int unit,
    bcm_field_entry_t entry,
    uint16 data,
    uint16 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_IcmpTypeCode(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
};

int BcmCinter::bcm_field_qualify_RangeCheck(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_range_t range,
    int invert) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_RangeCheck(",
          makeParamStr(
              unit,
              getCintVar(fieldEntryVars, entry),
              getCintVar(rangeIdVars, range),
              invert),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_entry_destroy(int unit, bcm_field_entry_t entry) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_entry_destroy(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry)),
          ")")));
  {
    fieldEntryVars.wlock()->erase(entry);
  }
  return 0;
}

vector<string> BcmCinter::cintForTrunkInfo(const bcm_trunk_info_t& trunk_info) {
  vector<string> cintLines = {
      "bcm_trunk_info_t_init(&trunk_info)",

      to<string>("trunk_info.psc=", trunk_info.psc),
      to<string>("trunk_info.dlf_index=", trunk_info.dlf_index),
      to<string>("trunk_info.mc_index=", trunk_info.mc_index),
      to<string>("trunk_info.ipmc_index=", trunk_info.ipmc_index)};
  return cintLines;
}

vector<string> BcmCinter::cintForTrunkMember(
    const bcm_trunk_member_t& trunk_member) {
  vector<string> cintLines = {
      "bcm_trunk_member_t_init(&trunk_member)",
      to<string>("trunk_member.flags=", trunk_member.flags),
      to<string>("trunk_member.gport=", trunk_member.gport)};
  return cintLines;
}

pair<string, vector<string>> BcmCinter::cintForTrunkMemberArray(
    const bcm_trunk_member_t* trunkMemberArray,
    int member_max) {
  vector<string> cintLines;
  auto memberArrayName = getNextTmpTrunkMemberArray();
  cintLines.push_back(
      to<string>(
          "bcm_trunk_member_t ", memberArrayName, "[BCM_TRUNK_MAX_PORTCNT]"));
  for (int i = 0; i < member_max; ++i) {
    cintLines.push_back(
        to<string>("bcm_trunk_member_t_init(&", memberArrayName, "[", i, "])"));
    cintLines.push_back(
        to<string>(
            memberArrayName, "[", i, "].flags=", trunkMemberArray[i].flags));
    cintLines.push_back(
        to<string>(
            memberArrayName, "[", i, "].gport=", trunkMemberArray[i].gport));
  }
  memberArrayName = member_max > 0 ? memberArrayName : "NULL";
  return make_pair(memberArrayName, cintLines);
}

int BcmCinter::bcm_trunk_init(int unit) {
  writeCintLines(
      wrapFunc(to<string>("bcm_trunk_init(", makeParamStr(unit), ")")));
  return 0;
}

int BcmCinter::bcm_trunk_destroy(int unit, bcm_trunk_t tid) {
  writeCintLines(
      wrapFunc(to<string>("bcm_trunk_destroy(", makeParamStr(unit, tid), ")")));
  return 0;
}

int BcmCinter::bcm_trunk_create(
    int unit,
    uint32 flags,
    bcm_trunk_t* /* tid */) {
  writeCintLines(wrapFunc(
      to<string>("bcm_trunk_create(", makeParamStr(unit, flags, "&tid"), ")")));
  return 0;
}

int BcmCinter::bcm_trunk_member_add(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_member_t* trunk_member) {
  auto cint = cintForTrunkMember(*trunk_member);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_trunk_member_add(",
          makeParamStr(unit, tid, "&trunk_member"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_trunk_member_delete(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_member_t* member) {
  auto cint = cintForTrunkMember(*member);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_trunk_member_delete(",
          makeParamStr(unit, tid, "&trunk_member"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_trunk_set(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_info_t* trunk_info,
    int member_max,
    bcm_trunk_member_t* member_array) {
  auto cintTrunkInfo = cintForTrunkInfo(*trunk_info);
  string trunkMemberVar = "NULL";
  vector<string> cintTrunkMemberArray;
  if (member_max > 0) {
    tie(trunkMemberVar, cintTrunkMemberArray) =
        cintForTrunkMemberArray(member_array, member_max);
  }
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_trunk_set(",
          makeParamStr(unit, tid, "&trunk_info", member_max, trunkMemberVar),
          ")"));
  cintTrunkInfo.insert(
      cintTrunkInfo.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  cintTrunkMemberArray.insert(
      cintTrunkMemberArray.end(),
      make_move_iterator(cintTrunkInfo.begin()),
      make_move_iterator(cintTrunkInfo.end()));
  writeCintLines(std::move(cintTrunkMemberArray));
  return 0;
}

int BcmCinter::bcm_port_loopback_set(int unit, bcm_port_t port, uint32 value) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_loopback_set(", makeParamStr(unit, port, value), ")")));
  return 0;
}

vector<string> BcmCinter::cintForBcmUdfHashConfig(
    const bcm_udf_hash_config_t& config) {
  vector<string> cintLines = {
      "bcm_udf_hash_config_t_init(&config)",
      to<string>("config.flags=", config.flags),
      to<string>("config.udf_id=", config.udf_id),
      to<string>("config.mask_length=", config.mask_length),
  };
  for (int i = 0; i < config.mask_length; i++) {
    cintLines.push_back(
        to<string>("config.hash_mask", "[", i, "]=", config.hash_mask[i]));
  }
  return cintLines;
}

int BcmCinter::bcm_udf_hash_config_add(
    int unit,
    uint32 options,
    bcm_udf_hash_config_t* config) {
  auto cint = cintForBcmUdfHashConfig(*config);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_udf_hash_config_add(",
          makeParamStr(unit, options, "&config"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_udf_hash_config_delete(
    int unit,
    bcm_udf_hash_config_t* config) {
  auto cint = cintForBcmUdfHashConfig(*config);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_udf_hash_config_delete(", makeParamStr(unit, "&config"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

vector<string> BcmCinter::cintForBcmUdfAllocHints(
    const bcm_udf_alloc_hints_t* hints) {
  if (hints == nullptr) {
    return {"bcm_udf_alloc_hints_init(&hints)"};
  }
  vector<string> cintLines = {
      "bcm_udf_alloc_hints_init(&hints)",
      to<string>("hints.flags=", hints->flags),
      to<string>("hints.shared_udf=", hints->shared_udf),
  };
  auto cintQset = cintForQset(hints->qset);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(cintQset.begin()),
      make_move_iterator(cintQset.end()));
  return cintLines;
}

vector<string> BcmCinter::cintForBcmUdfInfo(const bcm_udf_t* udf_info) {
  if (udf_info == nullptr) {
    return {"bcm_udf_t_init(&udf_info)"};
  }
  string pbmp;
  vector<string> pbmpCint, cintLines;
  tie(pbmp, pbmpCint) = cintForPortBitmap(udf_info->ports);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(pbmpCint.begin()),
      make_move_iterator(pbmpCint.end()));
  vector<string> udfInfoCintLines = {
      "bcm_udf_t_init(&udf_info)",
      to<string>("udf_info.flags=", udf_info->flags),
      to<string>("udf_info.layer=", udf_info->layer),
      to<string>("udf_info.start=", udf_info->start),
      to<string>("udf_info.width=", udf_info->width),
      to<string>("udf_info.ports=", pbmp),
  };
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(udfInfoCintLines.begin()),
      make_move_iterator(udfInfoCintLines.end()));
  return cintLines;
}

int BcmCinter::bcm_udf_create(
    int unit,
    bcm_udf_alloc_hints_t* hints,
    bcm_udf_t* udf_info,
    bcm_udf_id_t* /* udf_id */) {
  auto cintHints = cintForBcmUdfAllocHints(hints);

  auto cintInfo = cintForBcmUdfInfo(udf_info);

  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_udf_create(",
          makeParamStr(unit, "&hints", "&udf_info", "&udf_id"),
          ")"));
  cintHints.insert(
      cintHints.end(),
      make_move_iterator(cintInfo.begin()),
      make_move_iterator(cintInfo.end()));
  cintHints.insert(
      cintHints.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cintHints));
  return 0;
}

int BcmCinter::bcm_udf_destroy(int unit, bcm_udf_id_t udf_id) {
  writeCintLines(wrapFunc(
      to<string>("bcm_udf_destroy(", makeParamStr(unit, udf_id), ")")));
  return 0;
}

vector<string> BcmCinter::cintForBcmUdfPktFormatInfo(
    const bcm_udf_pkt_format_info_t& pktFormat) {
  vector<string> cintLines = {
      "bcm_udf_pkt_format_info_t_init(&pktFormat)",
      to<string>("pktFormat.prio=", pktFormat.prio),
      to<string>("pktFormat.ethertype=", pktFormat.ethertype),
      to<string>("pktFormat.ethertype_mask=", pktFormat.ethertype_mask),
      to<string>("pktFormat.ip_protocol=", pktFormat.ip_protocol),
      to<string>("pktFormat.ip_protocol_mask=", pktFormat.ip_protocol_mask),
      to<string>("pktFormat.l2=", pktFormat.l2),
      to<string>("pktFormat.vlan_tag=", pktFormat.vlan_tag),
      to<string>("pktFormat.outer_ip=", pktFormat.outer_ip),
      to<string>("pktFormat.inner_ip=", pktFormat.inner_ip),
      to<string>("pktFormat.tunnel=", pktFormat.tunnel),
      to<string>("pktFormat.mpls=", pktFormat.mpls),
      to<string>("pktFormat.fibre_chan_outer=", pktFormat.fibre_chan_outer),
      to<string>("pktFormat.fibre_chan_inner=", pktFormat.fibre_chan_inner),
      to<string>("pktFormat.higig=", pktFormat.higig),
      to<string>("pktFormat.vntag=", pktFormat.vntag),
      to<string>("pktFormat.etag=", pktFormat.etag),
      to<string>("pktFormat.cntag=", pktFormat.cntag),
      to<string>("pktFormat.icnm=", pktFormat.icnm),
      to<string>("pktFormat.subport_tag=", pktFormat.subport_tag),
      to<string>("pktFormat.class_id=", pktFormat.class_id),
      to<string>("pktFormat.inner_protocol=", pktFormat.inner_protocol),
      to<string>(
          "pktFormat.inner_protocol_mask=", pktFormat.inner_protocol_mask),
      to<string>("pktFormat.l4_dst_port=", pktFormat.l4_dst_port),
      to<string>("pktFormat.l4_dst_port_mask=", pktFormat.l4_dst_port_mask),
      to<string>("pktFormat.opaque_tag_type=", pktFormat.opaque_tag_type),
      to<string>(
          "pktFormat.opaque_tag_type_mask=", pktFormat.opaque_tag_type_mask),
      to<string>("pktFormat.int_pkt=", pktFormat.int_pkt),
      to<string>("pktFormat.src_port=", pktFormat.src_port),
      to<string>("pktFormat.src_port_mask=", pktFormat.src_port_mask),
      to<string>("pktFormat.lb_pkt_type=", pktFormat.lb_pkt_type),
      to<string>(
          "pktFormat.first_2bytes_after_mpls_bos=",
          pktFormat.first_2bytes_after_mpls_bos),
      to<string>(
          "pktFormat.first_2bytes_after_mpls_bos_mask=",
          pktFormat.first_2bytes_after_mpls_bos_mask),
      to<string>("pktFormat.outer_ifa=", pktFormat.outer_ifa),
      to<string>("pktFormat.inner_ifa=", pktFormat.inner_ifa),
      to<string>(
          "pktFormat.ip_gre_first_2bytes=", pktFormat.ip_gre_first_2bytes),
      to<string>(
          "pktFormat.ip_gre_first_2bytes_mask=",
          pktFormat.ip_gre_first_2bytes_mask),

  };
  return cintLines;
}

int BcmCinter::bcm_udf_pkt_format_create(
    int unit,
    bcm_udf_pkt_format_options_t options,
    bcm_udf_pkt_format_info_t* pkt_format,
    bcm_udf_pkt_format_id_t* pkt_format_id) {
  auto cintPktFormat = cintForBcmUdfPktFormatInfo(*pkt_format);

  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_udf_pkt_format_create(",
          makeParamStr(unit, options, "&pkt_format", *pkt_format_id),
          ")"));
  cintPktFormat.insert(
      cintPktFormat.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cintPktFormat));
  return 0;
}

int BcmCinter::bcm_udf_pkt_format_destroy(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_udf_pkt_format_destroy(",
          makeParamStr(unit, pkt_format_id),
          ")")));
  return 0;
}

int BcmCinter::bcm_port_pause_addr_set(
    int unit,
    bcm_port_t port,
    bcm_mac_t mac) {
  string cintMac, macVar;
  tie(cintMac, macVar) = cintForMac(mac);
  vector<string> cint = {cintMac};
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_port_pause_addr_set(", makeParamStr(unit, port, macVar), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_udf_pkt_format_add(
    int unit,
    bcm_udf_id_t udf_id,
    bcm_udf_pkt_format_id_t pkt_format_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_udf_pkt_format_add(",
          makeParamStr(unit, udf_id, pkt_format_id),
          ")")));
  return 0;
}

int BcmCinter::bcm_udf_pkt_format_delete(
    int unit,
    bcm_udf_id_t udf_id,
    bcm_udf_pkt_format_id_t pkt_format_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_udf_pkt_format_delete(",
          makeParamStr(unit, udf_id, pkt_format_id),
          ")")));
  return 0;
}

void BcmCinter::bcm_udf_pkt_format_info_t_init(
    bcm_udf_pkt_format_info_t* pkt_format) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_udf_pkt_format_info_t_init(", makeParamStr("pkt_format"), ")")));
}

void BcmCinter::bcm_udf_alloc_hints_t_init(bcm_udf_alloc_hints_t* udf_hints) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_udf_alloc_hints_t_init(", makeParamStr("udf_hints"), ")")));
}

void BcmCinter::bcm_udf_t_init(bcm_udf_t* udf_info) {
  writeCintLines(
      wrapFunc(to<string>("bcm_udf_t_init(", makeParamStr("udf_info"), ")")));
}

void BcmCinter::bcm_udf_hash_config_t_init(bcm_udf_hash_config_t* config) {
  writeCintLines(wrapFunc(
      to<string>("bcm_udf_hash_config_t_init(", makeParamStr("config"), ")")));
}

int BcmCinter::bcm_port_autoneg_set(int unit, bcm_port_t port, int autoneg) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_autoneg_set(", makeParamStr(unit, port, autoneg), ")")));
  return 0;
}

int BcmCinter::bcm_udf_init(int unit) {
  writeCintLines(
      wrapFunc(to<string>("bcm_udf_init(", makeParamStr(unit), ")")));
  return 0;
}

int BcmCinter::bcm_field_qset_id_multi_set(
    int unit,
    bcm_field_qualify_t qualifier,
    int num_objects,
    int* object_list,
    bcm_field_qset_t* qset) {
  auto cint = cintForQset(*qset);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_field_qset_id_multi_set(",
          makeParamStr(unit, qualifier, num_objects, *object_list, "&qset"),
          ")"));

  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_qualify_UdfClass(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_UdfClass(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), data, mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_udf(
    int unit,
    bcm_field_entry_t eid,
    bcm_udf_id_t udf_id,
    int length,
    uint8* data,
    uint8* mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_udf(",
          makeParamStr(
              unit,
              getCintVar(fieldEntryVars, eid),
              udf_id,
              length,
              *data,
              *mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_field_qualify_udf_get(
    int unit,
    bcm_field_entry_t eid,
    bcm_udf_id_t udf_id,
    int max_length,
    uint8* data,
    uint8* mask,
    int* actual_length) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_qualify_udf_get(",
          makeParamStr(
              unit,
              getCintVar(fieldEntryVars, eid),
              udf_id,
              max_length,
              *data,
              *mask,
              *actual_length),
          ")")));
  return 0;
}

void BcmCinter::bcm_field_group_config_t_init(
    bcm_field_group_config_t* /*group_config*/) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_group_config_t_init(",
          makeParamStr("group_config"),
          ")")));
}

int BcmCinter::bcm_port_phy_modify(
    int unit,
    bcm_port_t port,
    uint32 flags,
    uint32 phy_reg_addr,
    uint32 phy_data,
    uint32 phy_mask) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_phy_modify(",
          makeParamStr(unit, port, flags, phy_reg_addr, phy_data, phy_mask),
          ")")));
  return 0;
}

int BcmCinter::bcm_port_dtag_mode_set(int unit, bcm_port_t port, int mode) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_dtag_mode_set(", makeParamStr(unit, port, mode), ")")));
  return 0;
}

int BcmCinter::bcm_mirror_init(int unit) {
  writeCintLines(
      wrapFunc(to<string>("bcm_mirror_init(", makeParamStr(unit), ")")));
  return 0;
}

int BcmCinter::bcm_mirror_mode_set(int unit, int mode) {
  writeCintLines(wrapFunc(
      to<string>("bcm_mirror_mode_set(", makeParamStr(unit, mode), ")")));
  return 0;
}

int BcmCinter::bcm_mirror_destination_create(
    int unit,
    bcm_mirror_destination_t* mirror_dest) {
  auto cint = cintForMirrorDestination(mirror_dest);
  auto cintForFn = wrapFunc(
      to<string>(to<string>(
          "bcm_mirror_destination_create(",
          makeParamStr(unit, "&mirror_dest"),
          ")")));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  string destIdVar = getNextMirrorDestIdVar();
  mirrorDestIdVars.wlock()->emplace(mirror_dest->mirror_dest_id, destIdVar);
  cint.push_back(
      to<string>("bcm_gport_t ", destIdVar, " = mirror_dest.mirror_dest_id"));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_mirror_destination_destroy(
    int unit,
    bcm_gport_t mirror_dest_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_mirror_destination_destroy(",
          makeParamStr(unit, getCintVar(mirrorDestIdVars, mirror_dest_id)),
          ")")));
  return 0;
}

int BcmCinter::bcm_mirror_port_dest_add(
    int unit,
    bcm_port_t port,
    uint32 flags,
    bcm_gport_t mirror_dest_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_mirror_port_dest_add(",
          makeParamStr(
              unit, port, flags, getCintVar(mirrorDestIdVars, mirror_dest_id)),
          ")")));
  return 0;
}

int BcmCinter::bcm_mirror_port_dest_delete(
    int unit,
    bcm_port_t port,
    uint32 flags,
    bcm_gport_t mirror_dest_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_mirror_port_dest_delete(",
          makeParamStr(
              unit, port, flags, getCintVar(mirrorDestIdVars, mirror_dest_id)),
          ")")));
  return 0;
}

int BcmCinter::bcm_mirror_port_dest_delete_all(
    int unit,
    bcm_port_t port,
    uint32 flags) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_mirror_port_dest_delete_all(",
          makeParamStr(unit, port, flags),
          ")")));
  return 0;
}

int BcmCinter::bcm_l3_ecmp_member_add(
    int unit,
    bcm_if_t ecmp_group_id,
    bcm_l3_ecmp_member_t* ecmp_member) {
  vector<string> cintLines;
  cintLines.emplace_back("bcm_l3_ecmp_member_t_init(&member)");
  cintLines.push_back(
      to<string>(
          "member.egress_if=",
          getCintVar(l3IntfIdVars, ecmp_member->egress_if)));
  cintLines.push_back(to<string>("member.weight=", ecmp_member->weight));
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_ecmp_member_add(",
          makeParamStr(
              unit, getCintVar(l3IntfIdVars, ecmp_group_id), "&member"),
          ")"));
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cintLines));
  return 0;
}

int BcmCinter::bcm_l3_egress_ecmp_add(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    bcm_if_t intf) {
  auto cint = cintForL3Ecmp(*ecmp);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_egress_ecmp_add(",
          makeParamStr(unit, "&ecmp", getCintVar(l3IntfIdVars, intf)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_ecmp_member_delete(
    int unit,
    bcm_if_t ecmp_group_id,
    bcm_l3_ecmp_member_t* ecmp_member) {
  vector<string> cintLines;
  cintLines.emplace_back("bcm_l3_ecmp_member_t_init(&member)");
  cintLines.push_back(
      to<string>(
          "member.egress_if=",
          getCintVar(l3IntfIdVars, ecmp_member->egress_if)));
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_ecmp_member_delete(",
          makeParamStr(
              unit, getCintVar(l3IntfIdVars, ecmp_group_id), "&member"),
          ")"));
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cintLines));
  return 0;
}

int BcmCinter::bcm_l3_egress_ecmp_delete(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    bcm_if_t intf) {
  auto cint = cintForL3Ecmp(*ecmp);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_egress_ecmp_delete(",
          makeParamStr(unit, "&ecmp", getCintVar(l3IntfIdVars, intf)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_mpls_init(int unit) {
  writeCintLines(
      wrapFunc(to<string>("bcm_mpls_init(", makeParamStr(unit), ")")));
  return 0;
}

int BcmCinter::bcm_mpls_tunnel_switch_add(
    int unit,
    bcm_mpls_tunnel_switch_t* info) {
  auto cint = cintForMplsTunnelSwitch(*info);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_mpls_tunnel_switch_add(",
          makeParamStr(unit, "&switch_info"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_mpls_tunnel_switch_delete(
    int unit,
    bcm_mpls_tunnel_switch_t* info) {
  auto cint = cintForMplsTunnelSwitch(*info);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_mpls_tunnel_switch_delete(",
          makeParamStr(unit, "&switch_info"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_mpls_tunnel_initiator_set(
    int unit,
    bcm_if_t intf,
    int num_labels,
    bcm_mpls_egress_label_t* label_array) {
  auto cint = cintForLabelsArray(num_labels, label_array);
  cint.push_back(
      to<string>(
          "bcm_mpls_tunnel_initiator_set(",
          makeParamStr(
              unit,
              getCintVar(l3IntfIdVars, intf),
              num_labels,
              "label_array)")));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_mpls_tunnel_initiator_clear(int unit, bcm_if_t intf) {
  writeCintLine(
      to<string>(
          "bcm_mpls_tunnel_initiator_clear(",
          makeParamStr(unit, getCintVar(l3IntfIdVars, intf)),
          ")"));
  return 0;
}

int BcmCinter::bcm_l3_route_delete_by_interface(
    int unit,
    bcm_l3_route_t* l3_route) {
  auto cint = cintForL3Route(*l3_route);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_route_delete_by_interface(",
          makeParamStr(unit, "&l3_route"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_route_delete(int unit, bcm_l3_route_t* l3_route) {
  auto cint = cintForL3Route(*l3_route);
  auto cintForFn = wrapFunc(
      to<string>("bcm_l3_route_delete(", makeParamStr(unit, "&l3_route"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_route_add(int unit, bcm_l3_route_t* l3_route) {
  auto cint = cintForL3Route(*l3_route);
  auto cintForFn = wrapFunc(
      to<string>("bcm_l3_route_add(", makeParamStr(unit, "&l3_route"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_route_delete_all(int unit, bcm_l3_route_t* info) {
  auto cint = cintForL3Route(*info);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_route_delete_all(", makeParamStr(unit, "&l3_route"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_route_max_ecmp_set(int unit, int max) {
  writeCintLine(
      to<string>("bcm_l3_route_max_ecmp_set(", makeParamStr(unit, max), ")"));
  return 0;
}

int BcmCinter::bcm_l3_host_delete_by_interface(
    int unit,
    bcm_l3_host_t* l3_host) {
  auto cint = cintForL3Host(*l3_host);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_host_delete_by_interface(",
          makeParamStr(unit, "&l3_host"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_host_add(int unit, bcm_l3_host_t* l3_host) {
  auto cint = cintForL3Host(*l3_host);
  auto cintForFn = wrapFunc(
      to<string>("bcm_l3_host_add(", makeParamStr(unit, "&l3_host"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_host_delete(int unit, bcm_l3_host_t* l3_host) {
  auto cint = cintForL3Host(*l3_host);
  auto cintForFn = wrapFunc(
      to<string>("bcm_l3_host_delete(", makeParamStr(unit, "&l3_host"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_host_delete_all(int unit, bcm_l3_host_t* info) {
  auto cint = cintForL3Host(*info);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_host_delete_all(", makeParamStr(unit, "&l3_host"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_stat_custom_add(
    int unit,
    bcm_port_t port,
    bcm_stat_val_t type,
    bcm_custom_stat_trigger_t trigger) {
  auto cint = wrapFunc(
      to<string>(
          "bcm_stat_custom_add(",
          makeParamStr(unit, port, type, trigger),
          ")"));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_intf_create(int unit, bcm_l3_intf_t* l3_intf) {
  auto cint = cintForL3Intf(*l3_intf);
  auto cintForFn = wrapFunc(
      to<string>("bcm_l3_intf_create(", makeParamStr(unit, "&l3_intf"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_intf_delete(int unit, bcm_l3_intf_t* l3_intf) {
  auto cint = cintForL3Intf(*l3_intf);
  auto cintForFn = wrapFunc(
      to<string>("bcm_l3_intf_delete(", makeParamStr(unit, "&l3_intf"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

vector<string> BcmCinter::cintForL3Egress(const bcm_l3_egress_t* l3_egress) {
  vector<string> cintLines;
  string cintMac, macVar;
  tie(cintMac, macVar) = cintForMac(l3_egress->mac_addr);
  cintLines = {
      cintMac,
      "bcm_l3_egress_t_init(&l3_egress)",
      to<string>("l3_egress.flags = ", l3_egress->flags),
      to<string>("l3_egress.mac_addr = ", macVar),
      to<string>(
          "l3_egress.intf = ", getCintVar(l3IntfIdVars, l3_egress->intf)),
      to<string>("l3_egress.vlan = ", l3_egress->vlan),
      to<string>("l3_egress.module = ", l3_egress->module),
      to<string>("l3_egress.port = ", l3_egress->port),
      to<string>("l3_egress.trunk = ", l3_egress->trunk),
      to<string>("l3_egress.qos_map_id = ", l3_egress->qos_map_id),
      to<string>("l3_egress.mpls_qos_map_id = ", l3_egress->mpls_qos_map_id),
      to<string>("l3_egress.mpls_flags = ", l3_egress->mpls_flags),
      to<string>("l3_egress.mpls_label = ", l3_egress->mpls_label),
      to<string>(
          "l3_egress.dynamic_load_weight = ", l3_egress->dynamic_load_weight),
      to<string>(
          "l3_egress.dynamic_queue_size_weight = ",
          l3_egress->dynamic_queue_size_weight),
      to<string>(
          "l3_egress.dynamic_scaling_factor = ",
          l3_egress->dynamic_scaling_factor)};

  return cintLines;
}

int BcmCinter::bcm_l3_egress_create(
    int unit,
    uint32 flags,
    bcm_l3_egress_t* egr,
    bcm_if_t* if_id) {
  vector<string> cint =
      cintForL3Egress(reinterpret_cast<bcm_l3_egress_t*>(egr));
  if ((flags & BCM_L3_REPLACE) || (flags & BCM_L3_WITH_ID)) {
    auto funcCint = wrapFunc(
        to<string>(
            "bcm_l3_egress_create(",
            makeParamStr(
                unit,
                flags,
                "&l3_egress",
                to<string>("&", getCintVar(l3IntfIdVars, *if_id))),
            ")"));
    cint.insert(
        cint.end(),
        make_move_iterator(funcCint.begin()),
        make_move_iterator(funcCint.end()));
  } else {
    auto intfVar = getNextL3IntfIdVar();
    {
      l3IntfIdVars.wlock()->emplace(
          reinterpret_cast<bcm_if_t&>(*if_id), intfVar);
    }
    cint.push_back(to<string>("bcm_if_t ", intfVar));
    auto funcCint = wrapFunc(
        to<string>(
            "bcm_l3_egress_create(",
            makeParamStr(unit, flags, "&l3_egress", to<string>("&", intfVar)),
            ")"));
    cint.insert(
        cint.end(),
        make_move_iterator(funcCint.begin()),
        make_move_iterator(funcCint.end()));
  }
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_vlan_create(int unit, bcm_vlan_t vid) {
  writeCintLines(
      wrapFunc(to<string>("bcm_vlan_create(", makeParamStr(unit, vid), ")")));
  return 0;
}

pair<string, vector<string>> BcmCinter::cintForPortBitmap(bcm_pbmp_t pbmp) {
  auto pbmpVar = getNextTmpPortBitmapVar();
  vector<string> pbmpCint;
  for (int i = 0; i < FLAGS_pbmp_word_max; i++) {
    pbmpCint.push_back(to<string>("pbmp.pbits[", i, "] = ", pbmp.pbits[i]));
  }
  pbmpCint.push_back(to<string>("bcm_pbmp_t ", pbmpVar, " = (auto) pbmp"));
  return make_pair(pbmpVar, pbmpCint);
}

int BcmCinter::bcm_field_qualify_InPorts(
    int unit,
    bcm_field_entry_t entry,
    bcm_pbmp_t data,
    bcm_pbmp_t mask) {
  string pbmp, pbmpMask;
  vector<string> pbmpCint, pbmpMaskCint;
  tie(pbmp, pbmpCint) = cintForPortBitmap(data);
  tie(pbmpMask, pbmpMaskCint) = cintForPortBitmap(mask);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_field_qualify_InPorts(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry), pbmp, pbmpMask),
          ")"));
  vector<string> cint{};
  cint.insert(
      cint.end(),
      make_move_iterator(pbmpCint.begin()),
      make_move_iterator(pbmpCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(pbmpMaskCint.begin()),
      make_move_iterator(pbmpMaskCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_vlan_port_add(
    int unit,
    bcm_vlan_t vid,
    bcm_pbmp_t pbmp,
    bcm_pbmp_t ubmp) {
  string pbmpVar, ubmpVar;
  vector<string> pbmpCint, ubmpCint;
  tie(pbmpVar, pbmpCint) = cintForPortBitmap(pbmp);
  tie(ubmpVar, ubmpCint) = cintForPortBitmap(ubmp);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_vlan_port_add(",
          makeParamStr(unit, vid, pbmpVar, ubmpVar),
          ")"));
  vector<string> cint{};
  cint.insert(
      cint.end(),
      make_move_iterator(pbmpCint.begin()),
      make_move_iterator(pbmpCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(ubmpCint.begin()),
      make_move_iterator(ubmpCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_vlan_port_remove(int unit, bcm_vlan_t vid, bcm_pbmp_t pbmp) {
  string pbmpVar;
  vector<string> pbmpCint;
  tie(pbmpVar, pbmpCint) = cintForPortBitmap(pbmp);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_vlan_port_remove(", makeParamStr(unit, vid, pbmpVar), ")"));
  vector<string> cint{};
  cint.insert(
      cint.end(),
      make_move_iterator(pbmpCint.begin()),
      make_move_iterator(pbmpCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_port_vlan_member_set(
    int unit,
    bcm_port_t port,
    uint32 flags) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_vlan_member_set(", makeParamStr(unit, port, flags), ")")));
  return 0;
}

int BcmCinter::bcm_port_untagged_vlan_set(
    int unit,
    bcm_port_t port,
    bcm_vlan_t vid) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_untagged_vlan_set(", makeParamStr(unit, port, vid), ")")));
  return 0;
}

int BcmCinter::bcm_port_frame_max_set(int unit, bcm_port_t port, int size) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_frame_max_set(", makeParamStr(unit, port, size), ")")));
  return 0;
}

int BcmCinter::bcm_port_enable_set(int unit, bcm_port_t port, int enable) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_enable_set(", makeParamStr(unit, port, enable), ")")));
  return 0;
}

int BcmCinter::bcm_port_control_set(
    int unit,
    bcm_port_t port,
    bcm_port_control_t type,
    int value) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_control_set(",
          makeParamStr(unit, port, type, value),
          ")")));
  return 0;
}

int BcmCinter::bcm_port_interface_set(
    int unit,
    bcm_port_t port,
    bcm_port_if_t intf) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_interface_set(", makeParamStr(unit, port, intf), ")")));
  return 0;
}

int BcmCinter::bcm_l3_egress_destroy(int unit, bcm_if_t intf) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l3_egress_destroy(",
          makeParamStr(unit, getCintVar(l3IntfIdVars, intf)),
          ")")));
  {
    l3IntfIdVars.wlock()->erase(intf);
  }
  return 0;
}

int BcmCinter::bcm_field_range_create(
    int unit,
    bcm_field_range_t* range,
    uint32 flags,
    bcm_l4_port_t min,
    bcm_l4_port_t max) {
  string rangeVar = getNextRangeVar();
  {
    rangeIdVars.wlock()->emplace(*range, rangeVar);
  }
  vector<string> cint = {to<string>("bcm_field_range_t ", rangeVar)};
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_field_range_create(",
          makeParamStr(unit, to<string>("&", rangeVar), flags, min, max),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_range_destroy(int unit, bcm_field_range_t range) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_range_destroy(",
          makeParamStr(unit, getCintVar(rangeIdVars, range)),
          ")")));
  {
    rangeIdVars.wlock()->erase(range);
  }
  return 0;
}

int BcmCinter::bcm_qos_map_create(int unit, uint32 flags, int* map_id) {
  string qosMapVar = getNextQosMapVar();
  {
    qosMapIdVars.wlock()->emplace(*map_id, qosMapVar);
  }
  vector<string> cint = {to<string>("int ", qosMapVar)};
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_qos_map_create(",
          makeParamStr(unit, flags, to<string>("&", qosMapVar)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_qos_map_destroy(int unit, int map_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_qos_map_destroy(",
          makeParamStr(unit, getCintVar(qosMapIdVars, map_id)),
          ")")));
  {
    qosMapIdVars.wlock()->erase(map_id);
  }
  return 0;
}

vector<std::string> BcmCinter::cintForQosMapEntry(
    uint32 flags,
    const bcm_qos_map_t* qosMapEntry) const {
  if (flags & BCM_QOS_MAP_L3) {
    return {
        "bcm_qos_map_t_init(&qos_map_entry)",
        to<string>("qos_map_entry.dscp = ", qosMapEntry->dscp),
        to<string>("qos_map_entry.int_pri = ", qosMapEntry->int_pri),
    };
  } else {
    return {
        "bcm_qos_map_t_init(&qos_map_entry)",
        to<string>("qos_map_entry.exp = ", qosMapEntry->exp),
        to<string>("qos_map_entry.int_pri = ", qosMapEntry->int_pri),
    };
  }
}

int BcmCinter::bcm_qos_map_add(
    int unit,
    uint32 flags,
    bcm_qos_map_t* map,
    int map_id) {
  auto cint = cintForQosMapEntry(flags, map);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_qos_map_add(",
          makeParamStr(
              unit, flags, "&qos_map_entry", getCintVar(qosMapIdVars, map_id)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_qos_map_delete(
    int unit,
    uint32 flags,
    bcm_qos_map_t* map,
    int map_id) {
  auto cint = cintForQosMapEntry(flags, map);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_qos_map_delete(",
          makeParamStr(
              unit, flags, "&qos_map_entry", getCintVar(qosMapIdVars, map_id)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_qos_port_map_set(
    int unit,
    bcm_gport_t gport,
    int ing_map,
    int egr_map) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_qos_port_map_set(",
          makeParamStr(
              unit,
              gport,
              getCintVar(qosMapIdVars, ing_map),
              getCintVar(qosMapIdVars, egr_map)),
          ")")));
  return 0;
}

int BcmCinter::bcm_port_dscp_map_mode_set(int unit, bcm_port_t port, int mode) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_dscp_map_mode_set(", makeParamStr(unit, port, mode), ")")));
  return 0;
}

int BcmCinter::bcm_l3_egress_ecmp_ethertype_set(
    int unit,
    uint32 flags,
    int ethertype_count,
    int* ethertype_array) {
  std::vector<string> argValues;
  string arrayVarName = getNextEthertypeVar();
  for (int i = 0; i < ethertype_count; ++i) {
    argValues.push_back(to<string>(ethertype_array[i]));
  }
  string argVarArray = to<string>(
      "int ", arrayVarName, "[] =", " {", join(", ", argValues), "}");
  vector<string> cint = {argVarArray};

  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_egress_ecmp_ethertype_set(",
          makeParamStr(unit, flags, ethertype_count, arrayVarName),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_egress_ecmp_ethertype_get(
    int unit,
    uint32* flags,
    int ethertype_max,
    int* ethertype_array,
    int* ethertype_count) {
  std::vector<string> argValues;
  string arrayVarName = getNextEthertypeVar();
  for (int i = 0; i < *ethertype_count; ++i) {
    argValues.push_back(to<string>(ethertype_array[i]));
  }
  string argVarArray = to<string>(
      "int ", arrayVarName, "[] =", " {", join(", ", argValues), "}");
  vector<string> cint = {argVarArray};

  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_egress_ecmp_ethertype_get(",
          makeParamStr(
              unit, *flags, ethertype_max, arrayVarName, *ethertype_count),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_egress_ecmp_member_status_set(
    int unit,
    bcm_if_t intf,
    int status) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l3_egress_ecmp_member_status_set(",
          makeParamStr(unit, intf, status),
          ")")));
  return 0;
}

int BcmCinter::bcm_l3_egress_ecmp_member_status_get(
    int unit,
    bcm_if_t intf,
    int* status) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l3_egress_ecmp_member_status_get(",
          makeParamStr(unit, intf, *status),
          ")")));
  return 0;
}

int BcmCinter::bcm_l3_ecmp_create(
    int unit,
    uint32 options,
    bcm_l3_egress_ecmp_t* ecmp_info,
    int ecmp_member_count,
    bcm_l3_ecmp_member_t* ecmp_member_array) {
  auto cint =
      cintForL3Ecmp(reinterpret_cast<const bcm_l3_egress_ecmp_t&>(*ecmp_info));
  auto cintForMembers =
      cintForEcmpMembersArray(ecmp_member_array, ecmp_member_count);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_ecmp_create(",
          makeParamStr(
              unit, options, "&ecmp", ecmp_member_count, "&member_array[0]"),
          ")"));
  auto l3IntfIdVar = getNextL3IntfIdVar();
  {
    l3IntfIdVars.wlock()->emplace(ecmp_info->ecmp_intf, l3IntfIdVar);
  }
  cint.push_back(
      to<string>("bcm_if_t ", l3IntfIdVar, " = ", ecmp_info->ecmp_intf));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForMembers.begin()),
      make_move_iterator(cintForMembers.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

std::vector<std::string> BcmCinter::cintForEcmpMembersArray(
    const bcm_l3_ecmp_member_t* members,
    int member_count) {
  vector<string> cintLines;
  for (auto i = 0; i < member_count; i++) {
    cintLines.push_back(
        to<string>("bcm_l3_ecmp_member_t_init(&member_array[", i, "])"));
    cintLines.push_back(
        to<string>(
            "member_array[",
            i,
            "].egress_if=",
            getCintVar(l3IntfIdVars, members[i].egress_if)));
    cintLines.push_back(
        to<string>("member_array[", i, "].weight=", members[i].weight));
  }
  return cintLines;
}

int BcmCinter::bcm_l3_egress_ecmp_create(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    int intf_count,
    bcm_if_t* intf_array) {
  auto cint =
      cintForL3Ecmp(reinterpret_cast<const bcm_l3_egress_ecmp_t&>(*ecmp));
  auto cintForPaths = cintForPathsArray(intf_array, intf_count);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_egress_ecmp_create(",
          makeParamStr(unit, "&ecmp", intf_count, "&intf_array[0]"),
          ")"));
  auto l3IntfIdVar = getNextL3IntfIdVar();
  {
    l3IntfIdVars.wlock()->emplace(ecmp->ecmp_intf, l3IntfIdVar);
  }
  cint.push_back(to<string>("bcm_if_t ", l3IntfIdVar, " = ", ecmp->ecmp_intf));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForPaths.begin()),
      make_move_iterator(cintForPaths.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

std::vector<std::string> BcmCinter::cintForDlbPortQualityAttr(
    bcm_l3_ecmp_dlb_port_quality_attr_t quality_attr) {
  return {
      "bcm_l3_ecmp_dlb_port_quality_attr_t_init(&quality_attr)",
      to<string>("quality_attr.load_weight = ", quality_attr.load_weight),
      to<string>(
          "quality_attr.queue_size_weight = ", quality_attr.queue_size_weight),
      to<string>(
          "quality_attr.scaling_factor = ", quality_attr.scaling_factor)};
}

int BcmCinter::bcm_l3_ecmp_dlb_port_quality_attr_set(
    int unit,
    bcm_port_t port,
    bcm_l3_ecmp_dlb_port_quality_attr_t* quality_attr) {
  writeCintLines(cintForDlbPortQualityAttr(*quality_attr));
  writeCintLines(wrapFunc(
      fmt::format(
          "bcm_l3_ecmp_dlb_port_quality_attr_set({}, {}, &quality_attr)",
          unit,
          port)));
  return 0;
}

int BcmCinter::bcm_l3_ecmp_dlb_port_quality_attr_get(
    int unit,
    bcm_port_t port,
    bcm_l3_ecmp_dlb_port_quality_attr_t* quality_attr) {
  writeCintLines(cintForDlbPortQualityAttr(*quality_attr));
  writeCintLines(wrapFunc(
      fmt::format(
          "bcm_l3_ecmp_dlb_port_quality_attr_get({}, {}, &quality_attr)",
          unit,
          port)));
  return 0;
}

std::vector<std::string> BcmCinter::cintForPathsArray(
    const bcm_if_t* paths,
    int path_count) {
  vector<string> cintLines;
  for (auto i = 0; i < path_count; i++) {
    cintLines.push_back(
        to<string>("intf_array[", i, "]=", getCintVar(l3IntfIdVars, paths[i])));
  }
  return cintLines;
}

std::pair<std::string, std::vector<std::string>> BcmCinter::cintForReasons(
    bcm_rx_reasons_t reasons) {
  auto reasonsVar = getNextTmpReasonsVar();
  vector<string> reasonsCint = {};
  for (int i = 0; i < sizeof(_shr_rx_reasons_t) / sizeof(SHR_BITDCL); i++) {
    reasonsCint.push_back(
        to<string>("reasons.pbits[", i, "] = ", reasons.pbits[i]));
  }
  reasonsCint.push_back(
      to<string>("bcm_rx_reasons_t ", reasonsVar, " = (auto) reasons"));
  return make_pair(reasonsVar, reasonsCint);
}

int BcmCinter::bcm_rx_cosq_mapping_set(
    int unit,
    int index,
    bcm_rx_reasons_t reasons,
    bcm_rx_reasons_t reasons_mask,
    uint8 int_prio,
    uint8 int_prio_mask,
    uint32 packet_type,
    uint32 packet_type_mask,
    bcm_cos_queue_t cosq) {
  string reasonsVar, reasonsMaskVar;
  vector<string> cint, reasonsCint, reasonsMaskCint;
  tie(reasonsVar, reasonsCint) = cintForReasons(reasons);
  tie(reasonsMaskVar, reasonsMaskCint) = cintForReasons(reasons_mask);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_rx_cosq_mapping_set(",
          makeParamStr(
              unit,
              index,
              reasonsVar,
              reasonsMaskVar,
              int_prio,
              int_prio_mask,
              packet_type,
              packet_type_mask,
              cosq),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(reasonsCint.begin()),
      make_move_iterator(reasonsCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(reasonsMaskCint.begin()),
      make_move_iterator(reasonsMaskCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_rx_cosq_mapping_delete(int unit, int index) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_rx_cosq_mapping_delete(", makeParamStr(unit, index), ")")));
  return 0;
}

vector<string> BcmCinter::cintForRxCosqMapping(bcm_rx_cosq_mapping_t* cosqMap) {
  string reasonsVar, reasonsMaskVar;
  vector<string> reasonsCint, reasonsMaskCint;

  tie(reasonsVar, reasonsCint) = cintForReasons(cosqMap->reasons);
  tie(reasonsMaskVar, reasonsMaskCint) = cintForReasons(cosqMap->reasons_mask);
  vector<string> funcCint = {
      "bcm_rx_cosq_mapping_t_init(&cosqMap)",
      to<string>("cosqMap.flags = ", cosqMap->flags),
      to<string>("cosqMap.reasons = ", reasonsVar),
      to<string>("cosqMap.reasons_mask = ", reasonsMaskVar),
      to<string>("cosqMap.index = ", cosqMap->index),
      to<string>("cosqMap.int_prio = ", cosqMap->int_prio),
      to<string>("cosqMap.int_prio_mask = ", cosqMap->int_prio_mask),
      to<string>("cosqMap.packet_type = ", cosqMap->packet_type),
      to<string>("cosqMap.packet_type_mask = ", cosqMap->packet_type_mask),
      to<string>("cosqMap.cosq = ", cosqMap->cosq),
      to<string>("cosqMap.flex_key1 = ", cosqMap->flex_key1),
      to<string>("cosqMap.flex_key1_mask = ", cosqMap->flex_key1_mask),
      to<string>("cosqMap.flex_key2 = ", cosqMap->flex_key2),
      to<string>("cosqMap.flex_key2_mask = ", cosqMap->flex_key2_mask),
      to<string>("cosqMap.drop_event = ", cosqMap->drop_event),
      to<string>("cosqMap.drop_event_mask = ", cosqMap->drop_event_mask),
      to<string>("cosqMap.priority = ", cosqMap->priority),
  };

  vector<string> cint;
  cint.insert(
      cint.end(),
      make_move_iterator(reasonsCint.begin()),
      make_move_iterator(reasonsCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(reasonsMaskCint.begin()),
      make_move_iterator(reasonsMaskCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  return cint;
}

int BcmCinter::bcm_rx_cosq_mapping_extended_add(
    int unit,
    int options,
    bcm_rx_cosq_mapping_t* cosqMap) {
  vector<string> cint;
  vector<string> RxCosqMappingCint = cintForRxCosqMapping(cosqMap);
  vector<string> funcCint = wrapFunc(
      to<string>(
          "bcm_rx_cosq_mapping_extended_add(",
          makeParamStr(unit, options, "&cosqMap"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(RxCosqMappingCint.begin()),
      make_move_iterator(RxCosqMappingCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));

  writeCintLines(cint);
  return 0;
}

int BcmCinter::bcm_rx_cosq_mapping_extended_delete(
    int unit,
    bcm_rx_cosq_mapping_t* cosqMap) {
  vector<string> cint;
  vector<string> RxCosqMappingCint = cintForRxCosqMapping(cosqMap);
  vector<string> funcCint = wrapFunc(
      to<string>(
          "bcm_rx_cosq_mapping_extended_delete(",
          makeParamStr(unit, "&cosqMap"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(RxCosqMappingCint.begin()),
      make_move_iterator(RxCosqMappingCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));

  writeCintLines(cint);
  return 0;
}

int BcmCinter::bcm_rx_cosq_mapping_extended_set(
    int unit,
    uint32 options,
    bcm_rx_cosq_mapping_t* cosqMap) {
  vector<string> cint;

  vector<string> cosqMapCint = cintForRxCosqMapping(cosqMap);
  vector<string> funcCint = wrapFunc(
      to<string>(
          "bcm_rx_cosq_mapping_extended_set(",
          makeParamStr(unit, options, "&cosqMap"),
          ")"));

  cint.insert(
      cint.end(),
      make_move_iterator(cosqMapCint.begin()),
      make_move_iterator(cosqMapCint.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));

  writeCintLines(cint);
  return 0;
}

int BcmCinter::bcm_pkt_alloc(
    int /*unit*/,
    int /*size*/,
    uint32 /*flags*/,
    bcm_pkt_t** /*pkt_buf*/) {
  return 0;
}

int BcmCinter::bcm_tx(int unit, bcm_pkt_t* tx_pkt, void* /*cookie*/) {
  if (!FLAGS_gen_tx_cint) {
    return 0;
  }
  vector<string> cint{};
  string pbmpVar, ubmpVar;
  vector<string> pbmpCint, ubmpCint;

  auto allocFuncCint = wrapFunc(
      to<string>(
          "bcm_pkt_alloc(",
          makeParamStr(unit, tx_pkt->pkt_data->len, tx_pkt->flags, "&pkt"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(allocFuncCint.begin()),
      make_move_iterator(allocFuncCint.end()));

  tie(pbmpVar, pbmpCint) = cintForPortBitmap(tx_pkt->tx_pbmp);
  cint.insert(
      cint.end(),
      make_move_iterator(pbmpCint.begin()),
      make_move_iterator(pbmpCint.end()));
  cint.push_back(to<string>("pkt->tx_pbmp = ", pbmpVar));

  tie(ubmpVar, ubmpCint) = cintForPortBitmap(tx_pkt->tx_upbmp);
  cint.insert(
      cint.end(),
      make_move_iterator(ubmpCint.begin()),
      make_move_iterator(ubmpCint.end()));
  cint.push_back(to<string>("pkt->tx_upbmp = ", ubmpVar));

  for (int i = 0; i < tx_pkt->pkt_data->len; i++) {
    cint.push_back(
        to<string>(
            "pkt->pkt_data->data[", i, "] = ", tx_pkt->pkt_data->data[i]));
  }

  auto txFuncCint =
      wrapFunc(to<string>("bcm_tx(", makeParamStr(unit, "pkt", "NULL"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(txFuncCint.begin()),
      make_move_iterator(txFuncCint.end()));

  auto freeFuncCint =
      wrapFunc(to<string>("bcm_pkt_free(", makeParamStr(unit, "pkt"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(freeFuncCint.begin()),
      make_move_iterator(freeFuncCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_pktio_tx(int unit, bcm_pktio_pkt_t* tx_pkt) {
#ifdef INCLUDE_PKTIO
  if (!FLAGS_gen_tx_cint) {
    return 0;
  }

  vector<string> cint{};
  void* data;
  uint32_t length;

  bcm_pktio_pkt_data_get(unit, tx_pkt, &data, &length);

  cint.emplace_back("pktio_pkt = NULL");
  auto allocFuncCint = wrapFunc(
      to<string>(
          "bcm_pktio_alloc(",
          makeParamStr(unit, length, 0, "&pktio_pkt"),
          ")"));

  cint.insert(
      cint.end(),
      make_move_iterator(allocFuncCint.begin()),
      make_move_iterator(allocFuncCint.end()));

  cint.push_back(to<string>("pktio_pkt->flags = ", tx_pkt->flags));

  for (int i = 0; i < BCM_PKTIO_PMD_SIZE_WORDS; i++) {
    cint.push_back(
        to<string>("pktio_pkt->pmd.data[", i, "] = ", tx_pkt->pmd.data[i]));
  }

  auto putFuncCint = wrapFunc(
      to<string>(
          "bcm_pktio_put(",
          makeParamStr(unit, "pktio_pkt", length, "(auto) &pktio_data"),
          ")"));

  cint.insert(
      cint.end(),
      make_move_iterator(putFuncCint.begin()),
      make_move_iterator(putFuncCint.end()));

  cint.push_back(to<string>("pktio_bytes = (auto) pktio_data"));

  for (int i = 0; i < length; i++) {
    cint.push_back(
        to<string>("pktio_bytes[", i, "] = ", static_cast<uint8_t*>(data)[i]));
  }

  auto txFuncCint = wrapFunc(
      to<string>("bcm_pktio_tx(", makeParamStr(unit, "pktio_pkt"), ")"));

  cint.insert(
      cint.end(),
      make_move_iterator(txFuncCint.begin()),
      make_move_iterator(txFuncCint.end()));

  auto freeFuncCint = wrapFunc(
      to<string>("bcm_pktio_free(", makeParamStr(unit, "pktio_pkt"), ")"));

  cint.insert(
      cint.end(),
      make_move_iterator(freeFuncCint.begin()),
      make_move_iterator(freeFuncCint.end()));

  writeCintLines(std::move(cint));
#else
  (void)unit;
  (void)tx_pkt;
#endif
  return 0;
}

int BcmCinter::bcm_pktio_txpmd_stat_attach(int unit, uint32 counter_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_pktio_txpmd_stat_attach(",
          makeParamStr(unit, counter_id),
          ")")));
  return 0;
}

int BcmCinter::bcm_pktio_txpmd_stat_detach(int unit) {
  writeCintLines(wrapFunc(
      to<string>("bcm_pktio_txpmd_stat_detach(", makeParamStr(unit), ")")));
  return 0;
}

int BcmCinter::bcm_pkt_free(int /*unit*/, bcm_pkt_t* /*pkt*/) {
  return 0;
}

int BcmCinter::bcm_port_stat_enable_set(
    int unit,
    bcm_gport_t port,
    int enable) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_stat_enable_set(", makeParamStr(unit, port, enable), ")")));
  return 0;
}

int BcmCinter::bcm_port_stat_attach(
    int unit,
    bcm_port_t port,
    uint32 counterID_) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_stat_attach(", makeParamStr(unit, port, counterID_), ")")));
  return 0;
}

int BcmCinter::bcm_port_stat_detach_with_id(
    int unit,
    bcm_gport_t gPort,
    uint32 counterID) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_stat_detach_with_id(",
          makeParamStr(unit, gPort, counterID),
          ")")));
  return 0;
}

int BcmCinter::bcm_stat_clear(int unit, bcm_port_t port) {
  writeCintLines(
      wrapFunc(to<string>("bcm_stat_clear(", makeParamStr(unit, port), ")")));
  return 0;
}

int BcmCinter::bcm_field_hints_create(
    int unit,
    bcm_field_hintid_t* /* hint_id */) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_hints_create(", makeParamStr(unit, "&hint_id"), ")")));
  return 0;
}

std::vector<std::string> BcmCinter::cintForHint(bcm_field_hint_t hint) {
  return {
      "bcm_field_hint_t_init(&hint)",
      to<string>("hint.hint_type = ", hint.hint_type),
      to<string>("hint.qual = ", hint.qual),
      to<string>("hint.start_bit = ", hint.start_bit),
      to<string>("hint.end_bit = ", hint.end_bit)};
}

int BcmCinter::bcm_field_hints_add(
    int unit,
    bcm_field_hintid_t hint_id,
    bcm_field_hint_t* hint) {
  writeCintLines(cintForHint(*hint));
  writeCintLines(wrapFunc(
      fmt::format("bcm_field_hints_add({}, {}, &hint)", unit, hint_id)));
  return 0;
}

int BcmCinter::bcm_field_hints_destroy(int unit, bcm_field_hintid_t hint_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_field_hints_destroy(", makeParamStr(unit, hint_id), ")")));
  return 0;
}

int BcmCinter::bcm_field_hints_get(
    int unit,
    bcm_field_hintid_t hint_id,
    bcm_field_hint_t* hint) {
  writeCintLines(cintForHint(*hint));
  writeCintLines(wrapFunc(
      fmt::format("bcm_field_hints_get({}, {}, &hint)", unit, hint_id)));
  return 0;
}

std::vector<std::string> BcmCinter::cintForPortFdrConfig(
    bcm_port_fdr_config_t fdr_config) {
  return {
      "bcm_port_fdr_config_t_init(&fdr_config)",
      to<string>("fdr_config.fdr_enable = ", fdr_config.fdr_enable),
      to<string>("fdr_config.window_size = ", fdr_config.window_size),
      to<string>(
          "fdr_config.symb_err_threshold = ", fdr_config.symb_err_threshold),
      to<string>(
          "fdr_config.symb_err_stats_sel = ", fdr_config.symb_err_stats_sel),
      to<string>("fdr_config.intr_enable = ", fdr_config.intr_enable)};
}

int BcmCinter::bcm_port_fdr_config_set(
    int unit,
    bcm_port_t port,
    bcm_port_fdr_config_t* fdr_config) {
  if (FLAGS_enable_fdr_cinter) {
    writeCintLines(cintForPortFdrConfig(*fdr_config));
    writeCintLines(wrapFunc(
        fmt::format(
            "bcm_port_fdr_config_set({}, {}, &fdr_config)", unit, port)));
  }
  return 0;
}

int BcmCinter::bcm_cosq_bst_stat_clear(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_cosq_bst_stat_clear(",
          makeParamStr(unit, gport, cosq, bid),
          ")")));
  return 0;
}

int BcmCinter::bcm_vlan_control_port_set(
    int unit,
    int port,
    bcm_vlan_control_port_t type,
    int arg) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_vlan_control_port_set(",
          makeParamStr(unit, port, type, arg),
          ")")));
  return 0;
}

int BcmCinter::bcm_cosq_mapping_set(
    int unit,
    bcm_cos_t priority,
    bcm_cos_queue_t cosq) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_cosq_mapping_set(", makeParamStr(unit, priority, cosq), ")")));
  return 0;
}

int BcmCinter::bcm_switch_control_set(
    int unit,
    bcm_switch_control_t type,
    int arg) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_switch_control_set(", makeParamStr(unit, type, arg), ")")));
  return 0;
}

int BcmCinter::bcm_l2_age_timer_set(int unit, int age_seconds) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l2_age_timer_set(", makeParamStr(unit, age_seconds), ")")));
  return 0;
}

int BcmCinter::bcm_port_pause_set(
    int unit,
    bcm_port_t port,
    int pause_tx,
    int pause_rx) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_pause_set(",
          makeParamStr(unit, port, pause_tx, pause_rx),
          ")")));
  return 0;
}

int BcmCinter::bcm_port_sample_rate_set(
    int unit,
    bcm_port_t port,
    int ingress_rate,
    int egress_rate) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_sample_rate_set(",
          makeParamStr(unit, port, ingress_rate, egress_rate),
          ")")));
  return 0;
}

vector<string> BcmCinter::cintForL2Station(bcm_l2_station_t* station) {
  string cintMac, macVar, cintMacMask, macMaskVar;
  vector<string> cintLines;
  tie(cintMac, macVar) = cintForMac(station->dst_mac);
  tie(cintMacMask, macMaskVar) = cintForMac(station->dst_mac_mask);
  cintLines.push_back(cintMac);
  cintLines.push_back(cintMacMask);
  vector<string> l2StationCint = {
      "bcm_l2_station_t_init(&station)",
      to<string>("station.flags = ", station->flags),
      to<string>("station.dst_mac = ", macVar),
      to<string>("station.dst_mac_mask = ", macMaskVar),
      to<string>("station.vlan = ", station->vlan),
      to<string>("station.vlan_mask = ", station->vlan_mask),
      to<string>("station.src_port = ", station->src_port),
      to<string>("station.src_port_mask = ", station->src_port_mask),
  };
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(l2StationCint.begin()),
      make_move_iterator(l2StationCint.end()));
  return cintLines;
}

int BcmCinter::bcm_l2_station_add(
    int unit,
    int* station_id,
    bcm_l2_station_t* station) {
  vector<string> cintLines;
  auto l2StationCint = cintForL2Station(station);
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(l2StationCint.begin()),
      make_move_iterator(l2StationCint.end()));
  cintLines.push_back(
      to<string>("station.flags = ", station->flags | BCM_L2_STATION_WITH_ID));
  cintLines.push_back(to<string>("station_id = ", *station_id));
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_l2_station_add(",
          makeParamStr(unit, "&station_id", "&station"),
          ")"));
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(cintLines);
  return 0;
}

int BcmCinter::bcm_vlan_default_set(int unit, bcm_vlan_t vid) {
  writeCintLines(wrapFunc(
      to<string>("bcm_vlan_default_set(", makeParamStr(unit, vid), ")")));
  return 0;
}

int BcmCinter::bcm_port_phy_tx_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_tx_t* tx) {
  vector<string> cintLines;
  cintLines.push_back(to<string>("bcm_port_phy_tx_t_init(&tx)"));
  cintLines.push_back(to<string>("tx.pre2 = ", tx->pre2));
  cintLines.push_back(to<string>("tx.pre = ", tx->pre));
  cintLines.push_back(to<string>("tx.main = ", tx->main));
  cintLines.push_back(to<string>("tx.post = ", tx->post));
  cintLines.push_back(to<string>("tx.post2 = ", tx->post2));
  cintLines.push_back(to<string>("tx.post3 = ", tx->post3));
  cintLines.push_back(to<string>("tx.tx_tap_mode = ", tx->tx_tap_mode));
  cintLines.push_back(to<string>("tx.signalling_mode = ", tx->signalling_mode));

  auto funcCint = wrapFunc(
      to<string>("bcm_port_phy_tx_set(", makeParamStr(unit, port, "&tx"), ")"));
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(cintLines);
  return 0;
}

int BcmCinter::bcm_port_phy_control_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_control_t type,
    uint32 value) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_phy_control_set(",
          makeParamStr(unit, port, type, value),
          ")")));
  return 0;
}

int BcmCinter::bcm_port_speed_set(int unit, bcm_port_t port, int speed) {
  writeCintLines(wrapFunc(
      to<string>("bcm_port_speed_set(", makeParamStr(unit, port, speed), ")")));
  return 0;
}

int BcmCinter::bcm_l3_ecmp_destroy(int unit, bcm_if_t ecmp_group_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l3_ecmp_destroy(",
          makeParamStr(unit, getCintVar(l3IntfIdVars, ecmp_group_id)),
          ")")));
  {
    l3IntfIdVars.wlock()->erase(ecmp_group_id);
  }
  return 0;
}

int BcmCinter::bcm_l3_egress_ecmp_destroy(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp) {
  vector<string> cint =
      cintForL3Ecmp(reinterpret_cast<bcm_l3_egress_ecmp_t&>(*ecmp));
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_l3_egress_ecmp_destroy(", makeParamStr(unit, "&ecmp"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  {
    l3IntfIdVars.wlock()->erase(ecmp->ecmp_intf);
  }
  return 0;
}

int BcmCinter::bcm_stg_create(int unit, bcm_stg_t* stg_ptr) {
  string stgVar = getNextStgVar();
  {
    stgIdVars.wlock()->emplace(*stg_ptr, stgVar);
  }
  vector<string> cint = {to<string>("bcm_stg_t ", stgVar)};
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_stg_create(", makeParamStr(unit, to<string>("&", stgVar)), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_stg_default_set(int unit, bcm_stg_t stg) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_stg_default_set(",
          makeParamStr(unit, getCintVar(stgIdVars, stg)),
          ")")));
  return 0;
}

int BcmCinter::bcm_stg_destroy(int unit, bcm_stg_t stg) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_stg_destroy(",
          makeParamStr(unit, getCintVar(stgIdVars, stg)),
          ")")));
  {
    stgIdVars.wlock()->erase(stg);
  }
  return 0;
}

int BcmCinter::bcm_stg_stp_set(
    int unit,
    bcm_stg_t stg,
    bcm_port_t port,
    int stp_state) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_stg_stp_set(", makeParamStr(unit, stg, port, stp_state), ")")));
  return 0;
}

int BcmCinter::bcm_stg_vlan_add(int unit, bcm_stg_t stg, bcm_vlan_t vid) {
  writeCintLines(wrapFunc(
      to<string>("bcm_stg_vlan_add(", makeParamStr(unit, stg, vid), ")")));
  return 0;
}

int BcmCinter::bcm_port_learn_set(int unit, bcm_port_t port, uint32 flags) {
  writeCintLines(wrapFunc(
      to<string>("bcm_port_learn_set(", makeParamStr(unit, port, flags), ")")));
  return 0;
}

int BcmCinter::bcm_vlan_gport_delete_all(int unit, bcm_vlan_t vlan) {
  writeCintLines(wrapFunc(
      to<string>("bcm_vlan_gport_delete_all(", makeParamStr(unit, vlan), ")")));
  return 0;
}

int BcmCinter::bcm_l3_egress_multipath_create(
    int unit,
    uint32 flags,
    int intf_count,
    bcm_if_t* intf_array,
    bcm_if_t* mpintf) {
  vector<string> cint{};
  auto l3IntfIdVar = getNextL3IntfIdVar();
  {
    l3IntfIdVars.wlock()->emplace(
        reinterpret_cast<bcm_if_t&>(*mpintf), l3IntfIdVar);
  }
  cint.push_back(to<string>("bcm_if_t ", l3IntfIdVar));
  auto cintForPaths = cintForPathsArray(intf_array, intf_count);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_l3_egress_multipath_create(",
          makeParamStr(
              unit,
              flags,
              intf_count,
              "&intf_array[0]",
              to<string>("&", l3IntfIdVar)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForPaths.begin()),
      make_move_iterator(cintForPaths.end()));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_egress_multipath_add(
    int unit,
    bcm_if_t mpintf,
    bcm_if_t intf) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l3_egress_multipath_add(",
          makeParamStr(
              unit,
              getCintVar(l3IntfIdVars, mpintf),
              getCintVar(l3IntfIdVars, intf)),
          ")")));
  return 0;
}

int BcmCinter::bcm_l3_egress_multipath_delete(
    int unit,
    bcm_if_t mpintf,
    bcm_if_t intf) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l3_egress_multipath_delete(",
          makeParamStr(
              unit,
              getCintVar(l3IntfIdVars, mpintf),
              getCintVar(l3IntfIdVars, intf)),
          ")")));
  return 0;
}

int BcmCinter::bcm_l3_egress_multipath_destroy(int unit, bcm_if_t mpintf) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l3_egress_multipath_destroy(",
          makeParamStr(unit, getCintVar(l3IntfIdVars, mpintf)),
          ")")));
  {
    l3IntfIdVars.wlock()->erase(mpintf);
  }
  return 0;
}

int BcmCinter::bcm_port_pause_sym_set(int unit, bcm_port_t port, int pause) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_pause_sym_set(", makeParamStr(unit, port, pause), ")")));
  return 0;
}

int BcmCinter::bcm_vlan_destroy_all(int unit) {
  writeCintLines(
      wrapFunc(to<string>("bcm_vlan_destroy_all(", makeParamStr(unit), ")")));
  return 0;
}

int BcmCinter::bcm_vlan_destroy(int unit, bcm_vlan_t vid) {
  writeCintLines(
      wrapFunc(to<string>("bcm_vlan_destroy(", makeParamStr(unit, vid), ")")));
  return 0;
}

int BcmCinter::bcm_l2_station_delete(int unit, int station_id) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l2_station_delete(",
          makeParamStr(unit, getCintVar(l2StationIdVars, station_id)),
          ")")));
  {
    l2StationIdVars.wlock()->erase(station_id);
  }
  return 0;
}

vector<string> BcmCinter::cintForCosqBstProfile(
    bcm_cosq_bst_profile_t* profile) {
  return {
      "bcm_cosq_bst_profile_t_init(&cosq_bst_profile)",
      to<string>("cosq_bst_profile.byte = ", profile->byte)};
}

int BcmCinter::bcm_cosq_bst_profile_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    bcm_cosq_bst_profile_t* profile) {
  auto cint = cintForCosqBstProfile(profile);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_cosq_bst_profile_set(",
          makeParamStr(unit, gport, cosq, bid, "&cosq_bst_profile"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

// BCM port resource APIs
string BcmCinter::cintForInitPortResource(string name) {
  return to<string>("bcm_port_resource_t_init(&", name, ")");
}

vector<string> BcmCinter::cintForPortResource(
    string name,
    bcm_port_resource_t* resource) {
  return {
      cintForInitPortResource(name),
      to<string>(name, ".flags = ", resource->flags),
      to<string>(name, ".port = ", resource->port),
      to<string>(name, ".physical_port = ", resource->physical_port),
      to<string>(name, ".speed = ", resource->speed),
      to<string>(name, ".lanes = ", resource->lanes),
      to<string>(name, ".encap = ", resource->encap),
      to<string>(name, ".fec_type = ", resource->fec_type),
      to<string>(name, ".phy_lane_config = ", resource->phy_lane_config),
      to<string>(name, ".link_training = ", resource->link_training),
      to<string>(name, ".roe_compression = ", resource->roe_compression),
  };
}

vector<string> BcmCinter::cintForPortResources(
    int nport,
    bcm_port_resource_t* resource) {
  vector<string> output;
  for (int i = 0; i < nport; i++) {
    auto resourceCint =
        cintForPortResource(to<string>("resources[", i, "]"), &resource[i]);
    for (const auto& line : resourceCint) {
      output.push_back(line);
    }
  }
  return output;
}

int BcmCinter::bcm_port_resource_speed_set(
    int unit,
    bcm_gport_t port,
    bcm_port_resource_t* resource) {
  auto cint = cintForPortResource("resource", resource);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_port_resource_speed_set(",
          makeParamStr(unit, port, "&resource"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_port_resource_multi_set(
    int unit,
    int nport,
    bcm_port_resource_t* resource) {
  auto cint = cintForPortResources(nport, resource);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_port_resource_multi_set(",
          makeParamStr(unit, nport, "&resources"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_switch_control_port_set(
    int unit,
    bcm_port_t port,
    bcm_switch_control_t type,
    int arg) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_switch_control_port_set(",
          makeParamStr(unit, port, type, arg),
          ")")));
  return 0;
}

int BcmCinter::bcm_l2_addr_delete_by_port(
    int unit,
    bcm_module_t mod,
    bcm_port_t port,
    uint32 flags) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_l2_addr_delete_by_port(",
          makeParamStr(unit, mod, port, flags),
          ")")));
  return 0;
}

int BcmCinter::bcm_linkscan_enable_set(int unit, int us) {
  writeCintLines(wrapFunc(
      to<string>("bcm_linkscan_enable_set(", makeParamStr(unit, us), ")")));
  return 0;
}

int BcmCinter::bcm_linkscan_mode_set(int unit, bcm_port_t port, int mode) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_linkscan_mode_set(", makeParamStr(unit, port, mode), ")")));
  return 0;
}

vector<string> BcmCinter::cintForL3Ingress(const bcm_l3_ingress_t* l3_ingress) {
  vector<string> cintLines;
  cintLines = {
      "bcm_l3_ingress_t_init(&l3_ingress)",
      to<string>("l3_ingress.flags = ", l3_ingress->flags),
      to<string>("l3_ingress.vrf = ", l3_ingress->vrf)};
  return cintLines;
}

int BcmCinter::bcm_l3_ingress_create(
    int unit,
    bcm_l3_ingress_t* ing_intf,
    bcm_if_t* intf_id) {
  vector<string> cint =
      cintForL3Ingress(reinterpret_cast<bcm_l3_ingress_t*>(ing_intf));
  cint.push_back(to<string>("ing_intf_id = ", *intf_id));
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_l3_ingress_create(",
          makeParamStr(unit, "&l3_ingress", "&ing_intf_id"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_ingress_destroy(int unit, bcm_if_t intf_id) {
  writeCintLines(wrapFunc(
      to<string>("bcm_l3_ingress_destroy(", makeParamStr(unit, intf_id), ")")));
  return 0;
}

int BcmCinter::bcm_vlan_control_vlan_set(
    int unit,
    bcm_vlan_t vlan,
    bcm_vlan_control_vlan_t control) {
  vector<string> cint;
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_vlan_control_vlan_get(",
          makeParamStr(unit, vlan, "&vlan_ctrl"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  cint.emplace_back(to<string>("vlan_ctrl.ingress_if = ", control.ingress_if));
  funcCint = wrapFunc(
      to<string>(
          "bcm_vlan_control_vlan_set(",
          makeParamStr(unit, vlan, "vlan_ctrl"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::sh_process_command(int unit, char* cmd) {
  /* Add cmd in the output to associate output with the command */
  vector<string> cmdShowCint = {to<string>("printf(\"\\n== ", cmd, ":\\n\")")};
  writeCintLines(std::move(cmdShowCint));
  auto funcCint = wrapFunc(to<string>("bshell(", unit, ", \'", cmd, "\')"));
  vector<string> cintLine = {funcCint};
  writeCintLines(cintLine);
  return 0;
}

int BcmCinter::bcm_cosq_port_priority_group_property_set(
    int unit,
    bcm_port_t gport,
    int priority_group_id,
    bcm_cosq_port_prigroup_control_t type,
    int arg) {
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_cosq_port_priority_group_property_set(",
          makeParamStr(unit, gport, priority_group_id, type, arg),
          ")"));
  writeCintLines(funcCint);
  return 0;
}

int BcmCinter::bcm_port_priority_group_config_set(
    int unit,
    bcm_gport_t gport,
    int priority_group,
    bcm_port_priority_group_config_t* prigrp_config) {
  vector<string> cintLines;
  string varName = getNextPgConfigVar();
  cintLines.push_back(to<string>("bcm_port_priority_group_config_t ", varName));
  cintLines.push_back(
      to<string>("bcm_port_priority_group_config_t_init(&", varName, ")"));
  cintLines.push_back(
      to<string>(
          varName,
          ".pfc_transmit_enable=",
          prigrp_config->pfc_transmit_enable));
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_port_priority_group_config_set(",
          makeParamStr(unit, gport, priority_group, to<string>("&", varName)),
          ")"));
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator((cintForFn.end())));
  writeCintLines(std::move(cintLines));
  return 0;
}

int BcmCinter::bcm_port_phy_timesync_config_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_timesync_config_t* conf) {
  string pptConfigVar = getNextPptConfigVar();
  vector<string> cint = cintForPortPhyTimesyncConfig(conf, pptConfigVar);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_port_phy_timesync_config_set(",
          makeParamStr(unit, port, to<string>("&", pptConfigVar)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_port_timesync_config_set(
    int unit,
    bcm_port_t port,
    int config_count,
    bcm_port_timesync_config_t* config_array) {
  auto ppConfigArrayVar = getNextPtConfigArrayVar();
  auto cint = cintForPortTimesyncConfigArray(
      config_count, config_array, ppConfigArrayVar);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_port_timesync_config_set(",
          makeParamStr(
              unit, port, config_count, to<string>("&", ppConfigArrayVar)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_time_interface_add(int unit, bcm_time_interface_t* intf) {
  auto timeInterfaceVar = getNextTimeInterfaceVar();
  auto cint = cintForTimeInterface(intf, timeInterfaceVar);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_time_interface_add(",
          makeParamStr(unit, to<string>("&", timeInterfaceVar)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_time_interface_delete_all(int unit) {
  writeCintLines(wrapFunc(
      to<string>("bcm_time_interface_delete_all(", makeParamStr(unit), ")")));
  return 0;
}

int BcmCinter::bcm_field_entry_flexctr_attach(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_flexctr_config_t* flexctr_cfg) {
  auto cint = cintForFlexctrConfig(flexctr_cfg);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_field_entry_flexctr_attach(",
          makeParamStr(
              unit, getCintVar(fieldEntryVars, entry), "&flexctr_config"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_entry_flexctr_detach(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_flexctr_config_t* flexctr_cfg) {
  auto cint = cintForFlexctrConfig(flexctr_cfg);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_field_entry_flexctr_detach(",
          makeParamStr(
              unit, getCintVar(fieldEntryVars, entry), "&flexctr_config"),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_field_entry_remove(int unit, bcm_field_entry_t entry) {
  auto cint = wrapFunc(
      to<string>(
          "bcm_field_entry_remove(",
          makeParamStr(unit, getCintVar(fieldEntryVars, entry)),
          ")"));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_cosq_pfc_deadlock_control_set(
    int unit,
    bcm_port_t port,
    int priority,
    bcm_cosq_pfc_deadlock_control_t type,
    int arg) {
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_cosq_pfc_deadlock_control_set(",
          makeParamStr(unit, port, priority, type, arg),
          ")"));
  writeCintLines(funcCint);
  return 0;
}

int BcmCinter::bcm_flexctr_action_create(
    int unit,
    int options,
    bcm_flexctr_action_t* action,
    uint32* stat_counter_id) {
  string statCounterVar = getNextStateCounterVar();
  writeCintLine(to<string>("uint32 ", statCounterVar));
  {
    statCounterIdVars.wlock()->emplace(*stat_counter_id, statCounterVar);
  }
  auto cint = cintForFlexCtrAction(action);
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_flexctr_action_create(",
          makeParamStr(
              unit,
              options,
              "&flexctr_action",
              to<string>("&", statCounterVar)),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_flexctr_action_destroy(int unit, uint32 stat_counter_id) {
  auto cint = wrapFunc(
      to<string>(
          "bcm_flexctr_action_destroy(",
          makeParamStr(unit, stat_counter_id),
          ")"));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_route_stat_attach(
    int unit,
    bcm_l3_route_t* l3_route,
    uint32 stat_counter_id) {
  auto cint = cintForL3Route(*l3_route);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_route_stat_attach(",
          makeParamStr(unit, "&l3_route", stat_counter_id),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_route_flexctr_object_set(
    int unit,
    bcm_l3_route_t* l3_route,
    uint32 value) {
  auto cint = cintForL3Route(*l3_route);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_route_flexctr_object_set(",
          makeParamStr(unit, "&l3_route", value),
          ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_route_stat_detach(int unit, bcm_l3_route_t* l3_route) {
  auto cint = cintForL3Route(*l3_route);
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_l3_route_stat_detach(", makeParamStr(unit, "&l3_route"), ")"));
  cint.insert(
      cint.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_stat_custom_group_create(
    int unit,
    uint32 mode_id,
    bcm_stat_object_t stat_type,
    uint32* stat_counter_id,
    uint32* /* num_entries */) {
  string statCounterVar = getNextStateCounterVar();
  string numEntriesVar = "num_entries_" + statCounterVar;
  vector<string> cintLines;
  cintLines.push_back(to<string>("uint32 ", statCounterVar));
  cintLines.push_back(to<string>("uint32 ", numEntriesVar));
  {
    statCounterIdVars.wlock()->emplace(*stat_counter_id, statCounterVar);
  }
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_stat_custom_group_create(",
          makeParamStr(
              unit,
              mode_id,
              stat_type,
              to<string>("&", statCounterVar),
              to<string>("&", numEntriesVar)),
          ")"));
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cintLines));
  return 0;
}

int BcmCinter::bcm_stat_group_destroy(int unit, uint32 stat_counter_id) {
  auto cint = wrapFunc(
      to<string>(
          "bcm_stat_group_destroy(", makeParamStr(unit, stat_counter_id), ")"));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_stat_group_create(
    int unit,
    bcm_stat_object_t object,
    bcm_stat_group_mode_t group_mode,
    uint32* stat_counter_id,
    uint32* /*num_entries*/) {
  string statCounterVar = getNextStateCounterVar();
  string numEntriesVar = "num_entries_" + statCounterVar;
  vector<string> cintLines;
  cintLines.push_back(to<string>("uint32 ", statCounterVar));
  cintLines.push_back(to<string>("uint32 ", numEntriesVar));
  {
    statCounterIdVars.wlock()->emplace(*stat_counter_id, statCounterVar);
  }
  auto funcCint = wrapFunc(
      to<string>(
          "bcm_stat_group_create(",
          makeParamStr(
              unit,
              object,
              group_mode,
              to<string>("&", statCounterVar),
              to<string>("&", numEntriesVar)),
          ")"));
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(funcCint.begin()),
      make_move_iterator(funcCint.end()));
  writeCintLines(std::move(cintLines));
  return 0;
};

int BcmCinter::bcm_l3_ingress_stat_attach(
    int unit,
    bcm_if_t intf_id,
    uint32 stat_counter_id) {
  auto cint = wrapFunc(
      to<string>(
          "bcm_l3_ingress_stat_attach(",
          makeParamStr(
              unit, getCintVar(l3IntfIdVars, intf_id), stat_counter_id),
          ")"));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_l3_egress_stat_attach(
    int unit,
    bcm_if_t intf_id,
    uint32 stat_counter_id) {
  auto cint = wrapFunc(
      to<string>(
          "bcm_l3_egress_stat_attach(",
          makeParamStr(
              unit, getCintVar(l3IntfIdVars, intf_id), stat_counter_id),
          ")"));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_stat_group_mode_id_create(
    int unit,
    uint32 flags,
    uint32 total_counters,
    uint32 num_selectors,
    bcm_stat_group_mode_attr_selector_t* attr_selectors,
    uint32* /* mode_id */) {
  vector<string> cintLines;
  cintLines.push_back(
      to<string>(
          "bcm_stat_group_mode_attr_selector_t selector[", num_selectors, "]"));
  cintLines.push_back(to<string>("uint32 mode_id"));
  for (int i = 0; i < num_selectors; i++) {
    cintLines.push_back(
        to<string>(
            "bcm_stat_group_mode_attr_selector_t_init(&selector[", i, "])"));
    cintLines.push_back(
        to<string>(
            "selector[",
            i,
            "].counter_offset = ",
            attr_selectors[i].counter_offset));
    cintLines.push_back(
        to<string>("selector[", i, "].attr = ", attr_selectors[i].attr));
    cintLines.push_back(
        to<string>(
            "selector[", i, "].attr_value = ", attr_selectors[i].attr_value));
  }
  auto cintForFn = wrapFunc(
      to<string>(
          "bcm_stat_group_mode_id_create(",
          makeParamStr(
              unit,
              flags,
              total_counters,
              num_selectors,
              "selector",
              "&mode_id"),
          ")"));
  cintLines.insert(
      cintLines.end(),
      make_move_iterator(cintForFn.begin()),
      make_move_iterator(cintForFn.end()));
  writeCintLines(std::move(cintLines));
  return 0;
}

int BcmCinter::bcm_stat_group_mode_id_destroy(int unit, uint32 mode_id) {
  auto cint = wrapFunc(
      to<string>(
          "bcm_stat_group_mode_id_destroy(", makeParamStr(unit, mode_id), ")"));
  writeCintLines(std::move(cint));
  return 0;
}

int BcmCinter::bcm_port_ifg_set(
    int unit,
    bcm_port_t port,
    int speed,
    bcm_port_duplex_t duplex,
    int bit_times) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_ifg_set(",
          makeParamStr(unit, port, speed, duplex, bit_times),
          ")")));
  return 0;
}

int BcmCinter::bcm_port_control_phy_timesync_set(
    int unit,
    bcm_port_t port,
    bcm_port_control_phy_timesync_t type,
    uint64 value) {
  writeCintLines(wrapFunc(
      to<string>(
          "bcm_port_control_phy_timesync_set(",
          makeParamStr(unit, port, type, value),
          ")")));
  return 0;
}
} // namespace facebook::fboss
