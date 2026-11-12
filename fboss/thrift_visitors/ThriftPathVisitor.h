// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include <type_traits>

#include <thrift/lib/cpp2/Thrift.h>
#include <thrift/lib/cpp2/op/Get.h>

namespace facebook::fboss::fsdb {

/*
 * This visitor takes a path object and a thrift type and is able to
 * traverse the object and then run the visitor function against the
 * final type.
 *
 * TODO: examples
 */

struct CreateNodeIfMissing {};

// TODO: enable more details
enum class ThriftTraverseResult {
  OK,
  NON_EXISTENT_NODE,
  INVALID_ARRAY_INDEX,
  INVALID_MAP_KEY,
  INVALID_STRUCT_MEMBER,
  INVALID_VARIANT_MEMBER,
  INCORRECT_VARIANT_MEMBER,
  VISITOR_EXCEPTION,
  INVALID_SET_MEMBER,
};

template <typename T>
inline constexpr bool isConst =
    std::is_const_v<typename std::remove_reference_t<T>>;

template <typename T>
inline constexpr bool shouldCreateNodeIfMissing =
    std::is_same_v<T, CreateNodeIfMissing>;

template <typename Tag, typename Options>
struct ThriftPathVisitor;

/**
 * Enumeration
 */
template <typename T, typename Options>
struct ThriftPathVisitor<apache::thrift::type::enum_t<T>, Options> {
  template <typename Node, typename PathIter, typename Func>
  static ThriftTraverseResult
  visit(Node&& node, PathIter begin, PathIter end, Func&& f)
    requires(std::is_same_v<
             typename std::iterator_traits<PathIter>::value_type,
             std::string>)
  {
    using Tag = apache::thrift::type::enum_t<T>;

    // Check for terminal condition
    if (begin == end) {
      try {
        f(node, Tag{});
        return ThriftTraverseResult::OK;
      } catch (const std::exception&) {
        return ThriftTraverseResult::VISITOR_EXCEPTION;
      }
    }

    return ThriftTraverseResult::NON_EXISTENT_NODE;
  }
};

/**
 * Set
 */
template <typename ValueTag, typename Options>
struct ThriftPathVisitor<apache::thrift::type::set<ValueTag>, Options> {
  template <typename Node, typename PathIter, typename Func>
  static ThriftTraverseResult
  visit(Node&& node, PathIter begin, PathIter end, Func&& f)
    requires(std::is_same_v<
             typename std::iterator_traits<PathIter>::value_type,
             std::string>)
  {
    using Tag = apache::thrift::type::set<ValueTag>;
    using ValueT = typename std::remove_cvref_t<Node>::value_type;

    static_assert(
        !(shouldCreateNodeIfMissing<Options> && isConst<Node>),
        "Cannot use CreateNodeIfMissing option against a const node");
    constexpr auto createNodeIfMissing =
        shouldCreateNodeIfMissing<Options> && !isConst<Node>;

    if (begin == end) {
      try {
        f(node, Tag{});
        return ThriftTraverseResult::OK;
      } catch (const std::exception&) {
        return ThriftTraverseResult::VISITOR_EXCEPTION;
      }
    }

    auto tok = *begin++;
    ValueT value;

    // need to handle enumeration, integral, string types
    if constexpr (apache::thrift::type::
                      is_a_v<ValueTag, apache::thrift::type::enum_c>) {
      if (apache::thrift::util::tryParseEnum(tok, &value)) {
        auto it = node.find(value);
        if (it == node.end()) {
          if constexpr (!createNodeIfMissing) {
            return ThriftTraverseResult::NON_EXISTENT_NODE;
          } else {
            node.insert(value);
          }
        }

        // Recurse further
        return ThriftPathVisitor<ValueTag, Options>::visit(
            value, begin, end, std::forward<Func>(f));
      }
    }

    auto valueTry = folly::tryTo<ValueT>(tok);
    if (!valueTry.hasError()) {
      value = valueTry.value();
      auto it = node.find(value);
      if (it == node.end()) {
        if constexpr (!createNodeIfMissing) {
          return ThriftTraverseResult::NON_EXISTENT_NODE;
        } else {
          node.insert(value);
        }
      }
      // Recurse further
      return ThriftPathVisitor<ValueTag, Options>::visit(
          value, begin, end, std::forward<Func>(f));
    }

    // if we get here, we must have a malformed key
    return ThriftTraverseResult::INVALID_SET_MEMBER;
  }
};

/**
 * List
 */
template <typename ValueTag, typename Options>
struct ThriftPathVisitor<apache::thrift::type::list<ValueTag>, Options> {
  template <typename Node, typename PathIter, typename Func>
  static ThriftTraverseResult
  visit(Node&& node, PathIter begin, PathIter end, Func&& f)
    requires(std::is_same_v<
             typename std::iterator_traits<PathIter>::value_type,
             std::string>)
  {
    using Tag = apache::thrift::type::list<ValueTag>;

    static_assert(
        !(shouldCreateNodeIfMissing<Options> && isConst<Node>),
        "Cannot use CreateNodeIfMissing option against a const node");
    constexpr auto createNodeIfMissing =
        shouldCreateNodeIfMissing<Options> && !isConst<Node>;

    if (begin == end) {
      try {
        f(node, Tag{});
        return ThriftTraverseResult::OK;
      } catch (const std::exception&) {
        return ThriftTraverseResult::VISITOR_EXCEPTION;
      }
    }

    // Parse and pop token. Also check for index bound
    auto index = folly::tryTo<size_t>(*begin++);
    if (index.hasError()) {
      return ThriftTraverseResult::INVALID_ARRAY_INDEX;
    }
    if (index.value() >= node.size()) {
      if constexpr (!createNodeIfMissing) {
        return ThriftTraverseResult::NON_EXISTENT_NODE;
      } else {
        node.resize(index.value());
      }
    }

    // Recurse at a given index
    return ThriftPathVisitor<ValueTag, Options>::visit(
        node.at(index.value()), begin, end, std::forward<Func>(f));
  }
};

/**
 * Map
 */
template <typename KeyTag, typename MappedTag, typename Options>
struct ThriftPathVisitor<
    apache::thrift::type::map<KeyTag, MappedTag>,
    Options> {
  template <typename Node, typename PathIter, typename Func>
  static ThriftTraverseResult
  visit(Node&& node, PathIter begin, PathIter end, Func&& f)
    requires(std::is_same_v<
             typename std::iterator_traits<PathIter>::value_type,
             std::string>)
  {
    using Tag = apache::thrift::type::map<KeyTag, MappedTag>;

    static_assert(
        !(shouldCreateNodeIfMissing<Options> && isConst<Node>),
        "Cannot use CreateNodeIfMissing option against a const node");
    constexpr auto createNodeIfMissing =
        shouldCreateNodeIfMissing<Options> && !isConst<Node>;

    if (begin == end) {
      try {
        f(node, Tag{});
        return ThriftTraverseResult::OK;
      } catch (const std::exception&) {
        return ThriftTraverseResult::VISITOR_EXCEPTION;
      }
    }

    using KeyT = typename std::remove_cvref_t<Node>::key_type;
    // Get key
    auto token = *begin++;
    auto key = folly::tryTo<KeyT>(token);
    if (!key.hasValue()) {
      if constexpr (apache::thrift::type::
                        is_a_v<KeyTag, apache::thrift::type::enum_c>) {
        // special handling for enum keyed maps
        KeyT value;
        if (apache::thrift::util::tryParseEnum(token, &value)) {
          key = value;
        } else {
          return ThriftTraverseResult::INVALID_MAP_KEY;
        }
      } else {
        return ThriftTraverseResult::INVALID_MAP_KEY;
      }
    }

    if (node.find(*key) == node.end()) {
      if constexpr (!createNodeIfMissing) {
        return ThriftTraverseResult::NON_EXISTENT_NODE;
      } else {
        node[*key];
      }
    }

    // Recurse further
    return ThriftPathVisitor<MappedTag, Options>::visit(
        node.at(*key), begin, end, std::forward<Func>(f));
  }
};

/**
 * Variant
 */
template <typename T, typename Options>
struct ThriftPathVisitor<apache::thrift::type::union_t<T>, Options> {
  template <typename Node, typename PathIter, typename Func>
  static ThriftTraverseResult
  visit(Node&& node, PathIter begin, PathIter end, Func&& f)
    requires(std::is_same_v<
             typename std::iterator_traits<PathIter>::value_type,
             std::string>)
  {
    static_assert(
        !(shouldCreateNodeIfMissing<Options> && isConst<Node>),
        "Cannot use CreateNodeIfMissing option against a const node");
    constexpr auto createNodeIfMissing =
        shouldCreateNodeIfMissing<Options> && !isConst<Node>;

    if (begin == end) {
      try {
        f(node, apache::thrift::type::union_t<T>{});
        return ThriftTraverseResult::OK;
      } catch (const std::exception&) {
        return ThriftTraverseResult::VISITOR_EXCEPTION;
      }
    }

    auto result = ThriftTraverseResult::INVALID_VARIANT_MEMBER;

    // Get key
    const auto& key = *begin++;

    apache::thrift::op::for_each_field_id<T>([&]<class Id>(Id id) {
      const auto memberName = apache::thrift::op::get_name_v<T, Id>;

      if (memberName != key) {
        return;
      }

      auto field = apache::thrift::op::get<Id>(std::forward<Node>(node));
      if (static_cast<apache::thrift::FieldId>(node.getType()) != id) {
        if constexpr (!createNodeIfMissing) {
          result = ThriftTraverseResult::INCORRECT_VARIANT_MEMBER;
          return;
        } else {
          // switch union value to point at new path.
          field.ensure();
        }
      }

      result =
          ThriftPathVisitor<apache::thrift::op::get_type_tag<T, Id>, Options>::
              visit(*field, begin, end, std::forward<Func>(f));
    });
    return result;
  }
};

/**
 * Structure
 */
template <typename T, typename Options>
struct ThriftPathVisitor<apache::thrift::type::struct_t<T>, Options> {
  template <typename Node, typename PathIter, typename Func>
  static ThriftTraverseResult
  visit(Node&& node, PathIter begin, PathIter end, Func&& f)
    requires(std::is_same_v<
             typename std::iterator_traits<PathIter>::value_type,
             std::string>)
  {
    static_assert(
        !(shouldCreateNodeIfMissing<Options> && isConst<Node>),
        "Cannot use CreateNodeIfMissing option against a const node");
    constexpr auto createNodeIfMissing =
        shouldCreateNodeIfMissing<Options> && !isConst<Node>;

    if (begin == end) {
      try {
        f(node, apache::thrift::type::struct_t<T>{});
        return ThriftTraverseResult::OK;
      } catch (const std::exception&) {
        return ThriftTraverseResult::VISITOR_EXCEPTION;
      }
    }

    // Get key
    auto key = *begin++;

    // Perform linear search over all members for key
    ThriftTraverseResult result = ThriftTraverseResult::INVALID_STRUCT_MEMBER;
    apache::thrift::op::for_each_field_id<T>([&]<class Id>(Id) {
      // Look for the expected member name
      const auto memberName = apache::thrift::op::get_name_v<T, Id>;
      if (memberName != key) {
        return;
      }

      // Check for optionality
      auto field = apache::thrift::op::get<Id>(std::forward<Node>(node));
      if (apache::thrift::op::get_value_or_null(field) == nullptr) {
        if constexpr (!createNodeIfMissing) {
          result = ThriftTraverseResult::NON_EXISTENT_NODE;
          return;
        } else {
          field.ensure();
        }
      }

      // Recurse further
      using Tag = apache::thrift::op::get_type_tag<T, Id>;
      result = ThriftPathVisitor<Tag, Options>::visit(
          *field, begin, end, std::forward<Func>(f));
    });

    return result;
  }
};

/**
 * Cpp.Type
 */
template <typename T, typename Tag, typename Options>
struct ThriftPathVisitor<apache::thrift::type::cpp_type<T, Tag>, Options>
    : public ThriftPathVisitor<Tag, Options> {};

/**
 * Primitives - fallback specialization
 * - string / binary
 * - floating_point
 * - integral
 */
template <typename Tag, typename Options>
struct ThriftPathVisitor {
  static_assert(
      apache::thrift::type::is_a_v<Tag, apache::thrift::type::primitive_c>,
      "expected primitive type");

  template <typename Node, typename PathIter, typename Func>
  static ThriftTraverseResult
  visit(Node&& node, PathIter begin, PathIter end, Func&& f)
    requires(std::is_same_v<
             typename std::iterator_traits<PathIter>::value_type,
             std::string>)
  {
    if (begin == end) {
      try {
        f(std::forward<Node>(node), Tag{});
        return ThriftTraverseResult::OK;
      } catch (const std::exception&) {
        return ThriftTraverseResult::VISITOR_EXCEPTION;
      }
    }
    return ThriftTraverseResult::NON_EXISTENT_NODE;
  }
};

template <typename Node>
using RootThriftPathVisitor =
    ThriftPathVisitor<apache::thrift::type::struct_t<Node>, void>;

template <typename Node, typename Options>
using RootThriftPathVisitorWithOptions =
    ThriftPathVisitor<apache::thrift::type::struct_t<Node>, Options>;

} // namespace facebook::fboss::fsdb
