// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"

#include <string>
#include <utility>
#include <vector>

// Tags in compressed and lossless files through TagLib: ID3v2 and ID3v1 (MP3,
// TTA), Vorbis comments and pictures (FLAC, Ogg Vorbis, Opus, Speex), MP4 atoms
// (M4A, ALAC), APEv2 (WavPack, Monkey's Audio, Musepack), and TagLib's generic
// properties for the rest (WMA...). XMP goes in an ID3 PRIV frame (MP3), a
// uuid box (MP4), or a sidecar .xmp beside the file. WAV and AIFF chunks are in
// core/RiffChunks; their ID3 chunk is made here.
namespace tagio
{
    // Which schemas get written (riff to ixml: WAV and AIFF only).
    struct Options
    {
        bool riff = true, bext = true, cart = true, ixml = true;
        bool id3 = true, id3v1 = true, vorbis = true, mp4 = true, ape = true, xmp = true;
        int id3Version = 4;         // 3 or 4
        bool chapters = true;       // MP3: ID3 chapters from the markers
        std::string software;       // TSSE / ENCODER / ©too when not set
    };

    // True for files whose tags are written here (not WAV or AIFF).
    bool handles (const std::string& path);

    // Adds every tag in the file to tags (see meta::absorb); markers get the file's chapters.
    void read (const std::string& path, Tags&, std::vector<Marker>* chapters = nullptr, double sampleRate = 0.0);
    // Reads an ID3v2 tag from its bytes (a WAV or AIFF ID3 chunk).
    void readId3 (const std::string& bytes, Tags&);
    // The ID3v2 tag for tags, as bytes.
    std::string renderId3 (const Tags&, const Options&);

    // Replaces the tags of an existing file (frames PHI does not know stay).
    bool write (const std::string& path, const Tags&, const std::vector<Marker>& markers, double sampleRate,
                const Options&, std::string& error);

    // Every tag item TagLib sees in the file, for the raw view: name and a short value.
    std::vector<std::pair<std::string, std::string>> rawItems (const std::string& path);

    // Where a file's sidecar XMP goes: name.xmp beside it.
    std::string sidecarPath (const std::string& path);
}
