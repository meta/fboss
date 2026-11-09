/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/NodeMap.h"

namespace facebook::fboss {

using AclTableMapLegacyTraits = NodeMapTraits<std::string, AclTable>;

using AclTableMapTypeClass = apache::thrift::type_class::map<
    apache::thrift::type_class::string,
    apache::thrift::type_class::structure>;
using AclTableMapThriftType = std::map<std::string, state::AclTableFields>;

class AclTableMap;
using AclTableMapTraits = ThriftMapNodeTraits<
    AclTableMap,
    AclTableMapTypeClass,
    AclTableMapThriftType,
    AclTable>;

/*
 * A container for the set of tables.
 */
class AclTableMap : public ThriftMapNode<AclTableMap, AclTableMapTraits> {
 public:
  using BaseT = ThriftMapNode<AclTableMap, AclTableMapTraits>;
  using BaseT::modify;

  AclTableMap();
  ~AclTableMap() override;

  static std::shared_ptr<AclTableMap> createDefaultAclTableMapFromThrift(
      std::map<std::string, state::AclEntryFields> const& thriftMap);

  static std::shared_ptr<AclMap> getDefaultAclTableMap(
      std::map<std::string, state::AclTableFields> const& thriftMap);

  bool operator==(const AclTableMap& aclTableMap) const {
    if (numTables() != aclTableMap.numTables()) {
      return false;
    }
    for (auto const& iter : *this) {
      const auto& table = iter.second;
      if (!aclTableMap.getTableIf(table->getID()) ||
          *(aclTableMap.getTable(table->getID())) != *table) {
        return false;
      }
    }
    return true;
  }

  bool operator!=(const AclTableMap& aclTableMap) const {
    return !(*this == aclTableMap);
  }

  const std::shared_ptr<AclTable>& getTable(
      const std::string& tableName) const {
    return getNode(tableName);
  }
  std::shared_ptr<AclTable> getTableIf(const std::string& tableName) const {
    return getNodeIf(tableName);
  }

  size_t numTables() const {
    return size();
  }

  /*
   * The following functions modify the static state.
   * These should only be called on unpublished objects which are only visible
   * to a single thread.
   */

  void addTable(const std::shared_ptr<AclTable>& aclTable) {
    addNode(aclTable);
  }
  void removeTable(const std::string& tableName) {
    removeTable(getNode(tableName));
  }
  void removeTable(const std::shared_ptr<AclTable>& aclTable) {
    removeNode(aclTable);
  }

 private:
  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
