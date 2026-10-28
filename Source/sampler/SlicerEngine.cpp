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
        constexpr int half = numPads / 2;        // the 64 pads Push's grid shows at once

        // Push's 8x8 grid: the index (0..63) of a pad in its half to a note, and back.
        int gridNote (int local)
        {
            const auto bank = local / padsPerBank, index = local % padsPerBank;
            const auto row = (bank >= 2 ? 4 : 0) + index / 4, column = (bank % 2 == 1 ? 4 : 0) + index % 4;
            return 36 + row * 8 + column;
        }

        int gridPad (int note)
        {
            const auto n = note - 36;
            const auto row = n / 8, column = n % 8;
            const auto bank = (row >= 4 ? 2 : 0) + (column >= 4 ? 1 : 0);
            return bank * padsPerBank + (row % 4) * 4 + column % 4;
        }
    }

    bool Pad::used() const
    {
        for (const auto& l : layers)
            if (l.used)
                return true;
        return false;
    }

    int Pad::layerCount() const
    {
        int n = 0;
        for (const auto& l : layers)
            n += l.used ? 1 : 0;
        return n;
    }

    int Kit::receiveNote (int pad) const
    {
        if (pad < 0 || pad >= numPads)
            return -1;
        const auto r = pads[(size_t) pad].receive;
        return r >= 0 && r < 128 ? r : noteForPad (pad, layout, pushPage);
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
        size_t next = 0;
        for (auto& p : kit.pads)
        {
            auto& l = p.layers[0];
            if (l.used && l.sound != nullptr)
                continue;
            l.used = next < slices.size();
            if (l.used)
            {
                l.start = slices[next].first;
                l.end = slices[next].second;
                ++next;
            }
        }
    }

    void placeSound (Kit& kit, int pad, int layer, sampler::SamplePtr sound, const std::string& path)
    {
        if (pad < 0 || pad >= numPads || layer < 0 || layer >= maxLayers || sound == nullptr)
            return;
        auto& l = kit.pads[(size_t) pad].layers[(size_t) layer];
        l.used = true;
        l.start = 0;
        l.end = sound->length();
        l.sound = std::move (sound);
        l.path = path;
    }

    void swapPads (Kit& kit, int a, int b)
    {
        if (a < 0 || b < 0 || a >= numPads || b >= numPads || a == b)
            return;
        auto& x = kit.pads[(size_t) a];
        auto& y = kit.pads[(size_t) b];
        std::swap (x, y);
        std::swap (x.receive, y.receive);
    }

    int padForNote (int note, Layout layout, int page)
    {
        if (note < 0 || note > 127)
            return -1;
        if (layout == Layout::chromatic)
            return note;
        page = std::clamp (page, 0, 1);
        if (note >= 36 && note < 36 + half)
            return page * half + gridPad (note);
        return (1 - page) * half + (note >= 36 + half ? note - 36 - half : note + 128 - 36 - half);
    }

    int noteForPad (int pad, Layout layout, int page)
    {
        if (pad < 0 || pad >= numPads)
            return -1;
        if (layout == Layout::chromatic)
            return pad;
        page = std::clamp (page, 0, 1);
        const auto local = pad % half;
        if (pad / half == page)
            return gridNote (local);
        return local < 128 - 36 - half ? 36 + half + local : local - (128 - 36 - half);
    }

    void Engine::prepare (double sampleRate)
    {
        std::lock_guard g (lock);
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    }

    double Engine::sampleRate() const
    {
        std::lock_guard g (lock);
        return rate;
    }

    void Engine::setSample (sampler::SamplePtr s)
    {
        std::lock_guard g (lock);
        data = std::move (s);
        for (auto& v : voice)
            if (current.pads[(size_t) v.pad].layers[(size_t) v.layer].sound == nullptr)
                v.active = false;
    }

    void Engine::setKit (const Kit& k)
    {
        std::lock_guard g (lock);
        // A voice whose sound went away stops (the old sound is still held by the voice's layer until now).
        for (auto& v : voice)
            if (v.active && k.pads[(size_t) v.pad].layers[(size_t) v.layer].sound != current.pads[(size_t) v.pad].layers[(size_t) v.layer].sound)
                v.active = false;
        current = k;
        current.voices = std::clamp (k.voices, 1, maxVoices);
        rebuildNotes();
    }

    void Engine::rebuildNotes()
    {
        padsForNoteCount.fill (0);
        for (int pad = 0; pad < numPads; ++pad)
            if (const auto n = current.receiveNote (pad); n >= 0 && n < 128)
                padsForNote[(size_t) n][(size_t) padsForNoteCount[(size_t) n]++] = (std::uint8_t) pad;
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
        if (pad < 0 || pad >= numPads)
            return;
        const auto& p = current.pads[(size_t) pad];
        auto playable = [&] (const Layer& l)
        {
            const auto* s = l.sound != nullptr ? l.sound.get() : data.get();
            return l.used && s != nullptr && std::min (l.end, s->length()) - std::max<Frame> (0, l.start) >= 2;
        };
        bool any = false;
        for (const auto& l : p.layers)
            any = any || playable (l);
        if (! any)
            return;
        // The pad's own earlier hit and its choke group stop quickly.
        for (auto& v : voice)
            if (v.active && (v.pad == pad || (p.choke > 0 && current.pads[(size_t) v.pad].choke == p.choke)))
            {
                v.releasing = true;
                v.held = false;
                v.fastRelease = std::exp (-std::log (1000.0) / (chokeSeconds * rate));
            }
        for (int layer = 0; layer < maxLayers; ++layer)
        {
            const auto& l = p.layers[(size_t) layer];
            if (! playable (l))
                continue;
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
                    if (v.active && ! (v.pad == pad && ! v.releasing)
                        && (chosen == nullptr || (v.releasing && ! chosen->releasing) || (v.releasing == chosen->releasing && v.age < chosen->age)))
                        chosen = &v;
            if (chosen == nullptr)
                break;
            const auto length = (l.sound != nullptr ? l.sound : data)->length();
            *chosen = {};
            chosen->active = chosen->held = true;
            chosen->pad = pad;
            chosen->layer = layer;
            chosen->velocity = std::clamp (velocity, 0.0f, 1.0f);
            chosen->position = l.reverse ? (double) (std::min (l.end, length) - 1) : (double) std::max<Frame> (0, l.start);
            chosen->level = p.attack > 0.0 ? 0.0 : 1.0;
            chosen->age = ++clock;
        }
        last.store (pad, std::memory_order_relaxed);
    }

    void Engine::noteOn (int note, float velocity)
    {
        if (note < 0 || note > 127)
            return;
        std::array<std::uint8_t, numPads> pads;
        int count = 0;
        {
            std::lock_guard g (lock);
            pads = padsForNote[(size_t) note];
            count = padsForNoteCount[(size_t) note];
        }
        for (int i = 0; i < count; ++i)
        {
            const int pad = pads[(size_t) i];
            hit (pad, velocity);
        }
    }

    void Engine::noteOff (int note)
    {
        if (note < 0 || note > 127)
            return;
        std::array<std::uint8_t, numPads> pads;
        int count = 0;
        {
            std::lock_guard g (lock);
            pads = padsForNote[(size_t) note];
            count = padsForNoteCount[(size_t) note];
        }
        for (int i = 0; i < count; ++i)
        {
            const int pad = pads[(size_t) i];
            letGo (pad);
        }
    }

    void Engine::letGo (int pad)
    {
        std::lock_guard g (lock);
        if (pad < 0 || pad >= numPads)
            return;
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
        bool soloing = false;
        for (const auto& p : current.pads)
            soloing = soloing || p.solo;
        for (size_t k = 0; k < voice.size(); ++k)
        {
            auto& v = voice[k];
            const auto& p = current.pads[(size_t) v.pad];
            const auto& l = p.layers[(size_t) v.layer];
            const auto* s = l.sound != nullptr ? l.sound.get() : data.get();
            if (v.active && (s == nullptr || ! l.used))
                v.active = false;
            if (! v.active)
            {
                heads[k].store (-1.0, std::memory_order_relaxed);
                continue;
            }
            const auto length = s->length();
            const auto begin = (double) std::clamp<Frame> (l.start, 0, length);
            const auto end = (double) std::clamp<Frame> (l.end, (Frame) begin, length);
            const auto step = std::pow (2.0, (p.pitch + l.pitch) / 12.0) * (l.reverse ? -1.0 : 1.0);
            const bool audible = ! p.mute && (! soloing || p.solo);
            const auto gain = (audible ? std::pow (10.0, (p.gainDb + l.gainDb) / 20.0) : 0.0)
                            * (1.0 - std::clamp (current.velocity, 0.0, 1.0) * (1.0 - (double) v.velocity * v.velocity));
            const auto leftGain = (float) (gain * std::min (1.0, 1.0 - p.pan)), rightGain = (float) (gain * std::min (1.0, 1.0 + p.pan));
            const auto attackStep = p.attack > 0.0 ? 1.0 / (p.attack * rate) : 1.0;
            const auto releaseCoeff = std::exp (-std::log (1000.0) / (std::max (0.001, p.release) * rate));
            const auto tailFrames = std::max (1.0, std::max (std::min (p.release, 0.005), l.fadeOut) * rate);   // a fade into the end
            const auto fadeInFrames = l.fadeIn * rate;
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
                const auto toEnd = l.reverse ? v.position - begin : end - v.position;     // frames to the end
                const auto fromStart = l.reverse ? end - v.position : v.position - begin;  // frames played
                auto shape = std::min (1.0, toEnd / tailFrames);
                if (fadeInFrames >= 1.0)
                    shape *= std::min (1.0, fromStart / fadeInFrames);
                const auto i0 = (Frame) v.position;
                const auto t = (float) (v.position - (double) i0);
                auto at = [&] (const std::vector<float>& x, Frame n) { return n >= 0 && n < length ? x[(size_t) n] : 0.0f; };
                const auto lo = at (s->left, i0) + t * (at (s->left, i0 + 1) - at (s->left, i0));
                const auto ro = at (s->right, i0) + t * (at (s->right, i0 + 1) - at (s->right, i0));
                const auto a = (float) (v.level * shape);
                left[i] += lo * a * leftGain;
                right[i] += ro * a * rightGain;
                v.position += step;
                if (v.position >= end || v.position < begin)
                {
                    v.active = false;
                    break;
                }
            }
            heads[k].store (v.active ? v.position : -1.0, std::memory_order_relaxed);
            headPads[k].store (l.sound != nullptr && v.layer == 0 ? v.pad : -1, std::memory_order_relaxed);
            if (v.active && l.sound != nullptr && v.layer != 0)
                heads[k].store (-1.0, std::memory_order_relaxed);   // only the first layer is drawn
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

    std::vector<double> Engine::playheads (int pad) const
    {
        std::vector<double> out;
        for (size_t k = 0; k < heads.size(); ++k)
            if (const auto v = heads[k].load (std::memory_order_relaxed); v >= 0.0 && headPads[k].load (std::memory_order_relaxed) == pad)
                out.push_back (v);
        return out;
    }
}
