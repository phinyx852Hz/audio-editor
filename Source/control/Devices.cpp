// SPDX-License-Identifier: AGPL-3.0-only
// The devices: Novation Launchpad (X, Mini MK3, Pro MK3, Pro, MK2), Novation Launchkey (MK3,
// Mini MK3, and MK4 as an MK3) and Ableton Push 2 and 3 (docs/control-surfaces.md).
// The port names, programmer and DAW modes, button and pad numbers, SysEx and LED channels are
// ported from DrivenByMoss by Jürgen Moßgraber (https://github.com/git-moss/DrivenByMoss,
// LGPL-3.0; de.mossgrabers.controller.novation.launchpad, .novation.launchkey and .ableton.push),
// and checked against Novation's Programmer's Reference Guides and Ableton's Push 2 MIDI and
// Display Interface Manual.
#include "ControlSurface.h"

#include <algorithm>
#include <cmath>

namespace control
{
    namespace
    {
        Message sysex (std::initializer_list<int> header, std::initializer_list<int> body)
        {
            Message m;
            for (int b : header)
                m.push_back ((uint8_t) b);
            for (int b : body)
                m.push_back ((uint8_t) b);
            m.push_back (0xF7);
            return m;
        }

        // A MIDI message's parts: the status without its channel, the channel 0..15, data.
        struct Midi
        {
            int type = 0, channel = 0, a = 0, b = 0;
            bool noteOn() const { return type == 0x90 && b > 0; }
            bool noteOff() const { return type == 0x80 || (type == 0x90 && b == 0); }
            bool cc() const { return type == 0xB0; }
        };

        bool parse (const uint8_t* data, size_t size, Midi& m)
        {
            if (size < 2 || data[0] < 0x80 || data[0] >= 0xF0)
                return false;
            m.type = data[0] & 0xF0;
            m.channel = data[0] & 0x0F;
            m.a = data[1];
            m.b = size > 2 ? data[2] : 0;
            return true;
        }

        constexpr uint32_t white = 0xFFFFFF, green = 0x00FF00, red = 0xFF0000;
        constexpr int buttonLed = 1000;   // LED ids of buttons sent as CCs, apart from pads sent as notes
    }

    //==============================================================================
    // Launchpads in programmer mode: pads 11..88 (row 1 at the bottom), scene buttons 19..89 down
    // the right, buttons round the edge as CCs.
    namespace
    {
        enum class LaunchpadModel { x, miniMk3, proMk3, pro, mk2 };

        class Launchpad final : public PadSurface
        {
        public:
            explicit Launchpad (LaunchpadModel m) : PadSurface (8, 8), model (m) {}

            std::string name() const override
            {
                switch (model)
                {
                    case LaunchpadModel::x: return "Launchpad X";
                    case LaunchpadModel::miniMk3: return "Launchpad Mini MK3";
                    case LaunchpadModel::proMk3: return "Launchpad Pro MK3";
                    case LaunchpadModel::pro: return "Launchpad Pro";
                    case LaunchpadModel::mk2: return "Launchpad MK2";
                }
                return "Launchpad";
            }

            Messages start() override
            {
                switch (model)
                {
                    case LaunchpadModel::pro: return { header ({ 0x2C, 0x03 }) };
                    case LaunchpadModel::mk2: return { header ({ 0x22, 0x00 }) };
                    default: return { header ({ 0x0E, 0x01 }) };
                }
            }

            Messages stop() override
            {
                switch (model)
                {
                    case LaunchpadModel::pro: return { header ({ 0x2C, 0x00 }) };
                    case LaunchpadModel::mk2: return {};
                    default: return { header ({ 0x0E, 0x00 }) };
                }
            }

            void receive (const uint8_t* data, size_t size, Received& r) override
            {
                Midi m;
                if (! parse (data, size, m))
                    return;
                if (m.type == 0x90 || m.type == 0x80)
                {
                    const int row = m.a / 10, col = m.a % 10;
                    const int velocity = m.noteOff() ? 0 : m.b;
                    if (row >= 1 && row <= 8 && col >= 1 && col <= 8)
                        pad (col - 1, 8 - row, velocity, r);
                    else if (model == LaunchpadModel::mk2 && col == 9 && row >= 1 && row <= 8)
                        scene (8 - row, velocity > 0, r);
                    return;
                }
                if (! m.cc())
                    return;
                const bool down = m.b > 0;
                if (model != LaunchpadModel::mk2 && m.a % 10 == 9 && m.a >= 19 && m.a <= 89)
                {
                    scene (8 - m.a / 10, down, r);
                    return;
                }
                if (model == LaunchpadModel::proMk3 || model == LaunchpadModel::pro)
                {
                    if (m.a >= 101 && m.a <= 108)
                    {
                        track (m.a - 101, down, r);
                        return;
                    }
                }
                button (m.a, down, r);
            }

        protected:
            int padLed (int x, int y) const override { return (8 - y) * 10 + x + 1; }
            int sceneLed (int y) const override { return (8 - y) * 10 + 9; }
            int trackLed (int x) const override
            {
                return model == LaunchpadModel::proMk3 || model == LaunchpadModel::pro ? 101 + x : -1;
            }

            void lights (const State& s, std::map<int, Light>& out) override
            {
                padLights (s, out);
                sceneLights (s, out);
                trackLights (s, out);
                auto lit = [&] (int cc, uint32_t rgb) { if (cc > 0) out[cc] = { rgb, Anim::steady }; };
                const auto v = view();
                lit (ccOf (Button::up), dim (white, 0.2f));
                lit (ccOf (Button::down), dim (white, 0.2f));
                lit (ccOf (Button::left), dim (white, 0.2f));
                lit (ccOf (Button::right), dim (white, 0.2f));
                lit (ccOf (Button::session), v == View::session ? green : dim (white, 0.2f));
                lit (ccOf (Button::note), v == View::note ? green : dim (white, 0.2f));
                lit (ccOf (Button::mixer), v == View::mixer ? green : dim (white, 0.2f));
                lit (ccOf (Button::shift), shift ? white : dim (white, 0.2f));
                lit (ccOf (Button::play), s.playing ? green : dim (green, 0.2f));
                lit (ccOf (Button::record), s.recording ? red : dim (red, 0.2f));
                lit (ccOf (Button::click), s.metronome ? white : dim (white, 0.2f));
                lit (ccOf (Button::undo), dim (white, 0.2f));
                lit (ccOf (Button::stopMod), modifier == Modifier::stop ? red : dim (red, 0.2f));
                lit (ccOf (Button::muteMod), modifier == Modifier::mute ? 0xFFC000 : dim (0xFFC000, 0.2f));
                lit (ccOf (Button::soloMod), modifier == Modifier::solo ? 0x0060FF : dim (0x0060FF, 0.2f));
                lit (ccOf (Button::armMod), modifier == Modifier::arm ? red : dim (red, 0.2f));
            }

            void send (const std::vector<std::pair<int, Light>>& changed, Messages& out) override
            {
                if (model == LaunchpadModel::pro || model == LaunchpadModel::mk2)
                {
                    // Palette colours: 0A sets, 23 flashes, 28 pulses (the MK2 with a 00 before each LED).
                    Message steady = header ({ 0x0A }), flash = header ({ 0x23 }), pulse = header ({ 0x28 });
                    for (auto* m : { &steady, &flash, &pulse })
                        m->pop_back();
                    bool anyFlash = false, anyPulse = false;
                    for (const auto& [led, light] : changed)
                    {
                        const auto index = (uint8_t) paletteIndex (light.rgb);
                        steady.push_back ((uint8_t) led);
                        steady.push_back (light.anim == Anim::steady ? index : (uint8_t) paletteIndex (dim (light.rgb, 0.25f)));
                        if (light.anim == Anim::steady)
                            continue;
                        auto& m = light.anim == Anim::blink ? flash : pulse;
                        (light.anim == Anim::blink ? anyFlash : anyPulse) = true;
                        if (model == LaunchpadModel::mk2)
                            m.push_back (0x00);
                        m.push_back ((uint8_t) led);
                        m.push_back (index);
                    }
                    steady.push_back (0xF7);
                    out.push_back (steady);
                    if (anyFlash)
                        flash.push_back (0xF7), out.push_back (flash);
                    if (anyPulse)
                        pulse.push_back (0xF7), out.push_back (pulse);
                    return;
                }
                // 03: a list of LEDs, each steady RGB (3), flashing between two palette colours (1),
                // or pulsing in one (2).
                Message m = header ({ 0x03 });
                m.pop_back();
                for (const auto& [led, light] : changed)
                {
                    if (light.anim == Anim::steady)
                    {
                        m.insert (m.end(), { 0x03, (uint8_t) led, (uint8_t) ((light.rgb >> 17) & 0x7F), (uint8_t) ((light.rgb >> 9) & 0x7F),
                                             (uint8_t) ((light.rgb >> 1) & 0x7F) });
                    }
                    else if (light.anim == Anim::blink)
                        m.insert (m.end(), { 0x01, (uint8_t) led, 0x00, (uint8_t) paletteIndex (light.rgb) });
                    else
                        m.insert (m.end(), { 0x02, (uint8_t) led, (uint8_t) paletteIndex (light.rgb) });
                }
                m.push_back (0xF7);
                out.push_back (m);
            }

        private:
            enum class Button { none, up, down, left, right, session, note, mixer, shift, play, record, click, undo, stopMod, muteMod, soloMod, armMod };

            Message header (std::initializer_list<int> body) const
            {
                const int id = model == LaunchpadModel::x ? 0x0C : model == LaunchpadModel::miniMk3 ? 0x0D : model == LaunchpadModel::proMk3 ? 0x0E
                               : model == LaunchpadModel::pro                                         ? 0x10
                                                                                                      : 0x18;
                return sysex ({ 0xF0, 0x00, 0x20, 0x29, 0x02, id }, body);
            }

            // The buttons' CC numbers on each model (0: it has none).
            int ccOf (Button b) const
            {
                switch (model)
                {
                    case LaunchpadModel::x:
                    case LaunchpadModel::miniMk3:
                        switch (b)
                        {
                            case Button::up: return 91;
                            case Button::down: return 92;
                            case Button::left: return 93;
                            case Button::right: return 94;
                            case Button::session: return 95;
                            case Button::note: return 96;
                            case Button::mixer: return 97;
                            case Button::shift: return 98;
                            default: return 0;
                        }
                    case LaunchpadModel::mk2:
                        switch (b)
                        {
                            case Button::up: return 104;
                            case Button::down: return 105;
                            case Button::left: return 106;
                            case Button::right: return 107;
                            case Button::session: return 108;
                            case Button::note: return 109;
                            case Button::mixer: return 110;
                            case Button::shift: return 111;
                            default: return 0;
                        }
                    case LaunchpadModel::proMk3:
                        switch (b)
                        {
                            case Button::up: return 80;
                            case Button::down: return 70;
                            case Button::left: return 91;
                            case Button::right: return 92;
                            case Button::session: return 93;
                            case Button::note: return 94;
                            case Button::mixer: return 4;   // Volume
                            case Button::shift: return 90;
                            case Button::play: return 20;
                            case Button::record: return 10;
                            case Button::armMod: return 1;
                            case Button::muteMod: return 2;
                            case Button::soloMod: return 3;
                            case Button::stopMod: return 8;
                            default: return 0;
                        }
                    case LaunchpadModel::pro:
                        switch (b)
                        {
                            case Button::up: return 91;
                            case Button::down: return 92;
                            case Button::left: return 93;
                            case Button::right: return 94;
                            case Button::session: return 95;
                            case Button::note: return 96;
                            case Button::mixer: return 5;   // Volume
                            case Button::shift: return 80;
                            case Button::click: return 70;
                            case Button::undo: return 60;
                            case Button::play: return 20;
                            case Button::record: return 10;
                            case Button::armMod: return 1;
                            case Button::muteMod: return 3;
                            case Button::soloMod: return 4;
                            case Button::stopMod: return 8;
                            default: return 0;
                        }
                }
                return 0;
            }

            void button (int cc, bool down, Received& r)
            {
                Button b = Button::none;
                for (int k = (int) Button::up; k <= (int) Button::armMod; ++k)
                    if (ccOf ((Button) k) == cc)
                        b = (Button) k;
                // Shift held: the Pro MK3's Arm and Solo are Undo and Click, as printed under them.
                if (model == LaunchpadModel::proMk3 && shift && (b == Button::armMod || b == Button::soloMod))
                    b = b == Button::armMod ? Button::undo : Button::click;
                auto command = [&] (Action a)
                {
                    if (down)
                        r.commands.push_back ({ a });
                };
                switch (b)
                {
                    case Button::up: arrow (0, -1, down, r); break;
                    case Button::down: arrow (0, 1, down, r); break;
                    case Button::left: arrow (-1, 0, down, r); break;
                    case Button::right: arrow (1, 0, down, r); break;
                    case Button::session:
                        if (down)
                        {
                            setView (View::session);
                            r.commands.push_back ({ Action::showLauncher });
                        }
                        break;
                    case Button::note:
                        if (down)
                            setView (view() == View::note ? View::session : View::note);
                        break;
                    case Button::mixer:
                        if (down)
                            setView (view() == View::mixer ? View::session : View::mixer);
                        break;
                    case Button::shift:
                        // Tapped alone on the X and the Mini (Capture MIDI and its fourth mode button): Capture.
                        if (down)
                            shift = true, shiftUsed = false;
                        else
                        {
                            shift = false;
                            if (! shiftUsed && (model == LaunchpadModel::x || model == LaunchpadModel::miniMk3))
                                r.commands.push_back ({ Action::capture });
                        }
                        return;
                    case Button::play: command (shift ? Action::stop : Action::togglePlay); break;
                    case Button::record: command (shift ? Action::capture : Action::record); break;
                    case Button::click: command (Action::metronome); break;
                    case Button::undo: command (shift ? Action::redo : Action::undo); break;
                    case Button::stopMod: modifierButton (Modifier::stop, down, r); break;
                    case Button::muteMod: modifierButton (Modifier::mute, down, r); break;
                    case Button::soloMod: modifierButton (Modifier::solo, down, r); break;
                    case Button::armMod: modifierButton (Modifier::arm, down, r); break;
                    case Button::none: break;
                }
                if (shift && down)
                    shiftUsed = true;
            }

            LaunchpadModel model;
            bool shiftUsed = false;
        };

        //==============================================================================
        // Launchkeys in DAW mode, on their DAW port (the keys stay on the other port and play as a
        // keyboard): 2 rows of 8 pads for two scenes, 8 knobs, 8 faders and their buttons on the
        // bigger ones, and transport buttons.
        class Launchkey final : public PadSurface
        {
        public:
            explicit Launchkey (std::string n) : PadSurface (8, 2), label (std::move (n)) {}

            std::string name() const override { return label; }

            Messages start() override
            {
                return { { 0x9F, 0x0C, 0x7F },     // DAW online
                         { 0xBF, 0x03, 0x02 },     // pads: Session
                         { 0xBF, 0x09, 0x01 } };   // knobs: Volume
            }

            Messages stop() override { return { { 0x9F, 0x0C, 0x00 } }; }

            void receive (const uint8_t* data, size_t size, Received& r) override
            {
                Midi m;
                if (! parse (data, size, m))
                    return;
                if (m.type == 0x90 || m.type == 0x80)
                {
                    const int velocity = m.noteOff() ? 0 : m.b;
                    if (m.channel == 9)
                        r.notes.push_back (Message (data, data + size));   // Drum pads play the armed track
                    else if (m.a >= 96 && m.a <= 103)
                        pad (m.a - 96, 0, velocity, r);
                    else if (m.a >= 112 && m.a <= 119)
                        pad (m.a - 112, 1, velocity, r);
                    return;
                }
                if (m.type == 0xA0 || m.type == 0xD0)
                    return;   // pad pressure
                if (! m.cc())
                    return;
                const bool down = m.b > 0;
                const float unit = (float) m.b / 127.0f;
                if (m.a >= 0x15 && m.a <= 0x1C)
                    knobAbsolute (m.a - 0x15, unit, r);
                else if (m.a >= 0x35 && m.a <= 0x3C)
                    fader (m.a - 0x35, unit, r);
                else if (m.a == 0x3D)
                {
                    Command c;
                    c.action = Action::master;
                    c.value = unit;
                    r.commands.push_back (c);
                }
                else if (m.a >= 0x25 && m.a <= 0x2C)
                {
                    if (down)
                    {
                        Command c;
                        c.action = armButtons ? Action::arm : Action::select;
                        c.track = m.a - 0x25;
                        r.commands.push_back (c);
                    }
                }
                else if (m.a == 0x2D)
                {
                    if (down)
                        armButtons = ! armButtons;
                }
                else if (m.a == 0x03 && m.channel == 15)
                    padsInSession = m.b == 2;   // the device says which pad mode it is in
                else if (m.a == 0x09 && m.channel == 15)
                    setKnobs (m.b == 3 ? Knobs::pan : m.b == 4 ? Knobs::sendA : m.b == 5 ? Knobs::sendB : Knobs::volume);
                else if (m.a == 0x68)
                    scene (0, down, r);
                else if (m.a == 0x69)
                    scene (1, down, r);
                else if (m.a == 0x66)
                    arrow (1, 0, down, r);
                else if (m.a == 0x67)
                    arrow (-1, 0, down, r);
                else if (m.a == 0x6A)
                    arrow (0, -1, down, r);
                else if (m.a == 0x6B)
                    arrow (0, 1, down, r);
                else if (m.a == 0x6C)
                    shift = down;
                else if (down)
                {
                    Action a = Action::none;
                    switch (m.a)
                    {
                        case 0x73: a = shift ? Action::stop : Action::togglePlay; break;
                        case 0x74: a = Action::stop; break;
                        case 0x75: a = shift ? Action::capture : Action::record; break;
                        case 0x76: a = Action::loop; break;
                        case 0x4A: a = Action::capture; break;
                        case 0x4C: a = Action::metronome; break;
                        case 0x4D: a = shift ? Action::redo : Action::undo; break;
                        default: break;
                    }
                    if (a != Action::none)
                        r.commands.push_back ({ a });
                }
            }

        protected:
            int padLed (int x, int y) const override { return (y == 0 ? 96 : 112) + x; }
            int sceneLed (int y) const override { return buttonLed + (y == 0 ? 0x68 : 0x69); }

            void lights (const State& s, std::map<int, Light>& out) override
            {
                if (padsInSession)
                    padLights (s, out);
                sceneLights (s, out);
                // The fader buttons: armed tracks red, or the selected track in its colour.
                for (int x = 0; x < 8; ++x)
                {
                    Light l;
                    if (x < s.tracks)
                    {
                        const auto& t = s.track[(size_t) x];
                        l.rgb = armButtons ? (t.armed ? red : dim (red, 0.15f)) : (x == s.selected ? t.rgb : dim (t.rgb, 0.2f));
                    }
                    out[buttonLed + 0x25 + x] = l;
                }
                out[buttonLed + 0x2D] = { armButtons ? red : white, Anim::steady };
                out[buttonLed + 0x73] = { s.playing ? white : dim (white, 0.2f), Anim::steady };
                out[buttonLed + 0x75] = { s.recording ? white : dim (white, 0.2f), Anim::steady };
                out[buttonLed + 0x76] = { s.loop ? white : dim (white, 0.2f), Anim::steady };
                out[buttonLed + 0x4C] = { s.metronome ? white : dim (white, 0.2f), Anim::steady };
            }

            void send (const std::vector<std::pair<int, Light>>& changed, Messages& out) override
            {
                for (const auto& [led, light] : changed)
                {
                    const auto index = (uint8_t) paletteIndex (light.rgb);
                    if (led < buttonLed)
                    {
                        out.push_back ({ 0x90, (uint8_t) led, light.anim == Anim::steady ? index : (uint8_t) paletteIndex (dim (light.rgb, 0.25f)) });
                        if (light.anim != Anim::steady)   // channel 2 flashes, 3 pulses
                            out.push_back ({ (uint8_t) (light.anim == Anim::blink ? 0x91 : 0x92), (uint8_t) led, index });
                        continue;
                    }
                    const int cc = led - buttonLed;
                    // The transport buttons are white, lit on channel 16 by brightness.
                    if (cc >= 0x73 || cc == 0x4C)
                        out.push_back ({ 0xBF, (uint8_t) cc, (uint8_t) (light.rgb == 0 ? 0 : (light.rgb & 0xFF) > 0x80 ? 127 : 20) });
                    else
                        out.push_back ({ 0xB0, (uint8_t) cc, index });
                }
            }

        private:
            std::string label;
            bool armButtons = false;     // the fader buttons arm their tracks, else select them
            bool padsInSession = true;
        };

        //==============================================================================
        // Push 2 and 3 on their Live port: 64 pads (notes 36..99 from the bottom left), the scene
        // buttons, two rows of 8 buttons by the display, 8 encoders (relative), the master and tempo
        // encoders and the transport. PHI writes its palette into the pads' colour table.
        class Push final : public PadSurface
        {
        public:
            explicit Push (bool three) : PadSurface (8, 8), push3 (three) {}

            std::string name() const override { return push3 ? "Push 3" : "Push 2"; }

            Messages start() override
            {
                Messages m;
                if (! push3)
                    m.push_back (header ({ 0x0A, 0x00 }));   // MIDI mode: Live port
                for (int i = 1; i < 64; ++i)
                {
                    const auto rgb = paletteRgb (i);
                    const int r = (int) (rgb >> 16) & 0xFF, g = (int) (rgb >> 8) & 0xFF, b = (int) rgb & 0xFF, w = std::min ({ r, g, b });
                    m.push_back (header ({ 0x03, i, r & 0x7F, r >> 7, g & 0x7F, g >> 7, b & 0x7F, b >> 7, w & 0x7F, w >> 7 }));
                }
                m.push_back (header ({ 0x05 }));         // apply the palette
                m.push_back (header ({ 0x1E, 0x00 }));   // channel pressure, not poly
                return m;
            }

            Messages stop() override { return {}; }

            void receive (const uint8_t* data, size_t size, Received& r) override
            {
                Midi m;
                if (! parse (data, size, m))
                    return;
                if (m.type == 0x90 || m.type == 0x80)
                {
                    if (m.a >= 36 && m.a <= 99)
                        pad ((m.a - 36) % 8, 7 - (m.a - 36) / 8, m.noteOff() ? 0 : m.b, r);
                    return;   // below 36: the encoders and the touch strip touched
                }
                if (m.type == 0xE0 || m.type == 0xD0)
                {
                    if (view() == View::note)   // the touch strip bends; pad pressure
                        r.notes.push_back (Message (data, data + size));
                    return;
                }
                if (! m.cc())
                    return;
                const bool down = m.b > 0;
                if ((m.a >= 71 && m.a <= 79) || m.a == 14 || m.a == 15)
                {
                    const int steps = m.b < 64 ? m.b : m.b - 128;
                    if (m.a == 79)
                        masterKnob ((float) steps * (shift ? 0.001f : 0.005f), r);
                    else if (m.a == 14)
                    {
                        Command c;
                        c.action = Action::tempoBy;
                        c.value = (float) steps * (shift ? 0.1f : 1.0f);
                        r.commands.push_back (c);
                    }
                    else if (m.a != 15)
                        knob (m.a - 71, (float) steps * (shift ? 0.001f : 0.005f), r);
                    return;
                }
                if (m.a >= 20 && m.a <= 27)
                {
                    track (m.a - 20, down, r);
                    return;
                }
                if (m.a >= 102 && m.a <= 109)
                {
                    if (down && m.a - 102 < last.tracks)
                    {
                        Command c;
                        c.action = Action::arm;
                        c.track = m.a - 102;
                        r.commands.push_back (c);
                    }
                    return;
                }
                if (m.a >= 36 && m.a <= 43)
                {
                    scene (m.a - 36, down, r);
                    return;
                }
                auto command = [&] (Action a)
                {
                    if (down)
                        r.commands.push_back ({ a });
                };
                switch (m.a)
                {
                    case 49: shift = down; break;
                    case 85: command (shift ? Action::stop : Action::togglePlay); break;
                    case 86: command (shift ? Action::capture : Action::record); break;
                    case 65: command (Action::capture); break;
                    case 9: command (Action::metronome); break;
                    case 3: command (Action::tapTempo); break;
                    case 119: command (shift ? Action::redo : Action::undo); break;
                    case 44: arrow (-1, 0, down, r); break;
                    case 45: arrow (1, 0, down, r); break;
                    case 46: arrow (0, -1, down, r); break;
                    case 47: arrow (0, 1, down, r); break;
                    case 54: if (down) octave = std::max (0, octave - 1); break;
                    case 55: if (down) octave = std::min (8, octave + 1); break;
                    case 58: if (down) inKey = ! inKey; break;
                    case 50: if (down) setView (View::note); break;
                    case 51:
                        if (down)
                        {
                            setView (View::session);
                            r.commands.push_back ({ Action::showLauncher });
                        }
                        break;
                    case 31: if (down) setView (view() == View::mixer ? View::session : View::mixer); break;   // Layout
                    case 112: if (down) cycleKnobs(); break;   // Mix
                    case 60: modifierButton (Modifier::mute, down, r); break;
                    case 61: modifierButton (Modifier::solo, down, r); break;
                    case 29: modifierButton (Modifier::stop, down, r); break;
                    default: break;
                }
            }

        protected:
            int padLed (int x, int y) const override { return 36 + (7 - y) * 8 + x; }
            int sceneLed (int y) const override { return buttonLed + 36 + y; }
            int trackLed (int x) const override { return buttonLed + 20 + x; }

            void lights (const State& s, std::map<int, Light>& out) override
            {
                padLights (s, out);
                sceneLights (s, out);
                trackLights (s, out);
                for (int x = 0; x < 8; ++x)
                    out[buttonLed + 102 + x] = { x < s.tracks ? (s.track[(size_t) x].armed ? red : dim (red, 0.15f)) : 0, Anim::steady };
                auto button = [&] (int cc, bool on) { out[buttonLed + cc] = { on ? white : dim (white, 0.2f), Anim::steady }; };
                out[buttonLed + 85] = { s.playing ? green : dim (green, 0.2f), Anim::steady };
                out[buttonLed + 86] = { s.recording ? red : dim (red, 0.2f), Anim::steady };
                button (9, s.metronome);
                button (49, shift);
                button (50, view() == View::note);
                button (51, view() == View::session);
                button (31, view() == View::mixer);
                button (60, modifier == Modifier::mute);
                button (61, modifier == Modifier::solo);
                button (29, modifier == Modifier::stop);
                button (54, view() == View::note);
                button (55, view() == View::note);
                button (58, inKey && view() == View::note);
                for (int cc : { 3, 44, 45, 46, 47, 112, 119 })
                    button (cc, false);
            }

            void send (const std::vector<std::pair<int, Light>>& changed, Messages& out) override
            {
                for (const auto& [led, light] : changed)
                {
                    const auto index = (uint8_t) paletteIndex (light.rgb);
                    const bool isPad = led < buttonLed;
                    const int number = isPad ? led : led - buttonLed;
                    const int cc = number;
                    // Coloured: the pads, the scene buttons, the rows by the display, Play and Record.
                    const bool rgb = isPad || (cc >= 20 && cc <= 27) || (cc >= 36 && cc <= 43) || (cc >= 102 && cc <= 109) || cc == 85 || cc == 86;
                    const uint8_t status = isPad ? 0x90 : 0xB0;
                    if (! rgb)
                    {
                        // White buttons: 0 off, 30 on, 127 bright.
                        const int v = light.rgb == 0 ? 0 : ((light.rgb >> 16) & 0xFF) > 0x80 || ((light.rgb >> 8) & 0xFF) > 0x80 ? 127 : 30;
                        out.push_back ({ status, (uint8_t) number, (uint8_t) v });
                        continue;
                    }
                    out.push_back ({ status, (uint8_t) number, light.anim == Anim::steady ? index : (uint8_t) paletteIndex (dim (light.rgb, 0.25f)) });
                    // Channel 11 pulses, 15 blinks fast.
                    if (light.anim != Anim::steady)
                        out.push_back ({ (uint8_t) (status | (light.anim == Anim::pulse ? 10 : 14)), (uint8_t) number, index });
                }
            }

        private:
            Message header (std::initializer_list<int> body) const { return sysex ({ 0xF0, 0x00, 0x21, 0x1D, 0x01, 0x01 }, body); }
            bool push3;
        };
    }

    const std::vector<Kind>& kinds()
    {
        static const std::vector<Kind> list = []
        {
            std::vector<Kind> k;
            k.push_back ({ "launchpadX", "Launchpad X", { "Launchpad X" }, {}, { "LPX MIDI", "MIDI 2" }, 1,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Launchpad> (LaunchpadModel::x); } });
            k.push_back ({ "launchpadMiniMk3", "Launchpad Mini MK3", { "Launchpad Mini MK3" }, {}, { "LPMiniMK3 MI", "MIDI 2" }, 1,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Launchpad> (LaunchpadModel::miniMk3); } });
            k.push_back ({ "launchpadProMk3", "Launchpad Pro MK3", { "Launchpad Pro MK3" }, {}, { "LPProMK3 MIDI", "MIDI 1" }, 0,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Launchpad> (LaunchpadModel::proMk3); } });
            k.push_back ({ "launchpadPro", "Launchpad Pro", { "Launchpad Pro" }, { "MK3", "Mk3" }, { "Standalone", "MIDI 2" }, 1,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Launchpad> (LaunchpadModel::pro); } });
            k.push_back ({ "launchpadMk2", "Launchpad MK2", { "Launchpad MK2" }, {}, {}, 0,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Launchpad> (LaunchpadModel::mk2); } });
            k.push_back ({ "launchkeyMiniMk3", "Launchkey Mini MK3", { "Launchkey Mini MK3" }, {}, { "DAW", "MIDI 2" }, 1,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Launchkey> ("Launchkey Mini MK3"); } });
            k.push_back ({ "launchkeyMk3", "Launchkey MK3", { "Launchkey MK3" }, {}, { "DAW", "MIDI 2" }, 1,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Launchkey> ("Launchkey MK3"); } });
            k.push_back ({ "launchkeyMk4", "Launchkey MK4", { "Launchkey", "MK4" }, {}, { "DAW", "MIDI 2" }, 1,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Launchkey> ("Launchkey MK4"); } });
            k.push_back ({ "push2", "Push 2", { "Push 2" }, {}, { "Live Port", "MIDI 1" }, 0,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Push> (false); } });
            k.push_back ({ "push3", "Push 3", { "Push 3" }, {}, { "Live Port", "MIDI 1" }, 0,
                           [] () -> std::unique_ptr<Surface> { return std::make_unique<Push> (true); } });
            return k;
        }();
        return list;
    }
}
