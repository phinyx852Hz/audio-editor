// SPDX-License-Identifier: AGPL-3.0-only
// Analysis Meters: the measurements (analysis::State), one widget per meter, the
// cards they sit in, and the window that lays them out.
#include "AnalysisWindow.h"
#include "AnalysisReport.h"
#include "AmbiVisualizer.h"
#include "Goniometer.h"
#include "Settings.h"
#include "Theme.h"
#include "../core/Dsp.h"
#include "../core/Fft.h"
#include "../engine/AudioEngine.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QFutureWatcher>
#include <QMimeData>
#include <QtConcurrent>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

namespace analysis
{
    constexpr double minusInfinity = -200.0;
    constexpr double clipLevel = 0.99997;     // about -0.0003 dBFS: a sample at full scale
    constexpr int numBands = 31;              // third octaves from 20 Hz to 20 kHz
    const char* const bandNames[numBands] { "20", "25", "31", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500",
                                            "630", "800", "1k", "1.25k", "1.6k", "2k", "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k",
                                            "12.5k", "16k", "20k" };
    constexpr int numHolds = 8;
    constexpr int holdPoints = 512;           // a hold trace, log-spaced from 20 Hz to 20 kHz
    constexpr int ringSize = 1 << 15;         // the largest FFT
    constexpr double lowestHz = 20.0, highestHz = 20000.0;

    double ampDb (double amplitude) { return amplitude > 1.0e-10 ? 20.0 * std::log10 (amplitude) : minusInfinity; }
    double powerDb (double p)       { return p > 1.0e-20 ? 10.0 * std::log10 (p) : minusInfinity; }
    double lufsOf (double p)        { return p > 1.0e-20 ? -0.691 + 10.0 * std::log10 (p) : minusInfinity; }
    double powerOfLufs (double l)   { return std::pow (10.0, (l + 0.691) / 10.0); }

    QColor color (const char* key) { return Theme::current().color (QString::fromLatin1 (key)); }
    QColor faded (QColor c, double alpha) { c.setAlphaF ((float) std::clamp (alpha, 0.0, 1.0)); return c; }
    QColor mix (const QColor& a, const QColor& b, double t)
    {
        t = std::clamp (t, 0.0, 1.0);
        return QColor::fromRgbF ((float) (a.redF() + (b.redF() - a.redF()) * t), (float) (a.greenF() + (b.greenF() - a.greenF()) * t),
                                 (float) (a.blueF() + (b.blueF() - a.blueF()) * t), (float) (a.alphaF() + (b.alphaF() - a.alphaF()) * t));
    }

    QString number (double v, int decimals, const char* unit = nullptr)
    {
        const auto text = v <= -150.0 ? QStringLiteral ("-inf") : QString::number (v, 'f', decimals);
        return unit == nullptr ? text : text + QLatin1Char (' ') + QLatin1String (unit);
    }

    QString signedNumber (double v, int decimals)
    {
        return (v >= 0.0 ? QStringLiteral ("+") : QString()) + QString::number (v, 'f', decimals);
    }

    QString hzText (double hz)
    {
        if (hz >= 1000.0)
            return QStringLiteral ("%1 kHz").arg (hz / 1000.0, 0, 'f', hz >= 10000.0 ? 1 : 2);
        return QStringLiteral ("%1 Hz").arg (hz, 0, 'f', hz >= 100.0 ? 0 : 1);
    }

    QString shortHz (double hz)
    {
        return hz >= 1000.0 ? QStringLiteral ("%1k").arg (hz / 1000.0) : QString::number (hz);
    }

    // "A#3 +28 cents", A4 = 440 Hz.
    QString noteText (double hz)
    {
        if (hz < 8.0)
            return {};
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const auto nearest = (int) std::lround (midi);
        const auto cents = (int) std::lround ((midi - nearest) * 100.0);
        return QStringLiteral ("%1%2 %3 cents").arg (QLatin1String (names[((nearest % 12) + 12) % 12])).arg (nearest / 12 - 1)
            .arg (signedNumber (cents, 0));
    }

    // Loudness against the target, in the theme's level colours: blue well below, green on it, red far above.
    QColor loudnessColor (double luFromTarget)
    {
        const char* key = luFromTarget < -9.0 ? "levelQuiet" : luFromTarget < -3.0 ? "levelSoft" : luFromTarget <= 1.0 ? "levelMedium"
                         : luFromTarget <= 3.0 ? "levelLoud" : luFromTarget <= 6.0 ? "levelHot" : "levelPeak";
        return color (key);
    }

    // Zone edges (LU from the target, from the top) with the colour above each edge.
    struct Zone { double from; const char* key; };
    const std::array<Zone, 6> zones {{ { 6.0, "levelPeak" }, { 3.0, "levelHot" }, { 1.0, "levelLoud" }, { -3.0, "levelMedium" },
                                       { -9.0, "levelSoft" }, { -1000.0, "levelQuiet" } }};

    // Correlation from -1 (red) through 0 (yellow) to +1 (green).
    QColor correlationColor (double c)
    {
        return c < 0.0 ? mix (color ("meterHigh"), color ("meterMid"), c + 1.0) : mix (color ("meterMid"), color ("meterLow"), c);
    }

    struct Target { const char* name; double lufs, truePeak; };
    const std::array<Target, 6> targets {{
        { QT_TRANSLATE_NOOP ("AnalysisWindow", "Streaming: -14 LUFS, -1 dBTP"), -14.0, -1.0 },
        { QT_TRANSLATE_NOOP ("AnalysisWindow", "Apple Music, podcasts: -16 LUFS, -1 dBTP"), -16.0, -1.0 },
        { QT_TRANSLATE_NOOP ("AnalysisWindow", "Amazon Music: -14 LUFS, -2 dBTP"), -14.0, -2.0 },
        { QT_TRANSLATE_NOOP ("AnalysisWindow", "EBU R128 broadcast: -23 LUFS, -1 dBTP"), -23.0, -1.0 },
        { QT_TRANSLATE_NOOP ("AnalysisWindow", "ATSC A/85 broadcast: -24 LKFS, -2 dBTP"), -24.0, -2.0 },
        { QT_TRANSLATE_NOOP ("AnalysisWindow", "CD, club: -9 LUFS, -0.3 dBTP"), -9.0, -0.3 },
    }};

    const std::array<QColor, numHolds> holdColors { QColor (0xef5350), QColor (0xff9800), QColor (0xffeb3b), QColor (0x66bb6a),
                                                     QColor (0x26c6da), QColor (0x29b6f6), QColor (0x7e57c2), QColor (0xec407a) };

    double holdFrequency (int i) { return lowestHz * std::pow (highestHz / lowestHz, i / (double) (holdPoints - 1)); }

    //==============================================================================
    // Every measurement, from the samples heard since the last reset.
    struct State
    {
        enum Channel { left, right, mid, side, average };

        State() { setRate (48000.0); setFft (13, 1); }

        double rate = 0.0;
        double targetLufs = -14.0, targetTruePeak = -1.0;

        // Loudness (BS.1770): K-weighted power per 100 ms block.
        std::array<dsp::KWeighting, 2> kWeighting;
        std::array<dsp::TruePeakDetector, 2> truePeak;
        int blockLength = 4800, blockFill = 0;
        double blockPower = 0.0;
        std::vector<double> blocks;
        std::vector<float> momentaryHistory, shortHistory;   // LUFS, one per block
        double momentary = minusInfinity, shortTerm = minusInfinity, integrated = minusInfinity, range = 0.0;
        double maxMomentary = minusInfinity, maxShortTerm = minusInfinity;

        // Levels: since the last tick, then as shown (dB) with holds.
        double rmsCoefficient = 0.0;
        std::array<double, 2> tickPeak {}, tickTrue {}, rmsPower {};
        std::array<double, 2> peakDb { minusInfinity, minusInfinity }, rmsDb { minusInfinity, minusInfinity };
        std::array<double, 2> holdDb { minusInfinity, minusInfinity }, holdAge {};
        std::array<double, 2> maxTrue {};   // amplitude, since the reset

        // Correlation and balance, smoothed over about 300 ms.
        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        double correlation = 0.0, balanceDb = 0.0;
        bool hasSignal = false;

        // Amplitude statistics, as Audition has them (50 ms RMS windows).
        struct Stats
        {
            double sum = 0.0, squares = 0.0, windowSquares = 0.0;
            float minimum = 0.0f, maximum = 0.0f;
            long long clipped = 0;
            double maxRms = minusInfinity, minRms = 0.0, rmsDbSum = 0.0;
            int windows = 0;
            std::array<int, 61> histogram {};   // window RMS, 1 dB bins from -60 to 0
        };
        std::array<Stats, 2> stats;
        long long frames = 0;
        int windowLength = 2400, windowFill = 0;

        // Spectrum: the newest samples, transformed each tick and smoothed per bin.
        std::vector<float> ringLeft = std::vector<float> (ringSize), ringRight = std::vector<float> (ringSize);
        size_t ringPos = 0;
        int freshSamples = 0;
        int fftOrder = 13, windowType = 1, response = 1;
        std::unique_ptr<Fft> fft;
        std::vector<float> window;
        double windowScale = 1.0;
        std::vector<std::complex<float>> buffer;
        std::array<std::vector<double>, 4> power;           // left, right, mid, side, per bin
        std::array<std::vector<double>, 4> prefix;          // running sums of power, for fractional-octave smoothing
        std::vector<std::complex<double>> cross;            // left times conjugate right
        std::array<double, 2> dominant {};                  // Hz, 0 when quiet
        std::array<double, numBands> bandCorrelation {}, bandLevel {};

        struct WheelPoint { double angle, radius, levelDb, hz; };
        std::vector<WheelPoint> wheel;

        // Spectrum holds: dB at holdFrequency (i), and whether shown.
        std::array<std::vector<float>, numHolds> holds;

        int fftSize() const     { return 1 << fftOrder; }
        int numBins() const     { return fftSize() / 2 + 1; }
        double binHz() const    { return rate / fftSize(); }
        double nyquist() const  { return rate / 2.0; }
        double seconds() const  { return frames / rate; }

        void setRate (double newRate)
        {
            rate = newRate;
            for (auto& k : kWeighting)
                k.prepare (rate);
            blockLength = std::max (1, (int) std::lround (rate * 0.1));
            windowLength = std::max (1, (int) std::lround (rate * 0.05));
            rmsCoefficient = std::exp (-1.0 / (0.3 * rate));
            reset();
        }

        void setFft (int order, int type)
        {
            fftOrder = std::clamp (order, 10, 15);
            windowType = std::clamp (type, 0, 4);
            const int n = fftSize();
            fft = std::make_unique<Fft> (fftOrder);
            window.assign ((size_t) n, 1.0f);
            for (int i = 0; i < n; ++i)
            {
                const double x = 2.0 * M_PI * i / n;
                double w = 1.0;
                switch (windowType)
                {
                    case 0: w = 0.5 - 0.5 * std::cos (x); break;   // Hann
                    case 1: w = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x); break;   // Blackman-Harris
                    case 2: w = 0.21557895 - 0.41663158 * std::cos (x) + 0.277263158 * std::cos (2 * x)
                                - 0.083578947 * std::cos (3 * x) + 0.006947368 * std::cos (4 * x); break;   // flat top
                    case 3: w = 0.54 - 0.46 * std::cos (x); break;   // Hamming
                    default: break;                                  // rectangular
                }
                window[(size_t) i] = (float) w;
            }
            // A full-scale sine reads 0 dB.
            windowScale = 2.0 / std::accumulate (window.begin(), window.end(), 0.0);
            buffer.assign ((size_t) n, {});
            for (auto& p : power)
                p.assign ((size_t) numBins(), 0.0);
            for (auto& p : prefix)
                p.assign ((size_t) numBins() + 1, 0.0);
            cross.assign ((size_t) numBins(), {});
            for (auto& p : totalPower)
                p.assign ((size_t) numBins(), 0.0);
            totalCross.assign ((size_t) numBins(), {});
            spectra = 0;
        }

        void reset()
        {
            for (auto& k : kWeighting)
                k.reset();
            for (auto& t : truePeak)
                t.reset();
            blockFill = 0;
            blockPower = 0.0;
            blocks.clear();
            momentaryHistory.clear();
            shortHistory.clear();
            momentary = shortTerm = integrated = maxMomentary = maxShortTerm = minusInfinity;
            range = 0.0;
            tickPeak = tickTrue = rmsPower = maxTrue = {};
            peakDb = rmsDb = holdDb = { minusInfinity, minusInfinity };
            holdAge = {};
            sumLR = sumLL = sumRR = 0.0;
            allLR = allLL = allRR = 0.0;
            correlation = balanceDb = 0.0;
            hasSignal = false;
            stats = {};
            frames = 0;
            windowFill = 0;
        }

        void add (const std::vector<float>& left, const std::vector<float>& right)
        {
            const auto n = std::min (left.size(), right.size());
            const double c = rmsCoefficient, d = 1.0 - c;
            for (size_t i = 0; i < n; ++i)
            {
                const float x[2] { left[i], right[i] };

                for (int ch = 0; ch < 2; ++ch)
                {
                    const double v = x[ch], a = std::abs (v);
                    tickPeak[(size_t) ch] = std::max (tickPeak[(size_t) ch], a);
                    tickTrue[(size_t) ch] = std::max (tickTrue[(size_t) ch], (double) truePeak[(size_t) ch].process (x[ch]));
                    rmsPower[(size_t) ch] = c * rmsPower[(size_t) ch] + d * v * v;

                    auto& s = stats[(size_t) ch];
                    s.sum += v;
                    s.squares += v * v;
                    s.windowSquares += v * v;
                    s.minimum = std::min (s.minimum, x[ch]);
                    s.maximum = std::max (s.maximum, x[ch]);
                    if (a >= clipLevel)
                        ++s.clipped;
                }

                sumLR = c * sumLR + d * (double) x[0] * x[1];
                allLR += (double) x[0] * x[1];
                allLL += (double) x[0] * x[0];
                allRR += (double) x[1] * x[1];
                sumLL = c * sumLL + d * (double) x[0] * x[0];
                sumRR = c * sumRR + d * (double) x[1] * x[1];

                const double kl = kWeighting[0].process (x[0]), kr = kWeighting[1].process (x[1]);
                blockPower += kl * kl + kr * kr;
                if (++blockFill == blockLength)
                    finishBlock();

                ++frames;
                if (++windowFill == windowLength)
                    finishWindow();

                ringLeft[ringPos] = x[0];
                ringRight[ringPos] = x[1];
                ringPos = (ringPos + 1) & (ringSize - 1);
            }
            freshSamples += (int) n;
        }

        void finishBlock()
        {
            blocks.push_back (blockPower / blockLength);
            blockPower = 0.0;
            blockFill = 0;

            const auto meanOfLast = [this] (size_t count)
            {
                count = std::min (count, blocks.size());
                return std::accumulate (blocks.end() - (long) count, blocks.end(), 0.0) / (double) count;
            };
            momentary = lufsOf (meanOfLast (4));
            shortTerm = lufsOf (meanOfLast (30));
            momentaryHistory.push_back ((float) momentary);
            shortHistory.push_back ((float) shortTerm);
            if (blocks.size() >= 4)
                maxMomentary = std::max (maxMomentary, momentary);
            if (blocks.size() >= 30)
                maxShortTerm = std::max (maxShortTerm, shortTerm);
            if (blocks.size() % 5 == 0)
                integrated = dsp::integratedLoudness (blocks);
            if (blocks.size() % 10 == 0)
                range = loudnessRange();
        }

        // EBU Tech 3342: short-term values gated at -70 LUFS and 20 LU under their mean, 10th to 95th percentile.
        double loudnessRange() const
        {
            if (shortHistory.size() < 31)
                return 0.0;
            std::vector<double> values;
            double sum = 0.0;
            for (size_t i = 29; i < shortHistory.size(); ++i)
                if (shortHistory[i] > -70.0f)
                {
                    values.push_back (shortHistory[i]);
                    sum += powerOfLufs (shortHistory[i]);
                }
            if (values.size() < 2)
                return 0.0;
            const auto gate = lufsOf (sum / (double) values.size()) - 20.0;
            std::erase_if (values, [gate] (double v) { return v <= gate; });
            if (values.size() < 2)
                return 0.0;
            std::sort (values.begin(), values.end());
            const auto at = [&values] (double q) { return values[(size_t) std::lround (q * (double) (values.size() - 1))]; };
            return at (0.95) - at (0.10);
        }

        void finishWindow()
        {
            for (auto& s : stats)
            {
                const auto db = powerDb (s.windowSquares / windowLength);
                s.windowSquares = 0.0;
                if (db < -100.0)
                    continue;   // digital silence says nothing about the dynamics
                s.maxRms = std::max (s.maxRms, db);
                s.minRms = s.windows == 0 ? db : std::min (s.minRms, db);
                s.rmsDbSum += db;
                ++s.windows;
                s.histogram[(size_t) std::clamp ((int) std::floor (db + 60.0), 0, 60)]++;
            }
            windowFill = 0;
        }

        // Runs every UI tick: ballistics, then the spectrum and what is worked out from it.
        void update (double dt)
        {
            const bool fresh = freshSamples > 0;
            if (! fresh)
            {
                const auto fall = std::pow (rmsCoefficient, dt * rate);
                for (auto& p : rmsPower)
                    p *= fall;
                sumLR *= fall;
                sumLL *= fall;
                sumRR *= fall;
            }

            for (size_t ch = 0; ch < 2; ++ch)
            {
                peakDb[ch] = std::max (ampDb (tickPeak[ch]), peakDb[ch] - 24.0 * dt);
                rmsDb[ch] = powerDb (rmsPower[ch]) + 3.0103;   // a sine's RMS reads the same as its peak (AES-17)
                if (peakDb[ch] >= holdDb[ch])
                {
                    holdDb[ch] = peakDb[ch];
                    holdAge[ch] = 0.0;
                }
                else if ((holdAge[ch] += dt) > 2.0)
                    holdDb[ch] -= 24.0 * dt;
                maxTrue[ch] = std::max (maxTrue[ch], tickTrue[ch]);
            }
            tickPeak = tickTrue = {};

            hasSignal = sumLL + sumRR > 1.0e-9;
            correlation = sumLL * sumRR > 1.0e-18 ? std::clamp (sumLR / std::sqrt (sumLL * sumRR), -1.0, 1.0) : 0.0;
            balanceDb = hasSignal ? std::clamp (powerDb (sumRR + 1.0e-12) - powerDb (sumLL + 1.0e-12), -40.0, 40.0) : 0.0;

            updateSpectrum (dt, fresh);
            freshSamples = 0;
        }

        void updateSpectrum (double dt, bool fresh)
        {
            const int n = fftSize(), bins = numBins();
            const double tau = response == 0 ? 0.04 : response == 1 ? 0.15 : 0.6;
            const double a = 1.0 - std::exp (-dt / tau);

            if (fresh)
            {
                // Left in the real part and right in the imaginary part: one transform for both.
                auto pos = (ringPos + ringSize - (size_t) n) & (ringSize - 1);
                for (int i = 0; i < n; ++i)
                {
                    buffer[(size_t) i] = { ringLeft[pos] * window[(size_t) i], ringRight[pos] * window[(size_t) i] };
                    pos = (pos + 1) & (ringSize - 1);
                }
                fft->forward (buffer);
            }

            for (int k = 0; k < bins; ++k)
            {
                std::complex<double> l, r;
                if (fresh)
                {
                    const std::complex<double> z = buffer[(size_t) k], zc = std::conj ((std::complex<double>) buffer[(size_t) ((n - k) % n)]);
                    l = (z + zc) * 0.5 * windowScale;
                    r = (z - zc) * std::complex<double> (0.0, -0.5) * windowScale;
                }
                const double now[4] { std::norm (l), std::norm (r), std::norm ((l + r) * 0.5), std::norm ((l - r) * 0.5) };
                for (int c = 0; c < 4; ++c)
                    power[(size_t) c][(size_t) k] += (now[c] - power[(size_t) c][(size_t) k]) * a;
                cross[(size_t) k] += (l * std::conj (r) - cross[(size_t) k]) * a;
                if (accumulate && fresh)
                {
                    for (int c = 0; c < 4; ++c)
                        totalPower[(size_t) c][(size_t) k] += now[c];
                    totalCross[(size_t) k] += l * std::conj (r);
                }
            }
            if (accumulate && fresh)
                ++spectra;
            for (int c = 0; c < 4; ++c)
                for (int k = 0; k < bins; ++k)
                    prefix[(size_t) c][(size_t) k + 1] = prefix[(size_t) c][(size_t) k] + power[(size_t) c][(size_t) k];

            // The strongest frequency of each side, between bins by a parabola through the peak.
            const int first = std::max (1, (int) (lowestHz / binHz())), last = std::min (bins - 2, (int) (highestHz / binHz()));
            for (size_t ch = 0; ch < 2; ++ch)
            {
                const auto& p = power[ch];
                int best = first;
                for (int k = first; k <= last; ++k)
                    if (p[(size_t) k] > p[(size_t) best])
                        best = k;
                if (powerDb (p[(size_t) best]) < -80.0)
                {
                    dominant[ch] = 0.0;
                    continue;
                }
                const auto y0 = powerDb (p[(size_t) best - 1]), y1 = powerDb (p[(size_t) best]), y2 = powerDb (p[(size_t) best + 1]);
                const auto denominator = y0 - 2.0 * y1 + y2;
                const auto offset = std::abs (denominator) > 1.0e-9 ? std::clamp (0.5 * (y0 - y2) / denominator, -0.5, 0.5) : 0.0;
                dominant[ch] = (best + offset) * binHz();
            }

            // Correlation per third octave, from the cross spectrum.
            for (int b = 0; b < numBands; ++b)
            {
                const auto centre = 1000.0 * std::pow (2.0, (b - 17) / 3.0);
                const auto [lo, hi] = binRange (centre, 1.0 / 3.0);
                double re = 0.0, pl = 0.0, pr = 0.0;
                for (int k = lo; k <= hi; ++k)
                {
                    re += cross[(size_t) k].real();
                    pl += power[left][(size_t) k];
                    pr += power[right][(size_t) k];
                }
                bandLevel[(size_t) b] = powerDb ((pl + pr) * 0.5);
                bandCorrelation[(size_t) b] = pl * pr > 1.0e-24 ? std::clamp (re / std::sqrt (pl * pr), -1.0, 1.0) : 0.0;
            }

            // Phase wheel: where each 1/24 octave sits between mono (up), left or right (the sides) and inverse (down).
            wheel.clear();
            const auto top = std::min (highestHz, nyquist());
            for (double hz = lowestHz; hz < top; hz *= std::pow (2.0, 1.0 / 24.0))
            {
                const auto [lo, hi] = binRange (hz, 1.0 / 24.0);
                const auto sum = [&] (int c) { return prefix[(size_t) c][(size_t) hi + 1] - prefix[(size_t) c][(size_t) lo]; };
                const double pl = sum (left), pr = sum (right), pm = sum (mid), ps = sum (side);
                const auto level = powerDb ((pl + pr) * 0.5 / (hi - lo + 1));
                if (level < -90.0)
                    continue;
                const auto angle = 2.0 * std::atan2 (std::sqrt (ps), std::sqrt (pm));   // 0 mono, pi/2 one side, pi inverse
                wheel.push_back ({ pl >= pr ? -angle : angle, std::log (hz / lowestHz) / std::log (highestHz / lowestHz), level, hz });
            }
        }

        std::pair<int, int> binRange (double hz, double octaves) const
        {
            const auto half = std::pow (2.0, octaves / 2.0);
            const int lo = std::clamp ((int) std::ceil (hz / half / binHz()), 1, numBins() - 1);
            const int hi = std::clamp ((int) std::floor (hz * half / binHz()), lo, numBins() - 1);
            return { lo, hi };
        }

        // The level of a channel at a frequency in dB, averaged over `octaves` around it
        // (or between the two nearest bins when they are further apart than that).
        double levelAt (Channel channel, double hz, double octaves) const
        {
            if (channel == average)
                return powerDb ((powerAt (left, hz, octaves) + powerAt (right, hz, octaves)) * 0.5);
            return powerDb (powerAt (channel, hz, octaves));
        }

        double powerAt (Channel channel, double hz, double octaves) const
        {
            const auto& p = power[(size_t) channel];
            const auto half = std::pow (2.0, octaves / 2.0);
            const double from = hz / half / binHz(), to = hz * half / binHz();
            if (to - from < 1.0)
            {
                const auto x = std::clamp (hz / binHz(), 0.0, (double) numBins() - 1.001);
                const auto k = (size_t) x;
                const auto t = x - (double) k;
                return p[k] + (p[k + 1] - p[k]) * t;
            }
            const int lo = std::clamp ((int) std::lround (from), 0, numBins() - 1), hi = std::clamp ((int) std::lround (to), lo, numBins() - 1);
            const auto& s = prefix[(size_t) channel];
            return (s[(size_t) hi + 1] - s[(size_t) lo]) / (hi - lo + 1);
        }

        QString statsText() const;

        // For a whole-file report: every spectrum added up, and the sums of whole-file products.
        bool accumulate = false;
        std::array<std::vector<double>, 4> totalPower;
        std::vector<std::complex<double>> totalCross;
        int spectra = 0;
        double allLR = 0.0, allLL = 0.0, allRR = 0.0;
    };

    struct StatRow { QString name; QString values[2]; };

    std::vector<StatRow> statRows (const State& s)
    {
        std::vector<StatRow> rows;
        const auto row = [&rows] (const QString& name, auto&& value)
        {
            rows.push_back ({ name, { value (0), value (1) } });
        };
        const auto stat = [&s] (int ch) -> const State::Stats& { return s.stats[(size_t) ch]; };
        const auto totalRms = [&s, &stat] (int ch) { return s.frames > 0 ? powerDb (stat (ch).squares / (double) s.frames) : minusInfinity; };
        const auto peak = [&stat] (int ch) { return ampDb (std::max (-(double) stat (ch).minimum, (double) stat (ch).maximum)); };
        const auto measured = [&s] (auto&& f) { return [&s, f] (int ch) { return s.frames > 0 ? f (ch) : QStringLiteral ("-"); }; };
        const auto rmsMeasured = [&stat] (auto&& f) { return [&stat, f] (int ch) { return stat (ch).windows > 0 ? f (ch) : QStringLiteral ("-"); }; };

        row (QObject::tr ("Peak amplitude"), measured ([&] (int ch) { return number (peak (ch), 2, "dB"); }));
        row (QObject::tr ("True peak amplitude"), measured ([&] (int ch) { return number (ampDb (s.maxTrue[(size_t) ch]), 2, "dBTP"); }));
        row (QObject::tr ("Maximum sample value"), measured ([&] (int ch) { return QString::number (stat (ch).maximum, 'f', 5); }));
        row (QObject::tr ("Minimum sample value"), measured ([&] (int ch) { return QString::number (stat (ch).minimum, 'f', 5); }));
        row (QObject::tr ("Possibly clipped samples"), measured ([&] (int ch) { return QString::number (stat (ch).clipped); }));
        row (QObject::tr ("DC offset"), measured ([&] (int ch) { return QStringLiteral ("%1 %").arg (stat (ch).sum / (double) s.frames * 100.0, 0, 'f', 3); }));
        row (QObject::tr ("Total RMS amplitude"), measured ([&] (int ch) { return number (totalRms (ch), 2, "dB"); }));
        row (QObject::tr ("Maximum RMS amplitude"), rmsMeasured ([&] (int ch) { return number (stat (ch).maxRms, 2, "dB"); }));
        row (QObject::tr ("Minimum RMS amplitude"), rmsMeasured ([&] (int ch) { return number (stat (ch).minRms, 2, "dB"); }));
        row (QObject::tr ("Average RMS amplitude"), rmsMeasured ([&] (int ch) { return number (stat (ch).rmsDbSum / stat (ch).windows, 2, "dB"); }));
        row (QObject::tr ("Dynamic range"), rmsMeasured ([&] (int ch) { return number (stat (ch).maxRms - stat (ch).minRms, 2, "dB"); }));
        row (QObject::tr ("Crest factor"), measured ([&] (int ch) { return number (peak (ch) - totalRms (ch), 2, "dB"); }));
        return rows;
    }

    QString State::statsText() const
    {
        QStringList lines { QObject::tr ("Amplitude statistics of %1 heard\tLeft\tRight")
                                .arg (QStringLiteral ("%1:%2").arg ((int) seconds() / 60).arg ((int) seconds() % 60, 2, 10, QLatin1Char ('0'))) };
        for (const auto& r : statRows (*this))
            lines << r.name + QLatin1Char ('\t') + r.values[0] + QLatin1Char ('\t') + r.values[1];
        lines << QObject::tr ("ITU-R BS.1770 integrated loudness\t%1").arg (number (integrated, 2, "LUFS"))
              << QObject::tr ("Loudness range\t%1").arg (number (range, 1, "LU"))
              << QObject::tr ("Maximum momentary loudness\t%1").arg (number (maxMomentary, 1, "LUFS"))
              << QObject::tr ("Maximum short-term loudness\t%1").arg (number (maxShortTerm, 1, "LUFS"));
        return lines.join (QLatin1Char ('\n')) + QLatin1Char ('\n');
    }

    //==============================================================================
    QString fileReport (const AudioData& data, const QString& path, int targetIndex)
    {
        const int channels = data.numChannels();
        const auto rate = data.sampleRate();
        const auto length = data.length();
        State s;
        s.setRate (rate);
        s.setFft (13, 1);
        s.accumulate = true;

        // Mono is heard on both sides; more channels are folded down to stereo as PHINYX plays them.
        const auto fold = channels > 2 ? dsp::stereoDownmix (channels) : std::array<std::vector<float>, 2> {};
        const int block = s.fftSize();
        std::vector<std::vector<float>> buffers ((size_t) channels, std::vector<float> ((size_t) block));
        std::vector<float*> pointers;
        for (auto& b : buffers)
            pointers.push_back (b.data());
        std::vector<float> left, right;
        for (Frame pos = 0; pos < length; pos += block)
        {
            const auto count = (size_t) std::min<Frame> (block, length - pos);
            data.read (pos, (int) count, pointers.data());
            left.assign (count, 0.0f);
            right.assign (count, 0.0f);
            for (size_t i = 0; i < count; ++i)
            {
                if (channels == 1)
                    left[i] = right[i] = buffers[0][i];
                else if (channels == 2)
                {
                    left[i] = buffers[0][i];
                    right[i] = buffers[1][i];
                }
                else
                    for (size_t ch = 0; ch < (size_t) channels; ++ch)
                    {
                        left[i] += fold[0][ch] * buffers[ch][i];
                        right[i] += fold[1][ch] * buffers[ch][i];
                    }
            }
            s.add (left, right);
            s.update ((double) count / rate);
        }
        s.integrated = dsp::integratedLoudness (s.blocks);
        s.range = s.loudnessRange();
        const auto exact = dsp::measureLoudness (data, 0, length);   // every channel, weighted as BS.1770 says
        const auto monoCorrection = channels == 1 ? -3.0103 : 0.0;   // a mono channel counts once, not on both sides

        QStringList out;
        const auto line = [&out] (const QString& name, const QString& value)
        {
            out << QStringLiteral ("  ") + name.leftJustified (35, QLatin1Char (' ')) + value;
        };
        const auto heading = [&out] (const QString& title)
        {
            out << QString() << title.toUpper() << QString (title.size(), QLatin1Char ('-'));
        };
        const auto clock = [] (double seconds)
        {
            const auto ms = (long long) std::llround (seconds * 1000.0);
            return QStringLiteral ("%1:%2.%3").arg (ms / 60000).arg ((ms / 1000) % 60, 2, 10, QLatin1Char ('0')).arg (ms % 1000, 3, 10, QLatin1Char ('0'));
        };

        const QFileInfo info (path);
        const QString title = QObject::tr ("PHINYX analysis report");
        out << title << QString (title.size(), QLatin1Char ('='));
        line (QObject::tr ("File"), info.fileName());
        line (QObject::tr ("Folder"), QDir::toNativeSeparators (info.absolutePath()));
        line (QObject::tr ("Length"), QObject::tr ("%1 (%2 frames)").arg (clock (data.lengthSeconds())).arg (length));
        line (QObject::tr ("Format"), QObject::tr ("%1 Hz, %2 (%3)").arg (rate, 0, 'f', 0)
                                          .arg (channels == 1 ? QObject::tr ("1 channel") : QObject::tr ("%1 channels").arg (channels), QString::fromStdString (dsp::speakerLayout (channels).name)));
        if (info.exists())
            line (QObject::tr ("File size"), QStringLiteral ("%1 MB").arg (info.size() / 1048576.0, 0, 'f', 2));
        line (QObject::tr ("Measured"), QDateTime::currentDateTime().toString (QStringLiteral ("yyyy-MM-dd hh:mm")));
        if (channels > 2)
            out << QString() << QObject::tr ("  Loudness counts every channel; the rest is of the stereo fold-down, as PHINYX plays it.");
        if (channels == 1)
            out << QString() << QObject::tr ("  Mono: Left and Right below are the same channel.");

        heading (QObject::tr ("Loudness (ITU-R BS.1770 / EBU R128)"));
        const bool measured = exact.integratedLufs > -150.0;
        line (QObject::tr ("Integrated loudness"), number (exact.integratedLufs, 2, "LUFS"));
        line (QObject::tr ("Loudness range"), number (exact.loudnessRange, 1, "LU"));
        line (QObject::tr ("Max momentary (400 ms)"), number (s.maxMomentary + monoCorrection, 1, "LUFS"));
        line (QObject::tr ("Max short-term (3 s)"), number (s.maxShortTerm + monoCorrection, 1, "LUFS"));
        line (QObject::tr ("True peak"), number (exact.truePeakDb, 2, "dBTP"));
        line (QObject::tr ("Sample peak"), number (exact.samplePeakDb, 2, "dBFS"));
        if (measured)
        {
            line (QObject::tr ("PLR (true peak - integrated)"), number (exact.truePeakDb - exact.integratedLufs, 1, "LU"));
            if (s.maxShortTerm > -150.0)
                line (QObject::tr ("PSR (true peak - max short-term)"), number (exact.truePeakDb - (s.maxShortTerm + monoCorrection), 1, "LU"));
        }

        const auto& target = targets[(size_t) std::clamp (targetIndex, 0, (int) targets.size() - 1)];
        out << QString();
        line (QObject::tr ("Target"), QCoreApplication::translate ("AnalysisWindow", target.name));
        if (measured)
        {
            const auto lu = exact.integratedLufs - target.lufs;
            line (QObject::tr ("  Loudness"), std::abs (lu) <= 0.5 ? QObject::tr ("on target (%1 LU)").arg (signedNumber (lu, 1))
                                              : lu > 0.0 ? QObject::tr ("%1 LU above: normalised playback turns it down %1 dB").arg (lu, 0, 'f', 1)
                                                         : QObject::tr ("%1 LU below the target").arg (-lu, 0, 'f', 1));
        }
        const auto headroom = target.truePeak - exact.truePeakDb;
        line (QObject::tr ("  True peak"), headroom >= 0.0 ? QObject::tr ("within the %1 dBTP ceiling, %2 dB to spare").arg (target.truePeak).arg (headroom, 0, 'f', 2)
                                                           : QObject::tr ("%1 dB over the %2 dBTP ceiling").arg (-headroom, 0, 'f', 2).arg (target.truePeak));

        heading (QObject::tr ("Amplitude statistics"));
        out << QStringLiteral ("  ") + QString().leftJustified (35, QLatin1Char (' ')) + QObject::tr ("Left").rightJustified (14) + QObject::tr ("Right").rightJustified (14);
        for (const auto& r : statRows (s))
            out << QStringLiteral ("  ") + r.name.leftJustified (35, QLatin1Char (' ')) + r.values[0].rightJustified (14) + r.values[1].rightJustified (14);

        heading (QObject::tr ("Stereo"));
        const auto correlation = s.allLL * s.allRR > 1.0e-18 ? s.allLR / std::sqrt (s.allLL * s.allRR) : 0.0;
        const auto midPower = (s.allLL + s.allRR + 2.0 * s.allLR) / 4.0, sidePower = (s.allLL + s.allRR - 2.0 * s.allLR) / 4.0;
        const auto balance = s.allLL > 0.0 && s.allRR > 0.0 ? powerDb (s.allRR) - powerDb (s.allLL) : 0.0;
        line (QObject::tr ("Correlation (whole file)"), QString::number (correlation, 'f', 3) + QStringLiteral ("   ")
                                                        + (correlation > 0.9 ? QObject::tr ("(nearly mono)") : correlation > 0.3 ? QObject::tr ("(mono-safe stereo)")
                                                           : correlation > 0.0 ? QObject::tr ("(wide)") : QObject::tr ("(out of phase: loses a lot in mono)")));
        line (QObject::tr ("Balance"), std::abs (balance) < 0.05 ? QObject::tr ("centre")
                                       : QObject::tr ("%1 louder by %2 dB").arg (balance > 0.0 ? QObject::tr ("right") : QObject::tr ("left")).arg (std::abs (balance), 0, 'f', 2));
        line (QObject::tr ("Side against mid"), sidePower > 1.0e-12 && midPower > 1.0e-12 ? number (powerDb (sidePower) - powerDb (midPower), 1, "dB")
                                                                                       : QObject::tr ("no side (mono)"));

        // The long-term spectrum, corrected for the window's noise bandwidth so a band reads its power.
        const auto spectra = std::max (1, s.spectra);
        const auto squares = std::accumulate (s.window.begin(), s.window.end(), 0.0, [] (double a, float w) { return a + (double) w * w; });
        const auto sum = std::accumulate (s.window.begin(), s.window.end(), 0.0);
        const auto noiseBandwidth = s.fftSize() * squares / (sum * sum);
        const auto strongest = [&] (int ch)
        {
            const auto& p = s.totalPower[(size_t) ch];
            const int first = std::max (1, (int) (lowestHz / s.binHz())), last = std::min (s.numBins() - 2, (int) (highestHz / s.binHz()));
            int best = first;
            for (int k = first; k <= last; ++k)
                if (p[(size_t) k] > p[(size_t) best])
                    best = k;
            if (powerDb (p[(size_t) best] / spectra) < -90.0)
                return QStringLiteral ("-");
            const auto y0 = powerDb (p[(size_t) best - 1]), y1 = powerDb (p[(size_t) best]), y2 = powerDb (p[(size_t) best + 1]);
            const auto d = y0 - 2.0 * y1 + y2;
            const auto hz = (best + (std::abs (d) > 1.0e-9 ? std::clamp (0.5 * (y0 - y2) / d, -0.5, 0.5) : 0.0)) * s.binHz();
            return QStringLiteral ("%1  %2").arg (hzText (hz), noteText (hz));
        };
        line (QObject::tr ("Strongest frequency, left"), strongest (State::left));
        line (QObject::tr ("Strongest frequency, right"), strongest (State::right));

        heading (QObject::tr ("RMS histogram (50 ms windows, share of the time)"));
        {
            std::array<int, 12> buckets {};
            int total = 0;
            for (const auto& st : s.stats)
                for (int b = 0; b < 61; ++b)
                {
                    buckets[(size_t) std::min (11, b / 5)] += st.histogram[(size_t) b];
                    total += st.histogram[(size_t) b];
                }
            const auto most = std::max (1, *std::max_element (buckets.begin(), buckets.end()));
            for (int i = 11; i >= 0; --i)
            {
                const auto share = total > 0 ? 100.0 * buckets[(size_t) i] / total : 0.0;
                out << QStringLiteral ("  %1 to %2 dB  %3 %4 %")
                           .arg (-60 + i * 5, 3).arg (-55 + i * 5, 3)
                           .arg (QString ((int) std::lround (40.0 * buckets[(size_t) i] / most), QChar (0x2588)).leftJustified (40, QLatin1Char (' ')))
                           .arg (share, 5, 'f', 1);
            }
        }

        heading (QObject::tr ("Average spectrum (third octaves)"));
        out << QStringLiteral ("  ") + QObject::tr ("Band").leftJustified (10) + QObject::tr ("Left").rightJustified (12)
                   + QObject::tr ("Right").rightJustified (12) + QObject::tr ("Correlation").rightJustified (14);
        for (int b = 0; b < numBands; ++b)
        {
            const auto centre = 1000.0 * std::pow (2.0, (b - 17) / 3.0);
            if (centre > s.nyquist())
                break;
            const auto [lo, hi] = s.binRange (centre, 1.0 / 3.0);
            double pl = 0.0, pr = 0.0, re = 0.0;
            for (int k = lo; k <= hi; ++k)
            {
                pl += s.totalPower[State::left][(size_t) k];
                pr += s.totalPower[State::right][(size_t) k];
                re += s.totalCross[(size_t) k].real();
            }
            const auto bandDb = [&] (double p) { return number (powerDb (p / spectra / noiseBandwidth), 1, "dB"); };
            auto band = QString::fromLatin1 (bandNames[b]);
            band = band.endsWith (QLatin1Char ('k')) ? band.chopped (1) + QStringLiteral (" kHz") : band + QStringLiteral (" Hz");
            out << QStringLiteral ("  ") + band.leftJustified (10) + bandDb (pl).rightJustified (12)
                       + bandDb (pr).rightJustified (12)
                       + (pl * pr > 1.0e-24 ? signedNumber (std::clamp (re / std::sqrt (pl * pr), -1.0, 1.0), 2) : QStringLiteral ("-")).rightJustified (14);
        }
        out << QString() << QObject::tr ("Measured by PHINYX from the file itself, without the Master Section.");
        return out.join (QLatin1Char ('\n')) + QLatin1Char ('\n');
    }

    //==============================================================================
    // A meter: draws part of the State. step() runs once per tick unless frozen.
    class Meter : public QWidget
    {
    public:
        Meter (State& s, QWidget* parent) : QWidget (parent), state (s)
        {
            setAttribute (Qt::WA_OpaquePaintEvent, false);
            setFocusPolicy (Qt::NoFocus);
            setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
        }
        virtual void step (double /*dt*/) {}
        virtual void clear() {}

    protected:
        QFont smallFont (double scale = 0.8) const
        {
            auto f = font();
            f.setPointSizeF (f.pointSizeF() * scale);
            return f;
        }

        static void plotBackground (QPainter& g, const QRectF& r)
        {
            g.setPen (Qt::NoPen);
            g.setBrush (Theme::withOpacity (color ("meterBackground")));
            g.drawRoundedRect (r, 4.0, 4.0);
        }

        static QColor gridColor (double alpha = 0.5) { return faded (color ("mid"), alpha); }
        static QColor labelColor() { return color ("placeholderText"); }

        State& state;
    };

    //==============================================================================
    // A big gauge of the short-term loudness (thick) and the momentary (thin, outside)
    // around the target, the integrated loudness in the middle and the rest beside it.
    class LoudnessMeter final : public Meter
    {
    public:
        using Meter::Meter;
        QSize sizeHint() const override { return { 420, 260 }; }
        QSize minimumSizeHint() const override { return { 160, 140 }; }

    protected:
        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            const auto r = QRectF (rect()).adjusted (6, 6, -6, -6);
            const bool tableBeside = r.width() > r.height() * 1.45;
            const auto side = tableBeside ? std::min (r.height(), r.width() * 0.56) : std::min (r.width(), r.height() * 0.62);
            const QRectF gaugeArea (r.left(), r.top(), side, side);
            drawGauge (g, gaugeArea);
            const auto table = tableBeside ? QRectF (gaugeArea.right() + 10, r.top(), r.right() - gaugeArea.right() - 10, r.height())
                                           : QRectF (r.left(), gaugeArea.bottom() + 4, r.width(), r.bottom() - gaugeArea.bottom() - 4);
            drawTable (g, table, ! tableBeside);
        }

    private:
        double low() const  { return state.targetLufs - 27.0; }
        double high() const { return state.targetLufs + 9.0; }
        double fraction (double lufs) const { return std::clamp ((lufs - low()) / (high() - low()), 0.0, 1.0); }
        static double angleAt (double f) { return 225.0 - 270.0 * f; }   // degrees, counter-clockwise from 3 o'clock

        void arc (QPainter& g, const QPointF& c, double radius, double width, double f0, double f1, const QColor& colour) const
        {
            if (f1 <= f0)
                return;
            QPen pen (colour, width);
            pen.setCapStyle (Qt::FlatCap);
            g.setPen (pen);
            g.setBrush (Qt::NoBrush);
            g.drawArc (QRectF (c.x() - radius, c.y() - radius, radius * 2, radius * 2), (int) std::lround (angleAt (f0) * 16.0),
                       (int) std::lround (-270.0 * (f1 - f0) * 16.0));
        }

        // The track coloured by zone: faint everywhere, solid up to `value`.
        void zonedArc (QPainter& g, const QPointF& c, double radius, double width, double value, double alpha) const
        {
            double upper = high();
            for (const auto& z : zones)
            {
                const auto lower = std::max (low(), state.targetLufs + z.from);
                if (upper > lower)
                {
                    const auto zoneColour = color (z.key);
                    arc (g, c, radius, width, fraction (lower), fraction (upper), faded (zoneColour, alpha));
                    if (value > lower)
                        arc (g, c, radius, width, fraction (lower), fraction (std::min (value, upper)), zoneColour);
                }
                upper = std::min (upper, lower);
            }
        }

        static QPointF pointAt (const QPointF& c, double radius, double f)
        {
            const auto a = angleAt (f) * M_PI / 180.0;
            return { c.x() + radius * std::cos (a), c.y() - radius * std::sin (a) };
        }

        void drawGauge (QPainter& g, const QRectF& area) const
        {
            const auto c = area.center() + QPointF (0, area.height() * 0.04);
            const auto outer = area.width() * 0.46;
            const auto thick = outer * 0.15;
            const auto trackRadius = outer - outer * 0.08 - thick / 2.0;

            // Momentary outside, short-term inside.
            zonedArc (g, c, outer - outer * 0.025, outer * 0.05, state.momentary, 0.12);
            zonedArc (g, c, trackRadius, thick, state.shortTerm, 0.16);

            // The loudest short-term so far, and the target.
            if (state.maxShortTerm > low())
            {
                g.setPen (QPen (color ("meterPeakHold"), 2.0));
                g.drawLine (pointAt (c, trackRadius - thick / 2.0, fraction (state.maxShortTerm)),
                            pointAt (c, trackRadius + thick / 2.0, fraction (state.maxShortTerm)));
            }
            g.setPen (QPen (color ("windowText"), 2.5, Qt::SolidLine, Qt::RoundCap));
            const auto target = fraction (state.targetLufs);
            g.drawLine (pointAt (c, trackRadius - thick / 2.0 - 5.0, target), pointAt (c, outer + 2.0, target));

            // Scale every 6 LU, numbered against the target.
            auto f = smallFont (0.75);
            g.setFont (f);
            for (double lu = -24.0; lu <= 6.0; lu += 3.0)
            {
                const auto at = fraction (state.targetLufs + lu);
                const bool major = std::fmod (std::abs (lu), 6.0) < 0.01;
                g.setPen (QPen (gridColor (major ? 0.9 : 0.5), 1.0));
                g.drawLine (pointAt (c, trackRadius - thick / 2.0 - 2.0, at), pointAt (c, trackRadius - thick / 2.0 - (major ? 7.0 : 4.0), at));
                if (major && outer > 70.0)
                {
                    g.setPen (labelColor());
                    const auto p = pointAt (c, trackRadius - thick / 2.0 - 16.0, at);
                    g.drawText (QRectF (p.x() - 16, p.y() - 8, 32, 16), Qt::AlignCenter, QString::number (state.targetLufs + lu, 'f', 0));
                }
            }

            // The integrated loudness in the middle.
            const bool measured = state.integrated > -150.0;
            auto big = font();
            big.setPixelSize (std::max (10, (int) (outer * 0.36)));
            big.setWeight (QFont::DemiBold);
            g.setFont (big);
            g.setPen (measured ? loudnessColor (state.integrated - state.targetLufs) : labelColor());
            const QRectF centre (c.x() - outer, c.y() - outer * 0.32, outer * 2, outer * 0.42);
            g.drawText (centre, Qt::AlignHCenter | Qt::AlignBottom, measured ? QString::number (state.integrated, 'f', 1) : QStringLiteral ("--"));
            auto caption = font();
            caption.setPixelSize (std::max (8, (int) (outer * 0.1)));
            caption.setLetterSpacing (QFont::PercentageSpacing, 112);
            g.setFont (caption);
            g.setPen (labelColor());
            const auto line = outer * 0.13;
            g.drawText (QRectF (c.x() - outer, centre.bottom() + 2, outer * 2, line), Qt::AlignCenter, QObject::tr ("LUFS INTEGRATED"));
            if (measured)
            {
                g.setPen (color ("windowText"));
                g.drawText (QRectF (c.x() - outer, centre.bottom() + 2 + line, outer * 2, line), Qt::AlignCenter,
                            QObject::tr ("%1 LU from target").arg (signedNumber (state.integrated - state.targetLufs, 1)));
            }

            // Short-term underneath, between the ends of the arc.
            auto mid = font();
            mid.setPixelSize (std::max (9, (int) (outer * 0.15)));
            g.setFont (mid);
            g.setPen (state.shortTerm > -150.0 ? loudnessColor (state.shortTerm - state.targetLufs) : labelColor());
            g.drawText (QRectF (c.x() - outer, c.y() + outer * 0.5, outer * 2, outer * 0.2), Qt::AlignCenter,
                        number (state.shortTerm, 1) + QStringLiteral (" S"));
        }

        void drawTable (QPainter& g, const QRectF& area, bool twoColumns) const
        {
            if (area.width() < 110 || area.height() < 40)
                return;
            const double plr = state.integrated > -150.0 ? ampDb (std::max (state.maxTrue[0], state.maxTrue[1])) - state.integrated : minusInfinity;
            const auto truePeak = ampDb (std::max (state.maxTrue[0], state.maxTrue[1]));
            const int secs = (int) state.seconds();
            struct Row { QString name, value; QColor colour; };
            const auto text = color ("windowText");
            const auto zone = [this, &text] (double v) { return v > -150.0 ? loudnessColor (v - state.targetLufs) : text; };
            const Row rows[] {
                { QObject::tr ("Momentary"), number (state.momentary, 1, "LUFS"), zone (state.momentary) },
                { QObject::tr ("Short-term"), number (state.shortTerm, 1, "LUFS"), zone (state.shortTerm) },
                { QObject::tr ("Max momentary"), number (state.maxMomentary, 1, "LUFS"), text },
                { QObject::tr ("Max short-term"), number (state.maxShortTerm, 1, "LUFS"), text },
                { QObject::tr ("Loudness range"), number (state.range, 1, "LU"), text },
                { QObject::tr ("True peak max"), number (truePeak, 1, "dBTP"), truePeak > state.targetTruePeak ? color ("meterHigh") : text },
                { QObject::tr ("PLR"), plr > -150.0 ? number (plr, 1, "LU") : QStringLiteral ("-"), text },
                { QObject::tr ("Measured"), QStringLiteral ("%1:%2").arg (secs / 60).arg (secs % 60, 2, 10, QLatin1Char ('0')), text },
            };
            const int count = (int) std::size (rows);
            const int perColumn = twoColumns ? (count + 1) / 2 : count;
            const auto columnWidth = area.width() / (twoColumns ? 2 : 1);
            const auto rowHeight = std::min (area.height() / perColumn, 30.0);
            auto nameFont = smallFont (0.82), valueFont = font();
            valueFont.setWeight (QFont::DemiBold);
            for (int i = 0; i < count; ++i)
            {
                const QRectF cell (area.left() + (i / perColumn) * columnWidth, area.top() + (i % perColumn) * rowHeight, columnWidth - 8, rowHeight);
                g.setPen (gridColor (0.35));
                g.drawLine (QPointF (cell.left(), cell.bottom() - 0.5), QPointF (cell.right(), cell.bottom() - 0.5));
                g.setFont (nameFont);
                g.setPen (labelColor());
                g.drawText (cell, Qt::AlignLeft | Qt::AlignVCenter, rows[i].name);
                g.setFont (valueFont);
                g.setPen (rows[i].colour);
                g.drawText (cell, Qt::AlignRight | Qt::AlignVCenter, rows[i].value);
            }
        }
    };

    //==============================================================================
    // Short-term (filled) and momentary (thin) loudness over time, around the target.
    class HistoryMeter final : public Meter
    {
    public:
        using Meter::Meter;
        double spanSeconds = 60.0;
        QSize sizeHint() const override { return { 520, 260 }; }
        QSize minimumSizeHint() const override { return { 180, 120 }; }

    protected:
        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            const auto plot = QRectF (rect()).adjusted (36, 6, -8, -20);
            plotBackground (g, plot);
            const double top = state.targetLufs + 9.0, bottom = state.targetLufs - 27.0;
            const auto yFor = [&] (double lufs) { return plot.top() + plot.height() * (top - std::clamp (lufs, bottom, top)) / (top - bottom); };

            // Target band and grid.
            g.fillRect (QRectF (plot.left(), yFor (state.targetLufs + 1.0), plot.width(), yFor (state.targetLufs - 1.0) - yFor (state.targetLufs + 1.0)),
                        faded (color ("levelMedium"), 0.13));
            g.setFont (smallFont (0.75));
            for (double lu = 6.0; lu >= -24.0; lu -= 6.0)
            {
                const auto y = yFor (state.targetLufs + lu);
                g.setPen (QPen (gridColor (lu == 0.0 ? 0.0 : 0.4), 1.0, Qt::DotLine));
                g.drawLine (QPointF (plot.left(), y), QPointF (plot.right(), y));
                g.setPen (labelColor());
                g.drawText (QRectF (0, y - 8, plot.left() - 5, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number (state.targetLufs + lu, 'f', 0));
            }
            g.setPen (QPen (faded (color ("levelMedium"), 0.9), 1.2, Qt::DashLine));
            g.drawLine (QPointF (plot.left(), yFor (state.targetLufs)), QPointF (plot.right(), yFor (state.targetLufs)));

            // Time along the bottom.
            const int steps = 6;
            for (int i = 0; i <= steps; ++i)
            {
                const auto x = plot.left() + plot.width() * i / steps;
                const auto secondsAgo = spanSeconds * (steps - i) / steps;
                if (i > 0 && i < steps)
                {
                    g.setPen (QPen (gridColor (0.25), 1.0, Qt::DotLine));
                    g.drawLine (QPointF (x, plot.top()), QPointF (x, plot.bottom()));
                }
                g.setPen (labelColor());
                const auto label = i == steps ? QObject::tr ("now")
                                   : secondsAgo >= 60.0 ? QStringLiteral ("-%1:%2").arg ((int) secondsAgo / 60).arg ((int) secondsAgo % 60, 2, 10, QLatin1Char ('0'))
                                                        : QStringLiteral ("-%1s").arg (secondsAgo, 0, 'f', 0);
                g.drawText (QRectF (x - 30, plot.bottom() + 3, 60, 14), (i == steps ? Qt::AlignRight : i == 0 ? Qt::AlignLeft : Qt::AlignHCenter) | Qt::AlignTop,
                            label);
            }

            const auto& shortHistory = state.shortHistory;
            if (shortHistory.empty())
                return;

            // Pixels to blocks: the loudest block wherever several share a pixel.
            const auto blocksShown = spanSeconds * 10.0;
            const int columns = std::max (2, (int) plot.width());
            const auto series = [&] (const std::vector<float>& values)
            {
                QPolygonF line;
                const auto n = (double) values.size();
                for (int x = 0; x < columns; ++x)
                {
                    const auto fromBlock = n - blocksShown * (columns - x) / columns, toBlock = n - blocksShown * (columns - x - 1) / columns;
                    if (toBlock < 1.0)
                        continue;
                    const auto lo = (size_t) std::max (0.0, std::floor (fromBlock)), hi = (size_t) std::clamp (std::ceil (toBlock), (double) lo + 1, n);
                    const auto v = *std::max_element (values.begin() + (long) lo, values.begin() + (long) hi);
                    line << QPointF (plot.left() + x, yFor (v));
                }
                return line;
            };

            // The zones as a gradient, top to bottom, with hard edges.
            const auto zoneGradient = [&] (double alphaTop, double alphaBottom)
            {
                QLinearGradient gradient (0, plot.top(), 0, plot.bottom());
                double upper = top;
                QColor previous;
                for (const auto& z : zones)
                {
                    const auto lower = std::max (bottom, state.targetLufs + z.from);
                    const auto t0 = (yFor (upper) - plot.top()) / plot.height(), t1 = (yFor (lower) - plot.top()) / plot.height();
                    const auto alphaAt = [&] (double t) { return alphaTop + (alphaBottom - alphaTop) * t; };
                    const auto c = color (z.key);
                    gradient.setColorAt (std::min (1.0, t0 + 0.0001), faded (c, alphaAt (t0)));
                    gradient.setColorAt (std::max (0.0, t1 - 0.0001), faded (c, alphaAt (t1)));
                    upper = lower;
                    if (lower <= bottom)
                        break;
                }
                return gradient;
            };

            const auto shortLine = series (shortHistory);
            if (shortLine.size() >= 2)
            {
                g.save();
                g.setClipRect (plot);
                QPainterPath fill;
                fill.moveTo (shortLine.first().x(), plot.bottom());
                for (const auto& p : shortLine)
                    fill.lineTo (p);
                fill.lineTo (shortLine.last().x(), plot.bottom());
                fill.closeSubpath();
                g.fillPath (fill, zoneGradient (0.55, 0.12));

                const auto momentaryLine = series (state.momentaryHistory);
                g.setPen (QPen (faded (color ("windowText"), 0.35), 1.0));
                g.drawPolyline (momentaryLine);
                g.setPen (QPen (QBrush (zoneGradient (1.0, 1.0)), 1.8));
                g.drawPolyline (shortLine);
                g.restore();
            }

            // The integrated loudness so far.
            if (state.integrated > -150.0)
            {
                const auto y = yFor (state.integrated);
                g.setPen (QPen (color ("highlight"), 1.2, Qt::DashDotLine));
                g.drawLine (QPointF (plot.left(), y), QPointF (plot.right(), y));
                g.setFont (smallFont (0.8));
                const auto label = QObject::tr ("I %1").arg (state.integrated, 0, 'f', 1);
                const QRectF box (plot.left() + 6, y - 16, g.fontMetrics().horizontalAdvance (label) + 10, 14);
                g.setPen (Qt::NoPen);
                g.setBrush (faded (color ("base"), 0.8));
                g.drawRoundedRect (box, 3, 3);
                g.setPen (color ("highlight"));
                g.drawText (box, Qt::AlignCenter, label);
            }
        }
    };

    //==============================================================================
    // Left and right: RMS solid, peak lighter above it, a peak hold, true peak and clips.
    class LevelsMeter final : public Meter
    {
    public:
        using Meter::Meter;
        QSize sizeHint() const override { return { 170, 260 }; }
        QSize minimumSizeHint() const override { return { 110, 140 }; }

    protected:
        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            constexpr double floor = -60.0;
            const auto r = QRectF (rect()).adjusted (6, 4, -6, -4);
            const auto barWidth = std::min (34.0, r.width() * 0.3);
            const auto scaleWidth = r.width() - barWidth * 2;
            const QRectF bars (r.left(), r.top() + 32, r.width(), r.height() - 32 - 36);
            const auto yFor = [&] (double db) { return bars.top() + bars.height() * (std::clamp (db, floor, 0.0) / floor); };

            g.setFont (smallFont (0.75));
            for (double db : { 0.0, -3.0, -6.0, -9.0, -12.0, -18.0, -24.0, -30.0, -36.0, -48.0, -60.0 })
            {
                if ((db == -3.0 || db == -9.0 || db == -30.0) && bars.height() < 300)
                    continue;
                const auto y = yFor (db);
                g.setPen (gridColor (0.6));
                g.drawLine (QPointF (r.left() + barWidth + 3, y), QPointF (r.left() + barWidth + 7, y));
                g.drawLine (QPointF (r.right() - barWidth - 7, y), QPointF (r.right() - barWidth - 3, y));
                g.setPen (labelColor());
                g.drawText (QRectF (r.left() + barWidth, y - 8, scaleWidth, 16), Qt::AlignCenter, QString::number (std::abs (db), 'f', 0));
            }

            const QString names[2] { QObject::tr ("L"), QObject::tr ("R") };
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto x = ch == 0 ? r.left() : r.right() - barWidth;
                const QRectF bar (x, bars.top(), barWidth, bars.height());
                plotBackground (g, bar);

                // Peak, faint, then RMS, solid, both in the meter zones.
                const auto zoned = [&] (double db, double alpha)
                {
                    const std::pair<double, const char*> meterZones[] { { -18.0, "meterLow" }, { -6.0, "meterMid" }, { 0.0, "meterHigh" } };
                    double start = floor;
                    for (const auto& [end, key] : meterZones)
                    {
                        const auto topDb = std::min (db, end);
                        if (topDb > start)
                            g.fillRect (QRectF (bar.left() + 2, yFor (topDb), bar.width() - 4, yFor (start) - yFor (topDb)), faded (color (key), alpha));
                        start = end;
                    }
                };
                zoned (state.peakDb[(size_t) ch], 0.38);
                zoned (state.rmsDb[(size_t) ch], 0.95);

                if (state.holdDb[(size_t) ch] > floor)
                {
                    g.setPen (QPen (color ("meterPeakHold"), 2.0));
                    const auto y = yFor (state.holdDb[(size_t) ch]);
                    g.drawLine (QPointF (bar.left() + 2, y), QPointF (bar.right() - 2, y));
                }

                // Clip light with the count, and the highest true peak under it.
                const auto clips = state.stats[(size_t) ch].clipped;
                const QRectF light (x, r.top(), barWidth, 14);
                g.setPen (Qt::NoPen);
                g.setBrush (clips > 0 ? color ("meterHigh") : faded (color ("meterHigh"), 0.15));
                g.drawRoundedRect (light, 3, 3);
                g.setPen (clips > 0 ? color ("highlightedText") : labelColor());
                g.setFont (smallFont (0.7));
                g.drawText (light, Qt::AlignCenter, clips > 0 ? QString::number (clips) : QObject::tr ("CLIP"));
                const auto truePeak = ampDb (state.maxTrue[(size_t) ch]);
                g.setPen (truePeak > state.targetTruePeak ? color ("meterHigh") : color ("windowText"));
                g.drawText (QRectF (x - 6, r.top() + 15, barWidth + 12, 15), Qt::AlignCenter, number (truePeak, 1));

                // Name and readouts under the bar.
                g.setPen (color ("windowText"));
                g.setFont (smallFont (0.85));
                g.drawText (QRectF (x - 6, bars.bottom() + 2, barWidth + 12, 13), Qt::AlignCenter, names[ch]);
                g.setFont (smallFont (0.7));
                g.setPen (labelColor());
                g.drawText (QRectF (x - 8, bars.bottom() + 14, barWidth + 16, 11), Qt::AlignCenter, number (state.holdDb[(size_t) ch], 1));
                g.drawText (QRectF (x - 8, bars.bottom() + 24, barWidth + 16, 11), Qt::AlignCenter, number (state.rmsDb[(size_t) ch], 1));
            }
            g.setFont (smallFont (0.65));
            g.setPen (labelColor());
            g.drawText (QRectF (r.left() + barWidth, r.top(), scaleWidth, 14), Qt::AlignCenter, QObject::tr ("clips"));
            g.drawText (QRectF (r.left() + barWidth, r.top() + 15, scaleWidth, 15), Qt::AlignCenter, QObject::tr ("dBTP"));
            g.drawText (QRectF (r.left() + barWidth, bars.bottom() + 14, scaleWidth, 11), Qt::AlignCenter, QObject::tr ("peak"));
            g.drawText (QRectF (r.left() + barWidth, bars.bottom() + 24, scaleWidth, 11), Qt::AlignCenter, QObject::tr ("RMS"));
        }
    };

    //==============================================================================
    // The spectrum, as Audition's Frequency Analysis has it, with smoothing, slope, peak hold and holds.
    class SpectrumMeter final : public Meter
    {
    public:
        enum class Style { line, filled, bars };
        enum class Channels { stereo, midSide, average, left, right, mid, side };

        explicit SpectrumMeter (State& s, QWidget* parent) : Meter (s, parent)
        {
            setMouseTracking (true);
            setCursor (Qt::CrossCursor);
        }

        Style style = Style::filled;
        Channels channels = Channels::stereo;
        double smoothing = 1.0 / 6.0;   // octaves, 0 for none
        double slope = 3.0;             // dB per octave around 1 kHz
        double range = 96.0;            // dB shown below 0
        bool peakHold = true;

        QSize sizeHint() const override { return { 640, 280 }; }
        QSize minimumSizeHint() const override { return { 220, 120 }; }

        void clear() override
        {
            for (auto& p : peaks)
                p.clear();
            update();
        }

        void step (double dt) override
        {
            const auto plot = plotArea();
            const auto list = curves();
            const int columns = std::max (1, (int) plot.width());
            for (size_t i = 0; i < list.size(); ++i)
            {
                auto& p = peaks[i];
                if ((int) p.size() != columns)
                    p.assign ((size_t) columns, (float) minusInfinity);
                for (int x = 0; x < columns; ++x)
                {
                    const auto v = (float) level (list[i].channel, frequencyAt (plot, plot.left() + x));
                    p[(size_t) x] = std::max (v, p[(size_t) x] - (float) (12.0 * dt));
                }
            }
        }

        // The first curve as it is now, for a hold.
        std::vector<float> snapshot() const
        {
            std::vector<float> values ((size_t) holdPoints);
            const auto channel = curves().front().channel;
            for (int i = 0; i < holdPoints; ++i)
                values[(size_t) i] = (float) rawLevel (channel, holdFrequency (i));
            return values;
        }

    protected:
        struct Curve { State::Channel channel; QColor colour; QString name; };

        std::vector<Curve> curves() const
        {
            switch (channels)
            {
                case Channels::stereo:  return { { State::left, color ("highlight"), QObject::tr ("L") }, { State::right, color ("accent"), QObject::tr ("R") } };
                case Channels::midSide: return { { State::mid, color ("meterLow"), QObject::tr ("M") }, { State::side, color ("region"), QObject::tr ("S") } };
                case Channels::average: return { { State::average, color ("scope"), QObject::tr ("L+R") } };
                case Channels::left:    return { { State::left, color ("highlight"), QObject::tr ("L") } };
                case Channels::right:   return { { State::right, color ("accent"), QObject::tr ("R") } };
                case Channels::mid:     return { { State::mid, color ("meterLow"), QObject::tr ("M") } };
                case Channels::side:    return { { State::side, color ("region"), QObject::tr ("S") } };
            }
            return {};
        }

        QRectF plotArea() const { return QRectF (rect()).adjusted (6, 6, -34, -18); }
        double topHz() const { return std::min (highestHz, state.nyquist()); }
        double frequencyAt (const QRectF& plot, double x) const
        {
            return lowestHz * std::pow (topHz() / lowestHz, std::clamp ((x - plot.left()) / plot.width(), 0.0, 1.0));
        }
        double xFor (const QRectF& plot, double hz) const
        {
            return plot.left() + plot.width() * std::log (hz / lowestHz) / std::log (topHz() / lowestHz);
        }
        double yFor (const QRectF& plot, double db) const
        {
            return plot.top() + plot.height() * std::clamp (-db / range, 0.0, 1.0);
        }
        double tilt (double hz) const { return slope * std::log2 (hz / 1000.0); }
        double rawLevel (State::Channel c, double hz) const { return state.levelAt (c, hz, smoothing); }
        double level (State::Channel c, double hz) const { return rawLevel (c, hz) + tilt (hz); }

        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            const auto plot = plotArea();
            plotBackground (g, plot);
            drawGrid (g, plot);

            g.save();
            g.setClipRect (plot);

            // Holds, behind the live curves.
            for (int h = 0; h < numHolds; ++h)
                if (const auto& values = state.holds[(size_t) h]; ! values.empty())
                {
                    QPolygonF line;
                    for (int i = 0; i < holdPoints; ++i)
                        if (const auto hz = holdFrequency (i); hz <= topHz())
                            line << QPointF (xFor (plot, hz), yFor (plot, values[(size_t) i] + tilt (hz)));
                    g.setPen (QPen (faded (holdColors[(size_t) h], 0.9), 1.3));
                    g.drawPolyline (line);
                }

            const auto list = curves();
            const int columns = std::max (1, (int) plot.width());
            for (size_t i = list.size(); i-- > 0;)
            {
                const auto& curve = list[i];
                QPolygonF line;
                line.reserve (columns);
                for (int x = 0; x < columns; ++x)
                    line << QPointF (plot.left() + x, yFor (plot, level (curve.channel, frequencyAt (plot, plot.left() + x))));

                if (style == Style::bars && i == 0)
                    drawBars (g, plot, curve);
                else if (style == Style::filled || (style == Style::line && i == 0))
                {
                    QPainterPath fill;
                    fill.moveTo (plot.left(), plot.bottom());
                    for (const auto& p : line)
                        fill.lineTo (p);
                    fill.lineTo (plot.right(), plot.bottom());
                    fill.closeSubpath();
                    QLinearGradient gradient (0, plot.top(), 0, plot.bottom());
                    const auto strength = style == Style::filled ? 0.42 : 0.16;
                    gradient.setColorAt (0.0, faded (curve.colour, strength));
                    gradient.setColorAt (1.0, faded (curve.colour, 0.02));
                    g.fillPath (fill, gradient);
                }
                if (style != Style::bars || i > 0)
                {
                    g.setPen (QPen (curve.colour, 1.6));
                    g.drawPolyline (line);
                }

                if (peakHold && i < peaks.size() && (int) peaks[i].size() == columns)
                {
                    QPolygonF peakLine;
                    for (int x = 0; x < columns; ++x)
                        peakLine << QPointF (plot.left() + x, yFor (plot, peaks[i][(size_t) x]));
                    g.setPen (QPen (faded (curve.colour.lighter (140), 0.55), 1.0));
                    g.drawPolyline (peakLine);
                }
            }
            g.restore();

            drawLegend (g, plot, list);
            if (mouseX >= plot.left() && mouseX <= plot.right())
                drawReadout (g, plot, list);
        }

        void drawGrid (QPainter& g, const QRectF& plot) const
        {
            g.setFont (smallFont (0.72));
            for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
                for (int m = 1; m < 10; ++m)
                {
                    const auto hz = decade * m;
                    if (hz < lowestHz || hz > topHz())
                        continue;
                    const bool major = m == 1 || m == 2 || m == 5;
                    const auto x = xFor (plot, hz);
                    g.setPen (QPen (gridColor (major ? 0.55 : 0.22), 1.0));
                    g.drawLine (QPointF (x, plot.top()), QPointF (x, plot.bottom()));
                    if (major)
                    {
                        g.setPen (labelColor());
                        g.drawText (QRectF (x - 20, plot.bottom() + 2, 40, 14), Qt::AlignCenter, shortHz (hz));
                    }
                }
            const double step = range > 100.0 ? 20.0 : range > 60.0 ? 12.0 : 6.0;
            for (double db = 0.0; db >= -range; db -= step)
            {
                const auto y = yFor (plot, db);
                g.setPen (QPen (gridColor (0.4), 1.0, Qt::DotLine));
                g.drawLine (QPointF (plot.left(), y), QPointF (plot.right(), y));
                g.setPen (labelColor());
                g.drawText (QRectF (plot.right() + 3, y - 8, 30, 16), Qt::AlignLeft | Qt::AlignVCenter, QString::number (db, 'f', 0));
            }
        }

        // Sixth-octave bars, as in Audition's Bars display.
        void drawBars (QPainter& g, const QRectF& plot, const Curve& curve) const
        {
            const auto step = std::pow (2.0, 1.0 / 6.0);
            for (double hz = lowestHz * std::sqrt (step); hz < topHz(); hz *= step)
            {
                const auto x0 = xFor (plot, hz / std::sqrt (step)) + 1.0, x1 = xFor (plot, std::min (topHz(), hz * std::sqrt (step))) - 1.0;
                const auto db = state.levelAt (curve.channel, hz, 1.0 / 6.0) + tilt (hz);
                const auto y = yFor (plot, db);
                QLinearGradient gradient (0, y, 0, plot.bottom());
                gradient.setColorAt (0.0, faded (curve.colour, 0.9));
                gradient.setColorAt (1.0, faded (curve.colour, 0.25));
                g.fillRect (QRectF (x0, y, std::max (1.0, x1 - x0), plot.bottom() - y), gradient);
                g.fillRect (QRectF (x0, y, std::max (1.0, x1 - x0), 2.0), curve.colour.lighter (130));
            }
        }

        void drawLegend (QPainter& g, const QRectF& plot, const std::vector<Curve>& list) const
        {
            g.setFont (smallFont (0.75));
            auto x = plot.left() + 8;
            for (const auto& c : list)
            {
                g.setPen (Qt::NoPen);
                g.setBrush (c.colour);
                g.drawRoundedRect (QRectF (x, plot.top() + 8, 10, 4), 2, 2);
                g.setPen (labelColor());
                g.drawText (QRectF (x + 13, plot.top() + 3, 40, 14), Qt::AlignLeft | Qt::AlignVCenter, c.name);
                x += 18 + g.fontMetrics().horizontalAdvance (c.name);
            }
            if (slope != 0.0)
            {
                g.setPen (labelColor());
                g.drawText (QRectF (x + 4, plot.top() + 3, 160, 14), Qt::AlignLeft | Qt::AlignVCenter,
                            QObject::tr ("slope %1 dB/oct").arg (signedNumber (slope, 1)));
            }
        }

        void drawReadout (QPainter& g, const QRectF& plot, const std::vector<Curve>& list) const
        {
            const auto hz = frequencyAt (plot, mouseX);
            g.setPen (QPen (faded (color ("windowText"), 0.5), 1.0, Qt::DashLine));
            g.drawLine (QPointF (mouseX, plot.top()), QPointF (mouseX, plot.bottom()));

            QStringList lines { hzText (hz) + QStringLiteral ("   ") + noteText (hz) };
            for (const auto& c : list)
                lines << QStringLiteral ("%1  %2 dB").arg (c.name, number (rawLevel (c.channel, hz), 1));
            g.setFont (smallFont (0.8));
            const auto fm = g.fontMetrics();
            double w = 0.0;
            for (const auto& l : lines)
                w = std::max (w, (double) fm.horizontalAdvance (l));
            const auto h = lines.size() * (fm.height() + 1.0) + 8.0;
            auto box = QRectF (mouseX + 10, plot.top() + 22, w + 14, h);
            if (box.right() > plot.right())
                box.moveRight (mouseX - 10);
            g.setPen (QPen (gridColor (0.8), 1.0));
            g.setBrush (faded (color ("base"), 0.88));
            g.drawRoundedRect (box, 4, 4);
            for (int i = 0; i < lines.size(); ++i)
            {
                g.setPen (i == 0 ? color ("windowText") : list[(size_t) i - 1].colour);
                g.drawText (QRectF (box.left() + 7, box.top() + 4 + i * (fm.height() + 1.0), w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
            }
        }

        void mouseMoveEvent (QMouseEvent* e) override
        {
            mouseX = e->position().x();
            update();
        }

        void leaveEvent (QEvent*) override
        {
            mouseX = -1.0;
            update();
        }

    private:
        std::array<std::vector<float>, 2> peaks;
        double mouseX = -1.0;
    };

    //==============================================================================
    // A scrolling spectrogram, low frequencies at the bottom, in the theme's spectrogram colours.
    class SpectrogramMeter final : public Meter
    {
    public:
        using Meter::Meter;
        QSize sizeHint() const override { return { 360, 260 }; }
        QSize minimumSizeHint() const override { return { 120, 100 }; }

        void clear() override
        {
            image.fill (Qt::transparent);
            update();
        }

        void step (double) override
        {
            const auto plot = plotArea();
            const QSize size ((int) plot.width(), (int) plot.height());
            if (size.width() < 2 || size.height() < 2)
                return;
            if (image.size() != size)
            {
                image = QImage (size, QImage::Format_ARGB32_Premultiplied);
                image.fill (Qt::transparent);
                column = 0;
            }
            buildColours();
            const auto top = std::min (highestHz, state.nyquist());
            column = (column + 1) % size.width();
            for (int y = 0; y < size.height(); ++y)
            {
                const auto hz = lowestHz * std::pow (top / lowestHz, 1.0 - (y + 0.5) / size.height());
                const auto db = state.levelAt (State::average, hz, 1.0 / 12.0) + 3.0 * std::log2 (hz / 1000.0);
                const auto index = std::clamp ((int) std::lround ((db + 90.0) / 78.0 * 255.0), 0, 255);   // -90 to -12 dB
                reinterpret_cast<QRgb*> (image.scanLine (y))[column] = colours[(size_t) index];
            }
        }

    protected:
        QRectF plotArea() const { return QRectF (rect()).adjusted (6, 6, -32, -6); }

        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            const auto plot = plotArea();
            g.setRenderHint (QPainter::Antialiasing);
            plotBackground (g, plot);
            if (! image.isNull())
            {
                // Oldest on the left: the part after the newest column, then the part up to it.
                const int w = image.width(), after = w - column - 1;
                g.drawImage (QRectF (plot.left(), plot.top(), after, image.height()), image, QRectF (column + 1, 0, after, image.height()));
                g.drawImage (QRectF (plot.left() + after, plot.top(), column + 1, image.height()), image, QRectF (0, 0, column + 1, image.height()));
            }
            const auto top = std::min (highestHz, state.nyquist());
            g.setFont (smallFont (0.72));
            for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
            {
                if (hz > top)
                    continue;
                const auto y = plot.top() + plot.height() * (1.0 - std::log (hz / lowestHz) / std::log (top / lowestHz));
                g.setPen (QPen (faded (color ("windowText"), 0.18), 1.0, Qt::DotLine));
                g.drawLine (QPointF (plot.left(), y), QPointF (plot.right(), y));
                g.setPen (labelColor());
                g.drawText (QRectF (plot.right() + 3, y - 8, 30, 16), Qt::AlignLeft | Qt::AlignVCenter, shortHz (hz));
            }
        }

    private:
        void buildColours()
        {
            const auto& stops = Theme::current().spectrogram;
            if (stops.size() < 2)
                return;
            for (int i = 0; i < 256; ++i)
            {
                const auto t = i / 255.0 * (stops.size() - 1);
                const auto k = std::min ((int) t, (int) stops.size() - 2);
                auto c = mix (stops[k], stops[k + 1], t - k);
                c.setAlphaF ((float) std::clamp (i / 60.0, 0.0, 1.0));   // the quietest fade into the window
                colours[(size_t) i] = qPremultiply (c.rgba());
            }
        }

        QImage image;
        int column = 0;
        std::array<QRgb, 256> colours {};
    };

    //==============================================================================
    // Phase Wheel, like Audition's: each frequency as a dot, mono at the top, inverse at
    // the bottom, left and right at the sides; low frequencies near the middle.
    class PhaseWheelMeter final : public Meter
    {
    public:
        using Meter::Meter;
        QSize sizeHint() const override { return { 300, 260 }; }
        QSize minimumSizeHint() const override { return { 120, 100 }; }

        void clear() override
        {
            trace.fill (Qt::transparent);
            update();
        }

        void step (double) override
        {
            const auto plot = plotArea();
            const QSize size ((int) plot.width(), (int) plot.height());
            const auto ratio = devicePixelRatioF();
            if (size.isEmpty())
                return;
            if (trace.size() != size * ratio)
            {
                trace = QImage (size * ratio, QImage::Format_ARGB32_Premultiplied);
                trace.setDevicePixelRatio (ratio);
                trace.fill (Qt::transparent);
            }
            QPainter g (&trace);
            g.setCompositionMode (QPainter::CompositionMode_DestinationIn);
            g.fillRect (QRectF (QPointF(), QSizeF (size)), QColor (0, 0, 0, 200));   // fade like a phosphor screen
            g.setCompositionMode (QPainter::CompositionMode_SourceOver);
            g.setRenderHint (QPainter::Antialiasing);
            g.setPen (Qt::NoPen);
            const QPointF c (size.width() / 2.0, size.height() / 2.0);
            const auto rx = size.width() / 2.0 - 6.0, ry = size.height() / 2.0 - 6.0;
            for (const auto& p : state.wheel)
            {
                const auto strength = std::clamp ((p.levelDb + 84.0) / 60.0, 0.08, 1.0);
                const auto radius = 0.08 + 0.92 * std::clamp (p.radius, 0.0, 1.0);
                const QPointF at (c.x() + std::sin (p.angle) * radius * rx, c.y() - std::cos (p.angle) * radius * ry);
                auto dot = QColor::fromHsvF ((float) (0.62 - 0.62 * std::clamp (p.radius, 0.0, 1.0)), 0.7f, 1.0f);
                dot.setAlphaF ((float) strength);
                g.setBrush (dot);
                const auto d = 1.5 + 2.5 * strength;
                g.drawEllipse (at, d, d);
            }
        }

    protected:
        QRectF plotArea() const { return QRectF (rect()).adjusted (6, 6, -6, -6); }

        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            const auto plot = plotArea();
            plotBackground (g, plot);
            const auto c = plot.center();
            const auto rx = plot.width() / 2.0 - 6.0, ry = plot.height() / 2.0 - 6.0;

            g.setPen (QPen (gridColor (0.35), 1.0));
            g.drawLine (QPointF (plot.left() + 6, c.y()), QPointF (plot.right() - 6, c.y()));
            g.drawLine (QPointF (c.x(), plot.top() + 6), QPointF (c.x(), plot.bottom() - 6));
            g.setFont (smallFont (0.68));
            for (double hz : { 100.0, 1000.0, 10000.0 })
            {
                const auto f = 0.08 + 0.92 * std::log (hz / lowestHz) / std::log (highestHz / lowestHz);
                g.setPen (QPen (gridColor (0.3), 1.0, Qt::DotLine));
                g.setBrush (Qt::NoBrush);
                g.drawEllipse (c, rx * f, ry * f);
                g.setPen (labelColor());
                g.drawText (QRectF (c.x() + 3, c.y() + ry * f - 13, 40, 12), Qt::AlignLeft | Qt::AlignBottom, shortHz (hz));
            }
            g.setPen (QPen (faded (color ("meterHigh"), 0.55), 1.2));
            g.drawEllipse (c, rx, ry);

            if (! trace.isNull())
                g.drawImage (plot.topLeft(), trace);

            g.setFont (smallFont (0.72));
            g.setPen (faded (color ("meterLow"), 0.95));
            g.drawText (QRectF (plot.left(), plot.top() + 2, plot.width(), 14), Qt::AlignHCenter | Qt::AlignTop, QObject::tr ("Mono"));
            g.setPen (faded (color ("meterHigh"), 0.95));
            g.drawText (QRectF (plot.left(), plot.bottom() - 16, plot.width(), 14), Qt::AlignHCenter | Qt::AlignBottom, QObject::tr ("Inverse"));
            g.setPen (labelColor());
            g.drawText (QRectF (plot.left() + 6, c.y() - 16, 40, 14), Qt::AlignLeft | Qt::AlignBottom, QObject::tr ("Left"));
            g.drawText (QRectF (plot.right() - 46, c.y() - 16, 40, 14), Qt::AlignRight | Qt::AlignBottom, QObject::tr ("Right"));
        }

    private:
        QImage trace;
    };

    //==============================================================================
    // Correlation per third octave, then the overall correlation and balance, like Audition's Phase Meter.
    class CorrelationMeter final : public Meter
    {
    public:
        using Meter::Meter;
        QSize sizeHint() const override { return { 320, 260 }; }
        QSize minimumSizeHint() const override { return { 160, 120 }; }

    protected:
        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            const auto r = QRectF (rect()).adjusted (6, 6, -6, -6);
            const QRectF bands (r.left() + 26, r.top(), r.width() - 26, std::max (40.0, r.height() - 96));
            const QRectF overall (r.left(), bands.bottom() + 32, r.width(), 20);
            const QRectF balance (r.left(), overall.bottom() + 20, r.width(), 16);

            // Bands, from the zero line up (in phase) or down (out of phase).
            plotBackground (g, bands);
            const auto yFor = [&] (double v) { return bands.center().y() - v * (bands.height() / 2.0 - 4.0); };
            g.setFont (smallFont (0.7));
            for (double v : { 1.0, 0.5, 0.0, -0.5, -1.0 })
            {
                g.setPen (QPen (gridColor (v == 0.0 ? 0.8 : 0.3), 1.0, v == 0.0 ? Qt::SolidLine : Qt::DotLine));
                g.drawLine (QPointF (bands.left(), yFor (v)), QPointF (bands.right(), yFor (v)));
                g.setPen (labelColor());
                g.drawText (QRectF (r.left(), yFor (v) - 7, 23, 14), Qt::AlignRight | Qt::AlignVCenter, v == 0.0 ? QStringLiteral ("0") : signedNumber (v, 1));
            }
            const auto width = bands.width() / numBands;
            for (int b = 0; b < numBands; ++b)
            {
                const auto x = bands.left() + b * width;
                const auto active = state.bandLevel[(size_t) b] > -80.0;
                const auto v = active ? state.bandCorrelation[(size_t) b] : 0.0;
                const auto colour = faded (correlationColor (v), active ? 0.9 : 0.15);
                const auto y0 = yFor (0.0), y1 = yFor (v);
                g.fillRect (QRectF (x + 1, std::min (y0, y1), width - 2, std::max (1.5, std::abs (y1 - y0))), colour);
                if (b % 3 == 1)
                {
                    g.setPen (labelColor());
                    g.drawText (QRectF (x - 12, bands.bottom() + 2, width + 24, 12), Qt::AlignCenter, QLatin1String (bandNames[b]));
                }
            }

            // Overall correlation: the gradient from -1 to +1 with a marker.
            const QRectF bar (overall.left(), overall.top(), overall.width() - 46, overall.height());
            QLinearGradient gradient (bar.left(), 0, bar.right(), 0);
            gradient.setColorAt (0.0, faded (color ("meterHigh"), 0.5));
            gradient.setColorAt (0.5, faded (color ("meterMid"), 0.5));
            gradient.setColorAt (1.0, faded (color ("meterLow"), 0.5));
            g.setPen (Qt::NoPen);
            g.setBrush (gradient);
            g.drawRoundedRect (bar, 3, 3);
            g.setPen (labelColor());
            for (double v : { -1.0, -0.5, 0.0, 0.5, 1.0 })
            {
                const auto x = bar.left() + bar.width() * (v + 1.0) / 2.0;
                g.drawText (QRectF (x - 15, bar.top() - 14, 30, 12), Qt::AlignCenter, v == 0.0 ? QStringLiteral ("0") : signedNumber (v, 1));
            }
            if (state.hasSignal)
            {
                const auto x = bar.left() + bar.width() * (state.correlation + 1.0) / 2.0;
                g.setPen (QPen (color ("windowText"), 1.0));
                g.setBrush (correlationColor (state.correlation));
                g.drawRoundedRect (QRectF (x - 4, bar.top() - 2, 8, bar.height() + 4), 2, 2);
            }
            auto valueFont = font();
            valueFont.setWeight (QFont::DemiBold);
            g.setFont (valueFont);
            g.setPen (state.hasSignal ? correlationColor (state.correlation) : labelColor());
            g.drawText (QRectF (bar.right() + 4, bar.top(), 42, bar.height()), Qt::AlignRight | Qt::AlignVCenter,
                        state.hasSignal ? QString::number (state.correlation, 'f', 2) : QStringLiteral ("-"));

            // Balance: left or right louder, in dB.
            const QRectF track (balance.left() + 14, balance.center().y() - 3, balance.width() - 14 - 60, 6);
            g.setPen (Qt::NoPen);
            g.setBrush (Theme::withOpacity (color ("meterBackground")));
            g.drawRoundedRect (track, 3, 3);
            g.setFont (smallFont (0.75));
            g.setPen (labelColor());
            g.drawText (QRectF (balance.left(), balance.top(), 12, balance.height()), Qt::AlignCenter, QObject::tr ("L"));
            g.drawText (QRectF (track.right() + 2, balance.top(), 12, balance.height()), Qt::AlignCenter, QObject::tr ("R"));
            g.setPen (QPen (gridColor (0.8), 1.0));
            g.drawLine (QPointF (track.center().x(), track.top() - 3), QPointF (track.center().x(), track.bottom() + 3));
            if (state.hasSignal)
            {
                // +-12 dB end to end, on a curve that makes small differences visible.
                const auto t = std::tanh (state.balanceDb / 6.0);
                const auto x = track.center().x() + t * track.width() / 2.0;
                g.setPen (Qt::NoPen);
                g.setBrush (color ("highlight"));
                g.drawRect (QRectF (std::min (x, track.center().x()), track.top(), std::abs (x - track.center().x()), track.height()));
                g.drawEllipse (QPointF (x, track.center().y()), 5, 5);
            }
            g.setPen (color ("windowText"));
            const auto balanceText = ! state.hasSignal ? QStringLiteral ("-")
                                     : std::abs (state.balanceDb) < 0.05 ? QObject::tr ("centre")
                                     : QStringLiteral ("%1 %2 dB").arg (state.balanceDb > 0.0 ? QObject::tr ("R") : QObject::tr ("L"))
                                           .arg (std::abs (state.balanceDb), 0, 'f', 1);
            g.drawText (QRectF (track.right() + 14, balance.top(), 46, balance.height()), Qt::AlignRight | Qt::AlignVCenter, balanceText);
        }
    };

    //==============================================================================
    // Amplitude Statistics, as in Audition, with the RMS histogram below.
    class StatsMeter final : public Meter
    {
    public:
        using Meter::Meter;
        QSize sizeHint() const override { return { 360, 300 }; }
        QSize minimumSizeHint() const override { return { 220, 160 }; }

    protected:
        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            const auto r = QRectF (rect()).adjusted (8, 4, -8, -6);
            const auto rows = statRows (state);
            const auto rowHeight = std::clamp ((r.height() - 70.0) / (rows.size() + 1.0), 12.0, 19.0);
            const auto valueWidth = std::min (96.0, r.width() * 0.26);
            const auto nameWidth = r.width() - valueWidth * 2;

            auto nameFont = smallFont (0.8), valueFont = smallFont (0.85);
            g.setFont (nameFont);
            g.setPen (labelColor());
            g.drawText (QRectF (r.left() + nameWidth, r.top(), valueWidth, rowHeight), Qt::AlignRight | Qt::AlignVCenter, QObject::tr ("Left"));
            g.drawText (QRectF (r.left() + nameWidth + valueWidth, r.top(), valueWidth, rowHeight), Qt::AlignRight | Qt::AlignVCenter, QObject::tr ("Right"));
            auto y = r.top() + rowHeight;
            for (size_t i = 0; i < rows.size(); ++i, y += rowHeight)
            {
                if (i % 2 == 0)
                    g.fillRect (QRectF (r.left(), y, r.width(), rowHeight), faded (color ("windowText"), 0.035));
                g.setFont (nameFont);
                g.setPen (labelColor());
                g.drawText (QRectF (r.left() + 4, y, nameWidth - 8, rowHeight), Qt::AlignLeft | Qt::AlignVCenter, rows[i].name);
                g.setFont (valueFont);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const bool warn = i == 4 && rows[i].values[ch] != QStringLiteral ("0") && rows[i].values[ch] != QStringLiteral ("-");
                    g.setPen (warn ? color ("meterHigh") : color ("windowText"));
                    g.drawText (QRectF (r.left() + nameWidth + ch * valueWidth, y, valueWidth, rowHeight), Qt::AlignRight | Qt::AlignVCenter, rows[i].values[ch]);
                }
            }

            // RMS histogram: how much of the time each level was heard.
            const QRectF hist (r.left(), y + 16, r.width(), r.bottom() - y - 30);
            g.setFont (smallFont (0.72));
            g.setPen (labelColor());
            g.drawText (QRectF (r.left(), y + 2, r.width(), 14), Qt::AlignLeft | Qt::AlignVCenter, QObject::tr ("RMS histogram (50 ms windows)"));
            if (hist.height() < 24)
                return;
            plotBackground (g, hist);
            int most = 1;
            for (const auto& s : state.stats)
                most = std::max (most, *std::max_element (s.histogram.begin(), s.histogram.end()));
            const auto binWidth = hist.width() / 61.0;
            const QColor colours[2] { color ("highlight"), color ("accent") };
            for (int ch = 0; ch < 2; ++ch)
                for (int b = 0; b < 61; ++b)
                {
                    const auto count = state.stats[(size_t) ch].histogram[(size_t) b];
                    if (count == 0)
                        continue;
                    const auto h = (hist.height() - 4) * std::sqrt ((double) count / most);
                    const auto x = hist.left() + b * binWidth + (ch == 0 ? 0.0 : binWidth / 2.0);
                    g.fillRect (QRectF (x, hist.bottom() - 2 - h, binWidth / 2.0, h), faded (colours[ch], 0.8));
                }
            g.setPen (labelColor());
            for (int db = -60; db <= 0; db += 12)
                g.drawText (QRectF (hist.left() + (db + 60) * binWidth - 14, hist.bottom() + 1, 28 + binWidth, 12), Qt::AlignCenter, QString::number (db));
        }
    };

    //==============================================================================
    // A meter in a rounded panel with a title row for its own controls.
    class Card final : public QFrame
    {
    public:
        Card (const QString& cardId, const QString& title, QWidget* body, QWidget* parent) : QFrame (parent), id (cardId), content (body)
        {
            setObjectName (cardId);
            auto* column = new QVBoxLayout (this);
            column->setContentsMargins (8, 6, 8, 8);
            column->setSpacing (4);
            header = new QHBoxLayout;
            header->setSpacing (4);
            auto* label = new QLabel (title.toUpper(), this);
            auto f = label->font();
            f.setPointSizeF (f.pointSizeF() * 0.82);
            f.setWeight (QFont::DemiBold);
            f.setLetterSpacing (QFont::PercentageSpacing, 112);
            label->setFont (f);
            label->setForegroundRole (QPalette::PlaceholderText);
            header->addWidget (label);
            header->addStretch (1);
            column->addLayout (header);
            column->addWidget (body, 1);
            body->setParent (this);
            setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
        }

        void addControl (QWidget* w)
        {
            w->setFocusPolicy (Qt::NoFocus);
            header->addWidget (w);
        }

        const QString id;
        QWidget* const content;

    protected:
        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            g.setPen (QPen (faded (color ("mid"), 0.7), 1.0));
            g.setBrush (Theme::withOpacity (color ("alternateBase")));
            g.drawRoundedRect (QRectF (rect()).adjusted (0.5, 0.5, -0.5, -0.5), 7, 7);
            // A thin accent along the top, like a lit panel edge.
            QLinearGradient edge (0, 0, width(), 0);
            edge.setColorAt (0.0, faded (color ("highlight"), 0.0));
            edge.setColorAt (0.5, faded (color ("highlight"), 0.55));
            edge.setColorAt (1.0, faded (color ("highlight"), 0.0));
            g.setPen (QPen (QBrush (edge), 1.2));
            g.drawLine (QPointF (14, 0.8), QPointF (width() - 14, 0.8));
        }

    private:
        QHBoxLayout* header;
    };

    QComboBox* smallCombo (QWidget* parent, const QStringList& items, int current, const QString& tip)
    {
        auto* box = new QComboBox (parent);
        box->addItems (items);
        box->setCurrentIndex (std::clamp (current, 0, (int) items.size() - 1));
        box->setToolTip (tip);
        box->setAccessibleName (tip);
        box->setFocusPolicy (Qt::NoFocus);
        box->setSizeAdjustPolicy (QComboBox::AdjustToContents);
        return box;
    }
}

using namespace analysis;

//==============================================================================
AnalysisWindow::AnalysisWindow (QWidget* parent) : QWidget (parent, Qt::Window), state (std::make_unique<State>())
{
    setWindowTitle (tr ("Analysis Meters"));
    setObjectName (QStringLiteral ("AnalysisWindow"));
    setFocusPolicy (Qt::StrongFocus);
    Theme::makeTranslucent (this);   // the same theme and window opacity as the rest of PHINYX
    auto settings = appSettings();

    // The bar along the top: target, reset, freeze, which meters, and what is measured.
    auto* title = new QLabel (tr ("ANALYSIS"), this);
    {
        auto f = title->font();
        f.setPointSizeF (f.pointSizeF() * 1.15);
        f.setWeight (QFont::Bold);
        f.setLetterSpacing (QFont::PercentageSpacing, 125);
        title->setFont (f);
    }

    QStringList targetNames;
    for (const auto& t : targets)
        targetNames << tr (t.name);
    targetBox = smallCombo (this, targetNames, settings.value (QStringLiteral ("analysis/target"), 0).toInt(), tr ("Loudness target"));
    const auto applyTarget = [this] (int index)
    {
        const auto& t = targets[(size_t) std::clamp (index, 0, (int) targets.size() - 1)];
        state->targetLufs = t.lufs;
        state->targetTruePeak = t.truePeak;
        appSettings().setValue (QStringLiteral ("analysis/target"), index);
        update();
        for (auto* c : cards)
            c->content->update();
    };
    connect (targetBox, &QComboBox::currentIndexChanged, this, applyTarget);

    auto button = [this] (const QString& text, const QString& tip)
    {
        auto* b = new QPushButton (text, this);
        b->setFocusPolicy (Qt::NoFocus);
        b->setToolTip (tip);
        return b;
    };
    auto* resetButton = button (tr ("Reset"), tr ("Start measuring again (R)"));
    connect (resetButton, &QPushButton::clicked, this, &AnalysisWindow::reset);
    freezeButton = button (tr ("Freeze"), tr ("Hold every meter as it is (F)"));
    freezeButton->setCheckable (true);
    connect (freezeButton, &QPushButton::toggled, this, &AnalysisWindow::setFrozen);
    auto* metersButton = button (tr ("Meters"), tr ("Which meters are shown"));
    metersMenu = new QMenu (metersButton);
    metersButton->setMenu (metersMenu);
    status = new QLabel (this);
    status->setForegroundRole (QPalette::PlaceholderText);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins (4, 0, 4, 0);
    bar->setSpacing (8);
    bar->addWidget (title);
    bar->addSpacing (12);
    auto* targetLabel = new QLabel (tr ("&Target:"), this);
    targetLabel->setBuddy (targetBox);
    bar->addWidget (targetLabel);
    bar->addWidget (targetBox);
    bar->addStretch (1);
    bar->addWidget (status);
    bar->addSpacing (8);
    bar->addWidget (resetButton);
    bar->addWidget (freezeButton);
    bar->addWidget (metersButton);

    // The meters.
    auto* loudness = new LoudnessMeter (*state, this);
    auto* history = new HistoryMeter (*state, this);
    auto* levels = new LevelsMeter (*state, this);
    auto* spectrumMeter = new SpectrumMeter (*state, this);
    spectrum = spectrumMeter;
    auto* spectrogram = new SpectrogramMeter (*state, this);
    imaging = new Goniometer (this);
    auto* wheel = new PhaseWheelMeter (*state, this);
    auto* correlation = new CorrelationMeter (*state, this);
    auto* stats = new StatsMeter (*state, this);
    ambisonics = new AmbiVisualizer (this);

    const auto hidden = settings.value (QStringLiteral ("analysis/hidden")).toStringList();
    const auto addCard = [this, &hidden] (const char* id, const QString& name, QWidget* body)
    {
        auto* card = new Card (QString::fromLatin1 (id), name, body, this);
        cards.push_back (card);
        auto* action = metersMenu->addAction (name);
        action->setCheckable (true);
        action->setChecked (! hidden.contains (card->id));
        card->setVisible (action->isChecked());
        connect (action, &QAction::toggled, this, [this, card] (bool on)
        {
            card->setVisible (on);
            for (auto* row : splitters)
            {
                bool any = false;
                for (int i = 0; i < row->count(); ++i)
                    any = any || ! row->widget (i)->isHidden();
                row->setVisible (any);
            }
            saveLayout();
        });
        return card;
    };

    auto* loudnessCard = addCard ("loudness", tr ("Loudness"), loudness);
    auto* historyCard = addCard ("history", tr ("Loudness History"), history);
    auto* levelsCard = addCard ("levels", tr ("Levels"), levels);
    auto* spectrumCard = addCard ("spectrum", tr ("Spectrum"), spectrumMeter);
    auto* spectrogramCard = addCard ("spectrogram", tr ("Spectrogram"), spectrogram);
    auto* imagingCard = addCard ("imaging", tr ("Imaging"), imaging);
    auto* wheelCard = addCard ("phaseWheel", tr ("Phase Wheel"), wheel);
    auto* correlationCard = addCard ("correlation", tr ("Correlation"), correlation);
    auto* statsCard = addCard ("statistics", tr ("Amplitude Statistics"), stats);
    auto* ambiCard = addCard ("ambisonics", tr ("Ambisonics"), ambisonics);

    // Loudness History: how much time it shows.
    {
        const QList<double> spans { 30.0, 60.0, 180.0, 600.0 };
        auto* span = smallCombo (historyCard, { tr ("30 s"), tr ("1 min"), tr ("3 min"), tr ("10 min") },
                                 settings.value (QStringLiteral ("analysis/historySpan"), 1).toInt(), tr ("How much time the history shows"));
        const auto apply = [history, spans] (int i)
        {
            history->spanSeconds = spans.value (i, 60.0);
            history->update();
            appSettings().setValue (QStringLiteral ("analysis/historySpan"), i);
        };
        apply (span->currentIndex());
        connect (span, &QComboBox::currentIndexChanged, history, apply);
        historyCard->addControl (span);
    }

    // Spectrum: channels, smoothing, holds 1-8, and the rest in a menu.
    {
        const auto key = [] (const char* name) { return QStringLiteral ("analysis/spectrum/%1").arg (QLatin1String (name)); };
        auto* channels = smallCombo (spectrumCard, { tr ("Left & Right"), tr ("Mid & Side"), tr ("Average"), tr ("Left"), tr ("Right"), tr ("Mid"), tr ("Side") },
                                     settings.value (key ("channels"), 0).toInt(), tr ("Which channels the spectrum shows"));
        connect (channels, &QComboBox::currentIndexChanged, this, [spectrumMeter, key] (int i)
        {
            spectrumMeter->channels = static_cast<SpectrumMeter::Channels> (i);
            spectrumMeter->clear();
            appSettings().setValue (key ("channels"), i);
        });
        spectrumMeter->channels = static_cast<SpectrumMeter::Channels> (channels->currentIndex());

        const QList<double> smoothings { 0.0, 1.0 / 24.0, 1.0 / 12.0, 1.0 / 6.0, 1.0 / 3.0, 1.0 };
        auto* smoothing = smallCombo (spectrumCard, { tr ("No smoothing"), tr ("1/24 octave"), tr ("1/12 octave"), tr ("1/6 octave"), tr ("1/3 octave"), tr ("1 octave") },
                                      settings.value (key ("smoothing"), 3).toInt(), tr ("Smoothing: the level averaged over this much around each frequency"));
        const auto applySmoothing = [spectrumMeter, smoothings, key] (int i)
        {
            spectrumMeter->smoothing = smoothings.value (i, 1.0 / 6.0);
            spectrumMeter->clear();
            appSettings().setValue (key ("smoothing"), i);
        };
        applySmoothing (smoothing->currentIndex());
        connect (smoothing, &QComboBox::currentIndexChanged, this, applySmoothing);

        spectrumCard->addControl (channels);
        spectrumCard->addControl (smoothing);

        auto* holdsLabel = new QLabel (tr ("Hold:"), spectrumCard);
        holdsLabel->setForegroundRole (QPalette::PlaceholderText);
        spectrumCard->addControl (holdsLabel);
        for (int h = 0; h < numHolds; ++h)
        {
            auto* b = new QPushButton (QString::number (h + 1), spectrumCard);
            b->setCheckable (true);
            b->setFixedSize (22, 20);
            b->setToolTip (tr ("Hold the spectrum as it is now in this colour, or clear the hold (%1)").arg (h + 1));
            b->setStyleSheet (QStringLiteral ("QPushButton { border: none; border-bottom: 3px solid %1; border-radius: 2px; padding: 0; }"
                                              "QPushButton:checked { background: %2; color: black; }")
                                  .arg (holdColors[(size_t) h].name(), holdColors[(size_t) h].name()));
            connect (b, &QPushButton::clicked, this, [this, h] { toggleHold (h); });
            holdButtons.push_back (b);
            spectrumCard->addControl (b);
        }

        auto* more = new QToolButton (spectrumCard);
        more->setText (tr ("Options"));
        more->setToolTip (tr ("Display, slope, FFT size, window, response and range"));
        more->setPopupMode (QToolButton::InstantPopup);
        auto* menu = new QMenu (more);
        more->setMenu (menu);
        const auto radio = [this, menu, &settings] (const QString& title, const QStringList& names, const QString& settingKey, int fallback,
                                                     std::function<void (int)> apply)
        {
            auto* sub = menu->addMenu (title);
            auto* group = new QActionGroup (sub);
            const auto chosen = std::clamp (settings.value (settingKey, fallback).toInt(), 0, (int) names.size() - 1);
            for (int i = 0; i < names.size(); ++i)
            {
                auto* a = sub->addAction (names[i]);
                a->setCheckable (true);
                a->setChecked (i == chosen);
                group->addAction (a);
                connect (a, &QAction::triggered, this, [apply, settingKey, i]
                {
                    apply (i);
                    appSettings().setValue (settingKey, i);
                });
            }
            apply (chosen);
        };
        radio (tr ("Display"), { tr ("Lines"), tr ("Filled"), tr ("Bars") }, key ("style"), 1,
               [spectrumMeter] (int i) { spectrumMeter->style = static_cast<SpectrumMeter::Style> (i); spectrumMeter->update(); });
        radio (tr ("Slope"), { tr ("Flat"), tr ("3 dB per octave (pink noise flat)"), tr ("4.5 dB per octave (mixes flat)") }, key ("slope"), 1,
               [spectrumMeter] (int i) { spectrumMeter->slope = i == 0 ? 0.0 : i == 1 ? 3.0 : 4.5; spectrumMeter->clear(); });
        radio (tr ("FFT Size"), { QStringLiteral ("1024"), QStringLiteral ("2048"), QStringLiteral ("4096"), QStringLiteral ("8192"),
                                  QStringLiteral ("16384"), QStringLiteral ("32768") }, key ("fftSize"), 3,
               [this, spectrumMeter] (int i) { state->setFft (10 + i, state->windowType); spectrumMeter->clear(); });
        radio (tr ("Window"), { tr ("Hann"), tr ("Blackman-Harris"), tr ("Flat Top (exact levels)"), tr ("Hamming"), tr ("Rectangular") }, key ("window"), 1,
               [this, spectrumMeter] (int i) { state->setFft (state->fftOrder, i); spectrumMeter->clear(); });
        radio (tr ("Response"), { tr ("Fast"), tr ("Medium"), tr ("Slow") }, key ("response"), 1, [this] (int i) { state->response = i; });
        radio (tr ("Range"), { tr ("60 dB"), tr ("96 dB"), tr ("120 dB") }, key ("range"), 1,
               [spectrumMeter] (int i) { spectrumMeter->range = i == 0 ? 60.0 : i == 1 ? 96.0 : 120.0; spectrumMeter->update(); });
        auto* peakHold = menu->addAction (tr ("Peak Hold Curve"));
        peakHold->setCheckable (true);
        peakHold->setChecked (settings.value (key ("peakHold"), true).toBool());
        spectrumMeter->peakHold = peakHold->isChecked();
        connect (peakHold, &QAction::toggled, this, [spectrumMeter, key] (bool on)
        {
            spectrumMeter->peakHold = on;
            spectrumMeter->clear();
            appSettings().setValue (key ("peakHold"), on);
        });
        menu->addSeparator();
        menu->addAction (tr ("Clear All Holds"), this, [this]
        {
            for (int h = 0; h < numHolds; ++h)
                if (! state->holds[(size_t) h].empty())
                    toggleHold (h);
        });
        spectrumCard->addControl (more);
    }

    // Imaging: the Goniometer names its view itself; this moves to the next one, as clicking it does.
    {
        auto* next = new QPushButton (tr ("Next View"), imagingCard);
        next->setToolTip (tr ("Goniometer, Polar Sample, Polar Level, Lissajous, Binaural, Mid/Side Levels (or click the display)"));
        connect (next, &QPushButton::clicked, imaging, [this] { imaging->nextView(); });
        imagingCard->addControl (next);
    }

    // Ambisonics: the view, and the rest in a menu.
    {
        QStringList names;
        for (int v = 0; v < AmbiVisualizer::numViews; ++v)
            names << AmbiVisualizer::viewName (static_cast<AmbiVisualizer::View> (v));
        auto* views = smallCombo (ambiCard, names, (int) ambisonics->view(),
                                  tr ("Sphere, Four Views, Panorama, Head, Energy, Scope or Speakers (V for the next)"));
        connect (views, &QComboBox::currentIndexChanged, ambisonics, [this] (int i) { ambisonics->setView (static_cast<AmbiVisualizer::View> (i)); });
        ambisonics->viewChanged = [views] (AmbiVisualizer::View v) { const QSignalBlocker quiet (views); views->setCurrentIndex ((int) v); };
        ambiCard->addControl (views);

        assumeAmbisonics = settings.value (QStringLiteral ("analysis/ambi/assume"), false).toBool();
        auto* more = new QToolButton (ambiCard);
        more->setText (tr ("Options"));
        more->setToolTip (tr ("Range, scale, response, order shown and height"));
        more->setPopupMode (QToolButton::InstantPopup);
        auto* menu = new QMenu (more);
        more->setMenu (menu);
        const auto radio = [this, menu] (const QString& heading, const QStringList& items, int chosen, std::function<void (int)> apply)
        {
            auto* sub = menu->addMenu (heading);
            auto* group = new QActionGroup (sub);
            for (int i = 0; i < items.size(); ++i)
            {
                auto* a = sub->addAction (items[i]);
                a->setCheckable (true);
                a->setChecked (i == chosen);
                group->addAction (a);
                connect (a, &QAction::triggered, this, [apply, i] { apply (i); });
            }
            return group;
        };
        const QList<double> ranges { 24.0, 36.0, 48.0, 60.0 };
        auto* rangeGroup = radio (tr ("Range"), { tr ("24 dB"), tr ("36 dB"), tr ("48 dB"), tr ("60 dB") },
                                  (int) ranges.indexOf (ambisonics->range()), [this, ranges] (int i) { ambisonics->setRange (ranges.value (i, 24.0)); });
        radio (tr ("Scale"), { tr ("Relative to the Loudest"), tr ("Absolute (0 dB: full-scale plane wave)") }, ambisonics->relative() ? 0 : 1,
               [this] (int i) { ambisonics->setRelative (i == 0); });
        radio (tr ("Response"), { tr ("Fast"), tr ("Medium"), tr ("Slow") }, ambisonics->response(), [this] (int i) { ambisonics->setResponse (i); });
        QStringList orders;
        for (int o = 1; o <= AmbiVisualizer::maxOrder; ++o)
            orders << (o == AmbiVisualizer::maxOrder ? tr ("All of the File's (up to 10th)") : tr ("Up to Order %1").arg (o));
        radio (tr ("Order Shown"), orders, ambisonics->orderLimit() - 1, [this] (int i) { ambisonics->setOrderLimit (i + 1); });
        auto* height = menu->addAction (tr ("Height in Scope"));
        height->setCheckable (true);
        height->setChecked (ambisonics->height());
        connect (height, &QAction::toggled, ambisonics, [this] (bool on) { ambisonics->setHeight (on); });
        auto* assume = menu->addAction (tr ("Unmarked 4, 9 and 16 Channels Are Ambisonics"));
        assume->setCheckable (true);
        assume->setChecked (assumeAmbisonics);
        connect (assume, &QAction::toggled, this, [this] (bool on)
        {
            assumeAmbisonics = on;
            appSettings().setValue (QStringLiteral ("analysis/ambi/assume"), on);
        });
        menu->addSeparator();
        menu->addAction (tr ("Reset the View (Home)"), ambisonics, [this] { ambisonics->resetCamera(); });
        // Ctrl+Up/Down in the flat views changes the range: keep the menu's tick in step.
        connect (menu, &QMenu::aboutToShow, this, [this, rangeGroup, ranges]
        {
            const auto i = ranges.indexOf (ambisonics->range());
            for (int a = 0; a < rangeGroup->actions().size(); ++a)
                rangeGroup->actions()[a]->setChecked (a == i);
        });
        ambiCard->addControl (more);
    }

    // Statistics: Copy, like Audition's Copy All.
    {
        auto* copy = new QPushButton (tr ("Copy"), statsCard);
        copy->setToolTip (tr ("Copy the statistics and loudness as text, tab separated"));
        connect (copy, &QPushButton::clicked, this, [this]
        {
            QGuiApplication::clipboard()->setText (state->statsText());
            showMessage (tr ("Statistics copied"));
        });
        statsCard->addControl (copy);
    }

    // Three rows, each split in columns, every divider movable.
    rows = new QSplitter (Qt::Vertical, this);
    rows->setChildrenCollapsible (false);
    rows->setHandleWidth (6);
    const auto addRow = [this] (std::initializer_list<std::pair<Card*, int>> row)
    {
        auto* split = new QSplitter (Qt::Horizontal, rows);
        split->setChildrenCollapsible (false);
        split->setHandleWidth (6);
        QList<int> sizes;
        for (const auto& [card, stretch] : row)
        {
            split->addWidget (card);
            sizes << stretch * 100;
        }
        split->setSizes (sizes);
        splitters.push_back (split);
        rows->addWidget (split);
        connect (split, &QSplitter::splitterMoved, this, [this] { saveLayout(); });
    };
    addRow ({ { loudnessCard, 30 }, { historyCard, 50 }, { levelsCard, 14 } });
    addRow ({ { spectrumCard, 56 }, { spectrogramCard, 22 }, { imagingCard, 22 } });
    addRow ({ { wheelCard, 18 }, { correlationCard, 26 }, { statsCard, 26 }, { ambiCard, 30 } });
    rows->setSizes ({ 340, 360, 330 });
    connect (rows, &QSplitter::splitterMoved, this, [this] { saveLayout(); });

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (10, 8, 10, 10);
    layout->setSpacing (8);
    layout->addLayout (bar);
    layout->addWidget (rows, 1);

    // Dropping files: a hint over everything while one is held above the window.
    setAcceptDrops (true);
    dropHint = new QLabel (tr ("Drop to measure the whole file\nA full report opens in its own window"), this);
    dropHint->setAlignment (Qt::AlignCenter);
    dropHint->setAttribute (Qt::WA_TransparentForMouseEvents);
    {
        auto f = dropHint->font();
        f.setPointSizeF (f.pointSizeF() * 1.4);
        f.setWeight (QFont::DemiBold);
        dropHint->setFont (f);
        auto fill = Theme::current().color (QStringLiteral ("base"));
        fill.setAlphaF (0.82f);
        dropHint->setStyleSheet (QStringLiteral ("QLabel { background: %1; color: %2; border: 2px dashed %2; border-radius: 12px; }")
                                     .arg (fill.name (QColor::HexArgb), Theme::current().color (QStringLiteral ("highlight")).name()));
    }
    dropHint->hide();

    applyTarget (targetBox->currentIndex());
    restoreLayout();
    for (auto* row : splitters)
    {
        bool any = false;
        for (int i = 0; i < row->count(); ++i)
            any = any || ! row->widget (i)->isHidden();
        row->setVisible (any);
    }
    clock.start();
    updateStatus();
}

AnalysisWindow::~AnalysisWindow() = default;

void AnalysisWindow::addSamples (const std::vector<float>& left, const std::vector<float>& right, double sampleRate)
{
    if (frozen)
        return;
    if (sampleRate > 0.0 && std::abs (sampleRate - state->rate) > 0.5)
    {
        const auto order = state->fftOrder, type = state->windowType;
        state->setRate (sampleRate);
        state->setFft (order, type);
        imaging->setSampleRate (sampleRate);
    }
    state->add (left, right);
    if (! imaging->isHidden())
        imaging->addSamples (left, right);
}

void AnalysisWindow::addAmbisonics (const std::vector<std::vector<float>>& channels, int order)
{
    if (! frozen)
        ambisonics->analyse (channels, order);
}

bool AnalysisWindow::wantsAmbisonics() const
{
    return ! frozen && ambisonics->isVisible();
}

bool AnalysisWindow::assumesAmbisonics() const
{
    return assumeAmbisonics;
}

void AnalysisWindow::tick()
{
    const auto dt = std::clamp (clock.restart() / 1000.0, 0.001, 0.25);
    if (! frozen)
    {
        state->update (dt);
        for (auto* c : cards)
            if (auto* m = dynamic_cast<Meter*> (c->content); m != nullptr && c->isVisible())
                m->step (dt);
        if (ambisonics->isVisible())
            ambisonics->step (dt);
    }
    for (auto* c : cards)
        if (c->isVisible())
            c->content->update();
    updateStatus();
}

void AnalysisWindow::reset()
{
    state->reset();
    imaging->reset();
    ambisonics->reset();
    for (auto* c : cards)
        if (auto* m = dynamic_cast<Meter*> (c->content))
            m->clear();
    clock.restart();
    updateStatus();
}

void AnalysisWindow::setFrozen (bool on)
{
    frozen = on;
    if (freezeButton->isChecked() != on)
        freezeButton->setChecked (on);
    updateStatus();
}

void AnalysisWindow::toggleHold (int slot)
{
    auto& hold = state->holds[(size_t) slot];
    if (hold.empty())
        hold = static_cast<SpectrumMeter*> (spectrum)->snapshot();
    else
        hold.clear();
    holdButtons[(size_t) slot]->setChecked (! hold.empty());
    spectrum->update();
}

void AnalysisWindow::updateStatus()
{
    if (messageTicks > 0)
    {
        --messageTicks;   // a message stays up for a couple of seconds
        return;
    }
    const int secs = (int) state->seconds();
    QStringList parts;
    if (frozen)
        parts << tr ("Frozen");
    parts << tr ("%1:%2 measured").arg (secs / 60).arg (secs % 60, 2, 10, QLatin1Char ('0'))
          << tr ("%1 kHz").arg (state->rate / 1000.0, 0, 'f', 1);
    if (state->dominant[0] > 0.0 || state->dominant[1] > 0.0)
    {
        const auto at = [this] (int ch)
        {
            const auto hz = state->dominant[(size_t) ch];
            return hz > 0.0 ? QStringLiteral ("%1 (%2)").arg (hzText (hz), noteText (hz)) : QStringLiteral ("-");
        };
        parts << tr ("strongest: L %1, R %2").arg (at (0), at (1));
    }
    status->setText (parts.join (QStringLiteral ("  ·  ")));
}

void AnalysisWindow::saveLayout() const
{
    auto settings = appSettings();
    QStringList hidden;
    for (auto* c : cards)
        if (c->isHidden())
            hidden << c->id;
    settings.setValue (QStringLiteral ("analysis/hidden"), hidden);
    settings.setValue (QStringLiteral ("analysis/rows"), rows->saveState());
    for (size_t i = 0; i < splitters.size(); ++i)
        settings.setValue (QStringLiteral ("analysis/row%1-%2").arg (i).arg (splitters[i]->count()), splitters[i]->saveState());
    if (isVisible())
        settings.setValue (QStringLiteral ("analysis/geometry"), saveGeometry());
}

void AnalysisWindow::restoreLayout()
{
    auto settings = appSettings();
    if (const auto saved = settings.value (QStringLiteral ("analysis/rows")).toByteArray(); ! saved.isEmpty())
        rows->restoreState (saved);
    for (size_t i = 0; i < splitters.size(); ++i)
        if (const auto saved = settings.value (QStringLiteral ("analysis/row%1-%2").arg (i).arg (splitters[i]->count())).toByteArray(); ! saved.isEmpty())
            splitters[i]->restoreState (saved);
    if (! restoreGeometry (settings.value (QStringLiteral ("analysis/geometry")).toByteArray()))
    {
        const auto screen = QGuiApplication::primaryScreen()->availableGeometry();
        resize (std::min (1560, screen.width() * 9 / 10), std::min (980, screen.height() * 9 / 10));
    }
}

void AnalysisWindow::keyPressEvent (QKeyEvent* e)
{
    const auto key = e->key();
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    if ((key == Qt::Key_A && mods == (Qt::ControlModifier | Qt::AltModifier)) || (key == Qt::Key_W && (mods == Qt::ControlModifier || mods == Qt::MetaModifier)))
    {
        close();   // the key that opened it closes it again, as do Ctrl+W and Super+W
        return;
    }
    if (ambisonics->isVisible() && ambisonics->handleKey (e))
        return;   // arrows, Ctrl+Up/Down and Home turn and move the ambisonics view
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return QWidget::keyPressEvent (e);
    if (key == Qt::Key_Space)
        Q_EMIT playStopRequested();
    else if (key == Qt::Key_R)
        reset();
    else if (key == Qt::Key_F)
        setFrozen (! frozen);
    else if (key == Qt::Key_V && ambisonics->isVisible())
        ambisonics->nextView();
    else if (key >= Qt::Key_1 && key <= Qt::Key_8)
        toggleHold (key - Qt::Key_1);
    else if (key == Qt::Key_F11)
        isFullScreen() ? showNormal() : showFullScreen();
    else if (key == Qt::Key_Escape)
        isFullScreen() ? showNormal() : (void) close();
    else
        return QWidget::keyPressEvent (e);
}

void AnalysisWindow::closeEvent (QCloseEvent* e)
{
    saveLayout();
    Q_EMIT closed();
    QWidget::closeEvent (e);
}

void AnalysisWindow::showMessage (const QString& text)
{
    status->setText (text);
    messageTicks = 90;   // about three seconds, then back to what is measured
}

namespace
{
    QStringList audioFiles (const QMimeData* mime)
    {
        static const auto extensions = []
        {
            QStringList list;
            for (const auto& e : AudioEngine::readableExtensions())
                list << QString::fromStdString (e);
            return list;
        }();
        QStringList paths;
        if (mime != nullptr)
            for (const auto& url : mime->urls())
                if (url.isLocalFile() && extensions.contains (QFileInfo (url.toLocalFile()).suffix().toLower()))
                    paths << url.toLocalFile();
        return paths;
    }
}

void AnalysisWindow::dragEnterEvent (QDragEnterEvent* e)
{
    if (audioFiles (e->mimeData()).isEmpty())
        return;
    e->acceptProposedAction();
    dropHint->setGeometry (rect().adjusted (24, 48, -24, -24));
    dropHint->show();
    dropHint->raise();
}

void AnalysisWindow::dragLeaveEvent (QDragLeaveEvent*)
{
    dropHint->hide();
}

void AnalysisWindow::dropEvent (QDropEvent* e)
{
    dropHint->hide();
    const auto paths = audioFiles (e->mimeData());
    if (paths.isEmpty())
        return;
    e->acceptProposedAction();
    for (const auto& path : paths)
        analyseFile (path);
}

void AnalysisWindow::resizeEvent (QResizeEvent* e)
{
    if (dropHint != nullptr)
        dropHint->setGeometry (rect().adjusted (24, 48, -24, -24));
    QWidget::resizeEvent (e);
}

// Reads and measures the file away from the UI thread, then opens its report.
void AnalysisWindow::analyseFile (const QString& path)
{
    using Result = std::pair<QString, QString>;   // the report, or what went wrong
    const auto name = QFileInfo (path).fileName();
    const auto target = targetBox->currentIndex();
    ++measuring;
    showMessage (tr ("Measuring %1...").arg (name));

    auto* watcher = new QFutureWatcher<Result> (this);
    connect (watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, path, name]
    {
        const auto [text, error] = watcher->result();
        watcher->deleteLater();
        --measuring;
        if (! error.isEmpty())
        {
            showMessage (tr ("%1: %2").arg (name, error));
            return;
        }
        auto* report = new AnalysisReport (text, path, this);
        report->show();
        report->raise();
        report->activateWindow();
        showMessage (measuring > 0 ? tr ("%1 measured, %2 more to go").arg (name).arg (measuring) : tr ("%1 measured").arg (name));
    });
    watcher->setFuture (QtConcurrent::run ([path, target]() -> Result
    {
        std::vector<Marker> markers;
        Tags tags;
        std::string error;
        const auto audio = AudioEngine::loadFile (path.toStdString(), markers, tags, error);
        if (audio == nullptr || audio->length() == 0)
            return { {}, error.empty() ? QObject::tr ("cannot be read") : QString::fromStdString (error) };
        return { analysis::fileReport (*audio, path, target), {} };
    }));
}
