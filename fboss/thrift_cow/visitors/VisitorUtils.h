// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <thrift/lib/cpp2/TypeClass.h>
#include <thrift/lib/cpp2/op/Get.h>
#include <thrift/lib/cpp2/type/Field.h>
#include "folly/Conv.h"

namespace facebook::fboss::thrift_cow {

// Maps modern Thrift type tags to legacy type_class types
template <typename Tag>
struct TypeTagToTypeClass;

template <>
struct TypeTagToTypeClass<apache::thrift::type::bool_t> {
  using type = apache::thrift::type_class::integral;
};
template <>
struct TypeTagToTypeClass<apache::thrift::type::byte_t> {
  using type = apache::thrift::type_class::integral;
};
template <>
struct TypeTagToTypeClass<apache::thrift::type::i16_t> {
  using type = apache::thrift::type_class::integral;
};
template <>
struct TypeTagToTypeClass<apache::thrift::type::i32_t> {
  using type = apache::thrift::type_class::integral;
};
template <>
struct TypeTagToTypeClass<apache::thrift::type::i64_t> {
  using type = apache::thrift::type_class::integral;
};
template <>
struct TypeTagToTypeClass<apache::thrift::type::float_t> {
  using type = apache::thrift::type_class::floating_point;
};
template <>
struct TypeTagToTypeClass<apache::thrift::type::double_t> {
  using type = apache::thrift::type_class::floating_point;
};
template <>
struct TypeTagToTypeClass<apache::thrift::type::string_t> {
  using type = apache::thrift::type_class::string;
};
template <>
struct TypeTagToTypeClass<apache::thrift::type::binary_t> {
  using type = apache::thrift::type_class::binary;
};
template <typename E>
struct TypeTagToTypeClass<apache::thrift::type::enum_t<E>> {
  using type = apache::thrift::type_class::enumeration;
};
template <typename S>
struct TypeTagToTypeClass<apache::thrift::type::struct_t<S>> {
  using type = apache::thrift::type_class::structure;
};
template <typename U>
struct TypeTagToTypeClass<apache::thrift::type::union_t<U>> {
  using type = apache::thrift::type_class::variant;
};
template <typename E>
struct TypeTagToTypeClass<apache::thrift::type::exception_t<E>> {
  using type = apache::thrift::type_class::structure;
};
template <typename V>
struct TypeTagToTypeClass<apache::thrift::type::list<V>> {
  using type =
      apache::thrift::type_class::list<typename TypeTagToTypeClass<V>::type>;
};
template <typename V>
struct TypeTagToTypeClass<apache::thrift::type::set<V>> {
  using type =
      apache::thrift::type_class::set<typename TypeTagToTypeClass<V>::type>;
};
template <typename K, typename V>
struct TypeTagToTypeClass<apache::thrift::type::map<K, V>> {
  using type = apache::thrift::type_class::map<
      typename TypeTagToTypeClass<K>::type,
      typename TypeTagToTypeClass<V>::type>;
};
template <typename Adapter, typename Tag>
struct TypeTagToTypeClass<apache::thrift::type::adapted<Adapter, Tag>> {
  using type = typename TypeTagToTypeClass<Tag>::type;
};
template <typename T, typename Tag>
struct TypeTagToTypeClass<apache::thrift::type::cpp_type<T, Tag>> {
  using type = typename TypeTagToTypeClass<Tag>::type;
};

template <typename TType, typename Id>
std::string getMemberName(bool useId) {
  namespace op = apache::thrift::op;
  if (useId) {
    return folly::to<std::string>(
        folly::to_underlying(op::get_field_id_v<TType, Id>));
  } else {
    return std::string(op::get_field_name<TType, Id>());
  }
}

template <typename TType, typename Func>
void visitMember(const std::string& key, Func&& f) {
  namespace op = apache::thrift::op;
  // try to find child using id first, then fallback to name
  auto idTry = folly::tryTo<int16_t>(key);
  if (!idTry.hasError()) {
    op::invoke_by_field_id<TType>(
        apache::thrift::FieldId(idTry.value()),
        [&]<class Id>(Id) { f(Id{}); },
        []() { /* not found */ });
  } else {
    bool found = false;
    op::for_each_field_id<TType>([&]<class Id>(Id) {
      if (!found && op::get_field_name<TType, Id>() == key) {
        found = true;
        f(Id{});
      }
    });
  }
}

} // namespace facebook::fboss::thrift_cow
