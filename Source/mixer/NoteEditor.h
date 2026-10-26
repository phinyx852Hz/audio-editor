// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The MIDI editor's model (owner, 2026-10-26: "a 'regular' Western, tempered tuning scale and piano
// roll. then, when a multi-cultural modal midi editor based on the tradition we're using"): notes
// whose pitch is in cents, so a note can sit anywhere between the keys; the pitch system the rows
// follow (12-TET, any of PHI's modes, ragas, maqams and laras in Tunings.h, or a Scala .scl/.kbm),
// with each tradition's own names for its degrees; the edits of a Cubase-style key editor (quantise,
// legato, step input, velocity and controllers); restyling, after MIDIRestyle (MIT, Aaron Brata
// Aditama: a melody's notes moved to the same degrees of another scale, so its contour survives);
// and the MIDI it is saved as: plain keys, MPE (a channel a note, each bent to its pitch) or
// pitch bend shared by the notes on a channel. Plain C++, no JUCE or Qt.
//
// A pitch is in cents above MIDI key 0 in 12-TET: middle C (key 60) is 6000, A4 6900.
#include "MidiFile.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace phi::scala { struct Scale; struct KeyboardMap; }

namespace mixer { struct TuningSystem; }

namespace noteedit
{
    //==========================================================================
    // The pitches a piano roll lays out: degrees in cents within a period, repeating from a tonic.
    // A step counts degrees from the tonic: 0 is the tonic, size() the tonic a period up, -1 the
    // degree below it.
    struct PitchSystem
    {
        std::string name = "12-TET";
        std::string family = "equal";        // as Tunings.h has it ("raga", "maqam", "javanese gamelan"...)
        std::vector<double> degrees;         // from 0, sorted, within the period
        double period = 1200.0;
        double tonic = 6000.0;               // the tonic's pitch (C4 for a root of C)
        std::vector<std::string> names;      // the tradition's name for each degree, when it has them

        int size() const { return (int) degrees.size(); }
        bool isTwelveTet() const;
        double pitchOf (int step) const;
        int nearestStep (double pitch) const;
        // The steps whose pitches lie from low to high, in order.
        std::vector<int> steps (double low, double high) const;
        int degreeOf (int step) const;       // 0 .. size()-1
        int periodOf (int step) const;       // periods above (or below) the tonic's
    };

    PitchSystem twelveTet (int root = 0);                       // the twelve keys, root 0 (C) .. 11 (B)
    PitchSystem fromTuning (const mixer::TuningSystem&, int root);
    // A Scala scale on its keyboard map: the tonic is what the map's middle key plays.
    PitchSystem fromScala (const phi::scala::Scale&, const phi::scala::KeyboardMap&, const std::string& name);

    // How the rows and notes are named.
    enum class Naming
    {
        tradition,   // the tradition's own: sargam for ragas, kepatihan numbers for gamelan, wǔshēng...
        letters,     // C4, E½♭4, and the cents off the nearest name when there are any
        degrees,     // 1 to n, the tonic 1, with the period: 1, 2... 1', 2'
        cents        // above the tonic
    };
    std::string label (const PitchSystem&, int step, Naming);
    std::string letterName (double pitch, bool quarterTones);   // "C4", "F♯4 +14", "E½♭4"
    std::string centsText (double cents);                       // "+14", "−3.5", "0"

    // The names a family of tunings gives its degrees (the degree's cents above the tonic, its
    // index, and the system). New modes take their family's names; a new family adds its own here.
    // Returns an empty string for a degree it has no name for (letters are shown then).
    using Namer = std::function<std::string (const PitchSystem&, int degree)>;
    void addNamer (const std::string& familyPrefix, Namer);
    // How a family marks the period a degree is in: dots above and below (sargam, kepatihan,
    // jianpu), else the octave number after the name.
    void addDotted (const std::string& familyPrefix);

    //==========================================================================
    struct Note
    {
        int64_t start = 0, length = 0;   // ticks
        double pitch = 6000.0;
        int velocity = 100;              // 1..127
        int release = 64;                // note-off velocity
        int channel = 0;                 // the file's (drums stay on channel 10, index 9)
        bool selected = false;
        // Its pitch curve (owner, 2026-10-26: "pitch curves for meend, gamaka and glides"): points of
        // (ticks from its start, cents off its pitch), in time order, straight lines between; before
        // the first and after the last the curve holds. Empty: the note holds its pitch.
        std::vector<std::pair<int64_t, double>> curve;
        int64_t end() const { return start + length; }
        int key() const;                 // the nearest key, 0..127
        double bendAt (int64_t ticksIn) const;   // the curve's cents that far into the note
    };

    struct Sequence
    {
        int ticksPerQuarter = 480;
        std::vector<Note> notes;                 // in start order after sort()
        std::vector<midifile::Event> other;      // controllers, programs, pressure, bends between notes
        std::vector<midifile::Tempo> tempos { midifile::Tempo {} };
        std::vector<std::string> trackNames;
        int64_t lastTick = 0;                    // the clip's length; at least the last note's end

        void sort();
        int64_t end() const;                     // lastTick, or the last note's end when later
        bool anySelected() const;
        // The notes an edit works on: the picked ones, or all of them when none is picked.
        std::vector<size_t> targets() const;
    };

    // The notes as they sound: each note-on with its note-off, its pitch the key plus the bend its
    // channel had for it (a bend at the note-on's own tick, as MPE and the encodings below send it,
    // with the channel's bend range from RPN 0 or MPE's ±48). Those bends and the bend-range and
    // MPE set-up messages are taken into the notes; every other event is kept in `other`.
    Sequence decode (const midifile::Song&);

    // Saving, a curved note's pitch is sent as bends on its own channel every so many ticks (keys:
    // on the note's channel, as a file's pitch wheel would be).
    enum class Encoding
    {
        keys,           // each note on its nearest key, on its own channel, no bend: the track's tuning plays it
        mpe,            // MPE, the lower zone: a channel a note, bent to its pitch
        bendChannels    // notes bent the same share a channel (as MIDIRestyle writes it): for synths without MPE
    };
    struct EncodeOptions
    {
        Encoding encoding = Encoding::mpe;
        double bendRange = 48.0;    // semitones; MPE's default 48, ordinary synths' 2
        bool keepDrums = true;      // channel 10 is left to the drums, its notes kept there as keys
    };
    struct Encoded
    {
        midifile::SongPtr song;
        int clashes = 0;      // notes that had to share a channel bent for another pitch
        int channels = 0;     // channels used for notes
    };
    Encoded encode (const Sequence&, const EncodeOptions&);
    // Keys when every note sits on a key and none is curved, else MPE.
    Encoding suggestedEncoding (const Sequence&);

    //==========================================================================
    // Edits, each on the targets (the picked notes, else all). A grid is in ticks.
    void quantizeStarts (Sequence&, int64_t grid, double strength = 1.0, double swing = 0.0);
    void quantizeLengths (Sequence&, int64_t grid);
    void quantizeEnds (Sequence&, int64_t grid);
    void legato (Sequence&, int64_t gap = 0);       // each note held to the next start (minus gap)
    void fixedLength (Sequence&, int64_t length);
    void deleteOverlaps (Sequence&, bool mono);     // same pitch only, or any pitch (mono)
    void transposeCents (Sequence&, double cents);
    // Up or down `steps` degrees of the system, each note landing on a degree.
    void transposeSteps (Sequence&, const PitchSystem&, int steps);
    void snapToSystem (Sequence&, const PitchSystem&);      // each to its nearest degree
    void setVelocity (Sequence&, int velocity);
    void scaleVelocity (Sequence&, double factor, int add = 0);   // compress/expand around 64, then add
    void rampVelocity (Sequence&, int from, int to);              // in time order across the targets
    void splitAt (Sequence&, int64_t tick);                       // the targets that cross it
    void glue (Sequence&);                                        // each with the next of the same pitch
    void deleteTargets (Sequence&);
    void selectAll (Sequence&, bool selected = true);
    void nudge (Sequence&, int64_t ticks);

    // Restyle (after MIDIRestyle): each note found as a degree of `from` (snapped to the nearest
    // when it is between them) and played as the same degree of `to`, counted from each tonic, so a
    // line that rises still rises however many degrees each scale has. `byDegree` false is the
    // nearest pitch of `to` instead. Drums (channel 10) stay.
    void restyle (Sequence&, const PitchSystem& from, const PitchSystem& to, bool byDegree = true);
    // The key a melody is in, by the Krumhansl-Kessler key profiles weighted by duration: the root
    // 0..11 and whether it is minor.
    struct Key { int root = 0; bool minor = false; };
    Key detectKey (const Sequence&);

    //==========================================================================
    // Ornaments: pitch curves written onto the targets, each replacing the curve a note had.
    // Durations are in ticks; depths in cents.
    // Mīṇḍ, a glide: from `from` (0..1 of the note) to its end the pitch moves to the next note's
    // (the next start after it, the nearest of a chord); `throughDegrees` touches each degree between on the way, as a
    // sitar's pulled string does, else it slides.
    void glideToNext (Sequence&, const PitchSystem&, double from = 0.5, bool throughDegrees = true);
    // In from a degree below (steps < 0) or above, over `ticks`.
    void slideIn (Sequence&, const PitchSystem&, int steps, int64_t ticks);
    // Āndolan, vibrato: a swing of `depth` cents at `rateHz` (a beat being a quarter at `bpm`);
    // `below` swings under the pitch only (āndolan), else either side (vibrato).
    void swing (Sequence&, double depth, double rateHz, double bpm, bool below);
    // Gamak: a shake to the degree above and back, `rateHz` times a second.
    void gamak (Sequence&, const PitchSystem&, double rateHz, double bpm);
    // Kaṇ: the degree above (steps 1) or below (-1) touched for `ticks` as the note starts.
    void kan (Sequence&, const PitchSystem&, int steps, int64_t ticks);
    // A turn, each step `ticks` long, in degrees from the note: murkī {0, 1, 0, -1, 0}, khaṭkā
    // {1, 0, -1, 0}, zamzama {1, 0, 1, 0, -1, 0}.
    void turn (Sequence&, const PitchSystem&, const std::vector<int>& degrees, int64_t ticks);
    void clearCurves (Sequence&);

    //==========================================================================
    // What the views show (owner, 2026-10-26: a scale wheel, an interval clock and a consonance display).
    // The notes sounding at a tick: started and not ended.
    std::vector<size_t> soundingAt (const Sequence&, int64_t tick);
    // How rough pitches sound together: Sethares' fit (1993) of Plomp and Levelt's curve, summed
    // over the partials of each pair of notes (harmonic, the k-th at 0.88^k). 0 for one note; a
    // unison or octave is low, a semitone or a tritone high, in any tuning.
    double roughness (const std::vector<double>& pitches, int partials = 6);
    // How long each degree of the system sounds over the whole sequence, in ticks (drums left out).
    std::vector<double> degreeUsage (const Sequence&, const PitchSystem&);

    //==========================================================================
    // Step input (Cubase): notes entered at a cursor that moves on by a step.
    struct StepInput
    {
        int64_t position = 0;
        int64_t step = 120;       // how far the cursor moves
        int64_t length = 120;     // the notes' length
        int velocity = 100;
        bool chord = false;       // the cursor stays, so the next note is played with this one
        bool insert = false;      // the notes from the cursor on move later to make room
    };
    void stepNote (Sequence&, StepInput&, double pitch, int channel = 0);
    void stepRest (Sequence&, StepInput&);
    void stepBack (Sequence&, StepInput&);   // the cursor a step back, the notes starting there deleted

    //==========================================================================
    // A controller lane: control change 0..127, or these.
    constexpr int laneBend = 128, lanePressure = 129;
    struct LanePoint { int64_t tick = 0; int value = 0; };   // 0..127, a bend 0..16383
    std::vector<LanePoint> lanePoints (const Sequence&, int lane, int channel);
    // The lane's events from `from` to `to` replaced by a line from one value to the other, a point
    // every `every` ticks.
    void drawLane (Sequence&, int lane, int channel, int64_t from, int64_t to, int valueFrom, int valueTo, int64_t every);
    void eraseLane (Sequence&, int lane, int channel, int64_t from, int64_t to);
}
