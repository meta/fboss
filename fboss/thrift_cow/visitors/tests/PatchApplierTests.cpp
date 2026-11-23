// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/json/dynamic.h>
#include <gtest/gtest.h>

#include <fboss/thrift_cow/gen-cpp2/patch_types.h>
#include <fboss/thrift_cow/visitors/PatchApplier.h>
#include <fboss/thrift_cow/visitors/tests/VisitorTestUtils.h>
#include "fboss/thrift_cow/nodes/Types.h"
#include "fboss/thrift_cow/nodes/tests/gen-cpp2/test_types.h"

using namespace facebook::fboss;
using namespace facebook::fboss::thrift_cow;

namespace facebook::fboss::thrift_cow {

TEST(PatchApplierTests, ModifyPrimitive) {
  auto structA = createSimpleTestStruct();

  PatchNode n;
  n.set_val(
      serializeBuf<apache::thrift::type_class::string>(
          fsdb::OperProtocol::COMPACT, "new val"));

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);
  auto ret = PatchApplier<apache::thrift::type_class::string>::apply(
      *nodeA->template ref<k::inlineString>(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*nodeA->template ref<k::inlineString>(), "new val");

  // patch non cow struct
  ret = PatchApplier<apache::thrift::type_class::string>::apply(
      *structA.inlineString(), std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*structA.inlineString(), "new val");

  n = PatchNode();
  n.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 1234));

  ret = PatchApplier<apache::thrift::type_class::integral>::apply(
      *nodeA->template ref<k::inlineInt>(), std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*nodeA->template ref<k::inlineInt>(), 1234);
}

TEST(PatchApplierTests, ModifyStructMember) {
  auto structA = createSimpleTestStruct();

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode inlineString;
  inlineString.set_val(
      serializeBuf<apache::thrift::type_class::string>(
          fsdb::OperProtocol::COMPACT, "new val"));

  StructPatch structPatch;
  structPatch.children() = {
      {fieldId<TestStruct, k::inlineString>(), inlineString}};
  PatchNode n;
  n.set_struct_node(std::move(structPatch));

  auto ret = PatchApplier<apache::thrift::type_class::structure>::apply(
      *nodeA, std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*nodeA->template ref<k::inlineString>(), "new val");
}

TEST(PatchApplierTests, ModifyMapMember) {
  auto structA = createSimpleTestStruct();
  structA.mapOfI32ToI32() = {{123, 456}};
  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode intPatch;
  intPatch.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 42));

  MapPatch mapPatch;
  mapPatch.children() = {{"123", intPatch}};
  PatchNode n;
  n.set_map_node(mapPatch);

  auto ret = PatchApplier<apache::thrift::type_class::map<
      apache::thrift::type_class::integral,
      apache::thrift::type_class::integral>>::
      apply(*nodeA->template ref<k::mapOfI32ToI32>(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*nodeA->template ref<k::mapOfI32ToI32>()->at(123), 42);

  // patch non cow struct
  ret = PatchApplier<apache::thrift::type_class::map<
      apache::thrift::type_class::integral,
      apache::thrift::type_class::integral>>::
      apply(*structA.mapOfI32ToI32(), std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(structA.mapOfI32ToI32()->at(123), 42);
}

TEST(PatchApplierTests, AddRemoveMapMember) {
  auto structA = createSimpleTestStruct();
  structA.mapOfI32ToI32() = {{1, 2}};
  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode intPatch;
  intPatch.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 42));
  PatchNode delPatch;
  delPatch.set_del();

  MapPatch mapPatch;
  mapPatch.children() = {
      {"3" /* new key */, intPatch}, {"1" /* delete existing */, delPatch}};
  PatchNode n;
  n.set_map_node(mapPatch);

  auto ret = PatchApplier<apache::thrift::type_class::map<
      apache::thrift::type_class::integral,
      apache::thrift::type_class::integral>>::
      apply(*nodeA->template ref<k::mapOfI32ToI32>(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*nodeA->template ref<k::mapOfI32ToI32>()->at(3), 42);
  EXPECT_EQ(nodeA->template ref<k::mapOfI32ToI32>()->count(1), 0);
  EXPECT_EQ(nodeA->template ref<k::mapOfI32ToI32>()->size(), 1);

  // patch non cow struct
  ret = PatchApplier<apache::thrift::type_class::map<
      apache::thrift::type_class::integral,
      apache::thrift::type_class::integral>>::
      apply(*structA.mapOfI32ToI32(), std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(structA.mapOfI32ToI32()->size(), 1);
  EXPECT_EQ(structA.mapOfI32ToI32()->at(3), 42);
  EXPECT_EQ(structA.mapOfI32ToI32()->count(1), 0);
}

TEST(PatchApplierTests, ModifyListMember) {
  auto structA = createSimpleTestStruct();
  structA.listOfPrimitives() = {1, 2, 3};

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode intPatch;
  intPatch.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 42));

  ListPatch listPatch;
  listPatch.children() = {{1, intPatch}};
  PatchNode n;
  n.set_list_node(listPatch);

  auto ret = PatchApplier<
      apache::thrift::type_class::list<apache::thrift::type_class::integral>>::
      apply(*nodeA->template ref<k::listOfPrimitives>(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*nodeA->template ref<k::listOfPrimitives>()->at(1), 42);

  // patch non cow struct
  ret = PatchApplier<
      apache::thrift::type_class::list<apache::thrift::type_class::integral>>::
      apply(*structA.listOfPrimitives(), std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(structA.listOfPrimitives()->at(1), 42);
}

TEST(PatchApplierTests, AddListMembers) {
  auto structA = createSimpleTestStruct();
  structA.listOfPrimitives() = {1};

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode intPatch1;
  intPatch1.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 12));
  PatchNode intPatch2;
  intPatch2.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 34));

  ListPatch listPatch;
  listPatch.children() = {{1, intPatch1}, {2, intPatch2}};
  PatchNode n;
  n.set_list_node(listPatch);

  auto ret = PatchApplier<
      apache::thrift::type_class::list<apache::thrift::type_class::integral>>::
      apply(*nodeA->template ref<k::listOfPrimitives>(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(nodeA->template ref<k::listOfPrimitives>()->size(), 3);
  EXPECT_EQ(*nodeA->template ref<k::listOfPrimitives>()->at(1), 12);
  EXPECT_EQ(*nodeA->template ref<k::listOfPrimitives>()->at(2), 34);

  // patch non cow struct
  ret = PatchApplier<
      apache::thrift::type_class::list<apache::thrift::type_class::integral>>::
      apply(*structA.listOfPrimitives(), std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(structA.listOfPrimitives()->size(), 3);
  EXPECT_EQ(structA.listOfPrimitives()->at(1), 12);
  EXPECT_EQ(structA.listOfPrimitives()->at(2), 34);
}

TEST(PatchApplierTests, RemoveListMembers) {
  auto structA = createSimpleTestStruct();
  structA.listOfPrimitives() = {1, 2, 3};

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode delPatch;
  delPatch.set_del();

  ListPatch listPatch;
  listPatch.children() = {{1, delPatch}, {2, delPatch}};
  PatchNode n;
  n.set_list_node(listPatch);

  auto ret = PatchApplier<
      apache::thrift::type_class::list<apache::thrift::type_class::integral>>::
      apply(*nodeA->template ref<k::listOfPrimitives>(), std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(nodeA->template ref<k::listOfPrimitives>()->size(), 1);
  EXPECT_EQ(nodeA->template ref<k::listOfPrimitives>()->at(0), 1);
}

TEST(PatchApplierTests, ModifyVariantValue) {
  auto structA = createSimpleTestStruct();
  structA.inlineVariant()->set_inlineInt(123);

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode intPatch;
  intPatch.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 42));

  VariantPatch variantPatch;
  variantPatch.id() = fieldId<TestUnion, k::inlineInt>();
  variantPatch.child() = intPatch;
  PatchNode n;
  n.set_variant_node(variantPatch);

  auto ret = PatchApplier<apache::thrift::type_class::variant>::apply(
      *nodeA->template ref<k::inlineVariant>(), std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*nodeA->template ref<k::inlineVariant>()->ref<k::inlineInt>(), 42);
}

TEST(PatchApplierTests, ModifyVariantType) {
  auto structA = createSimpleTestStruct();
  structA.inlineVariant()->set_inlineString("some val");

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode intPatch;
  intPatch.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 42));

  VariantPatch variantPatch;
  variantPatch.id() = fieldId<TestUnion, k::inlineInt>();
  variantPatch.child() = intPatch;
  PatchNode n;
  n.set_variant_node(variantPatch);

  auto ret = PatchApplier<apache::thrift::type_class::variant>::apply(
      *nodeA->template ref<k::inlineVariant>(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*nodeA->template ref<k::inlineVariant>()->ref<k::inlineInt>(), 42);

  ret = PatchApplier<apache::thrift::type_class::variant>::apply(
      *structA.inlineVariant(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*structA.inlineVariant()->inlineInt(), 42);

  StructPatch structPatch;
  structPatch.children() = {{fieldId<cfg::L4PortRange, k::min>(), intPatch}};

  variantPatch.id() = fieldId<TestUnion, k::inlineStruct>();
  variantPatch.child()->set_struct_node(structPatch);
  n.set_variant_node(variantPatch);

  ret = PatchApplier<apache::thrift::type_class::variant>::apply(
      *nodeA->template ref<k::inlineVariant>(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(
      *nodeA->template ref<k::inlineVariant>()
           ->ref<k::inlineStruct>()
           ->toThrift()
           .min(),
      42);

  ret = PatchApplier<apache::thrift::type_class::variant>::apply(
      *structA.inlineVariant(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*structA.inlineVariant()->inlineStruct()->min(), 42);
}

TEST(PatchApplierTests, AddRemoveSetItems) {
  auto structA = createSimpleTestStruct();
  structA.setOfI32() = {1, 2};

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode intPatch;
  intPatch.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 3));

  PatchNode delPatch;
  delPatch.set_del();

  SetPatch setPatch;
  setPatch.children() = {{"3", intPatch}, {"2", delPatch}};
  PatchNode n;
  n.set_set_node(setPatch);

  auto ret = PatchApplier<
      apache::thrift::type_class::set<apache::thrift::type_class::integral>>::
      apply(*nodeA->template ref<k::setOfI32>(), PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(nodeA->template ref<k::setOfI32>()->size(), 2);
  EXPECT_EQ(nodeA->template ref<k::setOfI32>()->count(1), 1);
  EXPECT_EQ(nodeA->template ref<k::setOfI32>()->count(3), 1);

  // patch non cow struct
  ret = PatchApplier<
      apache::thrift::type_class::set<apache::thrift::type_class::integral>>::
      apply(*structA.setOfI32(), std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(structA.setOfI32()->size(), 2);
  EXPECT_EQ(structA.setOfI32()->count(1), 1);
  EXPECT_EQ(structA.setOfI32()->count(3), 1);
}

TEST(PatchApplierTests, FailPatchingSetEntry) {
  // Set entries are immutable, patching them directly should compile but fail
  auto structA = createSimpleTestStruct();
  structA.setOfI32() = {1, 2};

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode intPatch;
  intPatch.set_val(
      serializeBuf<apache::thrift::type_class::integral>(
          fsdb::OperProtocol::COMPACT, 3));

  std::vector<std::string> path = {
      folly::to<std::string>(fieldId<TestStruct, k::setOfI32>()), "1"};
  bool visited = false;
  auto process = [&](auto& node, auto /*begin*/, auto /*end*/) {
    EXPECT_FALSE(visited);
    visited = true;
    if constexpr (is_cow_type_v<decltype(node)>) {
      using NodeT = typename std::remove_cvref_t<decltype(node)>;
      using TC = typename NodeT::TC;
      auto ret = PatchApplier<TC>::apply(node, PatchNode(intPatch));
      EXPECT_EQ(ret, PatchApplyResult::PATCHING_IMMUTABLE_NODE);
    } else {
      FAIL() << "unexpected non-cow visit";
    }
  };
  visitPath(*nodeA, path.begin(), path.end(), process);
  EXPECT_TRUE(visited);
}

TEST(PatchApplierTests, SetMemberFull) {
  auto structA = createSimpleTestStruct();
  auto structB = structA;

  cfg::L4PortRange t;
  t.min() = 12;
  t.max() = 34;
  structB.inlineStruct() = std::move(t);
  structB.inlineVariant()->set_inlineString("test");
  structB.mapOfI32ToI32() = {{1, 2}, {3, 4}};
  structB.listOfPrimitives() = {5, 6};
  structB.setOfI32() = {7, 8};
  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);
  auto nodeB = std::make_shared<ThriftStructNode<TestStruct>>(structB);

  // use second node to create full patches
  PatchNode inlineStruct;
  inlineStruct.set_val(
      nodeB->ref<k::inlineStruct>()->encodeBuf(fsdb::OperProtocol::COMPACT));
  PatchNode inlineVariant;
  inlineVariant.set_val(
      nodeB->ref<k::inlineVariant>()->encodeBuf(fsdb::OperProtocol::COMPACT));
  PatchNode mapOfI32ToI32;
  mapOfI32ToI32.set_val(
      nodeB->ref<k::mapOfI32ToI32>()->encodeBuf(fsdb::OperProtocol::COMPACT));
  PatchNode listOfPrimitives;
  listOfPrimitives.set_val(nodeB->ref<k::listOfPrimitives>()->encodeBuf(
      fsdb::OperProtocol::COMPACT));
  PatchNode setOfI32;
  setOfI32.set_val(
      nodeB->ref<k::setOfI32>()->encodeBuf(fsdb::OperProtocol::COMPACT));
  StructPatch structPatch;
  structPatch.children() = {
      {fieldId<TestStruct, k::inlineStruct>(), inlineStruct},
      {fieldId<TestStruct, k::inlineVariant>(), inlineVariant},
      {fieldId<TestStruct, k::mapOfI32ToI32>(), mapOfI32ToI32},
      {fieldId<TestStruct, k::listOfPrimitives>(), listOfPrimitives},
      {fieldId<TestStruct, k::setOfI32>(), setOfI32}};
  PatchNode n;
  n.set_struct_node(std::move(structPatch));

  auto ret = RootPatchApplier::apply(*nodeA, PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);

  EXPECT_EQ(nodeA->ref<k::inlineStruct>()->toThrift(), *structB.inlineStruct());
  EXPECT_EQ(
      nodeA->ref<k::inlineVariant>()->toThrift(), *structB.inlineVariant());
  EXPECT_EQ(
      nodeA->ref<k::mapOfI32ToI32>()->toThrift(), *structB.mapOfI32ToI32());
  EXPECT_EQ(
      nodeA->ref<k::listOfPrimitives>()->toThrift(),
      *structB.listOfPrimitives());
  EXPECT_EQ(nodeA->ref<k::setOfI32>()->toThrift(), *structB.setOfI32());

  // patch non cow struct
  ret = RootPatchApplier::apply(structA, std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);

  EXPECT_EQ(*structA.inlineStruct(), *structB.inlineStruct());
  EXPECT_EQ(*structA.inlineVariant(), *structB.inlineVariant());
  EXPECT_EQ(*structA.mapOfI32ToI32(), *structB.mapOfI32ToI32());
  EXPECT_EQ(*structA.listOfPrimitives(), *structB.listOfPrimitives());
  EXPECT_EQ(*structA.setOfI32(), *structB.setOfI32());
}

TEST(PatchApplierTests, SetUnsetOptionalMember) {
  auto structA = createSimpleTestStruct();
  structA.optionalInt().reset();
  structA.optionalStruct().reset();
  auto structB = structA;
  structB.optionalInt() = 123;
  structB.optionalStruct() = cfg::L4PortRange();
  structB.optionalStruct()->max() = 321;
  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);
  auto nodeB = std::make_shared<ThriftStructNode<TestStruct>>(structB);

  PatchNode optionalInt;
  optionalInt.set_val(
      nodeB->ref<k::optionalInt>()->encodeBuf(fsdb::OperProtocol::COMPACT));
  PatchNode optionalStruct;
  optionalStruct.set_val(
      nodeB->ref<k::optionalStruct>()->encodeBuf(fsdb::OperProtocol::COMPACT));

  StructPatch structPatch;
  structPatch.children() = {
      {fieldId<TestStruct, k::optionalInt>(), optionalInt},
      {fieldId<TestStruct, k::optionalStruct>(), optionalStruct}};
  PatchNode n;
  n.set_struct_node(structPatch);

  auto ret = RootPatchApplier::apply(*nodeA, PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);

  EXPECT_EQ(nodeA->ref<k::optionalInt>()->toThrift(), *structB.optionalInt());
  EXPECT_EQ(
      nodeA->ref<k::optionalStruct>()->toThrift(), *structB.optionalStruct());

  // patch non cow struct
  ret = RootPatchApplier::apply(structA, PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_EQ(*structA.optionalInt(), *structB.optionalInt());
  EXPECT_EQ(*structA.optionalStruct(), *structB.optionalStruct());

  // unset optionals
  structPatch.children()[fieldId<TestStruct, k::optionalInt>()].set_del();
  structPatch.children()[fieldId<TestStruct, k::optionalStruct>()].set_del();
  n = PatchNode();
  n.set_struct_node(std::move(structPatch));

  ret = RootPatchApplier::apply(*nodeB, PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);

  EXPECT_FALSE(nodeB->isSet<k::optionalInt>());
  EXPECT_FALSE(nodeB->isSet<k::optionalStruct>());

  // patch non cow struct
  ret = RootPatchApplier::apply(structA, std::move(n));
  EXPECT_EQ(ret, PatchApplyResult::OK);
  EXPECT_FALSE(structA.optionalInt());
  EXPECT_FALSE(structA.optionalStruct());
}

TEST(PatchApplierTests, TestBadPatches) {
  auto structA = createSimpleTestStruct();
  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);

  PatchNode del;
  del.set_del();

  // invalid field id
  StructPatch structPatch;
  structPatch.children() = {{1234, del}};
  PatchNode n;
  n.set_struct_node(structPatch);

  auto ret = RootPatchApplier::apply(*nodeA, PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::INVALID_STRUCT_MEMBER);

  // invalid union id
  VariantPatch variantPatch;
  variantPatch.id() = 4321;
  n.set_variant_node(variantPatch);

  structPatch.children() = {{fieldId<TestStruct, k::inlineVariant>(), n}};
  n.set_struct_node(structPatch);

  ret = RootPatchApplier::apply(*nodeA, PatchNode(n));
  EXPECT_EQ(ret, PatchApplyResult::INVALID_VARIANT_MEMBER);
}
} // namespace facebook::fboss::thrift_cow
