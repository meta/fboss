// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/FsdbAdaptedCowStorage.h"

namespace facebook::fboss {

using FsdbAdaptedCowStorage =
    fsdb::CowStorage<fsdb::FsdbOperStateRoot, FsdbOperStateRoot>;

template std::optional<fsdb::StorageError> FsdbAdaptedCowStorage::patch_impl(
    fsdb::Patch&&);

template std::optional<fsdb::StorageError>
FsdbAdaptedCowStorage::set_encoded_impl(
    FsdbAdaptedCowStorage::PathIter,
    FsdbAdaptedCowStorage::PathIter,
    const fsdb::OperState&);

template std::optional<fsdb::StorageError> FsdbAdaptedCowStorage::remove_impl(
    FsdbAdaptedCowStorage::PathIter,
    FsdbAdaptedCowStorage::PathIter);

} // namespace facebook::fboss
