/*---------------------------------------------------------*\
| WakeRetriggerVerify.h                                     |
|                                                           |
|   Read-only hardware verification for the wake retrigger. |
|   Before any RGB command is sent after a wake event the   |
|   running machine must match the fingerprint captured     |
|   when the feature was enabled:                           |
|     1. CPU name contains the stored string                |
|     2. Mainboard name contains the stored string          |
|     3. Every stored DRAM module is still present in a      |
|        live SPD read (read-only, retry-until-stable,       |
|        padding-tolerant compare)                           |
|   Any mismatch aborts (bricking protection).              |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include "WakeRetriggerConfig.h"

namespace WakeRetriggerVerify
{
    /*-----------------------------------------------------*\
    | Build a read-only fingerprint of all installed DRAM   |
    | modules from their SPD data (memory type, JEDEC id,   |
    | part number).  No writes are ever issued.             |
    \*-----------------------------------------------------*/
    std::string ComputeSMBusFingerprint();

    /*-----------------------------------------------------*\
    | Same fingerprint, but read repeatedly until two       |
    | consecutive reads agree.  SPD access shares the SMBus |
    | with the running DRAM RGB controller, so a single     |
    | read can come back partial or with corrupted padding; |
    | retrying (still read-only) returns a trustworthy read.|
    \*-----------------------------------------------------*/
    std::string ComputeStableSMBusFingerprint();

    /*-----------------------------------------------------*\
    | Run the full 3-stage verification.  Returns true only |
    | if every stage matches.  On failure, failure_reason   |
    | describes which stage failed.                         |
    \*-----------------------------------------------------*/
    bool Verify(const WakeRetriggerConfig& config, std::string& failure_reason);
}
