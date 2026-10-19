// SPDX-License-Identifier: AGPL-3.0-only
// The Spatial menu: binaural rendering for headphones (ambisonics comes here later).
#include "MainWindow.h"
#include "BinauralDialog.h"
#include "Settings.h"
#include "../core/Binaural.h"
#include "../core/Dsp.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>

namespace
{
    // HRTF sets: your own, then the one installed with PHINYX (or built with it), then the system's.
    QStringList hrtfFiles()
    {
        QStringList folders {
            QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/hrtf"),
            QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHINYX_SHARE_RELATIVE "/hrtf")),
            QFileInfo (QStringLiteral (PHINYX_DEFAULT_HRTF)).absolutePath(),
            QStringLiteral ("/usr/share/libmysofa"),
        };
        QStringList files, seen;
        for (const auto& folder : folders)
            for (const auto& info : QDir (folder).entryInfoList ({ QStringLiteral ("*.sofa") }, QDir::Files, QDir::Name))
            {
                const auto real = info.canonicalFilePath();
                if (real.isEmpty() || seen.contains (real) || info.size() == 0)
                    continue;
                seen << real;
                files << info.absoluteFilePath();
            }
        return files;
    }
}

void MainWindow::binauralRender()
{
    if (refuseAlbumEdit())
        return;
    const auto d = data();
    if (d == nullptr)
        return;
    if (d->numChannels() > 2)
    {
        statusBar()->showMessage (tr ("Binaural works on mono and stereo files (ambisonics and surround come later)"), 6000);
        return;
    }

    const auto files = hrtfFiles();
    if (files.isEmpty())
    {
        QMessageBox::warning (this, tr ("Binaural"), tr ("No HRTF set (.sofa file) was found. Put one in %1.")
                                  .arg (QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/hrtf")));
        return;
    }

    auto settings = appSettings();
    settings.beginGroup (QStringLiteral ("binaural"));
    binaural::Settings initial;
    initial.azimuth = settings.value (QStringLiteral ("azimuth"), initial.azimuth).toDouble();
    initial.elevation = settings.value (QStringLiteral ("elevation"), initial.elevation).toDouble();
    initial.width = settings.value (QStringLiteral ("width"), initial.width).toDouble();
    initial.distance = settings.value (QStringLiteral ("distance"), initial.distance).toDouble();
    initial.room = settings.value (QStringLiteral ("room"), 0.2).toDouble();
    initial.roomSeconds = settings.value (QStringLiteral ("roomSeconds"), initial.roomSeconds).toDouble();
    initial.gainDb = settings.value (QStringLiteral ("gainDb"), initial.gainDb).toDouble();
    initial.hrtfPath = settings.value (QStringLiteral ("hrtf"), files.first()).toString().toStdString();
    if (! QFileInfo::exists (QString::fromStdString (initial.hrtfPath)))
        initial.hrtfPath = files.first().toStdString();

    const auto [s, e] = processRange();
    // The preview plays up to 15 s from the start of the range, looping, in place of your audio.
    const auto previewEnd = std::min<Frame> (e, s + (Frame) (d->sampleRate() * 15.0));
    const auto preview = [this, d, s = s, previewEnd] (const binaural::Settings* chosen)
    {
        if (chosen == nullptr)
        {
            stopExportPreview();
            return;
        }
        std::string error;
        const auto slice = d->slice (s, previewEnd);
        const auto heard = binaural::render (*slice, 0, slice->length(), *chosen, error);
        if (heard == nullptr)
        {
            statusBar()->showMessage (QString::fromStdString (error), 6000);
            return;
        }
        const auto whole = dsp::toStereo (d);
        engine.setReference (whole->replaced (s, previewEnd, heard.get()), 1.0f);
        engine.setReferenceBypassesMaster (false);
        engine.setHearingReference (true);
        if (previewName.isEmpty() || ! engine.isPlaying())
        {
            previewName = tr ("Binaural preview");
            previewRange = { s, previewEnd };
            engine.setLoop (s, previewEnd);
            engine.setPositionFrames (s);
            engine.play();
            placeReferenceBadge();
        }
    };

    BinauralDialog dialog (initial, d->numChannels() == 2, ! hasSelection(), files, preview, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    waveform->setFocus();
    if (! accepted)
        return;

    const auto chosen = dialog.settings();
    settings.setValue (QStringLiteral ("azimuth"), chosen.azimuth);
    settings.setValue (QStringLiteral ("elevation"), chosen.elevation);
    settings.setValue (QStringLiteral ("width"), chosen.width);
    settings.setValue (QStringLiteral ("distance"), chosen.distance);
    settings.setValue (QStringLiteral ("room"), chosen.room);
    settings.setValue (QStringLiteral ("roomSeconds"), chosen.roomSeconds);
    settings.setValue (QStringLiteral ("gainDb"), chosen.gainDb);
    settings.setValue (QStringLiteral ("hrtf"), QString::fromStdString (chosen.hrtfPath));

    AudioDataPtr result;
    std::string error;
    runWithProgress (tr ("Binaural"), [&] (std::atomic<float>& progress, const std::atomic<bool>&)
    {
        result = binaural::render (*d, s, e, chosen, error, &progress);
    });
    if (result == nullptr)
    {
        QMessageBox::warning (this, tr ("Binaural"), QString::fromStdString (error));
        return;
    }

    // The whole file is replaced (a mono file becomes stereo); its length and markers stay the same.
    auto state = doc.current();
    state.data = std::move (result);
    doc.apply (tr ("Binaural").toStdString(), std::move (state));
    refreshFromDocument (true);
    statusBar()->showMessage (tr ("Rendered binaurally: listen on headphones"), 5000);
}
