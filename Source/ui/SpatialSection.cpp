// SPDX-License-Identifier: AGPL-3.0-only
// The Spatial menu: binaural rendering for headphones, channel layouts, and ambisonics
// (encode, rotate and mirror, FuMa, decode, the scene view). Also the audio interface mixer.
#include "MainWindow.h"
#include "AmbiDialogs.h"
#include "AmbiScene.h"
#include "BinauralDialog.h"
#include "WaveformView.h"
#include "../core/Ambisonics.h"
#include "Settings.h"
#include "../core/Binaural.h"
#include "../core/Dsp.h"
#include <QAbstractButton>
#include <QCoreApplication>
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
    statusBar()->showMessage (toFuma ? tr ("Converted to FuMa (W X Y Z order) for older tools; PHINYX itself plays and edits AmbiX, so convert back before editing further")
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
            result = ambi::mix (*d, ambi::decoder (order, ambi::layout (target.toStdString())), &progress);
    });
    if (result == nullptr)
    {
        QMessageBox::warning (this, tr ("Decode Ambisonics"), QString::fromStdString (error));
        return;
    }
    auto state = doc.current();
    const int channels = result->numChannels();
    state.data = std::move (result);
    state.layout = headphones ? std::string() : storedLayout (channels, target.toStdString());
    doc.apply (tr ("Decode Ambisonics").toStdString(), std::move (state));
    refreshFromDocument (true);
    statusBar()->showMessage (headphones ? tr ("Decoded for headphones (binaural stereo)") : tr ("Decoded to %1 speakers").arg (target), 6000);
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
