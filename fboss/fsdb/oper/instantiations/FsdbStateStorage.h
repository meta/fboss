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

#include <folly/Expected.h>
#include <memory>
#include <optional>
#include <vector>

#include <fboss/thrift_cow/storage/Storage.h>
#include "fboss/fsdb/if/FsdbModel.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"
#include "fboss/fsdb/oper/NaivePeriodicSubscribableStorageBase.h"

namespace facebook::fboss::fsdb {

// Abstracts the FSDB state tree over its thrift_cow node flavor. Names no
// thrift_cow template, so including TUs form no storage specializations.
class FsdbStateStorage {
 public:
  using PathIter = NaivePeriodicSubscribableStorageBase::PathIter;
  using ExtPathIter = NaivePeriodicSubscribableStorageBase::ExtPathIter;
  template <typename T>
  using Result = folly::Expected<T, StorageError>;

  virtual ~FsdbStateStorage() = default;

  virtual NaivePeriodicSubscribableStorageBase& base() = 0;

  virtual std::optional<StorageError>
  set_encoded(PathIter begin, PathIter end, const OperState& state) = 0;
  virtual std::optional<StorageError> patch(const OperDelta& delta) = 0;
  virtual std::optional<StorageError> patch(Patch&& patch) = 0;

  virtual Result<OperState>
  get_encoded(PathIter begin, PathIter end, OperProtocol protocol) const = 0;
  virtual Result<std::vector<TaggedOperState>> get_encoded_extended(
      ExtPathIter begin,
      ExtPathIter end,
      OperProtocol protocol) const = 0;

  virtual FsdbOperStateRoot currentStateExpensive() const = 0;

  virtual bool usingHybridStorage() const = 0;
};

std::unique_ptr<FsdbStateStorage> makeFsdbStateStorage(
    bool enableHybridStorage,
    FsdbOperStateRoot root,
    const NaivePeriodicSubscribableStorageBase::StorageParams& params);

} // namespace facebook::fboss::fsdb
