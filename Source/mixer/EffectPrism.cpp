// SPDX-License-Identifier: AGPL-3.0-only
#include "EffectPrism.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mixer
{
    namespace
    {
        float rayGain (float db) { return db <= -60.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }

        bool contains (const std::vector<EffectPrism::Ray>& rays, const EffectPtr& e)
        {
            for (const auto& r : rays)
                for (const auto& x : r.effects)
                    if (x.effect == e)
                        return true;
            return false;
        }
    }

    EffectPrism::EffectPrism()
    {
        Ray first;
        list.push_back (first);
        lanes.push_back (std::make_shared<Lane>());
        prepareLane (*lanes.front());
        splitter.prepare (rate, 2);
        input[0].assign ((size_t) block, 0.0f);
        input[1].assign ((size_t) block, 0.0f);
    }

    std::string EffectPrism::name() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return title.empty() ? "Prism" : title;
    }

    void EffectPrism::setName (const std::string& n)
    {
        const std::lock_guard<std::mutex> l (lock);
        title = n;
    }

    std::array<double, 3> EffectPrism::crossovers() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return bandsHz;
    }

    void EffectPrism::setCrossovers (const std::array<double, 3>& hz)
    {
        const std::lock_guard<std::mutex> l (lock);
        for (size_t k = 0; k < hz.size(); ++k)
            bandsHz[k] = std::clamp (hz[k], 20.0, 20000.0);
        bandsChanged = true;
    }

    void EffectPrism::prepareLane (Lane& lane)
    {
        for (auto* v : { &lane.buffer, &lane.dry })
            for (auto& c : *v)
                c.assign ((size_t) block, 0.0f);
        lane.align.prepare (2, maxDelay);
    }

    void EffectPrism::setRays (std::vector<Ray> next)
    {
        if ((int) next.size() > maxRays)
            next.resize ((size_t) maxRays);
        for (auto& r : next)
        {
            r.effects.erase (std::remove_if (r.effects.begin(), r.effects.end(), [] (const Insert& x) { return x.effect == nullptr; }), r.effects.end());
            if ((int) r.effects.size() > maxEffects)
                r.effects.resize ((size_t) maxEffects);
            r.gainDb = std::clamp (r.gainDb, -60.0f, 12.0f);
        }

        const auto old = rays();
        for (auto& r : next)
            for (auto& x : r.effects)
                if (! contains (old, x.effect))
                    x.effect->prepare (rate, block);

        // A ray keeps its lane (its delay line) while it keeps its first effect; the rest get new ones.
        std::vector<std::shared_ptr<Lane>> previous;
        {
            const std::lock_guard<std::mutex> l (lock);
            previous = lanes;
        }
        std::vector<std::shared_ptr<Lane>> made;
        for (size_t i = 0; i < next.size(); ++i)
        {
            std::shared_ptr<Lane> lane;
            if (i < previous.size() && previous[i] != nullptr)
                std::swap (lane, previous[i]);
            else
            {
                lane = std::make_shared<Lane>();
                prepareLane (*lane);
            }
            made.push_back (lane);
        }

        const std::lock_guard<std::mutex> l (lock);
        std::swap (list, next);
        std::swap (lanes, made);
    }   // the effects taken out are let go here, after the lock

    std::vector<EffectPrism::Ray> EffectPrism::rays() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return list;
    }

    int EffectPrism::numRays() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return (int) list.size();
    }

    int EffectPrism::raysHeard() const { return prism::raysHeard (mode.load(), numRays()); }

    float EffectPrism::takeRayPeak (int ray)
    {
        const std::lock_guard<std::mutex> l (lock);
        if (ray < 0 || ray >= (int) lanes.size())
            return 0.0f;
        return lanes[(size_t) ray]->peak.exchange (0.0f);
    }

    int EffectPrism::latency() const
    {
        const std::lock_guard<std::mutex> l (lock);
        int most = 0;
        for (const auto& r : list)
        {
            int n = 0;
            for (const auto& x : r.effects)
                n += x.bypassed ? 0 : std::max (0, x.effect->latency());
            most = std::max (most, n);
        }
        return most;
    }

    void EffectPrism::prepare (double sampleRate, int maxBlock)
    {
        const std::lock_guard<std::mutex> l (lock);
        rate = sampleRate;
        block = std::max (1, maxBlock);
        for (auto& r : list)
            for (auto& x : r.effects)
                x.effect->prepare (rate, block);
        for (auto& lane : lanes)
            prepareLane (*lane);
        splitter.prepare (rate, 2);
        bandsChanged = true;
        input[0].assign ((size_t) block, 0.0f);
        input[1].assign ((size_t) block, 0.0f);
    }

    void EffectPrism::reset()
    {
        const std::lock_guard<std::mutex> l (lock);
        for (auto& r : list)
            for (auto& x : r.effects)
                x.effect->reset();
        for (auto& lane : lanes)
            lane->align.reset();
        splitter.reset();
    }

    void EffectPrism::process (float* left, float* right, int frames)
    {
        std::unique_lock<std::mutex> l (lock, std::try_to_lock);
        if (! l.owns_lock())
            return;   // the rays being changed: the sound passes through for this block
        for (int done = 0; done < frames;)
        {
            const auto n = std::min (block, frames - done);
            renderChunk (left + done, right + done, n);
            done += n;
        }
    }

    void EffectPrism::renderChunk (float* left, float* right, int frames)
    {
        const auto count = (int) std::min (list.size(), lanes.size());
        if (count == 0)
            return;   // no rays: as it came in
        const auto split = mode.load();
        if (bandsChanged.exchange (false))
            splitter.setBands (std::max (1, prism::raysHeard (prism::Split::bands, count)), bandsHz.data());

        std::copy (left, left + frames, input[0].begin());
        std::copy (right, right + frames, input[1].begin());
        const float* in[] { input[0].data(), input[1].data() };
        std::array<std::array<float*, 2>, maxRays> rayChannels {};
        std::array<float* const*, maxRays> rayPointers {};
        for (int i = 0; i < count; ++i)
        {
            rayChannels[(size_t) i] = { lanes[(size_t) i]->buffer[0].data(), lanes[(size_t) i]->buffer[1].data() };
            rayPointers[(size_t) i] = rayChannels[(size_t) i].data();
        }
        splitter.split (split, in, 2, frames, rayPointers.data(), count);

        const auto heard = prism::raysHeard (split, count);
        const bool anySolo = std::any_of (list.begin(), list.begin() + heard, [] (const Ray& r) { return r.soloed; });
        std::array<int, maxRays> delays {};
        int slowest = 0;
        for (int i = 0; i < heard; ++i)
        {
            for (const auto& x : list[(size_t) i].effects)
                delays[(size_t) i] += x.bypassed ? 0 : std::max (0, x.effect->latency());
            slowest = std::max (slowest, delays[(size_t) i]);
        }

        std::array<float, maxRays> unity {};
        for (int i = 0; i < heard; ++i)
        {
            auto& ray = list[(size_t) i];
            auto& lane = *lanes[(size_t) i];
            runEffects (ray, lane, frames);
            float* io[] { lane.buffer[0].data(), lane.buffer[1].data() };
            if (slowest > 0)
                lane.align.process (io, 2, frames, slowest - delays[(size_t) i]);

            // Its gain, ramped over the block so a move or a mute never clicks.
            const auto target = ray.muted || (anySolo && ! ray.soloed) ? 0.0f : rayGain (ray.gainDb);
            const auto from = lane.gain;
            const auto step = (target - from) / (float) frames;
            float peak = 0.0f;
            for (int k = 0; k < frames; ++k)
            {
                const auto g = from + step * (float) (k + 1);
                io[0][k] *= g;
                io[1][k] *= g;
                peak = std::max (peak, std::max (std::abs (io[0][k]), std::abs (io[1][k])));
            }
            lane.gain = target;
            if (peak > lane.peak.load (std::memory_order_relaxed))
                lane.peak.store (peak, std::memory_order_relaxed);
            unity[(size_t) i] = 1.0f;
        }

        float* out[] { left, right };
        std::array<const float* const*, maxRays> joined {};
        for (int i = 0; i < count; ++i)
            joined[(size_t) i] = rayPointers[(size_t) i];
        prism::Splitter::join (split, joined.data(), unity.data(), count, out, 2, frames);
    }

    void EffectPrism::runEffects (Ray& ray, Lane& lane, int frames)
    {
        auto* l = lane.buffer[0].data();
        auto* r = lane.buffer[1].data();
        for (auto& x : ray.effects)
        {
            if (x.bypassed)
                continue;
            const auto mix = std::clamp (x.mix, 0.0f, 1.0f);
            if (mix < 0.999f)
            {
                std::copy (l, l + frames, lane.dry[0].begin());
                std::copy (r, r + frames, lane.dry[1].begin());
            }
            x.effect->process (l, r, frames);
            if (mix < 0.999f)
                for (int k = 0; k < frames; ++k)
                {
                    l[k] = lane.dry[0][(size_t) k] + (l[k] - lane.dry[0][(size_t) k]) * mix;
                    r[k] = lane.dry[1][(size_t) k] + (r[k] - lane.dry[1][(size_t) k]) * mix;
                }
        }
    }

    // "name\t<name>", "split\t<0..3>\t<hz>\t<hz>\t<hz>", then a line a ray: "ray\t<gain dB>\t<muted>\t<soloed>\t<name>".
    std::string EffectPrism::state() const
    {
        const std::lock_guard<std::mutex> l (lock);
        std::ostringstream s;
        s << "name\t" << title << '\n';
        s << "split\t" << (int) mode.load() << '\t' << bandsHz[0] << '\t' << bandsHz[1] << '\t' << bandsHz[2] << '\n';
        for (const auto& r : list)
            s << "ray\t" << r.gainDb << '\t' << (int) r.muted << '\t' << (int) r.soloed << '\t' << r.name << '\n';
        return s.str();
    }

    // Applies to the rays already there, in order (their effects are set first).
    void EffectPrism::setState (const std::string& text)
    {
        auto next = rays();
        std::istringstream in (text);
        std::string line, newName;
        size_t ray = 0;
        while (std::getline (in, line))
        {
            std::istringstream fields (line);
            std::string tag;
            std::getline (fields, tag, '\t');
            if (tag == "name")
                std::getline (fields, newName);
            else if (tag == "split")
            {
                int s = 0;
                std::array<double, 3> hz { 200.0, 2000.0, 8000.0 };
                fields >> s >> hz[0] >> hz[1] >> hz[2];
                setSplit ((prism::Split) std::clamp (s, 0, prism::numSplits - 1));
                setCrossovers (hz);
            }
            else if (tag == "ray" && ray < next.size())
            {
                auto& r = next[ray++];
                int muted = 0, soloed = 0;
                fields >> r.gainDb >> muted >> soloed;
                r.muted = muted != 0;
                r.soloed = soloed != 0;
                fields.get();
                std::getline (fields, r.name);
            }
        }
        setName (newName);
        setRays (std::move (next));
    }
}
