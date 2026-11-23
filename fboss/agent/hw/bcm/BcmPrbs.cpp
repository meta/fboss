// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/bcm/BcmPrbs.h"
#include <fmt/format.h>
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"
extern "C" {
#include <bcm/error.h>
#include <bcm/port.h>
#include <bcm/types.h>
}

namespace facebook::fboss {

const PrbsMap& asicPrbsPolynomialMap() {
  // We pass in PRBS polynominal from cli as integers. However bcm api has it's
  // own value that should be used mapping to different PRBS polynominal values.
  // Hence we have a map here to mark the projection. The key here is the
  // polynominal from cli and the value is the value used in bcm api.
  // clang-format off
  static PrbsMap polynomialMap = boost::assign::list_of<PrbsMap::relation>(
    7, 0)(
    15, 1)(
    23, 2)(
    31, 3)(
    9, 4)(
    11, 5)(
    58, 6);
  // clang-format on
  return polynomialMap;
}

prbs::InterfacePrbsState getBcmPortPrbsState(int unit, bcm_port_t port) {
  prbs::InterfacePrbsState state;
  auto prbsMap = asicPrbsPolynomialMap();

  auto getPrbsState = [unit, port, &state, &prbsMap](
                          bcm_port_phy_control_t type) {
    std::string typeStr;
    if (type == BCM_PORT_PHY_CONTROL_PRBS_TX_ENABLE) {
      typeStr = "BCM_PORT_PHY_CONTROL_PRBS_TX_ENABLE";
    } else if (type == BCM_PORT_PHY_CONTROL_PRBS_RX_ENABLE) {
      typeStr = "BCM_PORT_PHY_CONTROL_PRBS_RX_ENABLE";
    } else {
      typeStr = "BCM_PORT_PHY_CONTROL_PRBS_POLYNOMIAL";
    }
    uint32 currVal{0};
    auto rv = bcm_port_phy_control_get(unit, port, type, &currVal);
    if (rv != BCM_E_NOT_FOUND) {
      bcmCheckError(
          rv,
          fmt::format(
              "Failed to get {} for port {} : {}",
              typeStr,
              port,
              bcm_errmsg(rv)));
      if (type == BCM_PORT_PHY_CONTROL_PRBS_TX_ENABLE) {
        state.generatorEnabled() = currVal;
      } else if (type == BCM_PORT_PHY_CONTROL_PRBS_RX_ENABLE) {
        state.checkerEnabled() = currVal;
      } else {
        auto it = prbsMap.right.find(currVal);
        if (it != prbsMap.right.end()) {
          state.polynomial() = static_cast<prbs::PrbsPolynomial>(it->second);
        }
      }
    }
  };

  getPrbsState(BCM_PORT_PHY_CONTROL_PRBS_TX_ENABLE);
  getPrbsState(BCM_PORT_PHY_CONTROL_PRBS_RX_ENABLE);
  getPrbsState(BCM_PORT_PHY_CONTROL_PRBS_POLYNOMIAL);

  return state;
}

} // namespace facebook::fboss
