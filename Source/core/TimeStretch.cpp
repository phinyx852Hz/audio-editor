// SPDX-License-Identifier: AGPL-3.0-only
#include "TimeStretch.h"

#include "Dsp.h"
#include "Paulstretch.h"
#include "WarpEdit.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

//==============================================================================
Frame WarpEdit::toTarget (Frame f) const
{
    const auto length = source != nullptr ? source->length() : 0;
    Frame s0 = 0, t0 = 0;
    for (const auto& m : markers)
    {
        if (f < m.source)
            return t0 + (Frame) std::llround ((double) (f - s0) * (double) (m.target - t0) / (double) std::max<Frame> (1, m.source - s0));
        s0 = m.source;
        t0 = m.target;
    }
    return t0 + (Frame) std::llround ((double) (f - s0) * (double) (length - t0) / (double) std::max<Frame> (1, length - s0));
}

Frame WarpEdit::toSource (Frame f) const
{
    const auto length = source != nullptr ? source->length() : 0;
    Frame s0 = 0, t0 = 0;
    for (const auto& m : markers)
    {
        if (f < m.target)
            return s0 + (Frame) std::llround ((double) (f - t0) * (double) (m.source - s0) / (double) std::max<Frame> (1, m.target - t0));
        s0 = m.source;
        t0 = m.target;
    }
    return s0 + (Frame) std::llround ((double) (f - t0) * (double) (length - s0) / (double) std::max<Frame> (1, length - t0));
}

//==============================================================================
double BeatGrid::beatAt (double seconds) const
{
    return isSet() ? (seconds - downbeat) / beatSeconds() : 0.0;
}

double BeatGrid::secondsAt (double beat) const
{
    return downbeat + beat * beatSeconds();
}

double BeatGrid::snap (double seconds, double division) const
{
    if (! isSet() || division <= 0.0)
        return seconds;
    return secondsAt (std::round (beatAt (seconds) / division) * division);
}

void BeatGrid::normalise()
{
    if (! isSet())
        return;
    beatsPerBar = std::clamp (beatsPerBar, 1, 32);
    const auto bar = barSeconds();
    downbeat = std::fmod (downbeat, bar);
    if (downbeat < 0.0)
        downbeat += bar;
}

BeatGrid beatgrid::fit (const std::vector<double>& beats, const std::vector<int>& beatInBar, int beatsPerBar)
{
    BeatGrid grid;
    if (beats.size() < 4)
        return grid;

    std::vector<double> gaps;
    for (size_t i = 1; i < beats.size(); ++i)
        if (beats[i] > beats[i - 1])
            gaps.push_back (beats[i] - beats[i - 1]);
    if (gaps.size() < 3)
        return grid;
    std::nth_element (gaps.begin(), gaps.begin() + (long) gaps.size() / 2, gaps.end());
    const auto median = gaps[gaps.size() / 2];

    // Number each beat by the median gap, then fit time = a + b * number.
    std::vector<double> numbers;
    for (const auto t : beats)
        numbers.push_back (std::round ((t - beats.front()) / median));
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    const auto n = (double) beats.size();
    for (size_t i = 0; i < beats.size(); ++i)
    {
        sx += numbers[i];
        sy += beats[i];
        sxx += numbers[i] * numbers[i];
        sxy += numbers[i] * beats[i];
    }
    const auto denominator = n * sxx - sx * sx;
    if (std::abs (denominator) < 1e-12)
        return grid;
    const auto b = (n * sxy - sx * sy) / denominator;
    const auto a = (sy - b * sx) / n;
    if (b <= 0.0)
        return grid;

    size_t first = 0;
    for (size_t i = 0; i < beatInBar.size() && i < beats.size(); ++i)
        if (beatInBar[i] == 1)
        {
            first = i;
            break;
        }
    grid.bpm = 60.0 / b;
    grid.beatsPerBar = beatsPerBar;
    grid.downbeat = a + b * numbers[first];
    grid.normalise();
    return grid;
}

BeatGrid beatgrid::fromLoop (double length, const std::vector<double>& hits, double near)
{
    BeatGrid grid;
    if (hits.size() < 4 || length <= 0.0)
        return grid;
    struct Candidate { double bpm, score; };
    std::vector<Candidate> candidates;
    for (int bars = 16; bars >= 1; --bars)
    {
        const auto bpm = 240.0 * bars / length;
        if (bpm < 60.0 || bpm > 200.0)
            continue;
        const auto step = 15.0 / bpm;
        const auto tolerance = std::max (0.012, 0.15 * step);
        size_t on = 0;
        for (const auto t : hits)
            if (std::abs (t - std::round (t / step) * step) <= tolerance)
                ++on;
        candidates.push_back ({ bpm, (double) on / (double) hits.size() });
    }
    if (candidates.empty())
        return grid;
    auto best = 0.0;
    for (const auto& c : candidates)
        best = std::max (best, c.score);
    if (best < 0.8)
        return grid;
    // Candidates run from slow to fast; a faster tempo always fits at least as well,
    // so the slowest that fits about as well as the best is the honest reading.
    const Candidate* pick = nullptr;
    for (const auto& c : candidates)
        if (c.score >= best - 0.05)
        {
            if (pick == nullptr)
                pick = &c;
            if (near > 0.0 && std::abs (c.bpm / near - 1.0) <= 0.04)
            {
                pick = &c;
                break;
            }
        }
    grid.bpm = pick->bpm;
    grid.downbeat = 0.0;
    return grid;
}

namespace
{
    // Per hop: the level (in dB) of the mono mix and of its low end (one-pole low-pass at 150 Hz).
    void levels (const AudioData& audio, Frame hop, std::vector<float>& level, std::vector<float>& low)
    {
        const auto rate = audio.sampleRate();
        const auto hops = (size_t) (audio.length() / hop);
        level.assign (hops, 0.0f);
        low.assign (hops, 0.0f);
        const auto channels = audio.numChannels();
        std::vector<std::vector<float>> buffer ((size_t) channels, std::vector<float> ((size_t) hop));
        std::vector<float*> ptrs;
        for (auto& c : buffer)
            ptrs.push_back (c.data());
        const auto k = (float) (1.0 - std::exp (-2.0 * M_PI * 150.0 / rate));
        float lp = 0.0f;
        for (size_t h = 0; h < hops; ++h)
        {
            audio.read ((Frame) h * hop, (int) hop, ptrs.data());
            double e = 0.0, el = 0.0;
            for (Frame i = 0; i < hop; ++i)
            {
                float x = 0.0f;
                for (const auto& c : buffer)
                    x += c[(size_t) i];
                x /= (float) channels;
                lp += k * (x - lp);
                e += (double) x * x;
                el += (double) lp * lp;
            }
            level[h] = (float) (10.0 * std::log10 (e / (double) hop + 1e-12));
            low[h] = (float) (10.0 * std::log10 (el / (double) hop + 1e-12));
        }
    }

    // The rise in level over 3 hops: sharp at a hit.
    float rise (const std::vector<float>& v, size_t h) { return h >= 3 ? std::max (0.0f, v[h] - v[h - 3]) : 0.0f; }
}

BeatGrid beatgrid::align (const AudioData& audio, BeatGrid grid, bool chooseBar1)
{
    if (! grid.isSet() || audio.length() == 0)
        return grid;
    const auto rate = audio.sampleRate();
    const auto hop = std::max<Frame> (1, (Frame) (0.001 * rate));   // 1 ms
    const auto hops = (size_t) (audio.length() / hop);
    if (hops < 16)
        return grid;

    std::vector<float> level, low;
    levels (audio, hop, level, low);
    std::vector<double> shifts;
    const auto window = (long) 50;   // ms either side
    const auto last = grid.beatAt (audio.lengthSeconds());
    for (auto beat = std::ceil (grid.beatAt (0.0)); beat <= last; beat += 1.0)
    {
        const auto at = (long) std::llround (grid.secondsAt (beat) * 1000.0);
        long best = -1;
        float bestRise = 6.0f;   // dB: quieter changes are not hits
        for (auto h = std::max (3L, at - window); h <= std::min ((long) hops - 1, at + window); ++h)
            if (const auto r = rise (level, (size_t) h); r > bestRise)
            {
                bestRise = r;
                best = h;
            }
        if (best >= 0)
            shifts.push_back ((double) (best - 2 - at) / 1000.0);   // the rise starts about 2 ms before its 3 ms peak
    }
    if (shifts.size() >= 4)
    {
        std::nth_element (shifts.begin(), shifts.begin() + (long) shifts.size() / 2, shifts.end());
        grid.downbeat += shifts[shifts.size() / 2];
    }

    if (chooseBar1 && grid.beatsPerBar > 1)
    {
        std::vector<double> punch ((size_t) grid.beatsPerBar, 0.0);
        for (auto beat = std::ceil (grid.beatAt (0.0)); beat <= last; beat += 1.0)
        {
            const auto at = (long) std::llround (grid.secondsAt (beat) * 1000.0);
            float best = 0.0f;
            for (auto h = std::max (3L, at - 10); h <= std::min ((long) hops - 1, at + 15); ++h)
                best = std::max (best, rise (low, (size_t) h));
            const auto inBar = ((long) beat % grid.beatsPerBar + grid.beatsPerBar) % grid.beatsPerBar;
            punch[(size_t) inBar] += best;
        }
        // The beat of the bar with the most punch; when several are about as strong
        // (kicks on 1 and 3), the one that comes first in the file.
        const auto most = *std::max_element (punch.begin(), punch.end());
        if (most > 0.0)
        {
            const auto bar = grid.barSeconds();
            auto earliest = bar;
            for (size_t b = 0; b < punch.size(); ++b)
                if (punch[b] >= most * 0.87)
                {
                    auto at = std::fmod (grid.secondsAt ((double) b), bar);
                    at = at < 0.0 ? at + bar : at;
                    earliest = std::min (earliest, at);
                }
            grid.downbeat = earliest;
        }
    }
    grid.normalise();
    return grid;
}

std::vector<Frame> timestretch::transients (const AudioData& audio, double sensitivity, double minGapMs)
{
    std::vector<Frame> out;
    const auto hop = std::max<Frame> (1, (Frame) (0.001 * audio.sampleRate()));
    std::vector<float> level, low;
    levels (audio, hop, level, low);
    if (level.size() < 16)
        return out;
    // The level over the last 5 ms, so the flicker of noisy decays (snares, cymbals) is not taken for hits.
    std::vector<float> smooth (level.size());
    for (size_t h = 0; h < level.size(); ++h)
    {
        double sum = 0.0;
        const auto first = h >= 4 ? h - 4 : 0;
        for (auto j = first; j <= h; ++j)
            sum += std::pow (10.0, level[j] / 10.0);
        smooth[h] = (float) (10.0 * std::log10 (sum / (double) (h - first + 1) + 1e-12));
    }
    const auto riseAt = [&] (size_t h) { return h >= 5 ? std::max (0.0f, smooth[h] - smooth[h - 5]) : 0.0f; };

    // A hit: the level rises this much in 5 ms (16 dB at 0 sensitivity to 5 dB at 1),
    // above -55 dB, and is the biggest rise within the gap (50 ms unless asked otherwise).
    const auto threshold = (float) (16.0 - 11.0 * std::clamp (sensitivity, 0.0, 1.0));
    const auto gap = (size_t) std::clamp (minGapMs, 5.0, 2000.0);
    for (size_t h = 5; h < level.size(); ++h)
    {
        const auto r = riseAt (h);
        if (r < threshold || smooth[h] < -55.0f)
            continue;
        bool biggest = true;
        for (size_t j = h > gap ? h - gap : 5; j <= std::min (level.size() - 1, h + gap) && biggest; ++j)
            biggest = j == h || riseAt (j) < r || (riseAt (j) <= r && j > h);
        if (! biggest)
            continue;
        // Where it starts: the first millisecond 6 dB over the level before the rise.
        auto start = h - 5;
        while (start < h && level[start] < smooth[h - 5] + 6.0f)
            ++start;
        const auto at = (Frame) (start > 0 ? start - 1 : 0) * hop;
        if (out.empty() || at - out.back() >= (Frame) gap * hop)
            out.push_back (at);
    }
    return out;
}

std::vector<std::pair<Frame, Frame>> timestretch::quantize (const std::vector<Frame>& hits, const BeatGrid& grid, double rate,
                                                            const Quantize& q, Frame from, Frame to)
{
    std::vector<audioquantize::Hit> at;
    for (const auto h : hits)
        at.push_back ({ h, h });
    return audioquantize::place (at, grid, rate, q, from, to);
}

//==============================================================================
const char* timestretch::modeName (Mode m)
{
    switch (m)
    {
        case Mode::drums:        return "Drums";
        case Mode::melody:       return "Melody";
        case Mode::fullMix:      return "Full Mix";
        case Mode::fullMixVoice: return "Full Mix + Voice";
        case Mode::varispeed:    return "Varispeed";
        case Mode::beats:        return "Beats";
        case Mode::beatsNoLoop:  return "Beats, No Loop";
        case Mode::texture:      return "Texture";
        case Mode::smooth:       return "Smooth";
        case Mode::ambient:      return "Ambient";
    }
    return "";
}

const char* timestretch::modeDescription (Mode m)
{
    switch (m)
    {
        case Mode::drums:        return "Phase vocoder with short windows: hits stay sharp. Drum loops and percussion.";
        case Mode::melody:       return "High-quality pitch: single voices, bass and lead instruments.";
        case Mode::fullMix:      return "The all-rounder for whole songs and mixed material.";
        case Mode::fullMixVoice: return "Full Mix that keeps formants, so voices keep their character.";
        case Mode::varispeed:    return "Like tape: speed and pitch change together. No artefacts.";
        case Mode::beats:        return "No stretching: cut at every hit, each piece starts on its new place; "
                                        "longer gaps loop the piece's tail. Hits stay exactly as recorded.";
        case Mode::beatsNoLoop:  return "As Beats, but a piece moved apart rings out and falls silent instead of looping.";
        case Mode::texture:      return "Overlapping grains, slightly scattered: pads, drones, noise and ambience.";
        case Mode::smooth:       return "A soft phase vocoder with long windows for sustained sounds without sharp hits.";
        case Mode::ambient:      return "Paulstretch: smeared into a smooth, evolving wash. Made for huge stretches "
                                        "(8 times and more) of pads, voices and drones; hits disappear.";
    }
    return "";
}

std::vector<timestretch::Mode> timestretch::modes()
{
    return { Mode::fullMix, Mode::fullMixVoice, Mode::drums, Mode::beats, Mode::beatsNoLoop, Mode::melody,
             Mode::texture, Mode::smooth, Mode::ambient, Mode::varispeed };
}

namespace
{
    using namespace timestretch;

    // The anchors as whole segments: (0, 0) first and (length, resultLength) last,
    // dropping any that would run backwards or sit on top of each other.
    std::vector<Anchor> segments (std::vector<Anchor> anchors, Frame length, Frame resultLength)
    {
        std::sort (anchors.begin(), anchors.end(), [] (const Anchor& x, const Anchor& y) { return x.source < y.source; });
        std::vector<Anchor> out { { 0, 0 } };
        for (const auto& a : anchors)
            if (a.source > out.back().source && a.source < length && a.target > out.back().target && a.target < resultLength)
                out.push_back (a);
        out.push_back ({ length, resultLength });
        return out;
    }

    bool cancelled (const std::atomic<bool>* cancel) { return cancel != nullptr && cancel->load(); }

    // Each segment resampled to its new length, so speed and pitch change together.
    AudioDataPtr varispeed (const AudioData& audio, Frame start, const std::vector<Anchor>& points, std::string& error,
                            std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        const auto channels = audio.numChannels();
        const auto rate = audio.sampleRate();
        AudioData::Builder out (channels, rate);
        std::vector<float*> pointers ((size_t) channels);
        const auto total = (double) std::max<Frame> (1, points.back().target);
        for (size_t i = 1; i < points.size(); ++i)
        {
            const auto from = points[i - 1], to = points[i];
            const auto inLength = to.source - from.source, outLength = to.target - from.target;
            AudioData::Builder piece (channels, rate);
            piece.appendShared (audio, start + from.source, start + to.source);
            const auto source = piece.finish();
            const auto done = (double) from.target;
            const auto moved = dsp::resample (*source, rate * (double) outLength / (double) inLength, [&] (float p)
            {
                if (progress != nullptr)
                    progress->store ((float) ((done + p * (double) outLength) / total));
                return ! cancelled (cancel);
            });
            if (moved == nullptr || cancelled (cancel))
            {
                error = "Cancelled.";
                return nullptr;
            }
            // Exactly the segment's length (the resampler can differ by a frame).
            std::vector<std::vector<float>> buffer ((size_t) channels, std::vector<float> ((size_t) outLength, 0.0f));
            for (int c = 0; c < channels; ++c)
                pointers[(size_t) c] = buffer[(size_t) c].data();
            moved->read (0, (int) std::min (outLength, moved->length()), pointers.data());
            out.append (pointers.data(), (int) outLength);
        }
        return out.finish();
    }
}

namespace
{
    // [start, end) at one ratio, streamed through Rubber Band's offline mode.
    AudioDataPtr renderConstant (const AudioData& audio, Frame start, Frame end, Frame resultLength, Mode mode, std::string& error,
                                 std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    const auto length = end - start;
    using RB = RubberBand::RubberBandStretcher;
    RB::Options options = RB::OptionProcessOffline | RB::OptionEngineFiner | RB::OptionChannelsTogether;
    switch (mode)
    {
        case Mode::drums:        options |= RB::OptionWindowShort; break;
        case Mode::melody:       options |= RB::OptionPitchHighQuality; break;
        case Mode::fullMixVoice: options |= RB::OptionFormantPreserved; break;
        case Mode::smooth:       options = RB::OptionProcessOffline | RB::OptionEngineFaster | RB::OptionChannelsTogether
                                         | RB::OptionTransientsSmooth | RB::OptionWindowLong; break;
        case Mode::fullMix:
        case Mode::varispeed:
        case Mode::beats:
        case Mode::beatsNoLoop:
        case Mode::texture:
        case Mode::ambient:      break;
    }
    const auto channels = audio.numChannels();
    const auto rate = audio.sampleRate();
    RB stretcher ((size_t) std::lround (rate), (size_t) channels, options);
    constexpr int block = 4096;
    stretcher.setMaxProcessSize (block);
    stretcher.setExpectedInputDuration ((size_t) length);
    stretcher.setTimeRatio ((double) resultLength / (double) length);

    std::vector<std::vector<float>> in ((size_t) channels, std::vector<float> (block)), scratch ((size_t) channels, std::vector<float> (block));
    std::vector<float*> inPtrs, scratchPtrs;
    for (auto& c : in)
        inPtrs.push_back (c.data());
    for (auto& c : scratch)
        scratchPtrs.push_back (c.data());

    // Study the whole range first (offline mode plans the stretch from it).
    for (Frame at = 0; at < length; at += block)
    {
        const auto n = (int) std::min<Frame> (block, length - at);
        audio.read (start + at, n, inPtrs.data());
        stretcher.study (inPtrs.data(), (size_t) n, at + n >= length);
        if (progress != nullptr)
            progress->store (0.3f * (float) at / (float) length);
        if (cancelled (cancel))
        {
            error = "Cancelled.";
            return nullptr;
        }
    }
    AudioData::Builder out (channels, rate);
    Frame written = 0;
    const auto collect = [&]
    {
        for (int available = stretcher.available(); available > 0; available = stretcher.available())
        {
            const auto got = (int) stretcher.retrieve (scratchPtrs.data(), (size_t) std::min (available, block));
            const auto keep = (int) std::min<Frame> (got, resultLength - written);
            if (keep > 0)
            {
                out.append (scratchPtrs.data(), keep);
                written += keep;
            }
            if (got <= 0)
                break;
        }
    };
    for (Frame at = 0; at < length; at += block)
    {
        const auto n = (int) std::min<Frame> (block, length - at);
        audio.read (start + at, n, inPtrs.data());
        stretcher.process (inPtrs.data(), (size_t) n, at + n >= length);
        collect();
        if (progress != nullptr)
            progress->store (0.3f + 0.7f * (float) at / (float) length);
        if (cancelled (cancel))
        {
            error = "Cancelled.";
            return nullptr;
        }
    }
    collect();

    // Rubber Band's length can differ from the ratio's by a few frames; pad with silence.
    if (written < resultLength)
    {
        for (auto& c : scratch)
            std::fill (c.begin(), c.end(), 0.0f);
        while (written < resultLength)
        {
            const auto n = (int) std::min<Frame> (block, resultLength - written);
            out.append (scratchPtrs.data(), n);
            written += n;
        }
    }
    if (progress != nullptr)
        progress->store (1.0f);
    return out.finish();
}
}

namespace
{
    // Where a frame of the range lands in the result, and back, along the anchors.
    double mapThrough (const std::vector<Anchor>& points, double f, bool forward)
    {
        for (size_t i = 1; i < points.size(); ++i)
        {
            const auto a = forward ? points[i - 1].source : points[i - 1].target, b = forward ? points[i].source : points[i].target;
            const auto x = forward ? points[i - 1].target : points[i - 1].source, y = forward ? points[i].target : points[i].source;
            if (f < (double) b || i + 1 == points.size())
                return (double) x + (f - (double) a) * (double) (y - x) / (double) std::max<Frame> (1, b - a);
        }
        return f;
    }

    AudioDataPtr fromChannels (std::vector<std::vector<float>>& result, double rate)
    {
        AudioData::Builder out ((int) result.size(), rate);
        const auto length = result.empty() ? Frame (0) : (Frame) result[0].size();
        for (Frame done = 0; done < length; done += 65536)
        {
            const auto n = (int) std::min<Frame> (65536, length - done);
            std::vector<const float*> at;
            for (auto& c : result)
                at.push_back (c.data() + done);
            out.append (at.data(), n);
        }
        return out.finish();
    }

    // Beats: the range cut at the anchors and hits; each piece copied, unstretched, to
    // where its start lands. A piece given more room loops its second half (crossfaded)
    // or, with `loop` off, rings out and fades; one given less is cut with a short fade.
    AudioDataPtr beatsRender (const AudioData& audio, Frame start, const std::vector<Anchor>& points, std::vector<Frame> hits,
                              bool loop, std::string& error, std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        const auto channels = audio.numChannels();
        const auto rate = audio.sampleRate();
        const auto length = points.back().source, resultLength = points.back().target;
        if (hits.empty())
            hits = timestretch::transients (*audio.slice (start, start + length));
        // The cuts: every anchor, and the hits not within 5 ms of one.
        std::vector<Frame> cuts;
        for (const auto& p : points)
            cuts.push_back (p.source);
        const auto near = (Frame) (0.005 * rate);
        for (const auto h : hits)
            if (h > near && h < length - near)
            {
                const auto it = std::lower_bound (cuts.begin(), cuts.end(), h);
                if ((it == cuts.end() || *it - h > near) && (it == cuts.begin() || h - *(it - 1) > near))
                    cuts.insert (it, h);
            }
        std::vector<std::vector<float>> result ((size_t) channels, std::vector<float> ((size_t) resultLength, 0.0f));
        std::vector<std::vector<float>> piece ((size_t) channels);
        std::vector<float*> ptrs ((size_t) channels);
        const auto fade = std::max<Frame> (2, (Frame) (0.003 * rate));
        for (size_t i = 1; i < cuts.size(); ++i)
        {
            const auto a = cuts[i - 1], b = cuts[i];
            const auto ta = std::clamp<Frame> ((Frame) std::llround (mapThrough (points, (double) a, true)), 0, resultLength);
            const auto tb = std::clamp<Frame> ((Frame) std::llround (mapThrough (points, (double) b, true)), ta, resultLength);
            const auto inLength = b - a, outLength = tb - ta;
            if (outLength <= 0 || inLength <= 0)
                continue;
            for (int c = 0; c < channels; ++c)
            {
                piece[(size_t) c].assign ((size_t) inLength, 0.0f);
                ptrs[(size_t) c] = piece[(size_t) c].data();
            }
            audio.read (start + a, (int) inLength, ptrs.data());
            auto put = [&] (Frame t, Frame from, float gain)
            {
                for (int c = 0; c < channels; ++c)
                    result[(size_t) c][(size_t) (ta + t)] += gain * piece[(size_t) c][(size_t) from];
            };
            const auto copied = std::min (inLength, outLength);
            for (Frame t = 0; t < copied; ++t)
            {
                float g = 1.0f;
                if (outLength < inLength || (! loop && outLength > inLength))
                    g = std::min (1.0f, (float) (copied - t) / (float) std::min (fade, copied));
                put (t, t, g);
            }
            if (loop && outLength > inLength)
            {
                // The second half of the piece again and again, each repeat crossfaded into the last.
                const auto span = std::clamp<Frame> (inLength / 2, std::min (inLength, (Frame) (0.01 * rate)), inLength);
                const auto cross = std::max<Frame> (1, std::min (span / 4, (Frame) (0.005 * rate)));
                for (Frame at = inLength - cross; at < outLength; at += span - cross)
                    for (Frame j = 0; j < span && at + j < outLength; ++j)
                    {
                        // The repeat fades in over the crossfade (the copy before it was not faded, so it fades out here).
                        const auto in = j < cross ? (float) (j + 1) / (float) (cross + 1) : 1.0f;
                        if (j < cross)
                            for (int c = 0; c < channels; ++c)
                                result[(size_t) c][(size_t) (ta + at + j)] *= 1.0f - in;
                        auto g = in;
                        if (outLength - (at + j) < fade)
                            g *= (float) (outLength - (at + j)) / (float) fade;
                        put (at + j, inLength - span + j, g);
                    }
            }
            if (progress != nullptr)
                progress->store ((float) tb / (float) std::max<Frame> (1, resultLength));
            if (cancel != nullptr && cancel->load())
            {
                error = "Cancelled.";
                return nullptr;
            }
        }
        return fromChannels (result, rate);
    }

    // Texture: 80 ms Hann grains at half-grain steps, each read from where its centre
    // maps back to in the source, scattered by up to a quarter grain so stretched
    // drones and noise do not buzz at the grain rate.
    AudioDataPtr textureRender (const AudioData& audio, Frame start, const std::vector<Anchor>& points, std::string& error,
                                std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        const auto channels = audio.numChannels();
        const auto rate = audio.sampleRate();
        const auto length = points.back().source, resultLength = points.back().target;
        const auto grain = std::max<Frame> (64, ((Frame) (0.08 * rate) / 2) * 2), hop = grain / 2;
        std::vector<float> window ((size_t) grain);
        for (Frame i = 0; i < grain; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) grain));
        std::vector<std::vector<float>> result ((size_t) channels, std::vector<float> ((size_t) resultLength, 0.0f));
        std::vector<std::vector<float>> buffer ((size_t) channels, std::vector<float> ((size_t) grain));
        std::vector<float*> ptrs;
        for (auto& c : buffer)
            ptrs.push_back (c.data());
        std::uint32_t random = 0x2545F491u;
        for (Frame centre = 0; centre - hop < resultLength; centre += hop)
        {
            random = random * 1664525u + 1013904223u;
            const auto scatter = ((double) (random >> 8) / (double) (1u << 24) - 0.5) * 0.5 * (double) grain;
            auto from = (Frame) std::llround (mapThrough (points, (double) centre, false) + scatter) - grain / 2;
            from = std::clamp<Frame> (from, 0, std::max<Frame> (0, length - grain));
            for (auto& c : buffer)
                std::fill (c.begin(), c.end(), 0.0f);
            audio.read (start + from, (int) std::min (grain, length - from), ptrs.data());
            for (Frame i = 0; i < grain; ++i)
            {
                const auto t = centre - grain / 2 + i;
                if (t < 0 || t >= resultLength)
                    continue;
                for (int c = 0; c < channels; ++c)
                    result[(size_t) c][(size_t) t] += window[(size_t) i] * buffer[(size_t) c][(size_t) i];
            }
            if ((centre / hop) % 64 == 0)
            {
                if (progress != nullptr)
                    progress->store ((float) centre / (float) std::max<Frame> (1, resultLength));
                if (cancel != nullptr && cancel->load())
                {
                    error = "Cancelled.";
                    return nullptr;
                }
            }
        }
        return fromChannels (result, rate);
    }
}

AudioDataPtr timestretch::SegmentCache::find (const AudioData& source, Frame from, Frame to, Frame length, Mode mode) const
{
    if (owner != &source)
        return nullptr;
    const std::array<long long, 4> key { from, to, length, (long long) mode };
    for (const auto& [k, piece] : pieces)
        if (k == key)
            return piece;
    return nullptr;
}

void timestretch::SegmentCache::add (const AudioData& source, Frame from, Frame to, Frame length, Mode mode, AudioDataPtr piece)
{
    if (owner != &source)
        clear();
    owner = &source;
    pieces.push_back ({ { from, to, length, (long long) mode }, std::move (piece) });
    if (pieces.size() > 4096)
        pieces.erase (pieces.begin(), pieces.begin() + 1024);
}

AudioDataPtr timestretch::render (const AudioData& audio, Frame start, Frame end, Frame resultLength, std::vector<Anchor> anchors,
                                  Mode mode, std::string& error, std::atomic<float>* progress, const std::atomic<bool>* cancel,
                                  SegmentCache* cache, const std::vector<Frame>& hits)
{
    start = std::clamp<Frame> (start, 0, audio.length());
    end = std::clamp<Frame> (end, start, audio.length());
    const auto length = end - start;
    if (length < 2 || resultLength < 2 || resultLength > length * 64)
    {
        error = "The range is too short, or the stretch is more than 64 times.";
        return nullptr;
    }
    const auto points = segments (std::move (anchors), length, resultLength);
    if (mode == Mode::varispeed)
        return varispeed (audio, start, points, error, progress, cancel);
    if (points.size() == 2 && resultLength == length)
    {
        AudioData::Builder copy (audio.numChannels(), audio.sampleRate());
        copy.appendShared (audio, start, end);
        return copy.finish();
    }
    if (mode == Mode::beats || mode == Mode::beatsNoLoop)
    {
        std::vector<Frame> inRange;
        for (const auto h : hits)
            if (h >= 0 && h < length)
                inRange.push_back (h);
        return beatsRender (audio, start, points, inRange, mode == Mode::beats, error, progress, cancel);
    }
    if (mode == Mode::texture)
        return textureRender (audio, start, points, error, progress, cancel);
    if (mode == Mode::ambient)
        return paulstretch::render (audio, start, end, resultLength, {}, error, progress, cancel,
                                    [&points] (double t) { return mapThrough (points, t, false); });
    if (points.size() == 2)
        return renderConstant (audio, start, end, resultLength, mode, error, progress, cancel);

    // Between anchors: each segment at its own ratio, so every anchor lands on its frame
    // (Rubber Band's key frame map lands them only within about 20 ms). Each segment is
    // rendered with a little audio either side so the stretcher has settled, then the
    // segments are joined with a 5 ms crossfade centred on each anchor.
    const auto channels = audio.numChannels();
    const auto rate = audio.sampleRate();
    const auto context = (Frame) (0.05 * rate);
    const auto fade = std::max<Frame> (2, (Frame) (0.005 * rate));
    std::vector<std::vector<float>> result ((size_t) channels, std::vector<float> ((size_t) resultLength, 0.0f));
    std::vector<float> weight ((size_t) resultLength, 0.0f);
    std::vector<std::vector<float>> piece ((size_t) channels);
    std::vector<float*> piecePtrs ((size_t) channels);
    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto from = points[i - 1], to = points[i];
        const bool same = to.target - from.target == to.source - from.source;
        const auto ratio = (double) (to.target - from.target) / (double) (to.source - from.source);
        const auto a = std::max<Frame> (0, from.source - context), b = std::min (length, to.source + context);
        const auto pieceLength = same ? b - a : std::max<Frame> (2, (Frame) std::llround ((double) (b - a) * ratio));
        AudioDataPtr rendered = cache != nullptr && ! same ? cache->find (audio, start + a, start + b, pieceLength, mode) : nullptr;
        if (same)
        {
            AudioData::Builder copy (channels, rate);
            copy.appendShared (audio, start + a, start + b);
            rendered = copy.finish();
        }
        else if (rendered == nullptr)
        {
            std::atomic<float> part { 0.0f };
            rendered = renderConstant (audio, start + a, start + b, pieceLength, mode, error, &part, cancel);
            if (rendered == nullptr)
                return nullptr;
            if (cache != nullptr)
                cache->add (audio, start + a, start + b, pieceLength, mode, rendered);
        }
        for (int c = 0; c < channels; ++c)
        {
            piece[(size_t) c].assign ((size_t) pieceLength, 0.0f);
            piecePtrs[(size_t) c] = piece[(size_t) c].data();
        }
        rendered->read (0, (int) pieceLength, piecePtrs.data());

        // Where the segment's start sits in the piece, and the frames it covers in the result.
        const auto offset = (Frame) std::llround ((double) (from.source - a) * ratio) - from.target;
        const auto first = i == 1 ? 0 : from.target - fade / 2;
        const auto last = i + 1 == points.size() ? resultLength : to.target + fade / 2;
        for (auto t = std::max<Frame> (0, first); t < std::min (resultLength, last); ++t)
        {
            const auto j = t + offset;
            if (j < 0 || j >= pieceLength)
                continue;
            float w = 1.0f;
            if (i > 1 && t < from.target + fade / 2)
                w = (float) (t - first) / (float) fade;
            if (i + 1 < points.size() && t > to.target - fade / 2)
                w = std::min (w, (float) (last - t) / (float) fade);
            for (int c = 0; c < channels; ++c)
                result[(size_t) c][(size_t) t] += w * piece[(size_t) c][(size_t) j];
            weight[(size_t) t] += w;
        }
        if (progress != nullptr)
            progress->store ((float) to.target / (float) resultLength);
        if (cancelled (cancel))
        {
            error = "Cancelled.";
            return nullptr;
        }
    }
    for (Frame t = 0; t < resultLength; ++t)
        if (weight[(size_t) t] > 0.0f && std::abs (weight[(size_t) t] - 1.0f) > 1e-6f)
            for (auto& c : result)
                c[(size_t) t] /= weight[(size_t) t];

    AudioData::Builder out (channels, rate);
    std::vector<float*> ptrs;
    for (auto& c : result)
        ptrs.push_back (c.data());
    for (Frame done = 0; done < resultLength; done += 65536)
    {
        const auto n = (int) std::min<Frame> (65536, resultLength - done);
        std::vector<const float*> at;
        for (auto* p : ptrs)
            at.push_back (p + done);
        out.append (at.data(), n);
    }
    if (progress != nullptr)
        progress->store (1.0f);
    return out.finish();
}

AudioDataPtr timestretch::stretch (const AudioData& audio, Frame start, Frame end, double ratio, Mode mode, std::string& error,
                                   std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    const auto length = std::max<Frame> (0, std::min (end, audio.length()) - std::max<Frame> (0, start));
    return render (audio, start, end, (Frame) std::llround ((double) length * ratio), {}, mode, error, progress, cancel);
}
