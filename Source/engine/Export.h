// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Codec.h"
#include "TagIO.h"

#include "../core/Dsp.h"

#include <atomic>
#include <string>
#include <vector>

// Export: one render path for every output format. Each output has its own
// format and settings, sample rate, bit depth and dither, loudness target with
// true-peak limiting, file name pattern, folder, metadata schemas and template.
// After writing a lossy file it is decoded again and its true peak checked.
namespace exporter
{
    struct Output
    {
        std::string name;                  // shown in the list: "Streaming MP3"
        std::string formatId = "wav";
        codec::Settings settings;
        int sampleRate = 0;                // 0: as the source (or the nearest the format takes)
        int bits = 24;                     // lossless and PCM (32: float)
        dsp::Dither dither = dsp::Dither::tpdf;
        bool normalize = false;            // to targetLufs
        double targetLufs = -14.0;
        bool limit = true;                 // keep true peaks under ceilingDb
        double ceilingDb = -1.0;
        std::string folder;                // empty: beside the source
        std::string pattern = "{filename}";
        enum class Overwrite { replace, number, skip } overwrite = Overwrite::number;
        tagio::Options schemas;
        std::string templateText;          // "key=value" lines with {variables}, applied to the tags
        bool fillLoudness = true;          // BWF loudness fields from the measured result
        bool checkPeaks = true;            // decode lossy files again and measure their true peak
    };

    struct Source
    {
        AudioDataPtr data;
        Frame start = 0, end = 0;
        std::vector<Marker> markers;       // all of the source's; those inside [start, end) are kept
        Tags tags;
        std::string sourcePath;            // for {filename} and the default folder
        std::string part;                  // {part}: region or marker name, empty for the whole
        int index = 0;                     // {index}: 1 based when exporting parts
    };

    struct Result
    {
        bool ok = false, skipped = false;
        std::string path, error, warning;
        double lufs = -200.0, truePeakDb = -200.0, encodedTruePeakDb = -200.0;
    };

    // The file an output would write for a source (before overwrite rules).
    std::string targetPath (const Source&, const Output&);
    // Template lines applied to tags: "title={part}" sets the title, and so on.
    Tags applyTemplate (const Tags&, const std::string& templateText, const std::vector<std::pair<std::string, std::string>>& vars);
    std::vector<std::pair<std::string, std::string>> variables (const Source&, const Output&);

    Result run (const Source&, const Output&, std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    // Output lists as text (JSON), for presets and settings.
    std::string toJson (const std::vector<Output>&);
    std::vector<Output> fromJson (const std::string&);
}
