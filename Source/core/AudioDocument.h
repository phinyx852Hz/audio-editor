// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <cstdint>
#include <map>
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

// Descriptive metadata, by schema-neutral key (see `tag`). Each file format
// stores the keys it can: WAV as LIST/INFO and BWF bext, FLAC and Ogg as Vorbis comments.
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
    // Broadcast Wave (bext), WAV only
    inline constexpr const char* bwfDescription = "bwf.description";
    inline constexpr const char* bwfOriginator = "bwf.originator";
    inline constexpr const char* bwfOriginatorRef = "bwf.originatorRef";
    inline constexpr const char* bwfDate = "bwf.originationDate";
    inline constexpr const char* bwfTime = "bwf.originationTime";
    inline constexpr const char* bwfTimeReference = "bwf.timeReference";
    inline constexpr const char* bwfCodingHistory = "bwf.codingHistory";
}

struct DocumentState
{
    AudioDataPtr data;
    std::vector<Marker> markers;
    Tags tags;
};

// Holds the current audio and markers plus a linear undo history of snapshots.
// Snapshots share audio chunks, so history costs little memory.
class AudioDocument
{
public:
    void reset (AudioDataPtr, std::vector<Marker> markers, const std::string& path, Tags tags = {});
    bool isOpen() const { return ! history.empty() && current().data != nullptr; }

    const DocumentState& current() const { return history[(size_t) position].state; }
    const std::string& path() const      { return filePath; }
    void setPath (const std::string& p)  { filePath = p; }

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
    void markSaved()        { savedPosition = position; }
    void markUnsaved()      { savedPosition = -1; }  // e.g. a new untitled copy that has never been saved

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
};
