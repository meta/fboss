// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <fboss/fsdb/if/FsdbModel.h>
#include <fboss/fsdb/oper/instantiations/FsdbCowRoot.h>
#include <fboss/thrift_cow/visitors/PathVisitor.h>

namespace facebook::fboss::thrift_cow {

extern template ThriftTraverseResult RootPathVisitor::visit<FsdbCowStateRoot>(
    FsdbCowStateRoot& node,
    pv_detail::PathIter begin,
    pv_detail::PathIter end,
    const PathVisitOptions& options,
    BasePathVisitorOperator& op);

extern template ThriftTraverseResult pv_detail::visitNode<
    apache::thrift::type_class::structure,
    FsdbCowStateRoot,
    BasePathVisitorOperator>(
    FsdbCowStateRoot& node,
    const pv_detail::VisitImplParams<BasePathVisitorOperator>& params,
    pv_detail::PathIter cursor,
    bool isContainerNode);

extern template ThriftTraverseResult RootPathVisitor::visit<FsdbCowStatsRoot>(
    FsdbCowStatsRoot& node,
    pv_detail::PathIter begin,
    pv_detail::PathIter end,
    const PathVisitOptions& options,
    BasePathVisitorOperator& op);

extern template ThriftTraverseResult pv_detail::visitNode<
    apache::thrift::type_class::structure,
    FsdbCowStatsRoot,
    BasePathVisitorOperator>(
    FsdbCowStatsRoot& node,
    const pv_detail::VisitImplParams<BasePathVisitorOperator>& params,
    pv_detail::PathIter cursor,
    bool isContainerNode);

extern template ThriftTraverseResult pv_detail::visitNode<
    apache::thrift::type_class::structure,
    const FsdbCowStatsRoot,
    BasePathVisitorOperator>(
    const FsdbCowStatsRoot& node,
    const pv_detail::VisitImplParams<BasePathVisitorOperator>& params,
    pv_detail::PathIter cursor,
    bool isContainerNode);

extern template ThriftTraverseResult pv_detail::visitNode<
    apache::thrift::type_class::structure,
    FsdbHybridCowStateRoot,
    BasePathVisitorOperator>(
    FsdbHybridCowStateRoot& node,
    const pv_detail::VisitImplParams<BasePathVisitorOperator>& params,
    pv_detail::PathIter cursor,
    bool isContainerNode);

extern template ThriftTraverseResult pv_detail::visitNode<
    apache::thrift::type_class::structure,
    const FsdbHybridCowStateRoot,
    BasePathVisitorOperator>(
    const FsdbHybridCowStateRoot& node,
    const pv_detail::VisitImplParams<BasePathVisitorOperator>& params,
    pv_detail::PathIter cursor,
    bool isContainerNode);

} // namespace facebook::fboss::thrift_cow
