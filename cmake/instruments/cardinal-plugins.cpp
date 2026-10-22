/*
 * Cardinal's list of built-in plugins, cut down for PHINYX to the foundational
 * modules: Cardinal's own host modules (audio, CV, MIDI, parameters, time) and
 * VCV Fundamental (oscillators, filter, amplifiers, envelope, LFOs, noise,
 * mixers, sequencer, scope, utilities). Their code and panels are
 * GPL-3.0-or-later (Fundamental) or GPL-3.0-or-later with CC0 panels
 * (Cardinal), so none of the non-commercial or permission-only artwork of
 * other module collections is built in.
 *
 * Replaces plugins/plugins.cpp (cmake/instruments/CardinalFoundational.cmake).
 * Based on Cardinal's plugins-mini.cpp:
 *
 * DISTRHO Cardinal Plugin
 * Copyright (C) 2021-2023 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the LICENSE file.
 */

#include "rack.hpp"
#include "plugin.hpp"

#include "DistrhoUtils.hpp"

// Cardinal (built-in)
#include "Cardinal/src/plugin.hpp"

// Fundamental
#include "Fundamental/src/plugin.hpp"

// known terminal modules
std::vector<Model*> hostTerminalModels;

// plugin instances
Plugin* pluginInstance__Cardinal;
Plugin* pluginInstance__Fundamental;

namespace rack {

namespace asset {
std::string pluginManifest(const std::string& dirname);
std::string pluginPath(const std::string& dirname);
}

namespace plugin {

struct StaticPluginLoader {
    Plugin* const plugin;
    FILE* file;
    json_t* rootJ;

    StaticPluginLoader(Plugin* const p, const char* const name)
        : plugin(p),
          file(nullptr),
          rootJ(nullptr)
    {
#ifdef DEBUG
        DEBUG("Loading plugin module %s", name);
#endif

        p->path = asset::pluginPath(name);

        const std::string manifestFilename = asset::pluginManifest(name);

        if ((file = std::fopen(manifestFilename.c_str(), "r")) == nullptr)
        {
            d_stderr2("Manifest file %s does not exist", manifestFilename.c_str());
            return;
        }

        json_error_t error;
        if ((rootJ = json_loadf(file, 0, &error)) == nullptr)
        {
            d_stderr2("JSON parsing error at %s %d:%d %s", manifestFilename.c_str(), error.line, error.column, error.text);
            return;
        }

        // force ABI, we use static plugins so this doesnt matter as long as it builds
        json_t* const version = json_string((APP_VERSION_MAJOR + ".0").c_str());
        json_object_set(rootJ, "version", version);
        json_decref(version);

        // Load manifest
        p->fromJson(rootJ);

        // Reject plugin if slug already exists
        if (Plugin* const existingPlugin = getPlugin(p->slug))
            throw Exception("Plugin %s is already loaded, not attempting to load it again", p->slug.c_str());
    }

    ~StaticPluginLoader()
    {
        if (rootJ != nullptr)
        {
            // Load modules manifest
            json_t* const modulesJ = json_object_get(rootJ, "modules");
            plugin->modulesFromJson(modulesJ);

            json_decref(rootJ);
            plugins.push_back(plugin);
        }

        if (file != nullptr)
            std::fclose(file);
    }

    bool ok() const noexcept
    {
        return rootJ != nullptr;
    }

    void removeModule(const char* const slugToRemove) const noexcept
    {
        json_t* const modules = json_object_get(rootJ, "modules");
        DISTRHO_SAFE_ASSERT_RETURN(modules != nullptr,);

        size_t i;
        json_t* v;
        json_array_foreach(modules, i, v)
        {
            if (json_t* const slug = json_object_get(v, "slug"))
            {
                if (const char* const value = json_string_value(slug))
                {
                    if (std::strcmp(value, slugToRemove) == 0)
                    {
                        json_array_remove(modules, i);
                        break;
                    }
                }
            }
        }
    }
};

static void initStatic__Cardinal()
{
    Plugin* const p = new Plugin;
    pluginInstance__Cardinal = p;

    const StaticPluginLoader spl(p, "Cardinal");
    if (spl.ok())
    {
        p->addModel(modelCardinalBlank);
        p->addModel(modelExpanderInputMIDI);
        p->addModel(modelExpanderOutputMIDI);
        p->addModel(modelHostAudio2);
        p->addModel(modelHostAudio8);
        p->addModel(modelHostCV);
        p->addModel(modelHostMIDI);
        p->addModel(modelHostMIDICC);
        p->addModel(modelHostMIDIGate);
        p->addModel(modelHostMIDIMap);
        p->addModel(modelHostParameters);
        p->addModel(modelHostParametersMap);
        p->addModel(modelHostTime);
        // Left out: plugin hosts, neural amp, file player, pitch tracker and visual toys.
        spl.removeModule("AIDA-X");
        spl.removeModule("AudioFile");
        spl.removeModule("AudioToCVPitch");
        spl.removeModule("Carla");
        spl.removeModule("Ildaeil");
        spl.removeModule("SassyScope");
        spl.removeModule("TextEditor");
        spl.removeModule("glBars");

        hostTerminalModels = {
            modelHostAudio2,
            modelHostAudio8,
            modelHostCV,
            modelHostMIDI,
            modelHostMIDICC,
            modelHostMIDIGate,
            modelHostMIDIMap,
            modelHostParameters,
            modelHostParametersMap,
            modelHostTime,
        };
    }
}

static void initStatic__Fundamental()
{
    Plugin* const p = new Plugin;
    pluginInstance__Fundamental = p;

    const StaticPluginLoader spl(p, "Fundamental");
    if (spl.ok())
    {
        p->addModel(model_8vert);
        p->addModel(modelADSR);
        p->addModel(modelDelay);
        p->addModel(modelLFO);
        p->addModel(modelLFO2);
        p->addModel(modelMerge);
        p->addModel(modelMidSide);
        p->addModel(modelMixer);
        p->addModel(modelMutes);
        p->addModel(modelNoise);
        p->addModel(modelOctave);
        p->addModel(modelPulses);
        p->addModel(modelQuantizer);
        p->addModel(modelRandom);
        p->addModel(modelScope);
        p->addModel(modelSEQ3);
        p->addModel(modelSequentialSwitch1);
        p->addModel(modelSequentialSwitch2);
        p->addModel(modelSplit);
        p->addModel(modelSum);
        p->addModel(modelVCA);
        p->addModel(modelVCA_1);
        p->addModel(modelVCF);
        p->addModel(modelVCMixer);
        p->addModel(modelVCO);
        p->addModel(modelVCO2);
    }
}

void initStaticPlugins()
{
    initStatic__Cardinal();
    initStatic__Fundamental();
}

void destroyStaticPlugins()
{
    for (Plugin* p : plugins)
        delete p;
    plugins.clear();
}

void updateStaticPluginsDarkMode()
{
}

}
}
