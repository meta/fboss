/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/hw/sai/api/SaiApi.h"

#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/SaiDefaultAttributeValues.h"

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>

#include <iterator>
#include <vector>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class FdbApi;

struct SaiFdbTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_FDB_ENTRY;
  using SaiApiT = FdbApi;
  struct Attributes {
    using EnumType = sai_fdb_entry_attr_t;
    using Type = SaiAttribute<EnumType, SAI_FDB_ENTRY_ATTR_TYPE, sai_int32_t>;
    using BridgePortId =
        SaiAttribute<EnumType, SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID, SaiObjectIdT>;
    using Metadata = SaiAttribute<
        EnumType,
        SAI_FDB_ENTRY_ATTR_META_DATA,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
  };

  class FdbEntry {
   public:
    FdbEntry() {}
    FdbEntry(
        sai_object_id_t switchId,
        sai_object_id_t bridgeVlanId,
        const folly::MacAddress& mac) {
      fdb_entry.switch_id = switchId;
      fdb_entry.bv_id = bridgeVlanId;
      toSaiMacAddress(mac, fdb_entry.mac_address);
    }
    explicit FdbEntry(const sai_object_key_t& key) {
      fdb_entry = key.key.fdb_entry;
    }
    folly::MacAddress mac() const {
      return fromSaiMacAddress(fdb_entry.mac_address);
    }
    sai_object_id_t switchId() const {
      return fdb_entry.switch_id;
    }
    sai_object_id_t bridgeVlanId() const {
      return fdb_entry.bv_id;
    }
    const sai_fdb_entry_t* entry() const {
      return &fdb_entry;
    }
    bool operator==(const FdbEntry& other) const {
      return (
          switchId() == other.switchId() &&
          bridgeVlanId() == other.bridgeVlanId() && mac() == other.mac());
    }

    folly::dynamic toFollyDynamic() const;
    static FdbEntry fromFollyDynamic(const folly::dynamic& json);
    std::string toString() const;

   private:
    sai_fdb_entry_t fdb_entry{};
  };

  using CreateAttributes = std::tuple<
      Attributes::Type,
      Attributes::BridgePortId,
      std::optional<Attributes::Metadata>>;
  using AdapterKey = FdbEntry;
  using AdapterHostKey = FdbEntry;
};

SAI_ATTRIBUTE_NAME(Fdb, Type)
SAI_ATTRIBUTE_NAME(Fdb, BridgePortId)
SAI_ATTRIBUTE_NAME(Fdb, Metadata)

template <>
struct IsSaiEntryStruct<SaiFdbTraits::FdbEntry> : public std::true_type {};

// Dynamic FDB entries age out in hardware on their own, so an entry FBOSS
// tracks - in the store, or in a warm boot state written moments earlier - may
// already be gone from HW.
template <>
struct SaiObjectMayBeMissingInHw<SaiFdbTraits> : public std::true_type {};

class FdbApi : public SaiApi<FdbApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_FDB;
  FdbApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for fdb api");
  }

 private:
  sai_status_t _create(
      const SaiFdbTraits::FdbEntry& fdbEntry,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_fdb_entry(fdbEntry.entry(), count, attr_list);
  }
  sai_status_t _remove(const SaiFdbTraits::FdbEntry& fdbEntry) const {
    return api_->remove_fdb_entry(fdbEntry.entry());
  }
  sai_status_t _getAttribute(
      const SaiFdbTraits::FdbEntry& fdbEntry,
      sai_attribute_t* attr) const {
    return api_->get_fdb_entry_attribute(fdbEntry.entry(), 1, attr);
  }
  sai_status_t _setAttribute(
      const SaiFdbTraits::FdbEntry& fdbEntry,
      const sai_attribute_t* attr) const {
    return api_->set_fdb_entry_attribute(fdbEntry.entry(), attr);
  }

  sai_fdb_api_t* api_;
  friend class SaiApi<FdbApi>;
};

inline void toAppend(const SaiFdbTraits::FdbEntry& entry, std::string* result) {
  result->append(entry.toString());
}

} // namespace facebook::fboss

namespace std {
template <>
struct hash<facebook::fboss::SaiFdbTraits::FdbEntry> {
  size_t operator()(
      const facebook::fboss::SaiFdbTraits::FdbEntry& FdbEntry) const;
};
} // namespace std
