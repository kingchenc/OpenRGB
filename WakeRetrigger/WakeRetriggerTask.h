/*---------------------------------------------------------*\
| WakeRetriggerTask.h                                       |
|                                                           |
|   Creates/removes the Windows Scheduled Task that          |
|   relaunches OpenRGB with --wake-trigger on a system       |
|   resume (power) event.  No-op on non-Windows platforms.   |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>

#define WAKE_RETRIGGER_TASK_NAME "OpenRGB_Sleep_Retrigger"

namespace WakeRetriggerTask
{
    /*-----------------------------------------------------*\
    | Create (or replace) the scheduled task.  profile_name  |
    | is the profile passed to the relaunched instance.      |
    | Returns true on success; on failure error_out holds    |
    | the schtasks output (e.g. an access-denied message).   |
    \*-----------------------------------------------------*/
    bool Enable(const std::string& profile_name, std::string& error_out);

    /*-----------------------------------------------------*\
    | Delete the scheduled task.  Returns true on success;   |
    | error_out holds the schtasks output on failure.        |
    \*-----------------------------------------------------*/
    bool Disable(std::string& error_out);
}
