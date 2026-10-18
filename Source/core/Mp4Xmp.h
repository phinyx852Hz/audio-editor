// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>

// XMP in MP4 files (M4A, MP4, MOV): Adobe's top-level "uuid" box
// BE7ACFCB-97A9-42E8-9C71-999491E3AFAC. Plain C++.
namespace mp4xmp
{
    // The packet, or empty.
    std::string read (const std::string& path);
    // Replaces the box (empty xml removes it). Written at the end of the file, so
    // the offsets of the audio stay valid.
    bool write (const std::string& path, const std::string& xml, std::string& error);
}
