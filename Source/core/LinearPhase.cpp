// SPDX-License-Identifier: AGPL-3.0-only
#include "LinearPhase.h"

#include <algorithm>
#include <cmath>

namespace phi
{
    namespace
    {
        constexpr double twoPi = 6.28318530717958647692;

        int orderOf (int n)
        {
            int order = 0;
            while ((1 << order) < n)
                ++order;
            return order;
        }
    }

    int LinearPhaseFir::tapsFor (double sampleRate)
    {
        return sampleRate > 50000.0 ? 32768 : 16384;
    }

    std::vector<float> LinearPhaseFir::design (const std::function<double (double)>& magnitude, int taps, double sampleRate)
    {
        const auto order = orderOf (taps);
        taps = 1 << order;
        std::vector<std::complex<float>> spectrum ((size_t) taps);
        for (int k = 0; k <= taps / 2; ++k)
        {
            const auto m = (float) magnitude (sampleRate * k / taps);
            spectrum[(size_t) k] = m;
            if (k > 0 && k < taps / 2)
                spectrum[(size_t) (taps - k)] = m;
        }
        Fft (order).inverse (spectrum);   // real and even: a zero-phase impulse around sample 0

        // Centred on taps / 2 and windowed (a periodic Blackman, 0 at the ends and
        // symmetric about the centre, so the phase stays exactly linear).
        std::vector<float> impulse ((size_t) taps);
        for (int n = 0; n < taps; ++n)
        {
            const auto w = 0.42 - 0.5 * std::cos (twoPi * n / taps) + 0.08 * std::cos (2.0 * twoPi * n / taps);
            impulse[(size_t) n] = (float) (spectrum[(size_t) ((n + taps / 2) % taps)].real() * w);
        }
        return impulse;
    }

    std::unique_ptr<LinearPhaseFir::Kernel> LinearPhaseFir::prepareKernel (const std::vector<float>& impulse)
    {
        auto k = std::make_unique<Kernel>();
        k->partitions = std::max (1, (int) ((impulse.size() + blockSize - 1) / blockSize));
        k->spectra.assign ((size_t) k->partitions * 2 * blockSize, {});
        const Fft transform (blockOrder + 1);
        std::vector<std::complex<float>> part ((size_t) 2 * blockSize);
        for (int p = 0; p < k->partitions; ++p)
        {
            std::fill (part.begin(), part.end(), std::complex<float> {});
            for (int i = 0; i < blockSize; ++i)
                if (const auto at = (size_t) (p * blockSize + i); at < impulse.size())
                    part[(size_t) i] = impulse[at];
            transform.forward (part);
            std::copy (part.begin(), part.end(), k->spectra.begin() + (std::ptrdiff_t) p * 2 * blockSize);
        }
        return k;
    }

    void LinearPhaseFir::prepare (int taps, int numChannels)
    {
        length = 1 << orderOf (taps);
        partitions = (length + blockSize - 1) / blockSize;
        channelCount = std::clamp (numChannels, 1, maxChannels);
        history.assign ((size_t) partitions * 2 * blockSize, {});
        input.assign ((size_t) blockSize, {});
        previous.assign ((size_t) blockSize, {});
        work.assign ((size_t) 2 * blockSize, {});
        sum.assign ((size_t) 2 * blockSize, {});
        fadeSum.assign ((size_t) 2 * blockSize, {});
        output.assign ((size_t) blockSize, {});
        reset();
    }

    void LinearPhaseFir::reset()
    {
        std::fill (history.begin(), history.end(), std::complex<float> {});
        std::fill (input.begin(), input.end(), std::complex<float> {});
        std::fill (previous.begin(), previous.end(), std::complex<float> {});
        std::fill (output.begin(), output.end(), std::complex<float> {});
        newest = 0;
        position = 0;
    }

    void LinearPhaseFir::give (std::unique_ptr<Kernel> kernel)
    {
        if (kernel == nullptr || kernel->partitions != partitions)
            return;   // made for another length (the sample rate changed meanwhile)
        if (current == nullptr)
            current = std::move (kernel);   // the first: nothing to fade from
        else if (incoming == nullptr)
            incoming = std::move (kernel);
    }

    void LinearPhaseFir::process (float* const* channels, int numChannels, int numFrames)
    {
        if (length == 0)
            return;
        const auto stereo = numChannels > 1 && channelCount > 1;
        for (int i = 0; i < numFrames; ++i)
        {
            const auto out = output[(size_t) position];
            input[(size_t) position] = { channels[0][i], stereo ? channels[1][i] : 0.0f };
            channels[0][i] = out.real();
            if (stereo)
                channels[1][i] = out.imag();
            if (++position == blockSize)
            {
                runBlock();
                position = 0;
            }
        }
        // A third channel and up: never given, see MasteringEffect (1 or 2).
    }

    void LinearPhaseFir::runBlock()
    {
        // Overlap-save: the last two blocks of input, transformed, join the history.
        std::copy (previous.begin(), previous.end(), work.begin());
        std::copy (input.begin(), input.end(), work.begin() + blockSize);
        std::copy (input.begin(), input.end(), previous.begin());
        fft.forward (work);
        newest = (newest + 1) % partitions;
        std::copy (work.begin(), work.end(), history.begin() + (std::ptrdiff_t) newest * 2 * blockSize);

        if (current == nullptr)
        {
            std::fill (output.begin(), output.end(), std::complex<float> {});
            return;
        }

        auto convolve = [&] (const Kernel& k, std::vector<std::complex<float>>& into)
        {
            std::fill (into.begin(), into.end(), std::complex<float> {});
            for (int p = 0; p < partitions; ++p)
            {
                const auto* x = history.data() + (size_t) ((newest - p + partitions) % partitions) * 2 * blockSize;
                const auto* h = k.spectra.data() + (size_t) p * 2 * blockSize;
                for (int b = 0; b < 2 * blockSize; ++b)
                    into[(size_t) b] += x[b] * h[b];
            }
            fft.inverse (into);
        };

        convolve (*current, sum);
        if (incoming != nullptr)
        {
            // Fades from the old kernel to the new over this block, then keeps the new.
            convolve (*incoming, fadeSum);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto t = (float) (i + 1) / blockSize;
                output[(size_t) i] = sum[(size_t) (blockSize + i)] * (1.0f - t) + fadeSum[(size_t) (blockSize + i)] * t;
            }
            if (retired == nullptr)
                retired = std::move (current);
            current = std::move (incoming);   // an old one not yet taken back is freed here
            return;
        }
        for (int i = 0; i < blockSize; ++i)
            output[(size_t) i] = sum[(size_t) (blockSize + i)];
    }
}
