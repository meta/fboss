// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/SaiDefaultAttributeValues.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include <folly/IPAddress.h>
#include <folly/IPAddressV6.h>

#include <array>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
class Srv6Api;

struct SaiSrv6SidListTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_SRV6_SIDLIST;
  using SaiApiT = Srv6Api;
  struct Attributes {
    using EnumType = sai_srv6_sidlist_attr_t;
    using Type =
        SaiAttribute<EnumType, SAI_SRV6_SIDLIST_ATTR_TYPE, sai_int32_t>;
    using SegmentList = SaiAttribute<
        EnumType,
        SAI_SRV6_SIDLIST_ATTR_SEGMENT_LIST,
        std::vector<std::array<uint8_t, 16>>>;
    using NextHopId =
        SaiAttribute<EnumType, SAI_SRV6_SIDLIST_ATTR_NEXT_HOP_ID, SaiObjectIdT>;
  };
  using AdapterKey = Srv6SidListSaiId;
  using CreateAttributes = std::tuple<
      Attributes::Type,
      std::optional<Attributes::SegmentList>,
      std::optional<Attributes::NextHopId>>;
  using AdapterHostKey = std::tuple<
      Attributes::Type,
      std::optional<Attributes::SegmentList>,
      RouterInterfaceSaiId,
      folly::IPAddress>;
};

SAI_ATTRIBUTE_NAME(Srv6SidList, Type);
SAI_ATTRIBUTE_NAME(Srv6SidList, SegmentList);
SAI_ATTRIBUTE_NAME(Srv6SidList, NextHopId);

struct SaiMySidEntryTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_MY_SID_ENTRY;
  using SaiApiT = Srv6Api;
  struct Attributes {
    using EnumType = sai_my_sid_entry_attr_t;
    using EndpointBehavior = SaiAttribute<
        EnumType,
        SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR,
        sai_int32_t>;
    using EndpointBehaviorFlavor = SaiAttribute<
        EnumType,
        SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR,
        sai_int32_t>;
    using NextHopId =
        SaiAttribute<EnumType, SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID, SaiObjectIdT>;
    using Vrf = SaiAttribute<EnumType, SAI_MY_SID_ENTRY_ATTR_VRF, SaiObjectIdT>;
    using PacketAction = SaiAttribute<
        EnumType,
        SAI_MY_SID_ENTRY_ATTR_PACKET_ACTION,
        sai_int32_t>;
    using TunnelId =
        SaiAttribute<EnumType, SAI_MY_SID_ENTRY_ATTR_TUNNEL_ID, SaiObjectIdT>;
  };
  class MySidEntry {
   public:
    MySidEntry() {}
    MySidEntry(
        sai_object_id_t switchId,
        sai_object_id_t vrId,
        uint8_t locatorBlockLen,
        uint8_t locatorNodeLen,
        uint8_t functionLen,
        uint8_t argsLen,
        const folly::IPAddressV6& sid) {
      my_sid_entry.switch_id = switchId;
      my_sid_entry.vr_id = vrId;
      my_sid_entry.locator_block_len = locatorBlockLen;
      my_sid_entry.locator_node_len = locatorNodeLen;
      my_sid_entry.function_len = functionLen;
      my_sid_entry.args_len = argsLen;
      toSaiIpAddressV6(sid, &my_sid_entry.sid);
    }
    explicit MySidEntry(const sai_object_key_t& key) {
      my_sid_entry = key.key.my_sid_entry;
    }
    sai_object_id_t switchId() const {
      return my_sid_entry.switch_id;
    }
    sai_object_id_t vrId() const {
      return my_sid_entry.vr_id;
    }
    uint8_t locatorBlockLen() const {
      return my_sid_entry.locator_block_len;
    }
    uint8_t locatorNodeLen() const {
      return my_sid_entry.locator_node_len;
    }
    uint8_t functionLen() const {
      return my_sid_entry.function_len;
    }
    uint8_t argsLen() const {
      return my_sid_entry.args_len;
    }
    folly::IPAddressV6 sid() const {
      return fromSaiIpAddress(my_sid_entry.sid);
    }
    const sai_my_sid_entry_t* entry() const {
      return &my_sid_entry;
    }
    bool operator==(const MySidEntry& other) const {
      return (
          switchId() == other.switchId() && vrId() == other.vrId() &&
          locatorBlockLen() == other.locatorBlockLen() &&
          locatorNodeLen() == other.locatorNodeLen() &&
          functionLen() == other.functionLen() &&
          argsLen() == other.argsLen() && sid() == other.sid());
    }
    std::string toString() const;
    folly::dynamic toFollyDynamic() const;
    static MySidEntry fromFollyDynamic(const folly::dynamic& json);

   private:
    sai_my_sid_entry_t my_sid_entry{};
  };

  using AdapterKey = MySidEntry;
  using AdapterHostKey = MySidEntry;
  // TunnelId is intentionally excluded because some SDKs return
  // SAI_STATUS_ITEM_NOT_FOUND when it is queried for non-decap behaviors.
  // Decap entries program it explicitly after creation.
  using CreateAttributes = std::tuple<
      Attributes::EndpointBehavior,
      Attributes::EndpointBehaviorFlavor,
      Attributes::NextHopId,
      std::optional<Attributes::Vrf>,
      Attributes::PacketAction>;
};
template <>
struct IsSaiEntryStruct<SaiMySidEntryTraits::MySidEntry>
    : public std::true_type {};

SAI_ATTRIBUTE_NAME(MySidEntry, EndpointBehavior);
SAI_ATTRIBUTE_NAME(MySidEntry, EndpointBehaviorFlavor);
SAI_ATTRIBUTE_NAME(MySidEntry, NextHopId);
SAI_ATTRIBUTE_NAME(MySidEntry, Vrf);
SAI_ATTRIBUTE_NAME(MySidEntry, PacketAction);
SAI_ATTRIBUTE_NAME(MySidEntry, TunnelId);

class Srv6Api : public SaiApi<Srv6Api> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_SRV6;
  Srv6Api() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for srv6 api");
  }

 private:
  sai_status_t _create(
      Srv6SidListSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_srv6_sidlist(
        rawSaiId(id), switch_id, static_cast<uint32_t>(count), attr_list);
  }
  sai_status_t _remove(Srv6SidListSaiId key) const {
    return api_->remove_srv6_sidlist(key);
  }
  sai_status_t _getAttribute(Srv6SidListSaiId key, sai_attribute_t* attr)
      const {
    return api_->get_srv6_sidlist_attribute(key, 1, attr);
  }
  sai_status_t _setAttribute(Srv6SidListSaiId key, const sai_attribute_t* attr)
      const {
    return api_->set_srv6_sidlist_attribute(key, attr);
  }
  sai_status_t _getStats(
      Srv6SidListSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids,
      sai_stats_mode_t /*mode*/,
      uint64_t* counters) const {
    return api_->get_srv6_sidlist_stats(
        key, num_of_counters, counter_ids, counters);
  }
  sai_status_t _clearStats(
      Srv6SidListSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids) const {
    return api_->clear_srv6_sidlist_stats(key, num_of_counters, counter_ids);
  }

  sai_status_t _create(
      const SaiMySidEntryTraits::MySidEntry& mySidEntry,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_my_sid_entry(
        mySidEntry.entry(), static_cast<uint32_t>(count), attr_list);
  }
  sai_status_t _remove(
      const SaiMySidEntryTraits::MySidEntry& mySidEntry) const {
    return api_->remove_my_sid_entry(mySidEntry.entry());
  }
  sai_status_t _getAttribute(
      const SaiMySidEntryTraits::MySidEntry& mySidEntry,
      sai_attribute_t* attr) const {
    return api_->get_my_sid_entry_attribute(mySidEntry.entry(), 1, attr);
  }
  sai_status_t _setAttribute(
      const SaiMySidEntryTraits::MySidEntry& mySidEntry,
      const sai_attribute_t* attr) const {
    return api_->set_my_sid_entry_attribute(mySidEntry.entry(), attr);
  }

  sai_srv6_api_t* api_{nullptr};
  friend class SaiApi<Srv6Api>;
};
inline void toAppend(
    const SaiMySidEntryTraits::MySidEntry& entry,
    std::string* result) {
  result->append(entry.toString());
}

#endif

} // namespace facebook::fboss

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
namespace std {
template <>
struct hash<facebook::fboss::SaiMySidEntryTraits::MySidEntry> {
  size_t operator()(
      const facebook::fboss::SaiMySidEntryTraits::MySidEntry& n) const;
};
} // namespace std
#endif
