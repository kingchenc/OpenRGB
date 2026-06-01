/*---------------------------------------------------------*\
| WakeRetriggerRunner.cpp                                   |
|                                                           |
|   Executes the post-standby retrigger sequence.           |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "WakeRetriggerRunner.h"
#include "WakeRetriggerConfig.h"
#include "WakeRetriggerVerify.h"
#include "ResourceManager.h"
#include "ProfileManager.h"
#include "RGBController.h"
#include "LogManager.h"

#include <chrono>
#include <thread>

bool WakeRetriggerRunner::Run(std::vector<RGBController*>& rgb_controllers)
{
    /*-----------------------------------------------------*\
    | Load the stored configuration                         |
    \*-----------------------------------------------------*/
    WakeRetriggerConfig config;
    config.Load(ResourceManager::get()->GetSettingsManager());

    /*-----------------------------------------------------*\
    | Read-only hardware verification (anti-brick gate)     |
    \*-----------------------------------------------------*/
    std::string failure_reason;

    if(!WakeRetriggerVerify::Verify(config, failure_reason))
    {
        LOG_ERROR("[WakeRetrigger] Verification failed, aborting: %s", failure_reason.c_str());
        return false;
    }

    LOG_INFO("[WakeRetrigger] Verification passed; re-applying profile '%s' (%u attempts, %u s delay)",
             config.profile_name.c_str(), config.attempts, config.delay);

    /*-----------------------------------------------------*\
    | Re-apply the profile config.attempts times.  Each      |
    | attempt reloads the stored profile and pushes mode +   |
    | LEDs to every controller so the configured state wins  |
    | even if the bus was not ready on the first try.        |
    \*-----------------------------------------------------*/
    for(unsigned int attempt = 0; attempt < config.attempts; attempt++)
    {
        if(!config.profile_name.empty())
        {
            ResourceManager::get()->GetProfileManager()->LoadProfile(config.profile_name);
        }

        for(std::size_t i = 0; i < rgb_controllers.size(); i++)
        {
            RGBController* device = rgb_controllers[i];

            device->DeviceUpdateMode();
            device->DeviceUpdateLEDs();
        }

        LOG_INFO("[WakeRetrigger] Attempt %u/%u applied", attempt + 1, config.attempts);

        if(config.delay > 0)
        {
            std::this_thread::sleep_for(std::chrono::seconds(config.delay));
        }
    }

    return true;
}
