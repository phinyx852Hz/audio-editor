// SPDX-License-Identifier: AGPL-3.0-only
#include "Codec.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>

namespace codec
{
namespace
{
    using Kind = Setting::Kind;

    // Bit depth codes: 32 is 32-bit float, 31 is 32-bit integer, 64 is 64-bit float.
    constexpr int int32 = 31, float32 = 32, float64 = 64;

    Setting choice (std::string id, std::string label, std::vector<std::pair<std::string, std::string>> choices, std::string def,
                    std::string showWhen = {})
    {
        Setting s;
        s.id = std::move (id);
        s.label = std::move (label);
        s.kind = Kind::choice;
        s.choices = std::move (choices);
        s.defaultValue = std::move (def);
        s.showWhen = std::move (showWhen);
        return s;
    }

    Setting numeric (std::string id, std::string label, double lo, double hi, double step, double def, std::string suffix = {},
                     std::string showWhen = {})
    {
        Setting s;
        s.id = std::move (id);
        s.label = std::move (label);
        s.kind = Kind::number;
        s.minimum = lo;
        s.maximum = hi;
        s.step = step;
        char buf[32];
        std::snprintf (buf, sizeof buf, "%g", def);
        s.defaultValue = buf;
        s.suffix = std::move (suffix);
        s.showWhen = std::move (showWhen);
        return s;
    }

    std::vector<std::pair<std::string, std::string>> kbps (std::initializer_list<int> list)
    {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto k : list)
            out.emplace_back (std::to_string (k), std::to_string (k) + " kbps");
        return out;
    }

    const AVCodec* encoderNamed (const std::string& name) { return name.empty() ? nullptr : avcodec_find_encoder_by_name (name.c_str()); }
    bool hasMuxer (const std::string& name) { return av_guess_format (name.c_str(), nullptr, nullptr) != nullptr; }

    // The sample rates and formats an encoder takes (FFmpeg 7.1 changed how to ask).
    std::vector<int> codecRates (const AVCodec* c)
    {
        std::vector<int> out;
        const int* rates = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
        int n = 0;
        if (avcodec_get_supported_config (nullptr, c, AV_CODEC_CONFIG_SAMPLE_RATE, 0, (const void**) &rates, &n) >= 0 && rates != nullptr)
            out.assign (rates, rates + n);
#else
        rates = c->supported_samplerates;
        for (; rates != nullptr && *rates != 0; ++rates)
            out.push_back (*rates);
#endif
        std::sort (out.begin(), out.end());
        return out;
    }

    std::vector<AVSampleFormat> codecSampleFormats (const AVCodec* c)
    {
        std::vector<AVSampleFormat> out;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
        const AVSampleFormat* fmts = nullptr;
        int n = 0;
        if (avcodec_get_supported_config (nullptr, c, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void**) &fmts, &n) >= 0 && fmts != nullptr)
            out.assign (fmts, fmts + n);
#else
        for (auto* f = c->sample_fmts; f != nullptr && *f != AV_SAMPLE_FMT_NONE; ++f)
            out.push_back (*f);
#endif
        return out;
    }

    std::string pcmEncoder (int bits, bool bigEndian, bool unsigned8)
    {
        const std::string e = bigEndian ? "be" : "le";
        switch (bits)
        {
            case 8:       return unsigned8 ? "pcm_u8" : "pcm_s8";
            case 16:      return "pcm_s16" + e;
            case 24:      return "pcm_s24" + e;
            case int32:   return "pcm_s32" + e;
            case float32: return "pcm_f32" + e;
            case float64: return "pcm_f64" + e;
            default:      return "pcm_s16" + e;
        }
    }

    // The encoder a job uses: PCM formats choose by depth.
    std::string encoderFor (const Format& f, int bits, const Settings& s)
    {
        if (f.encoder == "pcm-le")
            return pcmEncoder (bits, false, f.muxer != "caf");
        if (f.encoder == "pcm-be")
            return pcmEncoder (bits, true, false);
        if (f.encoder == "pcm-raw")
            return pcmEncoder (bits, s.count ("endian") > 0 && s.at ("endian") == "be", false);
        return f.encoder;
    }

    std::string muxerFor (const Format& f, int bits, const Settings& s)
    {
        if (f.encoder == "pcm-raw")
            return encoderFor (f, bits, s).substr (4);   // s16le, f32be, u8...
        return f.muxer;
    }

    // Whether an encoder and container really work together, by writing a header
    // to memory: the most channels that work (2 or 1), or 0.
    int probe (const AVCodec* c, const Format& f)
    {
        const auto rates = codecRates (c);
        const int rate = rates.empty() ? 44100 : (std::find (rates.begin(), rates.end(), 44100) != rates.end() ? 44100 : rates.back());
        for (const int channels : { 2, 1 })
        {
            auto* ctx = avcodec_alloc_context3 (c);
            ctx->sample_rate = rate;
            av_channel_layout_default (&ctx->ch_layout, channels);
            const auto fmts = codecSampleFormats (c);
            ctx->sample_fmt = fmts.empty() ? AV_SAMPLE_FMT_FLTP : fmts[0];
            ctx->time_base = { 1, rate };
            ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
            bool ok = false;
            if (avcodec_open2 (ctx, c, nullptr) >= 0)
            {
                AVFormatContext* fmt = nullptr;
                if (avformat_alloc_output_context2 (&fmt, nullptr, f.muxer.c_str(), nullptr) >= 0 && fmt != nullptr)
                {
                    if (auto* st = avformat_new_stream (fmt, nullptr))
                    {
                        avcodec_parameters_from_context (st->codecpar, ctx);
                        st->time_base = ctx->time_base;
                        if (avio_open_dyn_buf (&fmt->pb) >= 0)
                        {
                            ok = avformat_write_header (fmt, nullptr) >= 0;
                            uint8_t* buffer = nullptr;
                            avio_close_dyn_buf (fmt->pb, &buffer);
                            av_free (buffer);
                            fmt->pb = nullptr;
                        }
                    }
                    avformat_free_context (fmt);
                }
            }
            avcodec_free_context (&ctx);
            if (ok)
                return channels;
        }
        return 0;
    }

    std::vector<Format> build()
    {
        std::vector<Format> list;
        std::set<std::string> curated { "anull", "pcm_s16le", "pcm_s16be", "pcm_s24le", "pcm_s24be", "pcm_s32le", "pcm_s32be",
                                        "pcm_f32le", "pcm_f32be", "pcm_f64le", "pcm_f64be", "pcm_u8", "pcm_s8", "ac3_fixed", "mp2fixed",
                                        "libshine", "vorbis", "opus", "aac", "comfortnoise" };

        // Adds a format when FFmpeg has its encoder (first available of `encoders`) and muxer.
        const auto add = [&] (Format f, std::vector<std::string> encoders = {})
        {
            if (encoders.empty())
                encoders.push_back (f.encoder);
            for (const auto& e : encoders)
                curated.insert (e);
            if (f.encoder.compare (0, 4, "pcm-") == 0)
            {
                if (hasMuxer (f.muxer.empty() ? "s16le" : f.muxer))
                    list.push_back (std::move (f));
                return;
            }
            for (const auto& e : encoders)
                if (const auto* c = encoderNamed (e); c != nullptr && hasMuxer (f.muxer))
                {
                    f.encoder = e;
                    if (const auto rates = codecRates (c); ! rates.empty())
                    {
                        if (f.sampleRates.empty())
                            f.sampleRates = rates;
                        else
                            std::erase_if (f.sampleRates, [&] (int r) { return std::find (rates.begin(), rates.end(), r) == rates.end(); });
                    }
                    list.push_back (std::move (f));
                    return;
                }
        };

        {
            Format f;
            f.id = "wav"; f.name = "WAV (Broadcast Wave)"; f.group = "Uncompressed"; f.extension = "wav"; f.native = true;
            f.lossless = true; f.chapters = true; f.bitDepths = { 16, 24, float32 }; f.maxChannels = 8;
            f.note = "Markers as cue points and regions. Tags: RIFF INFO, BWF, cart, iXML, XMP and ID3. Over 4 GB becomes RF64.";
            list.push_back (f);
            f.id = "aiff"; f.name = "AIFF"; f.extension = "aiff";
            f.note = "Tags: name, author, copyright, annotation, ID3 and XMP.";
            f.chapters = false;
            list.push_back (f);
        }

        const std::vector<int> mpegRates { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };

        {
            Format f;
            f.id = "mp3"; f.name = "MP3"; f.group = "Lossy"; f.extension = "mp3"; f.muxer = "mp3"; f.encoder = "libmp3lame";
            f.taggedByTagLib = true; f.chapters = true; f.sampleRates = mpegRates; f.maxChannels = 2;
            f.settings = {
                choice ("mode", "Mode", { { "cbr", "Constant bit rate (CBR)" }, { "vbr", "Variable bit rate (VBR)" }, { "abr", "Average bit rate (ABR)" } }, "cbr"),
                choice ("bitrate", "Bit rate", kbps ({ 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 }), "320", "mode=cbr|abr"),
                choice ("quality", "Quality", { { "0", "V0 (about 245 kbps, best)" }, { "1", "V1 (225)" }, { "2", "V2 (190)" }, { "3", "V3 (175)" },
                                                { "4", "V4 (165)" }, { "5", "V5 (130)" }, { "6", "V6 (115)" }, { "7", "V7 (100)" }, { "8", "V8 (85)" },
                                                { "9", "V9 (65, smallest)" } }, "0", "mode=vbr"),
                choice ("stereo", "Stereo", { { "joint", "Joint stereo" }, { "stereo", "Plain stereo" } }, "joint"),
            };
            f.note = "LAME. Tags: ID3v2 and ID3v1, chapters from the markers.";
            add (f);
        }
        {
            Format f;
            f.id = "aac-m4a"; f.name = "AAC (M4A)"; f.group = "Lossy"; f.extension = "m4a"; f.muxer = "ipod";
            f.taggedByTagLib = true; f.chapters = true; f.maxChannels = 8;
            f.settings = { choice ("bitrate", "Bit rate", kbps ({ 64, 96, 128, 160, 192, 224, 256, 320 }), "256") };
            if (encoderNamed ("libfdk_aac") != nullptr)
                f.settings.push_back (choice ("profile", "Profile", { { "lc", "AAC-LC" }, { "he", "HE-AAC" }, { "hev2", "HE-AAC v2 (stereo)" } }, "lc"));
            f.note = "Tags: iTunes atoms and XMP, chapters from the markers.";
            add (f, { "libfdk_aac", "aac" });
        }
        {
            Format f;
            f.id = "aac-adts"; f.name = "AAC (raw .aac)"; f.group = "Lossy"; f.extension = "aac"; f.muxer = "adts"; f.maxChannels = 8;
            f.settings = { choice ("bitrate", "Bit rate", kbps ({ 64, 96, 128, 160, 192, 224, 256, 320 }), "256") };
            f.note = "A bare AAC stream: tags go in a small ID3 tag at the start.";
            add (f, { "libfdk_aac", "aac" });
        }
        {
            Format f;
            f.id = "opus"; f.name = "Opus"; f.group = "Lossy"; f.extension = "opus"; f.muxer = "opus";
            f.taggedByTagLib = true; f.chapters = true; f.sampleRates = { 48000 }; f.maxChannels = 8;
            f.settings = {
                numeric ("bitrate", "Bit rate", 6, 510, 2, 160, " kbps"),
                choice ("vbr", "Bit rate mode", { { "on", "Variable (VBR)" }, { "constrained", "Constrained VBR" }, { "off", "Constant (CBR)" } }, "on"),
                choice ("application", "Tuned for", { { "audio", "Music" }, { "voip", "Speech" }, { "lowdelay", "Low delay" } }, "audio"),
                numeric ("complexity", "Complexity", 0, 10, 1, 10),
            };
            f.note = "Always 48 kHz (converted when needed). Tags: Vorbis comments with pictures.";
            add (f, { "libopus", "opus" });
        }
        {
            Format f;
            f.id = "vorbis"; f.name = "Ogg Vorbis"; f.group = "Lossy"; f.extension = "ogg"; f.muxer = "ogg";
            f.taggedByTagLib = true; f.chapters = true; f.maxChannels = 8;
            f.settings = {
                choice ("mode", "Mode", { { "quality", "Quality" }, { "bitrate", "Bit rate" } }, "quality"),
                numeric ("quality", "Quality", -1, 10, 0.5, 6, {}, "mode=quality"),
                choice ("bitrate", "Bit rate", kbps ({ 64, 96, 128, 160, 192, 224, 256, 320, 400, 500 }), "192", "mode=bitrate"),
            };
            add (f, { "libvorbis" });
        }
        {
            Format f;
            f.id = "flac"; f.encoder = "flac"; f.name = "FLAC"; f.group = "Lossless"; f.extension = "flac"; f.muxer = "flac";
            f.lossless = true; f.taggedByTagLib = true; f.bitDepths = { 16, 24 }; f.maxChannels = 8;
            f.settings = { numeric ("compression", "Compression", 0, 12, 1, 8) };
            f.note = "Tags: Vorbis comments with pictures; XMP in a .xmp file beside it.";
            add (f);
        }
        {
            Format f;
            f.id = "alac"; f.encoder = "alac"; f.name = "Apple Lossless (ALAC, M4A)"; f.group = "Lossless"; f.extension = "m4a"; f.muxer = "ipod";
            f.lossless = true; f.taggedByTagLib = true; f.chapters = true; f.bitDepths = { 16, 24 }; f.maxChannels = 8;
            add (f);
        }
        {
            Format f;
            f.id = "wavpack"; f.encoder = "wavpack"; f.name = "WavPack"; f.group = "Lossless"; f.extension = "wv"; f.muxer = "wv";
            f.lossless = true; f.taggedByTagLib = true; f.bitDepths = { 16, 24, float32 }; f.maxChannels = 8;
            f.settings = { numeric ("compression", "Compression", 0, 8, 1, 3) };
            f.note = "Tags: APEv2 with pictures.";
            add (f);
        }
        {
            Format f;
            f.id = "tta"; f.encoder = "tta"; f.name = "True Audio (TTA)"; f.group = "Lossless"; f.extension = "tta"; f.muxer = "tta";
            f.lossless = true; f.taggedByTagLib = true; f.bitDepths = { 16, 24 }; f.maxChannels = 8;
            add (f);
        }
        {
            Format f;
            f.id = "mka-flac"; f.name = "Matroska Audio (FLAC inside)"; f.group = "Lossless"; f.extension = "mka"; f.muxer = "matroska";
            f.encoder = "flac"; f.lossless = true; f.chapters = true; f.bitDepths = { 16, 24 }; f.maxChannels = 8;
            f.settings = { numeric ("compression", "Compression", 0, 12, 1, 8) };
            f.note = "Chapters from the markers.";
            add (f);
        }
        {
            Format f;
            f.id = "mka-opus"; f.name = "Matroska Audio (Opus inside)"; f.group = "Lossy"; f.extension = "mka"; f.muxer = "matroska";
            f.chapters = true; f.sampleRates = { 48000 }; f.maxChannels = 8;
            f.settings = { numeric ("bitrate", "Bit rate", 6, 510, 2, 160, " kbps") };
            add (f, { "libopus", "opus" });
        }
        {
            Format f;
            f.id = "webm"; f.name = "WebM Audio (Opus)"; f.group = "Lossy"; f.extension = "webm"; f.muxer = "webm";
            f.chapters = true; f.sampleRates = { 48000 }; f.maxChannels = 8;
            f.settings = { numeric ("bitrate", "Bit rate", 6, 510, 2, 160, " kbps") };
            add (f, { "libopus", "opus" });
        }
        {
            Format f;
            f.id = "ac3"; f.name = "Dolby Digital (AC-3)"; f.group = "Broadcast and film"; f.extension = "ac3"; f.muxer = "ac3";
            f.sampleRates = { 32000, 44100, 48000 }; f.maxChannels = 6;
            f.settings = { choice ("bitrate", "Bit rate", kbps ({ 96, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640 }), "448") };
            add (f, { "ac3", "ac3_fixed" });
        }
        {
            Format f;
            f.id = "eac3"; f.encoder = "eac3"; f.name = "Dolby Digital Plus (E-AC-3)"; f.group = "Broadcast and film"; f.extension = "eac3"; f.muxer = "eac3";
            f.sampleRates = { 32000, 44100, 48000 }; f.maxChannels = 6;
            f.settings = { choice ("bitrate", "Bit rate", kbps ({ 96, 128, 192, 224, 256, 320, 384, 448, 640, 768, 1024 }), "448") };
            add (f);
        }
        {
            Format f;
            f.id = "dts"; f.name = "DTS (Coherent Acoustics)"; f.group = "Broadcast and film"; f.extension = "dts"; f.muxer = "dts";
            f.maxChannels = 6;
            f.settings = { choice ("bitrate", "Bit rate", kbps ({ 768, 1152, 1411, 1536 }), "1411") };
            f.note = "FFmpeg's DTS encoder is marked experimental.";
            add (f, { "dca" });
        }
        {
            Format f;
            f.id = "truehd"; f.encoder = "truehd"; f.name = "Dolby TrueHD"; f.group = "Broadcast and film"; f.extension = "thd"; f.muxer = "truehd";
            f.lossless = true; f.bitDepths = { 16, 24 }; f.maxChannels = 8;
            f.note = "Lossless. FFmpeg's TrueHD encoder is marked experimental.";
            add (f);
        }
        {
            Format f;
            f.id = "mp2"; f.name = "MPEG Layer II (MP2)"; f.group = "Broadcast and film"; f.extension = "mp2"; f.muxer = "mp2";
            f.taggedByTagLib = true; f.maxChannels = 2;
            f.settings = { choice ("bitrate", "Bit rate", kbps ({ 64, 96, 128, 160, 192, 224, 256, 320, 384 }), "256") };
            f.note = "For broadcast playout systems.";
            add (f, { "libtwolame", "mp2" });
        }
        {
            Format f;
            f.id = "wma"; f.name = "Windows Media Audio 2"; f.group = "Legacy and games"; f.extension = "wma"; f.muxer = "asf";
            f.taggedByTagLib = true; f.maxChannels = 2;
            f.settings = { choice ("bitrate", "Bit rate", kbps ({ 32, 48, 64, 96, 128, 160, 192, 256 }), "192") };
            add (f, { "wmav2" });
        }
        {
            Format f;
            f.id = "wma1"; f.name = "Windows Media Audio 1"; f.group = "Legacy and games"; f.extension = "wma"; f.muxer = "asf";
            f.taggedByTagLib = true; f.maxChannels = 2;
            f.settings = { choice ("bitrate", "Bit rate", kbps ({ 32, 48, 64, 96, 128, 160, 192 }), "128") };
            add (f, { "wmav1" });
        }
        {
            Format f;
            f.id = "speex"; f.name = "Speex"; f.group = "Speech and telephony"; f.extension = "spx"; f.muxer = "ogg";
            f.taggedByTagLib = true; f.sampleRates = { 8000, 16000, 32000 }; f.maxChannels = 2;
            f.settings = { numeric ("quality", "Quality", 0, 10, 1, 8) };
            add (f, { "libspeex" });
        }
        {
            Format f;
            f.id = "amrnb"; f.name = "AMR-NB (narrowband)"; f.group = "Speech and telephony"; f.extension = "amr"; f.muxer = "amr";
            f.sampleRates = { 8000 }; f.maxChannels = 1;
            f.settings = { choice ("bitrate", "Bit rate", { { "4750", "4.75 kbps" }, { "5150", "5.15 kbps" }, { "5900", "5.9 kbps" },
                                                            { "6700", "6.7 kbps" }, { "7400", "7.4 kbps" }, { "7950", "7.95 kbps" },
                                                            { "10200", "10.2 kbps" }, { "12200", "12.2 kbps" } }, "12200") };
            add (f, { "libopencore_amrnb" });
        }
        {
            Format f;
            f.id = "amrwb"; f.name = "AMR-WB (wideband)"; f.group = "Speech and telephony"; f.extension = "awb"; f.muxer = "amr";
            f.sampleRates = { 16000 }; f.maxChannels = 1;
            f.settings = { choice ("bitrate", "Bit rate", { { "6600", "6.6 kbps" }, { "8850", "8.85 kbps" }, { "12650", "12.65 kbps" },
                                                            { "14250", "14.25 kbps" }, { "15850", "15.85 kbps" }, { "18250", "18.25 kbps" },
                                                            { "19850", "19.85 kbps" }, { "23050", "23.05 kbps" }, { "23850", "23.85 kbps" } }, "23850") };
            add (f, { "libvo_amrwbenc" });
        }
        {
            Format f;
            f.id = "gsm"; f.name = "GSM 06.10"; f.group = "Speech and telephony"; f.extension = "gsm"; f.muxer = "gsm";
            f.sampleRates = { 8000 }; f.maxChannels = 1;
            add (f, { "libgsm" });
        }
        {
            Format f;
            f.id = "gsm-wav"; f.name = "GSM 06.10 (WAV)"; f.group = "Speech and telephony"; f.extension = "wav"; f.muxer = "wav";
            f.sampleRates = { 8000 }; f.maxChannels = 1;
            add (f, { "libgsm_ms" });
        }
        {
            Format f;
            f.id = "alaw"; f.name = "G.711 A-law (WAV)"; f.group = "Speech and telephony"; f.extension = "wav"; f.muxer = "wav"; f.maxChannels = 2;
            add (f, { "pcm_alaw" });
            f.id = "mulaw"; f.name = "G.711 \xC2\xB5-law (WAV)";
            add (f, { "pcm_mulaw" });
        }
        {
            Format f;
            f.id = "g722"; f.name = "G.722 (WAV)"; f.group = "Speech and telephony"; f.extension = "wav"; f.muxer = "wav";
            f.sampleRates = { 16000 }; f.maxChannels = 1;
            add (f, { "g722" });
        }
        {
            Format f;
            f.id = "g726"; f.name = "G.726 ADPCM (WAV)"; f.group = "Speech and telephony"; f.extension = "wav"; f.muxer = "wav";
            f.sampleRates = { 8000 }; f.maxChannels = 1;
            f.settings = { choice ("bitrate", "Bit rate", { { "16000", "16 kbps" }, { "24000", "24 kbps" }, { "32000", "32 kbps" }, { "40000", "40 kbps" } }, "32000") };
            add (f, { "g726" });
        }
        {
            Format f;
            f.id = "codec2"; f.name = "Codec 2"; f.group = "Speech and telephony"; f.extension = "c2"; f.muxer = "codec2";
            f.sampleRates = { 8000 }; f.maxChannels = 1;
            f.settings = { choice ("mode", "Bit rate", { { "3200", "3200 bps" }, { "2400", "2400 bps" }, { "1600", "1600 bps" }, { "1400", "1400 bps" },
                                                         { "1300", "1300 bps" }, { "1200", "1200 bps" }, { "700C", "700 bps" } }, "3200") };
            add (f, { "libcodec2" });
        }
        {
            Format f;
            f.group = "Legacy and games"; f.extension = "wav"; f.muxer = "wav"; f.maxChannels = 2;
            f.id = "ima-wav"; f.name = "IMA ADPCM (WAV)";
            add (f, { "adpcm_ima_wav" });
            f.id = "ms-adpcm"; f.name = "Microsoft ADPCM (WAV)";
            add (f, { "adpcm_ms" });
            f.id = "yamaha-adpcm"; f.name = "Yamaha ADPCM (WAV)";
            add (f, { "adpcm_yamaha" });
            f.id = "ima-qt"; f.name = "IMA ADPCM (AIFF-C)"; f.extension = "aifc"; f.muxer = "aiff";
            add (f, { "adpcm_ima_qt" });
            f.id = "adx"; f.name = "CRI ADX"; f.extension = "adx"; f.muxer = "adx";
            add (f, { "adpcm_adx" });
        }
        {
            Format f;
            f.group = "Bluetooth"; f.maxChannels = 2;
            f.id = "aptx"; f.name = "aptX"; f.extension = "aptx"; f.muxer = "aptx";
            add (f, { "aptx" });
            f.id = "aptx-hd"; f.name = "aptX HD"; f.extension = "aptxhd"; f.muxer = "aptx_hd";
            add (f, { "aptx_hd" });
            f.id = "sbc"; f.name = "SBC"; f.extension = "sbc"; f.muxer = "sbc";
            add (f, { "sbc" });
        }
        {
            Format f;
            f.group = "Uncompressed"; f.lossless = true; f.maxChannels = 8;
            f.id = "w64"; f.name = "Sony Wave64 (W64)"; f.extension = "w64"; f.muxer = "w64"; f.encoder = "pcm-le";
            f.bitDepths = { 8, 16, 24, int32, float32, float64 };
            add (f);
            f.id = "caf"; f.name = "Core Audio Format (CAF)"; f.extension = "caf"; f.muxer = "caf"; f.encoder = "pcm-be";
            add (f);
            f.id = "au"; f.name = "Sun / NeXT AU"; f.extension = "au"; f.muxer = "au"; f.encoder = "pcm-be";
            f.bitDepths = { 8, 16, 24, int32, float32, float64 };
            add (f);
            f.id = "voc"; f.name = "Creative Voice (VOC)"; f.extension = "voc"; f.muxer = "voc"; f.encoder = "pcm-le"; f.bitDepths = { 8, 16 };
            f.maxChannels = 2;
            add (f);
            f.id = "raw"; f.name = "Raw PCM (no header)"; f.extension = "raw"; f.muxer = "s16le"; f.encoder = "pcm-raw";
            f.bitDepths = { 8, 16, 24, int32, float32, float64 }; f.maxChannels = 8;
            f.settings = { choice ("endian", "Byte order", { { "le", "Little-endian" }, { "be", "Big-endian" } }, "le") };
            add (f);
        }

        // Every other audio encoder, in the first container that takes it.
        void* it = nullptr;
        std::vector<Format> others;
        // Trying each encoder makes FFmpeg complain about the ones that do not fit.
        const auto logLevel = av_log_get_level();
        av_log_set_level (AV_LOG_QUIET);
        while (const auto* c = av_codec_iterate (&it))
        {
            if (c->type != AVMEDIA_TYPE_AUDIO || ! av_codec_is_encoder (c) || curated.count (c->name) > 0)
                continue;
            if (std::string (c->name).compare (0, 4, "pcm_") == 0)
                continue;   // uncompressed formats above cover these
            Format f;
            f.id = std::string ("ffmpeg:") + c->name;
            f.name = std::string (c->long_name != nullptr ? c->long_name : c->name) + " (" + c->name + ")";
            f.group = "Other FFmpeg encoders";
            f.encoder = c->name;
            f.sampleRates = codecRates (c);
            for (const auto& [muxer, ext] : { std::pair<const char*, const char*> { "matroska", "mka" }, { "nut", "nut" }, { "wav", "wav" }, { "caf", "caf" } })
            {
                const auto* of = av_guess_format (muxer, nullptr, nullptr);
                if (of != nullptr && avformat_query_codec (of, c->id, FF_COMPLIANCE_EXPERIMENTAL) == 1)
                {
                    f.muxer = muxer;
                    f.extension = ext;
                    if (probe (c, f) > 0)
                        break;
                    f.muxer.clear();
                }
            }
            if (f.muxer.empty() || ! (f.maxChannels = probe (c, f)))
                continue;
            if ((c->capabilities & AV_CODEC_CAP_EXPERIMENTAL) != 0)
                f.note = "Marked experimental in FFmpeg.";
            others.push_back (std::move (f));
        }
        av_log_set_level (logLevel);
        std::sort (others.begin(), others.end(), [] (const auto& a, const auto& b) { return a.name < b.name; });
        list.insert (list.end(), others.begin(), others.end());
        return list;
    }

    void setOpt (AVDictionary** d, const char* key, const std::string& value) { av_dict_set (d, key, value.c_str(), 0); }

    std::string get (const Settings& s, const Format& f, const std::string& id)
    {
        if (const auto it = s.find (id); it != s.end())
            return it->second;
        for (const auto& setting : f.settings)
            if (setting.id == id)
                return setting.defaultValue;
        return {};
    }

    // Puts a format's settings on the encoder.
    void applySettings (const Format& f, const Settings& s, AVCodecContext* ctx, AVDictionary** opts)
    {
        const auto value = [&] (const char* id) { return get (s, f, id); };
        const auto kilobits = [&] (const char* id) { return (int64_t) std::llround (std::atof (value (id).c_str()) * 1000.0); };

        if (f.id == "mp3")
        {
            const auto mode = value ("mode");
            if (mode == "vbr")
            {
                ctx->flags |= AV_CODEC_FLAG_QSCALE;
                ctx->global_quality = FF_QP2LAMBDA * std::atoi (value ("quality").c_str());
            }
            else
            {
                ctx->bit_rate = kilobits ("bitrate");
                if (mode == "abr")
                    setOpt (opts, "abr", "1");
            }
            setOpt (opts, "joint_stereo", value ("stereo") == "joint" ? "1" : "0");
        }
        else if (f.id == "vorbis" && value ("mode") == "quality")
        {
            ctx->flags |= AV_CODEC_FLAG_QSCALE;
            ctx->global_quality = (int) std::lround (FF_QP2LAMBDA * std::atof (value ("quality").c_str()));
        }
        else if (f.id == "speex")
        {
            ctx->flags |= AV_CODEC_FLAG_QSCALE;
            ctx->global_quality = FF_QP2LAMBDA * std::atoi (value ("quality").c_str());
        }
        else if (f.id == "opus")
        {
            ctx->bit_rate = kilobits ("bitrate");
            setOpt (opts, "vbr", value ("vbr"));
            setOpt (opts, "application", value ("application"));
            ctx->compression_level = std::atoi (value ("complexity").c_str());
        }
        else if (f.id == "codec2")
            setOpt (opts, "mode", value ("mode"));
        else if (f.id == "amrnb" || f.id == "amrwb" || f.id == "g726")
            ctx->bit_rate = std::atoi (value ("bitrate").c_str());
        else if (! value ("bitrate").empty())
            ctx->bit_rate = kilobits ("bitrate");

        if (f.id == "aac-m4a" || f.id == "aac-adts")
        {
            const auto profile = value ("profile");
            if (profile == "he")
                ctx->profile = 4;    // AAC HE (libfdk_aac: aot 5)
            else if (profile == "hev2")
                ctx->profile = 28;   // AAC HE v2
        }
        if (! value ("compression").empty())
            ctx->compression_level = std::atoi (value ("compression").c_str());
    }

    // Converts float blocks to an encoder frame.
    void fill (AVFrame* frame, const std::vector<std::vector<float>>& block, int offset, int n, int channels)
    {
        const auto fmt = (AVSampleFormat) frame->format;
        const bool planar = av_sample_fmt_is_planar (fmt) != 0;
        const auto packed = av_get_packed_sample_fmt (fmt);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = block[(size_t) ch].data() + offset;
            for (int i = 0; i < n; ++i)
            {
                const auto x = std::clamp (src[i], -1.0f, 1.0f);
                const auto index = planar ? i : i * channels + ch;
                auto* plane = frame->data[planar ? ch : 0];
                switch (packed)
                {
                    case AV_SAMPLE_FMT_FLT: reinterpret_cast<float*> (plane)[index] = src[i]; break;
                    case AV_SAMPLE_FMT_DBL: reinterpret_cast<double*> (plane)[index] = src[i]; break;
                    case AV_SAMPLE_FMT_S16: reinterpret_cast<int16_t*> (plane)[index] = (int16_t) std::lrint (std::clamp (x * 32768.0f, -32768.0f, 32767.0f)); break;
                    case AV_SAMPLE_FMT_S32: reinterpret_cast<int32_t*> (plane)[index] = (int32_t) std::clamp<double> (std::llround ((double) x * 2147483648.0), -2147483648.0, 2147483647.0); break;
                    case AV_SAMPLE_FMT_S64: reinterpret_cast<int64_t*> (plane)[index] = (int64_t) std::llround ((double) x * 2147483648.0) << 32; break;
                    case AV_SAMPLE_FMT_U8:  plane[index] = (uint8_t) std::clamp (std::lrint (x * 128.0f) + 128L, 0L, 255L); break;
                    default: break;
                }
            }
        }
    }

    AVSampleFormat chooseSampleFormat (const AVCodec* c, const Format& f, int bits)
    {
        const auto formats = codecSampleFormats (c);
        if (formats.empty())
            return AV_SAMPLE_FMT_FLTP;
        const auto has = [&] (AVSampleFormat x) { return std::find (formats.begin(), formats.end(), x) != formats.end(); };
        std::vector<AVSampleFormat> order;
        if (f.lossless || ! f.bitDepths.empty())
        {
            if (bits == float32 || bits == float64)
                order = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP };
            else if (bits > 16)
                order = { AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT };
            else if (bits == 8)
                order = { AV_SAMPLE_FMT_U8, AV_SAMPLE_FMT_U8P, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P };
            else
                order = { AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_S32 };
        }
        else
            order = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S16 };
        for (const auto x : order)
            if (has (x))
                return x;
        return formats[0];
    }

    std::string avError (int code)
    {
        char buf[AV_ERROR_MAX_STRING_SIZE] {};
        av_strerror (code, buf, sizeof buf);
        return buf;
    }

    // FFmpeg's names for the common keys, for containers TagLib does not tag.
    const std::pair<const char*, const char*> ffmpegKeys[] {
        { tag::title, "title" }, { tag::artist, "artist" }, { tag::album, "album" }, { tag::albumArtist, "album_artist" },
        { tag::composer, "composer" }, { tag::genre, "genre" }, { tag::date, "date" }, { tag::track, "track" }, { tag::disc, "disc" },
        { tag::comment, "comment" }, { tag::copyright, "copyright" }, { tag::publisher, "publisher" }, { tag::language, "language" },
        { tag::lyrics, "lyrics" }, { tag::isrc, "ISRC" }, { tag::bpm, "BPM" },
    };
}

const std::vector<Format>& formats()
{
    static const std::vector<Format> list = build();
    return list;
}

const Format* find (const std::string& id)
{
    for (const auto& f : formats())
        if (f.id == id)
            return &f;
    return nullptr;
}

std::vector<std::string> missingFormats()
{
    std::vector<std::string> out;
    const std::pair<const char*, const char*> wanted[] {
        { "mp3", "MP3 (FFmpeg was built without LAME)" }, { "opus", "Opus" }, { "vorbis", "Ogg Vorbis (FFmpeg was built without libvorbis)" },
        { "aac-m4a", "AAC" }, { "speex", "Speex" }, { "amrnb", "AMR-NB (needs FFmpeg with opencore-amr)" },
        { "amrwb", "AMR-WB (needs FFmpeg with vo-amrwbenc)" }, { "codec2", "Codec 2" }, { "gsm", "GSM 06.10" },
    };
    for (const auto& [id, name] : wanted)
        if (find (id) == nullptr)
            out.emplace_back (name);
    if (avcodec_find_encoder_by_name ("libfdk_aac") == nullptr)
        out.emplace_back ("HE-AAC (needs FFmpeg with the non-free libfdk-aac)");
    out.emplace_back ("Monkey's Audio, OptimFROG, TAK, Musepack, MQA (no encoder on Linux in FFmpeg)");
    out.emplace_back ("DSD (DSF, DSDIFF): planned, with PHINYX's own modulator");
    return out;
}

std::string ffmpegVersion() { return av_version_info(); }

int rateFor (const Format& f, double rate)
{
    const auto r = (int) std::lround (rate);
    if (f.sampleRates.empty() || std::find (f.sampleRates.begin(), f.sampleRates.end(), r) != f.sampleRates.end())
        return r;
    for (const auto candidate : f.sampleRates)
        if (candidate >= r)
            return candidate;
    return f.sampleRates.back();
}

bool isShown (const Setting& s, const Settings& values)
{
    if (s.showWhen.empty())
        return true;
    const auto eq = s.showWhen.find ('=');
    const auto it = values.find (s.showWhen.substr (0, eq));
    const auto wanted = "|" + s.showWhen.substr (eq + 1) + "|";
    return it != values.end() && wanted.find ("|" + it->second + "|") != std::string::npos;
}

Settings defaults (const Format& f)
{
    Settings s;
    for (const auto& setting : f.settings)
        s[setting.id] = setting.defaultValue;
    return s;
}

bool encode (const AudioData& data, Frame start, Frame end, const std::string& path, const Job& job, std::string& error)
{
    if (job.format == nullptr)
        return error = "No format chosen", false;
    const auto& f = *job.format;
    const auto encoderName = encoderFor (f, job.bits, job.settings);
    const auto* c = encoderNamed (encoderName);
    if (c == nullptr)
        return error = "FFmpeg has no " + encoderName + " encoder", false;

    const auto channels = data.numChannels();
    const auto rate = (int) std::lround (data.sampleRate());
    if (channels > f.maxChannels)
        return error = f.name + " holds at most " + std::to_string (f.maxChannels) + " channel" + (f.maxChannels > 1 ? "s" : ""), false;

    AVFormatContext* fmtRaw = nullptr;
    const auto muxer = muxerFor (f, job.bits, job.settings);
    if (avformat_alloc_output_context2 (&fmtRaw, nullptr, muxer.c_str(), path.c_str()) < 0 || fmtRaw == nullptr)
        return error = "FFmpeg cannot write " + muxer, false;
    std::unique_ptr<AVFormatContext, void (*) (AVFormatContext*)> fmt (fmtRaw, [] (AVFormatContext* x)
    {
        if (x->pb != nullptr && (x->oformat->flags & AVFMT_NOFILE) == 0)
            avio_closep (&x->pb);
        avformat_free_context (x);
    });

    auto* stream = avformat_new_stream (fmt.get(), nullptr);
    std::unique_ptr<AVCodecContext, void (*) (AVCodecContext*)> ctx (avcodec_alloc_context3 (c), [] (AVCodecContext* x) { avcodec_free_context (&x); });
    if (stream == nullptr || ctx == nullptr)
        return error = "Out of memory", false;

    ctx->sample_rate = rate;
    av_channel_layout_default (&ctx->ch_layout, channels);
    ctx->sample_fmt = chooseSampleFormat (c, f, job.bits);
    if (job.bits == 24)
        ctx->bits_per_raw_sample = 24;
    ctx->time_base = { 1, rate };
    ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    if ((fmt->oformat->flags & AVFMT_GLOBALHEADER) != 0)
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary* optsRaw = nullptr;
    applySettings (f, job.settings, ctx.get(), &optsRaw);
    const auto opened = avcodec_open2 (ctx.get(), c, &optsRaw);
    av_dict_free (&optsRaw);
    if (opened < 0)
        return error = f.name + ": " + avError (opened) + " (" + std::to_string (channels) + " channels at " + std::to_string (rate) + " Hz)", false;

    avcodec_parameters_from_context (stream->codecpar, ctx.get());
    stream->time_base = ctx->time_base;

    // Tags for containers TagLib does not reach; chapters from the markers.
    if (! f.taggedByTagLib)
        for (const auto& [key, name] : ffmpegKeys)
            if (const auto it = job.tags.find (key); it != job.tags.end() && ! it->second.empty())
                av_dict_set (&fmt->metadata, name, it->second.c_str(), 0);
    if (f.chapters && f.id != "mp3" && f.id != "opus" && f.id != "vorbis")
    {
        for (size_t i = 0; i < job.chapters.size(); ++i)
        {
            const auto& m = job.chapters[i];
            const auto from = std::clamp<Frame> (m.start, start, end) - start;
            const auto to = (m.isRegion() ? std::clamp<Frame> (m.end, start, end) : (i + 1 < job.chapters.size() ? std::clamp<Frame> (job.chapters[i + 1].start, start, end) : end)) - start;
            auto* chapter = (AVChapter*) av_mallocz (sizeof (AVChapter));
            chapter->id = (int64_t) i + 1;
            chapter->time_base = { 1, rate };
            chapter->start = from;
            chapter->end = std::max (from, to);
            av_dict_set (&chapter->metadata, "title", m.name.c_str(), 0);
            int count = (int) fmt->nb_chapters;
            av_dynarray_add (&fmt->chapters, &count, chapter);
            fmt->nb_chapters = (unsigned int) count;
        }
    }

    AVDictionary* muxOpts = nullptr;
    if (f.id == "aac-adts")
        av_dict_set (&muxOpts, "write_id3v2", "1", 0);
    if ((fmt->oformat->flags & AVFMT_NOFILE) == 0)
        if (const auto r = avio_open (&fmt->pb, path.c_str(), AVIO_FLAG_WRITE); r < 0)
            return error = "Cannot write " + path + ": " + avError (r), false;
    if (const auto r = avformat_write_header (fmt.get(), &muxOpts); r < 0)
    {
        av_dict_free (&muxOpts);
        return error = "Cannot start " + path + ": " + avError (r), false;
    }
    av_dict_free (&muxOpts);

    std::unique_ptr<AVFrame, void (*) (AVFrame*)> frame (av_frame_alloc(), [] (AVFrame* x) { av_frame_free (&x); });
    std::unique_ptr<AVPacket, void (*) (AVPacket*)> packet (av_packet_alloc(), [] (AVPacket* x) { av_packet_free (&x); });
    const bool variable = (c->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0 || ctx->frame_size <= 0;
    const int frameSize = variable ? 4096 : ctx->frame_size;
    const bool smallLast = variable || (c->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME) != 0;

    const auto drain = [&] () -> bool
    {
        for (;;)
        {
            const auto r = avcodec_receive_packet (ctx.get(), packet.get());
            if (r == AVERROR (EAGAIN) || r == AVERROR_EOF)
                return true;
            if (r < 0)
                return error = "Encoding failed: " + avError (r), false;
            av_packet_rescale_ts (packet.get(), ctx->time_base, stream->time_base);
            packet->stream_index = stream->index;
            if (const auto w = av_interleaved_write_frame (fmt.get(), packet.get()); w < 0)
                return error = "Writing failed: " + avError (w), false;
        }
    };

    std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> ((size_t) frameSize));
    std::vector<float*> ptrs;
    for (auto& b : block)
        ptrs.push_back (b.data());

    int64_t pts = 0;
    bool ok = true;
    for (Frame pos = start; pos < end && ok; pos += frameSize)
    {
        if (job.cancel != nullptr && job.cancel->load())
        {
            error = "Cancelled";
            ok = false;
            break;
        }
        const auto n = (int) std::min<Frame> (frameSize, end - pos);
        data.read (pos, n, ptrs.data());
        const auto frameLength = n < frameSize && ! smallLast ? frameSize : n;
        for (auto& b : block)
            std::fill (b.begin() + n, b.end(), 0.0f);

        frame->nb_samples = frameLength;
        frame->format = ctx->sample_fmt;
        frame->sample_rate = rate;
        av_channel_layout_copy (&frame->ch_layout, &ctx->ch_layout);
        if (av_frame_get_buffer (frame.get(), 0) < 0 || av_frame_make_writable (frame.get()) < 0)
            return error = "Out of memory", false;
        fill (frame.get(), block, 0, frameLength, channels);
        frame->pts = pts;
        pts += frameLength;

        if (const auto r = avcodec_send_frame (ctx.get(), frame.get()); r < 0)
            ok = false, error = "Encoding failed: " + avError (r);
        av_frame_unref (frame.get());
        ok = ok && drain();
        if (job.progress != nullptr)
            job.progress->store ((float) (pos + n - start) / (float) std::max<Frame> (1, end - start));
    }

    if (ok)
    {
        avcodec_send_frame (ctx.get(), nullptr);
        ok = drain();
    }
    if (const auto r = av_write_trailer (fmt.get()); ok && r < 0)
        ok = false, error = "Finishing failed: " + avError (r);
    return ok;
}

AudioDataPtr decode (const std::string& path, std::string& error, std::atomic<float>* progress, const std::atomic<bool>* cancel,
                     std::vector<Marker>* chapters)
{
    AVFormatContext* fmtRaw = nullptr;
    if (const auto r = avformat_open_input (&fmtRaw, path.c_str(), nullptr, nullptr); r < 0)
        return error = "Cannot open " + path + ": " + avError (r), nullptr;
    std::unique_ptr<AVFormatContext, void (*) (AVFormatContext*)> fmt (fmtRaw, [] (AVFormatContext* x) { avformat_close_input (&x); });
    if (avformat_find_stream_info (fmt.get(), nullptr) < 0)
        return error = "Cannot read " + path, nullptr;

    const AVCodec* c = nullptr;
    const auto index = av_find_best_stream (fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &c, 0);
    if (index < 0 || c == nullptr)
        return error = "No audio in " + path, nullptr;
    auto* stream = fmt->streams[index];

    std::unique_ptr<AVCodecContext, void (*) (AVCodecContext*)> ctx (avcodec_alloc_context3 (c), [] (AVCodecContext* x) { avcodec_free_context (&x); });
    avcodec_parameters_to_context (ctx.get(), stream->codecpar);
    ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    if (const auto r = avcodec_open2 (ctx.get(), c, nullptr); r < 0)
        return error = "Cannot decode " + path + ": " + avError (r), nullptr;

    const auto channels = std::max (1, ctx->ch_layout.nb_channels);
    const auto rate = ctx->sample_rate;
    if (rate <= 0)
        return error = "No sample rate in " + path, nullptr;

    // Resampling only changes the sample format here (to planar float), never the rate.
    SwrContext* swrRaw = nullptr;
    AVChannelLayout layout {};
    av_channel_layout_copy (&layout, &ctx->ch_layout);
    if (layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default (&layout, channels);
    if (swr_alloc_set_opts2 (&swrRaw, &layout, AV_SAMPLE_FMT_FLTP, rate, &layout, ctx->sample_fmt, rate, 0, nullptr) < 0 || swr_init (swrRaw) < 0)
    {
        av_channel_layout_uninit (&layout);
        swr_free (&swrRaw);
        return error = "Cannot convert the samples of " + path, nullptr;
    }
    av_channel_layout_uninit (&layout);
    std::unique_ptr<SwrContext, void (*) (SwrContext*)> swr (swrRaw, [] (SwrContext* x) { swr_free (&x); });

    if (chapters != nullptr)
        for (unsigned int i = 0; i < fmt->nb_chapters; ++i)
        {
            const auto* ch = fmt->chapters[i];
            Marker m;
            m.id = (int) i + 1;
            m.start = (Frame) av_rescale_q (ch->start, ch->time_base, AVRational { 1, rate });
            m.end = m.start;
            const auto* title = av_dict_get (ch->metadata, "title", nullptr, 0);
            m.name = title != nullptr ? title->value : "Chapter " + std::to_string (i + 1);
            chapters->push_back (m);
        }

    AudioData::Builder builder (channels, rate);
    std::unique_ptr<AVFrame, void (*) (AVFrame*)> frame (av_frame_alloc(), [] (AVFrame* x) { av_frame_free (&x); });
    std::unique_ptr<AVPacket, void (*) (AVPacket*)> packet (av_packet_alloc(), [] (AVPacket* x) { av_packet_free (&x); });
    std::vector<std::vector<float>> out ((size_t) channels);
    std::vector<uint8_t*> outPtrs ((size_t) channels);
    const auto duration = fmt->duration > 0 ? (double) fmt->duration / AV_TIME_BASE : 0.0;
    double decoded = 0.0;

    const auto take = [&] () -> bool
    {
        for (;;)
        {
            const auto r = avcodec_receive_frame (ctx.get(), frame.get());
            if (r == AVERROR (EAGAIN) || r == AVERROR_EOF)
                return true;
            if (r < 0)
                return error = "Decoding failed: " + avError (r), false;
            const auto room = swr_get_out_samples (swr.get(), frame->nb_samples);
            for (int ch = 0; ch < channels; ++ch)
            {
                out[(size_t) ch].resize ((size_t) std::max (room, 1));
                outPtrs[(size_t) ch] = reinterpret_cast<uint8_t*> (out[(size_t) ch].data());
            }
            const auto n = swr_convert (swr.get(), outPtrs.data(), room, (const uint8_t**) frame->extended_data, frame->nb_samples);
            if (n > 0)
            {
                std::vector<const float*> ptrs;
                for (auto& o : out)
                    ptrs.push_back (o.data());
                builder.append (ptrs.data(), n);
                decoded += (double) n / rate;
            }
            av_frame_unref (frame.get());
        }
    };

    while (av_read_frame (fmt.get(), packet.get()) >= 0)
    {
        if (cancel != nullptr && cancel->load())
        {
            av_packet_unref (packet.get());
            return error = "Cancelled", nullptr;
        }
        if (packet->stream_index == index)
        {
            avcodec_send_packet (ctx.get(), packet.get());
            if (! take())
            {
                av_packet_unref (packet.get());
                return nullptr;
            }
            if (progress != nullptr && duration > 0.0)
                progress->store ((float) std::min (1.0, decoded / duration));
        }
        av_packet_unref (packet.get());
    }
    avcodec_send_packet (ctx.get(), nullptr);
    if (! take())
        return nullptr;
    return builder.finish();
}

std::vector<std::string> readableExtensions()
{
    return { "mp3", "mp2", "mpga", "m4a", "m4b", "mp4", "aac", "alac", "opus", "oga", "spx", "wma", "asf", "ac3", "eac3", "dts", "thd",
             "mka", "mkv", "webm", "wv", "tta", "ape", "mpc", "amr", "awb", "gsm", "au", "snd", "voc", "w64", "caf", "3gp", "mov",
             "ra", "rm", "dsf", "dff", "shn", "tak", "ofr", "aptx", "sbc", "adx", "c2", "aifc", "mlp", "qcp", "xa", "avi", "flv" };
}
}
