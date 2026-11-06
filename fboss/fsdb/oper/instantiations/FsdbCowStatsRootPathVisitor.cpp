// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fboss/fsdb/oper/instantiations/FsdbCowRootPathVisitor.h>

namespace facebook::fboss::thrift_cow {

template ThriftTraverseResult RootPathVisitor::visit<FsdbCowStatsRoot>(
    FsdbCowStatsRoot& node,
    pv_detail::PathIter begin,
    pv_detail::PathIter end,
    const PathVisitOptions& options,
    BasePathVisitorOperator& op);

template ThriftTraverseResult pv_detail::visitNode<
    apache::thrift::type_class::structure,
    FsdbCowStatsRoot,
    BasePathVisitorOperator>(
    FsdbCowStatsRoot& node,
    const pv_detail::VisitImplParams<BasePathVisitorOperator>& params,
    pv_detail::PathIter cursor,
    bool isContainerNode);

template ThriftTraverseResult pv_detail::visitNode<
    apache::thrift::type_class::structure,
    const FsdbCowStatsRoot,
    BasePathVisitorOperator>(
    const FsdbCowStatsRoot& node,
    const pv_detail::VisitImplParams<BasePathVisitorOperator>& params,
    pv_detail::PathIter cursor,
    bool isContainerNode);

} // namespace facebook::fboss::thrift_cow
