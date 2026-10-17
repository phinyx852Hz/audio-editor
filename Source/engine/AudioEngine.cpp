// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

struct AudioEngine::Impl
{
    Impl()
    {
        // The thread that creates the MessageManager becomes JUCE's message thread.
        juce::MessageManager::getInstance();
        formatManager.registerBasicFormats();
        readAheadThread.startThread();
        player.setSource (&transport);
    }

    ~Impl()
    {
        deviceManager.removeAudioCallback (&player);
        transport.setSource (nullptr);
        player.setSource (nullptr);
        deviceManager.closeAudioDevice();
    }

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioSourcePlayer player;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::TimeSliceThread readAheadThread { "audio read-ahead" };
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

bool AudioEngine::load (const std::string& path, std::string& error)
{
    const juce::File file (juce::String::fromUTF8 (path.c_str()));
    std::unique_ptr<juce::AudioFormatReader> reader (impl->formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Unsupported or unreadable file: " + path;
        return false;
    }

    impl->transport.stop();
    impl->transport.setSource (nullptr);

    const auto sampleRate = reader->sampleRate;
    impl->readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    impl->transport.setSource (impl->readerSource.get(), 32768, &impl->readAheadThread, sampleRate);
    return true;
}

bool AudioEngine::hasAudio() const { return impl->readerSource != nullptr; }

void AudioEngine::play()
{
    if (! hasAudio())
        return;

    if (position() >= length())
        setPosition (0.0);

    impl->transport.start();
}

void AudioEngine::stop()                    { impl->transport.stop(); }
bool AudioEngine::isPlaying() const         { return impl->transport.isPlaying(); }
double AudioEngine::position() const        { return impl->transport.getCurrentPosition(); }
double AudioEngine::length() const          { return impl->transport.getLengthInSeconds(); }

void AudioEngine::setPosition (double seconds)
{
    impl->transport.setPosition (juce::jlimit (0.0, juce::jmax (0.0, length()), seconds));
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

void AudioEngine::dispatchMessages()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (0);
}

std::vector<std::string> AudioEngine::supportedExtensions()
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::vector<std::string> result;
    for (auto& ext : juce::StringArray::fromTokens (fm.getWildcardForAllFormats(), ";", {}))
        result.push_back (ext.trim().toStdString());

    return result;
}

bool AudioEngine::computePeaks (const std::string& path, WaveformPeaks& out, const std::atomic<bool>& cancel)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (juce::String::fromUTF8 (path.c_str()))));

    if (reader == nullptr)
        return false;

    out.numChannels = (int) reader->numChannels;
    out.sampleRate = reader->sampleRate;
    out.lengthInSamples = reader->lengthInSamples;

    const auto numPeaks = (size_t) ((reader->lengthInSamples + out.samplesPerPeak - 1) / out.samplesPerPeak);
    out.minimums.assign ((size_t) out.numChannels, std::vector<float> (numPeaks, 0.0f));
    out.maximums.assign ((size_t) out.numChannels, std::vector<float> (numPeaks, 0.0f));

    constexpr int blockSize = 64 * 1024;
    juce::AudioBuffer<float> buffer (out.numChannels, blockSize);

    for (juce::int64 start = 0; start < reader->lengthInSamples; start += blockSize)
    {
        if (cancel.load())
            return false;

        const auto num = (int) juce::jmin ((juce::int64) blockSize, reader->lengthInSamples - start);
        reader->read (&buffer, 0, num, start, true, true);

        for (int ch = 0; ch < out.numChannels; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);

            for (int i = 0; i < num; i += out.samplesPerPeak)
            {
                const auto count = juce::jmin (out.samplesPerPeak, num - i);
                const auto range = juce::FloatVectorOperations::findMinAndMax (data + i, count);
                const auto index = (size_t) ((start + i) / out.samplesPerPeak);
                out.minimums[(size_t) ch][index] = range.getStart();
                out.maximums[(size_t) ch][index] = range.getEnd();
            }
        }
    }

    return true;
}
