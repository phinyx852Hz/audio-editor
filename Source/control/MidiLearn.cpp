// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiLearn.h"

#include <algorithm>

namespace control
{
    const std::vector<Target>& targets()
    {
        static const std::vector<Target> list {
            { Action::togglePlay, 0, "Play / Pause", false, false, false },
            { Action::stop, 0, "Stop", false, false, false },
            { Action::record, 0, "Record", false, false, false },
            { Action::capture, 0, "Capture MIDI", false, false, false },
            { Action::metronome, 0, "Metronome", false, false, false },
            { Action::loop, 0, "Loop", false, false, false },
            { Action::undo, 0, "Undo", false, false, false },
            { Action::redo, 0, "Redo", false, false, false },
            { Action::tapTempo, 0, "Tap Tempo", false, false, false },
            { Action::tempo, 0, "Tempo", true, false, false },
            { Action::launch, 0, "Launch Slot", false, true, true },
            { Action::launchScene, 0, "Launch Scene", false, false, true },
            { Action::stopTrack, 0, "Stop Track's Clip", false, true, false },
            { Action::stopAll, 0, "Stop All Clips", false, false, false },
            { Action::select, 0, "Select Track", false, true, false },
            { Action::selectBy, 1, "Next Track", false, false, false },
            { Action::selectBy, -1, "Previous Track", false, false, false },
            { Action::arm, 0, "Arm", false, true, false },
            { Action::mute, 0, "Mute", false, true, false },
            { Action::solo, 0, "Solo", false, true, false },
            { Action::volume, 0, "Volume", true, true, false },
            { Action::pan, 0, "Pan", true, true, false },
            { Action::send, 0, "Send A", true, true, false, 0 },
            { Action::send, 0, "Send B", true, true, false, 1 },
            { Action::master, 0, "Master Volume", true, false, false },
            { Action::showLauncher, 0, "Show the Clip Launcher", false, false, false },
        };
        return list;
    }

    const Target* targetOf (const Command& c)
    {
        for (const auto& t : targets())
            if (t.action == c.action && (t.action != Action::selectBy || (int) c.value == t.value) && (t.action != Action::send || c.index == t.index))
                return &t;
        return nullptr;
    }

    std::string describeSource (const Mapping& m)
    {
        std::string s = m.source == Mapping::Source::cc ? "CC " + std::to_string (m.number)
                        : m.source == Mapping::Source::note ? "Note " + std::to_string (m.number)
                                                            : std::string ("Pitch bend");
        s += m.channel > 0 ? ", channel " + std::to_string (m.channel) : ", any channel";
        if (! m.device.empty())
            s += ", " + m.device;
        return s;
    }

    void Learn::setMappings (std::vector<Mapping> m)
    {
        std::lock_guard g (lock);
        list = std::move (m);
        previous.clear();
    }

    std::vector<Mapping> Learn::mappings() const
    {
        std::lock_guard g (lock);
        return list;
    }

    void Learn::learn (const Command& target, bool continuous)
    {
        std::lock_guard g (lock);
        waiting = target;
        waitingContinuous = continuous;
    }

    void Learn::cancel()
    {
        std::lock_guard g (lock);
        waiting.reset();
    }

    bool Learn::learning() const
    {
        std::lock_guard g (lock);
        return waiting.has_value();
    }

    bool Learn::matches (const Mapping& m, const std::string& device, Mapping::Source source, int channel, int number)
    {
        return m.source == source && (m.channel == 0 || m.channel == channel) && (source == Mapping::Source::pitchBend || m.number == number)
               && (m.device.empty() || m.device == device);
    }

    bool Learn::handle (const std::string& device, int status, int channel, int a, int b, std::vector<Command>& out, std::optional<Mapping>* learned)
    {
        Mapping::Source source;
        int number = a, value = b;
        if (status == 0xB0)
            source = Mapping::Source::cc;
        else if (status == 0x90 || status == 0x80)
        {
            source = Mapping::Source::note;
            value = status == 0x80 ? 0 : b;
        }
        else if (status == 0xE0)
        {
            source = Mapping::Source::pitchBend;
            number = 0;
            value = a;   // 0..16383
        }
        else
            return false;

        std::lock_guard g (lock);
        if (waiting.has_value())
        {
            // A note let go, or a CC button's release, is not what is being learned.
            if ((source == Mapping::Source::note && value == 0))
                return true;
            Mapping m;
            m.source = source;
            m.channel = channel;
            m.number = number;
            m.device = device;
            m.target = *waiting;
            m.mode = waitingContinuous && source != Mapping::Source::note ? Mapping::Mode::absolute : Mapping::Mode::button;
            // One control, one job: a control mapped before is mapped again.
            list.erase (std::remove_if (list.begin(), list.end(), [&] (const Mapping& x) { return matches (x, device, source, channel, number) && x.device == device; }),
                        list.end());
            list.push_back (m);
            previous.clear();
            previous[list.size() - 1] = value;
            waiting.reset();
            if (learned != nullptr)
                *learned = m;
            return true;
        }

        bool mapped = false;
        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto& m = list[i];
            if (! matches (m, device, source, channel, number))
                continue;
            mapped = true;
            Command c = m.target;
            const float range = source == Mapping::Source::pitchBend ? 16383.0f : 127.0f;
            const auto* target = targetOf (c);
            const bool continuous = target != nullptr && target->continuous;
            if (m.mode == Mapping::Mode::button || ! continuous)
            {
                const int before = previous.count (i) ? previous[i] : 0;
                previous[i] = value;
                const bool press = source == Mapping::Source::note ? value > 0 : (value >= 64 && before < 64);
                if (! press)
                    continue;
                if (continuous)   // a button on a fader: to the top and back
                    c.value = 1.0f;
                out.push_back (c);
                continue;
            }
            if (m.mode == Mapping::Mode::absolute)
            {
                const float unit = (float) value / range;
                switch (c.action)
                {
                    case Action::pan: c.value = unit * 2.0f - 1.0f; break;
                    case Action::tempo: c.value = 60.0f + unit * 140.0f; break;
                    default: c.value = unit; break;
                }
                out.push_back (c);
                continue;
            }
            int steps = 0;
            switch (m.mode)
            {
                case Mapping::Mode::relativeTwos: steps = value < 64 ? value : value - 128; break;
                case Mapping::Mode::relativeOffset: steps = value - 64; break;
                case Mapping::Mode::relativeSigned: steps = value < 64 ? value : -(value - 64); break;
                default: break;
            }
            if (steps == 0)
                continue;
            switch (c.action)
            {
                case Action::volume: c.action = Action::volumeBy; c.value = (float) steps * 0.005f; break;
                case Action::pan: c.action = Action::panBy; c.value = (float) steps * 0.01f; break;
                case Action::send: c.action = Action::sendBy; c.value = (float) steps * 0.005f; break;
                case Action::master: c.action = Action::masterBy; c.value = (float) steps * 0.005f; break;
                case Action::tempo: c.action = Action::tempoBy; c.value = (float) steps; break;
                default: break;
            }
            out.push_back (c);
        }
        return mapped;
    }
}
