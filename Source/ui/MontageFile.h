// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/Montage.h"

#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

// Montage files (.montage): JSON with the album's tags and, per song, the file
// it comes from, the part used, the pause before it, gain, fades and tags.
// Song paths are kept relative to the montage when they sit beside it or below,
// so a folder with the montage and its songs can be moved as a whole.
namespace montagefile
{
    inline const QString extension = QStringLiteral ("montage");

    bool save (const Montage&, const QString& path, QString& error);

    // Reads a montage and loads its songs (at the montage's sample rate). Songs
    // that cannot be read stay in place, silent, and are listed in `problems`.
    // Null when the file itself cannot be read.
    std::shared_ptr<Montage> load (const QString& path, QStringList& problems, QString& error,
                                   std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    // Reads one song for a montage running at `sampleRate` (0 keeps the file's
    // rate), resampling when they differ. Null on failure, with `error` set.
    // `original` gets the audio as read, before resampling.
    AudioDataPtr loadSong (const QString& path, double sampleRate, Tags& tags, QString& error,
                           AudioDataPtr* original = nullptr, std::atomic<float>* progress = nullptr);
}
