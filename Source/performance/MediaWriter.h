// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Video and audio out through FFmpeg (libavcodec, libavformat, libswscale): an MP4 file
// with any number of video and audio tracks, or a live stream (RTMP, RTMPS, SRT).
// Video is H.265 (HEVC) or H.264 from the best encoder this computer has: VAAPI (AMD,
// Intel), NVENC (NVIDIA) or x265 / x264 on the processor. Audio is AAC-LC, up to 8
// channels a track. No Qt or JUCE types; one thread uses a writer at a time.
namespace perf
{
    enum class Codec { hevc, h264 };

    struct EncoderInfo
    {
        std::string name;    // FFmpeg's: "hevc_vaapi", "libx265"...
        std::string label;   // "VAAPI (graphics card)"
        bool hardware = false;
    };

    // The encoders that open on this computer for the codec, best first (graphics card
    // before processor). The first call tries each one, which takes up to a second; then cached.
    const std::vector<EncoderInfo>& workingEncoders (Codec);

    // The largest picture (1080, 1440 or 2160 lines) this computer should record live,
    // from its best HEVC encoder and processor cores.
    int recommendedHeight();

    // A bit rate that looks clean for screen content (bits per second).
    int64_t defaultBitrate (int width, int height, int fps, Codec);

    // Whether FFmpeg can send this codec over RTMP (HEVC needs FFmpeg 6.1's enhanced FLV).
    bool canStream (Codec);
    bool hasProtocol (const std::string& name);   // "rtmp", "rtmps", "srt"
    std::string ffmpegVersion();

    struct VideoTrack
    {
        int width = 1920, height = 1080, fps = 30;
        Codec codec = Codec::hevc;
        std::string encoder;         // empty: the best of workingEncoders
        int64_t bitrate = 0;         // 0: defaultBitrate
        bool constantRate = false;   // CBR, for streams
        std::string title;
    };

    struct AudioTrack
    {
        int channels = 2;            // 1 to 8; more than 2 are in WAV order (L R C LFE Ls Rs Lss Rss)
        int64_t bitrate = 256000;
        std::string title;
    };

    struct Output
    {
        std::string target;          // a file path or a URL
        std::string container;       // "mp4", "flv" (RTMP) or "mpegts" (SRT)
        std::vector<VideoTrack> video;
        std::vector<AudioTrack> audio;
        int sampleRate = 48000;
        bool fragmented = false;     // MP4 that stays playable if PHINYX stops without closing it
    };

    class MediaWriter
    {
    public:
        MediaWriter();
        ~MediaWriter();

        bool open (const Output&);
        // A picture for a video track: 32-bit pixels in memory order B, G, R, x (QImage's
        // Format_RGB32), shown from frame / fps seconds. Scaled when not the track's size.
        // Frames come in order; one whose index is not past the last is skipped.
        bool writeVideo (size_t track, const uint8_t* bgrx, int width, int height, int stride, int64_t frame);
        // Interleaved samples at the output's rate, the track's channel count each.
        bool writeAudio (size_t track, const float* interleaved, int frames);
        // Flushes the encoders and closes the file or stream.
        bool finish();
        // From another thread: a network write that is stuck gives up, and so does every later call.
        void abort() { aborted = true; }

        const std::string& error() const { return problem; }
        int64_t bytesWritten() const;
        std::string encoderName (size_t videoTrack) const;
        int64_t videoFrames (size_t track) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        std::string problem;
        std::atomic<bool> aborted { false };
    };
}
