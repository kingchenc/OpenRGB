/*---------------------------------------------------------*\
| WakeRetriggerConfig.h                                     |
|                                                           |
|   Persistent configuration for the post-standby RGB       |
|   retrigger feature.  Stores the expected hardware        |
|   fingerprint (CPU + mainboard) used for verification,    |
|   the profile to re-apply on wake, and the retrigger      |
|   loop parameters.  Loaded/saved through SettingsManager. |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include "SettingsManager.h"

#define WAKE_RETRIGGER_SETTINGS_KEY "WakeRetrigger"

class WakeRetriggerConfig
{
public:
    /*-----------------------------------------------------*\
    | Expected CPU name (substring match during verify)     |
    \*-----------------------------------------------------*/
    std::string     cpu_string;

    /*-----------------------------------------------------*\
    | Expected mainboard name (substring match)             |
    \*-----------------------------------------------------*/
    std::string     board_string;

    /*-----------------------------------------------------*\
    | Profile that is re-applied after wake                 |
    \*-----------------------------------------------------*/
    std::string     profile_name;

    /*-----------------------------------------------------*\
    | Number of retrigger attempts after a wake event       |
    \*-----------------------------------------------------*/
    unsigned int    attempts;

    /*-----------------------------------------------------*\
    | Delay in seconds between retrigger attempts           |
    \*-----------------------------------------------------*/
    unsigned int    delay;

    /*-----------------------------------------------------*\
    | Whether the scheduled wake task is active             |
    \*-----------------------------------------------------*/
    bool            enabled;

    WakeRetriggerConfig();

    void            Load(SettingsManager* settings_manager);
    void            Save(SettingsManager* settings_manager);
};
