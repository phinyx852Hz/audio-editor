// SPDX-License-Identifier: AGPL-3.0-only
// Tests for the plain C++ core: audio storage, undo history and DSP.
#include "../Source/core/AudioData.h"
#include "../Source/core/AudioDocument.h"
#include "../Source/core/Dsp.h"
#include "../Source/core/Fft.h"
#include "../Source/core/Repair.h"

#include <cmath>
#include <cstdio>
#include <functional>
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

        DocumentState next { d->replaced (50, 250, nullptr), doc.current().markers };
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
        doc.apply ("Other", { d, {} });
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

    if (failures == 0)
        std::printf ("All core tests passed\n");

    return failures == 0 ? 0 : 1;
}
