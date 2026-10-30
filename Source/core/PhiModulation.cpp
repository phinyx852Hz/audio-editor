// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiModulation.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace phi
{
    namespace
    {
        constexpr double pi = 3.141592653589793;
        constexpr double twoPi = 6.283185307179586;
        constexpr double goldenTurn = 1.0 - inverse;   // the golden angle as a share of a turn

        double frac (double v) { return v - std::floor (v); }
        double clamp01 (double v) { return std::clamp (v, 0.0, 1.0); }

        // n × step mod 1, worked out in parts so it stays exact for large n.
        double turns (std::int64_t n, double step)
        {
            return frac (std::fmod ((double) (n % 1000000) * step, 1.0)
                         + std::fmod ((double) (n / 1000000) * std::fmod (1000000.0 * step, 1.0), 1.0));
        }

        // What each stream adds to its step: nothing for the first, then quadratic irrationals
        // like φ (√7, √13...), the same as phi::deal's colours, so no two streams move together.
        double extra (int stream)
        {
            static const double roots[Flow::numStreams] { 0.0, std::sqrt (7.0), std::sqrt (13.0), std::sqrt (19.0),
                                                          std::sqrt (29.0), std::sqrt (41.0), std::sqrt (89.0), std::sqrt (73.0) };
            return frac (roots[((stream % Flow::numStreams) + Flow::numStreams) % Flow::numStreams]);
        }

        // Noise that is the same every time: event n of a stream, from a seed. -0.5 to 0.5.
        double noise (std::int64_t n, int stream, double seed)
        {
            auto x = (std::uint64_t) n * 0x9e3779b97f4a7c15ull ^ ((std::uint64_t) (stream + 1) << 56)
                   ^ (std::uint64_t) std::llround (clamp01 (seed) * 1.0e9);
            x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
            x ^= x >> 27; x *= 0x94d049bb133111ebull;
            x ^= x >> 31;
            return (double) (x >> 11) / 9007199254740992.0 - 0.5;
        }

        // Left (-1) to right (1) for the nth of a fan, each a golden angle further round.
        double fan (std::int64_t n, double width)
        {
            return clamp01 (width) * (2.0 * frac (0.5 + goldenTurn * (double) (n % 1000000)) - 1.0);
        }

        void panGains (double pan, float& left, float& right)
        {
            const auto a = (std::clamp (pan, -1.0, 1.0) + 1.0) * pi / 4.0;
            left = (float) std::cos (a);
            right = (float) std::sin (a);
        }
    }

    //==========================================================================
    double Flow::ratioOf (const Settings& s)
    {
        const auto r = std::clamp (s.ratio, 1.05, 4.0);
        return s.mode == Mode::periodic ? std::max (1.25, std::round (r * 4.0) / 4.0) : r;
    }

    double Flow::stepOf (const Settings& s, int stream)
    {
        const auto step = frac (1.0 / ratioOf (s) + extra (stream));
        if (s.mode == Mode::periodic)
            return std::clamp (std::round (step * 8.0), 1.0, 7.0) / 8.0;
        return step;
    }

    double Flow::outputRate (const Settings& s, int output)
    {
        return std::max (0.0, s.rateHz) * std::pow (ratioOf (s), (double) std::clamp (output, 0, numOutputs - 1));
    }

    std::int64_t Flow::moves (const Settings& s, int stream, std::int64_t n)
    {
        if (s.mode != Mode::fibonacci)
            return n;
        if (n < 0)
            return 0;
        // The hits of the Sturmian word with density `step` (phi::hit) on steps 0..n: with
        // 1/φ, the Fibonacci word, so a value is held for one event or two, never three.
        const auto d = stepOf (s, stream);
        return (std::int64_t) std::ceil ((double) (n + 1) * d);
    }

    double Flow::event (const Settings& s, int stream, std::int64_t n)
    {
        auto v = frac (golden (stream, clamp01 (s.seed)) + turns (moves (s, stream, n), stepOf (s, stream)));
        if (s.mode == Mode::hybrid)
            v = frac (v + clamp01 (s.jitter) * noise (n, stream, s.seed));
        return std::clamp (v, 0.0, 0.9999999);
    }

    double Flow::at (const Settings& s, int stream, double position)
    {
        const auto n = (std::int64_t) std::floor (position);
        const auto f = position - (double) n;
        const auto a = event (s, stream, n);
        if (s.shape == Shape::stepped)
            return a;
        const auto b = event (s, stream, n + 1);
        if (s.shape == Shape::smooth)
            return a + (b - a) * 0.5 * (1.0 - std::cos (pi * f));
        // Wave: the phase itself, turned the short way from one event to the next.
        const auto d = frac (b - a + 0.5) - 0.5;
        return 0.5 - 0.5 * std::cos (twoPi * (a + d * f));
    }

    void Flow::prepare (double sampleRate)
    {
        rate = std::max (1.0, sampleRate);
        reset();
    }

    void Flow::set (const Settings& s) { settings = s; }

    void Flow::reset()
    {
        positions.fill (0.0);
        values.fill (0.0f);
        started = false;
    }

    void Flow::advance (int numFrames)
    {
        const auto coef = (float) std::exp (-std::max (1, numFrames) / std::max (1.0, settings.smoothMs * 0.001 * rate));
        for (int k = 0; k < numOutputs; ++k)
        {
            auto& v = values[(size_t) k];
            const auto target = (float) at (settings, k, positions[(size_t) k]);
            v = started ? target + (v - target) * coef : target;
            // Kept below 2^40 events so the fraction stays fine; that is centuries at any rate.
            positions[(size_t) k] = std::fmod (positions[(size_t) k] + numFrames / rate * outputRate (settings, k), 1099511627776.0);
        }
        started = true;
    }

    void Flow::locate (double seconds)
    {
        for (int k = 0; k < numOutputs; ++k)
            positions[(size_t) k] = std::fmod (std::max (0.0, seconds) * outputRate (settings, k), 1099511627776.0);
    }

    //==========================================================================
    double SpectralSpace::mappedHz (const Settings& s, double hz)
    {
        const auto anchor = std::max (1.0, s.anchorHz);
        if (hz <= 0.0)
            return hz;
        const auto e = 1.0 + clamp01 (s.blend) * (std::log2 (std::clamp (s.ratio, 1.05, 4.0)) - 1.0);
        return anchor * std::pow (hz / anchor, e);
    }

    double SpectralSpace::pictureHz (int point)
    {
        return 20.0 * std::pow (1000.0, (double) point / (pictureSize - 1));
    }

    void SpectralSpace::prepare (double sampleRate, int numChannels)
    {
        rate = std::max (1.0, sampleRate);
        fft = std::make_unique<Fft> (order);
        size = fft->size();
        hop = size / 4;
        bins = size / 2 + 1;
        window.resize ((size_t) size);
        for (int j = 0; j < size; ++j)
            window[(size_t) j] = (float) (0.5 - 0.5 * std::cos (twoPi * j / size));
        const auto channels = (size_t) std::max (1, numChannels);
        input.assign (channels, std::vector<float> ((size_t) size, 0.0f));
        output.assign (channels, std::vector<float> ((size_t) size, 0.0f));
        spectrum.assign ((size_t) size, {});
        made.assign ((size_t) size, {});
        magnitude.assign ((size_t) bins, 0.0f);
        peaks.reserve ((size_t) bins);
        bounds.reserve ((size_t) bins + 1);
        lastPeaks.assign (channels, {});
        for (auto& p : lastPeaks)
            p.reserve ((size_t) bins);
        lastTurn.assign (channels, std::vector<double> ((size_t) bins, 0.0));
        turn.assign ((size_t) bins, 0.0);
        reset();
    }

    void SpectralSpace::set (const Settings& s) { settings = s; }

    void SpectralSpace::reset()
    {
        for (auto& v : input)  std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : output) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& p : lastPeaks) p.clear();
        for (auto& t : lastTurn) std::fill (t.begin(), t.end(), 0.0);
        pos = 0;
        sinceFrame = 0;
        for (auto& a : shownIn)  a.store (-120.0f);
        for (auto& a : shownOut) a.store (-120.0f);
        shownPeaks.store (0);
    }

    void SpectralSpace::process (float* const* channels, int numChannels, int numFrames)
    {
        if (fft == nullptr)
            return;
        const auto used = std::min (numChannels, (int) input.size());
        const auto mix = (float) clamp01 (settings.mix);
        for (int i = 0; i < numFrames; ++i)
        {
            // A frame of the last `size` samples every hop, added to what is to come.
            if (++sinceFrame >= hop)
            {
                sinceFrame = 0;
                if (mix > 0.0f)
                    for (int ch = 0; ch < used; ++ch)
                        frame (ch);
            }
            for (int ch = 0; ch < used; ++ch)
            {
                auto& in = input[(size_t) ch];
                auto& out = output[(size_t) ch];
                const auto dry = in[(size_t) pos];   // the input `size` samples ago, level with the frames
                const auto wet = out[(size_t) pos];
                out[(size_t) pos] = 0.0f;
                in[(size_t) pos] = channels[ch][i];
                channels[ch][i] = dry + mix * (wet - dry);
            }
            pos = (pos + 1) % size;
        }
    }

    void SpectralSpace::frame (int channel)
    {
        const auto& in = input[(size_t) channel];
        for (int j = 0; j < size; ++j)
            spectrum[(size_t) j] = { in[(size_t) ((pos + j) % size)] * window[(size_t) j], 0.0f };
        fft->forward (spectrum);

        float loudest = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            magnitude[(size_t) k] = std::abs (spectrum[(size_t) k]);
            loudest = std::max (loudest, magnitude[(size_t) k]);
        }

        // The partials: local peaks within 70 dB of the loudest, then the strongest share.
        peaks.clear();
        const auto floor = std::max (loudest * 3.16e-4f, 1.0e-7f);
        for (int k = 2; k < bins - 2; ++k)
        {
            const auto m = magnitude[(size_t) k];
            if (m > floor && m > magnitude[(size_t) k - 1] && m >= magnitude[(size_t) k + 1]
                && m > magnitude[(size_t) k - 2] && m >= magnitude[(size_t) k + 2])
                peaks.push_back (k);
        }
        const auto wanted = std::min ((size_t) maxPeaks, (size_t) std::ceil (clamp01 (settings.density) * (double) peaks.size()));
        if (wanted < peaks.size())
        {
            std::nth_element (peaks.begin(), peaks.begin() + (std::ptrdiff_t) wanted, peaks.end(),
                              [this] (int a, int b) { return magnitude[(size_t) a] > magnitude[(size_t) b]; });
            peaks.resize (wanted);
            std::sort (peaks.begin(), peaks.end());
        }

        // Each peak owns the bins out to the lowest points between it and its neighbours.
        bounds.clear();
        bounds.push_back (0);
        for (size_t p = 1; p < peaks.size(); ++p)
        {
            auto low = peaks[p - 1] + 1;
            for (int k = low; k < peaks[p]; ++k)
                if (magnitude[(size_t) k] < magnitude[(size_t) low])
                    low = k;
            bounds.push_back (low);
        }
        bounds.push_back (bins);

        // Move each peak's bins to where the peak goes, turning their phase on from where the
        // nearest peak of the last frame left it, so a moved partial keeps its new pitch
        // smoothly from frame to frame (Laroche and Dolson's phase-locked shifting).
        std::fill (made.begin(), made.end(), std::complex<float> {});
        const auto binHz = rate / size;
        const auto& before = lastPeaks[(size_t) channel];
        auto& turned = lastTurn[(size_t) channel];
        const auto anchor = std::max (1.0, settings.anchorHz);
        for (size_t p = 0; p < peaks.size(); ++p)
        {
            const auto k = peaks[p];
            const auto a = std::log (magnitude[(size_t) k - 1] + 1.0e-12f), b = std::log (magnitude[(size_t) k] + 1.0e-12f),
                       c = std::log (magnitude[(size_t) k + 1] + 1.0e-12f);
            const auto bend = a - 2.0f * b + c;
            const auto offset = bend < 0.0f ? std::clamp (0.5f * (a - c) / bend, -0.5f, 0.5f) : 0.0f;
            const auto fromHz = (k + offset) * binHz;
            const auto toHz = mappedHz (settings, fromHz);
            turn[(size_t) k] = 0.0;
            if (toHz >= rate * 0.49)
                continue;   // above Nyquist: left out
            const auto shift = (int) std::lround ((toHz - fromHz) / binHz);

            double was = 0.0;
            if (! before.empty())
            {
                auto it = std::lower_bound (before.begin(), before.end(), k);
                int nearest = it == before.end() ? before.back() : *it;
                if (it != before.begin() && (it == before.end() || k - *(it - 1) < *it - k))
                    nearest = *(it - 1);
                if (std::abs (nearest - k) <= 3)
                    was = turned[(size_t) nearest];
            }
            const auto now = std::fmod (was + twoPi * (toHz - fromHz) * hop / rate, twoPi);
            turn[(size_t) k] = now;

            const auto tilt = std::clamp (std::pow (10.0, settings.tiltDb * std::log2 (std::max (1.0, toHz) / anchor) / 20.0), 0.01, 8.0);
            const auto spin = std::polar ((float) tilt, (float) now);
            for (int j = bounds[p]; j < bounds[p + 1]; ++j)
            {
                const auto t = j + shift;
                if (t >= 1 && t < bins - 1)
                    made[(size_t) t] += spectrum[(size_t) j] * spin;
            }
        }
        for (const auto k : peaks)
            turned[(size_t) k] = turn[(size_t) k];
        lastPeaks[(size_t) channel] = peaks;

        if (channel == 0)
        {
            // The picture: the loudest bin near each point, before and after, in dB full scale.
            const auto fullScale = size / 4.0f;
            for (int i = 0; i < pictureSize; ++i)
            {
                const auto hz = pictureHz (i);
                const auto lo = std::clamp ((int) (hz / 1.03 / binHz), 0, bins - 1), hi = std::clamp ((int) std::ceil (hz * 1.03 / binHz), lo, bins - 1);
                float heard = 0.0f, given = 0.0f;
                for (int k = lo; k <= hi; ++k)
                {
                    heard = std::max (heard, magnitude[(size_t) k]);
                    given = std::max (given, std::abs (made[(size_t) k]));
                }
                shownIn[(size_t) i].store (20.0f * std::log10 (heard / fullScale + 1.0e-9f));
                shownOut[(size_t) i].store (20.0f * std::log10 (given / fullScale + 1.0e-9f));
            }
            shownPeaks.store ((int) peaks.size());
        }

        for (int k = 1; k < bins - 1; ++k)
            made[(size_t) (size - k)] = std::conj (made[(size_t) k]);
        fft->inverse (made);
        // Hann in and out, a quarter apart, add up to 1.5.
        auto& out = output[(size_t) channel];
        for (int j = 0; j < size; ++j)
            out[(size_t) ((pos + j) % size)] += made[(size_t) j].real() * window[(size_t) j] * (1.0f / 1.5f);
    }

    void SpectralSpace::picture (float* inDb, float* outDb) const
    {
        for (int i = 0; i < pictureSize; ++i)
        {
            inDb[i] = shownIn[(size_t) i].load();
            outDb[i] = shownOut[(size_t) i].load();
        }
    }

    //==========================================================================
    Grain::Plan Grain::plan (const Settings& s, const Flow::Settings& flow, std::int64_t n)
    {
        // Streams 4 to 7, so a grain's timing, start, size and pitch never move together.
        Plan p;
        p.gapSeconds = (0.5 + Flow::event (flow, 4, n)) / std::clamp (s.density, 0.1, 200.0);
        p.back = Flow::event (flow, 5, n);
        p.sizeMs = std::max (1.0, s.sizeMs) * std::pow (Flow::ratioOf (flow), 2.0 * Flow::event (flow, 6, n) - 1.0);
        p.semitones = std::max (0.0, s.pitch) * (2.0 * Flow::event (flow, 7, n) - 1.0);
        p.pan = fan (n, s.width);
        return p;
    }

    void Grain::prepare (double sampleRate, int)
    {
        rate = std::max (1.0, sampleRate);
        ringSize = (int) std::ceil (bufferSeconds * rate) + 4;
        ring.assign ((size_t) ringSize, 0.0f);
        reset();
    }

    void Grain::set (const Settings& s, const Flow::Settings& f)
    {
        settings = s;
        flow = f;
    }

    void Grain::reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        next = 0;
        untilNext = 0.0;
        for (auto& v : voices)
            v.on = false;
        for (auto& a : shown)
            a.store (-1.0f);
        smoothedMix = (float) clamp01 (settings.mix);
    }

    void Grain::start (float flowStart, float flowSize)
    {
        const auto p = plan (settings, flow, next++);
        untilNext += std::max (1.0, p.gapSeconds * rate);

        auto free = std::find_if (voices.begin(), voices.end(), [] (const Voice& v) { return ! v.on; });
        if (free == voices.end())
            return;   // as many grains as it holds: this one is skipped
        const auto amount = clamp01 (settings.flow);
        const auto ms = p.sizeMs * std::pow (Flow::ratioOf (flow), amount * (2.0 * flowSize - 1.0));
        const auto length = (int) std::clamp (ms * 0.001 * rate, 16.0, 2.0 * rate);
        const auto speed = std::pow (2.0, p.semitones / 12.0);
        // Far enough back that a grain played faster never catches up with what is coming in.
        const auto least = std::max (0.0, (speed - 1.0) * length) + 64.0;
        const auto most = std::max (least, (double) ringSize - speed * length - 8.0);
        const auto back = std::clamp (frac (p.back + amount * flowStart) * std::clamp (settings.spreadSeconds, 0.0, 5.0) * rate + least, least, most);
        auto& v = *free;
        v.on = true;
        v.read = frac ((writePos - back) / (double) ringSize) * ringSize;
        v.speed = speed;
        v.age = 0;
        v.length = length;
        panGains (p.pan, v.gainL, v.gainR);
    }

    void Grain::process (float* const* channels, int numChannels, int numFrames, float flowStart, float flowSize)
    {
        if (ring.empty() || numChannels < 1)
            return;
        const auto stereo = numChannels > 1;
        const auto overlap = std::clamp (settings.density, 0.1, 200.0) * std::max (1.0, settings.sizeMs) * 0.001;
        const auto level = (float) (1.0 / std::sqrt (std::max (1.0, overlap)));
        const auto targetMix = (float) clamp01 (settings.mix);
        const auto mixCoef = (float) (1.0 - std::exp (-1.0 / (0.02 * rate)));
        const auto size = (double) ringSize;

        for (int i = 0; i < numFrames; ++i)
        {
            const auto l = channels[0][i], r = stereo ? channels[1][i] : l;
            if (! settings.freeze)
            {
                ring[(size_t) writePos] = 0.5f * (l + r);
                writePos = (writePos + 1) % ringSize;
            }
            for (int guard = 0; untilNext <= 0.0 && guard < 8; ++guard)
                start (flowStart, flowSize);
            untilNext = std::max (untilNext - 1.0, -8.0 * rate);

            float wetL = 0.0f, wetR = 0.0f;
            for (auto& v : voices)
            {
                if (! v.on)
                    continue;
                // Hann, and a four-point Hermite read so pitched grains stay clean.
                const auto w = (float) std::pow (std::sin (pi * (v.age + 0.5) / v.length), 2.0);
                const auto base = (int) std::floor (v.read);
                const auto t = (float) (v.read - base);
                auto at = [&] (int k) { return ring[(size_t) (((base + k) % ringSize + ringSize) % ringSize)]; };
                const auto xm = at (-1), x0 = at (0), x1 = at (1), x2 = at (2);
                const auto c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
                const auto s = ((c3 * t + c2) * t + c1) * t + x0;
                wetL += w * v.gainL * s;
                wetR += w * v.gainR * s;
                v.read += v.speed;
                if (v.read >= size)
                    v.read -= size;
                if (++v.age >= v.length)
                    v.on = false;
            }
            smoothedMix += (targetMix - smoothedMix) * mixCoef;
            wetL *= level;
            wetR *= level;
            if (stereo)
            {
                channels[0][i] = l + smoothedMix * (wetL - l);
                channels[1][i] = r + smoothedMix * (wetR - r);
            }
            else
                channels[0][i] = l + smoothedMix * ((wetL + wetR) * 0.7071f - l);
        }

        const auto spread = std::max (1.0, std::clamp (settings.spreadSeconds, 0.0, 5.0) * rate);
        for (size_t k = 0; k < voices.size(); ++k)
        {
            const auto& v = voices[k];
            const auto back = frac ((writePos - v.read) / size) * size;
            shown[2 * k].store ((float) clamp01 (back / spread));
            shown[2 * k + 1].store (v.on ? (float) std::pow (std::sin (pi * (v.age + 0.5) / v.length), 2.0) : -1.0f);
        }
    }

    void Grain::picture (std::vector<float>& taps) const
    {
        taps.clear();
        for (size_t k = 0; k < (size_t) maxGrains; ++k)
            if (const auto level = shown[2 * k + 1].load(); level >= 0.0f)
            {
                taps.push_back (shown[2 * k].load());
                taps.push_back (level);
            }
    }

    //==========================================================================
    const char* Matrix::nodeName (int node)
    {
        static const char* names[numNodes] { "Echo", "Filter", "Drive", "Pitch" };
        return names[std::clamp (node, 0, numNodes - 1)];
    }

    std::array<int, 3> Matrix::route (const Flow::Settings& flow, std::int64_t n)
    {
        // Three picks from streams 1 to 3: the first of four, the second of the three left,
        // the third of the last two.
        std::array<int, numNodes> left { 0, 1, 2, 3 };
        std::array<int, 3> r {};
        for (int pick = 0; pick < 3; ++pick)
        {
            const auto count = numNodes - pick;
            const auto i = std::clamp ((int) (Flow::event (flow, pick + 1, n) * count), 0, count - 1);
            r[(size_t) pick] = left[(size_t) i];
            std::copy (left.begin() + i + 1, left.end(), left.begin() + i);
        }
        return r;
    }

    double Matrix::delaySeconds (const Settings& s, const Flow::Settings& flow, int node)
    {
        return std::min (maxDelaySeconds, std::max (0.001, s.timeMs * 0.001) * std::pow (Flow::ratioOf (flow), node));
    }

    void Matrix::prepare (double sampleRate, int)
    {
        rate = std::max (1.0, sampleRate);
        lineSize = (int) std::ceil (maxDelaySeconds * rate) + 4;
        for (auto& l : lines)
            l.assign ((size_t) lineSize, 0.0f);
        shiftWindow = std::max (64, (int) (0.06 * rate));
        shiftLine.assign ((size_t) shiftWindow * 2 + 8, 0.0f);
        reset();
    }

    void Matrix::set (const Settings& s, const Flow::Settings& f)
    {
        settings = s;
        flow = f;
        for (int i = 0; i < numNodes; ++i)
        {
            delayTarget[(size_t) i] = std::clamp (delaySeconds (s, f, i) * rate, 1.0, (double) lineSize - 3.0);
            float left = 0.0f, right = 0.0f;
            panGains (fan (i, s.width), left, right);
            panL[(size_t) i] = left;
            panR[(size_t) i] = right;
        }
        glide = (float) (1.0 - std::exp (-1.0 / std::max (1.0, s.glideMs * 0.001 * rate)));
        drive = (float) std::pow (10.0, std::clamp (s.driveDb, 0.0, 36.0) / 20.0);
        shiftStep = (Flow::ratioOf (f) - 1.0) / shiftWindow;   // up by the ratio: φ is 8.33 semitones
    }

    void Matrix::reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        std::fill (shiftLine.begin(), shiftLine.end(), 0.0f);
        writePos = shiftPos = 0;
        shiftPhase = 0.0;
        ic1 = ic2 = 0.0;
        meter.fill (0.0f);
        eventPosition = 0.0;
        event = 0;
        delay = delayTarget;
        target (true);
        smoothedMix = (float) clamp01 (settings.mix);
    }

    void Matrix::locate (double seconds)
    {
        eventPosition = std::max (0.0, seconds) * std::clamp (settings.eventsHz, 0.0, 50.0);
        event = (std::int64_t) std::floor (eventPosition);
        target (false);
    }

    void Matrix::target (bool snap)
    {
        const auto r = route (flow, event);
        // A node left out of the route gets nothing new, so it rings on and fades.
        sendTarget.fill (0.0f);
        outTarget.fill (0.0f);
        for (auto& row : gainTarget)
            row.fill (0.0f);
        sendTarget[(size_t) r[0]] = 1.0f;
        gainTarget[(size_t) r[1]][(size_t) r[0]] = 1.0f;
        gainTarget[(size_t) r[2]][(size_t) r[1]] = 1.0f;
        gainTarget[(size_t) r[0]][(size_t) r[2]] = (float) std::clamp (settings.feedback, 0.0, 0.95);
        for (const auto node : r)
            outTarget[(size_t) node] = node == 2 ? 0.5774f * std::sqrt (drive) : 0.5774f;   // the drive gets its level back
        if (snap)
        {
            send = sendTarget;
            out = outTarget;
            gain = gainTarget;
        }
        for (int i = 0; i < 3; ++i)
            shownRoute[(size_t) i].store (r[(size_t) i]);
        shownEvent.store (event);
    }

    void Matrix::design (float flowTone)
    {
        const auto amount = clamp01 (settings.flow);
        const auto hz = std::clamp (settings.toneHz * std::pow (Flow::ratioOf (flow), 4.0 * amount * (2.0 * flowTone - 1.0)), 20.0, rate * 0.45);
        g = std::tan (pi * hz / rate);
        k = 1.0 / (ratio * ratio);   // Q = φ²
    }

    void Matrix::process (float* const* channels, int numChannels, int numFrames, float flowTone)
    {
        if (lines[0].empty() || numChannels < 1)
            return;
        design (flowTone);
        const auto stereo = numChannels > 1;
        const auto a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const auto targetMix = (float) clamp01 (settings.mix);
        const auto mixCoef = (float) (1.0 - std::exp (-1.0 / (0.02 * rate)));
        const auto meterFall = (float) std::exp (-1.0 / (0.1 * rate));
        const auto eventStep = std::clamp (settings.eventsHz, 0.0, 50.0) / rate;
        const auto shiftSize = (int) shiftLine.size();

        for (int i = 0; i < numFrames; ++i)
        {
            eventPosition += eventStep;
            if (const auto n = (std::int64_t) std::floor (eventPosition); n != event)
            {
                event = n;
                target (false);
            }

            const auto l = channels[0][i], r = stereo ? channels[1][i] : l;
            const auto in = 0.5f * (l + r);

            std::array<float, numNodes> y {};
            for (int node = 0; node < numNodes; ++node)
            {
                auto& d = delay[(size_t) node];
                d += (delayTarget[(size_t) node] - d) * glide;
                const auto readAt = writePos - d;
                const auto base = (int) std::floor (readAt);
                const auto t = (float) (readAt - base);
                const auto& line = lines[(size_t) node];
                const auto x0 = line[(size_t) ((base % lineSize + lineSize) % lineSize)], x1 = line[(size_t) (((base + 1) % lineSize + lineSize) % lineSize)];
                y[(size_t) node] = x0 + (x1 - x0) * t;
            }

            // The band-pass, unity at its centre.
            {
                const auto v3 = (double) y[1] - ic2;
                const auto v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                y[1] = (float) (k * v1);
            }
            // The drive: tanh, no louder than what goes in.
            y[2] = std::tanh (drive * y[2]) / drive;
            // The pitch shifter: two taps a half window apart, each faded by sin².
            {
                shiftLine[(size_t) shiftPos] = y[3];
                float sum = 0.0f;
                for (const auto offset : { 0.0, 0.5 })
                {
                    const auto p = frac (shiftPhase + offset);
                    const auto readAt = shiftPos - 1.0 - p * shiftWindow;
                    const auto base = (int) std::floor (readAt);
                    const auto t = (float) (readAt - base);
                    const auto x0 = shiftLine[(size_t) ((base % shiftSize + shiftSize) % shiftSize)];
                    const auto x1 = shiftLine[(size_t) (((base + 1) % shiftSize + shiftSize) % shiftSize)];
                    sum += (float) std::pow (std::sin (pi * p), 2.0) * (x0 + (x1 - x0) * t);
                }
                shiftPhase = frac (shiftPhase - shiftStep);
                shiftPos = (shiftPos + 1) % shiftSize;
                y[3] = sum;
            }

            float wetL = 0.0f, wetR = 0.0f;
            for (int to = 0; to < numNodes; ++to)
            {
                auto& s = send[(size_t) to];
                s += (sendTarget[(size_t) to] - s) * glide;
                float u = s * in;
                for (int from = 0; from < numNodes; ++from)
                {
                    auto& gn = gain[(size_t) to][(size_t) from];
                    gn += (gainTarget[(size_t) to][(size_t) from] - gn) * glide;
                    u += gn * y[(size_t) from];
                }
                lines[(size_t) to][(size_t) writePos] = std::tanh (u);   // never past ±1, whatever the routing

                auto& o = out[(size_t) to];
                o += (outTarget[(size_t) to] - o) * glide;
                const auto heard = o * y[(size_t) to];
                wetL += panL[(size_t) to] * heard;
                wetR += panR[(size_t) to] * heard;
                meter[(size_t) to] = std::max (meter[(size_t) to] * meterFall, std::abs (y[(size_t) to]));
            }
            writePos = (writePos + 1) % lineSize;

            smoothedMix += (targetMix - smoothedMix) * mixCoef;
            if (stereo)
            {
                channels[0][i] = l + smoothedMix * (wetL - l);
                channels[1][i] = r + smoothedMix * (wetR - r);
            }
            else
                channels[0][i] = l + smoothedMix * ((wetL + wetR) * 0.7071f - l);
        }
        for (int node = 0; node < numNodes; ++node)
            shownLevel[(size_t) node].store (meter[(size_t) node]);
    }
}
