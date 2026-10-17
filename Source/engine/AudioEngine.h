// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../core/AudioDocument.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Audio engine built on JUCE. This header contains no JUCE or Qt types so the
// UI layer stays independent of the engine's implementation.

struct AudioOutputChoice
{
    std::string type, name;
};

// Level data for one ~100 ms slice of what was played, for the meters.
struct MeterBlock
{
    static constexpr int maxChannels = 8;
    int numChannels = 0;
    float samplePeak[maxChannels] {};
    float truePeak[maxChannels] {};
    double loudnessPower = 0.0;  // channel-weighted K-weighted mean square (BS.1770)
};

struct SaveOptions
{
    int bitDepth = 24;      // 16, 24 or 32 (32 = float for WAV/AIFF)
    bool dither = true;     // TPDF dither when reducing below 32-bit float
    int oggQuality = 6;     // 0..10 for Ogg Vorbis
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    // Opens the default output device. Returns an error message, or empty on success.
    std::string openDefaultDevice();

    // Replaces what is played. Safe to call while playing; position is kept.
    void setData (AudioDataPtr);
    bool hasAudio() const;

    void play();
    void stop();
    bool isPlaying() const;

    Frame positionFrames() const;
    void setPositionFrames (Frame);
    double position() const;  // seconds
    double length() const;    // seconds

    std::string deviceDescription() const;
    std::vector<AudioOutputChoice> availableOutputs();
    AudioOutputChoice currentOutput() const;
    std::string selectOutput (const AudioOutputChoice&);

    // Meter data produced since the last call.
    std::vector<MeterBlock> takeMeterBlocks();

    // Runs pending JUCE messages; call regularly from the UI thread.
    void dispatchMessages();

    // Lower-case file extensions without the dot, e.g. "wav".
    static std::vector<std::string> readableExtensions();
    static std::vector<std::string> writableExtensions();

    // File I/O, safe to call from worker threads.
    static AudioDataPtr loadFile (const std::string& path, std::vector<Marker>& markersOut, std::string& error,
                                  std::atomic<float>* progress = nullptr);
    static bool saveFile (const std::string& path, const AudioData&, const std::vector<Marker>&,
                          const SaveOptions&, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
