// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <map>
#include <string>

// FLAC Vorbis comments (TITLE, ARTIST...), read and written directly in the
// file's metadata blocks, since the FLAC codec we use does not handle them.
// Plain C++.
namespace flactags
{
    using Fields = std::map<std::string, std::string>;   // upper-case field name -> value

    // The comments in a FLAC file; empty when there are none or it is not FLAC.
    Fields read (const std::string& path);

    // Replaces the file's comments with `fields` (none removes them), keeping
    // every other block and the audio. False with `error` set on failure.
    bool write (const std::string& path, const Fields& fields, const std::string& vendor, std::string& error);
}
