// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/hw/sai/tracer/SaiTracer.h"

#if defined(SAI_CBFC_SUPPORTED)

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

sai_virtual_channel_api_t* wrappedVirtualChannelApi();

SET_ATTRIBUTE_FUNC_DECLARATION(VirtualChannel);
SET_ATTRIBUTE_FUNC_DECLARATION(CbfcCreditProfile);

} // namespace facebook::fboss

#endif
