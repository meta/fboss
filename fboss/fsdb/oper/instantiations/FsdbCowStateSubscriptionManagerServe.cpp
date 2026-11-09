/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fboss/fsdb/oper/instantiations/FsdbCowSubscriptionManager.h>

namespace facebook::fboss::fsdb {

template void
CowSubscriptionManager<thrift_cow::FsdbCowStateRoot>::serveSubscriptions(
    SubscriptionStore&,
    const std::shared_ptr<thrift_cow::FsdbCowStateRoot>&,
    const std::shared_ptr<thrift_cow::FsdbCowStateRoot>&,
    const SubscriptionMetadataServer&);

} // namespace facebook::fboss::fsdb
