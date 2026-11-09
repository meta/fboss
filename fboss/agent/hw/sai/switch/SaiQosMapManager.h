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

#include "fboss/agent/hw/sai/api/QosMapApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/state/QosPolicy.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"

#include "folly/container/F14Map.h"

#include <map>
#include <memory>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;

using SaiQosMap = SaiObject<SaiQosMapTraits>;

struct SaiQosMapHandle {
  std::shared_ptr<SaiQosMap> dscpToTcMap;
  std::shared_ptr<SaiQosMap> tcToQueueMap;
  std::shared_ptr<SaiQosMap> expToTcMap;
  std::shared_ptr<SaiQosMap> tcToExpMap;
  std::shared_ptr<SaiQosMap> pcpToTcMap;
  std::shared_ptr<SaiQosMap> tcToPcpMap;
  std::shared_ptr<SaiQosMap> tcToPgMap;
#if defined(SAI_CBFC_SUPPORTED)
  std::shared_ptr<SaiQosMap> tcToVcMap;
  std::shared_ptr<SaiQosMap> queueToVcMap;
#endif
  std::shared_ptr<SaiQosMap> pfcPriorityToQueueMap;
  std::shared_ptr<SaiQosMap> pfcPriorityToPgMap;
  std::shared_ptr<SaiQosMap> tcToVoqMap;
  // Cached state-side PFC priority -> queue id map (empty if unset).
  std::map<int16_t, int16_t> pfcPriorityToQueueId;
  std::string name;
  bool isDefault;
};

class SaiQosMapManager {
 public:
  SaiQosMapManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);
  void addQosMap(
      const std::shared_ptr<QosPolicy>& newQosPolicy,
      bool isDefault);
  void removeQosMap(
      const std::shared_ptr<QosPolicy>& oldQosPolicy,
      bool isDefault);
  void changeQosMap(
      const std::shared_ptr<QosPolicy>& oldQosPolicy,
      const std::shared_ptr<QosPolicy>& newQosPolicy,
      bool newPolicyIsDefault);

  SaiQosMapHandle* FOLLY_NULLABLE
  getQosMap(const std::optional<std::string>& qosPolicyName = std::nullopt);
  const SaiQosMapHandle* FOLLY_NULLABLE getQosMap(
      const std::optional<std::string>& qosPolicyName = std::nullopt) const;

  // PFC priority -> queue id map for the given (or default) QoS policy; empty
  // if not found or unset.
  std::map<int16_t, int16_t> getPfcPriorityToQueueId(
      const std::optional<std::string>& qosPolicyName = std::nullopt) const;

 private:
  std::shared_ptr<SaiQosMap> setDscpToTcQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
  std::shared_ptr<SaiQosMap> setTcToQueueQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy,
      bool voq);
  std::shared_ptr<SaiQosMap> setExpToTcQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
  std::shared_ptr<SaiQosMap> setTcToExpQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
  std::shared_ptr<SaiQosMap> setPcpToTcQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
  std::shared_ptr<SaiQosMap> setTcToPcpQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
  std::shared_ptr<SaiQosMap> setTcToPgQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
#if defined(SAI_CBFC_SUPPORTED)
  std::shared_ptr<SaiQosMap> setTcToVcQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
  std::shared_ptr<SaiQosMap> setQueueToVcQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
#endif
  std::shared_ptr<SaiQosMap> setPfcPriorityToQueueQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
  std::shared_ptr<SaiQosMap> setPfcPriorityToPgQosMap(
      const std::shared_ptr<QosPolicy>& qosPolicy);
  void setQosMaps(
      const std::shared_ptr<QosPolicy>& newQosPolicy,
      bool isDefault);

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  SaiQosMapHandle* FOLLY_NULLABLE
  getQosMapImpl(const std::optional<std::string>& qosPolicyName) const;
  std::unordered_map<std::string, std::unique_ptr<SaiQosMapHandle>> handles_;
};

} // namespace facebook::fboss
