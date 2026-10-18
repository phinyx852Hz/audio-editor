// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioEngine.h"
#include "../core/Dsp.h"
#include "../core/FlacTags.h"
#include "MasterChain.h"
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
                pos += n;
                done += n;
                if (to > from && pos >= to)
                    pos = from;
            }
            for (size_t ch = 0; ch < scratch.size(); ++ch)
                scratchPtrs[ch] = scratch[ch].data();

            // The buffer has the file's channels (see MonitorSource); a mono file also fills a second one.
            for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
            {
                const auto src = ch < numChannels ? ch : (numChannels == 1 ? 0 : -1);
                if (src >= 0)
                    info.buffer->copyFrom (ch, info.startSample, scratch[(size_t) src].data(), info.numSamples);
            }

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
        std::vector<std::vector<float>> scratch { 2, std::vector<float> (8192) };
        std::vector<float*> scratchPtrs { scratch[0].data(), scratch[1].data() };
    };

    // Sits after the transport and measures what is actually sent to the outputs.
    // Runs the Master Section on whatever is played.
    class MasterSource final : public juce::AudioSource
    {
    public:
        MasterSource (juce::AudioSource& in, MasterChain& c) : input (in), chain (c) {}

        void prepareToPlay (int blockSize, double sampleRate) override
        {
            input.prepareToPlay (blockSize, sampleRate);
            chain.prepare (sampleRate, blockSize);
        }

        void releaseResources() override { input.releaseResources(); }

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
        {
            input.getNextAudioBlock (info);
            chain.processRealtime (*info.buffer, info.startSample, info.numSamples);
            clock.fetch_add (info.numSamples);
        }

        std::atomic<juce::int64> clock { 0 };   // device samples processed so far

    private:
        juce::AudioSource& input;
        MasterChain& chain;
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

        static constexpr int fifoSize = 256;
        juce::AbstractFifo fifo { fifoSize };
        std::array<MeterBlock, fifoSize> blocks;
    };

    // The last stage before the device. Everything before it (transport, Master
    // Section, meters) works on the file's own channels; this sends them to the
    // outputs, one each when there are enough, or folded down to stereo.
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

                    for (int side = 0; side < std::min (outs, 2); ++side)
                        for (int src = 0; src < n; ++src)
                            if (const auto gain = downmix[(size_t) n][(size_t) side][(size_t) src]; gain > 0.0f)
                                info.buffer->addFrom (side, at, file, src, 0, count, outs == 1 ? gain * 0.5f : gain);

                    if (outs == 1)
                        for (int src = 0; src < n; ++src)
                            if (const auto gain = downmix[(size_t) n][1][(size_t) src]; gain > 0.0f)
                                info.buffer->addFrom (0, at, file, src, 0, count, gain * 0.5f);
                }

                done += count;
            }
        }

        std::atomic<int> channels { 2 };
        std::atomic<bool> separate { false }, alsaOrder { false }, downmixing { false };

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
        player.setSource (&monitor);
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
    MasterSource masterSource { transport, master };
    MeterSource meter { masterSource };
    MonitorSource monitor { meter };
    juce::AudioSourcePlayer player;
    double sourceRate = 0.0;
    int sourceChannels = 0;
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
            impl->transport.setSource (&impl->document, 0, nullptr, rate, std::max (channels, 2));

        impl->sourceRate = rate;
        impl->sourceChannels = channels > 0 ? channels : impl->sourceChannels;
        impl->document.setNextReadPosition (std::min<Frame> (pos, impl->document.getTotalLength()));
        if (wasPlaying)
            impl->transport.start();
    }
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

void AudioEngine::stop()            { impl->transport.stop(); }
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
    if (! impl->monitor.separate.load())
        return;

    const auto available = device->getOutputChannelNames().size();
    const auto wanted = std::clamp (channels, 2, std::max (2, available));
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

std::vector<PluginInfo> AudioEngine::knownPlugins() const            { return impl->master.knownPlugins(); }
std::vector<std::string> AudioEngine::pluginSearchFolders() const    { return impl->master.searchFolders(); }
int AudioEngine::startPluginScan()                                   { return impl->master.startScan(); }
bool AudioEngine::scanNextPlugin (std::string& file)                 { return impl->master.scanNext (file); }
std::string AudioEngine::finishPluginScan()                          { return impl->master.finishScan(); }

std::string AudioEngine::loadMasterPlugin (int slot, const std::string& id) { return impl->master.load (slot, id); }
void AudioEngine::clearMasterSlot (int slot)                         { impl->master.clear (slot); }
void AudioEngine::moveMasterSlot (int from, int to)                  { impl->master.move (from, to); }
void AudioEngine::setMasterSlotBypassed (int slot, bool b)           { impl->master.setBypassed (slot, b); }
int AudioEngine::masterChannels() const                              { return impl->master.channels(); }
MasterSlotInfo AudioEngine::masterSlot (int slot) const              { return impl->master.info (slot); }
void AudioEngine::setMasterGainDb (double db)                        { impl->master.setGainDb (db); }
double AudioEngine::masterGainDb() const                             { return impl->master.gainDb(); }
void AudioEngine::setMasterBypassed (bool b)                         { impl->master.setAllBypassed (b); }
bool AudioEngine::isMasterBypassed() const                           { return impl->master.isAllBypassed(); }
bool AudioEngine::masterIsActive() const                             { return impl->master.isActive(); }

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
void AudioEngine::saveMasterSection() const                          { impl->master.save(); }
std::string AudioEngine::restoreMasterSection()                      { return impl->master.restore(); }

AudioDataPtr AudioEngine::renderThroughMaster (const AudioData& data, Frame start, Frame end, std::string& error,
                                               std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    return impl->master.render (data, start, end, error, progress, cancel);
}

std::vector<std::string> AudioEngine::readableExtensions()
{
    std::lock_guard lock (formatsMutex);
    std::vector<std::string> result;
    for (auto& ext : juce::StringArray::fromTokens (formats().getWildcardForAllFormats(), ";", {}))
        result.push_back (ext.trim().fromFirstOccurrenceOf ("*.", false, false).toLowerCase().toStdString());

    return result;
}

std::vector<std::string> AudioEngine::writableExtensions()
{
    return { "wav", "flac", "aiff", "ogg" };
}

//==============================================================================
namespace
{
    // How each tag is called in WAV LIST/INFO and bext (as JUCE names them), and in Vorbis comments.
    struct TagName
    {
        const char* key;
        const char* wav;      // null: not kept in WAV
        const char* vorbis;   // null: not kept in FLAC or Ogg
    };

    const TagName tagNames[] {
        { tag::title,            "INAM", "TITLE" },
        { tag::artist,           "IART", "ARTIST" },
        { tag::album,            "IPRD", "ALBUM" },
        { tag::genre,            "GENR", "GENRE" },
        { tag::date,             "ICRD", "DATE" },
        { tag::track,            "TRCK", "TRACKNUMBER" },
        { tag::comment,          "ICMT", "COMMENT" },
        { tag::copyright,        "ICOP", "COPYRIGHT" },
        { tag::isrc,             "ISRC", "ISRC" },
        { tag::bwfDescription,   "bwav description", nullptr },
        { tag::bwfOriginator,    "bwav originator", nullptr },
        { tag::bwfOriginatorRef, "bwav originator ref", nullptr },
        { tag::bwfDate,          "bwav origination date", nullptr },
        { tag::bwfTime,          "bwav origination time", nullptr },
        { tag::bwfTimeReference, "bwav time reference", nullptr },
        { tag::bwfCodingHistory, "bwav coding history", nullptr },
    };

    // JUCE's Ogg Vorbis reader and writer know only these comments.
    const std::map<std::string, const char*> oggKeys {
        { "TITLE", "id3title" }, { "ARTIST", "id3artist" }, { "ALBUM", "id3album" }, { "COMMENT", "id3comment" },
        { "DATE", "id3date" }, { "GENRE", "id3genre" }, { "TRACKNUMBER", "id3trackNumber" },
    };
}

bool AudioEngine::keepsTag (const std::string& extension, const std::string& key)
{
    for (const auto& t : tagNames)
        if (key == t.key)
        {
            if (extension == "wav")
                return t.wav != nullptr;
            if (extension == "flac")
                return t.vorbis != nullptr;
            if (extension == "ogg")
                return t.vorbis != nullptr && oggKeys.count (t.vorbis) > 0;
            return false;
        }
    return false;
}

AudioDataPtr AudioEngine::loadFile (const std::string& path, std::vector<Marker>& markersOut, Tags& tagsOut,
                                    std::string& error, std::atomic<float>* progress)
{
    std::unique_ptr<juce::AudioFormatReader> reader;
    {
        std::lock_guard lock (formatsMutex);
        reader.reset (formats().createReaderFor (juce::File (juce::String::fromUTF8 (path.c_str()))));
    }

    if (reader == nullptr)
    {
        error = "Unsupported or unreadable file: " + path;
        return nullptr;
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

    // Tags: WAV and Ogg through JUCE's metadata, FLAC from its Vorbis comments.
    const auto ext = juce::File (juce::String::fromUTF8 (path.c_str())).getFileExtension().toLowerCase();
    const auto flacFields = ext == ".flac" ? flactags::read (path) : flactags::Fields();
    for (const auto& t : tagNames)
    {
        juce::String value;
        if (ext == ".wav" && t.wav != nullptr)
            value = meta.getValue (t.wav, {});
        else if (ext == ".ogg" && t.vorbis != nullptr && oggKeys.count (t.vorbis) > 0)
            value = meta.getValue (oggKeys.at (t.vorbis), {});
        else if (ext == ".flac" && t.vorbis != nullptr && flacFields.count (t.vorbis) > 0)
            value = juce::String::fromUTF8 (flacFields.at (t.vorbis).c_str());
        value = value.trim();
        if (value.isNotEmpty() && ! (t.key == std::string (tag::bwfTimeReference) && value == "0"))
            tagsOut[t.key] = value.toStdString();
    }

    return builder.finish();
}

bool AudioEngine::saveFile (const std::string& path, const AudioData& data, const std::vector<Marker>& markers, const Tags& tags,
                            const SaveOptions& options, std::string& error)
{
    const juce::File file (juce::String::fromUTF8 (path.c_str()));
    const auto ext = file.getFileExtension().toLowerCase();

    juce::AudioFormat* format = nullptr;
    {
        std::lock_guard lock (formatsMutex);
        format = formats().findFormatForFileExtension (ext);
    }

    if (format == nullptr || ext == ".mp3")
    {
        error = "Saving as " + ext.toStdString() + " is not supported yet. Use .wav, .flac, .aiff or .ogg.";
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
        writerOptions = writerOptions.withChannelLayout (MasterChain::channelSetFor (data.numChannels()));

    if (isLossy)
        writerOptions = writerOptions.withQualityOptionIndex (std::clamp (options.oggQuality, 0, format->getQualityOptions().size() - 1));

    std::unordered_map<juce::String, juce::String> meta;
    for (const auto& t : tagNames)
    {
        const auto it = tags.find (t.key);
        if (it == tags.end() || it->second.empty())
            continue;
        const auto value = juce::String::fromUTF8 (it->second.c_str());
        if (ext == ".wav" && t.wav != nullptr)
            meta[t.wav] = value;
        else if (ext == ".ogg" && t.vorbis != nullptr && oggKeys.count (t.vorbis) > 0)
            meta[oggKeys.at (t.vorbis)] = value;
    }
    if (ext == ".wav" && ! meta.empty())
        meta["ISFT"] = APP_DISPLAY_NAME;

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

    // Write to a temporary file first so a failed save never destroys the original.
    const auto temp = file.getSiblingFile ("." + file.getFileName() + ".phinyx-tmp");
    temp.deleteFile();

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

            if (options.dither && bits < 32 && ! isLossy)
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

    if (isFlac)
    {
        flactags::Fields fields;
        for (const auto& t : tagNames)
            if (const auto it = tags.find (t.key); t.vorbis != nullptr && it != tags.end() && ! it->second.empty())
                fields[t.vorbis] = it->second;
        if (! fields.empty() && ! flactags::write (temp.getFullPathName().toStdString(), fields, APP_DISPLAY_NAME, error))
        {
            temp.deleteFile();
            return false;
        }
    }

    if (! temp.moveFileTo (file))
    {
        error = "Could not replace " + path;
        temp.deleteFile();
        return false;
    }

    return true;
}
