// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/Album.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

// Album files (.phalbum; .montage before 2026-10-18, still opened): JSON with the album's tags and, per song, the file
// it comes from, the part used, the pause before it, gain, fades and tags.
// Song paths are kept relative to the album when they sit beside it or below,
// so a folder with the album and its songs can be moved as a whole.
namespace albumfile
{
    inline const QString extension = QStringLiteral ("phalbum");
    inline const QString oldExtension = QStringLiteral ("montage");   // what album files were called first
    inline bool isAlbumFile (const QString& path)
    {
        const auto suffix = QFileInfo (path).suffix().toLower();
        return suffix == extension || suffix == oldExtension;
    }

    bool save (const Album&, const QString& path, QString& error);

    // Reads an album and loads its songs (at the album's sample rate). Songs
    // that cannot be read stay in place, silent, and are listed in `problems`.
    // Null when the file itself cannot be read.
    std::shared_ptr<Album> load (const QString& path, QStringList& problems, QString& error,
                                   std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    // Reads one song for an album running at `sampleRate` (0 keeps the file's
    // rate), resampling when they differ. Null on failure, with `error` set.
    // `original` gets the audio as read, before resampling.
    AudioDataPtr loadSong (const QString& path, double sampleRate, Tags& tags, QString& error,
                           AudioDataPtr* original = nullptr, std::atomic<float>* progress = nullptr);
}
