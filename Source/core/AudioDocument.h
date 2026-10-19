// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"
#include "BeatGrid.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Marker
{
    enum class Type { generic, region };

    int id = 0;
    Type type = Type::generic;
    Frame start = 0;
    Frame end = 0;      // equals start for point markers
    std::string name;

    bool isRegion() const { return type == Type::region; }
};

// Moves markers to follow an edit that replaced [start, end) with insertedLength frames.
void adjustMarkersForEdit (std::vector<Marker>&, Frame start, Frame end, Frame insertedLength);

// Descriptive metadata, by schema-neutral key (see `tag`) plus schema keys
// where a schema differs or has its own fields (see Metadata.h).
using Tags = std::map<std::string, std::string>;

namespace tag
{
    inline constexpr const char* title = "title";
    inline constexpr const char* artist = "artist";
    inline constexpr const char* album = "album";
    inline constexpr const char* genre = "genre";
    inline constexpr const char* date = "date";
    inline constexpr const char* track = "track";
    inline constexpr const char* comment = "comment";
    inline constexpr const char* copyright = "copyright";
    inline constexpr const char* isrc = "isrc";
    inline constexpr const char* bpm = "bpm";           // tempo in beats per minute (WAV: acid chunk; FLAC: BPM)
    inline constexpr const char* key = "key";           // musical key, such as "A minor" (FLAC: INITIALKEY)
    inline constexpr const char* catalog = "catalog";   // UPC/EAN of an album (Album, CUE sheet)
    inline constexpr const char* albumArtist = "albumArtist";
    inline constexpr const char* composer = "composer";
    inline constexpr const char* lyricist = "lyricist";
    inline constexpr const char* conductor = "conductor";
    inline constexpr const char* publisher = "publisher";  // record label
    inline constexpr const char* disc = "disc";            // "1/2"
    inline constexpr const char* subtitle = "subtitle";
    inline constexpr const char* grouping = "grouping";
    inline constexpr const char* language = "language";
    inline constexpr const char* lyrics = "lyrics";
    inline constexpr const char* url = "url";
    inline constexpr const char* encodedBy = "encodedBy";
    inline constexpr const char* software = "software";
    inline constexpr const char* engineer = "engineer";
    inline constexpr const char* keywords = "keywords";
    inline constexpr const char* originalDate = "originalDate";
    // Broadcast Wave (bext), WAV only
    inline constexpr const char* bwfDescription = "bwf.description";
    inline constexpr const char* bwfOriginator = "bwf.originator";
    inline constexpr const char* bwfOriginatorRef = "bwf.originatorRef";
    inline constexpr const char* bwfDate = "bwf.originationDate";
    inline constexpr const char* bwfTime = "bwf.originationTime";
    inline constexpr const char* bwfTimeReference = "bwf.timeReference";
    inline constexpr const char* bwfCodingHistory = "bwf.codingHistory";
    inline constexpr const char* bwfUmid = "bwf.umid";                       // hex
    inline constexpr const char* bwfLoudness = "bwf.loudnessValue";          // LUFS
    inline constexpr const char* bwfLoudnessRange = "bwf.loudnessRange";     // LU
    inline constexpr const char* bwfTruePeak = "bwf.maxTruePeakLevel";       // dBTP
    inline constexpr const char* bwfMomentary = "bwf.maxMomentaryLoudness";  // LUFS
    inline constexpr const char* bwfShortTerm = "bwf.maxShortTermLoudness";  // LUFS
}

class Album;

struct DocumentState
{
    AudioDataPtr data;
    std::vector<Marker> markers;
    Tags tags;
    // Set when the document is an album: `data` is then its mixdown and `markers` its tracks.
    std::shared_ptr<const Album> album;
    // The channel layout when it is not the usual one for the channel count ("5.2",
    // "Ambisonics (1st order)"...; see dsp::layoutChoices). Empty: the usual one.
    std::string layout;
    // The tempo grid (Warp menu); set from the BPM tag when a file opens.
    BeatGrid grid;
};

// Holds the current audio and markers plus a linear undo history of snapshots.
// Snapshots share audio chunks, so history costs little memory.
class AudioDocument
{
public:
    void reset (AudioDataPtr, std::vector<Marker> markers, const std::string& path, Tags tags = {},
                std::shared_ptr<const Album> album = nullptr);
    bool isAlbum() const { return isOpen() && current().album != nullptr; }
    bool isOpen() const { return ! history.empty() && current().data != nullptr; }

    // With nothing open this is an empty state (no audio, markers or tags), never a bad read.
    const DocumentState& current() const
    {
        static const DocumentState none {};
        return history.empty() ? none : history[(size_t) position].state;
    }
    const std::string& path() const      { return filePath; }
    void setPath (const std::string& p)  { filePath = p; }
    // What the channels are, for the state as it is now, without an undo step (used on opening a file).
    void setLayout (std::string l)       { if (! history.empty()) history[(size_t) position].state.layout = std::move (l); }

    void apply (const std::string& description, DocumentState newState);

    bool canUndo() const { return position > 0; }
    bool canRedo() const { return position + 1 < (int) history.size(); }
    void undo()          { if (canUndo()) --position; }
    void redo()          { if (canRedo()) ++position; }
    std::string undoDescription() const { return canUndo() ? history[(size_t) position].description : std::string(); }
    std::string redoDescription() const { return canRedo() ? history[(size_t) position + 1].description : std::string(); }

    std::vector<std::string> historyDescriptions() const;
    int historyPosition() const { return position; }
    void jumpTo (int index);

    bool isModified() const { return position != savedPosition; }
    void markSaved()        { savedPosition = position; savedAudio = current().data; }
    void markUnsaved()      { savedPosition = -1; savedAudio.reset(); }  // e.g. a new untitled copy that has never been saved
    // The audio is what the file on disk holds (only tags or markers changed since).
    bool audioIsSaved() const { return savedAudio != nullptr && isOpen() && savedAudio == current().data; }

    int nextMarkerId();

    // Unique for each opened or created document in this run (0 when none);
    // it moves with the document between tabs. Autosave files are named by it.
    std::uint64_t id() const { return documentId; }

private:
    struct Entry
    {
        std::string description;
        DocumentState state;
    };

    std::vector<Entry> history;
    int position = 0, savedPosition = 0, markerCounter = 0;
    std::string filePath;
    std::uint64_t documentId = 0;
    AudioDataPtr savedAudio;
};
