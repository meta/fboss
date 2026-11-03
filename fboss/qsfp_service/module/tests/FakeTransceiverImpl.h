// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/qsfp_service/module/TransceiverImpl.h"

#include <array>
#include <map>
#include <utility>

namespace facebook {
namespace fboss {

class TransceiverManager;

// This class contains a fake implementation of a transceiver. It overrides the
// readTransceiver, writeTransceiver and some other methods. It uses a fake
// eeprom map, the reads read from the map and the writes modify the map.
class FakeTransceiverImpl : public TransceiverImpl {
 public:
  FakeTransceiverImpl(
      int module,
      const std::map<uint8_t, std::array<uint8_t, 128>>& lowerPage,
      const std::map<uint8_t, std::map<int, std::array<uint8_t, 128>>>&
          upperPages,
      TransceiverManager* mgr) {
    module_ = module;
    moduleName_ = folly::to<std::string>(module);
    lowerPages_.insert(lowerPage.begin(), lowerPage.end());
    upperPages_.insert(upperPages.begin(), upperPages.end());
    tcvrManager_ = mgr;
  }
  /* This function is used to read the SFP EEprom */
  int readTransceiver(
      const TransceiverAccessParameter& param,
      uint8_t* fieldValue,
      const int field) override;
  /* write to the eeprom (usually to change the page setting) */
  int writeTransceiver(
      const TransceiverAccessParameter& param,
      const uint8_t* fieldValue,
      uint64_t sleep,
      const int field) override;
  /* This function detects if a SFP is present on the particular port */
  bool detectTransceiver() override;
  /* Returns the name for the port */
  folly::StringPiece getName() override;
  int getNum() const override;
  void triggerQsfpHardReset() override;
  void updateTransceiverState(TransceiverStateMachineEvent event) override;

  // Writes seen at (page, offset-within-the-upper-page), summed over all banks
  int getUpperPageWriteCount(int page, int offset) const {
    auto it = upperPageWriteCounts_.find(std::make_pair(page, offset));
    return it == upperPageWriteCounts_.end() ? 0 : it->second;
  }

 protected:
  // Provide distinct EEPROM contents for a specific (bank, page) so multi-bank
  // (CPO) reads can be exercised. When the bank-select register (byte 126) is
  // set to a bank registered here, reads of that page return this data;
  // otherwise reads fall back to the bank-agnostic upperPages_ contents.
  void setBankedPage(uint8_t bank, int page, std::array<uint8_t, 128> data) {
    bankedPages_[bank][page] = data;
  }

  // Simulate AppSel-dependent datapath behavior: applying stage set 0 updates
  // the active control set, and lanes without an active application stay
  // deactivated when released from deinit.
  void setAppSelGatedDatapath() {
    appSelGatedDatapath_ = true;
  }

 private:
  int module_{0};
  std::string moduleName_;
  int page_{0};
  uint8_t selectedBank_{0};
  std::map<uint8_t, std::map<int, std::array<uint8_t, 128>>> upperPages_;
  // Optional per-bank page overrides, indexed [bank][page]. Empty by default,
  // so single-bank fixtures behave exactly as before.
  std::map<uint8_t, std::map<int, std::array<uint8_t, 128>>> bankedPages_;
  // Write counts keyed by (page, offset within the upper page)
  std::map<std::pair<int, int>, int> upperPageWriteCounts_;
  bool appSelGatedDatapath_{false};
  std::map<uint8_t, std::array<uint8_t, 128>> lowerPages_;
  TransceiverManager* tcvrManager_;
};

class SffDacTransceiver : public FakeTransceiverImpl {
 public:
  explicit SffDacTransceiver(int module, TransceiverManager* mgr);
};

class SffCwdm4Transceiver : public FakeTransceiverImpl {
 public:
  explicit SffCwdm4Transceiver(int module, TransceiverManager* mgr);
};

class SffFr1Transceiver : public FakeTransceiverImpl {
 public:
  explicit SffFr1Transceiver(int module, TransceiverManager* mgr);
};

class Sff200GCr4Transceiver : public FakeTransceiverImpl {
 public:
  explicit Sff200GCr4Transceiver(int module, TransceiverManager* mgr);
};

class BadSffCwdm4Transceiver : public FakeTransceiverImpl {
 public:
  explicit BadSffCwdm4Transceiver(int module, TransceiverManager* mgr);
};

class BadEepromSffCwdm4Transceiver : public FakeTransceiverImpl {
 public:
  explicit BadEepromSffCwdm4Transceiver(int module, TransceiverManager* mgr);
};

class MiniphotonOBOTransceiver : public FakeTransceiverImpl {
 public:
  explicit MiniphotonOBOTransceiver(int module, TransceiverManager* mgr);
};

class UnknownModuleIdentifierTransceiver : public FakeTransceiverImpl {
 public:
  explicit UnknownModuleIdentifierTransceiver(
      int module,
      TransceiverManager* mgr);
};

class Cmis200GTransceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis200GTransceiver(int module, TransceiverManager* mgr);
};

// Cmis200G variant whose Lower Page byte 3 sets the reserved bits 4-7 on top
// of ModuleState=READY in bits 1-3. Real modules do this, and the state has to
// be masked out of the byte rather than just shifted.
class Cmis200GReservedStateBitsTransceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis200GReservedStateBitsTransceiver(
      int module,
      TransceiverManager* mgr);
};

// Cmis200G variant whose bank select register (Lower Page byte 126) comes up
// holding a bank this single-bank module doesn't have.
class Cmis200GInvalidBankSelectTransceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis200GInvalidBankSelectTransceiver(
      int module,
      TransceiverManager* mgr);
};

class BadCmis200GTransceiver : public FakeTransceiverImpl {
 public:
  explicit BadCmis200GTransceiver(int module, TransceiverManager* mgr);
};

class Cmis400GLr4Transceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis400GLr4Transceiver(int module, TransceiverManager* mgr);
};

class CmisFlatMemTransceiver : public FakeTransceiverImpl {
 public:
  explicit CmisFlatMemTransceiver(int module, TransceiverManager* mgr);
};

class Sfp10GTransceiver : public FakeTransceiverImpl {
 public:
  explicit Sfp10GTransceiver(int module, TransceiverManager* mgr);
};

class Sfp10GBaseTTransceiver : public FakeTransceiverImpl {
 public:
  explicit Sfp10GBaseTTransceiver(int module, TransceiverManager* mgr);
};

class Cmis400GCr8Transceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis400GCr8Transceiver(int module, TransceiverManager* mgr);
};

class Cmis400GFr4MultiPortTransceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis400GFr4MultiPortTransceiver(int module, TransceiverManager* mgr);
};

class Cmis2x400GFr4Transceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis2x400GFr4Transceiver(int module, TransceiverManager* mgr);
};

class Cmis2x400GFr4LiteTransceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis2x400GFr4LiteTransceiver(int module, TransceiverManager* mgr);
};

class Cmis2x400GFr4LpoTransceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis2x400GFr4LpoTransceiver(int module, TransceiverManager* mgr);
};

class Cmis800GZrTransceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis800GZrTransceiver(int module, TransceiverManager* mgr);
};

// Cmis800GZr variant that also advertises the 3x200G FlexO-6e-DPO application
// (AppSel 9) and simulates AppSel-dependent datapath state.
class Cmis800GZr3x200GTransceiver : public Cmis800GZrTransceiver {
 public:
  explicit Cmis800GZr3x200GTransceiver(int module, TransceiverManager* mgr);
};

// Cmis800GZr variant that does NOT advertise rxConsActHoldOffTmrImpl support
// (Page 45h, Byte 129, Bit 2 cleared) for testing the unsupported-module path
// of configureRxConsActHoldOffTimer.
class Cmis800GZrNoHoldOffTmrTransceiver : public Cmis800GZrTransceiver {
 public:
  explicit Cmis800GZrNoHoldOffTmrTransceiver(
      int module,
      TransceiverManager* mgr);
};

class Cmis2x400GDr4Transceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis2x400GDr4Transceiver(int module, TransceiverManager* mgr);
};

// The 2km reach (XDR4) variant of the above. XDR4 has no media interface code
// of its own, so it advertises the same 400G-DR4 application and differs only
// in the SMF length (Page 01h byte 132).
class Cmis2x400GXdr4Transceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis2x400GXdr4Transceiver(int module, TransceiverManager* mgr);
};

// Custom transceiver for testing CWDM4_100G temperature thresholds
class SffCwdm4TempTransceiver : public FakeTransceiverImpl {
 public:
  explicit SffCwdm4TempTransceiver(int module, TransceiverManager* mgr);

  // Set the temperature value in the lower page
  void setTemperature(double tempValue);
};

class Cmis2x400GFr4WithMpiAlarmsTransceiver : public Cmis2x400GFr4Transceiver {
 public:
  explicit Cmis2x400GFr4WithMpiAlarmsTransceiver(
      int module,
      TransceiverManager* mgr);
};

class Cmis400GDr4Transceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis400GDr4Transceiver(int module, TransceiverManager* mgr);
};

class Cmis2x800GDr4Transceiver : public FakeTransceiverImpl {
 public:
  explicit Cmis2x800GDr4Transceiver(int module, TransceiverManager* mgr);
};

// Cmis2x800GDr4 variant advertising all three Meta custom features in Page 01h
// Byte 191 (CMIS 5.1). Used to exercise the mode-mismatch / thermal-margin
// feature handling on a non-ZR module built to the Meta FW spec.
class Cmis2x800GDr4CustomFeatureTransceiver : public Cmis2x800GDr4Transceiver {
 public:
  explicit Cmis2x800GDr4CustomFeatureTransceiver(
      int module,
      TransceiverManager* mgr);
};

// Same, but reporting CMIS 5.0. The Meta FW spec that gives Byte 191 its
// meaning requires CMIS >= 5.1, so below that revision the byte must be
// ignored.
class Cmis2x800GDr4Cmis50Transceiver
    : public Cmis2x800GDr4CustomFeatureTransceiver {
 public:
  explicit Cmis2x800GDr4Cmis50Transceiver(int module, TransceiverManager* mgr);
};

// Cmis2x800GDr4 custom-feature variant running out of thermal headroom and
// mismatched against the host: all three Meta custom latched flags (Lower
// Memory Byte 67) are asserted, both thermal margins (Bytes 68-69) are
// negative, and Page 14h Bytes 130-131 flag per-lane mode mismatches.
class Cmis2x800GDr4NegativeMarginTransceiver
    : public Cmis2x800GDr4CustomFeatureTransceiver {
 public:
  explicit Cmis2x800GDr4NegativeMarginTransceiver(
      int module,
      TransceiverManager* mgr);
};

// Real EEPROM dumps from deployed Arista XDR4 modules -- the parts that serve
// zeros for up to 100ms after DIAG_SEL changes (T224486560). Both were captured
// while qsfp_service had them selected on SNR, so page 14h byte 0 reads back
// as 6.
class CmisArista400GXdr4Transceiver : public FakeTransceiverImpl {
 public:
  explicit CmisArista400GXdr4Transceiver(int module, TransceiverManager* mgr);
};

class CmisArista2x400GXdr4Transceiver : public FakeTransceiverImpl {
 public:
  explicit CmisArista2x400GXdr4Transceiver(int module, TransceiverManager* mgr);
};

class CmisCpo6P4TDrTransceiver : public FakeTransceiverImpl {
 public:
  explicit CmisCpo6P4TDrTransceiver(int module, TransceiverManager* mgr);
};

// CPO module in READY state with distinct per-bank page 14h, for exercising the
// per-bank SNR (14h) read path.
class CmisCpo6P4TDrReadyTransceiver : public CmisCpo6P4TDrTransceiver {
 public:
  explicit CmisCpo6P4TDrReadyTransceiver(int module, TransceiverManager* mgr);
};

class CmisCredo800AEC : public FakeTransceiverImpl {
 public:
  explicit CmisCredo800AEC(int module, TransceiverManager* mgr);
};

// 200G/lane active electrical cable: advertises the Annex 176E host interface
// block (200GAUI-1 / 400GAUI-2 / 800GAUI-4 / 1.6TAUI-8) rather than the
// 100G/lane codes the 800G part uses. Comes up in its 8x200G default.
class Cmis1600GAec : public FakeTransceiverImpl {
 public:
  explicit Cmis1600GAec(int module, TransceiverManager* mgr);
};

class InvalidDatapathLaneStateTransceiver : public FakeTransceiverImpl {
 public:
  explicit InvalidDatapathLaneStateTransceiver(
      int module,
      TransceiverManager* mgr);
};

} // namespace fboss
} // namespace facebook
