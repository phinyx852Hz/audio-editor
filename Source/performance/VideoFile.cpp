// SPDX-License-Identifier: AGPL-3.0-only
#include "VideoFile.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <chrono>

namespace perf
{
void VideoFile::start (const std::string& path, int maxWidth)
{
    stop();
    {
        std::lock_guard l (lock);
        picture = nullptr;
        problem.clear();
    }
    stopping = false;
    running = true;
    thread = std::thread ([this, path, maxWidth] { run (path, maxWidth); });
}

void VideoFile::stop()
{
    stopping = true;
    if (thread.joinable())
        thread.join();
    running = false;
}

std::shared_ptr<const Picture> VideoFile::latest() const
{
    std::lock_guard l (lock);
    return picture;
}

std::string VideoFile::error() const
{
    std::lock_guard l (lock);
    return problem;
}

void VideoFile::run (std::string path, int maxWidth)
{
    auto fail = [this] (std::string text)
    {
        std::lock_guard l (lock);
        problem = std::move (text);
    };

    AVFormatContext* input = avformat_alloc_context();
    input->interrupt_callback.callback = [] (void* self) { return ((std::atomic<bool>*) self)->load() ? 1 : 0; };
    input->interrupt_callback.opaque = &stopping;
    if (avformat_open_input (&input, path.c_str(), nullptr, nullptr) < 0)
    {
        fail ("Cannot open " + path);
        running = false;
        return;
    }

    AVCodecContext* decoder = nullptr;
    int stream = -1;
    if (avformat_find_stream_info (input, nullptr) >= 0)
    {
        const AVCodec* codec = nullptr;
        stream = av_find_best_stream (input, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (stream >= 0 && codec != nullptr)
        {
            decoder = avcodec_alloc_context3 (codec);
            avcodec_parameters_to_context (decoder, input->streams[stream]->codecpar);
            decoder->thread_count = 2;
            if (avcodec_open2 (decoder, codec, nullptr) < 0)
                avcodec_free_context (&decoder);
        }
    }
    if (decoder == nullptr)
        fail ("No video found in " + path);

    SwsContext* scaler = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    uint64_t serial = 0;
    const auto timeBase = decoder != nullptr ? av_q2d (input->streams[stream]->time_base) : 0.0;
    using Clock = std::chrono::steady_clock;
    auto began = Clock::now();
    double firstTime = -1.0;

    while (decoder != nullptr && ! stopping)
    {
        const auto r = av_read_frame (input, packet);
        if (r < 0)
        {
            // The end: round again from the start.
            if (av_seek_frame (input, stream, 0, AVSEEK_FLAG_BACKWARD) < 0 || serial == 0)
            {
                if (serial == 0)
                    fail ("Cannot read the pictures of " + path);
                break;
            }
            avcodec_flush_buffers (decoder);
            firstTime = -1.0;
            continue;
        }
        if (packet->stream_index == stream && avcodec_send_packet (decoder, packet) >= 0)
        {
            while (! stopping && avcodec_receive_frame (decoder, frame) >= 0)
            {
                // Wait for its time, so the video plays at its own speed.
                const auto ts = frame->best_effort_timestamp;
                const double at = ts == AV_NOPTS_VALUE ? -1.0 : ts * timeBase;
                if (at >= 0.0)
                {
                    if (firstTime < 0.0)
                    {
                        firstTime = at;
                        began = Clock::now();
                    }
                    const auto due = began + std::chrono::duration_cast<Clock::duration> (std::chrono::duration<double> (at - firstTime));
                    while (! stopping && Clock::now() < due)
                        std::this_thread::sleep_for (std::min<Clock::duration> (due - Clock::now(), std::chrono::milliseconds (20)));
                }
                const int width = std::min (frame->width, std::max (16, maxWidth));
                const int height = std::max (1, (int) ((long long) frame->height * width / std::max (1, frame->width)));
                scaler = sws_getCachedContext (scaler, frame->width, frame->height, (AVPixelFormat) frame->format, width, height,
                                               AV_PIX_FMT_BGR0, SWS_AREA, nullptr, nullptr, nullptr);
                if (scaler == nullptr)
                    continue;
                auto p = std::make_shared<Picture>();
                p->width = width;
                p->height = height;
                p->pixels.resize ((size_t) width * (size_t) height * 4);
                uint8_t* dst[1] { p->pixels.data() };
                const int dstStride[1] { width * 4 };
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
