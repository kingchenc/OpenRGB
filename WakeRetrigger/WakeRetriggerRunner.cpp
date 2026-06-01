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
#include <vector>

/*---------------------------------------------------------*\
| Brief pause between the nudge and the restore so the       |
| hardware registers a real transition rather than a no-op.  |
\*---------------------------------------------------------*/
static const std::chrono::milliseconds WAKE_RETRIGGER_NUDGE_DELAY(120);

/*---------------------------------------------------------*\
| Apply the controller's currently loaded (target) state in  |
| a way the hardware actually honours after a wake.          |
|                                                            |
| Re-sending a value identical to what the device already    |
| holds is a no-op for many controllers (e.g. setting "off"  |
| again leaves the LEDs on after standby).  So we first push |
| a minimally different state, wait briefly, then restore    |
| the real target - forcing a genuine 0 -> 1 -> 0 transition.|
\*---------------------------------------------------------*/
static void wake_retrigger_apply_with_nudge(RGBController* device)
{
    /*-----------------------------------------------------*\
    | Snapshot the target state (already loaded by the       |
    | profile) so we can restore it after the nudge.         |
    \*-----------------------------------------------------*/
    std::vector<RGBColor> target_colors = device->colors;

    mode*        active            = nullptr;
    unsigned int target_brightness = 0;

    if(device->active_mode >= 0 && device->active_mode < (int)device->modes.size())
    {
        active            = &device->modes[device->active_mode];
        target_brightness = active->brightness;
    }

    /*-----------------------------------------------------*\
    | Nudge: flip the least-significant bit of every channel |
    | (visually negligible, but guaranteed different - "0"   |
    | becomes "1"), and step the brightness by one if the    |
    | mode exposes a brightness range.                       |
    \*-----------------------------------------------------*/
    for(std::size_t led = 0; led < device->colors.size(); led++)
    {
        device->colors[led] = target_colors[led] ^ 0x00010101;
    }

    if(active != nullptr
    && (active->flags & MODE_FLAG_HAS_BRIGHTNESS)
    && active->brightness_max > active->brightness_min)
    {
        active->brightness = (target_brightness < active->brightness_max)
                               ? target_brightness + 1
                               : target_brightness - 1;
    }

    device->DeviceUpdateMode();
    device->DeviceUpdateLEDs();

    std::this_thread::sleep_for(WAKE_RETRIGGER_NUDGE_DELAY);

    /*-----------------------------------------------------*\
    | Restore and push the real target state                |
    \*-----------------------------------------------------*/
    device->colors = target_colors;

    if(active != nullptr)
    {
        active->brightness = target_brightness;
    }

    device->DeviceUpdateMode();
    device->DeviceUpdateLEDs();
}

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
    | attempt reloads the stored profile and pushes it to     |
    | every controller via a nudge (target -> off-by-one ->   |
    | target) so the hardware honours the state even when it  |
    | equals what the device thinks it already holds after a  |
    | wake, and even if the bus was not ready on the first try.|
    \*-----------------------------------------------------*/
    for(unsigned int attempt = 0; attempt < config.attempts; attempt++)
    {
        if(!config.profile_name.empty())
        {
            ResourceManager::get()->GetProfileManager()->LoadProfile(config.profile_name);
        }

        for(std::size_t i = 0; i < rgb_controllers.size(); i++)
        {
            wake_retrigger_apply_with_nudge(rgb_controllers[i]);
        }

        LOG_INFO("[WakeRetrigger] Attempt %u/%u applied (with nudge)", attempt + 1, config.attempts);

        if(config.delay > 0)
        {
            std::this_thread::sleep_for(std::chrono::seconds(config.delay));
        }
    }

    return true;
}
