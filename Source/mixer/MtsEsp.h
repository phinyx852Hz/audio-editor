// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// MTS-ESP (owner, 2026-10-26): PHI as the session's MTS-ESP tuning master, so the synths that
// follow MTS-ESP (Surge XT, Vital, Pianoteq, ODDSound's own and many more, in PHI or in another
// program) play in the tuning in force on the live track, as the PHI synth does. Through
// ODDSound's libMTSMaster (third_party/MTS-ESP, a permissive licence), which loads the libMTS
// library ODDSound's installer puts in /usr/local/lib; without it nothing is sent.
#include <array>
#include <string>

namespace phi { class Tuning; }

namespace mixer
{
    class MtsEsp
    {
    public:
        ~MtsEsp();

        static bool installed();   // libMTS is where libMTSMaster looks for it
        enum class Start { started, noLibrary, otherMaster };
        Start start();
        void stop();
        bool running() const { return on; }
        // Every key's frequency, and the scale's name; nothing is sent when they are as last time.
        void send (const phi::Tuning&, const std::string& name);
        int clients() const;       // the synths following it now

    private:
        bool on = false;
        std::array<double, 128> sent {};
        std::string sentName;
    };
}
