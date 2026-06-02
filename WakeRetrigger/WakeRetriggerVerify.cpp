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

#include <algorithm>
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

/*---------------------------------------------------------*\
| Split a fingerprint into one normalised token per DRAM    |
| module.  Each module entry ends in ';'; the trailing      |
| whitespace inside an entry is stripped because DDR5       |
| part-number SPD fields are space/null padded and the SPD  |
| trimmer keeps one pad byte, so otherwise-identical reads  |
| jitter by trailing spaces.  Normalising here is what      |
| makes the compare tolerant.                               |
\*---------------------------------------------------------*/
static std::vector<std::string> wake_retrigger_tokens(const std::string& fingerprint)
{
    std::vector<std::string>    tokens;
    std::string                 token;
    std::istringstream          stream(fingerprint);

    while(std::getline(stream, token, ';'))
    {
        std::size_t end = token.find_last_not_of(" \t");

        if(end == std::string::npos)
        {
            continue;
        }

        tokens.push_back(token.substr(0, end + 1));
    }

    return tokens;
}

/*---------------------------------------------------------*\
| Order-independent canonical form of a fingerprint, used   |
| only to decide whether two live reads agree.              |
\*---------------------------------------------------------*/
static std::string wake_retrigger_canonical(const std::string& fingerprint)
{
    std::vector<std::string>    tokens = wake_retrigger_tokens(fingerprint);
    std::sort(tokens.begin(), tokens.end());

    std::string                 canonical;

    for(std::size_t i = 0; i < tokens.size(); i++)
    {
        canonical += tokens[i];
        canonical += ";";
    }

    return canonical;
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

std::string WakeRetriggerVerify::ComputeStableSMBusFingerprint()
{
    /*-----------------------------------------------------*\
    | SPD reads share the SMBus with the running DRAM RGB    |
    | controller, so a single read can come back partial    |
    | (a module dropping out) or with corrupted padding     |
    | bytes.  Read repeatedly until two consecutive reads   |
    | agree on their canonical form, then trust that read.   |
    | Every iteration only reads the SPD - no module is     |
    | ever written - so retrying cannot brick anything.     |
    \*-----------------------------------------------------*/
    const int       max_attempts = 6;
    std::string     previous     = ComputeSMBusFingerprint();
    std::string     previous_key = wake_retrigger_canonical(previous);

    for(int attempt = 1; attempt < max_attempts; attempt++)
    {
        std::string current     = ComputeSMBusFingerprint();
        std::string current_key  = wake_retrigger_canonical(current);

        if(!current_key.empty() && current_key == previous_key)
        {
            return current;
        }

        previous     = current;
        previous_key = current_key;
    }

    return previous;
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
    | Stage 3: every DRAM module captured when the feature  |
    | was enabled must still be present in a live SPD read.  |
    | The live read is taken with a retry-until-stable pass  |
    | (SMBus contention can corrupt a single read) and the   |
    | compare is tolerant of part-number padding jitter.     |
    | An empty stored fingerprint is treated as a failure -  |
    | we never proceed without a reference.                  |
    \*-----------------------------------------------------*/
    if(config.smbus_fingerprint.empty())
    {
        failure_reason = "No SMBus fingerprint stored; refusing to proceed";
        return false;
    }

    std::vector<std::string> stored_tokens = wake_retrigger_tokens(config.smbus_fingerprint);

    if(stored_tokens.empty())
    {
        failure_reason = "Stored SMBus fingerprint holds no DRAM module; refusing to proceed";
        return false;
    }

    std::string                 live        = ComputeStableSMBusFingerprint();
    std::vector<std::string>    live_tokens = wake_retrigger_tokens(live);

    for(std::size_t i = 0; i < stored_tokens.size(); i++)
    {
        if(std::find(live_tokens.begin(), live_tokens.end(), stored_tokens[i]) == live_tokens.end())
        {
            failure_reason = "SMBus fingerprint mismatch (stored '" + config.smbus_fingerprint + "', read '" + live + "')";
            return false;
        }
    }

    failure_reason = "";
    return true;
}
