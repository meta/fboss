/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/fsdb/oper/instantiations/FsdbCowStorage.h"

namespace facebook::fboss::fsdb {

using FsdbCowHybridStateStorage =
    CowStorage<FsdbOperStateRoot, thrift_cow::FsdbHybridCowStateRoot>;

template bool FsdbCowHybridStateStorage::isPublished_impl();

template void FsdbCowHybridStateStorage::publish_impl();

template folly::Expected<OperState, StorageError>
    FsdbCowHybridStateStorage::get_encoded_impl(
        FsdbCowHybridStateStorage::PathIter,
        FsdbCowHybridStateStorage::PathIter,
        OperProtocol) const;

template std::vector<TaggedOperState>
    FsdbCowHybridStateStorage::get_encoded_extended_impl(
        FsdbCowHybridStateStorage::ExtPathIter,
        FsdbCowHybridStateStorage::ExtPathIter,
        OperProtocol) const;

template std::optional<StorageError>
FsdbCowHybridStateStorage::set_encoded_impl(
    FsdbCowHybridStateStorage::PathIter,
    FsdbCowHybridStateStorage::PathIter,
    const OperState&);

template std::optional<StorageError> FsdbCowHybridStateStorage::patch_impl(
    const OperDelta&);

template std::optional<StorageError> FsdbCowHybridStateStorage::patch_impl(
    const TaggedOperState&);

template std::optional<StorageError> FsdbCowHybridStateStorage::remove_impl(
    FsdbCowHybridStateStorage::PathIter,
    FsdbCowHybridStateStorage::PathIter);

template const std::shared_ptr<FsdbCowHybridStateStorage::StorageImpl>&
FsdbCowHybridStateStorage::root() const;

template std::shared_ptr<FsdbCowHybridStateStorage::StorageImpl>
FsdbCowHybridStateStorage::root();

} // namespace facebook::fboss::fsdb
