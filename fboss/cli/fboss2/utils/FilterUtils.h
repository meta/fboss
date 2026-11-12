// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <thrift/lib/cpp2/op/Get.h>
#include "fboss/cli/fboss2/CmdGlobalOptions.h"

namespace facebook::fboss {

template <class T>
using get_value_type_t = typename T::value_type;
using ValidFilterMapType = std::unordered_map<
    std::string_view,
    std::shared_ptr<CmdGlobalOptions::BaseTypeVerifier>>;

// Returns true if the row satisfies a given filter term (<key op value>)
// Currently filtering is only supported for fundamental types and strings.
// Hence the static checks for those have been done

/* Logic: we perform thrift reflection on each filed of the row and check if a
filter needs to be applied on this field. If so, we compare the value of the
filed in the result and in the predicate that was passed as filter input.
The static checks have been put into place for string fields and primitive
fields because filtering is only supported on those.
*/
template <typename RowType>
bool satisfiesFilterTerm(
    const RowType& row,
    const CmdGlobalOptions::FilterTerm& filterTerm,
    const ValidFilterMapType& validFilterMap) {
  bool satisfies = false;
  apache::thrift::op::for_each_field_id<RowType>([&]<class Id>(Id) {
    auto field_name = apache::thrift::op::get_name_v<RowType, Id>;
    auto field_ref = apache::thrift::op::get<Id>(row);

    const auto& filterKey = std::get<0>(filterTerm);
    const auto& filterOp = std::get<1>(filterTerm);
    const auto& predicateValue = std::get<2>(filterTerm);

    if (field_name == filterKey) {
      using FieldType = std::remove_cvref_t<decltype(*field_ref)>;
      // Perform the comparison only if string or fundamental type
      if constexpr (std::is_fundamental<FieldType>::value) {
        auto it = validFilterMap.find(filterKey);
        const auto& value = std::to_string(*field_ref);
        if (it != validFilterMap.end()) {
          satisfies =
              (it->second)->compareValue(value, predicateValue, filterOp);
        }
      } else if constexpr (std::is_same<FieldType, std::string>::value) {
        auto it = validFilterMap.find(filterKey);
        if (it != validFilterMap.end()) {
          satisfies =
              (it->second)->compareValue(*field_ref, predicateValue, filterOp);
        }
      }
    }
  });
  return satisfies;
}

// returns true if all predicates in the given intersection list are satisfied
template <typename RowType>
bool satisfiesAllPredicates(
    const RowType& row,
    const CmdGlobalOptions::IntersectionList& intersectList,
    const ValidFilterMapType& validFilterMap) {
  for (const auto& filterTerm : intersectList) {
    if (!satisfiesFilterTerm(row, filterTerm, validFilterMap)) {
      return false;
    }
  }
  return true;
}

// returns true if atleast one intersection list is satisfied in the given union
// list
template <typename RowType>
bool satisfiesFilterCondition(
    const RowType& row,
    const CmdGlobalOptions::UnionList& unionList,
    const ValidFilterMapType& validFilterMap) {
  for (const auto& intersectList : unionList) {
    if (satisfiesAllPredicates(row, intersectList, validFilterMap)) {
      return true;
    }
  }
  return false;
}

/* logic: We use the result model obtained from queryClient and perform thrift
reflection on the model fields to obtain the field_refs (which correspond to the
result values). Here, the model would be a struct containing a single list of
structs(this check is statically done). Once we obtain the inner list, we check
if each row in the list satisifes the input filter condition. The rows that do
not satisfy are deleted. The resultant model (otatined after filtering out bad
rows) is returned at the end.
*/
template <typename CmdTypeT>
typename CmdTypeT::RetType filterOutput(
    typename CmdTypeT::RetType model,
    const facebook::fboss::CmdGlobalOptions::UnionList& parsedFilters,
    const ValidFilterMapType& validFilterMap) {
  if constexpr (
      apache::thrift::is_thrift_struct_v<typename CmdTypeT::RetType> &&
      CmdTypeT::Traits::ALLOW_FILTERING == true) {
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
          auto removed = std::remove_if(
              innerEntryVector.begin(),
              innerEntryVector.end(),
              [&](auto innerEntry) {
                return !satisfiesFilterCondition(
                    innerEntry, parsedFilters, validFilterMap);
              });
          innerEntryVector.erase(removed, innerEntryVector.end());
          *outer_field_ref = innerEntryVector;
        }
      }
    });
  }
  return model;
}
} // namespace facebook::fboss
