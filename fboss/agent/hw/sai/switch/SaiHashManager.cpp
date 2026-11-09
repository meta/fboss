/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiHashManager.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

namespace {
using namespace facebook::fboss;

SaiHashTraits::Attributes::NativeHashFieldList toNativeHashFieldList(
    const cfg::Fields& hashFields) {
  SaiHashTraits::Attributes::NativeHashFieldList::ValueType nativeHashFields;
  for (auto field : *hashFields.ipv4Fields()) {
    switch (field) {
      case cfg::IPv4Field::SOURCE_ADDRESS:
        nativeHashFields.emplace_back(SAI_NATIVE_HASH_FIELD_SRC_IP);
        break;
      case cfg::IPv4Field::DESTINATION_ADDRESS:
        nativeHashFields.emplace_back(SAI_NATIVE_HASH_FIELD_DST_IP);
        break;
    }
  }
  for (auto field : *hashFields.ipv6Fields()) {
    switch (field) {
      case cfg::IPv6Field::SOURCE_ADDRESS:
        nativeHashFields.emplace_back(SAI_NATIVE_HASH_FIELD_SRC_IP);
        break;
      case cfg::IPv6Field::DESTINATION_ADDRESS:
        nativeHashFields.emplace_back(SAI_NATIVE_HASH_FIELD_DST_IP);
        break;
      case cfg::IPv6Field::FLOW_LABEL:
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
        nativeHashFields.emplace_back(SAI_NATIVE_HASH_FIELD_IPV6_FLOW_LABEL);
        break;
#else
        throw FbossError(
            "Hashing on IPv6 Flow Label requires SAI version >= 1.12.0");
#endif
    }
  }
  for (auto field : *hashFields.transportFields()) {
    switch (field) {
      case cfg::TransportField::SOURCE_PORT:
        nativeHashFields.emplace_back(SAI_NATIVE_HASH_FIELD_L4_SRC_PORT);
        break;
      case cfg::TransportField::DESTINATION_PORT:
        nativeHashFields.emplace_back(SAI_NATIVE_HASH_FIELD_L4_DST_PORT);
        break;
    }
  }
  if (hashFields.mplsFields()->size()) {
    throw FbossError("Hashing on MPLS fields is not supported");
  }
  return nativeHashFields;
}
} // namespace

namespace facebook::fboss {

std::shared_ptr<SaiHash> SaiHashManager::getOrCreate(
    const cfg::Fields& hashFields,
    std::vector<sai_object_id_t> udfGroupIds) {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    throw FbossError("hash field customization is unsupported");
  }
  auto& store = saiStore_->get<SaiHashTraits>();
  auto nativeHashFields = toNativeHashFieldList(hashFields);
  SaiHashTraits::AdapterHostKey adapterHostKey{nativeHashFields, std::nullopt};
  if (!udfGroupIds.empty()) {
    adapterHostKey = std::make_tuple(nativeHashFields, udfGroupIds);
  }
  SaiHashTraits::CreateAttributes createAttrs = adapterHostKey;
  return store.setObject(adapterHostKey, createAttrs);
}

SaiHashManager::SaiHashManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {
  if (platform_->getAsic()->isSupported(HwAsic::Feature::TRAFFIC_HASHING) &&
      !platform_->getAsic()->isSupported(
          HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    // query default hash keys
    auto switchId = managerTable_->switchManager().getSwitchSaiId();
    auto ecmpHashId = SaiApiTable::getInstance()->switchApi().getAttribute(
        switchId, SaiSwitchTraits::Attributes::EcmpHash{});
    auto lagHashId = SaiApiTable::getInstance()->switchApi().getAttribute(
        switchId, SaiSwitchTraits::Attributes::LagHash{});
    CHECK_NE(ecmpHashId, SAI_NULL_OBJECT_ID)
        << "default hash for ECMP not found";
    CHECK_NE(lagHashId, SAI_NULL_OBJECT_ID) << "default hash for LAG not found";
  }
}

void SaiHashManager::removeUnclaimedDefaultHash() {
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    return;
  }
  saiStore_->get<SaiHashTraits>().removeUnclaimedWarmbootHandlesIf(
      [](const auto& hash) {
        hash->release();
        return true;
      });
}
} // namespace facebook::fboss
