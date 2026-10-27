// SPDX-License-Identifier: AGPL-3.0-only
#include "MtsEsp.h"

#include "../core/PhiTuning.h"
#include "../../third_party/MTS-ESP/libMTSMaster.h"

#include <filesystem>

namespace mixer
{
    MtsEsp::~MtsEsp() { stop(); }

    bool MtsEsp::installed()
    {
        std::error_code ignored;
        return std::filesystem::exists ("/usr/local/lib/libMTS.so", ignored);
    }

    MtsEsp::Start MtsEsp::start()
    {
        if (on)
            return Start::started;
        if (! installed())
            return Start::noLibrary;
        if (! MTS_CanRegisterMaster())
            return Start::otherMaster;
        MTS_RegisterMaster();
        on = true;
        sent = {};
        sentName.clear();
        return Start::started;
    }

    void MtsEsp::stop()
    {
        if (! on)
            return;
        MTS_DeregisterMaster();
        on = false;
    }

    void MtsEsp::send (const phi::Tuning& tuning, const std::string& name)
    {
        if (! on)
            return;
        std::array<double, 128> hz {};
        for (int k = 0; k < 128; ++k)
            hz[(size_t) k] = tuning.hz (k);
        if (hz != sent)
        {
            MTS_SetNoteTunings (hz.data());
            sent = hz;
        }
        if (name != sentName)
        {
            MTS_SetScaleName (name.c_str());
            sentName = name;
        }
    }

    int MtsEsp::clients() const { return on ? MTS_GetNumClients() : 0; }
}
