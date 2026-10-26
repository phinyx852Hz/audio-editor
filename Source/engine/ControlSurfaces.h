// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The control surfaces plugged in (control/ControlSurface.h): found by their MIDI ports' names,
// opened on the port each is driven through (JUCE, ALSA), and kept out of the plain MIDI inputs.
// What they send becomes Commands for the UI, and notes played straight from the MIDI thread.
#include "../control/ControlSurface.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

class ControlSurfaces
{
public:
    // A surface found: its kind, and the ports it is driven through.
    struct Found
    {
        std::string kind, name;              // Kind::id, Kind::name
        std::string input, inputName;        // identifier and name; empty: none
        std::string output, outputName;
        bool enabled = true;
        bool open = false;                   // connected and lit
    };

    ControlSurfaces();
    ~ControlSurfaces();

    // Notes, pitch bend and pressure a surface plays (Note view, drum pads): any thread.
    void setNotes (std::function<void (const uint8_t* data, int size)>);
    // Kinds turned off by the user are left alone, as plain MIDI inputs.
    void setDisabled (const std::set<std::string>& kinds);
    // Looks for surfaces plugged in or taken out, and opens or closes them. UI thread. True when
    // the ports in use changed.
    bool scan();
    std::vector<Found> found() const;
    std::vector<std::string> claimedInputs() const;   // identifiers to keep out of MidiInputs

    // A Push in use, as its display shows it: its view, what its encoders turn, the first scene shown.
    struct PushShown
    {
        bool open = false;
        control::PadSurface::View view = control::PadSurface::View::session;
        control::PadSurface::Knobs knobs = control::PadSurface::Knobs::volume;
        int sceneOffset = 0;
    };
    PushShown push() const;

    std::vector<control::Command> takeCommands();   // UI thread
    void refresh (const control::State&);           // UI thread: the lights
    void closeAll();                                // dark, and back to their own modes

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
