// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Control surfaces (docs/control-surfaces.md): Novation Launchpads and Launchkeys and Ableton
// Push 2 and 3 play the mixer's clip launcher, its strips and its transport, with their pads lit
// in the tracks' colours, the way Ableton Live and Bitwig drive them. Plain C++, no JUCE or Qt:
// a surface turns what the device sends into Commands (and notes to play), and the mixer's State
// into the MIDI messages that light the device. engine/ControlSurfaces.h opens the ports.
// The device facts (ports, modes, button numbers, SysEx, palettes) are ported from DrivenByMoss
// by Jürgen Moßgraber (https://github.com/git-moss/DrivenByMoss, LGPL-3.0).
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace control
{
    using Message = std::vector<uint8_t>;   // one MIDI message; a SysEx whole, F0 to F7
    using Messages = std::vector<Message>;

    constexpr int maxTracks = 8;
    constexpr int maxScenes = 32;

    // What the surfaces show: the mixer as it is, gathered by the UI on each refresh.
    struct SlotState
    {
        bool clip = false;
        bool playing = false;
        bool queued = false;      // waits for the quantise to launch
        bool recording = false;   // records or overdubs; with queued: waits to start recording
    };

    struct TrackState
    {
        bool armed = false, mute = false, solo = false;
        bool playing = false;     // a launcher clip plays
        uint32_t rgb = 0x808080;
        float volume = 0.0f;      // the fader as unitFromDb
        float pan = 0.0f;         // -1..1
        std::array<float, 2> send {};   // A and B, as unitFromDb
        std::string name, clip;   // the track's name and its playing clip's (Push's display)
    };

    struct State
    {
        int tracks = 0, scenes = 0;
        int selected = 0;         // the selected track
        std::array<TrackState, maxTracks> track;
        std::array<std::array<SlotState, maxScenes>, maxTracks> slot;
        bool playing = false, recording = false, metronome = false, loop = false;
        float master = 0.0f;      // as unitFromDb
        double tempo = 120.0;
    };

    // Faders and knobs as 0..1, as the mixer's faders throw: 0 is silence, 1 is +6 dB, 0 dB at 0.84.
    float unitFromDb (float db);
    float dbFromUnit (float unit);   // -100 (silence) .. +6

    // What a press, a turn or a learned control asks of the mixer. A track of -1 is the selected
    // one. Toggles take a value of -1 to flip, else 0 or 1.
    enum class Action
    {
        togglePlay, stop, record, capture, metronome, loop, undo, redo, tapTempo,
        tempo,            // value: BPM
        tempoBy,          // value: BPM up or down
        launch,           // track, scene: plays the slot; an empty one records when armed, else stops
        launchScene,      // scene
        stopTrack,        // track
        stopAll,
        select,           // track
        selectBy,         // value: tracks right (+) or left (-)
        arm, mute, solo,  // track, value
        volume, pan, send,         // track (send: index 0 A, 1 B), value: unit (pan -1..1)
        volumeBy, panBy, sendBy,   // track, value: unit up or down
        master, masterBy,          // value
        showLauncher,
        none
    };

    struct Command
    {
        Action action = Action::none;
        int track = -1;
        int scene = -1;
        int index = 0;
        float value = -1.0f;
        bool operator== (const Command&) const = default;
    };

    // What one message from a device gave: commands for the UI, and notes (and pitch bend and
    // pressure) to play straight away on the MIDI thread, as a keyboard's.
    struct Received
    {
        std::vector<Command> commands;
        Messages notes;
    };

    // The Novation colour palette (Launchpad, Launchkey; PHI writes it into Push 2 and 3 too):
    // 0 black, 1-3 greys to white, then from 4 a hue in four shades every four entries.
    int paletteIndex (uint32_t rgb);
    uint32_t paletteRgb (int index);
    uint32_t dim (uint32_t rgb, float amount);   // amount 0..1 of the brightness

    // A device: its ports found by name, its own messages, its lights.
    class Surface
    {
    public:
        virtual ~Surface() = default;
        virtual std::string name() const = 0;
        // Into the mode PHI drives it in (programmer, DAW), and back out when PHI lets it go.
        virtual Messages start() = 0;
        virtual Messages stop() = 0;
        // One message from the device (a SysEx whole). MIDI thread; the manager locks.
        virtual void receive (const uint8_t* data, size_t size, Received&) = 0;
        // The lights that changed since the last call. The first call (and after forget()) lights all.
        Messages refresh (const State&);
        Messages darken();   // every LED lit, off (before stop())
        void forget() { lit.clear(); }

        // An LED: a colour and how it moves.
        enum class Anim { steady, pulse, blink };
        struct Light
        {
            uint32_t rgb = 0;
            Anim anim = Anim::steady;
            bool operator== (const Light&) const = default;
        };

    protected:
        // The LEDs as the state wants them, by the device's own number for each.
        virtual void lights (const State&, std::map<int, Light>&) = 0;
        // Messages that set these LEDs.
        virtual void send (const std::vector<std::pair<int, Light>>& changed, Messages&) = 0;
        // The last state given, for the offsets and views worked out between refreshes.
        State last;

    private:
        std::map<int, Light> lit;
    };

    // A surface with a grid of pads (8x8, or 2x8 on a Launchkey): the clip launcher (Session),
    // the armed track's instrument (Note), or the strips (Mixer), with scene buttons beside the
    // pads and some transport buttons. Each device turns its messages into these calls.
    class PadSurface : public Surface
    {
    public:
        enum class View { session, note, mixer };
        enum class Knobs { volume, pan, sendA, sendB };
        View view() const { return shown; }
        Knobs knobs() const { return knobMode; }
        int sceneOffset() const { return offset; }

    protected:
        PadSurface (int columns, int rows) : cols (columns), rows (rows) {}

        // y counts from the top. Velocity 0 lets go.
        void pad (int x, int y, int velocity, Received&);
        void scene (int y, bool down, Received&);   // the button right of row y
        void track (int x, bool down, Received&);   // a button under column x
        // Held with a pad or a track button: that track's clip stops, or it is muted, soloed, armed.
        // Tapped on its own: the selected track (Shift with Stop: every track).
        enum class Modifier { none, stop, mute, solo, arm };
        void modifierButton (Modifier, bool down, Received&);
        void arrow (int dx, int dy, bool down, Received&);   // dy -1 up
        void setView (View);
        void cycleKnobs();
        void setKnobs (Knobs k) { knobMode = k; }
        void knob (int index, float delta, Received&);         // relative: units of a whole turn
        void knobAbsolute (int index, float unit, Received&);  // 0..1
        void fader (int index, float unit, Received&);         // always volume
        void masterKnob (float delta, Received&);

        // Lights for the pads (by their LED numbers) and the scene buttons.
        void padLights (const State&, std::map<int, Light>&) const;
        void sceneLights (const State&, std::map<int, Light>&) const;
        void trackLights (const State&, std::map<int, Light>&) const;
        virtual int padLed (int x, int y) const = 0;
        virtual int sceneLed (int y) const { (void) y; return -1; }   // -1: none
        virtual int trackLed (int x) const { (void) x; return -1; }

        const int cols, rows;
        bool shift = false;
        Modifier modifier = Modifier::none;
        bool modifierUsed = false;   // the modifier went with a button: not a tap on its own
        // Note view: rows a fourth apart, in key (C major) or chromatic.
        int octave = 3;              // the bottom-left pad is C of this octave (C3 = 48)
        bool inKey = true;

    private:
        int noteAt (int x, int y) const;   // -1: none
        Command onTrack (Action, int track) const;
        View shown = View::session;
        Knobs knobMode = Knobs::volume;
        int offset = 0;
        std::array<int, 128> sounding {};   // pads held on each note
        std::array<int, 64> padNoteHeld {};  // the note a held pad plays, +1 (0: none)
    };

    // Which device a port belongs to, and which of its ports PHI drives.
    struct Kind
    {
        std::string id;                    // "launchpadX", ...
        std::string name;                  // "Launchpad X"
        std::vector<std::string> match;    // all in the port's name
        std::vector<std::string> exclude;  // none in it
        std::vector<std::string> prefer;   // the port to drive: the first port name with one of these
        int fallbackPort = 0;              // else this one of the device's ports, sorted by name
        std::unique_ptr<Surface> (*make)();
    };
    const std::vector<Kind>& kinds();
    const Kind* kindOf (const std::string& portName);   // null: no surface
    // The port to drive among a device's ports: index into portNames, -1 none.
    int pickPort (const Kind&, const std::vector<std::string>& portNames);
}
