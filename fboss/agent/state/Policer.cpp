// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/state/Policer.h"

#include "fboss/agent/FbossError.h"

namespace facebook::fboss {

Policer::Policer(const cfg::Policer& policer) : BaseT(policer) {
  validate();
}

void Policer::validate() const {
  constexpr size_t kMaxLabelLength =
      31; // SAI chardata reserves a byte for NUL.
  const auto label = getLabel();
  if (label.empty() || label.size() > kMaxLabelLength ||
      label.find('\0') != std::string::npos) {
    throw FbossError("Policer label must be 1 to 31 bytes and contain no NUL");
  }
  if (getCommittedInformationRate() <= 0 || getCommittedBurstSize() <= 0) {
    throw FbossError(
        "Policer ",
        label,
        " committedInformationRate and committedBurstSize must be positive");
  }
  if (getMeterType() != cfg::PolicerMeterType::PACKETS &&
      getMeterType() != cfg::PolicerMeterType::BYTES) {
    throw FbossError("Policer ", label, " meterType must be PACKETS or BYTES");
  }
}

std::string Policer::getLabel() const {
  return get<switch_config_tags::label>()->cref();
}

cfg::PolicerMeterType Policer::getMeterType() const {
  return get<switch_config_tags::meterType>()->cref();
}

int64_t Policer::getCommittedInformationRate() const {
  return get<switch_config_tags::committedInformationRate>()->cref();
}

int64_t Policer::getCommittedBurstSize() const {
  return get<switch_config_tags::committedBurstSize>()->cref();
}

template struct ThriftStructNode<Policer, cfg::Policer>;

} // namespace facebook::fboss
