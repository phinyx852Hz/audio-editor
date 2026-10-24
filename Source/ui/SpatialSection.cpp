// SPDX-License-Identifier: AGPL-3.0-only
// The Spatial menu: binaural rendering for headphones, channel layouts, and ambisonics
// (encode, rotate and mirror, FuMa, decode, the scene view). Also the audio interface mixer.
#include "MainWindow.h"
#include "AmbiDialogs.h"
#include "AmbiScene.h"
#include "AmbiStudio.h"
#include "SpeakerArrays.h"
#include "BinauralDialog.h"
#include "MidSideDialog.h"
#include "WaveformView.h"
#include "../core/Ambisonics.h"
#include "Settings.h"
#include "../core/Binaural.h"
#include "../core/Dsp.h"
#include "../core/MidSide.h"
#include "../core/SpatialStudio.h"
#include "../engine/Codec.h"
#include <QAbstractButton>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QInputDialog>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>

#include <map>
#include <mutex>

namespace
{
    // HRTF sets: your own, then the one installed with PHI (or built with it), then the system's.
    QStringList hrtfFiles()
    {
        QStringList folders {
            QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/hrtf"),
            QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHI_SHARE_RELATIVE "/hrtf")),
            QFileInfo (QStringLiteral (PHI_DEFAULT_HRTF)).absolutePath(),
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

    QString orderLayout (int order)
    {
        static const char* names[] { "", "Ambisonics (1st order)", "Ambisonics (2nd order)", "Ambisonics (3rd order)" };
        return QString::fromLatin1 (names[std::clamp (order, 1, 3)]);
    }

    // The layout name to store: empty when it is the usual one for that many channels.
    std::string storedLayout (int channels, const std::string& name)
    {
        return name == dsp::speakerLayout (channels).name ? std::string() : name;
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

//==============================================================================
// Process or Spatial > Mid/Side Width.
void MainWindow::midSideWidth()
{
    if (refuseAlbumEdit())
        return;
    const auto d = data();
    if (d == nullptr)
        return;
    if (d->numChannels() > 2)
    {
        statusBar()->showMessage (tr ("Mid/Side Width works on mono and stereo files"), 6000);
        return;
    }

    auto settings = appSettings();
    settings.beginGroup (QStringLiteral ("midside"));
    midside::Settings initial;
    initial.width = settings.value (QStringLiteral ("width"), initial.width).toDouble();
    initial.huge = settings.value (QStringLiteral ("huge"), initial.huge).toBool();
    initial.monoBelowHz = settings.value (QStringLiteral ("monoBelowHz"), initial.monoBelowHz).toDouble();
    initial.generate = settings.value (QStringLiteral ("generate"), initial.generate).toDouble();
    initial.airDb = settings.value (QStringLiteral ("airDb"), initial.airDb).toDouble();
    initial.rotation = settings.value (QStringLiteral ("rotation"), initial.rotation).toDouble();
    initial.lift = settings.value (QStringLiteral ("lift"), initial.lift).toDouble();
    initial.matchLevel = settings.value (QStringLiteral ("matchLevel"), initial.matchLevel).toBool();
    initial.golden = settings.value (QStringLiteral ("golden"), initial.golden).toBool();

    const auto [s, e] = processRange();
    // The preview plays up to 15 s from the start of the range, looping, in place of your audio.
    const auto previewEnd = std::min<Frame> (e, s + (Frame) (d->sampleRate() * 15.0));
    const auto sample = dsp::toStereo (d->slice (s, previewEnd));
    const auto play = [this, d, s = s, previewEnd] (AudioDataPtr heard)
    {
        if (heard == nullptr)
        {
            stopExportPreview();
            return;
        }
        engine.setReference (dsp::toStereo (d)->replaced (s, previewEnd, heard.get()), 1.0f);
        engine.setReferenceBypassesMaster (false);
        if (previewName.isEmpty() || ! engine.isPlaying())
        {
            engine.setHearingReference (true);
            previewName = tr ("Mid/Side preview");
            previewRange = { s, previewEnd };
            engine.setLoop (s, previewEnd);
            engine.setPositionFrames (s);
            engine.play();
            placeReferenceBadge();
        }
    };
    const auto flip = [this]
    {
        engine.setHearingReference (! engine.isHearingReference());
        placeReferenceBadge();
        return engine.isHearingReference();
    };

    MidSideDialog dialog (initial, ! hasSelection(), sample, play, flip, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    waveform->setFocus();
    if (! accepted)
        return;

    const auto chosen = dialog.settings();
    settings.setValue (QStringLiteral ("width"), chosen.width);
    settings.setValue (QStringLiteral ("huge"), chosen.huge);
    settings.setValue (QStringLiteral ("monoBelowHz"), chosen.monoBelowHz);
    settings.setValue (QStringLiteral ("generate"), chosen.generate);
    settings.setValue (QStringLiteral ("airDb"), chosen.airDb);
    settings.setValue (QStringLiteral ("rotation"), chosen.rotation);
    settings.setValue (QStringLiteral ("lift"), chosen.lift);
    settings.setValue (QStringLiteral ("matchLevel"), chosen.matchLevel);
    settings.setValue (QStringLiteral ("golden"), chosen.golden);

    AudioDataPtr result;
    runWithProgress (tr ("Mid/Side Width"), [&] (std::atomic<float>&, const std::atomic<bool>&)
    {
        result = midside::render (*d, s, e, chosen);
    });
    if (result == nullptr)
        return;
    // The whole file is replaced (a mono file becomes stereo); its length and markers stay the same.
    auto state = doc.current();
    state.data = std::move (result);
    doc.apply (tr ("Mid/Side Width").toStdString(), std::move (state));
    refreshFromDocument (true);
    const auto after = midside::analyse (*data(), s, e);
    statusBar()->showMessage (tr ("Mid/Side Width applied: correlation %1, mono sum %2 dB").arg (after.correlation, 0, 'f', 2).arg (after.monoSumDb, 0, 'f', 1), 6000);
}

//==============================================================================
int MainWindow::ambisonicOrderOrHint (const QString& what)
{
    if (refuseAlbumEdit())
        return 0;
    const auto d = data();
    if (d == nullptr)
        return 0;
    const int order = dsp::ambisonicOrder (doc.current().layout);
    if (order <= 0)
    {
        statusBar()->showMessage (ambi::orderFor (d->numChannels()) > 0
                                      ? tr ("%1 needs an ambisonic file: mark it as one with Spatial > Channel Layout (Ctrl+Alt+Shift+L)").arg (what)
                                      : tr ("%1 needs an ambisonic file (AmbiX): encode one with Spatial > Encode to Ambisonics").arg (what), 7000);
        return 0;
    }
    return order;
}

void MainWindow::chooseChannelLayout()
{
    if (refuseAlbumEdit())
        return;
    const auto d = data();
    if (d == nullptr)
        return;
    const auto choices = dsp::layoutChoices (d->numChannels());
    if (choices.size() < 2)
    {
        statusBar()->showMessage (tr ("%1 channels can only be %2").arg (d->numChannels()).arg (QString::fromStdString (choices.front())), 5000);
        return;
    }
    QStringList items;
    for (const auto& c : choices)
        items << QString::fromStdString (c);
    const auto now = QString::fromStdString (dsp::speakerLayout (d->numChannels(), doc.current().layout).name);
    bool ok = false;
    const auto chosen = QInputDialog::getItem (this, tr ("Channel Layout"),
                                               tr ("What the %1 channels are (this changes only how they are labelled, played and processed, not the audio):")
                                                   .arg (d->numChannels()),
                                               items, std::max (0, (int) items.indexOf (now)), false, &ok);
    waveform->setFocus();
    if (! ok || chosen == now)
        return;
    auto state = doc.current();
    state.layout = storedLayout (d->numChannels(), chosen.toStdString());
    doc.apply (tr ("Channel Layout: %1").arg (chosen).toStdString(), std::move (state));
    refreshFromDocument (false);
    statusBar()->showMessage (tr ("Channel layout: %1").arg (chosen), 4000);
}

void MainWindow::ambiEncode()
{
    if (refuseAlbumEdit())
        return;
    const auto d = data();
    if (d == nullptr)
        return;
    if (d->numChannels() > 2)
    {
        statusBar()->showMessage (tr ("Encode to Ambisonics works on mono and stereo files"), 6000);
        return;
    }

    auto settings = appSettings();
    settings.beginGroup (QStringLiteral ("ambisonics"));
    ambi::EncodeSettings initial;
    initial.order = settings.value (QStringLiteral ("order"), 1).toInt();
    initial.azimuth = settings.value (QStringLiteral ("azimuth"), 0.0).toDouble();
    initial.elevation = settings.value (QStringLiteral ("elevation"), 0.0).toDouble();
    initial.width = settings.value (QStringLiteral ("width"), 60.0).toDouble();

    AmbiEncodeDialog dialog (initial, d->numChannels() == 2, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    waveform->setFocus();
    if (! accepted)
        return;
    const auto chosen = dialog.settings();
    settings.setValue (QStringLiteral ("order"), chosen.order);
    settings.setValue (QStringLiteral ("azimuth"), chosen.azimuth);
    settings.setValue (QStringLiteral ("elevation"), chosen.elevation);
    settings.setValue (QStringLiteral ("width"), chosen.width);

    AudioDataPtr result;
    runWithProgress (tr ("Encode to Ambisonics"), [&] (std::atomic<float>&, const std::atomic<bool>&)
    {
        result = ambi::encode (*d, chosen);
    });
    if (result == nullptr)
        return;
    auto state = doc.current();
    state.data = std::move (result);
    state.layout = orderLayout (chosen.order).toStdString();
    doc.apply (tr ("Encode to Ambisonics").toStdString(), std::move (state));
    refreshFromDocument (true);
    statusBar()->showMessage (tr ("Now %1 (AmbiX): it plays decoded to your speakers. See where the sound is with Spatial > Ambisonic Scene")
                                  .arg (orderLayout (chosen.order)), 7000);
}

void MainWindow::ambiRotate()
{
    const int order = ambisonicOrderOrHint (tr ("Rotate / Mirror"));
    if (order <= 0)
        return;
    const auto d = data();
    const auto [s, e] = processRange();
    const auto previewEnd = std::min<Frame> (e, s + (Frame) (d->sampleRate() * 15.0));
    const auto preview = [this, d, s = s, previewEnd] (const ambi::Matrix* m)
    {
        if (m == nullptr)
        {
            stopExportPreview();
            return;
        }
        const auto slice = d->slice (s, previewEnd);
        const auto turned = ambi::transform (*slice, 0, slice->length(), *m);
        engine.setReference (d->replaced (s, previewEnd, turned.get()), 1.0f);
        engine.setReferenceBypassesMaster (false);
        engine.setHearingReference (true);
        if (previewName.isEmpty() || ! engine.isPlaying())
        {
            previewName = tr ("Rotate preview");
            previewRange = { s, previewEnd };
            engine.setLoop (s, previewEnd);
            engine.setPositionFrames (s);
            engine.play();
            placeReferenceBadge();
        }
    };

    AmbiRotateDialog dialog (order, ! hasSelection(), preview, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    waveform->setFocus();
    if (! accepted)
        return;
    const auto m = dialog.matrix();
    AudioDataPtr result;
    runWithProgress (tr ("Rotate / Mirror"), [&] (std::atomic<float>&, const std::atomic<bool>&)
    {
        result = ambi::transform (*d, s, e, m);
    });
    if (result == nullptr)
        return;
    auto state = doc.current();
    state.data = std::move (result);
    doc.apply (tr ("Rotate / Mirror Scene").toStdString(), std::move (state));
    refreshFromDocument (false);
}

void MainWindow::ambiConvertFuma (bool toFuma)
{
    const auto what = toFuma ? tr ("AmbiX to FuMa") : tr ("FuMa to AmbiX");
    const int order = ambisonicOrderOrHint (what);
    if (order <= 0)
        return;
    const auto d = data();
    const auto m = toFuma ? ambi::ambixToFuma (order) : ambi::fumaToAmbix (order);
    AudioDataPtr result;
    runWithProgress (what, [&] (std::atomic<float>& progress, const std::atomic<bool>&)
    {
        result = ambi::mix (*d, m, &progress);
    });
    if (result == nullptr)
        return;
    auto state = doc.current();
    state.data = std::move (result);
    doc.apply (what.toStdString(), std::move (state));
    refreshFromDocument (false);
    statusBar()->showMessage (toFuma ? tr ("Converted to FuMa (W X Y Z order) for older tools; PHI itself plays and edits AmbiX, so convert back before editing further")
                                     : tr ("Converted from FuMa to AmbiX"), 8000);
}

void MainWindow::ambiDecode()
{
    const int order = ambisonicOrderOrHint (tr ("Decode Ambisonics"));
    if (order <= 0)
        return;
    const auto d = data();
    const auto files = hrtfFiles();
    auto settings = appSettings();
    settings.beginGroup (QStringLiteral ("ambisonics"));
    const auto initialHrtf = settings.value (QStringLiteral ("hrtf"), files.isEmpty() ? QString() : files.first()).toString();

    AmbiDecodeDialog dialog (settings.value (QStringLiteral ("decodeTo"), QStringLiteral ("Headphones")).toString(), files, initialHrtf, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    waveform->setFocus();
    if (! accepted)
        return;
    const auto target = dialog.target();
    settings.setValue (QStringLiteral ("decodeTo"), target);
    const bool headphones = target == QStringLiteral ("Headphones");
    const bool custom = studio::isCustom (target.toStdString());
    if (custom && speakerarrays::get (target.mid (8)).empty())
    {
        QMessageBox::warning (this, tr ("Decode Ambisonics"), tr ("The speaker array %1 is missing or not usable: check it in Spatial > Speaker Arrays").arg (target.mid (8)));
        return;
    }
    if (headphones)
    {
        if (dialog.hrtf().isEmpty())
        {
            QMessageBox::warning (this, tr ("Decode Ambisonics"), tr ("No HRTF set (.sofa file) was found. Put one in %1.")
                                      .arg (QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/hrtf")));
            return;
        }
        settings.setValue (QStringLiteral ("hrtf"), dialog.hrtf());
    }

    AudioDataPtr result;
    std::string error;
    runWithProgress (tr ("Decode Ambisonics"), [&] (std::atomic<float>& progress, const std::atomic<bool>&)
    {
        if (headphones)
            result = ambi::decodeBinaural (*d, dialog.hrtf().toStdString(), error, &progress);
        else
            result = ambi::mix (*d, ambi::decoder (order, custom ? speakerarrays::get (target.mid (8)) : ambi::layout (target.toStdString())), &progress);
    });
    if (result == nullptr)
    {
        QMessageBox::warning (this, tr ("Decode Ambisonics"), QString::fromStdString (error));
        return;
    }
    auto state = doc.current();
    const int channels = result->numChannels();
    state.data = std::move (result);
    state.layout = headphones || custom ? std::string() : storedLayout (channels, target.toStdString());
    doc.apply (tr ("Decode Ambisonics").toStdString(), std::move (state));
    refreshFromDocument (true);
    statusBar()->showMessage (headphones ? tr ("Decoded for headphones (binaural stereo)") : tr ("Decoded to %1 speakers").arg (target), 6000);
}

//==============================================================================
// Spatial > Ambisonic Studio: E4L's devices in a floating panel that stays open while you work.
void MainWindow::ambiStudio()
{
    if (refuseAlbumEdit())
        return;
    if (ambiStudioWindow != nullptr)
    {
        ambiStudioWindow->show();
        ambiStudioWindow->raise();
        ambiStudioWindow->activateWindow();
        return;
    }

    const auto studioPreview = tr ("Ambisonic Studio preview");
    AmbiStudio::Host host;
    host.hrtfFiles = hrtfFiles();
    host.input = [this] (AmbiStudio::Input& in)
    {
        const auto d = data();
        if (d == nullptr || doc.isAlbum())
            return false;
        in.data = d;
        in.inputOrder = dsp::ambisonicOrder (doc.current().layout);
        const auto [s, e] = processRange();
        in.start = s;
        in.end = e;
        in.bpm = doc.current().grid.bpm;
        return true;
    };
    host.preview = [this, studioPreview] (AudioDataPtr heard, Frame s, Frame e)
    {
        if (heard == nullptr)
        {
            if (previewName == studioPreview)
                stopExportPreview();
            return;
        }
        const auto d = data();
        if (d == nullptr)
            return;
        engine.setReference (dsp::toStereo (d)->replaced (s, e, heard.get()), 1.0f);
        engine.setReferenceBypassesMaster (false);
        engine.setHearingReference (true);
        if (previewName != studioPreview || ! engine.isPlaying() || previewRange != std::pair { s, e })
        {
            previewName = studioPreview;
            previewRange = { s, e };
            engine.setLoop (s, e);
            engine.setPositionFrames (s);
            engine.play();
            placeReferenceBadge();
        }
    };
    host.previewSeconds = [this, studioPreview]
    {
        const auto d = data();
        if (d == nullptr || previewName != studioPreview || ! engine.isPlaying() || ! engine.isHearingReference())
            return -1.0;
        return (double) (engine.heardPositionFrames() - previewRange.first) / d->sampleRate();
    };
    // Impulse responses are read once (until the file changes): every preview render asks again.
    host.load = [] (const std::string& path) -> AudioDataPtr
    {
        static std::mutex lock;
        static std::map<std::string, std::pair<QDateTime, AudioDataPtr>> cache;
        const auto changed = QFileInfo (QString::fromStdString (path)).lastModified();
        {
            std::lock_guard<std::mutex> guard (lock);
            if (const auto it = cache.find (path); it != cache.end() && it->second.first == changed)
                return it->second.second;
        }
        std::string error;
        auto read = codec::decode (path, error);
        if (read != nullptr)
        {
            std::lock_guard<std::mutex> guard (lock);
            if (cache.size() > 16)
                cache.clear();
            cache[path] = { changed, read };
        }
        return read;
    };
    host.apply = [this, load = host.load] (const studio::Scene& scene)
    {
        const auto d = data();
        if (d == nullptr || refuseAlbumEdit())
            return;
        if (scene.master.decoder == "Headphones" && scene.master.hrtfPath.empty())
        {
            QMessageBox::warning (ambiStudioWindow, tr ("Ambisonic Studio"), tr ("No HRTF set (.sofa file) was found. Put one in %1.")
                                      .arg (QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/hrtf")));
            return;
        }
        const int inputOrder = dsp::ambisonicOrder (doc.current().layout);
        AudioDataPtr result;
        std::string error;
        const bool finished = runWithProgress (tr ("Ambisonic Studio"), [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
        {
            std::atomic<float> stage { 0.0f };
            const auto rendered = studio::render (*d, inputOrder, scene, load, &progress, &cancel);
            error = rendered.error;
            if (rendered.scene == nullptr)
                return;
            progress.store (0.99f);
            result = studio::decode (*rendered.scene, scene.master, error, &stage);
        });
        if (! finished)
            return;
        if (result == nullptr)
        {
            QMessageBox::warning (ambiStudioWindow, tr ("Ambisonic Studio"), QString::fromStdString (error));
            return;
        }
        auto state = doc.current();
        const int channels = result->numChannels();
        state.data = std::move (result);
        state.layout = storedLayout (channels, studio::decodedLayout (scene.master, scene.order));
        doc.apply (tr ("Ambisonic Studio").toStdString(), std::move (state));
        refreshFromDocument (true);
        ambiStudioWindow->inputChanged();
        const auto what = scene.master.decoder == "Headphones" ? tr ("binaural stereo for headphones")
                        : scene.master.decoder == "AmbiX" || scene.master.decoder == "FuMa"
                            ? tr ("%1 (%2)").arg (QString::fromStdString (studio::decodedLayout (scene.master, scene.order)), QString::fromStdString (scene.master.decoder))
                            : tr ("%1 speakers").arg (QString::fromStdString (scene.master.decoder));
        statusBar()->showMessage (tr ("Ambisonic Studio rendered the file as %1. Undo brings the original back").arg (what), 8000);
    };

    ambiStudioWindow = new AmbiStudio (std::move (host), this);
    ambiStudioWindow->show();
}

// Spatial > Speaker Arrays: your own speaker setups (up to 16 channels) for the decoders.
void MainWindow::speakerArrays()
{
    SpeakerArraysDialog dialog ({}, this);
    dialog.exec();
    waveform->setFocus();
}

void MainWindow::feedAmbiScene()
{
    const auto d = data();
    if (d == nullptr || ! ambiSceneView->isVisible())
        return;
    const int order = dsp::ambisonicOrder (doc.current().layout);
    if (order <= 0)
    {
        ambiSceneView->analyse ({}, false);
        return;
    }
    constexpr int frames = 2048;
    const auto at = engine.isPlaying() ? engine.heardPositionFrames() : cursor;
    std::vector<std::vector<float>> channels ((size_t) d->numChannels(), std::vector<float> (frames));
    std::vector<float*> pointers;
    for (auto& c : channels)
        pointers.push_back (c.data());
    d->read (std::max<Frame> (0, at - frames / 2), frames, pointers.data());
    ambiSceneView->analyse (channels, true);
}

//==============================================================================
// Options > Audio Interface Mixer: the interface's own routing and mixer program
// (alsa-scarlett-gui for Focusrite Scarlett and Clarett), brought to the front.
namespace
{
    QString mixerProgram()
    {
        return appSettings().value (QStringLiteral ("interfaceMixer/program"), QStringLiteral ("alsa-scarlett-gui")).toString();
    }
}

void MainWindow::openInterfaceMixer()
{
    const auto command = QProcess::splitCommand (mixerProgram());
    if (command.isEmpty())
    {
        chooseInterfaceMixer();
        return;
    }
    const auto program = command.first();
    const auto found = QStandardPaths::findExecutable (program);
    if (found.isEmpty() && ! QFileInfo (program).isExecutable())
    {
        QMessageBox box (QMessageBox::Information, tr ("Audio Interface Mixer"),
                         tr ("%1 is not installed.").arg (program), QMessageBox::Close, this);
        box.setInformativeText (program == QStringLiteral ("alsa-scarlett-gui")
                                    ? tr ("For Focusrite Scarlett and Clarett interfaces, install it in a terminal with:\n\n"
                                          "    yay -S alsa-scarlett-gui\n\n(on Arch and Omarchy; other systems may have a package or a Flatpak). "
                                          "Then press this key again.")
                                    : tr ("Install it, or choose another program."));
        auto* other = box.addButton (tr ("&Choose Program..."), QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == other)
            chooseInterfaceMixer();
        return;
    }

    // Already open: bring it to the front (Hyprland can do that from here).
    QProcess running;
    running.start (QStringLiteral ("pgrep"), { QStringLiteral ("-x"), QFileInfo (program).fileName().left (15) });
    const bool isRunning = running.waitForFinished (1000) && running.exitCode() == 0;
    if (isRunning && qEnvironmentVariableIsSet ("HYPRLAND_INSTANCE_SIGNATURE"))
    {
        const auto pid = QString::fromLatin1 (running.readAllStandardOutput()).split (QLatin1Char ('\n'), Qt::SkipEmptyParts).value (0).trimmed();
        if (QProcess::startDetached (QStringLiteral ("hyprctl"), { QStringLiteral ("dispatch"), QStringLiteral ("focuswindow"), QStringLiteral ("pid:") + pid }))
        {
            statusBar()->showMessage (tr ("%1 is open: brought to the front").arg (program), 4000);
            return;
        }
    }
    if (isRunning)
    {
        statusBar()->showMessage (tr ("%1 is already open in another window").arg (program), 5000);
        return;
    }
    if (! QProcess::startDetached (program, command.mid (1)))
    {
        statusBar()->showMessage (tr ("Could not start %1").arg (program), 6000);
        return;
    }
    statusBar()->showMessage (tr ("Opening %1").arg (program), 4000);
}

void MainWindow::chooseInterfaceMixer()
{
    bool ok = false;
    const auto program = QInputDialog::getText (this, tr ("Audio Interface Mixer Program"),
                                                tr ("The program that sets up your audio interface's routing and mixer\n"
                                                    "(alsa-scarlett-gui for Focusrite Scarlett and Clarett, or for example alsamixer in a terminal: kitty alsamixer):"),
                                                QLineEdit::Normal, mixerProgram(), &ok);
    waveform->setFocus();
    if (! ok || program.trimmed().isEmpty())
        return;
    auto settings = appSettings();
    settings.setValue (QStringLiteral ("interfaceMixer/program"), program.trimmed());
    statusBar()->showMessage (tr ("The audio interface mixer key now opens %1").arg (program.trimmed()), 5000);
}
