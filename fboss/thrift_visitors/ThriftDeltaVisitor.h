// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include <algorithm>

#include <boost/iterator/function_output_iterator.hpp>

#include <thrift/lib/cpp2/Thrift.h>
#include <thrift/lib/cpp2/op/Get.h>

namespace facebook::fboss::fsdb {

/*
 * This visitor takes two thrift objects, comparing each field and
 * then running the provided function against any paths that differ
 * between the two objects.
 *
 * This can be used to find changes between two versions of the state
 * so that we can serve any related subscriptions.
 *
 * NOTE: this is not super efficient right now. In particular, we use
 * operator== to recursively compare the objects, which can be
 * nontrivially expensive.
 */

template <typename>
struct ThriftDeltaVisitor;

/**
 * Enumeration
 */
template <typename Node>
struct ThriftDeltaVisitor<apache::thrift::type::enum_t<Node>> {
  template <typename Func>
  static bool visit(
      std::vector<std::string>& path,
      const Node& oldNode,
      const Node& newNode,
      Func&& f) {
    using Tag = apache::thrift::type::enum_t<Node>;

    if (oldNode != newNode) {
      f(path, Tag{}, std::make_optional(oldNode), std::make_optional(newNode));
      return true;
    }

    return false;
  }
};

/**
 * Set
 */
template <typename ValueTag>
struct ThriftDeltaVisitor<apache::thrift::type::set<ValueTag>> {
  template <typename Node, typename Func>
  static bool visit(
      std::vector<std::string>& path,
      const Node& oldNode,
      const Node& newNode,
      Func&& f) {
    // assuming that sets cannot contain any complex types, so just
    // calculating difference.
    bool hasDifferences{false};
    std::set_symmetric_difference(
        oldNode.begin(),
        oldNode.end(),
        newNode.begin(),
        newNode.end(),
        boost::make_function_output_iterator([&]<class Value>(
                                                 const Value& val) {
          hasDifferences = true;
          path.push_back(folly::to<std::string>(val));
          if (std::find(oldNode.begin(), oldNode.end(), val) != oldNode.end()) {
            f(path,
              ValueTag{},
              std::make_optional(val),
              std::optional<Value>());
          } else {
            f(path,
              ValueTag{},
              std::optional<Value>(),
              std::make_optional(val));
          }
          path.pop_back();
        }));

    if (hasDifferences) {
      using Tag = apache::thrift::type::set<ValueTag>;
      f(path, Tag{}, std::make_optional(oldNode), std::make_optional(newNode));
    }
    return hasDifferences;
  }
};

/**
 * List
 */
template <typename ValueTag>
struct ThriftDeltaVisitor<apache::thrift::type::list<ValueTag>> {
  template <typename Node, typename Func>
  static bool visit(
      std::vector<std::string>& path,
      const Node& oldNode,
      const Node& newNode,
      Func&& f) {
    size_t minSize = std::min(oldNode.size(), newNode.size());

    bool hasDifferences{false};

    if (oldNode.size() > newNode.size()) {
      hasDifferences = true;
      for (int i = minSize; i < oldNode.size(); ++i) {
        path.push_back(folly::to<std::string>(i));
        f(path,
          ValueTag{},
          std::make_optional(oldNode.at(i)),
          std::optional<typename Node::value_type>());
        path.pop_back();
      }
    } else if (oldNode.size() < newNode.size()) {
      hasDifferences = true;
      for (int i = minSize; i < newNode.size(); ++i) {
        path.push_back(folly::to<std::string>(i));
        f(path,
          ValueTag{},
          std::optional<typename Node::value_type>(),
          std::make_optional(newNode.at(i)));
        path.pop_back();
      }
    }

    for (int i = 0; i < minSize; ++i) {
      path.push_back(folly::to<std::string>(i));
      auto differs = ThriftDeltaVisitor<ValueTag>::visit(
          path, oldNode.at(i), newNode.at(i), std::forward<Func>(f));
      hasDifferences = hasDifferences || differs;
      path.pop_back();
    }

    if (hasDifferences) {
      using Tag = apache::thrift::type::list<ValueTag>;
      f(path, Tag{}, std::make_optional(oldNode), std::make_optional(newNode));
    }

    return hasDifferences;
  }
};

/**
 * Map
 */
template <typename KeyTag, typename MappedTag>
struct ThriftDeltaVisitor<apache::thrift::type::map<KeyTag, MappedTag>> {
  template <typename Node, typename Func>
  static bool visit(
      std::vector<std::string>& path,
      const Node& oldNode,
      const Node& newNode,
      Func&& f) {
    bool hasDifferences{false};

    for (const auto& [key, val] : oldNode) {
      path.push_back(folly::to<std::string>(key));
      if (auto it = newNode.find(key); it != newNode.end()) {
        bool different = ThriftDeltaVisitor<MappedTag>::visit(
            path, val, it->second, std::forward<Func>(f));
        hasDifferences = hasDifferences || different;
      } else {
        hasDifferences = true;
        f(path,
          MappedTag{},
          std::make_optional(val),
          std::optional<std::remove_cvref_t<decltype(val)>>());
      }
      path.pop_back();
    }

    for (const auto& [key, val] : newNode) {
      if (!oldNode.contains(key)) {
        // only look at keys that didn't exist. First loop should handle all
        // replacement deltas
        hasDifferences = true;
        path.push_back(folly::to<std::string>(key));
        f(path,
          MappedTag{},
          std::optional<std::remove_cvref_t<decltype(val)>>(),
          std::make_optional(val));
        path.pop_back();
      }
    }

    if (hasDifferences) {
      using Tag = apache::thrift::type::map<KeyTag, MappedTag>;
      f(path, Tag{}, std::make_optional(oldNode), std::make_optional(newNode));
    }

    return hasDifferences;
  }
};

/**
 * Variant
 */
template <typename Node>
struct ThriftDeltaVisitor<apache::thrift::type::union_t<Node>> {
  template <typename Func>
  static bool visit(
      std::vector<std::string>& path,
      const Node& oldNode,
      const Node& newNode,
      Func&& f) {
    bool hasDifferences{false};

    if (oldNode.getType() != newNode.getType()) {
      hasDifferences = true;
      apache::thrift::op::invoke_by_field_id<Node>(
          static_cast<apache::thrift::FieldId>(oldNode.getType()),
          [&]<class Id>(Id) {
            path.emplace_back(apache::thrift::op::get_name_v<Node, Id>);
            f(path,
              apache::thrift::type::union_t<Node>{},
              std::make_optional(oldNode),
              std::optional<Node>());
            path.pop_back();
          },
          []() {
            // union is __EMPTY__
          });

      apache::thrift::op::invoke_by_field_id<Node>(
          static_cast<apache::thrift::FieldId>(newNode.getType()),
          [&]<class Id>(Id) {
            path.emplace_back(apache::thrift::op::get_name_v<Node, Id>);
            f(path,
              apache::thrift::type::union_t<Node>{},
              std::optional<Node>(),
              std::make_optional(newNode));
            path.pop_back();
          },
          []() {
            // union is __EMPTY__
          });
    } else {
      apache::thrift::op::invoke_by_field_id<Node>(
          static_cast<apache::thrift::FieldId>(oldNode.getType()),
          [&]<class Id>(Id) {
            path.emplace_back(apache::thrift::op::get_name_v<Node, Id>);
            hasDifferences =
                ThriftDeltaVisitor<apache::thrift::op::get_type_tag<Node, Id>>::
                    visit(
                        path,
                        *apache::thrift::op::get<Id>(oldNode),
                        *apache::thrift::op::get<Id>(newNode),
                        std::forward<Func>(f));
            path.pop_back();
          },
          []() {
            // union is __EMPTY__
          });
    }

    if (hasDifferences) {
      using Tag = apache::thrift::type::union_t<Node>;
      f(path, Tag{}, std::make_optional(oldNode), std::make_optional(newNode));
    }

    return hasDifferences;
  }
};

/**
 * Structure
 */
template <typename Node>
struct ThriftDeltaVisitor<apache::thrift::type::struct_t<Node>> {
  template <typename Func>
  static bool visit(const Node& oldNode, const Node& newNode, Func&& f) {
    std::vector<std::string> path;
    return visit(path, oldNode, newNode, std::forward<Func>(f));
  }

  template <typename Func>
  static bool visit(
      std::vector<std::string>& path,
      const Node& oldNode,
      const Node& newNode,
      Func&& f) {
    bool hasDifferences{false};

    apache::thrift::op::for_each_field_id<Node>([&]<class Id>(Id) {
      // Check for optionality
      auto oldNodeField = apache::thrift::op::get<Id>(oldNode);
      auto newNodeField = apache::thrift::op::get<Id>(newNode);

      if constexpr (apache::thrift::detail::is_optional_field_ref_v<
                        decltype(oldNodeField)>) {
        // skip optional fields that are not set
        if (!oldNodeField.has_value() && !newNodeField.has_value()) {
          return;
        }
      }

      // Look for the expected member name
      path.push_back(apache::thrift::op::get_name_v<Node, Id>.str());

      if (apache::thrift::op::get_value_or_null(oldNodeField) == nullptr &&
          apache::thrift::op::get_value_or_null(newNodeField) != nullptr) {
        hasDifferences = true;
        f(path,
          apache::thrift::op::get_type_tag<Node, Id>{},
          std::optional<apache::thrift::op::get_native_type<Node, Id>>(),
          std::make_optional(*newNodeField));
      } else if (
          apache::thrift::op::get_value_or_null(oldNodeField) != nullptr &&
          apache::thrift::op::get_value_or_null(newNodeField) == nullptr) {
        hasDifferences = true;
        f(path,
          apache::thrift::op::get_type_tag<Node, Id>{},
          std::make_optional(*oldNodeField),
          std::optional<apache::thrift::op::get_native_type<Node, Id>>());
      }

      // Recurse further
      auto different =
          ThriftDeltaVisitor<apache::thrift::op::get_type_tag<Node, Id>>::visit(
              path, *oldNodeField, *newNodeField, std::forward<Func>(f));
      hasDifferences = hasDifferences || different;
      path.pop_back();
    });

    if (hasDifferences) {
      using Tag = apache::thrift::type::struct_t<Node>;
      f(path, Tag{}, std::make_optional(oldNode), std::make_optional(newNode));
    }

    return hasDifferences;
  }
};

/**
 * Cpp.Type
 */
template <typename T, typename Tag>
struct ThriftDeltaVisitor<apache::thrift::type::cpp_type<T, Tag>>
    : public ThriftDeltaVisitor<Tag> {};

/**
 * Primitives - fallback specialization
 * - string / binary
 * - floating_point
 * - integral
 */
template <typename Tag>
struct ThriftDeltaVisitor {
  static_assert(
      apache::thrift::type::is_a_v<Tag, apache::thrift::type::primitive_c>,
      "expected primitive type");

  template <typename Node, typename Func>
  static bool visit(
      std::vector<std::string>& path,
      const Node& oldNode,
      const Node& newNode,
      Func&& f) {
    if (oldNode != newNode) {
      f(path, Tag{}, std::make_optional(oldNode), std::make_optional(newNode));
      return true;
    }

    return false;
  }
};

template <typename Node>
using RootThriftDeltaVisitor =
    ThriftDeltaVisitor<apache::thrift::type::struct_t<Node>>;

} // namespace facebook::fboss::fsdb
