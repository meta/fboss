// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/thriftpath_plugin/Path.h"

namespace thriftpath {

std::vector<std::string> copyAndExtendVec(
    const std::vector<std::string>& parents,
    std::string last) {
  std::vector<std::string> out(parents);
  out.emplace_back(std::move(last));
  return out;
}

std::vector<facebook::fboss::fsdb::OperPathElem> copyAndExtendVec(
    const std::vector<facebook::fboss::fsdb::OperPathElem>& parents,
    facebook::fboss::fsdb::OperPathElem last) {
  std::vector<facebook::fboss::fsdb::OperPathElem> out(parents);
  out.emplace_back(std::move(last));
  return out;
}

std::string pathElemToString(const facebook::fboss::fsdb::OperPathElem& elem) {
  switch (elem.getType()) {
    case facebook::fboss::fsdb::OperPathElem::Type::raw:
      return elem.raw().value();
    case facebook::fboss::fsdb::OperPathElem::Type::regex:
      return elem.regex().value();
    case facebook::fboss::fsdb::OperPathElem::Type::any:
      return "*";
    default:
      throw std::runtime_error("Unknown path elem type");
  }
}

} // namespace thriftpath
