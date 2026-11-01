// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "MidiFile.h"

#include <cstdint>
#include <string>
#include <vector>

// DAWproject (https://github.com/bitwig/dawproject, MIT): the open project format of Bitwig
// and PreSonus, which Bitwig Studio, Studio One, Cubase and others read and write. A
// .dawproject is a zip with project.xml, metadata.xml, the audio files and the plugins'
// states. This reads one into what a PHI project can hold and writes one back. Plain C++.
//
// What carries over: the tracks, their colours, volume, pan, mute, solo and sends; audio and
// note clips on the timeline (loops unrolled, nested clips flattened); the clip launcher's
// slots; tempo and time signature; plugins as which plugin and its saved state. What does not:
// built-in devices of another DAW, groups (their tracks are kept), automation, tempo changes,
// warping (a warped clip plays unstretched), fades. Each is named in `Project::notes`.
namespace dawproject
{
    struct Note
    {
        double time = 0.0, duration = 0.0;   // beats, from the clip's start
        int key = 60, channel = 0;
        double velocity = 0.8, release = 0.8;   // 0..1
    };

    // A plugin (or another DAW's own device, which PHI only names).
    struct Device
    {
        std::string element;          // "Vst3Plugin", "ClapPlugin", "Vst2Plugin", "AuPlugin", "BuiltinDevice", "Equalizer"...
        std::string role = "audioFX"; // "instrument", "noteFX", "audioFX", "analyzer"
        std::string deviceId;         // a VST3's class id in hex, a CLAP's id, a VST2's number
        std::string name, vendor;
        bool enabled = true;
        std::vector<uint8_t> state;   // in the plugin's own format: a .vstpreset, a CLAP state stream
        bool isPlugin() const { return element == "Vst3Plugin" || element == "ClapPlugin" || element == "Vst2Plugin" || element == "AuPlugin"; }
        std::string format() const;   // "VST3", "CLAP", "VST", "AudioUnit"; empty for a built-in device
    };

    // An audio clip. Seconds throughout.
    struct AudioClip
    {
        std::string file;      // an absolute path (imported: where it was unpacked or lies)
        double start = 0.0;    // on the timeline
        double offset = 0.0;   // into the file
        double length = 0.0;
        bool warped = false;   // the original stretched it; PHI plays it at its own speed
        bool muted = false;
        std::string name;
        // The file's, which project.xml states (written; read when given).
        int channels = 2, sampleRate = 48000;
        double fileLength = 0.0;
    };

    // A note clip.
    struct NoteClip
    {
        double start = 0.0, length = 0.0;   // seconds on the timeline
        double lengthBeats = 0.0;
        std::vector<Note> notes;            // in time order
        bool muted = false;
        std::string name;
    };

    // A clip in the launcher.
    struct Slot
    {
        int scene = 0;
        bool audio = false;
        AudioClip audioClip;   // start unused
        NoteClip noteClip;
    };

    struct Send
    {
        int target = 0;        // which of Project::returns
        double db = -100.0;
        bool pre = false;
        bool enabled = true;
    };

    struct Track
    {
        std::string name, colour;   // "#rrggbb"
        bool notes = false, audio = false;   // what it holds
        double volumeDb = 0.0, pan = 0.0;   // pan -1..1
        bool mute = false, solo = false;
        std::vector<Device> devices;  // instrument first when there is one
        std::vector<Send> sends;
        std::vector<AudioClip> audioClips;
        std::vector<NoteClip> noteClips;
        std::vector<Slot> slots;

        const Device* instrument() const;
    };

    struct Project
    {
        std::string application, applicationVersion;
        double bpm = 120.0;
        int numerator = 4, denominator = 4;
        double masterDb = 0.0;
        std::vector<Track> tracks;
        std::vector<Track> returns;   // effect channels (Bitwig's FX tracks, Live's returns)
        std::vector<std::string> scenes;
        std::string title, artist, comment;   // metadata.xml
        std::vector<std::string> notes;       // what did not carry over, for the person to read
    };

    // Reads a .dawproject; embedded files (audio) are unpacked into `folder`, which must exist.
    bool read (const std::string& path, const std::string& folder, Project&, std::string& error);
    // Reads project.xml as text; audio paths stay as written (relative to the archive).
    bool parse (const std::string& xml, Project&, std::string& error);

    // Writes a .dawproject. Audio files are embedded (each once, under audio/), so the
    // project can move to another computer.
    bool write (const Project&, const std::string& path, std::string& error);
    std::string toXml (const Project&);   // project.xml; audio as audio/<file name>

    // Notes as a MIDI file at a fixed tempo, 480 ticks a quarter, as long as `lengthBeats`.
    midifile::SongPtr songOf (const std::vector<Note>&, double lengthBeats, double bpm);
    // The notes of a MIDI file heard in a clip: from `offset` seconds into it for `length`
    // seconds, its time from its own tempo map (`fixedBpm` 0) or that tempo, in beats at `bpm`.
    std::vector<Note> notesOf (const midifile::Song&, double offset, double length, double fixedBpm, double bpm);

    // A .vstpreset's class id (32 hex digits), empty when it is not one.
    std::string vstPresetClassId (const std::vector<uint8_t>&);
}
