// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioDriven.h"

#include <algorithm>
#include <cmath>

namespace phi
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double golden = 1.6180339887498949;

        double noteHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

        // A one-pole smoothing coefficient for a time constant in seconds.
        double coefficient (double seconds, double rate) { return seconds <= 0.0 ? 0.0 : std::exp (-1.0 / (seconds * rate)); }

        float white (std::uint32_t& seed)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return (float) ((double) seed / 4294967295.0 * 2.0 - 1.0);
        }

        // The mono sum of a block's channels at frame k.
        float monoAt (float* const* channels, int numChannels, int k)
        {
            if (numChannels <= 0)
                return 0.0f;
            if (numChannels == 1)
                return channels[0][k];
            return 0.5f * (channels[0][k] + channels[1][k]);
        }

        // A naive saw and square, made gentler with a polyBLEP at their jumps.
        double polyBlep (double t, double dt)
        {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return t * t + t + t + 1.0;
            }
            return 0.0;
        }
        double saw (double phase, double dt) { return 2.0 * phase - 1.0 - polyBlep (phase, dt); }
        double square (double phase, double dt)
        {
            auto v = phase < 0.5 ? 1.0 : -1.0;
            v += polyBlep (phase, dt);
            v -= polyBlep (std::fmod (phase + 0.5, 1.0), dt);
            return v;
        }
    }

    //==========================================================================
    void PitchTracker::prepare (double sampleRate)
    {
        rate = sampleRate;
        factor = std::max (1, (int) std::lround (rate / 12000.0));
        reducedRate = rate / factor;
        // A window of two of the longest periods.
        const auto window = (int) std::ceil (2.0 * reducedRate / lowestHz);
        ring.assign ((size_t) window, 0.0f);
        difference.assign ((size_t) window / 2 + 1, 0.0);
        reset();
    }

    void PitchTracker::reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        write = phase = sinceAnalysis = 0;
        accumulator = 0.0;
        lowpass = {};
        frequency = clarity = 0.0;
    }

    void PitchTracker::push (float sample)
    {
        if (ring.empty())
            return;
        // Two one-pole low-passes at about a third of the reduced rate's Nyquist, then the mean of each `factor`.
        const auto a = std::exp (-2.0 * pi * (reducedRate / 6.0) / rate);
        lowpass[0] = sample + a * (lowpass[0] - sample);
        lowpass[1] = lowpass[0] + a * (lowpass[1] - lowpass[0]);
        accumulator += lowpass[1];
        if (++phase < factor)
            return;
        ring[(size_t) write] = (float) (accumulator / factor);
        write = (write + 1) % (int) ring.size();
        accumulator = 0.0;
        phase = 0;
        if (++sinceAnalysis >= 64)
        {
            sinceAnalysis = 0;
            analyse();
        }
    }

    void PitchTracker::analyse()
    {
        const auto n = (int) ring.size();
        const auto half = n / 2;
        const auto shortest = std::max (2, (int) std::floor (reducedRate / highestHz));
        const auto longest = std::min (half - 1, (int) std::ceil (reducedRate / lowestHz));
        auto at = [&] (int i) { return ring[(size_t) ((write + i) % n)]; };   // oldest first

        double energy = 0.0;
        for (int i = 0; i < n; ++i)
            energy += (double) at (i) * at (i);
        if (energy / n < 1.0e-7)   // below about -70 dBFS: nothing to follow
        {
            frequency = 0.0;
            clarity = 0.0;
            return;
        }

        // The difference function and its cumulative mean normalised form.
        difference[0] = 1.0;
        double running = 0.0;
        for (int tau = 1; tau <= longest; ++tau)
        {
            double d = 0.0;
            for (int i = 0; i < half; ++i)
            {
                const auto diff = (double) at (i) - at (i + tau);
                d += diff * diff;
            }
            running += d;
            difference[(size_t) tau] = running > 0.0 ? d * tau / running : 1.0;
        }

        // The first dip under the threshold, or else the deepest.
        int best = -1;
        for (int tau = shortest; tau <= longest; ++tau)
            if (difference[(size_t) tau] < 0.15)
            {
                while (tau + 1 <= longest && difference[(size_t) tau + 1] < difference[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            best = shortest;
            for (int tau = shortest; tau <= longest; ++tau)
                if (difference[(size_t) tau] < difference[(size_t) best])
                    best = tau;
        }

        // Parabolic interpolation around the dip.
        double refined = best;
        if (best > 1 && best < longest)
        {
            const auto s0 = difference[(size_t) best - 1], s1 = difference[(size_t) best], s2 = difference[(size_t) best + 1];
            const auto denominator = s0 + s2 - 2.0 * s1;
            if (std::abs (denominator) > 1.0e-12)
                refined = best + 0.5 * (s0 - s2) / denominator;
        }
        clarity = std::clamp (1.0 - difference[(size_t) best], 0.0, 1.0);
        frequency = refined > 0.0 ? reducedRate / refined : 0.0;
    }

    //==========================================================================
    void BandPass::design (double hz, double q, double sampleRate)
    {
        hz = std::clamp (hz, 10.0, sampleRate * 0.45);
        const auto w = 2.0 * pi * hz / sampleRate;
        const auto alpha = std::sin (w) / (2.0 * std::max (0.1, q));
        const auto a0 = 1.0 + alpha;
        b0 = alpha / a0;
        b2 = -alpha / a0;
        a1 = -2.0 * std::cos (w) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    //==========================================================================
    int Vocoder::chordNotes (Chord c, std::array<int, 4>& offsets)
    {
        switch (c)
        {
            case Chord::single:  offsets = { 0, 0, 0, 0 };   return 1;
            case Chord::octaves: offsets = { 0, 12, 0, 0 };  return 2;
            case Chord::fifth:   offsets = { 0, 7, 12, 0 };  return 3;
            case Chord::major:   offsets = { 0, 4, 7, 12 };  return 4;
            case Chord::minor:   offsets = { 0, 3, 7, 12 };  return 4;
            case Chord::sus4:    offsets = { 0, 5, 7, 12 };  return 4;
        }
        offsets = { 0, 0, 0, 0 };
        return 1;
    }

    void Vocoder::prepare (double sampleRate)
    {
        rate = sampleRate;
        tracker.prepare (rate);
        designed = false;
        attack = coefficient (0.002, rate);
        air.design (std::min (8000.0, rate * 0.4), 0.7, rate);
        reset();
    }

    void Vocoder::set (const Settings& s)
    {
        settings = s;
        settings.bands = std::clamp (s.bands, 4, maxBands);
        if (! designed || settings.bands != designedFor.bands || settings.formant != designedFor.formant)
            design();
        release = coefficient (std::max (1.0, settings.releaseMs) / 1000.0, rate);
    }

    void Vocoder::design()
    {
        const auto n = settings.bands;
        const auto low = 80.0, high = std::min (12000.0, rate * 0.42);
        const auto ratio = std::pow (high / low, 1.0 / (n - 1));
        // Each band about as wide as the step between them.
        const auto q = std::sqrt (ratio) / (ratio - 1.0);
        const auto shift = std::pow (2.0, settings.formant / 12.0);
        for (int b = 0; b < n; ++b)
        {
            const auto hz = low * std::pow (ratio, b);
            analysis[(size_t) b].design (hz, q, rate);
            synthesis[(size_t) b].design (hz * shift, q, rate);
            synthesis2[(size_t) b].design (hz * shift, q, rate);
        }
        designedFor = settings;
        designed = true;
    }

    void Vocoder::reset()
    {
        for (auto* bank : { &analysis, &synthesis, &synthesis2 })
            for (auto& f : *bank)
                f.clear();
        air.clear();
        envelope = {};
        phases = {};
        glideHz = 0.0;
        tracker.reset();
    }

    void Vocoder::process (float* const* channels, int numChannels, int numFrames)
    {
        if (! designed)
            design();
        const auto n = settings.bands;
        std::array<int, 4> offsets {};
        const auto voices = chordNotes (settings.chord, offsets);
        const auto glide = coefficient (0.02, rate);
        // Two cascaded band-passes give 12 dB/octave skirts and lose some level: make it up.
        const auto makeUp = 2.0f * std::sqrt ((float) n);
        for (int k = 0; k < numFrames; ++k)
        {
            const auto in = monoAt (channels, numChannels, k);

            // The carrier's root: a note, or the sound's own pitch while it has one.
            auto rootHz = noteHz (settings.note);
            if (settings.follow)
            {
                tracker.push (in);
                if (tracker.hz() > 0.0 && tracker.confidence() > 0.6)
                    rootHz = tracker.hz();
                else if (glideHz > 0.0)
                    rootHz = glideHz;
            }
            glideHz = glideHz <= 0.0 ? rootHz : rootHz + glide * (glideHz - rootHz);

            double carrier = 0.0;
            for (int v = 0; v < voices; ++v)
            {
                const auto hz = glideHz * std::pow (2.0, offsets[(size_t) v] / 12.0);
                const auto dt = std::min (0.5, hz / rate);
                auto& p = phases[(size_t) v];
                switch (settings.carrier)
                {
                    case Carrier::saw:
                    case Carrier::sawAndNoise: carrier += saw (p, dt); break;
                    case Carrier::square:      carrier += square (p, dt); break;
                    case Carrier::noise:       break;
                }
                p += dt;
                if (p >= 1.0)
                    p -= 1.0;
            }
            carrier /= std::sqrt ((double) voices);
            if (settings.carrier == Carrier::noise)
                carrier = white (noise);
            else if (settings.carrier == Carrier::sawAndNoise)
                carrier = 0.8 * carrier + 0.35 * white (noise);

            float out = 0.0f;
            for (int b = 0; b < n; ++b)
            {
                const auto level = std::abs (analysis[(size_t) b].run (in));
                auto& e = envelope[(size_t) b];
                const auto c = level > e ? attack : release;
                e = (float) (level + c * (e - level));
                out += e * synthesis2[(size_t) b].run (synthesis[(size_t) b].run ((float) carrier));
            }
            out *= makeUp;
            out += (float) settings.sibilance * air.run (in);

            const auto wet = (float) settings.mix, dry = 1.0f - wet;
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][k] = dry * channels[ch][k] + wet * out;
        }
    }

    //==========================================================================
    double Strings::stringHz (const Settings& s, int string)
    {
        static const std::array<std::vector<double>, 9> chords {{
            { 0.0 },                       // unison: octaves of the note
            { 0.0 },                       // octaves
            { 0.0, 7.0 },                  // fifths
            { 0.0, 4.0, 7.0 },             // major
            { 0.0, 3.0, 7.0 },             // minor
            { 0.0, 5.0, 7.0 },             // sus4
            { 0.0, 4.0, 7.0, 11.0 },       // major 7
            { 0.0, 3.0, 7.0, 10.0 },       // minor 7
            { 0.0 },                       // phi: the note × φⁿ
        }};
        if (s.chord == Chord::unison)
            return noteHz (s.note) * (1.0 + 0.0015 * string);   // a chorus of nearly the same string
        if (s.chord == Chord::phi)
            return noteHz (s.note) * std::pow (golden, string * 0.5);
        const auto& c = chords[(size_t) s.chord];
        const auto octave = string / (int) c.size();
        return noteHz (s.note + 12.0 * octave + c[(size_t) string % c.size()]);
    }

    void Strings::prepare (double sampleRate)
    {
        rate = sampleRate;
        const auto longest = (int) std::ceil (rate / 20.0) + 4;
        for (auto& s : strings)
            s.line.assign ((size_t) longest, 0.0f);
        tune();
        reset();
    }

    void Strings::set (const Settings& s)
    {
        settings = s;
        settings.strings = std::clamp (s.strings, 1, maxStrings);
        tune();
    }

    void Strings::tune()
    {
        used = settings.strings;
        for (int i = 0; i < used; ++i)
        {
            auto& s = strings[(size_t) i];
            if (s.line.empty())
                continue;
            const auto hz = std::clamp (stringHz (settings, i), 20.0, rate * 0.25);
            const auto period = rate / hz;
            s.length = std::clamp ((int) std::floor (period), 2, (int) s.line.size() - 2);
            s.fraction = period - s.length;
            // Feedback for the ring time: -60 dB over decaySeconds, round trips of `period`.
            s.feedback = (float) std::pow (10.0, -3.0 * period / (rate * std::max (0.05, settings.decaySeconds)));
            s.damp = (float) (0.05 + 0.9 * (1.0 - std::clamp (settings.brightness, 0.0, 1.0)));
            // Spread across the field, alternating sides from the centre out.
            const auto place = used == 1 ? 0.0 : ((i % 2 == 0 ? -1.0 : 1.0) * (0.5 + 0.5 * i / (double) (used - 1))) * settings.width;
            const auto angle = (place + 1.0) * pi / 4.0;
            s.panL = (float) std::cos (angle);
            s.panR = (float) std::sin (angle);
        }
    }

    void Strings::reset()
    {
        for (auto& s : strings)
        {
            std::fill (s.line.begin(), s.line.end(), 0.0f);
            s.write = 0;
            s.state = 0.0f;
        }
        dcIn = dcOut = 0.0f;
    }

    void Strings::process (float* const* channels, int numChannels, int numFrames)
    {
        const auto gain = 0.5f / std::sqrt ((float) std::max (1, used));
        for (int k = 0; k < numFrames; ++k)
        {
            const auto in = monoAt (channels, numChannels, k);
            float left = 0.0f, right = 0.0f;
            for (int i = 0; i < used; ++i)
            {
                auto& s = strings[(size_t) i];
                const auto size = (int) s.line.size();
                // Read the delay `length + fraction` behind the write position.
                auto r = s.write - s.length;
                if (r < 0)
                    r += size;
                auto r1 = r - 1;
                if (r1 < 0)
                    r1 += size;
                const auto delayed = (float) ((1.0 - s.fraction) * s.line[(size_t) r] + s.fraction * s.line[(size_t) r1]);
                // A one-pole low-pass in the loop: the string's damping.
                s.state = delayed + s.damp * (s.state - delayed);
                const auto y = s.state * s.feedback;
                s.line[(size_t) s.write] = (float) std::tanh (in * 0.5f + y);
                s.write = (s.write + 1) % size;
                left += y * s.panL;
                right += y * s.panR;
            }
            left *= gain;
            right *= gain;
            const auto wet = (float) settings.mix, dry = 1.0f - wet;
            if (numChannels == 1)
                channels[0][k] = dry * channels[0][k] + wet * 0.5f * (left + right);
            else
            {
                channels[0][k] = dry * channels[0][k] + wet * left;
                channels[1][k] = dry * channels[1][k] + wet * right;
                for (int ch = 2; ch < numChannels; ++ch)
                    channels[ch][k] = dry * channels[ch][k];
            }
        }
    }

    //==========================================================================
    double Grains::random()
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return (double) seed / 4294967295.0;
    }

    void Grains::prepare (double sampleRate)
    {
        rate = sampleRate;
        for (auto& c : loop)
            c.assign ((size_t) std::ceil (bufferSeconds * rate), 0.0f);
        reset();
    }

    void Grains::reset()
    {
        for (auto& c : loop)
            std::fill (c.begin(), c.end(), 0.0f);
        write = filled = 0;
        for (auto& g : grains)
            g.on = false;
        untilNext = 0.0;
    }

    int Grains::activeGrains() const
    {
        return (int) std::count_if (grains.begin(), grains.end(), [] (const Grain& g) { return g.on; });
    }

    void Grains::process (float* const* channels, int numChannels, int numFrames)
    {
        const auto size = (int) loop[0].size();
        if (size == 0)
            return;
        const auto grainLength = std::max (8, (int) (std::clamp (settings.sizeMs, 10.0, 1000.0) * rate / 1000.0));
        const auto interval = rate / std::clamp (settings.density, 1.0, 100.0);
        // Overlapping grains add up: keep the sum near the level of one.
        const auto overlap = std::max (1.0, grainLength / interval);
        const auto norm = (float) (1.0 / std::sqrt (overlap));
        for (int k = 0; k < numFrames; ++k)
        {
            if (! settings.freeze)
            {
                loop[0][(size_t) write] = channels[0][k];
                loop[1][(size_t) write] = numChannels > 1 ? channels[1][k] : channels[0][k];
                write = (write + 1) % size;
                filled = std::min (size, filled + 1);
            }

            // A new grain when it is time, if there is sound to take it from.
            untilNext -= 1.0;
            if (untilNext <= 0.0)
            {
                untilNext += interval * (0.5 + random());
                if (filled > grainLength)
                    for (auto& g : grains)
                        if (! g.on)
                        {
                            const auto semis = settings.pitch + settings.pitchJitter * (2.0 * random() - 1.0);
                            g.step = std::pow (2.0, semis / 12.0) * (settings.reverse ? -1.0 : 1.0);
                            const auto travel = std::abs (g.step) * grainLength;
                            auto back = settings.positionMs * rate / 1000.0 * (1.0 + settings.jitter * (2.0 * random() - 1.0));
                            // Never past the present (forwards) nor before what the loop holds.
                            back = std::clamp (back, g.step > 0.0 ? travel + 1.0 : 1.0, (double) filled - (g.step < 0.0 ? 1.0 : travel) - 1.0);
                            g.position = std::fmod (write - back + 2.0 * size, (double) size);
                            g.age = 0;
                            g.length = grainLength;
                            const auto place = settings.spread * (2.0 * random() - 1.0);
                            g.panL = (float) std::cos ((place + 1.0) * pi / 4.0);
                            g.panR = (float) std::sin ((place + 1.0) * pi / 4.0);
                            g.on = true;
                            break;
                        }
            }

            float left = 0.0f, right = 0.0f;
            for (auto& g : grains)
            {
                if (! g.on)
                    continue;
                const auto i0 = (int) std::floor (g.position);
                const auto frac = (float) (g.position - i0);
                const auto a = (i0 % size + size) % size, b = (a + 1) % size;
                const auto window = (float) (0.5 - 0.5 * std::cos (2.0 * pi * g.age / g.length));
                const auto l = ((1.0f - frac) * loop[0][(size_t) a] + frac * loop[0][(size_t) b]) * window;
                const auto r = ((1.0f - frac) * loop[1][(size_t) a] + frac * loop[1][(size_t) b]) * window;
                // A mono grain placed by its pan, keeping a little of its own stereo.
                const auto m = 0.5f * (l + r);
                left += m * g.panL * 1.414f * 0.7f + 0.3f * l;
                right += m * g.panR * 1.414f * 0.7f + 0.3f * r;
                g.position += g.step;
                if (g.position >= size)
                    g.position -= size;
                else if (g.position < 0.0)
                    g.position += size;
                if (++g.age >= g.length)
                    g.on = false;
            }
            left *= norm;
            right *= norm;
            const auto wet = (float) settings.mix, dry = 1.0f - wet;
            if (numChannels == 1)
                channels[0][k] = dry * channels[0][k] + wet * 0.5f * (left + right);
            else
            {
                channels[0][k] = dry * channels[0][k] + wet * left;
                channels[1][k] = dry * channels[1][k] + wet * right;
            }
        }
    }

    //==========================================================================
    void Double::prepare (double sampleRate)
    {
        rate = sampleRate;
        tracker.prepare (rate);
        reset();
    }

    void Double::reset()
    {
        tracker.reset();
        phase = currentHz = targetHz = level = gate = lowState = 0.0;
        phiPhases = {};
    }

    void Double::process (float* const* channels, int numChannels, int numFrames)
    {
        const auto glide = coefficient (std::max (1.0, settings.glideMs) / 1000.0, rate);
        const auto follow = coefficient (0.01, rate), fall = coefficient (0.08, rate);
        const auto gateTime = coefficient (0.015, rate);
        const auto cutoff = 200.0 * std::pow (60.0, std::clamp (settings.tone, 0.0, 1.0));   // 200 Hz .. 12 kHz
        const auto lowCoefficient = std::exp (-2.0 * pi * cutoff / rate);
        for (int k = 0; k < numFrames; ++k)
        {
            const auto in = monoAt (channels, numChannels, k);
            tracker.push (in);
            // The level of the line, followed quickly up and slowly down.
            const auto a = std::abs ((double) in);
            level = a + (a > level ? follow : fall) * (level - a);

            // A note while the tracker is sure of one.
            const bool sure = tracker.hz() > 0.0 && tracker.confidence() >= settings.threshold;
            if (sure)
            {
                auto semis = 12.0 * std::log2 (tracker.hz() / 440.0) + 69.0 + settings.interval;
                if (settings.quantize)
                    semis = std::round (semis);
                targetHz = noteHz (semis);
                if (currentHz <= 0.0)
                    currentHz = targetHz;
            }
            gate = (sure ? 1.0 : 0.0) + gateTime * (gate - (sure ? 1.0 : 0.0));
            if (currentHz > 0.0)
                currentHz = targetHz + glide * (currentHz - targetHz);

            double v = 0.0;
            const auto dt = std::min (0.5, currentHz / rate);
            switch (settings.wave)
            {
                case Wave::sine:     v = std::sin (2.0 * pi * phase); break;
                case Wave::triangle: v = 1.0 - 4.0 * std::abs (phase - 0.5); break;
                case Wave::saw:      v = saw (phase, dt); break;
                case Wave::square:   v = square (phase, dt); break;
                case Wave::phi:
                {
                    double amplitude = 1.0, sum = 0.0;
                    for (size_t p = 0; p < phiPhases.size(); ++p)
                    {
                        sum += amplitude * std::sin (2.0 * pi * phiPhases[p]);
                        phiPhases[p] = std::fmod (phiPhases[p] + dt * std::pow (golden, (double) p), 1.0);
                        amplitude /= golden;
                    }
                    v = sum / 2.2;
                    break;
                }
            }
            phase += dt;
            if (phase >= 1.0)
                phase -= 1.0;
            lowState = v + lowCoefficient * (lowState - v);
            const auto out = (float) (lowState * level * gate * 2.0);

            const auto wet = (float) settings.mix, dry = 1.0f - wet;
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][k] = dry * channels[ch][k] + wet * out;
        }
    }

    //==========================================================================
    void Trigger::prepare (double sampleRate)
    {
        rate = sampleRate;
        reset();
    }

    void Trigger::reset()
    {
        fast = slow = 0.0;
        holdLeft = count = 0;
        playing = false;
        t = phase = 0.0;
        noiseState = noiseState2 = 0.0;
        bellPhases = {};
    }

    void Trigger::fire (float v)
    {
        ++count;
        playing = true;
        t = 0.0;
        phase = 0.0;
        bellPhases = {};
        velocity = v;
    }

    float Trigger::voice()
    {
        if (! playing)
            return 0.0f;
        const auto tune = std::pow (2.0, settings.tune / 12.0);
        const auto length = 0.25 + 1.5 * std::clamp (settings.decay, 0.0, 1.0);   // a scale on each drum's own time
        double out = 0.0;
        auto envelope = [&] (double seconds) { return std::exp (-t / (seconds * length)); };
        auto advance = [&] (double hz) { phase = std::fmod (phase + hz / rate, 1.0); return std::sin (2.0 * pi * phase); };
        auto hp = [&] (double x)   // a crude high-pass for the noise: x minus its one-pole low-pass
        {
            noiseState = x + 0.6 * (noiseState - x);
            return x - noiseState;
        };
        switch (settings.sound)
        {
            case Sound::kick:
                out = advance ((45.0 + 110.0 * std::exp (-t / 0.03)) * tune) * envelope (0.35);
                break;
            case Sound::snare:
                out = 0.5 * advance (190.0 * tune) * envelope (0.08) + 0.6 * hp (white (seed)) * envelope (0.15);
                break;
            case Sound::clap:
            {
                // Three quick bursts, then a tail.
                const auto burst = std::fmod (t, 0.011) < 0.004 && t < 0.033 ? 1.0 : 0.0;
                const auto n = white (seed);
                noiseState2 = n + 0.3 * (noiseState2 - n);
                out = hp (noiseState2) * (burst * 0.9 + (t >= 0.033 ? envelope (0.12) : 0.0));
                break;
            }
            case Sound::hat:
                out = hp (white (seed)) * envelope (0.04);
                break;
            case Sound::tom:
                out = advance ((110.0 + 120.0 * std::exp (-t / 0.05)) * tune) * envelope (0.3);
                break;
            case Sound::rim:
                out = advance (1700.0 * tune) * envelope (0.012) + 0.4 * hp (white (seed)) * envelope (0.004);
                break;
            case Sound::bell:
            {
                double amplitude = 1.0;
                for (size_t p = 0; p < bellPhases.size(); ++p)
                {
                    out += amplitude * std::sin (2.0 * pi * bellPhases[p]);
                    bellPhases[p] = std::fmod (bellPhases[p] + 440.0 * tune * std::pow (golden, (double) p) / rate, 1.0);
                    amplitude /= golden;
                }
                out *= 0.45 * envelope (0.9);
                break;
            }
        }
        t += 1.0 / rate;
        if (t > 4.0 * length)
            playing = false;
        return (float) (out * velocity);
    }

    void Trigger::process (float* const* channels, int numChannels, int numFrames)
    {
        const auto fastC = coefficient (0.001, rate), slowRise = coefficient (0.03, rate), slowFall = coefficient (0.15, rate);
        const auto threshold = std::pow (10.0, settings.thresholdDb / 20.0);
        // Sensitivity 0..1: the fast envelope must beat the slow one by 4x down to 1.25x.
        const auto ratio = 4.0 - 2.75 * std::clamp (settings.sensitivity, 0.0, 1.0);
        const auto hold = (int) (std::max (5.0, settings.holdMs) * rate / 1000.0);
        for (int k = 0; k < numFrames; ++k)
        {
            const auto a = std::abs ((double) monoAt (channels, numChannels, k));
            fast = a + fastC * (fast - a);
            slow = fast + (fast > slow ? slowRise : slowFall) * (slow - fast);
            if (holdLeft > 0)
                --holdLeft;
            else if (fast > threshold && fast > ratio * slow + 1.0e-6)
            {
                // The hit's loudness, 0 dB at full scale, scaled by Dynamics.
                const auto loud = std::clamp (fast * 2.0, 0.0, 1.0);
                fire ((float) ((1.0 - settings.dynamics) + settings.dynamics * loud));
                holdLeft = hold;
            }
            const auto out = voice();
            const auto wet = (float) settings.mix, dry = 1.0f - wet;
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][k] = dry * channels[ch][k] + wet * out;
        }
    }
}
