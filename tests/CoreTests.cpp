// SPDX-License-Identifier: AGPL-3.0-only
// Tests for the plain C++ core: audio storage, undo history and DSP.
#include "../Source/core/AudioData.h"
#include "../Source/core/AudioDocument.h"
#include "../Source/core/Dsp.h"
#include "../Source/core/FlacTags.h"
#include "../Source/core/Fft.h"
#include "../Source/core/Montage.h"
#include "../Source/core/Nyquist.h"
#include "../Source/core/Repair.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <string>

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok)
        {
            std::printf ("FAIL: %s\n", what.c_str());
            ++failures;
        }
    }

    bool near (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

    // A ramp makes every frame distinct, so edits can be checked sample by sample.
    AudioDataPtr makeRamp (int channels, Frame length, double rate = 48000.0)
    {
        AudioData::Builder b (channels, rate);
        std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> (1000));
        std::vector<const float*> ptrs;
        for (auto& c : block)
            ptrs.push_back (c.data());

        for (Frame pos = 0; pos < length; pos += 1000)
        {
            const auto n = (int) std::min<Frame> (1000, length - pos);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < n; ++i)
                    block[(size_t) ch][(size_t) i] = (float) ((pos + i) % 100000) / 100000.0f * (ch == 0 ? 1.0f : -1.0f);
            b.append (ptrs.data(), n);
        }

        return b.finish();
    }

    AudioDataPtr makeSine (int channels, Frame length, double freq, double amplitude, double rate = 48000.0)
    {
        AudioData::Builder b (channels, rate);
        std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> (4096));
        std::vector<const float*> ptrs;
        for (auto& c : block)
            ptrs.push_back (c.data());

        for (Frame pos = 0; pos < length; pos += 4096)
        {
            const auto n = (int) std::min<Frame> (4096, length - pos);
            for (int i = 0; i < n; ++i)
            {
                const auto v = (float) (amplitude * std::sin (2.0 * M_PI * freq * (double) (pos + i) / rate));
                for (int ch = 0; ch < channels; ++ch)
                    block[(size_t) ch][(size_t) i] = v;
            }
            b.append (ptrs.data(), n);
        }

        return b.finish();
    }

    float sampleAt (const AudioData& d, int ch, Frame f)
    {
        std::vector<std::vector<float>> buf ((size_t) d.numChannels(), std::vector<float> (1));
        std::vector<float*> ptrs;
        for (auto& c : buf)
            ptrs.push_back (c.data());
        d.read (f, 1, ptrs.data());
        return buf[(size_t) ch][0];
    }

    float rampValue (Frame f) { return (float) (f % 100000) / 100000.0f; }

    void testStorage()
    {
        const Frame len = 200000;  // spans several chunks
        auto d = makeRamp (2, len);
        check (d->length() == len, "length");
        check (sampleAt (*d, 0, 123456) == rampValue (123456), "read across chunks");
        check (sampleAt (*d, 1, 70000) == -rampValue (70000), "second channel");
        check (sampleAt (*d, 0, len + 10) == 0.0f, "read past end is silent");

        float lo = 0, hi = 0;
        d->minMax (0, 1000, 150000, lo, hi);
        check (near (hi, rampValue (99999), 1e-6) && near (lo, 0.0, 1e-6), "minMax over long range");

        auto s = d->slice (65000, 70000);
        check (s->length() == 5000 && sampleAt (*s, 0, 0) == rampValue (65000), "slice");

        auto removed = d->replaced (1000, 2000, nullptr);
        check (removed->length() == len - 1000, "delete length");
        check (sampleAt (*removed, 0, 1000) == rampValue (2000), "delete joins audio");

        auto inserted = d->replaced (500, 500, s.get());
        check (inserted->length() == len + 5000, "insert length");
        check (sampleAt (*inserted, 0, 500) == rampValue (65000), "inserted audio");
        check (sampleAt (*inserted, 0, 5500) == rampValue (500), "audio after insert");

        // A mono clip pasted into stereo goes to both channels.
        auto mono = makeRamp (1, 10);
        auto pasted = d->replaced (0, 0, mono.get());
        check (sampleAt (*pasted, 1, 5) == rampValue (5), "mono maps to all channels");

        // Stereo pasted into 5.1 lands on L and R only; into mono, L and R are averaged.
        auto surround = makeRamp (6, 100);
        auto stereoClip = makeRamp (2, 10);
        auto mixedIn = surround->replaced (0, 0, stereoClip.get());
        check (sampleAt (*mixedIn, 0, 5) == rampValue (5) && sampleAt (*mixedIn, 1, 5) == -rampValue (5) && sampleAt (*mixedIn, 2, 5) == 0.0f
               && sampleAt (*mixedIn, 3, 5) == 0.0f, "stereo into 5.1 fills L/R only");
        auto monoDoc = makeRamp (1, 100);
        auto folded = monoDoc->replaced (0, 0, stereoClip.get());
        check (near (sampleAt (*folded, 0, 5), 0.0, 1.0e-6), "stereo into mono averages (the ramp's sides are opposite)");
    }

    void testStereoize()
    {
        auto mono = makeSine (1, 48000, 1000.0, 0.5);
        auto wide = dsp::stereoize (*mono, 0, mono->length(), {});
        check (wide->numChannels() == 2 && wide->length() == mono->length(), "stereoize makes a stereo copy");

        double maxSumError = 0.0, sideEnergy = 0.0;
        for (Frame f = 0; f < wide->length(); f += 37)
        {
            const auto l = sampleAt (*wide, 0, f), r = sampleAt (*wide, 1, f), m = sampleAt (*mono, 0, f);
            maxSumError = std::max (maxSumError, std::abs ((double) (l + r) * 0.5 - m));
            sideEnergy += (double) (l - r) * (l - r);
        }
        check (maxSumError < 1.0e-6, "stereoize folds back to the original mono");
        check (sideEnergy > 1.0, "stereoize adds width");

        auto part = dsp::stereoize (*mono, 10000, 20000, {});
        check (sampleAt (*part, 0, 5000) == sampleAt (*part, 1, 5000) && sampleAt (*part, 0, 30000) == sampleAt (*mono, 0, 30000),
               "outside the range both sides equal the mono file");
    }

    // Builds a mono file from a function of the frame number.
    AudioDataPtr makeSignal (Frame length, const std::function<float (Frame)>& f, double rate = 48000.0)
    {
        AudioData::Builder b (1, rate);
        std::vector<float> block (4096);
        const float* ptr = block.data();
        for (Frame pos = 0; pos < length; pos += 4096)
        {
            const auto n = (int) std::min<Frame> (4096, length - pos);
            for (int i = 0; i < n; ++i)
                block[(size_t) i] = f (pos + i);
            b.append (&ptr, n);
        }
        return b.finish();
    }

    std::vector<float> samples (const AudioData& d, Frame start, Frame end)
    {
        std::vector<float> out ((size_t) (end - start));
        float* p = out.data();
        d.read (start, (int) (end - start), &p);
        return out;
    }

    // Level of one frequency over [start, end) (Goertzel, amplitude of a sine).
    double toneLevel (const AudioData& d, Frame start, Frame end, double hz)
    {
        const auto x = samples (d, start, end);
        const auto w = 2.0 * M_PI * hz / d.sampleRate();
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            re += x[i] * std::cos (w * (double) i);
            im -= x[i] * std::sin (w * (double) i);
        }
        return 2.0 * std::sqrt (re * re + im * im) / (double) x.size();
    }

    double rmsDifference (const AudioData& a, const AudioData& b, Frame start, Frame end)
    {
        const auto x = samples (a, start, end), y = samples (b, start, end);
        double sum = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
            sum += (double) (x[i] - y[i]) * (x[i] - y[i]);
        return std::sqrt (sum / (double) x.size());
    }

    bool sameOutside (const AudioData& a, const AudioData& b, Frame start, Frame end)
    {
        const auto x = samples (a, 0, a.length()), y = samples (b, 0, b.length());
        for (Frame f = 0; f < (Frame) x.size(); ++f)
            if ((f < start || f >= end) && x[(size_t) f] != y[(size_t) f])
                return false;
        return x.size() == y.size();
    }

    void testRepair()
    {
        const double rate = 48000.0;
        auto twoTones = makeSignal (96000, [] (Frame f) { return (float) (0.3 * std::sin (2.0 * M_PI * 500.0 * f / 48000.0)
                                                                         + 0.3 * std::sin (2.0 * M_PI * 3000.0 * f / 48000.0)); });

        // Spectral gain: 0 dB changes nothing; removing a band takes out only that band.
        auto same = repair::spectralGain (*twoTones, { 30000, 60000, 2000.0, 4000.0 }, 0.0);
        check (rmsDifference (*same, *twoTones, 0, 96000) < 1.0e-6, "spectral gain of 0 dB leaves the audio alone");

        auto cut = repair::spectralGain (*twoTones, { 30000, 60000, 2000.0, 4000.0 }, repair::removeDb);
        check (toneLevel (*cut, 40000, 50000, 3000.0) < 0.003, "band removal takes out the 3 kHz tone ("
               + std::to_string (toneLevel (*cut, 40000, 50000, 3000.0)) + ")");
        check (near (toneLevel (*cut, 40000, 50000, 500.0), 0.3, 0.01), "band removal keeps the 500 Hz tone");
        check (sameOutside (*cut, *twoTones, 30000 - 4096, 60000 + 4096), "band removal leaves audio away from the box untouched");

        // Auto Heal of a click on a tone with a little noise.
        std::mt19937 random (7);
        std::normal_distribution<float> noise (0.0f, 0.01f);
        std::vector<float> hiss (96000);
        for (auto& h : hiss)
            h = noise (random);
        auto clean = makeSignal (96000, [&] (Frame f) { return (float) (0.4 * std::sin (2.0 * M_PI * 440.0 * f / rate)) + hiss[(size_t) f]; });
        auto clicked = makeSignal (96000, [&] (Frame f)
        {
            const auto c = (f >= 48000 && f < 48020) ? 0.9f * ((f % 2) ? 1.0f : -1.0f) : 0.0f;
            return (float) (0.4 * std::sin (2.0 * M_PI * 440.0 * f / rate)) + hiss[(size_t) f] + c;
        });
        std::string error;
        auto healed = repair::autoHeal (*clicked, { 47990, 48030 }, error);
        check (healed != nullptr, "auto heal of a click: " + error);
        if (healed != nullptr)
        {
            const auto before = rmsDifference (*clicked, *clean, 47900, 48100);
            const auto after = rmsDifference (*healed, *clean, 47900, 48100);
            check (after < 0.02 && after < before * 0.1, "auto heal removes a click (error " + std::to_string (before)
                   + " -> " + std::to_string (after) + ")");
            check (sameOutside (*healed, *clicked, 47990 - 4096, 48030 + 4096), "auto heal leaves audio away from the click untouched");
        }

        // A 0.3 s burst of loud noise (a cough) over a steady tone.
        std::vector<float> burst (96000);
        for (auto& b : burst)
            b = noise (random) * 30.0f;
        auto coughed = makeSignal (96000, [&] (Frame f)
        {
            const auto in = f >= 40000 && f < 54400;
            return (float) (0.4 * std::sin (2.0 * M_PI * 440.0 * f / rate)) + hiss[(size_t) f] + (in ? burst[(size_t) f] : 0.0f);
        });
        auto cured = repair::autoHeal (*coughed, { 40000, 54400 }, error);
        check (cured != nullptr, "auto heal of a cough: " + error);
        if (cured != nullptr)
        {
            const auto after = rmsDifference (*cured, *clean, 40000, 54400);
            check (after < 0.03, "auto heal replaces a cough with the tone (error " + std::to_string (after) + ")");
            check (near (toneLevel (*cured, 42000, 52000, 440.0), 0.4, 0.06), "the healed tone keeps its level ("
                   + std::to_string (toneLevel (*cured, 42000, 52000, 440.0)) + ")");
            // The background hiss carries on at the same level rather than going quiet.
            auto toneOnly = makeSignal (96000, [&] (Frame f) { return (float) (0.4 * std::sin (2.0 * M_PI * 440.0 * f / rate)); });
            const auto hissLevel = rmsDifference (*cured, *toneOnly, 42000, 52000);
            check (hissLevel > 0.007 && hissLevel < 0.014, "the healed hiss keeps its level (" + std::to_string (hissLevel) + ")");
        }

        // A phone ring healed in its band only: the tone underneath stays.
        auto ringing = makeSignal (96000, [&] (Frame f)
        {
            const auto ring = f >= 30000 && f < 54000 ? 0.3 * std::sin (2.0 * M_PI * 5000.0 * f / rate) : 0.0;
            return (float) (0.4 * std::sin (2.0 * M_PI * 300.0 * f / rate) + ring) + hiss[(size_t) f];
        });
        auto quiet = repair::autoHeal (*ringing, { 30000, 54000, 4000.0, 6500.0 }, error);
        check (quiet != nullptr, "band heal: " + error);
        if (quiet != nullptr)
        {
            check (toneLevel (*quiet, 34000, 50000, 5000.0) < 0.01, "band heal removes the ring ("
                   + std::to_string (toneLevel (*quiet, 34000, 50000, 5000.0)) + ")");
            check (near (toneLevel (*quiet, 34000, 50000, 300.0), 0.4, 0.01), "band heal keeps the tone below");
        }

        check (repair::autoHeal (*clean, { 0, 96000 }, error) == nullptr, "auto heal refuses the whole file");
    }

    void testRestoration()
    {
        const double rate = 48000.0;
        std::mt19937 random (11);
        std::normal_distribution<float> gauss (0.0f, 1.0f);
        auto tone = [&] (Frame f, double hz, double amp) { return (float) (amp * std::sin (2.0 * M_PI * hz * f / rate)); };

        // Noise reduction: learn from a noise-only second, then reduce under a tone.
        std::vector<float> hiss (192000);
        for (auto& h : hiss)
            h = 0.02f * gauss (random);
        auto noisy = makeSignal (192000, [&] (Frame f) { return hiss[(size_t) f] + (f >= 48000 ? tone (f, 440.0, 0.3) : 0.0f); });
        const auto profile = repair::learnNoise (*noisy, 0, 48000);
        check (profile.isValid() && profile.learnedFrom == 48000, "learn a noise profile");
        repair::NoiseReductionSettings nr;
        nr.reductionDb = 20.0;
        auto cleaned = repair::reduceNoise (*noisy, 0, 192000, profile, nr);
        check (cleaned != nullptr && cleaned->length() == noisy->length(), "noise reduction keeps the length");
        if (cleaned != nullptr)
        {
            auto pure = makeSignal (192000, [&] (Frame f) { return f >= 48000 ? tone (f, 440.0, 0.3) : 0.0f; });
            const auto before = rmsDifference (*noisy, *pure, 60000, 180000);
            const auto after = rmsDifference (*cleaned, *pure, 60000, 180000);
            check (after < before * 0.2, "noise reduction lowers the hiss under a tone ("
                   + std::to_string (before) + " -> " + std::to_string (after) + ")");
            check (near (toneLevel (*cleaned, 60000, 180000, 440.0), 0.3, 0.01), "noise reduction keeps the tone");
            const auto alone = rmsDifference (*cleaned, *makeSignal (192000, [] (Frame) { return 0.0f; }), 10000, 40000);
            check (alone < 0.02 * 0.15, "noise reduction turns noise alone down by the set amount (" + std::to_string (alone) + ")");
        }

        // DeClick: 40 clicks on music-like audio (two tones and a little hiss).
        std::vector<Frame> clickAt;
        for (int i = 0; i < 40; ++i)
            clickAt.push_back (5000 + i * 4500 + (Frame) (random() % 300));
        auto music = [&] (Frame f) { return tone (f, 220.0, 0.3) + tone (f, 1330.0, 0.1) + 0.002f * hiss[(size_t) f] / 0.02f; };
        auto clicked = makeSignal (192000, [&] (Frame f)
        {
            float c = 0.0f;
            for (auto at : clickAt)
                if (f >= at && f < at + 6)
                    c = (f - at) % 2 ? 0.5f : -0.6f;
            return music (f) + c;
        });
        auto smooth = makeSignal (192000, music);
        repair::Report report;
        auto declicked = repair::declick (*clicked, 0, 192000, 6.0, report);
        check (declicked != nullptr && report.fixed >= 38 && report.fixed <= 46, "declick finds the clicks (" + std::to_string (report.fixed)
               + " fixed, " + std::to_string (report.skipped) + " skipped)");
        if (declicked != nullptr)
        {
            const auto left = rmsDifference (*declicked, *smooth, 0, 192000);
            check (left < 0.003, "declick leaves little of the clicks (" + std::to_string (left) + ")");
        }
        repair::Report none;
        repair::declick (*smooth, 0, 192000, 6.0, none);
        check (none.fixed <= 2, "declick leaves clean audio alone (" + std::to_string (none.fixed) + " found)");

        // DeClip: a 300 Hz sine of amplitude 1.2 clipped at 0.9.
        auto loud = makeSignal (48000, [&] (Frame f) { return tone (f, 300.0, 1.2); });
        auto clipped = makeSignal (48000, [&] (Frame f) { return std::clamp (tone (f, 300.0, 1.2), -0.9f, 0.9f); });
        auto unclipped = repair::declip (*clipped, 0, 48000, 99.0, report);
        check (unclipped != nullptr && report.fixed > 500, "declip finds the clipped peaks (" + std::to_string (report.fixed) + ")");
        if (unclipped != nullptr)
        {
            const auto before = rmsDifference (*clipped, *loud, 5000, 43000);
            const auto after = rmsDifference (*unclipped, *loud, 5000, 43000);
            check (after < before * 0.3, "declip rebuilds the peaks (" + std::to_string (before) + " -> " + std::to_string (after) + ")");
        }

        // Diagnostics: the same clicks and clipping listed, then repaired from the list.
        const auto clicks = repair::findClicks (*clicked, 0, 192000, 6.0);
        int listedAt = 0;
        for (auto at : clickAt)
            listedAt += std::any_of (clicks.begin(), clicks.end(), [at] (const repair::Problem& p) { return p.start <= at && p.end >= at + 6; }) ? 1 : 0;
        check (clicks.size() >= 38 && clicks.size() <= 46 && listedAt >= 38, "diagnostics list the clicks (" + std::to_string (clicks.size())
               + " listed, " + std::to_string (listedAt) + " of 40 in the right place)");
        auto fromList = repair::repairProblems (*clicked, clicks, report);
        check (fromList != nullptr && report.fixed == (int) clicks.size(), "repair the listed clicks (" + std::to_string (report.fixed) + ")");
        if (fromList != nullptr)
            check (rmsDifference (*fromList, *smooth, 0, 192000) < 0.003, "repairing from the list leaves little of the clicks");
        check (repair::findClicks (*smooth, 0, 192000, 6.0).size() <= 2, "diagnostics find no clicks in clean audio");

        const auto clips = repair::findClipping (*clipped, 0, 48000, 99.0);
        check (clips.size() > 500 && clips[0].kind == repair::Problem::Kind::clipping, "diagnostics list the clipping (" + std::to_string (clips.size()) + ")");
        auto unclippedFromList = repair::repairProblems (*clipped, clips, report);
        if (unclippedFromList != nullptr)
            check (rmsDifference (*unclippedFromList, *loud, 5000, 43000) < rmsDifference (*clipped, *loud, 5000, 43000) * 0.3,
                   "repairing the listed clipping rebuilds the peaks");
        else
            check (false, "repairing the listed clipping");

        // Dropouts: three gaps of digital silence in a tone, and silence at the start that is not one.
        const std::vector<std::pair<Frame, Frame>> gaps { { 30000, 30096 }, { 60000, 61000 }, { 100000, 114400 } };
        auto steady = makeSignal (192000, [&] (Frame f) { return f < 4800 ? 0.0f : tone (f, 440.0, 0.3); });
        auto dropped = makeSignal (192000, [&] (Frame f)
        {
            for (auto [a, b] : gaps)
                if (f >= a && f < b)
                    return 0.0f;
            return f < 4800 ? 0.0f : tone (f, 440.0, 0.3);
        });
        const auto drops = repair::findDropouts (*dropped, 0, 192000);
        bool dropsRight = drops.size() == gaps.size();
        for (size_t i = 0; dropsRight && i < drops.size(); ++i)
            dropsRight = std::llabs (drops[i].start - gaps[i].first) <= 1 && std::llabs (drops[i].end - gaps[i].second) <= 1;
        check (dropsRight, "diagnostics find the three dropouts (" + std::to_string (drops.size()) + ")");
        auto filled = repair::repairProblems (*dropped, drops, report);
        check (filled != nullptr && report.fixed == 3, "repair the dropouts (" + std::to_string (report.fixed) + ")");
        if (filled != nullptr)
            check (rmsDifference (*filled, *steady, 29000, 62000) < 0.01 && toneLevel (*filled, 102000, 112000, 440.0) > 0.25,
                   "the dropouts are filled with the tone");

        // Silence: two seconds of near silence in the middle.
        auto paused = makeSignal (192000, [&] (Frame f) { return f >= 48000 && f < 144000 ? 0.0003f * hiss[(size_t) f] / 0.02f : tone (f, 440.0, 0.3); });
        const auto quiet = repair::findSilence (*paused, 0, 192000, -50.0, 1.0);
        check (quiet.size() == 1 && std::llabs (quiet[0].start - 48000) <= 480 && std::llabs (quiet[0].end - 144000) <= 480,
               "diagnostics find the silent stretch");
        check (repair::findSilence (*paused, 0, 192000, -50.0, 2.5).empty(), "silence shorter than asked for is not listed");

        // DeHum: 50 Hz hum and harmonics under a 1 kHz tone.
        auto hummed = makeSignal (192000, [&] (Frame f)
        {
            return tone (f, 1000.0, 0.3) + tone (f, 50.0, 0.1) + tone (f, 150.0, 0.05) + tone (f, 250.0, 0.03);
        });
        const auto hum = repair::findHum (*hummed, 0, 192000);
        check (near (hum, 50.0, 0.05), "find the hum at 50 Hz (" + std::to_string (hum) + ")");
        repair::HumSettings hs;
        hs.frequency = hum;
        auto dehummed = repair::dehum (*hummed, 0, 192000, hs);
        if (dehummed != nullptr)
        {
            check (toneLevel (*dehummed, 96000, 190000, 50.0) < 0.003 && toneLevel (*dehummed, 96000, 190000, 150.0) < 0.002,
                   "dehum removes the hum (" + std::to_string (toneLevel (*dehummed, 96000, 190000, 50.0)) + ")");
            check (near (toneLevel (*dehummed, 96000, 190000, 1000.0), 0.3, 0.005), "dehum keeps the tone");
            check (toneLevel (*dehummed, 0, 24000, 50.0) < 0.001, "dehum also works from the very start of the file ("
                   + std::to_string (toneLevel (*dehummed, 0, 24000, 50.0)) + ")");
        }
        check (repair::findHum (*smooth, 0, 192000) == 0.0, "no hum found in audio without hum");

        // DeEss: a loud 7 kHz burst is turned down, the 200 Hz tone under it is not.
        auto sss = makeSignal (96000, [&] (Frame f)
        {
            const auto s = f >= 40000 && f < 56000 ? tone (f, 6100.0, 0.15) + tone (f, 7300.0, 0.15) + tone (f, 8700.0, 0.1) : 0.0f;
            return tone (f, 200.0, 0.3) + s;
        });
        repair::DeEssSettings es;
        auto deessed = repair::deess (*sss, 0, 96000, es);
        if (deessed != nullptr)
        {
            auto plain = makeSignal (96000, [&] (Frame f) { return tone (f, 200.0, 0.3); });
            const auto before = rmsDifference (*sss, *plain, 42000, 54000);
            const auto after = rmsDifference (*deessed, *plain, 42000, 54000);
            check (after < before * 0.5, "deess turns the burst down (" + std::to_string (before) + " -> " + std::to_string (after) + ")");
            check (near (toneLevel (*deessed, 42000, 54000, 200.0), 0.3, 0.005), "deess keeps the low tone");
            check (rmsDifference (*deessed, *sss, 5000, 30000) < 1.0e-4, "deess leaves quiet passages alone");
        }

        // Interpolation fills a gap in a tone almost exactly.
        std::vector<float> x (4000);
        for (int i = 0; i < 4000; ++i)
            x[(size_t) i] = tone (i, 700.0, 0.5);
        auto truth = x;
        for (int i = 2000; i < 2060; ++i)
            x[(size_t) i] = 0.0f;
        check (repair::interpolateGap (x, 2000, 60), "interpolate a 60-sample gap");
        double worst = 0.0;
        for (int i = 2000; i < 2060; ++i)
            worst = std::max (worst, (double) std::abs (x[(size_t) i] - truth[(size_t) i]));
        check (worst < 0.01, "the gap matches the tone (" + std::to_string (worst) + ")");
    }

    void testLayouts()
    {
        check (dsp::speakerLayout (6).name == "5.1" && dsp::channelName (3, 6) == "LFE" && dsp::channelName (4, 6) == "Ls",
               "5.1 layout in WAV order");
        check (dsp::channelName (8, 12) == "Ltf" && dsp::channelName (2, 9) == "Ch 3", "7.1.4 and unknown layouts");
        check (dsp::loudnessChannelWeight (3, 6) == 0.0 && dsp::loudnessChannelWeight (5, 6) == 1.41
               && dsp::loudnessChannelWeight (1, 2) == 1.0, "BS.1770 channel weights");

        const auto m = dsp::stereoDownmix (6);
        check (m[0][0] == 1.0f && m[1][0] == 0.0f && near (m[0][2], 0.7071, 1.0e-3) && near (m[1][2], 0.7071, 1.0e-3)
               && m[0][3] == 0.0f && m[1][3] == 0.0f && near (m[0][4], 0.7071, 1.0e-3) && m[1][4] == 0.0f,
               "5.1 fold-down: centre and surrounds at -3 dB, LFE dropped");
        const auto mono = dsp::stereoDownmix (1);
        check (mono[0][0] == 1.0f && mono[1][0] == 1.0f, "mono plays on both sides");
    }

    void testDocument()
    {
        AudioDocument doc;
        auto d = makeRamp (1, 1000);
        doc.reset (d, { { 1, Marker::Type::generic, 800, 800, "A" }, { 2, Marker::Type::region, 100, 200, "R" } }, "x.wav");
        check (! doc.isModified() && ! doc.canUndo(), "fresh document");

        DocumentState next { d->replaced (50, 250, nullptr), doc.current().markers, {} };
        adjustMarkersForEdit (next.markers, 50, 250, 0);
        doc.apply ("Delete", next);
        check (doc.isModified() && doc.canUndo(), "modified after edit");
        check (doc.current().markers.size() == 1 && doc.current().markers[0].start == 600, "markers follow delete");

        doc.undo();
        check (! doc.isModified() && doc.current().data->length() == 1000 && doc.current().markers.size() == 2, "undo");
        doc.redo();
        check (doc.current().data->length() == 800, "redo");
        doc.markSaved();
        doc.undo();
        doc.apply ("Other", { d, {}, {} });
        check (doc.isModified() && ! doc.canRedo(), "branching drops redo");
        check (doc.historyDescriptions().size() == 2, "history list");
    }

    void testDsp()
    {
        auto d = makeSine (2, 48000 * 10, 997.0, 0.5);
        auto louder = dsp::applyGain (*d, 0, d->length(), 6.0206);
        check (near (louder->peakAbs (0, louder->length()), 1.0, 1e-3), "gain +6 dB doubles peak");

        auto faded = dsp::fadeIn (*d, 0, 48000, dsp::FadeShape::linear);
        check (near (sampleAt (*faded, 0, 0), 0.0, 1e-6), "fade starts silent");
        check (near (sampleAt (*faded, 0, 60000), sampleAt (*d, 0, 60000), 1e-6), "fade leaves rest alone");

        auto rev = dsp::reverse (*d, 0, 1000);
        check (sampleAt (*rev, 0, 0) == sampleAt (*d, 0, 999), "reverse");

        auto inv = dsp::invert (*d, 0, d->length());
        check (sampleAt (*inv, 0, 123) == -sampleAt (*d, 0, 123), "invert");

        // EBU Tech 3341: a 1 kHz sine at -23 dBFS peak on both stereo channels reads -23 LUFS.
        auto ref = makeSine (2, 48000 * 20, 1000.0, dsp::dbToGain (-23.0));
        auto report = dsp::measureLoudness (*ref, 0, ref->length());
        check (near (report.integratedLufs, -23.0, 0.1), "integrated loudness " + std::to_string (report.integratedLufs));
        check (near (report.samplePeakDb, -23.0, 0.05), "sample peak");
        check (near (report.truePeakDb, -23.0, 0.3), "true peak " + std::to_string (report.truePeakDb));
        check (report.loudnessRange < 0.5, "steady tone has no loudness range");

        // DC removal.
        AudioData::Builder b (1, 48000.0);
        std::vector<float> dc (48000, 0.25f);
        const float* p = dc.data();
        b.append (&p, 48000);
        auto offset = b.finish();
        auto cleaned = dsp::removeDcOffset (*offset, 0, offset->length());
        check (near (sampleAt (*cleaned, 0, 24000), 0.0, 1e-4), "DC removed");
    }

    void testSpectrum()
    {
        // A full-scale sine centred on bin 64 of a 1024-point analysis reads about 0 dB there.
        SpectrumAnalyzer analyzer (10);
        std::vector<float> input (1024), out ((size_t) analyzer.numBins());
        for (int i = 0; i < 1024; ++i)
            input[(size_t) i] = (float) std::sin (2.0 * M_PI * 64.0 * i / 1024.0);

        analyzer.magnitudesDb (input.data(), out.data());
        check (near (out[64], 0.0, 0.1), "sine bin level " + std::to_string (out[64]));
        check (out[200] < -100.0f, "far bins are quiet");
    }

    void testShapes()
    {
        const double rate = 48000.0;

        // Painting, lasso and harmonics.
        repair::SpectralMask brush (rate);
        brush.paint (10000, 1000.0, 2000.0, 3.0);
        check (brush.at (10000, 1000.0) > 0.99f && brush.at (10000, 4000.0) == 0.0f && brush.at (30000, 1000.0) == 0.0f,
               "the brush selects around where it paints");
        brush.addHarmonics (3);
        check (brush.at (10000, 2000.0) > 0.9f && brush.at (10000, 3000.0) > 0.9f && brush.at (10000, 1500.0) < 0.1f,
               "add harmonics selects the overtones");
        check (brush.start() <= 8000 && brush.end() >= 12000 && brush.end() < 13000, "the mask knows where it starts and ends");

        repair::SpectralMask lasso (rate);
        lasso.fillPolygon ({ { 30000, 2000.0 }, { 60000, 2000.0 }, { 60000, 4500.0 }, { 30000, 4500.0 } });
        check (lasso.at (45000, 3000.0) == 1.0f && lasso.at (45000, 500.0) == 0.0f && lasso.at (20000, 3000.0) == 0.0f,
               "the lasso selects what it surrounds");

        // Removing what the lasso surrounds takes out the 3 kHz tone and nothing else.
        auto twoTones = makeSignal (96000, [] (Frame f) { return (float) (0.3 * std::sin (2.0 * M_PI * 500.0 * f / 48000.0)
                                                                         + 0.3 * std::sin (2.0 * M_PI * 3000.0 * f / 48000.0)); });
        repair::SpectralRegion drawn;
        drawn.mask = std::make_shared<repair::SpectralMask> (lasso);
        auto cut = repair::spectralGain (*twoTones, drawn, repair::removeDb);
        check (cut != nullptr, "spectral gain on a drawn shape");
        if (cut != nullptr)
        {
            check (toneLevel (*cut, 40000, 50000, 3000.0) < 0.005, "the shape's removal takes out the 3 kHz tone ("
                   + std::to_string (toneLevel (*cut, 40000, 50000, 3000.0)) + ")");
            check (near (toneLevel (*cut, 40000, 50000, 500.0), 0.3, 0.01), "the shape's removal keeps the 500 Hz tone");
            check (sameOutside (*cut, *twoTones, 30000 - 4096, 60000 + 4096), "the shape's removal leaves the rest untouched");
        }

        // Spot healing: brushing over a ring removes it and keeps the tone below.
        std::mt19937 random (11);
        std::normal_distribution<float> noise (0.0f, 0.01f);
        std::vector<float> hiss (480000);
        for (auto& h : hiss)
            h = noise (random);
        auto ringing = makeSignal (96000, [&] (Frame f)
        {
            const auto ring = f >= 30000 && f < 54000 ? 0.3 * std::sin (2.0 * M_PI * 5000.0 * f / rate) : 0.0;
            return (float) (0.4 * std::sin (2.0 * M_PI * 300.0 * f / rate) + ring) + hiss[(size_t) f];
        });
        auto stroke = std::make_shared<repair::SpectralMask> (rate);
        for (Frame f = 29000; f <= 55000; f += 500)
            stroke->paint (f, 5000.0, 1500.0, 4.0);
        repair::SpectralRegion spot;
        spot.mask = stroke;
        std::string error;
        auto quiet = repair::autoHeal (*ringing, spot, error);
        check (quiet != nullptr, "spot heal: " + error);
        if (quiet != nullptr)
        {
            check (toneLevel (*quiet, 34000, 50000, 5000.0) < 0.01, "spot heal removes the ring ("
                   + std::to_string (toneLevel (*quiet, 34000, 50000, 5000.0)) + ")");
            check (near (toneLevel (*quiet, 34000, 50000, 300.0), 0.4, 0.01), "spot heal keeps the tone below");
        }

        // Find similar: six short chirps among hiss, a tone and a low thump.
        const std::vector<Frame> chirps { 48000, 110400, 177600, 244800, 316800, 393600 };
        auto busy = makeSignal (480000, [&] (Frame f)
        {
            double x = 0.2 * std::sin (2.0 * M_PI * 300.0 * f / rate) + hiss[(size_t) f];
            for (auto c : chirps)
                if (f >= c && f < c + 400)
                    x += 0.3 * std::sin (M_PI * (double) (f - c) / 400.0) * std::sin (2.0 * M_PI * 4000.0 * f / rate);
            if (f >= 216000 && f < 217440)
                x += 0.5 * std::sin (M_PI * (double) (f - 216000) / 1440.0) * std::sin (2.0 * M_PI * 90.0 * f / rate);
            return (float) x;
        });
        const auto found = repair::findSimilar (*busy, { 47900, 48500 }, 0, busy->length(), 0.8);
        bool allFound = found.size() == 5;
        for (size_t i = 0; allFound && i < found.size(); ++i)
            allFound = found[i].start <= chirps[i + 1] && found[i].end >= chirps[i + 1] + 400;
        check (allFound, "find similar finds the other five chirps (" + std::to_string (found.size()) + " found)");

        auto plain = makeSignal (480000, [&] (Frame f) { return (float) (0.2 * std::sin (2.0 * M_PI * 300.0 * f / rate)) + hiss[(size_t) f]; });
        check (repair::findSimilar (*plain, { 47900, 48500 }, 0, plain->length(), 0.8).empty(),
               "find similar finds nothing when the example does not stand out");
    }

    void testConvert()
    {
        // 48 kHz to 44.1 kHz: the tone keeps its level and shape.
        auto sine = makeSignal (96000, [] (Frame f) { return (float) (0.5 * std::sin (2.0 * M_PI * 1000.0 * f / 48000.0)); });
        auto down = dsp::resample (*sine, 44100.0);
        check (down != nullptr && down->length() == 88200 && down->sampleRate() == 44100.0, "resample to 44.1 kHz gives the right length");
        if (down != nullptr)
        {
            auto ideal = makeSignal (88200, [] (Frame f) { return (float) (0.5 * std::sin (2.0 * M_PI * 1000.0 * f / 44100.0)); }, 44100.0);
            const auto error = rmsDifference (*down, *ideal, 4000, 84000);
            check (error < 2.0e-4, "resampled tone matches the ideal (" + std::to_string (error) + ")");
        }

        // Down to 16 kHz: a 10 kHz tone is above the new limit and must not fold back to 6 kHz.
        auto high = makeSignal (96000, [] (Frame f) { return (float) (0.5 * std::sin (2.0 * M_PI * 10000.0 * f / 48000.0)
                                                                      + 0.3 * std::sin (2.0 * M_PI * 1000.0 * f / 48000.0)); });
        auto low = dsp::resample (*high, 16000.0);
        if (low != nullptr)
        {
            check (toneLevel (*low, 2000, 30000, 6000.0) < 2.0e-4, "no aliasing when resampling down ("
                   + std::to_string (toneLevel (*low, 2000, 30000, 6000.0)) + ")");
            check (near (toneLevel (*low, 2000, 30000, 1000.0), 0.3, 0.003), "the tone below the limit is kept");
        }
        else
            check (false, "resample to 16 kHz");

        // Up and back down again.
        auto up = dsp::resample (*sine, 96000.0);
        auto back = up != nullptr ? dsp::resample (*up, 48000.0) : nullptr;
        check (back != nullptr && back->length() == sine->length() && rmsDifference (*back, *sine, 4000, 92000) < 2.0e-4,
               "resampling up and back down returns the original");

        // Quantizing to 16 bits puts every sample on the grid, close to the original.
        auto quiet = makeSignal (48000, [] (Frame f) { return (float) (0.001 * std::sin (2.0 * M_PI * 440.0 * f / 48000.0)); });
        auto plain = dsp::quantize (*quiet, 16, dsp::Dither::tpdf);
        auto shaped = dsp::quantize (*quiet, 16, dsp::Dither::shaped);
        const auto x = samples (*quiet, 0, 48000), p = samples (*plain, 0, 48000), q = samples (*shaped, 0, 48000);
        bool onGrid = true;
        double worst = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            onGrid = onGrid && std::abs (p[i] * 32768.0f - std::round (p[i] * 32768.0f)) < 1.0e-3f;
            worst = std::max (worst, (double) std::abs (p[i] - x[i]));
        }
        check (onGrid && worst < 1.5 / 32768.0, "16-bit quantize with dither stays within the dither");

        // Noise shaping: less of the error at low frequencies (its spectrum at a few points below 2 kHz).
        auto lowNoise = [&] (const std::vector<float>& y)
        {
            double total = 0.0;
            for (double hz = 150.0; hz < 2000.0; hz += 100.0)
            {
                double re = 0.0, im = 0.0;
                for (size_t i = 0; i < y.size(); ++i)
                {
                    const auto e = (double) y[i] - x[i];
                    re += e * std::cos (2.0 * M_PI * hz * (double) i / 48000.0);
                    im -= e * std::sin (2.0 * M_PI * hz * (double) i / 48000.0);
                }
                total += re * re + im * im;
            }
            return total;
        };
        check (lowNoise (q) < lowNoise (p) * 0.1, "noise shaping moves the dither noise out of the low frequencies");

        // The true-peak limiter keeps a loud tone under its ceiling, including the
        // peaks between samples (fs/4 at 45 degrees peaks 3 dB above its samples).
        auto loudTone = makeSignal (96000, [] (Frame f) { return (float) (1.6 * std::sin (2.0 * M_PI * 997.0 * f / 48000.0)); });
        auto between = makeSignal (96000, [] (Frame f) { return (float) (0.99 * std::sin (M_PI * 0.5 * (double) f + M_PI * 0.25)); });
        for (const auto& [loud, name] : { std::pair { loudTone, "a loud tone" }, std::pair { between, "peaks between samples" } })
        {
            auto limited = dsp::limitTruePeak (*loud, 0, loud->length(), -1.0);
            const auto tp = dsp::measureLoudness (*limited, 0, limited->length()).truePeakDb;
            check (limited->length() == loud->length() && tp < -0.8 && tp > -1.6, "true-peak limiter on " + std::string (name) + " ("
                   + std::to_string (tp) + " dBTP)");
        }
        auto gentle = makeSignal (48000, [] (Frame f) { return (float) (0.3 * std::sin (2.0 * M_PI * 440.0 * f / 48000.0)); });
        auto untouched = dsp::limitTruePeak (*gentle, 0, gentle->length(), -1.0);
        check (rmsDifference (*untouched, *gentle, 0, 48000) < 1.0e-7, "true-peak limiter leaves quiet audio alone, in time");
        auto part = dsp::limitTruePeak (*loudTone, 24000, 48000, -6.0);
        check (part->length() == loudTone->length() && sampleAt (*part, 0, 1000) == sampleAt (*loudTone, 0, 1000)
               && dsp::measureLoudness (*part, 24000, 48000).truePeakDb < -5.8, "true-peak limiter on part of a file");

        auto stereo = dsp::toStereo (sine);
        auto mono = dsp::toMono (*stereo);
        check (mono->numChannels() == 1 && rmsDifference (*mono, *sine, 0, 96000) < 1.0e-7, "stereo of equal sides mixes back to the mono");
    }

    void testFlacTags()
    {
        // A FLAC file needs only "fLaC", a stream info block and frames for the tag code.
        const std::string path = "/tmp/phinyx-core-test.flac";
        {
            std::ofstream out (path, std::ios::binary | std::ios::trunc);
            out.write ("fLaC", 4);
            const unsigned char info[4] { 0x00, 0, 0, 34 };   // stream info, not the last block
            out.write ((const char*) info, 4);
            out.write (std::string (34, '\x11').data(), 34);
            const unsigned char pad[4] { 0x81, 0, 0, 8 };     // padding, the last block
            out.write ((const char*) pad, 4);
            out.write (std::string (8, '\0').data(), 8);
            out.write ("AUDIOFRAMES", 11);
        }
        check (flactags::read (path).empty(), "a FLAC file without comments has no tags");

        std::string error;
        check (flactags::write (path, { { "TITLE", "Night Drive" }, { "ARTIST", "Ünïcode Bänd" } }, "PHINYX", error), "write FLAC tags: " + error);
        auto fields = flactags::read (path);
        check (fields.size() == 2 && fields["TITLE"] == "Night Drive" && fields["ARTIST"] == "Ünïcode Bänd", "read FLAC tags back");

        check (flactags::write (path, { { "TITLE", "Second Take" } }, "PHINYX", error), "rewrite FLAC tags: " + error);
        fields = flactags::read (path);
        check (fields.size() == 1 && fields["TITLE"] == "Second Take", "rewriting replaces the old tags");

        std::ifstream in (path, std::ios::binary);
        const std::string bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        check (bytes.substr (8, 34) == std::string (34, '\x11') && bytes.size() > 11 && bytes.substr (bytes.size() - 11) == "AUDIOFRAMES",
               "the stream info and the audio are kept");
        std::remove (path.c_str());
    }

    void testNyquist()
    {
        const std::string share = PHINYX_SHARE_SOURCE_DIR;
        nyquist::setFolders (share + "/nyquist", { share + "/nyquist-plug-ins" }, "/tmp");

        // The header: name, type, controls.
        auto header = nyquist::parse (";nyquist plug-in\n;version 4\n;type process\n;name \"Half...\"\n"
                                      "$control amount (_ \"Amount\") real \"dB\" -6 -24 0\n"
                                      ";control mode \"Mode\" choice \"Soft,Hard\" 1\n"
                                      "$control label (_ \"Label\") string \"\" \"hello\"\n"
                                      "$control shape (_ \"Shape\") choice ((\"Lin\" (_ \"Linear\")) (_ \"Curved\")) 0\n"
                                      "(mult *track* (db-to-linear amount))\n");
        check (header.ok && header.name == "Half" && header.kind == nyquist::Kind::process && header.version == 4
               && header.controls.size() == 4, "Nyquist header parsed");
        if (header.controls.size() == 4)
            check (header.controls[0].value == -6.0 && header.controls[0].low == -24.0 && header.controls[0].label == "dB"
                   && header.controls[1].choices.size() == 2 && header.controls[1].value == 1.0 && header.controls[2].text == "hello"
                   && header.controls[3].choices == std::vector<std::string> { "Linear", "Curved" },
                   "Nyquist controls parsed");

        // Processing: the selection comes back 6 dB down; the rest is untouched by the caller.
        auto sine = makeSine (2, 48000, 440.0, 0.5);
        nyquist::Input in;
        in.audio = sine.get();
        in.start = 12000;
        in.end = 36000;
        auto half = nyquist::run (header, in);
        check (half.type == nyquist::Result::Type::audio && half.audio != nullptr && half.audio->numChannels() == 2
               && half.audio->length() == 24000, "Nyquist process returns the selection (" + half.text + half.output + ")");
        if (half.audio != nullptr)
            check (std::abs (sampleAt (*half.audio, 1, 1000) - dsp::dbToGain (-6.0) * sampleAt (*sine, 1, 13000)) < 1.0e-5,
                   "Nyquist process scales the audio");

        // Labels, messages, numbers.
        auto labels = nyquist::run (nyquist::parse ("(list (list 0.25 \"a\") (list 0.5 0.75 \"b\"))", true), in);
        check (labels.type == nyquist::Result::Type::labels && labels.labels.size() == 2
               && std::abs (labels.labels[1].start - 0.75) < 1.0e-9 && labels.labels[1].text == "b", "Nyquist labels, in file time");
        auto message = nyquist::run (nyquist::parse ("(format nil \"~a channels\" (arrayp *track*))", true), in);
        check (message.type == nyquist::Result::Type::text && message.text == "T channels", "Nyquist message (" + message.text + ")");
        auto peak = nyquist::run (nyquist::parse ("(get '*selection* 'peak-level)", true), in);
        check (peak.type == nyquist::Result::Type::number && std::abs (std::stod (peak.text) - 0.5) < 0.01, "Nyquist sees the selection's peak");

        // Generating: a second of tone, as many channels as the file.
        auto gen = nyquist::parse (";nyquist plug-in\n;version 4\n;type generate\n;name \"Tone\"\n(osc 69 1.0)\n");
        auto tone = nyquist::run (gen, in);
        check (tone.type == nyquist::Result::Type::audio && tone.audio != nullptr && tone.audio->numChannels() == 2
               && tone.audio->length() == 48000, "Nyquist generator");

        // An error is reported, not crashed on.
        auto broken = nyquist::run (nyquist::parse ("(this-is-not-defined 1)", true), in);
        check (broken.type == nyquist::Result::Type::error && broken.output.find ("unbound") != std::string::npos,
               "Nyquist error comes back with its message");

        // A bundled Audacity plug-in.
        auto tremolo = nyquist::load (share + "/nyquist-plug-ins/tremolo.ny");
        check (tremolo.ok && tremolo.name == "Tremolo" && ! tremolo.controls.empty(), "load Audacity's Tremolo");
        auto shaken = nyquist::run (tremolo, in);
        check (shaken.type == nyquist::Result::Type::audio && shaken.audio != nullptr && shaken.audio->length() == 24000,
               "run Audacity's Tremolo (" + shaken.text + shaken.output + ")");
    }
}

namespace
{
    AudioDataPtr makeConstant (int channels, Frame length, float value, double rate = 44100.0)
    {
        AudioData::Builder b (channels, rate);
        std::vector<float> block (4096, value);
        std::vector<const float*> ptrs ((size_t) channels, block.data());
        for (Frame pos = 0; pos < length; pos += 4096)
            b.append (ptrs.data(), (int) std::min<Frame> (4096, length - pos));
        return b.finish();
    }

    void testCurvedFade()
    {
        check (dsp::curvedFadeGain (0.0, 0.5) == 0.0f && dsp::curvedFadeGain (1.0, -0.5) == 1.0f, "curved fade: ends");
        check (near (dsp::curvedFadeGain (0.3, 0.0), 0.3, 1.0e-6), "curved fade: 0 is linear");
        check (dsp::curvedFadeGain (0.3, 0.8) > 0.6f && dsp::curvedFadeGain (0.3, -0.8) < 0.05f, "curved fade: bends both ways");
        const auto ramp = makeConstant (2, 1000, 1.0f);
        const auto in = dsp::curvedFade (*ramp, 0, 1000, 0.0, true);
        std::vector<float> l (1000), r (1000);
        float* lr[] { l.data(), r.data() };
        in->read (0, 1000, lr);
        check (l[0] == 0.0f && l[999] == 1.0f && near (l[500], 500.0 / 999.0, 1.0e-5), "curved fade in");
        const auto out = dsp::curvedFade (*ramp, 0, 1000, 0.5, false);
        out->read (0, 1000, lr);
        check (l[0] == 1.0f && l[999] == 0.0f, "curved fade out");
    }

    void testMontage()
    {
        const double rate = 44100.0;
        Montage m;
        m.sampleRate = rate;
        m.channels = 2;

        MontageClip a;
        a.path = "/music/one.wav";
        a.source = makeRamp (2, 200000, rate);
        a.sourceEnd = 200000;
        MontageClip b;
        b.path = "/music/Two Song.flac";
        b.source = makeConstant (1, 100000, 0.5f, rate);
        b.sourceStart = 10000;
        b.sourceEnd = 100000;
        b.pauseBefore = 44100;
        b.tags[tag::isrc] = "USRC17607839";
        m.clips = { a, b };

        check (m.clipStart (0) == 0 && m.clipStart (1) == 244100, "montage: a pause moves the next song");
        check (m.length() == 244100 + 90000, "montage: length is songs plus pauses");
        check (m.clipAt (100) == 0 && m.clipAt (220000) == -1 && m.clipAt (250000) == 1, "montage: song at a position");

        // Untouched audio is shared, so the album plays the source exactly.
        auto mix = m.mixdown();
        check (mix->length() == m.length() && mix->numChannels() == 2, "montage: mixdown length and channels");
        std::vector<float> l (100), r (100);
        float* lr[] { l.data(), r.data() };
        mix->read (123456, 100, lr);
        std::vector<float> sl (100), sr (100);
        float* slr[] { sl.data(), sr.data() };
        a.source->read (123456, 100, slr);
        check (l == sl && r == sr, "montage: first song unchanged");
        mix->read (220000, 100, lr);
        check (l[0] == 0.0f && r[99] == 0.0f, "montage: the pause is silent");
        mix->read (260000, 100, lr);
        check (l[0] == 0.5f && r[0] == 0.5f, "montage: a mono song fills both sides");

        // Gain and fades.
        m.clips[1].gainDb = -6.0;
        m.clips[1].fadeIn = 1000;
        mix = m.mixdown();
        mix->read (m.clipStart (1), 100, lr);
        check (l[0] < 0.01f, "montage: fade in starts near silence");
        mix->read (260000, 1, lr);
        check (near (l[0], 0.5 * dsp::dbToGain (-6.0), 1.0e-4), "montage: song gain");

        // A negative pause is a crossfade: equal power keeps a steady level for unrelated material.
        Montage x;
        x.sampleRate = rate;
        x.clips = { a, b };
        x.clips[0].source = makeConstant (2, 200000, 0.5f, rate);
        x.clips[1].pauseBefore = -20000;
        x.clampPauses();
        check (x.overlapBefore (1) == 20000 && x.clipStart (1) == 180000, "montage: overlap");
        check (near (x.envelope (0, 190000) * x.envelope (0, 190000) + x.envelope (1, 10000) * x.envelope (1, 10000), 1.0, 1.0e-3),
               "montage: crossfade is equal power");
        x.clips[1].pauseBefore = -500000;
        x.clampPauses();
        check (x.overlapBefore (1) == 90000, "montage: an overlap is never longer than a song");

        // Markers and CUE sheet.
        const auto markers = m.trackMarkers();
        check (markers.size() == 2 && markers[1].name == "2. Two Song" && markers[1].start == 244100, "montage: track markers");
        m.tags[tag::album] = "First \"Album\"";
        const auto cue = m.cueSheet ("album.wav");
        check (cue.find ("TITLE \"First 'Album'\"") != std::string::npos, "cue: album title");
        check (cue.find ("FILE \"album.wav\" WAVE") != std::string::npos, "cue: file line");
        check (cue.find ("TRACK 02 AUDIO\n    TITLE \"Two Song\"\n    ISRC USRC17607839\n    INDEX 00 00:04:40\n    INDEX 01 00:05:40") != std::string::npos,
               "cue: second track with pregap (" + cue + ")");
        check (cueTime (44100 * 61 + 44100 / 75 * 3, rate) == "01:01:03", "cue: time format");

        Montage empty;
        check (empty.mixdown() != nullptr && empty.mixdown()->length() == 0, "montage: empty mixdown");
    }
}

int main()
{
    testStorage();
    testDocument();
    testDsp();
    testLayouts();
    testStereoize();
    testSpectrum();
    testRepair();
    testRestoration();
    testShapes();
    testConvert();
    testFlacTags();
    testNyquist();
    testMontage();
    testCurvedFade();

    if (failures == 0)
        std::printf ("All core tests passed\n");

    return failures == 0 ? 0 : 1;
}
