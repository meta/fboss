/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmEgressQueueFlexCounter.h"

#include "fboss/agent/hw/bcm/BcmCosQueueCounterType.h"
#include "fboss/agent/hw/bcm/BcmCosQueueManagerUtils.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include <thrift/lib/cpp/util/EnumUtils.h>

extern "C" {
#include <bcm/flexctr.h>
#include <bcm/port.h>
}

namespace {
constexpr auto kNumCPUReservedPorts = 1;

struct BcmGetEgressQueueFlexCounterData {
  uint32_t counterID{0};
  uint32_t numIndex{0};
  uint8_t queueIdxMask{0};
  uint8_t portIdxMask{0};
};

struct BcmGetEgressQueueFlexCounterManagerData {
  BcmGetEgressQueueFlexCounterData cpu;
  BcmGetEgressQueueFlexCounterData port;
};

uint8_t getMask(int value) {
  // NOTE: Both numQueuesPerPort and numPorts can be 1, i.e. cpu egress queue
  // flex counter only collect cpu queue stats, so there's only one port, the
  // cpu port. But we still need at least one mask_size for it.
  return std::max(static_cast<int>(ceil(log2(value))), 1);
}

// Broadcom provided some reference code in CS00011199690 to implement the
// details of egress queue counters.
// TODO(joseph5wu) Need to ask Broadcom why using 1 here but 16 in IFP
constexpr auto kEgressQueueActionPacketOperationObj0Mask = 1;
constexpr auto kEgressQueueActionPacketOperationObj1Mask = 1;

using namespace facebook::fboss;

/*
 * Since the queue stat can be queried very frequently, we use a pre-allocate
 * map to store the necessary indexes and data so we don't have to allocate
 * the new memory every time when calling the getStats function.
 *
 * NOTE: Originally, we can use the following unordered_map, whose value is
 * BcmEgressQueueStatsData, in BcmEgressQueueFlexCounter class so both cpu
 * and regular front panel port flex counter can use the same data structure.
 * Unfortunately because bcm_flexctr_counter_value_t is not open source, so it's
 * quite difficult to make the std::vector<bcm_flexctr_counter_value_t> of
 * BcmEgressQueueStatsData in open source code. I've tried using void* to hide
 * this vector but it causes some memory leak issue if we're not properly handle
 * the vector * when we try to delete this object.
 * Since Broadcom will eventually support open sourcing these apis, I decided to
 * use a static value in the facebook cpp file to directly use
 *`bcm_flexctr_counter_value_t`.
 */
struct BcmEgressQueueStatsData {
  std::vector<uint32_t> indexes;
  std::vector<bcm_flexctr_counter_value_t> data;
};
using StatsDataMap = std::unordered_map<
    bcm_gport_t,
    folly::Synchronized<std::optional<BcmEgressQueueStatsData>>>;
static StatsDataMap cpuStatsData;
static StatsDataMap portStatsData;
StatsDataMap& getStatsDataMap(bool isForCPU) {
  return isForCPU ? cpuStatsData : portStatsData;
}

void setEgressQueueActionConfig(bcm_flexctr_action_t* action, int numIndex) {
  action->flags = 0;
  action->source = bcmFlexctrSourceEgrPort;
  action->mode = bcmFlexctrCounterModeNormal;
  // reserve resources
  action->index_num = numIndex;
}

void setEgressQueueActionIndex(
    bcm_flexctr_action_t* action,
    int numPorts,
    int numQueuesPerPort) {
  // The FlexCounter index cacluation logic is:
  // 1) We define two values in bcm_flexctr_index_operation_t.object array
  //   1.1) object[0] is used for queue id, with
  //        mask = ceil(log2(numQueuesPerPort)) and shift = 0;
  //   1.2) object[1] is used for local logical port id(our PortID), with
  //        mask = ceil(log2(numPorts)) and shift = object[0].mask
  // 2) So the final index will be:
  //      value0 = (SEL(object0) >> shift0) & ((1 << mask_size0) - 1)).
  //      value1 = (SEL(object1) & ((1 << mask_size1) - 1)) << shift1.
  //      index = (value1 | value0).
  // For example, in TH4, total number of queues of a port queue is 12, so
  // mask0 = ceil(log2(12)) = 4 and the index of logical port=8, uc queue=4 will
  // be 0b10000100. NOTE: UC queue comes before MC queue in SDK.
  bcm_flexctr_index_operation_t* index = &(action->index_operation);
  index->object[0] = bcmFlexctrObjectStaticEgrQueueNum;
  index->mask_size[0] = getMask(numQueuesPerPort);
  index->shift[0] = 0;
  index->object[1] = bcmFlexctrObjectStaticEgrEgressPort;
  index->mask_size[1] = getMask(numPorts);
  index->shift[1] = index->mask_size[0];
}

/*
 * The following functions are duplicated from
 * BcmIngressFieldProcessorFlexCounter. Unfortunately all the bcm_ types are
 * not open source yet, hence we don't have an easy way to pass in or return
 * these bcm_ types.
 * Once we have FlexCounter open source in the SDK, we can merge these functions
 */
void setActionPerPacketValueOperation(
    bcm_flexctr_value_operation_t* operation) {
  operation->select = bcmFlexctrValueSelectCounterValue;
  operation->object[0] = bcmFlexctrObjectConstOne;
  operation->mask_size[0] = kEgressQueueActionPacketOperationObj0Mask;
  operation->shift[0] = 0;
  operation->object[1] = bcmFlexctrObjectConstZero;
  operation->mask_size[1] = kEgressQueueActionPacketOperationObj1Mask;
  operation->shift[1] = 0;
  operation->type = bcmFlexctrValueOperationTypeInc;
}

void setActionPerBytesValueOperation(bcm_flexctr_value_operation_t* operation) {
  operation->select = bcmFlexctrValueSelectPacketLength;
  operation->type = bcmFlexctrValueOperationTypeInc;
}

void setActionValue(bcm_flexctr_action_t* action) {
  // PACKET type always use operation_a
  setActionPerPacketValueOperation(&(action->operation_a));
  setActionPerBytesValueOperation(&(action->operation_b));
}

uint32_t getCounterIndex(
    const BcmSwitch* hw,
    bcm_gport_t gPort,
    cfg::StreamType streamType,
    int queue,
    int reservedNumQueuePerPorts) {
  // Current implementation has two separate BcmEgressQueueFlexCounter for CPU
  // and regular port. And we only have MC queues for CPU
  if (gPort == BCM_GPORT_LOCAL_CPU) {
    if (streamType != cfg::StreamType::MULTICAST) {
      throw facebook::fboss::FbossError(
          "CPU only uses MULTICAST queue and doesn't support ",
          apache::thrift::util::enumNameSafe(streamType));
    }
    // For CPU queue FlexCounter, we only have one port with all multicast
    // queues, so whatever the queue number is passed in this function, it will
    // be the index of the queue in FlexCounter
    return queue;
  } else {
    // For Port queue FlexCounter, we store all regular port in one FlexCounter
    // refer to the comments in setEgressQueueActionIndex() above, we will use
    // a fixed reservedNumQueuePerPorts to reserve #queues indexes for a port.
    // And the SDK will store UC queues before MC queues
    bcm_port_t localPort;
    auto rv = bcm_port_local_get(hw->getUnit(), gPort, &localPort);
    bcmCheckError(rv, "Can't get local port id for gPort:", gPort);

    uint32_t queueIdxStart = localPort * reservedNumQueuePerPorts;
    if (streamType == cfg::StreamType::UNICAST) {
      return queueIdxStart + queue;
    } else if (streamType == cfg::StreamType::MULTICAST) {
      return queueIdxStart +
          hw->getPlatform()->getAsic()->getDefaultNumPortQueues(
              cfg::StreamType::UNICAST, cfg::PortType::INTERFACE_PORT) +
          queue;
    } else {
      throw FbossError(
          "Port:",
          localPort,
          " doesn't support ",
          apache::thrift::util::enumNameSafe(streamType));
    }
  }
}

int getEgressQueueFlexCountersCallback(
    int /* unit */,
    uint32 stat_counter_id,
    bcm_flexctr_action_t* action,
    void* user_data) {
  auto* cbUserData =
      static_cast<BcmGetEgressQueueFlexCounterManagerData*>(user_data);
  // Check action config and index config are using the same config we program
  if (action->source != bcmFlexctrSourceEgrPort ||
      action->index_operation.object[0] != bcmFlexctrObjectStaticEgrQueueNum ||
      action->index_operation.object[1] !=
          bcmFlexctrObjectStaticEgrEgressPort) {
    return 0;
  }

  auto fillInCallbackData = [&](BcmGetEgressQueueFlexCounterData& data,
                                const std::string& name) {
    if (action->index_num == data.numIndex ||
        action->index_operation.mask_size[0] == data.queueIdxMask ||
        action->index_operation.mask_size[1] == data.portIdxMask) {
      if (data.counterID != 0) {
        throw FbossError(
            "Multiple Egress Queue FlexCounters for ",
            name,
            ": ",
            data.counterID,
            " and ",
            stat_counter_id);
      } else {
        data.counterID = stat_counter_id;
        XLOG(DBG1) << "Found " << name
                   << " Egress Queue FlexCounter:" << stat_counter_id;
      }
    }
  };

  fillInCallbackData(cbUserData->cpu, "cpu");
  fillInCallbackData(cbUserData->port, "port");
  return 0;
}

void detachFromHW(int unit, bcm_gport_t gPort, uint32_t counterID) {
  auto rv = bcm_port_stat_detach_with_id(unit, gPort, counterID);
  if (rv == BCM_E_PARAM) {
    // If the counter is not attached to this port, it will return E_PARAM
    return;
  }
  bcmCheckError(
      rv,
      "Failed to detach Egress Queue FlexCounter:",
      counterID,
      " from port:",
      gPort,
      " on Hardware");
  XLOG(DBG1) << "Detached Egress Queue FlexCounter:" << counterID
             << " from port:" << gPort << " on Hardware";
}
} // namespace

namespace facebook::fboss {

BcmEgressQueueFlexCounter::BcmEgressQueueFlexCounter(
    BcmSwitch* hw,
    int numPorts,
    int numQueuesPerPort,
    bool isForCPU)
    : BcmFlexCounter(hw->getUnit()),
      hw_(hw),
      numQueuesPerPort_(numQueuesPerPort),
      isForCPU_(isForCPU) {
  if (numPorts < 1) {
    throw FbossError("Invalid numPorts:", numPorts, ", which needs to be >= 1");
  }
  if (numQueuesPerPort < 1) {
    throw FbossError(
        "Invalid numQueuesPerPort:",
        numQueuesPerPort,
        ", which needs to be >= 1");
  }
  // With FlexCounter feature in the new SDK, we can create one counter for all
  // queues of ports or cpu. Technically we can create one flex counter for each
  // port, but this doesn't utilize the new FlexCounter mechanism thoroughly.
  // NOTE: The following implementation follows SDK example:
  // examples/xgs/tomahawk4/flexctr/th4_hsdk_flexctr_egr_port_queue.c
  bcm_flexctr_action_t action;
  bcm_flexctr_action_t_init(&action);

  // set counter action index
  setEgressQueueActionIndex(&action, numPorts, numQueuesPerPort);
  // Because of the index design in FlexCounter, the reserved number of queues
  // per port will be the power of 2, which can be greater than numQueuesPerPort
  // And we need to use reservedNumQueuesPerPort_ to caculate the index of
  // queue for a port. More details in setEgressQueueActionIndex() comments.
  reservedNumQueuesPerPort_ = pow(2, action.index_operation.mask_size[0]);

  // Since we will only create this flex counter once, we have to reserve all
  // queues for all logical ports + cpu. In this way, we will always know the
  // exact index for a specific queue of a specific port. And we don't need to
  // consider the case that we add/remove a port and change the order.
  int totalIndexes = numPorts * reservedNumQueuesPerPort_;
  // set counter action config
  setEgressQueueActionConfig(&action, totalIndexes);

  // set counter action value
  setActionValue(&action);

  auto rv =
      bcm_flexctr_action_create(unit_, 0 /* 0 options */, &action, &counterID_);
  bcmCheckError(
      rv,
      "Failed to create Egress Queue FlexCounter for total index num:",
      totalIndexes);
  XLOG(DBG1) << "Successfully created Egress Queue FlexCounter:" << counterID_
             << " for total index num:" << totalIndexes;
  if (!isForCPU_) {
    rv = bcm_pktio_txpmd_stat_attach(unit_, counterID_);
    bcmCheckError(
        rv,
        "Failed to attach egress queue flex counter to SOBMH packets sent from control plane");
  }
}

BcmEgressQueueFlexCounter::BcmEgressQueueFlexCounter(
    BcmSwitch* hw,
    uint32_t counterID,
    int numQueuesPerPort,
    int reservedNumQueuesPerPort,
    bool isForCPU)
    : BcmFlexCounter(hw->getUnit(), counterID),
      hw_(hw),
      numQueuesPerPort_(numQueuesPerPort),
      reservedNumQueuesPerPort_(reservedNumQueuesPerPort),
      isForCPU_(isForCPU) {}

BcmEgressQueueFlexCounter::~BcmEgressQueueFlexCounter() {
  // Unfortunately Brcm SDK is not able to provide an api to return the attached
  // ports of a specific flex counter, and our statsDataMap is only populated
  // after we call the attach() function during BcmPort initialization.
  // But sometimes, our code might hit some issues before we finish BcmPort
  // initialization and have to call BcmSwitch::resetTables(). In such case,
  // statsDataMap might be empty but in HW, some ports are still attached to
  // this counter and eventually agent will crash because we're trying to delete
  // such counter before detach all ports first.
  bcm_port_config_t pcfg;
  bcm_port_config_t_init(&pcfg);
  auto rv = bcm_port_config_get(unit_, &pcfg);
  bcmCheckError(rv, "failed to get port configuration");

  bcm_port_t idx;
  bcm_gport_t gPort;
  BCM_PBMP_ITER(isForCPU_ ? pcfg.cpu : pcfg.port, idx) {
    rv = bcm_port_gport_get(hw_->getUnit(), idx, &gPort);
    bcmCheckError(rv, "Failed to get gport for BCM port ", idx);
    detachFromHW(hw_->getUnit(), gPort, counterID_);
  }
  if (!isForCPU_) {
    rv = bcm_pktio_txpmd_stat_detach(hw_->getUnit());
    bcmCheckError(
        rv, "Failed to detach egress queue flex counter from SOBMH packets");
  }
}

void BcmEgressQueueFlexCounter::attach(bcm_gport_t gPort) {
  // It's safe to call the attach function even if the counter is already
  // attached to the port. Always call atach function here, so we can always
  // make sure the flex counter is attached in HW
  auto rv = bcm_port_stat_attach(hw_->getUnit(), gPort, counterID_);
  bcmCheckError(
      rv,
      "Failed to attach Egress Queue FlexCounter:",
      counterID_,
      " to port:",
      gPort);

  // Always update the statsData when attach is called to make sure the cache
  // in statsDataMap has the latest queue settings.
  BcmEgressQueueStatsData statsData;
  auto prepareStatData = [&](int totalQueue, cfg::StreamType streamType) {
    for (int queue = 0; queue < totalQueue; queue++) {
      statsData.indexes.push_back(getCounterIndex(
          hw_, gPort, streamType, queue, reservedNumQueuesPerPort_));
      bcm_flexctr_counter_value_t values{};
      statsData.data.push_back(values);
    }
  };
  if (gPort == BCM_GPORT_LOCAL_CPU) {
    prepareStatData(numQueuesPerPort_, cfg::StreamType::MULTICAST);
  } else {
    // First handle UC queues
    const auto* asic = hw_->getPlatform()->getAsic();
    prepareStatData(
        asic->getDefaultNumPortQueues(
            cfg::StreamType::UNICAST, cfg::PortType::INTERFACE_PORT),
        cfg::StreamType::UNICAST);
    prepareStatData(
        asic->getDefaultNumPortQueues(
            cfg::StreamType::MULTICAST, cfg::PortType::INTERFACE_PORT),
        cfg::StreamType::MULTICAST);
  }

  // Now try to get the lock to update the Synchronized object
  auto& statsDataMap = getStatsDataMap(isForCPU_);
  auto cachedPortIt = statsDataMap.find(gPort);
  if (cachedPortIt == statsDataMap.end()) {
    statsDataMap.emplace(gPort, statsData);
    XLOG(DBG2) << "First time attach EgressQueueFlexCounter:" << counterID_
               << " to port:" << gPort;
  } else {
    auto wLockedCache = cachedPortIt->second.wlock();
    *wLockedCache = statsData;
    XLOG(DBG2) << "Attached EgressQueue FlexCounter:" << counterID_
               << " to port:" << gPort;
  }
}

void BcmEgressQueueFlexCounter::detach(bcm_gport_t gPort) {
  auto& statsDataMap = getStatsDataMap(isForCPU_);
  auto cachedPortIt = statsDataMap.find(gPort);
  if (cachedPortIt == statsDataMap.end()) {
    // It's safe to call the detach function even if the counter is not attached
    // to the port. Always call detach function here, so we can always make sure
    // the flex counter is detached in HW
    detachFromHW(hw_->getUnit(), gPort, counterID_);
    XLOG(DBG2) << "Trying to detach EgressQueue FlexCounter:" << counterID_
               << ", which is already detached from port:" << gPort;
  } else {
    // If the port already exists in statsDataMap, we need to fetch the wlock
    // before actually detaching from HW
    auto wLockedCache = cachedPortIt->second.wlock();
    detachFromHW(hw_->getUnit(), gPort, counterID_);
    // Instead of removing from the map, which might need a map level lock in
    // order to make sure the getStats() won't try to access the iterator while
    // it's in the process of detaching the flex counter, use std::optional as
    // the value of this port level Synchorized.
    *wLockedCache = std::nullopt;
    XLOG(DBG2) << "Detached EgressQueue FlexCounter:" << counterID_
               << " from port:" << gPort;
  }
}

void BcmEgressQueueFlexCounter::getStats(
    bcm_gport_t gPort,
    [[maybe_unused]] BcmEgressQueueTrafficCounterStats& stats) {
  auto& statsDataMap = getStatsDataMap(isForCPU_);
  auto statDataItr = statsDataMap.find(gPort);
  if (statDataItr == statsDataMap.end()) {
    XLOG(WARN) << "Can't get queue stats before attaching FlexCounter to port:"
               << gPort << ", skip getStats()";
    return;
  }
  auto queuesStatData = statDataItr->second.wlock();
  if (!queuesStatData->has_value()) {
    // The port might be disabled recently
    XLOG(WARN) << "Can't get queue stats before attaching FlexCounter to port:"
               << gPort << ", skip getStats()";
    return;
  }
  auto rv = bcm_flexctr_stat_get(
      hw_->getUnit(),
      counterID_,
      (*queuesStatData)->indexes.size(),
      (*queuesStatData)->indexes.data(),
      (*queuesStatData)->data.data());
  bcmCheckError(
      rv,
      "Failed to get Egress Queue FlexCounter stats for port:",
      gPort,
      ", #queues:",
      (*queuesStatData)->indexes.size());

  int frontPanelUcQueueNum =
      hw_->getPlatform()->getAsic()->getDefaultNumPortQueues(
          cfg::StreamType::UNICAST, cfg::PortType::INTERFACE_PORT);
  auto updateStatData =
      [&](int startIndex, int endIndex, cfg::StreamType streamType) {
        for (int queue = startIndex; queue <= endIndex; queue++) {
          // Because regular port queue has both UC and MC queues, and the SDK
          // put MC queue index immediately after UC queues, we need to subtract
          // ucQueueNum to make sure we can get the MC queue id starting from 0
          int queueIdInQueueStats = (gPort != BCM_GPORT_LOCAL_CPU &&
                                     streamType == cfg::StreamType::MULTICAST)
              ? queue - frontPanelUcQueueNum
              : queue;
          // Only update the queue stats if it's asked
          if (auto queueStatsItr = stats[streamType].find(queueIdInQueueStats);
              queueStatsItr != stats[streamType].end()) {
            queueStatsItr->second[cfg::CounterType::PACKETS] =
                (*queuesStatData)->data[queue].value[0];
            queueStatsItr->second[cfg::CounterType::BYTES] =
                (*queuesStatData)->data[queue].value[1];
          }
        }
      };

  if (gPort == BCM_GPORT_LOCAL_CPU) {
    updateStatData(
        0, (*queuesStatData)->indexes.size() - 1, cfg::StreamType::MULTICAST);
  } else {
    updateStatData(0, frontPanelUcQueueNum - 1, cfg::StreamType::UNICAST);
    updateStatData(
        frontPanelUcQueueNum,
        (*queuesStatData)->indexes.size() - 1,
        cfg::StreamType::MULTICAST);
  }
}

bool BcmEgressQueueFlexCounter::isSupported(BcmCosQueueStatType type) {
  return type == BcmCosQueueStatType::OUT_PACKETS ||
      type == BcmCosQueueStatType::OUT_BYTES;
}

BcmEgressQueueFlexCounterManager::BcmEgressQueueFlexCounterManager(
    BcmSwitch* hw) {
  int numCPUQueues = utility::getMaxCPUQueueSize(hw->getUnit());
  // Usually we only care about UC queue for regular ports, but to make sure
  // we can support collecting MC queue counters in the future, we create
  // a FlexCounter for both types queues.
  auto* asic = hw->getPlatform()->getAsic();
  int numQueuesPerPort =
      asic->getDefaultNumPortQueues(
          cfg::StreamType::UNICAST, cfg::PortType::INTERFACE_PORT) +
      asic->getDefaultNumPortQueues(
          cfg::StreamType::MULTICAST, cfg::PortType::INTERFACE_PORT);
  int numPorts = asic->getMaxNumLogicalPorts();

  // Currently there's not a straightforward way to find the flex counter id
  // from cpu or regular ports, so we have to use traverse callback to get
  // Egress Queue FlexCounter from the hardware. In this way, we won't create
  // a duplicated flexcounter during warm boot.
  // NOTE: both cpu or regular port can only have one attached flex counter.
  auto setupFlexCounterData = [](BcmGetEgressQueueFlexCounterData& data,
                                 int nPorts,
                                 int nQueuesPerPort) {
    data.numIndex = nPorts * nQueuesPerPort;
    data.queueIdxMask = getMask(nQueuesPerPort);
    data.portIdxMask = getMask(nPorts);
  };

  BcmGetEgressQueueFlexCounterManagerData userData;
  setupFlexCounterData(userData.cpu, kNumCPUReservedPorts, numCPUQueues);
  setupFlexCounterData(userData.port, numPorts, numQueuesPerPort);
  auto rv = bcm_flexctr_action_traverse(
      hw->getUnit(),
      &getEgressQueueFlexCountersCallback,
      static_cast<void*>(&userData));
  bcmCheckError(rv, "Failed to traverse flex counters for Egress Queue");

  // Create a BcmEgressQueueFlexCounter only for cpu
  if (userData.cpu.counterID == 0) {
    // Create a new CPU Egress Queue FlexCounter
    cpuQueueFlexCounter_ = std::make_unique<BcmEgressQueueFlexCounter>(
        hw, kNumCPUReservedPorts, numCPUQueues, true);
  } else {
    cpuQueueFlexCounter_ = std::make_unique<BcmEgressQueueFlexCounter>(
        hw,
        userData.cpu.counterID,
        numCPUQueues,
        pow(2, userData.cpu.queueIdxMask),
        true);
  }

  // Create a BcmEgressQueueFlexCounter for all regular ports
  if (userData.port.counterID == 0) {
    // Create a new Port Egress Queue FlexCounter
    portQueueFlexCounter_ = std::make_unique<BcmEgressQueueFlexCounter>(
        hw, numPorts, numQueuesPerPort);
  } else {
    portQueueFlexCounter_ = std::make_unique<BcmEgressQueueFlexCounter>(
        hw,
        userData.port.counterID,
        numQueuesPerPort,
        pow(2, userData.port.queueIdxMask));
  }
}
} // namespace facebook::fboss
