// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Webcams and capture cards (Video4Linux), read through FFmpeg's libavdevice on a
// thread of their own. Each keeps only its newest picture, for the recorder to take.
// No Qt or JUCE types.
namespace perf
{
    struct CameraDevice
    {
        std::string path;   // /dev/video0
        std::string name;   // "HD Pro Webcam C920"
        std::string bus;    // "usb-0000:00:14.0-1": the same camera across restarts, even when its /dev number changes
    };

    // The cameras plugged in now (video capture nodes only, not their metadata nodes).
    std::vector<CameraDevice> listCameras();

    // A picture in memory order B, G, R, x (QImage's Format_RGB32).
    struct Picture
    {
        int width = 0, height = 0;
        std::vector<uint8_t> pixels;   // width * 4 bytes a row
        uint64_t serial = 0;           // counts up with each new picture
    };

    class Camera
    {
    public:
        Camera() = default;
        ~Camera() { stop(); }

        // Starts reading at about this size and frame rate (the nearest the camera offers).
        void start (const std::string& path, int width, int height, int fps);
        void stop();
        bool isRunning() const { return running.load(); }

        // The newest picture, or null before the first one.
        std::shared_ptr<const Picture> latest() const;
        // Why it stopped (unplugged, busy...), or empty.
        std::string error() const;

    private:
        void run (std::string path, int width, int height, int fps);

        std::thread thread;
        std::atomic<bool> running { false }, stopping { false };
        mutable std::mutex lock;
        std::shared_ptr<const Picture> picture;
        std::string problem;
    };
}
