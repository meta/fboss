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

template class CowStorage<FsdbOperStatsRoot>;
template CowStorage<FsdbOperStatsRoot>::CowStorage(const FsdbOperStatsRoot&);

} // namespace facebook::fboss::fsdb
