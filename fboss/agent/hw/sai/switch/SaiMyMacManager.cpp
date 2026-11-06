/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiMyMacManager.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
namespace {
const folly::MacAddress kExactMatchMask("ff:ff:ff:ff:ff:ff");
} // namespace
#endif

SaiMyMacManager::SaiMyMacManager(SaiStore* saiStore) : saiStore_(saiStore) {}

void SaiMyMacManager::programMyMacs(
    const std::vector<cfg::MacAndVlan>& myMacs) {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  auto& store = saiStore_->get<SaiMyMacTraits>();
  std::vector<std::shared_ptr<SaiMyMac>> newMyMacs;
  newMyMacs.reserve(myMacs.size());
  for (const auto& myMac : myMacs) {
    SaiMyMacTraits::AdapterHostKey key{
        folly::MacAddress(*myMac.macAddress()),
        kExactMatchMask,
        static_cast<sai_uint16_t>(*myMac.vlanID())};
    newMyMacs.push_back(store.setObject(key, key));
  }
  // Entries no longer referenced get removed from hardware
  myMacs_ = std::move(newMyMacs);
#else
  if (!myMacs.empty()) {
    throw FbossError("MY_MAC is not supported by this SAI version");
  }
#endif
}

} // namespace facebook::fboss
