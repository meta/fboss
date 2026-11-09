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

#include <fboss/fsdb/oper/NaivePeriodicSubscribableStorage.h>
#include <fboss/fsdb/oper/instantiations/FsdbCowRoot.h>
#include <fboss/fsdb/oper/instantiations/FsdbCowStorage.h>
#include <fboss/fsdb/oper/instantiations/FsdbCowSubscriptionManager.h>
#include <fboss/fsdb/oper/instantiations/FsdbPathConverter.h>
#include "fboss/fsdb/if/FsdbModel.h"
#include "fboss/fsdb/oper/instantiations/templates/FsdbStateSubscriptionManagerInstantiations.h"
#include "fboss/fsdb/oper/instantiations/templates/FsdbThriftStructStdFunctionsInstantiations.h"

namespace facebook::fboss::fsdb {

extern template class NaivePeriodicSubscribableStorage<
    CowStorage<FsdbOperStateRoot>,
    CowSubscriptionManager<thrift_cow::FsdbCowStateRoot>>;

using FsdbNaivePeriodicSubscribableStorage = NaivePeriodicSubscribableStorage<
    CowStorage<FsdbOperStateRoot>,
    CowSubscriptionManager<thrift_cow::FsdbCowStateRoot>>;

extern template class NaivePeriodicSubscribableStorage<
    CowStorage<FsdbOperStateRoot, thrift_cow::FsdbHybridCowStateRoot>,
    CowSubscriptionManager<thrift_cow::FsdbHybridCowStateRoot>>;

using FsdbHybridNaivePeriodicSubscribableStorage =
    NaivePeriodicSubscribableStorage<
        CowStorage<FsdbOperStateRoot, thrift_cow::FsdbHybridCowStateRoot>,
        CowSubscriptionManager<thrift_cow::FsdbHybridCowStateRoot>>;

extern template class NaivePeriodicSubscribableStorage<
    CowStorage<FsdbOperStatsRoot>,
    CowSubscriptionManager<thrift_cow::FsdbCowStatsRoot>>;

using FsdbNaivePeriodicSubscribableStatsStorage =
    NaivePeriodicSubscribableStorage<
        CowStorage<FsdbOperStatsRoot>,
        CowSubscriptionManager<thrift_cow::FsdbCowStatsRoot>>;
} // namespace facebook::fboss::fsdb
