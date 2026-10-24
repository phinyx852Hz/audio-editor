// SPDX-License-Identifier: AGPL-3.0-only
#include "Batch.h"

#include "AudioEngine.h"

#include "../core/Metadata.h"
#include "../core/Repair.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace batch
{
namespace
{
    std::string number (double v, int decimals = 1)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        auto text = s.str();
        if (text.find ('.') != std::string::npos)
        {
            while (text.back() == '0')
                text.pop_back();
            if (text.back() == '.')
                text.pop_back();
        }
        return text;
    }

    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return s;
    }

    const char* whenText (When w)
    {
        switch (w)
        {
            case When::truePeakAbove: return "true peak is above %s dBTP";
            case When::peakAbove:     return "peak is above %s dBFS";
            case When::loudnessAbove: return "loudness is above %s LUFS";
            case When::loudnessBelow: return "loudness is below %s LUFS";
            case When::rateIsNot:     return "sample rate is not %s Hz";
            case When::always:        break;
        }
        return "";
    }

    // Brings the integrated loudness to target, limiting true peaks to the
    // ceiling when asked (up to four passes, as Normalize does).
    AudioDataPtr normalizeLoudness (const AudioData& audio, double target, bool limit, double ceiling, std::string& note)
    {
        const auto length = audio.length();
        const auto measured = dsp::measureLoudness (audio, 0, length).integratedLufs;
        if (measured <= -199.0)
        {
            note = "silent, left as it is";
            return nullptr;
        }
        auto change = target - measured;
        auto result = dsp::applyGain (audio, 0, length, change);
        if (! limit)
            return result;
        for (int pass = 0; pass < 4; ++pass)
        {
            const auto report = dsp::measureLoudness (*result, 0, length);
            if (report.truePeakDb <= ceiling + 0.05)
                break;
            result = dsp::limitTruePeak (*result, 0, length, ceiling);
            const auto missing = target - dsp::measureLoudness (*result, 0, length).integratedLufs;
            if (std::abs (missing) < 0.05)
                break;
            change += missing;
            result = dsp::applyGain (audio, 0, length, change);
        }
        if (dsp::measureLoudness (*result, 0, length).truePeakDb > ceiling + 0.05)
            result = dsp::limitTruePeak (*result, 0, length, ceiling);
        const auto reached = dsp::measureLoudness (*result, 0, length).integratedLufs;
        if (std::abs (reached - target) > 0.3)
            note = "reached " + number (reached) + " LUFS: the ceiling would need more limiting";
        return result;
    }

    AudioDataPtr padded (const AudioData& audio, double seconds, bool atStart)
    {
        const auto pad = dsp::silence (audio.numChannels(), audio.sampleRate(), (Frame) std::llround (seconds * audio.sampleRate()));
        return atStart ? audio.replaced (0, 0, pad.get()) : audio.replaced (audio.length(), audio.length(), pad.get());
    }
}

//==============================================================================
const std::vector<StepType>& stepTypes()
{
    static const std::vector<StepType> types {
        { "normalizeLoudness", "Normalize Loudness", "Integrated loudness to a target, with an optional true-peak ceiling",
          { { "target", "Target", -60.0, 0.0, 0.5, -14.0, " LUFS" },
            { "limit", "Limit true peaks", 0, 1, 1, 1, {}, { "Off", "On" } },
            { "ceiling", "Ceiling", -12.0, 0.0, 0.1, -1.0, " dBTP" } } },
        { "normalizePeak", "Normalize Peak", "The loudest sample to a level",
          { { "target", "Peak", -60.0, 0.0, 0.1, -1.0, " dBFS" } } },
        { "gain", "Gain", "Turn up or down", { { "db", "Gain", -60.0, 60.0, 0.5, 0.0, " dB" } } },
        { "limit", "True Peak Limiter", "Keeps peaks, including those between samples, under a ceiling",
          { { "ceiling", "Ceiling", -12.0, 0.0, 0.1, -1.0, " dBTP" }, { "release", "Release", 1.0, 1000.0, 10.0, 100.0, " ms" } } },
        { "master", "Master Section", "Through the plugins and gain of the Master Section as it is now", {} },
        { "trimSilence", "Trim Silence", "Removes silence at the start and end",
          { { "threshold", "Quieter than", -96.0, -20.0, 1.0, -60.0, " dB" }, { "keep", "Keep", 0.0, 5.0, 0.05, 0.1, " s" } } },
        { "padStart", "Add Silence at Start", "", { { "seconds", "Length", 0.0, 60.0, 0.1, 1.0, " s" } } },
        { "padEnd", "Add Silence at End", "", { { "seconds", "Length", 0.0, 60.0, 0.1, 1.0, " s" } } },
        { "fadeIn", "Fade In", "From the start", { { "seconds", "Length", 0.0, 60.0, 0.1, 0.5, " s" }, { "curve", "Curve", -1.0, 1.0, 0.1, 0.0 } } },
        { "fadeOut", "Fade Out", "To the end", { { "seconds", "Length", 0.0, 60.0, 0.1, 2.0, " s" }, { "curve", "Curve", -1.0, 1.0, 0.1, 0.0 } } },
        { "dcOffset", "Remove DC Offset", "", {} },
        { "invert", "Invert", "Flips the polarity", {} },
        { "mono", "Mix to Mono", "", {} },
        { "stereo", "Make Stereo", "Mono files become stereo; others are left as they are", {} },
        { "resample", "Change Sample Rate", "", { { "rate", "Sample rate", 0, 6, 1, 2, {}, { "22050 Hz", "32000 Hz", "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz", "192000 Hz" } } } },
        { "declick", "DeClick", "Finds clicks and crackle and rebuilds them",
          { { "sensitivity", "Sensitivity", 0, 2, 1, 1, {}, { "Low (loud clicks only)", "Medium", "High (vinyl crackle)" } } } },
        { "declip", "DeClip", "Rebuilds cut-off peaks", { { "level", "Clipped at or above", 80.0, 100.0, 0.5, 99.0, " %" } } },
        { "dehum", "DeHum", "Removes mains hum and its harmonics",
          { { "frequency", "Frequency", 0, 2, 1, 0, {}, { "Find it", "50 Hz", "60 Hz" } }, { "harmonics", "Harmonics", 1, 16, 1, 8 } } },
        { "deess", "DeEss", "Turns down harsh s sounds",
          { { "threshold", "Threshold", -60.0, 0.0, 1.0, -30.0, " dB" }, { "from", "From", 2000.0, 12000.0, 100.0, 5000.0, " Hz" },
            { "reduction", "At most", 0.0, 30.0, 1.0, 10.0, " dB" } } },
    };
    return types;
}

const StepType* findStep (const std::string& id)
{
    for (const auto& t : stepTypes())
        if (t.id == id)
            return &t;
    return nullptr;
}

double Step::value (const std::string& id) const
{
    if (const auto it = values.find (id); it != values.end())
        return it->second;
    if (const auto* t = findStep (type))
        for (const auto& p : t->parameters)
            if (p.id == id)
                return p.defaultValue;
    return 0.0;
}

Step makeStep (const std::string& type)
{
    Step s;
    s.type = type;
    if (const auto* t = findStep (type))
        for (const auto& p : t->parameters)
            s.values[p.id] = p.defaultValue;
    return s;
}

std::string describe (const Step& s)
{
    const auto* t = findStep (s.type);
    if (t == nullptr)
        return s.type;
    std::string text = t->name;
    std::vector<std::string> parts;
    for (const auto& p : t->parameters)
    {
        const auto v = s.value (p.id);
        if (! p.choices.empty())
            parts.push_back (p.label + " " + p.choices[(size_t) std::clamp ((int) v, 0, (int) p.choices.size() - 1)]);
        else if (p.suffix.empty())
            parts.push_back (p.label + " " + number (v, 2));
        else
            parts.push_back (number (v, 2) + p.suffix);
    }
    if (! parts.empty())
    {
        text += ": ";
        for (size_t i = 0; i < parts.size(); ++i)
            text += (i > 0 ? ", " : "") + parts[i];
    }
    if (s.when != When::always)
    {
        char buf[96];
        std::snprintf (buf, sizeof buf, whenText (s.when), number (s.whenValue).c_str());
        text += std::string (", only if ") + buf;
    }
    if (! s.enabled)
        text += " (off)";
    return text;
}

//==============================================================================
FileResult runFile (const std::string& path, const Job& job, const MasterRender& master,
                    std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    FileResult r;
    r.path = path;
    const auto started = std::chrono::steady_clock::now();
    auto report = [&] (float f) { if (progress != nullptr) progress->store (std::clamp (f, 0.0f, 1.0f)); };
    auto cancelled = [&] { return cancel != nullptr && cancel->load(); };

    std::vector<Marker> markers;
    Tags tags;
    std::string error;

    // Renaming touches only the file name.
    if (job.mode == Mode::rename)
    {
        AudioEngine::loadFile (path, markers, tags, error);   // for the tags
        const std::filesystem::path p (path);
        std::vector<std::pair<std::string, std::string>> vars { { "filename", p.stem().string() } };
        auto name = meta::expand (job.renamePattern, tags, vars);
        for (auto& c : name)
            if (c == '/' || c == '\0')
                c = '-';
        while (! name.empty() && (name.back() == ' ' || name.back() == '.'))
            name.pop_back();
        if (name.empty() || name.find ('{') != std::string::npos)
        {
            r.error = "the pattern left \"" + name + "\": a tag it uses is empty";
            return r;
        }
        const auto target = p.parent_path() / (name + p.extension().string());
        if (target == p)
        {
            r.ok = true;
            r.log.push_back ("already named so");
            return r;
        }
        if (std::filesystem::exists (target))
        {
            r.error = target.filename().string() + " already exists";
            return r;
        }
        std::error_code ec;
        std::filesystem::rename (p, target, ec);
        if (ec)
        {
            r.error = ec.message();
            return r;
        }
        r.ok = true;
        r.renamedTo = target.string();
        r.log.push_back ("renamed to " + target.filename().string());
        return r;
    }

    std::atomic<float> loadProgress { 0.0f };
    auto audio = AudioEngine::loadFile (path, markers, tags, error, &loadProgress);
    if (audio == nullptr)
    {
        r.error = error.empty() ? "could not open it" : error;
        return r;
    }
    r.seconds = audio->lengthSeconds();
    r.sampleRate = audio->sampleRate();
    r.channels = audio->numChannels();
    r.before = dsp::measureLoudness (*audio, 0, audio->length());
    report (0.1f);

    if (job.mode == Mode::report)
    {
        r.clippedStretches = (int) repair::findClipping (*audio, 0, audio->length(), 99.5).size();
        r.after = r.before;
        r.ok = true;
        return r;
    }

    if (job.mode == Mode::tagsOnly)
    {
        const std::filesystem::path p (path);
        std::vector<std::pair<std::string, std::string>> vars {
            { "filename", p.stem().string() },
            { "lufs", number (r.before.integratedLufs) }, { "truepeak", number (r.before.truePeakDb) },
            { "lra", number (r.before.loudnessRange) } };
        const auto changed = exporter::applyTemplate (tags, job.tagTemplate, vars);
        if (changed == tags)
        {
            r.ok = true;
            r.log.push_back ("tags already as the template has them");
            return r;
        }
        auto ext = lower (p.extension().string());
        if (! ext.empty())
            ext.erase (0, 1);
        tagio::Options options;
        options.software = APP_DISPLAY_NAME;
        options.id3 = ext != "wav" && ext != "aiff" && ext != "aif";   // ID3 in WAV only when the file already had it
        if (! AudioEngine::writeTags (path, markers, audio->sampleRate(), changed, options, error))
        {
            r.error = error;
            return r;
        }
        r.ok = true;
        r.log.push_back ("tags written");
        return r;
    }

    // Processing steps, each measured against its condition first.
    const auto steps = (float) std::max<size_t> (1, job.steps.size());
    for (size_t i = 0; i < job.steps.size(); ++i)
    {
        if (cancelled())
        {
            r.error = "Cancelled";
            return r;
        }
        const auto& s = job.steps[i];
        if (! s.enabled)
            continue;
        const auto* type = findStep (s.type);
        const auto name = type != nullptr ? type->name : s.type;

        if (s.when != When::always)
        {
            const auto m = s.when == When::rateIsNot ? dsp::LoudnessReport() : dsp::measureLoudness (*audio, 0, audio->length());
            bool run = true;
            switch (s.when)
            {
                case When::truePeakAbove: run = m.truePeakDb > s.whenValue; break;
                case When::peakAbove:     run = m.samplePeakDb > s.whenValue; break;
                case When::loudnessAbove: run = m.integratedLufs > s.whenValue; break;
                case When::loudnessBelow: run = m.integratedLufs < s.whenValue; break;
                case When::rateIsNot:     run = std::lround (audio->sampleRate()) != std::lround (s.whenValue); break;
                case When::always:        break;
            }
            if (! run)
            {
                r.log.push_back (name + ": skipped, the condition did not hold");
                continue;
            }
        }

        const auto length = audio->length();
        const auto rate = audio->sampleRate();
        std::atomic<float> stepDone { 0.0f };
        repair::Progress p { &stepDone, cancel };
        AudioDataPtr next;
        std::string note;

        if (s.type == "gain")
            next = dsp::applyGain (*audio, 0, length, s.value ("db"));
        else if (s.type == "normalizePeak")
        {
            const auto peak = dsp::gainToDb (audio->peakAbs (0, length));
            if (peak <= -199.0)
                note = "silent, left as it is";
            else
                next = dsp::applyGain (*audio, 0, length, s.value ("target") - peak);
        }
        else if (s.type == "normalizeLoudness")
            next = normalizeLoudness (*audio, s.value ("target"), s.value ("limit") > 0.5, s.value ("ceiling"), note);
        else if (s.type == "limit")
            next = dsp::limitTruePeak (*audio, 0, length, s.value ("ceiling"), s.value ("release"));
        else if (s.type == "master")
        {
            if (! master)
                note = "no Master Section here, skipped";
            else
            {
                std::string masterError;
                next = master (*audio, masterError, cancel);
                if (next == nullptr)
                {
                    r.error = "Master Section: " + masterError;
                    return r;
                }
            }
        }
        else if (s.type == "trimSilence")
        {
            const auto quiet = repair::findSilence (*audio, 0, length, s.value ("threshold"), 0.01, p);
            const auto keep = (Frame) std::llround (s.value ("keep") * rate);
            Frame from = 0, to = length;
            if (! quiet.empty() && quiet.front().start == 0)
                from = std::max<Frame> (0, quiet.front().end - keep);
            if (! quiet.empty() && quiet.back().end >= length)
                to = std::min (length, quiet.back().start + keep);
            if (to <= from)
                note = "all silent, left as it is";
            else if (from > 0 || to < length)
            {
                next = audio->slice (from, to);
                for (auto& m : markers)
                {
                    m.start = std::clamp<Frame> (m.start - from, 0, to - from);
                    m.end = std::clamp<Frame> (m.end - from, 0, to - from);
                }
                note = "removed " + number ((double) (length - (to - from)) / rate, 2) + " s";
            }
            else
                note = "no silence at the ends";
        }
        else if (s.type == "padStart" || s.type == "padEnd")
        {
            next = padded (*audio, s.value ("seconds"), s.type == "padStart");
            if (s.type == "padStart")
                for (auto& m : markers)
                {
                    m.start += next->length() - length;
                    m.end += next->length() - length;
                }
        }
        else if (s.type == "fadeIn" || s.type == "fadeOut")
        {
            const auto n = std::min (length, (Frame) std::llround (s.value ("seconds") * rate));
            next = s.type == "fadeIn" ? dsp::curvedFade (*audio, 0, n, s.value ("curve"), true)
                                      : dsp::curvedFade (*audio, length - n, length, s.value ("curve"), false);
        }
        else if (s.type == "dcOffset")
            next = dsp::removeDcOffset (*audio, 0, length);
        else if (s.type == "invert")
            next = dsp::invert (*audio, 0, length);
        else if (s.type == "mono")
        {
            if (audio->numChannels() > 1)
                next = dsp::toMono (*audio);
            else
                note = "already mono";
        }
        else if (s.type == "stereo")
        {
            if (audio->numChannels() == 1)
                next = dsp::toStereo (audio);
            else
                note = "not mono, left as it is";
        }
        else if (s.type == "resample")
        {
            const double rates[] { 22050, 32000, 44100, 48000, 88200, 96000, 192000 };
            const auto to = rates[std::clamp ((int) s.value ("rate"), 0, 6)];
            if (std::abs (to - rate) < 0.5)
                note = "already at that rate";
            else
            {
                next = dsp::resample (*audio, to, [&] (float f) { stepDone = f; return ! cancelled(); });
                for (auto& m : markers)
                {
                    m.start = (Frame) std::llround ((double) m.start * to / rate);
                    m.end = (Frame) std::llround ((double) m.end * to / rate);
                }
            }
        }
        else if (s.type == "declick")
        {
            const double thresholds[] { 9.0, 6.5, 4.5 };
            repair::Report rep;
            next = repair::declick (*audio, 0, length, thresholds[std::clamp ((int) s.value ("sensitivity"), 0, 2)], rep, p);
            note = std::to_string (rep.fixed) + " click(s) fixed";
        }
        else if (s.type == "declip")
        {
            repair::Report rep;
            next = repair::declip (*audio, 0, length, s.value ("level"), rep, p);
            note = std::to_string (rep.fixed) + " clipped stretch(es) rebuilt";
        }
        else if (s.type == "dehum")
        {
            repair::HumSettings h;
            const double frequencies[] { 0.0, 50.0, 60.0 };
            h.frequency = frequencies[std::clamp ((int) s.value ("frequency"), 0, 2)];
            if (h.frequency == 0.0)
                h.frequency = repair::findHum (*audio, 0, length);
            h.harmonics = (int) s.value ("harmonics");
            if (h.frequency <= 0.0)
                note = "no hum found";
            else
            {
                next = repair::dehum (*audio, 0, length, h, p);
                note = "hum at " + number (h.frequency, 2) + " Hz";
            }
        }
        else if (s.type == "deess")
        {
            repair::DeEssSettings d;
            d.thresholdDb = s.value ("threshold");
            d.fromHz = s.value ("from");
            d.maxReductionDb = s.value ("reduction");
            next = repair::deess (*audio, 0, length, d, p);
        }
        else
            note = "unknown step, skipped";

        if (cancelled())
        {
            r.error = "Cancelled";
            return r;
        }
        if (next != nullptr)
            audio = std::move (next);
        r.log.push_back (name + (note.empty() ? std::string() : ": " + note));
        report (0.1f + 0.5f * (float) (i + 1) / steps);
    }

    r.after = dsp::measureLoudness (*audio, 0, audio->length());

    // Every output.
    exporter::Source source;
    source.data = audio;
    source.start = 0;
    source.end = audio->length();
    source.markers = markers;
    source.tags = tags;
    source.sourcePath = path;
    const auto outputs = (float) std::max<size_t> (1, job.outputs.size());
    for (size_t i = 0; i < job.outputs.size(); ++i)
    {
        if (cancelled())
        {
            r.error = "Cancelled";
            return r;
        }
        std::atomic<float> one { 0.0f };
        auto result = exporter::run (source, job.outputs[i], &one, cancel);
        if (! result.ok && ! result.skipped && r.error.empty())
            r.error = result.error;
        r.outputs.push_back (std::move (result));
        report (0.6f + 0.4f * (float) (i + 1) / outputs);
    }
    r.ok = r.error.empty();
    r.seconds = audio->lengthSeconds();
    (void) started;
    return r;
}

//==============================================================================
std::vector<std::string> filesIn (const std::string& folder, bool recursive, const std::string& filter)
{
    std::vector<std::string> found;
    auto readable = AudioEngine::readableExtensions();
    const auto wanted = lower (filter);
    auto take = [&] (const std::filesystem::path& p)
    {
        auto ext = lower (p.extension().string());
        if (ext.size() < 2 || std::find (readable.begin(), readable.end(), ext.substr (1)) == readable.end())
            return;
        if (! wanted.empty() && lower (p.filename().string()).find (wanted) == std::string::npos)
            return;
        found.push_back (p.string());
    };
    std::error_code ec;
    if (recursive)
    {
        for (auto it = std::filesystem::recursive_directory_iterator (folder, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment (ec))
            if (! ec && it->is_regular_file (ec))
                take (it->path());
    }
    else
    {
        for (const auto& e : std::filesystem::directory_iterator (folder, ec))
            if (e.is_regular_file (ec))
                take (e.path());
    }
    std::sort (found.begin(), found.end());
    return found;
}

std::string reportCsv (const std::vector<FileResult>& results)
{
    auto quote = [] (const std::string& s)
    {
        std::string q = "\"";
        for (const auto c : s)
            q += c == '"' ? std::string ("\"\"") : std::string (1, c);
        return q + "\"";
    };
    auto db = [] (double v) { return v <= -199.0 ? std::string ("-inf") : number (v, 2); };
    std::string csv = "File,Result,Length (s),Sample rate,Channels,Integrated (LUFS),Loudness range (LU),Sample peak (dBFS),True peak (dBTP),"
                      "After: integrated (LUFS),After: true peak (dBTP),Clipped stretches,Outputs,Notes\n";
    for (const auto& r : results)
    {
        std::string outputs, notes;
        for (const auto& o : r.outputs)
        {
            outputs += std::string (outputs.empty() ? "" : "; ") + (o.skipped ? "skipped " : "") + std::filesystem::path (o.path).filename().string();
            if (! o.warning.empty())
                notes += std::string (notes.empty() ? "" : "; ") + o.warning;
        }
        for (const auto& l : r.log)
            notes += std::string (notes.empty() ? "" : "; ") + l;
        if (! r.renamedTo.empty())
            outputs = std::filesystem::path (r.renamedTo).filename().string();
        csv += quote (r.path) + "," + quote (r.ok ? "OK" : r.error) + "," + number (r.seconds, 3) + "," + number (r.sampleRate, 0) + ","
               + std::to_string (r.channels) + "," + db (r.before.integratedLufs) + "," + number (r.before.loudnessRange, 2) + ","
               + db (r.before.samplePeakDb) + "," + db (r.before.truePeakDb) + "," + db (r.after.integratedLufs) + "," + db (r.after.truePeakDb) + ","
               + std::to_string (r.clippedStretches) + "," + quote (outputs) + "," + quote (notes) + "\n";
    }
    return csv;
}

//==============================================================================
std::string toJson (const Job& job)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("phinyxBatch", 1);   // named before the rename to PHI, kept so older versions read it
    juce::Array<juce::var> files;
    for (const auto& f : job.files)
        files.add (juce::String::fromUTF8 (f.c_str()));
    root->setProperty ("files", files);
    juce::Array<juce::var> steps;
    for (const auto& s : job.steps)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", juce::String (s.type));
        o->setProperty ("enabled", s.enabled);
        o->setProperty ("when", (int) s.when);
        o->setProperty ("whenValue", s.whenValue);
        auto* values = new juce::DynamicObject();
        for (const auto& [k, v] : s.values)
            values->setProperty (juce::Identifier (juce::String (k)), v);
        o->setProperty ("values", juce::var (values));
        steps.add (juce::var (o));
    }
    root->setProperty ("steps", steps);
    root->setProperty ("outputs", juce::JSON::parse (juce::String::fromUTF8 (exporter::toJson (job.outputs).c_str())));
    const char* modes[] { "render", "tagsOnly", "rename", "report" };
    root->setProperty ("mode", modes[(int) job.mode]);
    root->setProperty ("tagTemplate", juce::String::fromUTF8 (job.tagTemplate.c_str()));
    root->setProperty ("renamePattern", juce::String::fromUTF8 (job.renamePattern.c_str()));
    root->setProperty ("report", juce::String::fromUTF8 (job.reportPath.c_str()));
    root->setProperty ("threads", job.threads);
    return juce::JSON::toString (juce::var (root)).toStdString();
}

Job fromJson (const std::string& text, std::string& error)
{
    Job job;
    const auto root = juce::JSON::parse (juce::String::fromUTF8 (text.c_str()));
    if (! root.isObject() || ! root.hasProperty ("phinyxBatch"))
    {
        error = "not a PHI batch file";
        return job;
    }
    if (const auto* files = root["files"].getArray())
        for (const auto& f : *files)
            job.files.push_back (f.toString().toStdString());
    if (const auto* steps = root["steps"].getArray())
        for (const auto& v : *steps)
        {
            auto s = makeStep (v["type"].toString().toStdString());
            s.enabled = (bool) v.getProperty ("enabled", true);
            s.when = (When) std::clamp ((int) v.getProperty ("when", 0), 0, (int) When::rateIsNot);
            s.whenValue = (double) v.getProperty ("whenValue", -1.0);
            if (const auto* values = v["values"].getDynamicObject())
                for (const auto& p : values->getProperties())
                    s.values[p.name.toString().toStdString()] = (double) p.value;
            job.steps.push_back (std::move (s));
        }
    job.outputs = exporter::fromJson (juce::JSON::toString (root["outputs"]).toStdString());
    const auto mode = root["mode"].toString();
    job.mode = mode == "tagsOnly" ? Mode::tagsOnly : mode == "rename" ? Mode::rename : mode == "report" ? Mode::report : Mode::render;
    job.tagTemplate = root["tagTemplate"].toString().toStdString();
    job.renamePattern = root.getProperty ("renamePattern", "{track} {title}").toString().toStdString();
    job.reportPath = root["report"].toString().toStdString();
    job.threads = (int) root.getProperty ("threads", 0);
    return job;
}
}
