// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Push 2 and 3's display (docs/control-surfaces.md): 960x160 pixels sent over USB with libusb,
// not over MIDI. How it is spoken to (the vendor and product ids, interface 0, a 16-byte header
// then the frame in BGR565 rows of 1024 to bulk endpoint 1, a frame every 40 ms) follows Ableton's
// Push 2 Display Interface Manual and Ardour's Push 2 support (libs/surfaces/push2, by Paul Davis
// and Robin Gareus, GPL-2.0-or-later). Built without libusb (PHI_LIBUSB 0), the display stays dark.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class PushDisplay
{
public:
    static constexpr int width = 960, height = 160;

    PushDisplay();
    ~PushDisplay();

    static bool available();   // built with libusb
    // Looks for a Push 2 or 3 on USB and starts sending to it. False if none, or no access to it
    // (Linux needs a udev rule, docs/control-surfaces.md). Again while open: true.
    bool open();
    void close();
    bool isOpen() const { return running.load(); }
    bool failed() const { return lost.load(); }   // the device went away while open

    // A picture to show: width x height pixels of 0xAARRGGBB, rows of `stride` pixels. Any thread.
    void setFrame (const uint32_t* pixels, int stride);

    // A pixel as the display takes it: blue 5 bits, green 6, red 5, red lowest.
    static uint16_t bgr565 (uint32_t argb)
    {
        const uint32_t r = (argb >> 16) & 0xff, g = (argb >> 8) & 0xff, b = argb & 0xff;
        return (uint16_t) ((r >> 3) | ((g & 0xfc) << 3) | ((b & 0xf8) << 8));
    }

private:
    void run();

    struct Usb;
    std::unique_ptr<Usb> usb;
    std::thread worker;
    std::atomic<bool> running { false }, lost { false };
    std::mutex frameLock;
    std::vector<uint16_t> frame;   // height rows of 1024: 960 pixels, then 64 of filler
};
