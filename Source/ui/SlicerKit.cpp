// SPDX-License-Identifier: AGPL-3.0-only
#include "SlicerKit.h"
#include "../core/Dsp.h"
#include "../engine/AudioEngine.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace slicerkit
{
    namespace
    {
        QString tr (const char* text) { return QCoreApplication::translate ("SlicerKit", text); }
    }

    sampler::SamplePtr loadSound (const QString& path, double rate, QString& error, AudioDataPtr* audio)
    {
        std::vector<Marker> markers;
        Tags tags;
        std::string why;
        auto loaded = AudioEngine::loadFile (path.toStdString(), markers, tags, why);
        if (loaded == nullptr)
        {
            error = QString::fromStdString (why);
            return nullptr;
        }
        if (std::abs (loaded->sampleRate() - rate) > 0.5)
            loaded = dsp::resample (*loaded, rate);
        if (audio != nullptr)
            *audio = loaded;
        return sampler::makeSample (*loaded);
    }

    AudioDataPtr asAudio (const sampler::Sample& s, double rate)
    {
        AudioData::Builder b (2, rate);
        for (Frame at = 0; at < s.length(); at += 65536)
        {
            const auto n = (int) std::min<Frame> (65536, s.length() - at);
            const float* part[2] = { s.left.data() + at, s.right.data() + at };
            b.append (part, n);
        }
        return b.finish();
    }

    bool saveSound (const sampler::Sample& s, double rate, const QString& path, QString& error)
    {
        SaveOptions options;
        options.bitDepth = 32;
        std::string why;
        if (AudioEngine::saveFile (path.toStdString(), *asAudio (s, rate), {}, {}, options, why))
            return true;
        error = QString::fromStdString (why);
        return false;
    }

    QString storedPath (const QString& file, const QDir& base)
    {
        if (file.isEmpty())
            return {};
        const auto relative = base.relativeFilePath (file);
        return relative.startsWith (QLatin1String ("../../")) ? QFileInfo (file).absoluteFilePath() : relative;
    }

    QString resolvedPath (const QString& stored, const QDir& base)
    {
        if (stored.isEmpty())
            return {};
        return QFileInfo (stored).isAbsolute() ? stored : QDir::cleanPath (base.absoluteFilePath (stored));
    }

    QJsonObject toJson (const slicer::Kit& k, const QDir& base, const Store& store)
    {
        QJsonArray pads;
        for (int i = 0; i < slicer::numPads; ++i)
        {
            const auto& p = k.pads[(size_t) i];
            const bool named = ! p.name.empty() || p.colour != 0 || p.receive >= 0;
            if (! p.used() && ! named)
                continue;
            QJsonArray layers;
            for (int n = 0; n < slicer::maxLayers; ++n)
            {
                const auto& l = p.layers[(size_t) n];
                if (! l.used)
                    continue;
                QJsonObject o { { "layer", n }, { "start", (double) l.start }, { "end", (double) l.end }, { "gain", l.gainDb },
                                { "pitch", l.pitch }, { "fadeIn", l.fadeIn }, { "fadeOut", l.fadeOut }, { "reverse", l.reverse } };
                if (l.sound != nullptr)
                {
                    auto file = QString::fromStdString (l.path);
                    if ((file.isEmpty() || ! QFileInfo::exists (file)) && store)
                        file = store (*l.sound, i, n);
                    if (file.isEmpty())
                        continue;
                    o.insert ("file", storedPath (file, base));
                }
                layers.append (o);
            }
            QJsonObject o { { "pad", i }, { "layers", layers }, { "gain", p.gainDb }, { "pan", p.pan }, { "pitch", p.pitch },
                            { "gate", p.play == slicer::Play::gate }, { "attack", p.attack }, { "release", p.release }, { "choke", p.choke } };
            if (! p.name.empty())
                o.insert ("name", QString::fromStdString (p.name));
            if (p.colour != 0)
                o.insert ("colour", p.colour);
            if (p.mute)
                o.insert ("mute", true);
            if (p.solo)
                o.insert ("solo", true);
            if (p.receive >= 0)
                o.insert ("receive", p.receive);
            pads.append (o);
        }
        return { { "version", 2 }, { "pads", pads }, { "velocity", k.velocity }, { "voices", k.voices },
                 { "layout", k.layout == slicer::Layout::push ? "push" : "chromatic" }, { "pushPage", k.pushPage } };
    }

    slicer::Kit fromJson (const QJsonObject& o, const QDir& base, double rate, QStringList* problems)
    {
        slicer::Kit k;
        k.velocity = o.value ("velocity").toDouble (0.6);
        k.voices = std::clamp (o.value ("voices").toInt (16), 1, slicer::Engine::maxVoices);
        k.layout = o.value ("layout").toString() == QLatin1String ("chromatic") ? slicer::Layout::chromatic : slicer::Layout::push;
        k.pushPage = std::clamp (o.value ("pushPage").toInt(), 0, 1);
        // Before 128 pads, Chromatic began at C1 (note 36) on the first pad; now pad n is note n.
        const auto version = o.value ("version").toInt (1);
        const auto shift = version < 2 && k.layout == slicer::Layout::chromatic ? 36 : 0;
        for (const auto& v : o.value ("pads").toArray())
        {
            const auto p = v.toObject();
            const auto i = p.value ("pad").toInt (-1) + shift;
            if (i < 0 || i >= slicer::numPads)
                continue;
            auto& pad = k.pads[(size_t) i];
            pad.gainDb = p.value ("gain").toDouble();
            pad.pan = p.value ("pan").toDouble();
            pad.pitch = p.value ("pitch").toDouble();
            pad.play = p.value ("gate").toBool() ? slicer::Play::gate : slicer::Play::oneShot;
            pad.attack = p.value ("attack").toDouble (0.0005);
            pad.release = p.value ("release").toDouble (0.03);
            pad.choke = std::clamp (p.value ("choke").toInt(), 0, slicer::numChokes);
            pad.name = p.value ("name").toString().toStdString();
            pad.colour = std::clamp (p.value ("colour").toInt(), 0, slicer::numColours);
            pad.mute = p.value ("mute").toBool();
            pad.solo = p.value ("solo").toBool();
            pad.receive = std::clamp (p.value ("receive").toInt (-1), -1, 127);
            if (version < 2)
            {
                // One slice a pad.
                auto& l = pad.layers[0];
                l.used = true;
                l.start = (Frame) p.value ("start").toDouble();
                l.end = (Frame) p.value ("end").toDouble();
                l.reverse = p.value ("reverse").toBool();
                continue;
            }
            for (const auto& lv : p.value ("layers").toArray())
            {
                const auto lo = lv.toObject();
                const auto n = lo.value ("layer").toInt (-1);
                if (n < 0 || n >= slicer::maxLayers)
                    continue;
                auto& l = pad.layers[(size_t) n];
                l.start = (Frame) lo.value ("start").toDouble();
                l.end = (Frame) lo.value ("end").toDouble();
                l.gainDb = lo.value ("gain").toDouble();
                l.pitch = lo.value ("pitch").toDouble();
                l.fadeIn = lo.value ("fadeIn").toDouble();
                l.fadeOut = lo.value ("fadeOut").toDouble();
                l.reverse = lo.value ("reverse").toBool();
                l.used = true;
                if (const auto file = resolvedPath (lo.value ("file").toString(), base); ! file.isEmpty())
                {
                    QString why;
                    l.sound = loadSound (file, rate, why);
                    l.path = file.toStdString();
                    if (l.sound == nullptr)
                    {
                        l.used = false;
                        if (problems != nullptr)
                            *problems << tr ("Pad %1: %2 could not be read (%3)").arg (i + 1).arg (file, why);
                    }
                    else
                        l.end = std::min (l.end, l.sound->length());
                }
            }
        }
        return k;
    }

    bool saveKitFile (const QString& path, const slicer::Kit& kit, const sampler::SamplePtr& sample, double rate, QString& error)
    {
        const QFileInfo info (path);
        const auto base = info.absoluteDir();
        const auto folder = info.completeBaseName() + tr (" Sounds");
        auto needFolder = [&] { return base.exists (folder) || base.mkpath (folder); };
        QJsonObject root { { "phikit", 1 }, { "rate", rate } };
        if (sample != nullptr)
        {
            const auto wav = base.filePath (folder + QStringLiteral ("/Sample.wav"));
            if (! needFolder() || ! saveSound (*sample, rate, wav, error))
                return false;
            root.insert ("sample", storedPath (wav, base));
        }
        QString why;
        root.insert ("kit", toJson (kit, base, [&] (const sampler::Sample& s, int pad, int layer)
        {
            const auto wav = base.filePath (folder + QStringLiteral ("/Pad %1 layer %2.wav").arg (pad + 1).arg (layer + 1));
            return needFolder() && saveSound (s, rate, wav, why) ? wav : QString();
        }));
        QFile f (path);
        if (! f.open (QIODevice::WriteOnly | QIODevice::Truncate) || f.write (QJsonDocument (root).toJson()) < 0)
        {
            error = f.errorString();
            return false;
        }
        if (! why.isEmpty())
            error = why;   // saved, but a pad's sound was left out
        return true;
    }

    bool loadKitFile (const QString& path, double rate, Loaded& out, QString& error)
    {
        QFile f (path);
        if (! f.open (QIODevice::ReadOnly))
        {
            error = f.errorString();
            return false;
        }
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson (f.readAll(), &parse);
        if (! doc.isObject() || ! doc.object().contains ("kit"))
        {
            error = parse.error != QJsonParseError::NoError ? parse.errorString() : tr ("This is not a Slicer kit");
            return false;
        }
        const auto root = doc.object();
        const auto base = QFileInfo (path).absoluteDir();
        if (const auto wav = resolvedPath (root.value ("sample").toString(), base); ! wav.isEmpty())
        {
            QString why;
            out.sample = loadSound (wav, rate, why, &out.audio);
            if (out.sample == nullptr)
                out.problems << tr ("The kit's sample %1 could not be read (%2)").arg (wav, why);
        }
        out.kit = fromJson (root.value ("kit").toObject(), base, rate, &out.problems);
        // Where layers start and end is in frames at the rate the kit was saved at.
        const auto saved = root.value ("rate").toDouble (rate);
        if (saved > 0.0 && std::abs (saved - rate) > 0.5)
            for (auto& p : out.kit.pads)
                for (auto& l : p.layers)
                    if (l.used)
                    {
                        l.start = (Frame) std::llround ((double) l.start * rate / saved);
                        l.end = (Frame) std::llround ((double) l.end * rate / saved);
                        if (l.sound != nullptr)
                            l.end = std::min (l.end, l.sound->length());
                    }
        return true;
    }
}
