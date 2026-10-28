// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Spatial mixing (owner, 2026-10-28; docs/spatial-mixing.md): in a project's Spatial mode each
// track, the Pre-Master and the returns are placed by a 3D panner in one 3rd-order ambisonic
// scene (AmbiX: ACN order, SN3D), which the master decodes to headphones (binaural through an
// HRTF set, or a stereo decode) and to a speaker array at the same time. Directions as in ambi:
// azimuth 0 ahead, 90 right; elevation 90 up. Plain C++; the decoders are made on the UI thread
// and run on the audio thread without allocating.

#include "../core/Ambisonics.h"
#include "../core/Binaural.h"

#include <array>
#include <memory>
#include <vector>

namespace mixer::spatial
{
    constexpr int order = 3;
    constexpr int channels = 16;   // (order + 1)^2
    constexpr int maxSpeakers = 32;

    // Where a source sits: its panner. A stereo source's sides are `spread` degrees apart.
    // Distance after E4L: 1 is the edge of the room; nearer turns the sound omni (all around
    // the listener), farther falls 1.5 dB a unit.
    struct Place
    {
        float azimuth = 0.0f, elevation = 0.0f, spread = 60.0f, distance = 1.0f;
    };
    constexpr float maxDistance = 10.0f;

    // A stereo source's left and right sides into the scene's channels.
    using Gains = std::array<std::array<float, channels>, 2>;
    Gains gains (const Place&);
    // Adds a stereo source to the scene, its gains moving from `from` to `to` over the block.
    void encode (const float* left, const float* right, int frames, const Gains& from, const Gains& to, float* const* scene);

    class Decoder
    {
    public:
        struct Setup
        {
            bool headphones = true;                        // the stereo out carries the scene
            std::shared_ptr<const ambi::EarFilters> ears;  // binaural; null: a stereo decode
            ambi::Matrix speakers;                         // L x channels (LFE rows zero); empty: none
            int firstOutput = 0;                           // the device output the first speaker is on
            double yaw = 0.0;                              // the whole scene turned, degrees to the right
        };

        explicit Decoder (Setup);
        const Setup& setup() const { return s; }
        int speakerCount() const { return (int) speak.size(); }
        int latency() const { return s.ears != nullptr ? block : 0; }   // the binaural decode's, in frames

        // Audio thread. The scene is turned in place; the headphone mix is added to left and
        // right, and the speakers' feeds written to `feeds` (speakerCount() of them).
        void process (float* const* scene, int frames, float* left, float* right, float* const* feeds);
        void reset();   // silence: a mix down starts from nothing

    private:
        static constexpr int block = 64;   // the binaural decode's partition, and its delay
        Setup s;
        std::vector<std::array<float, channels>> rotation;   // channels rows; empty: none
        std::array<std::array<float, channels>, 2> stereo {};
        std::vector<std::array<float, channels>> speak;
        std::array<std::vector<std::unique_ptr<binaural::Convolver>>, 2> conv;
        std::array<std::array<float, block>, channels> pending {};   // the scene waiting for a whole partition
        std::array<std::array<float, block>, 2> ready {};            // the last partition heard, per ear
        std::array<float, block> scratch {};
        std::vector<float> turned;                                   // one frame of the scene, turned
        int filled = 0;
    };
}
