// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <array>
#include <optional>
#include <vector>
#include "fboss/agent/hw/sai/fake/FakeManager.h"
#include "fboss/agent/hw/sai/fake/FakeSaiMySidEntry.h"

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
class FakeSaiSrv6SidList {
 public:
  FakeSaiSrv6SidList(
      sai_int32_t type,
      std::vector<std::array<uint8_t, 16>> segmentList,
      sai_object_id_t nextHopId)
      : type(type), segmentList(std::move(segmentList)), nextHopId(nextHopId) {}
  sai_object_id_t id{0};
  sai_int32_t type;
  std::vector<std::array<uint8_t, 16>> segmentList;
  sai_object_id_t nextHopId{SAI_NULL_OBJECT_ID};
};

using FakeSrv6SidListManager = FakeManager<sai_object_id_t, FakeSaiSrv6SidList>;

struct FakeMySidEntryAttributes {
  sai_int32_t endpointBehavior{0};
  sai_int32_t endpointBehaviorFlavor{0};
  sai_object_id_t nextHopId{SAI_NULL_OBJECT_ID};
  sai_object_id_t vrf{SAI_NULL_OBJECT_ID};
  sai_int32_t packetAction{SAI_PACKET_ACTION_FORWARD};
  std::optional<sai_object_id_t> tunnelId;
};

using FakeMySidEntryManager =
    FakeManager<FakeSaiMySidEntry, FakeMySidEntryAttributes>;

void populate_srv6_api(sai_srv6_api_t** srv6_api);
#endif

} // namespace facebook::fboss
