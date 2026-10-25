// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Plugin Analyzer's measurements: test signals through an effect (any stereo
// processor, usually a copy of a plugin), and what comes out. Plain C++, so the
// core tests can check them against effects whose answers are known.
//
// - Linear: an exponential sine sweep, deconvolved by spectral division
//   (Farina's method, as LSP Profiler and REW measure; Müller and Massarani's
//   regularisation). Gives the impulse response, so the magnitude, phase and group
//   delay, and the latency; the harmonics the effect adds come out of the same
//   sweep, ahead of the impulse in time, as their own responses (H2 to H5 by frequency).
// - Harmonics: one steady sine, its spectrum with every harmonic, THD and THD+N.
// - Aliasing: steady sines up to 20 kHz; harmonics above half the sample rate fold
//   back down to frequencies that are no harmonic at all. Their level is the aliasing.
// - Dynamics: the level in against the level out (a compressor's curve), and a
//   step in level to time the attack and release.
// - Modulation: a long steady sine; how its level and pitch move tells LFO rates,
//   tremolo and gates (the rhythm of a gate too).
// All of it measures the effect as it is set, at the chosen level; the effect is
// reset before each test.
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace measure
{
    // What is measured: stereo in, stereo out, in place, blocks up to Settings::block.
    class Device
    {
    public:
        virtual ~Device() = default;
        virtual void reset() = 0;
        virtual void process (float* left, float* right, int frames) = 0;
        virtual int reportedLatency() const { return 0; }   // samples, as the effect says
    };

    // Which input the tests feed: both channels the same, or one alone (the other silent).
    enum class Input { both, left, right };

    struct Settings
    {
        double sampleRate = 48000.0;
        int block = 512;
        double levelDb = -12.0;        // dBFS peak of the sweep and the sines
        Input input = Input::both;

        bool linear = true, harmonics = true, aliasing = true, dynamics = true, modulation = true;

        double sweepSeconds = 2.0;     // the sweep, 10 Hz to 0.95 × Nyquist
        double tailSeconds = 1.5;      // how long it may ring (echoes, reverbs): the impulse window, and the wait before steady tones
        double sineHz = 1000.0;        // the harmonics test's tone (moved to fall on an FFT bin)
        int fftOrder = 16;             // the sine tests' FFT: 2^16 = 1.4 s at 48 kHz
        std::vector<double> aliasTones { 1000.0, 2500.0, 5000.0, 7500.0, 10000.0, 12500.0, 15000.0, 17500.0, 20000.0 };
        double aliasSpectrumHz = 7500.0;   // whose spectrum is kept for the picture
        double dynamicsFromDb = -60.0, dynamicsToDb = 0.0, dynamicsStepDb = 2.0;
        double dynamicsSettleMs = 250.0;   // at each level before it is read
        double stepLowDb = -40.0, stepHighDb = -6.0;   // the level step for attack and release
        double modulationSeconds = 12.0;
    };

    struct Spectrum
    {
        double toneHz = 0.0;                    // the tone as played (on a bin)
        std::vector<double> hz;                 // display points, log spaced
        std::array<std::vector<double>, 2> db;  // the peak of the bins at each point, dB relative to the input tone
    };

    struct Line
    {
        double hz = 0.0;     // where it lies in the output
        double db = 0.0;     // relative to the fundamental
        int order = 0;       // the harmonic it comes from: 2 and up; for aliasing, the harmonic that folded
        bool folded = false; // a harmonic above Nyquist, heard here instead
    };

    struct AliasPoint
    {
        double toneHz = 0.0;
        std::array<double, 2> aliasDb { -200.0, -200.0 };      // all folded components together, relative to the fundamental
        std::array<double, 2> harmonicDb { -200.0, -200.0 };   // the true harmonics together
        std::array<double, 2> worstDb { -200.0, -200.0 };      // the loudest single folded component
        double worstHz = 0.0;
    };

    struct Results
    {
        double sampleRate = 48000.0;
        int reportedLatency = 0;
        double measuredLatency = 0.0;   // samples, from where the impulse response peaks (fractional)
        bool silent = false;            // nothing came out at all
        std::vector<std::string> notes; // what the reader should know: time-variant, nonlinear, mono out...

        // Linear
        bool hasLinear = false;
        std::vector<double> hz;                                  // display points, 10 Hz to Nyquist, log spaced
        std::array<std::vector<double>, 2> magnitudeDb, phaseDeg, groupDelayMs;
        std::array<std::vector<float>, 2> impulse;               // from impulsePre samples ahead of no delay to the tail's end
        int impulsePre = 0;
        std::array<std::vector<std::vector<double>>, 2> sweepHarmonicDb;   // [channel][k - 2][point], k = 2..5, relative to H1
        // The magnitude in fine detail (the FFT's own bins up to Nyquist), for finding peaks.
        double binHz = 0.0;
        std::array<std::vector<double>, 2> fineMagnitudeDb, fineGroupDelayMs;

        // Harmonics
        bool hasHarmonics = false;
        Spectrum sineSpectrum;
        std::array<std::vector<double>, 2> harmonicDb;   // H1 (relative to the input) and H2.. relative to H1, up to H10
        std::array<double, 2> thdPercent { 0.0, 0.0 }, thdnPercent { 0.0, 0.0 };
        std::array<double, 2> gainDb { 0.0, 0.0 };       // H1 against the input tone
        std::vector<Line> otherLines;                    // inharmonic peaks above -90 dB (pitch shifters, ring mods, aliasing)

        // Aliasing
        bool hasAliasing = false;
        std::vector<AliasPoint> alias;
        Spectrum aliasSpectrum;
        std::vector<Line> aliasLines;   // of the kept spectrum: the harmonics, folded or not

        // Dynamics
        bool hasDynamics = false;
        std::vector<double> levelInDb;
        std::array<std::vector<double>, 2> levelOutDb;
        double ratio = 1.0;             // the steepest stretch of the curve, as in:out (1 when there is none)
        double thresholdDb = 0.0;       // where it bends (when it does)
        std::vector<double> stepMs;     // the level step: time
        std::array<std::vector<double>, 2> stepGainDb;   // the gain the effect gave, out against in
        double attackMs = 0.0, releaseMs = 0.0;          // to 63 % of the gain change (0 when none)

        // Modulation
        bool hasModulation = false;
        std::vector<double> envelopeMs;
        std::array<std::vector<double>, 2> envelopeDb;   // level of the tone over time
        double modulationDepthDb = 0.0;                  // largest level swing after it settles
        double pitchDepthCents = 0.0;                    // largest pitch swing
        std::vector<double> modulationHz;                // LFO rates found (level and pitch), strongest first
        double modulationResolutionHz = 0.0;
        bool gated = false;                              // the level goes on and off (a gate, a rhythm)
        double gateStepMs = 0.0;                         // the shortest open or closed stretch
        int gateOpenSteps = 0, gateClosedSteps = 0;      // counted in those steps
    };

    // Runs the tests the settings ask for. `progress` (0..1) is called between steps;
    // when it returns false the measurement stops and what it has so far comes back.
    Results run (Device&, const Settings&, const std::function<bool (double)>& progress = {});

    // The tone a test plays for `hz`: on an odd FFT bin, so its harmonics, folded or not, each land on bins of their own.
    double coherentHz (double hz, double sampleRate, int fftOrder);

    // A text report of everything measured (for the window's Copy and the command line).
    std::string report (const Results&, const std::string& title);
}
