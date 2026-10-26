// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Fugue (owner, 2026-10-26): a fugue of two to four voices on a subject, the melody of a MIDI
// clip, in any of the mixer's modal systems: church and jazz modes, ragas, maqams, dastgahs
// and equal temperaments. Our own code: the plan follows the textbook fugue (and the ideas of
// the open-source generators surveyed, none of whose code is used), and the free voices are
// written by the species counterpoint search of mixer/Counterpoint.h. Plain C++, no JUCE or Qt.
//
// The plan: the exposition, each voice entering in turn with the subject or its answer at the
// fifth (tonal: the tonic and dominant of the head answer each other), the voice before going
// on in a countersubject; then episodes, the subject's head in sequence a step at a time,
// each leading to a middle entry on another degree of the mode (inverted, if asked); then a
// stretto, the entries overlapping where they sound best together; and a close on the tonic
// held in the bass. Every transposition goes by the mode's own degrees, so a raga's subject
// stays in the raga, and the āroha and avaroha are kept going up and coming down.
#include "Counterpoint.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fugue
{
    enum class Answer { tonal, real };   // real: every note a fifth up by the mode's degrees

    struct Settings
    {
        int voices = 3;                                          // 2..4
        counterpoint::Mode mode;                                 // invalid (empty): guessed from the subject
        Answer answer = Answer::tonal;
        bool countersubject = true;
        counterpoint::Species species = counterpoint::Species::fifth;   // the free voices' rhythm, against the beat
        int middleEntries = 2;                                   // 0..4, each after an episode
        bool inversion = false;                                  // every other middle entry upside down
        bool stretto = true;
        uint32_t seed = 0;
    };

    struct Section
    {
        std::string what;       // "subject", "answer", "countersubject", "episode", "middle entry", "stretto", "close"
        int voice = 0;          // top first
        int64_t start = 0, end = 0;
    };

    struct Result
    {
        std::vector<counterpoint::Line> voices;   // top first
        std::vector<std::string> names;           // "soprano", "alto", "tenor", "bass"; two voices "upper", "lower"
        std::vector<Section> plan;
        int64_t end = 0;                          // the last tick
        int64_t strettoDelay = 0;                 // ticks between the stretto's entries, 0 if there was none
        int bent = 0;                             // the strict rules broken where nothing else fitted
    };

    // The subject in a melody (counterpoint::cantus): its first phrase, at most four bars of
    // 4/4. Empty if it has fewer than two notes.
    counterpoint::Line subject (const counterpoint::Line& melody, int ticksPerQuarter);

    Result generate (const counterpoint::Line& subject, int ticksPerQuarter, const Settings&);
}
