// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

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

struct DocumentState
{
    AudioDataPtr data;
    std::vector<Marker> markers;
};

// Holds the current audio and markers plus a linear undo history of snapshots.
// Snapshots share audio chunks, so history costs little memory.
class AudioDocument
{
public:
    void reset (AudioDataPtr, std::vector<Marker> markers, const std::string& path);
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

    int nextMarkerId();

private:
    struct Entry
    {
        std::string description;
        DocumentState state;
    };

    std::vector<Entry> history;
    int position = 0, savedPosition = 0, markerCounter = 0;
    std::string filePath;
};
