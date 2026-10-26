// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// What plays a mixer track's MIDI: a MIDI file on the track, a MIDI keyboard, or
// both. Each instrument turns notes into stereo audio. Plain C++: the built-in
// Synth, a Sampler and a Slicer are here; plugin instruments (VST3, LV2, CLAP) are
// made by the engine (AudioEngine::loadInstrumentPlugin).
#include "../core/PhiTuning.h"
#include "../sampler/SamplerEngine.h"
#include "../sampler/SlicerEngine.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace phi { class Tuning; }

namespace mixer
{
    // A channel message at a frame within the block being rendered.
    struct NoteEvent
    {
        int offset = 0;
        uint8_t status = 0, data1 = 0, data2 = 0;
    };

    class Instrument
    {
    public:
        enum class Kind { synth, sampler, slicer, plugin, raga };

        virtual ~Instrument() = default;
        virtual Kind kind() const = 0;
        virtual std::string name() const = 0;
        virtual void prepare (double sampleRate, int maxBlock) = 0;
        // Audio thread: plays the events (in offset order) and adds its sound to left and right.
        virtual void process (const NoteEvent* events, int numEvents, float* left, float* right, int frames) = 0;
        virtual void releaseAll() = 0;   // every note let go, tails ring on (stop)
        virtual void reset() = 0;        // silent at once (locate, mix down)
        virtual bool hasWindow() const { return false; }   // a plugin's own editor
        virtual void showWindow() {}
        virtual void openWindow() {}   // shows the editor, or brings it to the front when it is open
        // Saved in a mix: the settings (a plugin's own state, the Synth's settings).
        virtual std::string state() const { return {}; }
        virtual void setState (const std::string&) {}
        // A plugin's parameter by its name ("Sample MIDI Chan3"), set to a value from 0 to 1.
        // False when there is no such parameter. UI thread.
        virtual bool setParameter (const std::string&, float) { return false; }
        // The tuning in force for this track (its playing clip's, else the scene's, else the
        // track's own; see Tunings.h), or nullptr for 12-TET. UI thread, whenever it changes.
        // Only the PHI synth retunes; the others play 12-TET.
        virtual void setTuning (std::shared_ptr<const phi::Tuning>) {}
        // When the tuning in force is a raga: its index in phi::builtInRagas(), its Sa (0 C to 11 B)
        // and whether it is the equal-tempered version; -1 when it is not a raga. UI thread. PHI
        // rāga plays it; the others don't use it.
        virtual void setRaga (int /*raga*/, int /*root*/, bool /*equalTempered*/) {}
    };
    using InstrumentPtr = std::shared_ptr<Instrument>;

    // For instruments that take notes one at a time: renders between the events.
    class NoteInstrument : public Instrument
    {
    public:
        void process (const NoteEvent* events, int numEvents, float* left, float* right, int frames) override;

    protected:
        virtual void handle (const NoteEvent&) = 0;
        virtual void render (float* left, float* right, int frames) = 0;   // adds
    };

    //==========================================================================
    // The built-in Synth, so a MIDI file sounds without loading anything: a simple
    // polyphonic synth (sine, triangle, saw or square through a low-pass filter, with
    // an envelope), and General MIDI drums on channel 10 made from noise and sweeps.
    // Channel volume (CC7), expression (CC11), pan (CC10), sustain (CC64) and pitch
    // bend (±2 semitones, or what RPN 0 sets; MPE's zone when one is set up) follow the file.
    struct SynthSettings
    {
        enum Wave { sine, triangle, saw, square };
        int wave = saw;
        double attack = 0.005, decay = 0.25, sustain = 0.7, release = 0.25;   // seconds; sustain 0..1
        double cutoff = 4000.0;     // Hz, the low-pass; it opens with velocity
        double gainDb = -6.0;
        bool drums = true;          // channel 10 plays the drum kit
        int voices = 24;            // 1..64
    };

    class Synth final : public NoteInstrument
    {
    public:
        static constexpr int maxVoices = 64;

        Kind kind() const override { return Kind::synth; }
        std::string name() const override { return "Synth"; }
        void prepare (double sampleRate, int maxBlock) override;
        void releaseAll() override;
        void reset() override;
        std::string state() const override;
        void setState (const std::string&) override;

        void setSettings (const SynthSettings&);
        SynthSettings settings() const;

    protected:
        void handle (const NoteEvent&) override;
        void render (float* left, float* right, int frames) override;

    private:
        struct Voice
        {
            bool active = false, held = false, sustained = false;
            int channel = 0, note = 0, drum = 0;   // drum: 0 tone, else a kind of drum
            float velocity = 1.0f;
            double phase = 0.0, frequency = 440.0, sweep = 0.0;
            int stage = 0;                          // 0 attack, 1 decay, 2 sustain, 3 release
            double level = 0.0, drumDecay = 0.0, time = 0.0;
            double low2 = 0.0, band = 0.0, noiseLow = 0.0;
            unsigned age = 0;
        };
        struct Channel
        {
            float volume = 100.0f / 127.0f, expression = 1.0f, pan = 0.0f;
            bool pedal = false;
        };
        void noteOn (int channel, int note, float velocity);
        void noteOff (int channel, int note);
        void resetMpe();
        Voice& freeVoice();
        float noise();

        mutable std::mutex lock;   // the settings
        SynthSettings current, live;
        double rate = 48000.0;
        std::array<Voice, maxVoices> voice {};
        std::array<Channel, 16> channel {};
        phi::Mpe mpe;
        unsigned clock = 0;
        uint32_t seed = 22222;
    };

    //==========================================================================
    // A track's own Sampler or Slicer (sampler/), played by the track's MIDI. The
    // Sampler and Slicer windows can edit them; the sound comes from a file or from
    // the instrument windows of the Sample menu.
    class SamplerInstrument final : public NoteInstrument
    {
    public:
        Kind kind() const override { return Kind::sampler; }
        std::string name() const override { return "Sampler"; }
        void prepare (double sampleRate, int) override { sound.prepare (sampleRate); }
        void releaseAll() override { sound.allNotesOff(); }
        void reset() override { sound.panic(); }
        sampler::Engine& engine() { return sound; }
        std::string samplePath;   // where its sound came from, if a file

    protected:
        void handle (const NoteEvent&) override;
        void render (float* left, float* right, int frames) override { sound.render (left, right, frames); }

    private:
        sampler::Engine sound;
    };

    class SlicerInstrument final : public NoteInstrument
    {
    public:
        Kind kind() const override { return Kind::slicer; }
        std::string name() const override { return "Slicer"; }
        void prepare (double sampleRate, int) override { sound.prepare (sampleRate); }
        void releaseAll() override;
        void reset() override { sound.panic(); }
        slicer::Engine& engine() { return sound; }
        std::string samplePath;

    protected:
        void handle (const NoteEvent&) override;
        void render (float* left, float* right, int frames) override { sound.render (left, right, frames); }

    private:
        slicer::Engine sound;
    };
}
