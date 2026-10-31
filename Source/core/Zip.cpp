// SPDX-License-Identifier: AGPL-3.0-only
#include "Zip.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <limits>

namespace zip
{
namespace
{
    uint16_t le16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
    uint32_t le32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }

    void put16 (std::vector<uint8_t>& v, uint32_t x) { v.push_back ((uint8_t) x); v.push_back ((uint8_t) (x >> 8)); }
    void put32 (std::vector<uint8_t>& v, uint32_t x) { put16 (v, x & 0xFFFF); put16 (v, x >> 16); }

    constexpr uint32_t localSignature = 0x04034b50, centralSignature = 0x02014b50, endSignature = 0x06054b50;
    constexpr uint64_t limit = 0xFFFFFFFFull;

    // Raw deflate (no zlib header), as zip keeps it.
    bool inflateRaw (const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t expected)
    {
        out.assign (expected, 0);
        z_stream z {};
        if (inflateInit2 (&z, -MAX_WBITS) != Z_OK)
            return false;
        z.next_in = const_cast<Bytef*> (data);
        z.avail_in = (uInt) size;
        z.next_out = out.data();
        z.avail_out = (uInt) out.size();
        const auto r = inflate (&z, Z_FINISH);
        const auto produced = z.total_out;
        inflateEnd (&z);
        return r == Z_STREAM_END && produced == expected;
    }

    bool deflateRaw (const uint8_t* data, size_t size, std::vector<uint8_t>& out)
    {
        z_stream z {};
        if (deflateInit2 (&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return false;
        out.resize (deflateBound (&z, (uLong) size));
        z.next_in = const_cast<Bytef*> (data);
        z.avail_in = (uInt) size;
        z.next_out = out.data();
        z.avail_out = (uInt) out.size();
        const auto r = deflate (&z, Z_FINISH);
        out.resize (z.total_out);
        deflateEnd (&z);
        return r == Z_STREAM_END;
    }

    void dosTime (uint16_t& time, uint16_t& date)
    {
        const auto now = std::time (nullptr);
        std::tm t {};
#if defined(_WIN32)
        localtime_s (&t, &now);
#else
        localtime_r (&now, &t);
#endif
        time = (uint16_t) ((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
        date = (uint16_t) (((std::max (t.tm_year, 80) - 80) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
    }
}

//==============================================================================
bool Reader::open (const std::string& path, std::string& error)
{
    list.clear();
    in.close();
    in.open (path, std::ios::binary);
    if (! in)
    {
        error = path + " could not be opened";
        return false;
    }
    in.seekg (0, std::ios::end);
    const auto fileSize = (uint64_t) in.tellg();
    // The end record is in the last 22 bytes, or before a comment of up to 64 KB.
    const auto tail = (size_t) std::min<uint64_t> (fileSize, 22 + 65535);
    std::vector<uint8_t> end (tail);
    in.seekg ((std::streamoff) (fileSize - tail));
    in.read ((char*) end.data(), (std::streamsize) tail);
    if (tail < 22)
    {
        error = "not a zip archive";
        return false;
    }
    size_t at = std::string::npos;
    for (size_t i = tail - 22 + 1; i-- > 0;)
        if (le32 (&end[i]) == endSignature)
        {
            at = i;
            break;
        }
    if (at == std::string::npos)
    {
        error = "not a zip archive";
        return false;
    }
    const auto count = le16 (&end[at + 10]);
    const auto dirSize = le32 (&end[at + 12]), dirOffset = le32 (&end[at + 16]);
    if (dirOffset == 0xFFFFFFFFu || count == 0xFFFF)
    {
        error = "Zip64 archives are not supported";
        return false;
    }
    if ((uint64_t) dirOffset + dirSize > fileSize)
    {
        error = "the zip archive is damaged";
        return false;
    }
    std::vector<uint8_t> dir (dirSize);
    in.seekg ((std::streamoff) dirOffset);
    in.read ((char*) dir.data(), (std::streamsize) dirSize);
    size_t p = 0;
    for (int i = 0; i < count; ++i)
    {
        if (p + 46 > dir.size() || le32 (&dir[p]) != centralSignature)
        {
            error = "the zip archive's directory is damaged";
            return false;
        }
        Entry e;
        e.method = le16 (&dir[p + 10]);
        e.crc = le32 (&dir[p + 16]);
        e.compressedSize = le32 (&dir[p + 20]);
        e.size = le32 (&dir[p + 24]);
        const auto nameLength = le16 (&dir[p + 28]), extra = le16 (&dir[p + 30]), comment = le16 (&dir[p + 32]);
        e.headerOffset = le32 (&dir[p + 42]);
        if (p + 46 + nameLength > dir.size())
        {
            error = "the zip archive's directory is damaged";
            return false;
        }
        e.name.assign ((const char*) &dir[p + 46], nameLength);
        std::replace (e.name.begin(), e.name.end(), '\\', '/');
        p += 46 + (size_t) nameLength + extra + comment;
        if (! e.name.empty() && e.name.back() != '/')   // folders are not entries of their own here
            list.push_back (std::move (e));
    }
    return true;
}

const Entry* Reader::find (const std::string& name) const
{
    auto wanted = name;
    while (wanted.rfind ("./", 0) == 0)
        wanted.erase (0, 2);
    for (const auto& e : list)
        if (e.name == wanted)
            return &e;
    return nullptr;
}

bool Reader::read (const Entry& e, std::vector<uint8_t>& out, std::string& error)
{
    uint8_t header[30];
    in.clear();
    in.seekg ((std::streamoff) e.headerOffset);
    in.read ((char*) header, 30);
    if (! in || le32 (header) != localSignature)
    {
        error = e.name + ": the zip archive is damaged";
        return false;
    }
    const auto skip = (std::streamoff) le16 (&header[26]) + le16 (&header[28]);
    in.seekg (skip, std::ios::cur);
    std::vector<uint8_t> raw ((size_t) e.compressedSize);
    in.read ((char*) raw.data(), (std::streamsize) raw.size());
    if (! in)
    {
        error = e.name + ": the zip archive is cut short";
        return false;
    }
    if (e.method == 0)
        out = std::move (raw);
    else if (e.method == 8)
    {
        if (! inflateRaw (raw.data(), raw.size(), out, (size_t) e.size))
        {
            error = e.name + ": could not be decompressed";
            return false;
        }
    }
    else
    {
        error = e.name + ": compressed in a way that is not supported (method " + std::to_string (e.method) + ")";
        return false;
    }
    if ((uint32_t) crc32 (0, out.data(), (uInt) out.size()) != e.crc)
    {
        error = e.name + ": damaged (its checksum is wrong)";
        return false;
    }
    return true;
}

bool Reader::read (const std::string& name, std::vector<uint8_t>& out, std::string& error)
{
    if (const auto* e = find (name))
        return read (*e, out, error);
    error = name + " is not in the archive";
    return false;
}

bool Reader::extract (const Entry& e, const std::string& toPath, std::string& error)
{
    std::vector<uint8_t> data;
    if (! read (e, data, error))
        return false;
    std::ofstream f (toPath, std::ios::binary | std::ios::trunc);
    f.write ((const char*) data.data(), (std::streamsize) data.size());
    if (! f)
    {
        error = toPath + " could not be written";
        return false;
    }
    return true;
}

//==============================================================================
Writer::~Writer()
{
    if (! closed)
    {
        out.close();
        std::remove (path.c_str());
    }
}

bool Writer::open (const std::string& p, std::string& error)
{
    path = p;
    out.open (p, std::ios::binary | std::ios::trunc);
    if (! out)
    {
        error = p + " could not be written";
        return false;
    }
    list.clear();
    offset = 0;
    closed = false;
    return true;
}

bool Writer::add (const std::string& name, const void* data, size_t size, bool deflate, std::string& error)
{
    const auto* bytes = (const uint8_t*) data;
    Entry e;
    e.name = name;
    e.size = size;
    e.crc = (uint32_t) crc32 (0, bytes, (uInt) size);
    std::vector<uint8_t> packed;
    if (deflate && size > 0 && deflateRaw (bytes, size, packed) && packed.size() < size)
    {
        e.method = 8;
        e.compressedSize = packed.size();
        bytes = packed.data();
    }
    else
        e.compressedSize = size;
    e.headerOffset = offset;
    if (offset + 30 + name.size() + e.compressedSize > limit)
    {
        error = "the project is over 4 GB, which this zip writer cannot do";
        return false;
    }
    uint16_t time = 0, date = 0;
    dosTime (time, date);
    std::vector<uint8_t> h;
    put32 (h, localSignature);
    put16 (h, 20);              // version needed
    put16 (h, 1 << 11);         // UTF-8 names
    put16 (h, e.method);
    put16 (h, time);
    put16 (h, date);
    put32 (h, e.crc);
    put32 (h, (uint32_t) e.compressedSize);
    put32 (h, (uint32_t) e.size);
    put16 (h, (uint32_t) name.size());
    put16 (h, 0);
    h.insert (h.end(), name.begin(), name.end());
    out.write ((const char*) h.data(), (std::streamsize) h.size());
    out.write ((const char*) bytes, (std::streamsize) e.compressedSize);
    if (! out)
    {
        error = path + " could not be written";
        return false;
    }
    offset += h.size() + e.compressedSize;
    list.push_back (std::move (e));
    return true;
}

bool Writer::addFile (const std::string& name, const std::string& fromPath, bool deflate, std::string& error)
{
    std::ifstream f (fromPath, std::ios::binary);
    if (! f)
    {
        error = fromPath + " could not be read";
        return false;
    }
    std::vector<uint8_t> data ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    return add (name, data.data(), data.size(), deflate, error);
}

bool Writer::close (std::string& error)
{
    if (closed)
        return true;
    uint16_t time = 0, date = 0;
    dosTime (time, date);
    std::vector<uint8_t> d;
    for (const auto& e : list)
    {
        put32 (d, centralSignature);
        put16 (d, 20);
        put16 (d, 20);
        put16 (d, 1 << 11);
        put16 (d, e.method);
        put16 (d, time);
        put16 (d, date);
        put32 (d, e.crc);
        put32 (d, (uint32_t) e.compressedSize);
        put32 (d, (uint32_t) e.size);
        put16 (d, (uint32_t) e.name.size());
        put16 (d, 0);
        put16 (d, 0);
        put16 (d, 0);
        put16 (d, 0);
        put32 (d, 0);
        put32 (d, (uint32_t) e.headerOffset);
        d.insert (d.end(), e.name.begin(), e.name.end());
    }
    if (list.size() >= 0xFFFF || offset + d.size() > limit)
    {
        error = "the project has too many files or is over 4 GB";
        return false;
    }
    const auto dirSize = (uint32_t) d.size();
    put32 (d, endSignature);
    put16 (d, 0);
    put16 (d, 0);
    put16 (d, (uint32_t) list.size());
    put16 (d, (uint32_t) list.size());
    put32 (d, dirSize);
    put32 (d, (uint32_t) offset);
    put16 (d, 0);
    out.write ((const char*) d.data(), (std::streamsize) d.size());
    out.close();
    if (! out)
    {
        error = path + " could not be written";
        return false;
    }
    closed = true;
    return true;
}
}
