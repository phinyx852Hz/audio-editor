// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"

#include <QFuture>
#include <QString>

#include <cstdint>
#include <map>
#include <vector>

// Autosave and crash recovery. Every open file with unsaved changes is
// written now and then (audio, markers and tags, as a 32-bit float WAV) to
// ~/.cache/phi/recovery, named after this run's process id. A clean quit
// removes them; after a crash the next start finds files whose run is gone
// and offers them back. The undo history is not kept.
class Recovery
{
public:
    Recovery();
    ~Recovery();

    struct Item
    {
        std::uint64_t id = 0;       // AudioDocument::id()
        DocumentState state;
        QString originalPath;       // empty for an untitled file
        QString name;               // as on its tab
    };

    // The files that need keeping safe right now. Changed ones are written in
    // the background; ones no longer listed (saved, closed) are removed.
    void update (const std::vector<Item>&);

    // Quitting normally: waits for a write in progress, then removes this run's files.
    void finish();

    struct LeftBehind
    {
        QString audioFile, infoFile;
        QString originalPath, name;
    };
    // Files from runs of PHI that are no longer running.
    static std::vector<LeftBehind> leftBehind();
    static void remove (const LeftBehind&);
    static QString folder();

private:
    struct Job
    {
        Item item;
        bool remove = false;
    };

    static QString runPrefix();   // the folder plus this run's process id
    static bool sameState (const DocumentState&, const DocumentState&);
    static void run (const QString& prefix, const std::vector<Job>&);

    std::map<std::uint64_t, DocumentState> written;   // what each file last held
    QFuture<void> running;
};
