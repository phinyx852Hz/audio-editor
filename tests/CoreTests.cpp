// SPDX-License-Identifier: AGPL-3.0-only
// Tests for the plain C++ core: audio storage, undo history and DSP.
#include "../Source/core/AudioData.h"
#include "../Source/core/AudioDocument.h"
#include "../Source/core/Dsp.h"
#include "../Source/core/MidSide.h"
#include "../Source/sampler/SamplerEngine.h"
#include "../Source/sampler/SlicerEngine.h"
#include "../Source/mixer/MidiFile.h"
#include "../Source/mixer/MixerEngine.h"
#include "../Source/patchbay/PatchBay.h"
#include "../Source/core/Ixml.h"
#include "../Source/core/Metadata.h"
#include "../Source/core/RiffChunks.h"
#include "../Source/core/Xmp.h"
#include "../Source/core/Fft.h"
#include "../Source/core/Album.h"
#include "../Source/core/Nyquist.h"
#include "../Source/core/Repair.h"
#include "../Source/core/SpectrumView.h"
#include "../Source/core/Generate.h"
#include "../Source/core/Pitch.h"
#include "../Source/core/PitchFix.h"
#include "../Source/core/TimeStretch.h"
#include "../Source/core/Quantize.h"
#include "../Source/core/Binaural.h"
#include "../Source/core/Ambisonics.h"
#include "../Source/core/SpatialStudio.h"
#include "../Source/core/PrismSplit.h"
#include "../Source/core/MasterAssist.h"
#include "../Source/core/Phi.h"

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

        // 100 Hz at 48 kHz crosses zero every 240 frames.
        const auto slow = makeSine (2, 48000, 100.0, 0.5);
        check (dsp::nearestZeroCrossing (*slow, 250, 100) == 240, "zero crossing: nearest one");
        check (std::abs (sampleAt (*slow, 0, dsp::nearestZeroCrossing (*slow, 1000, 200))) < 0.01f, "zero crossing: near silence");
        check (dsp::nearestZeroCrossing (*slow, 120, 10) == 110 || dsp::nearestZeroCrossing (*slow, 120, 10) == 130,
               "zero crossing: the quietest point when none is near");

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

    void testMetadata()
    {
        // One title for every schema; a differing ID3 title is kept as a conflict.
        Tags t;
        meta::absorb (t, meta::Schema::riff, { { "INAM", "Night Drive" }, { "IART", "Band" }, { "ISRC", "Studio A" } });
        meta::absorb (t, meta::Schema::id3, { { "TIT2", "Night Drive (Edit)" }, { "TPE1", "Band" }, { "TSRC", "USAB12600001" } });
        check (t[tag::title] == "Night Drive" && t[tag::artist] == "Band" && t[tag::isrc] == "USAB12600001", "metadata: common keys filled");
        check (t.count ("id3:TPE1") == 0 && t["id3:TIT2"] == "Night Drive (Edit)" && t["riff:ISRC"] == "Studio A", "metadata: only differences kept");
        check (meta::conflicts (t).size() == 1 && meta::conflicts (t)[0].key == "id3:TIT2", "metadata: one conflict");
        const auto* tit2 = meta::findId (meta::Schema::id3, "TIT2");
        const auto* dcTitle = meta::findId (meta::Schema::xmp, "dc:title");
        check (tit2 != nullptr && dcTitle != nullptr && meta::get (t, *dcTitle) == "Night Drive", "metadata: XMP follows the common title");
        meta::set (t, *tit2, "Night Drive");
        check (t.count ("id3:TIT2") == 0, "metadata: setting the common value removes the schema key");
        meta::set (t, *tit2, "Other");
        meta::sync (t);
        check (t.count ("id3:TIT2") == 0 && meta::conflicts (t).empty(), "metadata: sync removes conflicts");

        // Vorbis number and total make "3/12"; they come back out the same.
        Tags v;
        meta::absorb (v, meta::Schema::vorbis, { { "TRACKTOTAL", "12" }, { "TRACKNUMBER", "3" }, { "DISCNUMBER", "1" } });
        check (v[tag::track] == "3/12" && v[tag::disc] == "1" && v.count ("vorbis:TRACKTOTAL") == 0, "metadata: track number and total joined");
        const auto vorbis = meta::valuesFor (v, meta::Schema::vorbis);
        bool number = false, total = false;
        for (const auto& [id, value] : vorbis)
            number = number || (id == "TRACKNUMBER" && value == "3"), total = total || (id == "TRACKTOTAL" && value == "12");
        check (number && total, "metadata: track number and total written apart");
        meta::absorb (v, meta::Schema::mp4, { { "trkn", "3/12" } });
        check (v.count ("mp4:trkn") == 0, "metadata: the same track in MP4 is no conflict");

        // Unknown fields are kept and written back.
        meta::absorb (v, meta::Schema::id3, { { "TXXX:MOOD2", "calm" } });
        bool kept = false;
        for (const auto& [id, value] : meta::valuesFor (v, meta::Schema::id3))
            kept = kept || (id == "TXXX:MOOD2" && value == "calm");
        check (kept, "metadata: unknown frames kept");

        // Pictures
        meta::Picture pic;
        pic.data = std::string ("\xff\xd8\xff\xe0JFIF\0\x01", 10) + std::string (300, '\x7f');
        pic.description = "Front";
        meta::setPictures (v, { pic });
        const auto back = meta::pictures (v);
        check (back.size() == 1 && back[0].data == pic.data && back[0].mime == "image/jpeg" && back[0].type == 3 && back[0].description == "Front",
               "metadata: picture round trip");
        check (meta::base64Decode (meta::base64Encode ("ab")) == "ab" && meta::base64Encode ("abc") == "YWJj", "metadata: base64");

        // Templates
        check (meta::expand ("{track} {artist} - {title}{unknown}.{ext}", t, { { "ext", "flac" } }) == " Band - Night Drive{unknown}.flac",
               "metadata: template expansion");

        // CSV and JSON
        std::vector<meta::Row> rows { { "a.wav", { { tag::title, "Comma, \"Quote\"" }, { "id3:TIT2", "Line\nTwo" } } }, { "b.flac", { { tag::artist, "Ünï" } } } };
        std::string error;
        const auto csv = meta::fromCsv (meta::toCsv (rows), error);
        check (csv.size() == 2 && csv[0].file == "a.wav" && csv[0].tags == rows[0].tags && csv[1].tags == rows[1].tags, "metadata: CSV round trip " + error);
        const auto json = meta::fromJson (meta::toJson (rows), error);
        check (json.size() == 2 && json[0].tags == rows[0].tags && json[1].file == "b.flac" && json[1].tags == rows[1].tags, "metadata: JSON round trip " + error);
        meta::fromJson ("[{\"file\": \"x\", \"tags\": {\"title\": \"A\\u00e9\"}}]", error);
        check (meta::fromJson ("[{\"file\": \"x\", \"tags\": {\"title\": \"A\\u00e9\"}}]", error)[0].tags[tag::title] == "A\xc3\xa9", "metadata: JSON escapes");
    }

    void testXmp()
    {
        const std::string packet = R"(<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about="" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:xmpDM="http://ns.adobe.com/xmp/1.0/DynamicMedia/"
      xmlns:my="http://example.com/my/" xmlns:stEvt="http://ns.adobe.com/xap/1.0/sType/ResourceEvent#"
      xmlns:xmpMM="http://ns.adobe.com/xap/1.0/mm/" xmpDM:album="Album &amp; Co" my:flag="yes">
   <dc:title><rdf:Alt><rdf:li xml:lang="x-default">Night Drive</rdf:li></rdf:Alt></dc:title>
   <dc:creator><rdf:Seq><rdf:li>One</rdf:li><rdf:li>Two</rdf:li></rdf:Seq></dc:creator>
   <xmpDM:tempo>120</xmpDM:tempo>
   <xmpMM:History><rdf:Seq><rdf:li rdf:parseType="Resource"><stEvt:action>saved</stEvt:action></rdf:li></rdf:Seq></xmpMM:History>
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>)";
        Tags t;
        std::string error;
        check (xmp::read (packet, t, error), "xmp: parse " + error);
        check (t[tag::title] == "Night Drive" && t[tag::album] == "Album & Co" && t[tag::artist] == "One\nTwo" && t[tag::bpm] == "120",
               "xmp: values read");
        check (t["xmp:my:flag"] == "yes" && t["xmpns:my"] == "http://example.com/my/" && t.count ("xmpraw:xmpMM:History") == 1,
               "xmp: unknown namespace and a structure kept");

        t[tag::title] = "New Title";
        const auto written = xmp::write (t, "PHI");
        Tags again;
        check (xmp::read (written, again, error), "xmp: read what was written " + error);
        check (again[tag::title] == "New Title" && again[tag::artist] == "One\nTwo" && again["xmp:my:flag"] == "yes"
                   && again[tag::album] == "Album & Co" && again["xmpraw:xmpMM:History"].find ("saved") != std::string::npos,
               "xmp: round trip");
        check (written.find ("<rdf:Seq>") != std::string::npos && written.find ("xmlns:stEvt") != std::string::npos, "xmp: lists and structure namespaces written");
        Tags none;
        check (xmp::write (none, "PHI").empty() && ! xmp::read ("<a/>", none, error), "xmp: nothing to write, not XMP");
    }

    void testIxml()
    {
        const std::string doc = R"(<?xml version="1.0" encoding="UTF-8"?>
<BWFXML><IXML_VERSION>2.10</IXML_VERSION><PROJECT>Film</PROJECT><SCENE>12A</SCENE><TAKE>3</TAKE>
<SPEED><TIMECODE_RATE>25/1</TIMECODE_RATE></SPEED>
<TRACK_LIST><TRACK_COUNT>2</TRACK_COUNT><TRACK><CHANNEL_INDEX>1</CHANNEL_INDEX><NAME>Boom</NAME></TRACK>
<TRACK><CHANNEL_INDEX>2</CHANNEL_INDEX><NAME>Lav &lt;1&gt;</NAME></TRACK></TRACK_LIST><CUSTOM><THING>x</THING></CUSTOM></BWFXML>)";
        Tags t;
        std::string error;
        check (ixml::read (doc, t, error), "ixml: parse " + error);
        check (t["ixml:SCENE"] == "12A" && t["ixml:SPEED/TIMECODE_RATE"] == "25/1" && t["ixml:TRACK_LIST/TRACK[2]/NAME"] == "Lav <1>"
                   && t["ixml:TRACK_LIST/TRACK[1]/CHANNEL_INDEX"] == "1" && t["ixml:CUSTOM/THING"] == "x",
               "ixml: paths read");
        Tags again;
        check (ixml::read (ixml::write (t), again, error) && again == t, "ixml: round trip " + error);
    }

    uint32_t le32 (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    uint32_t be32 (const unsigned char* p) { return (uint32_t) p[3] | ((uint32_t) p[2] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[0] << 24); }

    std::string bytesOf (const std::string& path)
    {
        std::ifstream in (path, std::ios::binary);
        return std::string ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    }

    void testRiffChunks()
    {
        // A small WAV: fmt, an unknown chunk, data, an odd-sized chunk after it.
        const std::string path = "/tmp/phi-core-test.wav";
        const auto le32s = [] (uint32_t v) { std::string s; for (int i = 0; i < 4; ++i) s += (char) ((v >> (8 * i)) & 0xff); return s; };
        {
            std::string fmt = std::string ("\x01\x00\x01\x00", 4) + le32s (48000) + le32s (96000) + std::string ("\x02\x00\x10\x00", 4);
            std::string body = "WAVE";
            body += "fmt " + le32s (16) + fmt;
            body += "umid" + le32s (5) + "ABCDE" + '\0';
            body += "LIST" + le32s (16) + "INFO" + "INAM" + le32s (4) + "Old" + '\0';
            body += "data" + le32s (8) + std::string ("\x01\x02\x03\x04\x05\x06\x07\x08", 8);
            std::ofstream out (path, std::ios::binary | std::ios::trunc);
            out << "RIFF" << le32s ((uint32_t) body.size()) << body;
        }
        Tags read;
        riff::readMetadata (path, read);
        check (read[tag::title] == "Old" && read.count ("raw:wav:umid") == 1, "riff: INFO and an unknown chunk read");

        Tags t = read;
        t[tag::title] = "Night Drive";
        t[tag::comment] = "Line one\nLine two";
        t[tag::bwfOriginator] = "PHI";
        t[tag::bwfTimeReference] = "5000000000";
        t[tag::bwfLoudness] = "-14.00";
        t[tag::bwfTruePeak] = "-1.00";
        t[tag::bwfUmid] = "060a2b34";
        t[tag::bwfCodingHistory] = "A=PCM,F=48000,W=16,M=mono";
        t["cart:CutID"] = "C1234";
        t["cart:Timer.SEG1"] = "48000";
        t["cart:Timers"] = "MRK =96000";
        t["ixml:SCENE"] = "4";
        t["xmp:xmpDM:scene"] = "Scene 4";
        riff::Write w;
        w.software = "PHI";
        w.id3 = std::string ("ID3\x04\x00\x00\x00\x00\x00\x00", 10);
        std::string error;
        check (riff::writeMetadata (path, t, w, error), "riff: write " + error);

        const auto layout = riff::scan (path);
        std::vector<std::string> ids;
        for (const auto& c : layout.chunks)
            ids.push_back (c.id);
        const auto at = [&] (const char* id) { return std::find (ids.begin(), ids.end(), id) - ids.begin(); };
        check (layout.valid && at ("bext") < at ("data") && at ("cart") < at ("data") && at ("iXML") < at ("data") && at ("_PMX") < at ("data")
                   && at ("id3 ") < at ("data") && at ("umid") < (long) ids.size() && std::count (ids.begin(), ids.end(), "LIST") == 1,
               "riff: metadata chunks before the audio, the unknown chunk kept, one INFO list");
        const auto bytes = bytesOf (path);
        check (bytes.size() == le32 ((const unsigned char*) bytes.data() + 4) + 8, "riff: RIFF size updated");
        check (bytes.find (std::string ("\x01\x02\x03\x04\x05\x06\x07\x08", 8)) != std::string::npos, "riff: audio kept");

        Tags back;
        const auto id3 = riff::readMetadata (path, back);
        check (id3 == w.id3, "riff: ID3 chunk bytes returned");
        check (back[tag::title] == "Night Drive" && back[tag::comment] == "Line one\nLine two", "riff: INFO round trip");
        check (back[tag::bwfOriginator] == "PHI" && back[tag::bwfTimeReference] == "5000000000" && back[tag::bwfLoudness] == "-14.00"
                   && back[tag::bwfTruePeak] == "-1.00" && back.count (tag::bwfLoudnessRange) == 0 && back[tag::bwfCodingHistory] == "A=PCM,F=48000,W=16,M=mono"
                   && back[tag::bwfUmid].compare (0, 8, "060a2b34") == 0 && back.count (tag::bwfDescription) == 0,
               "riff: bext round trip (the description follows the comment)");
        check (back["cart:CutID"] == "C1234" && back["cart:Timer.SEG1"] == "48000" && back["cart:Timers"] == "MRK =96000" && back.count ("cart:Title") == 0,
               "riff: cart round trip");
        check (back["ixml:SCENE"] == "4" && back["xmp:xmpDM:scene"] == "Scene 4" && back["raw:wav:umid"] == read["raw:wav:umid"], "riff: iXML, XMP and raw chunk round trip");

        // AIFF: text chunks and XMP.
        const std::string aiff = "/tmp/phi-core-test.aiff";
        const auto be32s = [] (uint32_t v) { std::string s; for (int i = 3; i >= 0; --i) s += (char) ((v >> (8 * i)) & 0xff); return s; };
        {
            std::string body = "AIFF";
            body += "COMM" + be32s (18) + std::string (18, '\x01');
            body += "SSND" + be32s (12) + std::string (12, '\x02');
            std::ofstream out (aiff, std::ios::binary | std::ios::trunc);
            out << "FORM" << be32s ((uint32_t) body.size()) << body;
        }
        check (riff::writeMetadata (aiff, t, w, error), "aiff: write " + error);
        Tags fromAiff;
        riff::readMetadata (aiff, fromAiff);
        const auto aiffBytes = bytesOf (aiff);
        check (fromAiff[tag::title] == "Night Drive" && fromAiff["xmp:xmpDM:scene"] == "Scene 4" && fromAiff[tag::comment] == "Line one\nLine two"
                   && be32 ((const unsigned char*) aiffBytes.data() + 4) + 8 == aiffBytes.size() && aiffBytes.find (std::string (12, '\x02')) != std::string::npos,
               "aiff: metadata round trip");
        std::remove (path.c_str());
        std::remove (aiff.c_str());
    }

    void testNyquist()
    {
        const std::string share = PHI_SHARE_SOURCE_DIR;
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

    void testMidSide()
    {
        const double rate = 48000.0;
        const int n = 48000;
        auto run = [&] (midside::Settings s, auto&& source, std::vector<float>& l, std::vector<float>& r)
        {
            midside::Processor p;
            p.prepare (rate);
            p.set (s);
            l.assign ((size_t) n, 0.0f);
            r.assign ((size_t) n, 0.0f);
            for (int i = 0; i < n; ++i)
            {
                auto [a, b] = source (i);
                p.process (a, b);
                l[(size_t) i] = a;
                r[(size_t) i] = b;
            }
            return p.correlation();
        };
        auto tone = [&] (double hz, int i) { return (float) (0.5 * std::sin (2.0 * M_PI * hz * i / rate)); };
        auto rms = [] (const std::vector<float>& x, size_t from)
        {
            double sum = 0;
            for (size_t i = from; i < x.size(); ++i)
                sum += (double) x[i] * x[i];
            return std::sqrt (sum / (double) (x.size() - from));
        };
        std::vector<float> l, r;

        // Unchanged settings leave the audio alone.
        midside::Settings plain;
        plain.matchLevel = false;
        run (plain, [&] (int i) { return std::pair { tone (440, i), tone (660, i) }; }, l, r);
        double worst = 0;
        for (int i = 0; i < n; ++i)
            worst = std::max ({ worst, (double) std::abs (l[(size_t) i] - tone (440, i)), (double) std::abs (r[(size_t) i] - tone (660, i)) });
        check (worst < 1.0e-6, "mid/side: unchanged settings pass the audio through");

        // 0 % width is mono.
        auto mono = plain;
        mono.width = 0.0;
        run (mono, [&] (int i) { return std::pair { tone (440, i), tone (660, i) }; }, l, r);
        worst = 0;
        for (int i = n / 10; i < n; ++i)
            worst = std::max (worst, (double) std::abs (l[(size_t) i] - r[(size_t) i]));
        check (worst < 1.0e-5, "mid/side: 0 % width makes both sides the same");

        // Mono below 150 Hz takes a 50 Hz side away and keeps a 2 kHz one.
        auto tight = plain;
        tight.monoBelowHz = 150.0;
        run (tight, [&] (int i) { return std::pair { tone (50, i), -tone (50, i) }; }, l, r);
        check (rms (l, (size_t) n / 2) < 0.354 * 0.05, "mid/side: side below the crossover is removed");
        run (tight, [&] (int i) { return std::pair { tone (2000, i), -tone (2000, i) }; }, l, r);
        check (near (rms (l, (size_t) n / 2), 0.354, 0.02), "mid/side: side above the crossover is kept");

        // Made-up width from a mono source cancels when summed back to mono.
        auto generate = plain;
        generate.generate = 1.0;
        run (generate, [&] (int i) { return std::pair { tone (1000, i), tone (1000, i) }; }, l, r);
        worst = 0;
        double apart = 0;
        for (int i = 0; i < n; ++i)
        {
            worst = std::max (worst, (double) std::abs (0.5f * (l[(size_t) i] + r[(size_t) i]) - tone (1000, i)));
            apart = std::max (apart, (double) std::abs (l[(size_t) i] - r[(size_t) i]));
        }
        check (worst < 1.0e-5 && apart > 0.1, "mid/side: generated sides widen a mono sound and cancel in mono");

        // Rotating a centred sound by 45 degrees puts it on the right.
        auto turn = plain;
        turn.rotation = 45.0;
        run (turn, [&] (int i) { return std::pair { tone (440, i), tone (440, i) }; }, l, r);
        check (rms (l, 0) < 1.0e-5 && near (rms (r, 0), 0.5, 0.01), "mid/side: 45 degree rotation moves the centre to the right");

        // Level match keeps a widened sound about as loud as it was.
        auto wide = midside::Settings {};
        wide.width = 2.0;
        run (wide, [&] (int i) { return std::pair { tone (440, i), tone (660, i) }; }, l, r);
        const auto in = 0.354;
        check (near (std::sqrt (0.5 * (rms (l, (size_t) n / 2) * rms (l, (size_t) n / 2) + rms (r, (size_t) n / 2) * rms (r, (size_t) n / 2))), in, 0.03),
               "mid/side: level match keeps the loudness");

        // Huge pulls back while the image is out of phase.
        auto huge = plain;
        huge.huge = true;
        huge.width = 3.0;
        const auto corr = run (huge, [&] (int i) { return std::pair { tone (440, i) + 0.6f * tone (660, i), tone (440, i) - 0.6f * tone (660, i) }; }, l, r);
        check (corr > -0.4, "mid/side: huge backs off when the correlation falls");

        // Readouts.
        AudioData::Builder b (2, rate);
        std::vector<float> a ((size_t) n), c ((size_t) n);
        for (int i = 0; i < n; ++i)
            a[(size_t) i] = c[(size_t) i] = tone (440, i);
        const float* p[2] { a.data(), c.data() };
        b.append (p, n);
        const auto same = b.finish();
        const auto same1 = midside::analyse (*same, 0, n);
        check (near (same1.correlation, 1.0, 1.0e-6) && near (same1.monoSumDb, 0.0, 1.0e-6), "mid/side: identical sides read +1 and no mono loss");
        for (auto& x : c)
            x = -x;
        AudioData::Builder b2 (2, rate);
        b2.append (p, n);
        const auto anti = midside::analyse (*b2.finish(), 0, n);
        check (near (anti.correlation, -1.0, 1.0e-6) && anti.monoSumDb < -60.0, "mid/side: opposite sides read -1 and vanish in mono");

        const auto rendered = midside::render (*same, 1000, 20000, wide);
        check (rendered->numChannels() == 2 && rendered->length() == n, "mid/side: render keeps the length, two channels");
    }

    void testSampler()
    {
        const double rate = 48000.0;
        // One second of 100 Hz.
        AudioData::Builder b (1, rate);
        std::vector<float> x (48000);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) (0.5 * std::sin (2.0 * M_PI * 100.0 * (double) i / rate));
        const float* p[1] { x.data() };
        b.append (p, (int) x.size());
        const auto sound = b.finish();

        sampler::Engine e;
        e.prepare (rate);
        e.setSample (sampler::makeSample (*sound));
        sampler::Settings s;
        s.attack = 0.0;
        s.decay = 0.0;
        s.sustain = 1.0;
        s.release = 0.01;
        s.velocity = 0.0;
        s.loop = false;
        e.setSettings (s);

        auto play = [&] (int frames)
        {
            std::vector<float> l ((size_t) frames, 0.0f), r ((size_t) frames, 0.0f);
            for (int done = 0; done < frames; done += 512)
            {
                const auto n = std::min (512, frames - done);
                e.render (l.data() + done, r.data() + done, n);
            }
            return std::pair { l, r };
        };
        auto crossings = [] (const std::vector<float>& v, size_t from, size_t to)
        {
            int n = 0;
            for (size_t i = from + 1; i < to; ++i)
                n += (v[i - 1] < 0.0f) != (v[i] < 0.0f) ? 1 : 0;
            return n;
        };

        // The root note plays the sample as it is.
        e.noteOn (60, 1.0f);
        auto [l, r] = play (24000);
        double worst = 0;
        for (size_t i = 0; i < l.size(); ++i)
            worst = std::max ({ worst, (double) std::abs (l[i] - x[i]), (double) std::abs (r[i] - x[i]) });
        check (worst < 1.0e-5, "sampler: the root note plays the sample unchanged");
        e.panic();

        // An octave up plays twice as fast (and ends after half a second).
        e.noteOn (72, 1.0f);
        auto [up, upR] = play (48000);
        check (near (crossings (up, 0, 24000), 200, 2), "sampler: an octave up is twice the pitch");
        check (crossings (up, 24100, 48000) == 0, "sampler: the note ends with the sample");

        // Note-off releases a Classic voice.
        e.noteOn (60, 1.0f);
        play (4800);
        e.noteOff (60);
        auto [rel, relR] = play (9600);
        check (std::abs (rel.back()) < 1.0e-3 && ! e.isSounding (60), "sampler: note-off releases");

        // Looping keeps a held note going past the end of the sample.
        s.loop = true;
        s.loopStart = 24000;
        s.loopEnd = 48000;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        auto [held, heldR] = play (96000);
        check (crossings (held, 80000, 96000) > 60, "sampler: a held looped note keeps sounding");
        e.panic();

        // 1-Shot plays through even after note-off.
        s.mode = sampler::Mode::oneShot;
        s.loop = false;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        play (480);
        e.noteOff (60);
        auto [shot, shotR] = play (24000);
        check (crossings (shot, 12000, 24000) > 45, "sampler: 1-Shot ignores note-off");
        e.panic();

        // Velocity scales the level when it is turned up; voices are limited.
        s.mode = sampler::Mode::classic;
        s.velocity = 1.0;
        s.voices = 2;
        e.setSettings (s);
        e.noteOn (60, 0.5f);
        auto [soft, softR] = play (4800);
        double peak = 0;
        for (const auto v : soft)
            peak = std::max (peak, (double) std::abs (v));
        check (near (peak, 0.5 * 0.25, 0.01), "sampler: velocity 0.5 plays at a quarter level (squared)");
        e.noteOn (62, 1.0f);
        e.noteOn (64, 1.0f);
        play (512);
        check (! e.isSounding (60) && e.isSounding (62) && e.isSounding (64), "sampler: the oldest voice is taken when all are busy");
    }


    // A standard MIDI file made byte by byte.
    struct SmfWriter
    {
        std::vector<uint8_t> bytes;
        void be (uint32_t v, int n) { for (int i = n - 1; i >= 0; --i) bytes.push_back ((uint8_t) (v >> (8 * i))); }
        void header (int format, int tracks, int division)
        {
            bytes.insert (bytes.end(), { 'M', 'T', 'h', 'd' });
            be (6, 4); be ((uint32_t) format, 2); be ((uint32_t) tracks, 2); be ((uint32_t) division, 2);
        }
        void track (const std::vector<uint8_t>& body)
        {
            bytes.insert (bytes.end(), { 'M', 'T', 'r', 'k' });
            be ((uint32_t) body.size(), 4);
            bytes.insert (bytes.end(), body.begin(), body.end());
        }
    };

    void testMidiFile()
    {
        SmfWriter w;
        w.header (1, 2, 480);
        // Track 1: 120 BPM, then 60 BPM at beat 2.
        w.track ({ 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
                   0x83, 0x60, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40,
                   0x00, 0xFF, 0x2F, 0x00 });
        // Track 2: a name, C4 on at 0, off by running status (velocity 0) at 480, a kick on channel 10
        // at 480, and E4 at 480 on channel 1 (so the note-off goes first at that tick).
        w.track ({ 0x00, 0xFF, 0x03, 0x04, 'L', 'e', 'a', 'd',
                   0x00, 0x90, 60, 100,
                   0x83, 0x60, 60, 0,
                   0x00, 0x99, 36, 110,
                   0x00, 0x90, 64, 90,
                   0x87, 0x40, 0x80, 64, 0,
                   0x00, 0xFF, 0x2F, 0x00 });
        std::string error;
        auto song = midifile::parse (w.bytes.data(), w.bytes.size(), error);
        check (song != nullptr && error.empty(), "midi file: a type 1 file reads");
        if (song == nullptr)
            return;
        check (song->format == 1 && song->ticksPerQuarter == 480 && song->trackNames.size() == 2 && song->trackNames[1] == "Lead",
               "midi file: header and track names");
        check (song->events.size() == 5 && song->noteCount() == 3, "midi file: channel events, velocity 0 is a note-off");
        check (song->events.size() == 5 && song->events[1].kind() == 0x80 && song->events[1].tick == 480,
               "midi file: at the same tick a note-off comes before the note-ons");
        check (song->channelsUsed == ((1u << 0) | (1u << 9)), "midi file: channels with notes");
        check (near (song->fileBpm(), 120.0, 1.0e-9) && near (song->seconds (480), 0.5, 1.0e-9) && near (song->seconds (1440), 2.5, 1.0e-9),
               "midi file: the tempo map (120 then 60 BPM)");
        check (near (song->seconds (1440, 90.0), 2.0, 1.0e-9), "midi file: played at a fixed tempo");
        check (song->lastTick == 1440, "midi file: length to the last event");

        const auto clip = mixer::placeMidi (song, "", 48000.0, 0.0);
        check (clip->events.size() == 5 && clip->events[1].frame == 24000 && clip->events.back().frame == 120000 && clip->length == 120000,
               "midi file: events placed at frames through the tempo map");

        const uint8_t junk[] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0 };
        check (midifile::parse (junk, sizeof junk, error) == nullptr && ! error.empty(), "midi file: not a MIDI file is refused");
        // Cut short in the middle of a track: what was read is kept.
        auto cut = w.bytes;
        cut.resize (cut.size() - 8);
        check (midifile::parse (cut.data(), cut.size(), error) != nullptr, "midi file: a truncated file still reads");
    }

    // An instrument that writes down what it was sent and plays a constant level while a note is held.
    struct ProbeInstrument final : mixer::Instrument
    {
        std::vector<std::pair<Frame, mixer::NoteEvent>> heard;
        Frame clock = 0;
        int sounding = 0;
        Kind kind() const override { return Kind::synth; }
        std::string name() const override { return "Probe"; }
        void prepare (double, int) override {}
        void process (const mixer::NoteEvent* e, int n, float* left, float* right, int frames) override
        {
            int k = 0;
            for (int i = 0; i < frames; ++i)
            {
                while (k < n && e[k].offset <= i)
                {
                    heard.push_back ({ clock + i, e[k] });
                    if ((e[k].status & 0xF0) == 0x90 && e[k].data2 > 0)
                        ++sounding;
                    else if ((e[k].status & 0xF0) == 0x80 || (e[k].status & 0xF0) == 0x90)
                        sounding = std::max (0, sounding - 1);
                    ++k;
                }
                left[i] += sounding > 0 ? 0.25f : 0.0f;
                right[i] += sounding > 0 ? 0.25f : 0.0f;
            }
            clock += frames;
        }
        void releaseAll() override {}
        void reset() override { sounding = 0; }
    };

    void testMixer()
    {
        const double rate = 48000.0;
        mixer::Engine m;
        m.prepare (rate);
        m.setNumTracks (2);

        // A mono clip of 0.5, centred: equal power, -3 dB on each side.
        AudioData::Builder b (1, rate);
        std::vector<float> half (4800, 0.5f);
        const float* one[1] = { half.data() };
        b.append (one, 4800);
        const auto dc = b.finish();
        const auto dcClip = mixer::makeAudioClip (*dc, "dc.wav");
        m.setClips (0, { mixer::makeClip (dcClip, 0) });
        check (m.length() == 4800, "mixer: length from the clips");

        std::vector<float> l (512), r (512);
        auto render = [&] (int n)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            m.render (l.data(), r.data(), n);
        };
        m.play();
        render (512);
        check (near (l[100], 0.5 * std::sqrt (0.5), 1.0e-4) && near (r[100], 0.5 * std::sqrt (0.5), 1.0e-4), "mixer: mono audio is equal-power panned");
        check (m.position() == 512, "mixer: the transport moves while playing");

        m.strip (0).pan = -1.0f;
        render (512);
        render (512);
        check (near (l[100], 0.5, 1.0e-4) && near (r[100], 0.0, 1.0e-4), "mixer: hard left");
        m.strip (0).pan = 0.0f;
        m.strip (0).faderDb = -6.0206f;
        m.strip (0).invertLeft = true;
        render (512);
        render (512);
        check (near (l[100], -0.25 * std::sqrt (0.5), 1.0e-4) && near (r[100], 0.25 * std::sqrt (0.5), 1.0e-4), "mixer: fader and polarity");
        m.strip (0).faderDb = 0.0f;
        m.strip (0).invertLeft = false;
        m.strip (1).solo = true;
        render (512);
        render (512);
        check (std::abs (l[100]) < 1.0e-6, "mixer: soloing another track mutes this one");
        m.strip (1).solo = false;
        m.strip (0).mute = true;
        render (512);
        render (512);
        check (std::abs (l[100]) < 1.0e-6, "mixer: mute");
        m.strip (0).mute = false;
        render (512);
        render (512);
        check (! m.isPlaying(), "mixer: the transport stops at the end");

        // Track 2: a MIDI clip and an instrument, with the audio clip on the same track too.
        SmfWriter w;
        w.header (0, 1, 480);
        // At 120 BPM: C4 at tick 100 (5000 frames), off at 580 (29000); D4 on channel 2 at 580; drums at 580.
        w.track ({ 0x64, 0x90, 60, 100, 0x83, 0x60, 0x80, 60, 0, 0x00, 0x91, 62, 100, 0x00, 0x99, 36, 100,
                   0x83, 0x60, 0x81, 62, 0, 0x00, 0x89, 36, 0, 0x00, 0xFF, 0x2F, 0x00 });
        std::string error;
        auto song = midifile::parse (w.bytes.data(), w.bytes.size(), error);
        auto probe = std::make_shared<ProbeInstrument>();
        const auto songClip = mixer::placeMidi (song, "song.mid", rate, 0.0);
        m.setInstrument (1, probe);
        m.setClips (1, { mixer::makeClip (songClip, 0), mixer::makeClip (dcClip, 0) });   // overlapping: both heard
        m.strip (1).transpose = 12;
        m.strip (1).channels = 0xFFFF & ~(1u << 1);   // channel 2 left out
        m.setPosition (0);
        m.play();
        for (int i = 0; i < 100; ++i)
            render (480);
        bool atFrame = probe->heard.size() >= 2 && probe->heard[0].first == 5000 && probe->heard[0].second.data1 == 72
                       && probe->heard[1].first == 29000 && probe->heard[1].second.data1 == 72 && (probe->heard[1].second.status & 0xF0) == 0x80;
        check (atFrame, "mixer: MIDI clip notes play at their frames, transposed, and end with the transposed note");
        const bool drumsKept = std::any_of (probe->heard.begin(), probe->heard.end(), [] (const auto& h) { return h.second.status == 0x99 && h.second.data1 == 36; });
        const bool channelOut = std::none_of (probe->heard.begin(), probe->heard.end(), [] (const auto& h) { return (h.second.status & 0x0F) == 1; });
        check (drumsKept && channelOut, "mixer: drums are not transposed; a channel turned off is not played");

        // A note held when the transport jumps is let go.
        probe->heard.clear();
        m.setPosition (6000);
        m.play();
        render (480);
        m.setPosition (1000);
        render (480);
        check (probe->heard.empty() || std::all_of (probe->heard.begin(), probe->heard.end(), [] (const auto& h) { return (h.second.status & 0xF0) == 0x80; }),
               "mixer: jumping into the middle of a note does not start it");
        probe->heard.clear();
        m.setPosition (5000 - 100);
        render (480);
        m.setPosition (20000);
        render (480);
        check (probe->heard.size() == 2 && (probe->heard[1].second.status & 0xF0) == 0x80 && probe->heard[1].second.data1 == 72,
               "mixer: a note held across a jump is let go");
        m.stop();

        // Live MIDI plays the armed track's instrument while stopped.
        probe->heard.clear();
        m.setLiveTrack (1);
        m.liveMidi (0x90, 50, 100);
        render (480);
        check (probe->heard.size() == 1 && probe->heard[0].second.data1 == 50 && l[10] > 0.1f, "mixer: a MIDI keyboard plays the armed track");
        m.liveMidi (0x80, 50, 0);
        render (480);
        render (480);
        auto meterTrack = m.takeMeter (1), meterMaster = m.takeMeter (mixer::masterIndex);
        check (meterTrack.peak[0] > 0.1f && meterMaster.peak[0] > 0.1f && m.takeMeter (1).peak[0] == 0.0f, "mixer: meters, taken once");

        // Mixdown: the whole mix from the start, audio and instrument both heard.
        auto mix = m.mixdown (2.0, error);
        check (mix != nullptr && mix->numChannels() == 2 && mix->length() >= 53000 && mix->length() <= 53000 + 96000,
               "mixer: mixdown covers the clips and stops after the tail");
        if (mix != nullptr)
        {
            float a[1], c[1];
            float* pa[2] = { a, c };
            mix->read (100, 1, pa);
            const auto audioOnly = 2.0f * 0.5f * (float) std::sqrt (0.5);   // both tracks' clips at 100
            check (near (a[0], audioOnly, 1.0e-3), "mixer: mixdown sums the tracks");
            mix->read (6000, 1, pa);
            check (near (a[0], 0.25f, 1.0e-3), "mixer: mixdown plays the MIDI clip into the instrument");
        }

        // Bounce: one track alone, before its fader and pan, whatever mute and solo say.
        m.strip (0).mute = true;
        m.strip (0).faderDb = -12.0f;
        m.strip (0).pan = -1.0f;
        m.strip (1).solo = true;
        auto bounced = m.mixdown (2.0, error, nullptr, nullptr, 0);
        check (bounced != nullptr && bounced->length() >= 4800 && bounced->length() < 53000,
               "mixer: a bounce ends with its own track");
        if (bounced != nullptr && bounced->length() > 100)
        {
            float a[1], c[1];
            float* pa[2] = { a, c };
            bounced->read (100, 1, pa);
            check (near (a[0], 0.5f, 1.0e-3) && near (c[0], 0.5f, 1.0e-3), "mixer: a bounce is the track alone, pre-fader, unpanned");
        }
        m.strip (0).mute = false;
        m.strip (0).faderDb = 0.0f;
        m.strip (0).pan = 0.0f;
        m.strip (1).solo = false;
        auto again = m.mixdown (2.0, error);
        check (again != nullptr && again->length() >= 53000, "mixer: after a bounce the mixdown is the whole mix again");

        // Clips one after another on one track: audio, then a trimmed MIDI clip, then audio again.
        {
            mixer::Engine t;
            t.prepare (rate);
            t.setNumTracks (1);
            auto p2 = std::make_shared<ProbeInstrument>();
            t.setInstrument (0, p2);
            auto midiPart = mixer::makeClip (songClip, 10000);
            midiPart.offset = 4000;    // C4 (at 5000 in the file) starts 1000 frames in, at 11000
            midiPart.length = 20000;   // cut before its note-off at 29000: let go at 30000
            auto second = mixer::makeClip (dcClip, 40000);
            t.setClips (0, { second, mixer::makeClip (dcClip, 0), midiPart });
            const auto list = t.clips (0);
            check (list.size() == 3 && list[0].start == 0 && list[1].start == 10000 && list[2].start == 40000, "mixer: a track's clips are kept in order");
            check (t.length() == 44800 && t.trackEnd (0) == 44800, "mixer: the track ends with its last clip");
            std::vector<float> a (480), b (480);
            std::vector<float> heardL;
            t.play();
            for (int i = 0; i < 100; ++i)
            {
                std::fill (a.begin(), a.end(), 0.0f);
                std::fill (b.begin(), b.end(), 0.0f);
                t.render (a.data(), b.data(), 480);
                heardL.insert (heardL.end(), a.begin(), a.end());
            }
            const auto c4 = std::find_if (p2->heard.begin(), p2->heard.end(), [] (const auto& h) { return h.second.status == 0x90 && h.second.data1 == 60; });
            const auto c4off = std::find_if (p2->heard.begin(), p2->heard.end(), [] (const auto& h) { return (h.second.status & 0xF0) == 0x80 && h.second.data1 == 60; });
            check (c4 != p2->heard.end() && c4->first == 11000, "mixer: a MIDI clip plays from its trimmed start at its place");
            check (c4off != p2->heard.end() && c4off->first == 30000, "mixer: a MIDI clip cut short lets go of its notes at its end");
            check (near (heardL[100], 0.5 * std::sqrt (0.5), 1.0e-4) && std::abs (heardL[7000]) < 1.0e-6 && near (heardL[40100], 0.5 * std::sqrt (0.5), 1.0e-4),
                   "mixer: audio clips play at their places, silence between");
            check (std::abs (heardL[0]) < 0.1f, "mixer: a clip fades in over its first frames");

            // Jumping back before a clip that already played plays it again.
            p2->heard.clear();
            t.setPosition (10500);
            t.play();
            for (int i = 0; i < 4; ++i)
                t.render (a.data(), b.data(), 480);
            check (! p2->heard.empty() && p2->heard[0].second.data1 == 60, "mixer: after a jump back a clip plays again");

            // A muted clip is silent.
            t.setInstrument (0, nullptr);
            auto muted = t.clips (0);
            muted[0].muted = true;
            t.setClips (0, muted);
            t.setPosition (0);
            t.play();
            std::fill (a.begin(), a.end(), 0.0f);
            t.render (a.data(), b.data(), 480);
            check (std::abs (a[200]) < 1.0e-6, "mixer: a muted clip is silent");
        }

        // Reversed: an audio clip plays from its end; a MIDI file's notes run backwards.
        {
            mixer::Engine t;
            t.prepare (rate);
            t.setNumTracks (1);
            AudioData::Builder rb (1, rate);
            std::vector<float> ramp (1000);
            for (size_t k = 0; k < ramp.size(); ++k)
                ramp[k] = (float) k / 1000.0f;
            const float* rp[1] = { ramp.data() };
            rb.append (rp, 1000);
            auto clip = mixer::makeClip (mixer::makeAudioClip (*rb.finish(), "ramp.wav"), 0);
            clip.reversed = true;
            clip.offset = 100;    // in the reversed file: frames 899 down to 0 play
            clip.length = 900;
            t.setClips (0, { clip });
            std::vector<float> a (512, 0.0f), b (512, 0.0f);
            t.play();
            t.render (a.data(), b.data(), 512);
            check (near (a[200] / std::sqrt (0.5), (899 - 200) / 1000.0, 1.0e-4), "mixer: a reversed audio clip plays backwards from its trimmed start");

            mixer::MidiClip m;
            m.length = 1000;
            m.events = { { 0, 0xC0, 5, 0 }, { 100, 0x90, 60, 90 }, { 300, 0x80, 60, 0 }, { 500, 0x90, 64, 70 }, { 900, 0x80, 64, 0 } };
            const auto r = mixer::reverseMidi (m);
            const bool order = r->events.size() == 5 && r->events[0].status == 0xC0 && r->events[0].frame == 0
                               && r->events[1].frame == 100 && r->events[1].status == 0x90 && r->events[1].data1 == 64 && r->events[1].data2 == 70
                               && r->events[2].frame == 500 && r->events[2].status == 0x80 && r->events[2].data1 == 64
                               && r->events[3].frame == 700 && r->events[3].status == 0x90 && r->events[3].data1 == 60 && r->events[4].frame == 900;
            check (order, "mixer: a reversed MIDI file plays its notes backwards, each as long and as loud");
            const auto twice = mixer::reverseMidi (*r);
            check (twice->events.size() == 5 && twice->events[1].frame == 100 && twice->events[1].data1 == 60, "mixer: reversing a MIDI file twice gives it back");
        }

        // The built-in Synth: a note sounds and dies after its release; a drum rings out by itself.
        mixer::Synth synth;
        synth.prepare (rate, 256);
        std::vector<float> sl (4800, 0.0f), sr (4800, 0.0f);
        const mixer::NoteEvent on { 0, 0x90, 69, 100 };
        synth.process (&on, 1, sl.data(), sr.data(), 4800);
        float peak = 0.0f;
        for (auto v : sl)
            peak = std::max (peak, std::abs (v));
        check (peak > 0.05f, "synth: a note sounds");
        const mixer::NoteEvent off { 0, 0x80, 69, 0 };
        std::fill (sl.begin(), sl.end(), 0.0f);
        synth.process (&off, 1, sl.data(), sr.data(), 4800);
        for (int i = 0; i < 20; ++i)
        {
            std::fill (sl.begin(), sl.end(), 0.0f);
            synth.process (nullptr, 0, sl.data(), sr.data(), 4800);
        }
        peak = 0.0f;
        for (auto v : sl)
            peak = std::max (peak, std::abs (v));
        check (peak < 1.0e-3f, "synth: the note ends after its release");
        const mixer::NoteEvent kick { 0, 0x99, 36, 120 };
        std::fill (sl.begin(), sl.end(), 0.0f);
        synth.process (&kick, 1, sl.data(), sr.data(), 4800);
        peak = 0.0f;
        for (auto v : sl)
            peak = std::max (peak, std::abs (v));
        check (peak > 0.1f, "synth: channel 10 plays drums");
        mixer::Synth copy;
        auto settings = synth.settings();
        settings.wave = mixer::SynthSettings::square;
        settings.cutoff = 1234.0;
        synth.setSettings (settings);
        copy.setState (synth.state());
        check (copy.settings().wave == mixer::SynthSettings::square && near (copy.settings().cutoff, 1234.0, 1.0e-9), "synth: settings saved and restored");
    }

    // The clip launcher: launches on the quantise, loops, one-shots, stops, scenes and the timeline hand-back.
    void testLauncher()
    {
        const double rate = 48000.0;
        mixer::Engine m;
        m.prepare (rate);
        m.setNumTracks (2);
        m.tempo = 120.0;            // a beat is 24000 frames
        m.launchQuantise = 1.0;

        auto dcOf = [&] (float level, int frames)
        {
            AudioData::Builder b (1, rate);
            std::vector<float> v ((size_t) frames, level);
            const float* one[1] = { v.data() };
            b.append (one, frames);
            return mixer::makeAudioClip (*b.finish(), "dc.wav");
        };
        auto slotOf = [] (mixer::AudioClipPtr a, bool loop = true, double quantise = -1.0)
        {
            auto s = std::make_shared<mixer::Slot>();
            s->clip = mixer::makeClip (std::move (a), 0);
            s->loop = loop;
            s->quantise = quantise;
            return mixer::SlotPtr (s);
        };
        // Renders in blocks of 256, as a sound card would; returns the left side.
        auto run = [&] (int frames)
        {
            std::vector<float> out ((size_t) frames), l (256), r (256);
            for (int done = 0; done < frames; done += 256)
            {
                const auto n = std::min (256, frames - done);
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                m.render (l.data(), r.data(), n);
                std::copy (l.begin(), l.begin() + n, out.begin() + done);
            }
            return out;
        };
        const auto centre = 0.70710678f;   // a mono clip, centred: equal power

        m.setSlot (0, 0, slotOf (dcOf (0.5f, 12000)));
        m.setSlot (0, 1, slotOf (dcOf (0.25f, 6000)));
        check (m.slot (0, 0) != nullptr && m.slot (0, 2) == nullptr, "launcher: slots set and empty");

        // Stopped: a launch starts the transport and plays at once.
        m.launch (0, 0);
        check (m.isPlaying(), "launcher: a launch starts the transport");
        auto out = run (30000);
        auto info = m.launchInfo (0);
        check (info.playing == 0 && ! info.timeline, "launcher: the slot plays and the track leaves its timeline");
        check (near (out[500], 0.5f * centre, 1.0e-4) && near (out[12500], 0.5f * centre, 1.0e-4) && near (out[24500], 0.5f * centre, 1.0e-4),
               "launcher: the clip loops");
        check (info.passes == 2 && m.launchClock() == 30000, "launcher: passes counted on the launch clock");
        check (m.length() == 0 && m.isPlaying(), "launcher: the transport keeps going past the end of an empty timeline");

        // Another slot waits for the next beat (48000) and starts on that very frame.
        m.launch (0, 1);
        out = run (18000 - 200);   // to 47800
        check (m.launchInfo (0).queued == 1 && m.launchInfo (0).playing == 0, "launcher: the launch waits for the quantise");
        out = run (1000);          // 47800 .. 48800
        check (near (out[100], 0.5f * centre, 1.0e-4) && near (out[600], 0.25f * centre, 1.0e-4), "launcher: the switch is on the beat");
        check (m.launchInfo (0).playing == 1 && m.launchInfo (0).queued == -1, "launcher: the waiting slot plays");

        // Stop at the next beat (72000): silent, and still off the timeline.
        m.stopTrack (0);
        out = run (72000 - 48800 + 500);
        check (std::abs (out.back()) < 1.0e-6f && m.launchInfo (0).playing == -1 && ! m.launchInfo (0).timeline, "launcher: a stop at the quantise");
        check (! m.isPlaying(), "launcher: with nothing launched and no timeline, the transport stops");

        // A clip that plays once stops the track at its end; its own quantise of 0 launches at once.
        m.setSlot (1, 0, slotOf (dcOf (0.5f, 5000), false, 0.0));
        m.launch (1, 0);
        out = run (4000);
        check (m.launchInfo (1).playing == 0, "launcher: a one-shot plays");
        out = run (2000);
        check (m.launchInfo (1).playing == -1 && std::abs (out.back()) < 1.0e-6f, "launcher: a one-shot stops at its end");

        // Back to the timeline: the track's own clips are heard again.
        m.stop();
        run (256);
        m.setClips (0, { mixer::makeClip (dcOf (0.125f, 96000), 0) });
        m.setPosition (0);
        m.play();
        out = run (2000);
        check (std::abs (out[1000]) < 1.0e-6f, "launcher: a launched track ignores its timeline");
        m.backToTimeline (0);
        out = run (2000);
        check (near (out[1000], 0.125f * centre, 1.0e-4) && m.launchInfo (0).timeline, "launcher: back to the timeline");
        m.stop();
        run (256);

        // A scene launches every track's slot in it; a track with none stops.
        m.setSlot (1, 1, slotOf (dcOf (0.5f, 12000)));
        m.setPosition (0);
        m.launchScene (1);
        run (1000);
        check (m.launchInfo (0).playing == 1 && m.launchInfo (1).playing == 1, "launcher: a scene launches each track's slot");
        m.launchScene (0);   // track 0 has a slot there, track 1 a one-shot
        run (24000);
        check (m.launchInfo (0).playing == 0, "launcher: the next scene on the beat");
        m.stopAll();
        run (24000);
        check (m.launchInfo (0).playing == -1 && m.launchInfo (1).playing == -1, "launcher: stop all");
        m.stop();
        run (256);

        // Emptying the playing slot stops it.
        m.launch (0, 0);
        run (512);
        m.setSlot (0, 0, nullptr);
        run (512);
        check (m.launchInfo (0).playing == -1, "launcher: an emptied slot stops");
        m.stop();
        run (256);

        // A looping MIDI clip: each pass plays its note, and nothing is left sounding.
        {
            mixer::Engine e;
            e.prepare (rate);
            e.setNumTracks (1);
            e.tempo = 120.0;
            e.launchQuantise = 1.0;
            auto probe = std::make_shared<ProbeInstrument>();
            e.setInstrument (0, probe);
            auto midi = std::make_shared<mixer::MidiClip>();
            midi->events = { { 0, 0x90, 60, 100 }, { 6000, 0x80, 60, 0 }, { 9000, 0x90, 64, 90 } };   // the last note held to the end
            midi->length = 10000;
            auto s = std::make_shared<mixer::Slot>();
            s->clip = mixer::makeClip (mixer::MidiClipPtr (midi), 0);
            e.setSlot (0, 0, s);
            e.launch (0, 0);
            std::vector<float> l (256), r (256);
            for (int done = 0; done < 25000; done += 250)
                e.render (l.data(), r.data(), 250);
            int ons = 0, offs = 0;
            for (const auto& [at, ev] : probe->heard)
            {
                ons += (ev.status & 0xF0) == 0x90 && ev.data2 > 0 ? 1 : 0;
                offs += (ev.status & 0xF0) == 0x80 ? 1 : 0;
            }
            check (ons == 5, "launcher: a MIDI clip plays its notes on each pass");
            check (offs == 4 && probe->sounding == 1, "launcher: a pass lets go of its notes at its end");
            bool onTime = false;
            for (const auto& [at, ev] : probe->heard)
                onTime = onTime || ((ev.status & 0xF0) == 0x90 && ev.data1 == 60 && at == 20000);
            check (onTime, "launcher: the third pass starts on its frame");
            e.stopTrack (0);
            for (int done = 0; done < 30000; done += 250)
                e.render (l.data(), r.data(), 250);
            check (probe->sounding == 0, "launcher: a stop lets go of the notes");
        }
    }

    void testSlicer()
    {
        // Push layout: bottom-left 4x4 is bank A, pad 1 at the bottom left; every note round-trips.
        check (slicer::padForNote (36, slicer::Layout::push) == 0 && slicer::padForNote (40, slicer::Layout::push) == 16
               && slicer::padForNote (68, slicer::Layout::push) == 32 && slicer::padForNote (99, slicer::Layout::push) == 63
               && slicer::padForNote (44, slicer::Layout::push) == 4 && slicer::padForNote (35, slicer::Layout::push) == -1,
               "slicer: Push notes to banks A-D");
        bool round = true;
        for (int n = 36; n < 100; ++n)
            for (const auto layout : { slicer::Layout::push, slicer::Layout::chromatic })
                round = round && slicer::noteForPad (slicer::padForNote (n, layout), layout) == n;
        check (round, "slicer: notes and pads round-trip");

        const auto cuts = slicer::slicesFromCuts ({ 1000, 1010, 5000 }, 8000, 100);
        check (cuts.size() == 3 && cuts[0] == std::pair<Frame, Frame> (0, 1000) && cuts[1] == std::pair<Frame, Frame> (1000, 5000)
               && cuts[2].second == 8000, "slicer: slivers join the next slice");
        const auto equal = slicer::equalSlices (6400, 64);
        check (equal.size() == 64 && equal[1] == std::pair<Frame, Frame> (100, 200), "slicer: equal slices");

        // A ramp makes the slice being played easy to see.
        const double rate = 48000.0;
        AudioData::Builder b (1, rate);
        std::vector<float> x (8000);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) i / 8000.0f;
        const float* p[1] { x.data() };
        b.append (p, (int) x.size());

        slicer::Engine e;
        e.prepare (rate);
        e.setSample (sampler::makeSample (*b.finish()));
        slicer::Kit kit;
        kit.velocity = 0.0;
        slicer::assign (kit, slicer::equalSlices (8000, 4));
        for (auto& pad : kit.pads)
            pad.attack = 0.0;
        kit.pads[2].reverse = true;
        kit.pads[0].choke = kit.pads[1].choke = 1;
        kit.pads[3].play = slicer::Play::gate;
        e.setKit (kit);
        check (! kit.pads[4].used, "slicer: pads past the slices are empty");

        auto play = [&] (int frames)
        {
            std::vector<float> l ((size_t) frames, 0.0f), r ((size_t) frames, 0.0f);
            e.render (l.data(), r.data(), frames);
            return l;
        };
        e.hit (1, 1.0f);
        auto out = play (2500);
        check (near (out[10], x[2010], 1.0e-5) && out[2100] == 0.0f && ! e.isSounding (1), "slicer: a pad plays its slice and stops");
        e.hit (2, 1.0f);
        out = play (100);
        check (near (out[10], x[5989], 1.0e-5), "slicer: reverse plays the slice backwards");
        e.panic();

        e.hit (0, 1.0f);
        play (100);
        e.hit (1, 1.0f);
        play (1000);
        check (! e.isSounding (0) && e.isSounding (1), "slicer: a pad chokes its group");
        e.panic();

        e.hit (3, 1.0f);
        play (200);
        e.letGo (3);
        play (4800);
        check (! e.isSounding (3), "slicer: gate pads stop when let go");
        e.hit (0, 1.0f);
        play (200);
        e.letGo (0);
        play (200);
        check (e.isSounding (0), "slicer: one-shot pads play on when let go");
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

    void testAlbum()
    {
        const double rate = 44100.0;
        Album m;
        m.sampleRate = rate;
        m.channels = 2;

        AlbumClip a;
        a.path = "/music/one.wav";
        a.source = makeRamp (2, 200000, rate);
        a.sourceEnd = 200000;
        AlbumClip b;
        b.path = "/music/Two Song.flac";
        b.source = makeConstant (1, 100000, 0.5f, rate);
        b.sourceStart = 10000;
        b.sourceEnd = 100000;
        b.pauseBefore = 44100;
        b.tags[tag::isrc] = "USRC17607839";
        m.clips = { a, b };

        check (m.clipStart (0) == 0 && m.clipStart (1) == 244100, "album: a pause moves the next song");
        check (m.length() == 244100 + 90000, "album: length is songs plus pauses");
        check (m.clipAt (100) == 0 && m.clipAt (220000) == -1 && m.clipAt (250000) == 1, "album: song at a position");

        // Untouched audio is shared, so the album plays the source exactly.
        auto mix = m.mixdown();
        check (mix->length() == m.length() && mix->numChannels() == 2, "album: mixdown length and channels");
        std::vector<float> l (100), r (100);
        float* lr[] { l.data(), r.data() };
        mix->read (123456, 100, lr);
        std::vector<float> sl (100), sr (100);
        float* slr[] { sl.data(), sr.data() };
        a.source->read (123456, 100, slr);
        check (l == sl && r == sr, "album: first song unchanged");
        mix->read (220000, 100, lr);
        check (l[0] == 0.0f && r[99] == 0.0f, "album: the pause is silent");
        mix->read (260000, 100, lr);
        check (l[0] == 0.5f && r[0] == 0.5f, "album: a mono song fills both sides");

        // Gain and fades.
        m.clips[1].gainDb = -6.0;
        m.clips[1].fadeIn = 1000;
        mix = m.mixdown();
        mix->read (m.clipStart (1), 100, lr);
        check (l[0] < 0.01f, "album: fade in starts near silence");
        mix->read (260000, 1, lr);
        check (near (l[0], 0.5 * dsp::dbToGain (-6.0), 1.0e-4), "album: song gain");

        // A negative pause is a crossfade: equal power keeps a steady level for unrelated material.
        Album x;
        x.sampleRate = rate;
        x.clips = { a, b };
        x.clips[0].source = makeConstant (2, 200000, 0.5f, rate);
        x.clips[1].pauseBefore = -20000;
        x.clampPauses();
        check (x.overlapBefore (1) == 20000 && x.clipStart (1) == 180000, "album: overlap");
        check (near (x.envelope (0, 190000) * x.envelope (0, 190000) + x.envelope (1, 10000) * x.envelope (1, 10000), 1.0, 1.0e-3),
               "album: crossfade is equal power");
        // Two tracks: the overlap is not crossfaded, each song keeps its own level and fades.
        x.twoTracks = true;
        check (x.trackOf (0) == 0 && x.trackOf (1) == 1, "album: songs alternate tracks");
        check (near (x.envelope (0, 190000), 1.0, 1.0e-6) && near (x.envelope (1, 10000), 1.0, 1.0e-6),
               "album: two tracks play over each other");
        x.clips[0].fadeOut = 20000;
        check (x.envelope (0, 199990) < 0.01f && near (x.envelope (1, 0), 1.0, 1.0e-6), "album: own fades shape a two-track overlap");
        auto both = x.mixdown();
        both->read (185000, 1, lr);
        check (l[0] > 0.5f, "album: two tracks sum in the overlap");
        x.twoTracks = false;
        x.clips[0].fadeOut = 0;
        x.clips[1].pauseBefore = -500000;
        x.clampPauses();
        check (x.overlapBefore (1) == 90000, "album: an overlap is never longer than a song");

        // Markers and CUE sheet.
        const auto markers = m.trackMarkers();
        check (markers.size() == 2 && markers[1].name == "2. Two Song" && markers[1].start == 244100, "album: track markers");
        m.tags[tag::album] = "First \"Album\"";
        const auto cue = m.cueSheet ("album.wav");
        check (cue.find ("TITLE \"First 'Album'\"") != std::string::npos, "cue: album title");
        check (cue.find ("FILE \"album.wav\" WAVE") != std::string::npos, "cue: file line");
        check (cue.find ("TRACK 02 AUDIO\n    TITLE \"Two Song\"\n    ISRC USRC17607839\n    INDEX 00 00:04:40\n    INDEX 01 00:05:40") != std::string::npos,
               "cue: second track with pregap (" + cue + ")");
        check (cueTime (44100 * 61 + 44100 / 75 * 3, rate) == "01:01:03", "cue: time format");

        Album empty;
        check (empty.mixdown() != nullptr && empty.mixdown()->length() == 0, "album: empty mixdown");
    }
    void testSpectrumView()
    {
        using namespace spectrum;
        // Scales map back and forth.
        for (auto scale : { Scale::linear, Scale::log, Scale::mel })
            for (double hz : { 50.0, 440.0, 3000.0, 18000.0 })
                check (near (frequency (scale, position (scale, hz, 20.0, 24000.0), 20.0, 24000.0), hz, hz * 1.0e-9), "scale round trip");
        check (near (hzToMel (1000.0), 1000.0, 0.5), "1 kHz is about 1000 mel");
        check (noteName (440.0) == "A4", "A4");
        check (noteName (261.63) == "C4", "middle C");

        // Settings survive storing; nonsense is clamped.
        Settings s;
        s.windowSize = 8192; s.overlap = 8; s.oversample = 2; s.scale = Scale::mel; s.bins = Bins::frequencies;
        s.normalise = Normalise::view; s.gainDb = 6.5; s.rangeDb = 90.0; s.colourMap = 3; s.window = Window::blackmanHarris;
        check (fromString (toString (s)) == s, "settings round trip");
        check (fromString ("window=3000;overlap=99").windowSize == 4096 && fromString ("window=3000;overlap=99").overlap == 16, "settings clamped");

        // A full-scale sine reads about 0 dB at its bin, and reassignment finds its frequency between bins.
        const double rate = 48000.0, freq = 1003.7;
        Settings plain;
        Analyzer analyzer (plain, rate);
        std::vector<float> input ((size_t) analyzer.windowSize()), db ((size_t) analyzer.numBins()), hz ((size_t) analyzer.numBins());
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = (float) std::sin (2.0 * 3.14159265358979 * freq * (double) i / rate);
        analyzer.analyse (input.data(), db.data(), hz.data());
        const auto peak = (int) (std::max_element (db.begin(), db.end()) - db.begin());
        check (near (db[(size_t) peak], 0.0, 1.5), "sine level " + std::to_string (db[(size_t) peak]));
        check (near (hz[(size_t) peak], freq, 0.5), "reassigned " + std::to_string (hz[(size_t) peak]) + " Hz, bin " + std::to_string (peak * rate / 2048));
        check (near (hz[(size_t) peak + 1], freq, 2.0), "neighbour bin reassigned too " + std::to_string (hz[(size_t) peak + 1]));

        // Rendering: the sine's row is the loudest in each column, on every scale and bin mode.
        auto sine = makeSine (1, 48000, freq, 0.5);
        for (auto bins : { Bins::all, Bins::peaks, Bins::frequencies })
            for (auto scale : { Scale::linear, Scale::log, Scale::mel })
            {
                Settings r;
                r.bins = bins;
                r.scale = scale;
                const int columns = 40, rows = 200;
                const auto lanes = render (*sine, r, 0.0, 48000.0, columns, rows, 0.0, 0.0);
                const auto& lane = lanes[0];
                const auto expected = (int) ((1.0 - position (scale, freq, 0.0, 24000.0)) * rows);
                int good = 0;
                for (int x = 2; x < columns - 2; ++x)
                {
                    int loudest = 0;
                    for (int y = 1; y < rows; ++y)
                        if (lane[(size_t) (y * columns + x)] > lane[(size_t) (loudest * columns + x)])
                            loudest = y;
                    good += std::abs (loudest - expected) <= 1;
                }
                check (good == columns - 4, "rendered sine row, mode " + std::to_string ((int) bins) + " scale " + std::to_string ((int) scale));
            }

        // Column normalising puts each column's top at 0 dB.
        Settings n;
        n.normalise = Normalise::column;
        const auto quiet = makeSine (1, 48000, 500.0, 0.01);
        const auto lane = render (*quiet, n, 0.0, 48000.0, 10, 100, 0.0, 0.0)[0];
        check (near (*std::max_element (lane.begin(), lane.end()), 0.0, 0.01), "column normalised to 0 dB");
    }
    // Power in a band, averaged over 2048-point frames.
    double bandPowerDb (const std::vector<float>& x, double rate, double lowHz, double highHz)
    {
        SpectrumAnalyzer analyzer (11);
        std::vector<float> db ((size_t) analyzer.numBins());
        double total = 0.0;
        int frames = 0;
        for (size_t start = 0; start + 2048 <= x.size(); start += 1024, ++frames)
        {
            analyzer.magnitudesDb (x.data() + start, db.data());
            for (int k = 0; k < analyzer.numBins(); ++k)
                if (const auto hz = k * rate / 2048.0; hz >= lowHz && hz < highHz)
                    total += std::pow (10.0, db[(size_t) k] / 10.0);
        }
        return 10.0 * std::log10 (std::max (total / std::max (1, frames), 1.0e-30));
    }

    void testGenerate()
    {
        const double rate = 48000.0;
        gen::Tone t;
        t.startHz = t.endHz = 1000.0;
        auto sine = gen::tone (t, rate, 48000);
        int crossings = 0;
        for (size_t i = 1; i < sine.size(); ++i)
            crossings += (sine[i - 1] < 0.0f) != (sine[i] < 0.0f);
        check (std::abs (crossings - 2000) <= 2, "1 kHz sine crosses zero 2000 times: " + std::to_string (crossings));
        check (near (*std::max_element (sine.begin(), sine.end()), std::pow (10.0, -6.0 / 20.0), 1.0e-4), "peak at -6 dBFS");

        // Band-limited waves keep (almost) nothing where the raw ones alias: a 5 kHz saw's
        // 7th harmonic (35 kHz) folds to 13 kHz.
        for (auto wave : { gen::Wave::sawtooth, gen::Wave::square, gen::Wave::triangle })
        {
            gen::Tone w;
            w.wave = wave;
            w.startHz = w.endHz = 5000.0;
            auto clean = gen::tone (w, rate, 48000);
            w.bandLimited = false;
            auto raw = gen::tone (w, rate, 48000);
            const auto alias = [&] (const std::vector<float>& x) { return bandPowerDb (x, rate, 12800.0, 13200.0); };
            const auto fundamental = bandPowerDb (clean, rate, 4800.0, 5200.0);
            check (alias (clean) < alias (raw) - 10.0, "band-limiting removes aliasing, wave " + std::to_string ((int) wave)
                   + ": " + std::to_string (alias (clean)) + " vs " + std::to_string (alias (raw)));
            check (fundamental > alias (clean) + 30.0, "fundamental well above the alias, wave " + std::to_string ((int) wave));
            float lo = 1.0f, hi = -1.0f;
            for (auto v : clean) { lo = std::min (lo, v); hi = std::max (hi, v); }
            check (hi > 0.3f && lo < -0.3f, "wave swings both ways " + std::to_string ((int) wave));
        }

        // A sweep starts and ends at the asked frequencies.
        gen::Tone sweep;
        sweep.startHz = 100.0;
        sweep.endHz = 10000.0;
        auto up = gen::tone (sweep, rate, 96000);
        const std::vector<float> head (up.begin(), up.begin() + 4096), tail (up.end() - 4096, up.end());
        check (bandPowerDb (head, rate, 50.0, 200.0) > bandPowerDb (head, rate, 5000.0, 12000.0) + 30.0, "sweep starts low");
        check (bandPowerDb (tail, rate, 8000.0, 12000.0) > bandPowerDb (tail, rate, 50.0, 2000.0) + 30.0, "sweep ends high");

        // Noise colours: the change in power per octave band, measured three octaves apart.
        const std::vector<std::pair<gen::Noise, double>> slopes { { gen::Noise::white, 9.0 }, { gen::Noise::pink, 0.0 },
            { gen::Noise::brown, -9.0 }, { gen::Noise::blue, 18.0 }, { gen::Noise::violet, 27.0 } };
        for (const auto& [colour, expected] : slopes)
        {
            auto n = gen::noise (colour, -3.0, rate, 480000, 1234);
            const auto low = bandPowerDb (n, rate, 500.0, 1000.0), high = bandPowerDb (n, rate, 4000.0, 8000.0);
            check (near (high - low, expected, 2.0), "noise colour " + std::to_string ((int) colour) + " slope "
                   + std::to_string (high - low) + " dB, expected " + std::to_string (expected));
            float peak = 0.0f;
            for (auto v : n)
                peak = std::max (peak, std::abs (v));
            check (near (peak, std::pow (10.0, -3.0 / 20.0), 1.0e-4), "noise peak level");
        }
        check (gen::noise (gen::Noise::white, -3.0, rate, 100, 7) == gen::noise (gen::Noise::white, -3.0, rate, 100, 7), "same seed, same noise");
    }
    void testPitch()
    {
        const double rate = 48000.0;
        auto toData = [&] (const std::vector<float>& x)
        {
            AudioData::Builder b (1, rate);
            const float* p = x.data();
            b.append (&p, (int) x.size());
            return b.finish();
        };
        auto voicedNear = [] (const pitch::Track& t, double hz, double cents, Frame from, Frame to)
        {
            int good = 0, total = 0;
            for (const auto& p : t.points)
                if (p.at >= from && p.at < to)
                {
                    ++total;
                    good += p.hz > 0.0f && std::abs (1200.0 * std::log2 (p.hz / hz)) <= cents;
                }
            return total > 0 ? (double) good / total : 0.0;
        };

        gen::Tone t;
        t.startHz = t.endHz = 220.0;
        const auto sine = pitch::track (*toData (gen::tone (t, rate, 48000)));
        check (voicedNear (*sine, 220.0, 5.0, 4800, 43200) > 0.98, "sine 220 Hz tracked");
        check (std::abs (sine->at (24000) - 220.0f) < 0.5f, "sine pitch " + std::to_string (sine->at (24000)));

        t.wave = gen::Wave::sawtooth;
        t.startHz = t.endHz = 110.0;
        const auto saw = pitch::track (*toData (gen::tone (t, rate, 48000)));
        check (voicedNear (*saw, 110.0, 5.0, 4800, 43200) > 0.98, "sawtooth 110 Hz tracked, not an overtone");

        t.wave = gen::Wave::square;
        t.startHz = 200.0;
        t.endHz = 400.0;
        const auto glide = pitch::track (*toData (gen::tone (t, rate, 96000)));
        check (std::abs (glide->at (2400) - 200.0f) < 6.0f && std::abs (glide->at (93600) - 400.0f) < 10.0f,
               "glide followed " + std::to_string (glide->at (2400)) + " to " + std::to_string (glide->at (93600)));
        check (voicedNear (*glide, 282.8, 30.0, 47000, 49000) > 0.9, "glide passes the octave's middle");

        const auto noise = pitch::track (*toData (gen::noise (gen::Noise::white, -6.0, rate, 48000, 3)));
        int voiced = 0;
        for (const auto& p : noise->points)
            voiced += p.hz > 0.0f;
        check (voiced < (int) noise->points.size() / 10, "white noise has (almost) no pitch: " + std::to_string (voiced));

        const auto silence = pitch::track (*toData (std::vector<float> (48000, 0.0f)));
        check (std::all_of (silence->points.begin(), silence->points.end(), [] (const pitch::Point& p) { return p.hz == 0.0f; }), "silence has no pitch");
    }
    void testPitchFix()
    {
        const double rate = 48000.0;
        auto toData = [&] (const std::vector<float>& x)
        {
            AudioData::Builder b (1, rate);
            const float* p = x.data();
            b.append (&p, (int) x.size());
            return b.finish();
        };
        // A band-limited sawtooth-like tone (8 harmonics) whose pitch follows `hzAt`.
        auto voice = [&] (Frame length, const std::function<double (double)>& hzAt)
        {
            std::vector<float> x ((size_t) length);
            double phase = 0.0;
            for (Frame i = 0; i < length; ++i)
            {
                phase += hzAt ((double) i / rate) / rate;
                double v = 0.0;
                for (int h = 1; h <= 8; ++h)
                    v += std::sin (2.0 * M_PI * phase * h) / h;
                x[(size_t) i] = (float) (0.3 * v);
            }
            return x;
        };
        auto spread = [] (const pitch::Track& t, double hz, Frame from, Frame to, double& meanCents, double& worstCents)
        {
            double sum = 0.0;
            int n = 0;
            worstCents = 0.0;
            for (const auto& p : t.points)
                if (p.at >= from && p.at < to && p.hz > 0.0f)
                {
                    const auto c = pitchfix::cents (hz, p.hz);
                    sum += c;
                    worstCents = std::max (worstCents, std::abs (c));
                    ++n;
                }
            meanCents = n > 0 ? sum / n : 1e9;
        };

        check (std::abs (pitchfix::nearestNoteHz (216.24) - 220.0) < 1e-9, "216 Hz is nearest A3");
        check (std::abs (pitchfix::nearestNoteHz (453.0) - 466.1637615) < 1e-3, "453 Hz is nearest A#4");

        // A flat A3 (30 cents low) moved to 220 Hz, the audio outside the selection untouched.
        const auto flatHz = 220.0 * std::pow (2.0, -30.0 / 1200.0);
        const auto flat = toData (voice (96000, [&] (double) { return flatHz; }));
        const auto track = pitch::track (*flat);
        std::string error;
        const Frame start = 12000, end = 84000;
        const auto fixed = pitchfix::correct (*flat, start, end, *track, {}, error);
        check (fixed != nullptr && fixed->length() == end - start, "corrected audio has the selection's length " + error);
        if (fixed != nullptr)
        {
            const auto result = flat->replaced (start, end, fixed.get());
            double mean = 0.0, worst = 0.0;
            spread (*pitch::track (*result), 220.0, 24000, 72000, mean, worst);
            check (std::abs (mean) < 4.0 && worst < 12.0, "flat note corrected to A3: mean " + std::to_string (mean) + " worst " + std::to_string (worst) + " cents");
            std::vector<float> a (100), b (100);
            float* pa = a.data();
            float* pb = b.data();
            flat->read (start - 200, 100, &pa);
            result->read (start - 200, 100, &pb);
            check (a == b, "audio before the selection is untouched");
        }

        // Moved to another note: the flat A3 up to C4.
        pitchfix::Settings toC;
        toC.targetHz = 261.6256;
        if (const auto c4 = pitchfix::correct (*flat, start, end, *track, toC, error))
        {
            double mean = 0.0, worst = 0.0;
            spread (*pitch::track (*flat->replaced (start, end, c4.get())), toC.targetHz, 24000, 72000, mean, worst);
            check (std::abs (mean) < 5.0 && worst < 20.0, "moved to C4: mean " + std::to_string (mean) + " worst " + std::to_string (worst) + " cents");
        }
        else
            check (false, "moved to C4");

        // Vibrato of +-40 cents at 5 Hz, flattened.
        const auto wobbly = toData (voice (96000, [] (double t) { return 220.0 * std::pow (2.0, 40.0 * std::sin (2.0 * M_PI * 5.0 * t) / 1200.0); }));
        const auto wobblyTrack = pitch::track (*wobbly);
        double before = 0.0, wobble = 0.0;
        spread (*wobblyTrack, 220.0, 24000, 72000, before, wobble);
        pitchfix::Settings flatten;
        flatten.flatten = 1.0;
        const auto steady = pitchfix::correct (*wobbly, start, end, *wobblyTrack, flatten, error);
        check (steady != nullptr, "vibrato corrected");
        if (steady != nullptr)
        {
            double mean = 0.0, worst = 0.0;
            spread (*pitch::track (*wobbly->replaced (start, end, steady.get())), 220.0, 24000, 72000, mean, worst);
            check (wobble > 35.0 && worst < 15.0, "vibrato flattened from " + std::to_string (wobble) + " to " + std::to_string (worst) + " cents");
        }

        // Silence has no pitch to correct.
        const auto silence = toData (std::vector<float> (48000, 0.0f));
        error.clear();
        check (pitchfix::correct (*silence, 0, 48000, *pitch::track (*silence), {}, error) == nullptr && ! error.empty(), "silence is refused");
    }

    void testBinaural()
    {
        // The convolver matches a direct convolution, across several partitions.
        {
            std::mt19937 random (1);
            std::uniform_real_distribution<float> u (-1.0f, 1.0f);
            std::vector<float> x (1000), h (300);
            for (auto& v : x) v = u (random);
            for (auto& v : h) v = u (random);
            binaural::Convolver conv (h, 64);
            std::vector<float> y;
            for (size_t pos = 0; pos < x.size() + h.size(); pos += 64)
            {
                std::vector<float> in (64, 0.0f), out (64);
                for (size_t i = 0; i < 64 && pos + i < x.size(); ++i)
                    in[i] = x[pos + i];
                conv.process (in.data(), out.data());
                y.insert (y.end(), out.begin(), out.end());
            }
            double worst = 0.0;
            for (size_t n = 0; n < x.size() + h.size() - 1; ++n)
            {
                double expected = 0.0;
                for (size_t k = 0; k < h.size(); ++k)
                    if (n >= k && n - k < x.size())
                        expected += (double) h[k] * x[n - k];
                worst = std::max (worst, std::abs (expected - y[n]));
            }
            check (worst < 1e-3, "partitioned convolution matches direct convolution");
        }

        const double rate = 48000.0;
        std::vector<float> click (48000, 0.0f);
        click[24000] = 1.0f;
        AudioData::Builder b (1, rate);
        const float* p = click.data();
        b.append (&p, (int) click.size());
        const auto mono = b.finish();

        binaural::Settings settings;
        settings.hrtfPath = PHI_DEFAULT_HRTF;
        std::string error;
        const auto ears = [&] (double azimuth, double& leftEnergy, double& rightEnergy, Frame& leftPeak, Frame& rightPeak)
        {
            settings.azimuth = azimuth;
            const auto out = binaural::render (*mono, 0, mono->length(), settings, error);
            if (out == nullptr || out->numChannels() != 2 || out->length() != mono->length())
                return false;
            std::vector<float> l ((size_t) out->length()), r ((size_t) out->length());
            float* both[] { l.data(), r.data() };
            out->read (0, (int) out->length(), both);
            leftEnergy = rightEnergy = 0.0;
            leftPeak = rightPeak = 0;
            for (size_t i = 0; i < l.size(); ++i)
            {
                leftEnergy += (double) l[i] * l[i];
                rightEnergy += (double) r[i] * r[i];
                if (std::abs (l[i]) > std::abs (l[(size_t) leftPeak])) leftPeak = (Frame) i;
                if (std::abs (r[i]) > std::abs (r[(size_t) rightPeak])) rightPeak = (Frame) i;
            }
            return true;
        };

        double le = 0, re = 0;
        Frame lp = 0, rp = 0;
        check (ears (90.0, le, re, lp, rp), "binaural render of a click (" + error + ")");
        check (re > le * 4.0, "a sound at 90 degrees is louder in the right ear");
        check (rp < lp, "and reaches the right ear first");
        check (ears (-90.0, le, re, lp, rp) && le > re * 4.0 && lp < rp, "a sound at -90 degrees is on the left");
        check (ears (0.0, le, re, lp, rp) && std::abs (10.0 * std::log10 (le / re)) < 2.0, "a sound in front reaches both ears alike");

        // Further away is quieter; room sound adds a tail.
        double nearEnergy = le + re;
        settings.distance = 4.0;
        ears (0.0, le, re, lp, rp);
        check (le + re < nearEnergy / 8.0, "4 m away is about 12 dB quieter than 1 m");
        settings.distance = 1.0;
        settings.room = 1.0;
        const auto withRoom = binaural::render (*mono, 0, mono->length(), settings, error);
        std::vector<float> l (48000), r (48000);
        float* both[] { l.data(), r.data() };
        withRoom->read (0, 48000, both);
        double late = 0.0;
        for (size_t i = 24000 + 4800; i < 24000 + 14400; ++i)
            late += (double) l[i] * l[i];
        check (late > 1e-4, "the room rings on after the click");

        settings.hrtfPath = "/no/such.sofa";
        error.clear();
        check (binaural::render (*mono, 0, mono->length(), settings, error) == nullptr && ! error.empty(), "a missing HRTF file is reported");
    }

    void testSpatialStudio()
    {
        const double rate = 48000.0;
        const auto makeNoise = [rate] (int channels, Frame length)
        {
            std::mt19937 rng (5);
            std::uniform_real_distribution<float> u (-0.5f, 0.5f);
            AudioData::Builder b (channels, rate);
            std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> ((size_t) length));
            for (auto& c : block)
                for (auto& x : c)
                    x = u (rng);
            std::vector<const float*> ptrs;
            for (auto& c : block)
                ptrs.push_back (c.data());
            b.append (ptrs.data(), (int) length);
            return b.finish();
        };
        const auto channelEnergy = [] (const AudioData& d, int channel, Frame start, Frame end)
        {
            std::vector<std::vector<float>> all ((size_t) d.numChannels(), std::vector<float> ((size_t) (end - start)));
            std::vector<float*> ptrs;
            for (auto& c : all)
                ptrs.push_back (c.data());
            d.read (start, (int) (end - start), ptrs.data());
            double e = 0.0;
            for (auto x : all[(size_t) channel])
                e += (double) x * x;
            return e;
        };

        // Distance: within the centre the sound turns omni; beyond it, 1.5 dB a unit.
        double omni = 0.0, directional = 0.0;
        studio::distanceGains (0.05, 10.0, omni, directional);
        check (near (omni, 1.0, 1e-9) && near (directional, 0.25, 1e-9), "studio: inside the centre the directional part fades");
        studio::distanceGains (1.0, 10.0, omni, directional);
        check (near (20.0 * std::log10 (omni), -13.5, 1e-6), "studio: radius 1 at range 10 is 13.5 dB down");

        // A mono source panner at 90 degrees: first-order Y is minus W (Y points left).
        const auto mono = makeNoise (1, 24000);
        studio::Scene scene;
        scene.order = 1;
        scene.tailSeconds = 0.5;
        auto panner = studio::makeDevice (studio::Kind::sourcePanner);
        panner.set ("stereo", 0);
        panner.set ("azimuth", 90.0);
        scene.chain.push_back (panner);
        auto result = studio::render (*mono, 0, scene);
        check (result.scene != nullptr && result.scene->numChannels() == 4 && result.scene->length() == 24000 + 24000,
               "studio: a first-order scene, the tail added");
        if (result.scene != nullptr)
        {
            const double w = channelEnergy (*result.scene, 0, 1000, 20000), y = channelEnergy (*result.scene, 1, 1000, 20000),
                         x = channelEnergy (*result.scene, 3, 1000, 20000);
            check (w > 0.0 && near (y / w, 1.0, 1e-3) && x / w < 1e-6, "studio: the source panner places a sound on the right");
            check (! result.motion.frames.empty() && result.motion.at (0.2, 0) != nullptr && near (result.motion.at (0.2, 0)->front().azimuth, 90.0, 1e-3),
                   "studio: the heads-up display sees where the sound is");
        }

        // HOA Transform: a yaw of -90 brings it back to the front.
        auto turn = studio::makeDevice (studio::Kind::hoaTransform);
        turn.set ("yaw", -90.0);
        scene.chain.push_back (turn);
        result = studio::render (*mono, 0, scene);
        if (result.scene != nullptr)
        {
            const double w = channelEnergy (*result.scene, 0, 1000, 20000), y = channelEnergy (*result.scene, 1, 1000, 20000),
                         x = channelEnergy (*result.scene, 3, 1000, 20000);
            check (y / w < 1e-6 && near (x / w, 1.0, 1e-3), "studio: the HOA Transform turns the scene");
        }

        // The Spatial Slicer keeps a sound inside its slice and takes one outside away.
        const auto slice = [&] (double sliceAzimuth)
        {
            studio::Scene s;
            s.order = 3;
            s.tailSeconds = 0.0;
            auto pan = studio::makeDevice (studio::Kind::sourcePanner);
            pan.set ("stereo", 0);
            s.chain.push_back (pan);
            auto slicer = studio::makeDevice (studio::Kind::spatialSlicer);
            slicer.set ("timed", 0);
            slicer.set ("azimuth", sliceAzimuth);
            s.chain.push_back (slicer);
            const auto r = studio::render (*mono, 0, s);
            return r.scene != nullptr ? channelEnergy (*r.scene, 0, 2000, 20000) : -1.0;
        };
        const double inside = slice (0.0), outside = slice (180.0);
        check (inside > 0.0 && outside < inside * 0.05, "studio: the Spatial Slicer cuts away what is outside its slice");

        // The Multi-Delay: a click comes back after the delay time, from the side.
        std::vector<float> clicks (48000, 0.0f);
        clicks[1000] = 1.0f;
        AudioData::Builder cb (1, rate);
        const float* cp = clicks.data();
        cb.append (&cp, (int) clicks.size());
        const auto click = cb.finish();
        studio::Scene delayScene;
        delayScene.order = 1;
        delayScene.tailSeconds = 0.0;
        auto delay = studio::makeDevice (studio::Kind::multiDelay);
        delay.set ("time", 100.0);
        delay.set ("voices", 1);
        delay.set ("feedback", 0);
        delay.set ("spread", 90.0);
        delayScene.chain.push_back (delay);
        result = studio::render (*click, 0, delayScene);
        if (result.scene != nullptr)
        {
            const double echo = channelEnergy (*result.scene, 0, 1000 + 4800 - 4, 1000 + 4800 + 4);
            const double before = channelEnergy (*result.scene, 0, 0, 1000 + 4800 - 8);
            const double y = channelEnergy (*result.scene, 1, 1000 + 4800 - 4, 1000 + 4800 + 4);
            check (echo > 0.1 && before < 1e-9 && near (y / echo, 1.0, 1e-2), "studio: the Multi-Delay echoes 100 ms later, to the right");
        }

        // Every device renders a stereo file, the same each time, without blowing up.
        const auto stereo = makeNoise (2, 12000);
        studio::Scene all;
        all.order = 3;
        all.tailSeconds = 0.3;
        for (const auto& d : studio::devices())
            all.chain.push_back (studio::makeDevice (d.kind));
        const auto first = studio::render (*stereo, 0, all), second = studio::render (*stereo, 0, all);
        bool finite = first.scene != nullptr && second.scene != nullptr && first.scene->numChannels() == 16;
        if (finite)
        {
            for (int c = 0; c < 16 && finite; ++c)
            {
                const auto a = channelEnergy (*first.scene, c, 0, first.scene->length()), b = channelEnergy (*second.scene, c, 0, second.scene->length());
                finite = std::isfinite (a) && a < 1e9 && a == b;
            }
        }
        check (finite, "studio: every device together renders, the same both times (" + first.error + ")");

        // The rack survives a trip through JSON.
        all.bpm = 98.5;
        all.master.decoder = "7.1.4";
        all.chain[1].set ("azimuth", -42.5);
        all.chain[1].enabled = false;
        all.chain.back().files["W"] = "/tmp/ir \"hall\".wav";
        studio::Scene back;
        check (studio::fromJson (studio::toJson (all), back) && back.chain.size() == all.chain.size() && near (back.bpm, 98.5, 1e-9)
                   && back.master.decoder == "7.1.4" && near (back.chain[1].get ("azimuth"), -42.5, 1e-9) && ! back.chain[1].enabled
                   && back.chain.back().files["W"] == "/tmp/ir \"hall\".wav",
               "studio: a rack saved as JSON loads back the same");
        check (! studio::fromJson ("{not json", back), "studio: bad JSON is refused");

        // The master bus: stereo speakers hear a source on the left on the left.
        studio::Scene leftScene;
        leftScene.order = 3;
        leftScene.tailSeconds = 0.0;
        auto leftPan = studio::makeDevice (studio::Kind::sourcePanner);
        leftPan.set ("stereo", 0);
        leftPan.set ("azimuth", -60.0);
        leftScene.chain.push_back (leftPan);
        leftScene.master.decoder = "Stereo";
        const auto heard = studio::render (*mono, 0, leftScene);
        std::string error;
        const auto speakers = heard.scene != nullptr ? studio::decode (*heard.scene, leftScene.master, error) : nullptr;
        check (speakers != nullptr && speakers->numChannels() == 2
                   && channelEnergy (*speakers, 0, 0, 20000) > 4.0 * channelEnergy (*speakers, 1, 0, 20000),
               "studio: the stereo decoder puts a left source on the left");
        check (studio::decodedLayout (leftScene.master, 3) == "Stereo" && studio::decodedLayout ({ 0.0, "AmbiX", {} }, 2) == "Ambisonics (2nd order)",
               "studio: decoded layouts are named");

        // Your own speaker array: parsed from text, kept in a rack, decoded to.
        std::vector<ambi::Speaker> ring;
        std::string why;
        check (studio::parseSpeakers ("# my room\nA -45 0\nB 45 0\nC 135 0\nD -135 0\nSub 0 0 lfe\n", ring, why) && ring.size() == 5 && ring[4].lfe
                   && near (ring[3].azimuth, -135.0, 1e-9),
               "studio: a speaker array is read from text (" + why + ")");
        std::vector<ambi::Speaker> parsed;
        check (studio::parseSpeakers (studio::formatSpeakers (ring), parsed, why) && parsed.size() == ring.size(), "studio: a speaker array survives being written out");
        std::string tooMany;
        for (int i = 0; i < 17; ++i)
            tooMany += "S" + std::to_string (i) + " " + std::to_string (i * 20 - 170) + " 0\n";
        check (! studio::parseSpeakers (tooMany, parsed, why) && ! studio::parseSpeakers ("Only 0 0\n", parsed, why)
                   && ! studio::parseSpeakers ("Bad 0\n", parsed, why),
               "studio: more than 16 speakers, a lone speaker and a missing elevation are refused");
        for (const auto& [name, speakers] : studio::exampleArrays())
            check ((int) speakers.size() <= studio::maxSpeakers && studio::parseSpeakers (studio::formatSpeakers (speakers), parsed, why),
                   "studio: the example array " + name + " is usable");
        studio::Master custom;
        custom.decoder = "Custom: Room";
        custom.speakers = ring;
        const auto toRing = heard.scene != nullptr ? studio::decode (*heard.scene, custom, error) : nullptr;
        check (toRing != nullptr && toRing->numChannels() == 5 && channelEnergy (*toRing, 4, 0, 20000) == 0.0
                   && channelEnergy (*toRing, 0, 0, 20000) > 2.0 * channelEnergy (*toRing, 1, 0, 20000)
                   && channelEnergy (*toRing, 0, 0, 20000) > 2.0 * channelEnergy (*toRing, 2, 0, 20000),
               "studio: a custom array decodes a left source to the front-left speaker, the sub silent");
        check (studio::decodedLayout (custom, 3).empty(), "studio: a custom array is labelled by its channel count");
        studio::Scene withArray;
        withArray.master = custom;
        studio::Scene arrayBack;
        check (studio::fromJson (studio::toJson (withArray), arrayBack) && arrayBack.master.speakers.size() == 5 && arrayBack.master.speakers[4].lfe
                   && arrayBack.master.decoder == "Custom: Room",
               "studio: a rack keeps its custom array");

        // B-Format Input reads a FuMa file into AmbiX.
        ambi::EncodeSettings enc;
        enc.order = 1;
        enc.azimuth = 90.0;
        const auto ambix = ambi::encode (*mono, enc);
        const auto fuma = ambi::mix (*ambix, ambi::ambixToFuma (1));
        studio::Scene bf;
        bf.order = 1;
        bf.tailSeconds = 0.0;
        auto input = studio::makeDevice (studio::Kind::bformatInput);
        input.set ("format", 1);
        bf.chain.push_back (input);
        const auto restored = studio::render (*fuma, 1, bf);
        check (restored.scene != nullptr && near (channelEnergy (*restored.scene, 0, 0, 20000), channelEnergy (*ambix, 0, 0, 20000), 1e-3)
                   && near (channelEnergy (*restored.scene, 1, 0, 20000), channelEnergy (*ambix, 1, 0, 20000), 1e-3),
               "studio: B-Format Input turns FuMa into AmbiX");
    }

    void testAmbisonics()
    {
        const auto near = [] (double a, double b, double tolerance = 1e-6) { return std::abs (a - b) <= tolerance; };
        const auto right = ambi::harmonics (1, 90.0, 0.0);
        check (near (right[0], 1.0) && near (right[1], -1.0) && near (right[2], 0.0) && near (right[3], 0.0, 1e-9),
               "first order: a sound on the right is W 1, Y -1 (Y points left)");
        const auto up = ambi::harmonics (3, 0.0, 90.0);
        check (near (up[2], 1.0) && near (up[6], 1.0) && near (up[12], 1.0), "straight up: Z, R and K are 1");

        const auto apply = [] (const ambi::Matrix& m, const std::vector<double>& v)
        {
            std::vector<double> out (m.size(), 0.0);
            for (size_t r = 0; r < m.size(); ++r)
                for (size_t c = 0; c < v.size(); ++c)
                    out[r] += m[r][c] * v[c];
            return out;
        };
        const auto distance = [] (const std::vector<double>& a, const std::vector<double>& b)
        {
            double d = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
                d = std::max (d, std::abs (a[i] - b[i]));
            return d;
        };
        const auto turned = apply (ambi::rotation (3, 90.0, 0.0, 0.0), ambi::harmonics (3, 0.0, 0.0));
        check (distance (turned, ambi::harmonics (3, 90.0, 0.0)) < 1e-6, "yaw 90 turns the front to the right (3rd order)");
        const auto tilted = apply (ambi::rotation (2, 0.0, 90.0, 0.0), ambi::harmonics (2, 0.0, 0.0));
        check (distance (tilted, ambi::harmonics (2, 0.0, 90.0)) < 1e-6, "pitch 90 lifts the front straight up");
        const auto mirrored = apply (ambi::mirror (3, true, false, false), ambi::harmonics (3, -40.0, 20.0));
        check (distance (mirrored, ambi::harmonics (3, 40.0, 20.0)) < 1e-6, "left-right mirror");

        const auto fuma = apply (ambi::ambixToFuma (3), ambi::harmonics (3, 30.0, 10.0));
        check (near (fuma[0], M_SQRT1_2) && distance (apply (ambi::fumaToAmbix (3), fuma), ambi::harmonics (3, 30.0, 10.0)) < 1e-9,
               "FuMa W is 3 dB down and converts back exactly");

        // Decoders: a sound at 30 degrees right comes mostly from R; LFE stays silent.
        const auto five = ambi::layout ("5.1");
        const auto out51 = apply (ambi::decoder (3, five), ambi::harmonics (3, 30.0, 0.0));
        size_t loudest = 0;
        double energy = 0.0;
        for (size_t i = 0; i < out51.size(); ++i)
        {
            energy += out51[i] * out51[i];
            if (std::abs (out51[i]) > std::abs (out51[loudest])) loudest = i;
        }
        check (five[loudest].name == "R" && out51[3] == 0.0, "5.1 decode: 30 degrees right is the R speaker");
        check (energy > 0.5 && energy < 2.0, "5.1 decode keeps the level");
        const auto atmos = ambi::layout ("7.1.4");
        const auto high = apply (ambi::decoder (3, atmos), ambi::harmonics (3, 45.0, 45.0));
        loudest = 0;
        for (size_t i = 0; i < high.size(); ++i)
            if (std::abs (high[i]) > std::abs (high[loudest])) loudest = i;
        check (atmos[loudest].name == "Rtf", "7.1.4 decode: up and to the right front is Rtf");
        const auto stereo = apply (ambi::decoder (1, ambi::layout ("Stereo")), ambi::harmonics (1, -60.0, 0.0));
        check (stereo[0] > stereo[1] * 2.0, "stereo decode: the left is on the left");

        // Encode a click at 90 degrees, decode for headphones: louder on the right.
        const double rate = 48000.0;
        std::vector<float> click (24000, 0.0f);
        click[12000] = 1.0f;
        AudioData::Builder b (1, rate);
        const float* p = click.data();
        b.append (&p, (int) click.size());
        ambi::EncodeSettings settings;
        settings.order = 2;
        settings.azimuth = 90.0;
        const auto encoded = ambi::encode (*b.finish(), settings);
        check (encoded->numChannels() == 9 && encoded->length() == 24000, "2nd order encoding has 9 channels");
        std::string error;
        const auto ears = ambi::decodeBinaural (*encoded, PHI_DEFAULT_HRTF, error);
        check (ears != nullptr && ears->numChannels() == 2, "binaural decode (" + error + ")");
        if (ears != nullptr)
        {
            std::vector<float> l (24000), r (24000);
            float* both[] { l.data(), r.data() };
            ears->read (0, 24000, both);
            double le = 0.0, re = 0.0;
            for (size_t i = 0; i < l.size(); ++i) { le += (double) l[i] * l[i]; re += (double) r[i] * r[i]; }
            check (re > le * 3.0, "binaural decode: 90 degrees is on the right");
        }

        // The energy map peaks where the sound is.
        std::vector<std::vector<float>> noise (9, std::vector<float> (2048));
        std::mt19937 random (3);
        std::normal_distribution<float> gauss;
        const auto y = ambi::harmonics (2, -60.0, 30.0);
        for (int t = 0; t < 2048; ++t)
        {
            const auto x = gauss (random);
            for (size_t c = 0; c < 9; ++c)
                noise[c][(size_t) t] = (float) y[c] * x;
        }
        std::vector<const float*> ptrs;
        for (auto& v : noise)
            ptrs.push_back (v.data());
        std::vector<float> map;
        ambi::energyMap (ptrs.data(), 9, 2048, 72, 36, map);
        const auto peak = (size_t) (std::max_element (map.begin(), map.end()) - map.begin());
        const auto az = -180.0 + ((double) (peak % 72) + 0.5) * 5.0, el = 90.0 - ((double) (peak / 72) + 0.5) * 5.0;
        check (std::abs (az + 60.0) <= 10.0 && std::abs (el - 30.0) <= 10.0, "energy map peaks at the sound");

        // Layouts beyond the usual one for a channel count.
        const auto seven = dsp::layoutChoices (7);
        check (seven.size() == 2 && seven[0] == "6.1" && seven[1] == "5.2", "7 channels can be 6.1 or 5.2");
        check (dsp::channelName (4, 7, "5.2") == "LFE2" && dsp::channelName (3, 7, "5.2") == "LFE1", "5.2 has LFE1 and LFE2");
        check (dsp::channelName (8, 16, "9.1.6") == "Lw" && dsp::channelName (3, 16, "Ambisonics (3rd order)") == "X",
               "16 channels as 9.1.6 or 3rd-order Ambisonics");
        const auto fold = dsp::stereoDownmix (4, "Ambisonics (1st order)");
        const auto leftSound = ambi::harmonics (1, -90.0, 0.0);
        double l = 0.0, r = 0.0;
        for (size_t c = 0; c < 4; ++c) { l += fold[0][c] * leftSound[c]; r += fold[1][c] * leftSound[c]; }
        check (l > r * 3.0, "an ambisonic file folds down to stereo by decoding");
        check (dsp::speakerLayout (4, "nonsense").name == "Quad", "an unknown layout name falls back to the usual one");
    }
}

    void testTimeStretch()
    {
        const double rate = 48000.0;
        auto build = [&] (const std::vector<std::vector<float>>& x)
        {
            AudioData::Builder b ((int) x.size(), rate);
            std::vector<const float*> p;
            for (const auto& c : x)
                p.push_back (c.data());
            b.append (p.data(), (int) x[0].size());
            return b.finish();
        };
        auto channel = [] (const AudioData& d, int c)
        {
            std::vector<std::vector<float>> all ((size_t) d.numChannels(), std::vector<float> ((size_t) d.length()));
            std::vector<float*> p;
            for (auto& v : all)
                p.push_back (v.data());
            d.read (0, (int) d.length(), p.data());
            return all[(size_t) c];
        };
        auto crossings = [] (const std::vector<float>& x, size_t from, size_t to)
        {
            int n = 0;
            for (size_t i = from + 1; i < to && i < x.size(); ++i)
                n += (x[i - 1] < 0.0f) != (x[i] < 0.0f);
            return n;
        };
        // The loudest point in [from, to), for finding a click.
        auto peakAt = [] (const std::vector<float>& x, size_t from, size_t to)
        {
            size_t best = from;
            for (size_t i = from; i < to && i < x.size(); ++i)
                if (std::abs (x[i]) > std::abs (x[best]))
                    best = i;
            return (double) best;
        };

        // Pitch is kept: a 440 Hz tone 1.5 times as long still crosses zero at 440 Hz.
        std::vector<float> tone (96000);
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = (float) (0.5 * std::sin (2.0 * M_PI * 440.0 * (double) i / rate));
        const auto stereo = build ({ tone, tone });
        std::string error;
        const auto longer = timestretch::stretch (*stereo, 0, stereo->length(), 1.5, timestretch::Mode::fullMix, error);
        check (longer != nullptr && longer->length() == 144000, "stretch 1.5x gives exactly 1.5 times the length: " + error);
        if (longer != nullptr)
        {
            const auto l = channel (*longer, 0), r = channel (*longer, 1);
            const auto hz = crossings (l, 24000, 120000) / 2.0 / 2.0;
            check (std::abs (hz - 440.0) < 3.0, "the stretched tone stays at 440 Hz: " + std::to_string (hz));
            double diff = 0.0;
            for (size_t i = 0; i < l.size(); ++i)
                diff = std::max (diff, (double) std::abs (l[i] - r[i]));
            check (diff < 1e-4, "equal channels stay equal (stretched together)");
        }

        // Varispeed: twice as long and an octave lower.
        const auto slow = timestretch::stretch (*stereo, 0, stereo->length(), 2.0, timestretch::Mode::varispeed, error);
        check (slow != nullptr && slow->length() == 192000, "varispeed 2x doubles the length");
        if (slow != nullptr)
        {
            const auto hz = crossings (channel (*slow, 0), 24000, 168000) / 3.0 / 2.0;
            check (std::abs (hz - 220.0) < 2.0, "varispeed 2x plays an octave lower: " + std::to_string (hz));
        }

        // Anchors: clicks every half second; the second click is moved from 0.5 s to 0.6 s.
        std::vector<float> clicks (96000, 0.0f);
        for (size_t at = 4800; at < clicks.size(); at += 24000)
            for (size_t i = 0; i < 48; ++i)
                clicks[at + i] = (float) (0.9 * std::exp (-(double) i / 8.0) * (i % 2 == 0 ? 1.0 : -1.0));
        const auto drums = build ({ clicks });
        const auto warped = timestretch::render (*drums, 0, drums->length(), drums->length(), { { 28800, 33600 }, { 52800, 52800 } },
                                                 timestretch::Mode::drums, error);
        check (warped != nullptr && warped->length() == drums->length(), "a warp keeps the result length asked for");
        if (warped != nullptr)
        {
            const auto x = channel (*warped, 0);
            const auto first = peakAt (x, 0, 16000), second = peakAt (x, 26000, 42000), third = peakAt (x, 48000, 62000);
            check (std::abs (first - 5600.0) < 150.0, "the click before the anchor moves with its stretch, to 5600: " + std::to_string (first));
            check (std::abs (second - 33600.0) < 150.0, "the anchored click lands at 0.7 s: " + std::to_string (second));
            check (std::abs (third - 52800.0) < 150.0, "the click at the next anchor stays put: " + std::to_string (third));
        }

        // The grid: beats at 120 BPM with jitter and a missed beat, bars starting on the third beat.
        std::vector<double> beats;
        std::vector<int> inBar;
        for (int i = 0; i < 40; ++i)
            if (i != 7)
            {
                beats.push_back (0.25 + i * 0.5 + ((i * 37) % 5 - 2) * 0.002);
                inBar.push_back ((i + 2) % 4 + 1);
            }
        const auto grid = beatgrid::fit (beats, inBar);
        check (std::abs (grid.bpm - 120.0) < 0.1, "the grid's tempo is 120 BPM: " + std::to_string (grid.bpm));
        check (std::abs (grid.downbeat - 1.25) < 0.01, "the downbeat is the first beat 1, at 1.25 s: " + std::to_string (grid.downbeat));
        check (std::abs (grid.snap (3.3, 1.0) - 3.25) < 0.01 && std::abs (grid.snap (3.3, 4.0) - 3.25) < 0.01,
               "3.3 s snaps to the beat and the bar at 3.25 s");
        check (std::abs (grid.beatAt (2.25) - 2.0) < 0.01, "2.25 s is two beats after the downbeat");
        check (! beatgrid::fit ({ 0.1, 0.6 }, {}).isSet(), "two beats make no grid");

        // A 4-bar break at 155 BPM with ghost notes on sixteenths reads as 155, not 77.5;
        // straight eighths at 130 BPM over 2 bars read as 130; random hits read as nothing.
        {
            const auto step = 15.0 / 155.0;
            std::vector<double> hits;
            for (int bar = 0; bar < 4; ++bar)
                for (const int st : { 0, 4, 7, 9, 10, 12, 15 })
                    hits.push_back ((bar * 16 + st) * step + 0.004);
            const auto loop = beatgrid::fromLoop (64 * step, hits);
            check (std::abs (loop.bpm - 155.0) < 0.01, "a busy break reads at full speed");
            check (std::abs (beatgrid::fromLoop (64 * step, hits, 154.0).bpm - 155.0) < 0.01, "the tracker's tempo picks among fitting ones");
            std::vector<double> eighths;
            for (int i = 0; i < 16; ++i)
                eighths.push_back (i * 30.0 / 130.0);
            check (std::abs (beatgrid::fromLoop (8 * 60.0 / 130.0, eighths, 130.0).bpm - 130.0) < 0.01, "the tracker's tempo wins a tie");
            std::vector<double> scattered { 0.0, 0.31, 0.77, 1.13, 1.52, 2.05, 2.41, 2.93, 3.37, 3.61 };
            check (! beatgrid::fromLoop (4.0, scattered).isSet(), "scattered hits make no loop grid");
        }
        {
            std::vector<float> hitsAudio ((size_t) rate * 2, 0.0f);
            for (const double t : { 0.2, 0.7, 1.3 })
                for (size_t i = 0; i < 2000; ++i)
                    hitsAudio[(size_t) (t * rate) + i] = (float) (0.8 * std::exp (-(double) i / 300.0) * std::sin (0.3 * (double) i));
            const auto found = timestretch::transients (*build ({ hitsAudio }));
            check (found.size() == 3 && std::abs ((double) found[1] / rate - 0.7) < 0.004, "transients finds the three hits: " + std::to_string (found.size()));
        }

        // Aligning: kicks on beat 1 at 0.25 s + 2 s * n, hats on the other beats; the grid
        // starts 20 ms early with bar 1 on a hat, and ends on the kicks.
        {
            std::vector<float> loop ((size_t) (rate * 9.0), 0.0f);
            std::mt19937 noise (7);
            std::uniform_real_distribution<float> uniform (-1.0f, 1.0f);
            for (int b = 0; b < 16; ++b)
            {
                const auto at = (size_t) ((0.25 + b * 0.5) * rate);
                for (size_t i = 0; i < (size_t) (0.2 * rate) && at + i < loop.size(); ++i)
                {
                    const auto t = (double) i / rate;
                    loop[at + i] += b % 4 == 0 ? (float) (0.8 * std::exp (-t * 15.0) * std::sin (2.0 * M_PI * 60.0 * t))
                                               : (float) (0.3 * std::exp (-t * 90.0)) * uniform (noise);
                }
            }
            BeatGrid off;
            off.bpm = 120.0;
            off.downbeat = 0.75 - 0.02;
            const auto aligned = beatgrid::align (*build ({ loop }), off, true);
            check (std::abs (aligned.downbeat - 0.25) < 0.006, "align puts bar 1 on the kick at 0.25 s: " + std::to_string (aligned.downbeat));
        }

        // Quantize: hits 20 ms late on sixteenths (125 ms apart) at 120 BPM move onto the grid (or half way at 50 %).
        {
            BeatGrid g;
            g.bpm = 120.0;
            std::vector<Frame> hits;
            for (int i = 0; i < 16; ++i)
                hits.push_back ((Frame) ((i * 0.125 + 0.02) * rate));
            timestretch::Quantize q;
            const auto full = timestretch::quantize (hits, g, rate, q, 0, (Frame) (10 * rate));
            check (full.size() == 16 && full[3].second == (Frame) (0.375 * rate), "quantize 100 % puts each hit on its sixteenth");
            q.strength = 0.5;
            const auto half = timestretch::quantize (hits, g, rate, q, 0, (Frame) (10 * rate));
            check (! half.empty() && half[3].second == (Frame) ((0.375 + 0.01) * rate), "quantize 50 % moves each hit half way");
            q.strength = 1.0;
            q.leaveWithin = 0.25;  // a sixteenth is 125 ms: hits within 15.6 ms stay
            check (timestretch::quantize (hits, g, rate, q, 0, (Frame) (10 * rate)).size() == 16, "20 ms off is outside a 15.6 ms leave-within");
            q.leaveWithin = 0.4;   // within 25 ms stay
            check (timestretch::quantize (hits, g, rate, q, 0, (Frame) (10 * rate)).empty(), "20 ms off is inside a 25 ms leave-within");
            q.leaveWithin = 0.0;
            q.reach = 0.25;        // only hits within 15.6 ms move
            check (timestretch::quantize (hits, g, rate, q, 0, (Frame) (10 * rate)).empty(), "20 ms off is beyond a 15.6 ms reach");
            q.reach = 1.0;
            q.swing = 1.0;
            const auto swung = timestretch::quantize (hits, g, rate, q, 0, (Frame) (10 * rate));
            check (swung.size() >= 2 && std::abs ((double) swung[1].second / rate - (0.125 + 0.125 / 3.0)) < 1e-3, "full swing puts the second sixteenth a third of a step late");
        }

        // The Quantize panel's engine: grooves, iterative quantizing, randomizing, filtering hits.
        {
            BeatGrid g;
            g.bpm = 120.0;   // a sixteenth is 125 ms
            const auto mpc = audioquantize::swing (0.25, 58.0);
            check (mpc.offsets.size() == 2 && std::abs (mpc.offsets[1] - 0.16) < 1e-9, "58 % swing puts the offbeat 0.16 of a step late");
            audioquantize::Settings s;
            s.groove = mpc;
            check (std::abs (audioquantize::lineFor (0.27, s) - (0.25 + 0.16 * 0.25)) < 1e-9, "a grooved offbeat line is late");
            check (std::abs (audioquantize::lineFor (0.52, s) - 0.5) < 1e-9, "a grooved downbeat line stays");

            // Iterative: a hit 40 ms late, already warped half way (now 20 ms late), goes half way again from there.
            std::vector<audioquantize::Hit> hits { { (Frame) (0.165 * rate), (Frame) (0.145 * rate) } };
            audioquantize::Settings half;
            half.strength = 0.5;
            const auto again = audioquantize::place (hits, g, rate, half, 0, (Frame) rate);
            check (again.size() == 1 && again[0].first == hits[0].source && again[0].second == (Frame) std::llround (0.135 * rate),
                   "iterative quantize moves half way from where the hit is now");
            // A warped hit outside the catch range keeps its place.
            half.reach = 0.1;
            const auto kept = audioquantize::place (hits, g, rate, half, 0, (Frame) rate);
            check (kept.size() == 1 && kept[0].second == hits[0].now, "a warped hit left alone keeps where it was warped to");

            // Randomize: within the range, and the same every time for a seed.
            std::vector<audioquantize::Hit> line;
            for (int i = 0; i < 32; ++i)
                line.push_back ({ (Frame) ((i * 0.125 + 0.03) * rate), (Frame) ((i * 0.125 + 0.03) * rate) });
            audioquantize::Settings random;
            random.randomMs = 5.0;
            const auto r1 = audioquantize::place (line, g, rate, random, 0, (Frame) (10 * rate));
            const auto r2 = audioquantize::place (line, g, rate, random, 0, (Frame) (10 * rate));
            bool inside = r1.size() == 32, moved = false;
            for (size_t i = 0; i < r1.size(); ++i)
            {
                const auto off = (double) r1[i].second / rate - (double) i * 0.125;
                inside = inside && std::abs (off) <= 0.0051;
                moved = moved || std::abs (off) > 0.0005;
            }
            check (inside && moved && r1 == r2, "randomize lands each hit within 5 ms of its line, repeatably");

            // Only the hits on eighths.
            std::vector<Frame> mixed;
            for (int i = 0; i < 8; ++i)
                mixed.push_back ((Frame) (i * 0.125 * rate));
            check (audioquantize::onGrid (mixed, g, rate, 0.5).size() == 4, "the eighths filter keeps every other sixteenth");

            // A groove taken from a performance with late offbeats, and back through text.
            std::vector<Frame> played;
            for (int i = 0; i < 32; ++i)
                played.push_back ((Frame) ((i * 0.125 + (i % 2 == 1 ? 0.02 : 0.0)) * rate));
            const auto feel = audioquantize::extract (played, g, rate, 0.25, 2, 0, (Frame) (10 * rate));
            check (feel.offsets.size() == 2 && std::abs (feel.offsets[0]) < 0.01 && std::abs (feel.offsets[1] - 0.16) < 0.01,
                   "extracting a groove finds offbeats 20 ms (0.16 of a step) late");
            const auto back = audioquantize::fromText (audioquantize::toText (feel));
            check (back.offsets.size() == 2 && std::abs (back.offsets[1] - feel.offsets[1]) < 1e-6 && back.division == 0.25,
                   "a groove survives its text form");
            check (! audioquantize::fromText ("nonsense").isSet(), "unreadable groove text gives no groove");
        }

        // Spectral hitpoints find notes that change without getting louder; Level does not.
        {
            std::vector<float> legato ((size_t) (rate * 1.5));
            double phase = 0.0;
            for (size_t i = 0; i < legato.size(); ++i)
            {
                const auto t = (double) i / rate;
                phase += 2.0 * M_PI * (t < 0.5 ? 220.0 : t < 1.0 ? 330.0 : 262.0) / rate;
                legato[i] = (float) (0.5 * std::sin (phase));
            }
            const auto notes = build ({ legato });
            audioquantize::Detection d;
            check (audioquantize::hits (*notes, d).empty(), "level finds no hits in a steady legato line");
            d.method = audioquantize::Method::spectral;
            const auto found = audioquantize::hits (*notes, d);
            bool both = found.size() == 2;
            for (size_t i = 0; both && i < 2; ++i)
                both = std::abs ((double) found[i] / rate - 0.5 * (double) (i + 1)) < 0.015;
            check (both, "spectral finds the two note changes: " + std::to_string (found.size()));
        }

        // Beats: no stretching, so the clicks keep their exact shape at their new places.
        {
            const auto beats = timestretch::render (*drums, 0, drums->length(), drums->length(), { { 28800, 33600 }, { 52800, 52800 } },
                                                    timestretch::Mode::beats, error);
            check (beats != nullptr && beats->length() == drums->length(), "beats keeps the length asked for");
            if (beats != nullptr)
            {
                const auto x = channel (*beats, 0);
                const auto second = peakAt (x, 26000, 42000);
                check (std::abs (second - 33600.0) <= 2.0, "beats puts the anchored click at 0.7 s exactly: " + std::to_string (second));
                check (std::abs (std::abs (x[(size_t) second]) - 0.9f) < 1e-3, "the click is copied, not stretched");
            }
            // Twice as long with Beats: each click once per gap, the tails looped.
            const auto longer = timestretch::stretch (*drums, 0, drums->length(), 2.0, timestretch::Mode::beats, error);
            check (longer != nullptr && longer->length() == 2 * drums->length(), "beats stretches the length");
            if (longer != nullptr)
            {
                const auto x = channel (*longer, 0);
                check (std::abs (peakAt (x, 50000, 60000) - 57600.0) <= 50.0, "the second click starts at twice its time");
            }
        }

        // Texture and Smooth: the lengths asked for, and a tone keeps about its level.
        for (const auto mode : { timestretch::Mode::texture, timestretch::Mode::smooth })
        {
            const auto out = timestretch::stretch (*stereo, 0, stereo->length(), 1.5, mode, error);
            check (out != nullptr && out->length() == 144000, std::string (timestretch::modeName (mode)) + " stretches to the length asked for");
            if (out != nullptr)
            {
                const auto l = channel (*out, 0);
                double sum = 0.0;
                for (size_t i = 24000; i < 120000; ++i)
                    sum += (double) l[i] * l[i];
                const auto rms = std::sqrt (sum / 96000.0);
                check (rms > 0.25 && rms < 0.5, std::string (timestretch::modeName (mode)) + " keeps the tone's level: " + std::to_string (rms));
            }
        }
    }

    // An FX bus effect for the tests: doubles what goes through.
    struct DoubleEffect final : patchbay::Effect
    {
        std::string name() const override { return "Double"; }
        void prepare (double, int) override {}
        void process (float* left, float* right, int frames) override
        {
            for (int i = 0; i < frames; ++i)
            {
                left[i] *= 2.0f;
                right[i] *= 2.0f;
            }
        }
    };

    void testPatchBay()
    {
        using namespace patchbay;
        const double rate = 48000.0;
        sampler::Engine samplerEngine;
        slicer::Engine slicerEngine;
        samplerEngine.prepare (rate);
        slicerEngine.prepare (rate);
        mixer::Engine m;
        m.prepare (rate);
        m.setNumTracks (2);
        PatchBay bay (samplerEngine, slicerEngine, &m);
        bay.prepare (rate);

        std::vector<float> l (512), r (512);
        auto render = [&] (float in)
        {
            std::fill (l.begin(), l.end(), in);
            std::fill (r.begin(), r.end(), in);
            bay.render (l.data(), r.data(), 512);
        };

        check (bay.isDefault(), "patchbay: starts with every source where it is heard without one");
        render (0.25f);
        check (near (l[300], 0.25, 1.0e-6) && near (r[300], 0.25, 1.0e-6), "patchbay: the editor reaches the Main Out unchanged");

        check (bay.check (node::fxBus, node::fxBus) == Refusal::self, "patchbay: nothing is patched into itself");
        check (bay.check (node::mainOut, node::fxBus) == Refusal::noPort, "patchbay: the Main Out has no output");
        check (bay.check (node::fxBus, node::sampler) == Refusal::noPort, "patchbay: the Sampler has no input");
        check (bay.connect (node::track, node::fxBus) == Refusal::none, "patchbay: a track into an FX bus");
        check (bay.connect (node::fxBus, node::track + 1) == Refusal::none, "patchbay: an FX bus into another track");
        check (bay.check (node::track + 1, node::track) == Refusal::loop, "patchbay: a patch that would feed back is refused");
        check (bay.connect (node::track + 1, node::track) == Refusal::loop && ! bay.linked (node::track + 1, node::track), "patchbay: a refused patch is not made");
        check (bay.check (node::track + 1, node::mixBus) == Refusal::none, "patchbay: the rest stays open");

        // Editor -> FX bus (doubling, -6 dB on the way in) -> Main Out, and nothing else heard.
        bay.resetLinks();
        bay.setEffect (0, std::make_shared<DoubleEffect>());
        bay.disconnect (node::editor, node::mainOut);
        bay.connect (node::editor, node::fxBus, -6.0206f);
        render (0.25f);
        render (0.25f);
        check (near (l[300], 0.25, 1.0e-4) && near (r[300], 0.25, 1.0e-4), "patchbay: a patch's gain and the FX bus's effect");
        check (! bay.isDefault(), "patchbay: knows when it has been changed");
        bay.setGainDb (node::editor, node::fxBus, 0.0f);
        render (0.25f);
        render (0.25f);
        check (near (l[300], 0.5, 1.0e-4), "patchbay: a gain changes without patching again");

        // Ordering: the editor into track 2, track 2 into the FX bus, the FX bus into track 1,
        // track 1 to the Main Out: track 2 has to play before track 1, in the same block.
        bay.setLinks ({ { node::editor, node::track + 1 }, { node::track + 1, node::fxBus }, { node::fxBus, node::track },
                        { node::track, node::mainOut } });
        render (0.25f);   // new patches fade in over a block
        render (0.25f);
        render (0.5f);
        check (near (l[0], 1.0, 1.0e-4) && near (l[511], 1.0, 1.0e-4) && near (r[300], 1.0, 1.0e-4),
               "patchbay: tracks play in the order of their patches, with no block of delay");
        m.strip (0).faderDb = -6.0206f;
        render (0.25f);
        render (0.25f);
        check (near (l[300], 0.25, 1.0e-4), "patchbay: a patched track goes through its fader");
        m.strip (0).faderDb = 0.0f;
        m.strip (0).mute = true;
        render (0.25f);
        render (0.25f);
        check (std::abs (l[300]) < 1.0e-6, "patchbay: and its mute");
        m.strip (0).mute = false;

        // A track's own audio, routed away from the Mix Bus, is heard only where it is patched.
        AudioData::Builder b (1, rate);
        std::vector<float> half (48000, 0.5f);
        const float* one[1] = { half.data() };
        b.append (one, 48000);
        m.setClips (0, { mixer::makeClip (mixer::makeAudioClip (*b.finish(), "dc.wav"), 0) });
        bay.resetLinks();
        bay.disconnect (node::editor, node::mainOut);
        m.play();
        render (0.0f);
        render (0.0f);
        check (near (l[300], 0.5 * std::sqrt (0.5), 1.0e-4), "patchbay: by default a track goes through the Mix Bus to the Main Out");
        m.masterDb = -6.0206f;
        render (0.0f);
        render (0.0f);
        check (near (l[300], 0.25 * std::sqrt (0.5), 1.0e-4), "patchbay: the Mix Bus follows the mixer's master fader");
        m.masterDb = 0.0f;
        bay.disconnect (node::track, node::mixBus);
        bay.connect (node::track, node::fxBus);
        render (0.0f);
        render (0.0f);
        check (near (l[300], std::sqrt (0.5), 1.0e-4), "patchbay: a track sent to an FX bus instead of the Mix Bus");
        check (bay.takePeak (node::track) > 0.3f && bay.takePeak (node::track) == 0.0f, "patchbay: peaks are taken and reset");
        m.stop();

        // Timed messages (the Tracker's linked instruments) reach a synth at their frame,
        // across the patchbay's chunks and into later render calls.
        struct Recorder final : mixer::Instrument
        {
            Kind kind() const override { return Kind::plugin; }
            std::string name() const override { return "Recorder"; }
            void prepare (double, int) override {}
            void process (const mixer::NoteEvent* events, int numEvents, float*, float*, int frames) override
            {
                for (int i = 0; i < numEvents; ++i)
                    heard.emplace_back (at + events[i].offset, events[i].data1);
                at += frames;
            }
            void releaseAll() override {}
            void reset() override {}
            long at = 0;
            std::vector<std::pair<long, int>> heard;
        };
        auto recorder = std::make_shared<Recorder>();
        bay.setSynth (1, recorder);
        render (0.0f);
        const auto start = recorder->at;
        bay.scheduleMidi (1, 700, 0x80, 62, 0);
        bay.scheduleMidi (1, 10, 0x90, 60, 100);
        bay.scheduleMidi (1, 300, 0x90, 61, 100);
        render (0.0f);
        render (0.0f);
        check (recorder->heard.size() == 3 && recorder->heard[0] == std::make_pair (start + 10, 60)
                   && recorder->heard[1] == std::make_pair (start + 300, 61) && recorder->heard[2] == std::make_pair (start + 700, 62),
               "patchbay: timed messages play at their frame, in later chunks and render calls too");
        bay.setSynth (1, nullptr);
    }

    // A Prism with nothing in its rays gives back what went in, in every split.
    void testPrismSplit()
    {
        constexpr int channels = 2, frames = 48000, rays = 4;
        const double rate = 48000.0;
        std::vector<std::vector<float>> input (channels, std::vector<float> (frames));
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
        for (auto& c : input)
            for (auto& v : c)
                v = noise (rng);

        std::vector<std::vector<std::vector<float>>> rayData (rays, std::vector<std::vector<float>> (channels, std::vector<float> (frames)));
        std::vector<std::vector<float*>> rayChannels (rays);
        std::vector<float* const*> rayPtrs (rays);
        std::vector<const float* const*> rayReadPtrs (rays);
        for (int r = 0; r < rays; ++r)
        {
            for (auto& c : rayData[(size_t) r])
                rayChannels[(size_t) r].push_back (c.data());
            rayPtrs[(size_t) r] = rayChannels[(size_t) r].data();
            rayReadPtrs[(size_t) r] = rayChannels[(size_t) r].data();
        }
        std::vector<const float*> in { input[0].data(), input[1].data() };
        std::vector<std::vector<float>> output (channels, std::vector<float> (frames));
        std::vector<float*> out { output[0].data(), output[1].data() };
        const float unity[rays] { 1.0f, 1.0f, 1.0f, 1.0f };

        for (auto mode : { prism::Split::parallel, prism::Split::midSide, prism::Split::leftRight })
        {
            prism::Splitter s;
            s.prepare (rate, channels);
            s.split (mode, in.data(), channels, frames, rayPtrs.data(), rays);
            // Parallel adds the rays, so with gains of 1/n the sum is the input again.
            const auto heard = prism::raysHeard (mode, rays);
            float gains[rays];
            for (int r = 0; r < rays; ++r)
                gains[r] = mode == prism::Split::parallel ? 1.0f / (float) heard : 1.0f;
            prism::Splitter::join (mode, rayReadPtrs.data(), gains, rays, out.data(), channels, frames);
            float worst = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < frames; ++i)
                    worst = std::max (worst, std::abs (output[(size_t) ch][(size_t) i] - input[(size_t) ch][(size_t) i]));
            check (worst < 1.0e-5f, std::string ("prism ") + prism::splitName (mode) + " gives back its input");
        }

        // Mid/side: the side ray alone holds L - R.
        {
            prism::Splitter s;
            s.prepare (rate, channels);
            s.split (prism::Split::midSide, in.data(), channels, frames, rayPtrs.data(), rays);
            check (near (rayData[1][0][100], 0.5 * (input[0][100] - input[1][100]), 1.0e-6), "prism side ray holds the side");
            check (near (rayData[0][1][100], 0.5 * (input[0][100] + input[1][100]), 1.0e-6), "prism mid ray holds the mid");
            check (rayData[2][0][100] == 0.0f, "prism mid/side leaves ray 3 silent");
        }

        // Bands: each band is a part of the spectrum, and all four add back to the same level at every frequency.
        for (int bands = 2; bands <= 4; ++bands)
        {
            const double crossovers[] { 150.0, 1200.0, 6000.0 };
            for (double hz : { 40.0, 150.0, 500.0, 1200.0, 3000.0, 6000.0, 14000.0 })
            {
                prism::Splitter s;
                s.prepare (rate, 1);
                s.setBands (bands, crossovers);
                std::vector<float> sine (frames);
                for (int i = 0; i < frames; ++i)
                    sine[(size_t) i] = (float) std::sin (2.0 * M_PI * hz * i / rate);
                const float* sineIn[] { sine.data() };
                s.split (prism::Split::bands, sineIn, 1, frames, rayPtrs.data(), bands);
                float* mono[] { output[0].data() };
                prism::Splitter::join (prism::Split::bands, rayReadPtrs.data(), unity, bands, mono, 1, frames);
                // RMS, not the peak: a sampled sine seldom lands on its crest.
                double power = 0.0;
                for (int i = frames / 2; i < frames; ++i)
                    power += (double) output[0][(size_t) i] * output[0][(size_t) i];
                const auto rms = std::sqrt (power / (frames / 2)) * M_SQRT2;
                check (near (20.0 * std::log10 (rms), 0.0, 0.02),
                       "prism " + std::to_string (bands) + " bands add back flat at " + std::to_string ((int) hz) + " Hz");
            }
        }

        // The lowest band of two holds the low tone, the highest band the high one.
        {
            prism::Splitter s;
            s.prepare (rate, 1);
            const double crossover[] { 1000.0 };
            s.setBands (2, crossover);
            std::vector<float> low (frames);
            for (int i = 0; i < frames; ++i)
                low[(size_t) i] = (float) std::sin (2.0 * M_PI * 60.0 * i / rate);
            const float* lowIn[] { low.data() };
            s.split (prism::Split::bands, lowIn, 1, frames, rayPtrs.data(), 2);
            float lowPeak = 0.0f, highPeak = 0.0f;
            for (int i = frames / 2; i < frames; ++i)
            {
                lowPeak = std::max (lowPeak, std::abs (rayData[0][0][(size_t) i]));
                highPeak = std::max (highPeak, std::abs (rayData[1][0][(size_t) i]));
            }
            check (lowPeak > 0.99f && highPeak < 0.01f, "prism low tone goes to the low band");
        }

        // Delay lines rays up by whole samples.
        {
            prism::Delay d;
            d.prepare (1, 64);
            std::vector<float> block (16, 0.0f);
            block[0] = 1.0f;
            float* io[] { block.data() };
            d.process (io, 1, 16, 5);
            check (block[5] == 1.0f && block[0] == 0.0f, "prism delay of 5 samples");
        }
    }

    void testMasterAssist()
    {
        // Measuring: a 1 kHz sine, the same on both sides, at half scale.
        const auto sine = makeSine (2, 48000 * 10, 1000.0, 0.5);
        const auto p = assist::measure (*sine, 0, sine->length());
        check (p.valid, "assist: a sine is measured");
        check (near (p.lufs, -6.0, 0.5), "assist: loudness of a half-scale stereo sine is about -6 LUFS");
        check (near (p.plr, p.truePeakDb - p.lufs, 1.0e-9), "assist: PLR is true peak minus loudness");
        check (p.sideToMidDb < -100.0, "assist: identical sides have no side");
        const auto loudest = std::max_element (p.bandsDb.begin(), p.bandsDb.end()) - p.bandsDb.begin();
        check (loudest == 5, "assist: a 1 kHz sine is loudest in the 1 kHz octave");
        double mean = 0.0;
        for (auto b : p.bandsDb)
            mean += b;
        check (near (mean, 0.0, 1.0e-6), "assist: octave levels are against their mean");

        const auto silence = makeSine (2, 48000, 1000.0, 0.0);
        check (! assist::measure (*silence, 0, silence->length()).valid, "assist: silence is not a valid measure");

        // Tone curves are centred, and warm has more low end than bright.
        for (auto tone : { assist::Tone::balanced, assist::Tone::warm, assist::Tone::bright })
        {
            const auto c = assist::toneCurve (tone);
            double sum = 0.0;
            for (auto v : c)
                sum += v;
            check (near (sum, 0.0, 1.0e-9), "assist: a tone curve is centred on 0 dB");
        }
        check (assist::toneCurve (assist::Tone::warm)[1] > assist::toneCurve (assist::Tone::bright)[1],
               "assist: warm has more 63 Hz than bright");

        // Residuals: the peak counts only above the ceiling, the width not for mono.
        assist::Target t;
        t.lufs = -14.0;
        t.truePeakDb = -1.0;
        t.widthWeight = 1.0;
        t.sideToMidDb = -10.0;
        assist::Profile got;
        got.valid = true;
        got.lufs = -16.0;
        got.truePeakDb = -3.0;
        auto r = assist::residuals (got, t);
        check (r.size() == 4 + assist::Profile::numBands, "assist: residuals have a fixed length");
        check (near (r[0], -2.0, 1.0e-9), "assist: 2 LU too quiet");
        check (r[1] == 0.0, "assist: a peak under the ceiling costs nothing");
        check (r[3] == 0.0, "assist: no width residual for mono");
        got.truePeakDb = 0.0;
        check (near (assist::residuals (got, t)[1], 2.0, 1.0e-9), "assist: 1 dB over the ceiling, weighted 2");

        // An excerpt's target is shifted by what the excerpt differs from the whole by.
        assist::Profile whole = got, part = got;
        whole.lufs = -18.0;
        part.lufs = -16.0;
        check (near (t.forExcerpt (whole, part).lufs, -12.0, 1.0e-9), "assist: an excerpt 2 LU louder aims 2 LU higher");

        // Excerpts: two minutes, quiet but for 10 seconds at 60 s, come out shorter and with the loud part.
        {
            AudioData::Builder b (2, 48000.0);
            std::vector<float> l (48000), rr (48000);
            for (int sec = 0; sec < 120; ++sec)
            {
                const auto amp = sec >= 60 && sec < 70 ? 0.8f : 0.05f;
                for (int i = 0; i < 48000; ++i)
                    l[(size_t) i] = rr[(size_t) i] = amp * (float) std::sin (2.0 * M_PI * 220.0 * (sec * 48000 + i) / 48000.0);
                const float* ptrs[] = { l.data(), rr.data() };
                b.append (ptrs, 48000);
            }
            const auto song = b.finish();
            const auto ex = assist::excerpt (*song, 0, song->length(), 30.0);
            check (ex->lengthSeconds() <= 31.0 && ex->lengthSeconds() >= 18.0, "assist: an excerpt is about 30 seconds");
            check (ex->peakAbs (0, ex->length()) > 0.7f, "assist: an excerpt holds the loudest part");
            const auto shortOne = assist::excerpt (*sine, 0, sine->length(), 30.0);
            check (shortOne->length() == sine->length(), "assist: a short file is its own excerpt");
        }

        // Which parameters move by default.
        check (assist::guessRole ("Threshold (dB)") == assist::Role::dynamics, "assist: threshold is dynamics");
        check (assist::guessRole ("Output Ceiling") == assist::Role::ceiling, "assist: ceiling");
        check (assist::guessRole ("Attack") == assist::Role::time, "assist: attack is a time");
        check (assist::guessRole ("Gain 3") == assist::Role::eq, "assist: a numbered gain is an EQ band");
        check (assist::guessRole ("Freq 3") == assist::Role::other, "assist: a frequency is left alone");
        check (assist::guessRole ("Input Gain") == assist::Role::level, "assist: input gain is a level");
        check (assist::movedByDefault ("Input Gain", 0, false), "assist: input gain moves");
        check (assist::movedByDefault ("Width", 0, false), "assist: width moves");
        check (assist::movedByDefault ("Ratio", 0, false), "assist: ratio moves");
        check (! assist::movedByDefault ("Ceiling", 0, false), "assist: the ceiling stays yours");
        check (! assist::movedByDefault ("Release", 0, false), "assist: release stays");
        check (! assist::movedByDefault ("Bypass", 0, true), "assist: switches stay");
        check (! assist::movedByDefault ("Gain 2", 12, false), "assist: stepped parameters stay");

        // A knob only moves for its own job.
        {
            assist::Target loudOnly;
            check (assist::policy (assist::Role::level, loudOnly, 0.3).moves, "assist: a gain moves for loudness");
            check (assist::policy (assist::Role::level, loudOnly, 0.3).freedom == 1.0, "assist: a gain may go as far as it needs");
            check (! assist::policy (assist::Role::width, loudOnly, 0.3).moves, "assist: width does not move to get louder");
            check (! assist::policy (assist::Role::eq, loudOnly, 0.3).moves, "assist: EQ does not move when tone is not matched");
            auto toned = loudOnly;
            toned.toneWeight = 0.5;
            check (assist::policy (assist::Role::eq, toned, 0.3).moves, "assist: EQ moves when tone is matched");
            check (assist::policy (assist::Role::time, loudOnly, 0.3).moves, "assist: a knob you tick yourself moves");
        }

        // The optimiser on a made-up chain: a gain knob (-24..+24 dB), a tilt knob, and a knob that does nothing.
        {
            assist::Settings s;
            s.maxEvaluations = 40;
            int calls = 0;
            const assist::Evaluate chain = [&] (const std::vector<double>& x, std::vector<double>& res)
            {
                ++calls;
                const auto gain = -24.0 + 48.0 * x[0];
                const auto tilt = std::tanh (4.0 * (x[1] - 0.5)) * 6.0;
                res = { (-20.0 + gain) - (-14.0), tilt - 1.5 };
                return true;
            };
            const auto result = assist::optimise ({ 0.5, 0.5, 0.5 }, chain, s);
            check (result.error < 0.05 * result.startError, "assist: the optimiser gets close to the target");
            check (near (-24.0 + 48.0 * result.values[0], 6.0, 0.3), "assist: the gain lands near +6 dB");
            check (result.values[2] == 0.5, "assist: a knob that does nothing is left where it was");
            check (calls <= s.maxEvaluations, "assist: the optimiser keeps to its budget");
            check (result.evaluations == calls, "assist: evaluations are counted");

            // It may not move further than its freedom.
            s.freedom = 0.05;
            const auto held = assist::optimise ({ 0.5, 0.5, 0.5 }, chain, s);
            check (held.values[0] <= 0.55 + 1.0e-9, "assist: a parameter stays within its freedom");

            // Cancelled from the first render: nothing moves.
            const assist::Evaluate cancelled = [] (const std::vector<double>&, std::vector<double>&) { return false; };
            const auto none = assist::optimise ({ 0.3 }, cancelled, s);
            check (none.stopped && none.values[0] == 0.3, "assist: a cancelled search changes nothing");
        }

        // End to end on real audio: a gain "plugin" on a quiet sine, aimed at -14 LUFS.
        {
            const auto quiet = makeSine (2, 48000 * 4, 440.0, 0.1);
            assist::Target target;
            target.lufs = -14.0;
            target.truePeakDb = 0.0;
            assist::Settings s;
            s.maxEvaluations = 20;
            const assist::Evaluate gainChain = [&] (const std::vector<double>& x, std::vector<double>& res)
            {
                const auto g = (float) std::pow (10.0, (-24.0 + 48.0 * x[0]) / 20.0);
                const auto out = quiet->processed (0, quiet->length(), [g] (std::vector<std::vector<float>>& block, Frame)
                {
                    for (auto& c : block)
                        for (auto& v : c)
                            v *= g;
                });
                res = assist::residuals (assist::measure (*out, 0, out->length()), target);
                return true;
            };
            const auto result = assist::optimise ({ 0.5 }, gainChain, s);
            const auto g = std::pow (10.0, (-24.0 + 48.0 * result.values[0]) / 20.0);
            const auto louder = quiet->processed (0, quiet->length(), [g] (std::vector<std::vector<float>>& block, Frame)
            {
                for (auto& c : block)
                    for (auto& v : c)
                        v *= (float) g;
            });
            check (near (assist::measure (*louder, 0, louder->length()).lufs, -14.0, 0.5), "assist: a quiet sine is brought to -14 LUFS");
        }
    }

    // The Φ effects' core: golden numbers, rhythms and the four processors.
    void testPhi()
    {
        check (near (phi::ratio * phi::inverse, 1.0, 1.0e-12) && near (phi::ratio - 1.0, phi::inverse, 1.0e-12), "phi times its inverse is 1");
        check (phi::fibonacci (0) == 0 && phi::fibonacci (10) == 55 && phi::fibonacci (20) == 6765, "fibonacci numbers");
        check (near ((double) phi::fibonacci (31) / (double) phi::fibonacci (30), phi::ratio, 1.0e-12), "fibonacci ratios approach phi");

        // The golden sequence spreads 89 values so every gap is one of at most three sizes.
        {
            std::vector<double> v;
            for (int n = 0; n < 89; ++n)
                v.push_back (phi::golden (n));
            std::sort (v.begin(), v.end());
            double smallest = 1.0, largest = 0.0;
            for (size_t i = 1; i < v.size(); ++i)
            {
                smallest = std::min (smallest, v[i] - v[i - 1]);
                largest = std::max (largest, v[i] - v[i - 1]);
            }
            check (smallest > 0.0 && largest / smallest < phi::ratio * phi::ratio + 1.0e-6, "golden sequence is evenly spread");
        }

        // The Fibonacci word: hits on 1/φ of the steps, starting on step 0, never periodic.
        {
            const std::string word = "1101101011011010";
            std::string got;
            for (int n = 0; n < (int) word.size(); ++n)
                got += phi::hit (n, phi::density (0)) ? '1' : '0';
            check (got == word, "phi rhythm is the Fibonacci word, got " + got);
            for (int d = 0; d < phi::numDensities; ++d)
            {
                const int steps = 10000;
                int hits = 0;
                for (int n = 0; n < steps; ++n)
                    hits += phi::hit (n, phi::density (d)) ? 1 : 0;
                check (std::abs (hits - steps * phi::density (d)) <= 1.0, "phi rhythm density " + std::to_string (d));
            }
            bool periodic = false;
            for (int period = 1; period <= 64 && ! periodic; ++period)
            {
                bool same = true;
                for (int n = 0; n < 2000 && same; ++n)
                    same = phi::hit (n, phi::density (0)) == phi::hit (n + period, phi::density (0));
                periodic = same;
            }
            check (! periodic, "phi rhythm never repeats");
        }

        const double rate = 48000.0;
        auto sine = [rate] (double hz, int frames)
        {
            std::vector<float> s ((size_t) frames);
            for (int i = 0; i < frames; ++i)
                s[(size_t) i] = (float) (0.5 * std::sin (2.0 * M_PI * hz * i / rate));
            return s;
        };
        auto rmsDb = [] (const std::vector<float>& s, size_t from)
        {
            double sum = 0.0;
            for (size_t i = from; i < s.size(); ++i)
                sum += (double) s[i] * s[i];
            return 10.0 * std::log10 (sum / (double) (s.size() - from));
        };

        // Φ EQ: bands φ apart from 80 Hz; a boost lands on its band, and flat is untouched.
        {
            check (near (phi::Eq::bandHz (0), 80.0, 1.0e-9) && near (phi::Eq::bandHz (11), 15920.4, 0.1)
                   && near (phi::Eq::bandHz (5) / phi::Eq::bandHz (4), phi::ratio, 1.0e-9), "phi EQ bands are golden steps from 80 Hz");
            phi::Eq eq;
            eq.prepare (rate, 1);
            auto flat = sine (1000.0, 4800);
            const auto original = flat;
            float* io[] { flat.data() };
            eq.process (io, 1, 4800);
            check (flat == original, "flat phi EQ leaves the sound alone");

            std::array<double, phi::Eq::numBands> gains {};
            gains[5] = 6.0;
            eq.set (gains);
            check (near (eq.responseDb (phi::Eq::bandHz (5)), 6.0, 0.05), "phi EQ bell reaches its gain");
            check (std::abs (eq.responseDb (phi::Eq::bandHz (2))) < 0.6, "phi EQ bell leaves bands three steps away nearly alone");
            auto tone = sine (phi::Eq::bandHz (5), 24000);
            const auto before = rmsDb (tone, 12000);
            float* t[] { tone.data() };
            eq.process (t, 1, 24000);
            check (near (rmsDb (tone, 12000) - before, 6.0, 0.1), "phi EQ boosts its band by 6 dB");
        }

        // Φ Echo: an impulse comes back at t, tφ, tφ²..., each 1/φ quieter.
        {
            phi::Echo echo;
            phi::Echo::Settings s;
            s.timeMs = 100.0;
            s.taps = 4;
            s.spread = 0.0;
            s.mix = 1.0;
            echo.prepare (rate, 1);
            echo.set (s);
            const int frames = (int) (rate * 0.5);
            std::vector<float> x ((size_t) frames, 0.0f);
            x[0] = 1.0f;
            float* io[] { x.data() };
            echo.process (io, 1, frames);
            bool ok = true;
            for (int t = 0; t < 4; ++t)
            {
                const auto at = (size_t) std::lround (phi::Echo::tapSeconds (s, t) * rate);
                ok = ok && near (x[at], std::pow (phi::inverse, t), 0.01);
            }
            check (ok, "phi echo taps at golden times with golden decay");
        }

        // Φ Space: the lines never share a period, and the tail dies away.
        {
            phi::Space space;
            space.prepare (rate);
            phi::Space::Settings s;
            s.decaySeconds = 1.0;
            s.mix = 1.0;
            space.set (s);
            bool distinct = true;
            for (int a = 0; a < phi::Space::numLines; ++a)
                for (int b = a + 1; b < phi::Space::numLines; ++b)
                    distinct = distinct && space.lineLength (a) != space.lineLength (b);
            check (distinct, "phi space lines all differ");

            const int frames = (int) (rate * 3.0);
            std::vector<float> l ((size_t) frames, 0.0f), r ((size_t) frames, 0.0f);
            l[0] = r[0] = 1.0f;
            float* io[] { l.data(), r.data() };
            space.process (io, 2, frames);
            double early = 0.0, late = 0.0;
            bool finite = true;
            for (int i = 0; i < frames; ++i)
            {
                finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                const auto power = (double) l[(size_t) i] * l[(size_t) i];
                if (i < (int) (rate * 0.5))
                    early += power;
                else if (i >= (int) (rate * 2.0))
                    late += power;
            }
            check (finite && early > 0.0 && late < early * 1.0e-4, "phi space decays");
        }

        // Φ Drift stays within its depth; tremolo never goes above unity.
        {
            double lowest = 1.0, highest = -1.0;
            for (int i = 0; i < 100000; ++i)
            {
                const auto v = phi::Drift::lfo (i * 0.001, 0.618, 0.0);
                lowest = std::min (lowest, v);
                highest = std::max (highest, v);
            }
            check (lowest >= -1.0 && highest <= 1.0 && highest - lowest > 1.0, "phi drift LFO spans its range");
            phi::Drift drift;
            drift.prepare (rate, 1);
            drift.set ({ phi::Drift::Mode::tremolo, 2.0, 1.0, 1.0 });
            std::vector<float> ones (48000, 1.0f);
            float* io[] { ones.data() };
            drift.process (io, 1, 48000);
            check (*std::max_element (ones.begin(), ones.end()) <= 1.0f && *std::min_element (ones.begin(), ones.end()) >= 0.0f,
                   "phi tremolo stays between silence and unity");
        }

        // Φ Rhythm: open on hits, closed between them.
        {
            phi::Rhythm gate;
            gate.prepare (rate);
            phi::Rhythm::Settings s;
            s.bpm = 120.0;           // a sixteenth is 6000 samples
            s.smoothMs = 0.5;
            gate.set (s);
            std::vector<float> ones (6000 * 16, 1.0f);
            float* io[] { ones.data() };
            gate.process (io, 1, (int) ones.size());
            bool ok = true;
            for (int step = 0; step < 16; ++step)
            {
                const auto v = ones[(size_t) step * 6000 + 5000];
                ok = ok && near (v, phi::hit (step, phi::density (0)) ? 1.0f : 0.0f, 0.01);
            }
            check (ok, "phi rhythm gate follows the pattern");
        }

        // Stereoize's Φ switch: golden allpasses instead of one delay. The side is a
        // different signal but as strong, and left plus right is still the mid.
        {
            auto run = [&] (bool golden, std::vector<float>& side)
            {
                dsp::Stereoizer z;
                z.prepare (rate);
                z.set ({ 1.0, 12.0, 0.0, golden });
                z.reset();
                std::mt19937 noise (7);
                std::normal_distribution<float> n (0.0f, 0.1f);
                side.resize (48000);
                for (auto& v : side)
                    v = z.process (n (noise));
            };
            std::vector<float> plain, golden;
            run (false, plain);
            run (true, golden);
            double pp = 0.0, gg = 0.0, pg = 0.0;
            for (size_t i = 4800; i < plain.size(); ++i)
            {
                pp += (double) plain[i] * plain[i];
                gg += (double) golden[i] * golden[i];
                pg += (double) plain[i] * golden[i];
            }
            check (near (10.0 * std::log10 (gg / pp), 0.0, 0.5), "golden stereoize keeps the side level");
            check (std::abs (pg) / std::sqrt (pp * gg) < 0.5, "golden stereoize makes a different side");

            auto mono = makeSine (1, 48000, 1000.0, 0.5);
            dsp::StereoizeSettings settings;
            settings.golden = true;
            const auto wide = dsp::stereoize (*mono, 0, mono->length(), settings);
            double maxSumError = 0.0;
            for (Frame f = 0; f < wide->length(); f += 37)
                maxSumError = std::max (maxSumError, std::abs ((double) (sampleAt (*wide, 0, f) + sampleAt (*wide, 1, f)) * 0.5
                                                               - sampleAt (*mono, 0, f)));
            check (maxSumError < 1.0e-6, "golden stereoize still folds back to the original mono");
        }

        // Goertzel: the level of one frequency in a signal, in dB.
        auto toneDb = [rate] (const std::vector<float>& x, size_t from, double hz)
        {
            const auto k = 2.0 * std::cos (2.0 * M_PI * hz / rate);
            double s1 = 0.0, s2 = 0.0;
            for (size_t i = from; i < x.size(); ++i)
            {
                const auto s0 = x[i] + k * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            const auto power = s1 * s1 + s2 * s2 - k * s1 * s2;
            return 10.0 * std::log10 (power / std::pow ((double) (x.size() - from), 2.0) + 1.0e-30);
        };

        // Φ Resonator lets its golden bands through and holds back what lies between them.
        {
            phi::Resonator::Settings s;
            s.rootHz = 220.0;
            s.width = 0.0;
            check (near (phi::Resonator::bandHz (s, 2), 220.0 * phi::ratio * phi::ratio, 1.0e-6), "phi resonator bands");
            auto level = [&] (double hz)
            {
                phi::Resonator r;
                r.prepare (rate, 1);
                r.set (s);
                auto x = sine (hz, 48000);
                float* io[] { x.data() };
                r.process (io, 1, 48000);
                return rmsDb (x, 24000);
            };
            const auto on = level (phi::Resonator::bandHz (s, 2)), between = level (phi::Resonator::bandHz (s, 2) * 1.27);
            check (on - between > 20.0, "phi resonator rings on its bands only");
        }

        // Φ Stack adds a copy a golden ratio above the sound.
        {
            phi::Stack stack;
            phi::Stack::Settings s;
            s.copies = 1;
            s.decay = 1.0;
            s.width = 0.0;
            s.toneHz = 20000.0;
            stack.prepare (rate, 1);
            stack.set (s);
            auto x = sine (200.0, 48000);
            float* io[] { x.data() };
            stack.process (io, 1, 48000);
            const auto golden = toneDb (x, 9600, 200.0 * phi::ratio), elsewhere = toneDb (x, 9600, 270.0);
            check (golden - elsewhere > 15.0, "phi stack adds a copy at 200 Hz x phi");
        }

        // Φ Tame finds a kick's fundamental and cuts at fundamental x φ ÷ 2 × 10.
        {
            check (near (phi::Tame::cutHz (58.9), 476.5, 0.1), "phi tame cut frequency");
            phi::Tame tame;
            phi::Tame::Settings s;
            s.depthDb = -9.0;
            tame.prepare (rate, 1);
            tame.set (s);
            const int frames = 96000;
            std::vector<float> x ((size_t) frames);
            for (int i = 0; i < frames; ++i)
                x[(size_t) i] = (float) (0.5 * std::sin (2.0 * M_PI * 58.9 * i / rate) + 0.05 * std::sin (2.0 * M_PI * 476.5 * i / rate));
            const auto before = toneDb (x, 72000, 476.5);
            float* io[] { x.data() };
            tame.process (io, 1, frames);
            check (near (tame.fundamental(), 58.9, 1.0), "phi tame finds the fundamental, got " + std::to_string (tame.fundamental()));
            check (near (toneDb (x, 72000, 476.5) - before, -9.0, 1.0), "phi tame cuts at the golden point");
        }

        // Φ De-resonator: transparent at no depth (only delayed), turns a ringing tone
        // down, and leaves plain noise nearly alone.
        {
            std::mt19937 random (11);
            std::normal_distribution<float> gauss (0.0f, 0.1f);
            const int frames = 48000;
            std::vector<float> noise ((size_t) frames);
            for (auto& v : noise)
                v = gauss (random);

            phi::Deresonator d;
            phi::Deresonator::Settings s;
            s.depth = 0.0;
            d.prepare (rate, 1);
            d.set (s);
            auto x = noise;
            float* io[] { x.data() };
            d.process (io, 1, frames);
            double error = 0.0;
            for (int i = d.latency() + 4096; i < frames; ++i)
                error = std::max (error, (double) std::abs (x[(size_t) i] - noise[(size_t) (i - d.latency())]));
            check (error < 1.0e-4, "phi de-resonator at no depth gives the sound back, delayed by its latency");

            auto run = [&] (std::vector<float> y)
            {
                phi::Deresonator r;
                phi::Deresonator::Settings t;
                t.depth = 1.0;
                r.prepare (rate, 1);
                r.set (t);
                float* p[] { y.data() };
                r.process (p, 1, (int) y.size());
                return y;
            };
            auto ringing = noise;
            for (int i = 0; i < frames; ++i)
                ringing[(size_t) i] += (float) (0.3 * std::sin (2.0 * M_PI * 1000.0 * i / rate));
            const auto tamed = run (ringing);
            check (toneDb (tamed, 24000, 1000.0) - toneDb (ringing, 24000, 1000.0) < -6.0, "phi de-resonator turns a resonance down");
            const auto plain = run (noise);
            check (std::abs (rmsDb (plain, 24000) - rmsDb (noise, 24000)) < 1.5, "phi de-resonator leaves noise nearly alone");
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
    testSpectrumView();
    testGenerate();
    testPitch();
    testPitchFix();
    testTimeStretch();
    testBinaural();
    testAmbisonics();
    testRepair();
    testRestoration();
    testShapes();
    testConvert();
    testMetadata();
    testXmp();
    testIxml();
    testRiffChunks();
    testNyquist();
    testAlbum();
    testCurvedFade();
    testMidSide();
    testSampler();
    testMidiFile();
    testMixer();
    testLauncher();
    testPatchBay();
    testSpatialStudio();
    testPrismSplit();
    testMasterAssist();
    testPhi();

    if (failures == 0)
        std::printf ("All core tests passed\n");

    return failures == 0 ? 0 : 1;
}
