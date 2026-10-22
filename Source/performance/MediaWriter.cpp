// SPDX-License-Identifier: AGPL-3.0-only
#include "MediaWriter.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace perf
{
namespace
{
    std::string avError (int code)
    {
        char text[AV_ERROR_MAX_STRING_SIZE] {};
        av_strerror (code, text, sizeof (text));
        return text;
    }

    struct Candidate { const char* name; const char* label; bool hardware; };

    // Best first. QSV and AMF are left out: on Linux, VAAPI drives the same Intel and AMD chips.
    const std::vector<Candidate>& candidates (Codec c)
    {
        static const std::vector<Candidate> hevc {
            { "hevc_nvenc", "NVENC (NVIDIA graphics)", true },
            { "hevc_vaapi", "VAAPI (AMD or Intel graphics)", true },
            { "libx265", "x265 (processor)", false },
        };
        static const std::vector<Candidate> h264 {
            { "h264_nvenc", "NVENC (NVIDIA graphics)", true },
            { "h264_vaapi", "VAAPI (AMD or Intel graphics)", true },
            { "libx264", "x264 (processor)", false },
            { "libopenh264", "OpenH264 (processor)", false },
        };
        return c == Codec::hevc ? hevc : h264;
    }

    bool isVaapi (const std::string& name) { return name.find ("_vaapi") != std::string::npos; }
    bool isNvenc (const std::string& name) { return name.find ("_nvenc") != std::string::npos; }

    // x265 and x264 presets that keep up live: faster for more pixels a second on each core
    // (1080p30 is veryfast on 12 cores or more, superfast on 6 to 11, ultrafast on fewer).
    const char* processorPreset (int width, int height, int fps)
    {
        const auto cores = std::max (1u, std::thread::hardware_concurrency());
        const auto perCore = (double) width * height * fps / cores;
        const auto hd30 = 1920.0 * 1080.0 * 30.0;
        if (perCore <= hd30 / 12) return "veryfast";
        if (perCore <= hd30 / 6) return "superfast";
        return "ultrafast";
    }

    struct VideoEncoder
    {
        AVCodecContext* ctx = nullptr;
        AVBufferRef* device = nullptr;
        AVPixelFormat softwareFormat = AV_PIX_FMT_YUV420P;
        std::string name;

        VideoEncoder() = default;
        VideoEncoder (const VideoEncoder&) = delete;
        VideoEncoder& operator= (const VideoEncoder&) = delete;
        ~VideoEncoder() { reset(); }

        void reset()
        {
            avcodec_free_context (&ctx);
            av_buffer_unref (&device);
            softwareFormat = AV_PIX_FMT_YUV420P;
        }
    };

    // Opens an encoder for the track, with its hardware device when it needs one.
    bool openVideoEncoder (VideoEncoder& e, const std::string& name, const VideoTrack& t, bool globalHeader, std::string& error)
    {
        const auto* codec = avcodec_find_encoder_by_name (name.c_str());
        if (codec == nullptr)
        {
            error = "FFmpeg has no " + name + " encoder";
            return false;
        }
        e.name = name;
        e.ctx = avcodec_alloc_context3 (codec);
        auto* c = e.ctx;
        c->width = t.width;
        c->height = t.height;
        c->time_base = { 1, t.fps };
        c->framerate = { t.fps, 1 };
        c->gop_size = t.fps * 2;   // a keyframe every 2 s: what streaming services ask for, and quick seeking
        c->sample_aspect_ratio = { 1, 1 };
        c->colorspace = AVCOL_SPC_BT709;
        c->color_primaries = AVCOL_PRI_BT709;
        c->color_trc = AVCOL_TRC_BT709;
        c->color_range = AVCOL_RANGE_MPEG;

        const auto bitrate = t.bitrate > 0 ? t.bitrate : defaultBitrate (t.width, t.height, t.fps, t.codec);
        c->bit_rate = bitrate;
        c->rc_max_rate = t.constantRate ? bitrate : bitrate * 3 / 2;
        c->rc_buffer_size = (int) std::min<int64_t> (t.constantRate ? bitrate : bitrate * 2, INT32_MAX);
        if (globalHeader)
            c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        AVDictionary* options = nullptr;
        if (isVaapi (name))
        {
            if (const auto r = av_hwdevice_ctx_create (&e.device, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0); r < 0)
            {
                error = "No VAAPI graphics device: " + avError (r);
                return false;
            }
            auto* frames = av_hwframe_ctx_alloc (e.device);
            auto* fc = (AVHWFramesContext*) frames->data;
            fc->format = AV_PIX_FMT_VAAPI;
            fc->sw_format = AV_PIX_FMT_NV12;
            fc->width = t.width;
            fc->height = t.height;
            fc->initial_pool_size = 20;
            if (const auto r = av_hwframe_ctx_init (frames); r < 0)
            {
                av_buffer_unref (&frames);
                error = "VAAPI cannot hold " + std::to_string (t.width) + "x" + std::to_string (t.height) + " frames: " + avError (r);
                return false;
            }
            c->hw_frames_ctx = frames;
            c->pix_fmt = AV_PIX_FMT_VAAPI;
            e.softwareFormat = AV_PIX_FMT_NV12;
            av_dict_set (&options, "rc_mode", t.constantRate ? "CBR" : "VBR", 0);
        }
        else if (isNvenc (name))
        {
            c->pix_fmt = e.softwareFormat = AV_PIX_FMT_NV12;
            av_dict_set (&options, "preset", "p4", 0);
            av_dict_set (&options, "tune", t.constantRate ? "ll" : "hq", 0);
            av_dict_set (&options, "rc", t.constantRate ? "cbr" : "vbr", 0);
        }
        else
        {
            c->pix_fmt = e.softwareFormat = AV_PIX_FMT_YUV420P;
            c->thread_count = 0;
            if (name == "libx265")
            {
                av_dict_set (&options, "preset", processorPreset (t.width, t.height, t.fps), 0);
                av_dict_set (&options, "x265-params", "log-level=error", 0);
            }
            else if (name == "libx264")
            {
                av_dict_set (&options, "preset", processorPreset (t.width, t.height, t.fps), 0);
                if (t.constantRate)
                    av_dict_set (&options, "nal-hrd", "cbr", 0);
            }
        }

        const auto r = avcodec_open2 (c, codec, &options);
        av_dict_free (&options);
        if (r < 0)
        {
            error = "Cannot start the " + name + " encoder: " + avError (r);
            return false;
        }
        return true;
    }

    struct Probe
    {
        std::once_flag once;
        std::vector<EncoderInfo> found;
    };

    // Encodes one small frame, which is the only sure way to know a hardware encoder works.
    bool tryEncoder (const Candidate& candidate, Codec codec)
    {
        VideoTrack t;
        t.width = 640;
        t.height = 360;
        t.codec = codec;
        VideoEncoder e;
        std::string error;
        const auto previous = av_log_get_level();
        av_log_set_level (AV_LOG_QUIET);
        bool ok = openVideoEncoder (e, candidate.name, t, false, error);
        if (ok)
        {
            auto* frame = av_frame_alloc();
            frame->format = e.softwareFormat;
            frame->width = t.width;
            frame->height = t.height;
            ok = av_frame_get_buffer (frame, 0) >= 0;
            if (ok)
            {
                for (int plane = 0; plane < 3 && frame->data[plane] != nullptr; ++plane)
                    std::memset (frame->data[plane], plane == 0 ? 16 : 128, (size_t) frame->linesize[plane] * (size_t) (plane == 0 ? t.height : t.height / 2));
                AVFrame* send = frame;
                AVFrame* hw = nullptr;
                if (e.ctx->hw_frames_ctx != nullptr)
                {
                    hw = av_frame_alloc();
                    ok = av_hwframe_get_buffer (e.ctx->hw_frames_ctx, hw, 0) >= 0 && av_hwframe_transfer_data (hw, frame, 0) >= 0;
                    send = hw;
                }
                send->pts = 0;
                ok = ok && avcodec_send_frame (e.ctx, send) >= 0 && avcodec_send_frame (e.ctx, nullptr) >= 0;
                auto* packet = av_packet_alloc();
                bool got = false;
                while (ok && avcodec_receive_packet (e.ctx, packet) >= 0)
                {
                    got = true;
                    av_packet_unref (packet);
                }
                ok = ok && got;
                av_packet_free (&packet);
                av_frame_free (&hw);
            }
            av_frame_free (&frame);
        }
        av_log_set_level (previous);
        return ok;
    }

    // The channel layout AAC takes for n channels: FFmpeg's usual one, else the encoder's own with that many.
    AVChannelLayout aacLayout (const AVCodec* codec, int channels)
    {
        AVChannelLayout layout {};
        av_channel_layout_default (&layout, channels);
        const AVChannelLayout* list = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
        int n = 0;
        if (avcodec_get_supported_config (nullptr, codec, AV_CODEC_CONFIG_CHANNEL_LAYOUT, 0, (const void**) &list, &n) < 0)
            list = nullptr;
#else
        list = codec->ch_layouts;
#endif
        if (list == nullptr)
            return layout;
        for (auto* l = list; l->nb_channels != 0; ++l)
            if (av_channel_layout_compare (l, &layout) == 0)
                return layout;
        for (auto* l = list; l->nb_channels != 0; ++l)
            if (l->nb_channels == channels)
            {
                av_channel_layout_uninit (&layout);
                av_channel_layout_copy (&layout, l);
                return layout;
            }
        return layout;
    }
}

const std::vector<EncoderInfo>& workingEncoders (Codec codec)
{
    static Probe probes[2];
    auto& p = probes[codec == Codec::hevc ? 0 : 1];
    std::call_once (p.once, [&]
    {
        for (const auto& c : candidates (codec))
            if (tryEncoder (c, codec))
                p.found.push_back ({ c.name, c.label, c.hardware });
    });
    return p.found;
}

int recommendedHeight()
{
    const auto& hevc = workingEncoders (Codec::hevc);
    if (! hevc.empty() && hevc.front().hardware)
        return 2160;
    const auto cores = std::thread::hardware_concurrency();
    return cores >= 16 ? 1440 : 1080;
}

int64_t defaultBitrate (int width, int height, int fps, Codec codec)
{
    // About 0.19 bits a pixel at 1080p30 for HEVC (12 Mbit/s; 34 at 4K30, 48 at 4K60), less per pixel when larger;
    // screen content with text needs more than film. H.264 needs about 1.5 times as much.
    const auto pixels = (double) width * height;
    const auto motion = fps > 30 ? 1.4 : 1.0;
    auto bits = pixels * 30.0 * 0.193 * motion * std::pow (1920.0 * 1080.0 / pixels, 0.25);
    if (codec == Codec::h264)
        bits *= 1.5;
    return (int64_t) std::lround (bits / 100000.0) * 100000;
}

bool canStream (Codec codec)
{
    if (codec == Codec::h264)
        return true;
    const auto* flv = av_guess_format ("flv", nullptr, nullptr);
    return flv != nullptr && avformat_query_codec (flv, AV_CODEC_ID_HEVC, FF_COMPLIANCE_NORMAL) == 1;
}

bool hasProtocol (const std::string& name)
{
    void* opaque = nullptr;
    while (const char* p = avio_enum_protocols (&opaque, 1))
        if (name == p)
            return true;
    return false;
}

std::string ffmpegVersion()
{
    return av_version_info();
}

//==============================================================================
struct MediaWriter::Impl
{
    struct Video
    {
        VideoEncoder encoder;
        AVStream* stream = nullptr;
        SwsContext* scaler = nullptr;
        AVFrame* frame = nullptr;
        int64_t last = -1, written = 0;

        ~Video()
        {
            sws_freeContext (scaler);
            av_frame_free (&frame);
        }
    };

    struct Audio
    {
        AVCodecContext* ctx = nullptr;
        AVStream* stream = nullptr;
        AVFrame* frame = nullptr;
        std::vector<float> pending;   // interleaved, less than a frame
        int channels = 2;
        int64_t pts = 0;

        ~Audio()
        {
            avcodec_free_context (&ctx);
            av_frame_free (&frame);
        }
    };

    AVFormatContext* format = nullptr;
    std::vector<std::unique_ptr<Video>> video;
    std::vector<std::unique_ptr<Audio>> audio;
    AVPacket* packet = av_packet_alloc();
    bool headerWritten = false, finished = false;

    ~Impl()
    {
        video.clear();
        audio.clear();
        av_packet_free (&packet);
        if (format != nullptr)
        {
            if (format->pb != nullptr && (format->oformat->flags & AVFMT_NOFILE) == 0)
                avio_closep (&format->pb);
            avformat_free_context (format);
        }
    }

    bool drain (AVCodecContext* ctx, AVStream* stream, std::string& error)
    {
        for (;;)
        {
            const auto r = avcodec_receive_packet (ctx, packet);
            if (r == AVERROR (EAGAIN) || r == AVERROR_EOF)
                return true;
            if (r < 0)
            {
                error = "Encoding failed: " + avError (r);
                return false;
            }
            av_packet_rescale_ts (packet, ctx->time_base, stream->time_base);
            packet->stream_index = stream->index;
            if (const auto w = av_interleaved_write_frame (format, packet); w < 0)
            {
                error = "Cannot write: " + avError (w);
                return false;
            }
        }
    }
};

MediaWriter::MediaWriter() : impl (std::make_unique<Impl>()) {}

MediaWriter::~MediaWriter()
{
    if (impl->headerWritten && ! impl->finished)
        finish();
}

bool MediaWriter::open (const Output& out)
{
    auto& m = *impl;
    const bool network = out.target.find ("://") != std::string::npos;
    if (const auto r = avformat_alloc_output_context2 (&m.format, nullptr, out.container.c_str(), out.target.c_str()); r < 0)
    {
        problem = "FFmpeg cannot write " + out.container + ": " + avError (r);
        return false;
    }
    m.format->interrupt_callback.callback = [] (void* self) { return ((std::atomic<bool>*) self)->load() ? 1 : 0; };
    m.format->interrupt_callback.opaque = &aborted;
    const bool globalHeader = (m.format->oformat->flags & AVFMT_GLOBALHEADER) != 0;

    for (const auto& t : out.video)
    {
        auto v = std::make_unique<Impl::Video>();
        std::vector<std::string> names;
        if (! t.encoder.empty())
            names.push_back (t.encoder);
        else
            for (const auto& e : workingEncoders (t.codec))
                names.push_back (e.name);
        if (names.empty())
        {
            problem = t.codec == Codec::hevc ? "This FFmpeg has no working H.265 (HEVC) encoder; install one with x265 or a graphics driver with VAAPI or NVENC"
                                             : "This FFmpeg has no working H.264 encoder";
            return false;
        }
        bool opened = false;
        std::string error;
        for (const auto& name : names)
        {
            v->encoder.reset();
            if ((opened = openVideoEncoder (v->encoder, name, t, globalHeader, error)))
                break;
        }
        if (! opened)
        {
            problem = error;
            return false;
        }

        v->stream = avformat_new_stream (m.format, nullptr);
        avcodec_parameters_from_context (v->stream->codecpar, v->encoder.ctx);
        v->stream->time_base = v->encoder.ctx->time_base;
        v->stream->avg_frame_rate = v->encoder.ctx->framerate;
        if (t.codec == Codec::hevc && out.container == "mp4")
            v->stream->codecpar->codec_tag = MKTAG ('h', 'v', 'c', '1');   // so QuickTime and Apple devices play it too
        if (! t.title.empty())
        {
            av_dict_set (&v->stream->metadata, "title", t.title.c_str(), 0);
            av_dict_set (&v->stream->metadata, "handler_name", t.title.c_str(), 0);   // what MP4 keeps
        }
        if (m.video.size() > 0)
            v->stream->disposition = 0;   // camera tracks: not the one players pick

        v->frame = av_frame_alloc();
        v->frame->format = v->encoder.softwareFormat;
        v->frame->width = t.width;
        v->frame->height = t.height;
        v->frame->colorspace = AVCOL_SPC_BT709;
        v->frame->color_range = AVCOL_RANGE_MPEG;
        if (av_frame_get_buffer (v->frame, 0) < 0)
        {
            problem = "Out of memory for video frames";
            return false;
        }
        m.video.push_back (std::move (v));
    }

    const auto* aac = avcodec_find_encoder_by_name ("aac");
    if (aac == nullptr && ! out.audio.empty())
    {
        problem = "This FFmpeg has no AAC encoder";
        return false;
    }
    for (const auto& t : out.audio)
    {
        auto a = std::make_unique<Impl::Audio>();
        a->channels = std::clamp (t.channels, 1, 8);
        a->ctx = avcodec_alloc_context3 (aac);
        auto* c = a->ctx;
        c->sample_fmt = AV_SAMPLE_FMT_FLTP;
        c->sample_rate = out.sampleRate;
        c->time_base = { 1, out.sampleRate };
        c->bit_rate = t.bitrate;
        c->ch_layout = aacLayout (aac, a->channels);
        if (globalHeader)
            c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (const auto r = avcodec_open2 (c, aac, nullptr); r < 0)
        {
            problem = "Cannot start AAC with " + std::to_string (a->channels) + " channels: " + avError (r);
            return false;
        }
        a->stream = avformat_new_stream (m.format, nullptr);
        avcodec_parameters_from_context (a->stream->codecpar, c);
        a->stream->time_base = c->time_base;
        if (! t.title.empty())
        {
            av_dict_set (&a->stream->metadata, "title", t.title.c_str(), 0);
            av_dict_set (&a->stream->metadata, "handler_name", t.title.c_str(), 0);   // what MP4 keeps
        }
        if (! m.audio.empty())
            a->stream->disposition = 0;   // only the first audio track plays by default
        a->frame = av_frame_alloc();
        a->frame->format = c->sample_fmt;
        a->frame->nb_samples = c->frame_size > 0 ? c->frame_size : 1024;
        av_channel_layout_copy (&a->frame->ch_layout, &c->ch_layout);
        a->frame->sample_rate = c->sample_rate;
        if (av_frame_get_buffer (a->frame, 0) < 0)
        {
            problem = "Out of memory for audio frames";
            return false;
        }
        m.audio.push_back (std::move (a));
    }
    if (! m.video.empty())
        m.video.front()->stream->disposition = AV_DISPOSITION_DEFAULT;
    if (! m.audio.empty())
        m.audio.front()->stream->disposition = AV_DISPOSITION_DEFAULT;

    AVDictionary* options = nullptr;
    if ((m.format->oformat->flags & AVFMT_NOFILE) == 0)
    {
        if (network)
            av_dict_set (&options, "rw_timeout", "10000000", 0);   // 10 s without progress ends the stream
        const auto r = avio_open2 (&m.format->pb, out.target.c_str(), AVIO_FLAG_WRITE, &m.format->interrupt_callback, &options);
        av_dict_free (&options);
        if (r < 0)
        {
            problem = (network ? "Cannot connect to " : "Cannot create ") + out.target + ": " + avError (r);
            return false;
        }
    }

    if (out.container == "mp4" && out.fragmented)
        av_dict_set (&options, "movflags", "+frag_keyframe+empty_moov+default_base_moof", 0);
    if (out.container == "flv")
        av_dict_set (&options, "flvflags", "no_duration_filesize", 0);
    const auto r = avformat_write_header (m.format, &options);
    av_dict_free (&options);
    if (r < 0)
    {
        problem = "Cannot start writing: " + avError (r);
        return false;
    }
    m.headerWritten = true;
    return true;
}

bool MediaWriter::writeVideo (size_t track, const uint8_t* bgrx, int width, int height, int stride, int64_t frame)
{
    auto& m = *impl;
    if (aborted || ! m.headerWritten || m.finished || track >= m.video.size())
        return false;
    auto& v = *m.video[track];
    if (frame <= v.last)
        return true;

    const auto* ctx = v.encoder.ctx;
    auto* before = v.scaler;
    v.scaler = sws_getCachedContext (v.scaler, width, height, AV_PIX_FMT_BGR0, ctx->width, ctx->height, v.encoder.softwareFormat,
                                     width == ctx->width && height == ctx->height ? SWS_POINT : SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (v.scaler == nullptr)
    {
        problem = "Cannot convert the picture for the encoder";
        return false;
    }
    // Full-range RGB to limited-range BT.709, as players expect of HD video.
    if (v.scaler != before)
        sws_setColorspaceDetails (v.scaler, sws_getCoefficients (SWS_CS_DEFAULT), 1, sws_getCoefficients (SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16);

    if (av_frame_make_writable (v.frame) < 0)
    {
        problem = "Out of memory for video frames";
        return false;
    }
    const uint8_t* src[1] { bgrx };
    const int srcStride[1] { stride };
    sws_scale (v.scaler, src, srcStride, 0, height, v.frame->data, v.frame->linesize);

    AVFrame* send = v.frame;
    AVFrame* hw = nullptr;
    if (ctx->hw_frames_ctx != nullptr)
    {
        hw = av_frame_alloc();
        if (av_hwframe_get_buffer (ctx->hw_frames_ctx, hw, 0) < 0 || av_hwframe_transfer_data (hw, v.frame, 0) < 0)
        {
            av_frame_free (&hw);
            problem = "Cannot hand the picture to the graphics card";
            return false;
        }
        av_frame_copy_props (hw, v.frame);
        send = hw;
    }
    send->pts = frame;
    const auto r = avcodec_send_frame (v.encoder.ctx, send);
    av_frame_free (&hw);
    if (r < 0)
    {
        problem = "Video encoding failed: " + avError (r);
        return false;
    }
    v.last = frame;
    ++v.written;
    return m.drain (v.encoder.ctx, v.stream, problem);
}

bool MediaWriter::writeAudio (size_t track, const float* interleaved, int frames)
{
    auto& m = *impl;
    if (aborted || ! m.headerWritten || m.finished || track >= m.audio.size())
        return false;
    auto& a = *m.audio[track];
    a.pending.insert (a.pending.end(), interleaved, interleaved + (size_t) frames * (size_t) a.channels);

    const auto size = a.frame->nb_samples;
    size_t used = 0;
    while (a.pending.size() - used >= (size_t) size * (size_t) a.channels)
    {
        if (av_frame_make_writable (a.frame) < 0)
        {
            problem = "Out of memory for audio frames";
            return false;
        }
        const auto* in = a.pending.data() + used;
        for (int ch = 0; ch < a.channels; ++ch)
        {
            auto* out = (float*) a.frame->data[ch];
            for (int i = 0; i < size; ++i)
                out[i] = in[(size_t) i * (size_t) a.channels + (size_t) ch];
        }
        used += (size_t) size * (size_t) a.channels;
        a.frame->pts = a.pts;
        a.pts += size;
        if (const auto r = avcodec_send_frame (a.ctx, a.frame); r < 0)
        {
            problem = "Audio encoding failed: " + avError (r);
            return false;
        }
        if (! m.drain (a.ctx, a.stream, problem))
            return false;
    }
    a.pending.erase (a.pending.begin(), a.pending.begin() + (ptrdiff_t) used);
    return true;
}

bool MediaWriter::finish()
{
    auto& m = *impl;
    if (! m.headerWritten || m.finished)
        return problem.empty();
    m.finished = true;
    bool ok = ! aborted;

    for (size_t i = 0; i < m.audio.size(); ++i)
    {
        auto& a = m.audio[i];
        if (ok && ! a->pending.empty())
        {
            // The last part frame, padded with silence.
            const auto missing = (size_t) a->frame->nb_samples * (size_t) a->channels - a->pending.size();
            std::vector<float> silence (missing, 0.0f);
            m.finished = false;
            ok = writeAudio (i, silence.data(), (int) (missing / (size_t) a->channels));
            m.finished = true;
        }
        if (ok)
            ok = avcodec_send_frame (a->ctx, nullptr) >= 0 && m.drain (a->ctx, a->stream, problem);
    }
    for (auto& v : m.video)
        if (ok)
            ok = avcodec_send_frame (v->encoder.ctx, nullptr) >= 0 && m.drain (v->encoder.ctx, v->stream, problem);

    if (ok)
    {
        if (const auto r = av_write_trailer (m.format); r < 0)
        {
            problem = "Cannot finish the file: " + avError (r);
            ok = false;
        }
    }
    if (m.format->pb != nullptr && (m.format->oformat->flags & AVFMT_NOFILE) == 0)
        avio_closep (&m.format->pb);
    if (aborted && problem.empty())
        problem = "Stopped before it finished";
    return ok;
}

int64_t MediaWriter::bytesWritten() const
{
    auto* pb = impl->format != nullptr ? impl->format->pb : nullptr;
    return pb != nullptr ? avio_tell (pb) : 0;
}

std::string MediaWriter::encoderName (size_t track) const
{
    return track < impl->video.size() ? impl->video[track]->encoder.name : std::string();
}

int64_t MediaWriter::videoFrames (size_t track) const
{
    return track < impl->video.size() ? impl->video[track]->written : 0;
}
}
