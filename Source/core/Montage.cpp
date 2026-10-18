// SPDX-License-Identifier: AGPL-3.0-only
#include "Montage.h"
#include "Dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

Frame Montage::clipStart (int index) const
{
    Frame position = 0;
    for (int i = 0; i <= index && i < (int) clips.size(); ++i)
    {
        if (i > 0)
            position += clips[(size_t) i - 1].length();
        position += clips[(size_t) i].pauseBefore;
    }
    return std::max<Frame> (position, 0);
}

Frame Montage::length() const
{
    Frame end = 0;
    for (int i = 0; i < (int) clips.size(); ++i)
        end = std::max (end, clipEnd (i));
    return end;
}

int Montage::clipAt (Frame position) const
{
    for (int i = (int) clips.size() - 1; i >= 0; --i)
        if (position >= clipStart (i) && position < clipEnd (i))
            return i;
    return -1;
}

Frame Montage::overlapBefore (int index) const
{
    if (index <= 0 || index >= (int) clips.size())
        return 0;
    return std::max<Frame> (0, clipEnd (index - 1) - clipStart (index));
}

void Montage::clampPauses()
{
    for (size_t i = 0; i < clips.size(); ++i)
    {
        auto& c = clips[i];
        c.sourceEnd = std::max (c.sourceEnd, c.sourceStart);
        if (i == 0)
            c.pauseBefore = std::max<Frame> (c.pauseBefore, 0);
        else
            c.pauseBefore = std::max (c.pauseBefore, -std::min (c.length(), clips[i - 1].length()));
        c.fadeIn = std::clamp<Frame> (c.fadeIn, 0, c.length());
        c.fadeOut = std::clamp<Frame> (c.fadeOut, 0, c.length());
    }
}

namespace
{
    // Fade lengths of a song: the overlap with its neighbour (a crossfade) or its own fade, whichever is longer.
    struct Edges
    {
        Frame in = 0, out = 0;
        bool crossIn = false, crossOut = false;
    };

    Edges edgesOf (const Montage& m, int index)
    {
        const auto& c = m.clips[(size_t) index];
        const auto before = m.overlapBefore (index);
        const auto after = m.overlapBefore (index + 1);
        Edges e;
        e.in = std::min (std::max (before, c.fadeIn), c.length());
        e.out = std::min (std::max (after, c.fadeOut), c.length());
        e.crossIn = before > 0;
        e.crossOut = after > 0;
        return e;
    }
}

namespace
{
    float envelopeWith (const MontageClip& c, const Edges& e, Frame t)
    {
        const auto len = c.length();
        if (t < 0 || t >= len)
            return 0.0f;

        auto g = (float) dsp::dbToGain (c.gainDb);
        if (t < e.in)
            g *= dsp::fadeGain (((double) t + 0.5) / (double) e.in, e.crossIn ? dsp::FadeShape::equalPower : dsp::FadeShape::sCurve);
        if (t >= len - e.out)
            g *= dsp::fadeGain (((double) (len - t) - 0.5) / (double) e.out, e.crossOut ? dsp::FadeShape::equalPower : dsp::FadeShape::sCurve);
        return g;
    }
}

float Montage::envelope (int index, Frame t) const
{
    return envelopeWith (clips[(size_t) index], edgesOf (*this, index), t);
}

AudioDataPtr Montage::mixdown() const
{
    const auto total = length();
    AudioData::Builder out (channels, sampleRate);
    if (total <= 0)
        return out.finish();

    const auto n = (int) clips.size();
    std::vector<Frame> starts ((size_t) n), ends ((size_t) n);
    std::vector<Edges> edges ((size_t) n);
    std::vector<Frame> points { 0, total };
    for (int i = 0; i < n; ++i)
    {
        starts[(size_t) i] = clipStart (i);
        ends[(size_t) i] = clipEnd (i);
        edges[(size_t) i] = edgesOf (*this, i);
        for (const auto p : { starts[(size_t) i], starts[(size_t) i] + edges[(size_t) i].in,
                              ends[(size_t) i] - edges[(size_t) i].out, ends[(size_t) i] })
            points.push_back (std::clamp<Frame> (p, 0, total));
    }
    std::sort (points.begin(), points.end());
    points.erase (std::unique (points.begin(), points.end()), points.end());

    constexpr int block = AudioChunk::maxFrames;
    std::vector<std::vector<float>> mix ((size_t) channels, std::vector<float> (block));
    std::vector<std::vector<float>> src;
    std::vector<float*> srcPtrs;
    std::vector<const float*> mixPtrs;
    for (auto& m : mix)
        mixPtrs.push_back (m.data());

    for (size_t k = 0; k + 1 < points.size(); ++k)
    {
        const auto a = points[k], b = points[k + 1];
        std::vector<int> active;
        for (int i = 0; i < n; ++i)
            if (starts[(size_t) i] <= a && ends[(size_t) i] >= b && clips[(size_t) i].source != nullptr)
                active.push_back (i);

        // One song at full level, between its fades: share its audio instead of copying it.
        if (active.size() == 1)
        {
            const auto i = active[0];
            const auto& c = clips[(size_t) i];
            const auto& e = edges[(size_t) i];
            if (std::abs (c.gainDb) < 1.0e-9 && c.source->numChannels() == channels
                && a >= starts[(size_t) i] + e.in && b <= ends[(size_t) i] - e.out
                && c.sourceStart + (b - starts[(size_t) i]) <= c.source->length())
            {
                out.appendShared (*c.source, c.sourceStart + (a - starts[(size_t) i]), c.sourceStart + (b - starts[(size_t) i]));
                continue;
            }
        }

        for (Frame pos = a; pos < b; pos += block)
        {
            const auto count = (int) std::min<Frame> (block, b - pos);
            for (auto& m : mix)
                std::fill (m.begin(), m.begin() + count, 0.0f);

            for (const auto i : active)
            {
                const auto& c = clips[(size_t) i];
                const auto sourceChannels = c.source->numChannels();
                if ((int) src.size() < sourceChannels)
                {
                    src.resize ((size_t) sourceChannels, std::vector<float> (block));
                    srcPtrs.clear();
                    for (auto& s : src)
                        srcPtrs.push_back (s.data());
                }
                const auto local = pos - starts[(size_t) i];
                c.source->read (c.sourceStart + local, count, srcPtrs.data());

                for (int f = 0; f < count; ++f)
                {
                    const auto g = envelopeWith (c, edges[(size_t) i], local + f);
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        // A mono song fills every channel; extra channels of a song are left out.
                        const auto from = sourceChannels == 1 ? 0 : ch;
                        if (from < sourceChannels)
                            mix[(size_t) ch][(size_t) f] += g * src[(size_t) from][(size_t) f];
                    }
                }
            }
            out.append (mixPtrs.data(), count);
        }
    }

    return out.finish();
}

std::string Montage::clipName (int index) const
{
    const auto& c = clips[(size_t) index];
    if (const auto t = c.tags.find (tag::title); t != c.tags.end() && ! t->second.empty())
        return t->second;

    auto name = c.path.substr (c.path.find_last_of ('/') + 1);
    if (const auto dot = name.find_last_of ('.'); dot != std::string::npos && dot > 0)
        name.resize (dot);
    return name;
}

std::vector<Marker> Montage::trackMarkers() const
{
    std::vector<Marker> markers;
    for (int i = 0; i < (int) clips.size(); ++i)
    {
        Marker m;
        m.id = i + 1;
        m.type = Marker::Type::region;
        m.start = clipStart (i);
        m.end = clipEnd (i);
        m.name = std::to_string (i + 1) + ". " + clipName (i);
        markers.push_back (m);
    }
    return markers;
}

std::string cueTime (Frame position, double sampleRate)
{
    const auto frames = sampleRate > 0.0 ? (long long) std::floor ((double) position * 75.0 / sampleRate + 1.0e-9) : 0LL;
    char text[32];
    std::snprintf (text, sizeof (text), "%02lld:%02lld:%02lld", frames / (75 * 60), (frames / 75) % 60, frames % 75);
    return text;
}

namespace
{
    // CUE strings are quoted; a quote inside becomes an apostrophe.
    std::string quoted (std::string s)
    {
        std::replace (s.begin(), s.end(), '"', '\'');
        std::replace (s.begin(), s.end(), '\n', ' ');
        return "\"" + s + "\"";
    }

    std::string tagOf (const Tags& tags, const char* key)
    {
        const auto t = tags.find (key);
        return t != tags.end() ? t->second : std::string();
    }
}

std::string Montage::cueSheet (const std::string& audioFileName) const
{
    std::string type = "WAVE";
    const auto dot = audioFileName.find_last_of ('.');
    auto ext = dot == std::string::npos ? std::string() : audioFileName.substr (dot + 1);
    std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
    if (ext == "mp3")
        type = "MP3";
    else if (ext == "aif" || ext == "aiff")
        type = "AIFF";

    std::string cue = "REM COMMENT \"PHINYX\"\n";
    if (const auto catalog = tagOf (tags, tag::catalog); ! catalog.empty())
        cue += "CATALOG " + catalog + "\n";
    if (const auto artist = tagOf (tags, tag::artist); ! artist.empty())
        cue += "PERFORMER " + quoted (artist) + "\n";
    if (const auto title = tagOf (tags, tag::album); ! title.empty())
        cue += "TITLE " + quoted (title) + "\n";
    cue += "FILE " + quoted (audioFileName) + " " + type + "\n";

    for (int i = 0; i < (int) clips.size(); ++i)
    {
        char number[16];
        std::snprintf (number, sizeof (number), "%02d", i + 1);
        cue += std::string ("  TRACK ") + number + " AUDIO\n";
        const auto& c = clips[(size_t) i];
        cue += "    TITLE " + quoted (clipName (i)) + "\n";
        if (const auto artist = tagOf (c.tags, tag::artist); ! artist.empty())
            cue += "    PERFORMER " + quoted (artist) + "\n";
        if (const auto isrc = tagOf (c.tags, tag::isrc); ! isrc.empty())
            cue += "    ISRC " + isrc + "\n";
        // The pause before a song belongs to it, as its pregap (index 0).
        if (i > 0 && c.pauseBefore > 0)
            cue += "    INDEX 00 " + cueTime (clipEnd (i - 1), sampleRate) + "\n";
        cue += "    INDEX 01 " + cueTime (clipStart (i), sampleRate) + "\n";
    }
    return cue;
}
