// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/ResolvedNexthopProbe.h"
#include "fboss/agent/ArpHandler.h"
#include "fboss/agent/IPv6Handler.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/SwitchState.h"
#include "folly/Random.h"

#include <chrono>

namespace {
const std::chrono::milliseconds kInitialBackoff{1000};
const std::chrono::milliseconds kMaximumBackoff{10000};
auto constexpr kJitterPct = 10;
} // namespace

namespace facebook::fboss {

ResolvedNextHopProbe::ResolvedNextHopProbe(
    SwSwitch* sw,
    FbossEventBase* evb,
    ResolvedNextHop nexthop)
    : folly::AsyncTimeout(evb),
      sw_(sw),
      evb_(evb),
      nexthop_(nexthop),
      backoff_(kInitialBackoff, kMaximumBackoff) {}

void ResolvedNextHopProbe::timeoutExpired() noexcept {
  auto ip = nexthop_.addr();
  auto state = sw_->getState();
  auto intf = state->getInterfaces()->getNodeIf(nexthop_.intfID().value());
  if (!intf) {
    // probe and state update runs in distinct threads. probe runs in background
    // thread while state update in update thread.
    // imagine that probe is invoked right after state is updated but before
    // probe is either stopped or removed by probe scheduler.  in this state
    // update has either deleted interface or vlan. in such a case, a probe may
    // attempt to access interface or vlan which no longer exists. in such a
    // case simply stop scheduling this probe.
    XLOG(ERR) << "a spurios probe to " << nexthop_.addr() << " on interface "
              << nexthop_.intfID().value() << " exists!";
    stop();
    return;
  }

  if (ip.isV4()) {
    // send arp request
    ArpHandler::sendArpRequest(sw_, intf, ip.asV4());
    sw_->sentArpRequest(intf, ip.asV4());
  } else {
    // send ndp request
    IPv6Handler::sendMulticastNeighborSolicitation(sw_, intf, ip.asV6());
    sw_->sentNeighborSolicitation(intf, ip.asV6());
  }
  // exponential back-off
  backoff_.reportError();
  // add jitter to reduce contention
  auto backoff = backoff_.getTimeRemainingUntilRetry();
  auto timeout = backoff.count() +
      (folly::Random::rand32() % (backoff.count() * kJitterPct / 100));
  scheduleTimeout(timeout);
}

} // namespace facebook::fboss
