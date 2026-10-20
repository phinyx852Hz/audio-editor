// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// MIDI keyboards and controllers (ALSA on Linux, through JUCE), opened by their
// identifiers. Messages arrive on a MIDI thread and go to one handler, which must
// be quick and thread-safe (the Sampler's engine is).
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct MidiDevice
{
    std::string name, identifier;
};

class MidiInputs
{
public:
    // status: 0x80 note off, 0x90 note on, 0xB0 controller, 0xE0 pitch bend (value 0..16383 in data1);
    // channel 1..16.
    using Handler = std::function<void (int status, int channel, int data1, int data2)>;

    MidiInputs();
    ~MidiInputs();

    static std::vector<MidiDevice> available();
    void open (const std::vector<std::string>& identifiers);   // closes the others
    std::vector<std::string> opened() const;
    void setHandler (Handler);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
