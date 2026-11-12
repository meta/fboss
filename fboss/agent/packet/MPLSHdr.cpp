// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/packet/MPLSHdr.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/IPv4Hdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktUtil.h"

#include <folly/io/Cursor.h>
#include <sstream>

namespace facebook::fboss {

MPLSHdr::MPLSHdr(folly::io::Cursor* cursor) {
  MPLSLabel mplsLabel;
  while (cursor->tryReadBE<uint32_t>(mplsLabel.u32)) {
    stack_.push_back(mplsLabel.label);
    if (mplsLabel.label.bottomOfStack) {
      break;
    }
  }
}

void MPLSHdr::serialize(folly::io::RWPrivateCursor* cursor) const {
  MPLSLabel mplsLabel;
  for (auto label : stack_) {
    mplsLabel.label = label;
    cursor->writeBE<uint32_t>(mplsLabel.u32);
  }
}

std::string MPLSHdr::Label::toString() const {
  std::stringstream ss;
  ss << " Label: " << getLabelValue() << " TTL: " << getTTL()
     << " bottomOfStack: " << isbottomOfStack()
     << " trafficClass: " << getTrafficClass();
  return ss.str();
}

std::string MPLSHdr::toString() const {
  std::stringstream ss;
  for (const auto& label : stack_) {
    ss << label.toString() << std::endl;
  }
  return ss.str();
}
namespace utility {
std::optional<ETHERTYPE> decapsulateMplsPacket(folly::IOBuf* buf) {
  folly::io::Cursor cursor(buf);
  EthHdr ethHdr(cursor);
  MPLSHdr mplsHdr(&cursor);
  uint8_t version{0};
  cursor.pull(&version, 1);
  version >>= 4;
  if (version != IPV4_VERSION && version != IPV6_VERSION) {
    return std::nullopt;
  }

  auto etherType = (version == IPV4_VERSION) ? ETHERTYPE::ETHERTYPE_IPV4
                                             : ETHERTYPE::ETHERTYPE_IPV6;
  size_t srcMacDstMacSize = folly::MacAddress::SIZE + folly::MacAddress::SIZE;
  size_t vlanTagsSize = 0;
  if (!ethHdr.getVlanTags().empty()) {
    vlanTagsSize += (4 * ethHdr.getVlanTags().size());
  }
  const size_t csigTagSize = ethHdr.getCsigTag() ? CsigTag::SIZE : 0;
  size_t l2HeaderSize =
      srcMacDstMacSize + vlanTagsSize + csigTagSize + sizeof(ETHERTYPE);

  PktUtil::decapsulatePacket(
      buf, l2HeaderSize, mplsHdr.size(), static_cast<uint16_t>(etherType));

  return etherType;
}

} // namespace utility
} // namespace facebook::fboss
