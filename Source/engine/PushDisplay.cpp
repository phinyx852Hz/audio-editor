// SPDX-License-Identifier: AGPL-3.0-only
// Push 2 and 3's display over USB. The way of talking to it follows Ardour's Push 2 support
// (libs/surfaces/push2/push2.cc and canvas.cc, by Paul Davis and Robin Gareus,
// GPL-2.0-or-later) and Ableton's Push 2 Display Interface Manual; the code is PHI's own.
#include "PushDisplay.h"

#include <chrono>

#if PHI_LIBUSB
#include <libusb.h>
#endif

namespace
{
    constexpr int rowLength = 1024;   // 960 pixels and 64 of filler, so no row spans a 512-byte USB packet
#if PHI_LIBUSB
    constexpr uint16_t ableton = 0x2982;
    constexpr uint16_t products[] = { 0x1967, 0x1969 };   // Push 2, Push 3
    constexpr uint8_t endpoint = 0x01;
    // The frame header. Ardour sends this one and leaves the pixels un-XORed, and the colours come
    // out right.
    constexpr uint8_t header[16] = { 0xEF, 0xCD, 0xAB, 0x89, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
#endif
}

struct PushDisplay::Usb
{
#if PHI_LIBUSB
    libusb_context* context = nullptr;
    libusb_device_handle* handle = nullptr;
#endif
};

PushDisplay::PushDisplay() : usb (std::make_unique<Usb>()), frame ((size_t) (rowLength * height), 0) {}

PushDisplay::~PushDisplay() { close(); }

bool PushDisplay::available() { return PHI_LIBUSB != 0; }

bool PushDisplay::open()
{
    if (running)
        return true;
    close();
#if PHI_LIBUSB
    if (libusb_init (&usb->context) != LIBUSB_SUCCESS)
    {
        usb->context = nullptr;
        return false;
    }
    for (const auto product : products)
        if ((usb->handle = libusb_open_device_with_vid_pid (usb->context, ableton, product)) != nullptr)
            break;
    if (usb->handle == nullptr || libusb_claim_interface (usb->handle, 0) != LIBUSB_SUCCESS)
    {
        if (usb->handle != nullptr)
            libusb_close (usb->handle);
        usb->handle = nullptr;
        libusb_exit (usb->context);
        usb->context = nullptr;
        return false;
    }
    lost = false;
    running = true;
    worker = std::thread ([this] { run(); });
    return true;
#else
    return false;
#endif
}

void PushDisplay::close()
{
    running = false;
    if (worker.joinable())
        worker.join();
#if PHI_LIBUSB
    if (usb->handle != nullptr)
    {
        libusb_release_interface (usb->handle, 0);
        libusb_close (usb->handle);
        usb->handle = nullptr;
    }
    if (usb->context != nullptr)
    {
        libusb_exit (usb->context);
        usb->context = nullptr;
    }
#endif
}

void PushDisplay::setFrame (const uint32_t* pixels, int stride)
{
    std::lock_guard g (frameLock);
    for (int y = 0; y < height; ++y)
    {
        const auto* in = pixels + (size_t) y * (size_t) stride;
        auto* out = frame.data() + (size_t) y * rowLength;
        for (int x = 0; x < width; ++x)
            out[x] = bgr565 (in[x]);
    }
}

void PushDisplay::run()
{
#if PHI_LIBUSB
    // The display goes dark when no frame comes for a while, so the frame is sent again and
    // again, every 40 ms as Ardour does, whether it changed or not.
    std::vector<uint16_t> out (frame.size());
    auto next = std::chrono::steady_clock::now();
    while (running)
    {
        {
            std::lock_guard g (frameLock);
            out = frame;
        }
        int sent = 0;
        auto* head = const_cast<uint8_t*> (header);
        if (libusb_bulk_transfer (usb->handle, endpoint, head, (int) sizeof (header), &sent, 1000) != 0
            || libusb_bulk_transfer (usb->handle, endpoint, reinterpret_cast<uint8_t*> (out.data()),
                                     (int) (out.size() * sizeof (uint16_t)), &sent, 1000) != 0)
        {
            lost = true;
            running = false;
            return;
        }
        next += std::chrono::milliseconds (40);
        std::this_thread::sleep_until (next);
        if (std::chrono::steady_clock::now() > next + std::chrono::milliseconds (200))
            next = std::chrono::steady_clock::now();
    }
#endif
}
