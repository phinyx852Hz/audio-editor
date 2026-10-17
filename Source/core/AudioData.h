// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Immutable, chunked audio storage. Edits build a new AudioData that shares
// every untouched chunk with the old one, so undo snapshots are cheap and the
// audio thread can keep reading an old snapshot while the UI makes a new one.
// Plain C++: no JUCE or Qt.

using Frame = int64_t;

struct AudioChunk
{
    static constexpr int maxFrames = 1 << 16;
    static constexpr int framesPerPeak = 64;

    explicit AudioChunk (std::vector<std::vector<float>> channelSamples);

    int numFrames = 0;
    std::vector<std::vector<float>> samples;           // [channel][frame]
    std::vector<std::vector<float>> peakMin, peakMax;  // [channel][frame / framesPerPeak]
};

using ChunkPtr = std::shared_ptr<const AudioChunk>;

class AudioData;
using AudioDataPtr = std::shared_ptr<const AudioData>;

class AudioData
{
public:
    AudioData (int numChannels, double sampleRate);

    int numChannels() const     { return channels; }
    double sampleRate() const   { return rate; }
    Frame length() const        { return total; }
    double lengthSeconds() const { return rate > 0.0 ? (double) total / rate : 0.0; }

    // Copies frames into dest[channel]; frames outside the data are zero.
    void read (Frame start, int numFrames, float* const* dest) const;

    // Min/max of one channel over [start, end), using cached peaks for long ranges.
    void minMax (int channel, Frame start, Frame end, float& lo, float& hi) const;

    // Largest absolute sample value over [start, end) across all channels.
    float peakAbs (Frame start, Frame end) const;

    AudioDataPtr slice (Frame start, Frame end) const;

    // Replaces [start, end) with `insert` (or removes it when insert is null).
    // Inserted channels are mapped by index modulo their channel count.
    AudioDataPtr replaced (Frame start, Frame end, const AudioData* insert) const;

    // Runs `process` over [start, end) in blocks of up to AudioChunk::maxFrames.
    // The block is [channel][frame]; blockStart is its absolute position.
    using Processor = std::function<void (std::vector<std::vector<float>>& block, Frame blockStart)>;
    AudioDataPtr processed (Frame start, Frame end, const Processor& process) const;

    // Builds new data from interleaved-by-channel blocks; used by loaders and DSP.
    class Builder
    {
    public:
        Builder (int numChannels, double sampleRate);
        void append (const float* const* channelData, int numFrames);
        void appendShared (const AudioData& source, Frame start, Frame end);
        AudioDataPtr finish();

    private:
        void flush();
        std::shared_ptr<AudioData> data;
        std::vector<std::vector<float>> pending;
    };

private:
    int chunkIndexFor (Frame position) const;

    int channels;
    double rate;
    std::vector<ChunkPtr> chunks;
    std::vector<Frame> starts;
    Frame total = 0;
};
