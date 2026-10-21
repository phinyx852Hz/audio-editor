// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The mixer's sound: up to 8 tracks summed into a master out. Every track is the
// same kind of track: it can hold an audio file, a MIDI file and an instrument, in
// any mix. The audio clip plays as it is; the MIDI clip and a MIDI keyboard play
// the instrument; both are heard when a track has both. Each track then goes
// through trim, polarity, pan, fader, mute and solo. No MIDI editing.
// Plain C++, no JUCE or Qt: the UI sets things from its thread, render() runs on
// the audio thread, mixdown() on a worker thread.
#include "Instruments.h"
#include "MidiFile.h"
#include "../core/AudioData.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mixer
{
    constexpr int maxTracks = 8;
    constexpr int masterIndex = maxTracks;   // meters: 0..7 the tracks, 8 the master

    // An audio file on a track, already at the mixer's rate.
    struct AudioClip
    {
        std::vector<float> left, right;   // equal length; a mono file has both the same
        bool mono = false;
        std::string path;
        Frame length() const { return (Frame) left.size(); }
    };
    using AudioClipPtr = std::shared_ptr<const AudioClip>;
    AudioClipPtr makeAudioClip (const AudioData&, const std::string& path);   // the first two channels

    // A MIDI file on a track, its events placed at frames for the mixer's rate.
    struct MidiClip
    {
        struct Placed
        {
            Frame frame = 0;
            uint8_t status = 0, data1 = 0, data2 = 0;
        };
        midifile::SongPtr song;
        std::string path;
        std::vector<Placed> events;
        Frame length = 0;
        double bpm = 0.0;   // 0: the file's own tempo map; else this fixed tempo
    };
    using MidiClipPtr = std::shared_ptr<const MidiClip>;
    MidiClipPtr placeMidi (midifile::SongPtr, const std::string& path, double sampleRate, double bpm);

    // A track's controls, set from any thread.
    struct Strip
    {
        std::atomic<float> trimDb { 0.0f };     // ±24, before everything
        std::atomic<float> faderDb { 0.0f };    // -inf (below -96) .. +6
        std::atomic<float> pan { 0.0f };        // -1 left .. 1 right: equal power for mono audio, balance otherwise
        std::atomic<bool> invertLeft { false }, invertRight { false };
        std::atomic<bool> mute { false }, solo { false };
        std::atomic<bool> audioOn { true }, midiOn { true };   // hear the audio clip / play the MIDI clip
        std::atomic<double> audioOffset { 0.0 }, midiOffset { 0.0 };   // seconds the clip starts at
        std::atomic<int> transpose { 0 };       // semitones for the MIDI clip; channel 10 (drums) is left alone
        std::atomic<uint32_t> channels { 0xFFFF };   // MIDI channels of the clip that play, a bit each
    };

    struct Meter
    {
        float peak[2] {};   // linear, since the last take
        float rms[2] {};
    };

    class Engine
    {
    public:
        Engine();
        ~Engine();

        void prepare (double sampleRate);
        double sampleRate() const { return rate.load(); }

        int numTracks() const { return tracks.load(); }
        void setNumTracks (int);   // 1..8

        // Clips and instruments. Null clears. Safe while playing.
        void setAudio (int track, AudioClipPtr);
        void setMidi (int track, MidiClipPtr);
        void setInstrument (int track, InstrumentPtr);   // prepared here
        AudioClipPtr audio (int track) const;
        MidiClipPtr midi (int track) const;
        InstrumentPtr instrument (int track) const;
        Strip& strip (int track) { return strips[(size_t) track]; }
        const Strip& strip (int track) const { return strips[(size_t) track]; }

        std::atomic<float> masterDb { 0.0f };

        // Transport.
        void play();
        void stop();               // instruments let go of their notes
        bool isPlaying() const { return playing.load(); }
        Frame position() const { return pos.load(); }
        void setPosition (Frame);
        Frame length() const;      // where the last clip ends
        // Loops [start, end) while on; end <= start turns it off.
        void setLoop (Frame start, Frame end);
        bool isLooping() const { return loopEnd.load() > loopStart.load(); }

        // A MIDI keyboard plays the armed track's instrument (-1: none). Any thread.
        void setLiveTrack (int track) { armed = track; }
        int liveTrack() const { return armed.load(); }
        void liveMidi (uint8_t status, uint8_t data1, uint8_t data2);
        void panic();   // every instrument silent

        Meter takeMeter (int index);   // a track (0..7) or the master (8), after the fader

        // Audio thread: adds the mix to left and right.
        void render (float* left, float* right, int frames);

        // Renders the whole mix from the start, stereo at the mixer's rate, with the
        // instruments' tails (up to `tailSeconds` of sound after the last clip ends).
        // Live playback is silent meanwhile. Worker thread.
        AudioDataPtr mixdown (double tailSeconds, std::string& error,
                              std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    private:
        static constexpr int block = 256;

        struct Track
        {
            AudioClipPtr audio;
            MidiClipPtr midi;
            InstrumentPtr instrument;
            size_t cursor = 0;         // the next MIDI event to play
            bool relocate = true;      // the cursor needs finding again
            std::array<std::array<uint8_t, 128>, 16> held {};   // MIDI clip notes sounding: the note played + 1
            bool wasPlayingMidi = false;
            uint32_t lastChannels = 0xFFFF;
            float gainL = 0.0f, gainR = 0.0f;   // last block's gains, ramped from
            float audioL = 0.0f, audioR = 0.0f;
        };
        struct MeterState
        {
            std::atomic<float> peak[2], rms[2];
        };

        // One block of up to `block` frames at `at`; the lock must be held.
        void renderBlock (float* left, float* right, int frames, Frame at, bool play, bool withLive);
        Frame lengthLocked() const;
        void releaseHeld (Track&, std::vector<NoteEvent>&);
        void meter (int index, const float* left, const float* right, int frames);

        mutable std::mutex lock;   // tracks: held by render and by changes
        std::atomic<double> rate { 48000.0 };
        std::atomic<int> tracks { 2 };
        std::array<Track, maxTracks> lanes;
        std::array<Strip, maxTracks> strips;
        std::array<MeterState, maxTracks + 1> meters {};

        std::atomic<bool> playing { false }, stopPending { false };
        std::atomic<Frame> pos { 0 };
        Frame expected = -1;       // where the last block ended: anything else is a jump
        std::atomic<Frame> loopStart { 0 }, loopEnd { 0 };
        float masterGain = 1.0f;

        std::atomic<int> armed { 0 };
        std::mutex liveLock;
        std::vector<NoteEvent> liveQueue, liveTaken;
        std::atomic<bool> panicPending { false };

        std::vector<float> mixL, mixR, trackL, trackR, instL, instR, outL, outR;
        std::vector<NoteEvent> events;
    };
}
