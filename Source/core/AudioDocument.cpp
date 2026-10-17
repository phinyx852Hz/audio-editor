// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioDocument.h"

#include <algorithm>

void adjustMarkersForEdit (std::vector<Marker>& markers, Frame start, Frame end, Frame insertedLength)
{
    const auto delta = insertedLength - (end - start);

    auto move = [&] (Frame pos) -> Frame
    {
        if (pos >= end)
            return pos + delta;

        if (pos > start)
            return std::min (pos, start + insertedLength);

        return pos;
    };

    for (auto& m : markers)
    {
        m.start = move (m.start);
        m.end = m.isRegion() ? move (m.end) : m.start;
    }

    // A region whose whole span was deleted collapses; drop it.
    markers.erase (std::remove_if (markers.begin(), markers.end(),
                                   [] (const Marker& m) { return m.isRegion() && m.end <= m.start; }),
                   markers.end());
}

void AudioDocument::reset (AudioDataPtr data, std::vector<Marker> markers, const std::string& path)
{
    history.clear();
    history.push_back ({ "Open", { std::move (data), std::move (markers) } });
    position = savedPosition = 0;
    filePath = path;

    markerCounter = 0;
    for (const auto& m : current().markers)
        markerCounter = std::max (markerCounter, m.id);
}

void AudioDocument::apply (const std::string& description, DocumentState newState)
{
    history.resize ((size_t) position + 1);

    // A save point that was undone past and then overwritten can never be reached again.
    if (savedPosition > position)
        savedPosition = -1;

    history.push_back ({ description, std::move (newState) });
    ++position;
}

std::vector<std::string> AudioDocument::historyDescriptions() const
{
    std::vector<std::string> result;
    for (const auto& e : history)
        result.push_back (e.description);

    return result;
}

void AudioDocument::jumpTo (int index)
{
    position = std::clamp (index, 0, (int) history.size() - 1);
}

int AudioDocument::nextMarkerId()
{
    return ++markerCounter;
}
