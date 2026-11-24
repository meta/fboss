#pragma once

#include <folly/IPAddress.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <memory>
#include <type_traits>

#include <fboss/agent/AddressUtil.h>
#include "fboss/agent/Constants.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/NodeMap.h"

#include "common/network/if/gen-cpp2/Address_types.h"

#include "fboss/agent/gen-cpp2/switch_state_types.h"

#include "fboss/thrift_cow/nodes/Types.h"

namespace facebook::fboss {

class SwitchState;

namespace switch_state_tags = apache::thrift::ident;
namespace switch_config_tags = apache::thrift::ident;
namespace ctrl_if_tags = apache::thrift::ident;
namespace common_if_tags = apache::thrift::ident;

template <typename NodeT>
struct IsThriftCowNode {
  static constexpr bool value = false;
};

template <typename NodeT>
static constexpr bool kIsThriftCowNode = IsThriftCowNode<NodeT>::value;

#define USE_THRIFT_COW(NODE)            \
  class NODE;                           \
  template <>                           \
  struct IsThriftCowNode<NODE> {        \
    static constexpr bool value = true; \
  };

// All thrift state maps need to have an items field
inline constexpr folly::StringPiece kItems{"items"};

class ThriftyUtils {
 public:
  static void renameField(
      folly::dynamic& dyn,
      const std::string& oldName,
      const std::string& newName) {
    if (auto it = dyn.find(oldName); it != dyn.items().end()) {
      // keep old value for backwards compat
      dyn.insert(newName, it->second);
    }
  }

  template <typename EnumT>
  static void changeEnumToInt(folly::dynamic& dyn, const std::string& key) {
    if (auto it = dyn.find(key); it != dyn.items().end()) {
      EnumT val;
      if (apache::thrift::util::tryParseEnum(it->second.asString(), &val)) {
        it->second = static_cast<int>(val);
      }
    }
  }

  template <typename EnumT>
  static void changeEnumToString(folly::dynamic& dyn, const std::string& key) {
    if (auto it = dyn.find(key); it != dyn.items().end()) {
      try {
        it->second = apache::thrift::util::enumName(
            static_cast<EnumT>(it->second.asInt()));
      } catch (const folly::TypeError&) {
        // no problem, it was already a string
      }
    }
  }

  template <typename T>
  static bool listEq(
      std::vector<std::shared_ptr<T>> a,
      std::vector<std::shared_ptr<T>> b) {
    return a.size() == b.size() &&
        std::equal(
               std::begin(a),
               std::end(a),
               std::begin(b),
               std::end(b),
               [](const auto& lhs, const auto& rhs) { return *lhs == *rhs; });
  }

  template <typename T>
  static bool listEq(
      std::optional<std::vector<std::shared_ptr<T>>> a,
      std::optional<std::vector<std::shared_ptr<T>>> b) {
    return (!a.has_value() && !b.has_value()) ||
        (a.has_value() && b.has_value() && listEq(*a, *b));
  }

  static bool nodeNeedsMigration(const folly::dynamic& dyn) {
    return !dyn.isObject() ||
        !dyn.getDefault(kThriftySchemaUpToDate, true /* migrated to thrift */)
             .asBool();
  }

  // given folly dynamic of ip, return binary address
  static network::thrift::BinaryAddress toThriftBinaryAddress(
      const folly::dynamic& ip) {
    return network::toBinaryAddress(folly::IPAddress(ip.asString()));
  }

  // given folly dynamic of binary address, return folly ip address
  static folly::IPAddress toFollyIPAddress(const folly::dynamic& addr) {
    auto jsonStr = folly::toJson(addr);
    auto inBuf =
        folly::IOBuf::wrapBufferAsValue(jsonStr.data(), jsonStr.size());
    auto thriftBinaryAddr = apache::thrift::SimpleJSONSerializer::deserialize<
        network::thrift::BinaryAddress>(folly::io::Cursor{&inBuf});
    return network::toIPAddress(thriftBinaryAddr);
  }

  // return folly dynamic of binary address, given binary address
  static folly::dynamic toFollyDynamic(
      const network::thrift::BinaryAddress& addr) {
    std::string jsonStr;
    apache::thrift::SimpleJSONSerializer::serialize(addr, &jsonStr);
    return folly::parseJson(jsonStr);
  }

  // return folly dynamic of ip address, given ip address
  static folly::dynamic toFollyDynamic(const folly::IPAddress& addr) {
    return addr.str();
  }

  // ip address represented dyn is converted to type AddrT
  template <typename AddrT>
  static AddrT convertAddress(const folly::dynamic& dyn) {
    static_assert(
        std::is_same_v<AddrT, folly::IPAddress> ||
            std::is_same_v<AddrT, network::thrift::BinaryAddress>,
        "invalid address type");
    if constexpr (std::is_same_v<AddrT, folly::IPAddress>) {
      return toFollyIPAddress(dyn);
    } else {
      return toThriftBinaryAddress(dyn);
    }
  }

  // change dynamic representation to that of AddrT
  template <typename AddrT>
  static void translateTo(folly::dynamic& dyn) {
    AddrT addr = convertAddress<AddrT>(dyn);
    dyn = ThriftyUtils::toFollyDynamic(addr);
  }

  static void stringToInt(folly::dynamic& dyn, const std::string& field) {
    if (auto it = dyn.find(field); it != dyn.items().end()) {
      dyn[field] = dyn[field].asInt();
    }
  }

  static void thriftyMapToFollyArray(
      folly::dynamic& dyn,
      const std::string& map,
      const std::string& key,
      const std::string& value) {
    if (dyn.find(map) != dyn.items().end()) {
      folly::dynamic follyArray = folly::dynamic::array;
      for (const auto& follyMapKey : dyn[map].keys()) {
        folly::dynamic jsonEntry = folly::dynamic::object;
        jsonEntry[key] = follyMapKey;
        jsonEntry[value] = dyn[map][follyMapKey];
        follyArray.push_back(jsonEntry);
      }
      dyn[map] = follyArray;
    }
  }

  static void follyArraytoThriftyMap(
      folly::dynamic& newDyn,
      const std::string& map,
      const std::string& key,
      const std::string& value) {
    if (newDyn.find(map) != newDyn.items().end()) {
      folly::dynamic follyMap = folly::dynamic::object;
      for (const auto& entry : newDyn[map]) {
        follyMap[entry[key].asString()] = entry[value];
      }
      newDyn[map] = follyMap;
    }
  }

  static auto constexpr kThriftySchemaUpToDate = "__thrifty_schema_uptodate";

  static folly::CIDRNetwork toCIDRNetwork(const IpPrefix& prefix) {
    return folly::CIDRNetwork(
        network::toIPAddress(*prefix.ip()), *prefix.prefixLength());
  }

  static IpPrefix toIpPrefix(const folly::CIDRNetwork& cidr) {
    IpPrefix prefix{};
    prefix.ip() = network::toBinaryAddress(cidr.first);
    prefix.prefixLength() = cidr.second;
    return prefix;
  }
};

template <typename Derived, typename ThriftT>
class ThriftyFields {
 public:
  using ThriftType = ThriftT;
  using FieldsT = Derived;

  ThriftyFields() {}
  explicit ThriftyFields(const ThriftT& data) : data_(data) {}

  virtual ~ThriftyFields() = default;

  std::string str() const {
    auto obj = toThrift();
    std::string jsonStr;
    apache::thrift::SimpleJSONSerializer::serialize(obj, &jsonStr);
    return jsonStr;
  }

  virtual ThriftT toThrift() const = 0;

  const ThriftT& data() const {
    return data_;
  }

  ThriftT& writableData() {
    return data_;
  }

 protected:
  ThriftT data_;
};

template <typename NODE, typename TType>
struct ThriftStructNode
    : public thrift_cow::
          ThriftStructNode<TType, thrift_cow::TypeIdentity<NODE>> {
  using Base =
      thrift_cow::ThriftStructNode<TType, thrift_cow::TypeIdentity<NODE>>;
  using Base::Base;
};

template <
    typename MAP,
    typename TypeClass,
    typename MapThrift,
    typename NODE =
        thrift_cow::ThriftStructNode<typename MapThrift::mapped_type>,
    typename NodeThrift = typename MapThrift::mapped_type>
struct ThriftMapNodeTraits {
  using TC = TypeClass;
  using Type = MapThrift;
  using KeyType = typename Type::key_type;
  using KeyCompare = std::less<KeyType>;
  using Node = NODE;
  // for structure
  template <typename...>
  struct ValueTraits {
    using default_type =
        thrift_cow::ThriftStructNode<typename MapThrift::mapped_type>;
    using struct_type = NODE;
    using type = std::shared_ptr<struct_type>;
    using isChild = std::true_type;
  };
  template <typename... T>
  using ConvertToNodeTraits = ValueTraits<T...>;
};

template <
    typename MULTIMAP,
    typename MultiMapTypeClass,
    typename MultiMapThrift,
    typename MAP>
struct ThriftMultiSwitchMapNodeTraits {
  using TC = MultiMapTypeClass;
  using Type = MultiMapThrift;
  using KeyType = typename Type::key_type;
  using KeyCompare = std::less<KeyType>;
  using InnerMap = MAP;
  using Node = InnerMap;

  // for map
  template <typename...>
  struct ValueTraits {
    using default_type = thrift_cow::ThriftMapNode<thrift_cow::ThriftMapTraits<
        false,
        typename MAP::Traits::TC,
        typename MAP::Traits::Type>>;
    using map_type = MAP;
    using type = std::shared_ptr<map_type>;
    using isChild = std::true_type;
  };
  template <typename... T>
  using ConvertToNodeTraits = ValueTraits<T...>;
};

template <
    typename MAP,
    typename Traits,
    typename Resolver = thrift_cow::TypeIdentity<MAP>>
struct ThriftMapNode : public thrift_cow::ThriftMapNode<Traits, Resolver> {
  using Base = thrift_cow::ThriftMapNode<Traits, Resolver>;
  using Node = typename Base::mapped_type::element_type;
  using KeyType = typename Traits::KeyType;
  using ThriftType = typename Base::ThriftType;
  using Base::Base;
  using NodeContainer = typename Base::Fields::StorageType;

  virtual ~ThriftMapNode() = default;
  ThriftMapNode(const ThriftMapNode&) = delete;
  ThriftMapNode& operator=(const ThriftMapNode&) = delete;
  ThriftMapNode(ThriftMapNode&&) = delete;
  ThriftMapNode& operator=(ThriftMapNode&&) = delete;

  void addNode(std::shared_ptr<Node> node) {
    auto key = node->getID();
    addNode(key, node);
  }

  void addNode(const KeyType& key, std::shared_ptr<Node> node) {
    auto ret = this->insert(key, std::move(node));
    if (!ret.second) {
      throw FbossError("node ", key, " already exists");
    }
  }

  void removeNode(std::shared_ptr<Node> node) {
    removeNode(node->getID());
  }

  void updateNode(std::shared_ptr<Node> node) {
    auto id = node->getID();
    updateNode(id, node);
  }

  void updateNode(const KeyType& key, std::shared_ptr<Node> node) {
    this->ref(key) = std::move(node);
  }

  std::shared_ptr<Node> removeNode(const KeyType& key) {
    auto node = removeNodeIf(key);
    if (!node) {
      throw FbossError("Cannot remove node ", key, " does not exist");
    }
    return node;
  }

  std::shared_ptr<Node> removeNodeIf(KeyType key) {
    auto iter = this->find(key);
    if (iter == this->end()) {
      return nullptr;
    }
    auto node = iter->second;
    this->erase(iter);
    return node;
  }

  const std::shared_ptr<Node>& getNode(KeyType key) const {
    auto iter = this->find(key);
    if (iter == this->end()) {
      throw FbossError("Cannot get node ", key, " does not exist");
    }
    return iter->second;
  }

  std::shared_ptr<Node> getNodeIf(KeyType key) const {
    auto iter = this->find(key);
    if (iter == this->end()) {
      return nullptr;
    }
    return iter->second;
  }
};

#define RESOLVE_STRUCT_MEMBER(NODE, MEMBER_NAME, MEMBER_TYPE)                \
  template <>                                                                \
  struct thrift_cow::ResolveMemberType<NODE, MEMBER_NAME> : std::true_type { \
    using type = MEMBER_TYPE;                                                \
  };

template <typename MultiMapName, template <typename> typename TypeFor>
struct InnerMap {
  using type =
      typename TypeFor<MultiMapName>::element_type::mapped_type::element_type;
};

template <
    typename MAP,
    typename Traits,
    typename Resolver = thrift_cow::TypeIdentity<MAP>>
struct ThriftMultiSwitchMapNode : public ThriftMapNode<MAP, Traits, Resolver> {
  using Base = ThriftMapNode<MAP, Traits, Resolver>;
  using Base::Base;
  using InnerMap = typename Traits::InnerMap;
  using InnerNode = typename InnerMap::Traits::Node;

  static auto kNodeName() {
    return apache::thrift::op::get_class_name<typename InnerNode::ThriftType>();
  }

  void addNode(
      std::shared_ptr<InnerNode> node,
      const HwSwitchMatcher& matcher) {
    const auto& key = matcher.matcherString();
    auto mitr = this->find(key);
    if (mitr == this->end()) {
      mitr = this->insert(key, std::make_shared<InnerMap>()).first;
    }
    auto& innerMap = mitr->second;
    innerMap->addNode(std::move(node));
  }

  void updateNode(
      std::shared_ptr<InnerNode> node,
      const HwSwitchMatcher& matcher) {
    const auto& key = matcher.matcherString();
    auto mitr = this->find(key);
    if (mitr == this->end()) {
      throw FbossError("No map found for switchIds: ", key);
    }
    auto& innerMap = mitr->second;
    innerMap->updateNode(std::move(node));
  }

  void removeNode(const typename InnerMap::Traits::KeyType& key) {
    for (auto mitr = this->begin(); mitr != this->end(); ++mitr) {
      if (mitr->second->remove(key)) {
        return;
      }
    };
    throw FbossError("Node ", kNodeName(), " to remove not found: ", key);
  }

  void removeNode(std::shared_ptr<InnerNode> node) {
    removeNode(node->getID());
  }

  std::shared_ptr<InnerNode> getNodeIf(
      const typename InnerMap::Traits::KeyType& key) const {
    for (auto mnitr = this->cbegin(); mnitr != this->cend(); ++mnitr) {
      auto node = mnitr->second->getNodeIf(key);
      if (node) {
        return node;
      }
    }
    return nullptr;
  }

  std::shared_ptr<InnerNode> getNode(
      const typename InnerMap::Traits::KeyType& key) const {
    auto node = getNodeIf(key);
    if (!node) {
      throw FbossError("Node ", kNodeName(), " to get not found: ", key);
    }
    return node;
  }

  std::pair<std::shared_ptr<InnerNode>, HwSwitchMatcher> getNodeAndScope(
      const typename InnerMap::Traits::KeyType& key) const {
    for (auto mnitr = this->cbegin(); mnitr != this->cend(); ++mnitr) {
      auto nitr = mnitr->second->find(key);
      if (nitr != mnitr->second->cend()) {
        return std::make_pair(nitr->second, HwSwitchMatcher(mnitr->first));
      }
    }
    throw FbossError(
        "Node ", kNodeName(), " and scope to get not found: ", key);
  }

  size_t numNodes() const {
    size_t cnt = 0;
    for (auto mnitr = this->cbegin(); mnitr != this->cend(); ++mnitr) {
      cnt += mnitr->second->size();
    }
    return cnt;
  }

  void addMapNode(
      std::shared_ptr<InnerMap> node,
      const HwSwitchMatcher& matcher) {
    Base::addNode(matcher.matcherString(), node);
  }

  std::shared_ptr<InnerMap> getMapNodeIf(const HwSwitchMatcher& matcher) const {
    return Base::getNodeIf(matcher.matcherString());
  }

  void updateMapNode(
      std::shared_ptr<InnerMap> node,
      const HwSwitchMatcher& matcher) {
    Base::updateNode(matcher.matcherString(), node);
  }

  std::shared_ptr<MAP> clone() const {
    auto cloned = Base::clone();
    for (auto mnitr = cloned->cbegin(); mnitr != cloned->cend(); ++mnitr) {
      if (!mnitr->second->isPublished()) {
        continue;
      }
      auto newMap = mnitr->second->clone();
      cloned->ref(mnitr->first) = newMap;
    }
    return cloned;
  }

  std::shared_ptr<InnerMap> getAllNodes() const {
    auto nodes = std::make_shared<InnerMap>();
    for (const auto& [_, innerMap] : std::as_const(*this)) {
      for (const auto& [_2, node] : std::as_const(*innerMap)) {
        nodes->addNode(node);
      }
    }
    return nodes;
  }
};

} // namespace facebook::fboss
