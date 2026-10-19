// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"
#include "Fft.h"

#include <atomic>
#include <string>
#include <vector>

// The spectrogram's analysis, modeled on Sonic Visualiser's spectrogram layer:
// adjustable window size, shape, overlap and zero padding; linear, log or mel
// frequency scale over any frequency range; every bin, only peak bins, or peak
// frequencies sharpened by time-frequency reassignment; gain, range and
// normalising. Plain C++ (the Qt side only paints the result).
namespace spectrum
{
    enum class Scale { linear, log, mel };
    enum class Window { hann, hamming, blackmanHarris };
    enum class Bins { all, peaks, frequencies };
    enum class Normalise { none, column, view };

    struct Settings
    {
        int windowSize = 2048;          // 256 to 32768, a power of two
        Window window = Window::hann;
        int overlap = 4;                // frames per window length: 1, 2, 4, 8 or 16 (75 % overlap is 4)
        int oversample = 1;             // zero padding: 1, 2, 4 or 8
        Scale scale = Scale::log;
        Bins bins = Bins::all;
        Normalise normalise = Normalise::none;
        double gainDb = 0.0;            // added to every level
        double rangeDb = 120.0;         // levels this far below 0 dB (after gain) are the bottom colour
        int colourMap = 0;              // see colourMapNames(); 0 = the theme's

        bool operator== (const Settings&) const = default;
    };

    std::string toString (const Settings&);      // "window=2048;overlap=4;..." for storing
    Settings fromString (const std::string&);    // unknown or bad values keep their defaults
    const std::vector<std::string>& colourMapNames();
    // A colour map's stops as 0xRRGGBB, quietest first (empty for the theme's).
    std::vector<unsigned> colourMapStops (int map);

    // Where a frequency sits between `low` (0) and `high` (1) on a scale, and back.
    // The log scale cannot reach 0 Hz; its bottom is at least 10 Hz.
    double position (Scale, double hz, double low, double high);
    double frequency (Scale, double position, double low, double high);
    double lowestFor (Scale, double low);         // the low end a scale can actually show

    double hzToMel (double hz);
    double melToHz (double mel);
    std::string noteName (double hz);             // "A4", "C#5 +12¢"

    // Analyses frames for one channel: magnitudes in dB (a full-scale sine reads
    // about 0 dB) and, for Bins::frequencies, each bin's reassigned frequency.
    class Analyzer
    {
    public:
        Analyzer (const Settings&, double sampleRate);

        int fftSize() const   { return n; }
        int numBins() const   { return n / 2 + 1; }
        int windowSize() const { return w; }
        double binHz() const  { return rate / n; }

        // `input` holds windowSize() samples centred on the frame.
        void analyse (const float* input, float* outDb, float* outHz = nullptr);

    private:
        int w, n;
        double rate;
        Fft fft;
        std::vector<float> window, slope;          // the window and its time derivative (per sample)
        std::vector<std::complex<float>> a, b;
        float scale = 1.0f;
    };

    constexpr float silent = -1000.0f;

    // Renders each channel as `rows` rows by `columns` columns of levels in dB
    // after gain and normalising, row by row from the top, `silent` past the end.
    // Columns are pixel columns over [viewStart, viewStart + viewLength) frames;
    // `low`/`high` are the frequency range shown (high 0 = half the sample rate).
    // Runs on several threads; `cancel` may stop it early.
    std::vector<std::vector<float>> render (const AudioData&, const Settings&, double viewStart, double viewLength,
                                            int columns, int rows, double low, double high,
                                            const std::atomic<bool>* cancel = nullptr);
}
