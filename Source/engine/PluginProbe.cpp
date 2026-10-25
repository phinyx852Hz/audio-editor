// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginProbe.h"

#include <algorithm>

PluginProbe::Source PluginProbe::masterDevice (AudioEngine& engine, ChainPath path)
{
    return [&engine, path]
    {
        const auto info = engine.chainDevice (path);
        if (info.empty || info.failed || info.pluginId.empty())
            return std::vector<Link> {};
        return std::vector<Link> { { info.pluginId, info.name, engine.chainDeviceState (path), false, 1.0f } };
    };
}

PluginProbe::Source PluginProbe::masterSection (AudioEngine& engine)
{
    return [&engine]
    {
        std::vector<Link> links;
        const auto allOff = engine.isMasterBypassed();
        for (int i = 0; i < AudioEngine::numMasterSlots; ++i)
        {
            const auto info = engine.masterSlot (i);
            if (info.empty || info.failed || info.pluginId.empty())
                continue;
            ChainPath path;
            path.slot = i;
            links.push_back ({ info.pluginId, info.name, engine.chainDeviceState (path), allOff || info.bypassed, info.mix });
        }
        return links;
    };
}

PluginProbe::Source PluginProbe::inserts (const mixer::Inserts& list)
{
    struct Held { std::weak_ptr<mixer::Effect> effect; bool bypassed; float mix; };
    std::vector<Held> held;
    for (const auto& i : list)
        if (i.effect != nullptr)
            held.push_back ({ i.effect, i.bypassed, i.mix });
    return [held]
    {
        std::vector<Link> links;
        for (const auto& h : held)
            if (const auto e = h.effect.lock())
                links.push_back ({ e->pluginId(), e->name(), e->state(), h.bypassed, h.mix });
        return links;
    };
}

PluginProbe::PluginProbe (AudioEngine& e, Source s, double sampleRate, int blockSize)
    : engine (e), source (std::move (s)), rate (sampleRate), block (blockSize),
      dryLeft ((std::size_t) blockSize), dryRight ((std::size_t) blockSize)
{
}

bool PluginProbe::sync (std::string& error)
{
    const auto links = source ? source() : std::vector<Link> {};
    if (links == applied)
        return false;

    // The same plugins in the same order: only settings, bypass and mix to copy.
    const auto samePlugins = links.size() == applied.size()
        && std::equal (links.begin(), links.end(), applied.begin(), [] (const Link& a, const Link& b) { return a.pluginId == b.pluginId; })
        && twins.size() == links.size();
    if (samePlugins)
    {
        for (std::size_t i = 0; i < links.size(); ++i)
        {
            auto& t = twins[i];
            if (links[i].state != t.link.state && ! links[i].state.empty())
            {
                t.effect->setState (links[i].state);
                t.latency = std::clamp (t.effect->latency(), 0, 1 << 16);
                t.line.assign ((std::size_t) t.latency * 2, 0.0f);
                t.at = 0;
            }
            t.link = links[i];
        }
        applied = links;
        return true;
    }

    twins.clear();
    for (const auto& link : links)
    {
        if (link.pluginId.empty())
        {
            error += (error.empty() ? "" : "; ") + link.name + " cannot be copied to measure";
            continue;
        }
        std::string why;
        auto effect = engine.loadEffectPlugin (link.pluginId, rate, why);
        if (effect == nullptr)
        {
            error += (error.empty() ? "" : "; ") + link.name + ": " + why;
            continue;
        }
        effect->prepare (rate, block);
        if (! link.state.empty())
            effect->setState (link.state);
        Twin t;
        t.link = link;
        t.effect = std::move (effect);
        t.latency = std::clamp (t.effect->latency(), 0, 1 << 16);
        t.line.assign ((std::size_t) t.latency * 2, 0.0f);
        twins.push_back (std::move (t));
    }
    applied = links;
    return true;
}

std::string PluginProbe::title() const
{
    std::string text;
    for (const auto& t : twins)
        if (! t.link.bypassed)
            text += (text.empty() ? "" : " → ") + t.link.name;
    return text.empty() ? std::string ("Nothing to measure") : text;
}

void PluginProbe::reset()
{
    for (auto& t : twins)
    {
        t.effect->reset();
        std::fill (t.line.begin(), t.line.end(), 0.0f);
        t.at = 0;
    }
}

void PluginProbe::process (float* left, float* right, int frames)
{
    for (auto& t : twins)
    {
        if (t.link.bypassed)
            continue;
        const auto wet = std::clamp (t.link.mix, 0.0f, 1.0f);
        if (wet >= 0.999f)
        {
            t.effect->process (left, right, frames);
            continue;
        }
        // Dry/wet: the dry sound comes out as late as the effect's.
        std::copy (left, left + frames, dryLeft.begin());
        std::copy (right, right + frames, dryRight.begin());
        t.effect->process (left, right, frames);
        for (int i = 0; i < frames; ++i)
        {
            float l = dryLeft[(std::size_t) i], r = dryRight[(std::size_t) i];
            if (! t.line.empty())
            {
                std::swap (l, t.line[t.at * 2]);
                std::swap (r, t.line[t.at * 2 + 1]);
                t.at = (t.at + 1) % (t.line.size() / 2);
            }
            left[i] = wet * left[i] + (1.0f - wet) * l;
            right[i] = wet * right[i] + (1.0f - wet) * r;
        }
    }
}

int PluginProbe::reportedLatency() const
{
    int total = 0;
    for (const auto& t : twins)
        if (! t.link.bypassed)
            total += t.latency;
    return total;
}
