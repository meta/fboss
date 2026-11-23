// (c) Meta Platforms, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/fsdb/oper/DeltaValue.h"

namespace facebook::fboss::fsdb {

size_t getPatchNodeSize(const thrift_cow::PatchNode& val) {
  switch (val.getType()) {
    case thrift_cow::PatchNode::Type::val:
      return val.val()->length();
    case thrift_cow::PatchNode::Type::struct_node: {
      size_t totalSize = 0;
      const auto& structNode = val.struct_node().value();
      for (const auto& [key, child] : *structNode.children()) {
        totalSize += getPatchNodeSize(child);
      }
      if (structNode.compressedChildren()) {
        totalSize += structNode.compressedChildren()->computeChainDataLength();
      }
      return totalSize;
    }
    case thrift_cow::PatchNode::Type::map_node: {
      size_t totalSize = 0;
      const auto& mapNode = val.map_node().value();
      for (const auto& [key, child] : *mapNode.children()) {
        totalSize += getPatchNodeSize(child);
      }
      if (mapNode.compressedChildren()) {
        totalSize += mapNode.compressedChildren()->computeChainDataLength();
      }
      return totalSize;
    }
    case thrift_cow::PatchNode::Type::list_node: {
      size_t totalSize = 0;
      const auto& listNode = val.list_node().value();
      for (const auto& [key, child] : *listNode.children()) {
        totalSize += getPatchNodeSize(child);
      }
      if (listNode.compressedChildren()) {
        totalSize += listNode.compressedChildren()->computeChainDataLength();
      }
      return totalSize;
    }
    case thrift_cow::PatchNode::Type::set_node: {
      size_t totalSize = 0;
      const auto& setNode = val.set_node().value();
      for (const auto& [key, child] : *setNode.children()) {
        totalSize += getPatchNodeSize(child);
      }
      if (setNode.compressedChildren()) {
        totalSize += setNode.compressedChildren()->computeChainDataLength();
      }
      return totalSize;
    }
    case thrift_cow::PatchNode::Type::variant_node: {
      if (val.variant_node()->child()) {
        return getPatchNodeSize(*val.variant_node()->child());
      }
      return 0;
    }
    case thrift_cow::PatchNode::Type::del:
      return 0;
    default:
      return 0;
  }
}

size_t getOperDeltaSize(const OperDelta& delta) {
  size_t length{0};
  for (const auto& unit : *delta.changes()) {
    length +=
        unit.oldState().has_value() ? unit.oldState().value().length() : 0;
    length +=
        unit.newState().has_value() ? unit.newState().value().length() : 0;
    for (const auto& pathElem : *unit.path()->raw()) {
      length += pathElem.length();
    }
  }
  return length;
}

size_t getExtendedDeltaSize(const std::vector<TaggedOperDelta>& extDelta) {
  size_t length{0};
  for (const auto& delta : extDelta) {
    length += getOperDeltaSize(*delta.delta());
    for (const auto& pathElem : *delta.path()->path()) {
      length += pathElem.length();
    }
  }
  return length;
}

} // namespace facebook::fboss::fsdb
