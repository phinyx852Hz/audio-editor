// SPDX-License-Identifier: AGPL-3.0-only
// The parts every control surface shares: the palette, the lights sent only when they change,
// and the pad grid's Session, Note and Mixer views (docs/control-surfaces.md).
#include "ControlSurface.h"

#include <algorithm>
#include <cmath>

namespace control
{
    // As the mixer's own faders throw: gain = 2 x position^4.
    float unitFromDb (float db)
    {
        if (db <= -96.0f)
            return 0.0f;
        return std::clamp ((float) std::pow (std::pow (10.0, db / 20.0) / 2.0, 0.25), 0.0f, 1.0f);
    }

    float dbFromUnit (float unit)
    {
        if (unit <= 0.0005f)
            return -100.0f;
        const double p = std::min (unit, 1.0f);
        return std::clamp ((float) (20.0 * std::log10 (2.0 * p * p * p * p)), -100.0f, 6.0f);
    }

    namespace
    {
        // The hues of the palette's groups of four, from entry 4 on (red, amber, yellow, lime,
        // green, spring, turquoise, cyan, sky, ocean, blue, orchid, magenta, pink).
        constexpr std::array<float, 14> hues { 0, 30, 60, 90, 120, 145, 165, 185, 200, 215, 240, 270, 300, 330 };

        void toHsv (uint32_t rgb, float& h, float& s, float& v)
        {
            const float r = (float) ((rgb >> 16) & 0xFF) / 255.0f, g = (float) ((rgb >> 8) & 0xFF) / 255.0f, b = (float) (rgb & 0xFF) / 255.0f;
            const float hi = std::max ({ r, g, b }), lo = std::min ({ r, g, b }), d = hi - lo;
            v = hi;
            s = hi > 0.0f ? d / hi : 0.0f;
            h = 0.0f;
            if (d > 0.0f)
            {
                if (hi == r)
                    h = 60.0f * std::fmod ((g - b) / d, 6.0f);
                else if (hi == g)
                    h = 60.0f * ((b - r) / d + 2.0f);
                else
                    h = 60.0f * ((r - g) / d + 4.0f);
                if (h < 0.0f)
                    h += 360.0f;
            }
        }

        uint32_t fromHsv (float h, float s, float v)
        {
            const float c = v * s, x = c * (1.0f - std::abs (std::fmod (h / 60.0f, 2.0f) - 1.0f)), m = v - c;
            float r = 0, g = 0, b = 0;
            if (h < 60)
                r = c, g = x;
            else if (h < 120)
                r = x, g = c;
            else if (h < 180)
                g = c, b = x;
            else if (h < 240)
                g = x, b = c;
            else if (h < 300)
                r = x, b = c;
            else
                r = c, b = x;
            auto byte = [m] (float f) { return (uint32_t) std::lround (std::clamp (f + m, 0.0f, 1.0f) * 255.0f); };
            return (byte (r) << 16) | (byte (g) << 8) | byte (b);
        }
    }

    int paletteIndex (uint32_t rgb)
    {
        float h, s, v;
        toHsv (rgb, h, s, v);
        if (v < 0.08f)
            return 0;
        if (s < 0.25f)
            return v > 0.8f ? 3 : v > 0.45f ? 2 : 1;
        int group = 0;
        float best = 1000.0f;
        for (int k = 0; k < (int) hues.size(); ++k)
        {
            const float d = std::min (std::abs (h - hues[(size_t) k]), 360.0f - std::abs (h - hues[(size_t) k]));
            if (d < best)
                best = d, group = k;
        }
        const int shade = v > 0.66f ? (s < 0.5f ? 0 : 1) : v > 0.33f ? 2 : 3;
        return 4 + 4 * group + shade;
    }

    uint32_t paletteRgb (int index)
    {
        if (index <= 0 || index > 63)
            return 0;
        if (index < 4)
            return index == 1 ? 0x303030 : index == 2 ? 0x8C8C8C : 0xFFFFFF;
        const int group = (index - 4) / 4, shade = (index - 4) % 4;
        const float h = hues[(size_t) group];
        switch (shade)
        {
            case 0: return fromHsv (h, 0.45f, 1.0f);
            case 1: return fromHsv (h, 1.0f, 1.0f);
            case 2: return fromHsv (h, 1.0f, 0.5f);
            default: return fromHsv (h, 1.0f, 0.2f);
        }
    }

    uint32_t dim (uint32_t rgb, float amount)
    {
        auto part = [&] (int shift) { return (uint32_t) std::lround ((float) ((rgb >> shift) & 0xFF) * std::clamp (amount, 0.0f, 1.0f)) << shift; };
        return part (16) | part (8) | part (0);
    }

    //==============================================================================
    Messages Surface::refresh (const State& state)
    {
        last = state;
        std::map<int, Light> want;
        lights (state, want);
        std::vector<std::pair<int, Light>> changed;
        for (const auto& [led, light] : want)
        {
            const auto it = lit.find (led);
            if (it == lit.end() || ! (it->second == light))
                changed.emplace_back (led, light);
        }
        for (const auto& [led, light] : lit)
            if (want.count (led) == 0 && ! (light == Light {}))
                changed.emplace_back (led, Light {});
        Messages out;
        if (! changed.empty())
            send (changed, out);
        for (const auto& [led, light] : changed)
            lit[led] = light;
        return out;
    }

    Messages Surface::darken()
    {
        std::vector<std::pair<int, Light>> changed;
        for (const auto& [led, light] : lit)
            if (! (light == Light {}))
                changed.emplace_back (led, Light {});
        Messages out;
        if (! changed.empty())
            send (changed, out);
        lit.clear();
        return out;
    }

    //==============================================================================
    namespace
    {
        constexpr std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
        constexpr uint32_t green = 0x00FF00, red = 0xFF0000, yellow = 0xFFC000, blue = 0x0060FF, white = 0xFFFFFF;

        // The Mixer view's volume rows, top down.
        float volumeRow (int y)
        {
            static const std::array<float, 4> dbs { 0.0f, -6.0f, -15.0f, -30.0f };
            return unitFromDb (dbs[(size_t) std::clamp (y, 0, 3)]);
        }
    }

    int PadSurface::noteAt (int x, int y) const
    {
        const int below = rows - 1 - y;   // rows from the bottom
        int n;
        if (inKey)
        {
            const int degree = x + 3 * below;
            n = 12 * (octave + 1) + 12 * (degree / 7) + major[(size_t) (degree % 7)];
        }
        else
            n = 12 * (octave + 1) + x + 5 * below;
        return n >= 0 && n < 128 ? n : -1;
    }

    Command PadSurface::onTrack (Action a, int t) const
    {
        Command c;
        c.action = a;
        c.track = t;
        return c;
    }

    void PadSurface::setView (View v)
    {
        shown = v;
        std::fill (padNoteHeld.begin(), padNoteHeld.end(), 0);
        std::fill (sounding.begin(), sounding.end(), 0);
    }

    void PadSurface::cycleKnobs()
    {
        knobMode = (Knobs) (((int) knobMode + 1) % 4);
    }

    void PadSurface::pad (int x, int y, int velocity, Received& r)
    {
        if (x < 0 || x >= cols || y < 0 || y >= rows)
            return;
        if (shown == View::note)
        {
            auto& held = padNoteHeld[(size_t) (y * cols + x) % padNoteHeld.size()];
            if (velocity > 0)
            {
                const int n = noteAt (x, y);
                if (n < 0)
                    return;
                if (held != 0)   // pressed again without a release: let the old one go first
                    r.notes.push_back ({ 0x80, (uint8_t) (held - 1), 0 });
                held = n + 1;
                ++sounding[(size_t) n];
                r.notes.push_back ({ 0x90, (uint8_t) n, (uint8_t) std::clamp (velocity, 1, 127) });
            }
            else if (held != 0)
            {
                const int n = held - 1;
                held = 0;
                sounding[(size_t) n] = std::max (0, sounding[(size_t) n] - 1);
                r.notes.push_back ({ 0x80, (uint8_t) n, 0 });
            }
            return;
        }
        if (velocity <= 0 || x >= last.tracks)
            return;
        if (modifier != Modifier::none)
        {
            modifierUsed = true;
            track (x, true, r);
            return;
        }
        if (shown == View::mixer)
        {
            if (y < 4)
            {
                auto c = onTrack (Action::volume, x);
                c.value = volumeRow (y);
                r.commands.push_back (c);
            }
            else
                r.commands.push_back (onTrack (y == 4 ? Action::stopTrack : y == 5 ? Action::mute : y == 6 ? Action::solo : Action::arm, x));
            return;
        }
        if (shift)
        {
            r.commands.push_back (onTrack (Action::select, x));
            return;
        }
        auto c = onTrack (Action::launch, x);
        c.scene = offset + y;
        r.commands.push_back (c);
    }

    void PadSurface::scene (int y, bool down, Received& r)
    {
        if (! down || y < 0 || y >= rows)
            return;
        Command c;
        c.action = Action::launchScene;
        c.scene = offset + y;
        r.commands.push_back (c);
    }

    void PadSurface::track (int x, bool down, Received& r)
    {
        if (! down || x < 0 || x >= last.tracks)
            return;
        switch (modifier)
        {
            case Modifier::stop: r.commands.push_back (onTrack (Action::stopTrack, x)); break;
            case Modifier::mute: r.commands.push_back (onTrack (Action::mute, x)); break;
            case Modifier::solo: r.commands.push_back (onTrack (Action::solo, x)); break;
            case Modifier::arm: r.commands.push_back (onTrack (Action::arm, x)); break;
            case Modifier::none: r.commands.push_back (onTrack (Action::select, x)); break;
        }
        if (modifier != Modifier::none)
            modifierUsed = true;
    }

    void PadSurface::modifierButton (Modifier m, bool down, Received& r)
    {
        if (down)
        {
            modifier = m;
            modifierUsed = false;
            return;
        }
        if (modifier != m)
            return;
        modifier = Modifier::none;
        if (modifierUsed)
            return;
        switch (m)
        {
            case Modifier::stop: r.commands.push_back (shift ? Command { Action::stopAll } : onTrack (Action::stopTrack, -1)); break;
            case Modifier::mute: r.commands.push_back (onTrack (Action::mute, -1)); break;
            case Modifier::solo: r.commands.push_back (onTrack (Action::solo, -1)); break;
            case Modifier::arm: r.commands.push_back (onTrack (Action::arm, -1)); break;
            case Modifier::none: break;
        }
    }

    void PadSurface::arrow (int dx, int dy, bool down, Received& r)
    {
        if (! down)
            return;
        if (shown == View::note)
        {
            if (dy != 0)
                octave = std::clamp (octave - dy, 0, 8);
        }
        else if (dy != 0)
            offset = std::clamp (offset + dy * (shift ? rows : 1), 0, maxScenes - rows);
        if (dx != 0)
        {
            Command c;
            c.action = Action::selectBy;
            c.value = (float) dx;
            r.commands.push_back (c);
        }
    }

    void PadSurface::knob (int index, float delta, Received& r)
    {
        if (index < 0 || index >= maxTracks || index >= last.tracks || delta == 0.0f)
            return;
        Command c = onTrack (knobMode == Knobs::volume ? Action::volumeBy : knobMode == Knobs::pan ? Action::panBy : Action::sendBy, index);
        c.index = knobMode == Knobs::sendB ? 1 : 0;
        c.value = knobMode == Knobs::pan ? delta * 2.0f : delta;
        r.commands.push_back (c);
    }

    void PadSurface::knobAbsolute (int index, float unit, Received& r)
    {
        if (index < 0 || index >= maxTracks || index >= last.tracks)
            return;
        Command c = onTrack (knobMode == Knobs::volume ? Action::volume : knobMode == Knobs::pan ? Action::pan : Action::send, index);
        c.index = knobMode == Knobs::sendB ? 1 : 0;
        c.value = knobMode == Knobs::pan ? unit * 2.0f - 1.0f : unit;
        r.commands.push_back (c);
    }

    void PadSurface::fader (int index, float unit, Received& r)
    {
        if (index < 0 || index >= maxTracks || index >= last.tracks)
            return;
        Command c = onTrack (Action::volume, index);
        c.value = unit;
        r.commands.push_back (c);
    }

    void PadSurface::masterKnob (float delta, Received& r)
    {
        Command c;
        c.action = Action::masterBy;
        c.value = delta;
        r.commands.push_back (c);
    }

    void PadSurface::padLights (const State& s, std::map<int, Light>& out) const
    {
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x)
            {
                const int led = padLed (x, y);
                Light l;
                if (shown == View::note)
                {
                    const int n = noteAt (x, y);
                    if (n >= 0)
                    {
                        const int pc = n % 12;
                        if (sounding[(size_t) n] > 0)
                            l.rgb = green;
                        else if (pc == 0)
                            l.rgb = s.selected >= 0 && s.selected < maxTracks ? s.track[(size_t) s.selected].rgb : white;
                        else if (std::find (major.begin(), major.end(), pc) != major.end())
                            l.rgb = 0x303030;
                    }
                }
                else if (x < s.tracks)
                {
                    const auto& t = s.track[(size_t) x];
                    if (shown == View::mixer)
                    {
                        if (y < 4)
                            l.rgb = t.volume >= volumeRow (y) - 0.005f ? t.rgb : 0;
                        else if (y == 4)
                            l.rgb = t.playing ? red : dim (red, 0.15f);
                        else if (y == 5)
                            l.rgb = t.mute ? yellow : dim (yellow, 0.15f);
                        else if (y == 6)
                            l.rgb = t.solo ? blue : dim (blue, 0.15f);
                        else
                            l.rgb = t.armed ? red : dim (red, 0.15f);
                    }
                    else if (offset + y < maxScenes)
                    {
                        const auto& slot = s.slot[(size_t) x][(size_t) (offset + y)];
                        if (slot.recording)
                            l = { red, slot.queued ? Anim::blink : Anim::pulse };
                        else if (slot.queued)
                            l = { slot.clip ? t.rgb : red, Anim::blink };
                        else if (slot.playing)
                            l = { green, Anim::pulse };
                        else if (slot.clip)
                            l.rgb = t.rgb;
                        else if (t.armed)
                            l.rgb = dim (red, 0.2f);
                    }
                }
                out[led] = l;
            }
    }

    void PadSurface::sceneLights (const State& s, std::map<int, Light>& out) const
    {
        for (int y = 0; y < rows; ++y)
        {
            const int led = sceneLed (y);
            if (led < 0)
                continue;
            Light l;
            const int sc = offset + y;
            if (shown != View::note && sc < maxScenes)
            {
                bool any = false, playing = false;
                for (int t = 0; t < s.tracks; ++t)
                {
                    any |= s.slot[(size_t) t][(size_t) sc].clip;
                    playing |= s.slot[(size_t) t][(size_t) sc].playing;
                }
                l.rgb = playing ? green : any ? 0x303030 : 0;
            }
            out[led] = l;
        }
    }

    void PadSurface::trackLights (const State& s, std::map<int, Light>& out) const
    {
        for (int x = 0; x < cols; ++x)
        {
            const int led = trackLed (x);
            if (led < 0)
                continue;
            Light l;
            if (x < s.tracks)
            {
                const auto& t = s.track[(size_t) x];
                switch (modifier)
                {
                    case Modifier::stop: l.rgb = t.playing ? red : dim (red, 0.15f); break;
                    case Modifier::mute: l.rgb = t.mute ? yellow : dim (yellow, 0.15f); break;
                    case Modifier::solo: l.rgb = t.solo ? blue : dim (blue, 0.15f); break;
                    case Modifier::arm: l.rgb = t.armed ? red : dim (red, 0.15f); break;
                    case Modifier::none: l.rgb = x == s.selected ? t.rgb : dim (t.rgb, 0.25f); break;
                }
            }
            out[led] = l;
        }
    }

    //==============================================================================
    const Kind* kindOf (const std::string& portName)
    {
        for (const auto& k : kinds())
        {
            const bool all = std::all_of (k.match.begin(), k.match.end(), [&] (const std::string& m) { return portName.find (m) != std::string::npos; });
            const bool none = std::none_of (k.exclude.begin(), k.exclude.end(), [&] (const std::string& m) { return portName.find (m) != std::string::npos; });
            if (all && none)
                return &k;
        }
        return nullptr;
    }

    int pickPort (const Kind& kind, const std::vector<std::string>& names)
    {
        for (const auto& p : kind.prefer)
            for (size_t i = 0; i < names.size(); ++i)
                if (names[i].find (p) != std::string::npos)
                    return (int) i;
        if (names.empty())
            return -1;
        std::vector<size_t> order (names.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return names[a] < names[b]; });
        return (int) order[(size_t) std::clamp (kind.fallbackPort, 0, (int) order.size() - 1)];
    }
}
