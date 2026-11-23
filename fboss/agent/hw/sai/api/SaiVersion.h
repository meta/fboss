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

#ifndef IS_OSS

#if !defined(SAI_VERSION)
#define SAI_VERSION(major, minor, micro) \
  (100000 * (major) + 1000 * (minor) + (micro))
#endif

#if !defined(SAI_API_VERSION)
#if (!defined(SAI_VER_MAJOR)) || (!defined(SAI_VER_MINOR)) || \
    (!defined(SAI_VER_RELEASE))
#error "SAI version not defined"
#endif

#define SAI_API_VERSION \
  SAI_VERSION(SAI_VER_MAJOR, SAI_VER_MINOR, SAI_VER_RELEASE)
#endif

#endif

/*
 *  Aggregate SDK Version flags
 *
 *  Purpose of this is try to minimize the SDK version flags scattering around
 * the code base. It's error prone and hard to maintain.
 *
*  Apart from these, individual flags are available for versions greater than
 * certain SDK. They will be useful for features being supported since a
 * particular SDK version.

 *  High-level BCM-SDK flags:
 *   - BRCM_SAI_SDK_XGS: Flags for all XGS SDK versions
 *   - BRCM_SAI_SDK_DNX: Flags for DNX SDK versions
 *   - BRCM_SAI_SDK_XGS_AND_DNX: Flags for all BRCM-SAI SDK versions
 */
#if defined(SAI_VERSION_8_2_0_0_ODP) ||                                        \
    defined(SAI_VERSION_8_2_0_0_SIM_ODP) ||                                    \
    defined(SAI_VERSION_9_2_0_0_ODP) || defined(SAI_VERSION_9_0_EA_SIM_ODP) || \
    defined(SAI_VERSION_10_0_EA_ODP) ||                                        \
    defined(SAI_VERSION_10_0_EA_SIM_ODP) ||                                    \
    defined(SAI_VERSION_10_2_0_0_ODP) || defined(SAI_VERSION_11_0_EA_ODP) ||   \
    defined(SAI_VERSION_11_0_EA_SIM_ODP) ||                                    \
    defined(SAI_VERSION_11_3_0_0_ODP) || defined(SAI_VERSION_11_7_0_0_ODP) ||  \
    defined(SAI_VERSION_12_2_0_0_ODP) || defined(SAI_VERSION_13_0_EA_ODP) ||   \
    defined(SAI_VERSION_13_3_0_0_ODP) || defined(SAI_VERSION_14_0_EA_ODP) ||   \
    defined(SAI_VERSION_14_2_0_0_ODP) || defined(SAI_VERSION_15_0_EA_ODP) ||   \
    defined(SAI_VERSION_15_4_EA_ODP) || defined(SAI_VERSION_15_4_0_0_ODP) ||   \
    defined(SAI_VERSION_16_0_EA_ODP)
#define BRCM_SAI_SDK_XGS
#endif

#if defined(SAI_VERSION_10_0_EA_DNX_ODP) ||     \
    defined(SAI_VERSION_10_0_EA_DNX_SIM_ODP) || \
    defined(SAI_VERSION_11_0_EA_DNX_ODP) ||     \
    defined(SAI_VERSION_11_0_EA_DNX_SIM_ODP) || \
    defined(SAI_VERSION_11_3_0_0_DNX_ODP) ||    \
    defined(SAI_VERSION_11_7_0_0_DNX_ODP) ||    \
    defined(SAI_VERSION_12_0_EA_DNX_ODP) ||     \
    defined(SAI_VERSION_12_2_0_0_DNX_ODP) ||    \
    defined(SAI_VERSION_13_0_EA_DNX_ODP) ||     \
    defined(SAI_VERSION_13_3_0_0_DNX_ODP) ||    \
    defined(SAI_VERSION_14_0_EA_DNX_ODP) ||     \
    defined(SAI_VERSION_14_2_0_0_DNX_ODP) ||    \
    defined(SAI_VERSION_15_0_EA_DNX_ODP) ||     \
    defined(SAI_VERSION_16_0_EA_DNX_ODP)
#define BRCM_SAI_SDK_DNX
#endif

#if defined(BRCM_SAI_SDK_XGS) || defined(BRCM_SAI_SDK_DNX)
#define BRCM_SAI_SDK_XGS_AND_DNX
#endif

#if defined(SAI_VERSION_16_0_EA_DNX_ODP) || defined(SAI_VERSION_16_0_EA_ODP)
#define BRCM_SAI_SDK_GTE_16_0
#endif

#if defined(BRCM_SAI_SDK_GTE_16_0) || defined(SAI_VERSION_15_4_EA_ODP) || \
    defined(SAI_VERSION_15_4_0_0_ODP)
#define BRCM_SAI_SDK_GTE_15_4
#endif

#if defined(BRCM_SAI_SDK_GTE_15_4) || defined(SAI_VERSION_15_0_EA_ODP) || \
    defined(SAI_VERSION_15_0_EA_DNX_ODP)
#define BRCM_SAI_SDK_GTE_15_0
#endif

#if defined(BRCM_SAI_SDK_GTE_15_0) || defined(SAI_VERSION_14_0_EA_ODP) || \
    defined(SAI_VERSION_14_0_EA_DNX_ODP) ||                               \
    defined(SAI_VERSION_14_2_0_0_ODP) || defined(SAI_VERSION_14_2_0_0_DNX_ODP)
#define BRCM_SAI_SDK_GTE_14_0
#endif

#if defined BRCM_SAI_SDK_GTE_14_0 || defined(SAI_VERSION_13_0_EA_ODP) || \
    defined(SAI_VERSION_13_0_EA_DNX_ODP) ||                              \
    defined(SAI_VERSION_13_3_0_0_ODP) || defined(SAI_VERSION_13_3_0_0_DNX_ODP)
#define BRCM_SAI_SDK_GTE_13_0
#endif

#if defined(BRCM_SAI_SDK_GTE_13_0) || defined(SAI_VERSION_12_0_EA_DNX_ODP) || \
    defined(SAI_VERSION_12_2_0_0_DNX_ODP) || defined(SAI_VERSION_12_2_0_0_ODP)
#define BRCM_SAI_SDK_GTE_12_0
#endif

#if defined(SAI_VERSION_16_0_EA_DNX_ODP)
#define BRCM_SAI_SDK_DNX_GTE_16_0
#endif

#if defined(BRCM_SAI_SDK_DNX_GTE_16_0) || defined(SAI_VERSION_15_0_EA_DNX_ODP)
#define BRCM_SAI_SDK_DNX_GTE_15_0
#endif

#if defined(BRCM_SAI_SDK_DNX_GTE_15_0) ||   \
    defined(SAI_VERSION_14_0_EA_DNX_ODP) || \
    defined(SAI_VERSION_14_2_0_0_DNX_ODP)
#define BRCM_SAI_SDK_DNX_GTE_14_0
#endif

#if defined(BRCM_SAI_SDK_DNX_GTE_14_0) ||   \
    defined(SAI_VERSION_13_0_EA_DNX_ODP) || \
    defined(SAI_VERSION_13_3_0_0_DNX_ODP)
#define BRCM_SAI_SDK_DNX_GTE_13_0
#endif

#if defined(SAI_VERSION_16_0_EA_ODP)
#define BRCM_SAI_SDK_XGS_GTE_16_0
#endif

// brcm-sai 15.4.0.0_odp is Broadcom's 15.4.1 GA drop.
#if defined(BRCM_SAI_SDK_XGS_GTE_16_0) || defined(SAI_VERSION_15_4_0_0_ODP)
#define BRCM_SAI_SDK_XGS_GTE_15_4_1
#endif

// CBFC (saivirtualchannel.h, the CBFC port attributes, the VC qos map types)
// is upstream as of SAI 1.19.0. brcm-sai 16.0_ea_odp carries the same
// declarations via its cbfc.patch while still reporting 1.18.1, so neither
// check alone covers both.
//
// TODO(agrewal): delete SAI_CBFC_SUPPORTED once Broadcom's 16.0 drop is
// rebased onto the 1.19 tag and reports it. Every CBFC use can then gate on
// the spec version alone, as other upstream features do. Note the endpoint is
// the version check, not BRCM_SAI_SDK_XGS_GTE_16_0 on its own: fake SAI
// compiles the same files and never defines that macro.
//
// Compared on SAI_VER_MAJOR/MINOR rather than SAI_API_VERSION: those are plain
// -D values supplied by both the Buck config and the OSS cmake, whereas
// SAI_VERSION is only defined above under #ifndef IS_OSS, so using it here
// breaks the OSS build.
#if defined(BRCM_SAI_SDK_XGS_GTE_16_0)
#define SAI_CBFC_SUPPORTED
#elif defined(SAI_VER_MAJOR) && defined(SAI_VER_MINOR) && \
    (SAI_VER_MAJOR > 1 || (SAI_VER_MAJOR == 1 && SAI_VER_MINOR >= 19))
#define SAI_CBFC_SUPPORTED
#endif

#if defined(BRCM_SAI_SDK_XGS_GTE_16_0) || defined(SAI_VERSION_15_0_EA_ODP) || \
    defined(SAI_VERSION_15_4_EA_ODP) || defined(SAI_VERSION_15_4_0_0_ODP)
#define BRCM_SAI_SDK_XGS_GTE_15_0
#endif

#if defined(BRCM_SAI_SDK_XGS_GTE_15_0) || defined(SAI_VERSION_14_2_0_0_ODP)
#define BRCM_SAI_SDK_XGS_GTE_14_2
#endif

#if defined(BRCM_SAI_SDK_XGS_GTE_15_0) || defined(SAI_VERSION_14_0_EA_ODP) || \
    defined(SAI_VERSION_14_2_0_0_ODP)
#define BRCM_SAI_SDK_XGS_GTE_14_0
#endif

#if defined(BRCM_SAI_SDK_XGS_GTE_14_0) || defined(SAI_VERSION_13_0_EA_ODP) || \
    defined(SAI_VERSION_14_0_EA_ODP) || defined(SAI_VERSION_13_3_0_0_ODP)
#define BRCM_SAI_SDK_XGS_GTE_13_0
#endif

#if defined(BRCM_SAI_SDK_DNX_GTE_13_0) ||   \
    defined(SAI_VERSION_12_0_EA_DNX_ODP) || \
    defined(SAI_VERSION_12_2_0_0_DNX_ODP)
#define BRCM_SAI_SDK_DNX_GTE_12_0
#endif

// SDK dump rate limiting (CS00012465650). The OSS 14.2 DNX SDK artifact is
// built from an older drop than tp2 and lacks the SAI attribute ids.
//
// TODO: drop the IS_OSS clause once the OSS 14.2 DNX SDK artifact is rebuilt
// from current tp2.
#if defined(SAI_VERSION_12_2_0_0_DNX_ODP) || \
    (defined(BRCM_SAI_SDK_DNX_GTE_12_0) && !defined(IS_OSS))
#define SAI_SDK_DUMP_RATE_LIMIT_SUPPORTED
#endif

#if defined BRCM_SAI_SDK_GTE_12_0 || defined(SAI_VERSION_11_0_EA_ODP) || \
    defined(SAI_VERSION_11_0_EA_SIM_ODP) ||                              \
    defined(SAI_VERSION_11_0_EA_DNX_ODP) ||                              \
    defined(SAI_VERSION_11_0_EA_DNX_SIM_ODP) ||                          \
    defined(SAI_VERSION_11_3_0_0_DNX_ODP) ||                             \
    defined(SAI_VERSION_11_3_0_0_ODP) ||                                 \
    defined(SAI_VERSION_11_7_0_0_DNX_ODP) || defined(SAI_VERSION_11_7_0_0_ODP)
#define BRCM_SAI_SDK_GTE_11_0
#endif

#if defined BRCM_SAI_SDK_DNX_GTE_12_0 ||     \
    defined(SAI_VERSION_11_0_EA_DNX_ODP) ||  \
    defined(SAI_VERSION_11_3_0_0_DNX_ODP) || \
    defined(SAI_VERSION_11_7_0_0_DNX_ODP)
#define BRCM_SAI_SDK_DNX_GTE_11_0
#endif

#if defined BRCM_SAI_SDK_DNX_GTE_12_0 || defined(SAI_VERSION_11_7_0_0_DNX_ODP)
#define BRCM_SAI_SDK_DNX_GTE_11_7
#endif

#if defined BRCM_SAI_SDK_GTE_11_0 || defined(SAI_VERSION_10_0_EA_ODP) || \
    defined(SAI_VERSION_10_0_EA_SIM_ODP) ||                              \
    defined(SAI_VERSION_10_0_EA_DNX_ODP) ||                              \
    defined(SAI_VERSION_10_0_EA_DNX_SIM_ODP) ||                          \
    defined(SAI_VERSION_10_2_0_0_ODP)
#define BRCM_SAI_SDK_GTE_10_0
#endif

#if defined(BRCM_SAI_SDK_GTE_10_0) || defined BRCM_SAI_GTE_11_0 || \
    defined(SAI_VERSION_9_2_0_0_ODP) || defined(SAI_VERSION_9_0_EA_SIM_ODP)
#define BRCM_SAI_SDK_GTE_9_2
#endif

/*
 *  High-level TAJO flags:
 *   - TAJO_SDK_EBRO: Flags for all EBRO(GB) SDK
 *   - TAJO_SDK_MORGAN: Flags for Morgan(G200) SDK
 *   - TAJO_P4_WB_SDK: Flags for P4 WB SDK
 *   - TAJO_SDK_GTE_24_8_3001: Flags for SDK >= 24.8.3001
 *   - TAJO_SDK: Flags for all TAJO SDK
 */

/*
 * TAJO_SDK_GTE_24_8_3001 can do P4 Warmboot,
 * but it does not support switching between P4 Warmboot and normal Warmboot.
 * Do not enable the P4 WB SDK until a future SDK release with the fix.
 */

#if defined(TAJO_SDK_VERSION_1_42_8) || defined(TAJO_SDK_VERSION_24_8_3001) || \
    defined(TAJO_SDK_VERSION_25_5_4210) ||                                     \
    defined(TAJO_SDK_VERSION_25_11_4210) ||                                    \
    defined(TAJO_SDK_VERSION_26_2_4210) ||                                     \
    defined(TAJO_SDK_VERSION_26_5_4210) ||                                     \
    defined(TAJO_SDK_VERSION_26_5_5210) || defined(TAJO_SDK_VERSION_26_7_5211)
#define TAJO_SDK_EBRO
#endif

#if defined(TAJO_SDK_VERSION_26_5_4210) || \
    defined(TAJO_SDK_VERSION_26_5_5210) || defined(TAJO_SDK_VERSION_26_7_5211)
#define TAJO_SDK_GTE_26_5
#endif

#if defined(TAJO_SDK_VERSION_26_2_4210) || defined(TAJO_SDK_GTE_26_5)
#define TAJO_SDK_GTE_26_2
#endif

#if defined(TAJO_SDK_VERSION_25_5_4210) || \
    defined(TAJO_SDK_VERSION_25_11_4210) || defined(TAJO_SDK_GTE_26_2)
#define TAJO_SDK_GTE_25_5
#endif

#if defined(TAJO_SDK_VERSION_24_8_3001) || defined(TAJO_SDK_GTE_25_5)
#define TAJO_SDK_GTE_24_8_3001
#endif

#if defined(TAJO_SDK_EBRO) || defined(TAJO_SDK_MORGAN)
#define TAJO_SAI_SDK
#endif

#if defined(TAJO_SDK_VERSION_26_7_5211)
#define TAJO_SDK_P200
#endif

/*
 *  High-level Chenab (NVIDIA) flags:
 *   - CHENAB_SAI_SDK: Flags for all Chenab SDK (set in tp2 BUCK)
 *   - CHENAB_SAI_SDK_GTE_2511_36: Flags for SDK >= 2511.36
 *   - CHENAB_SAI_SDK_GTE_2605_37: Flags for SDK >= 2605.37
 *   - CHENAB_SAI_SDK_GTE_2605_38: Flags for SDK >= 2605.38
 */
#if defined(CHENAB_SAI_SDK_VERSION_2605_38_251_4)
#define CHENAB_SAI_SDK_GTE_2605_38
#endif

#if defined(CHENAB_SAI_SDK_VERSION_2605_37_0_20) || \
    defined(CHENAB_SAI_SDK_GTE_2605_38)
#define CHENAB_SAI_SDK_GTE_2605_37
#endif

#if defined(CHENAB_SAI_SDK_VERSION_2511_36_0_20) ||   \
    defined(CHENAB_SAI_SDK_VERSION_2511_36_251_10) || \
    defined(CHENAB_SAI_SDK_GTE_2605_37)
#define CHENAB_SAI_SDK_GTE_2511_36
#endif
