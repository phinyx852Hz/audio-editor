// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Audio engine built on JUCE. This header deliberately contains no JUCE or Qt
// types so the UI layer stays independent of the engine's implementation.

struct WaveformPeaks
{
    int numChannels = 0;
    int samplesPerPeak = 64;
    double sampleRate = 0.0;
    long long lengthInSamples = 0;
    std::vector<std::vector<float>> minimums, maximums; // [channel][peakIndex]

    double lengthSeconds() const { return sampleRate > 0.0 ? (double) lengthInSamples / sampleRate : 0.0; }
};

struct AudioOutputChoice
{
    std::string type, name;
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    // Opens the default output device. Returns an error message, or empty on success.
    std::string openDefaultDevice();

    bool load (const std::string& path, std::string& error);
    bool hasAudio() const;

    void play();
    void stop();
    bool isPlaying() const;

    double position() const;
    void setPosition (double seconds);
    double length() const;

    std::string deviceDescription() const;
    std::vector<AudioOutputChoice> availableOutputs();
    AudioOutputChoice currentOutput() const;
    std::string selectOutput (const AudioOutputChoice&);

    // Runs pending JUCE messages; call regularly from the UI thread.
    void dispatchMessages();

    static std::vector<std::string> supportedExtensions();

    // Reads the file and computes min/max peaks. Safe to call from a worker thread.
    static bool computePeaks (const std::string& path, WaveformPeaks& out, const std::atomic<bool>& cancel);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
