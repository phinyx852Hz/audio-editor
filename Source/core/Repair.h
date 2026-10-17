// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Spectral repair: edits on a time and frequency box, done on a short-time
// Fourier transform of the audio around it. Only the change is added back, so
// everything outside the box (and its feathered edge) stays bit-for-bit the same.
// Plain C++.
namespace repair
{
    // Progress from 0 to 1 and a way to stop early; either may be null.
    struct Progress
    {
        std::atomic<float>* done = nullptr;
        const std::atomic<bool>* cancel = nullptr;
        bool cancelled() const { return cancel != nullptr && cancel->load(); }
        void report (float f) const { if (done != nullptr) done->store (f); }
    };

    // A drawn or painted selection of time and frequency (lasso, brush), stored
    // as cells: columns of framesPerColumn frames by rows of 1/24 octave from 20 Hz.
    class SpectralMask
    {
    public:
        static constexpr int framesPerColumn = 256;
        static constexpr int rowsPerOctave = 24;
        static constexpr double lowestHz = 20.0;

        explicit SpectralMask (double sampleRate);

        double sampleRate() const { return rate; }
        int rows() const          { return rowCount; }
        double rowToHz (double row) const;
        double hzToRow (double hz) const;

        // Paints an ellipse around (frame, hz), radii in frames and rows, with a soft edge.
        void paint (Frame frame, double hz, double radiusFrames, double radiusRows);
        // Selects everything inside a polygon of (frame, hz) points.
        void fillPolygon (const std::vector<std::pair<Frame, double>>& points);
        // Adds the overtones (2, 3... `count` times the frequency) of what is selected.
        void addHarmonics (int count);

        float at (Frame, double hz) const;   // 0 to 1
        bool empty() const { return cells.empty(); }
        Frame start() const;
        Frame end() const;
        double lowestHzSelected() const;
        double highestHzSelected() const;

    private:
        void set (int64_t column, int row, float value);

        double rate;
        int rowCount;
        std::map<int64_t, std::vector<float>> cells;   // column -> a value per row
    };

    // A time range, optionally limited to a frequency band. Without a band
    // (highHz <= lowHz) the edit covers every frequency. A mask, when set,
    // replaces both: the edit covers what it selects.
    struct SpectralRegion
    {
        Frame start = 0, end = 0;
        double lowHz = 0.0, highHz = 0.0;
        std::shared_ptr<const SpectralMask> mask;

        bool hasBand() const { return highHz > lowHz || mask != nullptr; }
    };

    // Sounds like the example elsewhere in [start, end): stretches whose spectrum
    // stands out from its surroundings the same way. Each comes back as a region
    // of the example's length and band, with a little room either side. Higher
    // strictness (0 to 1) finds fewer. Empty when cancelled.
    std::vector<SpectralRegion> findSimilar (const AudioData&, const SpectralRegion& example, Frame start, Frame end,
                                             double strictness, Progress = {});

    // Turns the region down (or up) by gainDb. At or below removeDb it is removed.
    inline constexpr double removeDb = -120.0;
    AudioDataPtr spectralGain (const AudioData&, const SpectralRegion&, double gainDb);

    // Auto Heal: rebuilds the region from the sound just before and after it, for
    // clicks, coughs, squeaks and phone rings. Returns null with `error` set when
    // the region cannot be healed (too long, or no audio around it).
    inline constexpr double maxHealSeconds = 4.0;
    AudioDataPtr autoHeal (const AudioData&, const SpectralRegion&, std::string& error);

    //==========================================================================
    // Restoration over a time range (the selection, or the whole file). Each
    // reports Progress and stops early, returning null, when cancelled.

    // What a detect-and-fix tool found.
    struct Report
    {
        int fixed = 0;     // clicks or clipped stretches rebuilt
        int skipped = 0;   // found but too long to rebuild from their surroundings
    };

    // The average spectrum of background noise, learned from a stretch that holds only noise.
    struct NoiseProfile
    {
        double sampleRate = 0.0;
        int fftOrder = 11;
        std::vector<std::vector<float>> power;   // [channel][bin], mean power per bin
        Frame learnedFrom = 0;                   // frames of audio it was learned from

        bool isValid() const { return ! power.empty(); }
    };
    NoiseProfile learnNoise (const AudioData&, Frame start, Frame end);

    struct NoiseReductionSettings
    {
        double reductionDb = 12.0;   // how far the noise is turned down
        double sensitivityDb = 6.0;  // how far above the noise a sound must be to be kept
        double releaseMs = 100.0;    // how long the reduction takes to come back after a sound
    };
    AudioDataPtr reduceNoise (const AudioData&, Frame start, Frame end, const NoiseProfile&,
                              const NoiseReductionSettings&, Progress = {});

    // Finds clicks and crackle and rebuilds each from the audio around it.
    // Lower sensitivity values find more (it is a threshold in robust standard deviations).
    AudioDataPtr declick (const AudioData&, Frame start, Frame end, double sensitivity, Report&, Progress = {});

    // Rebuilds stretches of two or more samples stuck at the clipping level
    // (`levelPercent` of the loudest sample). The result may peak above 0 dBFS.
    AudioDataPtr declip (const AudioData&, Frame start, Frame end, double levelPercent, Report&, Progress = {});

    // Removes mains hum: narrow notches at the hum frequency and its harmonics.
    // frequency 0 finds 50 or 60 Hz (and the exact value) by itself.
    struct HumSettings
    {
        double frequency = 0.0;
        int harmonics = 8;
        double widthHz = 1.5;
    };
    double findHum (const AudioData&, Frame start, Frame end);   // 0 when there is no clear hum
    AudioDataPtr dehum (const AudioData&, Frame start, Frame end, const HumSettings&, Progress = {});

    // Turns down harsh "s" and "sh" sounds: above `thresholdDb` the band from
    // `fromHz` up is reduced, by at most `maxReductionDb`.
    struct DeEssSettings
    {
        double thresholdDb = -30.0;
        double fromHz = 5000.0;
        double maxReductionDb = 10.0;
    };
    AudioDataPtr deess (const AudioData&, Frame start, Frame end, const DeEssSettings&, Progress = {});

    //==========================================================================
    // Diagnostics: scans that list problems without changing anything, so they
    // can be reviewed, repaired one by one or all at once, or marked.
    struct Problem
    {
        enum class Kind { click, clipping, dropout, silence };
        Kind kind = Kind::click;
        Frame start = 0, end = 0;
        uint64_t channels = 0;   // a bit per channel (the first 64); 0 means every channel
        double size = 0.0;       // click: times louder than the audio around it; clipping: the clip level
    };

    std::vector<Problem> findClicks (const AudioData&, Frame start, Frame end, double sensitivity, Progress = {});
    std::vector<Problem> findClipping (const AudioData&, Frame start, Frame end, double levelPercent, Progress = {});
    // Stretches of digital silence (at least 1 ms) with sound on both sides: lost samples.
    std::vector<Problem> findDropouts (const AudioData&, Frame start, Frame end, Progress = {});
    // Stretches quieter than thresholdDb (RMS over 10 ms) for at least minSeconds.
    std::vector<Problem> findSilence (const AudioData&, Frame start, Frame end, double thresholdDb, double minSeconds, Progress = {});

    // Rebuilds listed clicks, clipping and dropouts (silence is left alone).
    // Null when cancelled or when nothing could be repaired.
    AudioDataPtr repairProblems (const AudioData&, const std::vector<Problem>&, Report&, Progress = {});

    // Fills x[gapStart, gapStart + gapLength) from an autoregressive model of the
    // samples around it (least-squares interpolation). x must hold enough audio
    // either side. Returns false when the gap is too long.
    bool interpolateGap (std::vector<float>& x, int gapStart, int gapLength);
}
