// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Tuning and MPE for the PHI synth: what pitch each key plays, and how MPE
// controllers bend, press and shape each note on its own. Plain C++ in phi_dsp,
// so the synth and the mixer share it.
//
// The mixer works out the tuning of each clip, track or scene (modes, ragas in just
// intonation, maqams with quarter steps, 2 to 21 notes to the octave) and sends it to
// the PHI synth as MIDI Tuning Standard messages (toSysEx), so it travels with the
// notes over any MIDI path; the synth reads them back (applySysEx).
#include <array>
#include <cstdint>
#include <vector>

namespace phi
{
    class Tuning
    {
    public:
        static constexpr int numKeys = 128;

        Tuning();                       // 12 equal steps, A4 (key 69) at 440 Hz
        double hz (int key) const;      // keys outside 0..127 are clamped
        void setHz (int key, double hz);

        // `divisions` equal steps to the octave (2 to 21, or any count up to 128: Carrillo's 96), `rootKey` at `rootHz`.
        static Tuning equal (int divisions, int rootKey = 69, double rootHz = 440.0);
        // A scale on consecutive keys: `cents` are its degrees above the tonic (the first is
        // usually 0), repeating every `periodCents` (1200 for an octave). `rootKey` plays the
        // tonic at `rootHz`; the keys above and below walk up and down the degrees.
        static Tuning scale (const std::vector<double>& cents, double periodCents = 1200.0,
                             int rootKey = 60, double rootHz = 261.6255653005986);
        static double ratioToCents (double ratio);   // 3/2 -> 701.955
        static double centsFromEqual (double hz, int key);   // how far `hz` is from key's 12-TET pitch

        // MIDI Tuning Standard, real-time single note tuning change (F0 7F dd 08 02 pp ll
        // [kk xx yy zz]... F7): every key, 64 keys a message. Resolution 100/16384 cent.
        std::vector<std::vector<std::uint8_t>> toSysEx (int program = 0, int device = 0x7f) const;
        // Takes in a single note tuning change (real-time 08 02, or non-real-time 08 07
        // with a bank); false when `data` is not one. With or without the F0 and F7.
        bool applySysEx (const std::uint8_t* data, int size);

        bool operator== (const Tuning& other) const { return keyHz == other.keyHz; }

    private:
        std::array<double, numKeys> keyHz {};
    };

    //==========================================================================
    // MPE (MIDI Polyphonic Expression): each note on a channel of its own, so its pitch
    // bend, pressure and timbre (CC74) are its own. A zone is set by the controller's
    // MPE Configuration Message (RPN 6 on channel 1 or 16); without one, the lower zone with
    // all 15 member channels is assumed, as most MPE controllers send. A channel outside the
    // zones is ordinary MIDI: its bend (±2 semitones unless RPN 0 says otherwise) and
    // pressure apply to every note on it.
    struct Expression
    {
        double bendSemitones = 0.0;   // the member channel's bend plus the zone's manager bend
        double pressure = 0.0;        // 0..1 (channel pressure, or polyphonic aftertouch)
        double timbre = 0.5;          // 0..1 (CC74; MPE's "slide", centred by default)
    };

    class Mpe
    {
    public:
        enum class Kind { none, noteOn, noteOff, expression, allNotesOff };
        struct Message
        {
            Kind kind = Kind::none;
            int channel = 0;   // 0..15
            int key = 0;
            double velocity = 0.0;   // 0..1; note off: the release velocity
        };

        Mpe();
        void reset();
        // One MIDI channel message; returns what it means for the notes.
        Message handle (std::uint8_t status, std::uint8_t data1, std::uint8_t data2);

        // The expression a note on `channel` has now (its key for polyphonic aftertouch).
        Expression expression (int channel, int key = -1) const;
        // The pitch of `key` played on `channel`, through `tuning`, bent as it is now.
        double noteHz (const Tuning& tuning, int channel, int key) const;

        bool isMember (int channel) const;    // a per-note channel of a zone
        int lowerZoneMembers() const { return lowerMembers; }   // 0..15
        int upperZoneMembers() const { return upperMembers; }

    private:
        struct Channel
        {
            int bend = 8192;              // 14 bits, 8192 centre
            double bendRange = 2.0;       // semitones, from RPN 0
            double pressure = 0.0, timbre = 0.5;
            std::array<std::uint8_t, Tuning::numKeys> polyPressure {};
            int rpnMsb = 127, rpnLsb = 127;   // the RPN selected (127/127: none)
        };
        int managerOf (int channel) const;     // the zone's manager channel, or -1
        void configureZone (int manager, int members);

        std::array<Channel, 16> channels {};
        int lowerMembers = 15, upperMembers = 0;
    };
}
