// SPDX-License-Identifier: AGPL-3.0-only
#include "AlbumFile.h"

#include "../core/Dsp.h"
#include "../engine/AudioEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>

namespace albumfile
{
    namespace
    {
        constexpr int formatVersion = 1;

        QJsonObject tagsToJson (const Tags& tags)
        {
            QJsonObject o;
            for (const auto& [key, value] : tags)
                if (! value.empty())
                    o.insert (QString::fromStdString (key), QString::fromStdString (value));
            return o;
        }

        Tags tagsFromJson (const QJsonObject& o)
        {
            Tags tags;
            for (auto it = o.begin(); it != o.end(); ++it)
                tags[it.key().toStdString()] = it.value().toString().toStdString();
            return tags;
        }

        Frame frames (const QJsonValue& v) { return (Frame) std::llround (v.toDouble()); }
    }

    bool save (const Album& m, const QString& path, QString& error)
    {
        const auto folder = QFileInfo (path).absoluteDir();
        QJsonArray songs;
        for (const auto& c : m.clips)
        {
            const auto songPath = QString::fromStdString (c.path);
            auto relative = folder.relativeFilePath (songPath);
            QJsonObject o {
                { QStringLiteral ("file"), relative.startsWith (QStringLiteral ("../")) ? songPath : relative },
                { QStringLiteral ("start"), (double) c.sourceStart },
                { QStringLiteral ("end"), (double) c.sourceEnd },
                { QStringLiteral ("pause"), (double) c.pauseBefore },
                { QStringLiteral ("gainDb"), c.gainDb },
                { QStringLiteral ("fadeIn"), (double) c.fadeIn },
                { QStringLiteral ("fadeOut"), (double) c.fadeOut },
            };
            if (! c.tags.empty())
                o.insert (QStringLiteral ("tags"), tagsToJson (c.tags));
            songs.append (o);
        }

        const QJsonObject root {
            { QStringLiteral ("phinyxAlbum"), formatVersion },
            { QStringLiteral ("sampleRate"), m.sampleRate },
            { QStringLiteral ("channels"), m.channels },
            { QStringLiteral ("tags"), tagsToJson (m.tags) },
            { QStringLiteral ("songs"), songs },
        };

        QSaveFile file (path);
        if (! file.open (QIODevice::WriteOnly))
        {
            error = file.errorString();
            return false;
        }
        file.write (QJsonDocument (root).toJson());
        if (! file.commit())
        {
            error = file.errorString();
            return false;
        }
        return true;
    }

    AudioDataPtr loadSong (const QString& path, double sampleRate, Tags& tags, QString& error, AudioDataPtr* original,
                           std::atomic<float>* progress)
    {
        std::vector<Marker> markers;
        std::string loadError;
        auto data = AudioEngine::loadFile (path.toStdString(), markers, tags, loadError, progress);
        if (data == nullptr)
        {
            error = QString::fromStdString (loadError);
            return nullptr;
        }
        if (original != nullptr)
            *original = data;
        if (sampleRate > 0.0 && std::abs (data->sampleRate() - sampleRate) > 1.0e-6)
            data = dsp::resample (*data, sampleRate);
        return data;
    }

    std::shared_ptr<Album> load (const QString& path, QStringList& problems, QString& error,
                                   std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        QFile file (path);
        if (! file.open (QIODevice::ReadOnly))
        {
            error = file.errorString();
            return nullptr;
        }
        QJsonParseError parseError;
        const auto json = QJsonDocument::fromJson (file.readAll(), &parseError);
        const auto root = json.object();
        if (json.isNull() || ! (root.contains (QStringLiteral ("phinyxAlbum")) || root.contains (QStringLiteral ("phinyxMontage"))))
        {
            error = json.isNull() ? parseError.errorString() : QObject::tr ("This is not a PHINYX album.");
            return nullptr;
        }

        auto m = std::make_shared<Album>();
        m->sampleRate = root.value (QStringLiteral ("sampleRate")).toDouble (44100.0);
        m->channels = std::clamp (root.value (QStringLiteral ("channels")).toInt (2), 1, MeterBlock::maxChannels);
        m->tags = tagsFromJson (root.value (QStringLiteral ("tags")).toObject());

        const auto folder = QFileInfo (path).absoluteDir();
        const auto songs = root.value (QStringLiteral ("songs")).toArray();
        for (int i = 0; i < songs.size(); ++i)
        {
            if (cancel != nullptr && cancel->load())
                return nullptr;

            const auto o = songs[i].toObject();
            AlbumClip c;
            const auto songPath = QDir::cleanPath (folder.absoluteFilePath (o.value (QStringLiteral ("file")).toString()));
            c.path = songPath.toStdString();
            c.sourceStart = frames (o.value (QStringLiteral ("start")));
            c.sourceEnd = frames (o.value (QStringLiteral ("end")));
            c.pauseBefore = frames (o.value (QStringLiteral ("pause")));
            c.gainDb = o.value (QStringLiteral ("gainDb")).toDouble();
            c.fadeIn = frames (o.value (QStringLiteral ("fadeIn")));
            c.fadeOut = frames (o.value (QStringLiteral ("fadeOut")));
            c.tags = tagsFromJson (o.value (QStringLiteral ("tags")).toObject());

            Tags fileTags;
            QString songError;
            c.source = loadSong (songPath, m->sampleRate, fileTags, songError, &c.original);
            if (c.source == nullptr)
                problems << QObject::tr ("%1: %2").arg (songPath, songError);
            m->clips.push_back (std::move (c));
            if (progress != nullptr)
                progress->store ((float) (i + 1) / (float) songs.size());
        }
        m->clampPauses();
        return m;
    }
}
