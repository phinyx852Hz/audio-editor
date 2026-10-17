// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Something PHINYX can record from, like an OBS audio source.
struct CaptureSource
{
    enum class Kind { desktop, input, application };

    Kind kind = Kind::desktop;
    std::string target;        // PipeWire node.name or object.serial; empty = the default output
    std::string name;          // shown to the user
    std::string detail;        // e.g. what an application is playing
    int channels = 2;          // 0 when unknown

    bool operator== (const CaptureSource& o) const { return kind == o.kind && target == o.target; }
};

// Records from PipeWire: the desktop's output ("what you hear"), an input such as
// a microphone, or one application's sound. Capture runs on PipeWire's own thread
// and fills a lock-free buffer that the UI empties with read(). Without PipeWire
// at build time isAvailable() is false and nothing else does anything.
// This header contains no PipeWire, JUCE or Qt types.
class AudioCapture
{
public:
    AudioCapture();
    ~AudioCapture();

    static bool isAvailable();

    // Connects to PipeWire and starts following its devices. Returns an error, or empty.
    std::string connect();
    bool isConnected() const;

    // The sources right now: the default output first, then outputs, inputs and applications.
    std::vector<CaptureSource> sources() const;
    // Changes whenever sources() would return something different.
    uint64_t sourcesVersion() const { return version.load(); }

    // Starts capturing; PipeWire converts to this rate and channel count. Returns an error, or empty.
    std::string start (const CaptureSource&, int sampleRate, int numChannels);
    void stop();
    bool isRunning() const;

    int sampleRate() const  { return rate; }
    int numChannels() const { return channels; }

    // Takes up to maxFrames interleaved frames of what was captured. Returns the frame count.
    int read (float* interleaved, int maxFrames);
    // Frames lost because read() was not called often enough (the buffer holds about 30 s).
    uint64_t droppedFrames() const { return dropped.load(); }
    // Set when PipeWire reports a problem with the stream, e.g. the source went away.
    std::string streamError() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
    std::atomic<uint64_t> version { 0 }, dropped { 0 };
    int rate = 48000, channels = 2;
};
