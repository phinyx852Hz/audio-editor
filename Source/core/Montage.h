// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"
#include "AudioDocument.h"

#include <string>
#include <vector>

// Album assembly (WaveLab's Montage, kept small): songs in order on one
// timeline, each with the pause before it, its own gain and fades, and its
// CD-Text and ISRC. A negative pause makes the song start before the previous
// one ends: the overlap becomes an equal-power crossfade.
//
// The montage is never edited sample by sample. `mixdown` assembles the album
// as ordinary AudioData that plays, meters and renders like any file; untouched
// stretches share the source files' audio, so rebuilding it after each change
// is quick and costs little memory. Plain C++: no JUCE or Qt.
struct MontageClip
{
    std::string path;           // the source file
    AudioDataPtr source;        // its audio (at the montage's sample rate); not saved in the montage file
    AudioDataPtr original;      // what `source` was made from (before resampling), to notice edits made in an editor tab
    Frame sourceStart = 0;      // the part of the source that is used: [sourceStart, sourceEnd)
    Frame sourceEnd = 0;
    Frame pauseBefore = 0;      // silence before the song; negative overlaps the song before it (a crossfade)
    double gainDb = 0.0;
    Frame fadeIn = 0, fadeOut = 0;   // to and from silence; an overlap is crossfaded over its whole length
    Tags tags;                  // title, artist, isrc

    Frame length() const { return sourceEnd - sourceStart; }
};

class Montage
{
public:
    static constexpr double defaultPauseSeconds = 2.0;

    double sampleRate = 44100.0;
    int channels = 2;
    std::vector<MontageClip> clips;
    Tags tags;                  // the album's title, artist and catalogue number (tag::catalog, UPC/EAN)

    // Where each song starts on the album timeline, and the album's length.
    Frame clipStart (int index) const;
    Frame clipEnd (int index) const { return clipStart (index) + clips[(size_t) index].length(); }
    Frame length() const;
    int clipAt (Frame) const;   // the song playing at a position (the later one in a crossfade), or -1

    // Overlap of song `index` with the one before it (0 when there is a pause).
    Frame overlapBefore (int index) const;
    // Keeps each pause within what the songs allow: an overlap is never longer
    // than either song, and the first song has no overlap.
    void clampPauses();

    // Gain of song `index` at frame `t` of its own length: fades, crossfades and gain together.
    float envelope (int index, Frame t) const;

    // The assembled album. An empty montage gives empty data at the montage's rate.
    AudioDataPtr mixdown() const;

    // One region per song, named "1. Title", for the editor's marker list.
    std::vector<Marker> trackMarkers() const;

    // A CUE sheet for the mixdown saved as `audioFileName` (WAVE, or MP3/AIFF by extension).
    std::string cueSheet (const std::string& audioFileName) const;

    // A song's display name: its title, or its file name without the folder and extension.
    std::string clipName (int index) const;
};

// mm:ss:ff as CUE sheets and CDs count (75 frames per second).
std::string cueTime (Frame position, double sampleRate);
