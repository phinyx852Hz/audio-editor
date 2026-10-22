// SPDX-License-Identifier: AGPL-3.0-only
#include "Cameras.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>

namespace perf
{
std::vector<CameraDevice> listCameras()
{
    std::vector<CameraDevice> found;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator ("/dev", ec))
    {
        const auto file = entry.path().filename().string();
        if (file.rfind ("video", 0) != 0)
            continue;
        const auto fd = ::open (entry.path().c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        v4l2_capability caps {};
        if (::ioctl (fd, VIDIOC_QUERYCAP, &caps) == 0)
        {
            const auto device = (caps.capabilities & V4L2_CAP_DEVICE_CAPS) != 0 ? caps.device_caps : caps.capabilities;
            if ((device & V4L2_CAP_VIDEO_CAPTURE) != 0 && (device & V4L2_CAP_STREAMING) != 0)
                found.push_back ({ entry.path().string(), (const char*) caps.card, (const char*) caps.bus_info });
        }
        ::close (fd);
    }
    std::sort (found.begin(), found.end(), [] (const auto& a, const auto& b)
    {
        return a.path.size() != b.path.size() ? a.path.size() < b.path.size() : a.path < b.path;
    });
    return found;
}

void Camera::start (const std::string& path, int width, int height, int fps)
{
    stop();
    {
        std::lock_guard l (lock);
        picture = nullptr;
        problem.clear();
    }
    stopping = false;
    running = true;
    thread = std::thread ([this, path, width, height, fps] { run (path, width, height, fps); });
}

void Camera::stop()
{
    stopping = true;
    if (thread.joinable())
        thread.join();
    running = false;
}

std::shared_ptr<const Picture> Camera::latest() const
{
    std::lock_guard l (lock);
    return picture;
}

std::string Camera::error() const
{
    std::lock_guard l (lock);
    return problem;
}

void Camera::run (std::string path, int width, int height, int fps)
{
    static std::once_flag registered;
    std::call_once (registered, [] { avdevice_register_all(); });

    auto fail = [this] (std::string text)
    {
        std::lock_guard l (lock);
        problem = std::move (text);
    };

    const auto* v4l2 = av_find_input_format ("video4linux2");
    if (v4l2 == nullptr)
    {
        fail ("This FFmpeg was built without Video4Linux cameras (libavdevice)");
        running = false;
        return;
    }

    // MJPEG first: most USB cameras give their larger sizes only that way. Then whatever it offers.
    AVFormatContext* input = nullptr;
    const auto size = std::to_string (width) + "x" + std::to_string (height);
    for (const char* format : { "mjpeg", "", (const char*) nullptr })
    {
        input = avformat_alloc_context();
        input->interrupt_callback.callback = [] (void* self) { return ((std::atomic<bool>*) self)->load() ? 1 : 0; };
        input->interrupt_callback.opaque = &stopping;
        input->flags |= AVFMT_FLAG_NONBLOCK;   // so stop() never waits on a camera that went quiet
        AVDictionary* options = nullptr;
        if (format != nullptr)
        {
            av_dict_set (&options, "video_size", size.c_str(), 0);
            av_dict_set (&options, "framerate", std::to_string (fps).c_str(), 0);
            if (*format != 0)
                av_dict_set (&options, "input_format", format, 0);
        }
        const auto r = avformat_open_input (&input, path.c_str(), v4l2, &options);
        av_dict_free (&options);
        if (r >= 0)
            break;
        input = nullptr;   // freed by avformat_open_input
        if (stopping)
            break;
    }
    if (input == nullptr)
    {
        if (! stopping)
            fail ("Cannot open the camera " + path + " (in use by another program?)");
        running = false;
        return;
    }

    AVCodecContext* decoder = nullptr;
    SwsContext* scaler = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    uint64_t serial = 0;

    if (avformat_find_stream_info (input, nullptr) >= 0 && input->nb_streams > 0)
    {
        const auto* par = input->streams[0]->codecpar;
        if (const auto* codec = avcodec_find_decoder (par->codec_id))
        {
            decoder = avcodec_alloc_context3 (codec);
            avcodec_parameters_to_context (decoder, par);
            if (avcodec_open2 (decoder, codec, nullptr) < 0)
                avcodec_free_context (&decoder);
        }
    }
    if (decoder == nullptr && ! stopping)
        fail ("Cannot read the pictures of the camera " + path);

    while (decoder != nullptr && ! stopping)
    {
        const auto r = av_read_frame (input, packet);
        if (r == AVERROR (EAGAIN))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (4));
            continue;
        }
        if (r < 0)
        {
            if (! stopping)
                fail ("The camera " + path + " stopped (unplugged?)");
            break;
        }
        if (avcodec_send_packet (decoder, packet) >= 0)
        {
            while (avcodec_receive_frame (decoder, frame) >= 0)
            {
                // MJPEG decodes to full-range YUVJ formats, which swscale wants named as such.
                auto source = (AVPixelFormat) frame->format;
                scaler = sws_getCachedContext (scaler, frame->width, frame->height, source, frame->width, frame->height,
                                               AV_PIX_FMT_BGR0, SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (scaler == nullptr)
                    continue;
                auto p = std::make_shared<Picture>();
                p->width = frame->width;
                p->height = frame->height;
                p->pixels.resize ((size_t) p->width * (size_t) p->height * 4);
                uint8_t* dst[1] { p->pixels.data() };
                const int dstStride[1] { p->width * 4 };
                sws_scale (scaler, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
                p->serial = ++serial;
                std::lock_guard l (lock);
                picture = std::move (p);
            }
        }
        av_packet_unref (packet);
    }

    sws_freeContext (scaler);
    av_frame_free (&frame);
    av_packet_free (&packet);
    avcodec_free_context (&decoder);
    avformat_close_input (&input);
    running = false;
}
}
