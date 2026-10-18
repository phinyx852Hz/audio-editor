// SPDX-License-Identifier: AGPL-3.0-only
#include "VampHost.h"
#include "StockPlugins.h"

#include <vamp-hostsdk/PluginLoader.h>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>

namespace vamphost
{
    namespace
    {
        using Vamp::Plugin;
        using Vamp::RealTime;
        using Vamp::HostExt::PluginLoader;

        // VAMP_PATH as the SDK reads it: the stock folder first, then the user's
        // own VAMP_PATH or the usual places.
        void setSearchPath()
        {
            static std::once_flag once;
            std::call_once (once, []
            {
                std::vector<std::string> folders;
                if (const auto stock = stock::folder(); stock != juce::File())
                    folders.push_back (stock.getChildFile ("vamp").getFullPathName().toStdString());

                if (const char* own = std::getenv ("VAMP_PATH"); own != nullptr && *own != 0)
                    folders.push_back (own);
                else
                {
                    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString();
                    for (const auto* f : { "/vamp", "/.vamp" })
                        folders.push_back (home + f);
                    folders.push_back ("/usr/local/lib/vamp");
                    folders.push_back ("/usr/lib/vamp");
                }

                std::string path;
                for (const auto& f : folders)
                    path += (path.empty() ? "" : ":") + f;
                setenv ("VAMP_PATH", path.c_str(), 1);
            });
        }

        std::mutex listLock;

        bool isTimeOutput (const Plugin::OutputDescriptor& od)
        {
            // Outputs with many values per step (spectrograms, chromagrams) are pictures, not times or curves.
            return ! od.hasFixedBinCount || od.binCount <= 1;
        }
    }

    std::vector<std::string> searchPath()
    {
        setSearchPath();
        std::vector<std::string> folders;
        auto all = juce::StringArray::fromTokens (juce::String (std::getenv ("VAMP_PATH")), ":", {});
        for (const auto& f : all)
            folders.push_back (f.toStdString());
        return folders;
    }

    const std::vector<Output>& outputs()
    {
        static std::vector<Output> list;
        static bool scanned = false;
        const std::lock_guard lock (listLock);
        if (scanned)
            return list;
        scanned = true;

        setSearchPath();
        auto* loader = PluginLoader::getInstance();
        for (const auto& key : loader->listPlugins())
        {
            std::unique_ptr<Plugin> plugin (loader->loadPlugin (key, 48000.0f, PluginLoader::ADAPT_ALL_SAFE));
            if (plugin == nullptr)
                continue;

            std::string category;
            for (const auto& part : loader->getPluginCategory (key))
                category += (category.empty() ? "" : " > ") + part;

            for (const auto& od : plugin->getOutputDescriptors())
            {
                if (! isTimeOutput (od))
                    continue;
                Output o;
                o.pluginKey = key;
                o.outputId = od.identifier;
                o.pluginName = plugin->getName();
                o.outputName = od.name;
                o.maker = plugin->getMaker();
                o.description = od.description.empty() ? plugin->getDescription() : od.description;
                o.unit = od.unit;
                o.category = category;
                o.hasDuration = od.hasDuration;
                o.hasValue = od.hasFixedBinCount && od.binCount == 1;
                list.push_back (std::move (o));
            }
        }
        return list;
    }

    const Output* find (const std::string& id)
    {
        for (const auto& o : outputs())
            if (o.id() == id)
                return &o;
        return nullptr;
    }

    Result run (const AudioData& data, Frame start, Frame end, const std::string& id,
                std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        Result result;
        const auto* output = find (id);
        if (output == nullptr)
        {
            result.error = "The analysis plugin " + id + " was not found.";
            return result;
        }

        start = std::clamp<Frame> (start, 0, data.length());
        end = std::clamp<Frame> (end, start, data.length());
        const auto rate = data.sampleRate();
        auto* loader = PluginLoader::getInstance();
        std::unique_ptr<Plugin> plugin (loader->loadPlugin (output->pluginKey, (float) rate, PluginLoader::ADAPT_ALL_SAFE));
        if (plugin == nullptr)
        {
            result.error = "Could not load " + output->pluginName + ".";
            return result;
        }

        auto block = (int) plugin->getPreferredBlockSize();
        auto step = (int) plugin->getPreferredStepSize();
        if (block <= 0)
            block = 1024;
        if (step <= 0)
            step = plugin->getInputDomain() == Plugin::FrequencyDomain ? block / 2 : block;

        const auto channels = data.numChannels();
        if (! plugin->initialise ((size_t) channels, (size_t) step, (size_t) block))
        {
            result.error = output->pluginName + " could not start with this audio (" + std::to_string (channels)
                         + " channels, blocks of " + std::to_string (block) + ").";
            return result;
        }

        const auto descriptors = plugin->getOutputDescriptors();
        int index = -1;
        for (size_t i = 0; i < descriptors.size(); ++i)
            if (descriptors[i].identifier == output->outputId)
                index = (int) i;
        if (index < 0)
        {
            result.error = output->pluginName + " has no output called " + output->outputId + ".";
            return result;
        }
        const auto& od = descriptors[(size_t) index];

        long fixedCount = 0;   // features so far, for fixed-rate outputs without their own times
        auto collect = [&] (const Plugin::FeatureSet& set, const RealTime& when)
        {
            const auto found = set.find (index);
            if (found == set.end())
                return;
            for (const auto& f : found->second)
            {
                RealTime t = when;
                if (od.sampleType == Plugin::OutputDescriptor::FixedSampleRate)
                {
                    if (f.hasTimestamp)
                        fixedCount = std::lround (f.timestamp.sec * od.sampleRate + f.timestamp.nsec * od.sampleRate / 1.0e9);
                    t = RealTime::fromSeconds ((double) fixedCount / std::max (1.0e-9, (double) od.sampleRate));
                    ++fixedCount;
                }
                else if (od.sampleType == Plugin::OutputDescriptor::VariableSampleRate || f.hasTimestamp)
                    t = f.timestamp;

                Feature feature;
                feature.start = start + RealTime::realTime2Frame (t, (unsigned int) std::lround (rate));
                if (f.hasDuration)
                    feature.duration = RealTime::realTime2Frame (f.duration, (unsigned int) std::lround (rate));
                feature.value = f.values.empty() ? 0.0f : f.values.front();
                feature.label = f.label;
                result.features.push_back (std::move (feature));
            }
        };

        std::vector<std::vector<float>> buffers ((size_t) channels, std::vector<float> ((size_t) block));
        std::vector<float*> ptrs;
        for (auto& b : buffers)
            ptrs.push_back (b.data());

        for (Frame pos = start; pos < end; pos += step)
        {
            if (cancel != nullptr && cancel->load())
            {
                result.error = "Cancelled.";
                return result;
            }
            const auto count = (int) std::min<Frame> (block, end - pos);
            data.read (pos, count, ptrs.data());
            for (auto& b : buffers)
                std::fill (b.begin() + count, b.end(), 0.0f);

            const auto when = RealTime::frame2RealTime ((long) (pos - start), (unsigned int) std::lround (rate));
            collect (plugin->process (ptrs.data(), when), when);
            if (progress != nullptr)
                progress->store ((float) (pos - start) / (float) std::max<Frame> (1, end - start));
        }
        collect (plugin->getRemainingFeatures(), RealTime::frame2RealTime ((long) (end - start), (unsigned int) std::lround (rate)));

        std::sort (result.features.begin(), result.features.end(), [] (const Feature& a, const Feature& b) { return a.start < b.start; });
        return result;
    }
}
