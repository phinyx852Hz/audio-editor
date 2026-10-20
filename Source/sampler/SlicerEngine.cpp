// SPDX-License-Identifier: AGPL-3.0-only
#include "SlicerEngine.h"

#include <algorithm>
#include <cmath>

namespace slicer
{
    namespace
    {
        constexpr double chokeSeconds = 0.005;   // how fast a choked or retriggered voice stops
        constexpr double quiet = 1.0e-4;
    }

    std::vector<std::pair<Frame, Frame>> slicesFromCuts (std::vector<Frame> cuts, Frame length, Frame minGap)
    {
        cuts.push_back (0);
        cuts.push_back (length);
        std::sort (cuts.begin(), cuts.end());
        cuts.erase (std::remove_if (cuts.begin(), cuts.end(), [length] (Frame f) { return f < 0 || f > length; }), cuts.end());
        cuts.erase (std::unique (cuts.begin(), cuts.end()), cuts.end());
        std::vector<std::pair<Frame, Frame>> slices;
        for (size_t i = 0; i + 1 < cuts.size(); ++i)
        {
            if (! slices.empty() && slices.back().second - slices.back().first < minGap)
                slices.back().second = cuts[i + 1];
            else
                slices.emplace_back (cuts[i], cuts[i + 1]);
        }
        if (slices.size() > 1 && slices.back().second - slices.back().first < minGap)
        {
            const auto last = slices.back();
            slices.pop_back();
            slices.back().second = last.second;
        }
        return slices;
    }

    std::vector<std::pair<Frame, Frame>> equalSlices (Frame length, int count)
    {
        std::vector<std::pair<Frame, Frame>> slices;
        count = std::clamp (count, 1, numPads);
        for (int i = 0; i < count; ++i)
            slices.emplace_back (length * i / count, length * (i + 1) / count);
        return slices;
    }

    void assign (Kit& kit, const std::vector<std::pair<Frame, Frame>>& slices)
    {
        for (int i = 0; i < numPads; ++i)
        {
            auto& p = kit.pads[(size_t) i];
            p.used = i < (int) slices.size();
            if (p.used)
            {
                p.start = slices[(size_t) i].first;
                p.end = slices[(size_t) i].second;
            }
        }
    }

    int padForNote (int note, Layout layout)
    {
        const auto n = note - 36;
        if (n < 0 || n >= numPads)
            return -1;
        if (layout == Layout::chromatic)
            return n;
        const auto row = n / 8, column = n % 8;
        const auto bank = (row >= 4 ? 2 : 0) + (column >= 4 ? 1 : 0);
        return bank * padsPerBank + (row % 4) * 4 + column % 4;
    }

    int noteForPad (int pad, Layout layout)
    {
        if (pad < 0 || pad >= numPads)
            return -1;
        if (layout == Layout::chromatic)
            return 36 + pad;
        const auto bank = pad / padsPerBank, index = pad % padsPerBank;
        const auto row = (bank >= 2 ? 4 : 0) + index / 4, column = (bank % 2 == 1 ? 4 : 0) + index % 4;
        return 36 + row * 8 + column;
    }

    void Engine::prepare (double sampleRate)
    {
        std::lock_guard g (lock);
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    }

    void Engine::setSample (sampler::SamplePtr s)
    {
        std::lock_guard g (lock);
        data = std::move (s);
        for (auto& v : voice)
            v.active = false;
    }

    void Engine::setKit (const Kit& k)
    {
        std::lock_guard g (lock);
        current = k;
        current.voices = std::clamp (k.voices, 1, maxVoices);
    }

    Kit Engine::kit() const
    {
        std::lock_guard g (lock);
        return current;
    }

    sampler::SamplePtr Engine::sample() const
    {
        std::lock_guard g (lock);
        return data;
    }

    void Engine::hit (int pad, float velocity)
    {
        std::lock_guard g (lock);
        if (data == nullptr || pad < 0 || pad >= numPads || ! current.pads[(size_t) pad].used)
            return;
        const auto& p = current.pads[(size_t) pad];
        if (p.end - p.start < 2)
            return;
        // The pad's own earlier hit and its choke group stop quickly.
        for (auto& v : voice)
            if (v.active && (v.pad == pad || (p.choke > 0 && current.pads[(size_t) v.pad].choke == p.choke)))
            {
                v.releasing = true;
                v.held = false;
                v.fastRelease = std::exp (-std::log (1000.0) / (chokeSeconds * rate));
            }
        int used = 0;
        for (const auto& v : voice)
            used += v.active ? 1 : 0;
        Voice* chosen = nullptr;
        for (auto& v : voice)
            if (! v.active && used < current.voices)
            {
                chosen = &v;
                break;
            }
        if (chosen == nullptr)
            for (auto& v : voice)
                if (v.active && (chosen == nullptr || (v.releasing && ! chosen->releasing) || (v.releasing == chosen->releasing && v.age < chosen->age)))
                    chosen = &v;
        if (chosen == nullptr)
            return;
        *chosen = {};
        chosen->active = chosen->held = true;
        chosen->pad = pad;
        chosen->velocity = std::clamp (velocity, 0.0f, 1.0f);
        chosen->position = p.reverse ? (double) (p.end - 1) : (double) p.start;
        chosen->level = p.attack > 0.0 ? 0.0 : 1.0;
        chosen->age = ++clock;
        last.store (pad, std::memory_order_relaxed);
    }

    void Engine::noteOn (int note, float velocity)
    {
        Layout layout;
        {
            std::lock_guard g (lock);
            layout = current.layout;
        }
        hit (padForNote (note, layout), velocity);
    }

    void Engine::noteOff (int note)
    {
        Layout layout;
        {
            std::lock_guard g (lock);
            layout = current.layout;
        }
        if (const auto pad = padForNote (note, layout); pad >= 0)
            letGo (pad);
    }

    void Engine::letGo (int pad)
    {
        std::lock_guard g (lock);
        for (auto& v : voice)
            if (v.active && v.held && v.pad == pad)
            {
                v.held = false;
                if (current.pads[(size_t) pad].play == Play::gate)
                    v.releasing = true;
            }
    }

    void Engine::panic()
    {
        std::lock_guard g (lock);
        for (auto& v : voice)
            v.active = false;
    }

    void Engine::render (float* left, float* right, int frames)
    {
        std::lock_guard g (lock);
        std::array<bool, numPads> on {};
        if (data == nullptr)
        {
            for (auto& s : sounding)
                s.store (false, std::memory_order_relaxed);
            for (auto& h : heads)
                h.store (-1.0, std::memory_order_relaxed);
            return;
        }
        const auto length = data->length();
        for (size_t k = 0; k < voice.size(); ++k)
        {
            auto& v = voice[k];
            if (! v.active)
            {
                heads[k].store (-1.0, std::memory_order_relaxed);
                continue;
            }
            const auto& p = current.pads[(size_t) v.pad];
            const auto begin = (double) std::clamp<Frame> (p.start, 0, length);
            const auto end = (double) std::clamp<Frame> (p.end, (Frame) begin, length);
            const auto step = std::pow (2.0, p.pitch / 12.0) * (p.reverse ? -1.0 : 1.0);
            const auto gain = std::pow (10.0, p.gainDb / 20.0)
                            * (1.0 - std::clamp (current.velocity, 0.0, 1.0) * (1.0 - (double) v.velocity * v.velocity));
            const auto leftGain = (float) (gain * std::min (1.0, 1.0 - p.pan)), rightGain = (float) (gain * std::min (1.0, 1.0 + p.pan));
            const auto attackStep = p.attack > 0.0 ? 1.0 / (p.attack * rate) : 1.0;
            const auto releaseCoeff = std::exp (-std::log (1000.0) / (std::max (0.001, p.release) * rate));
            const auto tailFrames = std::max (1.0, std::min (p.release, 0.005) * rate);   // a short fade into the end of the slice
            for (int i = 0; i < frames; ++i)
            {
                if (v.releasing)
                {
                    v.level *= v.fastRelease > 0.0 ? v.fastRelease : releaseCoeff;
                    if (v.level < quiet)
                    {
                        v.active = false;
                        break;
                    }
                }
                else if (v.level < 1.0)
                    v.level = std::min (1.0, v.level + attackStep);
                const auto left_ = p.reverse ? v.position - begin : end - v.position;   // frames to the end of the slice
                const auto tail = std::min (1.0, left_ / tailFrames);
                const auto i0 = (Frame) v.position;
                const auto t = (float) (v.position - (double) i0);
                auto at = [&] (const std::vector<float>& x, Frame n) { return n >= 0 && n < length ? x[(size_t) n] : 0.0f; };
                const auto l = at (data->left, i0) + t * (at (data->left, i0 + 1) - at (data->left, i0));
                const auto r = at (data->right, i0) + t * (at (data->right, i0 + 1) - at (data->right, i0));
                const auto a = (float) (v.level * tail);
                left[i] += l * a * leftGain;
                right[i] += r * a * rightGain;
                v.position += step;
                if (v.position >= end || v.position < begin)
                {
                    v.active = false;
                    break;
                }
            }
            heads[k].store (v.active ? v.position : -1.0, std::memory_order_relaxed);
            if (v.active)
                on[(size_t) v.pad] = true;
        }
        for (size_t n = 0; n < on.size(); ++n)
            sounding[n].store (on[n], std::memory_order_relaxed);
    }

    bool Engine::isSounding (int pad) const
    {
        return pad >= 0 && pad < numPads && sounding[(size_t) pad].load (std::memory_order_relaxed);
    }

    std::vector<double> Engine::playheads() const
    {
        std::vector<double> out;
        for (const auto& h : heads)
            if (const auto v = h.load (std::memory_order_relaxed); v >= 0.0)
                out.push_back (v);
        return out;
    }
}
