/*---------------------------------------------------------*\
| WakeRetriggerTask.cpp                                     |
|                                                           |
|   Creates/removes the Windows Scheduled Task for the       |
|   post-standby RGB retrigger.                             |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "WakeRetriggerTask.h"
#include "LogManager.h"

#ifdef _WIN32

#include <windows.h>
#include <cstdlib>

static std::string wake_retrigger_executable_path()
{
    char    path[MAX_PATH] = { 0 };
    DWORD   length         = GetModuleFileNameA(NULL, path, MAX_PATH);

    if(length == 0 || length == MAX_PATH)
    {
        return "";
    }

    return std::string(path, length);
}

bool WakeRetriggerTask::Enable(const std::string& profile_name)
{
    std::string exe = wake_retrigger_executable_path();

    if(exe.empty())
    {
        LOG_ERROR("[WakeRetrigger] Could not determine executable path; task not created");
        return false;
    }

    /*-----------------------------------------------------*\
    | Build the /tr action string.  Nested quotes are        |
    | escaped with \" so schtasks receives the whole action  |
    | (quoted exe path + arguments) as a single value.       |
    \*-----------------------------------------------------*/
    std::string action = "\\\"" + exe + "\\\"";

    if(!profile_name.empty())
    {
        action += " --profile \\\"" + profile_name + "\\\"";
    }

    action += " --wake-trigger";

    /*-----------------------------------------------------*\
    | Trigger on a System event-log entry from the           |
    | Power-Troubleshooter provider, EventID 1 (the machine  |
    | has resumed from sleep/standby).                       |
    \*-----------------------------------------------------*/
    std::string command =
        "schtasks /create /tn \"" WAKE_RETRIGGER_TASK_NAME "\""
        " /tr \"" + action + "\""
        " /sc ONEVENT /EC System"
        " /MO \"*[System[Provider[@Name='Microsoft-Windows-Power-Troubleshooter'] and EventID=1]]\""
        " /f /rl HIGHEST";

    LOG_INFO("[WakeRetrigger] Creating scheduled task: %s", command.c_str());

    int result = std::system(command.c_str());

    if(result != 0)
    {
        LOG_ERROR("[WakeRetrigger] schtasks /create returned %d", result);
        return false;
    }

    return true;
}

bool WakeRetriggerTask::Disable()
{
    std::string command = "schtasks /delete /tn \"" WAKE_RETRIGGER_TASK_NAME "\" /f";

    LOG_INFO("[WakeRetrigger] Deleting scheduled task: %s", command.c_str());

    int result = std::system(command.c_str());

    if(result != 0)
    {
        LOG_ERROR("[WakeRetrigger] schtasks /delete returned %d", result);
        return false;
    }

    return true;
}

#else /* _WIN32 */

bool WakeRetriggerTask::Enable(const std::string& /*profile_name*/)
{
    LOG_INFO("[WakeRetrigger] Scheduled wake task is only supported on Windows");
    return false;
}

bool WakeRetriggerTask::Disable()
{
    return true;
}

#endif /* _WIN32 */
