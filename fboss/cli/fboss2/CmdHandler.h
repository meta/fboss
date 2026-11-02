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

#include "fboss/cli/fboss2/CmdArgsLists.h"
#include "fboss/cli/fboss2/utils/AggregateUtils.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/FilterUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#include <CLI/App.hpp>

#include <folly/logging/xlog.h>
#include <thrift/lib/thrift/gen-cpp2/metadata_types.h>

namespace facebook::fboss {

template <typename Cmd>
class resolve_arg_types {
  template <typename Front, typename Tuple>
  struct tuple_push_back {};

  // Unwrap tuple and create a new type with our new type at the end
  template <typename Front, typename... Ts>
  struct tuple_push_back<Front, std::tuple<Ts...>> {
    using type = std::tuple<Ts..., Front>;
  };

  // recurse up the linked list of classes
  using parent = resolve_arg_types<typename Cmd::Traits::ParentCmd>;
  using appended_type = typename tuple_push_back<
      typename Cmd::Traits::ObjectArgType,
      typename parent::filtered_type>::type;

  // For backward compatibility with old format
  template <typename T>
  using is_monostate = std::is_same<T, std::monostate>;
  // Implementation for newly added argument type class
  template <typename T>
  using is_nonetype = std::is_same<T, utils::NoneArgType>;

 public:
  // filter out std::monostate and utils::NoneArgType
  using filtered_type = std::conditional_t<
      !is_monostate<typename Cmd::Traits::ObjectArgType>::value &&
          !is_nonetype<typename Cmd::Traits::ObjectArgType>::value,
      appended_type,
      typename parent::filtered_type>;
  // Used at run time to determine which index args we need and which to get rid
  // of (i.e. which positions are std::monostate)
  using unfiltered_type = typename tuple_push_back<
      typename Cmd::Traits::ObjectArgType,
      typename parent::unfiltered_type>::type;
};

// Base case for recursion (when ParentCmd is void)
template <>
struct resolve_arg_types<void> {
  using filtered_type = std::tuple<>;
  using unfiltered_type = std::tuple<>;
};

enum class CliReadWriteMode {
  CLI_MODE_WRITE,
  CLI_MODE_READ,
};

struct BaseCommandTraits {
  // Only for top level commands, nested subcommands will override this
  using ParentCmd = void;
  using ObjectArgType = std::monostate;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  static constexpr bool ALLOW_FILTERING = false;
  static constexpr bool ALLOW_AGGREGATION = false;
  static constexpr bool IS_LOCAL_COMMAND = false;
  static constexpr CliReadWriteMode CLI_READ_WRITE_MODE =
      CliReadWriteMode::CLI_MODE_WRITE;
  static constexpr bool ALLOW_MULTI_HOST = false;
  std::vector<utils::LocalOption> LocalOptions = {};

  // Default: no positional argument. Config commands override this.
  static void addCliArg(CLI::App& /*cmd*/, std::vector<std::string>& /*args*/) {
  }
};

struct ReadCommandTraits : public BaseCommandTraits {
  static constexpr CliReadWriteMode CLI_READ_WRITE_MODE =
      CliReadWriteMode::CLI_MODE_READ;
  static constexpr bool ALLOW_MULTI_HOST = true;
};

struct WriteCommandTraits : public BaseCommandTraits {
  static constexpr CliReadWriteMode CLI_READ_WRITE_MODE =
      CliReadWriteMode::CLI_MODE_WRITE;
};

// Tag a command's Traits with this base to grandfather it out of the CLI
// reference-wiki documentation requirement. Remove the tag once the command
// defines both hooks; enforcement then requires them.
struct CliDocsExempt {};

namespace detail {
template <typename T, typename = void>
struct HasTraitsCliDescription : std::false_type {};
template <typename T>
struct HasTraitsCliDescription<
    T,
    std::void_t<decltype(T::Traits::description())>> : std::true_type {};

template <typename T, typename = void>
struct HasClassCliDescription : std::false_type {};
template <typename T>
struct HasClassCliDescription<T, std::void_t<decltype(T::description())>>
    : std::true_type {};

template <typename T, typename = void>
struct HasCliSampleModel : std::false_type {};
template <typename T>
struct HasCliSampleModel<T, std::void_t<decltype(T::sampleModel())>>
    : std::true_type {};
} // namespace detail

// A command is documented for the CLI reference wiki when it exposes both a
// description() (on its Traits, or on the command class for the shared-Traits
// case) and a sampleModel().
template <typename Cmd>
inline constexpr bool kHasCliDocs =
    (detail::HasTraitsCliDescription<Cmd>::value ||
     detail::HasClassCliDescription<Cmd>::value) &&
    detail::HasCliSampleModel<Cmd>::value;

// Enforced at the commandHandler<T>() bind point (forward-declared in
// CmdList.h): every read ("show") command must carry the CLI reference-wiki
// hooks unless explicitly grandfathered with CliDocsExempt. No-op for write
// commands.
template <typename T>
void assertReadCommandDocumented() {
  if constexpr (
      T::Traits::CLI_READ_WRITE_MODE == CliReadWriteMode::CLI_MODE_READ) {
    static_assert(
        kHasCliDocs<T> || std::is_base_of_v<CliDocsExempt, typename T::Traits>,
        "fboss2 'show' command is missing CLI reference-wiki hooks: add a "
        "description() to its Traits and a static sampleModel() to the command "
        "class, or tag its Traits with CliDocsExempt to defer.");
  }
}

template <typename CmdTypeT, typename CmdTypeTraits>
class CmdHandler {
  static_assert(
      std::is_base_of_v<BaseCommandTraits, CmdTypeTraits>,
      "CmdTypeTraits needs to subclass BaseCommandTraits");

 public:
  using Traits = CmdTypeTraits;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      CmdTypeTraits::ObjectArgTypeId;
  using ObjectArgType = typename CmdTypeTraits::ObjectArgType;
  using RetType = typename CmdTypeTraits::RetType;
  using ThriftPrimitiveType = apache::thrift::metadata::ThriftPrimitiveType;

  void run();
  void runHelper();

  bool isFilterable();
  bool isAggregatable();

  std::unordered_map<std::string, std::vector<std::string>>
  getAcceptedFilterValues() {
    return {};
  }

  const ValidFilterMapType getValidFilters();
  const ValidAggMapType getValidAggs();

 protected:
  CmdTypeT& impl() {
    return static_cast<CmdTypeT&>(*this);
  }
  const CmdTypeT& impl() const {
    return static_cast<const CmdTypeT&>(*this);
  }
  std::vector<std::string> getHosts() {
    if (!CmdGlobalOptions::getInstance()->getHosts().empty()) {
      return CmdGlobalOptions::getInstance()->getHosts();
    }
    if (!CmdGlobalOptions::getInstance()->getSmc().empty()) {
      return utils::getHostsInSmcTier(
          CmdGlobalOptions::getInstance()->getSmc());
    }
    if (!CmdGlobalOptions::getInstance()->getFile().empty()) {
      return utils::getHostsFromFile(
          CmdGlobalOptions::getInstance()->getFile());
    }
    // if host is not specified, default to localhost
    return {"localhost"};
  }

 private:
  RetType queryClientHelper(const HostInfo& hostInfo) {
    using ArgTypes = resolve_arg_types<CmdTypeT>;
    auto tupleArgs = CmdArgsLists::getInstance()
                         ->getTypedArgs<
                             typename ArgTypes::unfiltered_type,
                             typename ArgTypes::filtered_type>();
    return std::apply(
        [&](auto const&... t) { return impl().queryClient(hostInfo, t...); },
        tupleArgs);
  }

  std::tuple<std::string, RetType, std::string> asyncHandler(
      const std::string& host,
      const CmdGlobalOptions::UnionList& parsedFilters,
      const ValidFilterMapType& validFilterMap) {
    auto hostInfo = HostInfo(host);
    XLOG(DBG2) << "host: " << host << " ip: " << hostInfo.getIpStr();

    std::string errStr;
    RetType result;
    try {
      result = queryClientHelper(hostInfo);
    } catch (std::invalid_argument const& err) {
      errStr = folly::to<std::string>("Invalid argument: ", err.what());
    } catch (std::exception const& err) {
      if constexpr (CmdTypeTraits::IS_LOCAL_COMMAND) {
        errStr = err.what();
      } else {
        errStr =
            folly::to<std::string>("Thrift call failed: '", err.what(), "'");
      }
    }
    if (!parsedFilters.empty()) {
      result = filterOutput<CmdTypeT>(result, parsedFilters, validFilterMap);
    }

    return std::make_tuple(host, result, errStr);
  }
};

} // namespace facebook::fboss
