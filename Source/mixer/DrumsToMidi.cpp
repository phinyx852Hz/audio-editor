// SPDX-License-Identifier: AGPL-3.0-only
#include "DrumsToMidi.h"

#include "../core/AudioData.h"
#include "../core/Fft.h"
#include "../core/TimeStretch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace mixer
{
    std::vector<DrumHit> findDrums (const Clip& clip, double sampleRate, const DrumsSettings& settings)
    {
        std::vector<DrumHit> out;
        if (clip.audio == nullptr || clip.length <= 0 || sampleRate <= 0.0)
            return out;
        // The clip as it plays, mixed to mono.
        const auto& a = *clip.audio;
        const auto last = a.length() - 1;
        std::vector<float> mono ((size_t) clip.length);
        for (Frame f = 0; f < clip.length; ++f)
        {
            const auto src = (size_t) std::clamp<Frame> (clip.reversed ? last - (clip.offset + f) : clip.offset + f, 0, std::max<Frame> (0, last));
            mono[(size_t) f] = a.left.empty() ? 0.0f : 0.5f * (a.left[src] + a.right[src]);
        }
        // Where hits start: in the whole sound, and in its top alone, so a hat under a kick's
        // ringing is found too. A little silence before, so a hit on the first frame rises.
        const auto lead = (Frame) (0.05 * sampleRate);
        auto onsets = [&] (const std::vector<float>& x)
        {
            std::vector<float> padded ((size_t) lead, 0.0f);
            padded.insert (padded.end(), x.begin(), x.end());
            AudioData::Builder b (1, sampleRate);
            const float* channels[1] = { padded.data() };
            b.append (channels, (int) padded.size());
            auto found = timestretch::transients (*b.finish(), settings.sensitivity, settings.minGapMs);
            for (auto& f : found)
                f = std::max<Frame> (0, f - lead);
            return found;
        };
        std::vector<float> top (mono.size());
        {
            const auto c = (float) std::exp (-2.0 * 3.14159265 * 5000.0 / sampleRate);   // a one-pole high pass at 5 kHz
            float x1 = 0.0f, y1 = 0.0f;
            for (size_t k = 0; k < mono.size(); ++k)
            {
                y1 = c * (y1 + mono[k] - x1);
                x1 = mono[k];
                top[k] = y1;
            }
        }
        auto hits = onsets (mono);
        const auto same = (Frame) (0.015 * sampleRate);
        for (const auto f : onsets (top))
            if (std::none_of (hits.begin(), hits.end(), [&] (Frame h) { return std::abs (h - f) < same; }))
                hits.push_back (f);
        std::sort (hits.begin(), hits.end());

        // Each hit by what it adds: its first 43 ms (at 48 kHz) against the 43 ms before it,
        // through a Hann window, in three bands.
        constexpr int order = 11, size = 1 << order;
        const Fft fft (order);
        std::vector<std::complex<float>> buffer ((size_t) size);
        const auto binHz = sampleRate / size;
        auto bands = [&] (Frame from, std::array<double, 3>& e)
        {
            for (int k = 0; k < size; ++k)
            {
                const auto f = from + k;
                const auto v = f >= 0 && f < clip.length ? mono[(size_t) f] : 0.0f;
                const auto w = 0.5f - 0.5f * std::cos (2.0f * 3.14159265f * (float) k / (float) (size - 1));
                buffer[(size_t) k] = { v * w, 0.0f };
            }
            fft.forward (buffer);
            e = { 0.0, 0.0, 0.0 };
            for (int k = 1; k < size / 2; ++k)
            {
                const auto hz = k * binHz;
                if (hz < 30.0)
                    continue;
                e[hz < 150.0 ? 0 : hz < 6000.0 ? 1 : 2] += (double) std::norm (buffer[(size_t) k]);
            }
        };
        for (const auto at : hits)
        {
            std::array<double, 3> after {}, before {};
            bands (at, after);
            bands (at - size, before);
            std::array<double, 3> added {};
            for (size_t b = 0; b < 3; ++b)
                added[b] = std::max (0.0, after[b] - before[b]);
            if (added[0] + added[1] + added[2] < 0.05 * (after[0] + after[1] + after[2]))
                added = after;   // nothing new stands out: the hit as it sounds
            const auto total = added[0] + added[1] + added[2];
            if (total <= 1.0e-12)
                continue;
            float peak = 0.0f;
            for (Frame f = at; f < std::min (clip.length, at + (Frame) (0.02 * sampleRate)); ++f)
                peak = std::max (peak, std::abs (mono[(size_t) f]));
            DrumHit hit;
            hit.at = at;
            // Mostly below 150 Hz a kick; mostly above 6 kHz a hat; the crack between a snare.
            hit.note = added[0] / total >= 0.45 ? gm::kick : added[2] / total >= 0.55 ? gm::hat : gm::snare;
            // -40 dBFS a soft 20, 0 dBFS a full 127.
            const auto db = 20.0 * std::log10 (std::max (1.0e-6f, peak));
            hit.velocity = (int) std::lround (std::clamp (20.0 + (db + 40.0) / 40.0 * 107.0, 1.0, 127.0));
            out.push_back (hit);
        }
        return out;
    }

    std::shared_ptr<MidiClip> drumsToMidi (const Clip& clip, double sampleRate, double bpm, const DrumsSettings& settings)
    {
        const auto hits = findDrums (clip, sampleRate, settings);
        auto midi = std::make_shared<MidiClip>();
        midi->bpm = bpm;
        midi->length = clip.length;
        const auto sixteenth = (Frame) std::llround (60.0 / std::clamp (bpm, 20.0, 999.0) * sampleRate / 4.0);
        for (size_t i = 0; i < hits.size(); ++i)
        {
            const auto& h = hits[i];
            auto end = std::min (h.at + sixteenth, clip.length - 1);
            for (auto j = i + 1; j < hits.size(); ++j)
                if (hits[j].note == h.note)
                {
                    end = std::min (end, hits[j].at - 1);
                    break;
                }
            if (end <= h.at)
                continue;
            midi->events.push_back ({ h.at, 0x99, (uint8_t) h.note, (uint8_t) h.velocity });   // channel 10: the kit
            midi->events.push_back ({ end, 0x89, (uint8_t) h.note, 0 });
        }
        std::stable_sort (midi->events.begin(), midi->events.end(), [] (const MidiClip::Placed& x, const MidiClip::Placed& y)
        {
            return x.frame < y.frame || (x.frame == y.frame && (x.status & 0xF0) == 0x80 && (y.status & 0xF0) != 0x80);   // a note off first
        });
        return midi;
    }
}
