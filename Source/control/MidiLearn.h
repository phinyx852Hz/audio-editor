// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// MIDI Learn (docs/control-surfaces.md): any knob, fader, key, pad or button of any keyboard or
// controller (a keytar's buttons and strips, an Intech Grid's encoders) mapped to the mixer and
// transport. Pick what it should do, move the control, and it does that from then on. The
// mapped messages no longer play the instrument. Plain C++; handle() runs on the MIDI thread.
#include "ControlSurface.h"

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace control
{
    struct Mapping
    {
        enum class Source { cc, note, pitchBend };
        // How a knob's values mean a turn: the value itself (absolute), or steps from what it was:
        // two's complement (1 up, 127 down), offset from 64 (65 up, 63 down), or sign and size
        // (1 up, 65 down). A button: a press (a note, or a CC at 64 or more).
        enum class Mode { absolute, relativeTwos, relativeOffset, relativeSigned, button };

        Source source = Source::cc;
        int channel = 0;          // 1..16; 0 any
        int number = 0;           // the CC or note; not used for pitch bend
        std::string device;       // the input's name; empty: any
        Command target;           // its action, track (-1 selected), scene, index
        Mode mode = Mode::absolute;
    };

    // What a control can be mapped to, for the UI's list.
    struct Target
    {
        Action action;
        int value;                // selectBy's direction; else 0
        const char* name;
        bool continuous;          // a knob or fader; else a button
        bool perTrack, perScene;
        int index = 0;            // send A or B
    };
    const std::vector<Target>& targets();
    const Target* targetOf (const Command&);
    std::string describeSource (const Mapping&);   // "CC 21, channel 1"

    class Learn
    {
    public:
        void setMappings (std::vector<Mapping>);
        std::vector<Mapping> mappings() const;
        // The next control moved (a CC, a note pressed, pitch bend) is mapped to `target`.
        void learn (const Command& target, bool continuous);
        void cancel();
        bool learning() const;
        // A message from a keyboard or controller. True when it is mapped (it then does not
        // play): `out` gets what it asks for. `learned` is set when it was learned just now.
        bool handle (const std::string& device, int status, int channel, int a, int b, std::vector<Command>& out,
                     std::optional<Mapping>* learned = nullptr);

    private:
        static bool matches (const Mapping&, const std::string& device, Mapping::Source, int channel, int number);
        mutable std::mutex lock;
        std::vector<Mapping> list;
        std::optional<Command> waiting;
        bool waitingContinuous = false;
        std::map<size_t, int> previous;   // a button mapping's last value, to act on presses only
    };
}
