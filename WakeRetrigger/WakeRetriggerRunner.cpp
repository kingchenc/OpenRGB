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
| Two distinct post-standby failure modes are defeated here: |
|                                                            |
| 1. Lost software-control handshake.  DRAM stays powered    |
|    across S3 (suspend-to-RAM uses self-refresh), so a DRAM  |
|    RGB controller keeps its mode register but drops the     |
|    host's software-control state on resume and reverts to   |
|    its autonomous onboard effect.  Drivers like Kingston    |
|    FURY only re-issue that handshake (their "preamble")     |
|    when the mode actually CHANGES, so re-applying the same  |
|    "off" mode is ignored and the LEDs stay lit.  We force   |
|    a real mode change (target -> neighbour -> target) to    |
|    make the driver re-send the handshake.  The interim mode |
|    is driven at minimum brightness so it produces no flash. |
|                                                            |
| 2. No-op value writes.  Re-sending a frame identical to    |
|    what the device (or a per-register write cache) already  |
|    holds is a no-op for many controllers.  We push a        |
|    minimally different frame, wait, then restore the real   |
|    target - a genuine 0 -> 1 -> 0 transition.  This also    |
|    covers single-mode / per-LED-only devices with no second |
|    mode to cycle through.                                   |
\*---------------------------------------------------------*/
static void wake_retrigger_apply_with_nudge(RGBController* device)
{
    const int  target_mode    = device->active_mode;
    const bool target_mode_ok =
        target_mode >= 0 && target_mode < (int)device->modes.size();

    /*-----------------------------------------------------*\
    | Snapshot the target state (already loaded by the       |
    | profile) so we can restore it after the nudges.        |
    \*-----------------------------------------------------*/
    std::vector<RGBColor> target_colors = device->colors;

    mode*        active            = target_mode_ok ? &device->modes[target_mode] : nullptr;
    unsigned int target_brightness = (active != nullptr) ? active->brightness : 0;

    /*-----------------------------------------------------*\
    | Mode-cycle nudge - re-arms the software-control        |
    | handshake on controllers that only send it on a mode   |
    | change (failure mode 1 above).                         |
    \*-----------------------------------------------------*/
    if(target_mode_ok && device->modes.size() > 1)
    {
        const int    nudge_mode    = (target_mode + 1) % (int)device->modes.size();
        mode*        nudge         = &device->modes[nudge_mode];
        unsigned int saved_bright  = nudge->brightness;

        if(nudge->flags & MODE_FLAG_HAS_BRIGHTNESS)
        {
            nudge->brightness = nudge->brightness_min;
        }

        device->active_mode = nudge_mode;
        device->DeviceUpdateMode();

        std::this_thread::sleep_for(WAKE_RETRIGGER_NUDGE_DELAY);

        nudge->brightness   = saved_bright;
        device->active_mode = target_mode;
    }

    /*-----------------------------------------------------*\
    | Value nudge: flip the least-significant bit of every   |
    | channel (visually negligible, but guaranteed different |
    | - "0" becomes "1"), and step the brightness by one if  |
    | the mode exposes a brightness range (failure mode 2).  |
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

bool WakeRetriggerRunner::Run(std::vector<RGBController*>& rgb_controllers,
                              const std::function<void(const std::string&)>& log_sink)
{
    /*-----------------------------------------------------*\
    | Forward a line to the optional log sink (no-op unset)  |
    \*-----------------------------------------------------*/
    auto emit = [&log_sink](const std::string& line)
    {
        if(log_sink)
        {
            log_sink(line);
        }
    };

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
        emit("Verifikation fehlgeschlagen: " + failure_reason);
        return false;
    }

    LOG_INFO("[WakeRetrigger] Verification passed; re-applying profile '%s' (%u attempts, %u s delay)",
             config.profile_name.c_str(), config.attempts, config.delay);

    emit("Verifikation OK (CPU + Mainboard + RAM-Fingerprint).");
    emit("Profil '" + config.profile_name + "', " + std::to_string(config.attempts)
         + " Versuch(e), " + std::to_string(config.delay) + " s Verzoegerung.");
    emit(std::to_string(rgb_controllers.size()) + " Geraet(e) erkannt.");

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
        emit("--- Versuch " + std::to_string(attempt + 1) + "/"
             + std::to_string(config.attempts) + " ---");

        if(!config.profile_name.empty())
        {
            ResourceManager::get()->GetProfileManager()->LoadProfile(config.profile_name);
        }

        for(std::size_t i = 0; i < rgb_controllers.size(); i++)
        {
            wake_retrigger_apply_with_nudge(rgb_controllers[i]);
            emit("  angewendet: " + rgb_controllers[i]->name);
        }

        LOG_INFO("[WakeRetrigger] Attempt %u/%u applied (with nudge)", attempt + 1, config.attempts);

        if(config.delay > 0)
        {
            emit("  warte " + std::to_string(config.delay) + " s ...");
            std::this_thread::sleep_for(std::chrono::seconds(config.delay));
        }
    }

    emit("Sequenz abgeschlossen.");

    return true;
}
