// SPDX-License-Identifier: AGPL-3.0-only
#include "InstrumentPrism.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mixer
{
    namespace
    {
        float rayGain (float db) { return db <= -60.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }

        bool contains (const std::vector<InstrumentPrism::Ray>& rays, const InstrumentPtr& p)
        {
            return std::any_of (rays.begin(), rays.end(), [&] (const InstrumentPrism::Ray& r) { return r.instrument == p; });
        }

        bool contains (const std::vector<InstrumentPrism::Ray>& rays, const EffectPtr& e)
        {
            for (const auto& r : rays)
                for (const auto& x : r.effects)
                    if (x.effect == e)
                        return true;
            return false;
        }
    }

    std::string prismNoteName (int note)
    {
        static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        note = std::clamp (note, 0, 127);
        return names[note % 12] + std::to_string (note / 12 - 1);
    }

    std::string InstrumentPrism::name() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return title.empty() ? "Instrument Prism" : title;
    }

    void InstrumentPrism::setName (const std::string& n)
    {
        const std::lock_guard<std::mutex> l (lock);
        title = n;
    }

    void InstrumentPrism::prepareLane (Lane& lane)
    {
        lane.left.assign ((size_t) block, 0.0f);
        lane.right.assign ((size_t) block, 0.0f);
        lane.dryL.assign ((size_t) block, 0.0f);
        lane.dryR.assign ((size_t) block, 0.0f);
        lane.events.clear();
        lane.events.reserve (1024);
        lane.align.prepare (2, maxDelay);
    }

    void InstrumentPrism::setRays (std::vector<Ray> next)
    {
        if ((int) next.size() > maxRays)
            next.resize ((size_t) maxRays);
        for (auto& r : next)
        {
            r.effects.erase (std::remove_if (r.effects.begin(), r.effects.end(), [] (const Insert& x) { return x.effect == nullptr; }), r.effects.end());
            if ((int) r.effects.size() > maxEffects)
                r.effects.resize ((size_t) maxEffects);
            r.lowKey = std::clamp (r.lowKey, 0, 127);
            r.highKey = std::clamp (r.highKey, r.lowKey, 127);
            r.lowVelocity = std::clamp (r.lowVelocity, 1, 127);
            r.highVelocity = std::clamp (r.highVelocity, r.lowVelocity, 127);
            r.gainDb = std::clamp (r.gainDb, -60.0f, 12.0f);
        }

        // What is new to the Prism is prepared before it is heard, and plays the tuning in force.
        const auto old = rays();
        for (auto& r : next)
        {
            if (r.instrument != nullptr && ! contains (old, r.instrument))
            {
                r.instrument->prepare (rate, block);
                r.instrument->setTuning (tuning);
                r.instrument->setRaga (raga, ragaRoot, ragaEqual);
            }
            for (auto& x : r.effects)
                if (! contains (old, x.effect))
                    x.effect->prepare (rate, block);
        }

        // Each ray keeps its lane (its notes, its delay) while it keeps its instrument.
        std::vector<std::shared_ptr<Lane>> previous;
        {
            const std::lock_guard<std::mutex> l (lock);
            previous = lanes;
        }
        std::vector<std::shared_ptr<Lane>> made;
        for (const auto& r : next)
        {
            auto it = std::find_if (previous.begin(), previous.end(), [&] (const std::shared_ptr<Lane>& lane)
            {
                return lane != nullptr && lane->instrument == r.instrument;
            });
            if (it != previous.end())
            {
                made.push_back (*it);
                it->reset();
                continue;
            }
            auto lane = std::make_shared<Lane>();
            lane->instrument = r.instrument;
            prepareLane (*lane);
            made.push_back (lane);
        }

        const std::lock_guard<std::mutex> l (lock);
        std::swap (list, next);
        std::swap (lanes, made);
    }   // the instruments and effects taken out are let go here, after the lock

    std::vector<InstrumentPrism::Ray> InstrumentPrism::rays() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return list;
    }

    int InstrumentPrism::numRays() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return (int) list.size();
    }

    float InstrumentPrism::takeRayPeak (int ray)
    {
        const std::lock_guard<std::mutex> l (lock);
        if (ray < 0 || ray >= (int) lanes.size())
            return 0.0f;
        return lanes[(size_t) ray]->peak.exchange (0.0f);
    }

    int InstrumentPrism::latency() const
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

    void InstrumentPrism::prepare (double sampleRate, int maxBlock)
    {
        const std::lock_guard<std::mutex> l (lock);
        rate = sampleRate;
        block = std::max (1, maxBlock);
        for (auto& r : list)
        {
            if (r.instrument != nullptr)
                r.instrument->prepare (rate, block);
            for (auto& x : r.effects)
                x.effect->prepare (rate, block);
        }
        for (auto& lane : lanes)
        {
            prepareLane (*lane);
            lane->held = {};
        }
    }

    void InstrumentPrism::process (const NoteEvent* events, int numEvents, float* left, float* right, int frames)
    {
        std::unique_lock<std::mutex> l (lock, std::try_to_lock);
        if (! l.owns_lock())
            return;   // the rays being changed, for this block
        // In pieces no longer than the block the rays were prepared for.
        int first = 0;
        for (int done = 0; done < frames;)
        {
            const auto n = std::min (block, frames - done);
            int last = first;
            while (last < numEvents && events[last].offset < done + n)
                ++last;
            renderChunk (events + first, last - first, done, left + done, right + done, n);
            first = last;
            done += n;
        }
    }

    void InstrumentPrism::renderChunk (const NoteEvent* events, int numEvents, int base, float* left, float* right, int frames)
    {
        const auto count = std::min (list.size(), lanes.size());
        for (size_t i = 0; i < count; ++i)
            lanes[i]->events.clear();

        // The notes go to the rays whose ranges hold them, and their ends to the rays that got
        // them; everything else (controllers, pitch bend, pressure) to every ray.
        auto give = [] (Lane& lane, NoteEvent e)
        {
            if (lane.events.size() < lane.events.capacity())
                lane.events.push_back (e);
        };
        for (int k = 0; k < numEvents; ++k)
        {
            auto e = events[k];
            e.offset = std::clamp (e.offset - base, 0, frames - 1);
            const auto type = e.status & 0xF0;
            const auto channel = e.status & 0x0F;
            const auto note = e.data1 & 0x7F;
            if (type == 0x90 && e.data2 > 0)
            {
                for (size_t i = 0; i < count; ++i)
                    if (list[i].hears (note, e.data2))
                    {
                        auto& held = lanes[i]->held[(size_t) channel][(size_t) note];
                        held = (uint8_t) std::min (255, held + 1);
                        give (*lanes[i], e);
                    }
            }
            else if (type == 0x80 || type == 0x90 || type == 0xA0)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    auto& held = lanes[i]->held[(size_t) channel][(size_t) note];
                    if (held == 0)
                        continue;
                    if (type != 0xA0)
                        held = 0;
                    give (*lanes[i], e);
                }
            }
            else
            {
                // All notes off, all sound off: the rays have none held on that channel any more.
                const bool allOff = type == 0xB0 && (e.data1 == 120 || e.data1 == 123);
                for (size_t i = 0; i < count; ++i)
                {
                    if (allOff)
                        lanes[i]->held[(size_t) channel] = {};
                    give (*lanes[i], e);
                }
            }
        }

        const bool anySolo = std::any_of (list.begin(), list.begin() + (std::ptrdiff_t) count, [] (const Ray& r) { return r.soloed; });
        std::array<int, maxRays> delays {};
        int slowest = 0;
        for (size_t i = 0; i < count; ++i)
        {
            for (const auto& x : list[i].effects)
                delays[i] += x.bypassed ? 0 : std::max (0, x.effect->latency());
            slowest = std::max (slowest, delays[i]);
        }

        for (size_t i = 0; i < count; ++i)
        {
            auto& ray = list[i];
            auto& lane = *lanes[i];
            std::fill (lane.left.begin(), lane.left.begin() + frames, 0.0f);
            std::fill (lane.right.begin(), lane.right.begin() + frames, 0.0f);
            if (ray.instrument != nullptr)
                ray.instrument->process (lane.events.data(), (int) lane.events.size(), lane.left.data(), lane.right.data(), frames);
            runEffects (ray, lane, frames);
            // Rays with less latency wait for the slowest, so layered rays stay together.
            float* io[] { lane.left.data(), lane.right.data() };
            if (slowest > 0)
                lane.align.process (io, 2, frames, slowest - delays[i]);

            const auto target = ray.muted || (anySolo && ! ray.soloed) ? 0.0f : rayGain (ray.gainDb);
            const auto from = lane.gain;
            const auto step = (target - from) / (float) frames;
            float peak = 0.0f;
            for (int k = 0; k < frames; ++k)
            {
                const auto g = from + step * (float) (k + 1);
                const auto l = lane.left[(size_t) k] * g, r = lane.right[(size_t) k] * g;
                left[k] += l;
                right[k] += r;
                peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            }
            lane.gain = target;
            if (peak > lane.peak.load (std::memory_order_relaxed))
                lane.peak.store (peak, std::memory_order_relaxed);
        }
    }

    void InstrumentPrism::runEffects (Ray& ray, Lane& lane, int frames)
    {
        auto* l = lane.left.data();
        auto* r = lane.right.data();
        for (auto& x : ray.effects)
        {
            if (x.bypassed)
                continue;
            // Mix below 100 % keeps some of what went in, as on a track's inserts.
            const auto mix = std::clamp (x.mix, 0.0f, 1.0f);
            if (mix < 0.999f)
            {
                std::copy (l, l + frames, lane.dryL.begin());
                std::copy (r, r + frames, lane.dryR.begin());
            }
            x.effect->process (l, r, frames);
            if (mix < 0.999f)
                for (int k = 0; k < frames; ++k)
                {
                    l[k] = lane.dryL[(size_t) k] + (l[k] - lane.dryL[(size_t) k]) * mix;
                    r[k] = lane.dryR[(size_t) k] + (r[k] - lane.dryR[(size_t) k]) * mix;
                }
        }
    }

    void InstrumentPrism::releaseAll()
    {
        const std::lock_guard<std::mutex> l (lock);
        for (auto& r : list)
            if (r.instrument != nullptr)
                r.instrument->releaseAll();
        for (auto& lane : lanes)
            lane->held = {};
    }

    void InstrumentPrism::reset()
    {
        const std::lock_guard<std::mutex> l (lock);
        for (auto& r : list)
        {
            if (r.instrument != nullptr)
                r.instrument->reset();
            for (auto& x : r.effects)
                x.effect->reset();
        }
        for (auto& lane : lanes)
        {
            lane->held = {};
            lane->align.reset();
        }
    }

    bool InstrumentPrism::setParameter (const std::string& parameter, float value)
    {
        bool any = false;
        for (auto& r : rays())
            if (r.instrument != nullptr)
                any = r.instrument->setParameter (parameter, value) || any;
        return any;
    }

    void InstrumentPrism::setTuning (std::shared_ptr<const phi::Tuning> t)
    {
        tuning = t;
        for (auto& r : rays())
            if (r.instrument != nullptr)
                r.instrument->setTuning (t);
    }

    void InstrumentPrism::setRaga (int which, int root, bool equalTempered)
    {
        raga = which;
        ragaRoot = root;
        ragaEqual = equalTempered;
        for (auto& r : rays())
            if (r.instrument != nullptr)
                r.instrument->setRaga (which, root, equalTempered);
    }

    // "name\t<name>", then a line a ray: "ray\t<gain dB>\t<muted>\t<soloed>\t<keys>\t<velocities>\t<name>".
    std::string InstrumentPrism::state() const
    {
        const std::lock_guard<std::mutex> l (lock);
        std::ostringstream s;
        s << "name\t" << title << '\n';
        for (const auto& r : list)
            s << "ray\t" << r.gainDb << '\t' << (int) r.muted << '\t' << (int) r.soloed << '\t' << r.lowKey << '\t' << r.highKey << '\t'
              << r.lowVelocity << '\t' << r.highVelocity << '\t' << r.name << '\n';
        return s.str();
    }

    // Applies to the rays already there, in order (their instruments and effects are set first).
    void InstrumentPrism::setState (const std::string& text)
    {
        auto next = rays();
        std::istringstream in (text);
        std::string line;
        size_t ray = 0;
        std::string newName;
        while (std::getline (in, line))
        {
            std::istringstream fields (line);
            std::string tag;
            std::getline (fields, tag, '\t');
            if (tag == "name")
                std::getline (fields, newName);
            else if (tag == "ray" && ray < next.size())
            {
                auto& r = next[ray++];
                int muted = 0, soloed = 0;
                fields >> r.gainDb >> muted >> soloed >> r.lowKey >> r.highKey >> r.lowVelocity >> r.highVelocity;
                r.muted = muted != 0;
                r.soloed = soloed != 0;
                fields.get();   // the tab before the name
                std::getline (fields, r.name);
            }
        }
        setName (newName);
        setRays (std::move (next));
    }
}
