// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Fft.h"

#include <complex>
#include <functional>
#include <memory>
#include <vector>

// A linear-phase FIR filter: made from a magnitude response alone (every frequency is
// delayed by the same time, so nothing is smeared in phase), then run by uniformly
// partitioned FFT convolution. The Φ EQ's linear-phase mode, for mastering. Plain C++.
namespace phi
{
    class LinearPhaseFir
    {
    public:
        static constexpr int blockOrder = 9;
        static constexpr int blockSize = 1 << blockOrder;   // 512: the partition, and the input buffering
        static constexpr int maxChannels = 2;               // two channels run as one complex signal

        // The filter's length: 16384 taps up to 50 kHz (2.9 Hz apart at 48 kHz, fine enough
        // for a 19 Hz low cut), 32768 above, so a cut means the same at 96 kHz.
        static int tapsFor (double sampleRate);

        // An impulse of `taps` (a power of 2) samples, symmetric about taps / 2, whose
        // magnitude is `magnitude (hz)` (linear, not dB): sampled on taps / 2 + 1 bins,
        // made into a zero-phase impulse, centred and Blackman-windowed.
        static std::vector<float> design (const std::function<double (double hz)>& magnitude, int taps, double sampleRate);

        // An impulse cut into partitions and transformed, ready to run. Made off the audio thread.
        struct Kernel
        {
            int partitions = 0;
            std::vector<std::complex<float>> spectra;   // partitions × 2 blockSize
        };
        static std::unique_ptr<Kernel> prepareKernel (const std::vector<float>& impulse);

        void prepare (int taps, int numChannels);
        void reset();
        int taps() const { return length; }
        // The delay the filter adds: the impulse's centre plus one block of buffering.
        int latency() const { return length / 2 + blockSize; }

        // Hands over a new kernel; the next block fades into it over one block. Only while
        // canTake(): one kernel waits at most. The kernel it replaces comes back from
        // takeRetired(), so it can be freed elsewhere than on the audio thread.
        bool canTake() const { return incoming == nullptr; }
        void give (std::unique_ptr<Kernel> kernel);
        std::unique_ptr<Kernel> takeRetired() { return std::move (retired); }
        bool hasKernel() const { return current != nullptr || incoming != nullptr; }

        // Silence until a kernel has been given.
        void process (float* const* channels, int numChannels, int numFrames);

    private:
        void runBlock();

        int length = 0, partitions = 0, channelCount = 1;
        Fft fft { blockOrder + 1 };
        std::unique_ptr<Kernel> current, incoming, retired;
        std::vector<std::complex<float>> history;   // the last `partitions` input spectra, a ring
        int newest = 0;
        std::vector<std::complex<float>> input, previous, work, sum, fadeSum;   // 2 blockSize, blockSize
        std::vector<std::complex<float>> output;    // blockSize, what is played while the next block fills
        int position = 0;
    };
}
