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

#include "fboss/agent/hw/sai/api/LoggingUtil.h"
#include "fboss/agent/hw/sai/api/PortApi.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/api/Traits.h"
#include "fboss/agent/hw/sai/store/Traits.h"
#include "fboss/lib/TupleUtils.h"

#include "fboss/agent/hw/sai/store/SaiObjectEventPublisher.h"

#include <variant>

namespace facebook::fboss {

template <typename SaiObjectTraits>
class SaiObjectStore;
class SaiStoreTest;

namespace detail {

template <typename SaiObjectTraits>
using AdapterHostKeyT = typename SaiObjectTraits::AdapterHostKey;

template <typename SaiObjectTraits>
concept SaiObjectCreateAttributes =
    SaiAttributeTuple<typename SaiObjectTraits::CreateAttributes>;

template <typename SaiObjectTraits>
concept AdapterHostKeyFromCreateAttribute =
    (SaiObjectCreateAttributes<SaiObjectTraits> &&
     IsElementOfTuple<
         AdapterHostKeyT<SaiObjectTraits>,
         typename SaiObjectTraits::CreateAttributes>::value);

template <typename SaiObjectTraits>
concept AdapterHostKeyFromCreateAttributeSubset =
    (SaiObjectCreateAttributes<SaiObjectTraits> &&
     IsSubsetOfTuple<
         AdapterHostKeyT<SaiObjectTraits>,
         typename SaiObjectTraits::CreateAttributes>::value);

template <typename SaiObjectTraits>
concept AdapterHostKeyFromEntryStruct =
    IsSaiEntryStruct<AdapterHostKeyT<SaiObjectTraits>>::value;

template <typename SaiObjectTraits>
concept SingleInstanceSaiObject =
    std::is_same_v<AdapterHostKeyT<SaiObjectTraits>, std::monostate>;

template <typename SaiObjectTraits>
concept NextHopGroupSaiObject =
    std::is_same_v<SaiObjectTraits, SaiNextHopGroupTraits>;

/*
 * For now, adapter host key extraction is a little clumsy.
 *
 * I thought it would be well matched by SFINAE on the Traits
 * object (particularly the relationship between AdapterHostKey and
 * CreateAttributes). However, the couple exceptions (default objects
 * using monostate and NextHopGroup being full on wild) resulted
 * in pretty complicated feeling logic being executed by "template
 * override selection" (if that is even a valid name for this...)
 * but really the logic in pseudo-code is:
 *
 * if AdapterHostKey is a single attribute:
 *   return that attribute
 * else if AdapterHostKey is a subset of attributes:
 *   return those attributes
 * else if AdapterHostKey is an entry type:
 *   return the entry type (AdapterKey)
 * else if the object is a single-instance default:
 *   return the bottom type (std::monostate)
 * else if the object is NextHopGroup:
 *   run the complicated, specific computation for NextHopGroup AdapterHostKey
 */

// If the adapter host key is one of the CreateAttributes of the object,
// simply extract that attribute from the tuple of CreateAttributes.
template <typename SaiObjectTraits>
  requires AdapterHostKeyFromCreateAttribute<SaiObjectTraits>
typename SaiObjectTraits::AdapterHostKey adapterHostKey(
    const typename SaiObjectTraits::AdapterKey&,
    const typename SaiObjectTraits::CreateAttributes& attributes) {
  return std::get<typename SaiObjectTraits::AdapterHostKey>(attributes);
}

// If the adapter host key is a tuple subset of the CreateAttributes of the
// object (e.g., key=std:tuple<A,C>, create=std::tuple<A,B,C,D>), then
// project the full set of CreateAttributes onto the subset.
template <typename SaiObjectTraits>
  requires AdapterHostKeyFromCreateAttributeSubset<SaiObjectTraits>
typename SaiObjectTraits::AdapterHostKey adapterHostKey(
    const typename SaiObjectTraits::AdapterKey&,
    const typename SaiObjectTraits::CreateAttributes& attributes) {
  return tupleProjection<
      typename SaiObjectTraits::CreateAttributes,
      typename SaiObjectTraits::AdapterHostKey>(attributes);
}

// If the adapter host key is an entry type (e.g. RouteEntry, FdbEntry), simply
// return the adapter key, which must also be the entry type.
template <typename SaiObjectTraits>
  requires AdapterHostKeyFromEntryStruct<SaiObjectTraits>
typename SaiObjectTraits::AdapterHostKey adapterHostKey(
    const typename SaiObjectTraits::AdapterKey& adapterKey,
    const typename SaiObjectTraits::CreateAttributes&) {
  return adapterKey;
}

// If the object always has only a single value, the key will be std::monostate
template <typename SaiObjectTraits>
  requires SingleInstanceSaiObject<SaiObjectTraits>
typename SaiObjectTraits::AdapterHostKey adapterHostKey(
    const typename SaiObjectTraits::AdapterKey&,
    const typename SaiObjectTraits::CreateAttributes&) {
  return std::monostate{};
}

// Exactly a special case for NextHopGroup
template <typename SaiObjectTraits>
  requires NextHopGroupSaiObject<SaiObjectTraits>
typename SaiObjectTraits::AdapterHostKey adapterHostKey(
    const typename SaiObjectTraits::AdapterKey& adapterKey,
    const typename SaiObjectTraits::CreateAttributes& attributes) {
  typename SaiObjectTraits::AdapterHostKey ret;
  const auto& apiTable = SaiApiTable::getInstance();
  auto memberIds = apiTable->nextHopGroupApi().getAttribute(
      adapterKey, SaiNextHopGroupTraits::Attributes::NextHopMemberList{});
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  auto arsSaiId = apiTable->nextHopGroupApi().getAttribute(
      adapterKey, SaiNextHopGroupTraits::Attributes::ArsObjectId());
  if (arsSaiId != SAI_NULL_OBJECT_ID) {
    ret.mode = apiTable->arsApi().getAttribute(
        static_cast<ArsSaiId>(arsSaiId), SaiArsTraits::Attributes::Mode());
  }
#endif
  for (const auto memberId : memberIds) {
    NextHopSaiId nextHopSaiId{apiTable->nextHopGroupApi().getAttribute(
        NextHopGroupMemberSaiId(memberId),
        SaiNextHopGroupMemberTraits::Attributes::NextHopId{})};
    unsigned int weight{apiTable->nextHopGroupApi().getAttribute(
        NextHopGroupMemberSaiId(memberId),
        SaiNextHopGroupMemberTraits::Attributes::Weight{})};

    /* next hop group member could be either mpls or ip next hop, so first read
     * condition attribute and then read adapter host key for  object trait
     * matching condition, */
    auto conditionAttributes = apiTable->nextHopApi().getAttribute(
        nextHopSaiId, SaiNextHopTraits::ConditionAttributes{});
    if (conditionAttributes == SaiIpNextHopTraits::kConditionAttributes) {
      ret.nhopMemberSet.insert(
          std::make_pair(
              apiTable->nextHopApi().getAttribute(
                  nextHopSaiId, SaiIpNextHopTraits::AdapterHostKey{}),
              weight));
    } else if (
        conditionAttributes == SaiMplsNextHopTraits::kConditionAttributes) {
      ret.nhopMemberSet.insert(
          std::make_pair(
              apiTable->nextHopApi().getAttribute(
                  nextHopSaiId, SaiMplsNextHopTraits::AdapterHostKey{}),
              weight));
    }
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    else if (
        conditionAttributes ==
        SaiSrv6SidlistNextHopTraits::kConditionAttributes) {
      ret.nhopMemberSet.insert(
          std::make_pair(
              apiTable->nextHopApi().getAttribute(
                  nextHopSaiId, SaiSrv6SidlistNextHopTraits::AdapterHostKey{}),
              weight));
    }
#endif
  }
  return ret;
}

} // namespace detail

/*
 * SaiObject is a generic object which manages an object in the SAI adapter.
 *
 * It is an RAII type, which can be empty (the live_ field denotes this) if
 * moved from. If it is live, destroying the SaiObject removes the
 * corresponding object from SAI.
 *
 * A SaiObject can be constructed in three ways:
 * 1. By loading it from the SAI adapter using the AdapterKey. This can be
 *    thought of as the SaiObject taking control of an existing object in SAI.
 * 2. By creating a new object in the SAI adapter using the AdapterHostKey and
 *    CreateAttributes
 * 3. Moving from another SaiObject. If the moved-from SaiObject was live,
 *    after the move, it is no longer live, so that at any point, only one
 *    SaiObject manages a given SAI object. (N.B., there is no general hard
 *    guarantee for this property -- a user could load the same SaiObject more
 *    than once).
 * In all three cases, (excepting the unlikely event of moving from a non-live
 * SaiObject), the newly constructed SaiObject is live and stores the
 * appropriate values of AdapterHostKey, AdapterKey, and CreateAttributes.
 *
 * Finally, SaiObject supports setting new values for the attributes. Given
 * a CreateAttributes, setAttributes will iterate over each attribute and
 * check the existing value against the new value. If they are unequal, we
 * set the new value. A particularly interesting case is an optional
 * attribute that goes from set to unset. In that case, we need to set the
 * attribute value back to a default value (which SAI always defines for any
 * optional attribute). That behavior is not yet correctly implemented in
 * SaiObject. TODO(borisb): remove this last note once we handle unsetting
 * optional attributes.
 */
template <typename SaiObjectTraits>
class SaiObject {
 protected:
  friend class SaiObjectStore<SaiObjectTraits>;
  friend class SaiStoreTest;

  // Load from adapter key
  explicit SaiObject(const typename SaiObjectTraits::AdapterKey& adapterKey)
      : adapterKey_(adapterKey) {
    auto& api =
        SaiApiTable::getInstance()->getApi<typename SaiObjectTraits::SaiApiT>();
    // N.B., fills out attributes_ as a side effect
    // XXX TODO: side-effect mode does NOT work with optionals
    attributes_ = api.getAttribute(adapterKey_, attributes_);
    live_ = true;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    if constexpr (std::is_same_v<SaiObjectTraits, SaiSrv6SidListTraits>) {
      CHECK(false)
          << "SaiSrv6SidListTraits cannot derive AdapterHostKey from "
          << "AdapterKey alone; use (AdapterKey, AdapterHostKey) constructor";
    } else {
#endif
      adapterHostKey_ =
          detail::adapterHostKey<SaiObjectTraits>(adapterKey_, attributes_);
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    }
#endif
  }

  // load with adapter key and adapter host key
  explicit SaiObject(
      const typename SaiObjectTraits::AdapterKey& adapterKey,
      const typename SaiObjectTraits::AdapterHostKey& adapterHostKey)
      : adapterKey_(adapterKey), adapterHostKey_(adapterHostKey) {
    static_assert(
        !AdapterHostKeyWarmbootRecoverable<SaiObjectTraits>::value,
        "object adapter host key is recoverable");
    auto& api =
        SaiApiTable::getInstance()->getApi<typename SaiObjectTraits::SaiApiT>();
    // N.B., fills out attributes_ as a side effect
    // XXX TODO: side-effect mode does NOT work with optionals
    attributes_ = api.getAttribute(adapterKey_, attributes_);
    live_ = true;
  }

  // Create a new one from adapter host key and attributes
  SaiObject(
      const typename SaiObjectTraits::AdapterHostKey& adapterHostKey,
      const typename SaiObjectTraits::CreateAttributes& attributes,
      sai_object_id_t switchId)
      : adapterHostKey_(adapterHostKey), attributes_(attributes) {
    adapterKey_ = createHelper(adapterHostKey, attributes, switchId);
    live_ = true;
  }

  // Create a new one from adapter key, adapter host key and attributes
  SaiObject(
      const typename SaiObjectTraits::AdapterKey& adapterKey,
      const typename SaiObjectTraits::AdapterHostKey& adapterHostKey,
      const typename SaiObjectTraits::CreateAttributes& attributes)
      : adapterKey_(adapterKey),
        adapterHostKey_(adapterHostKey),
        attributes_(attributes) {
    live_ = true;
  }

  bool live() const {
    return live_;
  }

 public:
  // Forbid copy construction and copy assignment
  SaiObject(const SaiObject& other) = delete;
  SaiObject& operator=(const SaiObject& other) = delete;

  // Implemented in terms of move assignment
  SaiObject(SaiObject&& other) {
    *this = std::move(other);
  }

  SaiObject& operator=(SaiObject&& other) {
    if (LIKELY(other.live_)) {
      // TODO(borisb): move members instead of copy?!
      adapterKey_ = other.adapterKey();
      adapterHostKey_ = other.adapterHostKey();
      attributes_ = other.attributes();
      live_ = true;
      other.live_ = false;
    } else {
      live_ = false;
    }
    return *this;
  }

  ~SaiObject() {
    if (live_) {
      remove();
    }
    live_ = false;
  }

  void setAttributes(
      const typename SaiObjectTraits::CreateAttributes& newAttributes,
      bool skipHwWrite = false) {
    if (UNLIKELY(!live_)) {
      XLOG(FATAL) << "Attempted to setAttributes on non-live SaiObject";
    }
    tupleForEach(
        [this, skipHwWrite](const auto& attr) {
          checkAndSetAttribute(attr, skipHwWrite);
        },
        newAttributes);
    attributes_ = newAttributes;
  }

  template <typename AttrT>
  void setAttribute(AttrT&& attr, bool skipHwWrite = false) {
    checkAndSetAttribute(std::forward<AttrT>(attr), skipHwWrite);
  }

  template <typename AttrT>
  void setOptionalAttribute(AttrT&& attr, bool skipHwWrite = false) {
    checkAndSetAttribute(
        std::optional<AttrT>{std::forward<AttrT>(attr)}, skipHwWrite);
  }

  void release() {
    live_ = false;
  }

  const typename SaiObjectTraits::AdapterKey& adapterKey() const {
    if (UNLIKELY(!live_)) {
      XLOG(FATAL) << "Attempted to get Adapter Key on non-live SaiObject";
    }
    return adapterKey_;
  }

  const typename SaiObjectTraits::AdapterHostKey& adapterHostKey() const {
    if (UNLIKELY(!live_)) {
      XLOG(FATAL) << "Attempted to get Adapter Host Key of non-live SaiObject";
    }
    return adapterHostKey_;
  }

  const typename SaiObjectTraits::CreateAttributes& attributes() const {
    if (UNLIKELY(!live_)) {
      XLOG(FATAL) << "Attempted to get attributes of non-live SaiObject";
    }
    return attributes_;
  }

  auto getPublisherKey() const {
    static_assert(
        IsObjectPublisher<SaiObjectTraits>::value,
        "object must be pubisher to notify destroy");
    if constexpr (!std::is_same_v<
                      typename PublisherKey<SaiObjectTraits>::custom_type,
                      std::monostate>) {
      return publisherKey_;
    } else {
      return adapterHostKey_;
    }
  }

  void setCustomPublisherKey(
      const typename PublisherKey<SaiObjectTraits>::custom_type& key) {
    publisherKey_ = key;
  }

  void notifyBeforeDestroy() const {
    static_assert(
        IsObjectPublisher<SaiObjectTraits>::value,
        "object must be pubisher to notify destroy");
    auto publisherKey = getPublisherKey();
    auto& publisher = facebook::fboss::SaiObjectEventPublisher::getInstance()
                          ->get<SaiObjectTraits>();
    publisher.notifyDelete(publisherKey);
  }

  void notifyAfterCreate(
      const std::shared_ptr<const SaiObject<SaiObjectTraits>>& object) const {
    static_assert(
        IsObjectPublisher<SaiObjectTraits>::value,
        "object must be pubisher to notify destroy");
    CHECK(this == object.get());
    auto publisherKey = getPublisherKey();
    auto& publisher = facebook::fboss::SaiObjectEventPublisher::getInstance()
                          ->get<SaiObjectTraits>();
    publisher.notifyCreate(publisherKey, object);
  }

  folly::dynamic adapterHostKeyToFollyDynamic();

  static typename SaiObjectTraits::AdapterHostKey follyDynamicToAdapterHostKey(
      const folly::dynamic&);

  void setIgnoreMissingInHwOnDelete(bool ignore) {
    ignoreMissingInHwOnDelete_ = ignore;
  }

  void setSkipRemove(bool skipRemove) {
    skipRemove_ = skipRemove;
  }

  bool isOwnedByAdapter() const {
    return ownedByAdapter_;
  }

  void setOwnedByAdapter(bool ownedByAdapter) {
    ownedByAdapter_ = ownedByAdapter;
  }

  template <typename AttrT>
  static void bulkSetAttributes(
      std::vector<typename SaiObjectTraits::AdapterKey>& adapterKeys,
      std::vector<AttrT>& attributes) {
    auto& api =
        SaiApiTable::getInstance()->getApi<typename SaiObjectTraits::SaiApiT>();
    api.bulkSetAttributes(adapterKeys, attributes);
  }

 protected:
  template <typename AttrT>
  void checkAndSetAttribute(AttrT&& newAttr, bool skipHwWrite) {
    auto& oldAttr = std::get<std::decay_t<AttrT>>(attributes_);
    XLOGF(
        DBG5,
        "checkAndSetAttribute ({}): oldAttr: {}; newAttr: {}",
        adapterKey_,
        oldAttr,
        newAttr);
    if (oldAttr != newAttr) {
      // CREATE_ONLY attributes cannot be set after create, so only update the
      // store. This matters on warm boot when the adapter has no getter (e.g.
      // brcm-sai for SAI_NEXT_HOP_GROUP_ATTR_HIERARCHICAL_NEXTHOP): reload
      // stores nullopt and the first apply looks like a change. Every input
      // that decides a create-only value must be part of the adapter host key,
      // so a real value change yields a new object rather than a set on this
      // one. Marking an attribute IsCreateOnlyAttribute asserts that property.
      if constexpr (!IsCreateOnlyAttribute<std::decay_t<AttrT>>::value) {
        if (!skipHwWrite) {
          setAttributeInHardware(newAttr);
        }
      }
      oldAttr = std::forward<AttrT>(newAttr);
    }
  }
  void remove() {
    if constexpr (IsObjectPublisher<SaiObjectTraits>::value) {
      notifyBeforeDestroy();
    }
    if (isOwnedByAdapter() || skipRemove_) {
      return;
    }
    if constexpr (!IsSaiObjectOwnedByAdapter<SaiObjectTraits>::value) {
      auto& api = SaiApiTable::getInstance()
                      ->getApi<typename SaiObjectTraits::SaiApiT>();
      try {
        api.remove(adapterKey_);
      } catch (const SaiApiError& e) {
        if (ignoreMissingInHwOnDelete_ &&
            e.getSaiStatus() == SAI_STATUS_ITEM_NOT_FOUND) {
          // Ignore error if entry was already removed from HW. One scenario
          // where this can occur is the following
          // - We learn a MAC and install it in HW
          // - Later we get a state update to transform this into a STATIC FDB
          // entry
          // - Meanwhile the dynamic MAC ages out and gets deleted
          // - While processing the state delta for changed MAC entry, we try
          // to delete the dynamic entry before adding static entry
          XLOGF(
              INFO,
              "Ignoring not found error on {} remove, entry already "
              "removed from hardware",
              adapterKey_);
        } else {
          throw;
        }
      }
    }
  }

  template <ObjectIdSaiObject T = SaiObjectTraits>
  typename T::AdapterKey createHelper(
      const typename T::AdapterHostKey& k,
      const typename T::CreateAttributes& attributes,
      sai_object_id_t switchId) {
    auto& api =
        SaiApiTable::getInstance()->getApi<typename SaiObjectTraits::SaiApiT>();
    return api.template create<T>(attributes, switchId);
  }

  template <EntryStructSaiObject T = SaiObjectTraits>
  typename T::AdapterKey createHelper(
      const typename T::AdapterHostKey& k,
      const typename T::CreateAttributes& attributes,
      sai_object_id_t switchId) {
    // Entry structs have AdapterKey = AdapterHostKey
    static_assert(
        std::is_same_v<typename T::AdapterHostKey, typename T::AdapterKey>,
        "SAI objects which use an entry struct must have "
        "AdapterKey == AdapterHostKey == entry struct");
    auto& api =
        SaiApiTable::getInstance()->getApi<typename SaiObjectTraits::SaiApiT>();
    api.template create<T>(k, attributes);
    return k;
  }

 private:
  template <typename AttrT>
  void setAttributeInHardware(const AttrT& newAttr) {
    auto& api =
        SaiApiTable::getInstance()->getApi<typename SaiObjectTraits::SaiApiT>();
    api.setAttribute(adapterKey(), newAttr);
  }
  template <typename AttrT>
  void setAttributeInHardware(const std::optional<AttrT>& newAttrOpt) {
    if (newAttrOpt) {
      // set only if optional attribute is provided
      setAttributeInHardware(newAttrOpt.value());
    }
  }
  bool live_{false};
  bool ownedByAdapter_{IsSaiObjectOwnedByAdapter<SaiObjectTraits>::value};
  // For some object types we can ignore missing in HW errors
  // on when deleting.
  bool ignoreMissingInHwOnDelete_{false};
  // For some object types we want to skip remove call when deleting
  bool skipRemove_{false};
  typename SaiObjectTraits::AdapterKey adapterKey_;
  typename SaiObjectTraits::AdapterHostKey adapterHostKey_;
  typename SaiObjectTraits::CreateAttributes attributes_;
  typename PublisherKey<SaiObjectTraits>::custom_type publisherKey_{};
};

/*
 * A couple helpful macros to make using the attributes tuple in SaiObjects
 * much more palatable.
 * compare:
 *
 * auto speed = GET_ATTR(Port, Speed, object.attributes());
 * to
 * auto speed =
 *   std::get<SaiPortTraits::Attributes::Speed>(object.attributes()).value();
 *
 * or even worse:
 *
 * auto mac = GET_OPT_ATTR(VlanRouterInterfaceface, SrcMac,
 *    object.attributes());
 * to
 * auto mac =
 *   std::get<std::optional<SaiVlanRouterInterfaceTraits::Attributes::SrcMac>>(
 *     object.attributes()).value().value();
 */
#define GET_ATTR(obj, attr, tuple) \
  std::get<Sai##obj##Traits::Attributes::attr>(tuple).value()

#define GET_OPT_ATTR(obj, attr, tuple)                               \
  std::get<std::optional<Sai##obj##Traits::Attributes::attr>>(tuple) \
      .value()                                                       \
      .value()

/*
 * A typical SAI object is shared ptr to SaiObject for a given ObjectTrait.
 * This trait defines SaiObjectType for object trait.
 */
template <typename ObjectTrait>
using SaiObjectPtr = typename std::shared_ptr<SaiObject<ObjectTrait>>;

/*
 * Defines SaiObjectType for condition object trait.
 * A condition object trait is composed of more than one object traits,
 * typically of same api. An example is next hop trait, which is condition trait
 * with two taits : ip next hop trait and mpls next hop traits This requires
 * SaiObjectType of condition object trait be a variant, where each member of
 * variant is a SaiObjectType of member object trait
 */
template <typename... ConditionObjectTrait>
struct ConditionSaiObjectType {
  static_assert(
      (... && SaiObjectHasConditionalAttributes<ConditionObjectTrait>::value),
      "non condition object trait can not use ConditionAdapterHostKeyTraits");
  using type = typename std::variant<SaiObjectPtr<ConditionObjectTrait>...>;
};

/*
 * Specialization of above trait to properly variant with elements in the same
 * order as defined in condition object trait */
template <typename... ConditionObjectTrait>
struct ConditionSaiObjectType<ConditionObjectTraits<ConditionObjectTrait...>> {
  using type = typename ConditionSaiObjectType<ConditionObjectTrait...>::type;
};
} // namespace facebook::fboss
