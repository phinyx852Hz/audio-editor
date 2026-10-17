// SPDX-License-Identifier: AGPL-3.0-only
#include "FlacTags.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace flactags
{
namespace
{
    constexpr int streamInfo = 0, padding = 1, vorbisComment = 4;

    struct Block
    {
        int type = 0;
        std::vector<unsigned char> data;
    };

    uint32_t le32 (const unsigned char* p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }

    void putLe32 (std::vector<unsigned char>& out, uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back ((unsigned char) (v >> (8 * i)));
    }

    // Reads "fLaC" and the metadata blocks; `audioStart` is where the frames begin.
    bool readBlocks (std::istream& in, std::vector<Block>& blocks, std::streamoff& audioStart)
    {
        char magic[4] {};
        if (! in.read (magic, 4) || std::string (magic, 4) != "fLaC")
            return false;

        for (bool last = false; ! last;)
        {
            unsigned char header[4];
            if (! in.read ((char*) header, 4))
                return false;
            last = (header[0] & 0x80) != 0;
            Block b;
            b.type = header[0] & 0x7f;
            const auto length = (size_t) header[1] << 16 | (size_t) header[2] << 8 | header[3];
            b.data.resize (length);
            if (length > 0 && ! in.read ((char*) b.data.data(), (std::streamsize) length))
                return false;
            blocks.push_back (std::move (b));
        }
        audioStart = in.tellg();
        return ! blocks.empty() && blocks.front().type == streamInfo;
    }

    std::string upper (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::toupper (c); });
        return s;
    }
}

Fields read (const std::string& path)
{
    Fields fields;
    std::ifstream in (path, std::ios::binary);
    std::vector<Block> blocks;
    std::streamoff audioStart = 0;
    if (! readBlocks (in, blocks, audioStart))
        return fields;

    for (const auto& b : blocks)
    {
        if (b.type != vorbisComment || b.data.size() < 8)
            continue;
        const auto* p = b.data.data();
        const auto* end = p + b.data.size();
        const auto vendorLength = le32 (p);
        p += 4;
        if ((size_t) (end - p) < (size_t) vendorLength + 4)
            break;
        p += vendorLength;
        const auto count = le32 (p);
        p += 4;
        for (uint32_t i = 0; i < count && end - p >= 4; ++i)
        {
            const auto length = le32 (p);
            p += 4;
            if ((size_t) (end - p) < length)
                break;
            const std::string entry ((const char*) p, length);
            p += length;
            if (const auto eq = entry.find ('='); eq != std::string::npos)
            {
                auto& value = fields[upper (entry.substr (0, eq))];
                value += (value.empty() ? "" : "; ") + entry.substr (eq + 1);   // repeated fields are joined
            }
        }
    }
    return fields;
}

bool write (const std::string& path, const Fields& fields, const std::string& vendor, std::string& error)
{
    std::vector<Block> blocks;
    std::streamoff audioStart = 0;
    {
        std::ifstream in (path, std::ios::binary);
        if (! readBlocks (in, blocks, audioStart))
        {
            error = "Not a FLAC file: " + path;
            return false;
        }
    }

    // Drop the old comments and any padding; add the new comments after the stream info.
    blocks.erase (std::remove_if (blocks.begin(), blocks.end(), [] (const Block& b)
    {
        return b.type == vorbisComment || b.type == padding;
    }), blocks.end());

    Block comments;
    comments.type = vorbisComment;
    putLe32 (comments.data, (uint32_t) vendor.size());
    comments.data.insert (comments.data.end(), vendor.begin(), vendor.end());
    uint32_t count = 0;
    std::vector<unsigned char> entries;
    for (const auto& [name, value] : fields)
    {
        if (value.empty())
            continue;
        const auto entry = upper (name) + "=" + value;
        putLe32 (entries, (uint32_t) entry.size());
        entries.insert (entries.end(), entry.begin(), entry.end());
        ++count;
    }
    putLe32 (comments.data, count);
    comments.data.insert (comments.data.end(), entries.begin(), entries.end());
    if (comments.data.size() >= (1u << 24))
    {
        error = "The tags are too long for a FLAC file.";
        return false;
    }
    blocks.insert (blocks.begin() + 1, std::move (comments));

    // Write the new file next to the old one, then replace it.
    const auto temp = path + ".tags-tmp";
    {
        std::ifstream in (path, std::ios::binary);
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        if (! out)
        {
            error = "Cannot write " + temp;
            return false;
        }
        out.write ("fLaC", 4);
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const auto& b = blocks[i];
            const auto length = b.data.size();
            const unsigned char header[4] { (unsigned char) ((i + 1 == blocks.size() ? 0x80 : 0) | b.type),
                                            (unsigned char) (length >> 16), (unsigned char) (length >> 8), (unsigned char) length };
            out.write ((const char*) header, 4);
            out.write ((const char*) b.data.data(), (std::streamsize) length);
        }
        in.seekg (audioStart);
        out << in.rdbuf();
        if (! out)
        {
            error = "Writing failed (disk full?)";
            std::remove (temp.c_str());
            return false;
        }
    }
    if (std::rename (temp.c_str(), path.c_str()) != 0)
    {
        error = "Could not replace " + path;
        std::remove (temp.c_str());
        return false;
    }
    return true;
}
}
