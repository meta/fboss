// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h>
#include <fboss/thrift_cow/gen-cpp2/patch_types.h>
#include <fboss/thrift_cow/nodes/NodeUtils.h>
#include <fboss/thrift_cow/nodes/Serializer.h>
#include <fboss/thrift_cow/visitors/PatchHelpers.h>
#include <fboss/thrift_cow/visitors/gen-cpp2/results_types.h>

#include <fboss/thrift_cow/visitors/VisitorUtils.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/op/Get.h>
#include <optional>

#pragma once

namespace facebook::fboss::thrift_cow {

template <typename TC>
struct PatchApplier;

struct NodeType;
struct FieldsType;
struct HybridNodeType;

namespace pa_detail {

template <typename TC, typename Node>
inline PatchApplyResult
patchNode(Node& n, ByteBuffer&& buf, const fsdb::OperProtocol& protocol) {
  if constexpr (is_cow_type_v<Node>) {
    n.fromEncodedBuf(protocol, std::move(buf));
  } else {
    n = deserializeBuf<TC, Node>(protocol, std::move(buf));
  }
  return PatchApplyResult::OK;
}

std::vector<int> getSortedIndices(const ListPatch& node);
} // namespace pa_detail

class PatchTraverser {
 public:
  void push(int tok);
  void push(std::string tok);
  PatchApplyResult traverseResult(const PatchApplyResult& result);
  void pop();

  PatchApplyResult currentResult() {
    return curResult_;
  }

 private:
  std::vector<std::string> curPath_;
  PatchApplyResult curResult_{PatchApplyResult::OK};
};

/*
 * Map
 */
template <typename KeyTypeClass, typename MappedTypeClass>
struct PatchApplier<
    apache::thrift::type_class::map<KeyTypeClass, MappedTypeClass>> {
  using TC = apache::thrift::type_class::map<KeyTypeClass, MappedTypeClass>;

  template <typename Node>
  static inline PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol = fsdb::OperProtocol::COMPACT) {
    PatchTraverser traverser;
    return apply(node, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        is_cow_type_v<Node> &&
        std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    auto& underlying = node.ref();
    return PatchApplier<TC>::apply(
        underlying, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        !is_cow_type_v<Node> ||
        !std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    if (patch.getType() == PatchNode::Type::val) {
      return pa_detail::patchNode<TC>(node, patch.move_val(), protocol);
    }
    if (patch.getType() != PatchNode::Type::map_node) {
      return PatchApplyResult::INVALID_PATCH_TYPE;
    }

    using key_type = typename Node::key_type;

    decompressPatch(patch);
    auto mapPatch = patch.move_map_node();
    for (auto&& [key, childPatch] : *std::move(mapPatch).children()) {
      traverser.push(key);
      if (auto parsedKey = tryParseKey<key_type, KeyTypeClass>(key)) {
        traverser.traverseResult(applyChildPatch(
            node, std::move(*parsedKey), std::move(childPatch), protocol));
      } else {
        traverser.traverseResult(PatchApplyResult::KEY_PARSE_ERROR);
      }
      traverser.pop();
    }

    return traverser.currentResult();
  }

 private:
  template <typename Node, typename KeyT>
  static PatchApplyResult applyChildPatch(
      Node& node,
      KeyT key,
      PatchNode&& childPatch,
      const fsdb::OperProtocol& protocol)
    requires(is_cow_type_v<Node>)
  {
    if (childPatch.getType() == PatchNode::Type::del) {
      node.remove(std::move(key));
      return PatchApplyResult::OK;
    } else {
      node.modifyTyped(key);
      return PatchApplier<MappedTypeClass>::apply(
          *node.ref(std::move(key)), std::move(childPatch), protocol);
    }
  }

  template <typename Node, typename KeyT>
  static PatchApplyResult applyChildPatch(
      Node& node,
      KeyT key,
      PatchNode&& childPatch,
      const fsdb::OperProtocol& protocol)
    requires(!is_cow_type_v<Node>)
  {
    if (childPatch.getType() == PatchNode::Type::del) {
      node.erase(std::move(key));
      return PatchApplyResult::OK;
    } else {
      return PatchApplier<MappedTypeClass>::apply(
          node[std::move(key)], std::move(childPatch), protocol);
    }
  }
};

/**
 * List
 */
template <typename ValueTypeClass>
struct PatchApplier<apache::thrift::type_class::list<ValueTypeClass>> {
  using TC = apache::thrift::type_class::list<ValueTypeClass>;

  template <typename Node>
  static inline PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol = fsdb::OperProtocol::COMPACT) {
    PatchTraverser traverser;
    return apply(node, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        is_cow_type_v<Node> &&
        std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    auto& underlying = node.ref();
    return PatchApplier<TC>::apply(
        underlying, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        !is_cow_type_v<Node> ||
        !std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    if (patch.getType() == PatchNode::Type::val) {
      return pa_detail::patchNode<TC>(node, patch.move_val(), protocol);
    }
    if (patch.getType() != PatchNode::Type::list_node) {
      return PatchApplyResult::INVALID_PATCH_TYPE;
    }

    decompressPatch(patch);
    auto listPatch = patch.move_list_node();

    // In case of removals, we want to make sure we resolve later indices first.
    // So first we need to sort the indices
    std::vector<int> indices = pa_detail::getSortedIndices(listPatch);

    // Iterate through sorted keys and access values in the map
    for (const int index : indices) {
      traverser.push(index);
      auto& childPatch = listPatch.children()->at(index);
      traverser.traverseResult(
          applyChildPatch(node, index, std::move(childPatch), protocol));
      traverser.pop();
    }

    return traverser.currentResult();
  }

 private:
  template <typename Node>
  static PatchApplyResult applyChildPatch(
      Node& node,
      int32_t index,
      PatchNode&& childPatch,
      const fsdb::OperProtocol& protocol)
    requires(is_cow_type_v<Node>)
  {
    if (childPatch.getType() == PatchNode::Type::del) {
      node.remove(index);
      return PatchApplyResult::OK;
    } else {
      node.modify(index);
      return PatchApplier<ValueTypeClass>::apply(
          *node.ref(index), std::move(childPatch), protocol);
    }
  }

  template <typename Node>
  static PatchApplyResult applyChildPatch(
      Node& node,
      int32_t index,
      PatchNode&& childPatch,
      const fsdb::OperProtocol& protocol)
    requires(!is_cow_type_v<Node>)
  {
    if (childPatch.getType() == PatchNode::Type::del) {
      node.erase(node.begin() + index);
      return PatchApplyResult::OK;
    } else {
      if (node.size() <= index) {
        node.resize(index + 1);
      }
      return PatchApplier<ValueTypeClass>::apply(
          node.at(index), std::move(childPatch), protocol);
    }
    return PatchApplyResult::OK;
  }
};

/**
 * Set
 */
template <typename KeyTypeClass>
struct PatchApplier<apache::thrift::type_class::set<KeyTypeClass>> {
  using TC = apache::thrift::type_class::set<KeyTypeClass>;

  template <typename Node>
  static inline PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol = fsdb::OperProtocol::COMPACT) {
    PatchTraverser traverser;
    return apply(node, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        is_cow_type_v<Node> &&
        std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    auto& underlying = node.ref();
    return PatchApplier<TC>::apply(
        underlying, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        !is_cow_type_v<Node> ||
        !std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    if (patch.getType() == PatchNode::Type::val) {
      return pa_detail::patchNode<TC>(node, patch.move_val(), protocol);
    }
    if (patch.getType() != PatchNode::Type::set_node) {
      return PatchApplyResult::INVALID_PATCH_TYPE;
    }

    using key_type = typename Node::key_type;

    decompressPatch(patch);
    auto setPatch = patch.move_set_node();
    for (auto&& [key, childPatch] : *std::move(setPatch).children()) {
      traverser.push(key);
      if (std::optional<key_type> value =
              tryParseKey<key_type, KeyTypeClass>(key)) {
        traverser.traverseResult(
            applyChildPatch(node, std::move(*value), std::move(childPatch)));
      } else {
        traverser.traverseResult(PatchApplyResult::KEY_PARSE_ERROR);
      }
      traverser.pop();
    }

    return traverser.currentResult();
  }

 private:
  template <typename Node, typename KeyT>
  static PatchApplyResult
  applyChildPatch(Node& node, KeyT key, PatchNode&& childPatch) {
    // We only support sets of primitives
    // lets not recurse and just handle add/remove here
    if (childPatch.getType() == PatchNode::Type::del) {
      node.erase(std::move(key));
      return PatchApplyResult::OK;
    } else if (childPatch.getType() == PatchNode::Type::val) {
      node.emplace(std::move(key));
      return PatchApplyResult::OK;
    } else {
      return PatchApplyResult::INVALID_PATCH_TYPE;
    }
  }
};

/**
 * Variant
 */
template <>
struct PatchApplier<apache::thrift::type_class::variant> {
  using TC = apache::thrift::type_class::variant;

  template <typename Node>
  static inline PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol = fsdb::OperProtocol::COMPACT) {
    PatchTraverser traverser;
    return apply(node, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        is_cow_type_v<Node> &&
        std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    auto& underlying = node.ref();
    return PatchApplier<TC>::apply(
        underlying, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        !is_cow_type_v<Node> ||
        !std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    if (patch.getType() == PatchNode::Type::val) {
      return pa_detail::patchNode<TC>(node, patch.move_val(), protocol);
    }
    if (patch.getType() != PatchNode::Type::variant_node) {
      return PatchApplyResult::INVALID_PATCH_TYPE;
    }

    auto variantPatch = patch.variant_node();
    auto key = *variantPatch->id();
    traverser.push(key);
    traverser.traverseResult(
        applyChildPatch(node, std::move(*variantPatch), protocol));
    traverser.pop();

    return traverser.currentResult();
  }

 private:
  template <typename Node>
  static PatchApplyResult applyChildPatch(
      Node& node,
      VariantPatch&& childPatch,
      const fsdb::OperProtocol& protocol)
    requires(is_cow_type_v<Node>)
  {
    using Fields = typename Node::Fields;
    using TType = typename Fields::ThriftType;
    auto key = *childPatch.id();
    PatchApplyResult result = PatchApplyResult::INVALID_VARIANT_MEMBER;
    apache::thrift::op::invoke_by_field_id<TType>(
        apache::thrift::FieldId(key),
        [&]<class Id>(Id) {
          using Traits = typename Fields::template FieldTraits<Id>;
          using TC_ = typename Traits::TC;

          node.template modify<Id>();
          auto& child = node.template ref<Id>();

          if (!child) {
            // child is unset, cannot traverse through missing optional child
            result = PatchApplyResult::NON_EXISTENT_NODE;
            return;
          }
          result = PatchApplier<TC_>::apply(
              *child, std::move(*childPatch.child()), protocol);
        },
        [&]() { /* not found - result stays INVALID_VARIANT_MEMBER */ });
    return result;
  }

  template <typename Node>
  static PatchApplyResult applyChildPatch(
      Node& node,
      VariantPatch&& childPatch,
      const fsdb::OperProtocol& protocol)
    requires(!is_cow_type_v<Node>)
  {
    PatchApplyResult result = PatchApplyResult::INVALID_VARIANT_MEMBER;
    auto key = *childPatch.id();
    using T = std::remove_cvref_t<Node>;
    apache::thrift::op::for_each_field_id<T>([&]<class Id>(Id) {
      constexpr auto fid = apache::thrift::op::get_field_id_v<T, Id>;
      using TC_ = typename TypeTagToTypeClass<
          apache::thrift::op::get_type_tag<T, Id>>::type;

      if (folly::to_underlying(fid) != key) {
        return;
      }

      // switch union value to point at new path.
      if (folly::to_underlying(node.getType()) != folly::to_underlying(fid)) {
        apache::thrift::op::get<Id>(node).ensure();
      }

      result = PatchApplier<TC_>::apply(
          *apache::thrift::op::get<Id>(node),
          std::move(*childPatch.child()),
          protocol);
    });
    return result;
  }
};

/**
 * Structure
 */
template <>
struct PatchApplier<apache::thrift::type_class::structure> {
  using TC = apache::thrift::type_class::structure;

  template <typename Node>
  static inline PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol = fsdb::OperProtocol::COMPACT) {
    PatchTraverser traverser;
    return apply(node, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        is_cow_type_v<Node> &&
        std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    auto& underlying = node.ref();
    return PatchApplier<TC>::apply(
        underlying, std::move(patch), protocol, traverser);
  }

  template <typename Node>
  static PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& traverser)
    requires(
        !is_cow_type_v<Node> ||
        !std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    if (patch.getType() == PatchNode::Type::val) {
      return pa_detail::patchNode<TC>(node, patch.move_val(), protocol);
    }
    if (patch.getType() != PatchNode::Type::struct_node) {
      return PatchApplyResult::INVALID_PATCH_TYPE;
    }

    decompressPatch(patch);
    auto structPatch = patch.move_struct_node();
    for (auto&& [key, childPatch] : *std::move(structPatch).children()) {
      traverser.push(key);
      traverser.traverseResult(
          applyChildPatch(node, key, std::move(childPatch), protocol));
      traverser.pop();
    }
    return traverser.currentResult();
  }

  template <typename Node>
  static PatchApplyResult applyChildPatch(
      Node& node,
      int16_t childKey,
      PatchNode&& childPatch,
      const fsdb::OperProtocol& protocol)
    requires(is_cow_type_v<Node>)
  {
    using Fields = typename Node::Fields;
    using TType = typename Fields::ThriftType;
    PatchApplyResult result = PatchApplyResult::INVALID_STRUCT_MEMBER;
    apache::thrift::op::invoke_by_field_id<TType>(
        apache::thrift::FieldId(childKey),
        [&, childPatch = std::move(childPatch)]<class Id>(Id) mutable {
          using Traits = typename Fields::template FieldTraits<Id>;
          using TC_ = typename Traits::TC;

          if (childPatch.getType() == PatchNode::Type::del) {
            node.template remove<Id>();
            result = PatchApplyResult::OK;
            return;
          }

          auto& child = node.template modify<Id>();

          if constexpr (Fields::template HasSkipThriftCow<Id>) {
            auto& underlying = child->ref();
            result = PatchApplier<TC_>::apply(
                underlying, std::move(childPatch), protocol);
            return;
          } else {
            result = PatchApplier<TC_>::apply(
                *child, std::move(childPatch), protocol);
          }
        },
        [&]() { /* not found - result stays INVALID_STRUCT_MEMBER */ });
    return result;
  }

  template <typename Node>
  static PatchApplyResult applyChildPatch(
      Node& node,
      int16_t childKey,
      PatchNode&& childPatch,
      const fsdb::OperProtocol& protocol)
    requires(!is_cow_type_v<Node>)
  {
    PatchApplyResult result = PatchApplyResult::INVALID_STRUCT_MEMBER;

    using T = std::remove_cvref_t<Node>;
    // Perform linear search over all members for key
    apache::thrift::op::invoke_by_field_id<T>(
        apache::thrift::FieldId(childKey),
        [&]<class Id>(Id) mutable {
          constexpr bool isOptional =
              apache::thrift::type::is_optional_or_union_field_v<T, Id>;
          using TC_ = typename TypeTagToTypeClass<
              apache::thrift::op::get_type_tag<T, Id>>::type;

          if (childPatch.getType() == PatchNode::Type::del) {
            if constexpr (isOptional) {
              apache::thrift::op::get<Id>(node).reset();
              result = PatchApplyResult::OK;
            } else {
              result = PatchApplyResult::INVALID_PATCH_TYPE;
            }
            return;
          }

          // If optional and not set, create it first
          if constexpr (isOptional) {
            apache::thrift::op::get<Id>(node).ensure();
          }

          // Recurse further
          result = PatchApplier<TC_>::apply(
              *apache::thrift::op::get<Id>(node),
              std::move(childPatch),
              protocol);
        },
        [&]() { /* not found - result stays INVALID_STRUCT_MEMBER */ });
    return result;
  }
};

/**
 * Primitives - fallback specialization
 * - string / binary
 * - floating_point
 * - integral
 * - enumeration
 */
template <typename TC>
struct PatchApplier {
  static_assert(
      !std::is_same<apache::thrift::type_class::unknown, TC>::value,
      "No static reflection support for the given type. "
      "Forgot to specify reflection option or include fatal header file? "
      "Refer to thrift/lib/cpp2/reflection/reflection.h");

  template <typename Node>
  static inline PatchApplyResult apply(
      Node& node,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol = fsdb::OperProtocol::COMPACT) {
    PatchTraverser traverser;
    return apply(node, std::move(patch), protocol, traverser);
  }

  template <typename Fields>
  static PatchApplyResult apply(
      Fields& fields,
      PatchNode&& patch,
      const fsdb::OperProtocol& protocol,
      PatchTraverser& /* traverser */) {
    if (patch.getType() != PatchNode::Type::val) {
      return PatchApplyResult::INVALID_PATCH_TYPE;
    }
    if constexpr (is_cow_type_v<Fields>) {
      if constexpr (Fields::immutable) {
        return PatchApplyResult::PATCHING_IMMUTABLE_NODE;
      }
    }
    return pa_detail::patchNode<TC>(fields, patch.move_val(), protocol);
  }
};

using RootPatchApplier = PatchApplier<apache::thrift::type_class::structure>;
} // namespace facebook::fboss::thrift_cow
