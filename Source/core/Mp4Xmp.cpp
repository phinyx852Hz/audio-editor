// SPDX-License-Identifier: AGPL-3.0-only
#include "Mp4Xmp.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace mp4xmp
{
namespace
{
    const unsigned char xmpUuid[16] { 0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8, 0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC };

    struct Box
    {
        uint64_t start = 0, size = 0, header = 8;
        bool xmp = false;
    };

    uint64_t be (const unsigned char* p, int n)
    {
        uint64_t v = 0;
        for (int i = 0; i < n; ++i)
            v = (v << 8) | p[i];
        return v;
    }

    std::vector<Box> boxes (std::ifstream& in, uint64_t fileSize)
    {
        std::vector<Box> out;
        for (uint64_t pos = 0; pos + 8 <= fileSize;)
        {
            unsigned char h[32] {};
            in.clear();
            in.seekg ((std::streamoff) pos);
            in.read ((char*) h, 32);
            Box b;
            b.start = pos;
            b.size = be (h, 4);
            if (b.size == 1)
                b.size = be (h + 8, 8), b.header = 16;
            else if (b.size == 0)
                b.size = fileSize - pos;
            if (b.size < b.header || pos + b.size > fileSize)
                break;
            b.xmp = std::string ((const char*) h + 4, 4) == "uuid" && b.size >= b.header + 16
                    && std::equal (xmpUuid, xmpUuid + 16, h + b.header);
            out.push_back (b);
            pos += b.size;
        }
        return out;
    }
}

std::string read (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return {};
    in.seekg (0, std::ios::end);
    const auto size = (uint64_t) in.tellg();
    for (const auto& b : boxes (in, size))
        if (b.xmp && b.size < (64u << 20))
        {
            std::string xml ((size_t) (b.size - b.header - 16), '\0');
            in.clear();
            in.seekg ((std::streamoff) (b.start + b.header + 16));
            in.read (xml.data(), (std::streamsize) xml.size());
            return xml;
        }
    return {};
}

bool write (const std::string& path, const std::string& xml, std::string& error)
{
    std::vector<Box> list;
    uint64_t size = 0;
    {
        std::ifstream in (path, std::ios::binary);
        if (! in)
            return error = "Cannot open " + path, false;
        in.seekg (0, std::ios::end);
        size = (uint64_t) in.tellg();
        list = boxes (in, size);
    }
    if (list.empty())
        return error = "Not an MP4 file: " + path, false;

    // An XMP box at the very end is cut off; one elsewhere becomes a free box.
    uint64_t end = size;
    {
        std::fstream f (path, std::ios::binary | std::ios::in | std::ios::out);
        for (const auto& b : list)
            if (b.xmp)
            {
                if (b.start + b.size == size && &b == &list.back())
                    end = b.start;
                else
                {
                    f.seekp ((std::streamoff) (b.start + 4));   // the box type, after its 32-bit size
                    f.write ("free", 4);
                }
            }
    }
    std::error_code ec;
    if (end < size)
        std::filesystem::resize_file (path, end, ec);
    if (ec)
        return error = ec.message(), false;
    if (xml.empty())
        return true;

    std::ofstream out (path, std::ios::binary | std::ios::app);
    const auto boxSize = (uint64_t) xml.size() + 24;
    unsigned char h[8] { (unsigned char) (boxSize >> 24), (unsigned char) (boxSize >> 16), (unsigned char) (boxSize >> 8),
                         (unsigned char) boxSize, 'u', 'u', 'i', 'd' };
    out.write ((const char*) h, 8);
    out.write ((const char*) xmpUuid, 16);
    out.write (xml.data(), (std::streamsize) xml.size());
    if (! out)
        return error = "Writing failed (disk full?)", false;
    return true;
}
}
