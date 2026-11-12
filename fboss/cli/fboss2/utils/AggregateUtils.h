// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <thrift/lib/cpp2/op/Get.h>
#include "fboss/cli/fboss2/CmdGlobalOptions.h"

namespace facebook::fboss {

template <class T>
using get_value_type_t = typename T::value_type;
using ValidAggMapType = std::unordered_map<
    std::string_view,
    std::shared_ptr<CmdGlobalOptions::BaseAggInfo>>;

template <typename RowType>
void aggregationHelper(
    RowType row,
    const std::optional<facebook::fboss::CmdGlobalOptions::AggregateOption>&
        parsedAggs,
    const ValidAggMapType& validAggMap,
    int rowNumber,
    double& aggAnswer) {
  apache::thrift::op::for_each_field_id<RowType>([&]<class Id>(Id) {
    auto field_ref = apache::thrift::op::get<Id>(row);
    auto field_name = apache::thrift::op::get_name_v<RowType, Id>;
    const auto& aggColumn = parsedAggs->columnName;
    const auto& aggOp = parsedAggs->aggOp;
    // this is the colum being aggregated
    if (field_name == aggColumn) {
      using FieldType = std::remove_cvref_t<decltype(*field_ref)>;
      if constexpr (std::is_fundamental<FieldType>::value) {
        auto it = validAggMap.find(aggColumn);
        // todo(surabih236): separate handling for string and numeric types
        // to aboid doing extra type conversions.
        const auto& value = std::to_string(*field_ref);
        if (it != validAggMap.end()) {
          if (rowNumber == 0) {
            aggAnswer = (it->second)->getInitValue(value, aggOp);
          } else {
            aggAnswer = (it->second)->performAggregate(value, aggAnswer, aggOp);
          }
        }
      } else if constexpr (std::is_same<FieldType, std::string>::value) {
        auto it = validAggMap.find(aggColumn);
        if (it != validAggMap.end()) {
          if (rowNumber == 0) {
            aggAnswer = (it->second)->getInitValue(*field_ref, aggOp);
          } else {
            aggAnswer =
                (it->second)->performAggregate(*field_ref, aggAnswer, aggOp);
          }
        }
      }
    }
  });
}

template <typename CmdTypeT>
double performAggregation(
    typename CmdTypeT::RetType model,
    const std::optional<facebook::fboss::CmdGlobalOptions::AggregateOption>&
        parsedAggs,
    const ValidAggMapType& validAggMap) {
  double aggAnswer = 0;
  if constexpr (
      apache::thrift::is_thrift_struct_v<typename CmdTypeT::RetType> &&
      CmdTypeT::Traits::ALLOW_AGGREGATION) {
    apache::thrift::op::for_each_field_id<
        typename CmdTypeT::RetType>([&]<class Id>(Id) {
      auto outer_field_ref = apache::thrift::op::get<Id>(model);
      if constexpr (apache::thrift::is_thrift_struct_v<folly::detected_or_t<
                        void,
                        get_value_type_t,
                        std::remove_cvref_t<decltype(*outer_field_ref)>>>) {
        using NestedType =
            get_value_type_t<std::remove_cvref_t<decltype(*outer_field_ref)>>;
        if constexpr (apache::thrift::is_thrift_struct_v<NestedType>) {
          auto innerEntryVector = *outer_field_ref;
          for (int rowNumber = 0; rowNumber < innerEntryVector.size();
               rowNumber++) {
            auto innerEntry = innerEntryVector[rowNumber];
            aggregationHelper(
                innerEntry, parsedAggs, validAggMap, rowNumber, aggAnswer);
          }
        }
      }
    });
  }
  return aggAnswer;
}
} // namespace facebook::fboss
