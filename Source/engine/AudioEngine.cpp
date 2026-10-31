// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioEngine.h"
#include "../core/Dsp.h"
#include "../core/Metadata.h"
#include "../core/RiffChunks.h"
#include "Codec.h"
#include "MasterChain.h"
#include "Prism.h"
#include "StockFrame.h"
#include "X11Windows.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <map>
#include <mutex>
#include <unordered_map>

namespace
{
    // Plays the current AudioData snapshot. Reads straight from memory, so the
    // transport needs no read-ahead buffer and edits are heard immediately.
    class DocumentSource final : public juce::PositionableAudioSource
    {
    public:
        void setData (AudioDataPtr d)
        {
            std::atomic_store (&data, std::move (d));
        }

        AudioDataPtr getData() const { return std::atomic_load (&data); }

        void prepareToPlay (int, double) override {}
        void releaseResources() override {}

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
        {
            info.clearActiveBufferRegion();
            const auto d = getData();
            if (d == nullptr)
                return;

            auto pos = position.load();
            const auto numChannels = d->numChannels();
            ensureScratch (numChannels, info.numSamples);
            const auto ref = std::atomic_load (&reference);
            const auto hearRef = ref != nullptr && hearReference.load();
            const auto withRef = ref != nullptr && (hearRef || refWeight > 0.0f);
            if (withRef)
                ensureRefScratch (ref->numChannels(), info.numSamples);

            // Looping wraps from the loop's end back to its start, within the block.
            const auto from = loopStart.load(), to = loopEnd.load();
            const auto now = clock != nullptr ? clock->load() : 0;
            const auto ratio = deviceFramesPerFrame.load();
            const auto restarted = startPending.exchange (false);
            for (int done = 0; done < info.numSamples;)
            {
                auto n = info.numSamples - done;
                if (to > from && pos < to)
                    n = (int) std::min<juce::int64> (n, to - pos);
                remember (now + (double) done * ratio, pos, restarted && done == 0);
                for (size_t ch = 0; ch < scratch.size(); ++ch)
                    scratchPtrs[ch] = scratch[ch].data() + done;
                d->read (pos, n, scratchPtrs.data());
                if (withRef)
                {
                    for (size_t ch = 0; ch < refScratch.size(); ++ch)
                        refPtrs[ch] = refScratch[ch].data() + done;
                    ref->read (pos, n, refPtrs.data());   // same place in the reference; silence past its end
                }
                pos += n;
                done += n;
                if (to > from && pos >= to)
                    pos = from;
            }
            for (size_t ch = 0; ch < scratch.size(); ++ch)
                scratchPtrs[ch] = scratch[ch].data();

            // The buffer has the file's channels (see MonitorSource); a mono file also fills a second one.
            // With a reference in a wide buffer (see MasterSource), the reference goes in a second set of channels.
            const auto wide = layout.load();
            const auto outChannels = wide > 0 ? wide : info.buffer->getNumChannels();
            for (int ch = 0; ch < outChannels; ++ch)
            {
                const auto src = ch < numChannels ? ch : (numChannels == 1 ? 0 : -1);
                if (src >= 0)
                    info.buffer->copyFrom (ch, info.startSample, scratch[(size_t) src].data(), info.numSamples);
            }

            if (withRef)
            {
                // A short equal-power crossfade between your audio and the reference, so switching never clicks.
                weights.resize ((size_t) info.numSamples);
                const auto step = (float) (1.0 / std::max (1.0, d->sampleRate() * 0.02));
                const auto target = hearRef ? 1.0f : 0.0f;
                auto current = refWeight.load();
                for (auto& w : weights)
                {
                    current = target > current ? std::min (target, current + step) : std::max (target, current - step);
                    w = current;
                }
                refWeight = current;
                const auto gain = referenceGain.load();
                const auto refChannels = (int) ref->numChannels();
                for (int ch = 0; ch < outChannels; ++ch)
                {
                    auto* main = info.buffer->getWritePointer (ch, info.startSample);
                    auto* into = wide > 0 && wide + ch < info.buffer->getNumChannels() ? info.buffer->getWritePointer (wide + ch, info.startSample) : main;
                    const auto src = refChannels == 1 ? 0 : (ch < refChannels ? ch : -1);
                    for (int i = 0; i < info.numSamples; ++i)
                    {
                        const auto w = weights[(size_t) i];
                        main[i] *= std::cos (w * juce::MathConstants<float>::halfPi);
                        if (src >= 0)
                            into[i] += gain * std::sin (w * juce::MathConstants<float>::halfPi) * refScratch[(size_t) src][(size_t) i];
                    }
                }
            }
            else
                refWeight = 0.0f;

            position = pos;
        }

        // Where the sound now leaving the speakers was read from: `delay` device
        // samples ago, following loops, jumps and restarts. `frameRatio` converts
        // device samples to file frames.
        juce::int64 heardPosition (double delay, double frameRatio) const
        {
            if (clock == nullptr)
                return position.load();
            const auto target = (double) clock->load() - delay;
            const auto newest = head.load (std::memory_order_acquire);
            for (int back = 0; back < historySize; ++back)
            {
                const auto& e = history[(size_t) ((newest - back + historySize) % historySize)];
                const auto time = e.time.load (std::memory_order_relaxed);
                if (time <= target)
                    return e.position.load (std::memory_order_relaxed) + (juce::int64) ((target - time) * frameRatio);
                if (e.restart.load (std::memory_order_relaxed))
                    return e.position.load (std::memory_order_relaxed);   // still hearing the start
            }
            return position.load();
        }

        // The A/B reference: another recording at the file's sample rate, heard in place
        // of the file at the same position. Set from the message thread.
        AudioDataPtr reference;
        std::atomic<float> referenceGain { 1.0f };
        std::atomic<bool> hearReference { false };
        std::atomic<bool> referenceClean { true };   // heard without the Master Section
        std::atomic<int> layout { 0 };               // set by MasterSource: > 0 puts the reference in channels [layout, 2 * layout)
        bool referenceAudible() const { return std::atomic_load (&reference) != nullptr && (hearReference.load() || refWeight > 0.0f); }

        // Device samples played so far, counted by MasterSource, and how many one callback advances it.
        const std::atomic<juce::int64>* clock = nullptr;
        std::atomic<bool> startPending { false };   // set by play(): the next read starts after a silence
        std::atomic<double> deviceFramesPerFrame { 1.0 };   // device rate / file rate

        void setLoop (juce::int64 start, juce::int64 end)
        {
            loopStart = start;
            loopEnd = end;
        }

        void setNextReadPosition (juce::int64 p) override { position = p; }
        juce::int64 getNextReadPosition() const override  { return position.load(); }

        juce::int64 getTotalLength() const override
        {
            const auto d = getData();
            return d != nullptr ? d->length() : 0;
        }

        bool isLooping() const override { return loopEnd.load() > loopStart.load(); }

    private:
        void ensureScratch (int channels, int frames)
        {
            // Grows only on the first blocks of a session; steady-state playback does not allocate.
            if ((int) scratch.size() < channels || (scratch.empty() ? 0 : (int) scratch[0].size()) < frames)
            {
                scratch.assign ((size_t) std::max (channels, 2), std::vector<float> ((size_t) std::max (frames, 8192)));
                scratchPtrs.clear();
                for (auto& s : scratch)
                    scratchPtrs.push_back (s.data());
            }
        }

        void ensureRefScratch (int channels, int frames)
        {
            if ((int) refScratch.size() != channels || (refScratch.empty() ? 0 : (int) refScratch[0].size()) < frames)
            {
                refScratch.assign ((size_t) channels, std::vector<float> ((size_t) std::max (frames, 8192)));
                refPtrs.clear();
                for (auto& r : refScratch)
                    refPtrs.push_back (r.data());
            }
        }

        void remember (double time, juce::int64 pos, bool restart)
        {
            const auto next = (head.load (std::memory_order_relaxed) + 1) % historySize;
            auto& e = history[(size_t) next];
            e.time.store (time, std::memory_order_relaxed);
            e.position.store (pos, std::memory_order_relaxed);
            e.restart.store (restart, std::memory_order_relaxed);
            head.store (next, std::memory_order_release);
        }

        // What was read when, for heardPosition: one entry per stretch read without a jump.
        struct Read
        {
            std::atomic<double> time { -1.0e18 };
            std::atomic<juce::int64> position { 0 };
            std::atomic<bool> restart { false };
        };
        static constexpr int historySize = 512;
        std::array<Read, historySize> history;
        std::atomic<int> head { 0 };

        AudioDataPtr data;
        std::atomic<juce::int64> position { 0 }, loopStart { 0 }, loopEnd { 0 };
        std::atomic<float> refWeight { 0.0f };   // 0 your audio, 1 the reference; moves smoothly toward hearReference
        std::vector<float> weights;
        std::vector<std::vector<float>> refScratch;
        std::vector<float*> refPtrs;
        std::vector<std::vector<float>> scratch { 2, std::vector<float> (8192) };
        std::vector<float*> scratchPtrs { scratch[0].data(), scratch[1].data() };
    };

    // Sits after the transport and measures what is actually sent to the outputs.
    // Runs the Master Section on whatever is played.
    class MasterSource final : public juce::AudioSource
    {
    public:
        MasterSource (juce::AudioSource& in, MasterChain& c, DocumentSource& d) : input (in), chain (c), document (d) {}

        void prepareToPlay (int blockSize, double sampleRate) override
        {
            input.prepareToPlay (blockSize, sampleRate);
            chain.prepare (sampleRate, blockSize);
        }

        void releaseResources() override { input.releaseResources(); }

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
        {
            const auto channels = std::min (info.buffer->getNumChannels(), maxChannels);
            if (document.referenceClean.load() && document.referenceAudible())
                cleanFor = std::max (cleanFor, (juce::int64) chain.listeningLatency() + info.numSamples);

            if (cleanFor <= 0)
            {
                document.layout = 0;
                input.getNextAudioBlock (info);
                chain.processRealtime (*info.buffer, info.startSample, info.numSamples);
                wasClean = false;
            }
            else
            {
                // The reference is read into a second set of channels and goes around the
                // Master Section, delayed by its latency so both stay in step.
                if (! wasClean)
                    for (auto& line : delay)
                        std::fill (line.begin(), line.end(), 0.0f);
                wasClean = true;
                for (int done = 0; done < info.numSamples;)
                {
                    const auto n = std::min (info.numSamples - done, wideFrames);
                    juce::AudioBuffer<float> wideBuffer (wide.getArrayOfWritePointers(), 2 * channels, n);
                    wideBuffer.clear();
                    document.layout = channels;
                    input.getNextAudioBlock ({ &wideBuffer, 0, n });
                    const auto at = info.startSample + done;
                    for (int ch = 0; ch < channels; ++ch)
                        info.buffer->copyFrom (ch, at, wideBuffer, ch, 0, n);
                    chain.processRealtime (*info.buffer, at, n);

                    const auto latency = std::clamp (chain.listeningLatency(), 0, delaySize - 1);
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        auto& line = delay[(size_t) ch];
                        const auto* in = wideBuffer.getReadPointer (channels + ch);
                        auto* out = info.buffer->getWritePointer (ch, at);
                        auto w = write;
                        for (int i = 0; i < n; ++i)
                        {
                            line[(size_t) w] = in[i];
                            out[i] += line[(size_t) ((w - latency + delaySize) % delaySize)];
                            w = (w + 1) % delaySize;
                        }
                    }
                    write = (write + n) % delaySize;
                    done += n;
                }
                document.layout = 0;
                cleanFor -= info.numSamples;
            }
            clock.fetch_add (info.numSamples);
        }

        std::atomic<juce::int64> clock { 0 };   // device samples processed so far

    private:
        static constexpr int maxChannels = MeterBlock::maxChannels;
        static constexpr int wideFrames = 4096;
        static constexpr int delaySize = 1 << 16;

        juce::AudioSource& input;
        MasterChain& chain;
        DocumentSource& document;
        juce::AudioBuffer<float> wide { 2 * maxChannels, wideFrames };
        std::vector<std::vector<float>> delay { (size_t) maxChannels, std::vector<float> ((size_t) delaySize) };
        int write = 0;
        juce::int64 cleanFor = 0;   // device samples the reference path stays on, so its delayed tail is heard
        bool wasClean = false;
    };

    class MeterSource final : public juce::AudioSource
    {
    public:
        explicit MeterSource (juce::AudioSource& in) : input (in) {}

        void prepareToPlay (int blockSize, double sampleRate) override
        {
            input.prepareToPlay (blockSize, sampleRate);
            rate = sampleRate;
            framesPerBlock = std::max (1, (int) std::lround (sampleRate * 0.1));
            for (auto& k : kWeighting)
                k.prepare (sampleRate);
            resetBlock();
        }

        void releaseResources() override { input.releaseResources(); }

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
        {
            input.getNextAudioBlock (info);

            const auto numChannels = std::min ({ info.buffer->getNumChannels(), channels.load(), MeterBlock::maxChannels });
            current.numChannels = numChannels;

            pushScope (*info.buffer, info.startSample, info.numSamples, numChannels);
            if (tapping.load())
                pushTap (*info.buffer, info.startSample, info.numSamples, numChannels);

            for (int i = 0; i < info.numSamples; ++i)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto x = info.buffer->getSample (ch, info.startSample + i);
                    current.samplePeak[ch] = std::max (current.samplePeak[ch], std::abs (x));
                    current.truePeak[ch] = std::max (current.truePeak[ch], truePeaks[(size_t) ch].process (x));
                    const auto k = kWeighting[(size_t) ch].process (x);
                    sums[(size_t) ch] += (double) k * k;
                }

                if (++framesInBlock == framesPerBlock)
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                        current.loudnessPower += dsp::loudnessChannelWeight (ch, numChannels) * sums[(size_t) ch] / framesPerBlock;

                    push (current);
                    resetBlock();
                }
            }
        }

        std::atomic<int> channels { 2 };   // the file's channels; a mono file's buffer has a copy in the second

        // Left and right (the first two channels; a mono file's are the same) for the goniometer.
        void takeScope (std::vector<float>& left, std::vector<float>& right)
        {
            const auto scope = scopeFifo.read (scopeFifo.getNumReady());
            for (const auto& [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
            {
                left.insert (left.end(), scopeLeft.begin() + start, scopeLeft.begin() + start + size);
                right.insert (right.end(), scopeRight.begin() + start, scopeRight.begin() + start + size);
            }
        }

        // The program for the Performance recorder: tapChannels per frame, interleaved.
        static constexpr int tapChannels = AudioEngine::programChannels;
        static constexpr int tapFrames = 1 << 18;

        void startTap()
        {
            if (tap.empty())
                tap.assign ((size_t) tapFrames * tapChannels, 0.0f);   // the audio thread only writes while tapping
            tapFifo.read (tapFifo.getNumReady());                        // anything left from a previous run
            tapping = true;
        }

        void stopTap() { tapping = false; }

        int takeTap (std::vector<float>& out, int& numChannels)
        {
            numChannels = tapWidth.load();
            const auto scope = tapFifo.read (tapFifo.getNumReady());
            for (const auto& [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
                out.insert (out.end(), tap.begin() + (ptrdiff_t) start * tapChannels, tap.begin() + (ptrdiff_t) (start + size) * tapChannels);
            return scope.blockSize1 + scope.blockSize2;
        }

        uint64_t tapDropped() const { return tapLost.load(); }

        void takeBlocks (std::vector<MeterBlock>& out)
        {
            const auto scope = fifo.read (fifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) out.push_back (blocks[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i) out.push_back (blocks[(size_t) (scope.startIndex2 + i)]);
        }

    private:
        void pushScope (const juce::AudioBuffer<float>& buffer, int start, int numSamples, int numChannels)
        {
            if (numChannels <= 0)
                return;
            // When the UI falls behind, the newest samples are dropped; the picture is never late.
            const auto scope = scopeFifo.write (std::min (numSamples, scopeFifo.getFreeSpace()));
            const auto* l = buffer.getReadPointer (0, start);
            const auto* r = buffer.getReadPointer (numChannels > 1 ? 1 : 0, start);
            std::copy (l, l + scope.blockSize1, scopeLeft.begin() + scope.startIndex1);
            std::copy (r, r + scope.blockSize1, scopeRight.begin() + scope.startIndex1);
            std::copy (l + scope.blockSize1, l + scope.blockSize1 + scope.blockSize2, scopeLeft.begin() + scope.startIndex2);
            std::copy (r + scope.blockSize1, r + scope.blockSize1 + scope.blockSize2, scopeRight.begin() + scope.startIndex2);
        }

        void pushTap (const juce::AudioBuffer<float>& buffer, int start, int numSamples, int numChannels)
        {
            // A mono file's buffer has a copy in the second channel, so the program is at least stereo.
            const auto width = std::min ({ std::max (numChannels, 2), tapChannels, buffer.getNumChannels() });
            tapWidth = width;
            const auto scope = tapFifo.write (std::min (numSamples, tapFifo.getFreeSpace()));
            if (const auto lost = numSamples - scope.blockSize1 - scope.blockSize2; lost > 0)
                tapLost.fetch_add ((uint64_t) lost);
            int from = start;
            for (const auto& [at, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
            {
                for (int i = 0; i < size; ++i)
                {
                    auto* frame = tap.data() + (size_t) (at + i) * tapChannels;
                    for (int ch = 0; ch < tapChannels; ++ch)
                        frame[ch] = ch < width ? buffer.getSample (ch, from + i) : 0.0f;
                }
                from += size;
            }
        }

    private:
        void push (const MeterBlock& b)
        {
            const auto scope = fifo.write (1);
            if (scope.blockSize1 > 0) blocks[(size_t) scope.startIndex1] = b;
            else if (scope.blockSize2 > 0) blocks[(size_t) scope.startIndex2] = b;
        }

        void resetBlock()
        {
            current = {};
            sums.fill (0.0);
            framesInBlock = 0;
        }

        juce::AudioSource& input;
        double rate = 44100.0;
        int framesPerBlock = 4410, framesInBlock = 0;
        MeterBlock current;
        std::array<double, MeterBlock::maxChannels> sums {};
        std::array<dsp::KWeighting, MeterBlock::maxChannels> kWeighting;
        std::array<dsp::TruePeakDetector, MeterBlock::maxChannels> truePeaks;

        static constexpr int scopeSize = 32768;
        juce::AbstractFifo scopeFifo { scopeSize };
        std::array<float, scopeSize> scopeLeft {}, scopeRight {};

        std::vector<float> tap;
        juce::AbstractFifo tapFifo { tapFrames };
        std::atomic<bool> tapping { false };
        std::atomic<int> tapWidth { 2 };
        std::atomic<uint64_t> tapLost { 0 };

        static constexpr int fifoSize = 256;
        juce::AbstractFifo fifo { fifoSize };
        std::array<MeterBlock, fifoSize> blocks;
    };

    // The last stage before the device. Everything before it (transport, Master
    // Section, meters) works on the file's own channels; this sends them to the
    // outputs, one each when there are enough, or folded down to stereo.
    // Something played live in place of the file, such as the Tracker: stereo at the
    // device's rate, after the Master Section (it is mixed, not mastered, in PHI).
    class LiveSource final : public juce::AudioSource
    {
    public:
        explicit LiveSource (juce::AudioSource& in) : input (in) {}

        void prepareToPlay (int blockSize, double sampleRate) override { input.prepareToPlay (blockSize, sampleRate); }
        void releaseResources() override { input.releaseResources(); }

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
        {
            std::unique_lock lock (mutex, std::try_to_lock);
            if (! lock.owns_lock() || ! render)
            {
                if (lock.owns_lock() || ! active.load())
                    input.getNextAudioBlock (info);
                else
                    info.clearActiveBufferRegion();
                return;
            }
            info.clearActiveBufferRegion();
            auto* left = info.buffer->getWritePointer (0, info.startSample);
            auto* right = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer (1, info.startSample) : nullptr;
            if (right != nullptr)
                render (left, right, info.numSamples);
            else
            {
                scratch.resize ((size_t) info.numSamples);
                render (left, scratch.data(), info.numSamples);
                for (int i = 0; i < info.numSamples; ++i)
                    left[i] = 0.5f * (left[i] + scratch[(size_t) i]);
            }
        }

        void set (AudioEngine::LiveRender r)
        {
            std::lock_guard lock (mutex);
            render = std::move (r);
            active = (bool) render;
        }
        std::atomic<bool> active { false };

    private:
        juce::AudioSource& input;
        std::mutex mutex;
        AudioEngine::LiveRender render;
        std::vector<float> scratch;
    };

    // Instruments (the Sampler) played on top of whatever else is heard: stereo at the
    // device's rate, added after the Master Section, so they sound while a file plays too.
    class InstrumentSource final : public juce::AudioSource
    {
    public:
        explicit InstrumentSource (juce::AudioSource& in) : input (in) {}

        void prepareToPlay (int blockSize, double sampleRate) override { input.prepareToPlay (blockSize, sampleRate); }
        void releaseResources() override { input.releaseResources(); }

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
        {
            input.getNextAudioBlock (info);
            std::unique_lock lock (mutex, std::try_to_lock);
            if (! lock.owns_lock() || (! render && ! sounding))
                return;
            auto* left = info.buffer->getWritePointer (0, info.startSample);
            // Both add to left and right; once a one-shot has ended it is not called again.
            const auto play = [this] (float* l, float* r, int n)
            {
                if (render)
                    render (l, r, n);
                if (sounding && ! oneShot (l, r, n))
                    sounding = false;   // kept, so nothing is freed on the audio thread
            };
            if (info.buffer->getNumChannels() < 2)
            {
                scratch.assign ((size_t) info.numSamples * 2, 0.0f);
                play (scratch.data(), scratch.data() + info.numSamples, info.numSamples);
                for (int i = 0; i < info.numSamples; ++i)
                    left[i] += 0.5f * (scratch[(size_t) i] + scratch[(size_t) (info.numSamples + i)]);
                return;
            }
            auto* right = info.buffer->getWritePointer (1, info.startSample);
            if (monoInput.load())
                std::copy (left, left + info.numSamples, right);   // a mono file, heard on both sides
            play (left, right, info.numSamples);
        }

        void set (AudioEngine::LiveRender r)
        {
            std::lock_guard lock (mutex);
            render = std::move (r);
            active = (bool) render;
        }
        void setOneShot (AudioEngine::OneShot s)
        {
            std::lock_guard lock (mutex);
            oneShot = std::move (s);   // the one before is freed here, not on the audio thread
            sounding = (bool) oneShot;
        }
        std::atomic<bool> active { false }, monoInput { false };

    private:
        juce::AudioSource& input;
        std::mutex mutex;
        AudioEngine::LiveRender render;
        AudioEngine::OneShot oneShot;
        bool sounding = false;
        std::vector<float> scratch;
    };

    class MonitorSource final : public juce::AudioSource
    {
    public:
        static constexpr int maxChannels = MeterBlock::maxChannels;

        explicit MonitorSource (juce::AudioSource& in) : input (in)
        {
            for (int n = 1; n <= maxChannels; ++n)
            {
                const auto m = dsp::stereoDownmix (n);
                for (int side = 0; side < 2; ++side)
                    std::copy (m[(size_t) side].begin(), m[(size_t) side].end(), downmix[(size_t) n][(size_t) side].begin());
            }
        }

        void prepareToPlay (int blockSize, double sampleRate) override
        {
            input.prepareToPlay (blockSize, sampleRate);
            work.setSize (maxChannels, std::max (blockSize, 512));
        }

        void releaseResources() override { input.releaseResources(); }

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
        {
            const auto n = std::clamp (channels.load(), 1, maxChannels);
            const auto outs = info.buffer->getNumChannels();
            const bool fold = n > 2 ? (! separate.load() || outs < n) : (n == 2 && outs < 2);
            const auto* order = alsaOrder.load() ? alsaPositions (n) : nullptr;
            downmixing = fold && outs > 0;

            for (int done = 0; done < info.numSamples;)
            {
                const auto count = std::min (work.getNumSamples(), info.numSamples - done);
                // A mono file gets a second channel so stereo plugins see both sides.
                juce::AudioBuffer<float> file (work.getArrayOfWritePointers(), std::max (n, 2), count);
                file.clear();
                input.getNextAudioBlock ({ &file, 0, count });

                const auto at = info.startSample + done;
                if (n == 1)
                {
                    // Both sides, which differ once an effect such as Stereoize widens them.
                    for (int ch = 0; ch < outs; ++ch)
                        info.buffer->copyFrom (ch, at, file, ch % 2, 0, count);
                    if (outs == 1)
                    {
                        info.buffer->applyGain (0, at, count, 0.5f);
                        info.buffer->addFrom (0, at, file, 1, 0, count, 0.5f);
                    }
                }
                else if (! fold)
                {
                    for (int ch = 0; ch < outs; ++ch)
                        info.buffer->clear (ch, at, count);
                    for (int ch = 0; ch < n; ++ch)
                        info.buffer->copyFrom (order != nullptr ? order[ch] : ch, at, file, ch, 0, count);
                }
                else
                {
                    for (int ch = 0; ch < outs; ++ch)
                        info.buffer->clear (ch, at, count);

                    const auto& matrix = customFor.load() == n ? custom : downmix[(size_t) n];
                    for (int side = 0; side < std::min (outs, 2); ++side)
                        for (int src = 0; src < n; ++src)
                            if (const auto gain = matrix[(size_t) side][(size_t) src]; gain != 0.0f)
                                info.buffer->addFrom (side, at, file, src, 0, count, outs == 1 ? gain * 0.5f : gain);

                    if (outs == 1)
                        for (int src = 0; src < n; ++src)
                            if (const auto gain = matrix[1][(size_t) src]; gain != 0.0f)
                                info.buffer->addFrom (0, at, file, src, 0, count, gain * 0.5f);
                }

                done += count;
            }
        }

        std::atomic<int> channels { 2 };
        std::atomic<bool> separate { false }, alsaOrder { false }, downmixing { false };

        // A fold-down for a layout other than the usual one, used while customFor is the channel count.
        void setCustom (int n, const std::array<std::vector<float>, 2>& m)
        {
            customFor = 0;
            for (size_t side = 0; side < 2; ++side)
                for (size_t ch = 0; ch < (size_t) maxChannels; ++ch)
                    custom[side][ch] = ch < m[side].size() ? m[side][ch] : 0.0f;
            customFor = n;
        }
        std::atomic<int> customFor { 0 };

    private:
        // Where each WAV-ordered channel goes on an ALSA device (FL FR RL RR FC LFE SL SR).
        static const int* alsaPositions (int n)
        {
            static constexpr int five[] { 0, 1, 4, 2, 3 }, six[] { 0, 1, 4, 5, 2, 3 }, eight[] { 0, 1, 4, 5, 2, 3, 6, 7 };
            return n == 5 ? five : n == 6 ? six : n == 8 ? eight : nullptr;
        }

        juce::AudioSource& input;
        juce::AudioBuffer<float> work { maxChannels, 2048 };
        std::array<std::array<std::array<float, maxChannels>, 2>, maxChannels + 1> downmix {};
        std::array<std::array<float, maxChannels>, 2> custom {};
    };

    // Spatial mixing's speakers (mixer::Engine::takeSpeakers): their feeds added to the device's
    // outputs after everything else, past the stereo pair the headphones (or the file) are on.
    class SpeakerSource final : public juce::AudioSource
    {
    public:
        explicit SpeakerSource (juce::AudioSource& in) : input (in) {}

        void prepareToPlay (int blockSize, double sampleRate) override
        {
            input.prepareToPlay (blockSize, sampleRate);
            outs.resize ((size_t) maxOutputs);
        }
        void releaseResources() override { input.releaseResources(); }

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
        {
            input.getNextAudioBlock (info);
            std::unique_lock lock (mutex, std::try_to_lock);
            if (! lock.owns_lock() || ! feed)
                return;
            const auto n = std::min (info.buffer->getNumChannels(), maxOutputs);
            outs.resize ((size_t) maxOutputs);   // sized in prepareToPlay; never grows here
            for (int ch = 0; ch < n; ++ch)
                outs[(size_t) ch] = info.buffer->getWritePointer (ch, info.startSample);
            feed (outs.data(), n, info.numSamples);
        }

        void set (AudioEngine::SpeakerFeed f)
        {
            std::lock_guard lock (mutex);
            feed = std::move (f);
        }
        static constexpr int maxOutputs = 64;

    private:
        juce::AudioSource& input;
        std::mutex mutex;
        AudioEngine::SpeakerFeed feed;
        std::vector<float*> outs = std::vector<float*> ((size_t) maxOutputs);
    };

    juce::AudioFormatManager& formats()
    {
        static juce::AudioFormatManager fm;
        static std::once_flag registered;
        std::call_once (registered, [] { fm.registerBasicFormats(); });
        return fm;
    }

    std::mutex formatsMutex;
}

//==============================================================================
struct AudioEngine::Impl
{
    Impl()
    {
        // The thread that creates the MessageManager becomes JUCE's message thread.
        juce::MessageManager::getInstance();
        player.setSource (&speakers);
        document.clock = &masterSource.clock;
    }

    ~Impl()
    {
        deviceManager.removeAudioCallback (&player);
        transport.setSource (nullptr);
        player.setSource (nullptr);
        deviceManager.closeAudioDevice();
    }

    MasterChain master;
    juce::AudioDeviceManager deviceManager;
    DocumentSource document;
    juce::AudioTransportSource transport;
    MasterSource masterSource { transport, master, document };
    LiveSource live { masterSource };
    InstrumentSource instrument { live };
    MeterSource meter { instrument };
    MonitorSource monitor { meter };
    SpeakerSource speakers { monitor };
    juce::AudioSourcePlayer player;
    double sourceRate = 0.0;
    int sourceChannels = 0;
    int speakerOutputs = 0;   // the outputs spatial mixing's speakers need, from the first
};

AudioEngine::AudioEngine() : impl (std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine()
{
    impl = nullptr;
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
}

std::string AudioEngine::openDefaultDevice()
{
    auto error = impl->deviceManager.initialiseWithDefaultDevices (0, 2);
    impl->deviceManager.addAudioCallback (&impl->player);
    useOutputsFor (impl->sourceChannels);
    return error.toStdString();
}

void AudioEngine::setData (AudioDataPtr data)
{
    const auto rate = data != nullptr ? data->sampleRate() : 0.0;
    const auto channels = data != nullptr ? std::clamp (data->numChannels(), 1, MeterBlock::maxChannels) : 0;
    const auto pos = impl->document.getNextReadPosition();

    if (channels > 0 && channels != impl->sourceChannels)
    {
        // The transport, Master Section and meters all follow the file's channel count.
        impl->master.setChannels (std::max (channels, 2));
        impl->monitor.channels = channels;
        impl->meter.channels = channels;
        useOutputsFor (channels);
    }

    impl->document.setData (std::move (data));

    if (std::abs (rate - impl->sourceRate) > 1.0e-6 || (channels > 0 && channels != impl->sourceChannels))
    {
        // A new sample rate or channel count needs the transport's resampler rebuilt.
        const bool wasPlaying = isPlaying();
        impl->transport.setSource (nullptr);
        if (rate > 0.0)
            impl->transport.setSource (&impl->document, 0, nullptr, rate, 2 * std::max (channels, 2));   // twice: room for an A/B reference

        impl->sourceRate = rate;
        impl->sourceChannels = channels > 0 ? channels : impl->sourceChannels;
        impl->document.setNextReadPosition (std::min<Frame> (pos, impl->document.getTotalLength()));
        if (wasPlaying)
            impl->transport.start();
    }
    updateInstrumentChannels();
}

bool AudioEngine::hasAudio() const { return impl->document.getData() != nullptr; }

void AudioEngine::play()
{
    if (! hasAudio())
        return;

    if (positionFrames() >= impl->document.getTotalLength())
        setPositionFrames (0);

    if (! isPlaying())
        impl->document.startPending = true;
    impl->transport.start();
}

void AudioEngine::setReference (AudioDataPtr reference, float gain)
{
    impl->document.referenceGain = gain;
    std::atomic_store (&impl->document.reference, std::move (reference));
}

void AudioEngine::setReferenceGain (float gain)        { impl->document.referenceGain = gain; }
void AudioEngine::setHearingReference (bool on)        { impl->document.hearReference = on; }
bool AudioEngine::isHearingReference() const           { return impl->document.hearReference.load(); }
void AudioEngine::setReferenceBypassesMaster (bool on) { impl->document.referenceClean = on; }

void AudioEngine::stop()            { impl->transport.stop(); }

void AudioEngine::setLiveSource (LiveRender render)
{
    const bool on = (bool) render;
    if (on)
        impl->transport.stop();
    impl->live.set (std::move (render));
    // Stereo while live; the file's own channel count again afterwards.
    const auto channels = on ? 2 : std::max (1, impl->sourceChannels);
    impl->monitor.channels = channels;
    impl->meter.channels = channels;
    updateInstrumentChannels();
}

bool AudioEngine::hasLiveSource() const { return impl->live.active.load(); }

void AudioEngine::setInstrument (LiveRender render)
{
    impl->instrument.set (std::move (render));
    updateInstrumentChannels();
}

bool AudioEngine::hasInstrument() const { return impl->instrument.active.load(); }

void AudioEngine::setSpeakerFeed (SpeakerFeed feed, int outputsNeeded)
{
    impl->speakers.set (std::move (feed));
    impl->speakerOutputs = std::clamp (outputsNeeded, 0, SpeakerSource::maxOutputs);
    useOutputsFor (impl->sourceChannels);
}

void AudioEngine::playOneShot (OneShot render)
{
    impl->instrument.setOneShot (std::move (render));
}

void AudioEngine::updateInstrumentChannels()
{
    // An instrument is stereo: a mono file is then sent to both sides before it is added.
    const bool on = impl->instrument.active.load();
    const auto file = std::max (1, impl->sourceChannels);
    impl->instrument.monoInput = on && file == 1 && ! impl->live.active.load();
    if (impl->live.active.load())
        return;
    const auto channels = on ? std::max (2, file) : file;
    impl->monitor.channels = channels;
    impl->meter.channels = channels;
}
void AudioEngine::setLoop (Frame start, Frame end) { impl->document.setLoop (start, end); }
bool AudioEngine::isPlaying() const { return impl->transport.isPlaying(); }

Frame AudioEngine::positionFrames() const { return impl->document.getNextReadPosition(); }

void AudioEngine::setPositionFrames (Frame f)
{
    impl->document.setNextReadPosition (std::clamp<Frame> (f, 0, impl->document.getTotalLength()));
}

Frame AudioEngine::heardPositionFrames() const
{
    if (! isPlaying() || impl->sourceRate <= 0.0)
        return positionFrames();

    auto* device = impl->deviceManager.getCurrentAudioDevice();
    const auto deviceRate = device != nullptr ? device->getCurrentSampleRate() : impl->sourceRate;
    const auto delay = impl->master.listeningLatency() + (device != nullptr ? device->getOutputLatencyInSamples() : 0);
    impl->document.deviceFramesPerFrame = deviceRate / impl->sourceRate;
    const auto heard = impl->document.heardPosition ((double) delay, impl->sourceRate / deviceRate);
    return std::clamp<Frame> (heard, 0, impl->document.getTotalLength());
}

int AudioEngine::listeningLatency() const
{
    return impl->master.listeningLatency();
}

double AudioEngine::position() const
{
    return impl->sourceRate > 0.0 ? (double) positionFrames() / impl->sourceRate : 0.0;
}

double AudioEngine::length() const
{
    const auto d = impl->document.getData();
    return d != nullptr ? d->lengthSeconds() : 0.0;
}

void AudioEngine::setLayoutName (const std::string& name)
{
    const auto n = std::clamp (impl->monitor.channels.load(), 1, MeterBlock::maxChannels);
    if (name.empty())
        impl->monitor.customFor = 0;
    else
        impl->monitor.setCustom (n, dsp::stereoDownmix (n, name));
}

double AudioEngine::deviceSampleRate() const
{
    if (auto* device = impl->deviceManager.getCurrentAudioDevice())
        return device->getCurrentSampleRate();
    return 48000.0;
}

double AudioEngine::audioLoad() const
{
    return impl->deviceManager.getCurrentAudioDevice() != nullptr ? impl->deviceManager.getCpuUsage() : -1.0;
}

std::string AudioEngine::deviceDescription() const
{
    if (auto* device = impl->deviceManager.getCurrentAudioDevice())
        return (device->getTypeName() + ": " + device->getName() + " @ "
                + juce::String (device->getCurrentSampleRate(), 0) + " Hz, "
                + juce::String (device->getCurrentBufferSizeSamples()) + " samples").toStdString();

    return "No audio device";
}

std::vector<AudioOutputChoice> AudioEngine::availableOutputs()
{
    std::vector<AudioOutputChoice> result;

    for (auto* type : impl->deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        for (auto& name : type->getDeviceNames (false))
            result.push_back ({ type->getTypeName().toStdString(), name.toStdString() });
    }

    return result;
}

AudioOutputChoice AudioEngine::currentOutput() const
{
    if (auto* device = impl->deviceManager.getCurrentAudioDevice())
        return { device->getTypeName().toStdString(), device->getName().toStdString() };

    return {};
}

std::string AudioEngine::selectOutput (const AudioOutputChoice& choice)
{
    auto& dm = impl->deviceManager;
    dm.setCurrentAudioDeviceType (juce::String (choice.type), true);

    auto setup = dm.getAudioDeviceSetup();
    setup.outputDeviceName = juce::String (choice.name);
    setup.inputDeviceName = {};
    setup.useDefaultOutputChannels = true;   // the previous device's channel choice may not exist here
    auto error = dm.setAudioDeviceSetup (setup, true).toStdString();
    if (error.empty())
        useOutputsFor (impl->sourceChannels);
    return error;
}

void AudioEngine::useOutputsFor (int channels)
{
    // Opens as many of the device's outputs as the file has channels (never fewer
    // than two), so 5.1 plays on six speakers when the device has them.
    auto* device = impl->deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    impl->monitor.alsaOrder = device->getTypeName().containsIgnoreCase ("ALSA");
    // Spatial mixing's speakers need theirs whatever the file's channels are heard on.
    const auto need = std::max (impl->monitor.separate.load() ? channels : 0, impl->speakerOutputs);
    if (need <= 2)
        return;

    const auto available = device->getOutputChannelNames().size();
    const auto wanted = std::clamp (need, 2, std::max (2, available));
    if (device->getActiveOutputChannels().countNumberOfSetBits() >= wanted)
        return;

    auto setup = impl->deviceManager.getAudioDeviceSetup();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, wanted, true);
    impl->deviceManager.setAudioDeviceSetup (setup, true);
}

void AudioEngine::setMonitorMode (MonitorMode m)
{
    impl->monitor.separate = m == MonitorMode::separateOutputs;
    if (m == MonitorMode::separateOutputs && impl->sourceChannels > 2)
        useOutputsFor (impl->sourceChannels);
}

AudioEngine::MonitorMode AudioEngine::monitorMode() const
{
    return impl->monitor.separate.load() ? MonitorMode::separateOutputs : MonitorMode::stereoDownmix;
}

int AudioEngine::outputChannels() const
{
    if (auto* device = impl->deviceManager.getCurrentAudioDevice())
        return device->getActiveOutputChannels().countNumberOfSetBits();
    return 0;
}

bool AudioEngine::isDownmixing() const
{
    return impl->monitor.downmixing.load();
}

void AudioEngine::takeScopeSamples (std::vector<float>& left, std::vector<float>& right)
{
    impl->meter.takeScope (left, right);
}

void AudioEngine::startProgramCapture()  { impl->meter.startTap(); }
void AudioEngine::stopProgramCapture()   { impl->meter.stopTap(); }
uint64_t AudioEngine::programFramesLost() const { return impl->meter.tapDropped(); }

int AudioEngine::takeProgram (std::vector<float>& interleaved, int& numChannels)
{
    return impl->meter.takeTap (interleaved, numChannels);
}

std::vector<MeterBlock> AudioEngine::takeMeterBlocks()
{
    std::vector<MeterBlock> result;
    impl->meter.takeBlocks (result);
    return result;
}

void AudioEngine::dispatchMessages()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (0);
}

//==============================================================================
void AudioEngine::setConfigFolder (const std::string& folder) { impl->master.setConfigFolder (juce::File (folder)); }

MasterChain& AudioEngine::masterChain() { return impl->master; }

std::vector<PluginInfo> AudioEngine::knownPlugins() const            { return impl->master.knownPlugins(); }
std::vector<PluginInfo> AudioEngine::allPlugins() const              { return impl->master.allPlugins(); }
void AudioEngine::setPluginEnabled (const std::string& id, bool on)  { impl->master.setPluginEnabled (id, on); }
std::vector<std::string> AudioEngine::skippedPluginFiles() const     { return impl->master.skippedFiles(); }
void AudioEngine::retrySkippedPlugin (const std::string& file)       { impl->master.retrySkipped (file); }
std::vector<std::string> AudioEngine::pluginSearchFolders() const    { return impl->master.searchFolders(); }
int AudioEngine::startPluginScan()                                   { return impl->master.startScan(); }
bool AudioEngine::scanNextPlugin (std::string& file)                 { return impl->master.scanNext (file); }
std::string AudioEngine::finishPluginScan()                          { return impl->master.finishScan(); }

std::string AudioEngine::loadMasterPlugin (int slot, const std::string& id) { return impl->master.load (slot, id); }
void AudioEngine::clearMasterSlot (int slot)                         { impl->master.clear (slot); }
std::string AudioEngine::reloadMasterSlot (int slot)                 { return impl->master.reload (slot); }
void AudioEngine::moveMasterSlot (int from, int to)                  { impl->master.move (from, to); }
void AudioEngine::setMasterSlotBypassed (int slot, bool b)           { impl->master.setBypassed (slot, b); }
int AudioEngine::masterChannels() const                              { return impl->master.channels(); }
MasterSlotInfo AudioEngine::masterSlot (int slot) const              { return impl->master.info (slot); }
std::vector<modrack::Modulator> AudioEngine::modulators() const      { return impl->master.modulators(); }
void AudioEngine::setModulators (const std::vector<modrack::Modulator>& m) { impl->master.setModulators (m); }
std::vector<modrack::Route> AudioEngine::modulatorRoutes() const      { return impl->master.modulatorRoutes(); }
void AudioEngine::setModulatorRoutes (const std::vector<modrack::Route>& r) { impl->master.setModulatorRoutes (r); }
float AudioEngine::modulatorValue (int m) const                      { return impl->master.modulatorValue (m); }
bool AudioEngine::modulatorRouteLive (int r) const                   { return impl->master.routeLive (r); }
void AudioEngine::setMasterGainDb (double db)                        { impl->master.setGainDb (db); }
double AudioEngine::masterGainDb() const                             { return impl->master.gainDb(); }
void AudioEngine::setMasterBypassed (bool b)                         { impl->master.setAllBypassed (b); }
bool AudioEngine::isMasterBypassed() const                           { return impl->master.isAllBypassed(); }
bool AudioEngine::masterIsActive() const                             { return impl->master.isActive(); }
void AudioEngine::setMasterDither (int bits, dsp::Dither d)          { impl->master.setDither (bits, d); }
int AudioEngine::masterDitherBits() const                            { return impl->master.ditherBits(); }
dsp::Dither AudioEngine::masterDitherType() const                    { return impl->master.ditherType(); }

std::vector<PluginParameterInfo> AudioEngine::masterSlotParameters (int slot) const { return impl->master.parameters (slot); }
PluginParameterInfo AudioEngine::masterSlotParameter (int slot, int index) const   { return impl->master.parameter (slot, index); }
bool AudioEngine::setMasterSlotParameterText (int slot, int index, const std::string& text) { return impl->master.setParameterFromText (slot, index, text); }
void AudioEngine::setMasterSoloSlot (int slot)                       { impl->master.setSolo (slot); }
int AudioEngine::masterSoloSlot() const                              { return impl->master.solo(); }
float AudioEngine::takeMasterSlotPeak (int slot)                     { return impl->master.takePeak (slot); }
void AudioEngine::setMasterSlotParameter (int slot, int index, float v)            { impl->master.setParameter (slot, index, v); }
bool AudioEngine::canShowPluginWindow (int slot) const               { return impl->master.canShowWindow (slot); }
bool AudioEngine::pluginWindowsLost() const                          { return x11::connectionLost(); }
void AudioEngine::showPluginWindow (int slot)                        { impl->master.showWindow (slot); }
void AudioEngine::setPluginFrameColours (const PluginFrameColours& c) { StockFrame::setColours (c); }
MasterSlotInfo AudioEngine::chainDevice (const ChainPath& p) const                      { return impl->master.info (p); }
std::string AudioEngine::chainDeviceState (const ChainPath& p) const                    { return impl->master.state (p); }
std::vector<PluginParameterInfo> AudioEngine::chainParameters (const ChainPath& p) const { return impl->master.parameters (p); }
PluginParameterInfo AudioEngine::chainParameter (const ChainPath& p, int i) const       { return impl->master.parameter (p, i); }
DeviceDisplay AudioEngine::chainDisplay (const ChainPath& p) const                       { return impl->master.display (p); }
void AudioEngine::setChainParameter (const ChainPath& p, int i, float v)                 { impl->master.setParameter (p, i, v); }
bool AudioEngine::setChainParameterText (const ChainPath& p, int i, const std::string& t) { return impl->master.setParameterFromText (p, i, t); }
void AudioEngine::setChainBypassed (const ChainPath& p, bool b)                          { impl->master.setBypassed (p, b); }
void AudioEngine::setChainMix (const ChainPath& p, float mix)                           { if (p.topLevel()) impl->master.setMix (p.slot, mix); }
std::string AudioEngine::reloadChainDevice (const ChainPath& p)                          { return impl->master.reload (p); }
void AudioEngine::removeChainDevice (const ChainPath& p)                                 { impl->master.remove (p); }
bool AudioEngine::canShowChainWindow (const ChainPath& p) const                          { return impl->master.canShowWindow (p); }
void AudioEngine::showChainWindow (const ChainPath& p)                                   { impl->master.showWindow (p); }
float AudioEngine::takeChainPeak (const ChainPath& p)                                    { return impl->master.takePeak (p); }

PrismInfo AudioEngine::prismInfo (const ChainPath& p) const { return impl->master.prismInfo (p); }
std::string AudioEngine::prismAddDevice (const ChainPath& p, int ray, int index, const std::string& id) { return impl->master.prismAdd (p, ray, index, id); }
float AudioEngine::prismTakeRayPeak (const ChainPath& p, int ray)  { return impl->master.takeRayPeak (p, ray); }
std::string AudioEngine::groupMasterSlots (int first, int last)     { return impl->master.group (first, last); }
std::string AudioEngine::ungroupMasterSlot (int slot)               { return impl->master.ungroup (slot); }
bool AudioEngine::midiController (int channel, int number, int value) { return impl->master.controller (channel, number, value); }
void AudioEngine::prismLearnController (const ChainPath& p, int macro)  { impl->master.learnController (p, macro); }
bool AudioEngine::prismLearning (const ChainPath& p, int macro) const   { return impl->master.learning (p, macro); }

// The rest act on the Prism at the path, when there is one.
#define PHI_WITH_PRISM(path, body) if (auto* prism = impl->master.prismAt (path)) { body; }
void AudioEngine::prismMoveDevice (const ChainPath& p, int fr, int f, int tr, int t) { PHI_WITH_PRISM (p, prism->moveDevice (fr, f, tr, t)) }
int AudioEngine::prismAddRay (const ChainPath& p)                   { PHI_WITH_PRISM (p, return prism->addRay()) return -1; }
void AudioEngine::prismRemoveRay (const ChainPath& p, int ray)      { PHI_WITH_PRISM (p, prism->removeRay (ray)) }
void AudioEngine::prismSetRayGain (const ChainPath& p, int ray, double db) { PHI_WITH_PRISM (p, prism->setRayGain (ray, db)) }
void AudioEngine::prismSetRayMuted (const ChainPath& p, int ray, bool b)   { PHI_WITH_PRISM (p, prism->setRayMuted (ray, b)) }
void AudioEngine::prismSetRaySoloed (const ChainPath& p, int ray, bool b)  { PHI_WITH_PRISM (p, prism->setRaySoloed (ray, b)) }
void AudioEngine::prismRenameRay (const ChainPath& p, int ray, const std::string& n) { PHI_WITH_PRISM (p, prism->renameRay (ray, n)) }
void AudioEngine::prismRename (const ChainPath& p, const std::string& n) { PHI_WITH_PRISM (p, prism->rename (juce::String (n).trim().isEmpty() ? juce::String ("Prism") : juce::String (n).trim())) }
void AudioEngine::prismSetSplit (const ChainPath& p, int split)
{
    PHI_WITH_PRISM (p, prism->setSplit ((PrismInfo::Split) std::clamp (split, 0, 3)))
}
std::string AudioEngine::prismGroup (const ChainPath& p, int ray, int first, int last) { PHI_WITH_PRISM (p, return prism->group (ray, first, last)) return "There is no Prism here."; }
std::string AudioEngine::prismUngroup (const ChainPath& device)
{
    if (device.topLevel())
        return ungroupMasterSlot (device.slot);
    const auto [ray, index] = device.steps.back();
    PHI_WITH_PRISM (device.outer(), return prism->ungroup (ray, index))
    return "There is no Prism here.";
}
void AudioEngine::prismRenameMacro (const ChainPath& p, int m, const std::string& n) { PHI_WITH_PRISM (p, prism->renameMacro (m, n)) }
std::string AudioEngine::prismMap (const ChainPath& p, int m, int ray, int device, int parameter, float low, float high)
{
    PHI_WITH_PRISM (p, return prism->map (m, ray, device, parameter, low, high))
    return "There is no Prism here.";
}
void AudioEngine::prismUnmap (const ChainPath& p, int m, int mapping) { PHI_WITH_PRISM (p, prism->unmap (m, mapping)) }
void AudioEngine::prismSetMappingRange (const ChainPath& p, int m, int mapping, float low, float high)
{
    PHI_WITH_PRISM (p, prism->setMappingRange (m, mapping, low, high); prism->applyMacrosNow())
}
void AudioEngine::prismForgetController (const ChainPath& p, int m) { PHI_WITH_PRISM (p, prism->setController (m, -1, 0)) }
#undef PHI_WITH_PRISM

void AudioEngine::saveMasterSection() const                          { impl->master.save(); }
std::string AudioEngine::restoreMasterSection()                      { return impl->master.restore(); }

AudioDataPtr AudioEngine::renderThroughMaster (const AudioData& data, Frame start, Frame end, std::string& error,
                                               std::atomic<float>* progress, const std::atomic<bool>* cancel, bool withDither)
{
    return impl->master.render (data, start, end, error, progress, cancel, withDither);
}

std::vector<std::string> AudioEngine::readableExtensions()
{
    std::vector<std::string> result;
    {
        std::lock_guard lock (formatsMutex);
        for (auto& ext : juce::StringArray::fromTokens (formats().getWildcardForAllFormats(), ";", {}))
            result.push_back (ext.trim().fromFirstOccurrenceOf ("*.", false, false).toLowerCase().toStdString());
    }
    for (const auto& ext : codec::readableExtensions())
        if (std::find (result.begin(), result.end(), ext) == result.end())
            result.push_back (ext);
    return result;
}

std::vector<std::string> AudioEngine::writableExtensions()
{
    std::vector<std::string> result { "wav", "flac", "aiff", "ogg" };
    // Save As also offers the common compressed formats; Export has every one.
    for (const auto* id : { "mp3", "aac-m4a", "opus", "wavpack" })
        if (const auto* f = codec::find (id))
            result.push_back (f->extension);
    return result;
}

const codec::Format* AudioEngine::codecFormatFor (const std::string& extension)
{
    // Lossless WAV, AIFF, FLAC and Ogg Vorbis are written by JUCE here.
    if (extension == "wav" || extension == "aiff" || extension == "aif" || extension == "flac" || extension == "ogg")
        return nullptr;
    for (const auto* id : { "mp3", "aac-m4a", "opus", "wavpack", "alac", "tta", "mp2", "wma", "ac3", "eac3" })
        if (const auto* f = codec::find (id); f != nullptr && f->extension == extension)
            return f;
    for (const auto& f : codec::formats())
        if (f.extension == extension && ! f.native)
            return &f;
    return nullptr;
}

//==============================================================================
namespace
{
    std::string extensionOf (const std::string& path)
    {
        return juce::File (juce::String::fromUTF8 (path.c_str())).getFileExtension().toLowerCase().fromFirstOccurrenceOf (".", false, false).toStdString();
    }

    bool isRiffOrAiff (const std::string& ext) { return ext == "wav" || ext == "aiff" || ext == "aif" || ext == "aifc" || ext == "bwf"; }

    // The schemas each file type can hold.
    std::vector<meta::Schema> schemasFor (const std::string& ext)
    {
        using S = meta::Schema;
        if (ext == "wav" || ext == "bwf")
            return { S::riff, S::bext, S::cart, S::ixml, S::ebucore, S::xmp, S::id3 };
        if (ext == "aiff" || ext == "aif" || ext == "aifc")
            return { S::id3, S::xmp };
        if (ext == "mp3" || ext == "mp2" || ext == "tta")
            return { S::id3, S::xmp };
        if (ext == "flac" || ext == "ogg" || ext == "opus" || ext == "spx" || ext == "oga")
            return { S::vorbis, S::xmp };
        if (ext == "m4a" || ext == "mp4" || ext == "m4b" || ext == "alac")
            return { S::mp4, S::xmp };
        if (ext == "wv" || ext == "ape" || ext == "mpc")
            return { S::ape, S::xmp };
        return { S::vorbis };   // TagLib's common properties
    }
}

bool AudioEngine::keepsTag (const std::string& extension, const std::string& key)
{
    const auto schemas = schemasFor (extension);
    const auto schema = meta::schemaOfKey (key);
    if (key.compare (0, 8, "picture.") == 0)
        return true;   // ID3, Vorbis, MP4 or APE pictures
    if (schema != meta::Schema::common)
        return std::find (schemas.begin(), schemas.end(), schema) != schemas.end();
    for (const auto& f : meta::fields())
        if (f.common == key && std::find (schemas.begin(), schemas.end(), f.schema) != schemas.end())
            return true;
    return false;
}

std::vector<meta::Schema> AudioEngine::schemasOf (const std::string& extension) { return schemasFor (extension); }

AudioDataPtr AudioEngine::loadFile (const std::string& path, std::vector<Marker>& markersOut, Tags& tagsOut,
                                    std::string& error, std::atomic<float>* progress, std::string* layoutOut)
{
    const auto ext = extensionOf (path);
    std::unique_ptr<juce::AudioFormatReader> reader;
    {
        std::lock_guard lock (formatsMutex);
        reader.reset (formats().createReaderFor (juce::File (juce::String::fromUTF8 (path.c_str()))));
    }

    // Everything JUCE does not read goes through FFmpeg, with TagLib for the tags.
    if (reader == nullptr)
    {
        uint64_t mask = 0;
        auto data = codec::decode (path, error, progress, nullptr, &markersOut, &mask);
        if (data == nullptr)
        {
            if (error.empty())
                error = "Unsupported or unreadable file: " + path;
            return nullptr;
        }
        if (layoutOut != nullptr)
            *layoutOut = dsp::layoutForWavMask (mask, data->numChannels());
        tagio::read (path, tagsOut);
        return data;
    }

    const auto numChannels = (int) reader->numChannels;
    AudioData::Builder builder (numChannels, reader->sampleRate);
    juce::AudioBuffer<float> buffer (numChannels, AudioChunk::maxFrames);

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += AudioChunk::maxFrames)
    {
        const auto n = (int) std::min<juce::int64> (AudioChunk::maxFrames, reader->lengthInSamples - pos);
        reader->read (&buffer, 0, n, pos, true, true);
        builder.append (buffer.getArrayOfReadPointers(), n);

        if (progress != nullptr)
            progress->store ((float) (pos + n) / (float) std::max<juce::int64> (1, reader->lengthInSamples));
    }

    // WAV cue points and labels (and regions from ltxt chunks) become markers.
    const auto& meta = reader->metadataValues;
    if (layoutOut != nullptr)
        *layoutOut = dsp::layoutForWavMask ((uint64_t) (uint32_t) meta.getValue ("ChannelMask", "0").getIntValue(), numChannels);
    std::map<int, std::string> labels;
    std::map<int, Frame> regionLengths;

    for (int i = 0; i < meta.getValue ("NumCueLabels", "0").getIntValue(); ++i)
        labels[meta.getValue ("CueLabel" + juce::String (i) + "Identifier", "0").getIntValue()]
            = meta.getValue ("CueLabel" + juce::String (i) + "Text", {}).toStdString();

    for (int i = 0; i < meta.getValue ("NumCueRegions", "0").getIntValue(); ++i)
        regionLengths[meta.getValue ("CueRegion" + juce::String (i) + "Identifier", "0").getIntValue()]
            = meta.getValue ("CueRegion" + juce::String (i) + "SampleLength", "0").getLargeIntValue();

    for (int i = 0; i < meta.getValue ("NumCuePoints", "0").getIntValue(); ++i)
    {
        const auto prefix = "Cue" + juce::String (i);
        const auto id = meta.getValue (prefix + "Identifier", "0").getIntValue();

        Marker m;
        m.id = id;
        m.start = meta.getValue (prefix + "Offset", "0").getLargeIntValue();
        m.end = m.start;
        m.name = labels.count (id) ? labels[id] : ("Marker " + std::to_string (id));

        if (auto it = regionLengths.find (id); it != regionLengths.end() && it->second > 0)
        {
            m.type = Marker::Type::region;
            m.end = m.start + it->second;
        }

        markersOut.push_back (m);
    }

    // Tags: WAV and AIFF chunks read here (ID3 chunk through TagLib), the rest through TagLib.
    if (isRiffOrAiff (ext))
    {
        const auto id3 = riff::readMetadata (path, tagsOut);
        if (! id3.empty())
            tagio::readId3 (id3, tagsOut);
        // WAV keeps a tempo in its acid chunk.
        if (const auto tempo = meta.getValue (juce::WavAudioFormat::acidTempo, {}).getDoubleValue(); tempo > 0.0 && tagsOut[tag::bpm].empty())
            tagsOut[tag::bpm] = juce::String (tempo, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".").toStdString();
        if (tagsOut[tag::bpm].empty())
            tagsOut.erase (tag::bpm);
    }
    else
        tagio::read (path, tagsOut);

    return builder.finish();
}

bool AudioEngine::writeTags (const std::string& path, const std::vector<Marker>& markers, double sampleRate, const Tags& tags,
                             const tagio::Options& options, std::string& error)
{
    const auto ext = extensionOf (path);
    if (isRiffOrAiff (ext))
    {
        riff::Write w;
        w.riff = options.riff;
        w.bext = options.bext;
        w.cart = options.cart;
        w.ixml = options.ixml;
        w.ebucore = options.ebucore;
        w.xmp = options.xmp;
        w.software = options.software;
        if (options.id3)
            w.id3 = tagio::renderId3 (tags, options);
        // AIFF's own text chunks follow the RIFF switch.
        return riff::writeMetadata (path, tags, w, error);
    }
    return tagio::write (path, tags, markers, sampleRate, options, error);
}

bool AudioEngine::saveFile (const std::string& path, const AudioData& data, const std::vector<Marker>& markers, const Tags& tags,
                            const SaveOptions& options, std::string& error)
{
    const juce::File file (juce::String::fromUTF8 (path.c_str()));
    const auto ext = file.getFileExtension().toLowerCase();
    auto metadata = options.metadata;
    if (metadata.software.empty())
        metadata.software = APP_DISPLAY_NAME;

    // Write to a temporary file first so a failed save never destroys the original.
    // It keeps the extension, so the tag writers know the type.
    const auto temp = file.getSiblingFile ("." + file.getFileNameWithoutExtension() + ".phi-tmp" + ext);
    temp.deleteFile();
    const auto tempPath = temp.getFullPathName().toStdString();

    // Compressed formats through FFmpeg.
    if (const auto* f = codecFormatFor (ext.substring (1).toStdString()))
    {
        const auto rate = codec::rateFor (*f, data.sampleRate());
        auto audio = std::lround (data.sampleRate()) != rate ? dsp::resample (data, rate) : data.slice (0, data.length());
        if (audio->numChannels() > f->maxChannels && f->maxChannels == 1)
            audio = dsp::toMono (*audio);
        codec::Job job;
        job.format = f;
        job.settings = options.codecSettings.empty() ? codec::defaults (*f) : options.codecSettings;
        job.bits = 16;
        for (const auto d : f->bitDepths)
            if (d <= options.bitDepth)
                job.bits = d;
        if (! f->bitDepths.empty() && job.bits <= 24)
            audio = dsp::quantize (*audio, job.bits, options.dither ? dsp::Dither::tpdf : dsp::Dither::none);
        job.chapters = metadata.chapters ? markers : std::vector<Marker>();
        job.tags = tags;
        if (! codec::encode (*audio, 0, audio->length(), tempPath, job, error))
        {
            temp.deleteFile();
            return false;
        }
        if (f->taggedByTagLib && ! tagio::write (tempPath, tags, job.chapters, rate, metadata, error))
        {
            temp.deleteFile();
            return false;
        }
        if (! temp.moveFileTo (file))
        {
            error = "Could not replace " + path;
            temp.deleteFile();
            return false;
        }
        return true;
    }

    juce::AudioFormat* format = nullptr;
    {
        std::lock_guard lock (formatsMutex);
        format = formats().findFormatForFileExtension (ext);
    }

    if (format == nullptr)
    {
        error = "Saving as " + ext.toStdString() + " is not supported. Use Export for other formats.";
        return false;
    }

    const bool isLossy = ext == ".ogg";
    const bool isFlac = ext == ".flac";
    const int bits = isLossy ? 16 : (isFlac ? std::min (options.bitDepth, 24) : options.bitDepth);
    const bool isFloat = bits == 32 && ! isFlac && ! isLossy;

    auto writerOptions = juce::AudioFormatWriterOptions{}
                             .withSampleRate (data.sampleRate())
                             .withNumChannels (data.numChannels())
                             .withBitsPerSample (bits)
                             .withSampleFormat (isFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                        : juce::AudioFormatWriterOptions::SampleFormat::automatic);

    // Surround files carry their speaker layout (the WAV channel mask).
    if (data.numChannels() > 2)
        writerOptions = writerOptions.withChannelLayout (MasterChain::channelSetFor (data.numChannels(), options.layout));

    if (isLossy)
        writerOptions = writerOptions.withQualityOptionIndex (std::clamp (options.oggQuality, 0, format->getQualityOptions().size() - 1));

    // JUCE writes the audio, markers and tempo; the tags are written afterwards (writeTags).
    std::unordered_map<juce::String, juce::String> meta;
    const auto bpm = tags.find (tag::bpm);
    if (ext == ".wav" && bpm != tags.end() && std::atof (bpm->second.c_str()) > 0.0)
    {
        // JUCE writes the acid chunk (where WAV keeps a tempo) only with a time signature in it.
        meta[juce::WavAudioFormat::acidTempo] = juce::String::fromUTF8 (bpm->second.c_str());
        meta.emplace (juce::WavAudioFormat::acidNumerator, juce::String (std::clamp (options.beatsPerBar, 1, 32)));
        meta.emplace (juce::WavAudioFormat::acidDenominator, "4");
    }
    if (ext == ".wav" && options.loopEnd > options.loopStart && options.loopStart >= 0)
    {
        meta["NumSampleLoops"] = "1";
        meta["MidiUnityNote"] = "60";
        meta["SamplePeriod"] = juce::String ((juce::int64) std::llround (1.0e9 / data.sampleRate()));
        meta["Loop0Identifier"] = "0";
        meta["Loop0Type"] = "0";   // forward
        meta["Loop0Start"] = juce::String (options.loopStart);
        meta["Loop0End"] = juce::String (options.loopEnd - 1);   // the last frame played, inclusive
        if (options.loopBeats > 0)
        {
            meta[juce::WavAudioFormat::acidStretch] = "1";   // a loop that follows the song's tempo, not a one-shot
            meta[juce::WavAudioFormat::acidBeats] = juce::String (options.loopBeats);
        }
    }

    if (ext == ".wav" && ! markers.empty())
    {
        int labelIndex = 0, regionIndex = 0;

        for (size_t i = 0; i < markers.size(); ++i)
        {
            const auto& m = markers[i];
            const auto id = juce::String ((int) i + 1);
            const auto cue = "Cue" + juce::String ((int) i);
            meta[cue + "Identifier"] = id;
            meta[cue + "Offset"] = juce::String (m.start);
            meta[cue + "ChunkID"] = juce::String (juce::ByteOrder::littleEndianInt ("data"));

            const auto label = "CueLabel" + juce::String (labelIndex++);
            meta[label + "Identifier"] = id;
            meta[label + "Text"] = juce::String::fromUTF8 (m.name.c_str());

            if (m.isRegion())
            {
                const auto region = "CueRegion" + juce::String (regionIndex++);
                meta[region + "Identifier"] = id;
                meta[region + "SampleLength"] = juce::String (m.end - m.start);
                meta[region + "Purpose"] = juce::String (juce::ByteOrder::littleEndianInt ("rgn "));
                meta[region + "Text"] = juce::String::fromUTF8 (m.name.c_str());
            }
        }

        meta["NumCuePoints"] = juce::String ((int) markers.size());
        meta["NumCueLabels"] = juce::String (labelIndex);
        if (regionIndex > 0)
            meta["NumCueRegions"] = juce::String (regionIndex);
    }
    if (! meta.empty())
        writerOptions = writerOptions.withMetadataValues (meta);

    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp);
        if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        {
            error = "Cannot write to " + file.getParentDirectory().getFullPathName().toStdString();
            return false;
        }

        auto writer = format->createWriterFor (stream, writerOptions);
        if (writer == nullptr)
        {
            error = "This format does not support " + std::to_string (bits) + "-bit, "
                    + std::to_string (data.numChannels()) + " channels at " + std::to_string ((int) data.sampleRate()) + " Hz.";
            temp.deleteFile();
            return false;
        }

        std::mt19937 rng (12345);
        bool ok = true;

        data.processed (0, data.length(), [&] (std::vector<std::vector<float>>& block, Frame)
        {
            if (! ok)
                return;

            // Audio the Master Section's final dither already rounded to this depth (or
            // coarser) is on the grid; dithering it again would only add noise.
            auto onGrid = [&]
            {
                const auto steps = (float) (1 << (bits - 1));
                for (const auto& ch : block)
                    for (const auto v : ch)
                        if (v * steps != std::round (v * steps))
                            return false;
                return true;
            };
            if (options.dither && bits < 32 && ! isLossy && ! onGrid())
                dsp::addTpdfDither (block, bits, rng);

            std::vector<const float*> ptrs;
            for (auto& ch : block)
                ptrs.push_back (ch.data());

            ok = writer->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) block[0].size());
        });

        if (! ok)
        {
            error = "Writing failed (disk full?)";
            writer.reset();
            temp.deleteFile();
            return false;
        }
    }

    if (! writeTags (tempPath, markers, data.sampleRate(), tags, metadata, error))
    {
        temp.deleteFile();
        return false;
    }

    if (! temp.moveFileTo (file))
    {
        error = "Could not replace " + path;
        temp.deleteFile();
        return false;
    }

    return true;
}
