// SPDX-License-Identifier: AGPL-3.0-only
#include "Export.h"

#include "AudioEngine.h"

#include "../core/Metadata.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <ctime>
#include <filesystem>

namespace exporter
{
namespace
{
    std::string baseName (const std::string& path)
    {
        return path.empty() ? std::string ("Untitled") : std::filesystem::path (path).stem().string();
    }

    std::string number (double v, int decimals)
    {
        char buf[32];
        std::snprintf (buf, sizeof buf, "%.*f", decimals, v);
        return buf;
    }

    std::string safeName (std::string name)
    {
        for (auto& ch : name)
            if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|'
                || (unsigned char) ch < 0x20)
                ch = '_';
        while (! name.empty() && (name.front() == '.' || name.front() == ' '))
            name.erase (name.begin());
        while (! name.empty() && name.back() == ' ')
            name.pop_back();
        return name.empty() ? std::string ("Untitled") : name;
    }

    int rateOf (const Source& s, const Output& o, const codec::Format& f)
    {
        return codec::rateFor (f, o.sampleRate > 0 ? o.sampleRate : s.data->sampleRate());
    }

    std::string depthName (int bits)
    {
        return bits == 32 ? "32f" : bits == 64 ? "64f" : bits == 31 ? "32" : std::to_string (bits);
    }
}

std::vector<std::pair<std::string, std::string>> variables (const Source& s, const Output& o)
{
    const auto* f = codec::find (o.formatId);
    std::vector<std::pair<std::string, std::string>> v;
    v.emplace_back ("filename", baseName (s.sourcePath));
    v.emplace_back ("part", s.part.empty() ? baseName (s.sourcePath) : s.part);
    char index[16];
    std::snprintf (index, sizeof index, "%02d", std::max (1, s.index));
    v.emplace_back ("index", index);
    v.emplace_back ("output", o.name);
    if (f != nullptr)
    {
        v.emplace_back ("format", f->id.substr (f->id.find (':') + 1));
        v.emplace_back ("ext", f->extension);
        const auto rate = rateOf (s, o, *f);
        v.emplace_back ("rate", rate % 1000 == 0 ? std::to_string (rate / 1000) : number (rate / 1000.0, 1));
        v.emplace_back ("bits", f->bitDepths.empty() ? std::string() : depthName (o.bits));
        v.emplace_back ("bitrate", o.settings.count ("bitrate") > 0 ? o.settings.at ("bitrate") : std::string());
    }
    const auto now = std::time (nullptr);
    char date[32];
    std::strftime (date, sizeof date, "%Y-%m-%d", std::localtime (&now));
    v.emplace_back ("today", date);
    v.emplace_back ("year", std::string (date).substr (0, 4));
    return v;
}

std::string targetPath (const Source& s, const Output& o)
{
    const auto* f = codec::find (o.formatId);
    auto folder = o.folder;
    if (folder.empty())
        folder = s.sourcePath.empty() ? juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName().toStdString()
                                      : std::filesystem::path (s.sourcePath).parent_path().string();
    const auto name = safeName (meta::expand (o.pattern.empty() ? "{filename}" : o.pattern, s.tags, variables (s, o)));
    return (std::filesystem::path (folder) / (name + "." + (f != nullptr ? f->extension : std::string ("wav")))).string();
}

Tags applyTemplate (const Tags& tags, const std::string& text, const std::vector<std::pair<std::string, std::string>>& vars)
{
    Tags out = tags;
    size_t start = 0;
    while (start < text.size())
    {
        auto nl = text.find ('\n', start);
        const auto line = text.substr (start, nl == std::string::npos ? std::string::npos : nl - start);
        start = nl == std::string::npos ? text.size() : nl + 1;
        const auto eq = line.find ('=');
        if (eq == std::string::npos || eq == 0 || line[0] == '#')
            continue;
        const auto key = line.substr (0, eq);
        auto value = meta::expand (line.substr (eq + 1), tags, vars);
        if (value.empty())
            out.erase (key);
        else
            out[key] = value;
    }
    return out;
}

Result run (const Source& source, const Output& o, std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    Result result;
    const auto* f = codec::find (o.formatId);
    if (f == nullptr)
        return result.error = "This FFmpeg cannot write " + o.formatId, result;
    if (source.data == nullptr || source.end <= source.start)
        return result.error = "Nothing to export", result;

    // Where it goes.
    result.path = targetPath (source, o);
    if (std::filesystem::exists (result.path))
    {
        if (o.overwrite == Output::Overwrite::skip)
        {
            result.skipped = true;
            result.ok = true;
            return result;
        }
        if (o.overwrite == Output::Overwrite::number)
        {
            const std::filesystem::path p (result.path);
            for (int n = 2; std::filesystem::exists (result.path); ++n)
                result.path = (p.parent_path() / (p.stem().string() + " (" + std::to_string (n) + ")" + p.extension().string())).string();
        }
    }
    std::error_code ec;
    std::filesystem::create_directories (std::filesystem::path (result.path).parent_path(), ec);

    const auto stage = [&] (float from, float to)
    {
        return [=] (float x) { if (progress != nullptr) progress->store (from + (to - from) * x); return cancel == nullptr || ! cancel->load(); };
    };

    // The part, at the output's rate and channels.
    auto audio = source.data->slice (source.start, source.end);
    std::vector<Marker> markers;
    for (const auto& m : source.markers)
        if (m.start >= source.start && m.start < source.end)
        {
            auto copy = m;
            copy.start -= source.start;
            copy.end = std::min (copy.end, source.end) - source.start;
            markers.push_back (copy);
        }

    if (audio->numChannels() > f->maxChannels)
    {
        if (f->maxChannels == 1)
            audio = dsp::toMono (*audio);
        else
            return result.error = f->name + " holds at most " + std::to_string (f->maxChannels) + " channels: fold the audio down first", result;
    }

    const auto rate = rateOf (source, o, *f);
    if (std::lround (audio->sampleRate()) != rate)
    {
        const auto ratio = rate / audio->sampleRate();
        audio = dsp::resample (*audio, rate, stage (0.0f, 0.3f));
        if (audio == nullptr)
            return result.error = "Cancelled", result;
        for (auto& m : markers)
            m.start = (Frame) std::llround (m.start * ratio), m.end = (Frame) std::llround (m.end * ratio);
    }

    // Loudness and peaks.
    const auto length = audio->length();
    if (o.normalize)
    {
        const auto report = dsp::measureLoudness (*audio, 0, length);
        if (report.integratedLufs <= -70.0)
            return result.error = "Too quiet or too short to measure its loudness", result;
        auto change = o.targetLufs - report.integratedLufs;
        auto processed = dsp::applyGain (*audio, 0, length, change);
        if (o.limit && report.truePeakDb + change > o.ceilingDb)
            for (int pass = 0; pass < 4; ++pass)
            {
                processed = dsp::limitTruePeak (*processed, 0, length, o.ceilingDb);
                const auto missing = o.targetLufs - dsp::measureLoudness (*processed, 0, length).integratedLufs;
                if (std::abs (missing) < 0.1 || pass == 3)
                    break;
                change += missing;
                processed = dsp::applyGain (*audio, 0, length, change);
            }
        audio = processed;
    }
    else if (o.limit && dsp::measureLoudness (*audio, 0, length).truePeakDb > o.ceilingDb)
        audio = dsp::limitTruePeak (*audio, 0, length, o.ceilingDb);
    if (progress != nullptr)
        progress->store (0.4f);

    const auto measured = dsp::measureLoudness (*audio, 0, length);
    result.lufs = measured.integratedLufs;
    result.truePeakDb = measured.truePeakDb;

    // Bit depth: lossless and PCM formats are rounded here, with dither.
    int bits = o.bits;
    if (! f->bitDepths.empty() && std::find (f->bitDepths.begin(), f->bitDepths.end(), bits) == f->bitDepths.end())
    {
        // The deepest it takes that is not deeper than asked.
        auto best = f->bitDepths.front();
        for (const auto d : f->bitDepths)
            if (d <= bits)
                best = d;
        bits = best;
    }
    if (! f->bitDepths.empty() && bits <= 24)
    {
        audio = dsp::quantize (*audio, bits, o.dither, stage (0.4f, 0.5f));
        if (audio == nullptr)
            return result.error = "Cancelled", result;
    }

    // Tags: the template, then what was measured.
    auto vars = variables (source, o);
    vars.emplace_back ("lufs", number (measured.integratedLufs, 1));
    vars.emplace_back ("truepeak", number (measured.truePeakDb, 1));
    vars.emplace_back ("lra", number (measured.loudnessRange, 1));
    auto tags = applyTemplate (source.tags, o.templateText, vars);
    if (! source.part.empty() && o.templateText.find ("title=") == std::string::npos && tags[tag::title].empty())
        tags[tag::title] = source.part;
    std::erase_if (tags, [] (const auto& kv) { return kv.second.empty(); });
    if (o.fillLoudness && f->native && o.schemas.bext && measured.integratedLufs > -70.0)
    {
        tags[tag::bwfLoudness] = number (measured.integratedLufs, 2);
        tags[tag::bwfLoudnessRange] = number (measured.loudnessRange, 2);
        tags[tag::bwfTruePeak] = number (measured.truePeakDb, 2);
    }
    if (f->native && o.schemas.bext && tags[tag::bwfCodingHistory].empty())
        tags[tag::bwfCodingHistory] = "A=PCM,F=" + std::to_string (rate) + ",W=" + std::to_string (bits > 24 ? 32 : bits)
                                      + ",M=" + (audio->numChannels() == 1 ? "mono" : audio->numChannels() == 2 ? "stereo" : "multitrack")
                                      + ",T=" + o.schemas.software;
    std::erase_if (tags, [] (const auto& kv) { return kv.second.empty(); });

    // Write.
    std::string error;
    std::atomic<float> encodeProgress { 0.0f };
    if (f->native)
    {
        SaveOptions save;
        save.bitDepth = bits > 24 ? 32 : bits;
        save.dither = false;   // done above
        save.metadata = o.schemas;
        save.layout = source.layout;
        // AIFF keeps no chapters; WAV keeps them as cue points.
        if (! AudioEngine::saveFile (result.path, *audio, f->chapters && o.schemas.chapters ? markers : std::vector<Marker>(), tags, save, error))
            return result.error = error, result;
    }
    else
    {
        codec::Job job;
        job.format = f;
        job.settings = o.settings;
        job.bits = bits;
        job.chapters = o.schemas.chapters ? markers : std::vector<Marker>();
        job.tags = tags;
        job.progress = &encodeProgress;
        job.cancel = cancel;
        const bool ok = codec::encode (*audio, 0, audio->length(), result.path, job, error);
        if (! ok)
        {
            std::filesystem::remove (result.path, ec);
            return result.error = error, result;
        }
        if (f->taggedByTagLib && ! tagio::write (result.path, tags, job.chapters, rate, o.schemas, error))
            return result.error = error, result;
    }
    if (progress != nullptr)
        progress->store (0.9f);

    // Lossy codecs can raise peaks: decode and measure again.
    if (! f->lossless && o.checkPeaks && f->id != "raw")
        if (const auto decoded = codec::decode (result.path, error, nullptr, cancel))
        {
            result.encodedTruePeakDb = dsp::measureLoudness (*decoded, 0, decoded->length()).truePeakDb;
            const auto allowed = o.limit ? o.ceilingDb : 0.0;
            if (result.encodedTruePeakDb > allowed + 0.05)
                result.warning = "After encoding, " + f->name + " peaks at " + number (result.encodedTruePeakDb, 1) + " dBTP (above "
                                 + number (allowed, 1) + "): lower the level or the true-peak ceiling";
        }
    if (progress != nullptr)
        progress->store (1.0f);
    result.ok = true;
    return result;
}

//==============================================================================
std::string toJson (const std::vector<Output>& outputs)
{
    juce::Array<juce::var> list;
    for (const auto& o : outputs)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name", juce::String::fromUTF8 (o.name.c_str()));
        obj->setProperty ("format", juce::String (o.formatId));
        auto* settings = new juce::DynamicObject();
        for (const auto& [k, v] : o.settings)
            settings->setProperty (juce::Identifier (juce::String (k)), juce::String::fromUTF8 (v.c_str()));
        obj->setProperty ("settings", juce::var (settings));
        obj->setProperty ("sampleRate", o.sampleRate);
        obj->setProperty ("bits", o.bits);
        obj->setProperty ("dither", (int) o.dither);
        obj->setProperty ("normalize", o.normalize);
        obj->setProperty ("targetLufs", o.targetLufs);
        obj->setProperty ("limit", o.limit);
        obj->setProperty ("ceilingDb", o.ceilingDb);
        obj->setProperty ("folder", juce::String::fromUTF8 (o.folder.c_str()));
        obj->setProperty ("pattern", juce::String::fromUTF8 (o.pattern.c_str()));
        obj->setProperty ("overwrite", (int) o.overwrite);
        obj->setProperty ("template", juce::String::fromUTF8 (o.templateText.c_str()));
        obj->setProperty ("fillLoudness", o.fillLoudness);
        obj->setProperty ("checkPeaks", o.checkPeaks);
        const auto& s = o.schemas;
        juce::StringArray off;
        const std::pair<const char*, bool> flags[] { { "riff", s.riff }, { "bext", s.bext }, { "cart", s.cart }, { "ixml", s.ixml }, { "id3", s.id3 },
                                                     { "id3v1", s.id3v1 }, { "vorbis", s.vorbis }, { "mp4", s.mp4 }, { "ape", s.ape }, { "xmp", s.xmp },
                                                     { "chapters", s.chapters } };
        for (const auto& [name, on] : flags)
            if (! on)
                off.add (name);
        obj->setProperty ("schemasOff", off.joinIntoString (","));
        obj->setProperty ("id3Version", s.id3Version);
        list.add (juce::var (obj));
    }
    return juce::JSON::toString (juce::var (list)).toStdString();
}

std::vector<Output> fromJson (const std::string& text)
{
    std::vector<Output> out;
    const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (text.c_str()));
    if (! parsed.isArray())
        return out;
    for (const auto& v : *parsed.getArray())
    {
        Output o;
        const auto str = [&] (const char* key, const std::string& def) { return v.hasProperty (key) ? v[key].toString().toStdString() : def; };
        o.name = str ("name", o.name);
        o.formatId = str ("format", o.formatId);
        if (auto* settings = v["settings"].getDynamicObject())
            for (const auto& p : settings->getProperties())
                o.settings[p.name.toString().toStdString()] = p.value.toString().toStdString();
        o.sampleRate = (int) v.getProperty ("sampleRate", o.sampleRate);
        o.bits = (int) v.getProperty ("bits", o.bits);
        o.dither = (dsp::Dither) std::clamp ((int) v.getProperty ("dither", (int) o.dither), 0, 2);
        o.normalize = (bool) v.getProperty ("normalize", o.normalize);
        o.targetLufs = (double) v.getProperty ("targetLufs", o.targetLufs);
        o.limit = (bool) v.getProperty ("limit", o.limit);
        o.ceilingDb = (double) v.getProperty ("ceilingDb", o.ceilingDb);
        o.folder = str ("folder", o.folder);
        o.pattern = str ("pattern", o.pattern);
        o.overwrite = (Output::Overwrite) std::clamp ((int) v.getProperty ("overwrite", (int) o.overwrite), 0, 2);
        o.templateText = str ("template", o.templateText);
        o.fillLoudness = (bool) v.getProperty ("fillLoudness", o.fillLoudness);
        o.checkPeaks = (bool) v.getProperty ("checkPeaks", o.checkPeaks);
        juce::StringArray off;
        off.addTokens (v["schemasOff"].toString(), ",", {});
        auto& s = o.schemas;
        s.riff = ! off.contains ("riff"); s.bext = ! off.contains ("bext"); s.cart = ! off.contains ("cart"); s.ixml = ! off.contains ("ixml");
        s.id3 = ! off.contains ("id3"); s.id3v1 = ! off.contains ("id3v1"); s.vorbis = ! off.contains ("vorbis"); s.mp4 = ! off.contains ("mp4");
        s.ape = ! off.contains ("ape"); s.xmp = ! off.contains ("xmp"); s.chapters = ! off.contains ("chapters");
        s.id3Version = (int) v.getProperty ("id3Version", 4) == 3 ? 3 : 4;
        out.push_back (std::move (o));
    }
    return out;
}
}
