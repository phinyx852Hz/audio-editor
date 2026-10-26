// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// PHI rāga's voice: a modelled sitar. Each string is a waveguide (a delay line one period long,
// with its losses in the loop) whose length follows the pitch as it is pulled, so mīṇḍ and
// āndolan bend the string, not a recording. The jawārī, the sitar's curved bridge, is the
// string's length shortening as it presses the bridge, which spreads its energy into the bright
// buzz; 0 is an open (khulā) bridge, 1 a closed (band) one. The tarab strings under the frets ring
// in sympathy with what is played, tuned to the raga; two chikārī strings are struck for rhythm.
// The gourd is two resonances. Plain C++ (phi_dsp); a few percent of a core at most.
#include <array>
#include <cstdint>
#include <vector>

namespace phi
{
    class SitarString
    {
    public:
        void prepare (double sampleRate);
        void pluck (double hz, double velocity, double brightness = 0.7);
        void setHz (double hz) { target = hz; }   // glides there over a few milliseconds
        void tune (double hz) { current = target = hz; }   // at once, for a string at rest
        void setDecay (double seconds);            // T60 at the fundamental
        void setJawari (double amount) { jawari = amount; }
        void excite (float input) { drive = input; }   // added into the loop next tick (sympathy)
        float tick();
        double hz() const { return current; }
        bool ringing() const { return energy > 1.0e-7f; }
        void silence();

    private:
        std::vector<float> line;
        int write = 0;
        double rate = 48000.0, current = 220.0, target = 220.0, glide = 0.0;
        double decaySeconds = 4.0, jawari = 0.6;
        float low = 0.0f, brightness = 0.6f, drive = 0.0f, energy = 0.0f;
        float dcIn = 0.0f, dcOut = 0.0f, dcPole = 0.997f;   // keeps the loop free of DC
        // The pluck: a short burst fed into the loop.
        std::vector<float> burst;
        int burstAt = 0;
        uint32_t seed = 12345;
    };

    class Sitar
    {
    public:
        static constexpr int maxPlayed = 4, maxTarab = 13;

        void prepare (double sampleRate);
        // A played string: plucked at `hz`; returns its number for setHz and release.
        int pluck (double hz, double velocity);
        void setHz (int string, double hz);
        void release (int string);                  // damped by the fretting finger lifting
        void setTarab (const std::vector<double>& hz);   // up to 13
        void strikeChikari (double velocity);
        void setChikari (double saHz);              // tuned to Sa an octave and two octaves up
        void setJawari (double amount);
        void setTarabLevel (double level) { tarabLevel = (float) level; }
        void process (float* left, float* right, int frames);   // adds
        void reset();

    private:
        struct Played
        {
            SitarString string;
            bool held = false;
            unsigned age = 0;
        };
        std::array<Played, maxPlayed> played;
        std::array<SitarString, maxTarab> tarab;
        int tarabCount = 0;
        std::array<SitarString, 2> chikari;
        double rate = 48000.0;
        float tarabLevel = 0.6f;
        unsigned clock = 0;
        // The gourd: two resonant band-passes.
        struct Resonance
        {
            float b0 = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0, x1 = 0, x2 = 0;
            void set (double hz, double q, double rate);
            float tick (float x);
        };
        std::array<Resonance, 2> body;
    };

    // The tānpūrā: four open strings plucked in turn, over and over, as the drone under the raga.
    // The first string is Pa below Sa (or the raga's ma or ni when it has no Pa), then Sa twice,
    // then kharaj Sa an octave below. A wide jawārī gives the long buzzing bloom. Plain C++.
    class Tanpura
    {
    public:
        void prepare (double sampleRate);
        // The four strings' pitches: Sa, and the first string in cents above Sa (702 for Pa,
        // 498 for ma, 1088 for ni), in the octave below Sa.
        void tune (double saHz, double firstCents);
        void setCycle (double seconds) { cycle = seconds; }   // one round of the four strings
        void setLevel (double amount) { level = (float) amount; }
        void setOn (bool on);
        bool isOn() const { return on; }
        void process (float* left, float* right, int frames);   // adds
        void reset();

    private:
        std::array<SitarString, 4> strings;
        std::array<double, 4> hz { 196.0, 261.6, 261.6, 130.8 };
        double rate = 48000.0, cycle = 4.0, position = 0.0;
        int next = 0;
        float level = 0.5f;
        bool on = false;
    };
}
