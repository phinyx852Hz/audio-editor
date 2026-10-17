// SPDX-License-Identifier: AGPL-3.0-only
// Tests for the plain C++ core: audio storage, undo history and DSP.
#include "../Source/core/AudioData.h"
#include "../Source/core/AudioDocument.h"
#include "../Source/core/Dsp.h"

#include <cmath>
#include <cstdio>
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
}

int main()
{
    testStorage();
    testDocument();
    testDsp();

    if (failures == 0)
        std::printf ("All core tests passed\n");

    return failures == 0 ? 0 : 1;
}
