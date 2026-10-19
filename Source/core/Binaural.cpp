// SPDX-License-Identifier: AGPL-3.0-only
#include "Binaural.h"
#include "Fft.h"

#include <mysofa.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace binaural
{
    //==========================================================================
    std::shared_ptr<Hrtf> Hrtf::open (const std::string& path, double sampleRate, std::string& error)
    {
        int taps = 0, err = 0;
        auto* easy = mysofa_open (path.c_str(), (float) sampleRate, &taps, &err);
        if (easy == nullptr || err != MYSOFA_OK || taps <= 0)
        {
            if (easy != nullptr)
                mysofa_close (easy);
            error = "The HRTF file " + path + " could not be read (libmysofa error " + std::to_string (err) + ")";
            return {};
        }
        std::shared_ptr<Hrtf> h (new Hrtf());
        h->easy = easy;
        h->taps = taps;
        h->rate = sampleRate;
        return h;
    }

    Hrtf::~Hrtf()
    {
        if (easy != nullptr)
            mysofa_close (easy);
    }

    void Hrtf::filters (double azimuth, double elevation, std::vector<float>& left, std::vector<float>& right) const
    {
        // SOFA measures azimuth anticlockwise (left is positive); ours goes clockwise.
        float c[3] { (float) -azimuth, (float) std::clamp (elevation, -90.0, 90.0), 1.2f };
        mysofa_s2c (c);
        std::vector<float> l ((size_t) taps), r ((size_t) taps);
        float delayLeft = 0.0f, delayRight = 0.0f;
        mysofa_getfilter_float (easy, c[0], c[1], c[2], l.data(), r.data(), &delayLeft, &delayRight);

        // Sets that store their delays apart from the filters get them back as leading zeros.
        const auto shift = [&] (const std::vector<float>& ir, float seconds)
        {
            const auto n = std::max (0, (int) std::lround (seconds * rate));
            std::vector<float> out ((size_t) n, 0.0f);
            out.insert (out.end(), ir.begin(), ir.end());
            return out;
        };
        left = shift (l, delayLeft);
        right = shift (r, delayRight);
    }

    //==========================================================================
    namespace
    {
        int orderFor (int n)
        {
            int order = 0;
            while ((1 << order) < n)
                ++order;
            return order;
        }
    }

    Convolver::Convolver (const std::vector<float>& impulse, int blockSize)
        : block (blockSize), order (orderFor (2 * blockSize))
    {
        const Fft fft (order);
        const auto size = (size_t) fft.size();
        const auto count = std::max<size_t> (1, (impulse.size() + (size_t) block - 1) / (size_t) block);
        for (size_t p = 0; p < count; ++p)
        {
            std::vector<std::complex<float>> part (size);
            for (size_t i = 0; i < (size_t) block && p * (size_t) block + i < impulse.size(); ++i)
                part[i] = impulse[p * (size_t) block + i];
            fft.forward (part);
            parts.push_back (std::move (part));
        }
        history.assign (count, std::vector<std::complex<float>> (size));
        buffer.resize (size);
        overlap.assign ((size_t) block, 0.0f);
    }

    void Convolver::process (const float* in, float* out)
    {
        thread_local std::unique_ptr<Fft> fft;
        if (fft == nullptr || fft->size() != (int) buffer.size())
            fft = std::make_unique<Fft> (order);

        // The newest input block's spectrum goes into the delay line...
        newest = (newest + history.size() - 1) % history.size();
        auto& spectrum = history[newest];
        std::fill (spectrum.begin(), spectrum.end(), std::complex<float>());
        for (int i = 0; i < block; ++i)
            spectrum[(size_t) i] = in[i];
        fft->forward (spectrum);

        // ...and each part of the impulse meets the block that is that many blocks old.
        std::fill (buffer.begin(), buffer.end(), std::complex<float>());
        for (size_t p = 0; p < parts.size(); ++p)
        {
            const auto& x = history[(newest + p) % history.size()];
            const auto& h = parts[p];
            for (size_t k = 0; k < buffer.size(); ++k)
                buffer[k] += x[k] * h[k];
        }
        fft->inverse (buffer);

        for (int i = 0; i < block; ++i)
        {
            out[i] = buffer[(size_t) i].real() + overlap[(size_t) i];
            overlap[(size_t) i] = buffer[(size_t) (i + block)].real();
        }
    }

    //==========================================================================
    void roomImpulses (double sampleRate, double seconds, std::vector<float>& left, std::vector<float>& right)
    {
        seconds = std::clamp (seconds, 0.1, 4.0);
        const auto length = (size_t) (sampleRate * seconds);
        const auto predelay = (size_t) (sampleRate * 0.008);
        const auto fadeIn = std::max<size_t> (1, (size_t) (sampleRate * 0.006));
        std::mt19937 random (852);
        std::normal_distribution<float> noise;

        for (auto* ir : { &left, &right })
        {
            ir->assign (predelay + length, 0.0f);
            // Darker as it decays: a one-pole low-pass that closes from about 9 kHz to 2 kHz.
            float state = 0.0f;
            double energy = 0.0;
            for (size_t i = 0; i < length; ++i)
            {
                const auto t = (double) i / sampleRate;
                const auto cutoff = 9000.0 * std::pow (2000.0 / 9000.0, t / seconds);
                const auto a = (float) std::exp (-2.0 * M_PI * cutoff / sampleRate);
                state = (1.0f - a) * noise (random) + a * state;
                const auto envelope = std::exp (-6.9078 * t / seconds) * std::min (1.0, (double) (i + 1) / (double) fadeIn);
                const auto v = (float) (state * envelope);
                (*ir)[predelay + i] = v;
                energy += (double) v * v;
            }
            const auto scale = energy > 0.0 ? (float) (1.0 / std::sqrt (energy)) : 0.0f;
            for (auto& v : *ir)
                v *= scale;
        }
    }

    //==========================================================================
    AudioDataPtr render (const AudioData& d, Frame start, Frame end, const Settings& settings,
                         std::string& error, std::atomic<float>* progress)
    {
        const auto rate = d.sampleRate();
        const auto inChannels = d.numChannels();
        if (inChannels < 1 || inChannels > 2)
        {
            error = "Binaural rendering works on mono and stereo audio";
            return {};
        }
        const auto hrtf = Hrtf::open (settings.hrtfPath, rate, error);
        if (hrtf == nullptr)
            return {};

        start = std::clamp<Frame> (start, 0, d.length());
        end = std::clamp<Frame> (end, start, d.length());
        constexpr int block = 4096;

        // One source per input channel, the two channels spread `width` apart.
        const auto distance = std::clamp (settings.distance, 0.5, 20.0);
        const auto gain = (float) (std::pow (10.0, settings.gainDb / 20.0) / distance);
        struct Source { std::unique_ptr<Convolver> left, right; float lowpass = 0.0f; };
        std::vector<Source> sources ((size_t) inChannels);
        int longest = 0;
        for (int ch = 0; ch < inChannels; ++ch)
        {
            const auto azimuth = inChannels == 1 ? settings.azimuth
                                                 : settings.azimuth + (ch == 0 ? -0.5 : 0.5) * std::clamp (settings.width, 0.0, 180.0);
            std::vector<float> l, r;
            hrtf->filters (azimuth, settings.elevation, l, r);
            longest = std::max ({ longest, (int) l.size(), (int) r.size() });
            sources[(size_t) ch].left = std::make_unique<Convolver> (l, block);
            sources[(size_t) ch].right = std::make_unique<Convolver> (r, block);
        }
        // Air absorbs high frequencies with distance: a gentle one-pole low-pass.
        const auto airCutoff = 20000.0 / (1.0 + distance * 0.12);
        const auto air = (float) std::exp (-2.0 * M_PI * std::min (airCutoff, rate * 0.45) / rate);

        const auto room = (float) std::clamp (settings.room, 0.0, 1.0);
        std::unique_ptr<Convolver> roomLeft, roomRight;
        if (room > 0.0f)
        {
            std::vector<float> l, r;
            roomImpulses (rate, settings.roomSeconds, l, r);
            longest = std::max (longest, (int) l.size());
            roomLeft = std::make_unique<Convolver> (l, block);
            roomRight = std::make_unique<Convolver> (r, block);
        }
        const auto roomGain = (float) (std::pow (10.0, settings.gainDb / 20.0) * 0.35 * room);

        const auto edge = std::max<Frame> (1, (Frame) (rate * 0.01));
        const auto ramp = [&] (Frame at)
        {
            if (at < start || at >= end)
                return 0.0f;
            return (float) std::min ({ 1.0, (double) (at - start + 1) / (double) edge, (double) (end - at) / (double) edge });
        };
        const auto renderEnd = std::min<Frame> (d.length(), end + longest + block);

        AudioData::Builder out (2, rate);
        if (start > 0)
        {
            if (inChannels == 2)
                out.appendShared (d, 0, start);
            else
                for (Frame pos = 0; pos < start;)
                {
                    const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, start - pos);
                    std::vector<float> mono ((size_t) n);
                    float* in[] { mono.data() };
                    d.read (pos, n, in);
                    const float* both[] { mono.data(), mono.data() };
                    out.append (both, n);
                    pos += n;
                }
        }

        std::vector<std::vector<float>> in ((size_t) inChannels, std::vector<float> (block));
        std::vector<float*> inPtrs;
        for (auto& v : in)
            inPtrs.push_back (v.data());
        std::vector<float> dry (block), wet (block), mid (block), left (block), right (block), scratch (block);
        const float* outPtrs[2] { left.data(), right.data() };

        for (Frame pos = start; pos < renderEnd; pos += block)
        {
            const auto n = (int) std::min<Frame> (block, renderEnd - pos);
            for (auto& v : in)
                std::fill (v.begin(), v.end(), 0.0f);
            d.read (pos, n, inPtrs.data());
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);
            std::fill (mid.begin(), mid.end(), 0.0f);

            for (int ch = 0; ch < inChannels; ++ch)
            {
                auto& s = sources[(size_t) ch];
                for (int i = 0; i < block; ++i)
                {
                    const auto x = in[(size_t) ch][(size_t) i] * ramp (pos + i);
                    mid[(size_t) i] += x / (float) inChannels;
                    s.lowpass = (1.0f - air) * x + air * s.lowpass;
                    dry[(size_t) i] = s.lowpass * gain;
                }
                s.left->process (dry.data(), scratch.data());
                for (int i = 0; i < block; ++i)
                    left[(size_t) i] += scratch[(size_t) i];
                s.right->process (dry.data(), scratch.data());
                for (int i = 0; i < block; ++i)
                    right[(size_t) i] += scratch[(size_t) i];
            }
            if (roomLeft != nullptr)
            {
                for (int i = 0; i < block; ++i)
                    wet[(size_t) i] = mid[(size_t) i] * roomGain;
                roomLeft->process (wet.data(), scratch.data());
                for (int i = 0; i < block; ++i)
                    left[(size_t) i] += scratch[(size_t) i];
                roomRight->process (wet.data(), scratch.data());
                for (int i = 0; i < block; ++i)
                    right[(size_t) i] += scratch[(size_t) i];
            }

            // A tail cut short by the end of the audio fades out over its last 10 ms; outside the
            // rendered part (and while it fades) the original stays.
            for (int i = 0; i < n; ++i)
            {
                const auto at = pos + i;
                if (at >= end)
                {
                    const auto fade = (float) std::clamp ((double) (d.length() - at) / (double) edge, 0.0, 1.0);
                    left[(size_t) i] *= fade;
                    right[(size_t) i] *= fade;
                }
                const auto keep = 1.0f - ramp (at);
                left[(size_t) i] += keep * in[0][(size_t) i];
                right[(size_t) i] += keep * in[inChannels > 1 ? 1 : 0][(size_t) i];
            }
            out.append (outPtrs, n);
            if (progress != nullptr)
                progress->store ((float) (pos - start) / (float) std::max<Frame> (1, renderEnd - start));
        }

        if (renderEnd < d.length())
        {
            if (inChannels == 2)
                out.appendShared (d, renderEnd, d.length());
            else
                for (Frame pos = renderEnd; pos < d.length();)
                {
                    const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, d.length() - pos);
                    std::vector<float> mono ((size_t) n);
                    float* inMono[] { mono.data() };
                    d.read (pos, n, inMono);
                    const float* both[] { mono.data(), mono.data() };
                    out.append (both, n);
                    pos += n;
                }
        }
        return out.finish();
    }
}
