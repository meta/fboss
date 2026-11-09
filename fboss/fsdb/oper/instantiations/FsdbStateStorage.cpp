/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/fsdb/oper/instantiations/FsdbStateStorage.h"

#include <type_traits>
#include <utility>

#include "fboss/fsdb/oper/instantiations/FsdbNaivePeriodicSubscribableStorage.h"

namespace facebook::fboss::fsdb {

namespace {

template <typename StorageT>
class FsdbStateStorageImpl : public FsdbStateStorage {
 public:
  FsdbStateStorageImpl(
      FsdbOperStateRoot root,
      const NaivePeriodicSubscribableStorageBase::StorageParams& params)
      : storage_(std::move(root), params) {}

  NaivePeriodicSubscribableStorageBase& base() override {
    return storage_;
  }

  std::optional<StorageError>
  set_encoded(PathIter begin, PathIter end, const OperState& state) override {
    return storage_.set_encoded(begin, end, state);
  }

  std::optional<StorageError> patch(const OperDelta& delta) override {
    return storage_.patch(delta);
  }

  std::optional<StorageError> patch(Patch&& patch) override {
    return storage_.patch(std::move(patch));
  }

  Result<OperState> get_encoded(
      PathIter begin,
      PathIter end,
      OperProtocol protocol) const override {
    return storage_.get_encoded(begin, end, protocol);
  }

  Result<std::vector<TaggedOperState>> get_encoded_extended(
      ExtPathIter begin,
      ExtPathIter end,
      OperProtocol protocol) const override {
    return storage_.get_encoded_extended(begin, end, protocol);
  }

  FsdbOperStateRoot currentStateExpensive() const override {
    return storage_.currentStateExpensive();
  }

  bool usingHybridStorage() const override {
    return std::is_same_v<StorageT, FsdbHybridNaivePeriodicSubscribableStorage>;
  }

 private:
  StorageT storage_;
};

} // namespace

std::unique_ptr<FsdbStateStorage> makeFsdbStateStorage(
    bool enableHybridStorage,
    FsdbOperStateRoot root,
    const NaivePeriodicSubscribableStorageBase::StorageParams& params) {
  if (enableHybridStorage) {
    return std::make_unique<
        FsdbStateStorageImpl<FsdbHybridNaivePeriodicSubscribableStorage>>(
        std::move(root), params);
  }
  return std::make_unique<
      FsdbStateStorageImpl<FsdbNaivePeriodicSubscribableStorage>>(
      std::move(root), params);
}

} // namespace facebook::fboss::fsdb
