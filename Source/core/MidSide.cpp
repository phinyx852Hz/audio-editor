// SPDX-License-Identifier: AGPL-3.0-only
#include "MidSide.h"

#include <algorithm>
#include <cmath>

namespace midside
{
    namespace
    {
        constexpr double airHz = 8000.0;
        constexpr double generateFromHz = 150.0;
        constexpr double generateDelayMs = 9.0;

        double onePole (double seconds, double rate) { return 1.0 - std::exp (-1.0 / (seconds * rate)); }

        void butterworth (double& b0, double& b1, double& b2, double& a1, double& a2, double hz, double rate, bool high)
        {
            const auto w0 = 2.0 * M_PI * std::clamp (hz, 10.0, rate * 0.45) / rate;
            const auto alpha = std::sin (w0) / (2.0 * M_SQRT1_2);
            const auto c = std::cos (w0), a0 = 1.0 + alpha;
            b0 = (high ? (1.0 + c) : (1.0 - c)) / 2.0 / a0;
            b1 = (high ? -(1.0 + c) : (1.0 - c)) / a0;
            b2 = b0;
            a1 = -2.0 * c / a0;
            a2 = (1.0 - alpha) / a0;
        }
    }

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> list = []
        {
            std::vector<Preset> p;
            auto add = [&p] (const char* name, auto&& change)
            {
                Settings s;
                change (s);
                p.push_back ({ name, s });
            };
            add ("Unchanged", [] (Settings&) {});
            add ("Gentle Widen", [] (Settings& s) { s.width = 1.2; s.monoBelowHz = 120.0; });
            add ("Wide Mix", [] (Settings& s) { s.width = 1.5; s.monoBelowHz = 150.0; s.airDb = 2.0; });
            add ("Huge", [] (Settings& s) { s.huge = true; s.width = 2.5; s.monoBelowHz = 200.0; s.airDb = 3.0; s.lift = 0.3; });
            add ("Tight Bass Only", [] (Settings& s) { s.monoBelowHz = 150.0; });
            add ("Mono to Stereo", [] (Settings& s) { s.generate = 0.6; s.monoBelowHz = 150.0; });
            add ("Airy Sides", [] (Settings& s) { s.width = 1.15; s.airDb = 4.0; });
            add ("Breathing Width", [] (Settings& s) { s.width = 1.2; s.lift = 0.6; s.monoBelowHz = 120.0; });
            add ("Narrow", [] (Settings& s) { s.width = 0.6; });
            add ("Mono", [] (Settings& s) { s.width = 0.0; });
            return p;
        }();
        return list;
    }

    void Processor::prepare (double sampleRate)
    {
        rate = sampleRate;
        generator.prepare (rate);
        fastAttack = onePole (0.005, rate);
        fastRelease = onePole (0.12, rate);
        slowCoeff = onePole (1.5, rate);
        avgCoeff = onePole (0.3, rate);
        powerCoeff = onePole (0.4, rate);
        matchCoeff = onePole (0.3, rate);
        widthCoeff = onePole (0.02, rate);
        guardCoeff = onePole (0.05, rate);
        fresh = true;
        reset();
        design();
    }

    void Processor::design()
    {
        designed = settings;
        for (auto& f : sideHigh)
            butterworth (f.b0, f.b1, f.b2, f.a1, f.a2, settings.monoBelowHz, rate, true);
        const auto bassHz = settings.monoBelowHz > 0.0 ? settings.monoBelowHz : generateFromHz;
        for (auto& f : bassLow)
            butterworth (f.b0, f.b1, f.b2, f.a1, f.a2, bassHz, rate, false);

        // RBJ high shelf, slope 1.
        const auto a = std::pow (10.0, settings.airDb / 40.0);
        const auto w0 = 2.0 * M_PI * std::min (airHz, rate * 0.45) / rate;
        const auto c = std::cos (w0), alpha = std::sin (w0) / 2.0 * M_SQRT2;
        const auto sa = 2.0 * std::sqrt (a) * alpha;
        const auto a0 = (a + 1) - (a - 1) * c + sa;
        air.b0 = a * ((a + 1) + (a - 1) * c + sa) / a0;
        air.b1 = -2 * a * ((a - 1) + (a + 1) * c) / a0;
        air.b2 = a * ((a + 1) + (a - 1) * c - sa) / a0;
        air.a1 = 2 * ((a - 1) - (a + 1) * c) / a0;
        air.a2 = ((a + 1) - (a - 1) * c - sa) / a0;

        generator.set ({ 1.0, generateDelayMs, generateFromHz, settings.golden });
    }

    void Processor::set (const Settings& s)
    {
        settings = s;
        settings.width = std::clamp (s.width, 0.0, s.maxWidth());
        settings.monoBelowHz = s.monoBelowHz < 1.0 ? 0.0 : std::clamp (s.monoBelowHz, 20.0, 500.0);
        settings.generate = std::clamp (s.generate, 0.0, 1.0);
        settings.airDb = std::clamp (s.airDb, -6.0, 6.0);
        settings.rotation = std::clamp (s.rotation, -45.0, 45.0);
        settings.lift = std::clamp (s.lift, 0.0, 1.0);
        if (std::abs (settings.monoBelowHz - designed.monoBelowHz) > 1.0e-9 || std::abs (settings.airDb - designed.airDb) > 1.0e-9
            || settings.golden != designed.golden)
            design();
        if (fresh)
            width = settings.width;
        fresh = false;
    }

    void Processor::reset()
    {
        for (auto& f : sideHigh)
            f.clear();
        for (auto& f : bassLow)
            f.clear();
        air.clear();
        generator.reset();
        fast = slow = 0;
        ll = rr = 1.0e-9;
        lr = 1.0e-9;
        inPower = outPower = 0;
        match = guard = 1;
    }

    double Processor::correlation() const
    {
        const auto d = std::sqrt (ll * rr);
        return d > 1.0e-12 ? std::clamp (lr / d, -1.0, 1.0) : 1.0;
    }

    void Processor::process (float& left, float& right)
    {
        const double l = left, r = right;
        const auto mid = 0.5 * (l + r);
        auto side = 0.5 * (l - r);

        if (settings.monoBelowHz > 0.0)
            side = sideHigh[1].run (sideHigh[0].run (side));
        if (settings.generate > 0.0)
            side += settings.generate * generator.process ((float) mid);
        else
            generator.process ((float) mid);   // keeps its delay line current, so turning it up does not click
        if (std::abs (settings.airDb) > 1.0e-6)
            side = air.run (side);

        // Lift: the louder the mid is against its last second or so, the wider.
        const auto level = std::abs (mid);
        fast += (level - fast) * (level > fast ? fastAttack : fastRelease);
        slow += (level - slow) * slowCoeff;
        const auto rise = std::clamp (20.0 * std::log10 ((fast + 1.0e-9) / (slow + 1.0e-9)) / 6.0, 0.0, 1.0);

        // Huge: wider than 200 % backs off towards 100 % while the image goes out of phase.
        if (settings.huge && settings.width > 1.0)
        {
            const auto target = correlation() < -0.3 ? 1.0 / settings.width : 1.0;
            guard += (target - guard) * guardCoeff * (target < guard ? 1.0 : 0.1);
        }
        else
            guard = 1.0;

        width += (settings.width - width) * widthCoeff;
        side *= width * guard * (1.0 + settings.lift * rise);

        auto outL = mid + side, outR = mid - side;
        if (std::abs (settings.rotation) > 1.0e-6)
        {
            const auto t = settings.rotation * M_PI / 180.0;
            const auto c = std::cos (t), s = std::sin (t);
            const auto a = outL * c - outR * s, b = outL * s + outR * c;
            outL = a;
            outR = b;
        }

        inPower += (l * l + r * r - inPower) * powerCoeff;
        outPower += (outL * outL + outR * outR - outPower) * powerCoeff;
        if (settings.matchLevel)
        {
            const auto target = outPower > 1.0e-12 && inPower > 1.0e-12 ? std::clamp (std::sqrt (inPower / outPower), 0.5, 2.0) : match;
            match += (target - match) * matchCoeff;
            outL *= match;
            outR *= match;
        }

        ll += (outL * outL - ll) * avgCoeff;
        rr += (outR * outR - rr) * avgCoeff;
        lr += (outL * outR - lr) * avgCoeff;

        switch (settings.listen)
        {
            case Listen::stereo: break;
            case Listen::mono: outL = outR = 0.5 * (outL + outR); break;
            case Listen::side: { const auto s = 0.5 * (outL - outR); outL = s; outR = s; break; }
            case Listen::bass:
                outL = bassLow[1].run (bassLow[0].run (outL));
                outR = bassLow[3].run (bassLow[2].run (outR));
                break;
        }
        left = (float) outL;
        right = (float) outR;
    }

    AudioDataPtr render (const AudioData& d, Frame start, Frame end, const Settings& settings)
    {
        const auto rate = d.sampleRate();
        const auto inChannels = std::min (2, d.numChannels());
        start = std::clamp<Frame> (start, 0, d.length());
        end = std::clamp<Frame> (end, start, d.length());

        Processor core;
        core.prepare (rate);
        core.set (settings);
        const auto edge = std::max<Frame> (1, (Frame) (rate * 0.01));   // 10 ms crossfades in and out of the effect

        AudioData::Builder out (2, rate);
        std::vector<std::vector<float>> in ((size_t) d.numChannels(), std::vector<float> (AudioChunk::maxFrames));
        std::vector<float*> inPtrs;
        for (auto& v : in)
            inPtrs.push_back (v.data());
        std::vector<float> left (AudioChunk::maxFrames), right (AudioChunk::maxFrames);
        const float* outPtrs[2] { left.data(), right.data() };

        for (Frame pos = 0; pos < d.length();)
        {
            const auto limit = pos < start ? start : (pos < end ? end : d.length());
            const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, limit - pos);
            d.read (pos, n, inPtrs.data());
            const bool inside = pos >= start && pos < end;
            for (int i = 0; i < n; ++i)
            {
                const auto l = in[0][(size_t) i];
                const auto r = in[inChannels > 1 ? 1 : 0][(size_t) i];
                if (! inside)
                {
                    left[(size_t) i] = l;
                    right[(size_t) i] = r;
                    continue;
                }
                auto pl = l, pr = r;
                core.process (pl, pr);
                const auto at = pos + i;
                const auto mix = (float) std::min ({ 1.0, (double) (at - start + 1) / (double) edge, (double) (end - at) / (double) edge });
                left[(size_t) i] = l + (pl - l) * mix;
                right[(size_t) i] = r + (pr - r) * mix;
            }
            out.append (outPtrs, n);
            pos += n;
        }
        return out.finish();
    }

    Analysis analyse (const AudioData& d, Frame start, Frame end)
    {
        start = std::clamp<Frame> (start, 0, d.length());
        end = std::clamp<Frame> (end, start, d.length());
        const auto stereo = d.numChannels() >= 2;
        std::vector<std::vector<float>> in ((size_t) d.numChannels(), std::vector<float> (AudioChunk::maxFrames));
        std::vector<float*> ptrs;
        for (auto& v : in)
            ptrs.push_back (v.data());
        double ll = 0, rr = 0, lr = 0, mm = 0, ss = 0;
        for (Frame pos = start; pos < end;)
        {
            const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, end - pos);
            d.read (pos, n, ptrs.data());
            for (int i = 0; i < n; ++i)
            {
                const double l = in[0][(size_t) i], r = in[stereo ? 1 : 0][(size_t) i];
                ll += l * l;
                rr += r * r;
                lr += l * r;
                const auto m = 0.5 * (l + r), s = 0.5 * (l - r);
                mm += m * m;
                ss += s * s;
            }
            pos += n;
        }
        Analysis a;
        const auto denominator = std::sqrt (ll * rr);
        a.correlation = denominator > 1.0e-12 ? std::clamp (lr / denominator, -1.0, 1.0) : 1.0;
        // Mono sum ((L + R) / 2 on both sides) against the stereo level: 0 dB when both sides match.
        const auto stereoPower = 0.5 * (ll + rr);
        a.monoSumDb = stereoPower > 1.0e-12 ? 10.0 * std::log10 (std::max (mm, 1.0e-12 * stereoPower) / stereoPower) : 0.0;
        a.sideDb = mm > 1.0e-12 ? 10.0 * std::log10 (std::max (ss, 1.0e-12 * mm) / mm) : (ss > 1.0e-12 ? 120.0 : -120.0);
        return a;
    }
}
