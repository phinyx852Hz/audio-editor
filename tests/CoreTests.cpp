// SPDX-License-Identifier: AGPL-3.0-only
// Tests for the plain C++ core: audio storage, undo history and DSP.
#include "../Source/core/AudioData.h"
#include "../Source/core/AudioDocument.h"
#include "../Source/core/Dsp.h"
#include "../Source/core/MidSide.h"
#include "../Source/sampler/SamplerEngine.h"
#include "../Source/sampler/SlicerEngine.h"
#include "../Source/mixer/MidiFile.h"
#include "../Source/mixer/DawProject.h"
#include "../Source/mixer/AbletonSet.h"
#include "../Source/core/Zip.h"
#include "../Source/mixer/Tunings.h"
#include "../Source/mixer/NoteEditor.h"
#include "../Source/core/Scala.h"
#include "../Source/core/XenTuner.h"
#include "../Source/mixer/Notation.h"
#include "../Source/mixer/NotationGrid.h"
#include "../Source/mixer/RagaInstrument.h"
#include "../Source/mixer/Counterpoint.h"
#include "../Source/mixer/Fugue.h"
#include "../Source/mixer/Cubify.h"
#include "../Source/mixer/Minimalize.h"
#include "../Source/mixer/MixerEngine.h"
#include "../Source/mixer/InstrumentPrism.h"
#include "../Source/mixer/DrumsToMidi.h"
#include "../Source/mixer/Rhythms.h"
#include "../Source/mixer/Architectonics.h"
#include "../Source/mixer/Compose.h"
#include "../Source/mixer/BolProcessor.h"
#include "../Source/sampler/PhiTabla.h"
#include "../Source/mixer/Harmonies.h"
#include "../Source/mixer/ModalHarmony.h"
#include "../Source/mixer/RhythmGenerator.h"
#include "../Source/mixer/Stochastic.h"
#include "../Source/mixer/Serial.h"
#include "../Source/patchbay/PatchBay.h"
#include "../Source/core/EbuCore.h"
#include "../Source/core/Ixml.h"
#include "../Source/core/Metadata.h"
#include "../Source/core/RiffChunks.h"
#include "../Source/core/Xmp.h"
#include "../Source/core/Fft.h"
#include "../Source/core/Windows.h"
#include "../Source/core/Album.h"
#include "../Source/core/Nyquist.h"
#include "../Source/core/Repair.h"
#include "../Source/core/SpectrumView.h"
#include "../Source/core/Generate.h"
#include "../Source/core/Pitch.h"
#include "../Source/core/PitchFix.h"
#include "../Source/core/TimeStretch.h"
#include "../Source/core/Paulstretch.h"
#include "../Source/core/Quantize.h"
#include "../Source/core/Binaural.h"
#include "../Source/core/Ambisonics.h"
#include "../Source/core/SpatialStudio.h"
#include "../Source/core/PhiVoice.h"
#include "../Source/core/PhiPhaser.h"
#include "../Source/core/PhiRepeat.h"
#include "../Source/core/PhiTuning.h"
#include "../Source/core/PhiRaga.h"
#include "../Source/core/PhiSitar.h"
#include "../Source/core/PrismSplit.h"
#include "../Source/core/MasterAssist.h"
#include "../Source/mixer/MixAssist.h"
#include "../Source/core/Sonify.h"
#include "../Source/core/Phi.h"
#include "../Source/core/PhiDynamics.h"
#include "../Source/core/PhiChorus.h"
#include "../Source/core/PhiModulation.h"
#include "../Source/core/Modulators.h"
#include "../Source/core/PluginMeasure.h"
#include "../Source/core/PhiForensics.h"
#include "../Source/control/ControlSurface.h"
#include "../Source/control/MidiLearn.h"
#include "../Source/engine/PushDisplay.h"

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <complex>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <filesystem>
#include <map>
#include <numeric>
#include <random>
#include <set>
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

        // EBU Tech 3341 signals 16 and 19: sines at fs/4 whose peaks fall exactly between two
        // samples (phase 45 degrees) must read their true peak, -6.0 and +3.0 dBTP (+0.2/-0.4).
        for (const auto [amplitude, expected] : { std::pair { 0.5, -6.0 }, std::pair { 1.41, 3.0 } })
        {
            dsp::TruePeakDetector detector;
            float peak = 0.0f;
            for (int i = 0; i < 4800; ++i)
                peak = std::max (peak, detector.process ((float) (amplitude * std::sin (M_PI / 2.0 * i + M_PI / 4.0))));
            const auto db = 20.0 * std::log10 (peak);
            check (db <= expected + 0.2 && db >= expected - 0.1, "true peak between samples " + std::to_string (db));
        }

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

        // The error's level in a band, for comparing the dither kinds.
        auto bandNoise = [&] (const std::vector<float>& y, double from, double to)
        {
            double total = 0.0;
            for (double hz = from; hz < to; hz += (to - from) / 8.0)
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
        {
            // Wannamaker's 9th-order shape: far less noise where hearing is keenest (3-4 kHz), more up high.
            const auto w = samples (*dsp::quantize (*quiet, 16, dsp::Dither::wannamaker), 0, 48000);
            const auto l = samples (*dsp::quantize (*quiet, 16, dsp::Dither::lipshitz), 0, 48000);
            check (bandNoise (w, 3000.0, 4000.0) < bandNoise (p, 3000.0, 4000.0) * 0.03, "Wannamaker shaping: at least 15 dB less noise at 3-4 kHz than TPDF");
            check (bandNoise (l, 3500.0, 4500.0) < bandNoise (p, 3500.0, 4500.0) * 0.1, "Lipshitz shaping: at least 10 dB less noise near 4 kHz than TPDF");
            check (bandNoise (w, 17000.0, 20000.0) > bandNoise (p, 17000.0, 20000.0), "Wannamaker shaping: the noise goes up high");

            // High-passed TPDF: on the grid, within ±1 LSB, less noise in the bass than plain TPDF.
            const auto h = samples (*dsp::quantize (*quiet, 16, dsp::Dither::hpTpdf), 0, 48000);
            bool within = true;
            for (size_t i = 0; i < h.size(); ++i)
                within = within && std::abs (h[i] - x[i]) < 1.5 / 32768.0;
            check (within && bandNoise (h, 100.0, 1000.0) < bandNoise (p, 100.0, 1000.0) * 0.85, "high-passed TPDF: less bass noise, within the dither");

            // Truncation always rounds down; rounding without dither is never more than half a step out.
            const auto t = samples (*dsp::quantize (*quiet, 16, dsp::Dither::truncate), 0, 48000);
            const auto r = samples (*dsp::quantize (*quiet, 16, dsp::Dither::none), 0, 48000);
            bool down = true, half = true;
            for (size_t i = 0; i < t.size(); ++i)
            {
                down = down && t[i] <= x[i] && x[i] - t[i] <= 1.0f / 32768.0f;
                half = half && std::abs (r[i] - x[i]) <= 0.5f / 32768.0f + 1.0e-9f;
            }
            check (down && half, "truncate rounds down, plain rounding stays within half a step");
        }
        {
            // Auto-blanking: after about 11 ms of digital silence the dither stops, for every kind.
            auto gap = makeSignal (48000, [] (Frame f) { return f < 12000 ? (float) (0.001 * std::sin (2.0 * M_PI * 440.0 * f / 48000.0)) : 0.0f; });
            bool silent = true, dithered = false;
            for (const auto kind : dsp::ditherOrder)
            {
                const auto y = samples (*dsp::quantize (*gap, 16, kind), 0, 48000);
                for (size_t i = 12000 + dsp::Quantizer::silenceRun; i < y.size(); ++i)
                    silent = silent && y[i] == 0.0f;
                if (kind == dsp::Dither::wannamaker)
                    for (size_t i = 12000; i < 12000 + dsp::Quantizer::silenceRun; ++i)
                        dithered = dithered || y[i] != 0.0f;
            }
            check (silent && dithered, "auto-blanking: digital silence stays silent once it has lasted a moment");
        }

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

        // A BEXT copy is not written back (the bext chunk is the official one), and the
        // bext loudness is repeated in iXML's own LOUDNESS object.
        Tags b = t;
        b["ixml:BEXT/BWF_ORIGINATOR"] = "Old";
        b[tag::bwfLoudness] = "-23.00";
        const auto x = ixml::write (b);
        Tags back;
        check (ixml::read (x, back, error) && back.count ("ixml:BEXT/BWF_ORIGINATOR") == 0 && back["ixml:LOUDNESS/LOUDNESS_VALUE"] == "-23.00",
               "ixml: no stale BEXT copy, LOUDNESS from bext");
    }

    void testEbuCore()
    {
        // A new document from the common fields.
        Tags t;
        t[tag::title] = "Night Drive";
        t[tag::artist] = "Ana\nBo";
        t[tag::composer] = "Cy";
        t[tag::isrc] = "AA6Q72000047";
        t[tag::date] = "2026-10-31";
        const auto x = ebucore::write (t);
        Tags back;
        std::string error;
        check (ebucore::read (x, back, error) && back[tag::title] == "Night Drive" && back[tag::artist] == "Ana\nBo" && back[tag::composer] == "Cy"
                   && back[tag::isrc] == "AA6Q72000047" && back[tag::date] == "2026-10-31",
               "ebucore: round trip " + error);
        check (x.find ("<created startDate=\"2026-10-31\"/>") != std::string::npos && x.find ("typeLabel=\"ISRC\"") != std::string::npos,
               "ebucore: dates as attributes, ISRC labelled");

        // Merging into a file's document keeps what PHI does not show (an ADM description,
        // another identifier) and what did not change (a composer's split name).
        const std::string original = R"(<?xml version="1.0"?><ebucore:ebuCoreMain xmlns:ebucore="urn:ebu:metadata-schema:ebuCore_2014">)"
            R"(<ebucore:coreMetadata><ebucore:title><dc:title xmlns:dc="http://purl.org/dc/elements/1.1/">Old</dc:title></ebucore:title>)"
            R"(<ebucore:identifier typeLabel="UUID"><dc:identifier xmlns:dc="http://purl.org/dc/elements/1.1/">u-1</dc:identifier></ebucore:identifier>)"
            R"(<ebucore:creator><ebucore:contactDetails><ebucore:givenName>Dee</ebucore:givenName><ebucore:familyName>Eff</ebucore:familyName>)"
            R"(</ebucore:contactDetails><ebucore:role typeLabel="Composer"/></ebucore:creator><ebucore:format><ebucore:audioFormatExtended>)"
            R"(<audioProgramme audioProgrammeID="APR_1001"/></ebucore:audioFormatExtended></ebucore:format></ebucore:coreMetadata></ebucore:ebuCoreMain>)";
        Tags file;
        check (ebucore::read (original, file, error) && file[tag::title] == "Old" && file[tag::composer] == "Dee Eff", "ebucore: read a file's document");
        file[tag::title] = "New";
        const auto merged = ebucore::write (file, original);
        Tags again;
        check (merged.find ("<ebucore:title><dc:title>New</dc:title></ebucore:title>") != std::string::npos
                   && merged.find ("audioProgrammeID=\"APR_1001\"") != std::string::npos && merged.find ("u-1") != std::string::npos
                   && merged.find ("<ebucore:givenName>Dee</ebucore:givenName>") != std::string::npos
                   && ebucore::read (merged, again, error) && again[tag::title] == "New",
               "ebucore: merge keeps the rest " + error);
        check (ebucore::write (Tags {}).empty() && ebucore::write (t, "<BWFXML/>").empty(), "ebucore: nothing to write, not EBUCore");
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
        {
            // Tech 3285 v2 2.4: round(100x) ties away from zero, and anything out of range is unused.
            Tags l;
            l[tag::bwfLoudness] = "-22.645";
            l[tag::bwfTruePeak] = "12.765";
            l[tag::bwfLoudnessRange] = "-3";
            l["bwf.maxMomentaryLoudness"] = "-120";
            const auto e = riff::encodeBext (l);
            const auto* u = (const unsigned char*) e.data();
            const auto w16 = [&] (int at) { return (int) (int16_t) (u[at] | (u[at + 1] << 8)); };
            check (e.size() >= 602 && w16 (346) == 2 && w16 (412) == -2265 && w16 (416) == 1277 && w16 (414) == 0x7fff && w16 (418) == 0x7fff,
                   "riff: bext loudness rounded and range-checked as Tech 3285 v2 says");
        }
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
        e.panic();

        // Phase 1 sound shaping. From a clean start each time.
        auto fresh = [&]
        {
            sampler::Settings t;
            t.attack = t.decay = 0.0;
            t.sustain = 1.0;
            t.release = 0.01;
            t.velocity = 0.0;
            t.loop = false;
            return t;
        };
        auto peakOf = [] (const std::vector<float>& v, size_t from, size_t to)
        {
            double p = 0;
            for (size_t i = from; i < std::min (to, v.size()); ++i)
                p = std::max (p, (double) std::abs (v[i]));
            return p;
        };

        // Reverse plays the sample from its end; Fixed Pitch ignores the key.
        s = fresh();
        s.reverse = true;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        {
            auto [rev, revR] = play (1000);
            double off = 0;
            for (size_t i = 0; i < rev.size(); ++i)
                off = std::max (off, (double) std::abs (rev[i] - x[x.size() - 1 - i]));
            check (off < 1.0e-5, "sampler: Reverse plays the sample backwards");
        }
        e.panic();
        s = fresh();
        s.fixedPitch = true;
        e.setSettings (s);
        e.noteOn (72, 1.0f);
        {
            auto [fixed, fixedR] = play (24000);
            check (near (crossings (fixed, 0, 24000), 100, 2), "sampler: Fixed Pitch plays every key at the root's pitch");
        }
        e.panic();

        // The key range leaves other keys silent.
        s = fresh();
        s.keyLow = 48;
        s.keyHigh = 72;
        e.setSettings (s);
        e.noteOn (80, 1.0f);
        play (512);
        check (! e.isSounding (80), "sampler: keys outside the range do not play");
        e.noteOn (60, 1.0f);
        play (512);
        check (e.isSounding (60), "sampler: keys inside the range play");
        e.panic();

        // Loop modes: Once goes round one time then ends; Until Release stops looping when let go;
        // Alternate turns round at the loop's end.
        s = fresh();
        s.loop = true;
        s.loopStart = 24000;
        s.loopEnd = 48000;
        s.loopMode = sampler::LoopMode::once;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        {
            auto [once, onceR] = play (96000);
            check (peakOf (once, 60000, 70000) > 0.4 && peakOf (once, 73000, 96000) < 1.0e-6, "sampler: Loop Once goes round one time");
        }
        e.panic();
        s.loopMode = sampler::LoopMode::untilRelease;
        s.release = 5.0;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        play (60000);
        e.noteOff (60);
        play (40000);
        check (! e.isSounding (60), "sampler: Until Release plays on to the end once let go");
        e.panic();
        s.loopMode = sampler::LoopMode::continuous;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        play (60000);
        e.noteOff (60);
        play (40000);
        check (e.isSounding (60), "sampler: Continuous keeps looping through the release");
        e.panic();
        s.loopMode = sampler::LoopMode::alternate;
        s.release = 0.01;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        play (50000);
        const auto there = e.playheads()[0];
        play (2000);
        check (there > 0.0 && e.playheads()[0] < there, "sampler: Alternate plays back from the loop's end");
        e.panic();

        // A loop crossfade still lands on the loop's start at the seam.
        s.loopMode = sampler::LoopMode::continuous;
        s.loopCrossfade = 0.05;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        {
            auto [xf, xfR] = play (60000);
            check (peakOf (xf, 46000, 52000) > 0.4, "sampler: a crossfaded loop keeps its level over the seam");
        }
        e.panic();

        // The filter: a low pass at 200 Hz takes 5 kHz away, a high pass takes 100 Hz away.
        {
            AudioData::Builder hb (1, rate);
            std::vector<float> hi (48000);
            for (size_t i = 0; i < hi.size(); ++i)
                hi[i] = (float) (0.5 * std::sin (2.0 * M_PI * 5000.0 * (double) i / rate));
            const float* hp[1] { hi.data() };
            hb.append (hp, (int) hi.size());
            sampler::Engine f;
            f.prepare (rate);
            f.setSample (sampler::makeSample (*hb.finish()));
            auto t = fresh();
            t.filter = true;
            t.cutoff = 200.0;
            t.resonance = 0.0;
            t.steep = true;
            f.setSettings (t);
            f.noteOn (60, 1.0f);
            std::vector<float> l (9600, 0.0f), r (9600, 0.0f);
            f.render (l.data(), r.data(), 9600);
            check (peakOf (l, 4800, 9600) < 0.5 * 0.002, "sampler: a 24 dB low pass at 200 Hz takes away 5 kHz");
        }
        s = fresh();
        s.filter = true;
        s.filterType = sampler::FilterType::highPass;
        s.cutoff = 2000.0;
        s.resonance = 0.0;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        {
            auto [high, highR] = play (9600);
            check (peakOf (high, 4800, 9600) < 0.5 * 0.01, "sampler: a high pass at 2 kHz takes away 100 Hz");
        }
        e.panic();

        // One voice: legato moves the note without starting again; letting go goes back to the held key.
        s = fresh();
        s.voices = 1;
        s.legato = true;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        play (4800);
        e.noteOn (64, 1.0f);
        play (512);
        check (e.isSounding (64) && ! e.isSounding (60) && e.playheads()[0] > 5000.0, "sampler: legato changes pitch without restarting");
        e.noteOff (64);
        play (512);
        check (e.isSounding (60), "sampler: letting go goes back to the key still held");
        e.panic();

        // Glide: an octave jump takes the glide time to arrive.
        s = fresh();
        s.voices = 1;
        s.glide = 0.1;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        play (480);
        e.noteOn (72, 1.0f);
        {
            auto [gl, glR] = play (24000);
            const auto early = crossings (gl, 0, 2400), late = crossings (gl, 12000, 14400);
            check (early < late - 3 && near (late, 20, 2), "sampler: glide slides up to the new note");
        }
        e.panic();

        // 1-Shot Gate fades out when let go; Trigger plays on.
        s = fresh();
        s.mode = sampler::Mode::oneShot;
        s.gate = true;
        s.fadeOut = 0.01;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        play (4800);
        e.noteOff (60);
        play (2000);
        check (! e.isSounding (60), "sampler: 1-Shot Gate stops when let go");
        e.panic();

        // Vintage: 12 bits, so every value is a step of 1/2048.
        s = fresh();
        s.quality = sampler::Quality::vintage;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        {
            auto [vin, vinR] = play (4800);
            bool steps = true;
            for (const auto v : vin)
                steps = steps && std::abs (v * 2048.0f - std::round (v * 2048.0f)) < 1.0e-3f;
            check (steps && peakOf (vin, 0, 4800) > 0.4, "sampler: Vintage plays at 12 bits");
        }
        e.panic();

        // Phase 2 modulation. A pitch envelope starts an octave up and falls back to the note.
        s = fresh();
        s.pitchEnvelope.on = true;
        s.pitchEnvelope.amount = 12.0;
        s.pitchEnvelope.attack = 0.0;
        s.pitchEnvelope.decay = 0.1;
        s.pitchEnvelope.sustain = 0.0;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        {
            auto [pe, peR] = play (24000);
            check (crossings (pe, 0, 960) > crossings (pe, 12000, 12960) + 1 && near (crossings (pe, 12000, 24000), 50, 2),
                   "sampler: a pitch envelope sweeps down to the note");
        }
        e.panic();

        // A filter envelope opens a closed low pass and lets it close again.
        s = fresh();
        s.filter = true;
        s.steep = true;
        s.cutoff = 20.0;
        s.resonance = 0.0;
        s.filterEnvelope.on = true;
        s.filterEnvelope.amount = 7.0;
        s.filterEnvelope.attack = 0.0;
        s.filterEnvelope.decay = 0.1;
        s.filterEnvelope.sustain = 0.0;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        {
            auto [fe, feR] = play (24000);
            check (peakOf (fe, 480, 1440) > 0.3 && peakOf (fe, 14400, 24000) < 0.01, "sampler: a filter envelope opens the filter and closes it");
        }
        e.panic();

        // An LFO synced to 120 BPM, a beat a cycle, as a pulse on the volume: on for a quarter second, off for one.
        s = fresh();
        s.tempo = 120.0;
        s.lfo[0].wave = sampler::Wave::pulse;
        s.lfo[0].shape = 0.5;
        s.lfo[0].sync = true;
        s.lfo[0].beats = 1.0;
        s.lfo[0].retrigger = sampler::Retrigger::eachNote;
        s.lfo[0].toVolume = 1.0;
        e.setSettings (s);
        e.noteOn (60, 1.0f);
        {
            auto [tr, trR] = play (24000);
            check (peakOf (tr, 1000, 11000) > 0.4 && peakOf (tr, 13000, 23000) < 1.0e-6, "sampler: a synced LFO pulses the volume in time");
        }
        e.panic();

        // The mod wheel: with Mod wheel at 100% the LFO waits for it.
        s.lfo[0].modWheel = 1.0;
        e.setSettings (s);
        e.modWheel (0.0);
        e.noteOn (60, 1.0f);
        {
            auto [mw, mwR] = play (24000);
            check (peakOf (mw, 13000, 23000) > 0.4, "sampler: the mod wheel down leaves the LFO out");
        }
        e.panic();
        e.modWheel (1.0);
        e.noteOn (60, 1.0f);
        {
            auto [mw, mwR] = play (24000);
            check (peakOf (mw, 13000, 23000) < 1.0e-6, "sampler: the mod wheel up brings the LFO in");
        }
        e.panic();
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

    // The fast MIDI edits (owner, 2026-10-25): four quarter notes C D E G in one bar of 4/4.
    void testMidiEdits()
    {
        auto song = std::make_shared<midifile::Song>();
        song->ticksPerQuarter = 480;
        song->tempos.push_back ({ 0, 500000.0 });
        const uint8_t pitches[] { 60, 62, 64, 67 };
        for (int i = 0; i < 4; ++i)
        {
            song->events.push_back ({ i * 480, 0x90, pitches[i], (uint8_t) (90 + i) });
            song->events.push_back ({ i * 480 + 240, 0x80, pitches[i], 0 });
        }
        song->lastTick = 1680;
        song->channelsUsed = 1;
        auto notes = [] (const midifile::Song& s)
        {
            std::vector<std::pair<int64_t, int>> out;   // start, pitch
            for (const auto& e : s.events)
                if (e.kind() == 0x90 && e.data2 > 0)
                    out.push_back ({ e.tick, e.data1 });
            return out;
        };
        check (midifile::span (*song) == 1920, "midi edits: a loop is whole bars long");

        // Written and read back the same.
        std::string error;
        const auto data = midifile::bytes (*song);
        const auto again = midifile::parse (data.data(), data.size(), error);
        check (again != nullptr && notes (*again) == notes (*song) && again->lastTick == 1680 && near (again->fileBpm(), 120.0, 1.0e-9),
               "midi edits: a song written as a type 0 file reads back the same");

        // Φ: the same starts and pitches, other pairs of them, and another order each turn.
        const auto a = midifile::phiShuffle (*song, 0), b = midifile::phiShuffle (*song, 1);
        auto starts = [&] (const midifile::Song& s) { std::vector<int64_t> v; for (auto& n : notes (s)) v.push_back (n.first); std::sort (v.begin(), v.end()); return v; };
        auto pitchSet = [&] (const midifile::Song& s) { std::vector<int> v; for (auto& n : notes (s)) v.push_back (n.second); std::sort (v.begin(), v.end()); return v; };
        check (starts (*a) == starts (*song) && pitchSet (*a) == pitchSet (*song) && notes (*a) != notes (*song),
               "midi edits: phi keeps the rhythm and the pitches and moves the notes");
        check (notes (*a) != notes (*b), "midi edits: phi again gives another order");
        check (a->noteCount() == 4 && a->events.size() == 8, "midi edits: phi keeps every note with its note-off");

        const auto twice = midifile::duplicated (*song);
        check (twice->noteCount() == 8 && twice->lastTick == 3840 && notes (*twice)[4] == std::make_pair<int64_t, int> (1920, 60),
               "midi edits: duplicated, the loop plays twice as one");
        const auto half = midifile::halfDensity (*song);
        check (notes (*half) == (std::vector<std::pair<int64_t, int>> { { 0, 60 }, { 960, 64 } }), "midi edits: half the density keeps every other note");
        const auto dbl = midifile::doubleDensity (*song);
        check (dbl->noteCount() == 8 && notes (*dbl)[1] == std::make_pair<int64_t, int> (120, 60), "midi edits: double the density plays each note twice");
        const auto back = midifile::reversed (*song);
        check (notes (*back).front() == std::make_pair<int64_t, int> (1440 - 1440 + 0, 67) || notes (*back).front().second == 67,
               "midi edits: reversed, the last note comes first");

        // D Dorian on C D E G: C goes down to B? No: C is in D Dorian (D E F G A B C), E stays, all stay.
        const auto dorian = midifile::toMode (*song, 2, 1);
        check (pitchSet (*dorian) == (std::vector<int> { 60, 62, 64, 67 }), "midi edits: notes already in the mode stay");
        // C Phrygian (C Db Eb F G Ab Bb): D goes down to Db, E down to Eb.
        const auto phrygian = midifile::toMode (*song, 0, 2);
        check (pitchSet (*phrygian) == (std::vector<int> { 60, 61, 63, 67 }), "midi edits: notes move to the nearest of the mode, down when between");
        check (pitchSet (*midifile::toKeys (*song, 0, 0b010110101011)) == pitchSet (*phrygian), "midi edits: any set of keys, as the Mode menu gives");
        // Ragas: Malkauns (S g m d n) on C, and Darbari and Jaunpuri listed with Asavari's notes.
        const auto& scales = midifile::scales();
        auto find = [&] (const std::string& name) { for (size_t i = 0; i < scales.size(); ++i) if (name == scales[i].name) return (int) i; return -1; };
        const auto malkauns = find ("mālkauns");
        check (malkauns >= 0 && pitchSet (*midifile::toMode (*song, 0, malkauns)) == (std::vector<int> { 60, 63, 63, 68 }), "midi edits: a raga's notes, pentatonic");
        // Hypo: bhūpālī (S R G P D) on C from the fifth folds every note into G3..G4 (55..67), Sa kept.
        const auto bhupali = find ("bhūpālī");
        check (bhupali >= 0 && mixer::hypoFamily (scales[(size_t) bhupali].family) && mixer::hasHypo (scales[(size_t) bhupali].notes, mixer::Hypo::fifth)
               && ! mixer::hasHypo (scales[(size_t) bhupali].notes, mixer::Hypo::fourth) && ! mixer::hypoFamily (scales[0].family),
               "midi edits: hypo for a raga with the degree, not the church modes");
        auto wide = std::make_shared<midifile::Song> (*song);
        wide->events.clear();
        const uint8_t spread[] { 48, 60, 62, 76 };
        for (int i = 0; i < 4; ++i)
        {
            wide->events.push_back ({ i * 480, 0x90, spread[i], 90 });
            wide->events.push_back ({ i * 480 + 240, 0x80, spread[i], 0 });
        }
        check (pitchSet (*midifile::inRange (*midifile::toMode (*wide, 0, bhupali), 0, 5)) == (std::vector<int> { 60, 60, 62, 64 }),
               "midi edits: a hypo range, notes folded into the octave from the fifth below");
        check (find ("darbārī kānaḍā") >= 0 && find ("jaunpurī") >= 0 && scales[(size_t) find ("darbārī kānaḍā")].notes == scales[(size_t) find ("āsāvarī")].notes
               && scales[(size_t) find ("jaunpurī")].notes == scales[(size_t) find ("āsāvarī")].notes, "midi edits: ragas sharing their notes are each listed");
        check (find ("nyamaropa") >= 0 && scales[(size_t) find ("nyamaropa")].notes == scales[(size_t) find ("mixolydian")].notes
               && find ("matepe") >= 0, "midi edits: the Shona mbira tunings on the keys");
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

        // The Pre-Master: a track sent to it plays through its trim, mute and fader into the master.
        {
            mixer::Engine t;
            t.prepare (rate);
            t.setNumTracks (2);
            t.setClips (0, { mixer::makeClip (dcClip, 0) });
            t.setClips (1, { mixer::makeClip (dcClip, 0) });
            std::vector<float> a (480), b (480);
            auto heard = [&]
            {
                t.setPosition (0);
                t.play();
                std::fill (a.begin(), a.end(), 0.0f);
                std::fill (b.begin(), b.end(), 0.0f);
                t.render (a.data(), b.data(), 480);
                return (double) a[200];
            };
            const auto one = 0.5 * std::sqrt (0.5);
            check (near (heard(), 2.0 * one, 1.0e-4), "mixer: both tracks go to the master by default");
            t.strip (1).toBus = true;
            check (near (heard(), 2.0 * one, 1.0e-4) && t.anyToBus(), "mixer: the Pre-Master at 0 dB passes a track as it is");
            t.busStrip().mute = true;
            check (near (heard(), one, 1.0e-4), "mixer: a muted Pre-Master silences the tracks sent to it, not the others");
            t.busStrip().mute = false;
            t.busStrip().trimDb = 20.0f * (float) std::log10 (2.0);
            check (near (heard(), 3.0 * one, 1.0e-3), "mixer: the Pre-Master's trim");
            t.busStrip().trimDb = 0.0f;
            t.busStrip().faderDb = -100.0f;
            check (near (heard(), one, 1.0e-4), "mixer: the Pre-Master's fader");
        }

        // Spatial mode: the tracks placed in a 3rd-order scene, decoded to headphones and speakers.
        {
            namespace sp = mixer::spatial;
            const auto w1 = sp::gains ({ 0.0f, 0.0f, 0.0f, 1.0f })[0][0], w2 = sp::gains ({ 0.0f, 0.0f, 0.0f, 2.0f })[0][0];
            check (near (w1, 1.0, 1.0e-6) && near (20.0 * std::log10 (w2 / w1), -1.5, 1.0e-3), "spatial: beyond the ring a source falls 1.5 dB a unit");
            const auto close = sp::gains ({ 90.0f, 0.0f, 0.0f, 0.0f });
            check (near (close[0][0], 1.0, 1.0e-6) && std::abs (close[0][1]) + std::abs (close[0][3]) < 1.0e-6,
                   "spatial: at the listener a source is all around (omni only)");
            const auto wide = sp::gains ({ 0.0f, 0.0f, 90.0f, 1.0f });
            check (wide[0][1] > 0.5f && wide[1][1] < -0.5f, "spatial: a stereo source's sides are spread to the left and right");

            mixer::Engine t;
            t.prepare (rate);
            t.setNumTracks (2);
            t.setClips (0, { mixer::makeClip (dcClip, 0) });
            std::vector<float> a (480), b (480);
            auto play = [&]
            {
                t.setPosition (0);
                t.play();
                std::fill (a.begin(), a.end(), 0.0f);
                std::fill (b.begin(), b.end(), 0.0f);
                t.render (a.data(), b.data(), 480);
            };
            play();
            const auto stereoL = a[200];
            t.spatialOn = true;
            play();
            check (near (a[200], stereoL, 1.0e-6), "spatial: without a decoder the mix stays stereo");

            // Four speakers around, from output 3; headphones as a stereo decode.
            sp::Decoder::Setup setup;
            const std::vector<ambi::Speaker> square { { "FL", -45.0, 0.0, false }, { "FR", 45.0, 0.0, false },
                                                      { "BL", -135.0, 0.0, false }, { "BR", 135.0, 0.0, false } };
            setup.speakers = ambi::decoder (sp::order, square);
            setup.firstOutput = 2;
            t.setDecoder (std::make_shared<sp::Decoder> (setup));
            auto& placed = t.strip (0).placed;
            placed.set ({ 90.0f, 0.0f, 0.0f, 1.0f });
            t.strip (0).pan = -1.0f;   // the panner, not pan, places it
            play();
            check (b[200] > 3.0f * std::abs (a[200]), "spatial: a track placed to the right is heard on the right of the headphones");
            std::vector<std::vector<float>> outs (6, std::vector<float> (480, 0.0f));
            std::vector<float*> ptrs;
            for (auto& o : outs)
                ptrs.push_back (o.data());
            t.takeSpeakers (ptrs.data(), 6, 480);   // the device takes each block's feeds as it plays
            for (auto& o : outs)
                std::fill (o.begin(), o.end(), 0.0f);
            placed.set ({ 45.0f, 0.0f, 0.0f, 1.0f });
            play();
            t.takeSpeakers (ptrs.data(), 6, 480);
            const auto fr = std::abs (outs[3][200]), others = std::max ({ std::abs (outs[2][200]), std::abs (outs[4][200]), std::abs (outs[5][200]) });
            check (fr > 0.05f && fr > 2.0f * others && std::abs (outs[0][200]) < 1.0e-9f,
                   "spatial: a track at 45° right is loudest on the front-right speaker, from the first speaker's output on");
            t.masterDb = -100.0f;
            play();
            for (auto& o : outs)
                std::fill (o.begin(), o.end(), 0.0f);
            t.takeSpeakers (ptrs.data(), 6, 480);
            check (std::abs (outs[3][400]) < 1.0e-4f, "spatial: the master fader turns the speakers down too");
            t.masterDb = 0.0f;

            // Binaural: the scene through each channel's filters, one partition (64 frames) late.
            auto ears = std::make_shared<ambi::EarFilters>();
            for (int ear = 0; ear < 2; ++ear)
            {
                (*ears)[(size_t) ear].assign (sp::channels, std::vector<float> { 0.0f });
                (*ears)[(size_t) ear][0] = { 1.0f };   // W alone, unchanged
            }
            sp::Decoder::Setup phones;
            phones.ears = ears;
            sp::Decoder binaural (phones);
            check (binaural.latency() == 64 && binaural.speakerCount() == 0, "spatial: the binaural decode is a partition late, with no speakers");
            std::vector<std::vector<float>> scene (sp::channels, std::vector<float> (100, 0.0f));
            std::vector<float*> sceneAt;
            for (auto& c : scene)
                sceneAt.push_back (c.data());
            for (int k = 0; k < 100; ++k)
                scene[0][(size_t) k] = (float) (k + 1);
            std::vector<float> l (100, 0.0f), r (100, 0.0f);
            binaural.process (sceneAt.data(), 100, l.data(), r.data(), nullptr);
            check (l[63] == 0.0f && near (l[64], 1.0, 1.0e-5) && near (r[99], 36.0, 1.0e-4), "spatial: the binaural decode's delay and filters");
            // Turned a quarter to the right, a source ahead is on the right.
            sp::Decoder::Setup turned;
            turned.yaw = 90.0;
            sp::Decoder turner (turned);
            for (auto& c : scene)
                std::fill (c.begin(), c.end(), 0.0f);
            const auto ahead = sp::gains ({ 0.0f, 0.0f, 0.0f, 1.0f });
            for (int c = 0; c < sp::channels; ++c)
                std::fill (scene[(size_t) c].begin(), scene[(size_t) c].end(), ahead[0][(size_t) c]);
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            turner.process (sceneAt.data(), 100, l.data(), r.data(), nullptr);
            check (r[50] > 3.0f * std::abs (l[50]), "spatial: the scene turned to the right");

            // A 5.1 file on a track: folded down outside Spatial mode; in it, its surround goes to the
            // speaker behind on the left, and its LFE to the subwoofer.
            AudioData::Builder sb (6, rate);
            std::vector<std::vector<float>> six (6, std::vector<float> (4800, 0.0f));
            std::fill (six[4].begin(), six[4].end(), 0.5f);   // Ls
            std::fill (six[3].begin(), six[3].end(), 0.5f);   // LFE: DC passes the low-pass
            std::vector<const float*> sp6;
            for (auto& c : six)
                sp6.push_back (c.data());
            sb.append (sp6.data(), 4800);
            const auto surroundClip = mixer::makeAudioClip (*sb.finish(), "five.wav");
            check (surroundClip->surround() && mixer::clipFormat (*surroundClip) == "5.1" && surroundClip->speakers[4].azimuth < -100.0f,
                   "mixer: a 5.1 file keeps its channels, Ls at -110°");
            check (surroundClip->left[100] > 0.3f && std::abs (surroundClip->right[100]) < 1.0e-6f, "mixer: and is heard folded down");
            const auto ten = mixer::makeAudioClip (*AudioData::Builder (10, rate).finish(), "x.wav", "5.1.4");
            check (mixer::clipFormat (*ten) == "5.1.4" && ten->speakers[8].elevation > 40.0f, "mixer: 5.1.4 heights");
            AudioData::Builder pb (16, rate);
            const auto poly = mixer::makeAudioClip (*pb.finish(), "poly.wav");
            check (! poly->surround() && mixer::clipFormat (*poly) == "16 ch", "mixer: 16 unmarked channels are a multitrack file");
            check (mixer::splitChannels (*AudioData::Builder (12, rate).finish(), "p.wav").size() == 12, "mixer: a file split into a clip per channel");

            mixer::Engine u;
            u.prepare (rate);
            u.setNumTracks (1);
            u.setClips (0, { mixer::makeClip (surroundClip, 0) });
            u.spatialOn = true;
            sp::Decoder::Setup fiveOne;
            const auto speakers51 = ambi::layout ("5.1");
            fiveOne.speakers = ambi::decoder (sp::order, speakers51);
            fiveOne.lfe = { 3 };
            fiveOne.headphones = false;
            u.setDecoder (std::make_shared<sp::Decoder> (fiveOne));
            std::vector<std::vector<float>> feed (6, std::vector<float> (480, 0.0f));
            std::vector<float*> feedAt;
            for (auto& o : feed)
                feedAt.push_back (o.data());
            for (int pass = 0; pass < 6; ++pass)
            {
                u.setPosition (0);
                u.play();
                u.render (a.data(), b.data(), 480);
                for (auto& o : feed)
                    std::fill (o.begin(), o.end(), 0.0f);
                u.takeSpeakers (feedAt.data(), 6, 480);
            }
            const auto lsFeed = std::abs (feed[4][300]);
            check (lsFeed > 0.1f && lsFeed > 2.0f * std::max ({ std::abs (feed[0][300]), std::abs (feed[1][300]), std::abs (feed[5][300]) }),
                   "spatial: a 5.1 file's Ls is loudest on the Ls speaker");
            check (feed[3][300] > 0.1f, "spatial: its LFE reaches the subwoofer");
            const auto peaks = u.takeSpeakerPeaks();
            check (peaks.size() == 6 && peaks[4] > 0.1f, "spatial: the master meters each speaker");
            std::string mixError;
            u.spatialRender = mixer::Engine::SpatialRender::speakers;
            const auto feeds = u.mixdown (0.0, mixError);
            check (feeds != nullptr && feeds->numChannels() == 6, "spatial: Mix Down to the speakers, a channel each");
            u.spatialRender = mixer::Engine::SpatialRender::ambix;
            const auto ambix = u.mixdown (0.0, mixError);
            check (ambix != nullptr && ambix->numChannels() == 16, "spatial: Mix Down to the AmbiX scene");
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
               && slicer::padForNote (44, slicer::Layout::push) == 4,
               "slicer: Push notes to banks A-D");
        // The notes left over play banks E to H; page 1 swaps the halves.
        check (slicer::padForNote (100, slicer::Layout::push) == 64 && slicer::padForNote (35, slicer::Layout::push) == 127
               && slicer::padForNote (36, slicer::Layout::push, 1) == 64 && slicer::padForNote (100, slicer::Layout::push, 1) == 0,
               "slicer: Push's other notes and page play banks E-H");
        check (slicer::padForNote (36, slicer::Layout::chromatic) == 36 && slicer::noteForPad (0, slicer::Layout::chromatic) == 0,
               "slicer: chromatic pad n is note n");
        bool round = true;
        std::vector<bool> seen (slicer::numPads, false);
        for (int n = 0; n < 128; ++n)
            for (const auto layout : { slicer::Layout::push, slicer::Layout::chromatic })
                for (const int page : { 0, 1 })
                {
                    const auto pad = slicer::padForNote (n, layout, page);
                    round = round && pad >= 0 && slicer::noteForPad (pad, layout, page) == n;
                    if (layout == slicer::Layout::push && page == 0 && pad >= 0)
                        seen[(size_t) pad] = true;
                }
        check (round && std::all_of (seen.begin(), seen.end(), [] (bool b) { return b; }), "slicer: notes and pads round-trip, all 128 pads");

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
        kit.pads[2].layers[0].reverse = true;
        kit.pads[0].choke = kit.pads[1].choke = 1;
        kit.pads[3].play = slicer::Play::gate;
        e.setKit (kit);
        check (! kit.pads[4].used(), "slicer: pads past the slices are empty");

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
        e.panic();

        // A pad with a sound of its own, a second layer on top, and the notes they answer.
        AudioData::Builder ob (1, rate);
        std::vector<float> own (1000, 0.25f);
        const float* op[1] { own.data() };
        ob.append (op, (int) own.size());
        const auto ownSound = sampler::makeSample (*ob.finish());
        slicer::placeSound (kit, 100, 0, ownSound, "own.wav");
        kit.pads[100].attack = 0.0;
        e.setKit (kit);
        e.hit (100, 1.0f);
        out = play (100);
        check (near (out[10], 0.25, 1.0e-5), "slicer: a pad plays a sound of its own");
        slicer::placeSound (kit, 100, 1, ownSound, "own.wav");
        e.setKit (kit);
        e.panic();   // the first hit would still be fading out under the second
        e.hit (100, 1.0f);
        out = play (100);
        check (near (out[10], 0.5, 1.0e-5) && kit.pads[100].layerCount() == 2, "slicer: a pad's layers sound together");
        e.panic();
        e.noteOn (slicer::noteForPad (100, kit.layout), 1.0f);
        play (10);
        check (e.isSounding (100), "slicer: a pad answers its note");
        e.panic();
        kit.pads[100].receive = 20;
        kit.pads[1].receive = 20;
        e.setKit (kit);
        e.noteOn (20, 1.0f);
        play (10);
        check (e.isSounding (100) && e.isSounding (1), "slicer: pads with the same Receive note play together");
        e.panic();

        kit.pads[100].mute = true;
        e.setKit (kit);
        e.hit (100, 1.0f);
        out = play (100);
        check (out[10] == 0.0f, "slicer: a muted pad is silent");
        kit.pads[100].mute = false;
        kit.pads[1].solo = true;
        e.setKit (kit);
        e.hit (100, 1.0f);
        out = play (100);
        check (out[10] == 0.0f, "slicer: a pad not soloed is silent while another is");
        kit.pads[1].solo = false;
        e.panic();

        // Slicing again leaves a pad's own sound alone; swapping keeps the notes where they were.
        slicer::assign (kit, slicer::equalSlices (8000, 128));
        check (kit.pads[100].layers[0].sound == ownSound && kit.pads[127].isSlice() && kit.pads[101].layers[0].start == 8000 * 100 / 128,
               "slicer: slicing skips pads with their own sound");
        kit.pads[5].receive = 70;
        slicer::swapPads (kit, 5, 100);
        check (kit.pads[5].layers[0].sound == ownSound && kit.pads[100].isSlice() && kit.pads[5].receive == 70,
               "slicer: swapping pads swaps their sounds, not their notes");
    }

    void testStepSequencer()
    {
        // Where a lane's steps fall, and which step each plays.
        slicer::Lane lane;
        lane.steps = 3;
        check (slicer::stepIndex (lane, 0) == 0 && slicer::stepIndex (lane, 3) == 0 && slicer::stepIndex (lane, 5) == 2, "steps: forward wraps at the lane's length");
        lane.direction = slicer::Direction::backward;
        check (slicer::stepIndex (lane, 0) == 2 && slicer::stepIndex (lane, 2) == 0, "steps: backward");
        lane.direction = slicer::Direction::alternate;
        check (slicer::stepIndex (lane, 1) == 1 && slicer::stepIndex (lane, 2) == 2 && slicer::stepIndex (lane, 3) == 1 && slicer::stepIndex (lane, 4) == 0,
               "steps: alternate turns at each end");
        lane.direction = slicer::Direction::random;
        bool same = true, inside = true;
        for (int k = 0; k < 50; ++k)
        {
            same = same && slicer::stepIndex (lane, k) == slicer::stepIndex (lane, k);
            inside = inside && slicer::stepIndex (lane, k) >= 0 && slicer::stepIndex (lane, k) < 3;
        }
        check (same && inside, "steps: random draws the same step each time through");
        slicer::Pattern swung;
        swung.swing = 100;
        slicer::Lane even;
        check (near (slicer::stepTime (even, swung, 1), 0.25 + 0.125, 1.0e-12) && near (slicer::stepTime (even, swung, 2), 0.5, 1.0e-12),
               "steps: swing moves every other step");

        // A pattern played to the frame: 1/16 at 120 BPM is 6000 frames at 48 kHz.
        const double rate = 48000.0;
        AudioData::Builder b (1, rate);
        std::vector<float> x (48000);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.1f + (float) i / 48000.0f;
        const float* p[1] { x.data() };
        b.append (p, (int) x.size());
        slicer::Engine e;
        e.prepare (rate);
        e.setSample (sampler::makeSample (*b.finish()));
        slicer::Kit kit;
        kit.velocity = 0.0;
        slicer::assign (kit, { { 0, 3000 }, { 10000, 13000 } });
        for (auto& pad : kit.pads)
            pad.attack = 0.0;
        e.setKit (kit);
        slicer::Pattern pattern;
        pattern.lanes[0].step[4].on = true;    // beat 1
        pattern.lanes[1].step[2].on = true;    // beat 0.5, twice
        pattern.lanes[1].step[2].repeats = 2;
        e.setPattern (0, pattern);
        e.play();
        std::vector<float> out (30000, 0.0f), r (30000, 0.0f);
        for (int at = 0; at < 30000; at += 256)
        {
            const auto n = std::min (256, 30000 - at);
            e.render (out.data() + at, r.data() + at, n);
        }
        check (out[23990] == 0.0f && near (out[24000 + 10], x[10], 1.0e-5), "steps: a step plays on its frame");
        check (near (out[12000 + 5], x[10005], 1.0e-5) && near (out[15000 + 5], x[10005], 1.0e-5), "steps: repeats hit within the step");
        check (e.playingBeat() > 1.2 && e.isPlaying(), "steps: the transport runs");
        e.stop();
        check (! e.isPlaying(), "steps: stop");

        // Chance 0 never plays; a queued pattern starts on the next bar.
        pattern.lanes[0].step[4].chance = 0;
        e.setPattern (0, pattern);
        slicer::Pattern other;
        other.lanes[0].step[0].on = true;
        e.setPattern (1, other);
        e.panic();
        e.play();
        std::vector<float> l2 (48000 * 3, 0.0f), r2 (l2.size(), 0.0f);
        bool queued = false;
        for (int at = 0; at < (int) l2.size(); at += 512)
        {
            if (at >= 30000 && ! queued)
            {
                e.queuePattern (1);
                queued = true;
            }
            e.render (l2.data() + at, r2.data() + at, std::min (512, (int) l2.size() - at));
        }
        check (l2[24000 + 10] == 0.0f, "steps: chance 0 never plays");
        check (e.playingPattern() == 1 && l2[96000 - 2] == 0.0f && near (l2[96000 + 10], x[10], 1.0e-5), "steps: a queued pattern starts on the bar line");
        e.stop();

        // Following a clock: it plays only while the clock runs, from the clock's beat.
        double clockBeat = 0.0;
        bool clockRuns = false;
        e.setClock ([&] (double& at, double& bpm) { at = clockBeat; bpm = 120.0; return clockRuns; });
        e.queuePattern (0);
        pattern.lanes[0].step[4].chance = 100;
        e.setPattern (0, pattern);
        e.panic();
        std::vector<float> l3 (512, 0.0f), r3 (512, 0.0f);
        clockBeat = 0.99;
        e.render (l3.data(), r3.data(), 512);
        check (! e.isPlaying(), "steps: following, still while the clock is stopped");
        clockRuns = true;
        e.render (l3.data(), r3.data(), 512);
        check (e.isPlaying() && near (l3[240 + 10], x[10], 1.0e-5), "steps: following, plays from the clock's beat");
        e.render (l3.data(), r3.data(), 512);   // the clock not read again yet: plays on
        check (near (e.playingBeat(), 0.99 + 1024.0 / 24000.0, 1.0e-9), "steps: following, a clock that has not moved yet is no jump");
        clockBeat = 3.0;
        e.render (l3.data(), r3.data(), 512);
        check (near (e.playingBeat(), 3.0 + 512.0 / 24000.0, 1.0e-9), "steps: following, the clock moved");
        e.setClock ({});
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
    // Every window is symmetric about the middle and peaks at 1; the ones with a design
    // sidelobe level reach it (measured on a 16x zero-padded spectrum).
    void testWindows()
    {
        const int n = 256, pad = 16, points = n * pad / 2;
        const auto highestSidelobeDb = [&] (const std::vector<double>& w)
        {
            std::vector<double> magnitude ((size_t) points);
            for (int f = 0; f < points; ++f)
            {
                std::complex<double> sum;
                for (int i = 0; i < n; ++i)
                    sum += w[(size_t) i] * std::polar (1.0, -2.0 * M_PI * f * i / (n * pad));
                magnitude[(size_t) f] = std::abs (sum);
            }
            int f = 1;
            while (f < points - 1 && magnitude[(size_t) f + 1] < magnitude[(size_t) f])
                ++f;   // the main lobe's first null
            return 20.0 * std::log10 (*std::max_element (magnitude.begin() + f, magnitude.end()) / magnitude[0]);
        };

        for (int kind = 0; kind < windows::numKinds; ++kind)
        {
            const auto w = windows::make (kind, n);
            const auto label = "window " + std::to_string (kind);
            check ((int) w.size() == n, label + " size");
            double asymmetry = 0.0;
            for (int i = 1; i < n; ++i)
                asymmetry = std::max (asymmetry, std::abs (w[(size_t) i] - w[(size_t) (n - i)]));
            check (asymmetry < 1.0e-12, label + " is symmetric");
            check (near (w[(size_t) n / 2], 1.0, 1.0e-8) && near (*std::max_element (w.begin(), w.end()), 1.0, 1.0e-8), label + " peaks at 1 in the middle");
        }

        const std::vector<std::pair<int, double>> designed {
            { windows::hann, -31.0 }, { windows::hamming, -42.0 }, { windows::blackman, -58.0 }, { windows::blackmanHarris, -92.0 },
            { windows::nuttall, -93.0 }, { windows::blackmanNuttall, -97.5 }, { windows::kaiser140, -140.0 }, { windows::kaiser180, -180.0 } };
        for (const auto& [kind, limit] : designed)
            check (highestSidelobeDb (windows::make (kind, n)) <= limit, "window " + std::to_string (kind) + " sidelobes");
        check (near (highestSidelobeDb (windows::make (windows::chebyshev, n)), -windows::chebyshevSidelobeDb, 0.5), "Dolph-Chebyshev sidelobes all at its level");

        // The flat tops, against Heinzel, Ruediger and Schilling (2002): the Stanford Research one
        // (FTSRS), HFT95 and HFT90D. Highest sidelobe, and the most a tone's level can be off
        // between bins (here at every tenth of a bin). Heinzel's sidelobe figures are for long
        // windows; at 256 points HFT95's comes out 0.5 dB higher.
        struct FlatTop { int kind; double sidelobeDb, maxErrorDb; };
        for (const auto& [kind, expectedSidelobe, maxError] : { FlatTop { windows::flatTop, -76.6, 0.0156 },
                                                                 FlatTop { windows::hft95, -95.0, 0.0044 },
                                                                 FlatTop { windows::hft90d, -90.2, 0.0039 } })
        {
            const auto w = windows::make (kind, n);
            const auto label = "flat top " + std::to_string (kind);
            // The main lobe is flat on top, so look for the first null from 4 bins out.
            std::vector<double> magnitude ((size_t) points);
            for (int f = 0; f < points; ++f)
            {
                std::complex<double> sum;
                for (int i = 0; i < n; ++i)
                    sum += w[(size_t) i] * std::polar (1.0, -2.0 * M_PI * f * i / (n * pad));
                magnitude[(size_t) f] = std::abs (sum);
            }
            int f = 4 * pad;
            while (f < points - 1 && magnitude[(size_t) f + 1] < magnitude[(size_t) f])
                ++f;
            const auto sidelobe = 20.0 * std::log10 (*std::max_element (magnitude.begin() + f, magnitude.end()) / magnitude[0]);
            check (near (sidelobe, expectedSidelobe, 0.6), label + " sidelobes " + std::to_string (sidelobe) + " dB");
            const double onBin = std::accumulate (w.begin(), w.end(), 0.0);
            double worst = 0.0;
            for (int tenth = 1; tenth <= 5; ++tenth)
            {
                std::complex<double> off;
                for (int i = 0; i < n; ++i)
                    off += w[(size_t) i] * std::polar (1.0, 2.0 * M_PI * 0.1 * tenth * i / n);
                worst = std::max (worst, std::abs (20.0 * std::log10 (std::abs (off) / onBin)));
            }
            check (worst <= maxError + 0.0005, label + " level error " + std::to_string (worst) + " dB");
        }

        // The largest FFT size stays finite (the Chebyshev's polynomial and Kaiser's Bessel function).
        for (int kind : { (int) windows::chebyshev, (int) windows::kaiser180 })
        {
            const auto w = windows::make (kind, 32768);
            check (std::all_of (w.begin(), w.end(), [] (double v) { return std::isfinite (v) && v > -1.0e-9; }), "window " + std::to_string (kind) + " at 32768");
        }

        // Slepian tapers (NW 4): orthonormal, symmetric even and odd, and as concentrated in
        // +-4 bins as Percival and Walden tabulate (the 7th about 0.937).
        {
            const int length = 512;
            const double bandwidth = 4.0 / length;
            const auto tapers = windows::dpss (length, 4.0, 7);
            check (tapers.size() == 7, "dpss count");
            double worst = 0.0;
            for (size_t a = 0; a < tapers.size(); ++a)
                for (size_t b = 0; b < tapers.size(); ++b)
                {
                    double dot = 0.0;
                    for (int i = 0; i < length; ++i)
                        dot += tapers[a][(size_t) i] * tapers[b][(size_t) i];
                    worst = std::max (worst, std::abs (dot - (a == b ? 1.0 : 0.0)));
                }
            check (worst < 1.0e-9, "dpss orthonormal");
            const double expected[] { 0.99999999997, 0.9999999724, 0.9999987915, 0.9999675878, 0.9994104943, 0.9925077219, 0.9366649347 };
            for (size_t k = 0; k < tapers.size(); ++k)
            {
                const auto& v = tapers[k];
                double concentration = 0.0, asymmetry = 0.0;
                for (int i = 0; i < length; ++i)
                {
                    asymmetry = std::max (asymmetry, std::abs (v[(size_t) i] - (k % 2 == 0 ? 1.0 : -1.0) * v[(size_t) (length - 1 - i)]));
                    for (int j = 0; j < length; ++j)
                        concentration += v[(size_t) i] * v[(size_t) j]
                                         * (i == j ? 2.0 * bandwidth : std::sin (2.0 * M_PI * bandwidth * (i - j)) / (M_PI * (i - j)));
                }
                check (asymmetry < 1.0e-9, "dpss " + std::to_string (k) + " symmetry");
                check (near (concentration, expected[k], 1.0e-6), "dpss " + std::to_string (k) + " concentration");
            }
            check (std::accumulate (tapers[0].begin(), tapers[0].end(), 0.0) > 0.0 && tapers[1][100] > 0.0, "dpss signs");
            const auto large = windows::dpss (32768, 4.0, 7);
            double largeDot = 0.0;
            for (size_t i = 0; i < large[0].size(); ++i)
                largeDot += large[0][i] * large[6][i];
            check (std::abs (largeDot) < 1.0e-9 && std::all_of (large[6].begin(), large[6].end(), [] (double v) { return std::isfinite (v); }), "dpss at 32768");
        }
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

        // 5.1.4 and 5.1.2 share their channel counts with 7.1.2 and 7.1; the WAV channel mask tells them apart.
        check (dsp::channelName (8, 10, "5.1.4") == "Ltb" && dsp::channelName (4, 10, "5.1.4") == "Ls" && dsp::channelName (6, 8, "5.1.2") == "Ltf",
               "10 channels can be 5.1.4, 8 can be 5.1.2");
        check (dsp::wavChannelMask (6, {}) == 0x60F && dsp::wavChannelMask (12, {}) == 0x2D63F, "5.1 and 7.1.4 channel masks");
        check (dsp::wavChannelMask (10, "5.1.4") == 0x2D60F && dsp::wavChannelMask (16, "9.1.6") == 0
                   && dsp::wavChannelMask (16, "Ambisonics (3rd order)") == 0, "5.1.4 has a mask; 9.1.6 and Ambisonics have none");
        check (dsp::layoutForWavMask (0x2D60F, 10) == "5.1.4" && dsp::layoutForWavMask (0x2D03F, 10) == "5.1.4",
               "a 5.1.4 mask (side or back surrounds) opens as 5.1.4");
        check (dsp::layoutForWavMask (0x563F, 10).empty() && dsp::layoutForWavMask (0x560F, 8) == "5.1.2" && dsp::layoutForWavMask (0, 10).empty(),
               "7.1.2 is the usual 10 channels; no mask says nothing");
        check (ambi::layout ("5.1.4").size() == 10 && ambi::layout ("5.1.4")[3].lfe, "5.1.4 speakers for decoding");
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

        // Texture, Smooth and Ambient: the lengths asked for, and a tone keeps about its level.
        for (const auto mode : { timestretch::Mode::texture, timestretch::Mode::smooth, timestretch::Mode::ambient })
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

        // Paulstretch: a chord of 220 Hz harmonics, 4 times as long, keeps its partials, level and
        // channels; the spectral chain moves, keeps or removes partials as asked.
        {
            std::vector<float> chord (48000);
            for (size_t i = 0; i < chord.size(); ++i)
                for (int h = 1; h <= 8; ++h)
                    chord[i] += (float) (0.1 * std::sin (2.0 * M_PI * 220.0 * h * (double) i / rate));
            std::vector<float> fifth (48000);
            for (size_t i = 0; i < fifth.size(); ++i)
                fifth[i] = (float) (0.3 * std::sin (2.0 * M_PI * 330.0 * (double) i / rate));
            const auto source = build ({ chord, fifth });
            // The level within 10 Hz of `hz` in the middle half of x (a sine of amplitude a reads
            // about a): the stretched partials are smeared over a few Hz, so their power is summed.
            auto at = [&] (const std::vector<float>& x, double hz)
            {
                const auto from = x.size() / 4, to = 3 * x.size() / 4;
                const auto step = rate / (double) (to - from);
                double power = 0.0;
                for (auto f = hz - 10.0; f <= hz + 10.0; f += step)
                {
                    const auto c = 2.0 * std::cos (2.0 * M_PI * f / rate);
                    double s1 = 0.0, s2 = 0.0;
                    for (size_t i = from; i < to; ++i)
                    {
                        const auto s0 = x[i] + c * s1 - s2;
                        s2 = s1;
                        s1 = s0;
                    }
                    const auto a = std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) (to - from) * 2.0;
                    power += a * a;
                }
                return std::sqrt (power);
            };
            auto rmsOf = [] (const std::vector<float>& x)
            {
                double sum = 0.0;
                for (size_t i = x.size() / 4; i < 3 * x.size() / 4; ++i)
                    sum += (double) x[i] * x[i];
                return std::sqrt (sum / (double) (x.size() / 2));
            };
            auto run = [&] (const paulstretch::Settings& settings, double ratio = 4.0)
            {
                return paulstretch::stretch (*source, 0, source->length(), ratio, settings, error);
            };

            const auto plain = run ({});
            check (plain != nullptr && plain->length() == 192000, "Paulstretch gives exactly 4 times the length");
            if (plain != nullptr)
            {
                const auto l = channel (*plain, 0), r = channel (*plain, 1);
                check (std::abs (rmsOf (l) - 0.2) < 0.03, "Paulstretch keeps the level: " + std::to_string (rmsOf (l)));
                check (at (l, 440.0) > 0.05 && at (l, 330.0) < 0.01 && at (r, 330.0) > 0.15 && at (r, 440.0) < 0.01,
                       "Paulstretch keeps each channel's partials and nothing else");
            }
            paulstretch::Settings up;
            up.pitchSemitones = 12.0;
            if (const auto o = run (up))
                check (at (channel (*o, 1), 660.0) > 0.15 && at (channel (*o, 1), 330.0) < 0.01, "pitch +12 moves 330 Hz to 660 Hz");
            paulstretch::Settings shifted;
            shifted.frequencyShiftHz = 100.0;
            if (const auto o = run (shifted))
                check (at (channel (*o, 1), 430.0) > 0.15 && at (channel (*o, 1), 330.0) < 0.01, "a 100 Hz shift moves 330 Hz to 430 Hz");
            paulstretch::Settings harmonics;
            harmonics.harmonics = 2;
            harmonics.harmonicsHz = 220.0;
            if (const auto o = run (harmonics))
            {
                const auto l = channel (*o, 0);
                check (at (l, 440.0) > 0.05 && at (l, 660.0) < 0.01 && at (l, 1320.0) < 0.01, "two harmonics keep 220 and 440 Hz only");
            }
            paulstretch::Settings band;
            band.lowHz = 500.0;
            band.highHz = 1000.0;
            if (const auto o = run (band))
            {
                const auto l = channel (*o, 0);
                check (at (l, 660.0) > 0.05 && at (l, 440.0) < 0.01 && at (l, 1320.0) < 0.01, "the band filter keeps 500 Hz to 1 kHz");
            }
            band.bandStop = true;
            if (const auto o = run (band))
            {
                const auto l = channel (*o, 0);
                check (at (l, 660.0) < 0.01 && at (l, 440.0) > 0.05, "the band stop removes 500 Hz to 1 kHz");
            }
            paulstretch::Settings mix;
            mix.ratioLevels = { 0, 1, 0, 0, 0, 0, 0, 0 };
            if (const auto o = run (mix))
                check (at (channel (*o, 1), 165.0) > 0.15 && at (channel (*o, 1), 330.0) < 0.01, "the ratio mix at ½ only moves 330 Hz to 165 Hz");
            paulstretch::Settings noise;
            noise.tonalNoise = -1.0;
            if (const auto o = run (noise))
                check (rmsOf (channel (*o, 0)) < 0.05, "noise only removes most of a chord");
            const auto again = run ({});
            if (plain != nullptr && again != nullptr)
                check (channel (*plain, 0) == channel (*again, 0), "the same seed renders the same result");

            // Keep Hits: clicks every half second, 8 times as long, sound for less of the time.
            std::vector<float> clicks (96000);
            for (int k = 0; k < 4; ++k)
                for (int i = 0; i < 2000; ++i)
                    clicks[(size_t) (k * 24000 + i)] = (float) (0.8 * std::exp (-i / 300.0) * std::sin (i * 0.3));
            const auto hits = build ({ clicks });
            auto active = [&] (double onsets)
            {
                paulstretch::Settings settings;
                settings.onsets = onsets;
                const auto o = paulstretch::stretch (*hits, 0, hits->length(), 8.0, settings, error);
                int n = 0;
                if (o != nullptr)
                {
                    const auto x = channel (*o, 0);
                    for (size_t i = 0; i + 2400 <= x.size(); i += 2400)
                    {
                        double sum = 0.0;
                        for (size_t j = i; j < i + 2400; ++j)
                            sum += (double) x[j] * x[j];
                        n += std::sqrt (sum / 2400.0) > 0.01;
                    }
                }
                return n;
            };
            const auto smeared = active (0.0), kept = active (1.0);
            check (kept < smeared * 0.8, "Keep Hits makes stretched clicks shorter: " + std::to_string (kept) + " of " + std::to_string (smeared));
            check (paulstretch::stretch (*hits, 0, hits->length(), 100000.0, {}, error) == nullptr, "Paulstretch refuses results over an hour");
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

    // The strip beyond gains: inserts, the cuts, width and track delay.
    void testMixerStrip()
    {
        const double rate = 48000.0;
        mixer::Engine m;
        m.prepare (rate);
        m.setNumTracks (1);
        auto clipOf = [&] (float left, float right, int channels)
        {
            AudioData::Builder b (channels, rate);
            std::vector<float> a (48000, left), c (48000, right);
            const float* ptrs[2] = { a.data(), c.data() };
            b.append (ptrs, 48000);
            return mixer::makeAudioClip (*b.finish(), "clip.wav");
        };
        std::vector<float> l (512), r (512);
        auto settle = [&] (int blocks = 3)
        {
            for (int i = 0; i < blocks; ++i)
            {
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                m.render (l.data(), r.data(), 512);
            }
        };

        m.setClips (0, { mixer::makeClip (clipOf (0.5f, 0.5f, 2), 0) });
        check (! m.monoTrack (0), "mixer strip: a track of stereo files is not mono");
        m.play();
        settle();
        check (near (l[300], 0.5, 1.0e-4) && near (r[300], 0.5, 1.0e-4), "mixer strip: flat, the stereo clip as it is");

        auto twice = std::make_shared<DoubleEffect>();
        m.setInserts (0, { { twice, false } });
        settle();
        check (m.inserts (0).size() == 1 && near (l[300], 1.0, 1.0e-4) && near (r[300], 1.0, 1.0e-4), "mixer strip: an insert processes the track");
        m.setInserts (0, { { twice, true } });
        settle();
        check (near (l[300], 0.5, 1.0e-4), "mixer strip: a bypassed insert is not heard");
        m.setInserts (0, { { twice, false } });
        m.strip (0).faderDb = -6.0206f;
        m.strip (0).pan = -1.0f;
        settle();
        check (near (l[300], 0.5, 1.0e-3) && near (r[300], 0.0, 1.0e-4), "mixer strip: pan and fader come after the inserts");
        m.strip (0).faderDb = 0.0f;
        m.strip (0).pan = 0.0f;

        std::string error;
        m.setPosition (0);
        const auto bounce = m.mixdown (0.0, error, nullptr, nullptr, 0);
        float a[1], c[1];
        float* pa[2] = { a, c };
        if (bounce != nullptr)
            bounce->read (1000, 1, pa);
        check (bounce != nullptr && near (a[0], 1.0, 1.0e-4), "mixer strip: a bounce has the inserts");
        m.setInserts (0, {});

        m.strip (0).lowCut = true;
        m.strip (0).lowCutHz = 100.0f;
        m.strip (0).lowCutSlope = 24;
        m.setPosition (0);
        m.play();
        settle (40);
        check (std::abs (l[300]) < 1.0e-3 && std::abs (r[300]) < 1.0e-3, "mixer strip: the low cut takes out DC");
        m.strip (0).lowCut = false;
        m.strip (0).highCut = true;
        m.strip (0).highCutHz = 1000.0f;
        settle (20);
        check (near (l[300], 0.5, 1.0e-3), "mixer strip: the high cut lets DC through");
        m.strip (0).highCut = false;

        m.setClips (0, { mixer::makeClip (clipOf (0.5f, 0.1f, 2), 0) });
        m.strip (0).width = 0.0f;
        m.setPosition (0);
        m.play();
        settle();
        check (near (l[300], 0.3, 1.0e-4) && near (r[300], 0.3, 1.0e-4), "mixer strip: width 0 is mono");
        m.strip (0).width = 2.0f;
        settle();
        check (near (l[300], 0.7, 1.0e-4) && near (r[300], -0.1, 1.0e-4), "mixer strip: width 200 % doubles the sides");
        m.strip (0).width = 1.0f;
        settle();
        check (near (l[300], 0.5, 1.0e-4) && near (r[300], 0.1, 1.0e-4), "mixer strip: back to 100 %");

        m.strip (0).delayMs = 10.0f;   // 480 frames
        m.setPosition (0);
        m.play();
        settle (1);
        check (std::abs (l[100]) < 1.0e-9 && l[511] > 0.1f, "mixer strip: track delay starts the clip later");
        check (m.length() == 48480, "mixer strip: the delay makes the mix longer");
        m.strip (0).delayMs = -10.0f;
        m.setPosition (0);
        settle (1);
        check (near (l[0], 0.5, 1.0e-4), "mixer strip: a negative delay plays the clip early");
        m.strip (0).delayMs = 0.0f;

        // A track of mono files keeps its equal-power pan through the inserts.
        m.setClips (0, { mixer::makeClip (clipOf (0.5f, 0.5f, 1), 0) });
        check (m.monoTrack (0), "mixer strip: a track of mono files is mono");
        m.setInserts (0, { { twice, false } });
        m.setPosition (0);
        m.play();
        settle();
        check (near (l[300], std::sqrt (0.5), 1.0e-4) && near (r[300], std::sqrt (0.5), 1.0e-4), "mixer strip: mono through an insert, equal power");
        m.setInserts (0, {});

        // Sends: post-fader by default, pre-fader when asked, into the returns and their effects.
        m.setClips (0, { mixer::makeClip (clipOf (0.5f, 0.5f, 2), 0) });
        m.setPosition (0);
        m.play();
        m.strip (0).sendDb[0] = 0.0f;
        settle();
        check (near (l[300], 1.0, 1.0e-4), "mixer strip: send A at 0 dB adds the track again through return A");
        m.setReturnEffect (0, twice);
        settle();
        check (near (l[300], 1.5, 1.0e-4), "mixer strip: return A's effect");
        auto ret = m.takeMeter (mixer::returnIndex);
        check (ret.peak[0] > 0.9f, "mixer strip: return A has a meter");
        m.strip (0).faderDb = -6.0206f;
        settle();
        check (near (l[300], 0.25 + 0.5, 1.0e-3), "mixer strip: a post-fader send follows the fader");
        m.strip (0).sendPre[0] = true;
        settle();
        check (near (l[300], 0.25 + 1.0, 1.0e-3), "mixer strip: a pre-fader send does not");
        m.returnStrip (0).mute = true;
        settle();
        check (near (l[300], 0.25, 1.0e-3), "mixer strip: a muted return");
        m.returnStrip (0).mute = false;
        m.strip (0).mute = true;
        settle();
        check (std::abs (l[300]) < 1.0e-6, "mixer strip: muting the track mutes its sends");
        m.strip (0).mute = false;
        const auto dry = m.mixdown (0.0, error, nullptr, nullptr, 0);
        if (dry != nullptr)
            dry->read (1000, 1, pa);
        check (dry != nullptr && near (a[0], 0.5, 1.0e-4), "mixer strip: a bounce has no sends");
        m.strip (0).sendDb[0] = -100.0f;
        m.strip (0).sendPre[0] = false;
        m.setReturnEffect (0, nullptr);
        m.strip (0).faderDb = 0.0f;

        // Track modes: a steady 0.25 from the instrument with the 0.5 audio clip.
        struct Steady final : mixer::Instrument
        {
            Kind kind() const override { return Kind::synth; }
            std::string name() const override { return "Steady"; }
            void prepare (double, int) override {}
            void process (const mixer::NoteEvent*, int, float* left, float* right, int frames) override
            {
                for (int i = 0; i < frames; ++i)
                {
                    left[i] += 0.25f;
                    right[i] += 0.25f;
                }
            }
            void releaseAll() override {}
            void reset() override {}
        };
        m.setInstrument (0, std::make_shared<Steady>());
        m.setPosition (0);
        m.play();
        settle();
        check (near (l[300], 0.75, 1.0e-4), "mixer modes: hybrid plays the audio and the instrument");
        m.strip (0).mode = (int) mixer::TrackMode::audio;
        settle();
        check (near (l[300], 0.5, 1.0e-4), "mixer modes: audio only");
        m.strip (0).mode = (int) mixer::TrackMode::instrument;
        settle();
        check (near (l[300], 0.25, 1.0e-4), "mixer modes: instrument only");
        m.strip (0).mode = (int) mixer::TrackMode::hybrid;
        m.strip (0).audioLayerDb = -6.0206f;
        settle();
        check (near (l[300], 0.5, 1.0e-3), "mixer modes: the audio layer's level");
        m.strip (0).audioLayerDb = 0.0f;
        m.strip (0).duckDb = 12.0f;   // the instrument at 0.25 (-12 dBFS) is half way: 6 dB down
        settle (20);
        check (near (l[300], 0.25 + 0.5 * 0.5012, 2.0e-3), "mixer modes: the instrument ducks the audio");
        m.strip (0).audioDucksInstrument = true;   // the audio at 0.5 (-6 dBFS): the whole 12 dB
        settle (20);
        check (near (l[300], 0.5 + 0.25 * 0.2512, 2.0e-3), "mixer modes: the audio ducks the instrument");
        m.strip (0).audioDucksInstrument = false;
        m.strip (0).duckBelowHz = 100.0f;   // DC is all below it: still ducked
        m.setPosition (0);   // the one-second clip from its start again
        settle (80);   // the key was the louder audio: its release first
        check (near (l[300], 0.25 + 0.5 * 0.5012, 3.0e-3), "mixer modes: ducking below a frequency ducks the lows");
        m.strip (0).duckDb = 0.0f;
        m.strip (0).duckBelowHz = 0.0f;
        m.setInstrument (0, nullptr);

        // Another track as the key: track 2's 0.5 ducks track 1 by the whole 12 dB.
        m.setNumTracks (2);
        m.setClips (1, { mixer::makeClip (clipOf (0.5f, 0.5f, 2), 0) });
        m.strip (0).duckKey = 1;
        m.strip (0).duckDb = 12.0f;
        m.strip (0).mode = (int) mixer::TrackMode::audio;
        m.setPosition (0);
        m.play();
        settle (20);
        check (near (l[300], 0.5 + 0.5 * 0.2512, 3.0e-3), "mixer modes: another track ducks this one");
        m.strip (0).duckDb = 0.0f;
        m.strip (0).duckKey = -1;
        m.strip (0).mode = (int) mixer::TrackMode::hybrid;
        m.setClips (1, {});

        // An insert's Mix: half of the doubled sound and half of the dry.
        m.setInserts (0, { { twice, false, 0.5f } });
        settle();
        check (near (l[300], 0.75, 1.0e-4), "mixer strip: an insert's mix keeps some of the dry sound");
        m.setInserts (0, {});
    }

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

        // A track sent to the Pre-Master leaves its patch: the Pre-Master goes into the Mix Bus.
        m.strip (0).toBus = true;
        render (0.25f);
        render (0.25f);
        check (std::abs (l[300]) < 1.0e-6, "patchbay: a track sent to the Pre-Master skips its own patch");
        bay.connect (node::mixBus, node::mainOut);
        render (0.25f);
        render (0.25f);
        check (near (l[300], 0.5, 1.0e-4), "patchbay: the Pre-Master is heard through the Mix Bus");
        m.busStrip().mute = true;
        render (0.25f);
        render (0.25f);
        check (std::abs (l[300]) < 1.0e-6, "patchbay: and the Pre-Master's mute");
        m.busStrip().mute = false;
        m.strip (0).toBus = false;
        bay.disconnect (node::mixBus, node::mainOut);

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

    // An Instrument Prism: notes reach the rays whose key and velocity ranges hold them, each ray
    // runs its instrument and then its effects in series, and the rays are added together.
    namespace prismtest
    {
        // Holds a level of 0.1 for every note it is playing; remembers what it was sent.
        struct Tone final : mixer::Instrument
        {
            Kind kind() const override { return Kind::synth; }
            std::string name() const override { return "Tone"; }
            void prepare (double, int) override {}
            void process (const mixer::NoteEvent* events, int numEvents, float* left, float* right, int frames) override
            {
                int done = 0;
                for (int i = 0; i <= numEvents; ++i)
                {
                    const auto at = i < numEvents ? std::clamp (events[i].offset, done, frames) : frames;
                    for (int k = done; k < at; ++k)
                    {
                        left[k] += 0.1f * (float) playing;
                        right[k] += 0.1f * (float) playing;
                    }
                    done = at;
                    if (i == numEvents)
                        break;
                    const auto& e = events[i];
                    received.push_back (e);
                    if ((e.status & 0xF0) == 0x90 && e.data2 > 0)
                        ++playing;
                    else if (((e.status & 0xF0) == 0x80 || (e.status & 0xF0) == 0x90) && playing > 0)
                        --playing;
                }
            }
            void releaseAll() override { playing = 0; }
            void reset() override { playing = 0; }
            int playing = 0;
            std::vector<mixer::NoteEvent> received;
        };

        // Doubles the sound, after a delay of `samples` it reports as its latency.
        struct Double final : mixer::Effect
        {
            explicit Double (int n = 0) : samples (n) {}
            std::string name() const override { return "Double"; }
            void prepare (double, int) override { line.assign ((size_t) samples + 1, { 0.0f, 0.0f }); }
            void process (float* left, float* right, int frames) override
            {
                for (int k = 0; k < frames; ++k)
                {
                    line[(size_t) at] = { left[k], right[k] };
                    at = (at + 1) % (int) line.size();
                    left[k] = 2.0f * line[(size_t) at].first;
                    right[k] = 2.0f * line[(size_t) at].second;
                }
            }
            int latency() const override { return samples; }
            int samples, at = 0;
            std::vector<std::pair<float, float>> line;
        };
    }

    void testInstrumentPrism()
    {
        using prismtest::Tone;
        constexpr int frames = 64;
        std::vector<float> left (frames), right (frames);
        auto play = [&] (mixer::InstrumentPrism& p, std::vector<mixer::NoteEvent> events)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);
            p.process (events.data(), (int) events.size(), left.data(), right.data(), frames);
        };
        auto on = [] (int note, int velocity) { return mixer::NoteEvent { 0, 0x90, (uint8_t) note, (uint8_t) velocity }; };
        auto off = [] (int note) { return mixer::NoteEvent { 0, 0x80, (uint8_t) note, 0 }; };

        // A split: the low ray below C4, the high ray from C4 up.
        {
            mixer::InstrumentPrism p;
            p.prepare (48000.0, frames);
            auto low = std::make_shared<Tone>(), high = std::make_shared<Tone>();
            mixer::InstrumentPrism::Ray a, b;
            a.instrument = low;
            a.highKey = 59;
            b.instrument = high;
            b.lowKey = 60;
            p.setRays ({ a, b });
            check (p.numRays() == 2 && p.kind() == mixer::Instrument::Kind::prism, "instrument prism holds its rays");
            play (p, { on (40, 100) });
            check (low->playing == 1 && high->playing == 0, "instrument prism: a low note plays the low ray only");
            play (p, { on (72, 100) });
            check (high->playing == 1 && low->playing == 1, "instrument prism: a high note plays the high ray only");
            check (near (left[frames - 1], 0.2, 1.0e-6), "instrument prism adds its rays");

            // The ranges change while the notes are held: their ends still reach the rays that play them.
            auto moved = p.rays();
            moved[0].highKey = 30;
            moved[1].lowKey = 100;
            p.setRays (moved);
            play (p, { off (40), off (72) });
            check (low->playing == 0 && high->playing == 0, "instrument prism: a note ends on the ray that got it");

            // Controllers go to every ray.
            const auto before = low->received.size() + high->received.size();
            play (p, { { 0, 0xB0, 64, 127 } });
            check (low->received.size() + high->received.size() == before + 2, "instrument prism: a controller reaches every ray");
        }

        // Layers by velocity, and the effects of a ray in series after its instrument.
        {
            mixer::InstrumentPrism p;
            p.prepare (48000.0, frames);
            auto soft = std::make_shared<Tone>(), hard = std::make_shared<Tone>();
            mixer::InstrumentPrism::Ray a, b;
            a.instrument = soft;
            a.highVelocity = 80;
            b.instrument = hard;
            b.lowVelocity = 81;
            b.effects.push_back ({ std::make_shared<prismtest::Double>(), false, 1.0f });
            p.setRays ({ a, b });
            play (p, { on (60, 120) });
            check (soft->playing == 0 && hard->playing == 1, "instrument prism: a hard note plays the hard layer");
            check (near (left[frames - 1], 0.2, 1.0e-6), "instrument prism: a ray's effect follows its instrument");
            play (p, { on (62, 40) });
            check (near (left[frames - 1], 0.3, 1.0e-6), "instrument prism: the soft layer adds to the hard one");

            // Bypassed, the effect is left out; at half Mix, half of it is heard.
            auto r = p.rays();
            r[1].effects[0].bypassed = true;
            p.setRays (r);
            play (p, {});
            check (near (left[frames - 1], 0.2, 1.0e-6), "instrument prism: a bypassed ray effect is left out");
            r[1].effects[0].bypassed = false;
            r[1].effects[0].mix = 0.5f;
            p.setRays (r);
            play (p, {});
            check (near (left[frames - 1], 0.25, 1.0e-6), "instrument prism: a ray effect's Mix");

            // Mute and solo, ramped over a block.
            r[1].effects[0].mix = 1.0f;
            r[1].muted = true;
            p.setRays (r);
            play (p, {});
            play (p, {});
            check (near (left[frames - 1], 0.1, 1.0e-6), "instrument prism: a muted ray is silent");
            r[1].muted = false;
            r[1].soloed = true;
            p.setRays (r);
            play (p, {});
            play (p, {});
            check (near (left[frames - 1], 0.2, 1.0e-6), "instrument prism: a soloed ray is all that is heard");
            r[1].soloed = false;
            r[0].gainDb = -60.0f;
            p.setRays (r);
            play (p, {});
            play (p, {});
            check (near (left[frames - 1], 0.2, 1.0e-6), "instrument prism: a ray at -60 dB is off");

            // Its own settings come back from its state.
            r[0].name = "Soft layer";
            r[0].gainDb = -6.0f;
            r[0].lowKey = 36;
            p.setRays (r);
            p.setName ("Keys");
            const auto saved = p.state();
            auto copy = r;
            copy[0].name.clear();
            copy[0].gainDb = 0.0f;
            copy[0].lowKey = 0;
            p.setRays (copy);
            p.setState (saved);
            const auto back = p.rays();
            check (back[0].name == "Soft layer" && back[0].gainDb == -6.0f && back[1].splitKey == 60 && back[0].lowKey == 36 && back[1].lowVelocity == 81 && p.name() == "Keys",
                   "instrument prism: its settings come back from its state");
        }

        // A ray with latency in its effects: the others wait for it, so layers stay together.
        {
            constexpr int delay = 10;
            mixer::InstrumentPrism p;
            p.prepare (48000.0, frames);
            mixer::InstrumentPrism::Ray a, b;
            a.instrument = std::make_shared<Tone>();
            a.effects.push_back ({ std::make_shared<prismtest::Double> (delay), false, 1.0f });
            b.instrument = std::make_shared<Tone>();
            p.setRays ({ a, b });
            check (p.latency() == delay, "instrument prism: its latency is the slowest ray's");
            play (p, { on (60, 100) });
            check (left[delay - 1] == 0.0f && near (left[delay + 1], 0.3, 1.0e-6), "instrument prism: rays line up with the slowest");
        }

        // Longer blocks than it was prepared for are played in pieces, the events in their place.
        {
            mixer::InstrumentPrism p;
            p.prepare (48000.0, 16);
            auto tone = std::make_shared<Tone>();
            mixer::InstrumentPrism::Ray a;
            a.instrument = tone;
            p.setRays ({ a });
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);
            const mixer::NoteEvent late { 40, 0x90, 60, 100 };
            p.process (&late, 1, left.data(), right.data(), frames);
            check (left[39] == 0.0f && near (left[40], 0.1, 1.0e-6) && near (left[frames - 1], 0.1, 1.0e-6),
                   "instrument prism: a long block is played in pieces");
        }
        // Two instruments in a ray, A and B: layered with a mix between them, or split at a key.
        {
            mixer::InstrumentPrism p;
            p.prepare (48000.0, frames);
            auto first = std::make_shared<Tone>(), second = std::make_shared<Tone>();
            mixer::InstrumentPrism::Ray ray;
            ray.instrument = first;
            ray.second = second;
            p.setRays ({ ray });
            play (p, { on (60, 100) });
            check (first->playing == 1 && second->playing == 1, "instrument prism: A and B both play a layered note");
            check (near (left[frames - 1], 0.2, 1.0e-6), "instrument prism: A and B in full at the middle of the mix");
            auto r = p.rays();
            r[0].blend = 0.0f;
            p.setRays (r);
            play (p, {});
            play (p, {});
            check (near (left[frames - 1], 0.1, 1.0e-6), "instrument prism: the mix at A plays only A");
            r[0].blend = 0.75f;
            p.setRays (r);
            play (p, {});
            play (p, {});
            check (near (left[frames - 1], 0.15, 1.0e-6), "instrument prism: the mix towards B fades A");
            play (p, { off (60) });
            check (first->playing == 0 && second->playing == 0, "instrument prism: a layered note ends on both");

            // Split at C4: below it A, from it up B; a note's end follows it even when the split moves.
            r[0].blend = 0.5f;
            r[0].split = true;
            r[0].splitKey = 60;
            p.setRays (r);
            play (p, { on (48, 100) });
            check (first->playing == 1 && second->playing == 0, "instrument prism: below the split plays A");
            play (p, { on (72, 100) });
            check (first->playing == 1 && second->playing == 1, "instrument prism: above the split plays B");
            r[0].splitKey = 30;
            p.setRays (r);
            play (p, { off (48), off (72) });
            check (first->playing == 0 && second->playing == 0, "instrument prism: split notes end where they started");

            // B on its own becomes A.
            r[0].instrument = nullptr;
            p.setRays (r);
            const auto lone = p.rays();
            check (lone[0].instrument == second && lone[0].second == nullptr && near (lone[0].gainA(), 1.0, 1.0e-6), "instrument prism: a lone B becomes A");
        }
        check (mixer::prismNoteName (60) == "C4" && mixer::prismNoteName (0) == "C-1" && mixer::prismNoteName (127) == "G9", "instrument prism: note names");
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

    // Image sonification: pictures read as notes, with free pitch through pitch bend.
    void testSonify()
    {
        using namespace sonify;
        {
            // B, G, R, x: the left half white, the right half black, averaged to 2 by 1.
            std::vector<uint8_t> px (4 * 2 * 4, 0);
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    for (int c = 0; c < 3; ++c)
                        px[(size_t) (y * 4 + x) * 4 + (size_t) c] = 255;
            const auto p = fromPixels (px.data(), 4, 2, 16, 2, 1);
            check (p.width == 2 && p.height == 1 && near (p.at (0, 0), 1.0, 1e-4) && near (p.at (1, 0), 0.0, 1e-4), "sonify picture from pixels");
        }

        check (nearestInScale (65.0, Scale::pentatonic, 0) == 64.0, "sonify: F snaps to E in C pentatonic");
        check (nearestInScale (66.0, Scale::major, 2) == 66.0, "sonify: F# is in D major");
        check (nearestInScale (60.4, Scale::chromatic, 0) == 60.0, "sonify: chromatic rounds");
        Settings s;
        s.lowest = 36;
        s.highest = 84;
        s.scale = Scale::major;
        s.quantize = 0.0;
        check (near (pitchFor (0.51, s), 60.48, 1e-9), "sonify: free pitch");
        s.quantize = 1.0;
        check (pitchFor (0.51, s) == 60.0, "sonify: quantized pitch");
        s.quantize = 0.5;
        check (near (pitchFor (0.51, s), 60.24, 1e-9), "sonify: half quantized");

        const auto find = [] (const std::vector<Message>& out, int status)
        {
            return std::find_if (out.begin(), out.end(), [status] (const Message& m) { return m.status == status; });
        };

        {
            // A bright spot at the top of the first column, scanned: the top note, then let go.
            Picture p;
            p.width = 16;
            p.height = 12;
            p.light.assign (16 * 12, 0.0f);
            p.light[0] = 1.0f;
            Sonifier son;
            Settings t;
            t.scale = Scale::pentatonic;
            t.voices = 1;
            t.drums = false;
            son.setSettings (t);
            std::vector<Message> out;
            son.advance (p, 0.0, out);
            const auto on = find (out, 0x90);
            check (on != out.end() && on->data1 == 84 && on->data2 == 127, "sonify scan plays the top note");
            check (son.sounding().size() == 1 && son.sounding()[0].y < 0.1f, "sonify scan says where it plays");
            out.clear();
            son.advance (p, 0.125, out);   // a sixteenth at 120 BPM: the next, dark column
            const auto off = find (out, 0x80);
            check (off != out.end() && off->data1 == 84, "sonify scan lets go in the dark");
            check (near (son.scanPosition(), 1.0 / 16.0, 1e-6), "sonify scan position");
        }
        {
            // A lane lit between notes: the note below and a pitch bend up to the rest.
            Picture p;
            p.width = 1;
            p.height = 5;
            p.light = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f };   // centred 2.5 rows down: 0.375 up
            Sonifier son;
            Settings t;
            t.mode = Mode::lanes;
            t.voices = 1;
            t.quantize = 0.0;
            t.lowest = 36;
            t.highest = 85;   // 36 + 0.375 * 49 = 54.375
            t.drums = false;
            son.setSettings (t);
            std::vector<Message> out;
            son.advance (p, 0.0, out);
            const auto bend = find (out, 0xE0), on = find (out, 0x90);
            check (on != out.end() && on->data1 == 54, "sonify lanes note");
            check (bend != out.end() && bend < on && std::abs ((bend->data1 | (bend->data2 << 7)) - (8192 + 1536)) <= 2, "sonify lanes bends to the free pitch");
            out.clear();
            son.stop (out);
            check (find (out, 0x80) != out.end() && find (out, 0xE0) != out.end(), "sonify stop lets go and centres the bend");
        }
        {
            // Stripes: every step changes everything, so the drums play on channel 10.
            Picture p;
            p.width = 8;
            p.height = 6;
            p.light.assign (8 * 6, 0.0f);
            for (int y = 0; y < 6; ++y)
                for (int x = 0; x < 8; x += 2)
                    p.light[(size_t) (y * 8 + x)] = 1.0f;
            Sonifier son;
            Settings t;
            t.columns = 8;
            son.setSettings (t);
            std::vector<Message> out;
            son.advance (p, 0.0, out);
            son.advance (p, 0.125, out);
            int drums = 0;
            for (const auto& m : out)
                drums += m.status == (0x90 | drumChannel) ? 1 : 0;
            check (drums == 3, "sonify drums on change");
        }

        {
            // Phi: powers of the golden ratio folded into the octave, between the keys.
            const double up = std::log2 ((1.0 + std::sqrt (5.0)) / 2.0) * 12.0;   // 8.33 semitones
            check (near (nearestInScale (60.0 + up + 0.2, Scale::phi, 0), 60.0 + up, 1e-9), "sonify phi scale step");
            check (nearestInScale (60.1, Scale::phi, 0) == 60.0, "sonify phi scale root");
            std::string word;
            for (long i = 0; i < 13; ++i)
                word += fibonacciBeat (i) ? '1' : '0';
            check (word == "1011010110110", "sonify Fibonacci word");
        }
        {
            // Spiral: the first point is the centre; the voices take turns.
            Picture p;
            p.width = 16;
            p.height = 12;
            p.light.assign (16 * 12, 1.0f);
            Sonifier son;
            Settings t;
            t.mode = Mode::spiral;
            t.voices = 2;
            t.drums = false;
            son.setSettings (t);
            std::vector<Message> out;
            son.advance (p, 0.0, out);
            check (son.sounding().size() == 1 && near (son.sounding()[0].x, 0.5, 0.05) && near (son.sounding()[0].y, 0.5, 0.05), "sonify spiral starts at the centre");
            son.advance (p, 0.125, out);
            check (son.sounding().size() == 2, "sonify spiral voices take turns");
            check (find (out, 0x91) != out.end(), "sonify spiral second voice on channel 2");
        }
    }

    // The Φ dynamics: golden ratios, the two-stage release, flat multiband crossovers
    // and a limiter that never passes its ceiling.
    void testPhiDynamics()
    {
        const double rate = 48000.0;
        const double golden = 1.6180339887498949;
        auto near = [] (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; };
        auto sine = [rate] (double hz, int frames, double amplitude)
        {
            std::vector<float> v ((size_t) frames);
            for (int i = 0; i < frames; ++i)
                v[(size_t) i] = (float) (amplitude * std::sin (2.0 * M_PI * hz * i / rate));
            return v;
        };
        auto rmsDb = [] (const std::vector<float>& v, size_t from)
        {
            double sum = 0.0;
            for (size_t i = from; i < v.size(); ++i)
                sum += (double) v[i] * v[i];
            return 10.0 * std::log10 (sum / (double) (v.size() - from) + 1.0e-30);
        };

        check (near (phi::ratioOf (2), golden, 1.0e-9) && near (phi::ratioOf (4), golden * golden, 1.0e-9), "phi ratios are powers of phi");

        // The curve: above the knee, 6 dB more in gives 6/φ dB more out at φ:1.
        {
            phi::Compressor::Settings s;
            check (near (phi::Compressor::curveDb (s, -6.0) - phi::Compressor::curveDb (s, -12.0), 6.0 / golden, 1.0e-6)
                   && near (phi::Compressor::curveDb (s, -40.0), -40.0, 1.0e-9), "phi compressor curve");
        }

        // A steady square wave at -6 dBFS, 12 dB over a -18 dB threshold at φ:1, comes out
        // 12 × (1 - 1/φ) = 4.58 dB down.
        {
            phi::Compressor c;
            c.prepare (rate, 1);
            c.set ({});
            std::vector<float> x ((size_t) rate * 2);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (i / 50) % 2 == 0 ? 0.5f : -0.5f;
            float* io[] { x.data() };
            c.process (io, 1, (int) x.size());
            const auto outDb = 20.0 * std::log10 (std::abs (x.back()));
            check (near (outDb, 20.0 * std::log10 (0.5) - 12.0 * (1.0 - 1.0 / golden), 0.1) && near (c.reductionDb(), 4.58, 0.1),
                   "phi compressor reaches its ratio, got " + std::to_string (outDb));
        }

        // The golden release: after `release` ms, 1/φ of e^-φ plus 1/φ² of e^-(1/φ) is left.
        {
            phi::GoldenRelease r;
            r.set (rate, 0.001, 100.0);
            for (int i = 0; i < 1000; ++i)
                r.next (10.0);
            for (int i = 0; i < (int) (rate * 0.1); ++i)
                r.next (0.0);
            const auto expected = 10.0 * (std::exp (-golden) / golden + std::exp (-1.0 / golden) / (golden * golden));
            check (near (r.current(), expected, 0.01 * expected), "phi release is golden two-stage, got " + std::to_string (r.current()));
        }

        // Φ Multiband sums back flat when nothing is compressed.
        {
            bool flat = true;
            std::string worst;
            for (const auto hz : { 40.0, 150.0, 209.0, 500.0, 887.0, 2000.0, 3760.0, 9000.0 })
            {
                phi::Multiband m;
                phi::Multiband::Settings s;
                for (auto& b : s.bands)
                    b.thresholdDb = 0.0;
                m.prepare (rate, 1);
                m.set (s);
                auto x = sine (hz, 48000, 0.1);
                const auto before = rmsDb (x, 24000);
                float* io[] { x.data() };
                m.process (io, 1, (int) x.size());
                if (! near (rmsDb (x, 24000), before, 0.02))
                {
                    flat = false;
                    worst = std::to_string (hz);
                }
            }
            check (flat, "phi multiband bands sum back flat" + (worst.empty() ? std::string() : ", not at " + worst + " Hz"));
            check (near (phi::Multiband::crossoverHz (2), 80.0 * golden * golden, 1.0e-9), "phi multiband crossovers at 80 Hz x phi^n");

            // Only the band over its threshold is turned down.
            phi::Multiband::Settings s;
            for (auto& b : s.bands)
                b.thresholdDb = 0.0;
            s.bands[3].thresholdDb = -40.0;
            auto level = [&] (double hz)
            {
                phi::Multiband m;
                m.prepare (rate, 1);
                m.set (s);
                auto x = sine (hz, 48000, 0.5);
                const auto before = rmsDb (x, 24000);
                float* io[] { x.data() };
                m.process (io, 1, (int) x.size());
                return rmsDb (x, 24000) - before;
            };
            check (std::abs (level (100.0)) < 0.3 && level (9000.0) < -6.0, "phi multiband compresses only its band");
        }

        // Φ Limiter: φ² ms ahead, never over the ceiling, and clean below it.
        {
            phi::Limiter l;
            phi::Limiter::Settings s;
            s.driveDb = 12.0;
            s.ceilingDb = -1.0;
            l.prepare (rate, 2);
            l.set (s);
            check (l.latency() == (int) std::lround (phi::Limiter::lookaheadMs * rate / 1000.0) + phi::TruePeak::delay, "phi limiter latency");
            std::mt19937 random (5);
            std::normal_distribution<float> gauss (0.0f, 0.3f);
            std::vector<float> a ((size_t) rate), b ((size_t) rate);
            for (size_t i = 0; i < a.size(); ++i)
            {
                a[i] = gauss (random);
                b[i] = gauss (random);
            }
            float* io[] { a.data(), b.data() };
            l.process (io, 2, (int) a.size());
            float peak = 0.0f;
            for (size_t i = 0; i < a.size(); ++i)
                peak = std::max ({ peak, std::abs (a[i]), std::abs (b[i]) });
            check (peak <= (float) std::pow (10.0, -1.0 / 20.0) + 1.0e-6f && l.reductionDb() > 0.0, "phi limiter holds its ceiling");

            phi::Limiter quiet;
            quiet.prepare (rate, 1);
            quiet.set ({});
            const auto x = sine (440.0, 4800, 0.1);
            auto y = x;
            float* q[] { y.data() };
            quiet.process (q, 1, (int) y.size());
            double error = 0.0;
            for (size_t i = (size_t) quiet.latency(); i < y.size(); ++i)
                error = std::max (error, (double) std::abs (y[i] - x[i - (size_t) quiet.latency()]));
            check (error < 1.0e-6, "phi limiter leaves quiet sound alone, only delayed");
        }
    }

    // Φ Chorus: rates √φ apart, voices spread by the golden angle, dry at no mix.
    void testPhiChorus()
    {
        const double rate = 48000.0;
        auto near = [] (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; };
        phi::Chorus::Settings s;
        bool golden = true, apart = true;
        for (int v = 0; v + 1 < s.voices; ++v)
        {
            golden = golden && near (phi::Chorus::voiceRate (s, v + 1) / phi::Chorus::voiceRate (s, v), std::sqrt (phi::ratio), 1.0e-9);
            for (int w = v + 1; w < s.voices; ++w)
                apart = apart && std::abs (phi::Chorus::voicePan (s, v) - phi::Chorus::voicePan (s, w)) > 0.1;
        }
        check (golden, "phi chorus rates are a golden step apart");
        check (apart, "phi chorus voices sit in different places");
        check (near (phi::Chorus::voiceRate (s, 2), s.rateHz, 1.0e-9) && near (phi::Chorus::voiceDelayMs (s, 2), s.delayMs, 1.0e-9),
               "phi chorus middle voice at the rate and delay");

        auto run = [&] (const phi::Chorus::Settings& settings, std::vector<float>& a, std::vector<float>& b)
        {
            phi::Chorus c;
            c.prepare (rate, 2);
            c.set (settings);
            float* io[] { a.data(), b.data() };
            c.process (io, 2, (int) a.size());
            return c.delayNowMs (0);
        };
        std::vector<float> a (48000), b (48000);
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = b[i] = (float) (0.5 * std::sin (2.0 * M_PI * 440.0 * (double) i / rate));
        auto dryA = a;
        auto settings = s;
        settings.mix = 0.0;
        run (settings, a, b);
        check (a == dryA, "phi chorus at no mix leaves the sound alone");

        settings = s;
        settings.voices = 8;
        settings.depthMs = 10.0;
        settings.delayMs = 34.0;
        settings.mix = 1.0;
        const auto delay = run (settings, a, b);
        double energy = 0.0, difference = 0.0;
        bool finite = true;
        for (size_t i = 24000; i < a.size(); ++i)
        {
            finite = finite && std::isfinite (a[i]) && std::isfinite (b[i]);
            energy += (double) a[i] * a[i];
            difference += std::abs ((double) a[i] - b[i]);
        }
        check (finite && energy > 100.0 && difference > 1.0, "phi chorus sings, wider than mono, at its longest");
        check (delay > 0.0 && delay <= phi::Chorus::maxDelayMs, "phi chorus delays stay inside its line");
    }

    // Φ Modulation: Flow's four modes, Space moving partials, Grain and Matrix staying sane.
    void testPhiModulation()
    {
        const double rate = 48000.0;
        auto near = [] (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; };

        // Flow
        phi::Flow::Settings f;
        check (near (phi::Flow::stepOf (f, 0), phi::inverse, 1.0e-12) && near (phi::Flow::outputRate (f, 2) / phi::Flow::outputRate (f, 1), phi::ratio, 1.0e-9),
               "phi flow steps 1/phi a turn, its outputs phi apart");
        {
            std::set<long long> seen;
            bool inside = true, again = true;
            for (int n = 0; n < 10000; ++n)
            {
                const auto v = phi::Flow::event (f, 0, n);
                inside = inside && v >= 0.0 && v < 1.0;
                again = again && v == phi::Flow::event (f, 0, n);
                seen.insert (std::llround (v * 1.0e9));
            }
            check (inside && again && seen.size() == 10000, "phi flow irrational: never the same value twice, the same every time");
        }
        {
            auto p = f;
            p.mode = phi::Flow::Mode::periodic;
            bool repeats = true;
            for (int n = 0; n < 100; ++n)
                repeats = repeats && phi::Flow::event (p, 0, n) == phi::Flow::event (p, 0, n + 8);
            check (repeats && near (phi::Flow::ratioOf (p), 1.5, 1.0e-12), "phi flow periodic: rounded to simple fractions, repeats every 8 events");
        }
        {
            // Fibonacci holds each value for one event or two, in the Fibonacci word.
            auto b = f;
            b.mode = phi::Flow::Mode::fibonacci;
            bool word = true;
            for (int n = 1; n < 200; ++n)
                word = word && ((phi::Flow::event (b, 0, n) != phi::Flow::event (b, 0, n - 1)) == phi::hit (n, phi::inverse));
            int longest = 1, run = 1;
            for (int n = 1; n < 200; ++n)
            {
                run = phi::Flow::event (b, 0, n) == phi::Flow::event (b, 0, n - 1) ? run + 1 : 1;
                longest = std::max (longest, run);
            }
            check (word && longest == 2, "phi flow fibonacci: values move on the hits of the Fibonacci word");
        }
        {
            auto h = f;
            h.mode = phi::Flow::Mode::hybrid;
            h.jitter = 0.1;
            bool bounded = true, moved = false;
            for (int n = 0; n < 1000; ++n)
            {
                const auto d = std::abs (phi::Flow::event (h, 0, n) - phi::Flow::event (f, 0, n));
                const auto wrapped = std::min (d, 1.0 - d);
                bounded = bounded && wrapped <= 0.05 + 1.0e-9;
                moved = moved || wrapped > 1.0e-6;
            }
            check (bounded && moved && phi::Flow::event (h, 0, 5) == phi::Flow::event (h, 0, 5), "phi flow hybrid: bounded noise, the same every time");
        }
        {
            phi::Flow a, b;
            a.prepare (rate);
            b.prepare (rate);
            a.set (f);
            b.set (f);
            bool same = true, inside = true;
            for (int block = 0; block < 2000; ++block)
            {
                a.advance (512);
                b.advance (512);
                for (int k = 0; k < phi::Flow::numOutputs; ++k)
                {
                    same = same && a.output (k) == b.output (k);
                    inside = inside && a.output (k) >= 0.0f && a.output (k) <= 1.0f;
                }
            }
            check (same && inside, "phi flow plays the same twice, inside 0 to 1");
        }

        // Space
        {
            phi::SpectralSpace::Settings s;
            check (near (phi::SpectralSpace::mappedHz (s, 440.0), 220.0 * phi::ratio, 1.0e-9) && near (phi::SpectralSpace::mappedHz (s, 880.0), 220.0 * phi::ratio * phi::ratio, 1.0e-9)
                   && near (phi::SpectralSpace::mappedHz (s, 220.0), 220.0, 1.0e-9), "phi space maps octaves above the anchor to powers of phi");
            auto plain = s;
            plain.blend = 0.0;
            check (near (phi::SpectralSpace::mappedHz (plain, 1234.0), 1234.0, 1.0e-9), "phi space at no blend leaves partials alone");

            const int frames = 512 * 93;
            std::vector<float> x ((size_t) frames);
            for (int i = 0; i < frames; ++i)
                x[(size_t) i] = (float) (0.5 * std::sin (2.0 * M_PI * 440.0 * i / rate) + 0.25 * std::sin (2.0 * M_PI * 880.0 * i / rate));
            auto run = [&] (const phi::SpectralSpace::Settings& settings)
            {
                phi::SpectralSpace space;
                space.prepare (rate, 1);
                space.set (settings);
                auto y = x;
                for (int i = 0; i + 512 <= frames; i += 512)
                {
                    float* io[] { y.data() + i };
                    space.process (io, 1, 512);
                }
                return y;
            };
            auto amplitude = [&] (const std::vector<float>& y, double hz)
            {
                double re = 0.0, im = 0.0;
                for (int i = 20000; i < frames; ++i)
                {
                    re += y[(size_t) i] * std::cos (2.0 * M_PI * hz * i / rate);
                    im += y[(size_t) i] * std::sin (2.0 * M_PI * hz * i / rate);
                }
                return 2.0 * std::sqrt (re * re + im * im) / (frames - 20000);
            };
            const auto same = run (plain);
            double error = 0.0, energy = 0.0;
            for (int i = 10000; i < frames; ++i)
            {
                error += std::pow (same[(size_t) i] - x[(size_t) i - 2048], 2.0);
                energy += std::pow (x[(size_t) i], 2.0);
            }
            check (error / energy < 1.0e-6, "phi space at no blend gives back the sound, 2048 samples late");
            const auto moved = run (s);
            check (amplitude (moved, 220.0 * phi::ratio) > 0.4 && amplitude (moved, 220.0 * phi::ratio * phi::ratio) > 0.2
                   && amplitude (moved, 440.0) < 0.02 && amplitude (moved, 880.0) < 0.02, "phi space moves 440 and 880 Hz to 356 and 576 Hz");
        }

        // Grain and Matrix: quiet in, quiet out; loud in, bounded and finite; the same twice.
        {
            auto render = [&] (bool matrixToo, std::vector<float>& left, std::vector<float>& right)
            {
                phi::Grain grain;
                phi::Matrix matrix;
                grain.prepare (rate, 2);
                matrix.prepare (rate, 2);
                phi::Grain::Settings g;
                g.mix = 1.0;
                g.pitch = 12.0;
                g.flow = 1.0;
                phi::Matrix::Settings m;
                m.mix = 1.0;
                m.feedback = 0.95;
                m.eventsHz = 8.0;
                m.driveDb = 36.0;
                grain.set (g, f);
                matrix.set (m, f);
                matrix.reset();
                for (size_t i = 0; i < left.size(); i += 512)
                {
                    float* io[] { left.data() + i, right.data() + i };
                    grain.process (io, 2, 512, 0.3f, 0.7f);
                    if (matrixToo)
                        matrix.process (io, 2, 512, 0.5f);
                }
                return matrix.currentRoute();
            };
            std::vector<float> l (512 * 2000), r (512 * 2000);
            for (size_t i = 0; i < 512 * 200; ++i)
                l[i] = r[i] = (float) std::sin (2.0 * M_PI * 220.0 * (double) i / rate);
            auto l2 = l, r2 = r;
            const auto route = render (true, l, r);
            render (true, l2, r2);
            bool finite = true;
            double peak = 0.0, heard = 0.0;
            for (size_t i = 0; i < l.size(); ++i)
            {
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                peak = std::max (peak, (double) std::max (std::abs (l[i]), std::abs (r[i])));
                heard += i < 512 * 200 ? 0.0 : std::abs (l[i]);
            }
            check (finite && peak < 4.0 && heard > 1.0, "phi grain and matrix: finite and bounded at full feedback, with a tail");
            check (l == l2 && r == r2, "phi grain and matrix play the same twice");
            check (route[0] != route[1] && route[1] != route[2] && route[0] != route[2], "phi matrix routes three different nodes");
            double last = 0.0;
            for (size_t i = l.size() - 4800; i < l.size(); ++i)
                last = std::max (last, (double) std::abs (l[i]));
            check (last < 1.0e-3, "phi matrix dies away once the sound stops");
        }
        {
            std::set<std::array<int, 3>> routes;
            for (int n = 0; n < 200; ++n)
                routes.insert (phi::Matrix::route (f, n));
            check (routes.size() > 12, "phi matrix visits many routes");
        }
    }

    // The modulator rack: every kind stays within 0..1, plays the same twice, and does its job.
    void testModulatorRack()
    {
        const double rate = 48000.0;
        auto near = [] (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; };
        using modrack::Kind;

        std::vector<modrack::Modulator> all;
        for (int k = 0; k < modrack::numKinds; ++k)
            all.push_back (modrack::make ((Kind) k));
        all[(size_t) Kind::sampleHold].values[0] = 1.0f;   // Sample & Hold catches the LFO
        all[(size_t) Kind::shaper].values[0] = 1.0f;
        all[(size_t) Kind::crossfader].values[0] = 1.0f;
        all[(size_t) Kind::crossfader].values[1] = 4.0f;
        all[(size_t) Kind::random].values[2] = 0.3f;

        std::vector<float> noise (modrack::chunk);
        auto run = [&] (std::vector<std::vector<float>>& out)
        {
            modrack::Rack rack;
            rack.prepare (rate);
            rack.set (all);
            out.assign (all.size(), {});
            for (int block = 0; block < 4000; ++block)
            {
                for (int i = 0; i < modrack::chunk; ++i)
                    noise[(size_t) i] = block % 400 < 40 ? (float) (0.5 * std::sin (0.05 * (block * modrack::chunk + i))) : 0.0f;
                const float* in[] { noise.data() };
                rack.process (in, 1, modrack::chunk);
                for (size_t m = 0; m < all.size(); ++m)
                    out[m].push_back (rack.value ((int) m));
            }
        };
        std::vector<std::vector<float>> a, b;
        run (a);
        run (b);
        bool inside = true, moves = true;
        for (size_t m = 0; m < a.size(); ++m)
        {
            float low = 1.0f, high = 0.0f;
            for (const auto v : a[m])
            {
                inside = inside && v >= 0.0f && v <= 1.0f;
                low = std::min (low, v);
                high = std::max (high, v);
            }
            if ((Kind) m != Kind::macro)
                moves = moves && high - low > 0.1f;
        }
        check (inside && a == b, "modulators stay within 0 to 1 and play the same twice");
        check (moves, "every modulator but the macro moves");

        // The LFO: a sine from 0, at the top half a cycle in.
        {
            modrack::Rack rack;
            rack.prepare (rate);
            rack.set ({ modrack::make (Kind::lfo) });
            for (int i = 0; i < (int) (0.5 * rate) / modrack::chunk; ++i)
                rack.process (nullptr, 0, modrack::chunk);
            check (rack.value (0) > 0.99f, "modulator lfo: a 1 Hz sine is at its top after half a second");
        }
        // Random at full déjà vu... at the middle: the loop of Length values comes round again.
        {
            auto r = modrack::make (Kind::random);
            r.values[2] = 0.5f;   // déjà vu: locked
            r.values[3] = 4.0f;   // length 5
            r.values[1] = 0.0f;   // no smoothing
            r.values[0] = 10.0f;
            modrack::Rack rack;
            rack.prepare (rate);
            rack.set ({ r });
            std::vector<float> events;
            float last = -1.0f;
            for (int i = 0; i < 4000 && events.size() < 20; ++i)
            {
                rack.process (nullptr, 0, modrack::chunk);
                if (rack.value (0) != last)
                    events.push_back (last = rack.value (0));
            }
            bool loops = events.size() >= 15;
            for (size_t i = 5; i < events.size(); ++i)
                loops = loops && events[i] == events[i - 5];
            check (loops, "modulator random: deja vu at the middle replays a loop of its length");
        }
        // Φ Flow gives Flow's own sequence.
        {
            auto f = modrack::make (Kind::phiFlow);
            f.values[0] = 2.0f;
            modrack::Rack rack;
            rack.prepare (rate);
            rack.set ({ f });
            const int blocks = 3000;
            for (int i = 0; i < blocks; ++i)
                rack.process (nullptr, 0, modrack::chunk);
            phi::Flow::Settings s;
            s.rateHz = 2.0;
            check (near (rack.value (0), phi::Flow::at (s, 0, 2.0 * blocks * modrack::chunk / rate), 1.0e-4), "modulator phi flow follows phi::Flow");
        }
        // Routes set within 0..1 between their ends.
        modrack::Route route;
        route.low = 0.8f;
        route.high = 0.2f;
        check (near (modrack::routed (route, 0.0f), 0.8, 1.0e-6) && near (modrack::routed (route, 0.5f), 0.5, 1.0e-6)
               && near (modrack::routed (route, 1.0f), 0.2, 1.0e-6), "modulator routes run from low to high, either way round");
    }

    // The PHI synth's tuning (keys to pitches, sent as MIDI Tuning Standard) and MPE.
    void testPhiTuning()
    {
        const phi::Tuning standard;
        check (near (standard.hz (69), 440.0, 1.0e-9) && near (standard.hz (60), 261.6255653, 1.0e-6), "tuning: 12-TET by default");
        check (phi::Tuning::equal (12) == standard, "tuning: 12 equal steps is the default");
        {
            const auto five = phi::Tuning::equal (5, 60, 261.6255653005986);
            check (near (five.hz (65), 2.0 * five.hz (60), 1.0e-9) && near (five.hz (61) / five.hz (60), std::pow (2.0, 0.2), 1.0e-12), "tuning: 5 equal steps to the octave");
        }
        {
            // A just major scale: 1, 9/8, 5/4, 4/3, 3/2, 5/3, 15/8, on consecutive keys from C4.
            std::vector<double> cents;
            for (const double r : { 1.0, 9.0 / 8.0, 5.0 / 4.0, 4.0 / 3.0, 3.0 / 2.0, 5.0 / 3.0, 15.0 / 8.0 })
                cents.push_back (phi::Tuning::ratioToCents (r));
            const auto just = phi::Tuning::scale (cents, 1200.0, 60, 260.0);
            check (near (just.hz (64) / just.hz (60), 1.5, 1.0e-12) && near (just.hz (67), 520.0, 1.0e-9)
                   && near (just.hz (59), 260.0 * 15.0 / 16.0, 1.0e-9), "tuning: a just scale on consecutive keys, up and down");
            check (near (phi::Tuning::ratioToCents (1.5), 701.955, 1.0e-3), "tuning: ratios to cents");

            // Through MIDI Tuning Standard and back, within its resolution (100/16384 cent).
            phi::Tuning back;
            bool parsed = true;
            for (const auto& m : just.toSysEx())
                parsed = parsed && m.front() == 0xf0 && m.back() == 0xf7 && back.applySysEx (m.data(), (int) m.size());
            bool close = parsed;
            for (int k = 0; k < phi::Tuning::numKeys; ++k)
                if (just.hz (k) > 8.2 && just.hz (k) < 12500.0)
                    close = close && std::abs (1200.0 * std::log2 (back.hz (k) / just.hz (k))) < 0.01;
            check (close, "tuning: survives MIDI Tuning Standard");
            const std::uint8_t notTuning[] = { 0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7 };
            check (! back.applySysEx (notTuning, 6), "tuning: other SysEx is left alone");
        }
        {
            // A quarter step: 50 cents above C4.
            phi::Tuning t;
            t.setHz (61, 261.6255653005986 * std::pow (2.0, 50.0 / 1200.0));
            phi::Tuning back;
            for (const auto& m : t.toSysEx())
                back.applySysEx (m.data(), (int) m.size());
            check (near (phi::Tuning::centsFromEqual (back.hz (61), 60), 50.0, 0.01), "tuning: a quarter step");
        }

        // MPE: each note bends, presses and slides on its own channel, plus the zone's master bend.
        {
            phi::Mpe mpe;
            check (mpe.isMember (1) && mpe.isMember (15) && ! mpe.isMember (0), "mpe: lower zone with 15 members by default");
            const auto on = mpe.handle (0x92, 60, 100);
            check (on.kind == phi::Mpe::Kind::noteOn && on.channel == 2 && on.key == 60, "mpe: note on");
            mpe.handle (0xe2, 0, 0x50);     // member bend: (0x50 << 7 - 8192) / 8192 × 48
            mpe.handle (0xd2, 64, 0);       // pressure
            mpe.handle (0xb2, 74, 127);     // timbre
            mpe.handle (0xe3, 0, 0x60);     // another note's channel
            const auto e = mpe.expression (2, 60);
            check (near (e.bendSemitones, (0x50 * 128 - 8192) / 8192.0 * 48.0, 1.0e-9) && near (e.pressure, 64.0 / 127.0, 1.0e-9) && near (e.timbre, 1.0, 1.0e-9),
                   "mpe: a note's own bend, pressure and timbre");
            mpe.handle (0xe0, 0, 0x60);     // master bend, ±2
            check (near (mpe.expression (2, 60).bendSemitones, e.bendSemitones + 1.0, 1.0e-9), "mpe: the master channel bends the whole zone");
            check (near (mpe.noteHz (phi::Tuning::equal (24), 2, 60) / phi::Tuning::equal (24).hz (60),
                         std::pow (2.0, mpe.expression (2, 60).bendSemitones / 12.0), 1.0e-12), "mpe: bends on top of the tuning");

            // An MPE Configuration Message for 7 members in the lower zone frees channels 8 to 15.
            mpe.handle (0xb0, 101, 0);
            mpe.handle (0xb0, 100, 6);
            mpe.handle (0xb0, 6, 7);
            check (mpe.lowerZoneMembers() == 7 && mpe.isMember (7) && ! mpe.isMember (8), "mpe: zone set by its configuration message");
            mpe.handle (0xb9, 101, 0);      // RPN 0 on an ordinary channel: bend range 12
            mpe.handle (0xb9, 100, 0);
            mpe.handle (0xb9, 6, 12);
            mpe.handle (0xe9, 0x7f, 0x7f);
            check (near (mpe.expression (9, 60).bendSemitones, 12.0 * 8191.0 / 8192.0, 1.0e-9), "mpe: an ordinary channel with its own bend range");
            check (mpe.handle (0x82, 60, 0).kind == phi::Mpe::Kind::noteOff && mpe.handle (0x92, 61, 0).kind == phi::Mpe::Kind::noteOff, "mpe: note off, and note on at velocity 0");
        }
    }

    // PHI rāga's sheets: every built-in sheet reads cleanly, and a sheet survives writing out.
    void testRagaSheets()
    {
        std::vector<std::string> errors;
        const auto all = phi::parseRagas (phi::builtInRagaSheets(), errors);
        check (errors.empty() && all.size() == 176 && phi::builtInRagas().size() == 176, "raga sheets: the 176 built-in sheets read without a mistake");
        for (const auto& e : errors)
            std::printf ("  %s\n", e.c_str());
        // Darpaṇ, made for PHI, is a mirror: each svara's reflection about Sa (and Ma) is one of its
        // svaras, tuned so, and its avaroha is its āroha reflected.
        for (const auto& r : all)
            if (r.plain == "darpan")
            {
                bool mirrored = near (r.cents[6], 600.0, 1.0e-9) && r.aroha.size() == r.avaroha.size();
                for (int k = 1; k < 12; ++k)
                    mirrored = mirrored && r.uses[(size_t) k] == r.uses[(size_t) (12 - k)]
                               && (! r.uses[(size_t) k] || near (r.cents[(size_t) k] + r.cents[(size_t) (12 - k)], 1200.0, 1.0e-6));
                for (size_t i = 0; mirrored && i < r.aroha.size(); ++i)
                    mirrored = (12 - r.aroha[i].key()) % 12 == ((r.avaroha[i].key() % 12) + 12) % 12;
                check (mirrored, "raga sheets: darpan is its own mirror");
            }
        bool complete = true;
        for (const auto& r : all)
            complete = complete && ! r.aroha.empty() && ! r.avaroha.empty() && r.vadi >= 0 && r.samvadi >= 0;
        check (complete, "raga sheets: each has its aroha, avaroha, vadi and samvadi");

        const auto* darbari = &all[0];
        for (const auto& r : all)
            if (r.plain == "darbari kanada")
                darbari = &r;
        check (darbari->name == "darbārī kānaḍā" && darbari->thaat == "āsāvarī" && near (darbari->cents[3], phi::Tuning::ratioToCents (32.0 / 27.0), 1.0e-9)
               && near (darbari->cents[7], phi::Tuning::ratioToCents (1.5), 1.0e-9) && ! darbari->uses[4], "raga sheets: svaras and their tuning");
        check (darbari->aroha.front() == phi::Svara { 10, -1 } && darbari->aroha.back() == phi::Svara { 0, 1 } && darbari->vadi == 2 && darbari->samvadi == 7,
               "raga sheets: aroha with octave marks, vadi and samvadi");
        const auto* swing = darbari->andolanOn (3);
        check (swing != nullptr && near (swing->depthCents, 20.0, 1.0e-9) && darbari->andolanOn (2) == nullptr, "raga sheets: andolan on ga");

        std::vector<std::string> again;
        const auto back = phi::parseRagas (phi::writeRagas (all), again);
        bool same = again.empty() && back.size() == all.size();
        for (size_t i = 0; same && i < all.size(); ++i)
            same = back[i].name == all[i].name && back[i].aroha == all[i].aroha && back[i].avaroha == all[i].avaroha && back[i].pakad == all[i].pakad
                   && back[i].uses == all[i].uses && std::abs (back[i].cents[3] - all[i].cents[3]) < 1.0e-6 && back[i].andolan.size() == all[i].andolan.size()
                   && back[i].murki == all[i].murki && back[i].khatka == all[i].khatka && back[i].gamak.size() == all[i].gamak.size();
        check (same, "raga sheets: written out and read back the same");

        // A dot below or above in the notation is the octave, as with , and '.
        phi::Phrase dots;
        check (phi::parsePhrase ("n\xcc\xa3 S R S\xcc\x87", dots) && dots == phi::Phrase { { 10, -1 }, { 0, 0 }, { 2, 0 }, { 0, 1 } }, "raga sheets: dotted notation");
        // The player: khamāj's one key plays N going up and n coming down.
        auto byPlain = [&] (const char* plain) -> const phi::Raga& { for (const auto& r : all) if (r.plain == plain) return r; return all[0]; };
        phi::RagaPlayer player;
        player.setRaga (&byPlain ("khamaj"), 60);
        player.noteOn (67);
        const auto upN = player.noteOn (70);
        player.noteOn (72);
        const auto downN = player.noteOn (70);
        check (upN.outKey == 71 && upN.svara.semitone == 11 && downN.outKey == 70 && downN.svara.semitone == 10, "raga player: the svara for the direction");
        // Bhūpālī has no ma: going up, ma becomes ga (the nearer svara, there being no tīvra ma).
        player.setRaga (&byPlain ("bhupali"), 60);
        player.reset();
        check (player.noteOn (65).outKey == 64 && player.noteOn (78).outKey == 79 && player.noteOn (77).svara == phi::Svara { 4, 1 }, "raga player: keys outside the raga go to its nearest svara");
        // Darbārī: ga at 32/27 above Sa, swinging 20 cents below and back at 0.5 Hz.
        player.setRaga (&byPlain ("darbari kanada"), 62, 293.6647679174076);
        player.reset();
        const auto ga = player.noteOn (65);
        check (near (ga.hz, 293.6647679174076 * 32.0 / 27.0, 1.0e-9) && near (player.hzAt (ga, 0.0), ga.hz, 1.0e-9)
               && near (1200.0 * std::log2 (ga.hz / player.hzAt (ga, 1.0)), 20.0, 1.0e-9) && near (player.hzAt (ga, 2.0), ga.hz, 1.0e-9),
               "raga player: tuned svaras, and andolan swings below and back");
        // In equal temperament the swing is centred: it starts on the svara and averages it.
        player.setCentredSwing (true);
        double meanCents = 0.0;
        for (int i = 0; i < 200; ++i)
            meanCents += 1200.0 * std::log2 (player.hzAt (ga, i / 100.0) / ga.hz) / 200.0;
        check (near (player.hzAt (ga, 0.0), ga.hz, 1.0e-9) && std::abs (meanCents) < 0.5 && near (1200.0 * std::log2 (ga.hz / player.hzAt (ga, 0.5)), 10.0, 1.0e-9),
               "raga player: a centred andolan swings either side of the svara");
        player.setCentredSwing (false);
        // Mīṇḍ in khamāj from G up to S': through m, P, D and N (the āroha's), 0.4 s, then there.
        player.setOrnaments (1.0);
        player.setRaga (&byPlain ("khamaj"), 60);
        player.reset();
        const auto from = player.noteOn (64);
        const auto pulled = player.glide (from, 72, 0.4);
        check (pulled.pathCount == 6 && near (pulled.path[0], from.cents, 1.0e-9) && near (pulled.path[4], 1200.0 * std::log2 (15.0 / 8.0), 1.0e-9)
               && near (pulled.approachSeconds(), 0.4, 1.0e-12) && near (player.hzAt (pulled, 0.0), from.hz, 1.0e-9)
               && near (player.hzAt (pulled, 0.5), pulled.hz, 1.0e-9), "raga player: mind touches the svaras between");
        bool rises = true;
        for (double t = 0.0, last = 0.0; t < 0.4; t += 0.01)
        {
            const auto hz = player.hzAt (pulled, t);
            rises = rises && hz >= last - 1.0e-9;
            last = hz;
        }
        check (rises, "raga player: an upward mind only rises");
        // Kaṇ: darbārī's ga touched from ma, then ga, then its āndolan.
        player.setRaga (&byPlain ("darbari kanada"), 62, 293.6647679174076);
        player.reset();
        auto kanGa = player.noteOn (65);
        player.addKan (kanGa);
        check (kanGa.pathCount == 2 && near (player.hzAt (kanGa, 0.0), 293.6647679174076 * 4.0 / 3.0, 1.0e-9)
               && near (player.hzAt (kanGa, 0.05), kanGa.hz, 1.0e-9) && near (1200.0 * std::log2 (kanGa.hz / player.hzAt (kanGa, 1.05)), 20.0, 1.0e-9),
               "raga player: kan from the sheet, then andolan");
        // Khamāj's murkī on ga: ga, ma, ga, re, ga, quickly; then ga as it is.
        player.setRaga (&byPlain ("khamaj"), 60);
        player.reset();
        auto murki = player.noteOn (64);
        player.ornament (murki);
        const auto cents = [&] (const phi::RagaPlayer::Voice& v, double t) { return 1200.0 * std::log2 (player.hzAt (v, t) / player.voiceSaHz()); };
        check (byPlain ("khamaj").ornamentOn (4) == phi::Ornament::murki && murki.pathCount == 5 && near (cents (murki, 0.03 * 1.4), 1200.0 * std::log2 (4.0 / 3.0), 1.0e-6)
               && near (cents (murki, 0.03 * 3.4), 1200.0 * std::log2 (9.0 / 8.0), 1.0e-6) && near (cents (murki, 0.2), murki.cents, 1.0e-9),
               "raga player: murki turns about the svara, then holds it");
        // Krintan, khaṭkā and zamzama from the neighbours, at half speed taking twice as long.
        auto krintan = player.noteOn (64), khatka = krintan, zamzama = krintan;
        player.ornament (krintan, phi::Ornament::krintan);
        player.ornament (khatka, phi::Ornament::khatka, 2.0);
        player.ornament (zamzama, phi::Ornament::zamzama);
        check (near (krintan.path[0], 700.0 + 1.955, 0.01) && krintan.pathCount == 3 && near (khatka.path[0], 1200.0 * std::log2 (4.0 / 3.0), 1.0e-9)
               && near (khatka.path[2], 1200.0 * std::log2 (9.0 / 8.0), 1.0e-9) && near (khatka.approachSeconds(), 0.27, 1.0e-12) && zamzama.pathCount == 6,
               "raga player: krintan, khatka and zamzama");
        // Mālkauns's gamak on komal dha: shaken up to komal ni (9/8 above) and back, five times a second.
        player.setRaga (&byPlain ("malkauns"), 60);
        player.reset();
        const auto dha = player.noteOn (68);
        check (near (dha.gamakHz, 5.0, 1.0e-12) && near (cents (dha, 0.1), dha.cents + dha.gamakCents, 1.0e-6) && near (cents (dha, 0.2), dha.cents, 1.0e-6)
               && near (dha.gamakCents, 1200.0 * std::log2 ((9.0 / 5.0) / (8.0 / 5.0)), 1.0e-6), "raga player: gamak to the svara above");
        player.setOrnaments (0.0);
        check (near (player.hzAt (ga, 1.0), ga.hz, 1.0e-12) && player.noteOn (76).swingCents == 0.0, "raga player: ornaments off, and no andolan on Pa");

        std::vector<std::string> mistakes;
        const auto wrong = phi::parseRagas ("raga: test\nplain: test\nsvaras: S R G P D\naroha: S R m P\nraga: fine\nplain: fine\nsvaras: S P\nkey: x\n", mistakes);
        check (wrong.empty() && mistakes.size() == 2 && mistakes[0].find ("line 4") == 0 && mistakes[1].find ("line 8") == 0,
               "raga sheets: a svara the raga lacks, or an unknown field, is reported by its line");
    }

    // PHI rāga's sitar: in tune, sympathetic strings that answer only their own pitch, and cheap.
    void testSitar()
    {
        constexpr double rate = 48000.0;
        auto render = [] (phi::Sitar& sitar, int frames)
        {
            std::vector<float> l ((size_t) frames, 0.0f), r ((size_t) frames, 0.0f);
            sitar.process (l.data(), r.data(), frames);
            for (size_t i = 0; i < l.size(); ++i)
                l[i] = 0.5f * (l[i] + r[i]);
            return l;
        };
        auto pitch = [] (const std::vector<float>& x, size_t from, size_t count, double lowHz, double highHz)
        {
            double best = -1.0e30;
            int bestLag = 0;
            for (int lag = (int) (rate / highHz); lag <= (int) (rate / lowHz); ++lag)
            {
                double sum = 0.0;
                for (size_t i = from; i < from + count; ++i)
                    sum += (double) x[i] * x[i + (size_t) lag];
                if (sum > best)
                    best = sum, bestLag = lag;
            }
            return rate / bestLag;
        };
        auto energy = [] (const std::vector<float>& x, size_t from, size_t to)
        {
            double e = 0.0;
            for (size_t i = from; i < to; ++i)
                e += (double) x[i] * x[i];
            return e;
        };
        {
            phi::Sitar sitar;
            sitar.prepare (rate);
            sitar.setJawari (0.0);
            sitar.pluck (220.0, 0.8);
            const auto open = render (sitar, (int) rate);
            const auto hz = pitch (open, 9600, 4800, 150.0, 300.0);
            check (std::abs (1200.0 * std::log2 (hz / 220.0)) < 15.0, "sitar: an open bridge plays in tune");
            bool finite = true;
            for (auto v : open)
                finite = finite && std::isfinite (v) && std::abs (v) < 4.0f;
            check (finite && energy (open, 0, 4800) > 10.0 * energy (open, 43200, 48000), "sitar: bounded, and it dies away");
        }
        {
            phi::Sitar sitar;
            sitar.prepare (rate);
            sitar.setJawari (0.7);
            const auto string = sitar.pluck (220.0, 0.8);
            auto buzz = render (sitar, (int) (0.5 * rate));
            check (std::abs (1200.0 * std::log2 (pitch (buzz, 9600, 4800, 150.0, 300.0) / 220.0)) < 40.0, "sitar: in tune with the jawari");
            // Pulled a whole tone (mīṇḍ), the string follows.
            sitar.setHz (string, 246.94);
            const auto pulled = render (sitar, (int) (0.5 * rate));
            check (std::abs (1200.0 * std::log2 (pitch (pulled, 4800, 4800, 150.0, 300.0) / 246.94)) < 40.0, "sitar: the string bends to a new pitch");
        }
        {
            // A tarab string at 220 Hz answers 220 Hz long after the played string is let go, and
            // barely answers 233 Hz.
            auto ring = [&] (double played)
            {
                phi::Sitar sitar;
                sitar.prepare (rate);
                sitar.setJawari (0.0);
                sitar.setTarab ({ 220.0 });
                const auto s = sitar.pluck (played, 0.9);
                render (sitar, (int) (0.6 * rate));
                sitar.release (s);
                const auto after = render (sitar, (int) (2.0 * rate));
                return energy (after, (size_t) (1.5 * rate), (size_t) (2.0 * rate));
            };
            const auto matched = ring (220.0), other = ring (233.08);
            check (matched > 4.0 * other && matched > 0.0, "sitar: tarab rings in sympathy with its own pitch");
        }
        {
            // PHI rāga on a track: its own raga, then the one the track's tuning puts in force.
            mixer::RagaInstrument raga;
            raga.setState ("raga=khamaj;sa=0;ornaments=1;jawari=0.6;tarab=0.6");
            raga.prepare (rate, 512);
            check (raga.playing() == "khamāj" && raga.state().find ("raga=khamaj;sa=0") == 0, "raga instrument: its own raga, saved in its state");
            std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
            const mixer::NoteEvent on[] = { { 0, 0x90, 67, 100 }, { 12000, 0x90, 70, 100 } };
            raga.process (on, 2, l.data(), r.data(), 24000);
            bool finite = true;
            for (auto v : l)
                finite = finite && std::isfinite (v);
            check (finite && energy (l, 12000, 24000) > 0.01, "raga instrument: plays the keys on the sitar");
            const auto& sheets = phi::builtInRagas();
            int darbari = -1;
            for (size_t i = 0; i < sheets.size(); ++i)
                if (sheets[i].plain == "darbari kanada")
                    darbari = (int) i;
            raga.setRaga (darbari, 2, true);
            check (raga.playing() == "darbārī kānaḍā (equal tempered)", "raga instrument: the track's raga in force");
            raga.setRaga (-1, 0, false);
            check (raga.playing() == "khamāj", "raga instrument: back to its own raga");
        }
        {
            // MPE: a bend on another note's channel leaves Pa alone; one on its own moves it.
            mixer::RagaInstrument raga;
            raga.setState ("raga=khamaj;sa=0;ornaments=0;jawari=0;tarab=0");
            raga.prepare (rate, 512);
            const auto upOne = 8192 + 8192 / 48;   // a semitone at MPE's ±48 on a member channel
            std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
            const mixer::NoteEvent first[] = { { 0, 0x92, 67, 100 }, { 0, 0xe1, (uint8_t) (upOne & 0x7f), (uint8_t) (upOne >> 7) } };
            raga.process (first, 2, l.data(), r.data(), 24000);
            const auto plain = pitch (l, 9600, 4800, 300.0, 500.0);
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            const mixer::NoteEvent bent[] = { { 0, 0xe2, (uint8_t) (upOne & 0x7f), (uint8_t) (upOne >> 7) } };
            raga.process (bent, 1, l.data(), r.data(), 24000);
            const auto moved = pitch (l, 9600, 4800, 300.0, 500.0);
            check (std::abs (1200.0 * std::log2 (plain / (261.6255653 * 1.5))) < 15.0 && std::abs (1200.0 * std::log2 (moved / plain) - 100.0) < 20.0,
                   "raga instrument: MPE bends each note on its own channel");
        }
        {
            // The tānpūrā: silent until on, then Pa below Sa first, the round's four plucks, and
            // the state keeps it.
            mixer::RagaInstrument raga;
            raga.setState ("raga=yaman (kalyan);sa=0;tarab=0;tanpura=1;tanpuraLevel=0.8;tanpuraSeconds=2");
            raga.prepare (rate, 512);
            std::vector<float> l ((size_t) (2.0 * rate), 0.0f), r = l;
            raga.process (nullptr, 0, l.data(), r.data(), (int) l.size());
            check (raga.state().find ("tanpura=1;tanpuraLevel=0.8;tanpuraSeconds=2") != std::string::npos
                   && std::abs (1200.0 * std::log2 (pitch (l, 2400, 9600, 100.0, 300.0) / (261.6255653 * 0.75))) < 40.0
                   && energy (l, (size_t) (0.4 * rate), (size_t) (0.45 * rate)) > 0.0 && energy (l, (size_t) (1.9 * rate), (size_t) (2.0 * rate)) > 0.0,
                   "raga instrument: the tanpura drone, Pa first");
            phi::Tanpura off;
            off.prepare (rate);
            std::vector<float> quiet (4800, 0.0f), quietR = quiet;
            off.process (quiet.data(), quietR.data(), 4800);
            check (energy (quiet, 0, 4800) == 0.0, "tanpura: silent and free when off");
        }
        {
            phi::Sitar sitar;
            sitar.prepare (rate);
            std::vector<double> tarab;
            for (int i = 0; i < 13; ++i)
                tarab.push_back (261.63 * std::pow (2.0, i / 12.0));
            sitar.setTarab (tarab);
            for (int i = 0; i < 4; ++i)
                sitar.pluck (220.0 * (i + 1), 0.8);
            const auto start = std::chrono::steady_clock::now();
            render (sitar, (int) (5.0 * rate));
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            check (seconds < 1.0, "sitar: 4 strings and 13 tarab render 5 s in under 1 s");
            std::printf ("  sitar: 5 s of 4 strings and 13 tarab in %.3f s\n", seconds);
        }
    }

    // Harmonize in the mode (owner, 2026-10-29): each mode's characteristic note, chords judged
    // in the cents the synth plays, each tradition's own texture, and the chords changing with
    // a rhythm's cycle; every melodic mode in every texture a valid example.
    void testModalHarmony()
    {
        using namespace modalharmony;
        const auto& all = mixer::tuningSystems();
        auto get = [&] (const char* plain) -> const mixer::TuningSystem& { return all[(size_t) std::max (0, mixer::findTuning (plain))]; };
        auto marked = [&] (const char* plain) { return characteristic (degrees (get (plain))); };
        check (marked ("dorian") == std::vector<int> { 9 } && marked ("mixolydian") == std::vector<int> { 10 }
               && marked ("lydian") == std::vector<int> { 6 } && marked ("phrygian") == std::vector<int> { 1 },
               "modal harmony: dorian's sixth, mixolydian's seventh, lydian's fourth, phrygian's second");

        // Dorian: the final's chord first, then chords with the major sixth, none a tritone,
        // the dominant a fifth above not the first choice.
        const auto d = degrees (get ("dorian"));
        const auto chords = progression (d, Texture::chords, 4);
        bool modal = chords.size() == 4 && chords[0].steps.front() == 0 && chords[1].characteristic;
        for (const auto& c : chords)
            modal = modal && c.roughness <= 45.0;
        modal = modal && chords[1].steps.front() != 4;
        check (modal, "modal harmony: dorian's progression from the final, by its characteristic note, not by V");

        // Rāst's neutral third is judged by its cents: rough, but a chord.
        const auto rast = degrees (get ("rast"));
        const bool neutral = std::any_of (rast.begin(), rast.end(), [] (const Degree& x) { return std::abs (x.cents - 350.0) < 10.0; });
        check (neutral && roughness (350.0) > 25.0 && roughness (350.0) < 45.0 && roughness (386.31) < 1.0e-6 && roughness (200.0) > 100.0
               && ! progression (rast, Texture::chords, 2).empty(),
               "modal harmony: a neutral third measured in its cents, near enough a just third to be a chord");

        check (traditional (get ("dorian")) == Texture::chords && traditional (get ("rast")) == Texture::heterophony
               && traditional (get ("pelog")) == Texture::layered && traditional (get ("echos protos")) == Texture::drone
               && traditional (all[(size_t) std::max (0, mixer::findTuning ("darbari kanada"))]) == Texture::drone,
               "modal harmony: each tradition's own texture");

        const auto tintal = rhythm::rhythms()[(size_t) std::max (0, rhythm::findRhythm ("tintal"))];
        const auto held = fromRhythm (tintal, false), struck = fromRhythm (tintal, true);
        check (held.beats == 16.0 && held.changes == std::vector<double> { 0.0, 4.0, 8.0, 12.0 } && held.strikes.size() == 4
               && struck.strikes.size() > 4 && steady (2.0, 4).beats == 8.0,
               "modal harmony: the chords change at tīntāl's vibhāgs, and are struck on its ṭhekā");

        bool valid = true;
        int tried = 0;
        for (const auto& t : all)
        {
            if ((t.kinds & mixer::melodic) == 0)
                continue;
            for (auto x : textures())
            {
                Settings s;
                s.texture = x;
                s.cycles = 2;
                const auto h = generate (t, ++tried % 2 == 0 ? held : steady (4.0, 4), s);
                if (! harmony::valid (h))
                    std::printf ("  invalid: %s in %s\n", t.plain.c_str(), textureName (x).c_str());
                valid = valid && harmony::valid (h);
            }
        }
        Settings s;
        s.cycles = 3;
        const auto song = harmony::toSong (generate (get ("dorian"), struck, s), 50, 1, 80.0);
        check (valid && tried > 1000 && song->lastTick == 3 * 16 * 480 && song->noteCount() > 0,
               "modal harmony: every melodic mode in every texture, and the cycles written out");
    }

    // The Mode menu's harmony (owner, 2026-10-29): every example playable, its tuning known, each
    // part's groups in order, jazz its own part, and the example written as a MIDI file.
    void testHarmonies()
    {
        const auto& all = harmony::harmonies();
        bool ok = all.size() >= 60, tunings = true, lower = true;
        std::set<std::string> plains;
        for (const auto& h : all)
        {
            if (! harmony::valid (h))
                std::printf ("  invalid harmony example: %s\n", h.name.c_str());
            ok = ok && harmony::valid (h) && plains.insert (h.plain).second && ! h.about.empty() && ! h.source.empty() && ! h.tradition.empty();
            tunings = tunings && (h.tuning.empty() || mixer::findTuning (h.tuning) >= 0);
            lower = lower && std::none_of (h.name.begin(), h.name.end(), [] (char c) { return c >= 'A' && c <= 'Z'; });
        }
        check (ok && tunings && lower, "harmony: every example valid and named once in lower case, its tuning known");

        size_t listed = 0;
        for (auto p : { harmony::Part::western, harmony::Part::jazz, harmony::Part::world, harmony::Part::meeting })
            for (const auto& g : harmony::groups (p))
                listed += harmony::inGroup (p, g).size();
        const auto western = harmony::groups (harmony::Part::western);
        check (listed == all.size() && harmony::groups (harmony::Part::jazz).size() == 1 && ! western.empty() && western.front() == "middle ages"
               && harmony::groupTitle (harmony::Part::world, "west asia") == "West Asia" && harmony::findHarmony ("georgian polyphony") >= 0
               && harmony::findHarmony ("sacred harp") >= 0,
               "harmony: every one in a group, jazz its own part, the eras in order");

        const auto& organum = all[(size_t) harmony::findHarmony ("parallel organum")];
        const auto song = harmony::toSong (organum, 50, 2, 60.0);
        int ons = 0;
        for (const auto& e : song->events)
            ons += e.kind() == 0x90 ? 1 : 0;
        std::string why;
        const auto bytes = midifile::bytes (*song);
        auto back = midifile::parse (bytes.data(), bytes.size(), why);
        check (ons == 2 * 14 && song->lastTick == 2 * 9 * 480 && back != nullptr && back->noteCount() == ons,
               "harmony: the example as a MIDI file, a channel a voice, repeated");
    }

    // The atlas (owner, 2026-10-27): the research's systems, each placed under its region
    // and its people or culture, non-octave scales, and the built-in Synth playing a tuning.
    void testModeAtlas()
    {
        const auto& all = mixer::tuningSystems();
        auto get = [&] (const std::string& plain) -> const mixer::TuningSystem& { return all[(size_t) std::max (0, mixer::findTuning (plain))]; };
        bool known = true;
        for (const char* plain : { "greek dorian", "enharmonic (archytas)", "intense diatonic (ptolemy)", "enharmonic (aristoxenus)", "pythagorean",
                                   "echos protos", "echos barys", "obikhod scale", "hexachord", "locrian", "werckmeister iii", "kirnberger iii",
                                   "zarlino meantone", "hungarian minor", "scala enigmatica", "prometheus (mystic chord)", "gaelic mode 3 (pentatonic)",
                                   "gaelic mode 4b", "ahavah rabbah", "partch 43", "well-tuned piano", "harp of new albion", "bohlen-pierce equal",
                                   "carlos alpha", "safi al-din 17", "arel-ezgi-uzdilek 24", "nikriz", "ussak", "hicaz", "rast (mugam)", "dashti",
                                   "melakarta 29 (dhirasankarabharanam)", "22 shruti", "shadja-grama", "patutan baro", "thai mode 1 (morton)",
                                   "dieu bac", "kumoijoshi", "tezeta major", "anchihoye", "96 equal steps", "53 equal steps" })
        {
            const bool found = mixer::findTuning (plain) >= 0;
            if (! found)
                std::printf ("  missing: %s\n", plain);
            known = known && found;
        }
        check (known, "mode atlas: the research's systems, by their plain names");

        // The menu capitalizes people, nations and empires (owner, 2026-10-27); the modes stay lower case.
        bool shown = true;
        for (const auto& o : mixer::origins())
            shown = shown && ! o.shown.empty() && o.shown.size() == o.name.size();
        const auto* safi = mixer::findOrigin ("ṣafī al-dīn al-urmawī");
        check (shown && safi != nullptr && safi->shown == "Ṣafī al-Dīn al-Urmawī" && mixer::regionTitle ("west asia") == "West Asia"
               && get ("partch 43").name == "partch 43-tone just intonation",
               "mode atlas: every origin has its capitalized name, and the regions theirs");

        // Every system shown is placed: it names a known origin (the equal steps have their own
        // submenu), and every origin is in a region and has something in it.
        bool placed = true, originsKnown = true;
        std::set<std::string> used;
        for (const auto& t : all)
        {
            const bool twin = t.family.find ("equal tempered") != std::string::npos;
            placed = placed && (twin ? t.kinds == 0 : t.kinds != 0) && (twin || t.family == "equal" || ! t.from.empty());
            for (const auto& o : t.from)
            {
                used.insert (o);
                if (mixer::findOrigin (o) == nullptr)
                {
                    std::printf ("  unknown origin: %s (%s)\n", o.c_str(), t.plain.c_str());
                    originsKnown = false;
                }
            }
        }
        bool regionsKnown = true, nonEmpty = true;
        for (const auto& o : mixer::origins())
        {
            regionsKnown = regionsKnown && std::find (mixer::regions().begin(), mixer::regions().end(), o.region) != mixer::regions().end();
            if (used.count (o.name) == 0)
            {
                std::printf ("  empty origin: %s\n", o.name.c_str());
                nonEmpty = false;
            }
            for (auto c : o.name)
                regionsKnown = regionsKnown && ! (c >= 'A' && c <= 'Z');
        }
        check (placed && originsKnown && regionsKnown && nonEmpty, "mode atlas: every system placed under a known people or culture, in a region");
        check (! mixer::systemsFrom ("gioseffo zarlino", mixer::melodic).empty() && ! mixer::systemsFrom ("gioseffo zarlino", mixer::tuning).empty()
               && mixer::systemsFrom ("harry partch", mixer::melodic).empty() && mixer::systemsFrom ("venkaṭamakhin", mixer::melodic).size() == 72
               && ! mixer::systemsFrom ("percy grainger", mixer::melodic).empty(), "mode atlas: melodic and tuning kinds, by person");
        const auto* first = &mixer::origins().front();
        check (first->region == "europe" && ! first->person, "mode atlas: origins by region, traditions before people");

        // The melakartas: 29 is the major scale, 65 the Lydian, 15 māyāmāḷavagauḷa (bhairav's notes).
        check (mixer::modeKeys (get ("melakarta 29 (dhirasankarabharanam)")) == 0b101010110101 && mixer::modeKeys (get ("melakarta 65 (mecakalyani)")) == 0b101011010101
               && mixer::modeKeys (get ("melakarta 15 (mayamalavagaula)")) == 0b100110110011 && get ("melakarta 1 (kanakangi)").name == "kanakāṅgi (1)",
               "mode atlas: the 72 melakartas");
        // Greek: the Dorian species is E to E (a limma first); Archytas's enharmonic starts 28/27;
        // Aristoxenus's enharmonic diesis is a quarter tone (50 cents).
        check (near (get ("greek dorian").cents[1], phi::Tuning::ratioToCents (256.0 / 243), 1.0e-9) && near (get ("enharmonic (archytas)").cents[1], phi::Tuning::ratioToCents (28.0 / 27), 1.0e-9)
               && near (get ("enharmonic (archytas)").cents[4], 701.955, 1.0e-3) && near (get ("enharmonic (aristoxenus)").cents[1], 50.0, 1.0e-9)
               && near (get ("enharmonic (aristoxenus)").cents[4], 700.0, 1.0e-9), "mode atlas: Greek species and genera");
        check (get ("partch 43").cents.size() == 43 && get ("22 shruti").cents.size() == 22 && get ("safi al-din 17").cents.size() == 17
               && get ("arel-ezgi-uzdilek 24").cents.size() == 24 && near (get ("safi al-din 17").cents[1], phi::Tuning::ratioToCents (256.0 / 243), 1.0e-6),
               "mode atlas: Partch's 43, the 22 śrutis, Ṣafī al-Dīn's 17, the 24 of Arel-Ezgi-Uzdilek");
        // Twelve-note tunings play a ratio a key, in their order: the Well-Tuned Piano's G (key 4
        // above E flat) is 21/16, though its G sharp (1323/1024) is lower.
        const auto wtp = mixer::keyCents (get ("well-tuned piano"));
        check (near (wtp[4], phi::Tuning::ratioToCents (21.0 / 16), 1.0e-9) && near (wtp[5], phi::Tuning::ratioToCents (1323.0 / 1024), 1.0e-9)
               && near (mixer::keyCents (get ("werckmeister iii"))[7], 696.090, 1.0e-9), "mode atlas: a twelve-note tuning a ratio a key");
        // Hajibeyov's rast repeats at the fourth: in the octave, C D E F G A B♭, on the keys.
        const auto& rast = get ("rast (mugam)");
        const auto oct = mixer::octaveCents (rast);
        check (near (rast.period, 500.0, 1.0e-9) && oct == std::vector<double> ({ 0, 200, 400, 500, 700, 900, 1000 })
               && near (mixer::makeTuning (rast, 0).hz (70), 440.0 * std::pow (2.0, 1.0 / 12.0), 1.0e-6), "mode atlas: a mode repeating at the fourth, on the keys");
        // Bohlen-Pierce: a degree a key, thirteen keys to the tritave; Carlos alpha a step a key.
        const auto bp = mixer::makeTuning (get ("bohlen-pierce equal"), 0);
        const auto alpha = mixer::makeTuning (get ("carlos alpha"), 0);
        check (near (bp.hz (73) / bp.hz (60), 3.0, 1.0e-9) && near (1200.0 * std::log2 (alpha.hz (61) / alpha.hz (60)), 77.965, 1.0e-6),
               "mode atlas: non-octave tunings, a degree a key");
        check (near (1200.0 * std::log2 (mixer::makeTuning (get ("96 equal steps"), 0).hz (61) / mixer::makeTuning (get ("96 equal steps"), 0).hz (60)), 12.5, 1.0e-6),
               "mode atlas: Carrillo's 96 equal steps");
        // Turkish makams in commas: uşşak's second is 8 commas.
        check (near (get ("ussak").cents[1], 8 * 1200.0 / 53, 1.0e-9) && get ("ussak").name == "uşşak" && get ("echos protos").name == "ēchos prōtos (first)",
               "mode atlas: makams in 53 commas, names transliterated");

        // The built-in Synth plays the tuning, as PHI Synth will: key 61 in 24 equal steps from
        // middle C is a quarter tone above it.
        mixer::Synth synth;
        synth.prepare (48000.0, 256);
        auto s = synth.settings();
        s.wave = mixer::SynthSettings::sine;
        s.cutoff = 20000.0;
        synth.setSettings (s);
        synth.setTuning (std::make_shared<const phi::Tuning> (phi::Tuning::equal (24, 60, 261.6255653005986)));
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        synth.process (nullptr, 0, l.data(), r.data(), 64);
        const mixer::NoteEvent on { 0, 0x90, 61, 100 };
        std::fill (l.begin(), l.end(), 0.0f);
        synth.process (&on, 1, l.data(), r.data(), 48000);
        int crossings = 0;
        for (size_t i = 4800; i + 1 < l.size(); ++i)
            crossings += (l[i] <= 0.0f && l[i + 1] > 0.0f) ? 1 : 0;
        const auto hz = crossings / (43200.0 / 48000.0);
        check (std::abs (hz - 261.6255653 * std::pow (2.0, 1.0 / 24.0)) < 2.0, "mode atlas: the built-in Synth plays the tuning");
        synth.setTuning (nullptr);
    }

    // Modal tunings for the PHI synth: the systems, the keys they put their degrees on, and which
    // level's choice is in force.
    void testTunings()
    {
        const auto& all = mixer::tuningSystems();
        auto get = [&] (const std::string& plain) -> const mixer::TuningSystem& { return all[(size_t) mixer::findTuning (plain)]; };
        bool known = true;
        for (const char* plain : { "bilaval", "darbari kanada", "dorian", "hypodorian", "altered", "rast", "hijaz", "shur", "chahargah", "slendro", "pelog", "selisir", "salendro", "nyamaropa", "nyunga nyunga", "matepe (saini madera)", "2 equal steps", "21 equal steps" })
            known = known && mixer::findTuning (plain) >= 0;
        check (known && mixer::findTuning ("no such tuning") == -1, "tunings: each family is listed, found by its plain name");
        check (get ("darbari kanada").name == "darbārī kānaḍā" && get ("hijaz").name == "ḥijāz" && get ("chahargah").name == "čahārgāh", "tunings: transliterated names");
        bool plainAscii = true, unique = true;
        for (size_t i = 0; i < all.size(); ++i)
        {
            for (auto c : all[i].plain)
                plainAscii = plainAscii && (unsigned char) c < 128;
            unique = unique && mixer::findTuning (all[i].plain) == (int) i;
        }
        check (plainAscii && unique, "tunings: the saved names are ASCII and unique");
        bool lower = true;
        for (const auto& t : all)
            for (auto c : t.name + t.plain)
                lower = lower && ! (c >= 'A' && c <= 'Z');
        check (lower && all[(size_t) mixer::findTuning ("yaman (kalyan)")].name == "yaman (kalyāṇ)", "tunings: names in lower case");
        // Each raga has a 12-TET version: Pa at 700 cents, the svaras on the keys.
        const auto et = mixer::keyCents (get ("darbari kanada (equal tempered)"));
        check (near (et[7], 700.0, 1.0e-9) && near (et[3], 300.0, 1.0e-9) && get ("darbari kanada (equal tempered)").cents.size() == get ("darbari kanada").cents.size(),
               "tunings: ragas in equal temperament too");

        // Rāst's E half-flat (350 cents) plays on E; the other keys stay in 12-TET.
        const auto rast = mixer::keyCents (get ("rast"));
        check (near (rast[4], 350.0, 1.0e-9) && near (rast[3], 300.0, 1.0e-9) && near (rast[11], 1050.0, 1.0e-9) && near (rast[10], 1000.0, 1.0e-9), "tunings: a quarter step on its key");
        // Bilāval in just intonation: Pa at 3/2, Ga at 5/4; Darbārī's ati-komal ga at 32/27.
        const auto bilaval = mixer::keyCents (get ("bilaval"));
        check (near (bilaval[7], 701.955, 1.0e-3) && near (bilaval[4], phi::Tuning::ratioToCents (1.25), 1.0e-9), "tunings: ragas in just intonation");
        check (near (mixer::keyCents (get ("darbari kanada"))[3], phi::Tuning::ratioToCents (32.0 / 27.0), 1.0e-9), "tunings: a raga's own komal svara");
        // Hypo ranges: for ragas, maqams and dastgahs with that degree; not the church modes (they
        // have their hypo-modes), jazz modes or equal steps.
        using mixer::Hypo;
        check (mixer::hasHypo (get ("bilaval"), Hypo::fifth) && mixer::hasHypo (get ("bilaval"), Hypo::fourth)
               && mixer::hasHypo (get ("rast"), Hypo::fifth) && mixer::hasHypo (get ("shur"), Hypo::fourth)
               && mixer::hasHypo (get ("bilaval (equal tempered)"), Hypo::fifth), "tunings: hypo for a tonic with its fifth and fourth");
        check (! mixer::hasHypo (get ("dorian"), Hypo::fifth) && ! mixer::hasHypo (get ("hypodorian"), Hypo::fifth) && ! mixer::hasHypo (get ("altered"), Hypo::fourth)
               && ! mixer::hasHypo (get ("12 equal steps"), Hypo::fifth) && ! mixer::hasHypo (get ("huzam"), Hypo::fifth),
               "tunings: no hypo for the church modes, jazz modes, equal steps, or a mode without the degree");
        check (mixer::hypoBelow (Hypo::fifth) == 5 && mixer::hypoBelow (Hypo::fourth) == 7 && mixer::hypoBelow (Hypo::none) == 0, "tunings: a hypo range's bottom");
        // The Mode menu's keys: dorian's seven, and rāst's half-flat third and seventh on E and B.
        check (mixer::modeKeys (get ("dorian")) == 0b011010101101 && mixer::modeKeys (get ("rast")) == 0b101010110101,
               "tunings: the keys a mode has a degree on");
        // Hypodorian on A has A's (Aeolian) Pythagorean steps: a 9/8 then a 256/243 limma.
        const auto hypo = mixer::keyCents (get ("hypodorian"));
        check (near (hypo[2], phi::Tuning::ratioToCents (9.0 / 8.0), 1.0e-9) && near (hypo[3] - hypo[2], phi::Tuning::ratioToCents (256.0 / 243.0), 1.0e-9),
               "tunings: hypo-modes in Pythagorean tuning");

        // On D, Rāst's E half-flat is key 66 (F#): 350 cents above D4.
        const auto onD = mixer::makeTuning (get ("rast"), 2);
        check (near (onD.hz (62), 440.0 * std::pow (2.0, -7.0 / 12.0), 1.0e-9) && near (1200.0 * std::log2 (onD.hz (66) / onD.hz (62)), 350.0, 1.0e-6)
               && near (onD.hz (74), 2.0 * onD.hz (62), 1.0e-9), "tunings: a maqam on its root, repeating each octave");
        // Gamelan: sléndro's five near-equal steps on five keys, pélog's seven uneven ones, and a
        // pathet read from its gong tone. The snap-to-scale list has them on the same keys.
        const auto slendro = mixer::keyCents (get ("slendro"));
        check (get ("slendro").name == "sléndro" && get ("pelog pathet barang").name == "pélog pathet barang" && get ("slendro").cents.size() == 5
               && near (slendro[2], 231.0, 1.0e-9) && near (slendro[10], 955.0, 1.0e-9) && near (slendro[1], 100.0, 1.0e-9), "tunings: sléndro in its own cents");
        check (get ("pelog").cents.size() == 7 && near (mixer::keyCents (get ("pelog"))[5], 539.0, 1.0e-9)
               && near (get ("slendro pathet sanga").cents[1], 955.0 - 717.0, 1.0e-9) && near (get ("pelog pathet nem").cents[1], 1200.0 - 785.0, 1.0e-9),
               "tunings: pélog, and pathet from their gong tone");
        check (get ("selisir").family == "balinese gamelan" && get ("degung").family == "sundanese gamelan" && get ("patutan tembung").cents.size() == 5,
               "tunings: Balinese and Sundanese gamelan");
        {
            const auto& snap = midifile::scales();
            bool same = true;
            for (auto [scale, plain] : { std::pair { "sléndro", "slendro pathet manyura" }, { "pélog", "pelog" }, { "pélog pathet lima", "pelog pathet lima" },
                                         { "pélog pathet nem", "pelog pathet nem" }, { "pélog pathet barang", "pelog pathet barang" },
                                         { "selisir", "selisir" }, { "degung", "degung" } })
            {
                uint16_t bits = 0;
                for (auto c : get (plain).cents)
                    bits = (uint16_t) (bits | (1u << (((int) std::floor (c / 100.0 + 0.5)) % 12)));
                bool found = false;
                for (const auto& sc : snap)
                    found = found || (std::string (sc.name) == scale && sc.notes == bits);
                same = same && found;
            }
            check (same, "tunings: gamelan in the snap-to-scale list on their keys");
        }
        // Shona mbira: nyamaropa's measured gamut, the other tunings its rotations, Madera's
        // matepe from Tracey's frequencies; none of them 12-TET.
        const auto nyamaropa = mixer::keyCents (get ("nyamaropa"));
        check (near (nyamaropa[2], 177.0, 1.0e-9) && near (nyamaropa[10], 1015.0, 1.0e-9) && near (nyamaropa[11], 1100.0, 1.0e-9),
               "tunings: nyamaropa's measured cents, Mixolydian's keys");
        const auto& gamut = get ("nyamaropa").cents;
        bool rotations = true;
        for (auto [plain, start] : { std::pair { "dambatsoko", 3 }, { "katsanzaira", 4 }, { "mavembe (gandanga)", 5 }, { "dindinyongwe", 2 } })
            for (size_t i = 0; i < 7; ++i)
                rotations = rotations && near (get (plain).cents[i], std::fmod (gamut[(start + i) % 7] - gamut[(size_t) start] + 1200.0, 1200.0), 1.0e-9);
        check (rotations && near (mixer::keyCents (get ("mavembe (gandanga)"))[1], 135.0, 1.0e-9), "tunings: the mbira tunings are one gamut from other keys");
        check (get ("nyunga nyunga").cents.size() == 6 && near (mixer::keyCents (get ("saungweme"))[3], 1200.0 * 2 / 7, 1.0e-9)
               && near (mixer::keyCents (get ("matepe (saini madera)"))[2], phi::Tuning::ratioToCents (240.0 / 216.0), 1.0e-9),
               "tunings: nyunga nyunga's six tones, saungweme's equal sevenths, Madera's matepe");
        const auto five = mixer::makeTuning (get ("5 equal steps"), 0);
        check (near (five.hz (65), 2.0 * five.hz (60), 1.0e-9) && near (five.hz (60), 261.6255653005986, 1.0e-9), "tunings: equal steps from the root");

        // Chinese and Japanese systems. The twelve lü by sanfen sunyi: linzhong a pure fifth,
        // zhonglü (the last) a Pythagorean comma above 4/3, on F's key.
        bool eastAsian = true;
        for (const char* plain : { "shier lu (sanfen sunyi)", "jing fang 60 lu", "cai yuanding 18 lu", "he chengtian xinlu", "zhu zaiyu xinfa milu",
                                   "chunlu wusheng (qin)", "gong", "yu", "yayue shang", "yanyue zhi", "gong (equal tempered)", "yifan (guangdong)",
                                   "zhongsan zhongliu (chaozhou)", "ichikotsucho", "banshikicho", "miyako-bushi (in)", "ryukyu", "hirajoshi" })
            eastAsian = eastAsian && mixer::findTuning (plain) >= 0;
        check (eastAsian && get ("jue").name == "jué" && get ("hyojo").name == "hyōjō", "tunings: Chinese and Japanese systems, by their plain names");
        const auto lu = mixer::keyCents (get ("shier lu (sanfen sunyi)"));
        check (near (lu[7], 701.955, 1.0e-3) && near (lu[5], phi::Tuning::ratioToCents (177147.0 / 131072.0), 1.0e-6), "tunings: the twelve lü by sanfen sunyi");
        // The five modes: gong's jue a Pythagorean third (81/64); yu (on la) has a minor third
        // (32/27) and no second; in 12-TET, yu is 0 3 5 7 10.
        check (near (mixer::keyCents (get ("gong"))[4], phi::Tuning::ratioToCents (81.0 / 64.0), 1.0e-9)
               && get ("yu").cents.size() == 5 && near (get ("yu").cents[1], phi::Tuning::ratioToCents (32.0 / 27.0), 1.0e-9)
               && get ("yu (equal tempered)").cents == std::vector<double> ({ 0, 300, 500, 700, 1000 }), "tunings: the five Chinese modes");
        // Yanyue on gong has qingjue (4/3) and run (16/9); yayue has bianzhi (729/512).
        const auto yanyue = get ("yanyue gong").cents, yayue = get ("yayue gong").cents;
        check (yanyue.size() == 7 && near (yanyue[3], phi::Tuning::ratioToCents (4.0 / 3.0), 1.0e-9) && near (yanyue[6], phi::Tuning::ratioToCents (16.0 / 9.0), 1.0e-9)
               && near (yayue[3], phi::Tuning::ratioToCents (729.0 / 512.0), 1.0e-9), "tunings: the seven-note Chinese scales");
        // He Chengtian's lü close the circle: each within 8 cents of 12-TET (sanfen sunyi's zhonglü
        // is 21.5 cents off).
        bool closes = get ("he chengtian xinlu").cents.size() == 12;
        for (size_t i = 0; closes && i < 12; ++i)
            closes = std::abs (get ("he chengtian xinlu").cents[i] - 100.0 * (double) i) < 8.0;
        check (closes, "tunings: He Chengtian's lü near equal");
        // Jing Fang's 60 lü play a degree a key: seyu, the 54th fifth, is 3.6 cents above huangzhong.
        const auto jing = mixer::makeTuning (get ("jing fang 60 lu"), 0);
        check (near (jing.hz (120), 2.0 * jing.hz (60), 1.0e-9) && near (1200.0 * std::log2 (jing.hz (61) / jing.hz (60)), 3.615, 1.0e-3),
               "tunings: Jing Fang's 60 lü, a degree a key");
        check (counterpoint::Mode::fromTuning (get ("jing fang 60 lu"), 0).valid() && counterpoint::Mode::fromTuning (get ("jing fang 60 lu"), 0).period == 60,
               "tunings: a long scale's degrees as counterpoint's keys");
        // Gagaku: hyōjō is ritsu (Dorian), sōjō ryo (Lydian), both Pythagorean.
        check (near (mixer::keyCents (get ("hyojo"))[3], phi::Tuning::ratioToCents (32.0 / 27.0), 1.0e-9)
               && near (mixer::keyCents (get ("sojo"))[6], phi::Tuning::ratioToCents (729.0 / 512.0), 1.0e-9), "tunings: ritsu and ryo");

        // Korean: p'yŏngjo on sol has a Pythagorean whole tone and fourth; kyemyŏnjo on la a
        // minor third of 32/27; the tori in 12-TET.
        check (get ("pyeongjo").name == "p'yŏngjo" && near (get ("pyeongjo").cents[2], phi::Tuning::ratioToCents (4.0 / 3.0), 1.0e-9)
               && near (get ("gyemyeonjo").cents[1], phi::Tuning::ratioToCents (32.0 / 27.0), 1.0e-9)
               && get ("menaritori").cents == std::vector<double> ({ 0, 300, 500, 800, 1000 }) && mixer::findTuning ("sibi-yul (sambun sonik)") >= 0,
               "tunings: Korean modes and tori");

        const mixer::TuningChoice none, clip { 3, 0 }, scene { 5, 2 }, track { 7, 4 };
        check (mixer::tuningInForce (clip, scene, track) == clip && mixer::tuningInForce (none, scene, track) == scene
               && mixer::tuningInForce (none, none, track) == track && mixer::tuningInForce (none, none, none).system == -1,
               "tunings: the clip's, else the scene's, else the track's");
    }

    // Minimalize: Reich's and Glass's processes on a scene of a melody, a chord pad and drums,
    // within eight tracks, each section as long as its longest gradual process.
    void testMinimalize()
    {
        using namespace minimalize;
        constexpr int64_t q = ticksPerQuarter, s16 = q / 4, bar = q * 4;
        // Piano Phase's twelve-note figure, in sixteenths (one bar of 3/4 within a 4/4 loop of 12 sixteenths).
        const int phase[] { 64, 66, 71, 73, 74, 66, 64, 73, 71, 66, 74, 73 };
        Part melody { { Cell { {}, 12 * s16 } }, "melody", false };
        for (int i = 0; i < 12; ++i)
            melody.cells[0].notes.push_back ({ i * s16, (i + 1) * s16, phase[i], 90, 0 });
        Part pad { { Cell { {}, bar } }, "pad", false };
        for (const int p : { 52, 59, 64 })
            pad.cells[0].notes.push_back ({ 0, bar, p, 70, 1 });
        Part drums { { Cell { {}, bar } }, "drums", true };
        for (int i = 0; i < 8; ++i)
            drums.cells[0].notes.push_back ({ i * q / 2, i * q / 2 + 60, i % 2 ? 42 : 36, 100, 9 });

        Settings reich;
        reich.style = Style::reich;
        reich.freeTracks = 8 - 3;   // a mix of 8 tracks, three in use
        auto r = arrange ({ melody, pad, drums }, reich);
        check (r.tracks.size() <= 8 && r.tracks.size() >= 5, "minimalize: the parts and new voices within eight tracks");
        check (r.tracks[0].process == Process::hold && r.tracks[3].process == Process::phase && r.tracks[3].added,
               "minimalize: Reich's phasing, the pattern held and a copy that moves");
        // The phase copy goes round all twelve steps back to unison: thirteen stages of four.
        check (r.length == (13 * 4 * 12 * s16 + bar - 1) / bar * bar, "minimalize: the section as long as the phase going round");
        bool inUnisonAtEnd = false, shiftedOnce = false;
        {
            const auto& held = r.tracks[0].notes;
            const auto& moving = r.tracks[3].notes;
            const auto lastStage = 12 * 4 * 12 * s16;
            std::set<int64_t> a, b, firstShift;
            for (const auto& n : held)
                if (n.start >= lastStage && n.start < lastStage + 12 * s16)
                    a.insert (n.start * 128 + n.pitch);
            for (const auto& n : moving)
            {
                if (n.start >= lastStage && n.start < lastStage + 12 * s16)
                    b.insert (n.start * 128 + n.pitch);
                if (n.start == 4 * 12 * s16 && n.pitch == phase[1])
                    shiftedOnce = true;   // the second stage starts on the figure's second note
            }
            inUnisonAtEnd = ! a.empty() && a == b;
        }
        check (shiftedOnce && inUnisonAtEnd, "minimalize: a sixteenth ahead each stage, and in unison again at the end");
        check (std::all_of (r.tracks.begin(), r.tracks.end(), [&] (const Track& t)
               { return std::all_of (t.notes.begin(), t.notes.end(), [&] (const Note& n) { return n.start >= 0 && n.end <= r.length && n.end > n.start; }); }),
               "minimalize: every note within the arrangement");

        Settings glass;
        glass.style = Style::glass;
        glass.repeats = 2;
        auto g = arrange ({ melody, pad }, glass);
        check (g.tracks[0].process == Process::additive && g.tracks[1].process == Process::arpeggio, "minimalize: Glass's additive process and arpeggios");
        // Additive: one note twice, then two notes twice...
        const auto& add = g.tracks[0].notes;
        check (add.size() >= 6 && add[0].pitch == phase[0] && add[1].pitch == phase[0] && add[1].start == s16
               && add[2].pitch == phase[0] && add[3].pitch == phase[1] && add[2].start == 2 * s16,
               "minimalize: the additive figure grows a note at a time");
        bool inChord = ! g.tracks[1].notes.empty();
        for (const auto& n : g.tracks[1].notes)
            inChord = inChord && (n.pitch % 12 == 4 || n.pitch % 12 == 11 || n.pitch % 12 == 6 || n.pitch % 12 == 1 || n.pitch % 12 == 2);
        check (inChord, "minimalize: the arpeggios keep to the scene's harmony");

        // A timeline cut into sections: two cells, two sections, one after the other.
        Notes line;
        for (int i = 0; i < 8; ++i)
            line.push_back ({ i * q, i * q + q / 2, 60 + i, 80, 0 });
        Part two { { cut (line, 0, bar), cut (line, bar, 2 * bar) }, "line", false };
        Settings build;
        build.processes = { Process::buildUp };
        build.newVoices = false;
        auto b = arrange ({ two }, build);
        check (b.sections.size() == 2 && b.sections[1] == 4 * 4 * bar && b.length == 2 * 4 * 4 * bar,
               "minimalize: a section a cell, each as long as its build-up");
        check (b.tracks.size() == 1 && b.tracks[0].notes.front().pitch == 60 && b.tracks[0].notes.back().pitch >= 64,
               "minimalize: the build-up starts on the downbeat, and the second section is the second cell");

        // One part phasing, or in canon, with new voices: the voice list grows from one while the
        // part's own entry is changed to hold (owner's crash, 2026-10-29: a reference kept across
        // the push_back wrote into freed memory; AddressSanitizer builds catch it here).
        for (const auto process : { Process::phase, Process::canon })
        {
            Settings one;
            one.processes = { process };
            const auto x = arrange ({ melody }, one);
            const auto copies = process == Process::phase ? 1u : 2u;
            check (x.tracks.size() == 1 + copies + 1 && x.tracks[0].process == Process::hold && ! x.tracks[0].added
                   && x.tracks[1].process == process && x.tracks[1].added && x.tracks.back().process == Process::pulse,
                   std::string ("minimalize: a single part's ") + name (process) + ", held, with its copies and a pulse on new tracks");
        }
    }

    // Species counterpoint: Fux's rules kept against his own cantus firmus, in every species,
    // in two to four parts, and in the modal systems beyond the church modes.
    void testCounterpoint()
    {
        using namespace counterpoint;
        // Fux's Dorian cantus firmus (Gradus ad Parnassum), a whole note a bar.
        const int fux[] { 62, 65, 64, 62, 67, 65, 69, 67, 65, 64, 62 };
        Line cf;
        for (int i = 0; i < 11; ++i)
            cf.push_back ({ i * 1920, (i + 1) * 1920, fux[i], 80 });
        const auto dorian = Mode::fromTuning (mixer::tuningSystems()[(size_t) mixer::findTuning ("dorian")], 2);
        check (dorian.valid() && dorian.ficta && dorian.scale[0] && dorian.scale[3] && ! dorian.scale[4], "counterpoint: a church mode's keys");

        auto at = [] (const Line& l, int64_t t, int64_t* start = nullptr)
        {
            for (const auto& x : l)
                if (x.start <= t && t < x.end)
                {
                    if (start != nullptr)
                        *start = x.start;
                    return x.pitch;
                }
            return -1;
        };
        auto dissonant = [] (int a, int b, int bass) { const auto i = std::abs (a - b) % 12; return i == 1 || i == 2 || i == 6 || i == 10 || i == 11 || (i == 5 && std::min (a, b) == bass); };
        // The rules, checked from outside: no parallel fifths or octaves between any two voices,
        // no dissonance struck on the beat, dissonances on the off-beats by step, suspensions
        // falling a step, perfect consonances to begin and end.
        auto keepsRules = [&] (const Result& r)
        {
            std::vector<Line> all { cf };
            all.insert (all.end(), r.voices.begin(), r.voices.end());
            std::set<int64_t> times;
            for (const auto& l : all)
                for (const auto& x : l)
                    times.insert (x.start);
            std::vector<int64_t> t (times.begin(), times.end());
            for (size_t i = 0; i < t.size(); ++i)
            {
                std::vector<int> p;
                std::vector<int64_t> started (all.size());
                for (size_t a = 0; a < all.size(); ++a)
                    p.push_back (at (all[a], t[i], &started[a]));
                int bass = 999;
                for (auto x : p)
                    if (x >= 0)
                        bass = std::min (bass, x);
                const bool beat = t[i] % 1920 == 0;
                for (size_t a = 0; a < all.size(); ++a)
                    for (size_t b = a + 1; b < all.size(); ++b)
                    {
                        if (p[a] < 0 || p[b] < 0)
                            continue;
                        if (dissonant (p[a], p[b], bass) && beat && started[a] == t[i] && started[b] == t[i])
                            return false;
                        if (i > 0)
                        {
                            const auto qa = at (all[a], t[i - 1]), qb = at (all[b], t[i - 1]);
                            auto kind = [] (int x) { x %= 12; return x == 0 ? 8 : x == 7 ? 5 : 0; };
                            if (qa >= 0 && qb >= 0 && qa != p[a] && qb != p[b] && kind (std::abs (qa - qb)) != 0
                                && kind (std::abs (qa - qb)) == kind (std::abs (p[a] - p[b])))
                                return false;
                        }
                    }
            }
            for (const auto& v : r.voices)
            {
                if (v.empty())
                    return false;
                // Each new voice against the melody alone: weak-beat dissonances by step, suspensions resolved down.
                for (size_t k = 0; k < v.size(); ++k)
                {
                    const auto c = at (cf, v[k].start);
                    const auto lowest = std::min (c, v[k].pitch);
                    if (c >= 0 && v.size() > 1 && r.voices.size() == 1 && dissonant (c, v[k].pitch, lowest))
                    {
                        if (v[k].start % 1920 == 0)
                            return false;
                        if (k == 0 || k + 1 >= v.size() || std::abs (v[k].pitch - v[k - 1].pitch) > 2 || std::abs (v[k + 1].pitch - v[k].pitch) > 2)
                            return false;
                    }
                    // A note held over the bar line and dissonant there falls a step.
                    for (int64_t bar = (v[k].start / 1920 + 1) * 1920; bar < v[k].end; bar += 1920)
                        if (r.voices.size() == 1 && dissonant (at (cf, bar), v[k].pitch, std::min (at (cf, bar), v[k].pitch))
                            && (k + 1 >= v.size() || v[k + 1].pitch >= v[k].pitch || v[k].pitch - v[k + 1].pitch > 2))
                            return false;
                }
                const auto first = std::abs (v.front().pitch - at (cf, v.front().start)) % 12;
                const auto last = std::abs (v.back().pitch - cf.back().pitch) % 12;
                if (r.voices.size() == 1 && ((first != 0 && first != 7) || last != 0))
                    return false;
            }
            return true;
        };

        bool allKept = true, noneBent = true;
        for (int species = 1; species <= 5; ++species)
            for (int place = 0; place < 3; ++place)
                for (int voices = 1; voices <= 3; ++voices)
                {
                    Settings s;
                    s.species = (Species) species;
                    s.placement = (Placement) place;
                    s.voices = voices;
                    s.mode = dorian;
                    const auto r = generate (cf, s);
                    allKept = allKept && r.voices.size() == (size_t) voices && keepsRules (r);
                    noneBent = noneBent && r.bent == 0;
                }
        check (allKept, "counterpoint: Fux's rules kept in every species, above, below and around, in two to four parts");
        check (noneBent, "counterpoint: no rule bent against Fux's cantus firmus");

        Settings first;
        first.mode = dorian;
        const auto one = generate (cf, first);
        bool inMode = one.voices.size() == 1 && one.voices[0].size() == 11 && one.names[0] == "above";
        for (size_t k = 0; inMode && k < one.voices[0].size(); ++k)
        {
            const auto key = ((one.voices[0][k].pitch - 2) % 12 + 12) % 12;
            inMode = dorian.scale[(size_t) key] || (k == 9 && key == 11);   // C♯, the raised leading note, at the cadence only
            inMode = inMode && one.voices[0][k].pitch > at (cf, one.voices[0][k].start) - 1;   // above the melody
        }
        check (inMode, "counterpoint: first species, a note a bar, in the mode and above");

        Settings second = first;
        second.species = Species::second;
        check (generate (cf, second).voices[0].size() == 21, "counterpoint: second species, two notes a bar and a whole note to end");
        Settings third = first;
        third.species = Species::third;
        check (generate (cf, third).voices[0].size() == 41, "counterpoint: third species, four notes a bar");
        Settings fourth = first;
        fourth.species = Species::fourth;
        const auto tied = generate (cf, fourth).voices[0];
        int over = 0;
        for (const auto& x : tied)
            over += x.start % 1920 != 0 && x.end - x.start == 1920 ? 1 : 0;
        check (tied.front().start == 960 && over >= 6, "counterpoint: fourth species, notes tied over the bar line");

        // Variations: the same seed the same lines, another seed others.
        Settings a = third, b = third;
        b.seed = 7;
        const auto ra = generate (cf, a), rb = generate (cf, b), ra2 = generate (cf, a);
        auto pitchesOf = [] (const Line& l) { std::vector<int> out; for (const auto& x : l) out.push_back (x.pitch); return out; };
        check (pitchesOf (ra.voices[0]) == pitchesOf (ra2.voices[0]) && pitchesOf (ra.voices[0]) != pitchesOf (rb.voices[0]), "counterpoint: a variation number gives its own lines");

        // A maqam: only its keys, with the neutral third heard as a third (Rāst on D: its E half-flat on F♯).
        const auto rast = Mode::fromTuning (mixer::tuningSystems()[(size_t) mixer::findTuning ("rast")], 2);
        Settings maqam;
        maqam.mode = rast;
        maqam.species = Species::third;
        const auto rm = generate (cf, maqam);
        bool rastKeys = rm.bent == 0 && ! rm.voices[0].empty();
        for (const auto& x : rm.voices[0])
            rastKeys = rastKeys && rast.scale[(size_t) (((x.pitch - 2) % 12 + 12) % 12)];
        check (rastKeys && near (rast.keyCents[4], 350.0, 1.0e-9), "counterpoint: a maqam's keys, its quarter steps included");
        // Hypo: Rāst on D from the fifth keeps the voice above to the octave A..A (69..81), a step past each end.
        auto plagal = maqam;
        plagal.mode.below = mixer::hypoBelow (mixer::Hypo::fifth);
        const auto rp = generate (cf, plagal);
        bool inPlagal = ! rp.voices[0].empty() && rp.voices[0].back().pitch % 12 == 2;
        for (const auto& x : rp.voices[0])
            inPlagal = inPlagal && x.pitch >= 67 && x.pitch <= 83 && rast.scale[(size_t) (((x.pitch - 2) % 12 + 12) % 12)];
        check (inPlagal, "counterpoint: a hypo range, the final kept");

        // A raga's āroha: going up never lands on a note it leaves out.
        auto bhairavi = Mode::fromTuning (mixer::tuningSystems()[(size_t) mixer::findTuning ("bhairavi")], 2);
        bhairavi.up = bhairavi.scale;
        bhairavi.up[1] = bhairavi.up[8] = false;   // no komal re or komal dha going up
        Settings raga;
        raga.mode = bhairavi;
        raga.species = Species::third;
        const auto rr = generate (cf, raga);
        bool aroha = ! rr.voices[0].empty();
        for (size_t k = 1; k < rr.voices[0].size(); ++k)
        {
            const auto key = ((rr.voices[0][k].pitch - 2) % 12 + 12) % 12;
            if (rr.voices[0][k].pitch > rr.voices[0][k - 1].pitch)
                aroha = aroha && key != 1 && key != 8;
            aroha = aroha && bhairavi.scale[(size_t) key];
        }
        check (aroha, "counterpoint: a raga's notes, and its āroha going up");

        // An equal temperament: the keys are its steps, 19 to the octave.
        const auto nineteen = Mode::fromTuning (mixer::tuningSystems()[(size_t) mixer::findTuning ("19 equal steps")], 0);
        Settings edo;
        edo.mode = nineteen;
        const auto re = generate (cf, edo);
        check (nineteen.period == 19 && re.voices.size() == 1 && re.voices[0].size() == 11, "counterpoint: in 19 equal steps");

        // The melody of a MIDI file: the top of a chord, a rest ending a phrase; and back to a file.
        auto song = std::make_shared<midifile::Song>();
        song->ticksPerQuarter = 480;
        song->tempos.push_back ({ 0, 500000.0 });
        auto add = [&] (int64_t on, int64_t off, uint8_t pitch) { song->events.push_back ({ on, 0x92, pitch, 100 }); song->events.push_back ({ off, 0x82, pitch, 0 }); };
        add (0, 900, 60);
        add (0, 900, 67);
        add (960, 1900, 65);
        add (2880, 3840, 64);
        std::stable_sort (song->events.begin(), song->events.end(), [] (const midifile::Event& x, const midifile::Event& y) { return x.tick < y.tick; });
        song->lastTick = 3840;
        const auto melody = cantus (*song);
        check (melody.size() == 3 && melody[0].pitch == 67 && melody[0].end == 960 && melody[1].end == 1900 && melody[2].start == 2880,
               "counterpoint: the melody is the top line, held to the next note, a rest kept");
        const auto guessed = guessMode (cf);
        check (guessed.root == 2 && guessed.scale == dorian.scale, "counterpoint: the mode guessed from the melody");
        const auto written = toSong (*song, one.voices[0], 2);
        int ons = 0;
        bool onChannel = true;
        for (const auto& e : written->events)
        {
            ons += e.kind() == 0x90 ? 1 : 0;
            onChannel = onChannel && e.channel() == 2;
        }
        check (ons == 11 && onChannel && written->ticksPerQuarter == 480 && written->channelsUsed == 4, "counterpoint: a voice as a MIDI file");
    }

    // Fugue: the plan (exposition, countersubject, episodes, middle entries, stretto, close),
    // the tonal answer, the rules kept between the voices, and every family of modes.
    void testFugue()
    {
        using namespace counterpoint;
        auto modeOf = [] (const char* plain, int root) { return Mode::fromTuning (mixer::tuningSystems()[(size_t) mixer::findTuning (plain)], root); };
        // A subject by degrees of a mode from its tonic, in quarters and halves (480 a quarter).
        auto subjectIn = [] (const Mode& m)
        {
            std::vector<int> notes;
            for (int p = 0; p < 128; ++p)
                if (m.scale[(size_t) (((p - 60 - m.root) % m.period + m.period) % m.period)])
                    notes.push_back (p);
            const auto tonic = (int) (std::lower_bound (notes.begin(), notes.end(), 60 + m.root) - notes.begin());
            const int degrees[] { 0, 4, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0 }, beats[] { 2, 2, 1, 1, 1, 1, 1, 1, 2, 1, 1, 4 };
            Line l;
            int64_t t = 0;
            for (int i = 0; i < 12; ++i)
            {
                l.push_back ({ t, t + beats[i] * 480, notes[(size_t) (tonic + degrees[i])], 90 });
                t += beats[i] * 480;
            }
            return l;
        };
        const auto dorian = modeOf ("dorian", 2);
        const auto subject = subjectIn (dorian);   // D A G F E D E F G F E D

        fugue::Settings three;
        three.mode = dorian;
        const auto r = fugue::generate (subject, 480, three);
        std::vector<std::string> plan;
        for (const auto& x : r.plan)
            plan.push_back (x.what);
        auto count = [&] (const std::string& what) { return std::count (plan.begin(), plan.end(), what); };
        check (r.voices.size() == 3 && r.names == std::vector<std::string> { "soprano", "alto", "bass" }
               && plan.size() > 4 && plan[0] == "subject" && plan[1] == "answer" && plan[2] == "countersubject"
               && count ("episode") == 3 && count ("middle entry") == 2 && count ("stretto") == 3 && plan.back() == "close",
               "fugue: exposition, countersubject, episodes, two middle entries, a stretto and the close");
        check (r.plan[0].voice == 1 && r.plan[1].voice == 0 && r.plan[1].start == 8640 && r.strettoDelay > 0 && r.strettoDelay <= 4320,
               "fugue: the alto enters first, the soprano answers, the stretto's entries overlap");
        // The tonal answer: the dominant A answered by the tonic D, then the rest at the fifth.
        int64_t answerAt = r.plan[1].start;
        std::vector<int> answer;
        for (const auto& x : r.voices[0])
            if (x.start >= answerAt && x.start < answerAt + 8640)
                answer.push_back (x.pitch);
        check (answer.size() == 12 && answer[0] == 69 && answer[1] == 74 && answer[2] == 74 && answer[11] == 69,
               "fugue: a tonal answer at the fifth, tonic and dominant answering each other");
        fugue::Settings real = three;
        real.answer = fugue::Answer::real;
        const auto rr = fugue::generate (subject, 480, real);
        int64_t realAt = rr.plan[1].start;
        int firstReal = -1, secondReal = -1;
        for (const auto& x : rr.voices[0])
            if (x.start == realAt)
                firstReal = x.pitch;
            else if (x.start == realAt + 960)
                secondReal = x.pitch;
        check (firstReal == 69 && secondReal == 76, "fugue: a real answer, every note a fifth up in the mode");

        // The rules between every two voices: no parallel fifths or octaves, nothing dissonant
        // struck together on the beat, in two and three voices.
        auto at = [] (const Line& l, int64_t t, bool* starts = nullptr)
        {
            for (const auto& x : l)
                if (x.start <= t && t < x.end)
                {
                    if (starts != nullptr)
                        *starts = x.start == t;
                    return x.pitch;
                }
            return -1;
        };
        bool kept = true;
        for (int voices = 2; voices <= 3; ++voices)
        {
            fugue::Settings s = three;
            s.voices = voices;
            const auto f = fugue::generate (subject, 480, s);
            std::set<int64_t> times;
            for (const auto& v : f.voices)
                for (const auto& x : v)
                    times.insert (x.start);
            const std::vector<int64_t> t (times.begin(), times.end());
            int broken = 0;
            for (size_t i = 1; i < t.size(); ++i)
                for (size_t a = 0; a < f.voices.size(); ++a)
                    for (size_t b = a + 1; b < f.voices.size(); ++b)
                    {
                        bool sa = false, sb = false;
                        const auto pa = at (f.voices[a], t[i], &sa), pb = at (f.voices[b], t[i], &sb);
                        const auto qa = at (f.voices[a], t[i - 1]), qb = at (f.voices[b], t[i - 1]);
                        if (pa < 0 || pb < 0)
                            continue;
                        auto kind = [] (int x) { x = std::abs (x) % 12; return x == 0 ? 8 : x == 7 ? 5 : 0; };
                        if (qa >= 0 && qb >= 0 && qa != pa && qb != pb && kind (pa - pb) != 0 && kind (pa - pb) == kind (qa - qb))
                            ++broken;
                        const auto iv = std::abs (pa - pb) % 12;
                        if (t[i] % 960 == 0 && sa && sb && (iv == 1 || iv == 2 || iv == 6 || iv == 10 || iv == 11))
                            ++broken;
                    }
            kept = kept && broken == 0 && f.bent == 0 && f.voices.size() == (size_t) voices;
        }
        check (kept, "fugue: no parallel fifths or octaves and no dissonance struck on the beat, in two and three voices");

        // Four voices, with the middle entries inverted and no stretto.
        fugue::Settings four = three;
        four.voices = 4;
        four.inversion = true;
        four.stretto = false;
        const auto r4 = fugue::generate (subject, 480, four);
        int inverted = 0, final = 0;
        for (const auto& x : r4.plan)
        {
            inverted += x.what == "middle entry, inverted" ? 1 : 0;
            final += x.what == "final entry" && x.voice == 3 ? 1 : 0;
        }
        check (r4.voices.size() == 4 && r4.names[2] == "tenor" && inverted == 1 && final == 1 && r4.strettoDelay == 0,
               "fugue: four voices, an inverted middle entry, the last entry in the bass");

        // Every family of modes: each voice in the mode's notes (the church modes' raised
        // leading note at a cadence aside), and a raga's āroha kept going up.
        bool inMode = true;
        for (const char* plain : { "yaman (kalyan)", "bhairavi (equal tempered)", "phrygian", "hypolydian", "altered", "blues", "rast", "segah", "19 equal steps" })
        {
            fugue::Settings s;
            s.mode = modeOf (plain, 2);
            const auto f = fugue::generate (subjectIn (s.mode), 480, s);
            inMode = inMode && f.voices.size() == 3 && f.end > 0;
            for (const auto& v : f.voices)
            {
                inMode = inMode && ! v.empty();
                for (const auto& x : v)
                    inMode = inMode && (s.mode.ficta || s.mode.scale[(size_t) (((x.pitch - 62) % s.mode.period + s.mode.period) % s.mode.period)]);
            }
        }
        check (inMode, "fugue: in ragas, church and jazz modes, maqams, dastgahs and equal steps, each voice in the mode");
        auto bhairavi = modeOf ("bhairavi", 2);
        bhairavi.up = bhairavi.scale;
        bhairavi.up[1] = bhairavi.up[8] = false;   // no komal re or komal dha going up
        fugue::Settings raga;
        raga.mode = bhairavi;
        const auto rg = fugue::generate (subjectIn (bhairavi), 480, raga);
        int wrongWay = 0;
        for (const auto& v : rg.voices)
            for (size_t k = 1; k < v.size(); ++k)
            {
                const auto key = ((v[k].pitch - 2) % 12 + 12) % 12;
                wrongWay += v[k].pitch > v[k - 1].pitch && (key == 1 || key == 8) ? 1 : 0;
            }
        check (wrongWay <= rg.bent, "fugue: a raga's āroha kept going up");

        // The subject in a melody: its first phrase, cut at a rest, at most four bars.
        Line melody { { 0, 480, 62, 90 }, { 480, 960, 64, 90 }, { 960, 1920, 65, 90 }, { 2880, 3840, 67, 90 } };
        check (fugue::subject (melody, 480).size() == 3, "fugue: the subject is the melody's first phrase");
        Line long_;
        for (int i = 0; i < 40; ++i)
            long_.push_back ({ i * 480, (i + 1) * 480, 60 + i % 5, 90 });
        check (fugue::subject (long_, 480).size() == 16 && fugue::subject ({ { 0, 480, 60, 90 } }, 480).empty(),
               "fugue: a subject of four bars at most, two notes at least");
    }

    // Recording (docs/recording.md): slot takes that end by themselves and play on from that
    // frame, quantised notes, the audio input with its delay, takes of no set length, the next
    // take following on, overdub, timeline takes, the count-in, Capture, and a launch on a frame
    // gone by.
    void testDrumsToMidi()
    {
        // Two bars at 120 BPM: kicks on the beats 1 and 3, snares on 2 and 4, hats on the offbeats.
        const double rate = 48000.0;
        const Frame beat = 24000;
        auto sound = std::make_shared<mixer::AudioClip>();
        sound->left.assign ((size_t) (8 * beat), 0.0f);
        uint32_t seed = 1;
        auto noise = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f; };
        auto add = [&] (Frame at, int kind, float level)
        {
            float last = 0.0f, smooth = 0.0f;
            for (Frame k = 0; k < beat / 2 && at + k < 8 * beat; ++k)
            {
                const auto t = (float) k / (float) rate;
                float v = 0.0f;
                if (kind == 0)
                    v = std::sin (2.0f * 3.14159265f * (50.0f + 60.0f * std::exp (-t / 0.03f)) * t) * std::exp (-t / 0.15f);
                else if (kind == 1)
                {
                    const auto n = noise();
                    smooth += (n - smooth) * 0.3f;   // the crack, below a few kHz
                    v = (0.6f * std::sin (2.0f * 3.14159265f * 190.0f * t) + 0.8f * smooth) * std::exp (-t / 0.06f);
                }
                else
                {
                    const auto n = noise();
                    v = (n - last) * 0.5f * std::exp (-t / 0.02f);   // a hiss, high up
                    last = n;
                }
                sound->left[(size_t) (at + k)] += v * level;
            }
        };
        for (int b = 0; b < 8; ++b)
        {
            add (b * beat, b % 2 == 0 ? 0 : 1, b % 2 == 0 ? 0.9f : 0.7f);
            add (b * beat + beat / 2, 2, 0.4f);
        }
        sound->right = sound->left;
        const auto clip = mixer::makeClip (mixer::AudioClipPtr (sound), 0);
        const auto hits = mixer::findDrums (clip, rate);
        int kicks = 0, snares = 0, hats = 0;
        bool placed = true;
        for (const auto& h : hits)
        {
            const auto step = (double) h.at / (beat / 2.0);
            placed = placed && std::abs (step - std::round (step)) * (beat / 2.0) < 0.005 * rate;
            const auto expected = (int) std::lround (step) % 2 == 1 ? mixer::gm::hat : ((int) std::lround (step) / 2) % 2 == 0 ? mixer::gm::kick : mixer::gm::snare;
            kicks += h.note == mixer::gm::kick && expected == h.note;
            snares += h.note == mixer::gm::snare && expected == h.note;
            hats += h.note == mixer::gm::hat && expected == h.note;
        }
        check (hits.size() == 16 && placed, "drums to MIDI: every hit found, where it was played");
        check (kicks == 4 && snares == 4 && hats == 8, "drums to MIDI: kicks, snares and hats told apart");
        // The same loop as a break of "my breaks": its tempo from its length, its hits on 16ths.
        {
            rhythm::Rhythm r;
            std::string why;
            const auto ok = mixer::breakRhythm (mixer::AudioClipPtr (sound), rate, "test break", r, why);
            check (ok && rhythm::valid (r) && r.group == "my breaks", "my breaks: a loop becomes a rhythm (" + why + ")");
            check (ok && std::abs (r.beats * 60.0 / r.bpm - 4.0) < 0.01, "my breaks: the loop's length in whole bars at its tempo");
            int found[3] {};
            for (const auto& h : rhythm::hits (r))
                for (int k = 0; k < 3; ++k)
                    found[k] += r.lanes[(size_t) h.lane].note == (k == 0 ? mixer::gm::kick : k == 1 ? mixer::gm::snare : mixer::gm::hat);
            check (found[0] == 4 && found[1] == 4 && found[2] == 8, "my breaks: kicks, snares and hats on the grid");
            check (ok && rhythm::toSong (r, 1, r.bpm)->noteCount() == 16, "my breaks: plays as MIDI");
            rhythm::Rhythm none;
            auto silence = std::make_shared<mixer::AudioClip>();
            silence->left.assign (48000, 0.0f);
            silence->right = silence->left;
            check (! mixer::breakRhythm (mixer::AudioClipPtr (silence), rate, "silence", none, why) && ! why.empty(), "my breaks: silence is refused, with why");
        }
        const auto midi = mixer::drumsToMidi (clip, rate, 120.0);
        bool kit = true;
        for (const auto& e : midi->events)
            kit = kit && (e.status & 0x0F) == 9;
        check (midi->events.size() == 32 && kit && midi->length == 8 * beat, "drums to MIDI: a note on the kit's channel for each hit");
        check (std::find_if (hits.begin(), hits.end(), [] (const mixer::DrumHit& h) { return h.note == mixer::gm::kick; })->velocity
                   > std::find_if (hits.begin(), hits.end(), [] (const mixer::DrumHit& h) { return h.note == mixer::gm::hat; })->velocity,
               "drums to MIDI: louder hits play louder");
    }

    void testRecording()
    {
        const double rate = 48000.0;
        const Frame beat = 24000;   // at 120 BPM
        struct Feed final : mixer::Input
        {
            int64_t sent = 0;
            int pending = 0;
            int read (float* io, int maxFrames) override
            {
                const auto n = std::min (maxFrames, pending);
                for (int k = 0; k < n; ++k)
                {
                    io[2 * k] = (float) (sent + k) * 1.0e-6f;   // the stream's own frame, to see where it lands
                    io[2 * k + 1] = -(float) (sent + k) * 1.0e-6f;
                }
                sent += n;
                pending -= n;
                return n;
            }
        };
        auto setUp = [&] (mixer::Engine& m)
        {
            m.prepare (rate);
            m.setNumTracks (2);
            m.tempo = 120.0;
            m.launchQuantise = 4.0;
            m.setLiveTrack (0);
            m.setInstrument (0, std::make_shared<mixer::Synth>());
            m.setLoop (0, 400 * beat);   // an empty mix would stop at once
        };
        std::vector<float> l (256), r (256);
        auto run = [&] (mixer::Engine& m, Frame frames, Feed* feed = nullptr)
        {
            for (Frame done = 0; done < frames; done += 256)
            {
                const auto n = (int) std::min<Frame> (256, frames - done);
                if (feed != nullptr)
                    feed->pending += n;
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                m.render (l.data(), r.data(), n);
            }
        };

        {
            // A bar of MIDI from a stopped transport: it starts at once, stops by itself and loops.
            mixer::Engine m;
            setUp (m);
            mixer::RecordRequest q;
            q.scene = 0;
            q.beats = 4.0;
            q.quantise = 1.0;
            std::string error;
            check (m.record (q, error) && m.recordInfo (0).state == mixer::RecordState::waiting, "recording: a slot take waits for the transport");
            m.play();
            run (m, 23040);
            m.liveMidi (0x90, 60, 100);   // a little early for beat 2: quantised onto it
            run (m, 12800);
            m.liveMidi (0x80, 60, 0);
            check (m.recordInfo (0).state == mixer::RecordState::recording && m.isRecording(), "recording: the take records");
            run (m, 4 * beat - 35840 + 256);
            const auto slot = m.slot (0, 0);
            check (slot != nullptr && slot->clip.midi != nullptr && slot->clip.length == 4 * beat && m.recordInfo (0).state == mixer::RecordState::none,
                   "recording: the take ends by itself at its length, in the slot");
            const auto& ev = slot->clip.midi->events;
            check (ev.size() == 2 && ev[0].frame == beat && ev[0].data1 == 60 && ev[1].frame == beat + 12800,
                   "recording: a note on the beat it was nearest, its length kept");
            const auto info = m.launchInfo (0);
            check (info.playing == 0 && ! info.timeline && near (info.progress, 256.0 / (4.0 * beat), 1.0e-6), "recording: it plays on from the frame it ended");
            const auto got = m.takeRecorded();
            check (got.size() == 1 && got[0].scene == 0 && got[0].midi == slot->clip.midi && got[0].notes && got[0].length == 4 * beat,
                   "recording: the take handed to the UI to keep");
            auto song = mixer::songOf (*got[0].midi, rate, 120.0);
            check (song->events.size() == 2 && song->events[0].tick == 480 && song->lastTick == 4 * 480, "recording: a take as a MIDI file");
        }

        {
            // Audio: the input as it came, moved earlier by its delay, ending with the bar.
            mixer::Engine m;
            setUp (m);
            m.setInstrument (0, nullptr);   // an audio track: records the input
            Feed feed;
            m.setInput (&feed);
            m.inputLatencyMs = 10.0f;   // 480 frames
            mixer::RecordRequest q;
            q.scene = 1;
            q.beats = 4.0;
            q.midi = false;
            q.audio = true;
            std::string error;
            check (m.record (q, error), "recording: an audio take");
            m.play();
            run (m, 4 * beat + 1024, &feed);
            const auto slot = m.slot (0, 1);
            check (slot != nullptr && slot->clip.audio != nullptr && slot->clip.length == 4 * beat, "recording: the audio take in the slot");
            const auto& a = *slot->clip.audio;
            check (near (a.left[0], 480.0e-6, 1.0e-7) && near (a.left[1000], 1480.0e-6, 1.0e-7) && near (a.right[1000], -1480.0e-6, 1.0e-7)
                   && near (a.left[(size_t) (4 * beat - 1)], (4.0 * beat - 1.0 + 480.0) * 1.0e-6, 1.0e-6),
                   "recording: the input moved earlier by its delay, the end filled in after the take");
            check (m.takeRecorded().size() == 1, "recording: handed over once its late audio is in");
            float loud = 0.0f;
            run (m, 256, &feed);
            for (auto v : l)
                loud = std::max (loud, std::abs (v));
            check (loud > 0.0f, "recording: the audio take plays");
        }

        {
            // No set length: it ends on the next quantise after stopping; the next take follows on.
            mixer::Engine m;
            setUp (m);
            mixer::RecordRequest q;
            q.scene = 0;
            q.beats = 0.0;
            std::string error;
            m.play();
            run (m, 1024);
            check (m.record (q, error), "recording: a take of no set length");
            run (m, 4 * beat);   // waits for the bar, then records
            m.liveMidi (0x90, 64, 90);
            run (m, beat + beat / 2);
            m.stopRecording (0);
            run (m, 3 * beat + 1024);
            const auto slot = m.slot (0, 0);
            check (slot != nullptr && slot->clip.length == 4 * beat && m.launchInfo (0).playing == 0, "recording: stopped, it ends on the next bar");

            mixer::RecordRequest a;
            a.scene = 2;
            a.beats = 4.0;
            check (m.record (a, error), "recording: a fixed take");
            run (m, 4 * beat);
            mixer::RecordRequest b = a;
            b.scene = 3;
            check (m.record (b, error) && m.recordInfo (0).scene == 2, "recording: the next take asked for while one records");
            run (m, 8 * beat);
            check (m.slot (0, 2) != nullptr && m.slot (0, 3) != nullptr && m.slot (0, 3)->clip.length == 4 * beat
                   && m.launchInfo (0).playing == 3 && m.recordInfo (0).state == mixer::RecordState::none,
                   "recording: the next take starts the frame the first ends, and plays once done");
        }

        {
            // Overdub: notes go into the playing clip where it has got to.
            mixer::Engine m;
            setUp (m);
            auto clip = std::make_shared<mixer::MidiClip>();
            clip->length = 4 * beat;
            clip->events.reserve (64);
            clip->events.push_back ({ 0, 0x90, 48, 100 });
            clip->events.push_back ({ beat, 0x80, 48, 0 });
            auto slot = std::make_shared<mixer::Slot>();
            slot->clip = mixer::makeClip (mixer::MidiClipPtr (clip), 0);
            m.setSlot (0, 0, slot);
            m.launch (0, 0);
            run (m, 4 * beat + 2 * beat);   // into the second pass, at beat 2
            mixer::RecordRequest q;
            q.scene = 0;
            q.overdub = true;
            q.overdubMidi = clip;
            std::string error;
            check (m.record (q, error) && m.recordInfo (0).state == mixer::RecordState::overdubbing, "recording: overdub");
            m.liveMidi (0x90, 67, 80);
            run (m, 3 * beat);   // over the loop's end
            m.liveMidi (0x80, 67, 0);
            run (m, 256);
            m.stopRecording (0);
            run (m, 256);
            check (clip->events.size() == 4 && clip->events[2].frame == 2 * beat && clip->events[2].data1 == 67
                   && clip->events[3].frame == 4 * beat - 1 && m.recordInfo (0).state == mixer::RecordState::none,
                   "recording: an overdubbed note where the clip had got to, held over its end ending with it");
            const auto got = m.takeRecorded();
            check (got.size() == 1 && got[0].overdub && got[0].midi == clip, "recording: the overdub handed back to keep");
        }

        {
            // The timeline: from where the transport is, a take per pass of a loop.
            mixer::Engine m;
            setUp (m);
            Feed feed;
            m.setInput (&feed);
            m.inputLatencyMs = 0.0f;
            m.setPosition (beat);
            m.setLoop (beat, 5 * beat);
            mixer::RecordRequest q;
            q.scene = -1;
            q.beats = 0.0;
            q.audio = true;
            std::string error;
            check (m.record (q, error), "recording: a timeline take");
            m.play();
            run (m, beat, &feed);
            m.liveMidi (0x90, 72, 100);
            run (m, beat, &feed);
            m.liveMidi (0x80, 72, 0);
            run (m, 2 * beat + beat, &feed);   // over the loop into a second pass
            auto got = m.takeRecorded();
            m.stopRecording (0);
            run (m, 256, &feed);
            const auto more = m.takeRecorded();
            got.insert (got.end(), more.begin(), more.end());
            check (got.size() == 2 && got[0].start == beat && got[0].length == 4 * beat && got[1].start == beat && got[1].pass == 1,
                   "recording: a take for each pass of the loop, from where it started");
            check (got[0].midi != nullptr && got[0].notes && got[0].midi->events[0].frame == beat && got[0].audio != nullptr
                   && near (got[0].audio->left[100], 100.0e-6, 1.0e-7) && got[1].midi == nullptr,
                   "recording: its notes and audio from its start");
        }

        {
            // The count-in, with the metronome.
            mixer::Engine m;
            setUp (m);
            m.metronome = true;
            m.playWithCountIn (1.0);
            check (m.countingIn() && ! m.isPlaying(), "recording: counting in");
            float loud = 0.0f;
            for (Frame done = 0; done < 4 * beat - 256; done += 256)
            {
                run (m, 256);
                for (auto v : l)
                    loud = std::max (loud, std::abs (v));
            }
            check (loud > 0.1f && ! m.isPlaying(), "recording: clicks while counting in");
            run (m, 512);
            check (m.isPlaying() && ! m.countingIn() && m.launchClock() > 0 && m.launchClock() <= 512, "recording: the transport starts after the count-in");
        }

        {
            // Capture: what was played without recording, in whole bars on the launch clock.
            mixer::Engine m;
            setUp (m);
            m.play();
            run (m, 9 * beat);
            m.liveMidi (0x90, 60, 100);   // the second beat of the third bar
            run (m, beat);
            m.liveMidi (0x80, 60, 0);
            m.liveMidi (0x90, 62, 100);
            run (m, 4 * beat);
            m.liveMidi (0x80, 62, 0);
            run (m, 512);
            Frame phase = -1;
            const auto clip = m.capture (0, phase);
            check (clip != nullptr && phase == 8 * beat && clip->length == 8 * beat && clip->events.size() == 4 && clip->events[0].frame == beat,
                   "recording: Capture keeps the last phrase, two bars from its bar line");
            Frame none = 0;
            check (m.capture (1, none) == nullptr, "recording: nothing to capture on a track not played");
        }

        {
            // A launch on a frame gone by plays in time with it.
            mixer::Engine m;
            setUp (m);
            AudioData::Builder b (1, rate);
            std::vector<float> v (48000, 0.5f);
            const float* one[1] = { v.data() };
            b.append (one, 48000);
            auto slot = std::make_shared<mixer::Slot>();
            slot->clip = mixer::makeClip (mixer::makeAudioClip (*b.finish(), "dc.wav"), 0);
            m.setSlot (0, 0, slot);
            m.play();
            run (m, 7680);
            m.launchAt (0, 0, 7000);   // gone by
            run (m, 256);
            const auto info = m.launchInfo (0);
            check (info.playing == 0 && info.progress > 0.0f && std::abs (info.progress - (float) (m.launchClock() - 7000) / 48000.0f) < 1.0e-3f,
                   "recording: a launch on a frame gone by plays in time with it");
        }

        {
            // A MIDI layer over a two-bar audio loop: one pass, round from where it began.
            mixer::Engine m;
            setUp (m);
            auto sound = std::make_shared<mixer::AudioClip>();
            sound->left.assign ((size_t) (8 * beat), 0.25f);
            sound->right = sound->left;
            auto slot = std::make_shared<mixer::Slot>();
            slot->clip = mixer::makeClip (mixer::AudioClipPtr (sound), 0);
            m.setSlot (0, 0, slot);
            m.launch (0, 0);
            run (m, 1024);
            auto notes = std::make_shared<mixer::MidiClip>();
            notes->events.reserve (64);
            notes->length = 8 * beat;
            auto layered = std::make_shared<mixer::Slot> (*slot);
            layered->clip.midi = notes;
            m.setSlot (0, 0, layered);
            mixer::RecordRequest q;
            q.scene = 0;
            q.layer = true;
            q.overdubMidi = notes;
            std::string error;
            check (m.record (q, error) && m.recordInfo (0).state == mixer::RecordState::waiting, "recording: a layer waits for the quantise");
            run (m, 5 * beat - 1024);
            check (m.recordInfo (0).state == mixer::RecordState::recording && m.launchInfo (0).playing == 0, "recording: the layer records, the loop playing on");
            m.liveMidi (0x90, 62, 100);
            run (m, beat / 2);
            m.liveMidi (0x80, 62, 0);
            run (m, 7 * beat);
            const auto now = m.slot (0, 0);
            check (now != nullptr && now->clip.audio == slot->clip.audio && now->clip.midi == notes && now->clip.length == 8 * beat
                   && m.recordInfo (0).state == mixer::RecordState::none && m.launchInfo (0).playing == 0,
                   "recording: the layer ends by itself after one pass; both play on");
            check (notes->events.size() == 2 && notes->events[0].frame == 5 * beat && notes->events[1].frame == 5 * beat + beat / 2,
                   "recording: the layer's notes round the loop from where it began");
            const auto got = m.takeRecorded();
            check (got.size() == 1 && got[0].layer && got[0].midi == notes && got[0].notes && got[0].length == 8 * beat, "recording: the layer handed to the UI");

            // Cancelled before it begins: handed back with nothing in it.
            auto more = std::make_shared<mixer::AudioClip>();
            more->left.assign ((size_t) (8 * beat), 0.0f);
            more->right = more->left;
            auto withBoth = std::make_shared<mixer::Slot>();
            withBoth->clip = mixer::makeClip (mixer::MidiClipPtr (notes), 0);
            withBoth->clip.audio = more;
            m.setSlot (0, 1, withBoth);
            mixer::RecordRequest c;
            c.scene = 1;
            c.layer = true;
            c.midi = false;
            c.audio = true;
            c.overdubAudio = more;
            check (m.record (c, error), "recording: an audio layer for a slot not playing");
            m.stopRecording (0);
            run (m, 256);
            const auto back = m.takeRecorded();
            check (back.size() == 1 && back[0].layer && back[0].length == 0 && back[0].audio == more, "recording: a layer cancelled, handed back to take out");
        }

        {
            // The track's instrument recorded as audio: as it sounds, with no input delay.
            mixer::Engine m;
            setUp (m);
            m.inputLatencyMs = 10.0f;
            mixer::RecordRequest q;
            q.scene = 0;
            q.beats = 4.0;
            q.midi = false;
            q.audio = true;
            q.fromInstrument = true;
            std::string error;
            check (m.record (q, error), "recording: the instrument as audio");
            m.play();
            m.liveMidi (0x90, 69, 110);
            run (m, beat);
            m.liveMidi (0x80, 69, 0);
            run (m, 3 * beat + 256);
            const auto slot = m.slot (0, 0);
            check (slot != nullptr && slot->clip.audio != nullptr && ! slot->clip.audio->mono && slot->clip.length == 4 * beat
                   && slot->clip.midi == nullptr, "recording: the instrument's take is a stereo audio clip");
            float early = 0.0f, late = 0.0f;
            for (Frame f = 0; f < 480; ++f)
                early = std::max (early, std::abs (slot->clip.audio->left[(size_t) f]));
            for (Frame f = 3 * beat; f < 4 * beat; ++f)
                late = std::max (late, std::abs (slot->clip.audio->left[(size_t) f]));
            check (early > 0.001f && late < early, "recording: the note from its first frame, not moved by the input's delay");
            mixer::RecordRequest none = q;
            none.scene = 1;
            m.setInstrument (1, nullptr);
            none.track = 1;
            check (! m.record (none, error), "recording: a track with no instrument cannot record it");
        }
    }

    // Φ Repeat: slices caught at the interval points, repeated for the gate, lower and quieter if asked.
    void testPhiRepeat()
    {
        const double rate = 48000.0;   // at 120 BPM a beat is 24000 samples, a sixteenth 6000
        const int frames = 96000 * 2;  // two bars
        auto ramp = [&] (int i) { return (float) ((i % 48000) / 48000.0); };
        auto run = [&] (phi::Repeat::Settings s, int channels = 1)
        {
            phi::Repeat r;
            r.prepare (rate, channels);
            r.set (s);
            std::vector<std::vector<float>> out ((size_t) channels, std::vector<float> ((size_t) frames));
            for (int i = 0; i < frames; ++i)
                for (auto& c : out)
                    c[(size_t) i] = ramp (i);
            for (int at = 0; at < frames; at += 512)
            {
                std::vector<float*> p;
                for (auto& c : out)
                    p.push_back (c.data() + at);
                r.process (p.data(), channels, std::min (512, frames - at));
            }
            return out;
        };

        phi::Repeat::Settings s;
        s.interval = 5;            // a bar
        s.offsetSixteenths = 0;
        s.grid = 3;                // 1/16
        s.gateSixteenths = 4;      // one beat
        s.mix = phi::Repeat::Mix::insert;

        auto never = s;
        never.chance = 0.0;
        const auto dry = run (never)[0];
        bool same = true;
        for (int i = 0; i < frames; ++i)
            same = same && dry[(size_t) i] == ramp (i);
        check (same, "phi repeat: no chance, no repeats, the sound untouched");

        const auto rep = run (s)[0];
        check (near (rep[3000], ramp (3000), 1.0e-6), "phi repeat: the first slice is the live sound");
        check (near (rep[9000], ramp (3000), 1.0e-6) && near (rep[15000], ramp (3000), 1.0e-6) && near (rep[21000], ramp (3000), 1.0e-6),
               "phi repeat: the slice plays again each sixteenth for the gate");
        check (near (rep[30000], ramp (30000), 1.0e-6), "phi repeat: after the gate the sound comes back");
        check (near (rep[96000 + 9000], ramp (96000 + 3000), 1.0e-6), "phi repeat: and repeats again at the next bar");

        auto offset = s;
        offset.offsetSixteenths = 4;   // a beat into the bar
        const auto late = run (offset)[0];
        check (near (late[9000], ramp (9000), 1.0e-6) && near (late[24000 + 9000], ramp (24000 + 3000), 1.0e-6),
               "phi repeat: offset moves where in the bar it repeats");

        auto lower = s;
        lower.pitch = -12.0;
        const auto down = run (lower)[0];
        check (near (down[2000], ramp (1000), 1.0e-6) && near (down[6000 + 2000], ramp (1000), 1.0e-6),
               "phi repeat: an octave down reads the slice at half speed");

        auto falling = s;
        falling.pitchDecay = 1.0;   // 3 semitones lower each repetition
        const auto fall = run (falling)[0];
        check (near (fall[6000 + 3000], ramp ((int) std::lround (3000 * std::pow (2.0, -3.0 / 12.0))), 1.0e-4),
               "phi repeat: pitch decay lowers each repetition");

        auto fading = s;
        fading.decay = 1.0;
        const auto fade = run (fading)[0];
        check (std::abs (fade[23000]) < 0.05f * ramp (5000) + 1.0e-3f && near (fade[9000], ramp (3000) * (1.0 - 9000.0 / 24000.0), 1.0e-4),
               "phi repeat: decay fades the repeats out by the gate's end");

        auto gated = s;
        gated.mix = phi::Repeat::Mix::gate;
        const auto only = run (gated)[0];
        check (only[40000] == 0.0f && near (only[9000], ramp (3000), 1.0e-6), "phi repeat: gate mode hears only the repeats");

        auto mixed = s;
        mixed.mix = phi::Repeat::Mix::mix;
        const auto both = run (mixed)[0];
        check (near (both[9000], ramp (9000) + ramp (3000), 1.0e-6), "phi repeat: mix mode keeps the dry sound under them");

        auto held = s;
        held.chance = 0.0;
        held.repeat = true;
        const auto hold = run (held)[0];
        check (near (hold[96000 + 9000], ramp (3000), 1.0e-6), "phi repeat: held Repeat repeats whatever the chance, past the gate");

        auto triplet = s;
        triplet.triplets = true;   // 1/16 triplets: 4000 samples
        const auto three = run (triplet)[0];
        check (near (three[4000 + 1000], ramp (1000), 1.0e-6), "phi repeat: triplets shorten the grid by a third");

        // Golden chance: the repeats fall on the golden rhythm's hits (Φ Rhythm's), 61.8 % of the points.
        {
            phi::Repeat r;
            r.prepare (rate, 1);
            auto g = s;
            g.interval = 1;    // every sixteenth
            g.gateSixteenths = 1;
            g.golden = true;
            g.chance = 0.6180339887;
            r.set (g);
            int repeats = 0;
            bool hits = true;
            std::vector<float> block (6000, 0.5f);
            float* p[] { block.data() };
            for (int k = 0; k < 89; ++k)
            {
                r.process (p, 1, 1);
                const bool on = r.repeating();
                repeats += on ? 1 : 0;
                hits = hits && on == phi::hit (k, g.chance);
                r.process (p, 1, 5999);
            }
            int expected = 0;
            for (int k = 0; k < 89; ++k)
                expected += phi::hit (k, g.chance) ? 1 : 0;
            check (repeats == expected && repeats >= 55 && repeats <= 56 && hits, "phi repeat: golden chance repeats on the golden rhythm's steps, 61.8 % of them");
        }

        auto band = s;
        band.filter = true;
        band.filterHz = 2000.0;
        band.filterOctaves = 1.0;
        band.variation = 1.0;
        band.variationMode = phi::Repeat::Variation::everyRepeat;
        const auto stereo = run (band, 2);
        bool finite = true;
        for (const auto& c : stereo)
            for (auto v : c)
                finite = finite && std::isfinite (v) && std::abs (v) < 4.0f;
        check (finite, "phi repeat: stable with the filter and every grid variation");
    }

    // Φ Phaser: stages φ apart, a sweep that never repeats, and a response that matches what it does.
    void testPhiPhaser()
    {
        phi::Phaser::Settings s;
        check (near (phi::Phaser::stageHz (s, 3, 0.0) / phi::Phaser::stageHz (s, 2, 0.0), phi::ratio, 1.0e-9)
               && near (phi::Phaser::stageHz (s, 2, 0.0), s.centreHz, 1.0e-9), "phi phaser: stages a golden ratio apart around the centre");
        {
            auto classic = s;
            classic.spread = 0.0;
            check (near (phi::Phaser::stageHz (classic, 0, 0.0), phi::Phaser::stageHz (classic, 4, 0.0), 1.0e-9), "phi phaser: no spread is a classic phaser");
        }
        bool bounded = true;
        for (int i = 0; i < 1000; ++i)
            bounded = bounded && std::abs (phi::Phaser::lfo (i * 0.037, 0.5, 0.0)) <= 1.0 + 1.0e-12;
        check (bounded, "phi phaser: the LFOs stay within -1 and 1");
        check (std::abs (phi::Phaser::lfo (0.3, 0.5, 0.0) - phi::Phaser::lfo (2.3, 0.5, 0.0)) > 0.01, "phi phaser: a slow cycle later the sweep is elsewhere");

        // With the LFOs still, its impulse response is the response it draws.
        {
            auto still = s;
            still.depth = 0.0;
            phi::Phaser p;
            p.prepare (48000.0, 2);
            p.set (still);
            p.reset();
            const int n = 1 << 16;
            std::vector<float> left ((size_t) n), right ((size_t) n);
            left[0] = right[0] = 1.0f;
            float* channels[] = { left.data(), right.data() };
            p.process (channels, 2, n);
            bool matches = true;
            for (const double hz : { 300.0, 1000.0, 3000.0 })
            {
                double re = 0.0, im = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    re += left[(size_t) i] * std::cos (6.283185307179586 * hz * i / 48000.0);
                    im -= left[(size_t) i] * std::sin (6.283185307179586 * hz * i / 48000.0);
                }
                matches = matches && near (10.0 * std::log10 (re * re + im * im), phi::Phaser::responseDb (still, hz, 48000.0), 0.1);
            }
            check (matches, "phi phaser: the drawn response is what it does");
            double deepest = 0.0;
            for (int i = 0; i < 400; ++i)
                deepest = std::min (deepest, phi::Phaser::responseDb (still, 20.0 * std::pow (1000.0, i / 399.0), 48000.0));
            check (deepest < -12.0, "phi phaser: it cuts notches");
        }

        // No mix leaves the sound alone; full feedback on 13 stages stays stable.
        {
            auto dry = s;
            dry.mix = 0.0;
            phi::Phaser p;
            p.prepare (48000.0, 1);
            p.set (dry);
            p.reset();
            std::vector<float> x (4800);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) std::sin (i * 0.01);
            auto y = x;
            float* channels[] = { y.data() };
            p.process (channels, 1, (int) y.size());
            check (y == x, "phi phaser: no mix is the dry sound");

            auto wild = s;
            wild.stages = 13;
            wild.feedback = -0.9;
            wild.depth = 1.0;
            wild.rateHz = 5.0;
            p.set (wild);
            float peak = 0.0f;
            std::mt19937 noise (7);
            std::uniform_real_distribution<float> uniform (-0.5f, 0.5f);
            float* noisy[] = { x.data() };
            for (int block = 0; block < 100; ++block)
            {
                for (auto& v : x)
                    v = uniform (noise);
                p.process (noisy, 1, (int) x.size());
                for (const auto v : x)
                    peak = std::max (peak, std::abs (v));
            }
            check (std::isfinite (peak) && peak < 4.0f, "phi phaser: stable with the most feedback");
        }
    }

    // The Plugin Analyzer's measurements, on effects whose answers are known.
    struct TestDevice : measure::Device
    {
        std::function<void (float*, float*, int)> fn;
        std::function<void()> clear;
        int latency = 0;
        void reset() override { if (clear) clear(); }
        void process (float* l, float* r, int n) override { fn (l, r, n); }
        int reportedLatency() const override { return latency; }
    };

    measure::Settings quickSettings()
    {
        measure::Settings s;
        s.fftOrder = 15;
        s.aliasTones = { 1000.0, 10000.0 };
        s.dynamicsStepDb = 4.0;
        s.modulationSeconds = 6.0;
        s.tailSeconds = 0.5;
        return s;
    }

    // Stochastic music (Xenakis): sieves read and written back, and lines drawn by the laws,
    // inside their range, sieve and grid, the same for the same seed.
    void testCubify()
    {
        using namespace cubify;
        // The group: closed, each rotation undone by its inverse, the A rotations keeping the
        // tetrahedra and the Q rotations swapping them, A·A and Q·Q in A, A·Q and Q·A in Q.
        const auto& all = group();
        bool closed = true, inverses = true, halves = true;
        for (int p = 0; p < 24; ++p)
        {
            inverses = inverses && product (p, inverse (p)) == 0 && product (inverse (p), p) == 0;
            for (int q = 0; q < 24; ++q)
            {
                const auto r = product (p, q);
                for (size_t i = 0; i < 8; ++i)
                    closed = closed && all[(size_t) r].seq[i] == all[(size_t) q].seq[(size_t) all[(size_t) p].seq[i]];
                halves = halves && all[(size_t) r].swaps == (all[(size_t) p].swaps != all[(size_t) q].swaps);
            }
            halves = halves && all[(size_t) p].swaps == (all[(size_t) p].seq[0] >= 4);
        }
        check (closed && inverses && halves, "cubify: the 24 rotations of the cube are a group, A and Q its halves");
        check (std::string (all[0].label) == "I" && find ("Q12") >= 0 && find ("X") < 0, "cubify: the labels");
        // Nomos Alpha's own path (Formalized Music, VIII), and the one in the first half alone.
        auto labels = [&] (const std::vector<int>& path)
        {
            std::string out;
            for (auto r : path)
                out += std::string (out.empty() ? "" : " ") + all[(size_t) r].label;
            return out;
        };
        check (labels (chain (find ("D"), find ("Q12"), 18)) == "D Q12 Q4 E Q8 Q2 E2 Q7 Q4 D2 Q3 Q11 L2 Q7 Q2 L Q8 Q11",
               "cubify: D, Q12 gives Xenakis's path");
        check (labels (chain (find ("E"), find ("G2"), 16)) == "E G2 L2 C G D E B D2 L G C L2 E2 D2 B", "cubify: E, G2 stays in A");

        // A scene: a melody, a drum loop and an audio loop.
        Part melody { "lead", {}, false, false };
        for (int i = 0; i < 16; ++i)
            melody.notes.push_back ({ i * 240, i * 240 + 200, 60 + (i * 5) % 14, 90 });
        Part drums { "drums", {}, true, false };
        for (int i = 0; i < 16; ++i)
            drums.notes.push_back ({ i * 240, i * 240 + 60, i % 4 == 0 ? 36 : i % 2 == 0 ? 38 : 42, 100 });
        Part loop { "pad", {}, false, true };
        Settings settings;
        auto result = arrange ({ melody, drums, loop }, settings);
        check (result.error.empty() && result.sections.size() == 24 && result.lines.size() == 3, "cubify: 24 sections, a line for each track");
        bool form = true, bars = true;
        for (size_t i = 0; i < result.sections.size(); ++i)
        {
            form = form && result.sections[i].free == (i % 4 == 3);
            bars = bars && result.sections[i].length > 0 && result.sections[i].length % (4 * 480) == 0
                && (i == 0 || result.sections[i].start == result.sections[i - 1].start + result.sections[i - 1].length);
        }
        check (form && bars, "cubify: V1 V1 V1 V2 six times, each section whole bars, one after another");
        check (result.corners[0] != result.corners[1] && result.corners[1] < 4, "cubify: the tracks on corners of their own, the drums low");
        std::set<int> kit;
        for (const auto& n : result.lines[1].notes)
            kit.insert (n.pitch);
        check (! result.lines[1].notes.empty() && kit.size() <= 3 && (kit.count (36) || kit.count (38) || kit.count (42)), "cubify: the drums keep their own drums");
        check (! result.lines[2].plays.empty() && result.lines[2].plays.size() < 18 && result.lines[2].notes.empty(), "cubify: the audio loop plays in some V1 sections, not all");
        bool quarter = false;
        for (const auto& c : result.lines[0].controls)
            quarter = quarter || (c.bend && c.value == 10240);
        check (quarter, "cubify: quarter tones by pitch bend");
        bool mono = true;
        for (size_t i = 1; i < result.lines[0].notes.size(); ++i)
            mono = mono && result.lines[0].notes[i].start >= result.lines[0].notes[i - 1].end;
        check (mono, "cubify: one note at a time, so each can bend on its own");
        check (result.commentary.find ("pitch D, durations E") != std::string::npos && result.modified.bars >= 0, "cubify: the commentary names the rotations");
        const auto song = toSong (result.lines[0], 480, 120.0, 0);
        std::string why;
        const auto bytes = midifile::bytes (*song);
        const auto back = midifile::parse (bytes.data(), bytes.size(), why);
        check (back != nullptr && back->noteCount() == (int) result.lines[0].notes.size(), "cubify: the line as a MIDI file and back");
        const auto again = arrange ({ melody, drums, loop }, settings);
        settings.seed = 2;
        const auto other = arrange ({ melody, drums, loop }, settings);
        check (again.commentary == result.commentary && other.commentary != result.commentary, "cubify: the same seed, the same arrangement; another, another");

        // Messiaen: mode 2 (octatonic) on the lowest note, palindromic durations, semitones.
        settings = {};
        settings.pitches = Pitches::messiaen;
        settings.messiaenMode = 2;
        settings.palindromes = true;
        settings.quarterTones = false;
        result = arrange ({ melody }, settings);
        bool octatonic = true;
        for (const auto& n : result.lines[0].notes)
            if (n.start < result.sections[3].start)
                octatonic = octatonic && ((0x6DB >> (((n.pitch - 60) % 12 + 12) % 12)) & 1) != 0;
        check (! result.lines[0].notes.empty() && octatonic, "cubify: pitches in Messiaen's mode 2");
        const auto& first = result.lines[0].notes;
        bool palindrome = first.size() > 8;
        for (size_t k = 0; palindrome && k < 4; ++k)
            palindrome = first[k + 1].start - first[k].start == first[8 - k].start - first[7 - k].start;
        check (palindrome, "cubify: non-retrogradable rhythms");
        settings.path = Path::modeDeValeurs;
        settings.glissandi = false;
        const auto locked = arrange ({ melody }, settings);
        bool still = true;
        for (const auto& c : locked.lines[0].controls)
            still = still && (! c.bend || c.value == 8192);
        check (locked.error.empty() && still && locked.commentary.find ("locked") != std::string::npos, "cubify: mode de valeurs, in semitones");
        check (! arrange ({}, settings).error.empty() && ! arrange (std::vector<Part> (9, melody), settings).error.empty(), "cubify: nothing, or more than eight tracks");
        // Messiaen's seven modes in the Mode menu's tunings, mode 2 the octatonic.
        bool seven = true;
        for (int m = 1; m <= 7; ++m)
            seven = seven && mixer::findTuning ("messiaen mode " + std::to_string (m)) >= 0;
        check (seven && mixer::modeKeys (mixer::tuningSystems()[(size_t) mixer::findTuning ("messiaen mode 2")]) == 0x6DB,
               "cubify: messiaen's modes of limited transposition are listed");
    }

    void testSerial()
    {
        using namespace serial;
        auto rowNamed = [] (const std::string& start)
        {
            for (const auto& h : historical())
                if (std::string (h.name).rfind (start, 0) == 0)
                    return h.row;
            return Row {};
        };
        const auto op25 = rowNamed ("schoenberg: suite for piano, op. 25");
        Row read {}, numbers {};
        std::string error;
        check (parse ("E F G C# F# Eb Ab D B C A Bb", read, error) && read == op25
               && parse ("4,5,7,1,6,3,8,2,e,0,9,t", numbers, error) && numbers == op25 && text (op25, true) == "e f g c♯ f♯ e♭ a♭ d b c a b♭",
               "serial: a row as note names or numbers");
        check (! parse ("e f g c# f# eb ab d b c a", read, error) && ! parse ("e e g c# f# eb ab d b c a bb", read, error)
               && ! parse ("0 1 2 3 4 5 6 7 8 9 10 12", read, error), "serial: not a row");
        check (transform (op25, { Form::i, 0 }) == Row { 4, 3, 1, 7, 2, 5, 0, 6, 9, 8, 11, 10 }
               && transform (op25, { Form::r, 0 }) == Row { 10, 9, 0, 11, 2, 8, 3, 6, 1, 7, 5, 4 }
               && transform (op25, { Form::ri, 2 }) == Row { 0, 1, 10, 11, 8, 2, 7, 4, 9, 3, 5, 6 }
               && transform (op25, { Form::p, 3 })[0] == 7, "serial: p, i, r and ri");
        std::vector<Statement> plan;
        check (parseStatements ("p0 RI5, i11 r3", plan, error) && plan.size() == 4 && plan[1] == Statement { Form::ri, 5 }
               && text (plan) == "p0 ri5 i11 r3" && ! parseStatements ("p12", plan, error) && ! parseStatements ("x3", plan, error),
               "serial: the forms' labels");
        const auto m = matrix (op25);
        bool square = m[0] == op25;
        for (int k = 0; k < 12; ++k)
        {
            Row column;
            for (int i = 0; i < 12; ++i)
                column[(size_t) i] = m[(size_t) i][(size_t) k];
            square = square && isRow (std::vector<int> (m[(size_t) k].begin(), m[(size_t) k].end()))
                     && m[(size_t) k] == transform (op25, { Form::p, primeLevelOfRow (op25, k) })
                     && column == transform (op25, { Form::i, inversionLevelOfColumn (op25, k) });
        }
        check (square, "serial: the matrix, its rows the p forms and its columns the i forms");
        check (allInterval (rowNamed ("berg: lyric suite")) && ! allInterval (op25), "serial: the lyric suite's is an all-interval row");
        const auto op33a = rowNamed ("schoenberg: piano piece, op. 33a");
        const auto with = combinatorial (op33a);
        check (std::find (with.begin(), with.end(), Statement { Form::i, 5 }) != with.end() && describe (op33a).find ("inversional with i5") != std::string::npos,
               "serial: op. 33a is combinatorial with i5");
        bool partners = true;
        for (int f = 0; f < 4; ++f)
            for (int n = 0; n < 12; ++n)
            {
                const Statement st { (Form) f, n };
                const auto a = transform (op25, st), b = transform (op25, partner (op25, st));
                unsigned bits = 0;
                for (int k = 0; k < 6; ++k)
                    bits |= (1u << a[(size_t) k]) | (1u << b[(size_t) k]);
                partners = partners && bits == 0xFFFu && ! (partner (op25, st) == st);
            }
        check (partners, "serial: every form's partner completes its first hexachord");
        const auto path = walk (op25, 20, 3);
        bool steps = path.size() == 20 && path[0] == Statement { Form::p, 0 };
        for (size_t k = 1; k < path.size(); ++k)
            steps = steps && ! (path[k] == path[k - 1]);
        check (steps && walk (op25, 20, 3) == path && walk (op25, 20, 4) != path, "serial: a walk through the matrix, the same for a seed");
        bool chance = true;
        for (uint32_t seed = 1; seed < 20; ++seed)
        {
            const auto r = randomRow (seed), a = randomAllInterval (seed);
            chance = chance && isRow (std::vector<int> (r.begin(), r.end())) && isRow (std::vector<int> (a.begin(), a.end())) && allInterval (a);
        }
        check (chance, "serial: rows and all-interval rows by chance");
        int missing = 0;
        const auto taken = rowOf ({ 62, 60, 74, 64, 61 }, missing);
        check (missing == 8 && taken[0] == 2 && taken[1] == 0 && taken[2] == 4 && taken[3] == 1 && taken[4] == 3, "serial: a row from a melody");

        // A piece: every twelve notes of a line all twelve, in the range, the bars asked for.
        Settings s;
        s.row = op25;
        s.bars = 4;
        auto aggregates = [] (const std::vector<Note>& notes, size_t from, size_t count)
        {
            unsigned bits = 0;
            for (size_t k = from; k < from + count && k < notes.size(); ++k)
                bits |= 1u << (notes[k].pitch % 12);
            return bits == 0xFFFu;
        };
        auto one = compose (s);
        bool whole = one.error.empty() && one.voices.size() == 1 && one.voices[0].notes.size() == 24 && one.length == 4 * 1920;
        for (size_t k = 0; whole && k < one.voices[0].notes.size(); k += 12)
            whole = aggregates (one.voices[0].notes, k, 12);
        for (const auto& n : one.voices[0].notes)
            whole = whole && n.pitch >= s.low && n.pitch <= s.high && n.end > n.start;
        check (whole, "serial: whole rows, in the range, the bars asked for");
        const auto song = toSong (one.voices[0], one.length, 480, 100.0, 2);
        check (song->noteCount() == 24 && song->lastTick == one.length && song->channelsUsed == (1u << 2), "serial: a voice as a MIDI file that loops");
        s.plan = { { Form::ri, 3 }, { Form::i, 0 } };
        const auto planned = compose (s);
        check (planned.voices[0].statements == std::vector<Statement> { { Form::ri, 3 }, { Form::i, 0 } }
               && planned.voices[0].notes[0].pitch % 12 == transform (op25, { Form::ri, 3 })[0], "serial: the plan");

        // Canon and mirror canon: the comes is the dux later (inverted).
        s.plan.clear();
        s.voices = 2;
        s.relation = Voices::canon;
        s.delay = 4;
        const auto canon = compose (s);
        bool follows = canon.voices.size() == 2 && canon.voices[1].notes.size() == canon.voices[0].notes.size() && canon.voices[1].name == "comes";
        for (size_t k = 0; follows && k < canon.voices[0].notes.size(); ++k)
            follows = canon.voices[1].notes[k].start == canon.voices[0].notes[k].start + 480
                      && canon.voices[1].notes[k].pitch % 12 == canon.voices[0].notes[k].pitch % 12;
        check (follows && canon.voices[0].notes.back().pitch > 0, "serial: a canon");
        s.relation = Voices::mirror;
        const auto mirror = compose (s);
        bool mirrored = mirror.voices.size() == 2;
        for (size_t k = 0; mirrored && k < mirror.voices[0].statements.size(); ++k)
        {
            const auto a = mirror.voices[0].statements[k], b = mirror.voices[1].statements[k];
            mirrored = a.level == b.level && (a.form == Form::p ? b.form == Form::i : a.form == Form::i ? b.form == Form::p : a.form == Form::r ? b.form == Form::ri : b.form == Form::r);
        }
        check (mirrored, "serial: a mirror canon");
        // Combinatorial pairs: each hexachord with its complement at once.
        s.row = op33a;
        s.relation = Voices::combinatorial;
        const auto pair = compose (s);
        bool complete = pair.voices.size() == 2 && pair.voices[0].notes.size() == pair.voices[1].notes.size();
        for (size_t k = 0; complete && k < pair.voices[0].notes.size(); k += 6)
        {
            unsigned bits = 0;
            for (size_t j = k; j < k + 6; ++j)
                bits |= (1u << (pair.voices[0].notes[j].pitch % 12)) | (1u << (pair.voices[1].notes[j].pitch % 12));
            complete = bits == 0xFFFu && pair.voices[0].notes[k].start == pair.voices[1].notes[k].start;
        }
        check (complete, "serial: combinatorial pairs make aggregates");
        // Chords, serial durations and a frozen register.
        s.voices = 1;
        s.chord = 3;
        s.rhythm = Rhythm::serial;
        s.unit = 1;
        s.reg = Register::fixed;
        s.bars = 8;
        const auto chords = compose (s);
        const auto& notes = chords.voices[0].notes;
        bool stacked = ! notes.empty() && notes.size() % 12 == 0;
        int frozen[12];
        std::fill (std::begin (frozen), std::end (frozen), -1);
        for (size_t k = 0; stacked && k < notes.size(); ++k)
        {
            if (k % 3 != 0)
                stacked = notes[k].start == notes[k - k % 3].start;
            auto& f = frozen[notes[k].pitch % 12];
            stacked = stacked && (f < 0 || f == notes[k].pitch);
            f = notes[k].pitch;
        }
        // The first chord's length: its first note's place in p0, in sixteenths.
        const auto first = transform (op33a, chords.voices[0].statements[0])[0];
        const auto place = std::find (op33a.begin(), op33a.end(), first) - op33a.begin();
        check (stacked && notes[3].start - notes[0].start == 120 * (place + 1), "serial: trichords, serial durations and a frozen register");
        check (! compose ([&] { auto bad = s; bad.chord = 5; return bad; }()).error.empty()
               && ! compose ([&] { auto bad = s; bad.low = 60; bad.high = 66; return bad; }()).error.empty(), "serial: what cannot be written");
        check (compose (s).commentary == chords.commentary && ! chords.commentary.empty(), "serial: the same seed, the same piece");

        // Babbitt. Time points: each note on its pitch class's twelfth of the modulus (12 sixteenths here), the next time it comes round.
        Settings tp;
        tp.row = op25;
        tp.rhythm = Rhythm::timePoint;
        tp.unit = 1;
        tp.bars = 3;
        const auto points = compose (tp);
        bool onPoints = ! points.voices[0].notes.empty() && points.error.empty();
        int64_t lastStart = -1;
        for (const auto& n : points.voices[0].notes)
        {
            onPoints = onPoints && (n.start / 120) % 12 == (n.pitch - op25[0] + 120) % 12 && n.start > lastStart;
            lastStart = n.start;
        }
        check (onPoints && points.voices[0].notes[0].start == 0 && points.length >= 3 * 1920 && points.length % 1920 == 0,
               "serial: babbitt's time points");
        // Derived rows: the cell's forms fill out the aggregate.
        Row derivedRow {};
        bool cells = derive ({ 0, 1, 3 }, 5, derivedRow, error) && isRow (std::vector<int> (derivedRow.begin(), derivedRow.end()))
                     && derivedRow[0] == 0 && derivedRow[1] == 1 && derivedRow[2] == 3;
        for (int k = 0; cells && k < 12; k += 3)
        {
            // Every trichord of it is the cell's shape: {0, 1, 3} or its inversion {0, 2, 3}, transposed.
            std::array<int, 3> t { derivedRow[(size_t) k], derivedRow[(size_t) k + 1], derivedRow[(size_t) k + 2] };
            std::sort (t.begin(), t.end());
            bool shaped = false;
            for (int r = 0; r < 3 && ! shaped; ++r)
            {
                const auto a = (t[(size_t) (r + 1) % 3] - t[(size_t) r] + 12) % 12, b = (t[(size_t) (r + 2) % 3] - t[(size_t) r] + 12) % 12;
                shaped = (a == 1 && b == 3) || (a == 2 && b == 3);
            }
            cells = shaped;
        }
        Row tetra {};
        check (cells && derive ({ 0, 1, 2, 3 }, 1, tetra, error) && ! derive ({ 0, 1 }, 1, tetra, error) && ! derive ({ 0, 4, 4 }, 1, tetra, error),
               "serial: babbitt's derived rows");
        // Source sets: a row on one is combinatorial in all four forms.
        bool sets = true;
        for (int set = 0; set < 6; ++set)
        {
            const auto r = fromSourceSet (set, 9);
            sets = sets && sourceSetOf (r) == set && describe (r).find ("all-combinatorial") != std::string::npos;
        }
        check (sets && sourceSetOf (op25) == -1 && sourceSets()[5].pcs[5] == 10, "serial: babbitt's all-combinatorial source sets");
        // Arrays: every block all twelve notes across the voices, each voice a segment of it.
        Settings ar;
        ar.row = op25;
        ar.voices = 4;
        ar.relation = Voices::array;
        ar.bars = 4;
        const auto arr = compose (ar);
        bool blocks = arr.voices.size() == 4 && arr.error.empty() && arr.voices[0].name == "lyne 1";
        for (size_t b = 0; blocks && b < arr.voices[0].statements.size(); ++b)
        {
            unsigned bits = 0;
            for (const auto& v : arr.voices)
                for (size_t k = b * 3; k < b * 3 + 3; ++k)
                    bits |= 1u << (v.notes[k].pitch % 12);
            blocks = bits == 0xFFFu && arr.voices[0].notes[b * 3].start == arr.voices[3].notes[b * 3].start;
        }
        // Block 0 gives voice 1 the first trichord, block 1 the second.
        const auto second = transform (op25, arr.voices[0].statements[1]);
        check (blocks && arr.voices[0].statements.size() >= 2 && arr.voices[0].notes[0].pitch % 12 == transform (op25, arr.voices[0].statements[0])[0]
               && arr.voices[0].notes[3].pitch % 12 == second[3], "serial: babbitt's arrays");

        // A fugue on a twelve-tone subject answers it at the fifth exactly, even when a tonal answer is asked for.
        counterpoint::Line subject;
        for (int k = 0; k < 12; ++k)
            subject.push_back ({ k * 480, k * 480 + 480, 60 + op25[(size_t) k], 90 });
        fugue::Settings f;
        f.voices = 2;
        f.answer = fugue::Answer::tonal;
        f.mode = counterpoint::Mode::chromatic ((op25[0] + 5) % 12);   // the subject begins on the dominant, which a tonal answer answers a fourth up
        const auto fg = fugue::generate (subject, 480, f);
        bool real = fg.plan.size() > 1 && fg.plan[1].what == "answer";
        if (real)
        {
            const auto& voice = fg.voices[(size_t) fg.plan[1].voice];
            std::vector<int> heard;
            for (const auto& x : voice)
                if (x.start >= fg.plan[1].start && x.start < fg.plan[1].end)
                    heard.push_back (x.pitch % 12);
            real = heard.size() == 12;
            for (size_t k = 0; real && k < 12; ++k)
                real = heard[k] == (op25[k] + 7) % 12;
        }
        check (real, "serial: the fugue answers a twelve-tone subject real");
    }

    void testStochastic()
    {
        using namespace stochastic;
        std::string error;
        // Xenakis's major scale (Sieves, 1990), and its analysis back into residue classes.
        const auto major = Sieve::parse ("(-3@2 & 4@0) | (-3@1 & 4@1) | (3@2 & 4@2) | (-3@0 & 4@3)", error);
        check (error.empty() && major.period() == 12, "stochastic: a sieve reads, period 12");
        check (major.points (0, 12) == std::vector<int64_t> { 0, 2, 4, 5, 7, 9, 11 }, "stochastic: the sieve is the major scale");
        const auto back = Sieve::fromPoints ({ 0, 2, 4, 5, 7, 9, 11 }, 12);
        check (back.points (-12, 24) == major.points (-12, 24), "stochastic: the analysis gives the same set (" + back.text() + ")");
        check (Sieve::fromKeys (0xFFF).text() == "1@0" && Sieve::fromKeys (0x555).text() == "2@0", "stochastic: the simplest sieves");
        for (const char* bad : { "3@", "(2@0", "2@0 |", "0@0", "x" })
            check (Sieve::parse (bad, error).empty() && ! error.empty(), std::string ("stochastic: \"") + bad + "\" does not read");
        check (Sieve::parse ("-2@0", error).contains (-3), "stochastic: the complement, and negative numbers");

        // Every rhythmic mode turns into a sieve that gives back its onsets.
        for (const auto& r : rhythm::rhythms())
        {
            const auto sieve = sieveFromRhythm (r);
            const auto rows = rhythm::rowsPerBeat (r);
            std::set<int64_t> onsets;
            for (const auto& h : rhythm::hits (r))
                onsets.insert (h.step * rows / r.lanes[(size_t) h.lane].perBeat);
            const auto points = sieve.points (0, (int64_t) r.beats * rows);
            check (std::vector<int64_t> (onsets.begin(), onsets.end()) == points, "stochastic: " + r.plain + " as a sieve (" + sieve.text() + ")");
        }
        const auto son = rhythm::findRhythm ("son clave 3-2");
        if (son >= 0)
            check (sieveFromRhythm (rhythm::rhythms()[(size_t) son]).text() == "16@0 | 16@3 | 16@6 | 16@10 | 16@12", "stochastic: the son clave's sieve");

        // The laws: centred where they are asked, the linear law's mean distance a third of its width.
        for (int l = 0; l < numLaws; ++l)
        {
            Random rng (7);
            double sum = 0;
            for (int i = 0; i < 20000; ++i)
                sum += draw ((Law) l, 5.0, 1.0, rng);
            check (std::abs (sum / 20000 - 5.0) < 0.1, std::string ("stochastic: the ") + lawName ((Law) l) + " law is centred");
        }
        {
            Random rng (3);
            double distance = 0;
            for (int i = 0; i < 20000; ++i)
                distance += std::abs (draw (Law::linear, 0.0, 12.0, rng));
            check (std::abs (distance / 20000 - 4.0) < 0.2, "stochastic: the linear law of intervals, mean a/3");
        }

        for (auto method : { Method::cloud, Method::screens })
        {
            Settings s;
            s.method = method;
            s.length = 480 * 64;
            s.density = 2.0;
            s.low = 50;
            s.high = 80;
            s.root = 2;
            s.pitches = major;   // D major
            s.seed = 11;
            const auto a = generate (s);
            const auto again = generate (s);
            check (a.error.empty() && a.notes.size() > 40 && a.notes.size() < 400, "stochastic: a line of about the density (" + std::to_string (a.notes.size()) + ")");
            check (a.notes.size() == again.notes.size() && std::equal (a.notes.begin(), a.notes.end(), again.notes.begin(),
                   [] (const Note& x, const Note& y) { return x.start == y.start && x.end == y.end && x.pitch == y.pitch && x.velocity == y.velocity; }),
                   "stochastic: the same seed draws the same line");
            bool inside = true;
            for (size_t i = 0; i < a.notes.size(); ++i)
            {
                const auto& n = a.notes[i];
                inside = inside && n.pitch >= 50 && n.pitch <= 80 && major.contains (n.pitch - 2) && n.start >= 0 && n.end <= s.length
                      && n.end > n.start && n.velocity >= 1 && n.velocity <= 127 && (i == 0 || a.notes[i - 1].start <= n.start);
                for (size_t j = 0; j < i; ++j)   // a key struck again is let go first
                    inside = inside && ! (a.notes[j].pitch == n.pitch && a.notes[j].end > n.start);
            }
            check (inside, "stochastic: every note in range, in the sieve, in time and let go before it is struck again");
            check ((method == Method::screens) == ! a.screens.empty(), "stochastic: the screens are named");

            // On a grid: only its places.
            s.grid = gridFromSieve (Sieve::parse ("16@0 | 16@3 | 16@6 | 16@10 | 16@12", error), 120, s.length);
            check (s.grid.size() == 5 * 16, "stochastic: the clave's grid over 16 bars");
            const auto g = generate (s);
            bool onGrid = ! g.notes.empty();
            for (const auto& n : g.notes)
                onGrid = onGrid && std::binary_search (s.grid.begin(), s.grid.end(), n.start);
            check (onGrid, "stochastic: notes start only on the grid");
        }
        Settings none;
        none.low = 61;
        none.high = 61;
        none.pitches = major;
        check (! generate (none).error.empty(), "stochastic: a sieve with no key in range says so");

        // A MIDI file's own notes and onsets as sieves, and the line back as a MIDI file.
        midifile::Song song;
        for (int i = 0; i < 4; ++i)
        {
            song.events.push_back ({ i * 480, 0x90, (uint8_t) (60 + 4 * (i % 2)), 100 });
            song.events.push_back ({ i * 480 + 240, 0x80, (uint8_t) (60 + 4 * (i % 2)), 0 });
        }
        check (pitchSieveOf (song, 0).points (0, 12) == std::vector<int64_t> { 0, 4 }, "stochastic: the clip's notes as a pitch sieve");
        check (rhythmSieveOf (song, 120, 16).text() == "4@0", "stochastic: the clip's onsets as a rhythm sieve");
        Settings small;
        small.seed = 4;
        const auto line = generate (small);
        const auto written = toSong (song, line.notes, 2);
        check (written->noteCount() == (int) line.notes.size() && written->channelsUsed == (1u << 2), "stochastic: the line as a MIDI file");
    }

    void testPluginMeasure()
    {
        // A wire: flat, no delay, no distortion, no aliasing, no compression.
        {
            TestDevice wire;
            wire.fn = [] (float*, float*, int) {};
            const auto r = measure::run (wire, quickSettings());
            double worst = 0.0;
            for (std::size_t i = 0; i < r.hz.size(); ++i)
                if (r.hz[i] > 30.0 && r.hz[i] < 20000.0)
                    worst = std::max (worst, std::abs (r.magnitudeDb[0][i]));
            check (r.hasLinear && worst < 0.1, "measure: a wire is flat within 0.1 dB (" + std::to_string (worst) + ")");
            check (std::abs (r.measuredLatency) < 0.5, "measure: a wire has no latency");
            check (r.thdPercent[0] < 0.001 && r.alias[1].aliasDb[0] < -120.0, "measure: a wire adds no harmonics or aliasing");
            check (std::abs (r.ratio - 1.0) < 0.05 && ! r.gated && r.modulationDepthDb < 0.2, "measure: a wire neither compresses nor moves");
        }

        // A delay that says so: found where it says.
        {
            TestDevice delay;
            std::array<std::vector<float>, 2> line { std::vector<float> (100, 0.0f), std::vector<float> (100, 0.0f) };
            std::size_t at = 0;
            delay.latency = 100;
            delay.clear = [&] { for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f); at = 0; };
            delay.fn = [&] (float* l, float* r, int n)
            {
                for (int i = 0; i < n; ++i)
                {
                    std::swap (line[0][at], l[i]);
                    std::swap (line[1][at], r[i]);
                    at = (at + 1) % 100;
                }
            };
            auto s = quickSettings();
            s.harmonics = s.aliasing = s.dynamics = s.modulation = false;
            const auto r = measure::run (delay, s);
            check (std::abs (r.measuredLatency - 100.0) < 0.5, "measure: a 100-sample delay peaks at 100 (" + std::to_string (r.measuredLatency) + ")");
            check (r.notes.empty(), "measure: a delay that reports its latency gets no warning");
        }

        // A bell of +6 dB at 1 kHz.
        {
            TestDevice bell;
            // RBJ's peaking EQ.
            struct Peaking
            {
                double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
                Peaking()
                {
                    const auto A = std::pow (10.0, 6.0 / 40.0), w = 2.0 * 3.14159265358979 * 1000.0 / 48000.0, alpha = std::sin (w) / 2.0;
                    const auto a0 = 1 + alpha / A;
                    b0 = (1 + alpha * A) / a0; b1 = -2 * std::cos (w) / a0; b2 = (1 - alpha * A) / a0;
                    a1 = -2 * std::cos (w) / a0; a2 = (1 - alpha / A) / a0;
                }
                float process (float x)
                {
                    const auto y = b0 * x + z1;
                    z1 = b1 * x - a1 * y + z2;
                    z2 = b2 * x - a2 * y;
                    return (float) y;
                }
                void reset() { z1 = z2 = 0; }
            };
            Peaking filters[2];
            bell.clear = [&] { for (auto& f : filters) f.reset(); };
            bell.fn = [&] (float* l, float* r, int n)
            {
                for (int i = 0; i < n; ++i)
                {
                    l[i] = filters[0].process (l[i]);
                    r[i] = filters[1].process (r[i]);
                }
            };
            auto s = quickSettings();
            s.harmonics = s.aliasing = s.dynamics = s.modulation = false;
            const auto r = measure::run (bell, s);
            const auto it = std::lower_bound (r.hz.begin(), r.hz.end(), 1000.0);
            const auto db = r.magnitudeDb[0][(std::size_t) (it - r.hz.begin())];
            check (std::abs (db - 6.0) < 0.3, "measure: a +6 dB bell reads +6 dB at 1 kHz (" + std::to_string (db) + ")");
            const auto features = forensic::features (r);
            const auto peak = std::find_if (features.begin(), features.end(), [] (const forensic::Feature& f) { return f.group == "frequency"; });
            check (peak != features.end() && std::abs (peak->value - 1000.0) < 10.0, "forensic: finds the bell's peak at 1 kHz");
        }

        // A hard saturator with no oversampling: harmonics, odd ones above even, and aliasing at 10 kHz.
        {
            TestDevice drive;
            drive.fn = [] (float* l, float* r, int n)
            {
                for (int i = 0; i < n; ++i)
                {
                    l[i] = std::tanh (8.0f * l[i]);
                    r[i] = std::tanh (8.0f * r[i]);
                }
            };
            auto s = quickSettings();
            s.linear = s.dynamics = s.modulation = false;
            const auto r = measure::run (drive, s);
            check (r.thdPercent[0] > 10.0 && r.harmonicDb[0][2] > r.harmonicDb[0][1] + 20.0, "measure: tanh makes odd harmonics, not even ones");
            check (r.alias[1].aliasDb[0] > -60.0 && r.alias[0].aliasDb[0] < r.alias[1].aliasDb[0], "measure: tanh aliases, more at 10 kHz than 1 kHz");
        }

        // A compressor of 4:1 above -20 dBFS (instant, on the peak level of the tone).
        {
            TestDevice comp;
            double envelope = 0.0;
            comp.clear = [&] { envelope = 0.0; };
            comp.fn = [&] (float* l, float* r, int n)
            {
                for (int i = 0; i < n; ++i)
                {
                    const auto x = std::max (std::abs (l[i]), std::abs (r[i]));
                    envelope = std::max ((double) x, envelope * 0.9995);   // peak hold, slow fall
                    const auto db = 20.0 * std::log10 (std::max (envelope, 1.0e-9));
                    const auto over = std::max (0.0, db + 20.0);
                    const auto gain = std::pow (10.0, -(over - over / 4.0) / 20.0);
                    l[i] = (float) (l[i] * gain);
                    r[i] = (float) (r[i] * gain);
                }
            };
            auto s = quickSettings();
            s.linear = s.harmonics = s.aliasing = s.modulation = false;
            const auto r = measure::run (comp, s);
            check (std::abs (r.ratio - 4.0) < 0.4 && std::abs (r.thresholdDb + 20.0) <= 4.0,
                   "measure: a 4:1 compressor reads 4:1 at -20 dBFS (" + std::to_string (r.ratio) + ", " + std::to_string (r.thresholdDb) + ")");
        }

        // A 2 Hz tremolo.
        {
            TestDevice tremolo;
            std::int64_t t = 0;
            tremolo.clear = [&] { t = 0; };
            tremolo.fn = [&] (float* l, float* r, int n)
            {
                for (int i = 0; i < n; ++i, ++t)
                {
                    const auto g = (float) (0.75 + 0.25 * std::sin (2.0 * 3.14159265358979 * 2.0 * (double) t / 48000.0));
                    l[i] *= g;
                    r[i] *= g;
                }
            };
            auto s = quickSettings();
            s.linear = s.harmonics = s.aliasing = s.dynamics = false;
            const auto r = measure::run (tremolo, s);
            check (! r.modulationHz.empty() && std::abs (r.modulationHz.front() - 2.0) < 0.05, "measure: finds a 2 Hz tremolo");
        }

        // Echoes at 100 ms × φⁿ, each 1/φ quieter: forensic Φ should be sure. At 100 ms × 1.5ⁿ it should not.
        auto echoes = [] (double base, double spacing, double fall)
        {
            auto device = std::make_shared<TestDevice>();
            auto line = std::make_shared<std::vector<float>> (48000 * 2, 0.0f);
            auto at = std::make_shared<std::size_t> (0);
            device->clear = [line, at] { std::fill (line->begin(), line->end(), 0.0f); *at = 0; };
            device->fn = [=] (float* l, float* r, int n)
            {
                const auto size = line->size();
                for (int i = 0; i < n; ++i)
                {
                    (*line)[*at] = 0.5f * (l[i] + r[i]);
                    float out = 0.0f, gain = 1.0f;
                    double time = base;
                    for (int tap = 0; tap < 4; ++tap, time *= spacing, gain *= (float) fall)
                        out += gain * (*line)[(*at + size - (std::size_t) std::lround (time * 48.0)) % size];
                    l[i] = r[i] = out;
                    *at = (*at + 1) % size;
                }
            };
            return device;
        };
        {
            auto golden = echoes (100.0, phi::ratio, phi::inverse);
            auto s = quickSettings();
            s.harmonics = s.aliasing = s.dynamics = s.modulation = false;
            s.tailSeconds = 1.0;
            const auto report = forensic::analyse (measure::run (*golden, s));
            check (report.verdict == forensic::Verdict::strong, "forensic: golden echoes are strong evidence (" + report.summary + ")");
        }
        {
            auto plain = echoes (100.0, 1.5, 0.5);
            auto s = quickSettings();
            s.harmonics = s.aliasing = s.dynamics = s.modulation = false;
            s.tailSeconds = 0.8;
            const auto report = forensic::analyse (measure::run (*plain, s));
            check (report.verdict != forensic::Verdict::strong && report.verdict != forensic::Verdict::suggestive,
                   "forensic: echoes 3:2 apart are no evidence of φ (" + report.summary + ")");
        }
        {
            // Bands far apart: a cut at 80 Hz × φ² and a boost at 80 Hz × φ¹⁰ are φ⁸ (47) apart.
            const std::vector<forensic::Feature> features { { "frequency", "Notch at 209.4 Hz", 209.44, 0.003, "Hz" },
                                                            { "frequency", "Peak at 9836 Hz", 9839.2, 0.003, "Hz" } };
            const auto report = forensic::analyse (features);
            check (report.matches.size() == 1 && report.matches.front().target == "φ⁸", "forensic: finds golden ratios wider than φ⁶");
        }
    }

    // The splash's sound: phase distortion, a patch per start, a granular reverb with a long tail.
    void testPhiVoice()
    {
        // No distortion is a cosine; full distortion bends it but keeps it within ±1.
        bool cosine = true, bounded = true;
        for (int w = 0; w < (int) phi::PdWave::count; ++w)
            for (int i = 0; i < 200; ++i)
            {
                const auto p = i / 200.0;
                cosine = cosine && near (phi::pdWave ((phi::PdWave) w, p, 0.0), std::cos (6.283185307179586 * p), 1.0e-9);
                for (double d : { 0.5, 1.0 })
                    bounded = bounded && std::abs (phi::pdWave ((phi::PdWave) w, p, d)) <= 1.0 + 1.0e-9;
            }
        check (cosine, "phase distortion: no distortion is a cosine");
        check (bounded, "phase distortion: every wave stays within full scale");
        check (phi::pdWave (phi::PdWave::saw, 0.1, 1.0) < phi::pdWave (phi::PdWave::saw, 0.1, 0.0), "phase distortion: the saw falls sooner when bent");

        // Successive starts get different patches, all within the space.
        {
            const auto a = phi::PdPatch::at (0, 0.3), b = phi::PdPatch::at (1, 0.3), c = phi::PdPatch::at (0, 0.3);
            bool inside = true;
            double moved = 0.0;
            for (int i = 0; i < phi::PdPatch::dimensions; ++i)
            {
                inside = inside && a.values[(size_t) i] >= 0.0 && a.values[(size_t) i] < 1.0;
                moved += std::abs (a.values[(size_t) i] - b.values[(size_t) i]);
            }
            check (inside && moved > 1.0 && a.values == c.values, "pd patch: each start a new point, the same for the same start");
        }

        // The reverb's tail: a short burst still rings 1.5 s later and has faded 60 dB by 2.5 s.
        {
            phi::GranularReverb reverb;
            reverb.prepare (48000.0);
            reverb.setMix (1.0);
            std::vector<float> left (48000 * 4), right (48000 * 4);
            for (int i = 0; i < 2400; ++i)
                left[(size_t) i] = right[(size_t) i] = 0.5f * (float) std::sin (i * 0.05);
            reverb.process (left.data(), right.data(), (int) left.size());
            const auto levelDb = [&] (double from, double to)
            {
                double e = 0.0;
                const auto a = (size_t) (from * 48000.0), b = (size_t) (to * 48000.0);
                for (size_t i = a; i < b; ++i)
                    e += left[i] * left[i] + right[i] * right[i];
                return 10.0 * std::log10 (e / (double) (b - a) + 1.0e-30);
            };
            const auto early = levelDb (0.25, 0.5), late = levelDb (1.5, 1.75), gone = levelDb (2.75, 3.0);
            check (late > early - 40.0, "granular reverb: the tail still rings after 1.5 s");
            check (gone < early - 45.0, "granular reverb: the tail fades within about 2 s");
        }

        // The whole sound: ends by itself, never clips, and is not silent.
        for (int launch = 0; launch < 6; ++launch)
        {
            phi::StartupSound sound (launch, 0.7);
            sound.prepare (48000.0);
            std::vector<float> left (48000 * 7), right (48000 * 7);
            int at = 0;
            while (at + 512 <= (int) left.size() && sound.render (left.data() + at, right.data() + at, 512))
                at += 512;
            float peak = 0.0f;
            for (size_t i = 0; i < left.size(); ++i)
                peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
            check (at < (int) left.size() - 512 && near (at / 48000.0, sound.lengthSeconds(), 0.05), "startup sound: ends after its length");
            check (peak > 0.05f && peak < 0.9f, "startup sound: heard and not clipping");
            check (std::abs (left.back()) < 1.0e-6f, "startup sound: silent at the end");
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

        // The Φ random selector: each colour spreads evenly over its favourites, never deals
        // the same one twice running (with three or more), and the colours don't move in
        // lockstep, so every deal of 8 colours x 8 favourites is a new combination.
        {
            bool even = true, noRepeat = true, inRange = true;
            for (int count = 1; count <= 8; ++count)
                for (int colour = 0; colour < 8; ++colour)
                {
                    std::vector<int> hits ((size_t) count, 0);
                    for (int n = 0; n < 89; ++n)
                    {
                        const auto d = phi::deal (n, colour, count, 0.3);
                        inRange = inRange && d >= 0 && d < count;
                        if (d >= 0 && d < count)
                            ++hits[(size_t) d];
                        if (count >= 3 && d == phi::deal (n + 1, colour, count, 0.3))
                            noRepeat = false;
                    }
                    for (const auto h : hits)
                        even = even && std::abs (h - 89.0 / count) < 4.0;
                }
            std::set<std::vector<int>> combinations;
            for (int n = 0; n < 1000; ++n)
            {
                std::vector<int> c;
                for (int colour = 0; colour < 8; ++colour)
                    c.push_back (phi::deal (n, colour, 8, 0.3));
                combinations.insert (c);
            }
            check (inRange && phi::deal (5, 0, 0) == 0, "phi deal stays in range");
            check (even, "phi deal spreads evenly over the favourites");
            check (noRepeat, "phi deal never repeats a favourite twice running");
            check (combinations.size() == 1000, "phi deal gives a new combination every deal");
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

        // The Φ EQ's grid, types, cuts and tilt.
        {
            check (near (phi::Eq::stepHz (4), phi::Eq::bandHz (1), 1.0e-9) && near (phi::Eq::stepHz (1) / phi::Eq::stepHz (0), std::pow (phi::ratio, 0.25), 1.0e-9)
                   && phi::Eq::nearestStep (1000.0) == phi::Eq::nearestStep (phi::Eq::stepHz (phi::Eq::nearestStep (1000.0)))
                   && phi::Eq::nearestStep (1.0e6) == phi::Eq::maxStep && phi::Eq::nearestStep (0.0) == phi::Eq::minStep,
                   "phi EQ grid is quarter golden steps from 80 Hz");
            check (near (phi::Eq::stepQ (2, false) / phi::Eq::stepQ (0, false), phi::ratio, 1.0e-9), "phi EQ widths are golden steps");

            auto response = [&] (const phi::Eq::Settings& s, double hz)
            {
                phi::Eq e;
                e.prepare (rate, 1);
                e.set (s);
                return e.responseDb (hz);
            };
            phi::Eq::Settings s;
            s.bands[5].type = phi::Eq::Type::notch;
            check (response (s, phi::Eq::bandHz (5)) < -40.0 && std::abs (response (s, phi::Eq::bandHz (2))) < 0.5, "phi EQ notch cuts only its frequency");
            s = {};
            s.bands[5].type = phi::Eq::Type::bandPass;
            check (near (response (s, phi::Eq::bandHz (5)), 0.0, 0.05) && response (s, phi::Eq::bandHz (1)) < -12.0, "phi EQ band-pass keeps only its band");
            s = {};
            s.bands[0].gainDb = 6.0;
            check (near (response (s, 25.0), 6.0, 0.3) && std::abs (response (s, 2000.0)) < 0.2, "phi EQ low shelf lifts the lows");
            s = {};
            s.bands[3].type = phi::Eq::Type::off;
            s.bands[3].gainDb = 9.0;
            check (std::abs (response (s, phi::Eq::bandHz (3))) < 1.0e-6, "phi EQ band switched off does nothing");

            for (const auto slope : { 12, 24, 48 })
            {
                s = {};
                s.lowCutHz = 200.0;
                s.lowCutSlope = slope;
                const auto octave = response (s, 100.0) - response (s, 50.0);
                check (near (response (s, 200.0), -3.0, 0.1) && near (octave, (double) slope, slope * 0.1) && std::abs (response (s, 5000.0)) < 0.05,
                       "phi EQ low cut is " + std::to_string (slope) + " dB an octave");
                s = {};
                s.highCutHz = 2000.0;
                s.highCutSlope = slope;
                check (near (response (s, 2000.0), -3.0, 0.1) && response (s, 8000.0) < -1.6 * slope && std::abs (response (s, 50.0)) < 0.05,
                       "phi EQ high cut is " + std::to_string (slope) + " dB an octave");
            }

            s = {};
            s.tiltDb = 6.0;
            check (near (response (s, 20.0), -3.0, 0.3) && near (response (s, 18000.0), 3.0, 0.4) && std::abs (response (s, 1000.0)) < 0.1,
                   "phi EQ tilt turns about 1 kHz");

            // Moving one band leaves the others' sound running: no stage restarts but its own.
            phi::Eq e;
            e.prepare (rate, 1);
            s = {};
            s.bands[2].gainDb = 6.0;
            e.set (s);
            auto tone = sine (phi::Eq::bandHz (2), 9600);
            float* t[] { tone.data() };
            e.process (t, 1, 4800);
            s.bands[8].gainDb = 3.0;
            e.set (s);
            float* rest[] { tone.data() + 4800 };
            e.process (rest, 1, 4800);
            float jump = 0.0f;
            for (int i = 4790; i < 4810; ++i)
                jump = std::max (jump, std::abs (tone[(size_t) i + 1] - tone[(size_t) i]));
            check (jump < 0.2f, "phi EQ changes one band without a click in another");
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

            // Its window's picture: the cut is deepest at the resonance, and the tone stands out there.
            {
                phi::Deresonator r;
                phi::Deresonator::Settings t;
                t.depth = 1.0;
                r.prepare (rate, 1);
                r.set (t);
                auto y = ringing;
                float* p[] { y.data() };
                r.process (p, 1, (int) y.size());
                std::array<float, phi::Deresonator::pictureSize> level {}, cut {};
                r.picture (level.data(), cut.data());
                const auto deepest = (int) (std::min_element (cut.begin(), cut.end()) - cut.begin());
                const auto loudest = (int) (std::max_element (level.begin(), level.end()) - level.begin());
                check (std::abs (std::log2 (phi::Deresonator::pictureHz (deepest) / 1000.0)) < 0.15 && cut[(size_t) deepest] < -3.0f,
                       "phi de-resonator's picture shows its cut at the resonance");
                check (std::abs (std::log2 (phi::Deresonator::pictureHz (loudest) / 1000.0)) < 0.15 && level[(size_t) loudest] > -30.0f
                           && level[(size_t) loudest] < 0.0f,
                       "phi de-resonator's picture shows the tone's level in dB full scale");
            }
        }
    }

    void testMixAssist()
    {
        using mixassist::Role;
        check (mixassist::guessRole ("Lead Vox") == Role::leadVocal, "mix assist: lead vox");
        check (mixassist::guessRole ("BV 2") == Role::vocal, "mix assist: backing vocals");
        check (mixassist::guessRole ("Vocals") == Role::leadVocal, "mix assist: a lone vocal leads");
        check (mixassist::guessRole ("KICK") == Role::kick, "mix assist: kick");
        check (mixassist::guessRole ("BA") == Role::bass, "mix assist: BA is bass");
        check (mixassist::guessRole ("DR") == Role::drums, "mix assist: DR is drums");
        check (mixassist::guessRole ("Gtr L") == Role::guitar, "mix assist: guitar");
        check (mixassist::guessRole ("Strings") == Role::pad, "mix assist: strings are a pad");
        check (mixassist::guessRole ("Track 3") == Role::other, "mix assist: an unknown name");
        check (mixassist::roleOffsetDb (Role::leadVocal) > mixassist::roleOffsetDb (Role::pad), "mix assist: the lead above a pad");

        // Pan laws as the mixer has them.
        mixassist::Track mono, stereo;
        mono.monoAudio = true;
        double l = 0.0, r = 0.0;
        mixassist::panGains (mono, 0.0, l, r);
        check (near (l, std::sqrt (0.5), 1.0e-9) && near (r, std::sqrt (0.5), 1.0e-9), "mix assist: mono audio is equal power");
        mixassist::panGains (stereo, 0.0, l, r);
        check (l == 1.0 && r == 1.0, "mix assist: the rest is balance");
        mixassist::panGains (stereo, -1.0, l, r);
        check (l == 1.0 && r == 0.0, "mix assist: balance hard left");

        auto track = [] (const char* name, double freq, double amp, bool monoAudio = false, double seconds = 40.0)
        {
            mixassist::Track t;
            t.name = name;
            t.role = mixassist::guessRole (name);
            t.monoAudio = monoAudio;
            t.stem = makeSine (2, (Frame) (48000 * seconds), freq, amp);
            return t;
        };

        // Mixing: a mono sine at the centre is 3 dB down on each side.
        {
            std::vector<mixassist::Track> one { track ("Kick", 60.0, 0.5, true) };
            const auto m = mixassist::mix (one, { 0.0 }, { 0.0 });
            check (near (m->peakAbs (0, m->length()), 0.5f * std::sqrt (0.5f), 0.01), "mix assist: a mono stem mixes at equal power");
        }

        // Pans by rule: kick and bass in the middle, the rest spread both ways, a panned track kept.
        {
            std::vector<mixassist::Track> ts { track ("Kick", 60.0, 0.3, true), track ("Keys", 2000.0, 0.2, true),
                                               track ("Gtr", 1000.0, 0.2, true), track ("Pad", 500.0, 0.2, true) };
            ts[3].pan = -0.4;
            std::vector<assist::Profile> ps;
            for (auto& t : ts)
                ps.push_back (assist::measure (*t.stem, 0, t.stem->length()));
            const auto pans = mixassist::rulePans (ts, ps, true);
            check (pans[0] == 0.0, "mix assist: the kick stays in the middle");
            check (pans[1] * pans[2] < 0.0, "mix assist: two others go to opposite sides");
            check (std::abs (pans[1]) >= std::abs (pans[2]), "mix assist: the brighter goes wider");
            check (pans[3] == -0.4, "mix assist: a track you panned keeps its pan");
        }

        // A whole suggestion: the vocal far too quiet, the pad too loud, the mix too hot.
        {
            std::vector<mixassist::Track> ts { track ("Lead Vox", 1000.0, 0.05, true), track ("Kick", 60.0, 0.4, true),
                                               track ("Bass", 120.0, 0.3, true), track ("Pad", 500.0, 0.5),
                                               track ("Gtr", 1000.0, 0.05, true, 30.0) };
            mixassist::Settings settings;
            const auto s = mixassist::suggest (ts, settings);
            check (s.faderDb.size() == ts.size() && s.evaluations > 0, "mix assist: a fader for every track");
            check (s.after.valid && near (s.after.lufs, settings.mixLufs, 1.5), "mix assist: the mix lands near its loudness");
            check (s.after.truePeakDb <= settings.ceilingDb + 0.5, "mix assist: the mix keeps its headroom");
            check (s.levelAfter[0] - s.levelAfter[3] > s.levelBefore[0] - s.levelBefore[3] + 10.0, "mix assist: the vocal comes up over the pad");
            double worst = 0.0;
            for (size_t i = 0; i < ts.size(); ++i)
                worst = std::max (worst, std::abs (s.levelAfter[i] - s.levelAim[i]));
            check (worst < 2.0, "mix assist: every track near the level its role asks for");
            check (s.pan[0] == 0.0 && s.pan[1] == 0.0 && s.pan[2] == 0.0, "mix assist: vocal, kick and bass centred");
            const auto overlap = std::find_if (s.overlaps.begin(), s.overlaps.end(), [] (const mixassist::Overlap& o) { return o.hidden == 0 && o.hiding == 4; });
            check (overlap != s.overlaps.end() && overlap->band == 5, "mix assist: the guitar hides the vocal at 1 kHz");

            // A fader you lock stays.
            ts[3].moveFader = false;
            const auto locked = mixassist::suggest (ts, settings);
            check (locked.faderDb[3] == 0.0, "mix assist: a locked fader stays");
        }
    }

    // The Mode menu's rhythmic modes: every pattern the length of its cycle, and each a MIDI file
    // that reads back with its notes; the tāls' and tāḷams' structures as the sources give them.
    void testRhythms()
    {
        const auto& all = rhythm::rhythms();
        check (all.size() > 100, "rhythms: the library is there (" + std::to_string (all.size()) + ")");
        for (const auto& r : all)
        {
            check (rhythm::valid (r), "rhythm " + r.plain + ": every lane is one cycle long, the sections add up");
            for (const auto c : r.name)
                check (! (c >= 'A' && c <= 'Z'), "rhythm " + r.plain + ": the name is lower case");
            check (std::count_if (all.begin(), all.end(), [&] (const rhythm::Rhythm& o) { return o.plain == r.plain; }) == 1,
                   "rhythm " + r.plain + ": its plain name is its own");
            const auto song = rhythm::toSong (r, 2, r.bpm);
            std::string error;
            const auto bytes = midifile::bytes (*song);
            const auto back = midifile::parse (bytes.data(), bytes.size(), error);
            check (back != nullptr && back->noteCount() == song->noteCount() && song->noteCount() > 0,
                   "rhythm " + r.plain + ": a MIDI file with its notes");
            check (song->lastTick == 2 * r.beats * 480, "rhythm " + r.plain + ": two cycles long");
        }
        auto find = [&] (const char* plain) -> const rhythm::Rhythm& { return all[(size_t) std::max (0, rhythm::findRhythm (plain))]; };
        check (rhythm::findRhythm ("tintal") >= 0 && find ("tintal").beats == 16 && find ("tintal").sections == std::vector<int> { 4, 4, 4, 4 },
               "tīntāl: 16 mātrās, 4+4+4+4");
        check (find ("jhaptal").beats == 10 && find ("rupak").beats == 7 && find ("ektal").beats == 12 && find ("dhamar").beats == 14,
               "jhaptāl 10, rūpak 7, ektāl 12, dhamār 14 mātrās");
        check (find ("caturasra jati triputa talam (adi talam)").beats == 8, "ādi tāḷam: 8 akṣaras (4+2+2)");
        check (find ("misra jati jhampa talam").beats == 10 && find ("khanda jati ata talam").beats == 14
                   && find ("sankirna jati dhruva talam").beats == 29 && find ("tisra jati eka talam").beats == 3,
               "sūḷādi tāḷas: laghu by jāti, drutam 2, anudrutam 1");
        int talams = 0;
        for (const auto& r : all)
            talams += r.group == "South Indian tāḷam" && r.subgroup != "cāpu";
        check (talams == 35, "the 35 sūḷādi tāḷas: seven tāḷas in five jātis");
        // The tracker writes a row for the finest step: tīntāl's ṭhekā is a bol a mātrā, ektāl's has tirakiṭa.
        check (rhythm::rowsPerBeat (find ("tintal")) == 1 && rhythm::rowsPerBeat (find ("ektal")) == 4
                   && rhythm::rowsPerBeat (find ("trap triplet hats")) == 12,
               "rows a beat: the steps every sound falls on");
        // A held note (the medieval modes' long) lasts its tempora.
        const auto& primus = find ("modus primus");
        const auto hits = rhythm::hits (primus);
        check (std::any_of (hits.begin(), hits.end(), [] (const rhythm::Hit& h) { return h.length == 2; }), "modus primus: the long held two tempora");
        for (const auto& g : rhythm::groups())
        {
            const auto reg = rhythm::region (g);
            const auto regs = rhythm::regions();
            check (std::find (regs.begin(), regs.end(), reg) != regs.end(), "rhythm group " + g + ": in a region of the menu");
        }
        check (rhythm::region ("iqāʿāt") == "West Asia" && rhythm::region ("medieval (ars antiqua)") == "Europe",
               "regions: west asia, and europe as one region among the others");
        // The iqāʿāt as Maqam World notates them: their time signatures, groupings and strokes.
        int iqaat = 0;
        for (const auto& r : all)
            iqaat += r.group == "iqāʿāt";
        check (iqaat == 38, "iqāʿāt: samāʿī thaqīl and the 37 others from Maqam World (" + std::to_string (iqaat) + ")");
        check (find ("wahda").lanes[0].hits == "x......." && find ("wahda").lanes[1].hits == "...x..x.", "waḥda: one dum, then tak on 4 and 7");
        check (find ("wahda w-nuss").lanes[1].hits == "...x..x.....x..." && find ("wahda w-nuss").lanes[1].perBeat == 4,
               "waḥda w-nuṣṣ: the first tak half an eighth late");
        check (find ("aqsaq").beats == 9 && find ("aqsaq").sections == std::vector<int> { 2, 2, 2, 3 }, "aqṣāq: 9/8, 2+2+2+3");
        check (find ("awis").beats == 11 && find ("zarafat").beats == 13 && find ("khush rank").beats == 17
                   && find ("murabbaa").beats == 13 && find ("muhajjar").beats == 14 && find ("awfar masri").beats == 19
                   && find ("fakhit").beats == 20 && find ("warshan arabi").beats == 32 && find ("sittatu ashar masri").beats == 32,
               "the long cycles: ʿawīṣ 11/8, ẓarāfāt 13/8, khūsh rānk 17/8, murabbaʿ 13/4, muḥajjar 14/4, awfar 19/4, fākhit 20/4, the 32/4s");
        check (find ("muhajjar").lanes[0].hits.rfind ("xxx", 0) == 0, "muḥajjar: three dums to open");
        check (find ("sudasi").lanes[1].hits.find ('o') != std::string::npos && find ("sudasi").counting.find ("ka") != std::string::npos,
               "sudāsī: the light tak-ka between");
        check (find ("dawr hindi").counting == "dum tak tak | dum – tak –", "dawr hindī counted 3+4");
        // kuku as afrodrumming.com writes it: djembe 1 bass on 1, tones on 2 and 2+, slap on 4; djembe 2 over two bars.
        const auto& kuku = find ("kuku");
        check (kuku.group == "West Africa" && kuku.beats == 8 && kuku.lanes.size() == 5
                   && kuku.lanes[1].hits == "..xx......xx...." && kuku.lanes[2].hits == "......x.......x."
                   && kuku.lanes[4].hits == "........x.x.xx..",
               "kuku: djembe 1's bar twice against djembe 2's two bars");
        check (rhythm::findRhythm ("maqsum") >= 0 && find ("maqsum").group == "North Africa", "maqsūm stays in North Africa, not repeated");
        check (rhythm::drumName (39) == "hand clap" && rhythm::drumName (81) == "open triangle" && rhythm::drumName (20).empty(), "general midi drum names");

        // Layakārī: the ṭhekā twice, three and four times as fast, the claps and waves as they were.
        const auto& tintal = find ("tintal");
        for (int times = 2; times <= 4; ++times)
        {
            const auto fast = rhythm::laya (tintal, times);
            check (rhythm::valid (fast) && fast.beats == tintal.beats && rhythm::rowsPerBeat (fast) == times,
                   "tīntāl at laya " + std::to_string (times) + ": still 16 mātrās, " + std::to_string (times) + " bols to the mātrā");
            bool kept = true;
            for (size_t i = 0; i < fast.lanes.size(); ++i)
                if (tintal.lanes[i].marksCycle)
                    kept = kept && fast.lanes[i].hits == tintal.lanes[i].hits && fast.lanes[i].perBeat == tintal.lanes[i].perBeat;
            check (kept, "tīntāl at laya " + std::to_string (times) + ": the claps and waves stay put");
        }
        check (rhythm::laya (tintal, 2).name == "tīntāl, duguṇ", "duguṇ in the name");
        // Naḍai: ādi tāḷam's pulses in tiśra, khaṇḍa, miśra and saṅkīrṇa.
        const auto& adi = find ("caturasra jati triputa talam (adi talam)");
        check (rhythm::hasGati (adi) && ! rhythm::hasGati (tintal), "a tāḷam has a gati, a tāl does not");
        for (int pulses : { 3, 5, 7, 9 })
        {
            const auto g = rhythm::gati (adi, pulses);
            check (rhythm::valid (g) && g.beats == 8, "ādi tāḷam in " + std::to_string (pulses) + " naḍai: still 8 akṣaras");
            const auto lane = std::find_if (g.lanes.begin(), g.lanes.end(), [] (const rhythm::Lane& l) { return l.pulse; });
            check (lane != g.lanes.end() && lane->perBeat == pulses && rhythm::steps (*lane) == 8 * pulses,
                   "ādi tāḷam in " + std::to_string (pulses) + " naḍai: " + std::to_string (pulses) + " pulses to the akṣara");
        }
        check (rhythm::gati (adi, 5).name.find ("khaṇḍa naḍai") != std::string::npos && rhythm::gati (adi, 4).name == adi.name,
               "the naḍai in the name, unless caturaśra");
    }

    // Architectonics (owner, 2026-10-29): a set by the golden ratio. Every section a Fibonacci
    // number of bars, back to back over the whole set; the climax at the golden section on the
    // fullest scene; never the same scene twice running; and a new seed a new set.
    void testArchitectonics()
    {
        namespace ar = architectonics;
        check (ar::fibonacci (144) == std::vector<int> { 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144 } && ar::isFibonacci (89) && ! ar::isFibonacci (90),
               "architectonics: the Fibonacci numbers");
        ar::Settings s;
        s.clips = { 2, 4, 0, 6, 8, 3, 5, 7, 1 };
        s.seed = 0.3;
        for (auto order : { ar::Order::arch, ar::Order::fibonacci, ar::Order::phiWalk })
        {
            s.order = order;
            const auto p = ar::plan (s);
            bool fib = ! p.sections.empty(), contiguous = true, apart = true, scenes = true;
            int at = 0, climaxes = 0;
            for (size_t i = 0; i < p.sections.size(); ++i)
            {
                const auto& x = p.sections[i];
                fib = fib && ar::isFibonacci (x.bars) && x.bars <= s.longest;
                contiguous = contiguous && x.start == at;
                at += x.bars;
                apart = apart && (i == 0 || x.scene != p.sections[i - 1].scene);
                scenes = scenes && x.scene >= 0 && x.scene < (int) s.clips.size() && s.clips[(size_t) x.scene] > 0;
                if (x.climax)
                {
                    ++climaxes;
                    check (x.start == 89 && x.scene == 4 && x.energy == 1.0 && x.function == ar::Function::climax,
                           "architectonics: the climax at the golden section of 144, on the fullest scene");
                }
            }
            check (fib && contiguous && at == 144 && apart && scenes && climaxes == 1 && p.climaxBar == 89
                       && p.sections.front().function == ar::Function::emergence && p.sections.back().function == ar::Function::dissolution,
                   "architectonics: Fibonacci sections over the whole set, in five parts, no scene twice running");
        }
        s.earlyClimax = true;
        check (ar::plan (s).climaxBar == 55, "architectonics: an early climax at 1/φ² of the way");
        s.earlyClimax = false;
        const auto one = ar::plan (s);
        s.seed = ar::nextSeed (s.seed);
        const auto two = ar::plan (s);
        bool differ = one.sections.size() != two.sections.size();
        for (size_t i = 0; ! differ && i < one.sections.size(); ++i)
            differ = one.sections[i].bars != two.sections[i].bars || one.sections[i].scene != two.sections[i].scene;
        check (differ && ar::sectionAt (one, 0.0) == 0 && ar::sectionAt (one, 144.0) == -1 && ar::sectionAt (one, 89.5) >= 0
                   && one.sections[(size_t) ar::sectionAt (one, 89.5)].climax,
               "architectonics: a new seed deals a new set; the section at a bar");
        s.clips = { 0, 0 };
        check (ar::plan (s).sections.empty(), "architectonics: no clips, no set");

        // Compose a Song: a scene a section; everyone at the climax; the rhythm a part's own;
        // onto the dominant before the climax and home at the end; the MIDI loops whole bars.
        ar::SongSettings ss;
        ss.present.fill (true);
        ss.form.seed = 0.42;
        ss.keys = 0x6AD;   // dorian: 0 2 3 5 7 9 10
        for (const auto& r : rhythm::rhythms())
            if (r.group == rhythm::rhythms().front().group)
                ss.rhythms.push_back (&r);
        const auto song = ar::compose (ss);
        bool aScene = ! song.scenes.empty() && song.scenes.size() == song.plan.sections.size(), climaxAll = false, loops = true, inMode = true;
        std::map<ar::Function, std::string> grooves;
        bool oneGroove = true;
        for (size_t i = 0; i < song.scenes.size(); ++i)
        {
            const auto& sc = song.scenes[i];
            aScene = aScene && song.plan.sections[i].scene == (int) i && sc.section == (int) i && (int) sc.chords.size() == ss.loopBars;
            if (song.plan.sections[i].climax)
                climaxAll = std::all_of (sc.plays.begin(), sc.plays.end(), [] (bool b) { return b; });
            if (! sc.rhythm.empty())
            {
                oneGroove = oneGroove && (grooves.count (sc.function) == 0 || grooves[sc.function] == sc.rhythm);
                grooves[sc.function] = sc.rhythm;
            }
            for (int c = 0; c < ar::colours; ++c)
                if (const auto& m = sc.midi[(size_t) c])
                {
                    loops = loops && m->noteCount() > 0 && sc.plays[(size_t) c]
                            && (ss.roles[(size_t) c] == ar::Role::rhythm ? m->lastTick % (480 * 4) == 0 : m->lastTick == 480 * 4 * ss.loopBars);
                    if (ss.roles[(size_t) c] != ar::Role::rhythm)
                        for (const auto& e : m->events)
                            inMode = inMode && (e.kind() != 0x90 || (ss.keys & (1u << (e.data1 % 12))) != 0);
                }
        }
        const auto before = std::find_if (song.plan.sections.begin(), song.plan.sections.end(), [] (const ar::Section& x) { return x.climax; })
                            - song.plan.sections.begin() - 1;
        check (aScene && climaxAll && loops && inMode && oneGroove && song.scenes.back().chords.back() == 0
                   && before >= 0 && song.scenes[(size_t) before].chords.back() == 4,
               "architectonics: a song, a scene a section, in the mode, everyone at the climax, a part's rhythm its own");
        const auto& quiet = song.scenes.back();
        check (std::count (quiet.plays.begin(), quiet.plays.end(), true) < ar::colours && quiet.plays[1],
               "architectonics: the dissolution thins out to the pad");

        // The score: every part from the first bar to the last, a channel a colour, the sections marked.
        const auto full = ar::score (song, ss, "score");
        std::set<int> used;
        bool inside = true;
        for (const auto& e : full->events)
        {
            used.insert (e.channel());
            inside = inside && e.tick <= full->lastTick;
        }
        std::string why;
        const auto bytes = midifile::bytes (*full);
        const auto back = midifile::parse (bytes.data(), bytes.size(), why);
        check (full->lastTick == 144 * 4 * 480 && full->markers.size() == song.scenes.size() && full->markers.front().text == "1 emergence"
                   && used.count (9) == 1 && used.size() == 5 && inside && back != nullptr && back->noteCount() == full->noteCount()
                   && ar::parts (song, ss)[1] != nullptr && ar::parts (song, ss)[0] == nullptr,
               "architectonics: the score, a part a colour, the sections marked, as a MIDI file");
    }

    // Compose (owner, 2026-10-29): every method writes a score from nothing in at most eight
    // parts, in its mode; and each keeps the structure its tradition gives it.
    void testCompose()
    {
        namespace co = composition;
        bool every = true, inMode = true, capped = true, files = true;
        for (const auto& m : co::methods())
        {
            co::Settings s;
            s.method = m.method;
            s.bars = 32;
            s.seed = 5;
            if (*m.home != 0)
                s.mode = counterpoint::Mode::fromTuning (mixer::tuningSystems()[(size_t) mixer::findTuning (m.home)], 0);
            const counterpoint::Mode mode = s.mode.valid() ? s.mode : [] { auto x = counterpoint::Mode::chromatic (0); x.scale.assign (12, false);
                                                                         for (int k : { 0, 2, 4, 5, 7, 9, 11 }) x.scale[(size_t) k] = true; return x; }();
            capped = capped && m.mostParts <= co::maxParts;
            for (int parts = 1; parts <= m.mostParts; ++parts)
            {
                s.parts = parts;
                const auto r = co::generate (s);
                const auto wanted = std::max (parts, m.method == co::Method::fugue || m.method == co::Method::hocket ? 2 : 1);
                size_t notes = 0;
                for (const auto& p : r.parts)
                {
                    notes += p.notes.size();
                    if (! p.drums && m.method != co::Method::spectral)
                        for (const auto& n : p.notes)
                        {
                            const auto k = ((n.key - 60 - mode.root) % mode.period + mode.period) % mode.period;
                            inMode = inMode && mode.scale[(size_t) k];
                        }
                }
                every = every && (int) r.parts.size() == wanted && notes > 0 && r.end > 0 && ! r.sections.empty() && r.sections.front().start == 0;
                if (parts == m.mostParts)
                {
                    std::string why;
                    const auto song = co::toSong (r, m.name);
                    const auto bytes = midifile::bytes (*song);
                    const auto back = midifile::parse (bytes.data(), bytes.size(), why);
                    files = files && back != nullptr && back->noteCount() == (int) notes && song->markers.size() == r.sections.size();
                }
            }
        }
        check (capped && every, "compose: every method, one to its most parts (never more than eight), writes a score");
        check (inMode, "compose: every pitched note in the method's mode");
        check (files, "compose: each score a MIDI file, a part a channel, the sections marked");

        auto run = [] (co::Method m, int parts, const char* home = "")
        {
            co::Settings s;
            s.method = m;
            s.parts = parts;
            s.bars = 32;
            s.seed = 11;
            if (*home != 0)
                s.mode = counterpoint::Mode::fromTuning (mixer::tuningSystems()[(size_t) mixer::findTuning (home)], 0);
            return co::generate (s);
        };
        // Isorhythm: the tenor's rhythm the same in each of the first taleae.
        {
            const auto r = run (co::Method::isorhythm, 1);
            std::vector<int64_t> starts;
            for (const auto& x : r.sections)
                starts.push_back (x.start);
            std::vector<std::vector<int64_t>> rhythms;
            for (size_t k = 0; k + 1 < starts.size() && k < 3; ++k)
            {
                rhythms.emplace_back();
                for (const auto& n : r.parts[0].notes)
                    if (n.start >= starts[k] && n.start < starts[k + 1])
                        rhythms.back().push_back (n.end - n.start);
            }
            check (rhythms.size() == 3 && ! rhythms[0].empty() && rhythms[0] == rhythms[1] && rhythms[1] == rhythms[2],
                   "compose: isorhythm, the tenor's talea the same in every statement");
        }
        // Tintinnabuli: the T-voice only on the tonic triad's notes.
        {
            const auto r = run (co::Method::tintinnabuli, 2);
            bool triad = ! r.parts[1].notes.empty();
            for (const auto& n : r.parts[1].notes)
                triad = triad && (((n.key - 60) % 12 + 12) % 12 == 0 || ((n.key - 60) % 12 + 12) % 12 == 4 || ((n.key - 60) % 12 + 12) % 12 == 7);
            check (triad, "compose: tintinnabuli, the T-voice on the tonic triad");
        }
        // Spectral: the third partial a twelfth (1902 cents) above the fundamental, in its own cents.
        {
            const auto r = run (co::Method::spectral, 2);
            const auto& f = r.parts[0].notes.front();
            const auto& p3 = r.parts[1].notes.front();
            const auto cents = (p3.key * 100.0 + p3.cents) - (f.key * 100.0 + f.cents);
            check (r.microtonal && std::abs (cents - 1901.955) < 0.5, "compose: spectral, partial 3 a just twelfth above the fundamental");
        }
        // Gamelan: four kenong strokes and one gong ageng in every gongan of a ladrang (32 beats).
        {
            const auto r = run (co::Method::gamelan, 8, "slendro pathet manyura");
            const auto& kenong = r.parts[3].notes;
            size_t gongs = 0;
            for (const auto& n : r.parts[4].notes)
                gongs += n.velocity >= 110;
            check (! kenong.empty() && kenong.size() == 4 * (gongs - 1), "compose: gamelan, the kenong four times and the gong once a gongan (after the buka's gong)");
        }
        // Ewe: the bell's standard pattern, seven strokes in each cycle of twelve pulses.
        {
            const auto r = run (co::Method::ostinato, 1);
            const auto cycle = 12 * co::ticksPerQuarter / 2;
            size_t first = 0;
            for (const auto& n : r.parts[0].notes)
                first += n.start < cycle;
            check (first == 7 && r.parts[0].drums, "compose: Ewe, the bell's timeline seven strokes in twelve pulses");
        }
        // Hocket: never two players on the same pulse while the melody is hocketed.
        {
            const auto r = run (co::Method::hocket, 5);
            std::map<int64_t, int> at;
            const auto untilVariations = r.sections.size() > 1 ? r.sections[1].start : r.end;
            for (const auto& p : r.parts)
                for (const auto& n : p.notes)
                    if (n.start < untilVariations)
                        ++at[n.start];
            bool alone = ! at.empty();
            for (const auto& [t, count] : at)
                alone = alone && count == 1;
            check (alone, "compose: hocket, one player a pulse");
        }
        // Congolese guitar: in the sebene the mi-solo plays only in the accompagnement's gaps, and the
        // solo sits above the mi-solo, the mi-solo above the accompagnement.
        {
            const auto r = run (co::Method::congolese, 8);
            int64_t from = -1, to = r.end;
            for (const auto& x : r.sections)
                if (from < 0 && x.name.rfind ("sebene", 0) == 0)
                    from = x.start;
                else if (x.name == "the close")
                    to = x.start;
            std::set<int64_t> accomp;
            for (const auto& n : r.parts[2].notes)
                if (n.start >= from && n.start < to)
                    accomp.insert (n.start);
            bool interlocked = from >= 0 && ! accomp.empty();
            size_t mi = 0;
            for (const auto& n : r.parts[1].notes)
                if (n.start >= from && n.start < to)
                {
                    ++mi;
                    interlocked = interlocked && accomp.count (n.start) == 0;
                }
            auto mean = [] (const co::Part& p)
            {
                double sum = 0.0;
                for (const auto& n : p.notes)
                    sum += n.key;
                return p.notes.empty() ? 0.0 : sum / (double) p.notes.size();
            };
            check (interlocked && mi > 0, "compose: Congolese guitar, the mi-solo in the accompagnement's gaps through the sebene");
            check (mean (r.parts[0]) > mean (r.parts[1]) && mean (r.parts[1]) > mean (r.parts[2]) && mean (r.parts[2]) > mean (r.parts[3])
                       && r.parts[4].drums && r.parts[7].drums && ! r.parts[0].drums,
                   "compose: Congolese guitar, solo above mi-solo above accompagnement above bass");
        }
        // Rāga: the gat ends on sam with a tihāī.
        {
            const auto r = run (co::Method::raga, 3, "yaman (kalyan)");
            check (r.sections.back().name == "tihāī" && r.parts[2].drums, "compose: rāga, the gat ends in a tihāī");
        }
        // Interlocking polyphony: with every voice in, the voices together fill most pulses of the period
        // though none alone fills half of them; and a yodelling voice leaps a sixth or more.
        {
            const auto r = run (co::Method::interlocking, 5);
            int64_t from = -1;
            for (const auto& x : r.sections)
                if (x.name == "all the voices interlock")
                    from = x.start;
            const int64_t pulse = co::ticksPerQuarter / 2, period = 24;
            std::set<int64_t> together;
            size_t most = 0;
            int leaps = 0;
            for (size_t v = 0; v < r.parts.size(); ++v)
            {
                std::set<int64_t> own;
                const co::Note* last = nullptr;
                for (const auto& n : r.parts[v].notes)
                    if (from >= 0 && n.start >= from && n.start < from + period * pulse)
                    {
                        own.insert ((n.start - from) / pulse);
                        together.insert ((n.start - from) / pulse);
                        if (v == 1 && last != nullptr && std::abs (n.key - last->key) >= 8)
                            ++leaps;
                        last = &n;
                    }
                most = std::max (most, own.size());
            }
            check (from > 0 && together.size() >= 20 && most < 12, "compose: interlocking, the composite fills the period, no voice alone does");
            check (leaps >= 2, "compose: interlocking, the yodel leaps between chest and head voice");
        }
        // Bol Processor grammar: the theme bharī has the bāyāṅ's open stroke, its khālī half none until the
        // last two cells open again; variations follow, and the tihāī lands on sam, a tīntāl cycle's start.
        {
            const auto r = run (co::Method::bolGrammar, 3);
            const int64_t beat = co::ticksPerQuarter;
            auto open = [&] (int64_t a, int64_t b)
            {
                int k = 0;
                for (const auto& n : r.parts[0].notes)
                    k += n.key == 64 && n.start >= a * beat && n.start < b * beat;
                return k;
            };
            int variations = 0;
            for (const auto& x : r.sections)
                variations += x.name.rfind ("variation", 0) == 0;
            const auto sam = r.sections.back().start;
            const auto lands = std::any_of (r.parts[0].notes.begin(), r.parts[0].notes.end(), [&] (const co::Note& n) { return n.start == sam && n.key == 64; });
            check (r.parts[0].drums && open (0, 8) > 0 && open (8, 14) == 0 && open (14, 16) > 0, "compose: bol grammar, bharī then khālī, open again for sam");
            check (variations >= 2 && r.sections.back().name == "sam" && r.sections[r.sections.size() - 2].name == "tihāī"
                       && sam % (16 * beat) == 0 && lands, "compose: bol grammar, variations and a tihāī landing on sam");
        }
    }

    // The Bol Processor (owner, 2026-10-31): BP3's items read back in time, and BP3 itself (phi-bp3) run on a grammar.
    void testBolProcessor()
    {
        namespace bp = bolprocessor;
        // Speed /2; a polymetric expression takes its first field's time; fields between periods a beat each; '_' lengthens.
        const auto t = bp::parse ("/2 a b {c d e, f g} . h _ - i");
        auto at = [&] (const std::string& s) { for (const auto& e : t.events) if (e.symbol == s) return e; return bp::Event {}; };
        check (t.events.size() == 9 && std::abs (t.beats - 2.0) < 1e-9 && std::abs (at ("c").beat - 0.4) < 1e-9 && std::abs (at ("f").length - 0.3) < 1e-9
                   && std::abs (at ("h").length - 0.5) < 1e-9 && std::abs (at ("i").beat - 1.75) < 1e-9,
               "bol processor: an item laid out by speeds, periods and polymetry");
        check (bp::splitItems ("dha ti\n\n// a comment\nge na\nka\n\n").size() == 2, "bol processor: the items, a blank line between");
        check (bp::strokes ("dha").size() == 2 && bp::strokes ("ta").size() == 1 && bp::strokes ("Dhin").size() == 2 && bp::strokes ("C4").empty(),
               "bol processor: dha is two strokes, ta one, a note none");

        // Bols written together split as BP's alphabets let them be, a unit each; a speed written onto them;
        // a polymetric expression's written duration.
        {
            const auto w = bp::parse ("4+4+4+4/4 dhatidhage . nadhatrkt");
            const auto d = bp::parse ("/6 dhin--dhagena");
            const auto k = bp::parse ("/4 dhati dha-/4 taki kite/2tete");
            const auto p = bp::parse ("{3/2,takiteta kite} {1,dha}");
            check (w.events.size() == 8 && std::abs (w.beats - 2.0) < 1e-9 && w.events[6].symbol == "tr" && std::abs (w.events[6].beat - 1.5) < 1e-9
                       && d.events.size() == 4 && std::abs (d.beats - 1.0) < 1e-9 && std::abs (d.events[1].beat - 0.5) < 1e-9
                       && k.events.size() == 9 && std::abs (k.beats - 3.0) < 1e-9
                       && p.events.size() == 7 && std::abs (p.beats - 2.5) < 1e-9 && std::abs (p.events[6].beat - 1.5) < 1e-9,
                   "bol processor: bols written together, speeds on them, and a written duration");
        }

        // A data file: its items read as written (the header, comments and remarks left out), without BP3.
        {
            const auto data = bp::run ("", "", bp::companions (PHI_SHARE_SOURCE_DIR "/bolprocessor/-da.kathak"), 0);
            const auto items = bp::splitItems (data.items);
            const auto first = items.empty() ? bp::Item {} : bp::parse (items.front());
            bool bols = ! first.events.empty();
            for (const auto& e : first.events)
                bols = bols && ! bp::strokes (e.symbol).empty();
            // "Check that the last bol falls on the 17th beat."
            check (bp::isData (PHI_SHARE_SOURCE_DIR "/bolprocessor/-da.kathak") && data.ok && items.size() >= 5 && bols
                       && std::abs (first.events.back().beat - 16.0) < 1e-9,
                   "bol processor: a data file's items, Birju Maharaj's kathak landing on the 17th beat");
        }

        // BP3 itself, on a grammar of our own: two statements of a cell and a polymetric cadence, at four bols a beat.
        const std::string exe = PHI_BP3_BUILD_PATH;
        if (! std::filesystem::exists (exe))
            return;
        const auto folder = std::filesystem::temp_directory_path() / "phi-bp3-test";
        std::filesystem::create_directories (folder);
        std::ofstream (folder / "-gr.phitest") << "-al.phitest\n\nORD\ngram#1 [1] S --> /4 A A B\ngram#1 [2] A --> dha ti dha ge\n"
                                                  "gram#1 [3] B --> {dhin na ge na, tin - - tin}\n";
        std::ofstream (folder / "-al.phitest") << "*\ndha --> ta\nti --> ti\nge --> ke\ndhin --> tin\nna --> na\ntin --> tin\n";
        const auto files = bp::companions ((folder / "-gr.phitest").string());
        const auto r = bp::run (exe, PHI_BOLPROCESSOR_SOURCE_DIR "/console_strings.json", files, 7, 30);
        const auto items = bp::splitItems (r.items);
        const auto item = items.empty() ? bp::Item {} : bp::parse (items.front());
        const auto score = bp::toScore ({ item }, 90.0);
        check (r.ok && ! files.alphabet.empty() && items.size() == 1 && item.events.size() == 14 && std::abs (item.beats - 3.0) < 1e-9
                   && score.parts.size() == 1 && score.parts[0].drums && score.parts[0].notes.size() == 19,
               "bol processor: BP3 produces the grammar's item, read back as bols on the tablā");
        std::filesystem::remove_all (folder);

        // The example PHI has: Kippen and Bel's grammar of an Ajrada qāʿida. Its items are 32 beats, theme and
        // variations bharī then khālī, every symbol a bol.
        {
            const auto q = bp::run (exe, PHI_BOLPROCESSOR_SOURCE_DIR "/console_strings.json", bp::companions (PHI_SHARE_SOURCE_DIR "/bolprocessor/-gr.dhin--"), 3, 30);
            std::vector<bp::Item> all;
            for (const auto& i : bp::splitItems (q.items))
                all.push_back (bp::parse (i));
            bool bols = ! all.empty();
            for (const auto& i : all)
            {
                bols = bols && std::abs (i.beats - 32.0) < 1e-9;
                for (const auto& e : i.events)
                    bols = bols && ! bp::strokes (e.symbol).empty();
            }
            check (q.ok && bols, "bol processor: the Ajrada qāʿida's grammar produces items of 32 beats, all bols");
        }
        // Kippen and Bel's other qāʿidas: items of 16 beats, every bol played, glued ones split.
        for (const char* name : { "-gr.dhati2", "-gr.dhadhatite" })
        {
            const auto q = bp::run (exe, PHI_BOLPROCESSOR_SOURCE_DIR "/console_strings.json", bp::companions (std::string (PHI_SHARE_SOURCE_DIR "/bolprocessor/") + name), 5, 30);
            bool bols = q.ok;
            for (const auto& i : bp::splitItems (q.items))
            {
                const auto item = bp::parse (i);
                bols = bols && std::abs (item.beats - 16.0) < 1e-9 && ! item.events.empty();
                for (const auto& e : item.events)
                    bols = bols && ! bp::strokes (e.symbol).empty();
            }
            check (bols, std::string ("bol processor: ") + name + " produces 16-beat items, all bols");
        }
    }

    // PHI tabla (owner, 2026-10-31): the owner's strokes as a Slicer kit of 32 pads for Push, the takes in turn,
    // and softer hits darker.
    void testPhiTabla()
    {
        namespace pt = phitabla;
        bool files = true, unique = true;
        std::set<int> seen;
        for (const auto& p : pt::pads())
        {
            unique = unique && p.pad >= 0 && p.pad < 48 && seen.insert (p.pad).second;
            // Every sound there (and any stand-ins).
            for (const auto& list : { p.sounds, p.fallback })
                for (const auto& x : list)
                    files = files && std::filesystem::exists (std::string (PHI_SHARE_SOURCE_DIR "/phi-tabla/") + x.file + ".wav");
        }
        // The owner's rack: 32 pads on banks A and B, its note 61 the first pad, 92 the last (dhā).
        bool rack = true;
        for (int pad = 0; pad < 32; ++pad)
            rack = rack && seen.count (pad) == 1;
        check (unique && files && rack && std::string (pt::pads()[31].name) == "dhā", "phi tabla: the owner's 32-pad rack on banks A and B, each sound in share/phi-tabla");
        check (pt::padFor ("ta", 0) == 82 - 61 && pt::padFor ("ta", 1) == 91 - 61 && pt::padFor ("ta", 2) == 82 - 61 && pt::padFor ("ne", 0) == pt::padFor ("ta", 0)
                   && pt::padFor ("dhee", 0) == pt::padFor ("dhi", 0) && pt::padFor ("C4", 0) < 0 && pt::noteFor ("ta", 0) == slicer::noteForPad (21, slicer::Layout::push, 0),
               "phi tabla: a bol's takes in turn, its spellings as one, on Push's notes");

        auto sound = std::make_shared<sampler::Sample>();
        uint32_t x = 1;
        for (int i = 0; i < 4800; ++i)   // white noise: its brightness shows
        {
            x = x * 1664525u + 1013904223u;
            const auto v = (float) ((x >> 8) / 8388608.0 - 1.0) * 0.5f;
            sound->left.push_back (v);
            sound->right.push_back (v);
        }
        const auto kit = pt::kit ("folder", [&] (const std::string&) { return sound; });
        const auto dha = pt::padFor ("dhin", 0), ta = pt::padFor ("ta", 0);
        check (kit.pads[(size_t) dha].layerCount() == 2 && kit.pads[(size_t) ta].layerCount() == 1 && kit.pads[(size_t) ta].name == "tā2"
                   && kit.pads[69 - 61].layers[0].start == 362 && kit.layout == slicer::Layout::push,
               "phi tabla: dhin is tin and ge together; the rack's names and start points");

        // A soft hit: quieter, and with less of its highs (the sample to sample difference) for its level.
        auto brightness = [&] (double tone)
        {
            slicer::Engine e;
            e.prepare (48000.0);
            auto k = kit;
            k.velocityTone = tone;
            e.setKit (k);
            e.hit (pt::padFor ("ta", 0), 0.3f);
            std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
            e.render (l.data(), r.data(), 2048);
            double level = 0.0, high = 0.0;
            for (size_t i = 1; i < l.size(); ++i)
            {
                level += l[i] * l[i];
                high += (l[i] - l[i - 1]) * (l[i] - l[i - 1]);
            }
            return high / std::max (1e-12, level);
        };
        check (brightness (0.5) < 0.5 * brightness (0.0), "phi tabla: a soft hit is darker");

        // The Bol Processor on PHI tabla: dha twice is its two takes.
        const auto score = bolprocessor::toScore ({ bolprocessor::parse ("dha dha ne") }, 90.0, true);
        check (score.parts[0].notes.size() == 3 && score.parts[0].notes[0].key == pt::noteFor ("dha", 0) && score.parts[0].notes[1].key == pt::noteFor ("dha", 1)
                   && score.parts[0].notes[0].key != score.parts[0].notes[1].key,
               "phi tabla: the Bol Processor's bols on its pads, the takes in turn");
    }

    void testRhythmGenerator()
    {
        // Toussaint (2005): the tresillo E(3,8), the cinquillo E(5,8), the bembé bell E(7,12) turned.
        auto pattern = [] (const std::vector<bool>& v) { std::string s; for (bool b : v) s += b ? 'x' : '.'; return s; };
        check (pattern (rhythmgen::euclid (3, 8)) == "x..x..x.", "euclid: E(3,8) is the tresillo");
        check (pattern (rhythmgen::euclid (5, 8)) == "x.xx.xx.", "euclid: E(5,8) is the cinquillo");
        check (pattern (rhythmgen::euclid (4, 16)) == "x...x...x...x...", "euclid: E(4,16) four on the floor");
        check (pattern (rhythmgen::euclid (0, 5)) == "....." && pattern (rhythmgen::euclid (9, 4)) == "xxxx", "euclid: none, and all");
        for (const auto& p : rhythmgen::presets())
            if (p.name.rfind ("bembé", 0) == 0)
                check (rhythmgen::cycle (p.settings.lanes[0]) == "X.x.xx.x.x.x", "the bembé bell, x.x.xx.x.x.x");

        // Polymeter: a 16-step lane against a 12-step one meets again after 3 bars of 4/4 at 16ths.
        rhythmgen::Settings s;
        rhythmgen::Lane kick;
        kick.steps = 16;
        kick.pulses = 4;
        rhythmgen::Lane bell;
        bell.note = 56;
        bell.steps = 12;
        bell.pulses = 5;
        s.lanes = { kick, bell };
        check (rhythmgen::lengthBars (s) == 3, "polymeter: 16 against 12 steps comes round in 3 bars");
        std::string error;
        const auto song = rhythmgen::generate (s, 120, error);
        check (error.empty() && song->noteCount() == 3 * 4 + 4 * 5 && song->lastTick == 3 * 4 * 480,
               "generate: every pulse of every lane, three bars");

        // Chance, swing, humanizing and the rules, all from the seed: the same seed, the same groove.
        s.lanes[1].chance = 0.5;
        s.swing = 0.2;
        s.humanizeMs = 6;
        s.humanizeVelocity = 8;
        s.fillEvery = 2;
        s.rotateEvery = 1;
        auto same = [] (const midifile::Song& a, const midifile::Song& b) {
            if (a.events.size() != b.events.size())
                return false;
            for (size_t i = 0; i < a.events.size(); ++i)
                if (a.events[i].tick != b.events[i].tick || a.events[i].data1 != b.events[i].data1 || a.events[i].data2 != b.events[i].data2)
                    return false;
            return true;
        };
        const auto one = rhythmgen::generate (s, 120, error), two = rhythmgen::generate (s, 120, error);
        check (same (*one, *two), "generate: the same seed writes the same groove");
        s.seed = 2;
        check (! same (*one, *rhythmgen::generate (s, 120, error)), "generate: another seed, another groove");

        s.lanes[0].sieve = "3@0|";
        rhythmgen::generate (s, 120, error);
        check (! error.empty(), "generate: a sieve that does not read is an error");
        s.lanes[0].sieve = "4@0";
        error.clear();

        // Feels: the triplet shuffle puts the second 8th two thirds into the beat.
        rhythmgen::Settings hats;
        rhythmgen::Lane hat;
        hat.note = 42;
        hat.steps = 2;
        hat.stepsPerBeat = 2;
        hat.pulses = 2;
        hats.lanes = { hat };
        hats.bars = 1;
        for (const auto& f : rhythmgen::feels())
            if (f.name == "triplet shuffle")
                rhythmgen::applyFeel (hats, f);
        const auto shuffled = rhythmgen::generate (hats, 120, error);
        std::vector<int64_t> ons;
        for (const auto& e : shuffled->events)
            if (e.kind() == 0x90 && e.data2 > 0)
                ons.push_back (e.tick);
        check (ons.size() == 8 && ons[1] == 320, "the triplet shuffle: the off-beat 8th at 2/3 of the beat");
        rhythmgen::Settings laid = rhythmgen::fromRhythm (rhythm::rhythms()[(size_t) rhythm::findRhythm ("funky drummer")]);
        for (const auto& f : rhythmgen::feels())
            if (f.name == "laid back")
                rhythmgen::applyFeel (laid, f);
        bool snareLate = false, kickOn = true;
        for (const auto& l : laid.lanes)
        {
            snareLate = snareLate || (l.note == 38 && l.pushMs > 0);
            kickOn = kickOn && (l.note != 36 || l.pushMs == 0);
        }
        check (snareLate && kickOn, "laid back: the snare behind the beat, the kick on it");

        // Back to a rhythmic mode for the Tracker, and a rhythmic mode into the generator.
        rhythm::Rhythm r;
        s.lanes[1].chance = 1.0;
        check (rhythmgen::toRhythm (s, "test", r, error) && rhythm::valid (r) && r.beats == 3 * 4 && r.lanes.size() == 2,
               "toRhythm: the clip as a rhythm the Tracker can write");
        const auto amen = rhythmgen::fromRhythm (rhythm::rhythms()[(size_t) rhythm::findRhythm ("amen break")]);
        check (amen.lanes.size() == 4 && rhythmgen::lengthBars (amen) == 1 && amen.beatsPerBar == 16, "fromRhythm: the amen break's four lanes, one cycle");
    }

    // Control surfaces and MIDI Learn (docs/control-surfaces.md).
    void testControlSurfaces()
    {
        using namespace control;
        check (PushDisplay::bgr565 (0xFFFF0000) == 0x001F && PushDisplay::bgr565 (0xFF00FF00) == 0x07E0 && PushDisplay::bgr565 (0xFF0000FF) == 0xF800
                   && PushDisplay::bgr565 (0xFFFFFFFF) == 0xFFFF,
               "control: Push's display takes red low, then green, then blue (BGR565)");
        check (std::abs (unitFromDb (0.0f) - 0.8409f) < 0.001f, "control: 0 dB where the mixer's fader has it");
        check (std::abs (dbFromUnit (unitFromDb (-12.0f)) + 12.0f) < 0.01f && unitFromDb (-120.0f) == 0.0f && dbFromUnit (1.0f) > 5.9f,
               "control: fader units and dB both ways");
        check (paletteIndex (0xFF0000) == 5 && paletteIndex (0x00FF00) == 21 && paletteIndex (0x0000FF) == 45 && paletteIndex (0xFFFFFF) == 3
                   && paletteIndex (0) == 0,
               "control: the Novation palette's red, green, blue, white and black");
        bool round = true;
        for (int i = 1; i < 60; ++i)   // 60..63 repeat hues
            round &= paletteIndex (paletteRgb (i)) == i;
        check (round, "control: each palette colour finds itself");

        auto kindId = [] (const char* port) { const auto* k = kindOf (port); return k != nullptr ? k->id : std::string ("none"); };
        check (kindId ("Launchpad X LPX MIDI In") == "launchpadX" && kindId ("Launchpad Pro MK3 LPProMK3 MIDI") == "launchpadProMk3"
                   && kindId ("Launchpad Pro MIDI 2") == "launchpadPro" && kindId ("Launchkey Mini MK3 MIDI 2") == "launchkeyMiniMk3"
                   && kindId ("Launchkey MK3 49 LKMK3 DAW In") == "launchkeyMk3" && kindId ("Ableton Push 2 MIDI 1") == "push2"
                   && kindId ("Ableton Push 3 Live Port") == "push3" && kindId ("Midi Through Port-0") == "none",
               "control: surfaces known by their ports' names");
        const auto& kx = *kindOf ("Launchpad X MIDI 1");
        check (pickPort (kx, { "Launchpad X LPX DAW In", "Launchpad X LPX MIDI In" }) == 1 && pickPort (kx, { "Launchpad X MIDI 2", "Launchpad X MIDI 1" }) == 0,
               "control: a Launchpad is driven through its MIDI port, not its DAW port");
        check (pickPort (*kindOf ("Launchkey MK3 49 MIDI 1"), { "Launchkey MK3 49 LKMK3 MIDI In", "Launchkey MK3 49 LKMK3 DAW In" }) == 1,
               "control: a Launchkey through its DAW port, its keys left to play");
        check (pickPort (*kindOf ("Ableton Push 2 MIDI 1"), { "Ableton Push 2 MIDI 2", "Ableton Push 2 MIDI 1" }) == 1, "control: Push through its first (Live) port");

        State state;
        state.tracks = 2;
        state.scenes = 4;
        state.track[0].rgb = 0xFF0000;
        state.track[1].rgb = 0x0000FF;
        state.slot[0][0].clip = true;
        state.slot[1][0].clip = true;
        state.slot[1][0].playing = true;
        state.track[1].playing = true;
        auto send = [] (Surface& s, std::initializer_list<int> bytes)
        {
            Message m;
            for (int b : bytes)
                m.push_back ((uint8_t) b);
            Received r;
            s.receive (m.data(), m.size(), r);
            return r;
        };
        auto launched = [] (const Received& r, int track, int scene)
        {
            return r.commands.size() == 1 && r.commands[0].action == Action::launch && r.commands[0].track == track && r.commands[0].scene == scene;
        };

        // Launchpad X: programmer mode, the grid lit by RGB SysEx, pads launching.
        {
            auto lp = kindOf ("Launchpad X LPX MIDI In")->make();
            check (lp->start() == Messages { { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7 } }, "launchpad: into programmer mode");
            const auto first = lp->refresh (state);
            bool topLeftRed = false;
            for (const auto& m : first)
                for (size_t i = 7; i + 4 < m.size(); ++i)
                    topLeftRed |= m[i] == 0x03 && m[i + 1] == 81 && m[i + 2] == 0x7F && m[i + 3] == 0 && m[i + 4] == 0;
            check (! first.empty() && first[0][6] == 0x03 && topLeftRed, "launchpad: the top-left pad in its track's red");
            check (lp->refresh (state).empty(), "launchpad: nothing sent again when nothing changed");
            check (launched (send (*lp, { 0x90, 81, 100 }), 0, 0) && launched (send (*lp, { 0x90, 12, 100 }), 1, 7), "launchpad: pads launch their slots");
            check (send (*lp, { 0x90, 81, 0 }).commands.empty(), "launchpad: a pad let go does nothing");
            send (*lp, { 0xB0, 92, 127 });   // down a scene
            check (launched (send (*lp, { 0x90, 81, 100 }), 0, 1), "launchpad: the arrows move the grid down the scenes");
            const auto scene = send (*lp, { 0xB0, 89, 127 });
            check (scene.commands.size() == 1 && scene.commands[0].action == Action::launchScene && scene.commands[0].scene == 1, "launchpad: scene buttons launch scenes");
            send (*lp, { 0xB0, 96, 127 });   // Note view
            const auto on = send (*lp, { 0x90, 11, 90 });
            const auto off = send (*lp, { 0x90, 11, 0 });
            check (on.notes == Messages { { 0x90, 48, 90 } } && off.notes == Messages { { 0x80, 48, 0 } } && on.commands.empty(),
                   "launchpad: in Note view the pads play, C3 at the bottom left");
            check (send (*lp, { 0x90, 12, 90 }).notes == Messages { { 0x90, 50, 90 } }, "launchpad: in key, the next pad is the next note of the scale");
        }

        // Push 2: pads from the bottom left, relative encoders, held Mute.
        {
            auto push = kindOf ("Ableton Push 2 MIDI 1")->make();
            check (push->start().size() >= 64, "push: its palette written at the start");
            push->refresh (state);
            check (launched (send (*push, { 0x90, 36, 100 }), 0, 7) && launched (send (*push, { 0x90, 99, 100 }), 7, 0) == false,
                   "push: the bottom-left pad is track 1's eighth scene; tracks the mix lacks do nothing");
            const auto up = send (*push, { 0xB0, 71, 1 }), down = send (*push, { 0xB0, 72, 127 });
            check (up.commands.size() == 1 && up.commands[0].action == Action::volumeBy && up.commands[0].track == 0 && up.commands[0].value > 0.0f
                       && down.commands.size() == 1 && down.commands[0].track == 1 && down.commands[0].value < 0.0f,
                   "push: the encoders turn the faders up and down");
            check (send (*push, { 0xB0, 85, 127 }).commands == std::vector<Command> { { Action::togglePlay } }, "push: Play plays");
            send (*push, { 0xB0, 60, 127 });
            const auto muted = send (*push, { 0xB0, 21, 127 });
            check (muted.commands.size() == 1 && muted.commands[0].action == Action::mute && muted.commands[0].track == 1
                       && send (*push, { 0xB0, 60, 0 }).commands.empty(),
                   "push: Mute held with a track button mutes that track");
            send (*push, { 0xB0, 60, 127 });
            const auto tap = send (*push, { 0xB0, 60, 0 });
            check (tap.commands.size() == 1 && tap.commands[0].action == Action::mute && tap.commands[0].track == -1, "push: Mute tapped mutes the selected track");
        }

        // Launchkey: DAW mode, two rows of pads, absolute knobs, drum pads playing.
        {
            auto lk = kindOf ("Launchkey MK3 49 LKMK3 DAW In")->make();
            check (lk->start()[0] == Message { 0x9F, 0x0C, 0x7F }, "launchkey: DAW mode on");
            lk->refresh (state);
            check (launched (send (*lk, { 0x90, 96, 100 }), 0, 0) && launched (send (*lk, { 0x90, 113, 100 }), 1, 1), "launchkey: its two rows are two scenes");
            const auto knob = send (*lk, { 0xBF, 0x15, 127 });
            check (knob.commands.size() == 1 && knob.commands[0].action == Action::volume && knob.commands[0].value == 1.0f, "launchkey: knobs set volumes");
            check (send (*lk, { 0x99, 36, 100 }).notes == Messages { { 0x99, 36, 100 } }, "launchkey: drum pads play the armed track");
        }

        // MIDI Learn.
        {
            Learn learn;
            std::vector<Command> out;
            std::optional<Mapping> learned;
            Command volume { Action::volume, 2 };
            learn.learn (volume, true);
            check (learn.handle ("Grid", 0xB0, 1, 32, 64, out, &learned) && learned.has_value() && out.empty(), "learn: the next control moved is learned");
            out.clear();
            check (learn.handle ("Grid", 0xB0, 1, 32, 127, out) && out.size() == 1 && out[0].action == Action::volume && out[0].track == 2 && out[0].value == 1.0f,
                   "learn: the learned knob sets the volume");
            out.clear();
            check (! learn.handle ("Keytar", 0xB0, 1, 32, 127, out) && ! learn.handle ("Grid", 0xB0, 2, 32, 127, out),
                   "learn: another device or channel is left alone");
            auto maps = learn.mappings();
            maps[0].mode = Mapping::Mode::relativeTwos;
            learn.setMappings (maps);
            learn.handle ("Grid", 0xB0, 1, 32, 127, out);
            check (out.size() == 1 && out[0].action == Action::volumeBy && out[0].value < 0.0f, "learn: an endless encoder turns it down");

            out.clear();
            learn.learn ({ Action::togglePlay }, false);
            learn.handle ("Keytar", 0x90, 1, 60, 100, out);
            learn.handle ("Keytar", 0x80, 1, 60, 0, out);
            check (learn.handle ("Keytar", 0x90, 1, 60, 100, out) && out.size() == 1 && out[0].action == Action::togglePlay
                       && learn.handle ("Keytar", 0x80, 1, 60, 0, out) && out.size() == 1,
                   "learn: a learned key plays and stops, and no longer sounds");
            out.clear();
            learn.learn ({ Action::metronome }, false);
            learn.handle ("Pad", 0xB0, 1, 20, 127, out);
            learn.handle ("Pad", 0xB0, 1, 20, 0, out);
            learn.handle ("Pad", 0xB0, 1, 20, 127, out);
            learn.handle ("Pad", 0xB0, 1, 20, 127, out);
            check (out.size() == 1, "learn: a CC button acts once a press");
        }
    }

    void testXenTuner()
    {
        using namespace phi::xen;
        std::string error;

        // The config format: the reference, the nominals and equave, a chain; arithmetic in the numbers.
        Config twelve;
        check (parse ("A4: 440\n0c 200c 300c 500c 700c 800c 1000c 1200c\nbb b (100c) # x // a comment\naux(0)\n", twelve, error),
               "xen: a 12-TET config is read");
        check (twelve.nominals.size() == 7 && twelve.chains.size() == 1 && twelve.chains[0].central == 2 && near (twelve.equave, 1200.0, 1e-9),
               "xen: its nominals, equave and chain");
        check (near (twelve.referencePitch(), 6900.0, 1e-9) && twelve.referenceStaff() == 33, "xen: A4 at 440 Hz is pitch 6900, staff A4");
        Speller sp (twelve);
        auto cSharp = sp.best (6100.0);
        check (cSharp.staff == 28 && cSharp.symbols.size() == 1 && cSharp.symbols[0].code == symbolCode ("#") && near (cSharp.off, 0.0, 1e-6),
               "xen: C#4 spelled on C with a sharp");
        auto dFlat = sp.best (6100.0, true);
        check (dFlat.staff == 29 && dFlat.symbols.size() == 1 && dFlat.symbols[0].code == symbolCode ("b"), "xen: or D-flat, flats first");
        check (sp.spell (6100.0).size() >= 2, "xen: its enharmonic spellings come with it");
        auto between = sp.best (6114.0);
        check (between.staff == 28 && near (between.off, 14.0, 1e-6), "xen: a pitch between is spelled with the cents it is off");
        check (sp.stepsPerEquave() == 12 && near (sp.next (6000.0, 1), 6100.0, 1e-6) && near (sp.next (6000.0, -1), 5900.0, 1e-6),
               "xen: up and down by the notated pitches");
        check (codePoint (symbolCode ("#")) == 0xE262 && smuflName (symbolCode ("b")) == "accidentalFlat", "xen: symbols are SMuFL glyphs");

        Config sums;
        check (parse ("C4: 440*16/27\n0 9/8 81/64 4/3 3/2 27/16 243/128 2/1\nb(Math.pow(2048/2187,1)) (0) #(2187/2048)\n'-' (1200*1/53c) '+'\n", sums, error),
               "xen: ratios, Math and text accidentals");
        check (near (sums.referencePitch(), 6900.0 - 1200.0 * std::log2 (27.0 / 16.0), 1e-6) && near (*sums.nominals[4], 701.955, 0.001)
                   && near (sums.chains[0].cents[2], 113.685, 0.001) && near (sums.chains[1].cents[2], 1200.0 / 53.0, 1e-9)
                   && sums.chains[1].symbols[0][0].text == "-",
               "xen: their values");
        Config bad;
        check (! parse ("A4: 440\n0c 200c 1200c\nbb b # x\n", bad, error) && ! error.empty(), "xen: a chain without its step is refused");
        check (! parse ("hello\nworld\n", bad, error), "xen: text that is not a config is refused");

        // Xen Tuner's own library: HEJI spells 5/4 above a tonic as its letter with one arrow.
        const std::string tunings = std::string (PHI_SHARE_SOURCE_DIR) + "/xen-tuner/tunings";
        Config heji;
        check (read (tunings + "/heji/5 limit.txt", heji, error) && heji.ligatures.size() >= 2 && ! heji.secondary.empty(), "xen: HEJI is read");
        const auto onC = Speller (heji).anchoredAt (6000.0);
        const auto third = onC.best (6000.0 + 1200.0 * std::log2 (5.0 / 4.0));
        check (third.staff == 30 && third.symbols.size() == 1 && near (third.off, 0.0, 0.01), "xen: 5/4 is E with an arrow down, in HEJI");
        const auto fifth = onC.best (6000.0 + 1200.0 * std::log2 (3.0 / 2.0));
        check (fifth.staff == 32 && fifth.symbols.empty() && near (fifth.off, 0.0, 0.01), "xen: 3/2 is a plain G");
        const auto minor = onC.best (6000.0 + 1200.0 * std::log2 (6.0 / 5.0));
        check (minor.staff == 30 && minor.symbols.size() == 1 && near (minor.off, 0.0, 0.01), "xen: 6/5 is E-flat with an arrow up, as one ligature");

        // Ups and downs in 31-EDO: up and down step by step.
        Config upDown;
        check (read (tunings + "/updown/31edo.txt", upDown, error), "xen: ups and downs is read");
        const auto ud = Speller (upDown).anchoredAt (6000.0);
        check (ud.stepsPerEquave() == 31 && near (ud.next (6000.0, 1), 6000.0 + 1200.0 / 31.0, 1e-6), "xen: 31 steps, a step at a time");

        // Every config shipped is read.
        int read = 0, failed = 0;
        for (const auto& f : std::filesystem::recursive_directory_iterator (tunings))
            if (f.path().extension() == ".txt")
            {
                Config c;
                if (phi::xen::read (f.path().string(), c, error) && Speller (c).valid())
                    ++read;
                else
                {
                    ++failed;
                    std::printf ("  %s: %s\n", f.path().string().c_str(), error.c_str());
                }
            }
        check (read > 200 && failed == 0, "xen: every tuning config shipped is read");

        // PHI's notations for the traditions.
        Config arabic;
        check (notation::load ({ notation::Kind::staff, "phi:arabic quarter tones", {} }, tunings, arabic, error), "notation: the maqam config is read");
        const Speller ar (arabic);
        const auto halfFlat = ar.best (6350.0);
        check (halfFlat.staff == 30 && halfFlat.symbols.size() == 1 && halfFlat.symbols[0].code == symbolCode ("b/"), "notation: E half-flat");
        const auto quarters = ar.spell (6250.0);
        check (! quarters.empty() && quarters[0].staff == 29 && quarters[0].symbols[0].code == symbolCode ("+")
                   && std::any_of (quarters.begin(), quarters.end(), [] (const Spelling& q)
                                   { return q.staff == 30 && q.symbols.size() == 1 && q.symbols[0].code == symbolCode ("|b"); }),
               "notation: D half-sharp, or E three-quarter flat in one sign");
        Config persian;
        check (notation::load ({ notation::Kind::staff, "phi:persian koron and sori", {} }, tunings, persian, error), "notation: the dastgah config is read");
        const auto koron = Speller (persian).best (6350.0);
        check (koron.staff == 30 && koron.symbols.size() == 1 && codePoint (koron.symbols[0].code) == 0xE460, "notation: E koron");
        for (const auto n : { 5, 7, 17, 19, 22, 41, 72 })
        {
            Config edo;
            check (parse (notation::upsAndDowns (n), edo, error) && Speller (edo).stepsPerEquave() == n,
                   "notation: ups and downs in " + std::to_string (n) + "-EDO writes every step");
        }

        // PHI's staff for every system by default, each tradition's own beside it.
        const auto& all = mixer::tuningSystems();
        const auto first = [&] (const std::string& family) -> const mixer::TuningSystem&
        { return *std::find_if (all.begin(), all.end(), [&] (const mixer::TuningSystem& t) { return t.family == family; }); };
        for (const auto* family : { "raga", "javanese gamelan" })
            check (notation::suggested (noteedit::fromTuning (first (family), 0), tunings).kind == notation::Kind::staff,
                   std::string ("notation: PHI's staff by default for ") + family);
        check (notation::traditional (noteedit::fromTuning (first ("raga"), 0), tunings).kind == notation::Kind::bhatkhande, "notation: a raga's own is Bhatkhande's sargam");
        check (notation::traditional (noteedit::fromTuning (first ("javanese gamelan"), 0), tunings).kind == notation::Kind::angka, "notation: gamelan's own is not angka");
        check (notation::traditional (noteedit::fromTuning (first ("maqam"), 0), tunings).config == "phi:arabic quarter tones", "notation: a maqam's own is its half-flats");
        check (notation::suggested (noteedit::fromTuning (first ("maqam"), 0), tunings).config == "phi:arabic quarter tones", "notation: a maqam with half-flats");
        check (notation::suggested (noteedit::fromTuning (first ("dastgah"), 0), tunings).config == "phi:persian koron and sori", "notation: a dastgah with koron");
        check (notation::suggested (noteedit::fromTuning (first ("church mode"), 0), tunings).config == "heji/5 limit.txt", "notation: a Pythagorean mode in HEJI");
        check (notation::suggested (noteedit::twelveTet (0), tunings).config == "phi:12-TET", "notation: 12-TET with sharps and flats");
        noteedit::PitchSystem edo31;
        edo31.family = "equal";
        for (int i = 0; i < 31; ++i)
            edo31.degrees.push_back (i * 1200.0 / 31.0);
        check (notation::equalDivisions (edo31) == 31 && notation::suggested (edo31, tunings).config == "sagittal/31edo revo.txt", "notation: 31-EDO in Sagittal");

        // Cipher: sargam's names with the octave in dots, jianpu's numbers.
        const auto raga = noteedit::fromTuning (first ("raga"), 0);
        const auto sa = notation::cipher (raga, raga.tonic + 1200.0, notation::CipherStyle::tradition);
        check (sa.name == "S" && sa.period == 1 && sa.off.empty(), "notation: Sa an octave up, a dot above");
        noteedit::PitchSystem gong;
        gong.family = "chinese mode";
        gong.degrees = { 0, 200, 400, 700, 900 };
        check (notation::cipher (gong, 6400.0, notation::CipherStyle::jianpu).name == "3" && notation::cipher (gong, 5700.0, notation::CipherStyle::jianpu).period == -1,
               "notation: jianpu numbers, a dot below an octave down");
    }

    // The notations written beat by beat (NotationGrid), LilyPond's key signatures, and the lyrics,
    // marks and cycles they read from the clip.
    void testNotationViews()
    {
        using namespace noteedit;
        std::string error;
        const std::string tunings = std::string (PHI_SHARE_SOURCE_DIR) + "/xen-tuner/tunings";

        // The Turkish makam staff: Arel-Ezgi-Uzdilek's commas.
        phi::xen::Config aeu;
        notation::Choice makam;
        makam.config = "phi:turkish aeu";
        check (notation::load (makam, tunings, aeu, error), "notation: the makam config is read");
        const phi::xen::Speller turkish (aeu);
        const double comma = 1200.0 / 53.0;
        const auto koma = turkish.best (6000.0 + 8 * comma);
        check (koma.staff == 29 && phi::xen::asText (koma.symbols) == "aeu-1" && near (koma.off, 0.0, 0.01), "notation: D with a koma flat");
        const auto bakiye = turkish.best (6000.0 + 4 * comma);
        check (bakiye.staff == 28 && phi::xen::asText (bakiye.symbols) == "aeu+4", "notation: C with a bakiye sharp");

        // LilyPond's key signatures, transposed to the tonic.
        check (notation::keySignatures().size() > 200, "notation: LilyPond's maqam, makam and dastgah keys");
        const auto* rast = notation::findKeySignature ("turkish", "rast");
        check (rast != nullptr, "notation: makam rast's key");
        if (rast != nullptr)
        {
            const auto g = notation::transpose (*rast, 4, 0, notation::naturals ("turkish"));
            check (near (g[3], 90.566, 0.01) && near (g[6], -22.642, 0.01), "notation: rast on G, F with a bakiye sharp and B a koma flat");
        }
        const auto* bayati = notation::findKeySignature ("arabic", "bayati");
        check (bayati != nullptr, "notation: maqam bayati's key");
        if (bayati != nullptr)
        {
            const auto d = notation::transpose (*bayati, 1, 0, notation::naturals ("arabic"));
            check (near (d[2], -50.0, 1e-6) && near (d[6], -100.0, 1e-6), "notation: bayati on D, E half-flat and B flat");
        }

        // A phrase of rāg yaman in tīntāl: four svaras two to a mātrā, then four a mātrā each.
        const auto& all = mixer::tuningSystems();
        const auto yaman = fromTuning (all[(size_t) mixer::findTuning ("yaman (kalyan)")], 0);
        Sequence s;
        s.ticksPerQuarter = 480;
        const double degrees[] = { 0, 200, 400, 600, 700, 900, 1100, 1200 };
        int64_t at = 0;
        for (int i = 0; i < 8; ++i)
        {
            Note n;
            n.start = at;
            n.length = i < 4 ? 240 : 480;
            n.pitch = 6000.0 + degrees[i];
            s.notes.push_back (n);
            at += n.length;
        }
        s.notes[0].lyric = "ga";
        s.cycles.push_back ({ 0, "tintal", 480 });
        s.sort();

        const auto bh = notation::bhatkhande (s, yaman, notation::Script::devanagari, 0, 16 * 480);
        check (bh.beats.size() == 16 && bh.beats[0].sign == "×" && bh.beats[4].sign == "२" && bh.beats[8].sign == "०" && bh.beats[12].sign == "३",
               "notation: tīntāl's sam, tālīs and khālī");
        check (bh.beats[0].grouped && bh.beats[0].signs.size() == 2 && bh.beats[0].signs[0].text == "सा" && bh.beats[0].signs[0].lyric == "ga",
               "notation: two svaras in a mātrā, the bol under Sa");
        check (bh.beats[1].signs[1].raised && bh.beats[5].signs[0].octave == 1 && bh.beats[6].signs[0].kind == notation::Sign::Kind::rest,
               "notation: tīvra Ma, tār Sa, then a rest");

        const auto ca = notation::carnatic (s, yaman, notation::Script::roman, 0, 8 * 480);
        check (ca.doubleBarAtCycle && ca.beats[0].signs.size() == 2 && ca.beats[0].signs[0].lines == 1 && ca.beats[2].signs[0].lines == 0,
               "notation: Karnatak speed lines over two svaras an akṣara");

        Sequence plain = s;
        plain.cycles.clear();
        const auto ji = notation::jianpu (plain, yaman, false, 0, 8 * 480);
        check (ji.beats.size() == 8 && ji.beats[0].signs[0].text == "1" && ji.beats[0].signs[0].lines == 1
                   && ji.beats[1].signs[1].before == "♯" && ji.beats[1].signs[1].text == "4" && ji.beats[6].signs[0].text == "0",
               "notation: jianpu's numbers, quaver lines, ♯4 and a rest");

        const auto jg = notation::jeongganbo (plain, yaman, 0, 8 * 480);
        check (jg.layout == notation::Writing::columns && jg.beats[0].rows == 2 && jg.beats[2].signs[0].text == "姑" && jg.beats[6].signs[0].text == "△",
               "notation: jeongganbo's boxes, gojeong for G, a rest");
        int octave = 0;
        check (notation::yulmyeong (6300.0, octave) == "黃" && octave == 0, "notation: hwangjong at E♭4");
        check (! notation::sigimsae().empty(), "notation: the sigimsae table");

        // Lyrics, marks and the cycle are saved in the clip's file and read back.
        s.notes[2].mark = "dā";
        const auto encoded = encode (s, {});
        const auto decoded = decode (*encoded.song);
        check (decoded.cycles.size() == 1 && decoded.cycles[0].rhythm == "tintal" && decoded.cycles[0].beat == 480, "notation: the cycle is saved");
        check (decoded.notes.size() == 8 && decoded.notes[0].lyric == "ga" && decoded.notes[2].mark == "dā", "notation: a lyric and a mark are saved");

        // Every notation offered by id.
        for (const auto& system : notation::systems())
        {
            notation::Choice c;
            check (notation::fromId (system.id, yaman, tunings, c), "notation: " + system.id + " is offered");
        }
        notation::Choice tradition;
        check (notation::fromId ("tradition", yaman, tunings, tradition) && tradition.kind == notation::Kind::bhatkhande, "notation: a rāga's tradition writes sargam");
    }

    void testScala()
    {
        using namespace phi::scala;
        std::string error;
        Scale s;
        const char* meantone =
            "! meantone.scl\n!\n1/4-comma meantone\n 4\n!\n 193.15686\n 5/4 major third\n 3/2\n 2/1\n";
        check (parseScl (meantone, s, error) && s.size() == 4 && s.description == "1/4-comma meantone", "scala: a .scl is read");
        check (near (s.cents[1], 386.3137, 0.001) && near (s.period(), 1200.0, 1.0e-9), "scala: ratios read as cents");
        check (s.degrees().size() == 4 && s.degrees()[0] == 0.0, "scala: the degrees within the period, the tonic first");
        Scale back;
        check (parseScl (writeScl (s, "meantone.scl"), back, error) && back.text[1] == "5/4" && near (back.cents[0], s.cents[0], 1.0e-4),
               "scala: written back, a ratio stays a ratio");
        check (! parseScl ("x\n 3\n 100.0\n", s, error) && ! error.empty(), "scala: a count the pitches don't match is refused");
        check (! parseScl ("x\n 1\n -3/2\n", s, error), "scala: a negative ratio is refused");

        // A linear map: key 60 the tonic at middle C; each key the next degree.
        Scale twelve = fromCents ({ 0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100 });
        const auto map = linearMap (60);
        check (near (keyHz (twelve, map, 69), 440.0, 1.0e-6) && near (keyHz (twelve, map, 48), 130.8128, 1.0e-3),
               "scala: twelve equal steps on a linear map is 12-TET");
        Scale five = fromCents ({ 0, 240, 480, 720, 960 });
        const auto t = toTuning (five, linearMap (60));
        check (near (t.hz (65) / t.hz (60), 2.0, 1.0e-9) && near (phi::Tuning::ratioToCents (t.hz (61) / t.hz (60)), 240.0, 1.0e-6),
               "scala: five steps a key, the octave five keys up");

        KeyboardMap kbm;
        const char* whiteKeys = "! white.kbm\n12\n0\n127\n60\n69\n440.0\n7\n0\nx\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n";
        check (parseKbm (whiteKeys, kbm, error) && kbm.size == 12 && kbm.mapping[1] == -1 && kbm.mapping[11] == 6, "scala: a .kbm is read");
        Scale seven = fromCents ({ 0, 200, 400, 500, 700, 900, 1100 });
        check (keyHz (seven, kbm, 61) == 0.0 && near (keyHz (seven, kbm, 72) / keyHz (seven, kbm, 60), 2.0, 1.0e-9)
                   && near (keyHz (seven, kbm, 69), 440.0, 1.0e-9),
               "scala: a mapped key plays its degree, an x plays nothing, the reference key its frequency");
        KeyboardMap again;
        check (parseKbm (writeKbm (kbm), again, error) && again.mapping == kbm.mapping && again.octaveDegree == 7, "scala: a .kbm written back");
    }

    void testNoteEditor()
    {
        using namespace noteedit;
        // A pitch system from a maqam, and its names.
        const auto rast = mixer::findTuning ("rast");
        check (rast >= 0, "note editor: rast is a tuning");
        const auto r = fromTuning (mixer::tuningSystems()[(size_t) rast], 0);
        check (r.size() == 7 && near (r.pitchOf (2), 6350.0, 1.0e-9) && near (r.pitchOf (7), 7200.0, 1.0e-9) && near (r.pitchOf (-1), 5850.0, 1.0e-9),
               "note editor: rast's degrees from C, up and down");
        check (r.nearestStep (6340.0) == 2 && r.nearestStep (7190.0) == 7 && r.nearestStep (5000.0) < 0, "note editor: the nearest degree");
        check (label (r, 2, Naming::tradition) == "E½♭4", "note editor: a maqam's neutral third spelled E half-flat: " + label (r, 2, Naming::tradition));
        check (letterName (6000.0 + 1050.0, true) == "B½♭4" && letterName (6450.0, true) == "E½♯4", "note editor: quarter-tone letters");
        check (letterName (6914.0, false) == "A4 +14", "note editor: a letter and the cents off it");
        const auto steps = r.steps (6000.0, 7200.0);
        check (steps.size() == 8 && steps.front() == 0 && steps.back() == 7, "note editor: the rows of an octave");

        const auto yaman = mixer::findTuning ("yaman (kalyan)");
        check (yaman >= 0, "note editor: yaman is a tuning");
        if (yaman >= 0)
        {
            const auto y = fromTuning (mixer::tuningSystems()[(size_t) yaman], 2);
            check (label (y, 0, Naming::tradition) == "S" && label (y, 3, Naming::tradition) == "M"
                       && label (y, 7, Naming::tradition) == "S\xCC\x87" && label (y, -1, Naming::tradition) == "N\xCC\xA3",
                   "note editor: sargam, tīvra ma, a dot above and below for the octave");
        }
        const auto slendro = mixer::findTuning ("slendro");
        check (slendro >= 0 && label (fromTuning (mixer::tuningSystems()[(size_t) slendro], 0), 3, Naming::tradition) == "5",
               "note editor: sléndro's tones by their kepatihan numbers (no 4)");
        const auto gong = mixer::findTuning ("shang");
        check (gong >= 0 && label (fromTuning (mixer::tuningSystems()[(size_t) gong], 2), 0, Naming::tradition).rfind ("shāng", 0) == 0,
               "note editor: the shāng mode starts on shāng");
        check (label (twelveTet(), 13, Naming::degrees) == "2'" && label (twelveTet(), 1, Naming::letters) == "C♯4", "note editor: degrees and letters");

        // Decode and encode: MPE keeps a pitch between the keys and comes back the same.
        Sequence s;
        s.notes.push_back ({ 0, 480, 6350.0, 100 });
        s.notes.push_back ({ 0, 480, 6700.0, 90 });
        s.notes.push_back ({ 480, 240, 7050.0, 80 });
        s.notes.push_back ({ 480, 240, 3600.0, 110, 64, 9 });   // a drum
        check (suggestedEncoding (s) == Encoding::mpe, "note editor: notes between keys want MPE");
        const auto mpe = encode (s, {});
        check (mpe.clashes == 0 && mpe.song->noteCount() == 4, "note editor: MPE, a channel a note");
        {
            phi::Mpe zone;
            std::map<int, double> heard;
            for (const auto& e : mpe.song->events)
            {
                const auto m = zone.handle (e.status, e.data1, e.data2);
                if (m.kind == phi::Mpe::Kind::noteOn && m.channel != 9)
                    heard[(int) std::lround (100.0 * (m.key + zone.expression (m.channel).bendSemitones))] = 1;
            }
            check (heard.count (6350) && heard.count (6700) && heard.count (7050), "note editor: an MPE synth hears each pitch");
        }
        auto back = decode (*mpe.song);
        check (back.notes.size() == 4, "note editor: decoded, every note");
        bool same = back.notes.size() == 4;
        s.sort();   // in the order decode() gives: by start, then pitch
        for (size_t i = 0; same && i < 4; ++i)
            same = near (back.notes[i].pitch, s.notes[i].pitch, 0.6) && back.notes[i].length == s.notes[i].length;
        check (same, "note editor: MPE round trip keeps pitch and length");
        check (back.other.empty(), "note editor: the MPE set-up and the notes' bends are not left as events");
        {
            const auto bytes = midifile::bytes (*mpe.song);
            std::string why;
            const auto file = midifile::parse (bytes.data(), bytes.size(), why);
            const auto again = file != nullptr ? decode (*file) : Sequence {};
            check (again.notes.size() == 4 && near (again.notes[1].pitch, s.notes[1].pitch, 0.6) && again.other.empty(),
                   "note editor: an MPE clip written as a .mid and read back");
        }
        const auto drum = std::find_if (back.notes.begin(), back.notes.end(), [] (const Note& n) { return n.channel == 9; });
        check (drum != back.notes.end() && drum->pitch == 3600.0, "note editor: drums stay on channel 10");

        EncodeOptions shared;
        shared.encoding = Encoding::bendChannels;
        shared.bendRange = 2.0;
        Sequence chord;
        for (int i = 0; i < 6; ++i)
            chord.notes.push_back ({ 0, 480, 6000.0 + 200.0 * i + (i % 2 ? 50.0 : 0.0) });
        const auto bend = encode (chord, shared);
        check (bend.channels == 2 && decode (*bend.song).notes.size() == 6, "note editor: notes bent alike share a channel");
        const auto keys = encode (chord, { Encoding::keys });
        check (keys.channels == 1 && decode (*keys.song).notes[1].pitch == 6300.0, "note editor: keys, each note on its nearest key");

        // Edits.
        Sequence q;
        q.notes.push_back ({ 10, 100, 6000.0 });
        q.notes.push_back ({ 130, 100, 6200.0 });
        q.notes.push_back ({ 250, 300, 6400.0 });
        quantizeStarts (q, 120);
        check (q.notes[0].start == 0 && q.notes[1].start == 120 && q.notes[2].start == 240, "note editor: quantised to the grid");
        legato (q);
        check (q.notes[0].length == 120 && q.notes[1].length == 120, "note editor: legato");
        q.notes[1].selected = true;
        transposeSteps (q, twelveTet(), 2);
        check (q.notes[1].pitch == 6400.0 && q.notes[0].pitch == 6000.0, "note editor: only the picked note moves");
        selectAll (q, false);
        transposeSteps (q, r, 1);   // into rast, a degree up
        check (near (q.notes[0].pitch, 6200.0, 1e-9) && near (q.notes[2].pitch, 6500.0, 1e-9), "note editor: a degree up in rast");
        splitAt (q, 300);
        check (q.notes.size() == 4, "note editor: split");
        glue (q);
        check (q.notes.size() == 3, "note editor: glued again");
        rampVelocity (q, 40, 120);
        check (q.notes.front().velocity == 40 && q.notes.back().velocity == 120, "note editor: velocity ramp");

        // Live's edits: duplicate, duplicate and push, delete and close the gap, chop.
        {
            Sequence d;
            d.notes = { { 0, 100, 6000.0 }, { 120, 100, 6200.0 }, { 480, 120, 6400.0 } };
            d.notes[0].selected = d.notes[1].selected = true;
            duplicate (d, 120);
            check (d.notes.size() == 5 && d.notes[2].start == 240 && d.notes[3].start == 360 && d.notes[2].selected && ! d.notes[0].selected
                       && d.notes[4].start == 480,
                   "note editor: duplicated after the span, rounded to the grid");
            duplicate (d, 120, true);
            check (d.notes.size() == 7 && d.notes[4].start == 480 && d.notes[5].start == 600 && d.notes[6].start == 720 && ! d.notes[6].selected,
                   "note editor: duplicate and push moves the later notes on");
            deleteAndClose (d, 120);
            check (d.notes.size() == 5 && d.notes[4].start == 480 && d.notes[4].pitch == 6400.0, "note editor: deleted and the gap closed");
            invertSelection (d);
            check (d.notes[4].selected && d.notes[0].selected, "note editor: selection inverted");
            selectAll (d, false);
            d.notes[4].selected = true;
            chop (d, 40);
            check (d.notes.size() == 7 && d.notes[5].start == 520 && d.notes[6].length == 40, "note editor: chopped on the grid");
            lengthen (d, -100, 10);
            check (d.notes[6].length == 10, "note editor: shortened, not below the shortest");
        }

        // Restyle: C major's degrees into sléndro keep the line rising.
        Sequence melody;
        for (int i = 0; i < 8; ++i)
            melody.notes.push_back ({ i * 120, 120, 6000.0 + (double[]) { 0, 200, 400, 500, 700, 900, 1100, 1200 }[i] });
        const auto key = detectKey (melody);
        check (key.root == 0 && ! key.minor, "note editor: C major found");
        const auto ionian = fromTuning (mixer::tuningSystems()[(size_t) mixer::findTuning ("ionian")], 0);
        const auto to = fromTuning (mixer::tuningSystems()[(size_t) slendro], 0);
        restyle (melody, ionian, to);
        bool rising = true;
        for (size_t i = 1; i < melody.notes.size(); ++i)
            rising = rising && melody.notes[i].pitch > melody.notes[i - 1].pitch;
        check (rising && near (melody.notes[5].pitch, 7200.0, 1e-6), "note editor: restyled by degree, the contour kept");

        // Step input: three notes, a rest, a chord.
        Sequence st;
        StepInput in;
        stepNote (st, in, 6000.0);
        stepNote (st, in, 6200.0);
        stepRest (st, in);
        in.chord = true;
        stepNote (st, in, 6400.0);
        stepNote (st, in, 6700.0);
        check (st.notes.size() == 4 && st.notes[2].start == 360 && st.notes[3].start == 360 && in.position == 360, "note editor: step input");
        in.chord = false;
        stepRest (st, in);
        stepBack (st, in);
        check (st.notes.size() == 2 && in.position == 360, "note editor: step back deletes the step's notes");

        // A controller lane.
        drawLane (st, 1, 0, 0, 480, 0, 127, 120);
        const auto pts = lanePoints (st, 1, 0);
        check (pts.size() == 5 && pts.front().value == 0 && pts.back().value == 127, "note editor: a modulation ramp drawn");
        eraseLane (st, 1, 0, 0, 240);
        check (lanePoints (st, 1, 0).size() == 2, "note editor: part of a lane erased");

        // The views' measures.
        check (roughness ({ 6000.0 }) == 0.0, "note editor: one note is not rough");
        const auto fifth = roughness ({ 6000.0, 6700.0 }), semitone = roughness ({ 6000.0, 6100.0 }), tritone = roughness ({ 6000.0, 6600.0 });
        const auto octave = roughness ({ 6000.0, 7200.0 }), neutral = roughness ({ 6000.0, 6350.0 });
        check (octave < fifth && fifth < tritone && tritone < semitone, "note editor: octave, fifth, tritone, semitone, roughest last");
        check (neutral > roughness ({ 6000.0, 6400.0 }) * 0.5 && neutral < semitone, "note editor: a neutral third between");
        check (soundingAt (st, 0).size() == 1 && soundingAt (st, 100000).empty(), "note editor: what sounds at a tick");
        const auto use = degreeUsage (st, twelveTet());
        check (use.size() == 12 && use[0] == 120.0 && use[2] == 120.0, "note editor: how long each degree sounds");

        // Pitch curves: a glide written, sent as MPE bends, and read back as the same curve.
        Sequence g;
        g.notes.push_back ({ 0, 480, 6000.0, 100 });
        g.notes.push_back ({ 480, 480, 6700.0, 100 });
        g.notes[0].selected = true;
        glideToNext (g, twelveTet(), 0.5, false);
        check (g.notes[0].curve.size() == 3 && near (g.notes[0].bendAt (240), 0.0, 1.0e-9) && near (g.notes[0].bendAt (480), 700.0, 1.0e-9)
                   && near (g.notes[0].bendAt (360), 350.0, 1.0) && g.notes[1].curve.empty(),
               "note editor: a glide to the next note from halfway");
        const auto curvedBack = decode (*encode (g, {}).song);
        check (curvedBack.notes.size() == 2 && ! curvedBack.notes[0].curve.empty() && near (curvedBack.notes[0].bendAt (470), 700.0 * 470.0 / 480.0 * 2.0 - 700.0, 12.0)
                   && near (curvedBack.notes[0].bendAt (100), 0.0, 1.0) && near (curvedBack.notes[1].pitch, 6700.0, 0.1) && curvedBack.notes[1].curve.empty(),
               "note editor: a curve through MPE and back");
        auto crowd = g;
        crowd.notes.push_back ({ 100, 100, 6000.0, 100 });
        const auto withBends = encode (crowd, { Encoding::bendChannels, 2.0, true });
        int bendsOn = 0;
        for (const auto& e : withBends.song->events)
            bendsOn += e.kind() == 0xE0 ? 1 : 0;
        const auto crowdBack = decode (*withBends.song);
        check (bendsOn >= 10 && crowdBack.notes.size() == 3 && crowdBack.notes[1].start == 100 && crowdBack.notes[1].channel != crowdBack.notes[0].channel,
               "note editor: a curved note keeps its channel to itself");
        glideToNext (g, twelveTet(), 0.0, true);
        check (near (g.notes[0].bendAt (480 / 7), 100.0, 1.0e-9), "note editor: mīṇḍ touches each degree on the way");
        splitAt (g, 240);
        check (g.notes.size() == 3 && near (g.notes[1].bendAt (0), g.notes[0].bendAt (240), 1.0e-9), "note editor: a split curve carries on where it was cut");
        glue (g);
        check (g.notes.size() == 2 && near (g.notes[0].bendAt (480), 700.0, 1.0e-9), "note editor: glued back, the curve whole");
        g.notes[0].selected = true;
        turn (g, r, { 0, 1, 0, -1, 0 }, 40);
        check (near (g.notes[0].bendAt (45), 200.0, 1.0e-9) && near (g.notes[0].bendAt (125), -150.0, 1.0e-9) && near (g.notes[0].bendAt (479), 0.0, 1.0e-9),
               "note editor: murkī, a degree of rast above and below");
        kan (g, r, 1, 30);
        check (near (g.notes[0].bendAt (0), 200.0, 1.0e-9) && near (g.notes[0].bendAt (30), 0.0, 1.0e-9), "note editor: kaṇ");
        swing (g, 30.0, 4.0, 120.0, true);
        double low = 0.0, high = -1.0e9;
        for (int64_t t = 0; t < 480; t += 5)
        {
            low = std::min (low, g.notes[0].bendAt (t));
            high = std::max (high, g.notes[0].bendAt (t));
        }
        check (low < -29.0 && high <= 1.0e-9, "note editor: āndolan swings under the pitch");
        gamak (g, r, 4.0, 120.0);
        check (near (g.notes[0].bendAt (120), 200.0, 1.0e-9) && near (g.notes[0].bendAt (240), 0.0, 1.0e-9), "note editor: gamak to the degree above");
        clearCurves (g);
        check (g.notes[0].curve.empty(), "note editor: curves cleared");

        // Tuning changes within a clip: rast, then from bar 2 sléndro; saved as markers and read back.
        Sequence m;
        m.notes.push_back ({ 0, 480, 6300.0, 100 });
        m.notes.push_back ({ 1920, 480, 6300.0, 100 });
        const auto sl = fromTuning (mixer::tuningSystems()[(size_t) std::max (0, slendro)], 0);
        changeSystem (m, 1920, sl);
        check (systemAt (m, r, 0).name == r.name && systemAt (m, r, 1920).name == sl.name, "note editor: the system in force");
        m.markers.push_back ({ 960, "verse" });
        changeSystem (m, 0, r);
        std::string why;
        const auto bytes = midifile::bytes (*encode (m, {}).song);
        const auto reread = midifile::parse (bytes.data(), bytes.size(), why);
        const auto mb = reread ? decode (*reread) : Sequence {};
        check (mb.systems.size() == 2 && mb.systems[1].tick == 1920 && mb.systems[1].system.name == sl.name && mb.systems[1].system.size() == sl.size()
                   && near (mb.systems[1].system.degrees[2], sl.degrees[2], 1.0e-6) && mb.systems[1].system.names == sl.names
                   && mb.systems[0].system.family == "maqam" && mb.markers.size() == 1 && mb.markers[0].text == "verse",
               "note editor: the systems through a file's markers, the other markers kept");
        // An edit by each stretch's system: up a degree is rast's in the first, sléndro's in the second.
        bySystem (m, r, [] (Sequence& q, const PitchSystem& sys) { transposeSteps (q, sys, 1); });
        check (near (m.notes[0].pitch, 6500.0, 1.0e-6) && near (m.notes[1].pitch, sl.pitchOf (sl.nearestStep (6300.0) + 1), 1.0e-6)
                   && ! m.notes[0].selected && ! m.notes[1].selected,
               "note editor: an edit in each stretch's own system");
        check (removeSystemChange (m, 2000) && m.systems.size() == 1 && systemAt (m, twelveTet(), 2000).name == r.name, "note editor: a change taken out");
        // Hum to notes: C4 for half a second (a blip to D in it), G4, a breath, then E4 slid into from D.
        std::vector<Heard> heard;
        for (int i = 0; i < 160; ++i)
        {
            const auto t = i * 0.01;
            double p = -1.0;
            if (t < 0.5)
                p = (t >= 0.30 && t < 0.32) ? 6200.0 : 6000.0 + 4.0 * std::sin (t * 30.0);
            else if (t < 1.0)
                p = 6700.0;
            else if (t >= 1.2)
                p = t < 1.3 ? 6200.0 + (t - 1.2) / 0.1 * 200.0 : 6400.0;
            heard.push_back ({ t, p, p < 0.0 ? 0.0 : 0.5 });
        }
        const auto sung = notesFromPitch (heard, twelveTet(), {}, 480, 120.0, 960);
        check (sung.size() == 3 && near (sung[0].pitch, 6000.0, 1.0e-9) && near (sung[1].pitch, 6700.0, 1.0e-9) && near (sung[2].pitch, 6400.0, 1.0e-9),
               "note editor: hummed notes, a blip and a slide not notes of their own");
        check (sung.size() == 3 && sung[0].start == 960 && std::abs (sung[0].length - 480) <= 10 && sung[1].start == 960 + 480 && std::abs (sung[2].start - (960 + 1152)) <= 2,
               "note editor: hummed notes in time, at the tempo");
        check (sung.size() == 3 && sung[0].bendAt (290) > 150.0 && sung[1].curve.empty() && ! sung[2].curve.empty() && sung[2].bendAt (0) < -150.0 && near (sung[2].bendAt (300), 0.0, 1.0e-9),
               "note editor: the blip and the slide kept as curves, steady singing left steady");
        check (sung.size() == 3 && sung[0].velocity > 100 && sung[0].velocity < 127, "note editor: hummed loudness as velocity");
        HumOptions asSung;
        asSung.snap = false;
        asSung.grid = 120;
        const auto free = notesFromPitch (heard, twelveTet(), asSung, 480, 120.0, 0);
        check (free.size() == 3 && std::abs (free[0].pitch - 6000.0) < 5.0 && free[2].start % 120 == 0, "note editor: hummed as sung, on the grid");

        // And from sound: a sung line of sines, C4 E4 G4 at 0.4 s each, heard back as those notes.
        {
            const double rate = 44100.0;
            AudioData::Builder b (1, rate);
            std::vector<float> wave;
            for (const double hz : { 261.63, 329.63, 392.0 })
                for (int i = 0; i < (int) (0.4 * rate); ++i)
                    wave.push_back ((float) (0.4 * std::sin (2.0 * 3.14159265358979323846 * hz * i / rate)));
            const float* ch[] { wave.data() };
            b.append (ch, (int) wave.size());
            const auto heardSines = listen (*b.finish());
            const auto line = notesFromPitch (heardSines, twelveTet(), {}, 480, 60.0, 0);
            check (line.size() == 3 && near (line[0].pitch, 6000.0, 1.0e-9) && near (line[1].pitch, 6400.0, 1.0e-9) && near (line[2].pitch, 6700.0, 1.0e-9)
                       && std::abs (line[1].start - 192) < 20,
                   "note editor: notes heard in a recording");
        }

        // A system that repeats at a fourth: the obikhod scale, 0 200 400, period 500.
        if (const auto obikhod = mixer::findTuning ("obikhod scale"); obikhod >= 0)
        {
            const auto ob = fromTuning (mixer::tuningSystems()[(size_t) obikhod], 7);
            check (near (ob.period, 500.0, 1.0e-9) && ob.size() == 3 && near (ob.pitchOf (3), ob.tonic + 500.0, 1.0e-9) && near (ob.pitchOf (5), ob.tonic + 900.0, 1.0e-9),
                   "note editor: a system's own period (the obikhod scale at the fourth)");
        }
        else
            check (false, "note editor: the obikhod scale is a tuning");

        PitchSystem junk;
        check (! parseSystemMarker ("verse", junk) && ! parseSystemMarker ("PHI system: x | y | tonic a | period 1200 | 0 |", junk), "note editor: other markers are not systems");
    }

    // DAWproject: a project as Bitwig writes one (a zip of project.xml, the audio and the plugins'
    // states) read into what PHI holds, and PHI's own written and read back.
    void testDawProject()
    {
        namespace fs = std::filesystem;
        const auto folder = fs::temp_directory_path() / "phi-dawproject-test";
        fs::remove_all (folder);
        fs::create_directories (folder / "unpacked");

        // The zip on its own: stored and deflated entries back as they went in.
        {
            const auto path = (folder / "test.zip").string();
            std::string error;
            zip::Writer w;
            const std::string text (5000, 'a');
            const std::vector<uint8_t> binary { 0, 1, 2, 3, 255 };
            check (w.open (path, error) && w.add ("a/text.txt", text.data(), text.size(), true, error)
                   && w.add ("b.bin", binary.data(), binary.size(), false, error) && w.close (error), "zip: written");
            zip::Reader r;
            std::vector<uint8_t> back;
            check (r.open (path, error) && r.entries().size() == 2, "zip: two entries");
            check (r.find ("a/text.txt") != nullptr && r.find ("a/text.txt")->method == 8, "zip: text deflated");
            check (r.read ("a/text.txt", back, error) && std::string (back.begin(), back.end()) == text, "zip: text back");
            check (r.read ("b.bin", back, error) && back == binary, "zip: binary back");
        }

        // As Bitwig writes it: an instrument track with a CLAP synth, a note clip with a loop, an
        // audio track with a warped clip and a plain one, an FX track the audio sends to, a
        // group, a scene with a clip in it, and tempo automation.
        const std::string xml = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Project version="1.0">
  <Application name="Bitwig Studio" version="5.2"/>
  <Transport>
    <Tempo unit="bpm" value="150.0" id="id0" name="Tempo"/>
    <TimeSignature denominator="8" numerator="6" id="id1"/>
  </Transport>
  <Structure>
    <Track contentType="notes" loaded="true" id="id2" name="Bass" color="#a2eabf">
      <Channel audioChannels="2" destination="id15" role="regular" solo="true" id="id3">
        <Devices>
          <ClapPlugin deviceID="org.surge-synth-team.surge-xt" deviceName="Surge XT" deviceRole="instrument" loaded="true" id="id7" name="Surge XT">
            <Parameters/>
            <Enabled value="true" id="id8" name="On/Off"/>
            <State path="plugins/surge.clap-preset"/>
          </ClapPlugin>
          <Equalizer deviceName="EQ+" deviceRole="audioFX" id="id9"/>
        </Devices>
        <Mute value="false" id="id6" name="Mute"/>
        <Pan max="1.000000" min="0.000000" unit="normalized" value="0.250000" id="id5" name="Pan"/>
        <Volume max="2.000000" min="0.000000" unit="linear" value="0.5" id="id4" name="Volume"/>
      </Channel>
    </Track>
    <Track contentType="tracks" loaded="true" id="id30" name="Drums group">
      <Channel role="submix" id="id31"/>
      <Track contentType="audio" loaded="true" id="id10" name="Loop">
        <Channel audioChannels="2" destination="id31" role="regular" solo="false" id="id11">
          <Mute value="true" id="id12" name="Mute"/>
          <Sends>
            <Send destination="id21" type="pre" id="id13" name="Send">
              <Enable value="true" id="id14"/>
              <Volume max="2" min="0" unit="linear" value="1.0" id="id16"/>
            </Send>
          </Sends>
          <Volume unit="decibel" value="-6.0" id="id17" name="Volume"/>
        </Channel>
      </Track>
    </Track>
    <Track contentType="audio" loaded="true" id="id20" name="Reverb">
      <Channel audioChannels="2" destination="id15" role="effect" id="id21">
        <Volume max="2" min="0" unit="linear" value="1.0" id="id22"/>
      </Channel>
    </Track>
    <Track contentType="audio notes" loaded="true" id="id40" name="Master">
      <Channel audioChannels="2" role="master" id="id15">
        <Volume max="2" min="0" unit="linear" value="0.25" id="id41"/>
      </Channel>
    </Track>
  </Structure>
  <Arrangement id="id50">
    <TempoAutomation unit="bpm" id="id51">
      <RealPoint time="0" value="150"/>
      <RealPoint time="16" value="140"/>
    </TempoAutomation>
    <Lanes timeUnit="beats" id="id52">
      <Lanes track="id2" id="id53">
        <Clips id="id54">
          <Clip time="8.0" duration="6.0" playStart="1.0" loopStart="0.0" loopEnd="4.0">
            <Notes id="id55">
              <Note time="0.0" duration="0.5" channel="0" key="36" vel="1.0" rel="0.5"/>
              <Note time="1.0" duration="0.5" channel="0" key="38" vel="0.5" rel="0.5"/>
              <Note time="3.0" duration="2.0" channel="1" key="40" vel="0.5" rel="0.5"/>
            </Notes>
          </Clip>
        </Clips>
      </Lanes>
      <Lanes track="id10" id="id56">
        <Clips id="id57">
          <Clip time="4" duration="8" playStart="0" contentTimeUnit="beats">
            <Warps contentTimeUnit="seconds" timeUnit="beats" id="id58">
              <Audio algorithm="stretch" channels="2" duration="2.0" sampleRate="44100" id="id59">
                <File path="audio/loop.wav"/>
              </Audio>
              <Warp time="0" contentTime="0"/>
              <Warp time="8" contentTime="2.0"/>
            </Warps>
          </Clip>
          <Clip time="16" duration="2" contentTimeUnit="seconds" playStart="0.5" name="plain">
            <Audio channels="2" duration="2.0" sampleRate="44100" id="id60">
              <File path="audio/loop.wav"/>
            </Audio>
          </Clip>
        </Clips>
        <Points unit="linear" id="id61"><Target parameter="id17"/><RealPoint time="0" value="1"/></Points>
      </Lanes>
    </Lanes>
  </Arrangement>
  <Scenes>
    <Scene id="id70" name="Verse">
      <Lanes timeUnit="beats" id="id71">
        <ClipSlot track="id2" hasStop="true" id="id72">
          <Clip time="0" duration="4" playStart="0">
            <Notes id="id73"><Note time="2" duration="1" channel="0" key="48" vel="0.5" rel="0.5"/></Notes>
          </Clip>
        </ClipSlot>
      </Lanes>
    </Scene>
  </Scenes>
</Project>)";
        const auto archive = (folder / "bitwig.dawproject").string();
        {
            std::string error;
            zip::Writer w;
            const std::vector<uint8_t> wav (1000, 7), clap { 1, 2, 3 };
            const std::string meta = "<MetaData><Title>Night &amp; Day</Title><Artist>Test</Artist></MetaData>";
            w.open (archive, error);
            w.add ("project.xml", xml.data(), xml.size(), true, error);
            w.add ("metadata.xml", meta.data(), meta.size(), true, error);
            w.add ("audio/loop.wav", wav.data(), wav.size(), false, error);
            w.add ("plugins/surge.clap-preset", clap.data(), clap.size(), false, error);
            check (w.close (error), "dawproject: test archive written");
        }
        dawproject::Project p;
        std::string error;
        check (dawproject::read (archive, (folder / "unpacked").string(), p, error), "dawproject: read (" + error + ")");
        check (p.application == "Bitwig Studio" && near (p.bpm, 150.0, 1e-9), "dawproject: application and tempo");
        check (p.numerator == 6 && p.denominator == 8, "dawproject: time signature");
        check (p.title == "Night & Day" && p.artist == "Test", "dawproject: metadata");
        check (near (p.masterDb, -12.04, 0.01), "dawproject: master volume");
        check (p.tracks.size() == 2 && p.returns.size() == 1, "dawproject: two tracks (the group's kept) and a return");
        if (p.tracks.size() == 2 && p.returns.size() == 1)
        {
            const auto& bass = p.tracks[0];
            check (bass.name == "Bass" && bass.colour == "#a2eabf" && bass.notes && bass.solo, "dawproject: bass track");
            check (near (bass.volumeDb, -6.02, 0.01) && near (bass.pan, -0.5, 1e-9), "dawproject: bass volume and pan");
            const auto* inst = bass.instrument();
            check (inst != nullptr && inst->format() == "CLAP" && inst->deviceId == "org.surge-synth-team.surge-xt"
                   && inst->state == std::vector<uint8_t> { 1, 2, 3 }, "dawproject: the CLAP instrument with its state");
            check (bass.devices.size() == 2 && ! bass.devices[1].isPlugin(), "dawproject: the built-in EQ named");
            // Played from beat 1 for 6 beats, looping 0..4: beats 1..4 then 0..3.
            check (bass.noteClips.size() == 1, "dawproject: one note clip");
            if (bass.noteClips.size() == 1)
            {
                const auto& c = bass.noteClips[0];
                check (near (c.start, 8.0 * 60.0 / 150.0, 1e-9) && near (c.lengthBeats, 6.0, 1e-9), "dawproject: note clip placed");
                std::vector<std::pair<double, int>> got;
                for (const auto& n : c.notes)
                    got.emplace_back (n.time, n.key);
                const std::vector<std::pair<double, int>> want { { 0.0, 38 }, { 2.0, 40 }, { 3.0, 36 }, { 4.0, 38 } };
                check (got == want, "dawproject: the loop unrolled");
                check (c.notes.size() == 4 && near (c.notes[1].duration, 1.0, 1e-9) && c.notes[1].channel == 1,
                       "dawproject: a note cut at the loop's end, on its channel");
            }
            const auto& loop = p.tracks[1];
            check (loop.name == "Loop" && loop.mute && near (loop.volumeDb, -6.0, 1e-9), "dawproject: track inside the group");
            check (loop.sends.size() == 1 && loop.sends[0].target == 0 && loop.sends[0].pre && near (loop.sends[0].db, 0.0, 1e-9),
                   "dawproject: pre-fader send to the return");
            check (loop.audioClips.size() == 2, "dawproject: two audio clips");
            if (loop.audioClips.size() == 2)
            {
                const auto& warped = loop.audioClips[0];
                check (near (warped.start, 4.0 * 0.4, 1e-9) && near (warped.length, 8.0 * 0.4, 1e-9) && near (warped.offset, 0.0, 1e-9),
                       "dawproject: warped clip placed");
                check (warped.warped, "dawproject: 2 s over 8 beats at 150 is warped");
                check (fs::exists (warped.file) && fs::file_size (warped.file) == 1000, "dawproject: the audio unpacked");
                const auto& plain = loop.audioClips[1];
                check (near (plain.start, 16.0 * 0.4, 1e-9) && near (plain.offset, 0.5, 1e-9) && near (plain.length, 2.0 * 0.4, 1e-9)
                       && ! plain.warped && plain.name == "plain" && plain.file == warped.file, "dawproject: plain clip, the same file once");
            }
            check (p.returns[0].name == "Reverb", "dawproject: the return");
            check (p.scenes.size() == 1 && p.scenes[0] == "Verse" && bass.slots.size() == 1, "dawproject: the scene and its slot");
            if (bass.slots.size() == 1)
                check (! bass.slots[0].audio && bass.slots[0].noteClip.notes.size() == 1 && near (bass.slots[0].noteClip.lengthBeats, 4.0, 1e-9),
                       "dawproject: the slot's clip");
        }
        auto said = [&] (const std::string& part)
        {
            return std::any_of (p.notes.begin(), p.notes.end(), [&] (const std::string& n) { return n.find (part) != std::string::npos; });
        };
        check (said ("Tempo changes") && said ("group") && said ("Automation") && said ("EQ+") && said ("Warped"),
               "dawproject: what did not carry over is said");

        // Written and read back.
        {
            const auto wav = (folder / "kick.wav").string();
            {
                std::ofstream f (wav, std::ios::binary);
                f << std::string (500, 'k');
            }
            dawproject::Project out;
            out.bpm = 90.0;
            out.numerator = 3;
            out.masterDb = -3.0;
            out.title = "Round";
            dawproject::Track t;
            t.name = "Keys & <more>";
            t.colour = "#112233";
            t.volumeDb = -10.0;
            t.pan = 0.5;
            t.solo = true;
            dawproject::Device synth;
            synth.element = "Vst3Plugin";
            synth.role = "instrument";
            synth.deviceId = "ABCDEF0123456789ABCDEF0123456789";
            synth.name = "Synth";
            synth.state = { 'V', 'S', 'T', '3', 9 };
            t.devices.push_back (synth);
            dawproject::NoteClip nc;
            nc.start = 2.0;   // 3 beats at 90
            nc.lengthBeats = 4.0;
            nc.length = 4.0 * 60.0 / 90.0;
            nc.notes = { { 0.5, 1.0, 60, 2, 0.75, 0.5 } };
            t.noteClips.push_back (nc);
            dawproject::AudioClip ac;
            ac.file = wav;
            ac.start = 1.0;
            ac.offset = 0.25;
            ac.length = 0.5;
            ac.fileLength = 1.0;
            t.audioClips.push_back (ac);
            t.sends.push_back ({ 0, -6.0, false, true });
            dawproject::Slot slot;
            slot.scene = 1;
            slot.noteClip = nc;
            t.slots.push_back (slot);
            out.tracks.push_back (t);
            dawproject::Track ret;
            ret.name = "FX";
            out.returns.push_back (ret);
            const auto file = (folder / "phi.dawproject").string();
            check (dawproject::write (out, file, error), "dawproject: written (" + error + ")");
            fs::create_directories (folder / "back");
            dawproject::Project back;
            check (dawproject::read (file, (folder / "back").string(), back, error), "dawproject: read back (" + error + ")");
            check (near (back.bpm, 90.0, 1e-9) && back.numerator == 3 && near (back.masterDb, -3.0, 1e-6) && back.title == "Round",
                   "dawproject: round trip: transport, master, title");
            check (back.tracks.size() == 1 && back.returns.size() == 1, "dawproject: round trip: tracks");
            if (back.tracks.size() == 1)
            {
                const auto& b = back.tracks[0];
                check (b.name == t.name && b.colour == t.colour && near (b.volumeDb, -10.0, 1e-6) && near (b.pan, 0.5, 1e-9) && b.solo,
                       "dawproject: round trip: mixer");
                check (b.instrument() != nullptr && b.instrument()->state == synth.state && b.instrument()->deviceId == synth.deviceId,
                       "dawproject: round trip: plugin and state");
                check (b.sends.size() == 1 && b.sends[0].target == 0 && near (b.sends[0].db, -6.0, 1e-6), "dawproject: round trip: send");
                check (b.noteClips.size() == 1 && near (b.noteClips[0].start, 2.0, 1e-9) && b.noteClips[0].notes.size() == 1
                       && near (b.noteClips[0].notes[0].time, 0.5, 1e-9) && b.noteClips[0].notes[0].channel == 2, "dawproject: round trip: notes");
                check (b.audioClips.size() == 1 && near (b.audioClips[0].start, 1.0, 1e-9) && near (b.audioClips[0].offset, 0.25, 1e-9)
                       && near (b.audioClips[0].length, 0.5, 1e-9) && ! b.audioClips[0].warped && fs::file_size (b.audioClips[0].file) == 500,
                       "dawproject: round trip: audio");
                check (b.slots.size() == 1 && b.slots[0].scene == 1 && b.slots[0].noteClip.notes.size() == 1, "dawproject: round trip: slot");
            }
        }

        // Notes to a MIDI file and back.
        {
            const std::vector<dawproject::Note> notes { { 0.0, 1.0, 60, 0, 1.0, 0.5 }, { 1.5, 0.5, 64, 3, 0.5, 0.5 } };
            const auto song = dawproject::songOf (notes, 8.0, 120.0);
            check (song->lastTick == 8 * 480 && song->noteCount() == 2, "dawproject: notes as MIDI");
            const auto back = dawproject::notesOf (*song, 0.25, 2.0, 0.0, 120.0);   // from beat 0.5 for 4 beats
            check (back.size() == 1 && back[0].key == 64 && back[0].channel == 3 && near (back[0].time, 1.0, 1e-6) && near (back[0].duration, 0.5, 1e-6),
                   "dawproject: the notes of a trimmed MIDI clip");
        }

        const std::vector<uint8_t> preset = [] { std::vector<uint8_t> b { 'V', 'S', 'T', '3', 1, 0, 0, 0 }; for (int i = 0; i < 32; ++i) b.push_back ('A'); b.resize (48, 0); return b; }();
        check (dawproject::vstPresetClassId (preset) == std::string (32, 'A'), "dawproject: a .vstpreset's class id");
        fs::remove_all (folder);
    }

    // Ableton Live Sets: a small set as Live 10 and 11 write them, gzipped, with a Drum Rack.
    void testAbletonSet()
    {
        namespace fs = std::filesystem;
        const auto folder = fs::temp_directory_path() / "phi-als-test";
        fs::remove_all (folder);
        fs::create_directories (folder / "Samples" / "Recorded");
        for (const auto* name : { "Samples/Recorded/kick.wav", "snare.wav", "loop.wav" })
            std::ofstream (folder / name, std::ios::binary) << std::string (100, 's');
        const auto snare = (folder / "snare.wav").string();

        const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Ableton MajorVersion="5" MinorVersion="11.0_433" Creator="Ableton Live 11.3">
  <LiveSet>
    <Tracks>
      <MidiTrack Id="1">
        <Name><EffectiveName Value="Drums" /><UserName Value="Drums" /></Name>
        <DeviceChain>
          <Mixer>
            <Sends>
              <TrackSendHolder Id="0"><Send><Manual Value="0.5" /></Send></TrackSendHolder>
            </Sends>
            <Speaker><Manual Value="true" /></Speaker>
            <SoloSink Value="true" />
            <Pan><Manual Value="-0.5" /></Pan>
            <Volume><Manual Value="0.5" /></Volume>
          </Mixer>
          <MainSequencer>
            <ClipSlotList>
              <ClipSlot Id="0"><ClipSlot><Value /></ClipSlot></ClipSlot>
              <ClipSlot Id="1"><ClipSlot><Value>
                <MidiClip Id="2" Time="0">
                  <CurrentStart Value="0" /><CurrentEnd Value="4" />
                  <Loop><LoopStart Value="0" /><LoopEnd Value="4" /><StartRelative Value="0" /><LoopOn Value="true" /></Loop>
                  <Name Value="Beat" />
                  <Notes><KeyTracks>
                    <KeyTrack Id="0"><Notes><MidiNoteEvent Time="1" Duration="0.5" Velocity="127" OffVelocity="64" IsEnabled="true" /></Notes><MidiKey Value="37" /></KeyTrack>
                  </KeyTracks></Notes>
                </MidiClip>
              </Value></ClipSlot></ClipSlot>
            </ClipSlotList>
            <ClipTimeable><ArrangerAutomation><Events>
              <MidiClip Id="1" Time="8">
                <CurrentStart Value="8" /><CurrentEnd Value="14" />
                <Loop><LoopStart Value="0" /><LoopEnd Value="4" /><StartRelative Value="1" /><LoopOn Value="true" /></Loop>
                <Name Value="Groove" />
                <Disabled Value="false" />
                <Notes><KeyTracks>
                  <KeyTrack Id="0"><Notes>
                    <MidiNoteEvent Time="0" Duration="0.5" Velocity="127" OffVelocity="64" IsEnabled="true" />
                    <MidiNoteEvent Time="2" Duration="0.5" Velocity="64" OffVelocity="64" IsEnabled="false" />
                  </Notes><MidiKey Value="36" /></KeyTrack>
                  <KeyTrack Id="1"><Notes><MidiNoteEvent Time="1" Duration="0.5" Velocity="63.5" OffVelocity="64" IsEnabled="true" /></Notes><MidiKey Value="37" /></KeyTrack>
                </KeyTracks></Notes>
              </MidiClip>
            </Events></ArrangerAutomation></ClipTimeable>
          </MainSequencer>
          <DeviceChain><Devices>
            <MidiVelocity Id="0" />
            <DrumGroupDevice Id="1">
              <Branches>
                <DrumBranch Id="0">
                  <Name><EffectiveName Value="Kick" /></Name>
                  <DeviceChain><MidiToAudioDeviceChain Id="0"><Devices>
                    <OriginalSimpler Id="0">
                      <Player><MultiSampleMap><SampleParts>
                        <MultiSamplePart Id="0">
                          <Volume Value="1" /><Panorama Value="0.25" />
                          <SampleStart Value="4800" /><SampleEnd Value="47999" />
                          <SampleRef>
                            <FileRef>
                              <RelativePathType Value="3" />
                              <RelativePath><RelativePathElement Id="0" Dir="Samples" /><RelativePathElement Id="1" Dir="Recorded" /></RelativePath>
                              <Name Value="kick.wav" />
                            </FileRef>
                            <DefaultDuration Value="48000" /><DefaultSampleRate Value="48000" />
                          </SampleRef>
                        </MultiSamplePart>
                      </SampleParts></MultiSampleMap></Player>
                      <Globals><PlaybackMode Value="1" /></Globals>
                      <TransposeKey><KeyMidi><NoteOrController Value="60" /></KeyMidi><Manual Value="-2" /></TransposeKey>
                      <TransposeFine><Manual Value="50" /></TransposeFine>
                      <Volume><Manual Value="-12" /></Volume>
                    </OriginalSimpler>
                  </Devices>
                  <Mixer><Volume><Manual Value="1" /></Volume><Pan><Manual Value="0" /></Pan><Speaker><Manual Value="true" /></Speaker></Mixer>
                  </MidiToAudioDeviceChain></DeviceChain>
                  <BranchInfo><ReceivingNote Value="92" /><SendingNote Value="60" /><ChokeGroup Value="0" /></BranchInfo>
                </DrumBranch>
                <DrumBranch Id="1">
                  <Name><EffectiveName Value="Snare" /></Name>
                  <DeviceChain><MidiToAudioDeviceChain Id="0"><Devices>
                    <OriginalSimpler Id="0">
                      <Player><MultiSampleMap><SampleParts>
                        <MultiSamplePart Id="0">
                          <Volume Value="2" /><Panorama Value="0" />
                          <SampleStart Value="0" /><SampleEnd Value="0" />
                          <SampleRef>
                            <FileRef><RelativePath Value="nowhere/snare.wav" /><Path Value="/no/such/folder/snare.wav" /></FileRef>
                            <DefaultDuration Value="44100" /><DefaultSampleRate Value="44100" />
                          </SampleRef>
                        </MultiSamplePart>
                      </SampleParts></MultiSampleMap></Player>
                      <Globals><PlaybackMode Value="0" /></Globals>
                      <Volume><Manual Value="0" /></Volume>
                    </OriginalSimpler>
                  </Devices>
                  <Mixer><Volume><Manual Value="0.5" /></Volume><Pan><Manual Value="-0.5" /></Pan><Speaker><Manual Value="false" /></Speaker></Mixer>
                  </MidiToAudioDeviceChain></DeviceChain>
                  <BranchInfo><ReceivingNote Value="91" /><ChokeGroup Value="1" /></BranchInfo>
                </DrumBranch>
              </Branches>
            </DrumGroupDevice>
            <Reverb Id="2"><On><Manual Value="true" /></On></Reverb>
          </Devices></DeviceChain>
        </DeviceChain>
      </MidiTrack>
      <AudioTrack Id="2">
        <Name><EffectiveName Value="Loop" /></Name>
        <DeviceChain>
          <Mixer><Volume><Manual Value="1" /></Volume><Speaker><Manual Value="false" /></Speaker></Mixer>
          <MainSequencer>
            <Sample><ArrangerAutomation><Events>
              <AudioClip Id="0" Time="4">
                <CurrentStart Value="4" /><CurrentEnd Value="12" />
                <Loop><LoopStart Value="0" /><LoopEnd Value="8" /><StartRelative Value="0" /><LoopOn Value="false" /></Loop>
                <Name Value="loop" />
                <SampleRef><FileRef><Path Value=")" + (folder / "loop.wav").string() + R"(" /></FileRef><DefaultDuration Value="96000" /><DefaultSampleRate Value="48000" /></SampleRef>
                <IsWarped Value="true" />
                <WarpMarkers><WarpMarker Id="0" SecTime="0" BeatTime="0" /><WarpMarker Id="1" SecTime="4" BeatTime="8" /></WarpMarkers>
              </AudioClip>
              <AudioClip Id="1" Time="16">
                <CurrentStart Value="16" /><CurrentEnd Value="18" />
                <Loop><LoopStart Value="0.5" /><LoopEnd Value="2" /><StartRelative Value="0" /><LoopOn Value="false" /></Loop>
                <SampleRef><FileRef><Path Value=")" + (folder / "loop.wav").string() + R"(" /></FileRef></SampleRef>
                <IsWarped Value="false" />
              </AudioClip>
            </Events></ArrangerAutomation></Sample>
          </MainSequencer>
        </DeviceChain>
      </AudioTrack>
      <GroupTrack Id="3"><Name><EffectiveName Value="Bus" /></Name></GroupTrack>
      <ReturnTrack Id="4">
        <Name><EffectiveName Value="A-Reverb" /></Name>
        <DeviceChain><Mixer><Volume><Manual Value="1" /></Volume></Mixer></DeviceChain>
      </ReturnTrack>
    </Tracks>
    <MasterTrack>
      <DeviceChain><Mixer><Tempo><Manual Value="120" /></Tempo><Volume><Manual Value="1" /></Volume></Mixer></DeviceChain>
    </MasterTrack>
    <SceneNames><Scene Id="0" Value="Intro" /><Scene Id="1" Value="Verse" /><Scene Id="2" Value="" /></SceneNames>
  </LiveSet>
</Ableton>)";
        const auto set = (folder / "song.als").string();
        {
            auto* gz = gzopen (set.c_str(), "wb");
            gzwrite (gz, xml.data(), (unsigned) xml.size());
            gzclose (gz);
        }
        dawproject::Project p;
        std::string error;
        check (ableton::read (set, p, error), "als: read (" + error + ")");
        check (p.application == "Ableton Live 11.3" && near (p.bpm, 120.0, 1e-9), "als: creator and tempo");
        check (p.tracks.size() == 2 && p.returns.size() == 1, "als: two tracks (the group left out) and a return");
        check (p.scenes.size() == 2 && p.scenes[1] == "Verse", "als: the scenes up to the last used");
        if (p.tracks.size() == 2)
        {
            const auto& d = p.tracks[0];
            check (d.name == "Drums" && d.notes && d.solo && ! d.mute && near (d.volumeDb, -6.02, 0.01) && near (d.pan, -0.5, 1e-9), "als: the drum track's mixer");
            check (d.sends.size() == 1 && d.sends[0].target == 0 && near (d.sends[0].db, -6.02, 0.01), "als: the send to the return");
            check (d.kit.size() == 2, "als: two pads");
            if (d.kit.size() == 2)
            {
                const auto& k = d.kit[0];
                check (k.note == 36 && k.name == "Kick" && fs::path (k.file).filename() == "kick.wav" && fs::exists (k.file), "als: kick pad, its sample found beside the set");
                check (near (k.start, 0.1, 1e-9) && near (k.end, 1.0, 1e-9) && near (k.gainDb, -12.0, 1e-6) && near (k.pan, 0.25, 1e-9)
                       && near (k.pitch, -1.5, 1e-9) && ! k.gate, "als: kick's start, end, level, pan, transpose and one-shot");
                const auto& s = d.kit[1];
                check (s.note == 37 && s.file == snare && s.choke == 1 && s.mute && s.gate && s.end < 0.0, "als: snare found by name, choke group, muted, classic");
                check (near (s.gainDb, 20.0 * std::log10 (2.0) + 20.0 * std::log10 (0.5), 1e-6) && near (s.pan, -0.5, 1e-9), "als: snare's levels and its chain's pan");
            }
            check (d.noteClips.size() == 1, "als: one arrangement clip");
            if (d.noteClips.size() == 1)
            {
                const auto& c = d.noteClips[0];
                check (near (c.start, 4.0, 1e-9) && near (c.lengthBeats, 6.0, 1e-9) && c.name == "Groove", "als: the clip placed");
                // From beat 1 of a 4-beat loop for 6 beats: the 37 at 0, the 36 at 3, the 37 at 4; the disabled note left out.
                std::vector<std::pair<double, int>> got;
                for (const auto& n : c.notes)
                    got.emplace_back (n.time, n.key);
                check (got == (std::vector<std::pair<double, int>> { { 0.0, 37 }, { 3.0, 36 }, { 4.0, 37 } }), "als: the loop unrolled");
                check (! c.notes.empty() && near (c.notes[0].velocity, 0.5, 1e-9), "als: velocity");
            }
            check (d.slots.size() == 1 && d.slots[0].scene == 1 && d.slots[0].noteClip.notes.size() == 1, "als: the Session clip in its scene");
            const auto& l = p.tracks[1];
            check (l.name == "Loop" && l.mute && l.audio, "als: the audio track");
            check (l.audioClips.size() == 2, "als: two audio clips");
            if (l.audioClips.size() == 2)
            {
                // 8 beats over 4 s at 120 BPM: not stretched.
                check (near (l.audioClips[0].start, 2.0, 1e-9) && near (l.audioClips[0].length, 4.0, 1e-9) && ! l.audioClips[0].warped
                       && near (l.audioClips[0].fileLength, 2.0, 1e-9), "als: the warped clip at its tempo");
                check (near (l.audioClips[1].start, 8.0, 1e-9) && near (l.audioClips[1].offset, 0.5, 1e-9) && near (l.audioClips[1].length, 1.0, 1e-9),
                       "als: the unwarped clip, its start in seconds");
            }
        }
        auto said = [&] (const std::string& part)
        {
            return std::any_of (p.notes.begin(), p.notes.end(), [&] (const std::string& n) { return n.find (part) != std::string::npos; });
        };
        check (said ("MidiVelocity") && said ("Reverb") && said ("group"), "als: what did not carry over is said");
        check (! ableton::parse ("<Project/>", folder.string(), p, error), "als: not a set");
        fs::remove_all (folder);
    }

int main()
{
    testStorage();
    testDocument();
    testDsp();
    testLayouts();
    testStereoize();
    testSpectrum();
    testWindows();
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
    testEbuCore();
    testRiffChunks();
    testNyquist();
    testAlbum();
    testCurvedFade();
    testMidSide();
    testSampler();
    testSlicer();
    testStepSequencer();
    testMidiFile();
    testMidiEdits();
    testMixer();
    testLauncher();
    testPatchBay();
    testMixerStrip();
    testSpatialStudio();
    testPrismSplit();
    testInstrumentPrism();
    testMasterAssist();
    testMixAssist();
    testSonify();
    testPhi();
    testPhiDynamics();
    testPhiChorus();
    testPhiModulation();
    testModulatorRack();
    testPhiVoice();
    testPhiPhaser();
    testPhiRepeat();
    testPhiTuning();
    testRagaSheets();
    testSitar();
    testTunings();
    testModeAtlas();
    testHarmonies();
    testModalHarmony();
    testScala();
    testXenTuner();
    testNotationViews();
    testNoteEditor();
    testCounterpoint();
    testFugue();
    testMinimalize();
    testRecording();
    testControlSurfaces();
    testDrumsToMidi();
    testRhythms();
    testRhythmGenerator();
    testArchitectonics();
    testCompose();
    testBolProcessor();
    testPhiTabla();
    testStochastic();
    testCubify();
    testSerial();
    testPluginMeasure();
    testDawProject();
    testAbletonSet();

    if (failures == 0)
        std::printf ("All core tests passed\n");

    return failures == 0 ? 0 : 1;
}
