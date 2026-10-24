// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioDocument.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The metadata chunks of WAV (RIFF and RF64) and AIFF files, read and written
// directly: RIFF INFO and DISP, Broadcast Wave bext (version 2, with loudness),
// AES46 cart, iXML and XMP (_PMX), the ID3 chunk's bytes, and every chunk
// PHI does not know, which is kept as "raw:wav:<id>". In AIFF: NAME, AUTH,
// (c), ANNO, the ID3 chunk and XMP (APPL "XMP "). Plain C++.
namespace riff
{
    struct Chunk
    {
        std::string id;          // four characters
        std::string listType;    // LIST chunks: INFO, adtl...
        uint64_t offset = 0;     // where its data starts
        uint64_t size = 0;
    };

    struct Layout
    {
        bool valid = false, aiff = false, rf64 = false;
        std::vector<Chunk> chunks;
    };

    Layout scan (const std::string& path);
    std::string readChunk (const std::string& path, const Chunk&, uint64_t maxBytes = 64u << 20);

    // Adds every schema a WAV or AIFF file holds to tags (see meta::absorb), and
    // returns the bytes of its ID3 chunk (empty when it has none) for the caller to read.
    std::string readMetadata (const std::string& path, Tags&);

    // Which schemas to write.
    struct Write
    {
        bool riff = true, bext = true, cart = true, ixml = true, xmp = true;
        std::string id3;         // the ID3 chunk's bytes, made by the caller; empty: none
        std::string software;    // ISFT, xmp:CreatorTool and the cart ProducerAppID when not set
    };

    // Replaces the metadata chunks of a WAV or AIFF file (written without
    // metadata by the audio writer) with those for tags, before the audio.
    bool writeMetadata (const std::string& path, const Tags&, const Write&, std::string& error);

    // The chunks themselves, for tests and the raw view: id and payload.
    std::vector<std::pair<std::string, std::string>> wavChunks (const Tags&, const Write&);
    std::string encodeBext (const Tags&);
    void decodeBext (const std::string& payload, Tags&);
    std::string encodeCart (const Tags&, const std::string& software);
    void decodeCart (const std::string& payload, Tags&);
    std::string encodeInfo (const Tags&, const std::string& software);
    void decodeInfo (const std::string& payload, Tags&);

    // Bytes that are not UTF-8 are read as Latin-1.
    std::string toUtf8 (const std::string&);
}
