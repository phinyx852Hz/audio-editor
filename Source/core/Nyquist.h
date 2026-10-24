// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <atomic>
#include <string>
#include <vector>

// Nyquist effects, as in Audacity: small programs in Nyquist (a Lisp for
// sound, by Roger B. Dannenberg) in .ny files whose header names the effect
// and its controls. PHI runs them with libnyquist, the same library
// Audacity uses (third_party/libnyquist), so Audacity's own plug-ins and the
// many shared online work here. Header parsing and the variables set up for
// the script follow Audacity's NyquistBase.cpp (GPLv2+). Plain C++.
namespace nyquist
{
    enum class Kind { process, generate, analyze, tool };

    struct Control
    {
        enum class Type { real, integer, realText, intText, time, string, choice, file, text };

        Type type = Type::real;
        std::string var;            // the Lisp variable it sets
        std::string name;           // shown beside it
        std::string label;          // unit, or the text itself for Type::text
        double value = 0.0, low = 0.0, high = 1.0;
        std::string text;           // value of string and file controls
        std::vector<std::string> choices;
        std::string fileTypes, fileFlags;   // file controls: "save", "open", "multiple"...
    };

    struct Script
    {
        bool ok = false;
        std::string error;
        std::string path;           // empty for the Nyquist Prompt
        std::string name, action, info, author, release, copyright;
        Kind kind = Kind::process;
        bool typeGiven = false;     // the header has a ;type line
        bool inToolsMenu = false;   // ;type tool, or ;type tool process|generate|analyze
        bool spectral = false;      // works on the selection's frequency band
        bool sal = false;           // SAL rather than Lisp
        bool trace = false;
        int version = 1;
        long long maxLength = 0;    // frames; 0 = the whole selection
        std::vector<Control> controls;
        std::string code;
    };

    // Reads a script and its header. For the Nyquist Prompt, `prompt` is true:
    // the language is guessed and it runs as version 4.
    Script parse (const std::string& text, bool prompt = false);
    Script load (const std::string& path);

    struct Label
    {
        double start = 0.0, end = 0.0;   // seconds in the file
        std::string text;
    };

    struct Input
    {
        const AudioData* audio = nullptr;   // the file, or null (a tool, or nothing open)
        Frame start = 0, end = 0;           // the selection; generators replace it
        double sampleRate = 48000.0;        // when there is no audio
        double lowHz = -1.0, highHz = -1.0; // the selection's frequency band, if any
        std::string trackName, projectName;
        bool debug = false;                 // trace on, and keep everything printed
    };

    struct Result
    {
        enum class Type { nothing, audio, labels, text, number, error, cancelled };
        Type type = Type::nothing;
        AudioDataPtr audio;          // replaces [start, end) (process and generate)
        std::vector<Label> labels;
        std::string text;            // a message, or the number as text
        std::string output;          // what the script printed (errors, traces)
    };

    struct Progress
    {
        std::atomic<float>* fraction = nullptr;
        const std::atomic<bool>* cancel = nullptr;
    };

    // Runs a script (one at a time; callable from a worker thread). The values
    // of its controls are taken from script.controls.
    Result run (const Script&, const Input&, const Progress& = {});

    // Folders: the runtime (init.lsp, nyquist.lsp...), the bundled plug-ins,
    // and where the user's own plug-ins go (the Plugin Installer copies there).
    void setFolders (const std::string& runtime, const std::vector<std::string>& pluginFolders, const std::string& userPlugins);
    const std::vector<std::string>& pluginFolders();
    const std::string& userPluginFolder();

    // "*home*/x.txt", "~/x", "*default*" (Documents)... as Audacity resolves them for file controls.
    std::string resolveFilePath (const std::string& path, const std::string& extension = {});

    // Lisp string literal contents: \ and " escaped.
    std::string escape (const std::string&);
}
