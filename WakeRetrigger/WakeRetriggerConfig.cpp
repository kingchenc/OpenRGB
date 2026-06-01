/*---------------------------------------------------------*\
| WakeRetriggerConfig.cpp                                   |
|                                                           |
|   Load/save the post-standby RGB retrigger configuration  |
|   from the OpenRGB settings store.                        |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "WakeRetriggerConfig.h"

WakeRetriggerConfig::WakeRetriggerConfig()
{
    cpu_string        = "";
    board_string      = "";
    profile_name      = "";
    smbus_fingerprint = "";
    attempts          = 3;
    delay             = 2;
    enabled           = false;
}

void WakeRetriggerConfig::Load(SettingsManager* settings_manager)
{
    if(settings_manager == nullptr)
    {
        return;
    }

    json settings = settings_manager->GetSettings(WAKE_RETRIGGER_SETTINGS_KEY);

    if(settings.contains("cpu_string"))
    {
        cpu_string = settings["cpu_string"];
    }

    if(settings.contains("board_string"))
    {
        board_string = settings["board_string"];
    }

    if(settings.contains("profile_name"))
    {
        profile_name = settings["profile_name"];
    }

    if(settings.contains("smbus_fingerprint"))
    {
        smbus_fingerprint = settings["smbus_fingerprint"];
    }

    if(settings.contains("attempts"))
    {
        attempts = settings["attempts"];
    }

    if(settings.contains("delay"))
    {
        delay = settings["delay"];
    }

    if(settings.contains("enabled"))
    {
        enabled = settings["enabled"];
    }
}

void WakeRetriggerConfig::Save(SettingsManager* settings_manager)
{
    if(settings_manager == nullptr)
    {
        return;
    }

    json settings;

    settings["cpu_string"]        = cpu_string;
    settings["board_string"]      = board_string;
    settings["profile_name"]      = profile_name;
    settings["smbus_fingerprint"] = smbus_fingerprint;
    settings["attempts"]          = attempts;
    settings["delay"]             = delay;
    settings["enabled"]           = enabled;

    settings_manager->SetSettings(WAKE_RETRIGGER_SETTINGS_KEY, settings);
    settings_manager->SaveSettings();
}
