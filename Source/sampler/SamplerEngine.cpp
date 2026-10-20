// SPDX-License-Identifier: AGPL-3.0-only
#include "SamplerEngine.h"

#include <algorithm>
#include <cmath>

namespace sampler
{
    namespace
    {
        constexpr double quiet = 1.0e-4;   // -80 dB: a releasing voice is done
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

    void Engine::start (Voice& v, int note, float velocity)
    {
        v = {};
        v.active = v.held = true;
        v.note = note;
        v.velocity = std::clamp (velocity, 0.0f, 1.0f);
        v.position = (double) std::max<Frame> (0, current.start);
        v.stage = current.attack > 0.0 ? 0 : 1;
        v.level = current.attack > 0.0 ? 0.0 : 1.0;
        v.age = ++clock;
    }

    void Engine::releaseVoice (Voice& v)
    {
        v.held = false;
        if (pedal)
        {
            v.sustained = true;
            return;
        }
        if (current.mode == Mode::classic)
            v.stage = 3;
    }

    void Engine::noteOn (int note, float velocity)
    {
        if (velocity <= 0.0f)
        {
            noteOff (note);
            return;
        }
        std::lock_guard g (lock);
        if (data == nullptr || note < 0 || note > 127)
            return;
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
            start (*chosen, note, velocity);
    }

    void Engine::noteOff (int note)
    {
        std::lock_guard g (lock);
        for (auto& v : voice)
            if (v.active && v.held && v.note == note)
                releaseVoice (v);
    }

    void Engine::allNotesOff()
    {
        std::lock_guard g (lock);
        pedal = false;
        for (auto& v : voice)
            if (v.active)
            {
                v.held = v.sustained = false;
                v.stage = 3;
            }
    }

    void Engine::panic()
    {
        std::lock_guard g (lock);
        pedal = false;
        for (auto& v : voice)
            v.active = false;
    }

    void Engine::pitchBend (double amount)
    {
        std::lock_guard g (lock);
        bend = std::clamp (amount, -1.0, 1.0);
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
                    if (current.mode == Mode::classic)
                        v.stage = 3;
                }
    }

    float Engine::sampleAt (const std::vector<float>& x, double position) const
    {
        // Cubic (Catmull-Rom) between the neighbours, so transposed notes stay smooth.
        const auto n = (Frame) x.size();
        const auto i = (Frame) std::floor (position);
        const auto t = (float) (position - (double) i);
        auto at = [&] (Frame k) { return k >= 0 && k < n ? x[(size_t) k] : 0.0f; };
        const auto a = at (i - 1), b = at (i), c = at (i + 1), d = at (i + 2);
        return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
    }

    void Engine::render (float* left, float* right, int frames)
    {
        std::lock_guard g (lock);
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
        const bool looping = s.mode == Mode::classic && s.loop && loopEnd - loopStart >= 2.0;
        const auto gain = std::pow (10.0, s.gainDb / 20.0);
        const auto leftGain = (float) (gain * std::min (1.0, 1.0 - s.pan)), rightGain = (float) (gain * std::min (1.0, 1.0 + s.pan));
        const auto attackStep = s.attack > 0.0 ? 1.0 / (s.attack * rate) : 1.0;
        const auto decayCoeff = fall (s.decay, rate), releaseCoeff = fall (s.release, rate);
        const auto sustain = std::clamp (s.sustain, 0.0, 1.0);
        const auto fadeInFrames = s.fadeIn * rate, fadeOutFrames = s.fadeOut * rate;

        std::array<bool, 128> on {};
        for (size_t k = 0; k < voice.size(); ++k)
        {
            auto& v = voice[k];
            if (! v.active)
            {
                heads[k].store (-1.0, std::memory_order_relaxed);
                continue;
            }
            const auto step = std::pow (2.0, ((double) (v.note - s.rootNote) + s.transpose + s.detune / 100.0 + bend * s.bendRange) / 12.0);
            const auto velocityGain = (float) (1.0 - std::clamp (s.velocity, 0.0, 1.0) * (1.0 - (double) v.velocity * v.velocity));
            for (int i = 0; i < frames && v.active; ++i)
            {
                double amp = 1.0;
                if (s.mode == Mode::oneShot)
                {
                    const auto played = v.position - begin;
                    if (fadeInFrames > 0.0)
                        amp = std::min (amp, played / fadeInFrames);
                    if (fadeOutFrames > 0.0)
                        amp = std::min (amp, (end - v.position) / fadeOutFrames);
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
                const auto a = (float) std::max (0.0, amp) * velocityGain;
                left[i] += sampleAt (data->left, v.position) * a * leftGain;
                right[i] += sampleAt (data->right, v.position) * a * rightGain;
                v.position += step;
                if (looping && v.position >= loopEnd)
                    v.position -= loopEnd - loopStart;
                else if (v.position >= end)
                    v.active = false;
            }
            heads[k].store (v.active ? v.position : -1.0, std::memory_order_relaxed);
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
