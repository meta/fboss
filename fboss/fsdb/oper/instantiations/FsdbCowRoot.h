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

#include "fboss/fsdb/if/FsdbModel.h"
#include "fboss/thrift_cow/nodes/Types.h"

namespace facebook::fboss::thrift_cow {

extern template class ThriftStructNode<fsdb::FsdbOperStateRoot>;
using FsdbCowStateRoot = ThriftStructNode<fsdb::FsdbOperStateRoot>;

extern template class ThriftStructNode<
    fsdb::FsdbOperStateRoot,
    ThriftStructResolver<fsdb::FsdbOperStateRoot, true>,
    /*EnableHybridStorage=*/true>;
using FsdbHybridCowStateRoot = ThriftStructNode<
    fsdb::FsdbOperStateRoot,
    ThriftStructResolver<fsdb::FsdbOperStateRoot, true>,
    /*EnableHybridStorage=*/true>;

extern template class ThriftStructNode<fsdb::FsdbOperStatsRoot>;
using FsdbCowStatsRoot = ThriftStructNode<fsdb::FsdbOperStatsRoot>;

} // namespace facebook::fboss::thrift_cow
