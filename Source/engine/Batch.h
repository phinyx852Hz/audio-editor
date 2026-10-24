// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Export.h"

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

// The Batch Processor, modeled on WaveLab's: a list of files, a chain of
// processing steps (each optionally only when a condition holds) and the
// Export outputs, run on every file. A batch can also only write tags, rename
// files from their tags, or measure them into a report. Saved as .phbatch.
namespace batch
{
    struct Parameter
    {
        std::string id, label;
        double minimum = 0.0, maximum = 1.0, step = 0.1, defaultValue = 0.0;
        std::string suffix;
        std::vector<std::string> choices;   // when set, the value is an index into these
    };

    struct StepType
    {
        std::string id, name, description;
        std::vector<Parameter> parameters;
    };
    // Every step a batch can run, in menu order.
    const std::vector<StepType>& stepTypes();
    const StepType* findStep (const std::string& id);

    // Run a step only when the audio meets this (measured just before it).
    enum class When { always, truePeakAbove, peakAbove, loudnessAbove, loudnessBelow, rateIsNot };

    struct Step
    {
        std::string type;
        std::map<std::string, double> values;   // by parameter id; missing ones take the default
        bool enabled = true;
        When when = When::always;
        double whenValue = -1.0;

        double value (const std::string& id) const;
    };
    Step makeStep (const std::string& type);
    std::string describe (const Step&);        // "Normalize loudness to -14 LUFS, only if..."

    enum class Mode
    {
        render,     // process, then write every output
        tagsOnly,   // apply the tag template to the files themselves (lossy files are not encoded again)
        rename,     // rename the files from the name pattern
        report      // measure only, write the report
    };

    struct Job
    {
        std::vector<std::string> files;
        std::vector<Step> steps;
        std::vector<exporter::Output> outputs;
        Mode mode = Mode::render;
        std::string tagTemplate;            // tagsOnly: "key=value" lines with {variables}
        std::string renamePattern = "{track} {title}";
        std::string reportPath;             // CSV; empty: no report (always written in report mode, beside the first file)
        int threads = 0;                    // 0: one per CPU core
    };

    struct FileResult
    {
        std::string path;
        bool ok = false;
        std::string error;
        std::vector<std::string> log;       // what was done, step by step
        std::vector<exporter::Result> outputs;
        dsp::LoudnessReport before, after;
        double seconds = 0.0, sampleRate = 0.0;
        int channels = 0, clippedStretches = 0;
        std::string renamedTo;
    };

    // Renders audio through the Master Section (supplied by the app; may be empty).
    using MasterRender = std::function<AudioDataPtr (const AudioData&, std::string& error, const std::atomic<bool>* cancel)>;

    FileResult runFile (const std::string& path, const Job&, const MasterRender&,
                        std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    // Audio files in a folder (and its subfolders when recursive) that PHI opens.
    std::vector<std::string> filesIn (const std::string& folder, bool recursive, const std::string& filter = {});

    std::string reportCsv (const std::vector<FileResult>&);

    std::string toJson (const Job&);
    Job fromJson (const std::string&, std::string& error);
}
