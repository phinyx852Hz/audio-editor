// SPDX-License-Identifier: AGPL-3.0-only
#include "Recovery.h"
#include "../engine/AudioEngine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>

namespace
{
    // Whether a PHI run with this process id is still going (a crashed one
    // can linger as a zombie for a moment: state Z in /proc/<pid>/stat).
    bool stillRunning (qint64 pid)
    {
        QFile comm (QStringLiteral ("/proc/%1/comm").arg (pid)), stat (QStringLiteral ("/proc/%1/stat").arg (pid));
        if (! comm.open (QIODevice::ReadOnly) || comm.readAll().trimmed() != APP_ID || ! stat.open (QIODevice::ReadOnly))
            return false;
        const auto fields = stat.readAll();
        const auto afterName = fields.lastIndexOf (')');
        return afterName < 0 || fields.mid (afterName + 2, 1) != "Z";
    }

    QString prefixFor (qint64 pid) { return QString::number (pid) + QLatin1Char ('-'); }
}

QString Recovery::folder()
{
    return QStandardPaths::writableLocation (QStandardPaths::GenericCacheLocation) + QStringLiteral ("/" APP_ID "/recovery");
}

Recovery::Recovery()
{
    QDir().mkpath (folder());
}

Recovery::~Recovery()
{
    running.waitForFinished();
}

QString Recovery::runPrefix()
{
    return folder() + QLatin1Char ('/') + prefixFor (QCoreApplication::applicationPid());
}

bool Recovery::sameState (const DocumentState& a, const DocumentState& b)
{
    auto sameMarker = [] (const Marker& x, const Marker& y)
    {
        return x.id == y.id && x.type == y.type && x.start == y.start && x.end == y.end && x.name == y.name;
    };
    return a.data == b.data && a.tags == b.tags
        && std::equal (a.markers.begin(), a.markers.end(), b.markers.begin(), b.markers.end(), sameMarker);
}

void Recovery::update (const std::vector<Item>& items)
{
    // One write at a time; what changed meanwhile is caught on the next call.
    if (running.isRunning())
        return;

    std::vector<Job> jobs;
    for (const auto& item : items)
    {
        const auto found = written.find (item.id);
        if (found == written.end() || ! sameState (found->second, item.state))
        {
            jobs.push_back ({ item, false });
            written[item.id] = item.state;
        }
    }

    for (auto it = written.begin(); it != written.end();)
    {
        const auto listed = std::any_of (items.begin(), items.end(), [&] (const Item& i) { return i.id == it->first; });
        if (listed)
        {
            ++it;
            continue;
        }
        Job gone;
        gone.item.id = it->first;
        gone.remove = true;
        jobs.push_back (gone);
        it = written.erase (it);
    }

    if (! jobs.empty())
        running = QtConcurrent::run ([prefix = runPrefix(), jobs] { run (prefix, jobs); });
}

void Recovery::run (const QString& prefix, const std::vector<Job>& jobs)
{
    for (const auto& job : jobs)
    {
        const auto name = prefix + QString::number (job.item.id);
        if (job.remove)
        {
            QFile::remove (name + QStringLiteral (".wav"));
            QFile::remove (name + QStringLiteral (".ini"));
            continue;
        }

        // Written to a temporary name first, so a crash mid-write keeps the previous copy.
        SaveOptions lossless;
        lossless.bitDepth = 32;
        lossless.dither = false;
        lossless.layout = job.item.state.layout;
        std::string error;
        const auto temp = name + QStringLiteral (".tmp.wav");
        if (job.item.state.data == nullptr
            || ! AudioEngine::saveFile (temp.toStdString(), *job.item.state.data, job.item.state.markers, job.item.state.tags, lossless, error))
        {
            QFile::remove (temp);
            continue;
        }
        QFile::remove (name + QStringLiteral (".wav"));
        QFile::rename (temp, name + QStringLiteral (".wav"));

        QSettings info (name + QStringLiteral (".ini"), QSettings::IniFormat);
        info.setValue (QStringLiteral ("originalPath"), job.item.originalPath);
        info.setValue (QStringLiteral ("name"), job.item.name);
        info.setValue (QStringLiteral ("savedAt"), QDateTime::currentDateTime());
        info.sync();
    }
}

void Recovery::finish()
{
    running.waitForFinished();
    QDir dir (folder());
    for (const auto& f : dir.entryList ({ prefixFor (QCoreApplication::applicationPid()) + QLatin1Char ('*') }, QDir::Files))
        dir.remove (f);
    written.clear();
}

std::vector<Recovery::LeftBehind> Recovery::leftBehind()
{
    std::vector<LeftBehind> result;
    QDir dir (folder());
    for (const auto& f : dir.entryInfoList ({ QStringLiteral ("*.ini") }, QDir::Files, QDir::Time))
    {
        bool ok = false;
        const auto pid = f.completeBaseName().section (QLatin1Char ('-'), 0, 0).toLongLong (&ok);
        const auto audio = f.absolutePath() + QLatin1Char ('/') + f.completeBaseName() + QStringLiteral (".wav");
        if (! ok || stillRunning (pid) || ! QFile::exists (audio))
            continue;

        QSettings info (f.absoluteFilePath(), QSettings::IniFormat);
        result.push_back ({ audio, f.absoluteFilePath(), info.value (QStringLiteral ("originalPath")).toString(),
                            info.value (QStringLiteral ("name")).toString() });
    }
    return result;
}

void Recovery::remove (const LeftBehind& l)
{
    QFile::remove (l.audioFile);
    QFile::remove (l.infoFile);
    QFile::remove (l.audioFile.chopped (4) + QStringLiteral (".tmp.wav"));
}
