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

using FsdbCowStateStorage = CowStorage<FsdbOperStateRoot>;

template bool FsdbCowStateStorage::isPublished_impl();

template void FsdbCowStateStorage::publish_impl();

template folly::Expected<OperState, StorageError>
    FsdbCowStateStorage::get_encoded_impl(
        FsdbCowStateStorage::PathIter,
        FsdbCowStateStorage::PathIter,
        OperProtocol) const;

template std::vector<TaggedOperState>
    FsdbCowStateStorage::get_encoded_extended_impl(
        FsdbCowStateStorage::ExtPathIter,
        FsdbCowStateStorage::ExtPathIter,
        OperProtocol) const;

template std::optional<StorageError> FsdbCowStateStorage::set_encoded_impl(
    FsdbCowStateStorage::PathIter,
    FsdbCowStateStorage::PathIter,
    const OperState&);

template std::optional<StorageError> FsdbCowStateStorage::patch_impl(
    const OperDelta&);

template std::optional<StorageError> FsdbCowStateStorage::patch_impl(
    const TaggedOperState&);

template std::optional<StorageError> FsdbCowStateStorage::remove_impl(
    FsdbCowStateStorage::PathIter,
    FsdbCowStateStorage::PathIter);

template const std::shared_ptr<FsdbCowStateStorage::StorageImpl>&
FsdbCowStateStorage::root() const;

template std::shared_ptr<FsdbCowStateStorage::StorageImpl>
FsdbCowStateStorage::root();

} // namespace facebook::fboss::fsdb
