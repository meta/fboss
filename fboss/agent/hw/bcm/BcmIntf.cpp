/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmIntf.h"

extern "C" {
#include <bcm/l2.h>
}

#include <folly/IPAddress.h>
#include <folly/logging/xlog.h>
#include "fboss/agent/Constants.h"
#include "fboss/agent/hw/bcm/BcmAclEntry.h"
#include "fboss/agent/hw/bcm/BcmAddressFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmHost.h"
#include "fboss/agent/hw/bcm/BcmRoute.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/BcmWarmBootCache.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/Interface.h"

namespace facebook::fboss {

using folly::IPAddress;
using folly::MacAddress;
using std::make_pair;
using std::shared_ptr;
using std::unique_ptr;

BcmStation::~BcmStation() {
  if (id_ == INVALID) {
    return;
  }
  auto rc = bcm_l2_station_delete(hw_->getUnit(), id_);
  bcmLogFatal(rc, hw_, "failed to delete station entry ", id_);
  XLOG(DBG3) << "deleted station entry " << id_;
}

void BcmStation::program(MacAddress mac, int intfId) {
  CHECK_EQ(INVALID, id_);
  bcm_l2_station_t params;
  bcm_l2_station_t_init(&params);
  memcpy(&params.dst_mac, mac.bytes(), sizeof(params.dst_mac));
  memset(&params.dst_mac_mask, 0xFF, sizeof(params.dst_mac_mask));
  params.flags |= BCM_L2_STATION_IPV4 | BCM_L2_STATION_IPV6 |
      BCM_L2_STATION_ARP_RARP | BCM_L2_STATION_MPLS;
  if (intfId != INVALID) {
    params.vlan = intfId;
    params.vlan_mask = 0x0;
    params.flags |= BCM_L2_STATION_WITH_ID;
  }
  int rc;
  bool addStation = false;
  const auto warmBootCache = hw_->getWarmBootCache();
  auto vlanStationItr = warmBootCache->findVlanStation(VlanID(intfId));
  auto id = hw_->getPlatform()->getAsic()->getStationID(intfId);
  auto asic = hw_->getPlatform()->getAsic()->getAsicType();
  if (asic == cfg::AsicType::ASIC_TYPE_TOMAHAWK4) {
    int maxId;
    rc = bcm_l2_station_size_get(hw_->getUnit(), &maxId);
    bcmCheckError(rc, "failed to get l2 station table size");
    if (id > maxId) {
      throw FbossError(
          "Station id ", id, " is larger than maximum allowed id ", maxId);
    }
    // BCM_L2_STATION_UNDERLAY is required on TH4 to program correct L2 station
    // table
    params.flags |= BCM_L2_STATION_UNDERLAY;
  }
  if (vlanStationItr != warmBootCache->vlan2Station_end()) {
    // Lambda to compare with existing entry to check if we need to reprogram
    auto equivalent = [=](const bcm_l2_station_t& newStation,
                          const bcm_l2_station_t& oldStation) {
      return macFromBcm(oldStation.dst_mac) == macFromBcm(newStation.dst_mac) &&
          macFromBcm(oldStation.dst_mac_mask) ==
          macFromBcm(newStation.dst_mac_mask);
    };
    const auto& existingStation = vlanStationItr->second;
    if (!equivalent(params, existingStation)) {
      // Delete old station end and set addStation to true
      XLOG(DBG1) << "Updating BCM station with Mac : " << mac << " and " << id;
      rc = bcm_l2_station_delete(hw_->getUnit(), id);
      bcmCheckError(rc, "failed to delete station entry ", id);
      addStation = true;
    } else {
      XLOG(DBG1) << " station entry " << id << " already exists ";
      id_ = id;
    }

  } else {
    addStation = true;
  }

  if (addStation) {
    if (vlanStationItr != warmBootCache->vlan2Station_end()) {
      XLOG(DBG1) << "Adding BCM station with Mac : " << mac << " and " << id;
    }
    rc = bcm_l2_station_add(hw_->getUnit(), &id, &params);
    bcmCheckError(
        rc, "failed to program station entry ", id, " to ", mac.toString());
    id_ = id;
    XLOG(DBG1) << "updated station entry " << id_ << " to " << mac.toString();
  }
  if (vlanStationItr != warmBootCache->vlan2Station_end()) {
    warmBootCache->programmed(vlanStationItr);
  }
  CHECK_NE(id_, INVALID);
}

BcmIntf::BcmIntf(BcmSwitch* hw) : hw_(hw) {}

void BcmIntf::program(const shared_ptr<Interface>& intf) {
  const auto& oldIntf = intf_;
  if (oldIntf != nullptr) {
    // vid cannot be changed by this method.
    if (oldIntf->getVlanID() != intf->getVlanID()) {
      throw FbossError(
          "Interface VLAN ID (",
          oldIntf->getVlanID(),
          " vs ",
          intf->getVlanID(),
          " is changed during programming");
    }
  }

  // create or update a station entry
  if (station_ == nullptr) {
    station_ = unique_ptr<BcmStation>(new BcmStation(hw_));
  }
  if (oldIntf == nullptr || oldIntf->getMac() != intf->getMac()) {
    // Program BCM station to have the same ID as interface
    // so we can look them up during warm boot
    station_->program(intf->getMac(), intf->getID());
  }

  // The BCM agent has interfaces in VRF 0 only
  const auto vrf = BcmSwitch::getBcmVrfId(RouterID(0));

  // Lambda to compare with existing intf to determine if we need to
  // reprogram
  auto updateIntfIfNeeded = [&](bcm_l3_intf_t& newIntf,
                                const bcm_l3_intf_t& oldIntf) {
    auto equivalent = [=](const bcm_l3_intf_t& newIntf,
                          const bcm_l3_intf_t& existingIntf) {
      CHECK(newIntf.l3a_vrf == existingIntf.l3a_vrf);
      return macFromBcm(newIntf.l3a_mac_addr) ==
          macFromBcm(existingIntf.l3a_mac_addr) &&
          newIntf.l3a_vid == existingIntf.l3a_vid &&
          newIntf.l3a_mtu == existingIntf.l3a_mtu;
    };

    if (!equivalent(newIntf, oldIntf)) {
      XLOG(DBG1) << "Updating interface for vlan : " << intf->getVlanID()
                 << " and mac: " << intf->getMac();
      CHECK_NE(bcmIfId_, INVALID);
      newIntf.l3a_intf_id = bcmIfId_;
      newIntf.l3a_flags = BCM_L3_WITH_ID | BCM_L3_REPLACE;
      return true;
    }
    return false;
  };

  const auto intftoIfparam = [=](const auto& Intf) {
    bcm_l3_intf_t ifParams;
    bcm_l3_intf_t_init(&ifParams);
    ifParams.l3a_vrf = vrf;
    memcpy(
        &ifParams.l3a_mac_addr,
        Intf->getMac().bytes(),
        sizeof(ifParams.l3a_mac_addr));
    ifParams.l3a_vid = Intf->getVlanID();
    ifParams.l3a_mtu = Intf->getMtu();
    return ifParams;
  };

  auto ifParams = intftoIfparam(intf);
  // create the interface if needed
  if (bcmIfId_ == INVALID) {
    // create a BCM l3 interface
    bool addInterface = false;
    const auto warmBootCache = hw_->getWarmBootCache();
    auto vlanMac2IntfItr =
        warmBootCache->findL3Intf(intf->getVlanID(), intf->getMac());
    if (vlanMac2IntfItr != warmBootCache->vlanAndMac2Intf_end()) {
      const auto& existingIntf = vlanMac2IntfItr->second;
      bcmIfId_ = existingIntf.l3a_intf_id;
      if (hw_->getPlatform()->getAsic()->isSupported(
              HwAsic::Feature::INGRESS_L3_INTERFACE)) {
        bcmIngIfId_ = bcmIfId_;
      }
      if (updateIntfIfNeeded(ifParams, existingIntf)) {
        // Set add interface to true, we will no issue the call to add
        // but with the above flags set this will cause the entry to be
        // updated.
        addInterface = true;
      } else {
        XLOG(DBG1) << "Interface for vlan " << intf->getVlanID() << " and mac "
                   << intf->getMac() << " already exists";
      }

    } else {
      addInterface = true;
    }
    if (addInterface) {
      auto rc = bcm_l3_intf_create(hw_->getUnit(), &ifParams);
      bcmCheckError(rc, "failed to create L3 interface ", intf->getID());
      bcmIfId_ = ifParams.l3a_intf_id;
      bool updateIngress = true;
      if (vlanMac2IntfItr == warmBootCache->vlanAndMac2Intf_end()) {
        XLOG(DBG1) << "Adding interface for vlan : " << intf->getVlanID()
                   << " and mac: " << intf->getMac();
        // Todo : This is a temporary workaround to avoid crashing on
        // the RTSWs where l3_ingress config is present in asic due to a
        // bug. On enabling the "sidelinks" feature via warmboot upgrade on
        // these RTSWs l3_ingress is created again which causes a crash.
        // The bug has been fixed by D51830932. Once "sidelinks" feature is
        // enabled on the RTSWs with the bug, this code can be removed.
        // and we would add updateIngress = false; back here.
        bcm_l3_ingress_t l3_ingress;
        bcm_l3_ingress_t_init(&l3_ingress);
        bcm_if_t ingress_if = bcmIfId_;
        rc = bcm_l3_ingress_get(hw_->getUnit(), ingress_if, &l3_ingress);
        if (rc != BCM_E_NONE) {
          updateIngress = false;
        }
      }
      programIngressIfNeeded(intf, updateIngress);
    }
    if (vlanMac2IntfItr != warmBootCache->vlanAndMac2Intf_end()) {
      warmBootCache->programmed(vlanMac2IntfItr);
    }
    CHECK_NE(bcmIfId_, INVALID);
  }

  if (oldIntf) {
    auto oldIfParams = intftoIfparam(oldIntf);
    if (updateIntfIfNeeded(ifParams, oldIfParams)) {
      auto rc = bcm_l3_intf_create(hw_->getUnit(), &ifParams);
      bcmCheckError(rc, "failed to update L3 interface ", intf->getID());
      programIngressIfNeeded(intf, true);
    }
  }
  std::unordered_set<std::shared_ptr<BcmHostIf>> hosts;
  for (auto iter : std::as_const(*intf->getAddresses())) {
    auto addr = folly::IPAddress(iter.first);
    std::shared_ptr<BcmHostIf> host;
    BcmHostKey key = BcmHostKey(vrf, addr, intf->getID());
    if (hw_->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::HOSTTABLE)) {
      host = hw_->writableHostTable()->refOrEmplaceHost(key);
    } else {
      host = hw_->writableRouteTable()->refOrEmplaceHost(key);
    }
    CHECK(host);
    if (!host->isProgrammed()) {
      // new host has been created
      if (addr.isV4()) {
        host->setLookupClassId(BcmAclEntry::kLocalIp4DstClassL3Id);
      } else {
        host->setLookupClassId(BcmAclEntry::kLocalIp6DstClassL3Id);
      }
      host->programToCPU(bcmIfId_);
    }
    hosts.emplace(std::move(host));
  }
  // create new host entries and discard updated or deleted ones,
  // existing host entries are not "deleted", they carry over.
  hosts_ = std::move(hosts);

  // all new info have been programmed, store the interface configuration
  intf_ = intf;
  XLOG(DBG3) << "updated L3 interface " << bcmIfId_ << " for interface ID "
             << intf->getID();
}

void BcmIntf::programIngressIfNeeded(
    const std::shared_ptr<Interface>& intf,
    bool replace) {
  if (!hw_->getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::INGRESS_L3_INTERFACE)) {
    return;
  }
  bcm_l3_ingress_t l3_ingress;
  bcm_l3_ingress_t_init(&l3_ingress);
  l3_ingress.flags = BCM_L3_INGRESS_WITH_ID;
  if (replace) {
    l3_ingress.flags |= BCM_L3_INGRESS_REPLACE;
  }
  bcm_if_t ingress_if = bcmIfId_;
  l3_ingress.vrf = BcmSwitch::getBcmVrfId(RouterID(0));
  auto rc = bcm_l3_ingress_create(hw_->getUnit(), &l3_ingress, &ingress_if);
  bcmCheckError(
      rc, "failed to create or update ingress L3 interface ", intf->getID());
  bcmIngIfId_ = ingress_if;

  bcm_vlan_control_vlan_t vlan_ctrl;
  bcm_vlan_control_vlan_t_init(&vlan_ctrl);
  bcm_vlan_control_vlan_get(hw_->getUnit(), intf->getVlanID(), &vlan_ctrl);
  vlan_ctrl.ingress_if = ingress_if;
  rc = bcm_vlan_control_vlan_set(hw_->getUnit(), intf->getVlanID(), vlan_ctrl);
  bcmCheckError(rc, "failed to set vlan control ", intf->getVlanID());
}

BcmIntf::~BcmIntf() {
  if (bcmIfId_ == INVALID) {
    return;
  }
  // remove all BcmHost objects
  hosts_.clear();
  bcm_l3_intf_t ifParams;
  bcm_l3_intf_t_init(&ifParams);
  ifParams.l3a_intf_id = bcmIfId_;
  ifParams.l3a_flags |= BCM_L3_WITH_ID;
  auto rc = bcm_l3_intf_delete(hw_->getUnit(), &ifParams);
  bcmLogFatal(rc, hw_, "failed to delete L3 interface ", bcmIfId_);
  XLOG(DBG3) << "deleted L3 interface " << bcmIfId_;
  if (bcmIngIfId_ != INVALID) {
    rc = bcm_l3_ingress_destroy(hw_->getUnit(), bcmIfId_);
    bcmCheckError(rc, "failed to destroy ingress L3 interface ", bcmIfId_);
  }
}

std::shared_ptr<BcmLabeledTunnel> BcmIntf::getBcmLabeledTunnel(
    const LabelForwardingAction::LabelStack& stack) {
  std::shared_ptr<BcmLabeledTunnel> tunnel;
  bool created = false;
  std::tie(tunnel, created) =
      labelStack2MplsTunnel_.refOrEmplace(stack, hw_, bcmIfId_, stack);
  if (created) {
    XLOG(DBG3) << "created MPLS tunnel " << tunnel->str()
               << " over L3 interface " << getBcmIfId();
  } else {
    XLOG(DBG3) << "referenced MPLS tunnel " << tunnel->str()
               << " over L3 interface " << getBcmIfId();
  }
  return tunnel;
}

long BcmIntf::getLabeledTunnelRefCount(
    const LabelForwardingAction::LabelStack& stack) const {
  return labelStack2MplsTunnel_.referenceCount(stack);
}

BcmIntfTable::BcmIntfTable(BcmSwitch* hw) : hw_(hw) {}

BcmIntfTable::~BcmIntfTable() {}

BcmIntf* BcmIntfTable::getBcmIntfIf(bcm_if_t id) const {
  auto iter = bcmIntfs_.find(id);
  if (iter == bcmIntfs_.end()) {
    return nullptr;
  }
  return iter->second;
}

BcmIntf* BcmIntfTable::getBcmIntf(bcm_if_t id) const {
  auto ptr = getBcmIntfIf(id);
  if (ptr == nullptr) {
    throw FbossError("Cannot find interface ", id);
  }
  return ptr;
}

BcmIntf* BcmIntfTable::getBcmIntfIf(InterfaceID id) const {
  auto iter = intfs_.find(id);
  if (iter == intfs_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

BcmIntf* BcmIntfTable::getBcmIntf(InterfaceID id) const {
  auto ptr = getBcmIntfIf(id);
  if (ptr == nullptr) {
    throw FbossError("Cannot find interface ", id);
  }
  return ptr;
}

void BcmIntfTable::addIntf(const shared_ptr<Interface>& intf) {
  auto newIntf = unique_ptr<BcmIntf>(new BcmIntf(hw_));
  auto intfPtr = newIntf.get();
  auto ret = intfs_.insert(make_pair(intf->getID(), std::move(newIntf)));
  if (!ret.second) {
    throw FbossError("Adding an existing interface ", intf->getID());
  }
  intfPtr->program(intf);
  auto ret2 = bcmIntfs_.insert(make_pair(intfPtr->getBcmIfId(), intfPtr));
  CHECK_EQ(ret2.second, true);
}

void BcmIntfTable::programIntf(const shared_ptr<Interface>& intf) {
  auto intfPtr = getBcmIntf(intf->getID());
  intfPtr->program(intf);
}

void BcmIntfTable::deleteIntf(const std::shared_ptr<Interface>& intf) {
  auto iter = intfs_.find(intf->getID());
  if (iter == intfs_.end()) {
    throw FbossError(
        "Failed to delete a non-existing interface ", intf->getID());
  }
  auto bcmIfId = iter->second->getBcmIfId();
  intfs_.erase(iter);
  bcmIntfs_.erase(bcmIfId);
}

} // namespace facebook::fboss
