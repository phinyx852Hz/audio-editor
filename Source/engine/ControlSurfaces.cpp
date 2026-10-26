// SPDX-License-Identifier: AGPL-3.0-only
#include "ControlSurfaces.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <map>
#include <mutex>

struct ControlSurfaces::Impl
{
    // A surface in use: its ports open, its device lit.
    struct Open final : public juce::MidiInputCallback
    {
        Open (Impl& o, Found f, std::unique_ptr<control::Surface> s) : owner (o), info (std::move (f)), surface (std::move (s)) {}

        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            control::Received r;
            {
                std::lock_guard g (lock);
                surface->receive (m.getRawData(), (size_t) m.getRawDataSize(), r);
            }
            if (! r.notes.empty())
            {
                std::function<void (const uint8_t*, int)> play;
                {
                    std::lock_guard g (owner.notesLock);
                    play = owner.notes;
                }
                if (play)
                    for (const auto& n : r.notes)
                        play (n.data(), (int) n.size());
            }
            if (! r.commands.empty())
            {
                std::lock_guard g (owner.commandsLock);
                owner.commands.insert (owner.commands.end(), r.commands.begin(), r.commands.end());
            }
        }

        void send (const control::Messages& messages)
        {
            if (output == nullptr)
                return;
            for (const auto& m : messages)
                if (! m.empty())
                    output->sendMessageNow (juce::MidiMessage (m.data(), (int) m.size()));
        }

        Impl& owner;
        Found info;
        std::unique_ptr<control::Surface> surface;
        std::unique_ptr<juce::MidiInput> input;
        std::unique_ptr<juce::MidiOutput> output;
        std::mutex lock;
    };

    void close (Open& o)
    {
        if (o.input != nullptr)
            o.input->stop();
        control::Messages bye;
        {
            std::lock_guard g (o.lock);
            bye = o.surface->darken();
            const auto stop = o.surface->stop();
            bye.insert (bye.end(), stop.begin(), stop.end());
        }
        o.send (bye);
        o.input = nullptr;
        o.output = nullptr;
    }

    std::mutex notesLock;
    std::function<void (const uint8_t*, int)> notes;
    std::mutex commandsLock;
    std::vector<control::Command> commands;
    std::set<std::string> disabled;
    std::vector<Found> found;
    std::map<std::string, std::unique_ptr<Open>> open;   // by kind
};

ControlSurfaces::ControlSurfaces() : impl (std::make_unique<Impl>()) {}

ControlSurfaces::~ControlSurfaces() { closeAll(); }

void ControlSurfaces::setNotes (std::function<void (const uint8_t*, int)> fn)
{
    std::lock_guard g (impl->notesLock);
    impl->notes = std::move (fn);
}

void ControlSurfaces::setDisabled (const std::set<std::string>& kinds) { impl->disabled = kinds; }

bool ControlSurfaces::scan()
{
    const auto before = claimedInputs();
    const auto inputs = juce::MidiInput::getAvailableDevices();
    const auto outputs = juce::MidiOutput::getAvailableDevices();

    // Each kind's ports, then the one it is driven through.
    std::vector<Found> now;
    for (const auto& kind : control::kinds())
    {
        auto pick = [&kind] (const juce::Array<juce::MidiDeviceInfo>& ports, std::string& id, std::string& name)
        {
            std::vector<std::string> names, ids;
            for (const auto& p : ports)
                if (control::kindOf (p.name.toStdString()) == &kind)
                {
                    names.push_back (p.name.toStdString());
                    ids.push_back (p.identifier.toStdString());
                }
            const auto k = control::pickPort (kind, names);
            if (k >= 0)
            {
                id = ids[(size_t) k];
                name = names[(size_t) k];
            }
        };
        Found f;
        f.kind = kind.id;
        f.name = kind.name;
        pick (inputs, f.input, f.inputName);
        pick (outputs, f.output, f.outputName);
        if (f.input.empty() && f.output.empty())
            continue;
        f.enabled = impl->disabled.count (kind.id) == 0;
        now.push_back (f);
    }

    // Close what went away (or was turned off, or moved to other ports).
    for (auto it = impl->open.begin(); it != impl->open.end();)
    {
        const auto still = std::find_if (now.begin(), now.end(), [&] (const Found& f)
        {
            return f.kind == it->first && f.enabled && f.input == it->second->info.input && f.output == it->second->info.output;
        });
        if (still == now.end())
        {
            impl->close (*it->second);
            it = impl->open.erase (it);
        }
        else
            ++it;
    }

    // Open what came.
    for (auto& f : now)
    {
        if (! f.enabled || f.input.empty() || impl->open.count (f.kind) != 0)
            continue;
        const auto* kind = [&]
        {
            for (const auto& k : control::kinds())
                if (k.id == f.kind)
                    return &k;
            return (const control::Kind*) nullptr;
        }();
        auto o = std::make_unique<Impl::Open> (*impl, f, kind->make());
        if (! f.output.empty())
            o->output = juce::MidiOutput::openDevice (juce::String (f.output));
        o->input = juce::MidiInput::openDevice (juce::String (f.input), o.get());
        if (o->input == nullptr)
            continue;
        control::Messages hello;
        {
            std::lock_guard g (o->lock);
            hello = o->surface->start();
            o->surface->forget();
        }
        o->send (hello);
        o->input->start();
        impl->open[f.kind] = std::move (o);
    }
    for (auto& f : now)
        f.open = impl->open.count (f.kind) != 0;
    impl->found = now;
    return claimedInputs() != before;
}

std::vector<ControlSurfaces::Found> ControlSurfaces::found() const { return impl->found; }

std::vector<std::string> ControlSurfaces::claimedInputs() const
{
    std::vector<std::string> ids;
    for (const auto& [kind, o] : impl->open)
        ids.push_back (o->info.input);
    std::sort (ids.begin(), ids.end());
    return ids;
}

ControlSurfaces::PushShown ControlSurfaces::push() const
{
    PushShown p;
    for (const auto& [kind, o] : impl->open)
        if (kind == "push2" || kind == "push3")
        {
            std::lock_guard g (o->lock);
            if (const auto* pads = dynamic_cast<const control::PadSurface*> (o->surface.get()))
            {
                p.open = true;
                p.view = pads->view();
                p.knobs = pads->knobs();
                p.sceneOffset = pads->sceneOffset();
            }
        }
    return p;
}

std::vector<control::Command> ControlSurfaces::takeCommands()
{
    std::lock_guard g (impl->commandsLock);
    return std::exchange (impl->commands, {});
}

void ControlSurfaces::refresh (const control::State& state)
{
    for (auto& [kind, o] : impl->open)
    {
        control::Messages lights;
        {
            std::lock_guard g (o->lock);
            lights = o->surface->refresh (state);
        }
        o->send (lights);
    }
}

void ControlSurfaces::closeAll()
{
    for (auto& [kind, o] : impl->open)
        impl->close (*o);
    impl->open.clear();
    for (auto& f : impl->found)
        f.open = false;
}
