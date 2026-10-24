// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Cameras.h"

// A video file played silently, over and over, on a thread of its own (FFmpeg), at
// its own speed and small: it keeps only its newest picture, as a Camera does, for
// whoever wants to look at it (the Visualizer's picture, for sonifying). No Qt or JUCE types.
namespace perf
{
    class VideoFile
    {
    public:
        VideoFile() = default;
        ~VideoFile() { stop(); }

        // Starts playing, scaled to at most `maxWidth` pixels across.
        void start (const std::string& path, int maxWidth);
        void stop();
        bool isRunning() const { return running.load(); }

        std::shared_ptr<const Picture> latest() const;
        std::string error() const;

    private:
        void run (std::string path, int maxWidth);

        std::thread thread;
        std::atomic<bool> running { false }, stopping { false };
        mutable std::mutex lock;
        std::shared_ptr<const Picture> picture;
        std::string problem;
    };
}
