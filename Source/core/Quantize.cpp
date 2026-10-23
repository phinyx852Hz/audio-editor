// SPDX-License-Identifier: AGPL-3.0-only
#include "Quantize.h"

#include "Fft.h"
#include "TimeStretch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>

const char* audioquantize::methodName (Method m)
{
    switch (m)
    {
        case Method::level:    return "Level (drums, plucks)";
        case Method::spectral: return "Spectral (notes, vocals)";
        case Method::combined: return "Combined";
    }
    return "";
}

namespace
{
    // Onsets from the rise of the spectrum: each 1024-point frame (hop 256) compared with
    // the one before, counting only bins that got louder (log magnitudes, so a quiet
    // note starting under a loud one still counts). Peaks above the local median by a
    // margin set by the sensitivity are onsets.
    std::vector<Frame> spectralHits (const AudioData& audio, double sensitivity, Frame minGap)
    {
        std::vector<Frame> out;
        constexpr int order = 10, size = 1 << order, hop = size / 4;
        const auto length = audio.length();
        if (length < size * 2)
            return out;
        const auto channels = audio.numChannels();
        const Fft fft (order);
        std::vector<float> window (size);
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * M_PI * i / size));
        std::vector<std::vector<float>> buffer ((size_t) channels, std::vector<float> (size));
        std::vector<float*> ptrs;
        for (auto& c : buffer)
            ptrs.push_back (c.data());
        std::vector<std::complex<float>> spectrum (size);
        std::vector<float> previous (size / 2 + 1, 0.0f), current (size / 2 + 1);
        std::vector<float> flux, energy;
        for (Frame at = 0; at + size <= length; at += hop)
        {
            audio.read (at, size, ptrs.data());
            double e = 0.0;
            for (int i = 0; i < size; ++i)
            {
                float x = 0.0f;
                for (const auto& c : buffer)
                    x += c[(size_t) i];
                x /= (float) channels;
                e += (double) x * x;
                spectrum[(size_t) i] = { x * window[(size_t) i], 0.0f };
            }
            fft.forward (spectrum);
            double f = 0.0;
            for (size_t b = 1; b < current.size(); ++b)
            {
                current[b] = std::log1p (1000.0f * std::abs (spectrum[b]) / (float) size);
                f += std::max (0.0f, current[b] - previous[b]);
            }
            std::swap (previous, current);
            flux.push_back (flux.empty() ? 0.0f : (float) f);
            energy.push_back ((float) (10.0 * std::log10 (e / size + 1e-12)));
        }
        const auto most = *std::max_element (flux.begin(), flux.end());
        if (most <= 0.0f)
            return out;
        for (auto& f : flux)
            f /= most;
        const auto margin = (float) (0.02 + 0.3 * (1.0 - std::clamp (sensitivity, 0.0, 1.0)));
        std::vector<float> near;
        for (size_t i = 1; i + 1 < flux.size(); ++i)
        {
            if (energy[i] < -55.0f || flux[i] < flux[i - 1] || flux[i] < flux[i + 1])
                continue;
            near.clear();
            for (auto j = i >= 8 ? i - 8 : 0; j <= std::min (flux.size() - 1, i + 8); ++j)
                near.push_back (flux[j]);
            std::nth_element (near.begin(), near.begin() + (long) near.size() / 2, near.end());
            if (flux[i] < near[near.size() / 2] + margin)
                continue;
            // The frame is centred on the change; its start is a hop before the centre.
            const auto at = std::max<Frame> (0, (Frame) i * hop + size / 2 - hop);
            if (out.empty() || at - out.back() >= minGap)
                out.push_back (at);
        }
        return out;
    }

    double positiveMod (double x, double n) { return x - n * std::floor (x / n); }

    // A repeatable random number from -1 to 1 for a hit.
    double jitter (unsigned seed, Frame source)
    {
        auto x = (std::uint64_t) seed * 0x9E3779B97F4A7C15ull ^ (std::uint64_t) source * 0xBF58476D1CE4E5B9ull;
        x ^= x >> 31;
        x *= 0x94D049BB133111EBull;
        x ^= x >> 29;
        return (double) (x >> 11) / (double) (1ull << 53) * 2.0 - 1.0;
    }
}

std::vector<Frame> audioquantize::hits (const AudioData& audio, const Detection& d)
{
    const auto minGap = std::max<Frame> (1, (Frame) (d.minGapMs * 0.001 * audio.sampleRate()));
    if (d.method == Method::level)
        return timestretch::transients (audio, d.sensitivity, d.minGapMs);
    auto spectral = spectralHits (audio, d.sensitivity, minGap);
    if (d.method == Method::spectral)
        return spectral;
    // Combined: the level's hits (placed more exactly), then the spectral ones not near any of them.
    auto all = timestretch::transients (audio, d.sensitivity, d.minGapMs);
    const auto near = (Frame) (0.03 * audio.sampleRate());
    for (const auto s : spectral)
    {
        const auto it = std::lower_bound (all.begin(), all.end(), s - near);
        if (it == all.end() || *it > s + near)
            all.insert (std::upper_bound (all.begin(), all.end(), s), s);
    }
    std::vector<Frame> out;
    for (const auto h : all)
        if (out.empty() || h - out.back() >= minGap)
            out.push_back (h);
    return out;
}

std::vector<Frame> audioquantize::onGrid (const std::vector<Frame>& hits, const BeatGrid& grid, double rate, double division,
                                          double tolerance)
{
    if (! grid.isSet() || division <= 0.0 || rate <= 0.0)
        return hits;
    std::vector<Frame> out;
    for (const auto h : hits)
    {
        const auto steps = grid.beatAt ((double) h / rate) / division;
        if (std::abs (steps - std::round (steps)) <= tolerance)
            out.push_back (h);
    }
    return out;
}

audioquantize::Groove audioquantize::swing (double division, double percent)
{
    Groove g;
    g.division = division;
    g.offsets = { 0.0, std::clamp (2.0 * percent / 100.0 - 1.0, -0.5, 0.5) };
    std::ostringstream name;
    name << "Swing " << (division >= 0.5 ? "8ths " : "16ths ") << percent << " %";
    g.name = name.str();
    return g;
}

std::vector<audioquantize::Groove> audioquantize::builtInGrooves()
{
    std::vector<Groove> out;
    for (const double division : { 0.25, 0.5 })
        for (const double percent : { 54.0, 58.0, 62.0, 66.0 })
            out.push_back (swing (division, percent));
    out.push_back ({ "Shuffle (8th triplets)", 0.5, { 0.0, 1.0 / 3.0 } });
    out.push_back ({ "Pushed offbeats (16ths)", 0.25, { 0.0, -0.08 } });
    // Backbeat on beats 2 and 4 (steps 4 and 12 of a bar of sixteenths) a little late.
    std::vector<double> laid (16, 0.0);
    laid[4] = laid[12] = 0.12;
    out.push_back ({ "Laid-back backbeat (16ths)", 0.25, laid });
    return out;
}

audioquantize::Groove audioquantize::extract (const std::vector<Frame>& hits, const BeatGrid& grid, double rate, double division,
                                              int steps, Frame from, Frame to)
{
    Groove g;
    if (! grid.isSet() || division <= 0.0 || steps < 1 || rate <= 0.0)
        return g;
    std::vector<std::vector<double>> slots ((size_t) steps);
    size_t found = 0;
    for (const auto h : hits)
    {
        if (h < from || h >= to)
            continue;
        const auto at = grid.beatAt ((double) h / rate) / division;
        const auto step = std::round (at);
        if (std::abs (at - step) > 0.45)
            continue;
        slots[(size_t) positiveMod (step, steps)].push_back (at - step);
        ++found;
    }
    if (found < 4)
        return g;
    g.division = division;
    for (auto& s : slots)
    {
        if (s.empty())
        {
            g.offsets.push_back (0.0);
            continue;
        }
        std::nth_element (s.begin(), s.begin() + (long) s.size() / 2, s.end());
        g.offsets.push_back (s[s.size() / 2]);
    }
    g.name = "Groove";
    return g;
}

std::string audioquantize::toText (const Groove& g)
{
    std::ostringstream out;
    auto name = g.name;
    std::replace (name.begin(), name.end(), '|', '/');
    out << name << '|' << g.division << '|';
    for (size_t i = 0; i < g.offsets.size(); ++i)
        out << (i > 0 ? "," : "") << g.offsets[i];
    return out.str();
}

audioquantize::Groove audioquantize::fromText (const std::string& text)
{
    Groove g;
    const auto a = text.find ('|');
    const auto b = a == std::string::npos ? a : text.find ('|', a + 1);
    if (b == std::string::npos)
        return g;
    try
    {
        g.name = text.substr (0, a);
        g.division = std::stod (text.substr (a + 1, b - a - 1));
        std::istringstream list (text.substr (b + 1));
        for (std::string item; std::getline (list, item, ',');)
            g.offsets.push_back (std::clamp (std::stod (item), -0.5, 0.5));
    }
    catch (...)
    {
        return {};
    }
    if (g.division <= 0.0 || g.offsets.size() > 256)
        return {};
    return g;
}

double audioquantize::lineFor (double beat, const Settings& s)
{
    const bool grooved = s.groove.isSet();
    const auto division = grooved ? s.groove.division : s.division;
    if (division <= 0.0)
        return beat;
    const auto n = (double) s.groove.offsets.size();
    auto line = [&] (double step)
    {
        double offset = 0.0;
        if (grooved)
            offset = s.groove.offsets[(size_t) positiveMod (step, n)];
        else if ((long long) std::llround (std::abs (step)) % 2 == 1)
            offset = std::clamp (s.swing, 0.0, 1.0) / 3.0;
        return (step + offset) * division;
    };
    // The nearest of the lines around it (a swung or grooved line can be nearer than the straight one).
    const auto centre = std::round (beat / division);
    auto best = line (centre);
    for (const double step : { centre - 1.0, centre + 1.0 })
        if (const auto l = line (step); std::abs (l - beat) < std::abs (best - beat))
            best = l;
    return best;
}

std::vector<std::pair<Frame, Frame>> audioquantize::place (const std::vector<Hit>& hits, const BeatGrid& grid, double rate,
                                                           const Settings& s, Frame from, Frame to)
{
    std::vector<std::pair<Frame, Frame>> out;
    const auto division = s.groove.isSet() ? s.groove.division : s.division;
    if (! grid.isSet() || division <= 0.0 || rate <= 0.0)
        return out;
    const auto gap = (Frame) (0.001 * rate);
    for (const auto& hit : hits)
    {
        if (hit.now < from || hit.now >= to)
            continue;
        const auto beat = grid.beatAt ((double) hit.now / rate);
        const auto line = lineFor (beat, s);
        const auto distance = std::abs (line - beat) / division;   // in steps; at most about half
        Frame target = hit.now;
        if (distance > s.leaveWithin / 2.0 && distance <= s.reach / 2.0 + 1e-9)
        {
            target = (Frame) std::llround (grid.secondsAt (beat + (line - beat) * std::clamp (s.strength, 0.0, 1.0)) * rate);
            if (s.randomMs > 0.0)
                target += (Frame) std::llround (jitter (s.seed, hit.source) * s.randomMs * 0.001 * rate);
        }
        // A hit that stays put needs no marker unless it was warped before (then it keeps its place).
        if (target == hit.source)
            continue;
        if (! out.empty() && (target <= out.back().second + gap || hit.source <= out.back().first + gap))
            continue;
        out.push_back ({ hit.source, target });
    }
    return out;
}
