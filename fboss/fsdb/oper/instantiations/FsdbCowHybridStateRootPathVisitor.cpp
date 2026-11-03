// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fboss/fsdb/oper/instantiations/FsdbCowRootPathVisitor.h>

namespace facebook::fboss::thrift_cow {

template ThriftTraverseResult pv_detail::visitNode<
    apache::thrift::type_class::structure,
    FsdbHybridCowStateRoot,
    BasePathVisitorOperator>(
    FsdbHybridCowStateRoot& node,
    const pv_detail::VisitImplParams<BasePathVisitorOperator>& params,
    pv_detail::PathIter cursor,
    bool isContainerNode);

template ThriftTraverseResult pv_detail::visitNode<
    apache::thrift::type_class::structure,
    const FsdbHybridCowStateRoot,
    BasePathVisitorOperator>(
    const FsdbHybridCowStateRoot& node,
    const pv_detail::VisitImplParams<BasePathVisitorOperator>& params,
    pv_detail::PathIter cursor,
    bool isContainerNode);

} // namespace facebook::fboss::thrift_cow
