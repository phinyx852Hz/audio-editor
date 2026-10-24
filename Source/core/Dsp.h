// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <array>
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <vector>

// Offline processing and measurement on AudioData. Plain C++.
namespace dsp
{
    inline double gainToDb (double g)   { return g > 0.0 ? 20.0 * std::log10 (g) : -200.0; }
    inline double dbToGain (double db)  { return std::pow (10.0, db / 20.0); }

    enum class FadeShape { linear, equalPower, exponential, sCurve };
    float fadeGain (double t, FadeShape);  // t in [0, 1], rising

    // A fade whose bend is set by `curve` from -1 to +1, like Audition's fade
    // handles: 0 is a straight line, above 0 rises fast and levels off
    // (logarithmic feel), below 0 starts slow and rises late (exponential feel).
    float curvedFadeGain (double t, double curve);   // t in [0, 1], rising
    AudioDataPtr curvedFade (const AudioData&, Frame start, Frame end, double curve, bool rising);

    AudioDataPtr applyGain (const AudioData&, Frame start, Frame end, double gainDb);
    AudioDataPtr fadeIn (const AudioData&, Frame start, Frame end, FadeShape);
    AudioDataPtr fadeOut (const AudioData&, Frame start, Frame end, FadeShape);
    AudioDataPtr invert (const AudioData&, Frame start, Frame end);
    AudioDataPtr reverse (const AudioData&, Frame start, Frame end);
    AudioDataPtr removeDcOffset (const AudioData&, Frame start, Frame end);
    AudioDataPtr silence (int numChannels, double sampleRate, Frame length);

    // Mono to stereo (Stereoize). Inside [start, end) the mono signal (or the mid of
    // a stereo file) gets a delayed copy of its upper part added to the left and
    // subtracted from the right, so left + right is still exactly the original:
    // the result folds back to mono without comb filtering. Bass below bassHz stays
    // centred. Returns a two-channel copy of the whole file; outside the range a
    // mono file plays the same on both sides and a stereo file is unchanged.
    struct StereoizeSettings
    {
        double width = 0.6;      // 0..1, the level of the added side signal
        double delayMs = 12.0;   // 1..40
        double bassHz = 200.0;   // 0 = everything is widened
        // Φ: four allpasses at delay, delay/φ, delay/φ², delay/φ³ (each with 1/φ
        // feedback) in place of the single delay, so the comb it makes is no
        // longer evenly spaced and the width sounds less pitched. Still sums
        // back to the original in mono.
        bool golden = false;
    };

    AudioDataPtr stereoize (const AudioData&, Frame start, Frame end, const StereoizeSettings&);

    // The streaming core of Stereoize, shared by the offline command and the live
    // effect: feed it the mid signal, add the result to the left and subtract it
    // from the right. No allocation after prepare().
    class Stereoizer
    {
    public:
        static constexpr double maxDelayMs = 40.0;

        void prepare (double sampleRate);      // allocates the delay line
        void set (const StereoizeSettings&);   // safe between samples
        float process (float mid);             // returns the side signal (before the width)
        void reset();

    private:
        static constexpr int goldenStages = 4;

        double rate = 48000.0;
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        std::vector<float> ring;
        size_t ringPos = 0, delay = 1;
        double bassHz = -1.0;
        // Φ: both paths always run and the switch crossfades between them, so it never clicks.
        std::array<std::vector<float>, goldenStages> allpass;
        std::array<size_t, goldenStages> allpassDelay {};
        float golden = 0.0f, goldenTarget = 0.0f, goldenStep = 0.002f;
    };

    // A mono file as two equal channels (other files are returned as they are).
    AudioDataPtr toStereo (const AudioDataPtr&);
    // Every channel averaged into one.
    AudioDataPtr toMono (const AudioData&);

    // Called with progress from 0 to 1; return false to stop (the result is then null).
    using ProgressCallback = std::function<bool (float)>;

    // Sample-rate conversion with a windowed-sinc (Kaiser) filter: flat to 95 % of
    // the lower Nyquist frequency, more than 90 dB of rejection above it.
    AudioDataPtr resample (const AudioData&, double newRate, const ProgressCallback& = {});

    // Rounds every sample to `bits` (8 to 24) with optional dither, so the file
    // sounds now as it will once saved at that depth. Noise shaping moves the
    // dither noise up to where hearing is least sensitive.
    enum class Dither { none, tpdf, shaped };
    AudioDataPtr quantize (const AudioData&, int bits, Dither, const ProgressCallback& = {});

    // The same rounding a block at a time, for the live Dither effect.
    class Quantizer
    {
    public:
        void set (int bits, Dither, int numChannels);
        void process (float* const* channels, int numChannels, int numFrames);

    private:
        int bits = 24;
        Dither dither = Dither::tpdf;
        std::vector<std::array<float, 2>> errors;   // last two rounding errors per channel, for noise shaping
        std::mt19937 rng { 20261017 };
    };

    // ITU-R BS.1770 K-weighting: high shelf followed by high pass.
    class KWeighting
    {
    public:
        void prepare (double sampleRate);
        float process (float x);
        void reset();

    private:
        struct Biquad
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            float process (float x);
        };

        Biquad shelf, highPass;
    };

    // 4x oversampled peak detector for ITU-R BS.1770 true peak.
    class TruePeakDetector
    {
    public:
        TruePeakDetector();
        float process (float x);  // returns the largest |value| of the 4 interpolated points
        void reset();

    private:
        static constexpr int tapsPerPhase = 12;
        std::array<std::array<float, tapsPerPhase>, 4> phases {};
        std::array<float, tapsPerPhase> history {};
        int writeIndex = 0;
    };

    // Look-ahead brickwall limiter that keeps the true peak (4x oversampled, as
    // BS.1770 measures it) at or below the ceiling. All channels share one gain,
    // so the stereo image stays put. The audio comes out `latency()` samples late.
    class TruePeakLimiter
    {
    public:
        void prepare (double sampleRate, int numChannels, double lookaheadMs = 5.0);
        void set (double ceilingDb, double releaseMs);
        void reset();
        int latency() const { return window + detectorDelay; }
        void process (float* const* channels, int numChannels, int numFrames);
        float lastGain() const { return currentGain; }   // 1 = no reduction

    private:
        float nextGain (float required);

        static constexpr int detectorDelay = 6;   // TruePeakDetector's interpolation delay, in samples
        std::vector<TruePeakDetector> detectors;
        std::vector<std::vector<float>> delay;     // the audio, held back by latency()
        std::vector<std::pair<long long, float>> minQueue;   // sliding minimum of the required gain
        std::vector<float> box;                    // the smoothing filter's history
        int window = 240, hold = 244, writePos = 0, queueHead = 0, queueTail = 0, boxPos = 0;
        long long counter = 0;
        double boxSum = 0.0;
        float ceiling = 1.0f, releaseCoef = 0.0f, released = 1.0f, currentGain = 1.0f;
        double rate = 48000.0, releaseMs = 100.0;
    };

    // Runs [start, end) through a TruePeakLimiter (latency removed); the rest is unchanged.
    AudioDataPtr limitTruePeak (const AudioData&, Frame start, Frame end, double ceilingDb, double releaseMs = 100.0);

    // Speakers, and the layout PHI assumes for each channel count, in WAV
    // (WAVEFORMATEXTENSIBLE / SMPTE) order: 5.1 is L R C LFE Ls Rs.
    enum class Speaker { mono, left, right, centre, lfe, sideLeft, sideRight, rearLeft, rearRight, rearCentre,
                         topFrontLeft, topFrontRight, topRearLeft, topRearRight, other,
                         lfe2, wideLeft, wideRight, topMiddleLeft, topMiddleRight, ambisonic };

    struct SpeakerLayout
    {
        std::string name;               // "Stereo", "5.1", "7.1.4", "10 channels"...
        std::vector<Speaker> speakers;  // one per channel
    };

    SpeakerLayout speakerLayout (int numChannels);
    Speaker speakerAt (int channel, int numChannels);
    std::string channelName (int channel, int numChannels);  // "L", "LFE", "Ls", or "Ch 9"

    // Layouts a file of this many channels can be, the usual one first: 7 channels are 6.1 or
    // 5.2, 16 are 9.1.6 or third-order Ambisonics, and so on. Two-subwoofer layouts put LFE2
    // straight after LFE; Ambisonics is AmbiX (ACN order, SN3D).
    std::vector<std::string> layoutChoices (int numChannels);
    // The named layout when it fits the channel count, else the usual one.
    SpeakerLayout speakerLayout (int numChannels, const std::string& name);
    std::string channelName (int channel, int numChannels, const std::string& layoutName);
    int ambisonicOrder (const std::string& layoutName);   // 0 when it is not Ambisonics

    // Stereo fold-down for monitoring: gains from each channel into left and right
    // (ITU-R BS.775: centre and surrounds at -3 dB, LFE left out). Ambisonics is decoded.
    std::array<std::vector<float>, 2> stereoDownmix (int numChannels);
    std::array<std::vector<float>, 2> stereoDownmix (int numChannels, const std::string& layoutName);

    // Channel weight for loudness summing (BS.1770: surrounds count 1.41, LFE not at all).
    double loudnessChannelWeight (int channel, int numChannels);

    struct LoudnessReport
    {
        double integratedLufs = -200.0;
        double loudnessRange = 0.0;   // LU
        double samplePeakDb = -200.0;
        double truePeakDb = -200.0;
    };

    LoudnessReport measureLoudness (const AudioData&, Frame start, Frame end);

    // The zero crossing nearest `at` within `radius` frames: where the first
    // channel changes sign (or is exactly zero), the quietest point across all
    // channels breaking ties. Without one, the quietest point. Edits there do not click.
    Frame nearestZeroCrossing (const AudioData&, Frame at, Frame radius);

    // Integrated loudness from 100 ms block powers (K-weighted, channel-weighted mean squares).
    double integratedLoudness (const std::vector<double>& blockPowers100ms);

    // Adds triangular (TPDF) dither of one LSB at the given bit depth.
    void addTpdfDither (std::vector<std::vector<float>>& block, int bitDepth, std::mt19937& rng);
}
