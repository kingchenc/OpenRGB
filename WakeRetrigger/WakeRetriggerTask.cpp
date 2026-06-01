/*---------------------------------------------------------*\
| WakeRetriggerTask.cpp                                     |
|                                                           |
|   Creates/removes the Windows Scheduled Task for the       |
|   post-standby RGB retrigger.                             |
|                                                           |
|   The task is created from a Task Scheduler XML definition |
|   (schtasks /create /xml) rather than an inline /tr        |
|   command, which avoids fragile nested-quote escaping for  |
|   the executable path and arguments.                      |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "WakeRetriggerTask.h"
#include "LogManager.h"

#ifdef _WIN32

#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <iterator>

static std::wstring wake_retrigger_temp_dir()
{
    wchar_t buffer[MAX_PATH] = { 0 };
    DWORD   length           = GetTempPathW(MAX_PATH, buffer);

    if(length == 0 || length > MAX_PATH)
    {
        return L"";
    }

    return std::wstring(buffer, length);
}

static std::wstring wake_retrigger_executable_path()
{
    wchar_t buffer[MAX_PATH] = { 0 };
    DWORD   length           = GetModuleFileNameW(NULL, buffer, MAX_PATH);

    if(length == 0 || length == MAX_PATH)
    {
        return L"";
    }

    return std::wstring(buffer, length);
}

static std::wstring wake_retrigger_xml_escape(const std::wstring& value)
{
    std::wstring out;

    for(wchar_t c : value)
    {
        switch(c)
        {
            case L'&': out += L"&amp;";  break;
            case L'<': out += L"&lt;";   break;
            case L'>': out += L"&gt;";   break;
            default:   out += c;         break;
        }
    }

    return out;
}

static std::string wake_retrigger_read_file(const std::wstring& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);

    if(!file)
    {
        return "";
    }

    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

/*---------------------------------------------------------*\
| Run a schtasks command, capturing stdout+stderr into       |
| output.  Returns the process exit code (0 = success).      |
\*---------------------------------------------------------*/
static int wake_retrigger_run_schtasks(const std::wstring& args, std::string& output)
{
    std::wstring log_file = wake_retrigger_temp_dir() + L"OpenRGB_Sleep_Retrigger.log";

    std::wstring command = L"schtasks " + args + L" > \"" + log_file + L"\" 2>&1";

    int result = _wsystem(command.c_str());

    output = wake_retrigger_read_file(log_file);

    return result;
}

bool WakeRetriggerTask::Enable(const std::string& profile_name, std::string& error_out)
{
    std::wstring exe = wake_retrigger_executable_path();

    if(exe.empty())
    {
        error_out = "Could not determine the OpenRGB executable path.";
        return false;
    }

    /*-----------------------------------------------------*\
    | Arguments for the relaunched instance.  The profile    |
    | name is the internal 'WakeRetrigger' slot (no spaces). |
    \*-----------------------------------------------------*/
    std::wstring args = L"--wake-trigger";

    if(!profile_name.empty())
    {
        std::wstring profile(profile_name.begin(), profile_name.end());
        args = L"--profile " + profile + L" --wake-trigger";
    }

    /*-----------------------------------------------------*\
    | Build the Task Scheduler XML.  Trigger: System event   |
    | log, Power-Troubleshooter provider, EventID 1 (resume  |
    | from sleep).  Runs with highest privileges so SMBus is  |
    | accessible.                                            |
    \*-----------------------------------------------------*/
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo>\r\n"
        L"    <Description>OpenRGB post-standby RGB retrigger</Description>\r\n"
        L"  </RegistrationInfo>\r\n"
        L"  <Triggers>\r\n"
        L"    <EventTrigger>\r\n"
        L"      <Enabled>true</Enabled>\r\n"
        L"      <Subscription>&lt;QueryList&gt;&lt;Query Id=\"0\" Path=\"System\"&gt;&lt;Select Path=\"System\"&gt;*[System[Provider[@Name='Microsoft-Windows-Power-Troubleshooter'] and EventID=1]]&lt;/Select&gt;&lt;/Query&gt;&lt;/QueryList&gt;</Subscription>\r\n"
        L"    </EventTrigger>\r\n"
        L"  </Triggers>\r\n"
        L"  <Principals>\r\n"
        L"    <Principal id=\"Author\">\r\n"
        L"      <LogonType>InteractiveToken</LogonType>\r\n"
        L"      <RunLevel>HighestAvailable</RunLevel>\r\n"
        L"    </Principal>\r\n"
        L"  </Principals>\r\n"
        L"  <Settings>\r\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
        L"    <StartWhenAvailable>true</StartWhenAvailable>\r\n"
        L"    <Enabled>true</Enabled>\r\n"
        L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
        L"  </Settings>\r\n"
        L"  <Actions Context=\"Author\">\r\n"
        L"    <Exec>\r\n"
        L"      <Command>" + wake_retrigger_xml_escape(exe) + L"</Command>\r\n"
        L"      <Arguments>" + wake_retrigger_xml_escape(args) + L"</Arguments>\r\n"
        L"    </Exec>\r\n"
        L"  </Actions>\r\n"
        L"</Task>\r\n";

    /*-----------------------------------------------------*\
    | Write the XML as UTF-16LE with BOM (schtasks expects   |
    | a Unicode task file).                                  |
    \*-----------------------------------------------------*/
    std::wstring xml_file = wake_retrigger_temp_dir() + L"OpenRGB_Sleep_Retrigger.xml";

    {
        std::ofstream file(xml_file.c_str(), std::ios::binary);

        if(!file)
        {
            error_out = "Could not write the task definition to the temp folder.";
            return false;
        }

        const unsigned char bom[2] = { 0xFF, 0xFE };
        file.write(reinterpret_cast<const char*>(bom), 2);
        file.write(reinterpret_cast<const char*>(xml.data()),
                   static_cast<std::streamsize>(xml.size() * sizeof(wchar_t)));
    }

    std::wstring args_cmd =
        L"/create /tn \"" L"OpenRGB_Sleep_Retrigger" L"\" /xml \"" + xml_file + L"\" /f";

    int result = wake_retrigger_run_schtasks(args_cmd, error_out);

    if(result != 0)
    {
        LOG_ERROR("[WakeRetrigger] schtasks /create failed (%d): %s", result, error_out.c_str());
        return false;
    }

    LOG_INFO("[WakeRetrigger] Scheduled task created");
    error_out.clear();
    return true;
}

bool WakeRetriggerTask::Disable(std::string& error_out)
{
    int result = wake_retrigger_run_schtasks(
        L"/delete /tn \"" L"OpenRGB_Sleep_Retrigger" L"\" /f", error_out);

    if(result != 0)
    {
        LOG_ERROR("[WakeRetrigger] schtasks /delete failed (%d): %s", result, error_out.c_str());
        return false;
    }

    LOG_INFO("[WakeRetrigger] Scheduled task deleted");
    error_out.clear();
    return true;
}

#else /* _WIN32 */

bool WakeRetriggerTask::Enable(const std::string& /*profile_name*/, std::string& error_out)
{
    error_out = "Scheduled wake task is only supported on Windows.";
    LOG_INFO("[WakeRetrigger] %s", error_out.c_str());
    return false;
}

bool WakeRetriggerTask::Disable(std::string& error_out)
{
    error_out.clear();
    return true;
}

#endif /* _WIN32 */
