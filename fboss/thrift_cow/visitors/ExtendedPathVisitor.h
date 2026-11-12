// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include <folly/logging/xlog.h>
#include <type_traits>
#include "folly/Conv.h"

#include <fboss/thrift_cow/nodes/NodeUtils.h>
#include <fboss/thrift_cow/nodes/Serializer.h>
#include <fboss/thrift_cow/visitors/ExtendedPathMatcher.h>
#include <fboss/thrift_cow/visitors/VisitorUtils.h>
#include <re2/re2.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/Thrift.h>
#include <thrift/lib/cpp2/TypeClass.h>
#include <thrift/lib/cpp2/op/Get.h>
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"

namespace facebook::fboss::thrift_cow {

struct HybridNodeType;

/*
 * This visitor takes a path object and a thrift type and is able to
 * traverse the object and then run the visitor function against the
 * final type.
 *
 * TODO: add error result handling similar to PathVisitor.
 */

template <typename TC>
struct ExtendedPathVisitor;

struct NodeType;
struct FieldsType;

struct ExtPathVisitorOptions {
  explicit ExtPathVisitorOptions(bool outputIdPaths = false)
      : outputIdPaths(outputIdPaths) {}

  bool outputIdPaths;
};

namespace epv_detail {

using ExtPathIter = typename std::vector<fsdb::OperPathElem>::const_iterator;

template <typename Func>
struct ExtVisitImplParams {
 public:
  explicit ExtVisitImplParams(
      epv_detail::ExtPathIter begin,
      epv_detail::ExtPathIter end,
      const ExtPathVisitorOptions& options,
      Func& visitorFn)
      : begin(begin), end(end), options(options), visitorFn(visitorFn) {}

  epv_detail::ExtPathIter begin;
  epv_detail::ExtPathIter end;
  const ExtPathVisitorOptions& options;
  Func& visitorFn;

  std::vector<std::string> path;
};

inline std::string pathElemToString(const fsdb::OperPathElem& elem) {
  switch (elem.getType()) {
    case fsdb::OperPathElem::Type::raw:
      return elem.raw().value();
    case fsdb::OperPathElem::Type::regex:
      return elem.regex().value();
    case fsdb::OperPathElem::Type::any:
      return "*";
    case fsdb::OperPathElem::Type::__EMPTY__:
      throw std::runtime_error("Illformed extended path");
    default:
      throw std::runtime_error("Unexpected OperPathElem::Type");
  }
}

inline std::string makeVisitedPathString(
    ExtPathIter begin,
    ExtPathIter end,
    ExtPathIter cursor,
    const std::vector<std::string>& path) {
  std::vector<std::string> pathElems;
  for (auto it = begin; it != end; ++it) {
    pathElems.push_back(pathElemToString(*it));
  }
  return folly::to<std::string>(
      "request.path: ",
      folly::join("/", pathElems),
      ", visited: ",
      folly::join("/", path),
      " at: ",
      (cursor == end ? "(end)" : pathElemToString(*cursor)));
}

template <typename TC, typename Node, typename Func>
void visitNode(Node& node, ExtVisitImplParams<Func>& params, ExtPathIter cursor)
  requires(std::is_same_v<typename Node::CowType, NodeType>)
{
  if (cursor == params.end) {
    try {
      params.visitorFn(params.path, node);
    } catch (const std::exception& ex) {
      std::string message = folly::to<std::string>(
          "ExtendedPathVisitor exception: ",
          makeVisitedPathString(params.begin, params.end, cursor, params.path),
          ", visitorFn exception: ",
          ex.what());
      throw std::runtime_error(message);
    }
    return;
  }

  if constexpr (std::is_const_v<Node>) {
    ExtendedPathVisitor<TC>::visit(*node.getFields(), params, cursor);
  } else {
    ExtendedPathVisitor<TC>::visit(*node.writableFields(), params, cursor);
  }
}

template <typename Enum>
std::optional<std::string> matchingEnumToken(
    const Enum& e,
    const fsdb::OperPathElem& elem) {
  // TODO: should we allow regex/raw matching by int value?
  auto enumName = apache::thrift::util::enumName(e);
  if (thrift_cow::matchesStrToken(enumName, elem)) {
    return enumName;
  }

  auto enumValue = folly::to<std::string>(static_cast<int>(e));
  if (thrift_cow::matchesStrToken(enumValue, elem)) {
    return enumValue;
  }

  return std::nullopt;
}

template <typename TC, typename TType>
std::optional<std::string> matchingToken(
    const TType& val,
    const fsdb::OperPathElem& elem) {
  if constexpr (std::is_same_v<TC, apache::thrift::type_class::string>) {
    if (thrift_cow::matchesStrToken(val, elem)) {
      return val;
    }
  } else if constexpr (std::is_same_v<
                           TC,
                           apache::thrift::type_class::enumeration>) {
    return matchingEnumToken(val, elem);
  } else {
    auto strToken = folly::to<std::string>(val);
    if (thrift_cow::matchesStrToken(strToken, elem)) {
      return strToken;
    }
  }

  return std::nullopt;
}

} // namespace epv_detail

/**
 * Set
 */
template <typename ValueTypeClass>
struct ExtendedPathVisitor<apache::thrift::type_class::set<ValueTypeClass>> {
  using TC = apache::thrift::type_class::set<ValueTypeClass>;

  template <typename Node, typename Func>
  static inline void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(std::is_same_v<typename Node::CowType, NodeType>)
  {
    epv_detail::visitNode<TC>(node, params, cursor);
  }

  template <typename Node, typename Func>
  static void visit(
      Node& /* node */,
      epv_detail::ExtVisitImplParams<Func>& /* params */,
      epv_detail::ExtPathIter /* cursor */)
    requires(std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    throw std::runtime_error("Set: not implemented yet");
  }

  template <typename Node, typename Func>
  static void visit(
      Node& /* node */,
      epv_detail::ExtVisitImplParams<Func>& /* params */,
      epv_detail::ExtPathIter /* cursor */)
    requires(!is_cow_type_v<Node> && !is_field_type_v<Node>)
  {
    throw std::runtime_error("Set: not implemented yet");
  }

  template <typename Fields, typename Func>
  static void visit(
      Fields& fields,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(
        is_field_type_v<Fields> &&
        std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    const auto& elem = *cursor++;

    for (auto& val : fields) {
      auto matching =
          epv_detail::matchingToken<ValueTypeClass>(val->ref(), elem);
      if (matching) {
        params.path.push_back(*matching);
        ExtendedPathVisitor<ValueTypeClass>::visit(*val, params, cursor);
        params.path.pop_back();
      }
    }
  }
};

/**
 * List
 */
template <typename ValueTypeClass>
struct ExtendedPathVisitor<apache::thrift::type_class::list<ValueTypeClass>> {
  using TC = apache::thrift::type_class::list<ValueTypeClass>;

  template <typename Node, typename Func>
  static inline void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(std::is_same_v<typename Node::CowType, NodeType>)
  {
    epv_detail::visitNode<TC>(node, params, cursor);
  }

  template <typename Node, typename Func>
  static void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    const auto& tObj = node.ref();
    const auto& elem = *cursor++;
    for (int i = 0; i < tObj.size(); ++i) {
      auto matching =
          epv_detail::matchingToken<apache::thrift::type_class::integral>(
              i, elem);
      if (matching) {
        params.path.push_back(*matching);

        ExtendedPathVisitor<ValueTypeClass>::visit(tObj.at(i), params, cursor);
        params.path.pop_back();
      }
    }
  }

  template <typename Node, typename Func>
  static void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(!is_cow_type_v<Node> && !is_field_type_v<Node>)
  {
    const auto& elem = *cursor++;
    for (int i = 0; i < node.size(); ++i) {
      auto matching =
          epv_detail::matchingToken<apache::thrift::type_class::integral>(
              i, elem);
      if (matching) {
        params.path.push_back(*matching);

        ExtendedPathVisitor<ValueTypeClass>::visit(node.at(i), params, cursor);
        params.path.pop_back();
      }
    }
  }

  template <typename Fields, typename Func>
  static void visit(
      Fields& fields,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(
        is_field_type_v<Fields> &&
        std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    const auto& elem = *cursor++;
    for (int i = 0; i < fields.size(); ++i) {
      auto matching =
          epv_detail::matchingToken<apache::thrift::type_class::integral>(
              i, elem);
      if (matching) {
        params.path.push_back(*matching);
        if constexpr (std::is_const_v<Fields>) {
          const auto& next = *fields.ref(i);
          ExtendedPathVisitor<ValueTypeClass>::visit(next, params, cursor);
        } else {
          ExtendedPathVisitor<ValueTypeClass>::visit(
              *fields.ref(i), params, cursor);
        }
        params.path.pop_back();
      }
    }
  }
};

/**
 * Map
 */
template <typename KeyTypeClass, typename MappedTypeClass>
struct ExtendedPathVisitor<
    apache::thrift::type_class::map<KeyTypeClass, MappedTypeClass>> {
  using TC = apache::thrift::type_class::map<KeyTypeClass, MappedTypeClass>;

  template <typename Node, typename Func>
  static inline void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(std::is_same_v<typename Node::CowType, NodeType>)
  {
    epv_detail::visitNode<TC>(node, params, cursor);
  }

  template <typename Node, typename Func>
  static void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(!is_cow_type_v<Node> && !is_field_type_v<Node>)
  {
    if (cursor == params.end) {
      SerializableWrapper<TC, std::remove_const_t<Node>> wrapper(
          *const_cast<std::remove_const_t<Node>*>(&node));
      try {
        params.visitorFn(params.path, wrapper);
      } catch (const std::exception& ex) {
        std::string message = folly::to<std::string>(
            "ExtendedPathVisitor exception: ",
            epv_detail::makeVisitedPathString(
                params.begin, params.end, cursor, params.path),
            ", visitorFn exception: ",
            ex.what());
        throw std::runtime_error(message);
      }
      return;
    }

    const auto& elem = *cursor++;
    if (elem.raw()) {
      using KeyT = typename std::remove_cvref_t<decltype(node)>::key_type;
      auto key = folly::to<KeyT>(*elem.raw());
      if (node.find(key) != node.end()) {
        params.path.push_back(folly::to<std::string>(*elem.raw()));
        ExtendedPathVisitor<MappedTypeClass>::visit(
            node.at(key), params, cursor);
        params.path.pop_back();
      }
      return;
    }

    for (auto& [key, val] : node) {
      auto matching = epv_detail::matchingToken<KeyTypeClass>(key, elem);
      if (matching) {
        params.path.push_back(*matching);

        ExtendedPathVisitor<MappedTypeClass>::visit(val, params, cursor);

        params.path.pop_back();
      }
    }
  }

  template <typename Node, typename Func>
  static void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    if (cursor == params.end) {
      ExtendedPathVisitor<TC>::visit(node.ref(), params, cursor);
      return;
    }

    const auto& elem = *cursor++;
    if (elem.raw()) {
      using KeyT = typename std::remove_cvref_t<decltype(node.ref())>::key_type;
      auto key = folly::to<KeyT>(*elem.raw());
      if (node.ref().find(key) != node.ref().end()) {
        params.path.push_back(folly::to<std::string>(*elem.raw()));
        ExtendedPathVisitor<MappedTypeClass>::visit(
            node.ref().at(key), params, cursor);
        params.path.pop_back();
      }
      return;
    }

    for (auto& [key, val] : node.ref()) {
      auto matching = epv_detail::matchingToken<KeyTypeClass>(key, elem);
      if (matching) {
        params.path.push_back(*matching);

        ExtendedPathVisitor<MappedTypeClass>::visit(val, params, cursor);

        params.path.pop_back();
      }
    }
  }

  template <typename Fields, typename Func>
  static void visit(
      Fields& fields,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(
        is_field_type_v<Fields> &&
        std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    const auto& elem = *cursor++;
    if (elem.raw()) {
      using KeyT = typename std::remove_cvref_t<decltype(fields)>::key_type;
      auto key = folly::to<KeyT>(*elem.raw());
      if (fields.find(key) != fields.end()) {
        params.path.push_back(folly::to<std::string>(*elem.raw()));
        auto& val = fields.ref(key);

        // ensure we propagate constness, since children will have type
        // const shared_ptr<T>, not shared_ptr<const T>.
        if constexpr (std::is_const_v<Fields>) {
          const auto& next = *val;
          ExtendedPathVisitor<MappedTypeClass>::visit(next, params, cursor);
        } else {
          ExtendedPathVisitor<MappedTypeClass>::visit(*val, params, cursor);
        }
        params.path.pop_back();
      }
      return;
    }
    for (auto& [key, val] : fields) {
      auto matching = epv_detail::matchingToken<KeyTypeClass>(key, elem);
      if (matching) {
        params.path.push_back(*matching);

        // ensure we propagate constness, since children will have type
        // const shared_ptr<T>, not shared_ptr<const T>.
        if constexpr (std::is_const_v<Fields>) {
          const auto& next = *val;
          ExtendedPathVisitor<MappedTypeClass>::visit(next, params, cursor);
        } else {
          ExtendedPathVisitor<MappedTypeClass>::visit(*val, params, cursor);
        }

        params.path.pop_back();
      }
    }
  }
};

/**
 * Variant
 */
template <>
struct ExtendedPathVisitor<apache::thrift::type_class::variant> {
  using TC = apache::thrift::type_class::variant;

  template <typename Node, typename Func>
  static inline void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(std::is_same_v<typename Node::CowType, NodeType>)
  {
    epv_detail::visitNode<TC>(node, params, cursor);
  }

  template <typename Node, typename Func>
  static void visit(
      Node& /* node */,
      epv_detail::ExtVisitImplParams<Func>& /* params */,
      epv_detail::ExtPathIter /* cursor */)
    requires(std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    throw std::runtime_error("Variant: not implemented yet");
  }

  template <typename Node, typename Func>
  static void visit(
      Node& /* node */,
      epv_detail::ExtVisitImplParams<Func>& /* params */,
      epv_detail::ExtPathIter /* cursor */)
    requires(!is_cow_type_v<Node> && !is_field_type_v<Node>)
  {
    throw std::runtime_error("Variant: not implemented yet");
  }

  template <typename Fields, typename Func>
  static void visit(
      Fields& fields,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(
        is_field_type_v<Fields> &&
        std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    const auto& elem = *cursor++;
    auto raw = elem.raw();
    if (!raw) {
      // Error! wildcards not supported for enum or struct
      return;
    }

    // TODO: A lot of shared logic with PathVisitor. Could we share code?
    using TType = typename Fields::ThriftType;
    visitMember<TType>(*raw, [&]<class Id>(Id) {
      using Traits = typename Fields::template FieldTraits<Id>;
      using TC_ = typename Traits::TC;

      if (folly::to_underlying(fields.type()) !=
          folly::to_underlying(apache::thrift::op::get_field_id_v<TType, Id>)) {
        // TODO: error handling
        return;
      }

      std::string memberName = params.options.outputIdPaths
          ? folly::to<std::string>(folly::to_underlying(
                apache::thrift::op::get_field_id_v<TType, Id>))
          : std::string(apache::thrift::op::get_name_v<TType, Id>);

      params.path.push_back(std::move(memberName));

      // ensure we propagate constness, since children will have type
      // const shared_ptr<T>, not shared_ptr<const T>.
      auto& child = fields.template ref<Id>();
      if constexpr (std::is_const_v<Fields>) {
        const auto& next = *child;
        ExtendedPathVisitor<TC_>::visit(next, params, cursor);
      } else {
        ExtendedPathVisitor<TC_>::visit(*child, params, cursor);
      }

      params.path.pop_back();
    });
  }
};

/**
 * Structure
 */
template <>
struct ExtendedPathVisitor<apache::thrift::type_class::structure> {
  using TC = apache::thrift::type_class::structure;

  template <typename Node, typename Func>
  static inline void visit(
      Node& node,
      epv_detail::ExtPathIter begin,
      epv_detail::ExtPathIter end,
      const ExtPathVisitorOptions& options,
      Func&& f)
    requires(std::is_same_v<typename Node::CowType, NodeType>)
  {
    epv_detail::ExtVisitImplParams<Func> params(begin, end, options, f);
    visit(node, params, begin);
  }

  template <typename Node, typename Func>
  static inline void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(std::is_same_v<typename Node::CowType, NodeType>)
  {
    epv_detail::visitNode<TC>(node, params, cursor);
  }

  template <typename Node, typename Func>
  static void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(!is_cow_type_v<Node> && !is_field_type_v<Node>)
  {
    if (cursor == params.end) {
      // Node is not a Serializable, dispatch with wrapper
      SerializableWrapper<TC, Node> wrapper(node);
      try {
        params.visitorFn(params.path, wrapper);
      } catch (const std::exception& ex) {
        std::string message = folly::to<std::string>(
            "ExtendedPathVisitor exception: ",
            epv_detail::makeVisitedPathString(
                params.begin, params.end, cursor, params.path),
            ", visitorFn exception: ",
            ex.what());
        throw std::runtime_error(message);
      }
      return;
    }

    using T = std::remove_cv_t<Node>;

    const auto& elem = *cursor++;
    auto raw = elem.raw();
    if (!raw) {
      // Error! wildcards not supported for enum or struct
      return;
    }

    // Perform trie search over all members for key
    visitMember<T>(*raw, [&]<class Id>(Id) {
      using TC_ = typename TypeTagToTypeClass<
          apache::thrift::op::get_type_tag<T, Id>>::type;

      // Recurse further
      auto& child = *apache::thrift::op::get<Id>(node);

      std::string memberName = params.options.outputIdPaths
          ? folly::to<std::string>(
                folly::to_underlying(apache::thrift::op::get_field_id_v<T, Id>))
          : std::string(apache::thrift::op::get_name_v<T, Id>);

      params.path.push_back(std::move(memberName));

      ExtendedPathVisitor<TC_>::visit(child, params, cursor);

      params.path.pop_back();
    });
  }

  template <typename Node, typename Func>
  static void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(std::is_same_v<typename Node::CowType, HybridNodeType>)
  {
    if (cursor == params.end) {
      ExtendedPathVisitor<TC>::visit(node.ref(), params, cursor);
      return;
    }

    auto& tObj = node.ref();
    using T = typename Node::ThriftType;

    const auto& elem = *cursor++;
    auto raw = elem.raw();
    if (!raw) {
      // Error! wildcards not supported for enum or struct
      return;
    }

    // Perform trie search over all members for key
    visitMember<T>(*raw, [&]<class Id>(Id) {
      using TC_ = typename TypeTagToTypeClass<
          apache::thrift::op::get_type_tag<T, Id>>::type;

      // Recurse further
      auto& child = *apache::thrift::op::get<Id>(tObj);

      std::string memberName = params.options.outputIdPaths
          ? folly::to<std::string>(
                folly::to_underlying(apache::thrift::op::get_field_id_v<T, Id>))
          : std::string(apache::thrift::op::get_name_v<T, Id>);

      params.path.push_back(std::move(memberName));

      ExtendedPathVisitor<TC_>::visit(child, params, cursor);

      params.path.pop_back();
    });
  }

  template <typename Fields, typename Func>
  static void visit(
      Fields& fields,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(
        is_field_type_v<Fields> &&
        std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    using TType = typename Fields::ThriftType;

    const auto& elem = *cursor++;
    auto raw = elem.raw();
    if (!raw) {
      // Error! wildcards not supported for enum or struct
      return;
    }

    // Perform trie search over all members for key
    visitMember<TType>(*raw, [&]<class Id>(Id) {
      using Traits = typename Fields::template FieldTraits<Id>;
      using TC_ = typename Traits::TC;

      // Recurse further
      auto& child = fields.template ref<Id>();

      if (!child) {
        // child is unset, cannot traverse through missing optional child
        return;
      }
      std::string memberName = params.options.outputIdPaths
          ? folly::to<std::string>(folly::to_underlying(
                apache::thrift::op::get_field_id_v<TType, Id>))
          : std::string(apache::thrift::op::get_name_v<TType, Id>);

      params.path.push_back(std::move(memberName));

      // ensure we propagate constness, since children will have type
      // const shared_ptr<T>, not shared_ptr<const T>.
      if constexpr (std::is_const_v<Fields>) {
        const auto& next = *child;
        ExtendedPathVisitor<TC_>::visit(next, params, cursor);
      } else {
        ExtendedPathVisitor<TC_>::visit(*child, params, cursor);
      }

      params.path.pop_back();
    });
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
struct ExtendedPathVisitor {
  static_assert(
      !std::is_same<apache::thrift::type_class::unknown, TC>::value,
      "No static reflection support for the given type. "
      "Forgot to specify reflection option or include fatal header file? "
      "Refer to thrift/lib/cpp2/reflection/reflection.h");

  template <typename Node, typename Func>
  static void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(is_cow_type_v<Node>)
  {
    try {
      params.visitorFn(params.path, node);
    } catch (const std::exception& ex) {
      std::string message = folly::to<std::string>(
          "ExtendedPathVisitor exception: ",
          epv_detail::makeVisitedPathString(
              params.begin, params.end, cursor, params.path),
          ", visitorFn exception: ",
          ex.what());
      throw std::runtime_error(message);
    }
  }

  template <typename Node, typename Func>
  static void visit(
      Node& node,
      epv_detail::ExtVisitImplParams<Func>& params,
      epv_detail::ExtPathIter cursor)
    requires(!is_cow_type_v<Node>)
  {
    // Node is not a Serializable, dispatch with wrapper
    SerializableWrapper<TC, std::remove_const_t<Node>> wrapper(
        *const_cast<std::remove_const_t<Node>*>(&node));
    try {
      params.visitorFn(params.path, wrapper);
    } catch (const std::exception& ex) {
      std::string message = folly::to<std::string>(
          "ExtendedPathVisitor exception: ",
          epv_detail::makeVisitedPathString(
              params.begin, params.end, cursor, params.path),
          ", visitorFn exception: ",
          ex.what());
      throw std::runtime_error(message);
    }
  }
};

using RootExtendedPathVisitor =
    ExtendedPathVisitor<apache::thrift::type_class::structure>;

} // namespace facebook::fboss::thrift_cow
