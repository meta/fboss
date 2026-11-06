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

#include <fboss/fsdb/oper/instantiations/FsdbCowRoot.h>
#include <fboss/fsdb/oper/instantiations/FsdbCowRootPathVisitor.h>
#include <fboss/fsdb/oper/instantiations/templates/FsdbPatchApplierOperStateInstantiations.h>
#include <fboss/fsdb/oper/instantiations/templates/FsdbPathVisitorOperStateInstantiations.h>
#include <fboss/fsdb/oper/instantiations/templates/FsdbThriftStructOperInstantiations.h>
#include <fboss/thrift_cow/storage/CowStorage.h>
#include "fboss/fsdb/if/FsdbModel.h"

namespace facebook::fboss::fsdb {

extern template class CowStorage<FsdbOperStateRoot>;
extern template CowStorage<FsdbOperStateRoot>::CowStorage(
    const FsdbOperStateRoot&);

extern template class CowStorage<
    FsdbOperStateRoot,
    thrift_cow::FsdbHybridCowStateRoot>;
extern template CowStorage<
    FsdbOperStateRoot,
    thrift_cow::FsdbHybridCowStateRoot>::CowStorage(const FsdbOperStateRoot&);

extern template class CowStorage<FsdbOperStatsRoot>;
extern template CowStorage<FsdbOperStatsRoot>::CowStorage(
    const FsdbOperStatsRoot&);

} // namespace facebook::fboss::fsdb
