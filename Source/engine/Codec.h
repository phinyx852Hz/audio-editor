// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"

#include <atomic>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Encoding and decoding through FFmpeg (libavcodec, libavformat): every audio
// encoder the installed FFmpeg has, so a newer FFmpeg adds formats without a
// new PHI. Well-known formats get their own settings; the rest are listed as
// "Other FFmpeg encoders" in a container that takes them.
namespace codec
{
    struct Setting
    {
        enum class Kind { choice, number, toggle };
        std::string id, label;
        Kind kind = Kind::choice;
        std::vector<std::pair<std::string, std::string>> choices;   // value, label
        double minimum = 0.0, maximum = 0.0, step = 1.0;
        std::string defaultValue, suffix;
        std::string showWhen;                                        // "mode=cbr": shown only then
    };

    struct Format
    {
        std::string id;             // "mp3", "aac-m4a", "ffmpeg:nellymoser"...
        std::string name;           // "MP3 (LAME)"
        std::string group;          // Lossy, Lossless, Broadcast and film, Speech and telephony, Legacy and games, Other
        std::string extension;      // without the dot
        std::string muxer, encoder; // FFmpeg names
        bool lossless = false;
        bool native = false;           // WAV and AIFF: written by PHI's own writer, with every chunk schema
        bool taggedByTagLib = false;   // else FFmpeg writes the common tags
        bool chapters = false;         // the container keeps chapters (from the markers)
        std::vector<int> bitDepths;    // lossless and PCM: the depths it takes (32 = float)
        std::vector<int> sampleRates;  // empty: any
        int maxChannels = 8;
        std::vector<Setting> settings;
        std::string note;
    };

    // The formats this FFmpeg can write, in display order.
    const std::vector<Format>& formats();
    const Format* find (const std::string& id);
    // Well-known formats this FFmpeg cannot write (no encoder), for the dialog to show as missing.
    std::vector<std::string> missingFormats();
    std::string ffmpegVersion();

    // The sample rate a format will use for audio at `rate`: the same, or the
    // nearest it takes (higher if it can).
    int rateFor (const Format&, double rate);

    using Settings = std::map<std::string, std::string>;
    Settings defaults (const Format&);
    // Whether a setting applies with these values (its showWhen).
    bool isShown (const Setting&, const Settings&);

    struct Job
    {
        const Format* format = nullptr;
        Settings settings;
        int bits = 16;                          // lossless and PCM formats
        std::vector<Marker> chapters;
        Tags tags;                              // written by FFmpeg when TagLib does not tag the format
        std::atomic<float>* progress = nullptr;
        const std::atomic<bool>* cancel = nullptr;
    };

    // Writes [start, end) of the audio, already at the format's rate and depth (not native formats).
    bool encode (const AudioData&, Frame start, Frame end, const std::string& path, const Job&, std::string& error);

    // Reads any file FFmpeg can (MP3, AAC, Opus, WMA, AC-3...).
    // Chapters become markers.
    AudioDataPtr decode (const std::string& path, std::string& error, std::atomic<float>* progress = nullptr,
                         const std::atomic<bool>* cancel = nullptr, std::vector<Marker>* chapters = nullptr);
    // Extensions FFmpeg reads (in addition to what JUCE reads).
    std::vector<std::string> readableExtensions();
}
