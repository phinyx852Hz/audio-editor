// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiRepeat.h"
#include "Phi.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace phi
{
    namespace
    {
        constexpr double pi = 3.141592653589793;

        // Audio EQ Cookbook high- and low-pass, Q = 1/√2.
        template <typename Band>
        Band pass (double hz, double rate, bool high)
        {
            const auto w = 2.0 * pi * std::clamp (hz, 10.0, rate * 0.45) / rate;
            const auto alpha = std::sin (w) / (2.0 * 0.7071067811865476), c = std::cos (w);
            const auto a0 = 1.0 + alpha;
            Band b;
            if (high)
            {
                b.b0 = (1.0 + c) / 2.0 / a0; b.b1 = -(1.0 + c) / a0; b.b2 = b.b0;
            }
            else
            {
                b.b0 = (1.0 - c) / 2.0 / a0; b.b1 = (1.0 - c) / a0; b.b2 = b.b0;
            }
            b.a1 = -2.0 * c / a0;
            b.a2 = (1.0 - alpha) / a0;
            return b;
        }

        template <typename Band, typename State>
        float run (const Band& c, State& s, float x)
        {
            const auto y = c.b0 * x + s.z1;
            s.z1 = c.b1 * x - c.a1 * y + s.z2;
            s.z2 = c.b2 * x - c.a2 * y;
            return (float) y;
        }
    }

    double Repeat::gridLength (int grid, bool triplets)
    {
        const auto g = gridBeats[(size_t) std::clamp (grid, 0, numGrids - 1)];
        return triplets ? g * 2.0 / 3.0 : g;
    }

    void Repeat::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        channels = std::max (1, numChannels);
        ringFrames = (int) std::ceil (maxSliceSeconds * rate) + 4;
        ring.assign ((size_t) (ringFrames * channels), 0.0f);
        hpState.assign ((size_t) channels, {});
        lpState.assign ((size_t) channels, {});
        fadeStep = (float) (1.0 / (0.003 * rate));        // 3 ms in and out
        edgeSamples = std::max (8, (int) (0.0015 * rate)); // 1.5 ms at a repetition's edges
        set (settings);
        reset();
    }

    void Repeat::set (const Settings& s)
    {
        const bool filterChanged = s.filterHz != settings.filterHz || s.filterOctaves != settings.filterOctaves;
        settings = s;
        settings.bpm = std::clamp (s.bpm, 20.0, 400.0);
        settings.interval = std::clamp (s.interval, 0, numIntervals - 1);
        settings.grid = std::clamp (s.grid, 0, numGrids - 1);
        settings.offsetSixteenths = std::clamp (s.offsetSixteenths, 0.0, 15.0);
        settings.gateSixteenths = std::clamp (s.gateSixteenths, 1.0, 64.0);
        settings.chance = std::clamp (s.chance, 0.0, 1.0);
        settings.variation = std::clamp (s.variation, 0.0, 1.0);
        settings.pitch = std::clamp (s.pitch, minPitch, 0.0);
        settings.pitchDecay = std::clamp (s.pitchDecay, 0.0, 1.0);
        settings.decay = std::clamp (s.decay, 0.0, 1.0);
        settings.filterHz = std::clamp (s.filterHz, 20.0, 20000.0);
        settings.filterOctaves = std::clamp (s.filterOctaves, 0.1, 9.0);
        if (filterChanged || highPass.b0 == 1.0)
            updateFilter();
    }

    void Repeat::updateFilter()
    {
        const auto half = std::pow (2.0, settings.filterOctaves / 2.0);
        highPass = pass<Band> (settings.filterHz / half, rate, true);
        lowPass = pass<Band> (settings.filterHz * half, rate, false);
    }

    void Repeat::reset()
    {
        beats = 0.0;
        sixteenth = 0;
        lastInterval = std::numeric_limits<std::int64_t>::min();
        lastIntervalBeats = lastOffset = -1.0;
        lastVariationStep = -1;
        triggers = 0;
        active = false;
        wetGain = 0.0f;
        dryGain = settings.mix == Mix::gate ? 0.0f : 1.0f;
        write = 0;
        std::fill (ring.begin(), ring.end(), 0.0f);
        for (auto& s : hpState) s = {};
        for (auto& s : lpState) s = {};
    }

    void Repeat::setPosition (double b)
    {
        beats = b;
    }

    double Repeat::random01()
    {
        seed ^= seed >> 12;
        seed ^= seed << 25;
        seed ^= seed >> 27;
        return (double) ((seed * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
    }

    double Repeat::pickGrid()
    {
        // Variation halves the grid up to four times, at random.
        const auto base = gridLength (settings.grid, settings.triplets);
        const auto most = (int) std::lround (settings.variation * 4.0);
        if (most <= 0)
            return base;
        const auto halvings = std::min (most, (int) (random01() * (most + 1)));
        return base / (double) (1 << halvings);
    }

    void Repeat::start()
    {
        active = true;
        write = 0;   // the slice is caught from here on into the ring
        gateStart = beats;
        gateEnd = beats + settings.gateSixteenths * 0.25;
        repetition = 0;
        repStart = beats;
        repLength = pickGrid();
        readSpeed = std::pow (2.0, settings.pitch / 12.0);
        readPosition = 0.0;
        lastVariationStep = -1;
        for (auto& s : hpState) s = {};
        for (auto& s : lpState) s = {};
    }

    void Repeat::nextRepetition()
    {
        repStart += repLength;
        ++repetition;
        const auto mode = settings.variationMode;
        if (mode == Variation::everyRepeat)
            repLength = pickGrid();
        else if (mode != Variation::trigger)
        {
            const auto every = mode == Variation::quarter ? 1.0 : mode == Variation::eighth ? 0.5 : 0.25;
            const auto step = (std::int64_t) std::floor (repStart / every);
            if (step != lastVariationStep)
            {
                lastVariationStep = step;
                repLength = pickGrid();
            }
        }
        const auto semitones = std::max (minPitch, settings.pitch - settings.pitchDecay * 3.0 * repetition);
        readSpeed = std::pow (2.0, semitones / 12.0);
        readPosition = 0.0;
    }

    float Repeat::slice (int channel, double offset) const
    {
        // The slice as far as it has been caught (`write` frames), read between samples.
        const auto last = std::max (0, std::min (write, ringFrames) - 1);
        const auto p = std::clamp (offset, 0.0, (double) last);
        const auto i = (int) p;
        const auto j = std::min (i + 1, last);
        const auto f = (float) (p - i);
        const auto* c = ring.data() + (size_t) channel * (size_t) ringFrames;
        return c[i] + f * (c[j] - c[i]);
    }

    void Repeat::process (float* const* io, int numChannels, int numFrames)
    {
        const auto n = std::min (numChannels, channels);
        if (n <= 0)
            return;
        const auto beatsPerSample = settings.bpm / 60.0 / rate;
        const auto samplesPerBeat = rate * 60.0 / settings.bpm;
        const auto interval = intervalBeats[(size_t) settings.interval];
        const auto offset = std::fmod (settings.offsetSixteenths * 0.25, interval);
        const auto volume = (float) std::pow (10.0, settings.volumeDb / 20.0);
        const auto gateBeats = settings.gateSixteenths * 0.25;
        const auto mode = settings.mix;
        // The slice is one grid (before variation) long, as long as the ring holds.
        const auto sliceFrames = std::min (ringFrames, (int) std::ceil (gridLength (settings.grid, settings.triplets) * samplesPerBeat));

        for (int i = 0; i < numFrames; ++i)
        {
            // Half a sample early, so rounding never puts a boundary a sample late.
            const auto now = beats + beatsPerSample * 0.5;
            sixteenth = (std::int64_t) std::floor (now * 4.0);

            // At each interval point the dice (or the golden rhythm) decide. After a reset, or a
            // new interval or offset, counting starts again: only a point landed on exactly counts.
            const auto at = (std::int64_t) std::floor ((now - offset) / interval);
            bool point = false;
            if (interval != lastIntervalBeats || offset != lastOffset)
            {
                lastIntervalBeats = interval;
                lastOffset = offset;
                point = std::fmod (now - offset + interval * 64.0, interval) < beatsPerSample;
            }
            else
                point = at != lastInterval;
            lastInterval = at;
            if (point)
            {
                const auto step = triggers++;
                const bool go = settings.golden ? (settings.chance > 0.0 && hit (step, settings.chance))
                                                : random01() < settings.chance;
                if (go && ! settings.repeat)
                    start();
            }

            if (settings.repeat && ! active)
                start();
            if (active && ! settings.repeat && now >= gateEnd)
                active = false;
            if (active && settings.repeat)
                gateEnd = beats + gateBeats;   // held: it goes on
            if (active || wetGain > 0.0f)
                while (now >= repStart + repLength)
                    nextRepetition();

            // Catch the slice.
            if (active && write < sliceFrames)
            {
                for (int ch = 0; ch < n; ++ch)
                    ring[(size_t) ch * (size_t) ringFrames + (size_t) write] = io[ch][i];
                for (int ch = n; ch < channels; ++ch)
                    ring[(size_t) ch * (size_t) ringFrames + (size_t) write] = io[n - 1][i];
                ++write;
            }

            const auto wetTarget = active ? 1.0f : 0.0f;
            const auto dryTarget = mode == Mix::gate ? 0.0f : mode == Mix::insert && active ? 0.0f : 1.0f;
            wetGain = wetGain < wetTarget ? std::min (wetTarget, wetGain + fadeStep) : std::max (wetTarget, wetGain - fadeStep);
            dryGain = dryGain < dryTarget ? std::min (dryTarget, dryGain + fadeStep) : std::max (dryTarget, dryGain - fadeStep);

            if (wetGain > 0.0f)
            {
                // Short fades at each repetition's edges (not the first's start, which is the live sound).
                const auto length = repLength * samplesPerBeat;
                const auto into = (beats - repStart) * samplesPerBeat;
                auto edge = (float) std::min (1.0, (length - into) / edgeSamples);
                if (repetition > 0)
                    edge = std::min (edge, (float) (into / edgeSamples));
                edge = std::clamp (edge, 0.0f, 1.0f);
                const auto progress = std::clamp ((beats - gateStart) / gateBeats, 0.0, 1.0);
                const auto level = wetGain * edge * volume * (float) (1.0 - settings.decay * progress);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto c = std::min (ch, channels - 1);
                    auto wet = slice (c, readPosition);
                    if (settings.filter)
                        wet = run (lowPass, lpState[(size_t) c], run (highPass, hpState[(size_t) c], wet));
                    io[ch][i] = io[ch][i] * dryGain + wet * level;
                }
                readPosition += readSpeed;
            }
            else
                for (int ch = 0; ch < numChannels; ++ch)
                    io[ch][i] *= dryGain;

            beats += beatsPerSample;
        }
    }
}
