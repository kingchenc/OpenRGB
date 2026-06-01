/*---------------------------------------------------------*\
| WakeRetriggerVerify.cpp                                   |
|                                                           |
|   Read-only hardware verification for the wake retrigger. |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "WakeRetriggerVerify.h"
#include "ResourceManager.h"
#include "dmiinfo.h"
#include "pci_ids.h"
#include "i2c_smbus.h"
#include "SPDDetector.h"
#include "SPDWrapper.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

static bool wake_retrigger_contains(const std::string& haystack, const std::string& needle)
{
    if(needle.empty())
    {
        return false;
    }

    return haystack.find(needle) != std::string::npos;
}

std::string WakeRetriggerVerify::ComputeSMBusFingerprint()
{
    std::vector<i2c_smbus_interface*>&  busses = ResourceManager::get()->GetI2CBusses();
    std::ostringstream                  fingerprint;

    for(unsigned int bus = 0; bus < busses.size(); bus++)
    {
        IF_DRAM_SMBUS(busses[bus]->pci_vendor, busses[bus]->pci_device)
        {
            SPDMemoryType dimm_type = SPD_RESERVED;

            for(uint8_t spd_addr = 0x50; spd_addr < 0x58; spd_addr++)
            {
                SPDDetector spd(busses[bus], spd_addr, dimm_type);

                if(spd.is_valid())
                {
                    SPDWrapper accessor(spd);
                    dimm_type = spd.memory_type();

                    fingerprint << "b"  << bus
                                << "a"  << std::hex << (int)spd_addr
                                << "t"  << (int)accessor.memory_type()
                                << "j"  << std::setw(4) << std::setfill('0') << accessor.jedec_id()
                                << "p"  << accessor.part_number()
                                << ";";
                }
            }
        }
    }

    return fingerprint.str();
}

bool WakeRetriggerVerify::Verify(const WakeRetriggerConfig& config, std::string& failure_reason)
{
    DMIInfo dmi;

    /*-----------------------------------------------------*\
    | Stage 1: CPU name must contain the stored string      |
    \*-----------------------------------------------------*/
    std::string cpu = dmi.getProcessor();

    if(!wake_retrigger_contains(cpu, config.cpu_string))
    {
        failure_reason = "CPU mismatch (expected to contain '" + config.cpu_string + "', read '" + cpu + "')";
        return false;
    }

    /*-----------------------------------------------------*\
    | Stage 2: mainboard name must contain the stored string|
    \*-----------------------------------------------------*/
    std::string board = dmi.getManufacturer();

    if(!dmi.getProductName().empty())
    {
        if(!board.empty())
        {
            board += " ";
        }
        board += dmi.getProductName();
    }

    if(!wake_retrigger_contains(board, config.board_string))
    {
        failure_reason = "Mainboard mismatch (expected to contain '" + config.board_string + "', read '" + board + "')";
        return false;
    }

    /*-----------------------------------------------------*\
    | Stage 3: live SMBus fingerprint must match the stored |
    | one.  An empty stored fingerprint is treated as a     |
    | failure - we never proceed without a reference.       |
    \*-----------------------------------------------------*/
    if(config.smbus_fingerprint.empty())
    {
        failure_reason = "No SMBus fingerprint stored; refusing to proceed";
        return false;
    }

    std::string live = ComputeSMBusFingerprint();

    if(live != config.smbus_fingerprint)
    {
        failure_reason = "SMBus fingerprint mismatch (stored '" + config.smbus_fingerprint + "', read '" + live + "')";
        return false;
    }

    failure_reason = "";
    return true;
}
