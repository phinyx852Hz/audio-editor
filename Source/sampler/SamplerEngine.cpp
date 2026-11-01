// SPDX-License-Identifier: AGPL-3.0-only
#include "SamplerEngine.h"

#include <algorithm>
#include <cmath>

namespace sampler
{
    namespace
    {
        constexpr double quiet = 1.0e-4;   // -80 dB: a releasing voice is done
        constexpr int control = 16;        // frames between updates of pitch, cutoff, envelopes and LFOs
        constexpr int maxBlock = 1024;     // render() works in blocks of up to this many frames
        constexpr int maxSteps = maxBlock / control;

        // A segment's progress (0..1) bowed by a curve: above 0 it moves fast first, below 0 slow first.
        double bowed (double p, double curve)
        {
            p = std::clamp (p, 0.0, 1.0);
            return curve >= 0.0 ? 1.0 - std::pow (1.0 - p, 1.0 + 4.0 * curve) : std::pow (p, 1.0 - 4.0 * curve);
        }
        // A one-pole curve that falls 60 dB in `seconds`.
        double fall (double seconds, double rate) { return seconds > 0.0 ? std::exp (-std::log (1000.0) / (seconds * rate)) : 0.0; }
    }

    SamplePtr makeSample (const AudioData& d)
    {
        auto s = std::make_shared<Sample>();
        const auto n = d.length();
        s->left.resize ((size_t) n);
        s->right.resize ((size_t) n);
        std::vector<std::vector<float>> all ((size_t) d.numChannels(), std::vector<float> (AudioChunk::maxFrames));
        std::vector<float*> ptrs;
        for (auto& c : all)
            ptrs.push_back (c.data());
        for (Frame pos = 0; pos < n;)
        {
            const auto count = (int) std::min<Frame> (AudioChunk::maxFrames, n - pos);
            d.read (pos, count, ptrs.data());
            std::copy (all[0].begin(), all[0].begin() + count, s->left.begin() + pos);
            const auto& r = all[d.numChannels() > 1 ? 1 : 0];
            std::copy (r.begin(), r.begin() + count, s->right.begin() + pos);
            pos += count;
        }
        return s;
    }

    void Engine::prepare (double sampleRate)
    {
        std::lock_guard g (lock);
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    }

    void Engine::setSample (SamplePtr s)
    {
        std::lock_guard g (lock);
        data = std::move (s);
        for (auto& v : voice)
            v.active = false;
    }

    void Engine::setSettings (const Settings& s)
    {
        std::lock_guard g (lock);
        current = s;
        current.voices = std::clamp (s.voices, 1, maxVoices);
        current.keyLow = std::clamp (s.keyLow, 0, 127);
        current.keyHigh = std::clamp (s.keyHigh, current.keyLow, 127);
    }

    Settings Engine::settings() const
    {
        std::lock_guard g (lock);
        return current;
    }

    SamplePtr Engine::sample() const
    {
        std::lock_guard g (lock);
        return data;
    }

    void Engine::start (Voice& v, int note, float velocity, double fromPitch)
    {
        v = {};
        v.active = v.held = true;
        v.note = note;
        v.pitch = note;
        v.velocity = std::clamp (velocity, 0.0f, 1.0f);
        v.position = (double) std::max<Frame> (0, current.start);
        const bool classic = current.mode == Mode::classic;
        v.stage = classic && current.attack > 0.0 ? 0 : 1;
        v.level = classic && current.attack > 0.0 ? 0.0 : 1.0;
        v.age = ++clock;
        glideTo (v, note, fromPitch);
    }

    void Engine::glideTo (Voice& v, int note, double fromPitch)
    {
        v.note = note;
        // Fingered: only from a key still held (the caller passes -1 otherwise).
        if (current.glide > 0.0 && fromPitch >= 0.0 && std::abs (fromPitch - note) > 1.0e-9)
        {
            v.pitch = fromPitch;
            v.glideLeft = std::max<Frame> (1, (Frame) (current.glide * rate));
            v.glideStep = ((double) note - fromPitch) / (double) v.glideLeft;
        }
        else
        {
            v.pitch = note;
            v.glideLeft = 0;
        }
        lastPitch = note;
    }

    void Engine::releaseVoice (Voice& v)
    {
        v.held = false;
        if (pedal)
        {
            v.sustained = true;
            return;
        }
        if (current.mode == Mode::classic || current.gate)
            startRelease (v);
    }

    // The amp and the pitch and filter envelopes go on to their releases together.
    void Engine::startRelease (Voice& v)
    {
        v.stage = 3;
        for (auto& e : v.envelope)
            if (e.stage < 3)
            {
                e.stage = 3;
                e.time = 0.0;
                e.from = e.level;
            }
    }

    void Engine::push (int note)
    {
        pop (note);
        if (stacked < (int) stack.size())
            stack[(size_t) stacked++] = note;
    }

    void Engine::pop (int note)
    {
        for (int i = 0; i < stacked; ++i)
            if (stack[(size_t) i] == note)
            {
                std::copy (stack.begin() + i + 1, stack.begin() + stacked, stack.begin() + i);
                --stacked;
                return;
            }
    }

    void Engine::noteOn (int note, float velocity)
    {
        if (velocity <= 0.0f)
        {
            noteOff (note);
            return;
        }
        std::lock_guard g (lock);
        if (data == nullptr || note < 0 || note > 127 || note < current.keyLow || note > current.keyHigh)
            return;
        const bool othersHeld = stacked > 0;
        push (note);
        retrigger (! othersHeld);
        // Glide comes from the last note, or with Fingered only from one still held.
        const auto from = current.fingered && ! othersHeld ? -1.0 : lastPitch;

        if (current.voices == 1)
        {
            // One voice: a key held over another moves it (legato) or starts it again.
            Voice* playing = nullptr;
            for (auto& v : voice)
                if (v.active)
                    playing = &v;
            if (playing != nullptr && current.legato && othersHeld && playing->held)
            {
                glideTo (*playing, note, playing->pitch);
                return;
            }
            const auto pitchNow = playing != nullptr && othersHeld ? playing->pitch : from;
            for (auto& v : voice)
                v.active = false;
            start (voice[0], note, velocity, pitchNow);
            return;
        }

        // A free voice, else the oldest released one, else the oldest.
        int used = 0;
        for (const auto& v : voice)
            used += v.active ? 1 : 0;
        Voice* chosen = nullptr;
        if (used < current.voices)
            for (auto& v : voice)
                if (! v.active)
                {
                    chosen = &v;
                    break;
                }
        if (chosen == nullptr)
        {
            for (auto& v : voice)
                if (v.active && (chosen == nullptr || (! v.held && chosen->held) || ((v.held == chosen->held) && v.age < chosen->age)))
                    chosen = &v;
        }
        if (chosen != nullptr)
            start (*chosen, note, velocity, from);
    }

    void Engine::noteOff (int note)
    {
        std::lock_guard g (lock);
        pop (note);
        if (current.voices == 1 && stacked > 0)
        {
            // One voice: letting go of the key that sounds goes back to the last one still held.
            for (auto& v : voice)
                if (v.active && v.held && v.note == note)
                {
                    const auto back = stack[(size_t) stacked - 1];
                    if (current.legato)
                        glideTo (v, back, v.pitch);
                    else
                        start (v, back, v.velocity, v.pitch);
                    return;
                }
        }
        for (auto& v : voice)
            if (v.active && v.held && v.note == note)
                releaseVoice (v);
    }

    void Engine::allNotesOff()
    {
        std::lock_guard g (lock);
        pedal = false;
        stacked = 0;
        for (auto& v : voice)
            if (v.active)
            {
                v.held = v.sustained = false;
                startRelease (v);
            }
    }

    void Engine::panic()
    {
        std::lock_guard g (lock);
        pedal = false;
        stacked = 0;
        for (auto& v : voice)
            v.active = false;
    }

    void Engine::pitchBend (double amount)
    {
        std::lock_guard g (lock);
        bend = std::clamp (amount, -1.0, 1.0);
    }

    void Engine::modWheel (double amount)
    {
        std::lock_guard g (lock);
        wheel = std::clamp (amount, 0.0, 1.0);
    }

    // Each Note starts the LFOs again at every note, First Note only when no other key is held.
    void Engine::retrigger (bool firstNote)
    {
        for (size_t k = 0; k < lfos.size(); ++k)
        {
            const auto& o = current.lfo[k];
            if (o.retrigger == Retrigger::off || (o.retrigger == Retrigger::firstNote && ! firstNote))
                continue;
            noise = noise * 1664525u + 1013904223u;
            lfos[k].phase = o.randomPhase ? (double) (noise >> 8) / 16777216.0 : std::clamp (o.phase, 0.0, 1.0);
        }
    }

    void Engine::sustainPedal (bool down)
    {
        std::lock_guard g (lock);
        pedal = down;
        if (! down)
            for (auto& v : voice)
                if (v.active && v.sustained)
                {
                    v.sustained = false;
                    if (current.mode == Mode::classic || current.gate)
                        startRelease (v);
                }
    }

    float Engine::sampleAt (const std::vector<float>& x, double position) const
    {
        // Cubic (Catmull-Rom) between the neighbours, so transposed notes stay smooth.
        const auto n = (Frame) x.size();
        if (current.reverse)
            position = (double) (n - 1) - position;
        const auto i = (Frame) std::floor (position);
        const auto t = (float) (position - (double) i);
        auto at = [&] (Frame k) { return k >= 0 && k < n ? x[(size_t) k] : 0.0f; };
        const auto a = at (i - 1), b = at (i), c = at (i + 1), d = at (i + 2);
        return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
    }

    namespace
    {
        // One state-variable filter (Simper's trapezoidal form): low, band and high at once.
        struct FilterCoefficients
        {
            double a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 2.0;
            FilterCoefficients (double cutoff, double rate, double k_) : k (k_)
            {
                const auto g = std::tan (M_PI * std::clamp (cutoff, 20.0, 0.45 * rate) / rate);
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }
            FilterCoefficients() = default;
        };

        double filtered (double x, double* state, const FilterCoefficients& c, FilterType type)
        {
            const auto v3 = x - state[1];
            const auto v1 = c.a1 * state[0] + c.a2 * v3;
            const auto v2 = state[1] + c.a2 * state[0] + c.a3 * v3;
            state[0] = 2.0 * v1 - state[0];
            state[1] = 2.0 * v2 - state[1];
            switch (type)
            {
                case FilterType::highPass: return x - c.k * v1 - v2;
                case FilterType::bandPass: return v1 * c.k;   // unity at the centre
                case FilterType::notch: return x - c.k * v1;
                default: return v2;
            }
        }

        // A pitch or filter envelope `dt` seconds on.
        template <typename Stage>
        void advance (Stage& e, const Envelope& o, double dt)
        {
            const auto sustain = std::clamp (o.sustain, 0.0, 1.0);
            switch (e.stage)
            {
                case 0:
                    e.time += dt;
                    if (o.attack <= 0.0 || e.time >= o.attack)
                    {
                        e.level = 1.0;
                        e.stage = 1;
                        e.time = 0.0;
                    }
                    else
                        e.level = bowed (e.time / o.attack, o.curve);
                    break;
                case 1:
                    e.time += dt;
                    if (o.decay <= 0.0 || e.time >= o.decay)
                    {
                        e.level = sustain;
                        e.stage = 2;
                    }
                    else
                        e.level = 1.0 + (sustain - 1.0) * bowed (e.time / o.decay, o.curve);
                    break;
                case 2: e.level = sustain; break;
                case 3:
                    e.time += dt;
                    if (o.release <= 0.0 || e.time >= o.release)
                    {
                        e.level = 0.0;
                        e.stage = 4;
                    }
                    else
                        e.level = e.from * (1.0 - bowed (e.time / o.release, o.curve));
                    break;
                default: e.level = 0.0; break;
            }
        }

        // One cycle of an LFO's wave at phase p (0..1), from -1 to 1; the random waves are drawn elsewhere.
        double waveAt (Wave w, double p, double shape)
        {
            shape = std::clamp (shape, 0.0, 1.0);
            switch (w)
            {
                case Wave::sine:
                {
                    const auto x = std::sin (2.0 * M_PI * p);
                    return (1.0 - shape) * x + shape * std::tanh (3.0 * x) / std::tanh (3.0);   // rounder to squarer
                }
                case Wave::triangle:
                {
                    const auto x = p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0;
                    return std::clamp (x * (1.0 + 4.0 * shape), -1.0, 1.0);   // flattened into a trapezoid
                }
                case Wave::saw:
                {
                    // The peak's place: 0 a ramp down, 0.5 a triangle, 1 a ramp up.
                    const auto peak = std::clamp (shape, 0.001, 0.999);
                    return p < peak ? -1.0 + 2.0 * p / peak : 1.0 - 2.0 * (p - peak) / (1.0 - peak);
                }
                case Wave::pulse: return p < std::clamp (shape, 0.02, 0.98) ? 1.0 : -1.0;
                case Wave::ramp:
                {
                    const auto wait = std::min (shape, 0.98);
                    return p < wait ? -1.0 : -1.0 + 2.0 * (p - wait) / (1.0 - wait);
                }
                case Wave::log: return 2.0 * bowed (p, 2.0 * shape - 1.0) - 1.0;
                default: return 0.0;
            }
        }

        // Vintage and Turntable: the rate and bits the sound is brought down to.
        double vintageRate (Quality q)
        {
            switch (q)
            {
                case Quality::vintage: return 26040.0;
                case Quality::turntable45: return 26040.0 * 33.333 / 45.0;
                case Quality::turntable78: return 26040.0 * 33.333 / 78.0;
                default: return 0.0;
            }
        }
    }

    void Engine::render (float* left, float* right, int frames)
    {
        std::lock_guard g (lock);
        for (int done = 0; done < frames;)
        {
            const auto n = std::min (frames - done, maxBlock);
            renderBlock (left + done, right + done, n);
            done += n;
        }
    }

    void Engine::renderBlock (float* left, float* right, int frames)
    {
        if (data == nullptr)
        {
            for (auto& h : heads)
                h.store (-1.0, std::memory_order_relaxed);
            for (auto& s : sounding)
                s.store (false, std::memory_order_relaxed);
            return;
        }
        const auto& s = current;
        const auto length = data->length();
        const auto end = (double) (s.end < 0 ? length : std::clamp<Frame> (s.end, 1, length));
        const auto begin = (double) std::clamp<Frame> (s.start, 0, (Frame) end - 1);
        const auto loopEnd = (double) (s.loopEnd < 0 ? (Frame) end : std::clamp<Frame> (s.loopEnd, (Frame) begin + 1, (Frame) end));
        const auto loopStart = (double) std::clamp<Frame> (s.loopStart, (Frame) begin, (Frame) loopEnd - 1);
        const auto loopLength = loopEnd - loopStart;
        const bool looping = s.mode == Mode::classic && s.loop && loopLength >= 2.0;
        const bool alternate = s.loopMode == LoopMode::alternate || s.loopMode == LoopMode::alternateUntilRelease;
        // The seam is blended with what comes just before the loop's start, so there must be some.
        const auto crossfade = alternate ? 0.0 : std::min ({ s.loopCrossfade * rate, loopStart, loopLength / 2.0 });
        const auto gain = std::pow (10.0, s.gainDb / 20.0);
        const auto attackStep = s.attack > 0.0 ? 1.0 / (s.attack * rate) : 1.0;
        const auto decayCoeff = fall (s.decay, rate), releaseCoeff = fall (s.release, rate);
        const auto sustain = std::clamp (s.sustain, 0.0, 1.0);
        const auto fadeInFrames = s.fadeIn * rate, fadeOutFrames = s.fadeOut * rate;
        const auto gateStep = 1.0 / (std::max (s.fadeOut, 0.002) * rate);
        const auto drive = s.drive > 0.0 ? std::pow (10.0, s.drive / 20.0) : 1.0;
        const auto driveNorm = 1.0 / std::tanh (drive);
        // Resonance from flat (2, no peak) to almost ringing; a 24 dB filter's second stage stays flat.
        const auto k = 2.0 - 1.96 * std::clamp (s.resonance, 0.0, 1.0);
        const auto holdStep = vintageRate (s.quality) / rate;

        auto loopOn = [&] (const Voice& v)
        {
            if (! looping)
                return false;
            switch (s.loopMode)
            {
                case LoopMode::once: return v.loops < 1;
                case LoopMode::untilRelease:
                case LoopMode::alternateUntilRelease: return v.held || v.sustained;
                default: return true;
            }
        };

        // The LFOs, one value for each control step of this block, shared by the voices.
        const auto steps = (frames + control - 1) / control;
        std::array<std::array<double, maxSteps>, 2> lfoAt {};
        std::array<double, 2> lfoDepth {};   // how far the mod wheel lets each in
        for (size_t k = 0; k < lfos.size(); ++k)
        {
            const auto& o = s.lfo[k];
            auto& st = lfos[k];
            if (o.toPitch == 0.0 && o.toFilter == 0.0 && o.toVolume == 0.0 && o.toPan == 0.0)
                continue;
            const auto hz = o.sync ? s.tempo / 60.0 / std::max (o.beats, 1.0 / 64.0) : o.rate;
            const auto advanceBy = std::clamp (hz, 0.0, 200.0) * control / rate;
            lfoDepth[k] = 1.0 - std::clamp (o.modWheel, 0.0, 1.0) * (1.0 - wheel);
            const bool random = o.wave == Wave::sampleHold || o.wave == Wave::sampleHold2;
            for (int j = 0; j < steps; ++j)
            {
                if (random)
                {
                    // Shape glides from the last step to this one over that much of the cycle.
                    const auto glide = std::clamp (o.shape, 0.0, 1.0);
                    st.value = glide > 0.0 && st.phase < glide ? st.previous + (st.held - st.previous) * (st.phase / glide) : st.held;
                }
                else
                    st.value = waveAt (o.wave, st.phase, o.shape);
                lfoAt[k][(size_t) j] = st.value;
                st.phase += advanceBy;
                if (st.phase >= 1.0)
                {
                    st.phase -= std::floor (st.phase);
                    noise = noise * 1664525u + 1013904223u;
                    const auto r = (double) (noise >> 8) / 16777216.0;   // 0..1
                    st.previous = st.value;
                    st.high = ! st.high;
                    st.held = o.wave == Wave::sampleHold2 ? (st.high ? r : -r) : 2.0 * r - 1.0;
                }
            }
        }
        const auto dt = (double) control / rate;

        std::array<bool, 128> on {};
        for (size_t n = 0; n < voice.size(); ++n)
        {
            auto& v = voice[n];
            if (! v.active)
            {
                heads[n].store (-1.0, std::memory_order_relaxed);
                continue;
            }
            const auto velocityGain = (float) (1.0 - std::clamp (s.velocity, 0.0, 1.0) * (1.0 - (double) v.velocity * v.velocity));
            double step = 1.0, tremolo = 1.0;
            float leftGain = 1.0f, rightGain = 1.0f;
            FilterCoefficients first, second;
            for (int i = 0; i < frames && v.active; ++i)
            {
                if (i % control == 0)
                {
                    if (v.glideLeft > 0)
                    {
                        const auto by = std::min<Frame> (v.glideLeft, control);
                        v.pitch += v.glideStep * (double) by;
                        v.glideLeft -= by;
                        if (v.glideLeft == 0)
                            v.pitch = v.note;
                    }
                    // The envelopes and LFOs: semitones on the pitch, octaves on the cutoff.
                    const auto j = (size_t) (i / control);
                    double pitchBy = 0.0, octaves = 0.0, panBy = 0.0;
                    tremolo = 1.0;
                    for (int e = 0; e < 2; ++e)
                    {
                        const auto& o = e == 0 ? s.pitchEnvelope : s.filterEnvelope;
                        if (! o.on)
                            continue;
                        advance (v.envelope[e], o, dt);
                        const auto by = o.amount * v.envelope[e].level * (1.0 - std::clamp (o.velocity, 0.0, 1.0) * (1.0 - v.velocity));
                        (e == 0 ? pitchBy : octaves) += by;
                    }
                    for (size_t k = 0; k < lfos.size(); ++k)
                    {
                        const auto x = lfoAt[k][j], depth = lfoDepth[k];
                        pitchBy += s.lfo[k].toPitch * x * depth;
                        octaves += s.lfo[k].toFilter * x * depth;
                        panBy += s.lfo[k].toPan * x * depth;
                        tremolo *= 1.0 - std::clamp (s.lfo[k].toVolume, 0.0, 1.0) * depth * (0.5 - 0.5 * x);
                    }
                    const auto pan = std::clamp (s.pan + panBy, -1.0, 1.0);
                    leftGain = (float) (gain * std::min (1.0, 1.0 - pan));
                    rightGain = (float) (gain * std::min (1.0, 1.0 + pan));
                    const auto played = s.fixedPitch ? (double) s.rootNote : v.pitch;
                    step = std::pow (2.0, (played - s.rootNote + s.transpose + s.detune / 100.0 + bend * s.bendRange + pitchBy) / 12.0);
                    if (s.filter)
                    {
                        const auto cutoff = s.cutoff * std::pow (2.0, s.keyFollow * (v.pitch - s.rootNote) / 12.0
                                                                       - 4.0 * std::clamp (s.filterVelocity, 0.0, 1.0) * (1.0 - v.velocity) + octaves);
                        first = FilterCoefficients (cutoff, rate, k);
                        second = FilterCoefficients (cutoff, rate, std::sqrt (2.0));
                    }
                }

                double amp = 1.0;
                if (s.mode == Mode::oneShot)
                {
                    const auto played = v.position - begin;
                    if (fadeInFrames > 0.0)
                        amp = std::min (amp, played / fadeInFrames);
                    if (fadeOutFrames > 0.0)
                        amp = std::min (amp, (end - v.position) / fadeOutFrames);
                    if (v.stage == 3)
                    {
                        v.level -= gateStep;
                        if (v.level <= 0.0)
                        {
                            v.active = false;
                            break;
                        }
                        amp *= v.level;
                    }
                }
                else
                {
                    switch (v.stage)
                    {
                        case 0:
                            v.level += attackStep;
                            if (v.level >= 1.0)
                            {
                                v.level = 1.0;
                                v.stage = 1;
                            }
                            break;
                        case 1:
                            v.level = sustain + (v.level - sustain) * decayCoeff;
                            if (std::abs (v.level - sustain) < quiet)
                            {
                                v.level = sustain;
                                v.stage = 2;
                            }
                            break;
                        case 2:
                            v.level = sustain;
                            break;
                        default:
                            v.level *= releaseCoeff;
                            break;
                    }
                    amp = v.level;
                    if (v.stage == 3 && v.level < quiet)
                    {
                        v.active = false;
                        break;
                    }
                    if (v.stage == 2 && sustain <= 0.0)
                    {
                        v.active = false;
                        break;
                    }
                }

                // The sound here, blended towards the loop's start just before its end.
                const bool loopNow = loopOn (v);
                double l = sampleAt (data->left, v.position), r = sampleAt (data->right, v.position);
                if (loopNow && crossfade > 0.0 && v.forward && v.position > loopEnd - crossfade)
                {
                    const auto w = (v.position - (loopEnd - crossfade)) / crossfade;
                    const auto from = std::sqrt (1.0 - w), to = std::sqrt (w);
                    l = l * from + sampleAt (data->left, v.position - loopLength) * to;
                    r = r * from + sampleAt (data->right, v.position - loopLength) * to;
                }
                if (holdStep > 0.0)
                {
                    // Vintage: new values only at the lower rate, at 12 bits.
                    v.holdPhase += holdStep;
                    if (v.holdPhase >= 1.0)
                    {
                        v.holdPhase -= std::floor (v.holdPhase);
                        v.kept[0] = std::round ((float) l * 2048.0f) / 2048.0f;
                        v.kept[1] = std::round ((float) r * 2048.0f) / 2048.0f;
                    }
                    l = v.kept[0];
                    r = v.kept[1];
                }
                if (s.filter)
                {
                    if (s.drive > 0.0)
                    {
                        l = std::tanh (l * drive) * driveNorm;
                        r = std::tanh (r * drive) * driveNorm;
                    }
                    l = filtered (l, v.filter[0][0], first, s.filterType);
                    r = filtered (r, v.filter[0][1], first, s.filterType);
                    if (s.steep)
                    {
                        l = filtered (l, v.filter[1][0], second, s.filterType);
                        r = filtered (r, v.filter[1][1], second, s.filterType);
                    }
                }
                const auto a = (float) (std::max (0.0, amp) * tremolo) * velocityGain;
                left[i] += (float) l * a * leftGain;
                right[i] += (float) r * a * rightGain;

                if (v.forward)
                {
                    v.position += step;
                    if (loopNow && v.position >= loopEnd)
                    {
                        if (alternate)
                        {
                            v.position = std::max (loopStart, loopEnd - (v.position - loopEnd));
                            v.forward = false;
                        }
                        else
                            v.position -= loopLength;
                        ++v.loops;
                    }
                    else if (v.position >= end)
                        v.active = false;
                }
                else
                {
                    v.position -= step;
                    if (! loopNow)
                        v.forward = true;   // let go: on to the end
                    else if (v.position < loopStart)
                    {
                        v.position = std::min (loopEnd, loopStart + (loopStart - v.position));
                        v.forward = true;
                        ++v.loops;
                    }
                }
            }
            heads[n].store (v.active ? v.position : -1.0, std::memory_order_relaxed);
            if (v.active)
                on[(size_t) v.note] = true;
        }
        for (size_t n = 0; n < on.size(); ++n)
            sounding[n].store (on[n], std::memory_order_relaxed);
    }

    std::array<double, Engine::maxVoices> Engine::playheads() const
    {
        std::array<double, maxVoices> out {};
        for (size_t k = 0; k < out.size(); ++k)
            out[k] = heads[k].load (std::memory_order_relaxed);
        return out;
    }

    bool Engine::isSounding (int note) const
    {
        return note >= 0 && note < 128 && sounding[(size_t) note].load (std::memory_order_relaxed);
    }
}
