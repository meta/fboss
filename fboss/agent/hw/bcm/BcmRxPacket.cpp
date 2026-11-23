/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmRxPacket.h"

#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/BcmTrunkTable.h"
#include "fboss/agent/hw/bcm/RxUtils.h"

#include <fmt/format.h>
#include <tuple>

extern "C" {
#include <bcm/rx.h>
#ifdef INCLUDE_PKTIO
#include <bcm/pktio.h>
#include <bcm/pktio_defs.h>
#include <bcmpkt/bcmpkt_rxpmd_defs.h>
#endif
}

using folly::IOBuf;

namespace {

/*
 * This is the callback in the IOBuf destructor for releasing SDK buf.
 * Only used in non-PKTIO case.
 */
void freeRxBuf(void* ptr, void* arg) {
  intptr_t unit = reinterpret_cast<intptr_t>(arg);
  bcm_rx_free(unit, ptr);
}

#ifdef INCLUDE_PKTIO
/*  IOBuf requires a free callback.  Otherwise the default free() will
 *  be called.  For PKTIO, nothing needs to be done.  Provide an empty
 *  callback just to avoid the default free() being called.
 */
void pktioIOBufEmptyCallback(void* /* ptr */, void* arg) {}
#endif

const char* const kRxReasonNames[] = BCM_RX_REASON_NAMES_INITIALIZER;

#ifdef INCLUDE_PKTIO
const struct {
  std::string name;
  int reason;
} kPktIORxReasonInfos[] = {BCMPKT_REASON_NAME_MAP_INIT};
#endif

} // namespace

namespace facebook::fboss {

BcmRxPacket::BcmRxPacket(const BcmPacketT& bcmPacket) {
  usePktIO_ = bcmPacket.usePktIO;

  if (!usePktIO_) {
    bcm_pkt_t* pkt = bcmPacket.ptrUnion.pkt;
    unit_ = pkt->unit;

    // The BCM RX code always uses a single buffer.
    // As long as there is just a single buffer, we don't need to allocate
    // a separate array of bcm_pkt_blk_t objects.

    CHECK_EQ(pkt->blk_count, 1);
    CHECK_EQ(pkt->pkt_data, &pkt->_pkt_data);

    buf_ = IOBuf::takeOwnership(
        pkt->pkt_data->data, // void* buf
        pkt->pkt_len,
        freeRxBuf, // FreeFunction freeFn
        reinterpret_cast<void*>(unit_)); // void* userData

    srcPort_ = PortID(pkt->src_port);
    srcVlan_ = VlanID(pkt->vlan);
    len_ = pkt->pkt_len;

  } else { // PKTIO
#ifdef INCLUDE_PKTIO
    bcm_pktio_pkt_t* pkt = bcmPacket.ptrUnion.pktioPkt;
    unit_ = pkt->unit;
    void* buffer;
    int rv;
    uint32 val;

    rv = bcm_pktio_pmd_field_get(
        unit_, pkt, bcmPktioPmdTypeHigig2, BCM_PKTIO_HG2_SRC_PORT, &val);
    bcmCheckError(rv, "failed to get pktio BCM_PKTIO_HG2_SRC_PORT");
    srcPort_ = PortID(val);

    // lowest bit in module id is used to accommodate port logical ids greater
    // than 255
    rv = bcm_pktio_pmd_field_get(
        unit_, pkt, bcmPktioPmdTypeHigig2, BCM_PKTIO_HG2_SRC_MODID, &val);
    bcmCheckError(rv, "failed to get pktio BCM_PKTIO_HG2_SRC_MODID");
    srcPort_ = (val << 8) | srcPort_;

    rv = bcm_pktio_pmd_field_get(
        unit_, pkt, bcmPktioPmdTypeRx, BCMPKT_RXPMD_OUTER_VID, &val);
    bcmCheckError(rv, "failed to get pktio OUTER_VID");
    srcVlan_ = VlanID(val);

    rv = bcm_pktio_pkt_data_get(unit_, pkt, &buffer, &len_);
    bcmCheckError(rv, "failed to get pktio packet data");

    buf_ = IOBuf::takeOwnership(buffer, len_, pktioIOBufEmptyCallback, nullptr);
#else
    throw FbossError("invalid PKTIO configuration");
#endif
  } // PKTIO
}

BcmRxPacket::~BcmRxPacket() {
  // Nothing to do.  The IOBuf destructor will call freeRxBuf()
  // to free the packet data
}

FbBcmRxPacket::FbBcmRxPacket(
    const BcmPacketT& bcmPacket,
    const BcmSwitch* bcmSwitch)
    : BcmRxPacket(bcmPacket) {
  bool usePktIO = bcmPacket.usePktIO;
  _reasons.usePktIO = usePktIO;
  if (!usePktIO) {
    bcm_pkt_t* pkt = bcmPacket.ptrUnion.pkt;
    cosQueue_ = pkt->cos;

    if (pkt->flags & BCM_PKT_F_TRUNK) {
      isFromAggregatePort_ = true;
      srcAggregatePort_ =
          bcmSwitch->getTrunkTable()->getAggregatePortId(pkt->src_trunk);
    }

    _priority = pkt->prio_int;
    _reasons.reasonsUnion.reasons = pkt->rx_reasons;

  } else { // PKTIO
#ifdef INCLUDE_PKTIO
    bcm_pktio_pkt_t* pkt = bcmPacket.ptrUnion.pktioPkt;
    uint32 val;

    auto rv = bcm_pktio_pmd_field_get(
        unit_, pkt, bcmPktioPmdTypeRx, BCMPKT_RXPMD_CPU_COS, &val);
    bcmCheckError(rv, "failed to get pktio CPU_COS");
    cosQueue_ = val;

    if (auto optRet = bcmSwitch->getTrunkTable()->portToAggPortGet(
            PortID(getSrcPort()))) {
      isFromAggregatePort_ = true;
      srcAggregatePort_ = *optRet;
    }

    rv = bcm_pktio_pmd_field_get(
        unit_, pkt, bcmPktioPmdTypeRx, BCMPKT_RXPMD_OUTER_PRI, &val);
    bcmCheckError(rv, "failed to get pktio OUTER_PRI");
    _priority = val;

    rv = bcm_pktio_pmd_reasons_get(
        unit_, pkt, &_reasons.reasonsUnion.pktio_reasons);
    bcmCheckError(rv, "failed to get pktio RX reasons");

#endif
  }
}

std::string FbBcmRxPacket::describeDetails() const {
  return fmt::format(
      "cos={} priority={} reasons={}",
      cosQueue_ ? std::to_string(*cosQueue_) : "none",
      _priority,
      RxUtils::describeReasons(_reasons));
}

std::vector<BcmRxPacket::RxReason> FbBcmRxPacket::getReasons() {
  std::vector<BcmRxPacket::RxReason> reasons;
  if (!usePktIO_) {
    for (int n = 0; n < bcmRxReasonCount; n++) {
      if (BCM_RX_REASON_GET(_reasons.reasonsUnion.reasons, n)) {
        reasons.push_back({n, kRxReasonNames[n]});
      }
    }
  } else {
#ifdef INCLUDE_PKTIO
    for (int n = BCMPKT_RX_REASON_NONE; n < BCMPKT_RX_REASON_COUNT; n++) {
      if (BCM_PKTIO_REASON_GET(
              _reasons.reasonsUnion.pktio_reasons.rx_reasons, n)) {
        reasons.push_back({n, kPktIORxReasonInfos[n].name});
      }
    }
#endif
  }
  return reasons;
}

} // namespace facebook::fboss
