// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include <type_traits>

#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"
#include "fboss/thrift_cow/nodes/Types.h"
#include "fboss/thrift_cow/visitors/PatchBuilder.h"
#include "fboss/thrift_cow/visitors/tests/VisitorTestUtils.h"

namespace {
using namespace facebook::fboss;
using namespace facebook::fboss::thrift_cow;
using namespace apache::thrift;

template <typename TC, typename T>
T deserializeNode(PatchNode&& node) {
  EXPECT_EQ(node.getType(), PatchNode::Type::val);
  return deserializeBuf<TC, T>(fsdb::OperProtocol::COMPACT, node.move_val());
}

int32_t deserializeInt(PatchNode&& node) {
  return deserializeNode<type_class::integral, int32_t>(std::move(node));
}

std::string deserializeString(PatchNode&& node) {
  return deserializeNode<type_class::string, std::string>(std::move(node));
}

template <typename T>
T deserializeStruct(PatchNode&& node) {
  return deserializeNode<type_class::structure, T>(std::move(node));
}

StructPatch patchRoot(const TestStruct& s1, const TestStruct& s2) {
  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(s1);
  auto nodeB = std::make_shared<ThriftStructNode<TestStruct>>(s2);
  return PatchBuilder::build(nodeA, nodeB, {})
      .patch()
      ->struct_node()
      .as_const()
      .value();
}

} // namespace

namespace facebook::fboss::thrift_cow {

TEST(PatchBuilderTests, Simple) {
  auto structA = createSimpleTestStruct();
  auto structB = structA;
  structB.inlineInt() = 123;
  structB.inlineString() = "new value";

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 2);

  auto val = deserializeInt(
      std::move(patch.children()->at(fieldId<TestStruct, k::inlineInt>())));
  EXPECT_EQ(val, 123);

  auto s = deserializeString(
      std::move(patch.children()->at(fieldId<TestStruct, k::inlineString>())));
  EXPECT_EQ(s, "new value");
}

TEST(PatchBuilderTests, NestedStructVals) {
  auto structA = createSimpleTestStruct();
  auto structB = structA;
  structB.inlineStruct()->min() = 12345;
  structB.inlineStruct()->max() = 54321;

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  auto inlineStruct = patch.children()
                          ->at(fieldId<TestStruct, k::inlineStruct>())
                          .struct_node()
                          .as_const()
                          .value();

  EXPECT_EQ(inlineStruct.children()->size(), 2);
  auto value = deserializeInt(
      std::move(
          inlineStruct.children()->at(fieldId<cfg::L4PortRange, k::min>())));
  EXPECT_EQ(value, 12345);

  auto maxId = fieldId<cfg::L4PortRange, k::max>();
  value = deserializeInt(std::move(inlineStruct.children()->at(maxId)));
  EXPECT_EQ(value, 54321);
}

TEST(PatchBuilderTests, SetUnsetOptionals) {
  auto structA = createSimpleTestStruct();
  structA.optionalString().reset();
  structA.optionalStruct().reset();
  auto structB = structA;
  structB.optionalString() = "new val";
  cfg::L4PortRange pr;
  pr.min() = 123;
  structB.optionalStruct() = pr;

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 2);

  auto optionalString =
      patch.children()->at(fieldId<TestStruct, k::optionalString>());
  auto value = deserializeString(std::move(optionalString));
  EXPECT_EQ(value, "new val");

  auto optionalStruct =
      patch.children()->at(fieldId<TestStruct, k::optionalStruct>());
  auto prBack = deserializeStruct<cfg::L4PortRange>(std::move(optionalStruct));
  EXPECT_EQ(pr, prBack);

  // reverse
  patch = patchRoot(structB, structA);
  EXPECT_EQ(patch.children()->size(), 2);
  EXPECT_EQ(
      patch.children()->at(fieldId<TestStruct, k::optionalString>()).getType(),
      PatchNode::Type::del);
  EXPECT_EQ(
      patch.children()->at(fieldId<TestStruct, k::optionalStruct>()).getType(),
      PatchNode::Type::del);
}

TEST(PatchBuilderTests, ModifyVariant) {
  auto structA = createSimpleTestStruct();
  TestUnion v;
  v.set_inlineInt(123);
  structA.inlineVariant() = v;
  auto structB = structA;
  v.set_inlineInt(321);
  structB.inlineVariant() = v;

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  auto inlineVariant = patch.children()
                           ->at(fieldId<TestStruct, k::inlineVariant>())
                           .variant_node()
                           .as_const()
                           .value();

  EXPECT_EQ(*inlineVariant.id(), (fieldId<TestUnion, k::inlineInt>()));
  auto val = deserializeInt(std::move(*inlineVariant.child()));
  EXPECT_EQ(val, 321);
}

TEST(PatchBuilderTests, ModifyVariantType) {
  auto structA = createSimpleTestStruct();
  TestUnion v;
  v.set_inlineInt(123);
  structA.inlineVariant() = v;
  auto structB = structA;
  v.set_inlineStruct();
  v.inlineStruct()->min() = 111;
  v.inlineStruct()->max() = 123;
  structB.inlineVariant() = v;

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  auto inlineVariant = patch.children()
                           ->at(fieldId<TestStruct, k::inlineVariant>())
                           .variant_node()
                           .as_const()
                           .value();
  EXPECT_EQ(*inlineVariant.id(), (fieldId<TestUnion, k::inlineStruct>()));
  auto pr =
      deserializeStruct<cfg::L4PortRange>(std::move(*inlineVariant.child()));
  EXPECT_EQ(pr, structB.inlineVariant()->inlineStruct().as_const().value());

  // reverse
  patch = patchRoot(structB, structA);
  EXPECT_EQ(patch.children()->size(), 1);

  inlineVariant = patch.children()
                      ->at(fieldId<TestStruct, k::inlineVariant>())
                      .variant_node()
                      .as_const()
                      .value();
  EXPECT_EQ(*inlineVariant.id(), (fieldId<TestUnion, k::inlineInt>()));
  auto val = deserializeInt(std::move(*inlineVariant.child()));
  EXPECT_EQ(val, 123);
}

TEST(PatchBuilderTests, ModifyVariantStructVal) {
  auto structA = createSimpleTestStruct();
  TestUnion v;
  v.set_inlineStruct();
  v.inlineStruct()->min() = 123;
  structA.inlineVariant() = v;
  auto structB = structA;
  v.inlineStruct()->min() = 321;
  structB.inlineVariant() = v;

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  auto inlineVariant = patch.children()
                           ->at(fieldId<TestStruct, k::inlineVariant>())
                           .variant_node()
                           .as_const()
                           .value();

  EXPECT_EQ(*inlineVariant.id(), (fieldId<TestUnion, k::inlineStruct>()));
  auto inlineStruct = inlineVariant.child()->struct_node().as_const().value();
  EXPECT_EQ(inlineStruct.children()->size(), 1);
  auto valuePatch =
      inlineStruct.children()->at(fieldId<cfg::L4PortRange, k::min>());
  auto val = deserializeInt(std::move(valuePatch));
  EXPECT_EQ(val, 321);
}

TEST(PatchBuilderTests, ListOfPrimitives) {
  auto structA = createSimpleTestStruct();
  structA.listOfPrimitives() = {1, 2};
  auto structB = structA;
  // unchanged, modified, added
  structB.listOfPrimitives() = {1, 4, 5};

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  auto listOfPrimitives = patch.children()
                              ->at(fieldId<TestStruct, k::listOfPrimitives>())
                              .list_node()
                              .as_const()
                              .value();
  ASSERT_EQ(listOfPrimitives.children()->size(), 2);
  // modified
  auto val = deserializeInt(std::move(listOfPrimitives.children()->at(1)));
  EXPECT_EQ(val, 4);
  // added
  val = deserializeInt(std::move(listOfPrimitives.children()->at(2)));
  EXPECT_EQ(val, 5);

  // reverse
  patch = patchRoot(structB, structA);
  EXPECT_EQ(patch.children()->size(), 1);

  listOfPrimitives = patch.children()
                         ->at(fieldId<TestStruct, k::listOfPrimitives>())
                         .list_node()
                         .as_const()
                         .value();

  ASSERT_EQ(listOfPrimitives.children()->size(), 2);
  val = deserializeInt(std::move(listOfPrimitives.children()->at(1)));
  EXPECT_EQ(val, 2);
  EXPECT_EQ(listOfPrimitives.children()->at(2).getType(), PatchNode::Type::del);
}

TEST(PatchBuilderTests, ListOfStructs) {
  auto structA = createSimpleTestStruct();
  cfg::L4PortRange pr1, pr2, pr3;
  pr1.min() = 11;
  pr2.min() = 21;
  structA.listOfStructs() = {pr1, pr2};

  auto structB = structA;
  pr2.min() = 211;
  pr3.min() = 311;
  // unchanged, modified, added
  structB.listOfStructs() = {pr1, pr2, pr3};

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  // listOfPrimitives
  auto listOfStructs = patch.children()
                           ->at(fieldId<TestStruct, k::listOfStructs>())
                           .list_node()
                           .as_const()
                           .value();
  ASSERT_EQ(listOfStructs.children()->size(), 2);

  auto structPatch =
      listOfStructs.children()->at(1).struct_node().as_const().value();
  // modified
  EXPECT_EQ(structPatch.children()->size(), 1);
  auto modifiedVal = deserializeInt(
      std::move(
          structPatch.children()->at(fieldId<cfg::L4PortRange, k::min>())));
  EXPECT_EQ(modifiedVal, 211);

  // added
  auto newStruct = deserializeStruct<cfg::L4PortRange>(
      std::move(listOfStructs.children()->at(2)));
  EXPECT_EQ(*newStruct.min(), 311);

  // reverse
  patch = patchRoot(structB, structA);
  EXPECT_EQ(patch.children()->size(), 1);

  listOfStructs = patch.children()
                      ->at(fieldId<TestStruct, k::listOfStructs>())
                      .list_node()
                      .as_const()
                      .value();

  ASSERT_EQ(listOfStructs.children()->size(), 2);
  structPatch =
      listOfStructs.children()->at(1).struct_node().as_const().value();
  EXPECT_EQ(structPatch.children()->size(), 1);
  modifiedVal = deserializeInt(
      std::move(
          structPatch.children()->at(fieldId<cfg::L4PortRange, k::min>())));
  EXPECT_EQ(modifiedVal, 21);
  EXPECT_EQ(listOfStructs.children()->at(2).getType(), PatchNode::Type::del);
}

TEST(PatchBuilderTests, ListOfLists) {
  auto structA = createSimpleTestStruct();
  structA.listOfListOfPrimitives() = {{1, 2}, {3}};

  auto structB = structA;
  // unchanged, modified, added
  structB.listOfListOfPrimitives() = {{1, 2}, {4, 5}, {6, 7}};

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  auto listOfListOfPrimitives =
      patch.children()
          ->at(fieldId<TestStruct, k::listOfListOfPrimitives>())
          .list_node()
          .as_const()
          .value();
  ASSERT_EQ(listOfListOfPrimitives.children()->size(), 2);

  auto listPatch =
      listOfListOfPrimitives.children()->at(1).list_node().as_const().value();
  EXPECT_EQ(listPatch.children()->size(), 2);
  // modified
  auto val = deserializeInt(std::move(listPatch.children()->at(0)));
  EXPECT_EQ(val, 4);
  val = deserializeInt(std::move(listPatch.children()->at(1)));
  EXPECT_EQ(val, 5);

  // added
  auto addedList = deserializeNode<
      type_class::list<type_class::integral>,
      std::vector<int32_t>>(
      std::move(listOfListOfPrimitives.children()->at(2)));
  EXPECT_EQ(addedList.size(), 2);
  EXPECT_EQ(addedList, std::vector<int32_t>({6, 7}));

  // reverse
  patch = patchRoot(structB, structA);
  EXPECT_EQ(patch.children()->size(), 1);

  listOfListOfPrimitives =
      patch.children()
          ->at(fieldId<TestStruct, k::listOfListOfPrimitives>())
          .list_node()
          .as_const()
          .value();

  listPatch =
      listOfListOfPrimitives.children()->at(1).list_node().as_const().value();
  EXPECT_EQ(listPatch.children()->size(), 2);
  val = deserializeInt(std::move(listPatch.children()->at(0)));
  EXPECT_EQ(val, 3);
  EXPECT_EQ(listPatch.children()->at(1).getType(), PatchNode::Type::del);

  EXPECT_EQ(
      listOfListOfPrimitives.children()->at(2).getType(), PatchNode::Type::del);
}

TEST(PatchBuilderTests, MapOfEnumToI32) {
  auto structA = createSimpleTestStruct();
  structA.mapOfEnumToI32() = {{TestEnum::FIRST, 2}, {TestEnum::SECOND, 4}};
  auto structB = structA;
  // unchanged, modified, added
  structB.mapOfEnumToI32() = {
      {TestEnum::FIRST, 2}, {TestEnum::SECOND, 5}, {TestEnum::THIRD, 7}};

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  auto mapOfEnumToI32 = patch.children()
                            ->at(fieldId<TestStruct, k::mapOfEnumToI32>())
                            .map_node()
                            .as_const()
                            .value();
  ASSERT_EQ(mapOfEnumToI32.children()->size(), 2);

  // modified
  auto val = deserializeInt(std::move(mapOfEnumToI32.children()->at("2")));
  EXPECT_EQ(val, 5);
  // added
  val = deserializeInt(std::move(mapOfEnumToI32.children()->at("3")));
  EXPECT_EQ(val, 7);

  // reverse
  patch = patchRoot(structB, structA);
  EXPECT_EQ(patch.children()->size(), 1);

  mapOfEnumToI32 = patch.children()
                       ->at(fieldId<TestStruct, k::mapOfEnumToI32>())
                       .map_node()
                       .as_const()
                       .value();

  ASSERT_EQ(mapOfEnumToI32.children()->size(), 2);
  val = deserializeInt(std::move(mapOfEnumToI32.children()->at("2")));
  EXPECT_EQ(val, 4);
  EXPECT_EQ(mapOfEnumToI32.children()->at("3").getType(), PatchNode::Type::del);
}

TEST(PatchBuilderTests, MapOfI32ToStruct) {
  auto structA = createSimpleTestStruct();
  cfg::L4PortRange pr1, pr2, pr3;
  pr1.min() = 11;
  pr2.min() = 21;
  structA.mapOfI32ToStruct() = {{1, pr1}, {2, pr2}};

  auto structB = structA;
  pr2.min() = 211;
  pr3.min() = 311;
  // unchanged, modified, added
  structB.mapOfI32ToStruct() = {{1, pr1}, {2, pr2}, {3, pr3}};

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  auto mapOfI32ToStruct = patch.children()
                              ->at(fieldId<TestStruct, k::mapOfI32ToStruct>())
                              .map_node()
                              .as_const()
                              .value();
  ASSERT_EQ(mapOfI32ToStruct.children()->size(), 2);

  auto structPatch =
      mapOfI32ToStruct.children()->at("2").struct_node().as_const().value();
  // modified
  EXPECT_EQ(structPatch.children()->size(), 1);
  auto modifiedVal = deserializeInt(
      std::move(
          structPatch.children()->at(fieldId<cfg::L4PortRange, k::min>())));
  EXPECT_EQ(modifiedVal, 211);

  // added
  auto newStruct = deserializeStruct<cfg::L4PortRange>(
      std::move(mapOfI32ToStruct.children()->at("3")));
  EXPECT_EQ(*newStruct.min(), 311);

  // reverse
  patch = patchRoot(structB, structA);
  EXPECT_EQ(patch.children()->size(), 1);

  mapOfI32ToStruct = patch.children()
                         ->at(fieldId<TestStruct, k::mapOfI32ToStruct>())
                         .map_node()
                         .as_const()
                         .value();

  ASSERT_EQ(mapOfI32ToStruct.children()->size(), 2);
  structPatch =
      mapOfI32ToStruct.children()->at("2").struct_node().as_const().value();
  EXPECT_EQ(structPatch.children()->size(), 1);
  modifiedVal = deserializeInt(
      std::move(
          structPatch.children()->at(fieldId<cfg::L4PortRange, k::min>())));
  EXPECT_EQ(modifiedVal, 21);
  EXPECT_EQ(
      mapOfI32ToStruct.children()->at("3").getType(), PatchNode::Type::del);
}

TEST(PatchBuilderTests, SetOfI32) {
  auto structA = createSimpleTestStruct();
  structA.setOfI32() = {1, 2};
  auto structB = structA;
  // unchanged, modified (deleted + added)
  structB.setOfI32() = {1, 3};

  auto patch = patchRoot(structA, structB);
  EXPECT_EQ(patch.children()->size(), 1);

  auto setOfI32 = patch.children()
                      ->at(fieldId<TestStruct, k::setOfI32>())
                      .set_node()
                      .as_const()
                      .value();
  ASSERT_EQ(setOfI32.children()->size(), 2);

  // deleted
  EXPECT_EQ(setOfI32.children()->at("2").getType(), PatchNode::Type::del);
  // added
  auto val = deserializeInt(std::move(setOfI32.children()->at("3")));
  EXPECT_EQ(val, 3);
}

TEST(PatchBuilderTests, PatchNonStruct) {
  auto structA = createSimpleTestStruct();
  structA.listOfPrimitives() = {2, 3};
  auto structB = structA;
  // unchanged, modified (deleted + added)
  structB.listOfPrimitives() = {4, 5};

  auto nodeA = std::make_shared<ThriftStructNode<TestStruct>>(structA);
  auto nodeB = std::make_shared<ThriftStructNode<TestStruct>>(structB);

  std::vector<std::string> path = {
      folly::to<std::string>(fieldId<TestStruct, k::listOfPrimitives>())};
  auto patch = PatchBuilder::build(
      nodeA->ref<k::listOfPrimitives>(),
      nodeB->ref<k::listOfPrimitives>(),
      path);

  EXPECT_EQ(*patch.basePath(), path);
  EXPECT_EQ(patch.patch()->getType(), PatchNode::Type::list_node);
  auto patchRoot = patch.patch()->list_node().as_const().value();
  EXPECT_EQ(patchRoot.children()->size(), 2);
}
} // namespace facebook::fboss::thrift_cow
