// SPDX-License-Identifier: AGPL-3.0-only
// Tests for the Performance menu's video writer (Source/performance/MediaWriter): files are
// written with FFmpeg's encoders and read back with its demuxers. Plain C++, no Qt or JUCE.
#include "../Source/performance/MediaWriter.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok)
        {
            std::printf ("FAIL: %s\n", what.c_str());
            ++failures;
        }
    }

    std::string tempPath (const std::string& name)
    {
        return (std::filesystem::temp_directory_path() / ("phinyx-performance-" + name)).string();
    }

    // Writes `seconds` of a moving picture and a tone on each audio track.
    bool writeSample (perf::MediaWriter& w, const perf::Output& out, double seconds, int sourceWidth, int sourceHeight)
    {
        const auto fps = out.video.front().fps;
        std::vector<uint8_t> picture ((size_t) sourceWidth * (size_t) sourceHeight * 4);
        const auto frames = (int) std::lround (seconds * fps);
        const int block = 480;   // 10 ms, as the recorder sends it
        int64_t audioDone = 0;
        for (int f = 0; f < frames; ++f)
        {
            for (int y = 0; y < sourceHeight; ++y)
                for (int x = 0; x < sourceWidth; ++x)
                {
                    auto* p = picture.data() + ((size_t) y * (size_t) sourceWidth + (size_t) x) * 4;
                    p[0] = (uint8_t) (x + f * 4);
                    p[1] = (uint8_t) (y * 2);
                    p[2] = (uint8_t) (f * 8);
                    p[3] = 255;
                }
            for (size_t t = 0; t < out.video.size(); ++t)
                if (! w.writeVideo (t, picture.data(), sourceWidth, sourceHeight, sourceWidth * 4, f))
                    return false;
            // The same frame again is skipped, not an error.
            if (! w.writeVideo (0, picture.data(), sourceWidth, sourceHeight, sourceWidth * 4, f))
                return false;

            const auto audioDue = (int64_t) std::llround ((f + 1) * (double) out.sampleRate / fps);
            while (audioDone < audioDue)
            {
                for (size_t t = 0; t < out.audio.size(); ++t)
                {
                    const auto ch = out.audio[t].channels;
                    std::vector<float> samples ((size_t) block * (size_t) ch);
                    for (int i = 0; i < block; ++i)
                        for (int c = 0; c < ch; ++c)
                            samples[(size_t) i * (size_t) ch + (size_t) c] = 0.25f * (float) std::sin (2.0 * M_PI * (220.0 * (c + 1)) * (double) (audioDone + i) / out.sampleRate);
                    if (! w.writeAudio (t, samples.data(), block))
                        return false;
                }
                audioDone += block;
            }
        }
        return w.finish();
    }

    struct Probed
    {
        std::vector<AVCodecID> codecs;
        std::vector<int> channels;
        std::vector<std::string> titles;
        std::vector<int> widths;
        std::vector<int64_t> packets;
        double duration = 0.0;
        bool defaultAudioFirst = false;
    };

    Probed probe (const std::string& path)
    {
        Probed p;
        AVFormatContext* f = nullptr;
        if (avformat_open_input (&f, path.c_str(), nullptr, nullptr) < 0)
            return p;
        avformat_find_stream_info (f, nullptr);
        bool firstAudio = true;
        for (unsigned i = 0; i < f->nb_streams; ++i)
        {
            const auto* s = f->streams[i];
            p.codecs.push_back (s->codecpar->codec_id);
            p.channels.push_back (s->codecpar->ch_layout.nb_channels);
            p.widths.push_back (s->codecpar->width);
            const auto* title = av_dict_get (s->metadata, "title", nullptr, 0);
            if (title == nullptr)
                title = av_dict_get (s->metadata, "handler_name", nullptr, 0);
            p.titles.push_back (title != nullptr ? title->value : "");
            if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && firstAudio)
            {
                p.defaultAudioFirst = (s->disposition & AV_DISPOSITION_DEFAULT) != 0;
                firstAudio = false;
            }
        }
        p.packets.assign (f->nb_streams, 0);
        AVPacket* packet = av_packet_alloc();
        while (av_read_frame (f, packet) >= 0)
        {
            ++p.packets[(size_t) packet->stream_index];
            av_packet_unref (packet);
        }
        av_packet_free (&packet);
        p.duration = f->duration > 0 ? f->duration / (double) AV_TIME_BASE : 0.0;
        avformat_close_input (&f);
        return p;
    }

    perf::Codec videoCodec()
    {
        return ! perf::workingEncoders (perf::Codec::hevc).empty() ? perf::Codec::hevc : perf::Codec::h264;
    }

    void testBitrates()
    {
        using perf::Codec;
        const auto hd = perf::defaultBitrate (1920, 1080, 30, Codec::hevc);
        check (hd >= 8000000 && hd <= 16000000, "1080p30 HEVC is about 12 Mbit/s, got " + std::to_string (hd));
        check (perf::defaultBitrate (3840, 2160, 30, Codec::hevc) > 2 * hd, "4K needs more than twice 1080p");
        check (perf::defaultBitrate (1920, 1080, 60, Codec::hevc) > hd, "60 frames a second need more than 30");
        check (perf::defaultBitrate (1920, 1080, 30, Codec::h264) > hd, "H.264 needs more than HEVC");
        const auto h = perf::recommendedHeight();
        check (h == 1080 || h == 1440 || h == 2160, "recommended height is 1080, 1440 or 2160");
    }

    void testMp4 (bool fragmented)
    {
        const auto codec = videoCodec();
        perf::Output out;
        out.target = tempPath (fragmented ? "fragmented.mp4" : "plain.mp4");
        out.container = "mp4";
        out.fragmented = fragmented;
        perf::VideoTrack main;
        main.width = 640;
        main.height = 360;
        main.fps = 30;
        main.codec = codec;
        main.title = "PHINYX";
        perf::VideoTrack camera = main;
        camera.width = 320;
        camera.height = 240;
        camera.title = "Camera";
        out.video = { main, camera };
        out.audio = { { 2, 256000, "Mix" }, { 6, 640000, "PHINYX (6 channels)" }, { 1, 96000, "Microphone" } };

        perf::MediaWriter w;
        const bool opened = w.open (out);
        check (opened, "MP4 opens: " + w.error());
        if (! opened)
            return;
        check (! w.encoderName (0).empty(), "an encoder was chosen");
        check (writeSample (w, out, 2.0, 800, 450), "MP4 written: " + w.error());
        check (w.videoFrames (0) == 60, "60 pictures in 2 s at 30 (the repeated ones skipped), got " + std::to_string (w.videoFrames (0)));

        const auto p = probe (out.target);
        check (p.codecs.size() == 5, "MP4 has 2 video and 3 audio tracks, got " + std::to_string (p.codecs.size()));
        if (p.codecs.size() == 5)
        {
            const auto video = codec == perf::Codec::hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
            check (p.codecs[0] == video && p.codecs[1] == video, "video tracks are " + std::string (codec == perf::Codec::hevc ? "HEVC" : "H.264"));
            check (p.widths[0] == 640 && p.widths[1] == 320, "video tracks keep their sizes");
            check (p.codecs[2] == AV_CODEC_ID_AAC && p.codecs[3] == AV_CODEC_ID_AAC && p.codecs[4] == AV_CODEC_ID_AAC, "audio tracks are AAC");
            check (p.channels[2] == 2 && p.channels[3] == 6 && p.channels[4] == 1, "audio tracks have 2, 6 and 1 channels");
            check (p.titles[3] == "PHINYX (6 channels)", "audio tracks keep their names, got '" + p.titles[3] + "'");
            check (p.defaultAudioFirst, "the mix is the default audio track");
            check (p.packets[0] >= 59 && p.packets[1] >= 59, "every picture was written");
            // 2 s at 48 kHz is 94 AAC frames of 1024.
            check (p.packets[2] >= 90 && p.packets[3] >= 90 && p.packets[4] >= 90, "every second of sound was written");
        }
        check (p.duration > 1.8 && p.duration < 2.3, "lasts about 2 s, got " + std::to_string (p.duration));
        std::filesystem::remove (out.target);
    }

    void testStreamContainers()
    {
        // What a stream sends, written to a file: MPEG-TS for SRT, FLV for RTMP.
        for (const auto& [container, name, codec] : { std::tuple { "mpegts", "stream.ts", perf::Codec::h264 },
                                                      std::tuple { "flv", "stream.flv", perf::Codec::h264 },
                                                      std::tuple { "flv", "stream-hevc.flv", perf::Codec::hevc } })
        {
            if (perf::workingEncoders (codec).empty() || (codec == perf::Codec::hevc && ! perf::canStream (codec)))
            {
                std::printf ("skipped %s: no encoder or FFmpeg too old\n", name);
                continue;
            }
            perf::Output out;
            out.target = tempPath (name);
            out.container = container;
            perf::VideoTrack v;
            v.width = 640;
            v.height = 360;
            v.codec = codec;
            v.constantRate = true;
            v.bitrate = 2000000;
            out.video = { v };
            out.audio = { { 2, 160000, {} } };
            perf::MediaWriter w;
            check (w.open (out), std::string (name) + " opens: " + w.error());
            check (writeSample (w, out, 1.0, 640, 360), std::string (name) + " written: " + w.error());
            const auto p = probe (out.target);
            check (p.codecs.size() == 2 && p.codecs[1] == AV_CODEC_ID_AAC, std::string (name) + " has video and AAC");
            std::filesystem::remove (out.target);
        }
    }

    void testBadTarget()
    {
        perf::Output out;
        out.target = "/nonexistent-folder/x.mp4";
        out.container = "mp4";
        out.video = { perf::VideoTrack {} };
        out.video[0].codec = videoCodec();
        perf::MediaWriter w;
        check (! w.open (out) && ! w.error().empty(), "a folder that does not exist is an error, not a crash");
    }
}

int main()
{
    av_log_set_level (AV_LOG_ERROR);
    std::printf ("FFmpeg %s\n", perf::ffmpegVersion().c_str());
    for (auto codec : { perf::Codec::hevc, perf::Codec::h264 })
        for (const auto& e : perf::workingEncoders (codec))
            std::printf ("encoder: %s (%s)\n", e.name.c_str(), e.label.c_str());
    if (perf::workingEncoders (perf::Codec::hevc).empty() && perf::workingEncoders (perf::Codec::h264).empty())
    {
        std::printf ("No H.265 or H.264 encoder in this FFmpeg: video tests skipped\n");
        testBitrates();
        return failures == 0 ? 0 : 1;
    }

    testBitrates();
    testMp4 (false);
    testMp4 (true);
    testStreamContainers();
    testBadTarget();

    if (failures == 0)
        std::printf ("All performance tests passed\n");
    return failures == 0 ? 0 : 1;
}
