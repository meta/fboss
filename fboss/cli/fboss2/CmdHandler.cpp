/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/CmdHandler.h"
#include <thrift/lib/cpp2/op/Get.h>
#include "fboss/cli/fboss2/CmdArgsLists.h"
#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/CmdList.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_types.h"
#include "fboss/cli/fboss2/utils/AggregateOp.h"
#include "fboss/cli/fboss2/utils/AggregateUtils.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "thrift/lib/cpp/util/EnumUtils.h"
#include "thrift/lib/cpp2/protocol/Serializer.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/Demangle.h>
#include <folly/ScopeGuard.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <folly/stop_watch.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
template <typename T>
struct is_vector : std::false_type {};

template <typename T, typename Alloc>
struct is_vector<std::vector<T, Alloc>> : std::true_type {};

template <typename T>
inline constexpr bool is_vector_v = is_vector<T>::value;
} // namespace

template <typename CmdTypeT>
void printTabular(
    CmdTypeT& cmd,
    std::vector<std::shared_future<
        std::tuple<std::string, typename CmdTypeT::RetType, std::string>>>&
        results,
    std::ostream& out,
    std::ostream& err) {
  for (auto& result : results) {
    auto [host, data, errStr] = result.get();
    if (results.size() != 1) {
      out << host << "::" << std::endl << std::string(80, '=') << std::endl;
    }

    if (errStr.empty()) {
      cmd.printOutput(data);
    } else if constexpr (!CmdTypeT::Traits::IS_LOCAL_COMMAND) {
      err << errStr << std::endl << std::endl;
    }
  }
}

template <typename CmdTypeT>
void printJson(
    const CmdTypeT& /* cmd */,
    std::vector<std::shared_future<
        std::tuple<std::string, typename CmdTypeT::RetType, std::string>>>&
        results,
    std::ostream& out,
    std::ostream& err) {
  std::map<std::string, typename CmdTypeT::RetType> hostResults;
  for (auto& result : results) {
    auto [host, data, errStr] = result.get();
    if (errStr.empty()) {
      hostResults[host] = data;
    } else if constexpr (!CmdTypeT::Traits::IS_LOCAL_COMMAND) {
      err << host << "::" << std::endl << std::string(80, '=') << std::endl;
      err << errStr << std::endl << std::endl;
    }
  }

  if constexpr (std::is_same_v<typename CmdTypeT::RetType, folly::dynamic>) {
    // The thrift serializer cannot handle folly::dynamic (it throws
    // "expected dynamic type 'array', but had type 'object'"), so render the
    // per-host map with folly's own JSON writer.
    folly::dynamic hostObj = folly::dynamic::object;
    for (auto& [host, data] : hostResults) {
      hostObj[host] = std::move(data);
    }
    out << folly::toJson(hostObj) << std::endl;
  } else {
    out << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
               hostResults)
        << std::endl;
  }
}

template <typename CmdTypeT>
void printAggregate(
    const std::optional<facebook::fboss::CmdGlobalOptions::AggregateOption>&
        parsedAgg,
    std::vector<std::shared_future<
        std::tuple<std::string, typename CmdTypeT::RetType, std::string>>>&
        results,
    const facebook::fboss::ValidAggMapType& validAggMap) {
  if (!parsedAgg->acrossHosts) {
    for (auto& result : results) {
      auto [host, data, errStr] = result.get();
      if (errStr.empty()) {
        std::cout << host << " Aggregation result:: "
                  << facebook::fboss::performAggregation<CmdTypeT>(
                         data, parsedAgg, validAggMap)
                  << std::endl;
      } else if constexpr (!CmdTypeT::Traits::IS_LOCAL_COMMAND) {
        std::cerr << host << "::" << std::endl
                  << std::string(80, '=') << std::endl;
        std::cerr << errStr << std::endl << std::endl;
      }
    }
  } else {
    // double because aggregation results are doubles.
    std::vector<double> hostAggResults;
    for (auto& result : results) {
      auto [host, data, errStr] = result.get();
      if (errStr.empty()) {
        hostAggResults.push_back(
            facebook::fboss::performAggregation<CmdTypeT>(
                data, parsedAgg, validAggMap));
      } else if constexpr (!CmdTypeT::Traits::IS_LOCAL_COMMAND) {
        std::cerr << host << "::" << std::endl
                  << std::string(80, '=') << std::endl;
        std::cerr << errStr << std::endl << std::endl;
      }
    }

    double aggregateAcrossHosts;
    int rowNumber = 0;
    const auto& aggOp = parsedAgg->aggOp;
    const auto& aggColumn = parsedAgg->columnName;
    auto it = validAggMap.find(aggColumn);
    for (auto result : hostAggResults) {
      if (rowNumber == 0) {
        if (aggOp != facebook::fboss::AggregateOpEnum::COUNT) {
          aggregateAcrossHosts =
              (it->second)->getInitValue(std::to_string(result), aggOp);
        } else {
          // can't use init value from the countAgg() here, hence separate
          // handling
          aggregateAcrossHosts = result;
        }
        rowNumber += 1;
      } else {
        switch (aggOp) {
          case facebook::fboss::AggregateOpEnum::SUM:
            aggregateAcrossHosts = facebook::fboss::SumAgg<double>().accumulate(
                result, aggregateAcrossHosts);

            break;
          case facebook::fboss::AggregateOpEnum::MIN:
            aggregateAcrossHosts = facebook::fboss::MinAgg<double>().accumulate(
                result, aggregateAcrossHosts);
            break;
          case facebook::fboss::AggregateOpEnum::MAX:
            aggregateAcrossHosts = facebook::fboss::MaxAgg<double>().accumulate(
                result, aggregateAcrossHosts);
            break;
          // when aggregating counts across hosts, we need to sum them!
          case facebook::fboss::AggregateOpEnum::COUNT:
            aggregateAcrossHosts = facebook::fboss::SumAgg<double>().accumulate(
                result, aggregateAcrossHosts);
            break;
          case facebook::fboss::AggregateOpEnum::AVG:
            std::cerr << "Average aggregation not supported yet!" << std::endl;
            break;
        }
      }
    }
    std::cout << "Result of aggregating across all hosts: "
              << aggregateAcrossHosts << std::endl;
  }
}

namespace facebook::fboss {

template <typename CmdTypeT, typename CmdTypeTraits>
void CmdHandler<CmdTypeT, CmdTypeTraits>::runHelper() {
  // Note: The callback wrapper in CmdSubcommands.cpp ensures that only the
  // leaf command handler is invoked. It checks if get_subcommands() is empty.
  auto extraOptionsEC =
      CmdGlobalOptions::getInstance()->validateNonFilterOptions();
  if (extraOptionsEC != cli::CliOptionResult::EOK) {
    throw std::invalid_argument(
        folly::to<std::string>(
            "Error in filter parsing: ",
            apache::thrift::util::enumNameSafe(extraOptionsEC)));
  }

  /* If there are errors during filter parsing, we do not exit in the getFilters
  method of CmdGlobalOptions.cpp. Instead, we get the parsing error code and
  exit here. This is to avoid having living references during the
  destrowInstances time!
  */
  auto filterParsingEC = cli::CliOptionResult::EOK;
  auto parsedFilters =
      CmdGlobalOptions::getInstance()->getFilters(filterParsingEC);
  if (filterParsingEC != cli::CliOptionResult::EOK) {
    throw std::invalid_argument(
        folly::to<std::string>(
            "Error in filter parsing: ",
            apache::thrift::util::enumNameSafe(filterParsingEC)));
  }

  ValidFilterMapType validFilters = {};
  if (!parsedFilters.empty()) {
    validFilters = getValidFilters();
    const auto& errorCode =
        CmdGlobalOptions::getInstance()->isValid(validFilters, parsedFilters);
    if (!(errorCode == cli::CliOptionResult::EOK)) {
      throw std::invalid_argument(
          folly::to<std::string>(
              "Error in filter parsing: ",
              apache::thrift::util::enumNameSafe(errorCode)));
    }
  }

  auto aggParsingEC = cli::CliOptionResult::EOK;
  auto parsedAggregationInput =
      CmdGlobalOptions::getInstance()->parseAggregate(aggParsingEC);

  if (aggParsingEC != cli::CliOptionResult::EOK) {
    throw std::invalid_argument(
        folly::to<std::string>(
            "Error in aggregate parsing: ",
            apache::thrift::util::enumNameSafe(aggParsingEC)));
  }

  ValidAggMapType validAggs = {};
  if (parsedAggregationInput.has_value()) {
    validAggs = getValidAggs();
    const auto& errorCode = CmdGlobalOptions::getInstance()->isValidAggregate(
        validAggs, parsedAggregationInput);
    if (errorCode != cli::CliOptionResult::EOK) {
      throw std::invalid_argument(
          folly::to<std::string>(
              "Error in Aggregate validation: ",
              apache::thrift::util::enumNameSafe(errorCode)));
    }
  }

  auto hosts = getHosts();

  if (hosts.size() > 1 && !CmdTypeT::Traits::ALLOW_MULTI_HOST) {
    throw std::invalid_argument(
        folly::to<std::string>(
            "multi host operation is not supported for this command"));
  }

  std::vector<std::shared_future<std::tuple<std::string, RetType, std::string>>>
      futureList;

  // Limit concurrent host queries to prevent resource exhaustion. The value of
  // 20 was chosen empirically as a balance between parallelism and resource
  // usage - it's high enough to provide good throughput for typical multi-host
  // queries, but low enough to avoid overwhelming client resources. Consider
  // increasing this value if queries to many hosts are consistently slow,
  // or decreasing if resource exhaustion issues are observed.
  constexpr int kMaxParallelism = 20;
  const auto parallelism =
      std::min(static_cast<int>(hosts.size()), kMaxParallelism);

  folly::CPUThreadPoolExecutor executor(parallelism);

  for (const auto& host : hosts) {
    auto promise = std::make_shared<
        std::promise<std::tuple<std::string, RetType, std::string>>>();
    futureList.push_back(promise->get_future().share());

    executor.add([this, host, &parsedFilters, &validFilters, promise]() {
      try {
        promise->set_value(asyncHandler(host, parsedFilters, validFilters));
      } catch (...) {
        promise->set_exception(std::current_exception());
      }
    });
  }

  if (!parsedAggregationInput.has_value()) {
    if (CmdGlobalOptions::getInstance()->getFmt().isJson()) {
      printJson(impl(), futureList, std::cout, std::cerr);
    } else {
      printTabular(impl(), futureList, std::cout, std::cerr);
    }
  } else {
    printAggregate<CmdTypeT>(parsedAggregationInput, futureList, validAggs);
  }

  // Join after printing: the print functions call .get() on each future,
  // which blocks until that individual result is ready, enabling streaming
  // output (especially for tabular mode) instead of waiting for all tasks.
  executor.join();

  std::queue<std::pair<std::string, std::string>> executionFailures{};

  for (auto& fut : futureList) {
    auto [host, data, errStr] = fut.get();
    if (!errStr.empty()) {
      executionFailures.push({host, errStr});
    }
  }

  // Collect errors and display at end of execution, then throw if any failures
  std::string combinedErrors;
  while (!executionFailures.empty()) {
    auto [host, errStr] = executionFailures.front();
    executionFailures.pop();
    if (!combinedErrors.empty()) {
      combinedErrors += "; ";
    }
    if constexpr (CmdTypeT::Traits::IS_LOCAL_COMMAND) {
      combinedErrors += errStr;
    } else {
      XLOG(ERR) << host << " - Error in command execution: " << errStr;
      combinedErrors += fmt::format("{}: {}", host, errStr);
    }
  }
  if (!combinedErrors.empty()) {
    throw std::runtime_error(combinedErrors);
  }
}

template <typename CmdTypeT, typename CmdTypeTraits>
void CmdHandler<CmdTypeT, CmdTypeTraits>::run() {
  utils::setLogLevel(CmdGlobalOptions::getInstance()->getLogLevel());

  // setup a stop_watch for total time to run command
  folly::stop_watch<> watch;
  utils::CmdLogInfo cmdLogInfo;

  try {
    // The MACROS are executed in reversed order
    // In order to have SCOPE_EXIT be the last one to get executed,
    // it must come before SCOPE_FAIL and SCOPE_SUCCESS
    SCOPE_EXIT {
      cmdLogInfo.CmdName = folly::demangle(typeid(this)).toStdString();
      cmdLogInfo.Arguments = CmdArgsLists::getInstance()->getArgStr();
      cmdLogInfo.Duration = utils::getDurationStr(watch);
      cmdLogInfo.UserInfo = utils::getUserInfo();
      utils::logUsage(cmdLogInfo);
    };

    SCOPE_FAIL {
      cmdLogInfo.ExitStatus = "FAILURE";
    };

    SCOPE_SUCCESS {
      cmdLogInfo.ExitStatus = "SUCCESS";
    };

    runHelper();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    // Rethrow so the caller can handle appropriately (e.g., tests can catch and
    // check exit codes, CLI main() can exit with non-zero status)
    throw;
  }
}

/* Logic: We consider a thrift struct to be filterable only if it is of type
  list and has a single element in it that is another thrift struct.
  The idea is that the outer thrift struct (which corresponds to the RetType()
  in the cmd traits) should contain a list of the actual displayable thrift
  entry. For instance, the ShowPortModel struct has only one elelent that is a
  list of PortEntry struct. This makes it filterable.
  */
template <typename CmdTypeT, typename CmdTypeTraits>
bool CmdHandler<CmdTypeT, CmdTypeTraits>::isFilterable() {
  int numFields = 0;
  bool filterable = true;
  if constexpr (
      apache::thrift::is_thrift_struct_v<RetType> &&
      CmdTypeT::Traits::ALLOW_FILTERING == true) {
    apache::thrift::op::for_each_field_id<RetType>([&]<class Id>(Id) {
      auto field_ref = apache::thrift::op::get<Id>(RetType{});
      using FieldRefType = std::remove_cvref_t<decltype(field_ref)>;
      using ValueType = typename FieldRefType::value_type;

      if constexpr (!is_vector_v<ValueType>) {
        filterable = false;
      }
      numFields += 1;
    });
  }

  if (numFields != 1) {
    filterable = false;
  }
  return filterable;
}

/* Logic (similar to isFilterable): We consider a thrift struct to be
  amenable to aggregation only if it is of type list and has a single element
  in it that is another thrift struct.
  The idea is that the outer thrift struct (which corresponds to the RetType()
  in the cmd traits) should contain a list of the actual displayable thrift
  entry. For instance, the ShowPortModel struct has only one elelent that is a
  list of PortEntry struct. This makes it aggregatable.
  */
template <typename CmdTypeT, typename CmdTypeTraits>
bool CmdHandler<CmdTypeT, CmdTypeTraits>::isAggregatable() {
  int numFields = 0;
  bool aggregatable = true;
  if constexpr (
      apache::thrift::is_thrift_struct_v<RetType> &&
      CmdTypeT::Traits::ALLOW_AGGREGATION) {
    apache::thrift::op::for_each_field_id<RetType>([&]<class Id>(Id) {
      auto field_ref = apache::thrift::op::get<Id>(RetType{});
      using FieldRefType = std::remove_cvref_t<decltype(field_ref)>;
      using ValueType = typename FieldRefType::value_type;

      if constexpr (!is_vector_v<ValueType>) {
        aggregatable = false;
      }
      numFields += 1;
    });
  }

  if (numFields != 1) {
    aggregatable = false;
  }
  return aggregatable;
}

/* Logic: We first check if this thrift struct is filterable at all. If it is,
 we constuct a filter map that contains filterable fields. For now, only
 primitive fields will be added to the filter map and considered filterable.
 Also, here we are traversing the nested thrift struct using the reflection
 api. for that, we start with the RetType() object and get to the fields of
 the inner struct using reflection. This is a much cleaner approach than the
 one that redirects through each command's handler to get the inner struct.
 */
template <typename CmdTypeT, typename CmdTypeTraits>
const ValidFilterMapType
CmdHandler<CmdTypeT, CmdTypeTraits>::getValidFilters() {
  if (!CmdHandler<CmdTypeT, CmdTypeTraits>::isFilterable()) {
    return {};
  }
  ValidFilterMapType filterMap;

  if constexpr (
      apache::thrift::is_thrift_struct_v<RetType> &&
      CmdTypeT::Traits::ALLOW_FILTERING == true) {
    apache::thrift::op::for_each_field_id<RetType>([&filterMap,
                                                    this]<class OuterId>(
                                                       OuterId) {
      auto outer_field_ref = apache::thrift::op::get<OuterId>(RetType{});
      using OuterFieldRefType = std::remove_cvref_t<decltype(outer_field_ref)>;
      using OuterValueType = typename OuterFieldRefType::value_type;

      if constexpr (is_vector_v<OuterValueType>) {
        using NestedType = typename OuterValueType::value_type;
        if constexpr (apache::thrift::is_thrift_struct_v<NestedType>) {
          apache::thrift::op::for_each_field_id<
              NestedType>([&, this]<class InnerId>(InnerId) {
            auto inner_field_ref =
                apache::thrift::op::get<InnerId>(NestedType{});
            using InnerFieldRefType =
                std::remove_cvref_t<decltype(inner_field_ref)>;
            using InnerValueType = typename InnerFieldRefType::value_type;

            // Use the field name directly as map key - get_name_v returns a
            // folly::StringPiece pointing to static storage (string literal)
            const auto& field_name_view =
                apache::thrift::op::get_name_v<NestedType, InnerId>;
            // Create a string for TypeVerifier constructor
            std::string field_name_str(field_name_view);

            // we are only supporting filtering on primitive typed
            // keys for now. This will be expanded as we proceed.
            if constexpr (std::is_same_v<InnerValueType, std::string>) {
              filterMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::TypeVerifier<std::string>>(
                      field_name_str,
                      impl().getAcceptedFilterValues()[field_name_str]);
            } else if constexpr (std::is_same_v<InnerValueType, int16_t>) {
              filterMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::TypeVerifier<int16_t>>(
                      field_name_str);
            } else if constexpr (std::is_same_v<InnerValueType, int32_t>) {
              filterMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::TypeVerifier<int32_t>>(
                      field_name_str);
            } else if constexpr (std::is_same_v<InnerValueType, int64_t>) {
              filterMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::TypeVerifier<int64_t>>(
                      field_name_str);
            } else if constexpr (std::is_same_v<InnerValueType, std::byte>) {
              filterMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::TypeVerifier<std::byte>>(
                      field_name_str);
            }
          });
        }
      }
    });
  }
  return filterMap;
}

/* Logic: We first check if this thrift struct is aggregatable at all. If it
 is, we constuct an aggregate map that contains aggregatable fields. Only
 numeric and string fields are aggregatable for now. The map maps to the
 AggInfo object that contains information about the operations that are
 permitted for this column. This information is subsequenlty used to perform
 aggregate validation. Also, here we are traversing the nested thrift struct
 using the reflection api. for that, we start with the RetType() object and
 get to the fields of the inner struct using reflection.
 */
template <typename CmdTypeT, typename CmdTypeTraits>
const ValidAggMapType CmdHandler<CmdTypeT, CmdTypeTraits>::getValidAggs() {
  if (!CmdHandler<CmdTypeT, CmdTypeTraits>::isAggregatable()) {
    return {};
  }
  ValidAggMapType aggMap;

  if constexpr (
      apache::thrift::is_thrift_struct_v<RetType> &&
      CmdTypeT::Traits::ALLOW_AGGREGATION) {
    apache::thrift::op::for_each_field_id<RetType>([&aggMap]<class OuterId>(
                                                       OuterId) {
      auto outer_field_ref = apache::thrift::op::get<OuterId>(RetType{});
      using OuterFieldRefType = std::remove_cvref_t<decltype(outer_field_ref)>;
      using OuterValueType = typename OuterFieldRefType::value_type;

      if constexpr (is_vector_v<OuterValueType>) {
        using NestedType = typename OuterValueType::value_type;
        if constexpr (apache::thrift::is_thrift_struct_v<NestedType>) {
          apache::thrift::op::for_each_field_id<NestedType>([&]<class InnerId>(
                                                                InnerId) {
            auto inner_field_ref =
                apache::thrift::op::get<InnerId>(NestedType{});
            using InnerFieldRefType =
                std::remove_cvref_t<decltype(inner_field_ref)>;
            using InnerValueType = typename InnerFieldRefType::value_type;

            // Use the field name directly as map key - get_name_v returns a
            // folly::StringPiece pointing to static storage (string literal)
            const auto& field_name_view =
                apache::thrift::op::get_name_v<NestedType, InnerId>;
            // Create a string for AggInfo constructor
            std::string field_name_str(field_name_view);

            // Aggregation only supported on numerical keys
            if constexpr (std::is_same_v<InnerValueType, int16_t>) {
              aggMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::AggInfo<int16_t>>(
                      field_name_str,
                      std::vector<AggregateOpEnum>{
                          AggregateOpEnum::SUM,
                          AggregateOpEnum::AVG,
                          AggregateOpEnum::MIN,
                          AggregateOpEnum::MAX,
                          AggregateOpEnum::COUNT});
            } else if constexpr (std::is_same_v<InnerValueType, int32_t>) {
              aggMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::AggInfo<int32_t>>(
                      field_name_str,
                      std::vector<AggregateOpEnum>{
                          AggregateOpEnum::SUM,
                          AggregateOpEnum::AVG,
                          AggregateOpEnum::MIN,
                          AggregateOpEnum::MAX,
                          AggregateOpEnum::COUNT});
            } else if constexpr (std::is_same_v<InnerValueType, int64_t>) {
              aggMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::AggInfo<int64_t>>(
                      field_name_str,
                      std::vector<AggregateOpEnum>{
                          AggregateOpEnum::SUM,
                          AggregateOpEnum::AVG,
                          AggregateOpEnum::MIN,
                          AggregateOpEnum::MAX,
                          AggregateOpEnum::COUNT});
            } else if constexpr (std::is_same_v<InnerValueType, float>) {
              aggMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::AggInfo<float>>(
                      field_name_str,
                      std::vector<AggregateOpEnum>{
                          AggregateOpEnum::SUM,
                          AggregateOpEnum::AVG,
                          AggregateOpEnum::MIN,
                          AggregateOpEnum::MAX,
                          AggregateOpEnum::COUNT});
            } else if constexpr (std::is_same_v<InnerValueType, double>) {
              aggMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::AggInfo<double>>(
                      field_name_str,
                      std::vector<AggregateOpEnum>{
                          AggregateOpEnum::SUM,
                          AggregateOpEnum::AVG,
                          AggregateOpEnum::MIN,
                          AggregateOpEnum::MAX,
                          AggregateOpEnum::COUNT});
            } else if constexpr (std::is_same_v<InnerValueType, std::string>) {
              aggMap[field_name_view] =
                  std::make_shared<CmdGlobalOptions::AggInfo<std::string>>(
                      field_name_str,
                      std::vector<AggregateOpEnum>{AggregateOpEnum::COUNT});
            }
          });
        }
      }
    });
  }
  return aggMap;
}

} // namespace facebook::fboss
