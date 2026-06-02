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
| Force a single DRAM controller fully OFF in a way the      |
| hardware actually honours after a wake.                    |
|                                                            |
| Goal: only the RAM is touched, and it ends up dark - not   |
| reset to a coloured default and not via a multi-device     |
| profile that would also clobber the keyboard/mouse/GPU.    |
|                                                            |
| The hard part is the software-control handshake.  DRAM     |
| stays powered across S3 (suspend-to-RAM uses self-refresh),|
| so the controller keeps its mode register but drops the    |
| host's software-control state on resume and reverts to its |
| autonomous onboard effect.  Drivers like Kingston FURY only|
| re-issue that handshake (their "preamble") when the mode    |
| actually CHANGES, so re-applying the same mode is ignored  |
| and the LEDs stay lit.  We therefore cycle through a second |
| mode (blanked, so it never flashes) to force the handshake,|
| then land on a per-LED mode with every LED set to black.   |
\*---------------------------------------------------------*/
static void wake_retrigger_force_off(RGBController* device)
{
    if(device->modes.empty())
    {
        for(std::size_t led = 0; led < device->colors.size(); led++)
        {
            device->colors[led] = 0;
        }
        device->DeviceUpdateLEDs();
        return;
    }

    /*-----------------------------------------------------*\
    | Pick a per-LED mode (e.g. Direct) so we can blacken    |
    | every LED; fall back to the current/first mode.        |
    \*-----------------------------------------------------*/
    int off_mode = device->active_mode >= 0
                 && device->active_mode < (int)device->modes.size()
                 ? device->active_mode : 0;

    for(std::size_t i = 0; i < device->modes.size(); i++)
    {
        if(device->modes[i].flags & MODE_FLAG_HAS_PER_LED_COLOR)
        {
            off_mode = (int)i;
            break;
        }
    }

    /*-----------------------------------------------------*\
    | Blacken everything the off mode can show               |
    \*-----------------------------------------------------*/
    for(std::size_t led = 0; led < device->colors.size(); led++)
    {
        device->colors[led] = 0;
    }
    for(std::size_t c = 0; c < device->modes[off_mode].colors.size(); c++)
    {
        device->modes[off_mode].colors[c] = 0;
    }
    if(device->modes[off_mode].flags & MODE_FLAG_HAS_BRIGHTNESS)
    {
        device->modes[off_mode].brightness = device->modes[off_mode].brightness_min;
    }

    /*-----------------------------------------------------*\
    | Mode-cycle to force the software-control handshake.    |
    | The interim mode is blanked (colours black + minimum   |
    | brightness) and restored afterwards so it never flashes|
    | and the user's stored mode definitions stay intact.    |
    \*-----------------------------------------------------*/
    if(device->modes.size() > 1)
    {
        const int             nudge_mode   = (off_mode + 1) % (int)device->modes.size();
        mode*                 nudge        = &device->modes[nudge_mode];
        const unsigned int    saved_bright = nudge->brightness;
        std::vector<RGBColor> saved_colors = nudge->colors;

        for(std::size_t c = 0; c < nudge->colors.size(); c++)
        {
            nudge->colors[c] = 0;
        }
        if(nudge->flags & MODE_FLAG_HAS_BRIGHTNESS)
        {
            nudge->brightness = nudge->brightness_min;
        }

        device->active_mode = nudge_mode;
        device->DeviceUpdateMode();

        std::this_thread::sleep_for(WAKE_RETRIGGER_NUDGE_DELAY);

        nudge->brightness = saved_bright;
        nudge->colors     = saved_colors;
    }

    /*-----------------------------------------------------*\
    | Land on the blacked-out off mode                      |
    \*-----------------------------------------------------*/
    device->active_mode = off_mode;
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

    /*-----------------------------------------------------*\
    | Collect ONLY the DRAM controllers.  The whole feature  |
    | exists to stop RAM RGB coming back on after wake; every |
    | other device (keyboard, mouse, GPU, mainboard) keeps    |
    | its own state across S3 and must NOT be touched.        |
    \*-----------------------------------------------------*/
    std::vector<RGBController*> dram_controllers;

    for(std::size_t i = 0; i < rgb_controllers.size(); i++)
    {
        if(rgb_controllers[i]->type == DEVICE_TYPE_DRAM)
        {
            dram_controllers.push_back(rgb_controllers[i]);
        }
    }

    LOG_INFO("[WakeRetrigger] Verification passed; forcing %u DRAM device(s) off (%u attempts, %u s delay)",
             (unsigned int)dram_controllers.size(), config.attempts, config.delay);

    emit("Verifikation OK (CPU + Mainboard + RAM-Fingerprint).");
    emit(std::to_string(dram_controllers.size()) + " RAM-Geraet(e) von "
         + std::to_string(rgb_controllers.size()) + " gesamt - nur diese werden abgeschaltet.");
    emit(std::to_string(config.attempts) + " Versuch(e), "
         + std::to_string(config.delay) + " s Verzoegerung.");

    if(dram_controllers.empty())
    {
        emit("Kein DRAM-Geraet gefunden - nichts zu tun.");
        return true;
    }

    /*-----------------------------------------------------*\
    | Force every DRAM controller off config.attempts times. |
    | The handshake mode-cycle inside force_off makes the    |
    | hardware honour the off state even when it ignored the |
    | first try after a wake / before the bus was ready.     |
    \*-----------------------------------------------------*/
    for(unsigned int attempt = 0; attempt < config.attempts; attempt++)
    {
        emit("--- Versuch " + std::to_string(attempt + 1) + "/"
             + std::to_string(config.attempts) + " ---");

        for(std::size_t i = 0; i < dram_controllers.size(); i++)
        {
            wake_retrigger_force_off(dram_controllers[i]);
            emit("  abgeschaltet: " + dram_controllers[i]->name);
        }

        LOG_INFO("[WakeRetrigger] Attempt %u/%u applied", attempt + 1, config.attempts);

        if(config.delay > 0)
        {
            emit("  warte " + std::to_string(config.delay) + " s ...");
            std::this_thread::sleep_for(std::chrono::seconds(config.delay));
        }
    }

    emit("Sequenz abgeschlossen - RAM aus.");

    return true;
}
