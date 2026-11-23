// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <regex>

#include "fboss/cli/fboss2/utils/CommandExit.h"

namespace facebook::fboss {
class FilterOp {
 public:
  virtual ~FilterOp() {}
  FilterOp() {}
  virtual bool compare(const int& resultValue, const int& predicateValue) {
    return false;
  }
  virtual bool compare(const long& resultValue, const long& predicateValue) {
    return false;
  }
  virtual bool compare(const short& resultValue, const short& predicateValue) {
    return false;
  }
  virtual bool compare(
      const std::string& resultValue,
      const std::string& predicateValue) {
    return false;
  }
  virtual bool compare(
      const std::byte& resultValue,
      const std::byte& predicateValue) {
    return false;
  }
};

class FilterOpEq : public FilterOp {
 public:
  bool compare(const int& resultValue, const int& predicateValue) override {
    return resultValue == predicateValue;
  }
  bool compare(const long& resultValue, const long& predicateValue) override {
    return resultValue == predicateValue;
  }
  bool compare(const short& resultValue, const short& predicateValue) override {
    return resultValue == predicateValue;
  }
  bool compare(
      const std::string& resultValue,
      const std::string& predicateValue) override {
    return resultValue == predicateValue;
  }
  bool compare(const std::byte& resultValue, const std::byte& predicateValue)
      override {
    return resultValue == predicateValue;
  }
};

class FilterOpNeq : public FilterOp {
 public:
  bool compare(const int& resultValue, const int& predicateValue) override {
    return resultValue != predicateValue;
  }
  bool compare(const long& resultValue, const long& predicateValue) override {
    return resultValue != predicateValue;
  }
  bool compare(const short& resultValue, const short& predicateValue) override {
    return resultValue != predicateValue;
  }
  bool compare(
      const std::string& resultValue,
      const std::string& predicateValue) override {
    return resultValue != predicateValue;
  }
  bool compare(const std::byte& resultValue, const std::byte& predicateValue)
      override {
    return resultValue != predicateValue;
  }
};

class FilterOpGt : public FilterOp {
 public:
  bool compare(const int& resultValue, const int& predicateValue) override {
    return resultValue > predicateValue;
  }
  bool compare(const long& resultValue, const long& predicateValue) override {
    return resultValue > predicateValue;
  }
  bool compare(const short& resultValue, const short& predicateValue) override {
    return resultValue > predicateValue;
  }
  bool compare(
      const std::string& resultValue,
      const std::string& predicateValue) override {
    std::cerr << "> operator on strings not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(const std::byte& resultValue, const std::byte& predicateValue)
      override {
    return resultValue > predicateValue;
  }
};

class FilterOpLt : public FilterOp {
 public:
  bool compare(const int& resultValue, const int& predicateValue) override {
    return resultValue < predicateValue;
  }
  bool compare(const long& resultValue, const long& predicateValue) override {
    return resultValue < predicateValue;
  }
  bool compare(const short& resultValue, const short& predicateValue) override {
    return resultValue < predicateValue;
  }
  bool compare(
      const std::string& resultValue,
      const std::string& predicateValue) override {
    std::cerr << "< operator on strings not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(const std::byte& resultValue, const std::byte& predicateValue)
      override {
    return resultValue < predicateValue;
  }
};

class FilterOpGte : public FilterOp {
 public:
  bool compare(const int& resultValue, const int& predicateValue) override {
    return resultValue >= predicateValue;
  }
  bool compare(const long& resultValue, const long& predicateValue) override {
    return resultValue >= predicateValue;
  }
  bool compare(const short& resultValue, const short& predicateValue) override {
    return resultValue >= predicateValue;
  }
  bool compare(
      const std::string& resultValue,
      const std::string& predicateValue) override {
    std::cerr << ">= operator on strings not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(const std::byte& resultValue, const std::byte& predicateValue)
      override {
    return resultValue >= predicateValue;
  }
};

class FilterOpLte : public FilterOp {
 public:
  bool compare(const int& resultValue, const int& predicateValue) override {
    return resultValue <= predicateValue;
  }
  bool compare(const long& resultValue, const long& predicateValue) override {
    return resultValue <= predicateValue;
  }
  bool compare(const short& resultValue, const short& predicateValue) override {
    return resultValue <= predicateValue;
  }
  bool compare(
      const std::string& resultValue,
      const std::string& predicateValue) override {
    std::cerr << "<= operator on strings not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(const std::byte& resultValue, const std::byte& predicateValue)
      override {
    return resultValue <= predicateValue;
  }
};

class FilterOpStartsWith : public FilterOp {
 public:
  bool compare(const int& resultValue, const int& predicateValue) override {
    std::cerr << "=^ operator on non-strings not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(const long& resultValue, const long& predicateValue) override {
    std::cerr << "=^ operator on non-strings not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(const short& resultValue, const short& predicateValue) override {
    std::cerr << "=^ operator on non-strings not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(
      const std::string& resultValue,
      const std::string& predicateValue) override {
    return resultValue.rfind(predicateValue, 0) == 0;
  }
  bool compare(const std::byte& resultValue, const std::byte& predicateValue)
      override {
    std::cerr << "=^ operator on non-strings not supported" << std::endl;
    utils::commandExit(1);
  }
};

class FilterOpRegex : public FilterOp {
 public:
  bool compare(const int& resultValue, const int& predicateValue) override {
    std::cerr << "=~ operator on ints not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(const long& resultValue, const long& predicateValue) override {
    std::cerr << "=~ operator on longs not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(const short& resultValue, const short& predicateValue) override {
    std::cerr << "=~ operator on shorts not supported" << std::endl;
    utils::commandExit(1);
  }
  bool compare(
      const std::string& resultValue,
      const std::string& predicateValue) override {
    auto matchPattern = [&predicateValue](const std::string& value) {
      std::regex pattern{predicateValue};
      std::smatch matches;
      return regex_search(value, matches, pattern);
    };
    return matchPattern(resultValue);
  }
  bool compare(const std::byte& resultValue, const std::byte& predicateValue)
      override {
    std::cerr << "=~ operator on bytes not supported" << std::endl;
    utils::commandExit(1);
  }
};
} // namespace facebook::fboss
