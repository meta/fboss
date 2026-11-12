// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/fsdb/oper/instantiations/FsdbCowStorage.h"

namespace facebook::fboss::fsdb {

using FsdbCowStateStorage = CowStorage<FsdbOperStateRoot>;

template std::optional<StorageError> FsdbCowStateStorage::patch_impl(Patch&&);

template FsdbCowStateStorage::CowStorage(const FsdbOperStateRoot&);

} // namespace facebook::fboss::fsdb
