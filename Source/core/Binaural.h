// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <atomic>
#include <complex>
#include <memory>
#include <string>
#include <vector>

struct MYSOFA_EASY;

// Binaural rendering for headphones: places a mono or stereo sound around the
// listener's head with a measured HRTF set (a SOFA file, read with libmysofa),
// at a distance, with an optional synthetic room. Plain C++.
namespace binaural
{
    struct Settings
    {
        double azimuth = 0.0;     // degrees: 0 in front, 90 right, -90 left, 180 behind
        double elevation = 0.0;   // degrees: -40 below to 90 overhead
        double width = 60.0;      // degrees between the left and right channels of a stereo sound
        double distance = 1.0;    // metres, 0.5 to 20: level and air absorption
        double room = 0.0;        // 0 to 1: how much room sound
        double roomSeconds = 0.6; // the room's decay time (RT60)
        double gainDb = 0.0;      // output level
        std::string hrtfPath;     // the SOFA file
    };

    // One HRTF set, resampled to the audio's rate.
    class Hrtf
    {
    public:
        static std::shared_ptr<Hrtf> open (const std::string& path, double sampleRate, std::string& error);
        ~Hrtf();

        int length() const { return taps; }
        // The two ears' impulse responses for a direction (degrees, as in Settings), delays included.
        void filters (double azimuth, double elevation, std::vector<float>& left, std::vector<float>& right) const;

    private:
        Hrtf() = default;
        ::MYSOFA_EASY* easy = nullptr;
        int taps = 0;
        double rate = 0.0;
    };

    // Uniformly partitioned overlap-add convolution, fed in blocks of exactly blockSize()
    // samples, with no latency. Feed zero blocks after the input to get the tail.
    class Convolver
    {
    public:
        Convolver (const std::vector<float>& impulse, int blockSize);
        int blockSize() const { return block; }
        void process (const float* in, float* out);   // out may be in

    private:
        int block, order;
        std::vector<std::vector<std::complex<float>>> parts, history;
        std::vector<std::complex<float>> buffer;
        std::vector<float> overlap;
        size_t newest = 0;
    };

    // A decorrelated room tail for each ear, energy 1.
    void roomImpulses (double sampleRate, double seconds, std::vector<float>& left, std::vector<float>& right);

    // The audio with [start, end) rendered binaurally: always stereo, the same length. The
    // rendered part fades in and out over 10 ms and its tail rings on into the audio after it.
    // Mono or stereo audio only. Empty (with `error` set) when the HRTF cannot be read.
    AudioDataPtr render (const AudioData&, Frame start, Frame end, const Settings&,
                         std::string& error, std::atomic<float>* progress = nullptr);
}
