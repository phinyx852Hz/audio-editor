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

    void Engine::hit (int pad, float velocity, double semitones)
    {
        std::lock_guard g (lock);
        hitLocked (pad, velocity, semitones);
        // Recording: the hit lands on the nearest step of the pad's lane.
        if (recording.load() && running.load() && pad >= 0 && pad < numPads)
        {
            auto& pattern = steps->patterns[(size_t) playingIndex];
            auto& lane = pattern.lanes[(size_t) pad];
            const auto k = (int64_t) std::llround ((beat - origin) / rateBeats (lane.rate));
            auto& s = lane.step[(size_t) stepIndex (lane, k)];
            s.on = true;
            s.velocity = (uint8_t) std::clamp ((int) std::lround (velocity * 127.0f), 1, 127);
            laneUsed[(size_t) pad] = true;
            recordCount.fetch_add (1);
        }
    }

    void Engine::hitLocked (int pad, float velocity, double semitones, bool played)
    {
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
            chosen->pitch = semitones;
            chosen->position = l.reverse ? (double) (std::min (l.end, length) - 1) : (double) std::max<Frame> (0, l.start);
            chosen->level = p.attack > 0.0 ? 0.0 : 1.0;
            chosen->age = ++clock;
        }
        if (played)
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
        letGoLocked (pad);
    }

    void Engine::letGoLocked (int pad)
    {
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
        sequence (left, right, frames);
    }

    void Engine::renderVoices (float* left, float* right, int frames)
    {
        if (frames <= 0)
            return;
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
            const auto step = std::pow (2.0, (p.pitch + l.pitch + v.pitch) / 12.0) * (l.reverse ? -1.0 : 1.0);
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

    //==========================================================================
    // The step sequencer.

    std::unique_ptr<Song> Engine::song() const
    {
        auto copy = std::make_unique<Song>();
        std::lock_guard g (lock);
        *copy = *steps;
        return copy;
    }

    void Engine::setSong (const Song& s)
    {
        std::lock_guard g (lock);
        *steps = s;
        steps->bpm = std::clamp (s.bpm, 20.0, 400.0);
        steps->beatsPerBar = std::clamp (s.beatsPerBar, 1, 16);
        steps->chainLength = std::clamp (s.chainLength, 0, maxChain);
        playingIndex = std::clamp (playingIndex, 0, numPatterns - 1);
        for (int i = 0; i < numLanes; ++i)
            laneUsed[(size_t) i] = ! steps->patterns[(size_t) playingIndex].lanes[(size_t) i].empty();
    }

    Pattern Engine::pattern (int index) const
    {
        std::lock_guard g (lock);
        return steps->patterns[(size_t) std::clamp (index, 0, numPatterns - 1)];
    }

    void Engine::setPattern (int index, const Pattern& p)
    {
        std::lock_guard g (lock);
        index = std::clamp (index, 0, numPatterns - 1);
        steps->patterns[(size_t) index] = p;
        if (index == playingIndex)
            for (int i = 0; i < numLanes; ++i)
                laneUsed[(size_t) i] = ! p.lanes[(size_t) i].empty();
    }

    void Engine::setLane (int index, int lane, const Lane& l)
    {
        std::lock_guard g (lock);
        if (index < 0 || index >= numPatterns || lane < 0 || lane >= numLanes)
            return;
        steps->patterns[(size_t) index].lanes[(size_t) lane] = l;
        if (index == playingIndex)
            laneUsed[(size_t) lane] = ! l.empty();
    }

    void Engine::setTempo (double bpm)
    {
        std::lock_guard g (lock);
        steps->bpm = std::clamp (bpm, 20.0, 400.0);
    }

    double Engine::tempo() const
    {
        std::lock_guard g (lock);
        return steps->bpm;
    }

    std::vector<int> Engine::chain() const
    {
        std::lock_guard g (lock);
        return { steps->chain.begin(), steps->chain.begin() + steps->chainLength };
    }

    bool Engine::chainOn() const
    {
        std::lock_guard g (lock);
        return steps->chainOn;
    }

    void Engine::setBars (int index, int bars)
    {
        std::lock_guard g (lock);
        if (index >= 0 && index < numPatterns)
            steps->patterns[(size_t) index].bars = (uint8_t) std::clamp (bars, 1, 16);
    }

    void Engine::setSwing (int index, int swing)
    {
        std::lock_guard g (lock);
        if (index >= 0 && index < numPatterns)
            steps->patterns[(size_t) index].swing = (uint8_t) std::clamp (swing, 0, 100);
    }

    void Engine::setChain (const std::vector<int>& patterns, bool on)
    {
        std::lock_guard g (lock);
        steps->chainLength = 0;
        for (const auto p : patterns)
            if (p >= 0 && p < numPatterns && steps->chainLength < maxChain)
                steps->chain[(size_t) steps->chainLength++] = (int8_t) p;
        steps->chainOn = on && steps->chainLength > 0;
        chainAt = 0;
    }

    void Engine::play()
    {
        std::lock_guard g (lock);
        if (follow || running.load())
            return;
        beat = origin = 0.0;
        plannedCount = 0;
        chainAt = 0;
        if (steps->chainOn && steps->chainLength > 0)
            switchPattern (steps->chain[0], 0.0);
        else if (const auto q = patternQueued.exchange (-1); q >= 0)
            switchPattern (q, 0.0);
        running.store (true);
    }

    void Engine::stop()
    {
        std::lock_guard g (lock);
        if (follow)
            return;
        running.store (false);
        releaseSequenced();
    }

    void Engine::setClock (Clock c)
    {
        std::lock_guard g (lock);
        follow = std::move (c);
        following = false;
        running.store (false);
        releaseSequenced();
    }

    bool Engine::followsClock() const
    {
        std::lock_guard g (lock);
        return (bool) follow;
    }

    void Engine::queuePattern (int index)
    {
        std::lock_guard g (lock);
        index = std::clamp (index, 0, numPatterns - 1);
        if (running.load())
            patternQueued.store (index);
        else
            switchPattern (index, beat);
    }

    void Engine::switchPattern (int index, double at)
    {
        playingIndex = std::clamp (index, 0, numPatterns - 1);
        origin = at;
        patternNow.store (playingIndex);
        for (int i = 0; i < numLanes; ++i)
            laneUsed[(size_t) i] = ! steps->patterns[(size_t) playingIndex].lanes[(size_t) i].empty();
    }

    // Stopped: what the sequencer was holding is let go, what it had coming is dropped.
    void Engine::releaseSequenced()
    {
        for (int i = 0; i < plannedCount; ++i)
            if (planned[(size_t) i].velocity <= 0.0f)
                letGoLocked (planned[(size_t) i].pad);
        plannedCount = 0;
    }

    void Engine::schedule (double from, double to)
    {
        const auto& pattern = steps->patterns[(size_t) playingIndex];
        bool soloing = false;
        for (int i = 0; i < numLanes; ++i)
            soloing = soloing || (laneUsed[(size_t) i] && pattern.lanes[(size_t) i].solo);
        auto plan = [this] (double at, int pad, float velocity, double pitch)
        {
            if (plannedCount < maxPlanned)
                planned[(size_t) plannedCount++] = { at, pad, velocity, pitch };
        };
        for (int lane = 0; lane < numLanes; ++lane)
        {
            if (! laneUsed[(size_t) lane])
                continue;
            const auto& l = pattern.lanes[(size_t) lane];
            if (l.mute || (soloing && ! l.solo))
                continue;
            const auto length = rateBeats (l.rate);
            // Swing and shift move a step by at most a step, so a step either side is enough.
            const auto first = (int64_t) std::floor ((from - origin) / length) - 1;
            const auto last = (int64_t) std::ceil ((to - origin) / length) + 1;
            for (auto k = std::max<int64_t> (0, first); k <= last; ++k)
            {
                const auto at = origin + stepTime (l, pattern, k);
                if (at < from || at >= to)
                    continue;
                const auto& s = l.step[(size_t) stepIndex (l, k)];
                if (! s.on)
                    continue;
                if (s.chance < 100)
                {
                    random ^= random << 13;
                    random ^= random >> 17;
                    random ^= random << 5;
                    if ((int) (random % 100u) >= s.chance)
                        continue;
                }
                const auto repeats = std::clamp<int> (s.repeats, 1, maxRepeats);
                const auto each = length / repeats;
                for (int r = 0; r < repeats; ++r)
                {
                    const auto ramp = repeats > 1 ? s.ramp / 100.0 * r / (repeats - 1) : 0.0;
                    const auto velocity = (float) std::clamp (s.velocity / 127.0 * (1.0 + ramp), 0.01, 1.0);
                    const auto hitAt = at + r * each;
                    plan (hitAt, lane, velocity, s.pitch);
                    plan (hitAt + each * std::clamp<int> (s.gate, 1, 100) / 100.0, lane, 0.0f, 0.0);
                }
            }
        }
    }

    void Engine::sequence (float* left, float* right, int frames)
    {
        double bpm = steps->bpm, from = beat;
        if (follow)
        {
            double at = 0.0, tempo = 120.0;
            const bool runs = follow (at, tempo);
            if (! runs)
            {
                if (following)
                    releaseSequenced();
                following = false;
                running.store (false);
                renderVoices (left, right, frames);
                return;
            }
            bpm = std::clamp (tempo, 20.0, 400.0);
            // Started, or moved: the patterns count from the clock's beat 0 (or the chain's start).
            // The clock may be read more often than it moves (it ticks a block at a time, and this
            // may render in parts of a block), so only a new reading far from here is a move.
            const auto slack = std::max (0.1, 8192.0 * bpm / 60.0 / rate);
            const bool moved = ! following || (at != clockWas && std::abs (at - beat) > slack);
            clockWas = at;
            if (! moved)
                at = beat;
            else
            {
                releaseSequenced();
                chainAt = 0;
                const auto start = steps->chainOn && steps->chainLength > 0 ? (int) steps->chain[0] : playingIndex;
                switchPattern (start, 0.0);
                if (steps->chainOn && steps->chainLength > 0 && at > 0.0)
                {
                    // Into the chain as far as the clock has got.
                    double t = 0.0;
                    for (;;)
                    {
                        const auto bars = steps->patterns[(size_t) playingIndex].bars * steps->beatsPerBar;
                        if (t + bars > at)
                            break;
                        t += bars;
                        chainAt = (chainAt + 1) % steps->chainLength;
                        switchPattern (steps->chain[(size_t) chainAt], t);
                    }
                }
            }
            following = true;
            running.store (true);
            from = at;
        }
        else if (! running.load())
        {
            renderVoices (left, right, frames);
            return;
        }
        const auto perFrame = bpm / 60.0 / rate;
        const auto to = from + frames * perFrame;

        // Plan the steps that start in this block, a pattern at a time: a change of pattern
        // (queued, or the chain's next) happens on a bar line.
        for (auto at = from; at < to;)
        {
            const auto bar = (double) steps->beatsPerBar;
            auto next = to;
            int change = -1;
            if (const auto q = patternQueued.load(); q >= 0)
            {
                const auto line = std::ceil (at / bar - 1.0e-9) * bar;
                if (line < to)
                {
                    next = line;
                    change = q;
                }
            }
            else if (steps->chainOn && steps->chainLength > 0)
            {
                const auto end = origin + steps->patterns[(size_t) playingIndex].bars * bar;
                if (end < to)
                {
                    next = std::max (at, end);
                    change = steps->chain[(size_t) ((chainAt + 1) % steps->chainLength)];
                }
            }
            schedule (at, next);
            if (change < 0)
                break;
            if (patternQueued.load() >= 0)
                patternQueued.store (-1);
            else
                chainAt = (chainAt + 1) % steps->chainLength;
            switchPattern (change, next);
            at = next;
        }

        // Play what falls in this block, in order, rendering the voices between.
        std::sort (planned.begin(), planned.begin() + plannedCount, [] (const Planned& a, const Planned& b)
        {
            return a.beat < b.beat || (a.beat == b.beat && a.velocity < b.velocity);   // let-goes first
        });
        int done = 0, rendered = 0;
        for (; done < plannedCount && planned[(size_t) done].beat < to; ++done)
        {
            const auto& e = planned[(size_t) done];
            // The first frame at or after the step (a hair's leeway for the beat's rounding).
            const auto offset = std::clamp ((int) std::ceil ((e.beat - from) / perFrame - 1.0e-6), rendered, frames);
            renderVoices (left + rendered, right + rendered, offset - rendered);
            rendered = offset;
            if (e.velocity > 0.0f)
                hitLocked (e.pad, e.velocity, e.pitch, false);
            else
                letGoLocked (e.pad);
        }
        renderVoices (left + rendered, right + rendered, frames - rendered);
        std::move (planned.begin() + done, planned.begin() + plannedCount, planned.begin());
        plannedCount -= done;
        beat = to;
        beatNow.store (to - origin, std::memory_order_relaxed);
    }
}
