// SPDX-License-Identifier: AGPL-3.0-only
#include "RiffChunks.h"

#include "Ixml.h"
#include "Metadata.h"
#include "Xmp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace riff
{
namespace
{
    uint32_t le32 (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    uint32_t be32 (const unsigned char* p) { return (uint32_t) p[3] | ((uint32_t) p[2] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[0] << 24); }
    uint64_t le64 (const unsigned char* p) { return (uint64_t) le32 (p) | ((uint64_t) le32 (p + 4) << 32); }

    void putLe16 (std::string& s, uint16_t v) { s += (char) (v & 0xff); s += (char) (v >> 8); }
    void putLe32 (std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += (char) ((v >> (8 * i)) & 0xff); }
    void putBe32 (std::string& s, uint32_t v) { for (int i = 3; i >= 0; --i) s += (char) ((v >> (8 * i)) & 0xff); }

    // A fixed-size text field: cut to fit, padded with zeros.
    void putFixed (std::string& s, const std::string& value, size_t size)
    {
        auto v = value.substr (0, size);
        // Do not cut a UTF-8 sequence in half.
        while (v.size() == size && ! v.empty() && ((unsigned char) v.back() & 0xc0) == 0x80)
            v.pop_back();
        if (v.size() == size && ! v.empty() && ((unsigned char) v.back() & 0xc0) == 0xc0)
            v.pop_back();
        s += v;
        s.append (size - v.size(), '\0');
    }

    std::string fixedText (const std::string& payload, size_t at, size_t size)
    {
        if (at >= payload.size())
            return {};
        auto v = payload.substr (at, std::min (size, payload.size() - at));
        v.resize (std::strlen (v.c_str()));
        while (! v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\n'))
            v.pop_back();
        return toUtf8 (v);
    }

    std::string crlfToLf (std::string s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
            if (! (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n'))
                out += s[i] == '\r' ? '\n' : s[i];
        return out;
    }

    std::string lfToCrlf (const std::string& s)
    {
        std::string out;
        for (const auto ch : s)
            out += ch == '\n' ? std::string ("\r\n") : std::string (1, ch);
        return out;
    }

    std::string valueOf (const std::vector<std::pair<std::string, std::string>>& values, const std::string& id)
    {
        for (const auto& [k, v] : values)
            if (k == id)
                return v;
        return {};
    }

    std::string number (double v)
    {
        char buf[32];
        std::snprintf (buf, sizeof buf, "%.2f", v);
        return buf;
    }

    // Chunks a WAV file's audio writer makes, or that describe the old audio and are dropped.
    const std::set<std::string> wavKnown { "fmt ", "data", "fact", "cue ", "acid", "ds64", "JUNK", "junk", "PAD ", "FLLR",
                                           "PEAK", "levl", "md5 ", "MD5 ", "DISP", "bext", "cart", "iXML", "_PMX", "id3 ", "ID3 " };
    // Chunks writeMetadata replaces.
    const std::set<std::string> wavOwned { "DISP", "bext", "cart", "iXML", "_PMX", "id3 ", "ID3 " };
    const std::set<std::string> aiffOwned { "NAME", "AUTH", "(c) ", "ANNO", "ID3 ", "id3 " };

    bool isXmpAppl (const std::string& path, const Chunk& c)
    {
        return c.id == "APPL" && c.size >= 4 && readChunk (path, { c.id, {}, c.offset, 4 }) == "XMP ";
    }
}

std::string toUtf8 (const std::string& s)
{
    // Valid UTF-8 stays as it is.
    bool valid = true;
    for (size_t i = 0; i < s.size() && valid;)
    {
        const auto c = (unsigned char) s[i];
        const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        valid = extra >= 0 && i + (size_t) extra < s.size();
        for (int k = 1; k <= extra && valid; ++k)
            valid = ((unsigned char) s[i + (size_t) k] & 0xc0) == 0x80;
        i += (size_t) std::max (extra, 0) + 1;
    }
    if (valid)
        return s;

    std::string out;
    for (const auto ch : s)
    {
        const auto c = (unsigned char) ch;
        if (c < 0x80)
            out += ch;
        else
        {
            out += (char) (0xc0 | (c >> 6));
            out += (char) (0x80 | (c & 63));
        }
    }
    return out;
}

Layout scan (const std::string& path)
{
    Layout layout;
    std::ifstream in (path, std::ios::binary);
    unsigned char head[12];
    if (! in.read ((char*) head, 12))
        return layout;

    const std::string magic ((const char*) head, 4), form ((const char*) head + 8, 4);
    if ((magic == "RIFF" || magic == "RF64" || magic == "BW64") && form == "WAVE")
        layout.rf64 = magic != "RIFF";
    else if (magic == "FORM" && (form == "AIFF" || form == "AIFC"))
        layout.aiff = true;
    else
        return layout;

    in.seekg (0, std::ios::end);
    const auto fileSize = (uint64_t) in.tellg();
    uint64_t pos = 12, rf64Data = 0;
    while (pos + 8 <= fileSize)
    {
        in.seekg ((std::streamoff) pos);
        unsigned char h[12] {};
        if (! in.read ((char*) h, 8))
            break;
        Chunk c;
        c.id.assign ((const char*) h, 4);
        c.offset = pos + 8;
        c.size = layout.aiff ? be32 (h + 4) : le32 (h + 4);
        if (layout.rf64 && c.id == "ds64" && c.size >= 16)
        {
            unsigned char d[16];
            if (in.read ((char*) d, 16))
                rf64Data = le64 (d + 8);
        }
        if (layout.rf64 && c.id == "data" && c.size == 0xffffffffu)
            c.size = rf64Data;
        c.size = std::min (c.size, fileSize - c.offset);
        if (c.id == "LIST" && c.size >= 4)
        {
            char t[4];
            in.seekg ((std::streamoff) c.offset);
            if (in.read (t, 4))
                c.listType.assign (t, 4);
        }
        layout.chunks.push_back (c);
        pos = c.offset + c.size + (c.size & 1);
    }
    layout.valid = true;
    return layout;
}

std::string readChunk (const std::string& path, const Chunk& c, uint64_t maxBytes)
{
    if (c.size > maxBytes)
        return {};
    std::ifstream in (path, std::ios::binary);
    std::string out ((size_t) c.size, '\0');
    in.seekg ((std::streamoff) c.offset);
    in.read (out.data(), (std::streamsize) c.size);
    out.resize ((size_t) std::max<std::streamsize> (0, in.gcount()));
    return out;
}

//==============================================================================
void decodeInfo (const std::string& p, Tags& tags)
{
    std::vector<std::pair<std::string, std::string>> values;
    for (size_t pos = 4; pos + 8 <= p.size();)
    {
        const auto id = p.substr (pos, 4);
        const auto size = le32 ((const unsigned char*) p.data() + pos + 4);
        const auto text = p.substr (pos + 8, std::min<size_t> (size, p.size() - pos - 8));
        values.emplace_back (id, crlfToLf (toUtf8 (std::string (text.c_str()))));
        pos += 8 + size + (size & 1);
    }
    meta::absorb (tags, meta::Schema::riff, values);
}

std::string encodeInfo (const Tags& tags, const std::string& software)
{
    auto values = meta::valuesFor (tags, meta::Schema::riff);
    std::erase_if (values, [] (const auto& v) { return v.first == "DISP" || v.first.size() != 4; });
    if (values.empty())
        return {};
    if (valueOf (values, "ISFT").empty() && ! software.empty())
        values.emplace_back ("ISFT", software);

    std::string out = "INFO";
    for (const auto& [id, value] : values)
    {
        const auto text = lfToCrlf (value) + '\0';
        out += id;
        putLe32 (out, (uint32_t) text.size());
        out += text;
        if (text.size() & 1)
            out += '\0';
    }
    return out;
}

void decodeBext (const std::string& p, Tags& tags)
{
    if (p.size() < 348)
        return;
    const auto* u = (const unsigned char*) p.data();
    std::vector<std::pair<std::string, std::string>> v {
        { "description", fixedText (p, 0, 256) },
        { "originator", fixedText (p, 256, 32) },
        { "originatorRef", fixedText (p, 288, 32) },
        { "originationDate", fixedText (p, 320, 10) },
        { "originationTime", fixedText (p, 330, 8) },
    };
    const auto timeRef = (uint64_t) le32 (u + 338) | ((uint64_t) le32 (u + 342) << 32);
    if (timeRef > 0)
        v.emplace_back ("timeReference", std::to_string (timeRef));

    const auto version = p.size() >= 348 ? (int) (u[346] | (u[347] << 8)) : 0;
    if (p.size() >= 412)
    {
        bool any = false;
        std::string hex;
        for (int i = 0; i < 64; ++i)
        {
            char b[3];
            std::snprintf (b, sizeof b, "%02x", u[348 + i]);
            hex += b;
            any = any || u[348 + i] != 0;
        }
        if (any)
            v.emplace_back ("umid", hex);
    }
    if (version >= 2 && p.size() >= 422)
    {
        const char* names[] { "loudnessValue", "loudnessRange", "maxTruePeakLevel", "maxMomentaryLoudness", "maxShortTermLoudness" };
        for (int i = 0; i < 5; ++i)
        {
            // Tech 3285 v2 2.4: 7FFFh means unused, and values outside -99.99..99.99 (0..99.99
            // for the loudness range) are ignored too.
            const auto raw = (int16_t) (u[412 + 2 * i] | (u[413 + 2 * i] << 8));
            if (raw >= (i == 1 ? 0 : -9999) && raw <= 9999)
                v.emplace_back (names[i], number (raw / 100.0));
        }
    }
    if (p.size() > 602)
    {
        auto history = p.substr (602);
        history.resize (std::strlen (history.c_str()));
        v.emplace_back ("codingHistory", crlfToLf (toUtf8 (history)));
    }
    meta::absorb (tags, meta::Schema::bext, v);
}

std::string encodeBext (const Tags& tags)
{
    const auto v = meta::valuesFor (tags, meta::Schema::bext);
    if (v.empty())
        return {};

    std::string out;
    putFixed (out, valueOf (v, "description"), 256);
    putFixed (out, valueOf (v, "originator"), 32);
    putFixed (out, valueOf (v, "originatorRef"), 32);
    putFixed (out, valueOf (v, "originationDate"), 10);
    putFixed (out, valueOf (v, "originationTime"), 8);
    const auto timeRef = (uint64_t) std::strtoull (valueOf (v, "timeReference").c_str(), nullptr, 10);
    putLe32 (out, (uint32_t) timeRef);
    putLe32 (out, (uint32_t) (timeRef >> 32));

    const char* names[] { "loudnessValue", "loudnessRange", "maxTruePeakLevel", "maxMomentaryLoudness", "maxShortTermLoudness" };
    bool loudness = false;
    for (const auto* n : names)
        loudness = loudness || ! valueOf (v, n).empty();
    putLe16 (out, loudness ? 2 : 1);

    const auto umid = valueOf (v, "umid");
    for (size_t i = 0; i < 64; ++i)
        out += (char) (2 * i + 1 < umid.size() ? std::strtoul (umid.substr (2 * i, 2).c_str(), nullptr, 16) : 0);
    for (const auto* n : names)
    {
        // Tech 3285 v2 2.4: round(100x), ties away from zero; a value outside the valid range
        // (-99.99..99.99, or 0..99.99 for the loudness range) is written as unused (7FFFh).
        const auto s = valueOf (v, n);
        const auto x = s.empty() ? 0L : std::lround (std::atof (s.c_str()) * 100.0);
        const bool valid = ! s.empty() && x >= (n == names[1] ? 0L : -9999L) && x <= 9999L;
        putLe16 (out, valid ? (uint16_t) (int16_t) x : (uint16_t) 0x7fff);
    }
    out.append (180, '\0');
    if (const auto history = valueOf (v, "codingHistory"); ! history.empty())
        out += lfToCrlf (history) + (history.back() == '\n' ? "" : "\r\n");
    if (out.size() & 1)
        out += '\0';
    return out;
}

namespace
{
    const char* standardTimers[] { "SEG1", "SEG2", "SEG3", "INT1", "INT2", "INT3", "AUD1", "AUD2" };
    const char* cartText[] { "Title", "Artist", "CutID", "ClientID", "Category", "Classification", "OutCue" };
}

void decodeCart (const std::string& p, Tags& tags)
{
    if (p.size() < 2048)
        return;
    const auto* u = (const unsigned char*) p.data();
    std::vector<std::pair<std::string, std::string>> v { { "Version", fixedText (p, 0, 4) } };
    size_t at = 4;
    for (const auto& t : cartText)
        v.emplace_back (t, fixedText (p, at, 64)), at += 64;
    v.emplace_back ("StartDate", fixedText (p, 452, 10));
    v.emplace_back ("StartTime", fixedText (p, 462, 8));
    v.emplace_back ("EndDate", fixedText (p, 470, 10));
    v.emplace_back ("EndTime", fixedText (p, 480, 8));
    v.emplace_back ("ProducerAppID", fixedText (p, 488, 64));
    v.emplace_back ("ProducerAppVersion", fixedText (p, 552, 64));
    v.emplace_back ("UserDef", fixedText (p, 616, 64));
    if (const auto level = (int32_t) le32 (u + 680); level != 0)
        v.emplace_back ("LevelReference", std::to_string (level));

    std::string others;
    for (int t = 0; t < 8; ++t)
    {
        auto usage = p.substr (684 + 8 * (size_t) t, 4);   // four characters, spaces included
        usage.resize (std::strlen (usage.c_str()));
        if (usage.empty())
            continue;
        const auto value = std::to_string (le32 (u + 688 + 8 * t));
        const bool standard = std::find_if (std::begin (standardTimers), std::end (standardTimers),
                                            [&] (const char* s) { return usage == s; }) != std::end (standardTimers);
        if (standard)
            v.emplace_back ("Timer." + usage, value);
        else
            others += (others.empty() ? "" : "\n") + usage + "=" + value;
    }
    if (! others.empty())
        v.emplace_back ("Timers", others);
    v.emplace_back ("URL", fixedText (p, 1024, 1024));
    if (p.size() > 2048)
    {
        auto text = p.substr (2048);
        text.resize (std::strlen (text.c_str()));
        v.emplace_back ("TagText", crlfToLf (toUtf8 (text)));
    }
    std::erase_if (v, [] (const auto& x) { return x.second.empty(); });
    if (v.size() == 1 && v[0].first == "Version")
        return;
    meta::absorb (tags, meta::Schema::cart, v);
}

std::string encodeCart (const Tags& tags, const std::string& software)
{
    const auto v = meta::valuesFor (tags, meta::Schema::cart);
    bool meaningful = false;
    for (const auto& [id, value] : v)
        meaningful = meaningful || (id != "Title" && id != "Artist" && id != "URL" && id != "Version");
    if (! meaningful)
        return {};   // a cart chunk with only the title and artist adds nothing

    std::string out;
    putFixed (out, valueOf (v, "Version").empty() ? "0101" : valueOf (v, "Version"), 4);
    for (const auto& t : cartText)
        putFixed (out, valueOf (v, t), 64);
    putFixed (out, valueOf (v, "StartDate"), 10);
    putFixed (out, valueOf (v, "StartTime"), 8);
    putFixed (out, valueOf (v, "EndDate"), 10);
    putFixed (out, valueOf (v, "EndTime"), 8);
    putFixed (out, valueOf (v, "ProducerAppID").empty() ? software : valueOf (v, "ProducerAppID"), 64);
    putFixed (out, valueOf (v, "ProducerAppVersion"), 64);
    putFixed (out, valueOf (v, "UserDef"), 64);
    putLe32 (out, (uint32_t) (int32_t) std::atol (valueOf (v, "LevelReference").c_str()));

    std::vector<std::pair<std::string, uint32_t>> timers;
    for (const auto* s : standardTimers)
        if (const auto value = valueOf (v, std::string ("Timer.") + s); ! value.empty())
            timers.emplace_back (s, (uint32_t) std::strtoul (value.c_str(), nullptr, 10));
    const auto others = valueOf (v, "Timers");
    for (size_t start = 0; start < others.size();)
    {
        auto nl = others.find ('\n', start);
        const auto line = others.substr (start, nl == std::string::npos ? std::string::npos : nl - start);
        if (const auto eq = line.find ('='); eq != std::string::npos)
            timers.emplace_back (line.substr (0, eq), (uint32_t) std::strtoul (line.c_str() + eq + 1, nullptr, 10));
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    timers.resize (std::min<size_t> (timers.size(), 8));
    for (size_t t = 0; t < 8; ++t)
    {
        putFixed (out, t < timers.size() ? timers[t].first : std::string(), 4);
        putLe32 (out, t < timers.size() ? timers[t].second : 0);
    }
    out.append (276, '\0');
    putFixed (out, valueOf (v, "URL"), 1024);
    if (const auto text = valueOf (v, "TagText"); ! text.empty())
        out += lfToCrlf (text) + "\r\n";
    if (out.size() & 1)
        out += '\0';
    return out;
}

//==============================================================================
std::string readMetadata (const std::string& path, Tags& tags)
{
    const auto layout = scan (path);
    if (! layout.valid)
        return {};

    std::string id3;
    if (layout.aiff)
    {
        const std::pair<const char*, const char*> text[] { { "NAME", tag::title }, { "AUTH", tag::artist },
                                                           { "(c) ", tag::copyright }, { "ANNO", tag::comment } };
        for (const auto& c : layout.chunks)
        {
            if (c.id == "ID3 " || c.id == "id3 ")
                id3 = readChunk (path, c);
            else if (isXmpAppl (path, c))
            {
                std::string error;
                xmp::read (readChunk (path, c).substr (4), tags, error);
            }
            else
                for (const auto& [id, key] : text)
                    if (c.id == id)
                    {
                        auto v = readChunk (path, c, 1u << 20);
                        v.resize (std::strlen (v.c_str()));
                        if (auto& slot = tags[key]; slot.empty())
                            slot = toUtf8 (v);
                        if (tags[key].empty())
                            tags.erase (key);
                    }
        }
        return id3;
    }

    std::map<std::string, int> seen;
    for (const auto& c : layout.chunks)
    {
        std::string error;
        if (c.id == "LIST" && c.listType == "INFO")
            decodeInfo (readChunk (path, c), tags);
        else if (c.id == "DISP" && c.size > 4)
        {
            const auto p = readChunk (path, c, 1u << 20);
            if (le32 ((const unsigned char*) p.data()) == 1)   // CF_TEXT
                meta::absorb (tags, meta::Schema::riff, { { "DISP", toUtf8 (std::string (p.c_str() + 4)) } });
        }
        else if (c.id == "bext")
            decodeBext (readChunk (path, c), tags);
        else if (c.id == "cart")
            decodeCart (readChunk (path, c), tags);
        else if (c.id == "iXML")
        {
            auto x = readChunk (path, c);
            x.resize (std::strlen (x.c_str()));
            ixml::read (x, tags, error);
        }
        else if (c.id == "_PMX")
        {
            auto x = readChunk (path, c);
            x.resize (std::strlen (x.c_str()));
            xmp::read (x, tags, error);
        }
        else if (c.id == "id3 " || c.id == "ID3 ")
            id3 = readChunk (path, c);
        else if (wavKnown.count (c.id) == 0 && ! (c.id == "LIST" && (c.listType == "INFO" || c.listType == "adtl")))
        {
            if (const auto bytes = readChunk (path, c, 16u << 20); ! bytes.empty() || c.size == 0)
            {
                const auto n = ++seen[c.id];
                tags["raw:wav:" + c.id + (n > 1 ? "#" + std::to_string (n) : "")] = meta::base64Encode (bytes);
            }
        }
    }
    return id3;
}

std::vector<std::pair<std::string, std::string>> wavChunks (const Tags& tags, const Write& w)
{
    std::vector<std::pair<std::string, std::string>> out;
    if (w.riff)
    {
        if (const auto info = encodeInfo (tags, w.software); ! info.empty())
            out.emplace_back ("LIST", info);
        if (const auto* f = meta::findId (meta::Schema::riff, "DISP"))
            if (const auto title = meta::get (tags, *f); ! title.empty())
            {
                std::string p;
                putLe32 (p, 1);
                p += title + '\0';
                if (p.size() & 1)
                    p += '\0';
                out.emplace_back ("DISP", p);
            }
    }
    if (w.bext)
        if (auto b = encodeBext (tags); ! b.empty())
            out.emplace_back ("bext", std::move (b));
    if (w.cart)
        if (auto c = encodeCart (tags, w.software); ! c.empty())
            out.emplace_back ("cart", std::move (c));
    if (w.ixml)
        if (auto x = ixml::write (tags); ! x.empty())
            out.emplace_back ("iXML", std::move (x));
    if (w.xmp)
        if (auto x = xmp::write (tags, w.software); ! x.empty())
            out.emplace_back ("_PMX", std::move (x));
    if (! w.id3.empty())
        out.emplace_back ("id3 ", w.id3);

    for (auto it = tags.lower_bound ("raw:wav:"); it != tags.end() && it->first.compare (0, 8, "raw:wav:") == 0; ++it)
        if (it->first.size() >= 12)
            out.emplace_back (it->first.substr (8, 4), meta::base64Decode (it->second));
    return out;
}

bool writeMetadata (const std::string& path, const Tags& tags, const Write& w, std::string& error)
{
    const auto layout = scan (path);
    if (! layout.valid)
    {
        error = "Not a WAV or AIFF file: " + path;
        return false;
    }

    std::vector<std::pair<std::string, std::string>> add;
    if (layout.aiff)
    {
        const std::pair<const char*, const char*> text[] { { "NAME", tag::title }, { "AUTH", tag::artist },
                                                           { "(c) ", tag::copyright }, { "ANNO", tag::comment } };
        if (w.riff)
            for (const auto& [id, key] : text)
                if (const auto it = tags.find (key); it != tags.end() && ! it->second.empty())
                    add.emplace_back (id, it->second);
        if (! w.id3.empty())
            add.emplace_back ("ID3 ", w.id3);
        if (w.xmp)
            if (const auto x = xmp::write (tags, w.software); ! x.empty())
                add.emplace_back ("APPL", "XMP " + x);
    }
    else
        add = wavChunks (tags, w);

    std::set<std::string> present;
    for (const auto& c : layout.chunks)
        present.insert (c.id);

    const auto owned = [&] (const Chunk& c)
    {
        if (layout.aiff)
            return aiffOwned.count (c.id) > 0 || isXmpAppl (path, c);
        return wavOwned.count (c.id) > 0 || (c.id == "LIST" && c.listType == "INFO");
    };
    // A kept chunk the audio writer made again is not added twice.
    std::erase_if (add, [&] (const auto& a)
    {
        const bool mine = layout.aiff ? (aiffOwned.count (a.first) > 0 || a.first == "APPL")
                                      : (wavOwned.count (a.first) > 0 || a.first == "LIST");
        return ! mine && present.count (a.first) > 0;
    });

    const auto temp = path + ".phi-meta";
    {
        std::ifstream in (path, std::ios::binary);
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        if (! in || ! out)
        {
            error = "Cannot rewrite " + path;
            return false;
        }

        // Header: written again at the end with the new size.
        char head[12];
        in.read (head, 12);
        out.write (head, 12);

        const auto writeHeader = [&] (const std::string& id, uint64_t size)
        {
            std::string h = id;
            if (layout.aiff)
                putBe32 (h, (uint32_t) size);
            else
                putLe32 (h, size > 0xffffffffull ? 0xffffffffu : (uint32_t) size);
            out.write (h.data(), 8);
        };

        bool added = false;
        const auto addNew = [&]
        {
            for (const auto& [id, payload] : add)
            {
                writeHeader (id, payload.size());
                out.write (payload.data(), (std::streamsize) payload.size());
                if (payload.size() & 1)
                    out.put ('\0');
            }
            added = true;
        };

        std::vector<char> buffer (1 << 20);
        for (const auto& c : layout.chunks)
        {
            if (! added && (c.id == (layout.aiff ? "SSND" : "data")))
                addNew();
            if (owned (c))
                continue;
            const auto size = c.size;
            writeHeader (c.id, layout.rf64 && c.id == "data" ? 0xffffffffull : size);
            in.seekg ((std::streamoff) c.offset);
            for (uint64_t left = size + (size & 1); left > 0;)
            {
                const auto n = (std::streamsize) std::min<uint64_t> (left, buffer.size());
                if (! in.read (buffer.data(), n))
                {
                    // A last chunk without its pad byte.
                    out.write (buffer.data(), in.gcount());
                    if (in.gcount() < n && left == 1)
                        out.put ('\0');
                    break;
                }
                out.write (buffer.data(), n);
                left -= (uint64_t) n;
            }
        }
        if (! added)
            addNew();

        const auto total = (uint64_t) out.tellp() - 8;
        if (! layout.rf64 && total > 0xffffffffull)
        {
            error = "The file would pass 4 GB with its metadata";
            out.close();
            std::filesystem::remove (temp);
            return false;
        }
        out.seekp (4);
        std::string size;
        if (layout.aiff)
            putBe32 (size, (uint32_t) total);
        else
            putLe32 (size, layout.rf64 ? 0xffffffffu : (uint32_t) total);
        out.write (size.data(), 4);

        // RF64 keeps the real size in ds64, the first chunk.
        if (layout.rf64 && ! layout.chunks.empty() && layout.chunks[0].id == "ds64")
        {
            std::string big;
            putLe32 (big, (uint32_t) total);
            putLe32 (big, (uint32_t) (total >> 32));
            out.seekp (12 + 8);
            out.write (big.data(), 8);
        }
        if (! out)
        {
            error = "Writing failed (disk full?)";
            out.close();
            std::filesystem::remove (temp);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename (temp, path, ec);
    if (ec)
    {
        error = "Could not replace " + path + ": " + ec.message();
        std::filesystem::remove (temp);
        return false;
    }
    return true;
}
}
