// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The mixer's sound: up to 8 tracks summed into a master out. Every track is the
// same kind of track: a timeline of clips, each an audio file or a MIDI file, placed
// one after another or overlapping, plus an instrument. Audio clips play as they
// are; MIDI clips and a MIDI keyboard play the instrument. Each track then goes
// through its strip: trim, polarity, low and high cut, the inserts (effects), width,
// pan, fader, mute and solo. No MIDI editing.
// The clip launcher (docs/clip-launcher.md) plays the same tracks from a grid of slots,
// one clip per track at a time, launched on the beat.
// Plain C++, no JUCE or Qt: the UI sets things from its thread, render() runs on
// the audio thread, mixdown() on a worker thread.
#include "Effect.h"
#include "Instruments.h"
#include "MidiFile.h"
#include "Spatial.h"
#include "Tunings.h"
#include "../core/AudioData.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mixer
{
    constexpr int maxTracks = 8;
    constexpr int masterIndex = maxTracks;   // meters: 0..7 the tracks, 8 the master
    constexpr int numReturns = 2;            // A (a reverb) and B (a delay), fed by each track's sends
    constexpr int returnIndex = masterIndex + 1;   // meters: 9 return A, 10 return B
    // The Pre-Master (owner, 2026-10-28): a bus the tracks can go to instead of the master,
    // with its own inserts, sends, width, pan, fader and mute, then into the master.
    constexpr int busIndex = returnIndex + numReturns;   // meters: 11 the Pre-Master
    constexpr int busTrack = maxTracks;                  // inserts() and setInserts(): 8 the Pre-Master's
    constexpr int maxBusInserts = 4;

    // A source's 3D panner, in Spatial mode (Spatial.h): its place in the scene.
    struct Panner3d
    {
        static constexpr float defaultSpread = 60.0f, returnSpread = 120.0f;   // a return's reverb wraps wider
        std::atomic<float> azimuth { 0.0f }, elevation { 0.0f }, spread { defaultSpread }, distance { 1.0f };
        Panner3d() = default;
        explicit Panner3d (float spreadDegrees) : spread (spreadDegrees) {}
        void set (const spatial::Place& p) { azimuth = p.azimuth; elevation = p.elevation; spread = p.spread; distance = p.distance; }
        spatial::Place place() const { return { azimuth.load(), elevation.load(), spread.load(), distance.load() }; }
    };

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
    // A clip of frames (a take) as a MIDI file at a fixed tempo, 480 ticks a quarter, as long as the clip.
    midifile::SongPtr songOf (const MidiClip&, double sampleRate, double bpm);

    // A clip on a track's timeline: an audio file or a MIDI file (one of the two),
    // the part of it heard, and where that part starts.
    struct Clip
    {
        AudioClipPtr audio;
        MidiClipPtr midi;
        Frame start = 0;     // on the timeline
        Frame offset = 0;    // into the file: what is trimmed off the front
        Frame length = 0;    // heard from there
        bool muted = false;
        // Played backwards: offset and length then count in the reversed file. An audio clip
        // is read backwards here; a MIDI clip's file is given reversed (reverseMidi).
        bool reversed = false;
        uint32_t id = 0;     // for the UI, to know a clip again after edits; not used here
        Frame end() const { return start + length; }
        Frame sourceLength() const { return audio != nullptr ? audio->length() : midi != nullptr ? midi->length : 0; }
        bool isMidi() const { return midi != nullptr; }
    };
    using Clips = std::vector<Clip>;

    // The MIDI file backwards: each note keeps its length and velocity, ending where it began
    // mirrored; the set-up at the very start (programs, controllers) stays at the start.
    MidiClipPtr reverseMidi (const MidiClip&);
    Clip makeClip (AudioClipPtr, Frame start);   // the whole file
    Clip makeClip (MidiClipPtr, Frame start);

    // The clip launcher: a slot for each track in each scene (a row of the grid).
    constexpr int maxScenes = 32;
    constexpr int stopQueued = -2;   // LaunchInfo::queued: the track stops at the next quantise

    // What a slot holds: a clip, and how it plays when launched.
    struct Slot
    {
        Clip clip;                 // its start is not used: the launch places it
        bool loop = true;          // else it plays once and the track stops
        double quantise = -1.0;    // beats to launch on: -1 the launcher's, 0 at once
        TuningChoice tuning;       // the PHI synth's tuning while it plays, else the scene's
    };
    using SlotPtr = std::shared_ptr<const Slot>;

    // What a track's launcher is doing, for the UI.
    struct LaunchInfo
    {
        int playing = -1;          // the scene whose slot plays, -1 none
        int queued = -1;           // the scene waiting to launch, -1 none, or stopQueued
        int passes = 0;            // times the playing clip has gone round
        float progress = 0.0f;     // through the current pass, 0..1
        bool timeline = true;      // the track plays its timeline clips, not the launcher
    };

    // Recording (docs/recording.md). The audio input, read on the audio thread each block:
    // as many stereo frames as have come in, up to maxFrames, interleaved.
    class Input
    {
    public:
        virtual ~Input() = default;
        virtual int read (float* interleaved, int maxFrames) = 0;
    };

    // A take asked for: on a track, into a slot or onto the timeline.
    struct RecordRequest
    {
        int track = 0;
        int scene = -1;              // the slot; -1 the timeline, from where the transport is
        double beats = 16.0;         // how long; 0 until stopRecording
        bool midi = true, audio = false;   // what it records: the keyboard, the audio input
        bool fromInstrument = false; // the audio from the track's own instrument (before its strip), not the input
        // Into the slot's playing clip, pass after pass, until stopRecording. The UI gives the
        // clip to write into (its own copy, already in the slot): overdubAudio or overdubMidi.
        bool overdub = false;
        std::shared_ptr<AudioClip> overdubAudio;
        std::shared_ptr<MidiClip> overdubMidi;
        // A layer (owner, 2026-10-26): the slot's clip gets its other half, audio under MIDI or
        // MIDI over audio, one pass of the loop long, so both play together. The UI puts the new,
        // empty layer in the slot first and gives it here as overdubAudio or overdubMidi. It
        // starts on the quantise (the slot is launched for it if it is not playing) and ends by
        // itself.
        bool layer = false;
        bool playAfter = true;       // a slot's take loops from the frame it ends on, else the track stops
        double quantise = 0.0;       // the notes onto this grid, in beats; 0 as played
        double maxSeconds = 120.0;   // the room a take of no set length has (the timeline's has no end)
    };

    enum class RecordState { none, waiting, recording, overdubbing };
    struct RecordInfo
    {
        RecordState state = RecordState::none;
        int scene = -1;              // the slot, -1 the timeline
        float progress = 0.0f;       // through a take of set length, 0..1
        Frame recorded = 0;          // frames so far
        Frame length = 0;            // the take's, 0 none set
        Frame start = 0;             // where it began: on the timeline, or on the launch clock
    };

    // A take that ended. A slot's is in the slot already (playing, when asked); the UI saves it
    // as files and gives the clips their paths (and the MIDI its song), which the audio thread
    // never reads. A timeline take's clips are for the UI to place, at `start`.
    struct Recorded
    {
        int track = 0, scene = -1;
        bool overdub = false;
        bool layer = false;   // the slot's other layer (RecordRequest::layer)
        AudioClipPtr audio;   // null: none recorded
        MidiClipPtr midi;     // null: none recorded
        Frame start = 0, length = 0;
        int pass = 0;                // a timeline take over a loop: each pass is a take of its own
        bool notes = false;          // the MIDI has a note in it
    };

    // What a track plays (owner, 2026-10-24): its audio clips only, its instrument only (the
    // MIDI clips and the keyboard), or both as two layers, each with its own level, one of
    // which can duck the other (a kick on the instrument pumping a drum loop in the audio).
    enum class TrackMode { hybrid = 0, audio = 1, instrument = 2 };

    // A track's controls, set from any thread.
    struct Strip
    {
        std::atomic<int> mode { (int) TrackMode::hybrid };
        // Hybrid: each layer's level, and the duck: the source layer's level pushes the other
        // down by up to duckDb (reached when the source peaks at -6 dBFS), with attack and release.
        std::atomic<float> audioLayerDb { 0.0f }, instrumentLayerDb { 0.0f };   // -inf (below -96) .. +6
        std::atomic<float> duckDb { 0.0f };          // 0 (off) .. 48
        std::atomic<bool> audioDucksInstrument { false };   // else the instrument ducks the audio
        // The key: -1 the other layer (a hybrid track), or another track whose sound (after its
        // fader) ducks this whole track, in any mode. duckBelowHz > 0 ducks only below it.
        std::atomic<int> duckKey { -1 };
        std::atomic<float> duckBelowHz { 0.0f };
        std::atomic<float> duckAttackMs { 1.0f }, duckReleaseMs { 150.0f };
        std::atomic<float> trimDb { 0.0f };     // ±24, before everything
        std::atomic<float> faderDb { 0.0f };    // -inf (below -96) .. +6
        std::atomic<float> pan { 0.0f };        // -1 left .. 1 right: equal power for mono audio, balance otherwise
        std::atomic<bool> invertLeft { false }, invertRight { false };
        std::atomic<bool> mute { false }, solo { false };
        std::atomic<int> transpose { 0 };       // semitones for the MIDI clips; channel 10 (drums) is left alone
        std::atomic<uint32_t> channels { 0xFFFF };   // MIDI channels of the clips that play, a bit each
        // Pre: a low cut and a high cut after trim and polarity (Butterworth, 12 or 24 dB an
        // octave), and a delay of the track's clips against the others, ± milliseconds.
        std::atomic<bool> lowCut { false }, highCut { false };
        std::atomic<float> lowCutHz { 80.0f }, highCutHz { 12000.0f };
        std::atomic<int> lowCutSlope { 12 }, highCutSlope { 12 };   // dB an octave: 12 or 24
        std::atomic<float> delayMs { 0.0f };    // ±maxDelayMs: + later, - earlier
        // After the inserts: the stereo width, 0 mono .. 1 as it is .. 2 twice as wide.
        // Tracks of mono files only have none.
        std::atomic<float> width { 1.0f };
        // Sends to the returns A and B (Ableton): after the fader (and pan) unless `sendPre`,
        // then after the inserts and width but before the fader. Muting the track mutes them.
        std::array<std::atomic<float>, numReturns> sendDb { -100.0f, -100.0f };   // -inf (below -96) .. 0
        std::array<std::atomic<bool>, numReturns> sendPre { false, false };
        // Recording: what a take on the track records (InputKind), and whether the audio input is
        // heard through the track (Monitor: auto while the track is armed and records audio).
        // inputInstrument: the track's own instrument, played from the keyboard, recorded as audio.
        enum InputKind { inputAuto = 0, inputMidi = 1, inputAudio = 2, inputBoth = 3, inputInstrument = 4 };
        enum Monitor { monitorAuto = 0, monitorOn = 1, monitorOff = 2 };
        std::atomic<int> input { inputAuto };       // auto: the keyboard with an instrument (not an Audio track), else the audio input
        std::atomic<int> monitor { monitorAuto };
        std::atomic<int> inputChannels { 0 };       // 0 the stereo pair, 1 the left alone, 2 the right alone (both sides)
        std::atomic<bool> toBus { false };          // after the fader into the Pre-Master, not the master
        Panner3d placed;                            // in Spatial mode, in place of pan and width
    };

    // The Pre-Master's controls; its inserts are set on the engine (busTrack).
    struct BusStrip
    {
        std::atomic<float> trimDb { 0.0f };     // ±24, before the inserts
        std::atomic<float> faderDb { 0.0f };    // -inf (below -96) .. +6
        std::atomic<float> pan { 0.0f };        // balance
        std::atomic<float> width { 1.0f };      // 0 mono .. 2 twice as wide
        std::atomic<bool> mute { false };
        std::array<std::atomic<float>, numReturns> sendDb { -100.0f, -100.0f };
        std::array<std::atomic<bool>, numReturns> sendPre { false, false };
        Panner3d placed;
    };

    // A return's controls: its effect (fully wet) is set on the engine.
    struct ReturnStrip
    {
        std::atomic<float> faderDb { 0.0f };
        std::atomic<float> pan { 0.0f };    // balance
        std::atomic<bool> mute { false };
        Panner3d placed { Panner3d::returnSpread };
    };
    constexpr float maxDelayMs = 500.0f;
    constexpr float minCutHz = 20.0f, maxCutHz = 20000.0f;

    // An effect in a track's inserts, before the width, pan and fader.
    constexpr int maxInserts = 8;
    struct Insert
    {
        EffectPtr effect;
        bool bypassed = false;
        float mix = 1.0f;   // 0..1: what the effect gives, against what went in (parallel)
    };
    using Inserts = std::vector<Insert>;

    // The patchbay (patchbay/PatchBay.h) routes the tracks while it is on. It says in
    // which order to play them, adds what is patched into a track (before trim, pan and
    // fader), and takes each track's sound, which then goes where it is patched instead
    // of into the mixer's own sum. Called on the audio thread, never during a mix down.
    class Patch
    {
    public:
        virtual ~Patch() = default;
        virtual int trackAt (int k) const = 0;   // the k-th track to play: 0..maxTracks-1, each once
        // `offset`: where these frames start in the block the patchbay asked the mixer for.
        virtual void trackInput (int track, int offset, float* left, float* right, int frames) = 0;   // adds
        virtual void trackOutput (int track, int offset, const float* left, const float* right, int frames) = 0;
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

        // A track's clips (kept in order of their starts) and its instrument; null
        // clears it. Safe while playing: notes the old clips held are let go.
        void setClips (int track, Clips);
        Clips clips (int track) const;
        void setInstrument (int track, InstrumentPtr);   // prepared here
        InstrumentPtr instrument (int track) const;
        // The track's inserts, in order (up to maxInserts; busTrack: the Pre-Master's, up to maxBusInserts). The effects are prepared here;
        // the ones no longer used are let go after the swap, on the calling thread.
        void setInserts (int track, Inserts);
        Inserts inserts (int track) const;
        // Whether the track only has mono files: then it pans with equal power and has no width.
        bool monoTrack (int track) const;
        Strip& strip (int track) { return strips[(size_t) track]; }
        // The returns: what the sends feed, summed, through an effect (null: as it is), pan,
        // fader and mute, into the master (or, with the patchbay, its Mix Bus).
        void setReturnEffect (int index, EffectPtr);   // prepared here
        EffectPtr returnEffect (int index) const;
        ReturnStrip& returnStrip (int index) { return returnStrips[(size_t) index]; }
        BusStrip& busStrip() { return bus; }
        const BusStrip& busStrip() const { return bus; }
        bool anyToBus() const;   // a track goes to the Pre-Master
        const Strip& strip (int track) const { return strips[(size_t) track]; }

        std::atomic<float> masterDb { 0.0f };
        // Every track's inserts, or every send, left out at once; the mix keeps them. Bounces
        // and mix downs follow them too: they render what you hear.
        std::atomic<bool> bypassInserts { false }, bypassSends { false };

        // Spatial mode (Spatial.h): the tracks (but the ones sent to the Pre-Master), the
        // Pre-Master and the returns go into one ambisonic scene by their 3D panners, which the
        // decoder turns into the headphone mix (the stereo out, as the master is otherwise) and
        // the speakers' feeds. Without a decoder the mix stays stereo. A bounce stays stereo.
        std::atomic<bool> spatialOn { false };
        void setDecoder (std::shared_ptr<spatial::Decoder>);   // UI thread; the old one is let go here
        std::shared_ptr<spatial::Decoder> decoder() const;
        // The speakers' feeds the last blocks made, added to the device's outputs from the decoder's
        // first output on (outs: numOuts channels, frames long). On the audio thread, after render().
        void takeSpeakers (float* const* outs, int numOuts, int frames);

        // Transport.
        void play();
        void stop();               // instruments let go of their notes
        bool isPlaying() const { return playing.load(); }
        Frame position() const { return pos.load(); }
        void setPosition (Frame);
        Frame length() const;      // where the last clip ends
        Frame trackEnd (int track) const;
        // Loops [start, end) while on; end <= start turns it off.
        void setLoop (Frame start, Frame end);
        bool isLooping() const { return loopEnd.load() > loopStart.load(); }

        // The clip launcher. Setting a slot is an edit; launching is for playing live and
        // never holds up the audio. A launch waits for the next quantise of the launch
        // clock, which runs while the transport plays and starts again at 0 with it; when
        // stopped, a launch starts the transport and plays at once. Launching takes the
        // track off its timeline clips until backToTimeline.
        void setSlot (int track, int scene, SlotPtr);   // null empties it; an emptied playing slot stops
        SlotPtr slot (int track, int scene) const;
        void launch (int track, int scene);   // -1 stops the track's clip
        void launchScene (int scene);         // every track's slot in it; tracks with none stop
        // A launch on a given frame of the launch clock while it runs: a loop just recorded
        // starts on the bar line it was recorded up to. One in the past plays from where it
        // has got to since, in time with that frame. Scene -1 (or an empty slot) stops the
        // track on that frame: Architectonics ends a set on its last bar.
        void launchAt (int track, int scene, Frame clockFrame);
        void stopTrack (int track) { launch (track, -1); }
        void stopAll();                       // every track's clip, at the next quantise
        void backToTimeline (int track);      // -1: every track
        LaunchInfo launchInfo (int track) const;
        Frame launchClock() const { return clock.load(); }
        std::atomic<double> tempo { 120.0 };          // for the quantise, in 4/4
        std::atomic<double> launchQuantise { 4.0 };   // beats; 0 launches at once

        // A MIDI keyboard plays the armed track's instrument (-1: none). Any thread.
        void setLiveTrack (int track) { armed = track; }
        int liveTrack() const { return armed.load(); }
        // `heard` false: not shown to the live listener (the MIDI editor's own auditions and playback).
        void liveMidi (uint8_t status, uint8_t data1, uint8_t data2, bool heard = true);
        // Sees each message liveMidi() is given, on the thread that gives it (the MIDI thread):
        // the MIDI editor's step input. Null for none. Any thread.
        void setLiveListener (std::function<void (uint8_t, uint8_t, uint8_t)>);
        void panic();   // every instrument silent
        // Recording (docs/recording.md): a track records what the MIDI keyboard plays on it and
        // what comes in at the audio input, into a launcher slot or onto its timeline. Everything
        // is decided on the audio thread, to the frame: a slot's take starts on the quantise,
        // stops by itself at its length and plays on from that very frame. The UI asks with
        // record(), which allocates the take's room first, and keeps what comes back from
        // takeRecorded() as files.
        void setInput (Input*);   // null: no audio input; returns once the audio thread has let go of the last
        bool record (const RecordRequest&, std::string& error);   // UI thread
        // A slot's take of no set length ends at the next quantise (its length whole quantise
        // steps); one of set length ends by itself, and no next take follows it; an overdub or a
        // timeline take ends at once. -1: every track.
        void stopRecording (int track);
        RecordInfo recordInfo (int track) const;
        bool isRecording() const;   // a take waits, records or overdubs on any track
        std::vector<Recorded> takeRecorded();   // the takes that ended since, oldest first; UI thread
        // The audio input arrives this much later than it was played (the device's delay):
        // takes are moved earlier by it. Milliseconds.
        std::atomic<float> inputLatencyMs { 10.0f };
        // Capture (Ableton's Capture MIDI): the keyboard is always kept, armed or not, recording
        // or not. capture() makes a clip of the last phrase played on the track (back to a rest
        // of two bars): with the transport playing, whole bars on the launch clock (1, 2, 4, 8,
        // 16 or 32 of them) and `phase` the clock frame its start fell on, for launchAt; stopped,
        // from its first note to whole bars after it. Null when nothing was played. UI thread.
        MidiClipPtr capture (int track, Frame& phase);

        // The metronome: a click on each beat while the transport plays (the first of each bar
        // higher), and during a count-in. Not in mix downs.
        std::atomic<bool> metronome { false };
        std::atomic<float> metronomeDb { -6.0f };
        std::atomic<int> beatsPerBar { 4 };   // the time signature's top; quantise stays in beats
        // play() after a count-in of so many bars of clicks (0: none); the transport is still
        // stopped meanwhile. countingIn() says it is counting.
        void playWithCountIn (double bars);
        bool countingIn() const { return countLeft.load() > 0; }

        Meter takeMeter (int index);   // a track (0..7), the master (8), a return (9, 10) or the Pre-Master (11), after the fader

        // With a patch, render() adds only the returns: the tracks go to the patch, which
        // does the master (masterDb) and its meter (meterMaster), and adds what render() gave
        // into its Mix Bus. Null: the usual sum.
        void setPatch (Patch* p) { patch = p; }
        void meterMaster (const float* left, const float* right, int frames) { meter (masterIndex, left, right, frames); }
        // How many frames the next block may have: up to `wanted`, ending at the loop end.
        int nextBlock (int wanted) const;

        // Audio thread: adds the mix to left and right.
        void render (float* left, float* right, int frames);

        // Renders the whole mix from the start, stereo at the mixer's rate, with the
        // instruments' tails (up to `tailSeconds` of sound after the last clip ends).
        // With `onlyTrack` it is a bounce: that track alone, from the start of the mix to
        // its own last clip's end, before its fader and pan (with its cuts, inserts and
        // width), whatever mute and solo say.
        // Live playback is silent meanwhile. Worker thread.
        AudioDataPtr mixdown (double tailSeconds, std::string& error,
                              std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr,
                              int onlyTrack = -1);

    private:
        static constexpr int block = 256;

        struct Track
        {
            Clips clips;
            InstrumentPtr instrument;
            std::vector<size_t> cursors;   // each MIDI clip's next event
            bool relocate = true;      // the cursors need finding again
            std::array<std::array<uint8_t, 128>, 16> held {};    // MIDI clip notes sounding: the note played + 1
            std::array<std::array<uint16_t, 128>, 16> owner {};  // and the clip that played it
            bool wasPlayingMidi = false;
            uint32_t lastChannels = 0xFFFF;
            float gainL = 0.0f, gainR = 0.0f;   // last block's gains, ramped from: stereo and instrument
            float monoL = 0.0f, monoR = 0.0f;   // mono audio, equal power

            // The launcher: the slots, the one playing and the one waiting, in launch clock frames.
            std::array<SlotPtr, maxScenes> slots;
            SlotPtr playing, queued;
            int playingScene = -1, queuedScene = -1;
            Frame launchedAt = 0, queuedAt = 0;
            bool timeline = true;
            bool launchChanged = false;       // notes held so far are let go
            Clips passClips = Clips (2);      // the playing clip's pass and the next, by pass parity
            std::vector<size_t> passCursors = std::vector<size_t> (2, 0);

            // The strip's processing.
            Inserts inserts;
            bool mono = false;                // only mono files: equal-power pan, no width
            Frame delay = 0;                  // the clips' delay used last, in frames
            struct Biquad
            {
                float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
                float z[2][2] {};             // per side
                void process (float* l, float* r, int frames);
            };
            struct Cut
            {
                bool on = false;
                float hz = 0.0f;
                int slope = 0;
                std::array<Biquad, 2> stages; // one for 12 dB, both for 24
                void update (bool on, float hz, int slope, bool high, double rate);
                void process (float* l, float* r, int frames);
            };
            Cut lowCut, highCut;
            float preL = 0.0f, preR = 0.0f, preMono = 0.0f;   // last block's trim and polarity gains
            float postL = 0.0f, postR = 0.0f;   // and pan and fader gains, when the strip processes
            float width = 1.0f;
            std::array<float, numReturns> sendGain {};   // last block's send gains
            float audioLayer = 1.0f, instrumentLayer = 1.0f;   // last block's layer gains
            float duckEnvelope = 0.0f;
            Cut duckSplit;                    // its low-pass, below which the duck works
            float duckState[5][2] {};         // per ducked side: the instrument's, the audio's, mono

            // Recording: the take on this track, made in record() and run on the audio thread.
            struct Take
            {
                RecordRequest request;
                RecordState state = RecordState::none;
                bool timeline = false;
                std::shared_ptr<AudioClip> audio;   // a slot's sound, written as it comes, with room for all of it
                std::shared_ptr<MidiClip> midi;     // its notes, with room reserved
                std::shared_ptr<Slot> slot;         // what the slot gets once it ends
                Frame startAt = -1;                 // on the launch clock (a slot) or the timeline; -1 once the transport starts
                Frame length = 0;                   // frames; 0 until stopped
                Frame endAt = -1;                   // once known
                Frame lastPos = -1;                 // a timeline take: where the last block ended (else a new pass)
                bool stopAsked = false;
                bool notes = false;
                int pass = 0;
                Frame phase = 0;                    // a layer: where in the loop it began
                std::array<std::array<Frame, 128>, 16> on;      // notes held: where they began in the take, -1 none
                std::array<std::array<Frame, 128>, 16> shift;   // and how far quantising (or playing early) moved them
                Frame room = 0;                     // frames the take has room for
                // The next take, asked for while this one records: it starts the frame this one ends.
                int nextScene = -1;
                std::shared_ptr<AudioClip> nextAudio;
                std::shared_ptr<MidiClip> nextMidi;
                std::shared_ptr<Slot> nextSlot;
                // The audio of a take that has ended, still coming in late by the input's delay.
                std::shared_ptr<AudioClip> tailAudio;
                Frame tailStart = 0, tailTo = -1, tailLength = 0;
                int tailChannels = 0;
                Recorded done;                      // for the UI once liveLock is free (and the tail is in)
                SlotPtr replaced;                   // the slot it took the place of, freed by the UI
                bool reporting = false;
            };
            Take take;
        };
        struct LaunchRequest
        {
            int track = 0, scene = -1;
            bool now = false;          // the transport was stopped: no waiting for the quantise
            bool toTimeline = false;
            Frame at = -1;             // launchAt: on this frame of the launch clock
        };
        struct LaunchShared
        {
            std::atomic<int> playing { -1 }, queued { -1 }, passes { 0 };
            std::atomic<float> progress { 0.0f };
            std::atomic<bool> timeline { true };
        };
        struct MeterState
        {
            std::atomic<float> peak[2], rms[2];
        };
        struct RecordShared
        {
            std::atomic<int> state { 0 }, scene { -1 };
            std::atomic<float> progress { 0.0f };
            std::atomic<Frame> recorded { 0 }, length { 0 }, start { 0 };
        };
        // A timeline take goes to the UI as it comes: its audio through a ring, its notes and
        // its passes as marks, gathered in takeRecorded().
        struct Mark
        {
            enum Kind : uint8_t { note, pass, end } kind = note;
            int track = 0;
            Frame frame = 0;        // on the timeline
            int64_t audioAt = 0;    // frames of the take's audio sent before it
            uint8_t status = 0, data1 = 0, data2 = 0;
        };
        // The keyboard as played, for capture().
        struct Kept
        {
            Frame live = 0, clock = 0;
            uint32_t epoch = 0;     // which run of the transport `clock` belongs to; playing or not
            bool playing = false;
            int track = 0;
            uint8_t status = 0, data1 = 0, data2 = 0;
        };

        // One block of up to `block` frames at `at`; the lock must be held.
        void renderBlock (float* left, float* right, int frames, Frame at, bool play, bool withLive, int blockOffset = 0);
        Frame lengthLocked() const;
        Frame trackEndLocked (int track) const;
        Frame delayFrames (int track) const;   // the strip's delay, at the mixer's rate
        static bool monoClips (const Clips&);
        void releaseHeld (Track&, std::vector<NoteEvent>&, int offset = 0, int clip = -1);   // -1: every clip's
        // Launcher, with the lock held, on the audio thread.
        void takeLaunches (bool play);    // requests, and queued launches that are due
        Frame quantiseFrom (double beats) const;   // the next launch frame for that quantise
        void placePasses (Track&, Frame at);       // passClips for the block at `at`
        void endPasses (int frames);      // after a block: one-shot clips that ended, the UI's info
        bool launcherBusy() const;
        void meter (int index, const float* left, const float* right, int frames);
        void runInserts (Inserts&, float* left, float* right, int frames);   // in order, each at its Mix
        void renderBus (int frames, bool jumped, bool withLive, bool spatialMix);   // busL and busR through the Pre-Master, into the mix or the scene
        void place (const float* left, const float* right, int frames, const Panner3d&, spatial::Gains& last, bool jumped);   // into the scene
        void feedSpeakers (int frames);   // the decoder's feeds, at the master's level, into the speakers' queue
        void layers (int index, Track&, const Strip&, TrackMode, bool hasMono, int frames, bool jumped);
        // Recording, on the audio thread with the lock held.
        bool recordsAudio (int track) const;
        void endLayer (int track, Track::Take&, Frame length);   // for the UI, with what it has (0: nothing)
        void takeInput (int frames);                       // the next block's audio input into inL and inR
        void recordBlock (Frame clockAt, Frame posAt, int frames, bool play);
        void recordNote (Track&, Frame frame, const NoteEvent&, Frame length);   // into a slot take's clip
        void finishSlotTake (int index, Track&);
        void report (int index, Track&);                   // the take's Recorded to the UI, when liveLock is free
        void click (float* left, float* right, int frames, Frame from, double beatFrames);
        void resetClock();

        mutable std::mutex lock;   // tracks: held by render and by changes
        std::atomic<double> rate { 48000.0 };
        std::atomic<int> tracks { 2 };
        std::array<Track, maxTracks> lanes;
        std::array<Strip, maxTracks> strips;
        std::array<MeterState, maxTracks + 1 + numReturns + 1> meters {};
        BusStrip bus;
        Inserts busInserts;
        std::vector<float> busL, busR;
        float busTrim = 1.0f, busGainL = 1.0f, busGainR = 1.0f, busWidth = 1.0f;   // last block's
        std::array<float, numReturns> busSendGain {};
        // Spatial mode.
        std::shared_ptr<spatial::Decoder> spatialDecoder;
        std::array<std::vector<float>, spatial::channels> scene;
        std::array<float*, spatial::channels> sceneAt {};
        std::array<spatial::Gains, maxTracks> trackPlaces {};
        spatial::Gains busPlace {};
        std::array<spatial::Gains, numReturns> returnPlaces {};
        std::array<std::vector<float>, spatial::maxSpeakers> feeds;
        std::array<float*, spatial::maxSpeakers> feedAt {};
        static constexpr int queueFrames = 8192;
        std::vector<float> speakerQueue;    // interleaved, queueChannels a frame
        uint64_t queued = 0, taken = 0;     // frames written and read; audio thread only
        int queueChannels = 0, queueFirst = 0;
        float speakerGain = 1.0f;
        std::array<ReturnStrip, numReturns> returnStrips;
        std::array<EffectPtr, numReturns> returnEffects;
        std::array<std::array<float, 2>, numReturns> returnGain {};   // last block's, left and right
        std::array<std::vector<float>, numReturns> returnL, returnR;

        std::atomic<bool> playing { false }, stopPending { false };
        std::atomic<Frame> pos { 0 };
        Frame expected = -1;       // where the last block ended: anything else is a jump
        std::atomic<Frame> loopStart { 0 }, loopEnd { 0 };
        float masterGain = 1.0f;
        int only = -1;             // a bounce renders this track alone

        std::atomic<Patch*> patch { nullptr };
        std::atomic<int> armed { 0 };
        std::mutex liveLock;
        std::vector<NoteEvent> liveQueue, liveTaken;
        std::mutex listenerLock;
        std::function<void (uint8_t, uint8_t, uint8_t)> liveListener;   // under listenerLock
        std::atomic<Input*> input { nullptr };
        std::vector<float> inputFifo;                         // stereo, interleaved: what came in and is not yet used
        size_t inputHave = 0;
        std::vector<float> inL, inR, inScratch;               // the block's input
        // Each track's instrument as it sounded in the block, for a take that records it as audio.
        std::array<std::array<float, block>, maxTracks> sourceL {}, sourceR {};
        std::array<RecordShared, maxTracks> recordShared {};
        std::vector<Recorded> recordedOut;                    // under liveLock: takes that ended
        std::vector<SlotPtr> slotsOut;                        // under liveLock: slots a take took the place of, freed by the UI
        // The timeline take's stream: a ring the audio thread writes and the UI reads.
        std::vector<float> streamRing;
        std::atomic<uint64_t> streamWritten { 0 }, streamRead { 0 };
        std::vector<Mark> marks;                              // under liveLock
        uint64_t streamSent = 0;                              // audio thread: frames written into the ring for the take
        // The UI's side of a timeline take, filled in takeRecorded().
        struct Gathered
        {
            int track = -1;
            std::vector<float> audio;                         // stereo, interleaved, from the take's first frame
            std::vector<Mark> marks;
            bool audioOn = false, midiOn = false;
            bool fromInstrument = false;                      // no input delay, both sides
            double quantise = 0.0;
        };
        Gathered gathered;
        std::array<Kept, 8192> kept {};                       // a ring, written under the lock
        size_t keptNext = 0;
        Frame liveClock = 0;                                  // frames rendered since the start
        uint32_t epoch = 0;
        std::atomic<Frame> countLeft { 0 };
        Frame countTotal = 0;
        float clickPhase = 0.0f;
        int clickAt = -1;                                     // frames into the click sounding, -1 none
        float clickHz = 1000.0f, clickGain = 0.0f;
        std::atomic<bool> panicPending { false };

        std::vector<LaunchRequest> launchQueue, launchTaken;   // launchQueue under liveLock
        std::array<LaunchShared, maxTracks> launchShared {};
        std::atomic<Frame> clock { 0 };
        std::atomic<bool> clockReset { false };

        std::vector<float> mixL, mixR, trackL, trackR, instL, instR, monoBuf, outL, outR, sendPreL, sendPreR, audL, audR, dryL, dryR, duckGains;
        std::array<std::vector<float>, maxTracks> keyLevels;   // each track's sound, for ducks
        std::vector<NoteEvent> events;
    };
}
