// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/PortApi.h"

extern "C" {
#include <sai.h>

#if defined(TAJO_SDK_GTE_26_5)
#include <saiextensions.h>
#include <saiportcustom.h>
#else
#include <experimental/sai_attr_ext.h>
#endif
}

#if defined(TAJO_SDK_GTE_24_8_3001)
#define RETURN_SUPPORTED_ATTR(attr) return (attr);
#else
#define RETURN_SUPPORTED_ATTR(attr) return std::nullopt;
#endif

namespace facebook::fboss {
std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxLutModeIdWrapper::operator()() {
  return SAI_PORT_SERDES_ATTR_EXT_TX_LUT_MODE;
}
std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxCtleCodeIdWrapper::operator()() {
  return SAI_PORT_SERDES_ATTR_EXT_RX_CTLE_CODE;
}
std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxDspModeIdWrapper::operator()() {
  return SAI_PORT_SERDES_ATTR_EXT_RX_DSP_MODE;
}
std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxAfeTrimIdWrapper::operator()() {
  return SAI_PORT_SERDES_ATTR_EXT_RX_AFE_TRIM;
}
std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxAcCouplingBypassIdWrapper::operator()() {
  return SAI_PORT_SERDES_ATTR_EXT_RX_AC_COUPLING_BYPASS;
}
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeSystemPortId::operator()() {
  return SAI_PORT_ATTR_EXT_SYSTEM_PORT_ID;
}
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeAttachedCoreIndex::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeAttachedCorePortIndex::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxAfeAdaptiveEnableWrapper::operator()() {
  return SAI_PORT_SERDES_ATTR_EXT_RX_AFE_ADAPTIVE_ENABLE;
}
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeSerdesLaneList::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeDiagModeEnable::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeRxLaneSquelchEnable::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRVgaWrapper::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxReachWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeTransmitPrecodingStateWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeReceivePrecodingStateWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeDcoWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeFltMWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeFltSWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxPfWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxPfLfqWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxPfHfqWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxEqP2Wrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxEqP1Wrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxEqMWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxEq1Wrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxEq2Wrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxEq3Wrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxTap2Wrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxTap1Wrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTpChn2Wrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTpChn1Wrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTpChn0Wrapper::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeFdrEnable::operator()() {
  return std::nullopt;
}
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeCrcErrorDetect::operator()() {
  return std::nullopt;
}
#endif
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeCablePropogationDelayNS::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeFabricDataCellsFilterStatus::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeReachabilityGroup::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeFecErrorDetectEnable::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributePfcMonitorDirection::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeAmIdles::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeResetQueueCreditBalance::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributePgDropStatus::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxDiffEncoderEnWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_TX_DIFF_ENCODER_EN);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxDigGainWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_TX_DIG_GAIN);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxFfeCoeff0Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_0);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxFfeCoeff1Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_1);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxFfeCoeff2Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_2);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxFfeCoeff3Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_3);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxFfeCoeff4Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_4);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxDriverSwingWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_TX_DRIVER_SWING);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeTxLdoBypassWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_TX_LDO_BYPASS);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgBoost1StartWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST1_STRT);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgBoost1StepWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST1_STEP);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgBoost1StopWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST1_STOP);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgBoost2OrHrStartWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST2_OR_HR_STRT);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgBoost2OrHrStepWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST2_OR_HR_STEP);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgBoost2OrHrStopWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST2_OR_HR_STOP);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgC1Start1p7Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_C1_START_1P7);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgC1Step1p7Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_C1_STEP_1P7);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgC1Stop1p7Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_C1_STOP_1P7);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgDfeStart1p7Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_DFE_START_1P7);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgDfeStep1p7Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_DFE_STEP_1P7);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgDfeStop1p7Wrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_DFE_STOP_1P7);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxEnableScanSelectionWrapper ::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_ENABLE_SCAN);
}
std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgScanUseSrSettingsWrapper ::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_SCAN_USE_SR_SETTINGS);
}
std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxCdrCfgOvEnWrapper ::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_CDR_CFG_OV_EN);
}
std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxCdrTdet1stOrdStepOvValWrapper ::operator()() {
  RETURN_SUPPORTED_ATTR(
      SAI_PORT_SERDES_ATTR_EXT_RX_CDR_TDET_1ST_ORD_STEP_OV_VAL);
}
std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxCdrTdet2ndOrdStepOvValWrapper ::operator()() {
  RETURN_SUPPORTED_ATTR(
      SAI_PORT_SERDES_ATTR_EXT_RX_CDR_TDET_2ND_ORD_STEP_OV_VAL);
}
std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxCdrTdetFineStepOvValWrapper ::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_CDR_TDET_FINE_STEP_OV_VAL);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxLdoBypassWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_LDO_BYPASS);
}

std::optional<sai_attr_id_t>
SaiPortSerdesTraits::Attributes::AttributeRxDiffEncoderEnWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_DIFF_ENCODER_EN);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxInstgEnableScanWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_ENABLE_SCAN);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxFfeLengthBitmapWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_FFE_LENGTH_BITMAP);
}

std::optional<sai_attr_id_t> SaiPortSerdesTraits::Attributes::
    AttributeRxFfeLmsDynamicGatingEnWrapper::operator()() {
  RETURN_SUPPORTED_ATTR(SAI_PORT_SERDES_ATTR_EXT_RX_FFE_LMS_DYNAMIC_GATING_EN);
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeCondEntropyRehashEnable::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeCondEntropyRehashPeriodUS::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeCondEntropyRehashSeed::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeShelEnable::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeArsLinkState::operator()() {
  return std::nullopt;
}

const std::vector<sai_stat_id_t>&
SaiPortTraits::macTxDataQueueMinWatermarkStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiPortTraits::macTxDataQueueMaxWatermarkStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeFabricSystemPort::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeStaticModuleId::operator()() {
  return std::nullopt;
}

const std::vector<sai_stat_id_t>& SaiPortTraits::fabricControlRxPacketStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiPortTraits::fabricControlTxPacketStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiPortTraits::pfcXoffTotalDurationStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

// 26.2.4210 dropped SAI_PORT_ATTR_LINK_{UP,DOWN}_DEBOUNCE_RETRIGGER_COUNT and
// replaced them with counters served by get_port_stats. 25.5.4210 still
// serve the attributes, so these lists stay empty there and the attribute
// path below is used instead.
const std::vector<sai_stat_id_t>&
SaiPortTraits::linkDownDebounceRetriggerStats() {
#if defined(TAJO_SDK_VERSION_26_2_4210) || \
    defined(TAJO_SDK_VERSION_26_5_4210) || defined(TAJO_SDK_VERSION_26_5_5210)
  static const std::vector<sai_stat_id_t> stats{
      SAI_PORT_STAT_EXT_LINK_DOWN_DEBOUNCE_RETRIGGER_COUNT};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiPortTraits::linkUpDebounceRetriggerStats() {
#if defined(TAJO_SDK_VERSION_26_2_4210) || \
    defined(TAJO_SDK_VERSION_26_5_4210) || defined(TAJO_SDK_VERSION_26_5_5210)
  static const std::vector<sai_stat_id_t> stats{
      SAI_PORT_STAT_EXT_LINK_UP_DEBOUNCE_RETRIGGER_COUNT};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeIsHyperPortMember::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeHyperPortMemberList::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiPortTraits::Attributes::
    AttributeCablePropagationDelayMediaType::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributePfcPauseDurationOverride::operator()() {
  return std::nullopt;
}

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeExtOperStatusLatch::operator()() {
#if defined(TAJO_SDK_VERSION_25_5_4210)
  return SAI_PORT_ATTR_EXT_OPER_STATUS_LATCH;
#else
  return std::nullopt;
#endif
}
#endif

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeCablePropagationDelayMeasure::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeLinkUpDebouncePeriodMs::operator()() {
#if defined(TAJO_SDK_GTE_26_2) || defined(TAJO_SDK_VERSION_25_5_4210)
  return SAI_PORT_ATTR_LINK_UP_DEBOUNCE_PERIOD_MILLISECONDS;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeLinkDownDebouncePeriodMs::operator()() {
#if defined(TAJO_SDK_GTE_26_2) || defined(TAJO_SDK_VERSION_25_5_4210)
  return SAI_PORT_ATTR_LINK_DOWN_DEBOUNCE_PERIOD_MILLISECONDS;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeLinkUpDebounceRetriggerCount::operator()() {
// 26.2.4210 deprecated this attribute
#if defined(TAJO_SDK_VERSION_25_5_4210)
  return SAI_PORT_ATTR_LINK_UP_DEBOUNCE_RETRIGGER_COUNT;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t> SaiPortTraits::Attributes::
    AttributeLinkDownDebounceRetriggerCount::operator()() {
#if defined(TAJO_SDK_VERSION_25_5_4210)
  return SAI_PORT_ATTR_LINK_DOWN_DEBOUNCE_RETRIGGER_COUNT;
#else
  return std::nullopt;
#endif
}

const std::vector<sai_stat_id_t>& SaiPortTraits::llrExtensionStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeLinkScanMode::operator()() {
  return std::nullopt;
}

// CSIG is delivered in Leaba SDK 26.5.5210.4.
std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeCsigTagActionWrapper::operator()() {
#if defined(TAJO_SDK_GTE_26_5)
  return SAI_PORT_ATTR_CSIG_TAG_ACTION;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiPortTraits::Attributes::AttributeCsigLinkLocatorWrapper::operator()() {
#if defined(TAJO_SDK_GTE_26_5)
  return SAI_PORT_ATTR_CSIG_LINK_LOCATOR;
#else
  return std::nullopt;
#endif
}

} // namespace facebook::fboss
