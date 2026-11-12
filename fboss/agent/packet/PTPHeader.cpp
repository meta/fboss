/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/packet/PTPHeader.h"
#include <folly/io/IOBuf.h>
#include "fboss/agent/FbossError.h"

namespace facebook::fboss {

using folly::IOBuf;
using folly::io::Cursor;

PTPHeader::PTPHeader(Cursor* cursor) {
  parse(cursor);
}

void PTPHeader::checkPtpVersionValid() {
  if (ptpVersion_ == static_cast<uint8_t>(PTPVersion::PTP_V2) ||
      ptpVersion_ == static_cast<uint8_t>(PTPVersion::PTP_V1)) {
    return;
  }
  throw HdrParseError("PTP version incorrect!");
}

void PTPHeader::parse(Cursor* cursor) {
  ptpType_ = cursor->read<uint8_t>();
  ptpVersion_ = cursor->read<uint8_t>();
  checkPtpVersionValid();

  // for now only supports PTP DELAY message type
  // we are mostly interested in reading the correction field in the packet for
  // now
  if (ptpType_ == static_cast<uint8_t>(PTPMessageType::PTP_DELAY_REQUEST)) {
    cursor->tryReadBE<uint16_t>(ptpMessageLength_); // skip ptp message length
    cursor->skip(1); // domain number
    cursor->skip(1); // reserved
    cursor->skip(2); // skip ptp flags
    cursor->tryReadBE<uint64_t>(ptpCorrectionField_);
  } else {
    throw HdrParseError("PTP packet type unsupported");
  }
}

uint64_t PTPHeader::getCorrectionField() {
  return ptpCorrectionField_;
}

PTPMessageType PTPHeader::getPtpType() {
  return static_cast<PTPMessageType>(ptpType_);
}

PTPVersion PTPHeader::getPtpVersion() {
  return static_cast<PTPVersion>(ptpVersion_);
}
uint16_t PTPHeader::getPtpMessageLength() {
  return ptpMessageLength_;
}

void PTPHeader::write(folly::io::RWPrivateCursor* cursor) const {
  if (ptpType_ == static_cast<uint8_t>(PTPMessageType::PTP_DELAY_REQUEST)) {
    int hdrLength = PTP_DELAY_REQUEST_MSG_SIZE;
    int ptpFlags = 0x0400; // ptp_unicast
    uint8_t miscPtpOptions[11] = {};
    cursor->template write<uint8_t>((ptpType_)); // 1 byte
    cursor->template write<uint8_t>((ptpVersion_)); // 2 bytes
    cursor->template writeBE<uint16_t>(
        static_cast<uint16_t>(hdrLength)); // 4 bytes
    cursor->template writeBE<uint16_t>(
        static_cast<uint16_t>(0)); // domain, reserved, 6 bytes
    cursor->template writeBE<uint16_t>(
        static_cast<uint16_t>(ptpFlags)); // 8 bytes
    cursor->template writeBE<uint64_t>(
        ptpCorrectionField_); // correctionField , 16 bytes
    cursor->template writeBE<uint32_t>(
        static_cast<uint32_t>(0)); // messageType Specific, 20 bytes
    cursor->template writeBE<uint64_t>(
        static_cast<uint64_t>(0x1234)); // clock id, arbit, 28 bytes
    cursor->template writeBE<uint16_t>(
        static_cast<uint16_t>(1)); // src_port_id, arbit, 30 bytes
    cursor->template writeBE<uint16_t>(
        static_cast<uint16_t>(10)); // seq id, arbit, 32 bytes
    cursor->template write<uint8_t>(
        (ptpType_)); // controlField: same as ptp message type, 33 bytes
    cursor->push(miscPtpOptions, 11);
  }
}

int PTPHeader::getPayloadSize(PTPMessageType ptpPktType) {
  if (ptpPktType == PTPMessageType::PTP_DELAY_REQUEST) {
    return PTP_DELAY_REQUEST_MSG_SIZE;
  }
  throw FbossError("Invalid ptpPktType: ", static_cast<uint8_t>(ptpPktType));
}

} // namespace facebook::fboss
