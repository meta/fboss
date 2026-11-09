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

#include <string>
#include <vector>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

// Helper methods to setup attributes

/* Primitive Types */

// OidAttr not only serializes oid, but also look into the variable mappings
// to check if the oid matches any previously declared variables
std::string oidAttr(const sai_attribute_t* attr_list, int i);

std::string boolAttr(const sai_attribute_t* attr_list, int i);

std::string s8Attr(const sai_attribute_t* attr_list, int i);

std::string u8Attr(const sai_attribute_t* attr_list, int i);

std::string s16Attr(const sai_attribute_t* attr_list, int i);

std::string u16Attr(const sai_attribute_t* attr_list, int i);

std::string s32Attr(const sai_attribute_t* attr_list, int i);

std::string u32Attr(const sai_attribute_t* attr_list, int i);

std::string u64Attr(const sai_attribute_t* attr_list, int i);

/* Various Attribute Types */

void aclEntryActionSaiObjectIdAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryActionU8Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryActionBoolAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryActionU32Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryFieldSaiObjectIdAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryFieldIpV4Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryFieldIpV6Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryFieldU8Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryFieldU16Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryFieldU32Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void aclEntryFieldMacAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void charDataAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void macAddressAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void ipAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void s32RangeAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void u32RangeAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void prbsRxStateAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

/* List Types */

void aclEntryFieldU8ListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

void oidListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

void aclEntryActionSaiObjectIdListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

void s32ListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

void u32ListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

void segmentListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

void qosMapListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

void mapListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
void latchStatusAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

void portLaneLatchStatusListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
void portFrequencyOffsetPpmListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

void portSnrListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
void jsonAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);
#endif

void systemPortConfigListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry = true);

void systemPortConfigAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines);

/* Other types */
void s8ListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool nullable = false);

void u8ArrGenericAttr(
    const uint8_t* array,
    uint arrLength,
    int i,
    std::vector<std::string>& attrLines,
    const std::string& attrName);

std::string attrNameToEnum(
    const std::string& objType,
    const std::string& attrName);

} // namespace facebook::fboss
