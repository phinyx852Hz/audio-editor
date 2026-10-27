// SPDX-License-Identifier: AGPL-3.0-only
#include "MtsEsp.h"

#include "../core/PhiTuning.h"
#include "../../third_party/MTS-ESP/libMTSMaster.h"

#include <filesystem>

namespace mixer
{
    namespace
    {
        bool present()
        {
            std::error_code ignored;
            return std::filesystem::exists (MtsEsp::libraryPath, ignored);
        }
        // libMTSMaster loads libMTS as PHI starts, if it is there then.
        const bool presentAtStart = present();
    }

    MtsEsp::~MtsEsp() { stop(); }

    bool MtsEsp::installed() { return present(); }

    std::string MtsEsp::downloadUrl()
    {
        const std::string base = "https://raw.githubusercontent.com/ODDSound/MTS-ESP/main/libMTS/Linux/";
#if defined(__x86_64__)
        return base + "x86_64/libMTS.so";
#elif defined(__aarch64__)
        return base + "arm64/libMTS.so";
#elif defined(__arm__)
        return base + "arm32hf/libMTS.so";
#else
        return {};
#endif
    }

    MtsEsp::Start MtsEsp::start()
    {
        if (on)
            return Start::started;
        if (! installed())
            return Start::noLibrary;
        if (! presentAtStart)
            return Start::restart;
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
