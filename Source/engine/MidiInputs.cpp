// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiInputs.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <map>
#include <mutex>

struct MidiInputs::Impl final : public juce::MidiInputCallback
{
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        std::lock_guard g (lock);
        if (! handler)
            return;
        const auto channel = m.getChannel();
        if (m.isNoteOn())
            handler (0x90, channel, m.getNoteNumber(), m.getVelocity());
        else if (m.isNoteOff())
            handler (0x80, channel, m.getNoteNumber(), m.getVelocity());
        else if (m.isController())
            handler (0xB0, channel, m.getControllerNumber(), m.getControllerValue());
        else if (m.isPitchWheel())
            handler (0xE0, channel, m.getPitchWheelValue(), 0);
    }

    std::mutex lock;
    Handler handler;
    std::map<std::string, std::unique_ptr<juce::MidiInput>> devices;
};

MidiInputs::MidiInputs() : impl (std::make_unique<Impl>()) {}

MidiInputs::~MidiInputs()
{
    for (auto& [id, device] : impl->devices)
        device->stop();
    impl->devices.clear();
}

std::vector<MidiDevice> MidiInputs::available()
{
    std::vector<MidiDevice> list;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        list.push_back ({ d.name.toStdString(), d.identifier.toStdString() });
    return list;
}

void MidiInputs::open (const std::vector<std::string>& identifiers)
{
    for (auto it = impl->devices.begin(); it != impl->devices.end();)
    {
        if (std::find (identifiers.begin(), identifiers.end(), it->first) == identifiers.end())
        {
            it->second->stop();
            it = impl->devices.erase (it);
        }
        else
            ++it;
    }
    for (const auto& id : identifiers)
    {
        if (impl->devices.count (id) != 0)
            continue;
        if (auto device = juce::MidiInput::openDevice (juce::String (id), impl.get()))
        {
            device->start();
            impl->devices[id] = std::move (device);
        }
    }
}

std::vector<std::string> MidiInputs::opened() const
{
    std::vector<std::string> ids;
    for (const auto& [id, device] : impl->devices)
        ids.push_back (id);
    return ids;
}

void MidiInputs::setHandler (Handler h)
{
    std::lock_guard g (impl->lock);
    impl->handler = std::move (h);
}
