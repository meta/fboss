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

#include <CLI/CLI.hpp>
#include "fboss/cli/fboss2/gen-cpp2/cli_types.h"
#include "fboss/cli/fboss2/options/OutputFormat.h"
#include "fboss/cli/fboss2/options/SSLPolicy.h"
#include "fboss/cli/fboss2/utils/AggregateOp.h"
#include "fboss/cli/fboss2/utils/FilterOp.h"
#include "folly/String.h"

namespace facebook::fboss {
class CmdGlobalOptions {
 public:
  CmdGlobalOptions() = default;
  ~CmdGlobalOptions() = default;
  CmdGlobalOptions(const CmdGlobalOptions& other) = delete;
  CmdGlobalOptions& operator=(const CmdGlobalOptions& other) = delete;

  struct AggregateOption {
    AggregateOpEnum aggOp;
    std::string columnName;
    bool acrossHosts = false;
  };

  // using pointer here to avoid the object slicing problem
  using FilterTerm =
      std::tuple<std::string, std::shared_ptr<FilterOp>, std::string>;
  using IntersectionList = std::vector<FilterTerm>;
  using UnionList = std::vector<IntersectionList>;

  // Static function for getting the CmdGlobalOptions folly::Singleton
  static std::shared_ptr<CmdGlobalOptions> getInstance();

  void init(CLI::App& app);

  class BaseTypeVerifier {
   public:
    virtual ~BaseTypeVerifier() {}
    virtual cli::CliOptionResult verify(
        std::string& value,
        std::ostream& out) = 0;
    virtual bool compareValue(
        const std::string& result,
        const std::string& predicate,
        std::shared_ptr<FilterOp> filterOp) = 0;
    virtual std::vector<std::string> getAcceptableValues() = 0;
  };

  class BaseAggInfo {
   public:
    virtual ~BaseAggInfo() {}

    virtual std::vector<AggregateOpEnum> getAcceptableOps() = 0;
    virtual double performAggregate(
        const std::string& newValue,
        double& accumulatedValue,
        AggregateOpEnum aggOp) = 0;

    virtual double getInitValue(
        const std::string& newValue,
        AggregateOpEnum aggOp) = 0;
  };

  template <typename ExpectedType>
  class AggInfo : public BaseAggInfo {
    std::string aggColumn;
    std::vector<AggregateOpEnum> acceptedAggOps;

   public:
    AggInfo(
        const std::string& aggColumn,
        const std::vector<AggregateOpEnum>& acceptableOps)
        : aggColumn(aggColumn) {
      for (const auto& op : acceptableOps) {
        acceptedAggOps.push_back(op);
      }
    }

    double getInitValue(const std::string& newValue, AggregateOpEnum aggOp)
        override {
      switch (aggOp) {
        case AggregateOpEnum::COUNT:
          return 1;
          break;
        default:
          return folly::tryTo<double>(newValue).value();
      }
    }

    /* Returning double here because the aggregation only concerns with numeric
    types for now. If we extend it later to something like a string aggregate,
    we might want to change this to return a string. But for now, it's not clear
    if that is a real use case and so we have optimized for performance and
    sticked with double instead of string.

    TODO(surabhi236:) Moving forward, we will make some changes to this design
    and do separate handling for strings and numeric types. All numeric types
    will be converted to double and no conversions to or from ExpectedType will
    happen. Similarly, all string computations will happen separately, thus
    avoiding the need for conversions to and from ExpectedType. For this, we
    will also make changes to the AggregateOp's accumulate to handle these cases
    separately!
    */
    double performAggregate(
        const std::string& newValue,
        double& accumulatedValue,
        AggregateOpEnum aggOp) override {
      auto convertedNewVal = folly::tryTo<ExpectedType>(newValue);
      ExpectedType convertedAccValue;
      if constexpr (!std::is_same<ExpectedType, std::string>::value) {
        convertedAccValue =
            folly::tryTo<ExpectedType>(accumulatedValue).value();
      } else {
        convertedAccValue = std::to_string(accumulatedValue);
      }
      if (convertedNewVal.hasError()) {
        std::cerr << "Fatal: error in converting value to the correct type!"
                  << std::endl;
        exit(1);
      }

      double newAccVal;
      switch (aggOp) {
        case AggregateOpEnum::SUM:
          newAccVal = SumAgg<ExpectedType>().accumulate(
              convertedNewVal.value(), convertedAccValue);
          break;
        case AggregateOpEnum::MIN:
          newAccVal = MinAgg<ExpectedType>().accumulate(
              convertedNewVal.value(), convertedAccValue);
          break;
        case AggregateOpEnum::MAX:
          newAccVal = MaxAgg<ExpectedType>().accumulate(
              convertedNewVal.value(), convertedAccValue);
          break;
        case AggregateOpEnum::COUNT:
          newAccVal = CountAgg<ExpectedType>().accumulate(
              convertedNewVal.value(), accumulatedValue);
          break;
        case AggregateOpEnum::AVG:
          newAccVal = AvgAgg<ExpectedType>().accumulate(
              convertedNewVal.value(), convertedAccValue);
          break;
      }
      return newAccVal;
    }

    std::vector<AggregateOpEnum> getAcceptableOps() override {
      return acceptedAggOps;
    }
  };

  /* This is being done becase of the following reasons:
    1) switch statement (used in getFilterOp) in c++ doesn't work with strings.
    Hence, we need to find the hash of the string.
    2) The built-in hash function does not return a constexpr which is needed
    here. Hence, we defined a new hash function as below!
  */
  static constexpr unsigned int hash(const char* s, int off = 0) {
    return !s[off] ? 5381 : (hash(s, off + 1) * 33) ^ s[off];
  }

  std::shared_ptr<FilterOp> getFilterOp(std::string parsedOp) const {
    switch (hash(parsedOp.c_str())) {
      case hash("=="):
        return std::make_shared<FilterOpEq>();
      case hash("<"):
        return std::make_shared<FilterOpLt>();
      case hash("<="):
        return std::make_shared<FilterOpLte>();
      case hash(">"):
        return std::make_shared<FilterOpGt>();
      case hash(">="):
        return std::make_shared<FilterOpGte>();
      case hash("!="):
        return std::make_shared<FilterOpNeq>();
      case hash("=^"):
        return std::make_shared<FilterOpStartsWith>();
      case hash("=~"):
        return std::make_shared<FilterOpRegex>();
      default:
        throw std::invalid_argument("Invalid filter argument passed");
    }
  }

  AggregateOpEnum getAggregateOp(std::string parsedOp) const {
    switch (hash(parsedOp.c_str())) {
      case hash("SUM"):
        return AggregateOpEnum::SUM;
      case hash("MIN"):
        return AggregateOpEnum::MIN;
      case hash("MAX"):
        return AggregateOpEnum::MAX;
      case hash("AVG"):
        return AggregateOpEnum::AVG;
      case hash("COUNT"):
        return AggregateOpEnum::COUNT;
      default:
        throw std::invalid_argument("Invalid aggregate operation passed");
    }
  }

  template <typename ExpectedType>
  class TypeVerifier : public BaseTypeVerifier {
    std::vector<std::string> acceptedFilterValues;
    std::string key;

   public:
    TypeVerifier(
        const std::string& filterKey,
        const std::vector<std::string>& acceptedValues)
        : key(filterKey) {
      for (const auto& value : acceptedValues) {
        acceptedFilterValues.push_back(value);
      }
    }

    explicit TypeVerifier(const std::string& filterKey) : key(filterKey) {}

    cli::CliOptionResult verify(
        std::string& value,
        std::ostream& out = std::cerr) override {
      auto converted = folly::tryTo<ExpectedType>(value);
      if (converted.hasError()) {
        out << "invalid filter value data type passsed for key " << key
            << std::endl;
        return cli::CliOptionResult::TYPE_ERROR;
      }
      if (!acceptedFilterValues.empty()) {
        if (std::find(
                acceptedFilterValues.begin(),
                acceptedFilterValues.end(),
                value) == acceptedFilterValues.end()) {
          out << "invalid filter value for key " << key << std::endl
              << "accepted values are: { ";
          for (auto& acceptedVal : acceptedFilterValues) {
            out << acceptedVal << " ";
          }
          out << "}\n";
          return cli::CliOptionResult::VALUE_ERROR;
        }
      }
      return cli::CliOptionResult::EOK;
    }

    bool compareValue(
        const std::string& resultValue,
        const std::string& predicateValue,
        std::shared_ptr<FilterOp> filterOp) override {
      // these should not fail because filter input validation has already
      // been done before coming here
      auto convertedResult = folly::tryTo<ExpectedType>(resultValue);
      auto convertedPrediate = folly::tryTo<ExpectedType>(predicateValue);

      if (convertedResult.hasError() || convertedPrediate.hasError()) {
        std::cerr << "Fatal: this shouldn't happen!" << std::endl;
        exit(1);
      }
      return filterOp->compare(
          convertedResult.value(), convertedPrediate.value());
    }

    std::vector<std::string> getAcceptableValues() override {
      return acceptedFilterValues;
    }
  };

  cli::CliOptionResult isValid(
      const std::unordered_map<
          std::string_view,
          std::shared_ptr<BaseTypeVerifier>>& validFilters,
      UnionList& filters,
      std::ostream& out = std::cerr) const {
    for (const auto& intersectList : filters) {
      for (const auto& filtTerm : intersectList) {
        auto key = std::get<0>(filtTerm);
        auto val = std::get<2>(filtTerm);
        // invalid filter key
        auto it = validFilters.find(key);
        if (it == validFilters.end()) {
          out << "Invalid filter key passsed " << key << std::endl;
          out << "Filterable fields: { ";
          for (const auto& field : validFilters) {
            out << field.first << " ";
          }
          out << "}" << std::endl;
          return cli::CliOptionResult::KEY_ERROR;
        }
        auto typeVerifyEC = (it->second)->verify(val, out);
        if (typeVerifyEC != cli::CliOptionResult::EOK) {
          return typeVerifyEC;
        }
        // Note that the operator validation is done while parsing
        // hence, we don't perform operator validation here.
      }
    }
    return cli::CliOptionResult::EOK;
  }

  cli::CliOptionResult isValidAggregate(
      const std::unordered_map<std::string_view, std::shared_ptr<BaseAggInfo>>&
          validAggs,
      const std::optional<AggregateOption>& parsedAggs,
      std::ostream& out = std::cerr) {
    const auto& op = parsedAggs->aggOp;
    const auto& column = parsedAggs->columnName;
    auto it = validAggs.find(column);

    if (it == validAggs.end()) {
      out << "Invalid aggregate field name passed " << column << std::endl;
      out << "Aggregatable fields are: { ";
      for (const auto& field : validAggs) {
        out << field.first << " ";
      }
      out << "}" << std::endl;
      return cli::CliOptionResult::KEY_ERROR;
    }

    const auto& baseAggInfo = it->second;
    const auto& validOps = baseAggInfo->getAcceptableOps();

    if (std::find(validOps.begin(), validOps.end(), op) == validOps.end()) {
      out << "Invalid aggregate operator passed for column " << column
          << std::endl;
      return cli::CliOptionResult::OP_ERROR;
    }
    return cli::CliOptionResult::EOK;
  }

  cli::CliOptionResult validateNonFilterOptions() {
    bool hostsSet = !getHosts().empty();
    bool smcSet = !getSmc().empty();
    bool fileSet = !getFile().empty();

    if ((hostsSet && (smcSet || fileSet)) ||
        (smcSet && (hostsSet || fileSet)) ||
        (fileSet && (hostsSet || smcSet))) {
      std::cerr << "only one of host(s), smc or file can be set\n";
      return cli::CliOptionResult::EXTRA_OPTIONS;
    }
    return cli::CliOptionResult::EOK;
  }

  std::vector<std::string> getHosts() const {
    return hosts_;
  }

  std::string getSmc() const {
    return smc_;
  }

  std::string getFile() const {
    return file_;
  }

  std::string getLogLevel() const {
    return logLevel_;
  }

  const SSLPolicy& getSslPolicy() const {
    return sslPolicy_;
  }

  const OutputFormat& getFmt() const {
    return fmt_;
  }

  bool isDetailed() const {
    return detail_;
  }

  bool tag2name() const {
    return tag2name_;
  }

  bool isForce() const {
    return force_;
  }

  std::string getLogUsage() const {
    return logUsage_;
  }

  int getAgentThriftPort() const {
    return agentThriftPort_;
  }

  int getHwAgentThriftPort(int switchIndex) const {
    CHECK_LT(switchIndex, hwAgentThriftPort_.size());
    return hwAgentThriftPort_[switchIndex];
  }

  int getBgpStreamThriftPort() const {
    return bgpStreamThriftPort_;
  }

  int getQsfpThriftPort() const {
    return qsfpThriftPort_;
  }

  int getLedThriftPort() const {
    return ledThriftPort_;
  }

  int getBgpThriftPort() const {
    return bgpThriftPort_;
  }

  int getSmcThriftPort() const {
    return smcThriftPort_;
  }

  int getFsdbThriftPort() const {
    return fsdbThriftPort_;
  }

  int getLocalDrainerThriftPort() const {
    return localDrainerThriftPort_;
  }

  int getOpenrThriftPort() const {
    return openrThriftPort_;
  }

  int getMkaThriftPort() const {
    return mkaThriftPort_;
  }

  int getCoopThriftPort() const {
    return coopThriftPort_;
  }

  int getRackmonThriftPort() const {
    return rackmonThriftPort_;
  }

  int getSensorServiceThriftPort() const {
    return sensorServiceThriftPort_;
  }

  int getFanServiceThriftPort() const {
    return fanServiceThriftPort_;
  }

  int getTeAgentThriftPort() const {
    return teAgentThriftPort_;
  }

  int getTeSrv6AgentThriftPort() const {
    return teSrv6AgentThriftPort_;
  }

  int getDataCorralServiceThriftPort() const {
    return dataCorralServiceThriftPort_;
  }

  int getBmcHttpPort() const {
    return bmcHttpPort_;
  }

  int getVipInjectorPort() const {
    return vipInjectorThriftPort_;
  }

  std::string getColor() const {
    return color_;
  }

  // Setters for testing purposes
  void setSslPolicy(SSLPolicy& sslPolicy) {
    sslPolicy_ = sslPolicy;
  }

  void setAgentThriftPort(int port) {
    agentThriftPort_ = port;
  }

  void setHwAgentThriftPort(int switchIndex, int port) {
    if (switchIndex >= hwAgentThriftPort_.size()) {
      hwAgentThriftPort_.resize(switchIndex + 1);
    }
    hwAgentThriftPort_[switchIndex] = port;
  }

  void setQsfpThriftPort(int port) {
    qsfpThriftPort_ = port;
  }

  void setBgpThriftPort(int port) {
    bgpThriftPort_ = port;
  }

  void setSmcThriftPort(int port) {
    smcThriftPort_ = port;
  }

  void setBgpStreamThriftPort(int port) {
    bgpStreamThriftPort_ = port;
  }

  void setOpenrThriftPort(int port) {
    openrThriftPort_ = port;
  }

  void setCoopThriftPort(int port) {
    coopThriftPort_ = port;
  }

  void setVipThriftPort(int port) {
    vipInjectorThriftPort_ = port;
  }

  void setFsdbThriftPort(int port) {
    fsdbThriftPort_ = port;
  }

  void setTeAgentThriftPort(int port) {
    teAgentThriftPort_ = port;
  }

  void setTeSrv6AgentThriftPort(int port) {
    teSrv6AgentThriftPort_ = port;
  }

  void setFilterInput(std::string& filter) {
    filter_ = filter;
  }

  void setAggregateInput(std::string& aggregate) {
    aggregate_ = aggregate;
  }

  void aggregateAcrossDevices(bool acrossDevices) {
    aggregateAcrossDevices_ = acrossDevices;
  }

  UnionList getFilters(cli::CliOptionResult& filterParsingEC) const;
  std::optional<AggregateOption> parseAggregate(
      cli::CliOptionResult& aggregateParsingEC) const;

 private:
  void initAdditional(CLI::App& app);

  std::vector<std::string> hosts_;
  std::string smc_;
  std::string file_;
  std::string logLevel_{"DBG0"};
  SSLPolicy sslPolicy_{"plaintext"};
  OutputFormat fmt_;
  bool detail_{false};
  bool tag2name_{false};
  bool force_{false};
  std::string logUsage_{"scuba"};
  int fsdbThriftPort_{5908};
  int agentThriftPort_{5909};
  std::vector<int> hwAgentThriftPort_{5931, 5932};
  int qsfpThriftPort_{5910};
  int ledThriftPort_{5930};
  int bgpThriftPort_{6909};
  int smcThriftPort_{1421};
  int bgpStreamThriftPort_{6910};
  int openrThriftPort_{2018};
  int coopThriftPort_{6969};
  int mkaThriftPort_{5920};
  int bmcHttpPort_{8443};
  int rackmonThriftPort_{5973};
  int sensorServiceThriftPort_{5970};
  int fanServiceThriftPort_{5972};
  int teAgentThriftPort_{2022};
  // DEFAULT_TE_SRV6_AGENT_PORT from nettools/ebb/platform/if/TeSrv6Agent.thrift
  int teSrv6AgentThriftPort_{5911};
  int dataCorralServiceThriftPort_{5971};
  int vipInjectorThriftPort_{3333};
  std::string color_{"yes"};
  std::string filter_;
  std::string aggregate_;
  bool aggregateAcrossDevices_{false};
  int localDrainerThriftPort_{10701};
};

} // namespace facebook::fboss
