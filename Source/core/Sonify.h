// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Image sonification: pictures, still or moving, played as notes. A picture comes in
// small (a few dozen pixels each way) and is read like a score: higher up is a higher
// note, brighter is louder, and change (from one column to the next, or from one frame
// to the next) strikes drums. The notes go out as MIDI messages for any instrument,
// each voice on a channel of its own (as MPE does), so that its pitch bend can carry a
// pitch between the notes of the scale: the quantize amount slides from the picture's
// own pitch (0) to the nearest note of the scale (1). Drums are General MIDI, on
// channel 10. Plain C++, no Qt or JUCE.
#include <cstdint>
#include <vector>

namespace sonify
{
    // A picture, row by row from the top: the brightness of each pixel, 0 to 1.
    struct Picture
    {
        int width = 0, height = 0;
        std::vector<float> light;

        float at (int x, int y) const { return light[(size_t) y * (size_t) width + (size_t) x]; }
        bool empty() const { return width <= 0 || height <= 0; }
    };

    // From 32-bit pixels in memory order B, G, R, x (QImage's Format_RGB32), averaged
    // down to `w` by `h`.
    Picture fromPixels (const uint8_t* pixels, int width, int height, int stride, int w, int h);

    // scan: a line sweeps across the picture, a column a step, and plays the bright
    // spots it crosses (for still pictures). lanes: the picture is cut into a lane per
    // voice, and each plays where its light is, gliding as it moves (for moving ones).
    enum class Mode { scan, lanes };

    enum class Scale { chromatic, major, minor, pentatonic, minorPentatonic, blues, dorian, wholeTone, count };
    const char* scaleName (Scale);

    struct Settings
    {
        Mode mode = Mode::scan;
        double quantize = 1.0;          // 0: the picture's own pitch (by pitch bend), 1: notes of the scale
        Scale scale = Scale::pentatonic;
        int root = 0;                   // the scale's first note as a pitch class, 0 = C
        int lowest = 36, highest = 84;  // MIDI notes at the bottom and the top of the picture
        double bpm = 120.0;             // a step is a sixteenth note; 0 holds still
        int columns = 16;               // scan: steps across the picture
        int voices = 4;                 // notes at once, 1 to maxVoices
        double threshold = 0.4;         // how bright a spot must be to sound, 0 to 1
        bool drums = true;              // change strikes drums on channel 10
        double bendRange = 2.0;         // the instrument's pitch bend range in semitones
    };

    constexpr int maxVoices = 8;        // on channels 1 to 8
    constexpr int drumChannel = 9;      // channel 10, counted from 0

    // A channel message; the channel is in the status.
    struct Message
    {
        uint8_t status = 0, data1 = 0, data2 = 0;
    };

    // Where a sounding note comes from in the picture, for drawing: 0 to 1 from the top left.
    struct Spot
    {
        float x = 0.0f, y = 0.0f, level = 0.0f;
    };

    // The pitch for a height in the picture (0 bottom, 1 top), as a fractional MIDI note,
    // pulled towards the scale by the quantize amount.
    double pitchFor (double height, const Settings&);
    double nearestInScale (double note, Scale, int root);

    class Sonifier
    {
    public:
        void setSettings (const Settings&);
        const Settings& settings() const { return set; }

        // Moves time on by `seconds` with the newest picture (the same one again for a
        // still picture) and adds what to play to `out`.
        void advance (const Picture&, double seconds, std::vector<Message>& out);
        // Lets every note go and centres the pitch bends.
        void stop (std::vector<Message>& out);

        // Scan: where the line is across the picture, 0 to 1; lanes: -1.
        double scanPosition() const;
        const std::vector<Spot>& sounding() const { return spots; }

    private:
        struct Voice
        {
            int note = -1;          // held, or -1
            double target = 0.0;    // the pitch it plays, fractional
            float energy = 0.0f;    // lanes: the lane's last brightness, for onsets
            float x = 0.0f, y = 0.0f, level = 0.0f;
        };

        void step (const Picture&, std::vector<Message>& out);
        void scanStep (const Picture&, std::vector<Message>& out);
        void laneStep (const Picture&, bool onStep, std::vector<Message>& out);
        void drumStep (const std::vector<float>& profile, std::vector<Message>& out);
        void play (int voice, double target, float level, std::vector<Message>& out);
        void release (int voice, std::vector<Message>& out);
        void bend (int channel, double semitones, std::vector<Message>& out);
        void collectSpots();

        Settings set;
        Voice voice[maxVoices];
        int lastBend[16] { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
        std::vector<int> drumsHeld;
        std::vector<float> lastDrumProfile;
        double clock = 0.0;     // seconds into the current step
        long steps = 0;         // steps taken
        std::vector<Spot> spots;
    };
}
