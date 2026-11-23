// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include <algorithm>

#include <folly/Conv.h>
#include <thrift/lib/cpp2/Thrift.h>
#include <thrift/lib/cpp2/op/Get.h>

namespace facebook::fboss::fsdb {

template <typename>
struct ThriftLeafVisitor;

/**
 * Enumeration
 */
template <typename Node>
struct ThriftLeafVisitor<apache::thrift::type::enum_t<Node>> {
  template <typename Func>
  static void
  visit(std::vector<std::string>& path, const Node& node, Func&& f) {
    using Tag = apache::thrift::type::enum_t<Node>;
    f(path, Tag{}, node);
  }
};

/**
 * Set
 */
template <typename ValueTag>
struct ThriftLeafVisitor<apache::thrift::type::set<ValueTag>> {
  template <typename Node, typename Func>
  static void
  visit(std::vector<std::string>& path, const Node& node, Func&& f) {
    for (auto& val : node) {
      path.push_back(folly::to<std::string>(val));
      f(path, ValueTag{}, val);
      path.pop_back();
    }
  }
};

/**
 * List
 */
template <typename ValueTag>
struct ThriftLeafVisitor<apache::thrift::type::list<ValueTag>> {
  template <typename Node, typename Func>
  static void
  visit(std::vector<std::string>& path, const Node& node, Func&& f) {
    for (int i = 0; i < node.size(); ++i) {
      path.push_back(folly::to<std::string>(i));
      ThriftLeafVisitor<ValueTag>::visit(
          path, node.at(i), std::forward<Func>(f));
      path.pop_back();
    }
  }
};

/**
 * Map
 */
template <typename KeyTag, typename MappedTag>
struct ThriftLeafVisitor<apache::thrift::type::map<KeyTag, MappedTag>> {
  template <typename Node, typename Func>
  static void
  visit(std::vector<std::string>& path, const Node& node, Func&& f) {
    for (const auto& [key, val] : node) {
      path.push_back(folly::to<std::string>(key));
      ThriftLeafVisitor<MappedTag>::visit(path, val, std::forward<Func>(f));
      path.pop_back();
    }
  }
};

/**
 * Variant
 */
template <typename Node>
struct ThriftLeafVisitor<apache::thrift::type::union_t<Node>> {
  template <typename Func>
  static void
  visit(std::vector<std::string>& path, const Node& node, Func&& f) {
    apache::thrift::op::invoke_by_field_id<Node>(
        static_cast<apache::thrift::FieldId>(node.getType()),
        [&]<class Id>(Id) {
          path.emplace_back(apache::thrift::op::get_field_name<Node, Id>());
          ThriftLeafVisitor<apache::thrift::op::get_type_tag<Node, Id>>::visit(
              path,
              *apache::thrift::op::get<Id, Node>(node),
              std::forward<Func>(f));
          path.pop_back();
        },
        [] {
          // // union is __EMPTY__
        });
  }
};

/**
 * Structure
 */
template <typename Node>
struct ThriftLeafVisitor<apache::thrift::type::struct_t<Node>> {
  template <typename Func>
  static void visit(const Node& node, Func&& f) {
    std::vector<std::string> path;
    visit(path, node, std::forward<Func>(f));
  }

  template <typename Func>
  static void
  visit(std::vector<std::string>& path, const Node& node, Func&& f) {
    apache::thrift::op::for_each_field_id<Node>([&]<class Id>(Id) {
      auto val = apache::thrift::op::get<Id, Node>(node);
      if constexpr (apache::thrift::detail::is_optional_field_ref_v<
                        decltype(val)>) {
        // skip optional fields that are not set
        if (!val.has_value()) {
          return;
        }
      }
      // Look for the expected member name
      path.emplace_back(apache::thrift::op::get_field_name<Node, Id>());

      // Recurse further
      ThriftLeafVisitor<apache::thrift::op::get_type_tag<Node, Id>>::visit(
          path, *val, std::forward<Func>(f));

      path.pop_back();
    });
  }
};

/**
 * Cpp.Type
 */
template <typename T, typename Tag>
struct ThriftLeafVisitor<apache::thrift::type::cpp_type<T, Tag>>
    : public ThriftLeafVisitor<Tag> {};

/**
 * Primitives - fallback specialization
 * - string / binary
 * - floating_point
 * - integral
 */
template <typename Tag>
struct ThriftLeafVisitor {
  static_assert(
      apache::thrift::type::is_a_v<Tag, apache::thrift::type::primitive_c>,
      "expected primitive type");

  template <typename Node, typename Func>
  static void
  visit(std::vector<std::string>& path, const Node& node, Func&& f) {
    f(path, Tag{}, node);
  }
};

template <typename Node>
using RootThriftLeafVisitor =
    ThriftLeafVisitor<apache::thrift::type::struct_t<Node>>;

} // namespace facebook::fboss::fsdb
