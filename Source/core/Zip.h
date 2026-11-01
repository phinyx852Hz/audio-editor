// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Zip archives, as far as a DAWproject needs them: reading entries that are stored or
// deflated, and writing a new archive. No Zip64 (nothing over 4 GB), no encryption,
// no spanning. Plain C++ on zlib.
namespace zip
{
    struct Entry
    {
        std::string name;              // as in the archive: '/' between folders
        uint16_t method = 0;           // 0 stored, 8 deflated
        uint32_t crc = 0;
        uint64_t compressedSize = 0, size = 0;
        uint64_t headerOffset = 0;     // its local header
    };

    class Reader
    {
    public:
        bool open (const std::string& path, std::string& error);
        const std::vector<Entry>& entries() const { return list; }
        const Entry* find (const std::string& name) const;
        bool read (const Entry&, std::vector<uint8_t>& out, std::string& error);
        bool read (const std::string& name, std::vector<uint8_t>& out, std::string& error);
        bool extract (const Entry&, const std::string& toPath, std::string& error);

    private:
        std::ifstream in;
        std::vector<Entry> list;
    };

    class Writer
    {
    public:
        ~Writer();
        bool open (const std::string& path, std::string& error);
        // `deflate`: compressed (text); otherwise stored (audio, which hardly compresses).
        bool add (const std::string& name, const void* data, size_t size, bool deflate, std::string& error);
        bool addFile (const std::string& name, const std::string& fromPath, bool deflate, std::string& error);
        bool close (std::string& error);   // writes the central directory

    private:
        std::ofstream out;
        std::string path;
        std::vector<Entry> list;
        uint64_t offset = 0;
        bool closed = true;
    };
}
