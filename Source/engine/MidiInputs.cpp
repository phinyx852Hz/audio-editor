// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiInputs.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <map>
#include <mutex>

struct MidiInputs::Impl final : public juce::MidiInputCallback
{
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m) override
    {
        std::lock_guard g (lock);
        const auto channel = m.getChannel();
        int status = 0, a = 0, b = 0;
        if (m.isNoteOn())
            status = 0x90, a = m.getNoteNumber(), b = m.getVelocity();
        else if (m.isNoteOff())
            status = 0x80, a = m.getNoteNumber(), b = m.getVelocity();
        else if (m.isController())
            status = 0xB0, a = m.getControllerNumber(), b = m.getControllerValue();
        else if (m.isPitchWheel())
            status = 0xE0, a = m.getPitchWheelValue();
        else
            return;
        if (filter && filter (source != nullptr ? source->getName().toStdString() : std::string(), status, channel, a, b))
            return;
        if (handler)
            handler (status, channel, a, b);
    }

    std::mutex lock;
    Handler handler;
    Filter filter;
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

void MidiInputs::setFilter (Filter f)
{
    std::lock_guard g (impl->lock);
    impl->filter = std::move (f);
}
