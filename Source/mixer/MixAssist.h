// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Mix Assistant's core: like the Master Assistant (core/MasterAssist.h) it only
// turns knobs of the mix you made, here each track's fader and pan. It works on
// the tracks bounced on their own (before fader and pan), so every try is a sum
// in memory, not a render. Faders are set so the tracks sit at levels that suit
// what they are (the lead on top, kick and bass close behind, pads and effects
// under them: the cross-adaptive "equal loudness" mix of Perez-Gonzalez and Reiss,
// with an offset per role), with headroom left for mastering and, given a
// reference mix, its tonal balance. Pans follow rules (the low end and the lead
// in the middle, the rest spread by brightness). Overlaps where one track hides
// another are reported, for EQ by hand. Plain C++.
#include "../core/MasterAssist.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace mixassist
{
    enum class Role { leadVocal, vocal, kick, snare, drums, bass, leadInstrument, guitar, keys, pad, fx, other };
    constexpr int numRoles = 12;

    const char* roleName (Role);   // "Lead vocal"...
    Role guessRole (const std::string& trackName);   // from words and short names: "Lead Vox", "BV", "KICK", "BA"
    double roleOffsetDb (Role);    // level against the average of the tracks, by rule of thumb
    bool staysCentred (Role);      // the lead vocal, kick, snare and bass
    int rolePriority (Role);       // who wins where two overlap: higher is more important

    struct Track
    {
        AudioDataPtr stem;            // bounced: before fader and pan, at the mix's rate
        std::string name;
        Role role = Role::other;
        double faderDb = 0.0, pan = 0.0;
        bool monoAudio = false;       // its clips are mono audio files, which the mixer pans with equal power
        bool moveFader = true, movePan = true;
    };

    struct Settings
    {
        double mixLufs = -18.0;       // the whole mix, leaving room for mastering
        double ceilingDb = -6.0;      // its true peak at most
        bool balance = true;          // faders by role
        bool panning = true;          // pans by rule, for tracks you allow
        bool keepPans = true;         // a track already panned keeps its pan
        const assist::Profile* reference = nullptr;   // a reference mix's tonal balance to lean towards
        int maxEvaluations = 80;
    };

    struct Overlap
    {
        int hidden = 0, hiding = 0;   // tracks: `hiding` covers `hidden` in that octave
        int band = 0;                 // assist::Profile::bandCentres() index
        double cutDb = 0.0;           // a suggested cut on `hiding` there
    };

    struct Suggestion
    {
        std::vector<double> faderDb, pan;            // per track
        std::vector<double> levelBefore, levelAfter, levelAim;   // each track's loudness in the mix against their average, dB
        assist::Profile before, after;               // the whole mix
        std::vector<Overlap> overlaps;               // strongest first
        int evaluations = 0;
        bool stopped = false;
    };

    // The mix of these tracks with these faders and pans, as long as the longest
    // stem. Mono audio pans with equal power, the rest by balance, as the mixer does.
    AudioDataPtr mix (const std::vector<Track>&, const std::vector<double>& faderDb, const std::vector<double>& pan);
    void panGains (const Track&, double pan, double& left, double& right);

    // Pans by rule for the tracks that may move.
    std::vector<double> rulePans (const std::vector<Track>&, const std::vector<assist::Profile>& stems, bool keepPans);

    Suggestion suggest (const std::vector<Track>&, const Settings&, const std::atomic<bool>* cancel = nullptr,
                        const std::function<void (double)>& progress = {});
}
