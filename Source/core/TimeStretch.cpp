// SPDX-License-Identifier: AGPL-3.0-only
#include "TimeStretch.h"

#include "Dsp.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>

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

BeatGrid beatgrid::align (const AudioData& audio, BeatGrid grid, bool chooseBar1)
{
    if (! grid.isSet() || audio.length() == 0)
        return grid;
    const auto rate = audio.sampleRate();
    const auto hop = std::max<Frame> (1, (Frame) (0.001 * rate));   // 1 ms
    const auto hops = (size_t) (audio.length() / hop);
    if (hops < 16)
        return grid;

    // Per millisecond: the level (in dB) of the mono mix and of its low end (one-pole low-pass at 150 Hz).
    std::vector<float> level (hops), low (hops);
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
    // The rise in level over 3 ms: sharp at a hit.
    auto rise = [] (const std::vector<float>& v, size_t h) { return h >= 3 ? std::max (0.0f, v[h] - v[h - 3]) : 0.0f; };

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
    }
    return "";
}

std::vector<timestretch::Mode> timestretch::modes()
{
    return { Mode::fullMix, Mode::fullMixVoice, Mode::drums, Mode::melody, Mode::varispeed };
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
        case Mode::fullMix:
        case Mode::varispeed:    break;
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

AudioDataPtr timestretch::render (const AudioData& audio, Frame start, Frame end, Frame resultLength, std::vector<Anchor> anchors,
                                  Mode mode, std::string& error, std::atomic<float>* progress, const std::atomic<bool>* cancel)
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
        const auto ratio = (double) (to.target - from.target) / (double) (to.source - from.source);
        const auto a = std::max<Frame> (0, from.source - context), b = std::min (length, to.source + context);
        const auto pieceLength = std::max<Frame> (2, (Frame) std::llround ((double) (b - a) * ratio));
        std::atomic<float> part { 0.0f };
        const auto rendered = renderConstant (audio, start + a, start + b, pieceLength, mode, error, &part, cancel);
        if (rendered == nullptr)
            return nullptr;
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
