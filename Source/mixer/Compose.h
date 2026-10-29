// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Compose (owner, 2026-10-29: "a Compose menu ... all the compositional methods available in Phi
// ... non-Western compositional approaches as well related to any type of music as identified by
// musicologists and ethnomusicologists ... the idea of composing is definitely related to
// organization and structure ... limited to 8 parts for the 8 tracks"). Each method writes a
// score from nothing, at most eight parts, organized as the tradition organizes music: its form,
// its cycles, its layers and the way its parts relate. The pitches come from a mode (the Mode
// menu's, or the tradition's own tuning), so a rāga stays in its rāga and a gamelan piece in its
// laras; the score opens in the microtonal MIDI editor. Our own code (no port of another program);
// the sources each method follows are in methods() and docs/compose.md. Plain C++, no JUCE or Qt.
//
// The methods PHI already had (Architectonics, counterpoint and the fugue on a clip, the stochastic
// line, cubify, the twelve-tone row, minimalize and the rhythm generator) stay where they are and
// are gathered in the Compose menu; counterpoint, the fugue and minimalism are here too, on a cantus,
// a subject or a cell written for them, so they can start from a blank page.
#include "Counterpoint.h"
#include "MidiFile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace composition
{
    constexpr int maxParts = 8;            // the mixer's eight tracks
    constexpr int ticksPerQuarter = 480;

    enum class Method
    {
        // Europe and the Americas, oldest first.
        organum,          // Notre Dame, c. 1170–1230: a held chant tenor, voices above in the rhythmic modes
        isorhythm,        // Ars nova, 14th c.: talea and color in the tenor, the upper voices panisorhythmic
        counterpoint,     // Fux, 1725: species counterpoint on a cantus firmus written for it
        fugue,            // the textbook fugue on a subject written for it
        stochastic,       // Xenakis, Achorripsis (1957): a matrix of parts and sections, densities by Poisson's law
        aleatory,         // Cage's chance operations (1951) and Lutosławski's aleatory counterpoint (1961)
        process,          // Reich and Glass, 1960s–70s: phasing and additive process on a cell written for it
        tintinnabuli,     // Pärt, 1976: a melodic voice and a triad voice, and the mensuration canon
        spectral,         // Grisey and Murail, 1970s: the harmonic series as harmony, harmonic to inharmonic
        // South and West Asia.
        raga,             // Hindustani: ālāp, joṛ, jhālā and a gat in tīntāl, over the tānpūrā
        maqam,            // Arab: taqsīm through the ajnās, then a waṣla (samāʿī, muwashshaḥ, qafla)
        radif,            // Persian: a dastgāh's gushe-hā from darāmad to owj and forud, then a reng
        // East and Southeast Asia.
        gamelan,          // Javanese: balungan and its elaboration in a colotomic gong cycle, two irama
        joHaKyu,          // Japanese: jo-ha-kyū (Zeami), in the gagaku ensemble's layers
        sanjo,            // Korean: sanjo's jangdan from jinyangjo to hwimori, slow to fast
        chineseSuite,     // Chinese: sǎn-màn-zhōng-kuài-sǎn, phrases in qǐ-chéng-zhuǎn-hé
        // Africa.
        ostinato,         // Ewe (Ghana, Togo): layered ostinati on the bell's timeline, the lead drum's calls
        hocket            // Central Africa (Banda-Linda horns, Aka): one note a player, interlocked
    };
    constexpr int numMethods = 18;

    struct MethodInfo
    {
        Method method;
        const char* id;           // "isorhythm": for settings and command ids
        const char* name;         // as the menu shows it: "Isorhythm"
        const char* tradition;    // "Ars nova (France, 14th c.)"
        const char* region;       // "europe", "south and west asia", "east and southeast asia", "africa"
        const char* about;        // one or two sentences: how the method organizes the music
        const char* sources;      // whom it follows
        const char* home;         // the tuning it is in by default (Tunings.h plain name), or "" for the Mode menu's
        int defaultParts;
        int mostParts;            // never more than maxParts
        const char* parts;        // the parts, in the order they are added, "|" between
    };
    const std::vector<MethodInfo>& methods();
    const MethodInfo& info (Method);
    int find (const std::string& id);   // its index in methods(), or -1

    struct Settings
    {
        Method method = Method::isorhythm;
        int parts = 4;                    // 1..mostParts (the method may need a few: see Result::parts)
        int bars = 32;                    // about how long, in bars of 4/4 (forms round it to their own units)
        double bpm = 90.0;
        // The pitches: the keys of a period the mode uses and what they play (Counterpoint.h). An
        // invalid mode: the major scale on C. A method that needs a pentatonic or a triad takes it
        // from the mode's own degrees.
        counterpoint::Mode mode;
        uint32_t seed = 1;
    };

    struct Note
    {
        int64_t start = 0, end = 0;       // ticks
        int key = 60;                     // as the mode's tuning plays it (or 12-TET plus `cents`)
        int velocity = 90;
        double cents = 0.0;               // a pitch off the key, sent as pitch bend (spectral music only)
    };

    struct Part
    {
        std::string name;                 // "tenor", "gankogui (bell)"
        std::vector<Note> notes;          // by start
        bool drums = false;               // General MIDI drums, channel 10
    };

    struct Section
    {
        int64_t start = 0;                // ticks
        std::string name;                 // "ālāp", "talea 2 (diminution)"
    };

    struct Result
    {
        std::vector<Part> parts;          // at most maxParts
        std::vector<Section> sections;    // in order, the first at 0
        int64_t end = 0;
        double bpm = 90.0;
        bool microtonal = false;          // pitches off the keys (in cents): play in 12-TET with the bends
        std::string summary;              // a line on what was written
    };

    Result generate (const Settings&);

    // The score as one type 0 MIDI file: a channel a part (past the drums' tenth; drum parts on
    // it), a marker at each section, the bends before their notes.
    midifile::SongPtr toSong (const Result&, const std::string& title);

    // A melody the counterpoint and fugue methods work on, written for them: a cantus firmus by
    // Fux's rules (steps, one high point, a leap turned back, the last two notes by step to the
    // final), `notes` long, each a whole note.
    counterpoint::Line cantusFirmus (const counterpoint::Mode&, int notes, uint32_t seed);
}
