/*---------------------------------------------------------*\
| WakeRetriggerRunner.h                                     |
|                                                           |
|   Executes the post-standby retrigger sequence: verify    |
|   the hardware, then re-apply the configured profile a    |
|   number of times with a delay between attempts.  Invoked |
|   from the CLI when OpenRGB is relaunched with             |
|   --wake-trigger by the scheduled task.                   |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <functional>
#include <string>
#include <vector>

class RGBController;

namespace WakeRetriggerRunner
{
    /*-----------------------------------------------------*\
    | Verify the hardware fingerprint, then re-apply the    |
    | stored profile config.attempts times with config.delay|
    | seconds between attempts.  Returns true if the         |
    | verification passed and the loop ran, false if         |
    | verification failed (in which case NO command is sent).|
    |                                                        |
    | When log_sink is set it receives a human-readable line |
    | for each step (verification, per attempt, per device), |
    | which the settings page uses to drive its live "Test"  |
    | log window.  It is always safe to leave it unset.      |
    \*-----------------------------------------------------*/
    bool Run(std::vector<RGBController*>& rgb_controllers,
             const std::function<void(const std::string&)>& log_sink = nullptr);
}
