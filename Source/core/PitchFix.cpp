// SPDX-License-Identifier: AGPL-3.0-only
#include "PitchFix.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>

namespace pitchfix
{
    double nearestNoteHz (double hz)
    {
        if (hz <= 0.0)
            return 0.0;
        const auto semitones = std::round (12.0 * std::log2 (hz / 440.0));
        return 440.0 * std::pow (2.0, semitones / 12.0);
    }

    double cents (double fromHz, double toHz)
    {
        return fromHz > 0.0 && toHz > 0.0 ? 1200.0 * std::log2 (toHz / fromHz) : 0.0;
    }

    double centreHz (const pitch::Track& track, Frame start, Frame end)
    {
        std::vector<float> voiced;
        for (const auto& p : track.points)
            if (p.at >= start && p.at < end && p.hz > 0.0f)
                voiced.push_back (p.hz);
        if (voiced.size() < 3)
            return 0.0;
        std::nth_element (voiced.begin(), voiced.begin() + (long) voiced.size() / 2, voiced.end());
        return voiced[voiced.size() / 2];
    }

    namespace
    {
        constexpr double easeSeconds = 0.03;     // the shift eases in and out over this, inside the selection
        constexpr double farCents = 300.0;       // points further than this from the note are another note: not flattened
    }

    std::vector<double> shiftCurve (const pitch::Track& track, Frame start, Frame end, const Settings& s)
    {
        std::vector<double> curve;
        const auto centre = centreHz (track, start, end);
        if (centre <= 0.0 || track.hop <= 0)
            return curve;

        const auto target = s.targetHz > 0.0 ? s.targetHz : nearestNoteHz (centre);
        const auto base = std::clamp (s.amount, 0.0, 1.0) * cents (centre, target);
        const auto flatten = std::clamp (s.flatten, 0.0, 1.0);

        for (const auto& p : track.points)
        {
            if (p.at < start || p.at >= end)
                continue;
            auto shift = base;
            if (p.hz > 0.0f)
                if (const auto off = cents (centre, p.hz); std::abs (off) < farCents)
                    shift -= flatten * off;
            curve.push_back (shift);
        }

        // Smooth over about 25 ms, so the shift glides rather than steps.
        const auto radius = std::max (1, (int) std::lround (0.0125 * track.sampleRate / track.hop));
        std::vector<double> smooth (curve.size());
        for (size_t i = 0; i < curve.size(); ++i)
        {
            double sum = 0.0;
            int count = 0;
            for (auto j = (long) i - radius; j <= (long) i + radius; ++j)
                if (j >= 0 && j < (long) curve.size())
                {
                    sum += curve[(size_t) j];
                    ++count;
                }
            smooth[i] = sum / count;
        }

        // Ease in and out at the edges.
        const auto ease = std::max (1.0, std::min (easeSeconds * track.sampleRate / track.hop, smooth.size() / 4.0));
        for (size_t i = 0; i < smooth.size(); ++i)
        {
            const auto edge = std::min ((double) i, (double) (smooth.size() - 1 - i));
            if (edge < ease)
                smooth[i] *= 0.5 - 0.5 * std::cos (M_PI * edge / ease);
        }
        return smooth;
    }

    AudioDataPtr correct (const AudioData& data, Frame start, Frame end, const pitch::Track& track, const Settings& s,
                          std::string& error, std::atomic<float>* progress)
    {
        start = std::clamp<Frame> (start, 0, data.length());
        end = std::clamp<Frame> (end, start, data.length());
        const auto curve = shiftCurve (track, start, end, s);
        if (curve.empty())
        {
            error = "No steady pitch was found in the selection.";
            return {};
        }

        const auto channels = data.numChannels();
        const auto rate = data.sampleRate();
        const auto hop = track.hop;
        Frame firstPoint = end;
        for (const auto& p : track.points)
            if (p.at >= start)
            {
                firstPoint = p.at;
                break;
            }

        // The shift at a frame: the curve between points, 0 outside the selection.
        const auto centsAt = [&] (Frame f)
        {
            if (f < start || f >= end)
                return 0.0;
            const auto x = (double) (f - firstPoint) / hop;
            const auto i = (size_t) std::max (0.0, x);
            if (x <= 0.0 || i + 1 >= curve.size())
                return 0.0;   // the curve eases to 0 at both ends
            const auto t = x - (double) i;
            return curve[i] * (1.0 - t) + curve[i + 1] * t;
        };

        // A little audio around the selection, so Rubber Band settles before it.
        const auto context = (Frame) (0.1 * rate);
        const auto from = std::max<Frame> (0, start - context), to = std::min (data.length(), end + context);
        const auto total = to - from;

        using RB = RubberBand::RubberBandStretcher;
        RB::Options options = RB::OptionProcessRealTime | RB::OptionEngineFiner | RB::OptionPitchHighConsistency
                            | RB::OptionChannelsTogether | (s.keepFormants ? RB::OptionFormantPreserved : RB::OptionFormantShifted);
        RB stretcher ((size_t) rate, (size_t) channels, options);
        constexpr int block = 256;
        stretcher.setMaxProcessSize (block);
        // A new pitch scale reaches the audio Rubber Band is working on, which trails the
        // input fed by about its start padding and delay (measured: 3,500 frames at 48 kHz,
        // 7,500 at 96 kHz). So each block is given the shift for that earlier audio.
        const auto lead = (Frame) (stretcher.getPreferredStartPad() + stretcher.getStartDelay()) - (Frame) (0.0073 * rate);

        std::vector<std::vector<float>> in ((size_t) channels, std::vector<float> (block, 0.0f));
        std::vector<std::vector<float>> out ((size_t) channels);
        for (auto& c : out)
            c.reserve ((size_t) total);
        std::vector<float*> inPtrs, scratchPtrs;
        for (auto& c : in)
            inPtrs.push_back (c.data());
        std::vector<std::vector<float>> scratch ((size_t) channels, std::vector<float> (4096));
        for (auto& c : scratch)
            scratchPtrs.push_back (c.data());

        auto skip = (Frame) stretcher.getStartDelay();
        const auto collect = [&]
        {
            for (int available = stretcher.available(); available > 0; available = stretcher.available())
            {
                const auto got = (int) stretcher.retrieve (scratchPtrs.data(), (size_t) std::min (available, 4096));
                for (int i = 0; i < got; ++i)
                {
                    if (skip > 0)
                    {
                        --skip;
                        continue;
                    }
                    if ((Frame) out[0].size() >= total)
                        continue;
                    for (int ch = 0; ch < channels; ++ch)
                        out[(size_t) ch].push_back (scratch[(size_t) ch][(size_t) i]);
                }
            }
        };

        for (auto pad = (Frame) stretcher.getPreferredStartPad(); pad > 0; pad -= block)
        {
            const auto n = (int) std::min<Frame> (block, pad);
            for (auto& c : in)
                std::fill (c.begin(), c.end(), 0.0f);
            stretcher.process (inPtrs.data(), (size_t) n, false);
            collect();
        }
        for (auto at = from; at < to; at += block)
        {
            const auto n = (int) std::min<Frame> (block, to - at);
            data.read (at, n, inPtrs.data());
            stretcher.setPitchScale (std::pow (2.0, centsAt (at + n / 2 - lead) / 1200.0));
            stretcher.process (inPtrs.data(), (size_t) n, at + n >= to);
            collect();
            if (progress != nullptr)
                progress->store ((float) (at - from) / (float) total);
        }
        collect();

        // Back into the selection, crossfading 5 ms to the original at each edge.
        const auto length = end - start;
        std::vector<std::vector<float>> result ((size_t) channels, std::vector<float> ((size_t) length));
        std::vector<std::vector<float>> original ((size_t) channels, std::vector<float> ((size_t) length));
        std::vector<float*> originalPtrs;
        for (auto& c : original)
            originalPtrs.push_back (c.data());
        for (Frame done = 0; done < length;)
        {
            const auto n = (int) std::min<Frame> (65536, length - done);
            std::vector<float*> at;
            for (auto* p : originalPtrs)
                at.push_back (p + done);
            data.read (start + done, n, at.data());
            done += n;
        }
        const auto fade = std::min<Frame> ((Frame) (0.005 * rate), length / 2);
        for (int ch = 0; ch < channels; ++ch)
            for (Frame i = 0; i < length; ++i)
            {
                const auto j = (size_t) (start - from + i);
                const auto shifted = j < out[(size_t) ch].size() ? out[(size_t) ch][j] : 0.0f;
                const auto edge = std::min (i, length - 1 - i);
                const auto mix = fade > 0 && edge < fade ? (float) edge / (float) fade : 1.0f;
                result[(size_t) ch][(size_t) i] = shifted * mix + original[(size_t) ch][(size_t) i] * (1.0f - mix);
            }

        AudioData::Builder builder (channels, rate);
        std::vector<const float*> ptrs;
        for (const auto& c : result)
            ptrs.push_back (c.data());
        for (Frame done = 0; done < length;)
        {
            const auto n = (int) std::min<Frame> (65536, length - done);
            std::vector<const float*> at;
            for (auto* p : ptrs)
                at.push_back (p + done);
            builder.append (at.data(), n);
            done += n;
        }
        if (progress != nullptr)
            progress->store (1.0f);
        return builder.finish();
    }
}
