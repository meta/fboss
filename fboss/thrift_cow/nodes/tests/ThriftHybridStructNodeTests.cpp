/*
 *  Copyright (c) 2024-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/thrift_cow/nodes/Serializer.h"
#include "fboss/thrift_cow/nodes/Types.h"
#include "fboss/thrift_cow/nodes/tests/gen-cpp2/test_types.h"

#include <gtest/gtest.h>
#include <type_traits>

using namespace facebook::fboss;
using namespace facebook::fboss::thrift_cow;
using namespace ::testing;

namespace k = apache::thrift::ident;

template <typename ThriftType, bool EnableHybridStorage>
struct is_allow_skip_thrift_cow {
  static constexpr bool value =
      type_allow_skip_thrift_cow<ThriftType> && EnableHybridStorage;
};

struct TestParams {
  bool enableHybridStorage;
};

template <typename T>
class ThriftHybridStructNodeTestSuite : public ::testing::Test {};

TYPED_TEST_SUITE_P(ThriftHybridStructNodeTestSuite);

template <bool EnableHybridStorage>
void readThriftStructAnnotation() {
  static_assert(
      is_allow_skip_thrift_cow<ParentTestStruct, EnableHybridStorage>::value ==
      false);
  static_assert(
      is_allow_skip_thrift_cow<TestStruct2, EnableHybridStorage>::value ==
      false);
  static_assert(
      is_allow_skip_thrift_cow<TestStruct3, EnableHybridStorage>::value ==
      EnableHybridStorage);
}

TYPED_TEST_P(ThriftHybridStructNodeTestSuite, ReadThriftStructAnnotation) {
  readThriftStructAnnotation<false>();
  readThriftStructAnnotation<true>();
}

template <bool EnableHybridStorage>
void thriftStructNodeAnnotations() {
  {
    ThriftStructFields<
        TestStruct,
        ThriftStructResolver<TestStruct, EnableHybridStorage>,
        EnableHybridStorage>
        fields;

    static_assert(
        fields.template isSkipThriftCowEnabled<k::hybridMap>() ==
        EnableHybridStorage);
    static_assert(fields.template isSkipThriftCowEnabled<k::cowMap>() == false);
  }
  {
    ThriftStructNode<
        TestStruct,
        ThriftStructResolver<TestStruct, EnableHybridStorage>,
        EnableHybridStorage>
        node;

    ASSERT_EQ(
        node.template isSkipThriftCowEnabled<k::hybridMap>(),
        EnableHybridStorage);
    ASSERT_EQ(node.template isSkipThriftCowEnabled<k::cowMap>(), false);
  }
  // annotation that is other than `allow_skip_thrift_cow`
  {
    ThriftStructNode<TestStruct2> node;
    ASSERT_EQ(node.isSkipThriftCowEnabled<k::deprecatedField>(), false);
  }
}

TYPED_TEST_P(ThriftHybridStructNodeTestSuite, ThriftStructNodeAnnotations) {
  thriftStructNodeAnnotations<false>();
  thriftStructNodeAnnotations<true>();
}

template <bool EnableHybridStorage>
void testHybridNodeTypes() {
  ThriftStructNode<
      TestStruct,
      ThriftStructResolver<TestStruct, EnableHybridStorage>,
      EnableHybridStorage>
      node;
  // cow member
  auto val = node.template get<k::cowMap>();
  using underlying_type = std::remove_cvref_t<decltype(*val)>::ThriftType;
  static_assert(std::is_same_v<underlying_type, std::map<int, bool>>);
  // hybrid member
  auto valHybrid = node.template get<k::hybridMap>();
  using underlying_type = std::remove_cvref_t<decltype(*valHybrid)>::ThriftType;
  static_assert(std::is_same_v<underlying_type, std::map<int, bool>>);
  static_assert(
      std::is_same_v<
          typename decltype(valHybrid)::element_type::CowType,
          HybridNodeType> == EnableHybridStorage);
  if constexpr (EnableHybridStorage) {
    using ref_type = std::remove_cvref_t<decltype(valHybrid->ref())>;
    static_assert(std::is_same_v<ref_type, std::map<int, bool>>);
  }
}

TYPED_TEST_P(ThriftHybridStructNodeTestSuite, TestHybridNodeTypes) {
  testHybridNodeTypes<false>();
  testHybridNodeTypes<true>();
}

template <bool EnableHybridStorage>
void thriftStructFieldsHybridMemberGetSet() {
  ThriftStructFields<
      TestStruct,
      ThriftStructResolver<TestStruct, EnableHybridStorage>,
      EnableHybridStorage>
      fields;

  auto data = std::make_shared<std::map<int, bool>>();
  data->insert({1, false});
  data->insert({2, true});

  // cow member
  fields.template set<k::cowMap>(*data);
  auto val = fields.template get<k::cowMap>();
  ASSERT_EQ(val->size(), 2);
  ASSERT_EQ(val->at(1), false);
  ASSERT_EQ(val->at(2), true);
  // hybrid member
  fields.template set<k::hybridMap>(*data);
  auto valHybrid = fields.template get<k::hybridMap>();
  static_assert(
      std::is_same_v<
          typename decltype(valHybrid)::element_type::CowType,
          HybridNodeType> == EnableHybridStorage);
  if constexpr (EnableHybridStorage) {
    ASSERT_EQ(valHybrid->cref().size(), 2);
    ASSERT_EQ(valHybrid->cref().at(1), false);
    ASSERT_EQ(valHybrid->cref().at(2), true);
  }
}

TYPED_TEST_P(
    ThriftHybridStructNodeTestSuite,
    ThriftStructFieldsHybridMemberGetSet) {
  thriftStructFieldsHybridMemberGetSet<false>();
  thriftStructFieldsHybridMemberGetSet<true>();
}

template <bool EnableHybridStorage>
void thriftStructFieldsHybridMemberToFromThrift() {
  ThriftStructFields<
      TestStruct,
      ThriftStructResolver<TestStruct, EnableHybridStorage>,
      EnableHybridStorage>
      fields;

  auto data = std::make_shared<std::map<int, bool>>();
  data->insert({1, false});
  data->insert({2, true});

  // fromThrift
  auto val = fields.template get<k::hybridMap>();
  val->fromThrift(*data);

  // toThrift
  auto thrift = val->toThrift();
  ASSERT_EQ(thrift, *data);
}

TYPED_TEST_P(
    ThriftHybridStructNodeTestSuite,
    ThriftStructFieldsHybridMemberToFromThrift) {
  thriftStructFieldsHybridMemberToFromThrift<false>();
  thriftStructFieldsHybridMemberToFromThrift<true>();
}

template <bool EnableHybridStorage>
void thriftStructNodeHybridMemberToFromThrift() {
  ThriftStructNode<
      TestStruct,
      ThriftStructResolver<TestStruct, EnableHybridStorage>,
      EnableHybridStorage>
      node;

  auto data = std::make_shared<std::map<int, bool>>();
  data->insert({1, false});
  data->insert({2, true});

  // fromThrift
  auto val = node.template get<k::hybridMap>();
  val->fromThrift(*data);

  // toThrift
  auto thrift = val->toThrift();
  ASSERT_EQ(thrift, *data);
}

TYPED_TEST_P(
    ThriftHybridStructNodeTestSuite,
    ThriftStructNodeHybridMemberToFromThrift) {
  thriftStructNodeHybridMemberToFromThrift<false>();
  thriftStructNodeHybridMemberToFromThrift<true>();
}

template <bool EnableHybridStorage>
void testHybridNodeMemberTypes() {
  ThriftStructNode<
      TestStruct,
      ThriftStructResolver<TestStruct, EnableHybridStorage>,
      EnableHybridStorage>
      node;

  // list
  ASSERT_EQ(
      node.template isSkipThriftCowEnabled<k::hybridList>(),
      EnableHybridStorage);
  auto v_list = node.template get<k::hybridList>();
  using underlying_type_l = std::remove_cvref_t<decltype(*v_list)>::ThriftType;
  static_assert(std::is_same_v<underlying_type_l, std::vector<int>>);
  static_assert(
      std::is_same_v<
          typename decltype(v_list)::element_type::CowType,
          HybridNodeType> == EnableHybridStorage);
  if constexpr (EnableHybridStorage) {
    using ref_type_l = std::remove_cvref_t<decltype(v_list->ref())>;
    static_assert(std::is_same_v<ref_type_l, std::vector<int>>);
  }

  // set
  ASSERT_EQ(
      node.template isSkipThriftCowEnabled<k::hybridSet>(),
      EnableHybridStorage);
  auto v_set = node.template get<k::hybridSet>();
  using underlying_type_s = std::remove_cvref_t<decltype(*v_set)>::ThriftType;
  static_assert(std::is_same_v<underlying_type_s, std::set<int>>);
  static_assert(
      std::is_same_v<
          typename decltype(v_set)::element_type::CowType,
          HybridNodeType> == EnableHybridStorage);
  if constexpr (EnableHybridStorage) {
    using ref_type_s = std::remove_cvref_t<decltype(v_set->ref())>;
    static_assert(std::is_same_v<ref_type_s, std::set<int>>);
  }

  // union
  ASSERT_EQ(
      node.template isSkipThriftCowEnabled<k::hybridUnion>(),
      EnableHybridStorage);
  auto v_union = node.template get<k::hybridUnion>();
  using underlying_type_u = std::remove_cvref_t<decltype(*v_union)>::ThriftType;
  static_assert(std::is_same_v<underlying_type_u, TestUnion>);
  static_assert(
      std::is_same_v<
          typename decltype(v_union)::element_type::CowType,
          HybridNodeType> == EnableHybridStorage);
  if constexpr (EnableHybridStorage) {
    using ref_type_u = std::remove_cvref_t<decltype(v_union->ref())>;
    static_assert(std::is_same_v<ref_type_u, TestUnion>);
  }
  // child struct
  ASSERT_EQ(
      node.template isSkipThriftCowEnabled<k::hybridStruct>(),
      EnableHybridStorage);
  auto v_struct = node.template get<k::hybridStruct>();
  using underlying_type_c =
      std::remove_cvref_t<decltype(*v_struct)>::ThriftType;
  static_assert(std::is_same_v<underlying_type_c, ChildStruct>);
  static_assert(
      std::is_same_v<
          typename decltype(v_struct)::element_type::CowType,
          HybridNodeType> == EnableHybridStorage);
  if constexpr (EnableHybridStorage) {
    using ref_type_c = std::remove_cvref_t<decltype(v_struct->ref())>;
    static_assert(std::is_same_v<ref_type_c, ChildStruct>);
  }
}

TYPED_TEST_P(ThriftHybridStructNodeTestSuite, TestHybridNodeMemberTypes) {
  testHybridNodeMemberTypes<false>();
  testHybridNodeMemberTypes<true>();
}

using EnableHybridStorageTypes = testing::Types<bool>;

REGISTER_TYPED_TEST_SUITE_P(
    ThriftHybridStructNodeTestSuite,
    ReadThriftStructAnnotation,
    ThriftStructNodeAnnotations,
    TestHybridNodeTypes,
    ThriftStructFieldsHybridMemberGetSet,
    ThriftStructFieldsHybridMemberToFromThrift,
    ThriftStructNodeHybridMemberToFromThrift,
    TestHybridNodeMemberTypes);

INSTANTIATE_TYPED_TEST_SUITE_P(
    ThriftHybridStructNodeTests,
    ThriftHybridStructNodeTestSuite,
    EnableHybridStorageTypes);

#ifdef ENABLE_DYNAMIC_APIS
TEST(ThriftHybridStructNodeTests, FollyDynamicTest) {
  // toFollyDynamic
  {
    TestStruct data;
    data.inlineBool() = true;
    data.inlineInt() = 123;
    data.inlineString() = "HelloThere";

    ThriftStructNode<TestStruct> node(data);
    auto dyn = node.toFollyDynamic();
    TestStruct myObj = facebook::thrift::from_dynamic<TestStruct>(
        dyn, facebook::thrift::dynamic_format::JSON_1);
    EXPECT_EQ(123, *myObj.inlineInt());
    EXPECT_EQ("HelloThere", *myObj.inlineString());
    EXPECT_TRUE(*myObj.inlineBool());
  }

  // fromFollyDynamic
  {
    TestStruct data;
    ThriftStructNode<TestStruct> node(data);
    data.inlineBool() = true;
    data.inlineInt() = 123;
    data.inlineString() = "HelloThere";
    folly::dynamic dyn;
    facebook::thrift::to_dynamic(
        dyn, data, facebook::thrift::dynamic_format::PORTABLE);
    node.fromFollyDynamic(dyn);
    auto valDyn = node.toFollyDynamic();
    TestStruct myObj = facebook::thrift::from_dynamic<TestStruct>(
        valDyn, facebook::thrift::dynamic_format::JSON_1);
    EXPECT_EQ(123, *myObj.inlineInt());
    EXPECT_EQ("HelloThere", *myObj.inlineString());
    EXPECT_TRUE(*myObj.inlineBool());
  }
}
#endif // ENABLE_DYNAMIC_APIS
