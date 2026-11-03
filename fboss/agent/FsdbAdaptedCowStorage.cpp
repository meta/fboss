// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/FsdbAdaptedCowStorage.h"

namespace facebook::fboss {

using FsdbAdaptedCowStorage =
    fsdb::CowStorage<fsdb::FsdbOperStateRoot, FsdbOperStateRoot>;

template bool FsdbAdaptedCowStorage::isPublished_impl();

template void FsdbAdaptedCowStorage::publish_impl();

template folly::Expected<fsdb::OperState, fsdb::StorageError>
    FsdbAdaptedCowStorage::get_encoded_impl(
        FsdbAdaptedCowStorage::PathIter,
        FsdbAdaptedCowStorage::PathIter,
        fsdb::OperProtocol) const;

template std::vector<fsdb::TaggedOperState>
    FsdbAdaptedCowStorage::get_encoded_extended_impl(
        FsdbAdaptedCowStorage::ExtPathIter,
        FsdbAdaptedCowStorage::ExtPathIter,
        fsdb::OperProtocol) const;

template std::optional<fsdb::StorageError> FsdbAdaptedCowStorage::patch_impl(
    const fsdb::OperDelta&);

template std::optional<fsdb::StorageError> FsdbAdaptedCowStorage::patch_impl(
    const fsdb::TaggedOperState&);

template const std::shared_ptr<FsdbAdaptedCowStorage::StorageImpl>&
FsdbAdaptedCowStorage::root() const;

template std::shared_ptr<FsdbAdaptedCowStorage::StorageImpl>
FsdbAdaptedCowStorage::root();

} // namespace facebook::fboss
