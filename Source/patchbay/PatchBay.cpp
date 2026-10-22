// SPDX-License-Identifier: AGPL-3.0-only
#include "PatchBay.h"
#include "../sampler/SamplerEngine.h"
#include "../sampler/SlicerEngine.h"

#include <algorithm>
#include <cmath>

namespace patchbay
{
    namespace
    {
        float dbToGain (float db) { return db < PatchBay::minDb ? 0.0f : std::pow (10.0f, db / 20.0f); }

        // Adds `in` times a gain ramped across the block, so moving a gain never clicks.
        void addRamped (float* out, const float* in, int frames, float from, float to)
        {
            if (std::abs (to - from) < 1.0e-9f)
            {
                if (std::abs (to) > 1.0e-9f)
                    for (int i = 0; i < frames; ++i)
                        out[i] += in[i] * to;
                return;
            }
            const auto step = (to - from) / (float) frames;
            for (int i = 0; i < frames; ++i)
                out[i] += in[i] * (from + step * (float) (i + 1));
        }
    }

    Kind kindOf (int n)
    {
        if (n == node::editor)   return Kind::editor;
        if (n == node::sampler)  return Kind::sampler;
        if (n == node::slicer)   return Kind::slicer;
        if (n < node::track)     return Kind::synth;
        if (n < node::mixBus)    return Kind::track;
        if (n == node::mixBus)   return Kind::mixBus;
        if (n < node::mainOut)   return Kind::fxBus;
        return Kind::mainOut;
    }

    int indexOf (int n)
    {
        switch (kindOf (n))
        {
            case Kind::synth: return n - node::synth;
            case Kind::track: return n - node::track;
            case Kind::fxBus: return n - node::fxBus;
            case Kind::editor:
            case Kind::sampler:
            case Kind::slicer:
            case Kind::mixBus:
            case Kind::mainOut: return 0;
        }
        return 0;
    }

    bool hasOutput (int n) { return n >= 0 && n < numNodes && n != node::mainOut; }

    bool hasInput (int n)
    {
        if (n < 0 || n >= numNodes)
            return false;
        const auto k = kindOf (n);
        return k == Kind::track || k == Kind::mixBus || k == Kind::fxBus || k == Kind::mainOut;
    }

    std::string defaultName (int n)
    {
        const auto number = std::to_string (indexOf (n) + 1);
        switch (kindOf (n))
        {
            case Kind::editor:  return "Editor";
            case Kind::sampler: return "Sampler";
            case Kind::slicer:  return "Slicer";
            case Kind::synth:   return "Synth " + number;
            case Kind::track:   return "Track " + number;
            case Kind::mixBus:  return "Mix Bus";
            case Kind::fxBus:   return "FX Bus " + number;
            case Kind::mainOut: return "Main Out";
        }
        return {};
    }

    //==========================================================================
    PatchBay::PatchBay (sampler::Engine& s, slicer::Engine& c, mixer::Engine* m)
        : samplerEngine (s), slicerEngine (c), mix (m)
    {
        for (auto& g : gains)
            g = 0.0f;
        for (auto& p : peaks)
            p = 0.0f;
        buffers.assign ((size_t) numNodes * 2 * block, 0.0f);
        for (auto* v : { &trackInL, &trackInR })
            v->assign ((size_t) mixer::maxTracks * block, 0.0f);
        for (auto* v : { &scratchL, &scratchR })
            v->assign ((size_t) block, 0.0f);
        for (auto& q : liveQueue)
            q.reserve (256);
        for (auto& q : liveTaken)
            q.reserve (256);
        setLinks (defaultLinks());
        if (mix != nullptr)
            mix->setPatch (this);
    }

    PatchBay::~PatchBay()
    {
        if (mix != nullptr)
            mix->setPatch (nullptr);
    }

    void PatchBay::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& s : synthSlots)
            if (s != nullptr)
                s->prepare (rate.load(), block);
        for (auto& e : effectSlots)
            if (e != nullptr)
                e->prepare (rate.load(), block);
        const std::lock_guard<std::mutex> l (lock);
        fresh = true;
    }

    //==========================================================================
    bool PatchBay::linked (int from, int to) const
    {
        return from >= 0 && to >= 0 && from < numNodes && to < numNodes && edges[(size_t) from][(size_t) to];
    }

    float PatchBay::gainDb (int from, int to) const
    {
        if (from < 0 || to < 0 || from >= numNodes || to >= numNodes)
            return 0.0f;
        return gains[(size_t) (from * numNodes + to)].load();
    }

    bool PatchBay::reaches (int from, int to) const
    {
        std::array<bool, numNodes> seen {};
        std::vector<int> stack { from };
        while (! stack.empty())
        {
            const auto n = stack.back();
            stack.pop_back();
            if (n == to)
                return true;
            if (seen[(size_t) n])
                continue;
            seen[(size_t) n] = true;
            for (int next = 0; next < numNodes; ++next)
                if (edges[(size_t) n][(size_t) next])
                    stack.push_back (next);
        }
        return false;
    }

    Refusal PatchBay::check (int from, int to) const
    {
        if (from == to)
            return Refusal::self;
        if (! hasOutput (from) || ! hasInput (to))
            return Refusal::noPort;
        if (linked (from, to))
            return Refusal::none;
        // to already feeds from: patching from into to would close the loop.
        if (reaches (to, from))
            return Refusal::loop;
        return Refusal::none;
    }

    Refusal PatchBay::connect (int from, int to, float db)
    {
        const auto why = check (from, to);
        if (why != Refusal::none)
            return why;
        setGainDb (from, to, db);
        if (! edges[(size_t) from][(size_t) to])
        {
            edges[(size_t) from][(size_t) to] = true;
            replan();
        }
        return Refusal::none;
    }

    void PatchBay::disconnect (int from, int to)
    {
        if (! linked (from, to))
            return;
        edges[(size_t) from][(size_t) to] = false;
        replan();
    }

    void PatchBay::setGainDb (int from, int to, float db)
    {
        if (from < 0 || to < 0 || from >= numNodes || to >= numNodes)
            return;
        gains[(size_t) (from * numNodes + to)] = std::clamp (db, minDb - 1.0f, maxDb);
    }

    std::vector<Link> PatchBay::links() const
    {
        std::vector<Link> out;
        for (int f = 0; f < numNodes; ++f)
            for (int t = 0; t < numNodes; ++t)
                if (edges[(size_t) f][(size_t) t])
                    out.push_back ({ f, t, gainDb (f, t) });
        return out;
    }

    void PatchBay::setLinks (const std::vector<Link>& list)
    {
        edges = {};
        for (const auto& k : list)
            if (check (k.from, k.to) == Refusal::none)
            {
                edges[(size_t) k.from][(size_t) k.to] = true;
                setGainDb (k.from, k.to, k.gainDb);
            }
        replan();
    }

    std::vector<Link> PatchBay::defaultLinks()
    {
        std::vector<Link> out { { node::editor, node::mainOut }, { node::sampler, node::mainOut }, { node::slicer, node::mainOut },
                                { node::mixBus, node::mainOut } };
        for (int i = 0; i < maxSynths; ++i)
            out.push_back ({ node::synth + i, node::mainOut });
        for (int i = 0; i < mixer::maxTracks; ++i)
            out.push_back ({ node::track + i, node::mixBus });
        for (int i = 0; i < maxBuses; ++i)
            out.push_back ({ node::fxBus + i, node::mainOut });
        return out;
    }

    bool PatchBay::isDefault() const
    {
        const auto mine = links();
        const auto standard = defaultLinks();
        if (mine.size() != standard.size())
            return false;
        for (const auto& k : standard)
            if (! linked (k.from, k.to) || std::abs (gainDb (k.from, k.to)) > 1.0e-4f)
                return false;
        return true;
    }

    std::vector<int> PatchBay::destinationsOf (int from) const
    {
        std::vector<int> out;
        for (int t = 0; t < numNodes; ++t)
            if (linked (from, t))
                out.push_back (t);
        return out;
    }

    std::vector<int> PatchBay::sourcesOf (int to) const
    {
        std::vector<int> out;
        for (int f = 0; f < numNodes; ++f)
            if (linked (f, to))
                out.push_back (f);
        return out;
    }

    //==========================================================================
    void PatchBay::setSynth (int index, mixer::InstrumentPtr instrument)
    {
        if (index < 0 || index >= maxSynths)
            return;
        if (instrument != nullptr)
            instrument->prepare (rate.load(), block);
        synthSlots[(size_t) index] = std::move (instrument);
        replan();
    }

    mixer::InstrumentPtr PatchBay::synth (int index) const
    {
        return index >= 0 && index < maxSynths ? synthSlots[(size_t) index] : nullptr;
    }

    void PatchBay::setEffect (int bus, EffectPtr fx)
    {
        if (bus < 0 || bus >= maxBuses)
            return;
        if (fx != nullptr)
            fx->prepare (rate.load(), block);
        effectSlots[(size_t) bus] = std::move (fx);
        replan();
    }

    EffectPtr PatchBay::effect (int bus) const
    {
        return bus >= 0 && bus < maxBuses ? effectSlots[(size_t) bus] : nullptr;
    }

    void PatchBay::liveMidi (int index, uint8_t status, uint8_t data1, uint8_t data2)
    {
        if (index < 0 || index >= maxSynths)
            return;
        const std::lock_guard<std::mutex> l (liveLock);
        auto& q = liveQueue[(size_t) index];
        if (q.size() < 1024)
            q.push_back ({ 0, status, data1, data2 });
    }

    void PatchBay::panic() { panicPending = true; }

    float PatchBay::takePeak (int n)
    {
        if (n < 0 || n >= numNodes)
            return 0.0f;
        return peaks[(size_t) n].exchange (0.0f, std::memory_order_relaxed);
    }

    // Sources come before what they feed (Kahn's sort; ties in node order, so the
    // editor and instruments go first and the Main Out last).
    void PatchBay::replan()
    {
        auto next = std::make_unique<Plan>();
        std::array<int, numNodes> waiting {};
        for (int f = 0; f < numNodes; ++f)
            for (int t = 0; t < numNodes; ++t)
                if (edges[(size_t) f][(size_t) t])
                {
                    ++waiting[(size_t) t];
                    next->inputs[(size_t) t].push_back (f);
                }
        std::array<bool, numNodes> placed {};
        while ((int) next->order.size() < numNodes)
        {
            int pick = -1;
            for (int n = 0; n < numNodes && pick < 0; ++n)
                if (! placed[(size_t) n] && waiting[(size_t) n] == 0)
                    pick = n;
            if (pick < 0)   // a loop, which check() never lets in: play the rest in node order
                for (int n = 0; n < numNodes && pick < 0; ++n)
                    if (! placed[(size_t) n])
                        pick = n;
            placed[(size_t) pick] = true;
            next->order.push_back (pick);
            for (int t = 0; t < numNodes; ++t)
                if (edges[(size_t) pick][(size_t) t])
                    --waiting[(size_t) t];
        }
        int k = 0;
        for (const auto n : next->order)
            if (kindOf (n) == Kind::track)
                next->tracks[(size_t) k++] = indexOf (n);
        next->synths = synthSlots;
        next->effects = effectSlots;

        {
            const std::lock_guard<std::mutex> l (lock);
            std::swap (plan, next);
        }
    }   // the old plan (and any instrument or effect only it held) goes here, off the audio thread

    //==========================================================================
    int PatchBay::trackAt (int k) const
    {
        if (! rendering || plan == nullptr)
            return k;
        return plan->tracks[(size_t) std::clamp (k, 0, mixer::maxTracks - 1)];
    }

    void PatchBay::trackInput (int track, int offset, float* left, float* right, int frames)
    {
        if (! rendering || track < 0 || track >= mixer::maxTracks || offset < 0 || offset + frames > chunk)
            return;
        auto* inL = trackInL.data() + (size_t) track * block;
        auto* inR = trackInR.data() + (size_t) track * block;
        if (! trackInputDone[(size_t) track])
        {
            trackInputDone[(size_t) track] = true;
            sumInputs (node::track + track, inL, inR);
        }
        for (int i = 0; i < frames; ++i)
        {
            left[i] += inL[offset + i];
            right[i] += inR[offset + i];
        }
    }

    void PatchBay::trackOutput (int track, int offset, const float* left, const float* right, int frames)
    {
        if (! rendering || track < 0 || track >= mixer::maxTracks || offset < 0 || offset + frames > chunk)
            return;
        const auto n = node::track + track;
        std::copy (left, left + frames, bufL (n) + offset);
        std::copy (right, right + frames, bufR (n) + offset);
    }

    void PatchBay::sumInputs (int n, float* left, float* right)
    {
        std::fill (left, left + chunk, 0.0f);
        std::fill (right, right + chunk, 0.0f);
        for (const auto from : plan->inputs[(size_t) n])
        {
            ensure (from);
            const auto edge = (size_t) (from * numNodes + n);
            const auto g = dbToGain (gains[edge].load (std::memory_order_relaxed));
            const auto was = fresh ? g : lastGain[edge];
            addRamped (left, bufL (from), chunk, was, g);
            addRamped (right, bufR (from), chunk, was, g);
            lastGain[edge] = g;
        }
    }

    void PatchBay::ensure (int n)
    {
        if (done[(size_t) n])
            return;
        done[(size_t) n] = true;
        auto* left = bufL (n);
        auto* right = bufR (n);
        switch (kindOf (n))
        {
            case Kind::sampler:
                std::fill (left, left + chunk, 0.0f);
                std::fill (right, right + chunk, 0.0f);
                samplerEngine.render (left, right, chunk);
                break;
            case Kind::slicer:
                std::fill (left, left + chunk, 0.0f);
                std::fill (right, right + chunk, 0.0f);
                slicerEngine.render (left, right, chunk);
                break;
            case Kind::synth:
            {
                std::fill (left, left + chunk, 0.0f);
                std::fill (right, right + chunk, 0.0f);
                const auto i = (size_t) indexOf (n);
                if (auto& s = plan->synths[i])
                {
                    auto& events = liveTaken[i];
                    s->process (events.data(), (int) events.size(), left, right, chunk);
                    events.clear();
                }
                break;
            }
            case Kind::mixBus:
            {
                sumInputs (n, left, right);
                const auto db = mix != nullptr ? mix->masterDb.load() : 0.0f;
                const auto master = db <= -96.0f ? 0.0f : std::pow (10.0f, db / 20.0f);
                const auto was = fresh ? master : lastMaster;
                for (int i = 0; i < chunk; ++i)
                {
                    const auto g = was + (master - was) * (float) (i + 1) / (float) chunk;
                    left[i] *= g;
                    right[i] *= g;
                }
                lastMaster = master;
                if (mix != nullptr)
                    mix->meterMaster (left, right, chunk);
                break;
            }
            case Kind::fxBus:
                sumInputs (n, left, right);
                if (auto& fx = plan->effects[(size_t) indexOf (n)])
                    fx->process (left, right, chunk);
                break;
            case Kind::mainOut:
                sumInputs (n, left, right);
                break;
            case Kind::editor:
            case Kind::track:
                break;   // filled before
        }
    }

    void PatchBay::render (float* left, float* right, int frames)
    {
        std::unique_lock<std::mutex> l (lock, std::try_to_lock);
        if (! l.owns_lock() || plan == nullptr)
        {
            std::fill (left, left + frames, 0.0f);   // a new plan arriving: one quiet block
            std::fill (right, right + frames, 0.0f);
            return;
        }
        {
            std::unique_lock<std::mutex> q (liveLock, std::try_to_lock);
            if (q.owns_lock())
                for (size_t i = 0; i < liveQueue.size(); ++i)
                {
                    liveTaken[i].insert (liveTaken[i].end(), liveQueue[i].begin(), liveQueue[i].end());
                    liveQueue[i].clear();
                }
        }
        if (panicPending.exchange (false))
        {
            for (auto& s : plan->synths)
                if (s != nullptr)
                    s->reset();
            for (auto& fx : plan->effects)
                if (fx != nullptr)
                    fx->reset();
        }

        int at = 0;
        while (at < frames)
        {
            auto n = std::min (block, frames - at);
            if (mix != nullptr)
                n = mix->nextBlock (n);   // the mixer plays it in one piece
            renderChunk (left + at, right + at, n);
            at += n;
        }
    }

    void PatchBay::renderChunk (float* left, float* right, int frames)
    {
        chunk = frames;
        done = {};
        trackInputDone = {};
        std::copy (left, left + frames, bufL (node::editor));
        std::copy (right, right + frames, bufR (node::editor));
        done[(size_t) node::editor] = true;
        for (int t = 0; t < mixer::maxTracks; ++t)
        {
            const auto n = node::track + t;
            std::fill (bufL (n), bufL (n) + frames, 0.0f);
            std::fill (bufR (n), bufR (n) + frames, 0.0f);
            done[(size_t) n] = true;
        }

        // The mixer plays its tracks in the plan's order, taking what is patched into
        // each from here (worked out on the way) and giving back what each plays.
        if (mix != nullptr)
        {
            std::fill (scratchL.begin(), scratchL.begin() + frames, 0.0f);
            std::fill (scratchR.begin(), scratchR.begin() + frames, 0.0f);
            rendering = true;
            mix->render (scratchL.data(), scratchR.data(), frames);
            rendering = false;
        }
        for (const auto n : plan->order)
            ensure (n);
        for (auto& events : liveTaken)
            events.clear();   // a synth slot left empty

        std::copy (bufL (node::mainOut), bufL (node::mainOut) + frames, left);
        std::copy (bufR (node::mainOut), bufR (node::mainOut) + frames, right);
        fresh = false;

        for (int n = 0; n < numNodes; ++n)
        {
            float peak = 0.0f;
            for (int i = 0; i < frames; ++i)
                peak = std::max ({ peak, std::abs (bufL (n)[i]), std::abs (bufR (n)[i]) });
            auto& p = peaks[(size_t) n];
            if (peak > p.load (std::memory_order_relaxed))
                p.store (peak, std::memory_order_relaxed);
        }
    }
}
