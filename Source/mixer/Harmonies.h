// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Harmony as "a method or way" (owner, 2026-10-29): the ways voices sound together, from the
// Western art music tradition and beyond it, for the Mode menu's "harmony". Four parts: Western
// art music by era; jazz; the harmony and harmony-like practices the ethnomusicological
// literature describes, by region as melody and tuning are (drones, parallel and diaphonic
// singing, heterophony, interlocking and hocket, overtone singing, stratified polyphony); and
// the traditions that grew from meeting Western harmony into systems of their own (shape-note
// singing, barbershop, gospel, makwaya and isicathamiya, highlife, Pacific hymn singing, bossa
// nova). Each has a short account, where it comes from, and a few bars to hear it by.
// docs/harmony.md cites the sources. Every name is lower case (owner); peoples, places and
// persons are capitalized. Plain C++, no JUCE or Qt: the Research menu reads the same list.
#include "MidiFile.h"

#include <string>
#include <vector>

namespace harmony
{
    // One note of an example: semitones above the final (the Mode menu's), or a rest.
    struct Note
    {
        int pitch = 0;
        double beats = 1.0;
        bool rest = false;
        int velocity = 80;
    };

    struct Voice
    {
        std::string name;          // "vox principalis", "drone", "kushaura"
        std::vector<Note> notes;
    };

    // The four parts of the menu.
    enum class Part { western, jazz, world, meeting };

    struct Harmony
    {
        std::string name;          // as shown, lower case: "parallel organum", "cantu a tenore"
        std::string plain;         // ASCII, for searching and saving: "parallel organum"
        Part part = Part::world;
        // Where it sits in the menu: an era for Western art music ("Middle Ages"), a region for
        // the world and the meeting traditions (one of mixer::regions(): "europe", "africa"),
        // empty for jazz.
        std::string group;
        std::string tradition;     // the people, place, school or person, capitalized: "Georgia", "Notre-Dame school"
        std::string dates;         // "c. 900", "1160s–1250", "1959", or empty
        int year = 0;              // for the order within its group
        std::string texture;       // "drone", "parallel", "counterpoint", "functional", "heterophony", "interlocking",
                                   // "overtone", "stratified", "chordal", "cluster", "polyphony"
        std::string about;         // what it is, a paragraph
        std::string sonorities;    // the intervals and chords it rests on
        std::string source;        // the main sources, short (docs/harmony.md has them in full)
        std::string tuning;        // a mixer::tuningSystems() plain name it is heard in, or empty (12-TET)
        double bpm = 72.0;
        std::vector<Voice> voices; // the example, every voice the same length
    };

    const std::vector<Harmony>& harmonies();
    int findHarmony (const std::string& plain);    // its index, or -1
    std::string partTitle (Part);                  // "Western art music", "jazz", ...
    // The groups of a part in menu order: the eras in order of time, the regions as melody and
    // tuning have them; those with nothing in them left out. Jazz has one, "".
    std::vector<std::string> groups (Part);
    std::string groupTitle (Part, const std::string& group);   // "Middle Ages", "West Asia"
    // The indices of the harmonies of a part and group, by tradition in order of date, then as listed.
    std::vector<int> inGroup (Part, const std::string& group);

    double beats (const Voice&);
    bool valid (const Harmony&);   // has voices, all the same length, every pitch playable on `root` 36..71

    // The example `cycles` times at `bpm` on the final `root` (a MIDI note), a channel a voice,
    // as a type 0 MIDI file, 480 ticks a beat.
    midifile::SongPtr toSong (const Harmony&, int root, int cycles, double bpm);
}
