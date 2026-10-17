// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioEngine.h"
#include "../core/Dsp.h"

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

            const auto pos = position.load();
            const auto numChannels = std::min (d->numChannels(), (int) scratchPtrs.size());
            ensureScratch (d->numChannels(), info.numSamples);
            d->read (pos, info.numSamples, scratchPtrs.data());

            for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
            {
                // Mono files play on every output channel.
                const auto src = numChannels > 0 ? ch % d->numChannels() : 0;
                info.buffer->copyFrom (ch, info.startSample, scratch[(size_t) src].data(), info.numSamples);
            }

            position = pos + info.numSamples;
        }

        void setNextReadPosition (juce::int64 p) override { position = p; }
        juce::int64 getNextReadPosition() const override  { return position.load(); }

        juce::int64 getTotalLength() const override
        {
            const auto d = getData();
            return d != nullptr ? d->length() : 0;
        }

        bool isLooping() const override { return false; }

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

        AudioDataPtr data;
        std::atomic<juce::int64> position { 0 };
        std::vector<std::vector<float>> scratch { 2, std::vector<float> (8192) };
        std::vector<float*> scratchPtrs { scratch[0].data(), scratch[1].data() };
    };

    // Sits after the transport and measures what is actually sent to the outputs.
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

            const auto numChannels = std::min (info.buffer->getNumChannels(), MeterBlock::maxChannels);
            current.numChannels = numChannels;

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

        void takeBlocks (std::vector<MeterBlock>& out)
        {
            const auto scope = fifo.read (fifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) out.push_back (blocks[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i) out.push_back (blocks[(size_t) (scope.startIndex2 + i)]);
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

        static constexpr int fifoSize = 256;
        juce::AbstractFifo fifo { fifoSize };
        std::array<MeterBlock, fifoSize> blocks;
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
        player.setSource (&meter);
    }

    ~Impl()
    {
        deviceManager.removeAudioCallback (&player);
        transport.setSource (nullptr);
        player.setSource (nullptr);
        deviceManager.closeAudioDevice();
    }

    juce::AudioDeviceManager deviceManager;
    DocumentSource document;
    juce::AudioTransportSource transport;
    MeterSource meter { transport };
    juce::AudioSourcePlayer player;
    double sourceRate = 0.0;
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
    return error.toStdString();
}

void AudioEngine::setData (AudioDataPtr data)
{
    const auto rate = data != nullptr ? data->sampleRate() : 0.0;
    const auto pos = impl->document.getNextReadPosition();
    impl->document.setData (std::move (data));

    if (std::abs (rate - impl->sourceRate) > 1.0e-6)
    {
        // A new sample rate needs the transport's resampler rebuilt.
        const bool wasPlaying = isPlaying();
        impl->transport.setSource (nullptr);
        if (rate > 0.0)
            impl->transport.setSource (&impl->document, 0, nullptr, rate);

        impl->sourceRate = rate;
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

    impl->transport.start();
}

void AudioEngine::stop()            { impl->transport.stop(); }
bool AudioEngine::isPlaying() const { return impl->transport.isPlaying(); }

Frame AudioEngine::positionFrames() const { return impl->document.getNextReadPosition(); }

void AudioEngine::setPositionFrames (Frame f)
{
    impl->document.setNextReadPosition (std::clamp<Frame> (f, 0, impl->document.getTotalLength()));
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
    return dm.setAudioDeviceSetup (setup, true).toStdString();
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
AudioDataPtr AudioEngine::loadFile (const std::string& path, std::vector<Marker>& markersOut,
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

    return builder.finish();
}

bool AudioEngine::saveFile (const std::string& path, const AudioData& data, const std::vector<Marker>& markers,
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

    if (isLossy)
        writerOptions = writerOptions.withQualityOptionIndex (std::clamp (options.oggQuality, 0, format->getQualityOptions().size() - 1));

    if (ext == ".wav" && ! markers.empty())
    {
        std::unordered_map<juce::String, juce::String> meta;
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

        writerOptions = writerOptions.withMetadataValues (meta);
    }

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

    if (! temp.moveFileTo (file))
    {
        error = "Could not replace " + path;
        temp.deleteFile();
        return false;
    }

    return true;
}
