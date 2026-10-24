// SPDX-License-Identifier: AGPL-3.0-only
// Visualizer > Sonify: what the Visualizer shows (any view, MilkDrop too), a picture or
// video file, a camera or the web page behind PHINYX, played as notes (core/Sonify.h)
// on the built-in Synth in the patchbay, or on whatever the MIDI keyboard plays. The
// picture is read small (64 by 48) about 20 times a second, from the UI timer; the notes
// go to the instruments' live MIDI queues, which the audio thread takes each block.
#include "MainWindow.h"
#include "PatchBayWindow.h"
#include "Settings.h"
#include "SonifyPanel.h"
#include "Visualizer.h"
#include "../core/Sonify.h"
#include "../mixer/Instruments.h"
#include "../patchbay/PatchBay.h"
#include "../performance/Cameras.h"
#include "../performance/VideoFile.h"

#if PHINYX_WEB_BACKDROP
 #include "WebBackdrop.h"
#endif

#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>

namespace
{
    constexpr int pictureWidth = 64, pictureHeight = 48;
    constexpr qint64 frameEvery = 50;   // ms

    QString key (const char* name) { return QStringLiteral ("sonify/") + QLatin1String (name); }
}

void MainWindow::registerSonifyCommands()
{
    sonifier = std::make_unique<sonify::Sonifier>();
    sonifyPicture = std::make_unique<sonify::Picture>();
    sonifyClock.start();

    const auto category = tr ("View");
    auto add = [this, &category] (const char* id, const QString& text, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, {}, std::move (fn));
        addAction (action);
        return action;
    };
    auto* toggle = add ("visualizer.sonify", tr ("&Sonify the Visualizer"), [this] { setSonifying (! sonifying); });
    toggle->setCheckable (true);
    toggle->setStatusTip (tr ("Plays what the visualizer shows as notes on a synth: higher up is higher, brighter is louder, change strikes drums"));
    add ("visualizer.sonifyFile", tr ("Sonify a &Picture or Video..."), [this]
    {
        if (chooseSonifySource (SonifyPanel::file))
            setSonifying (true);
    })->setStatusTip (tr ("Shows a picture or a video in the visualizer's Picture view and plays it as notes"));
    add ("visualizer.sonifyCamera", tr ("Sonify a &Camera"), [this]
    {
        if (chooseSonifySource (SonifyPanel::camera))
            setSonifying (true);
    })->setStatusTip (tr ("Shows a camera in the visualizer's Picture view and plays it as notes"));
#if PHINYX_WEB_BACKDROP
    add ("visualizer.sonifyWeb", tr ("Sonify the &Web Page"), [this]
    {
        if (chooseSonifySource (SonifyPanel::web))
            setSonifying (true);
    })->setStatusTip (tr ("Plays the web page behind PHINYX (a video, an animation) as notes"));
#endif
}

// The Sonify bar in the Visualizer window, with the settings it was left with.
void MainWindow::addSonifyPanel()
{
    sonifyPanel = new SonifyPanel (visualizer, PHINYX_WEB_BACKDROP != 0);
    auto s = appSettings();
    sonify::Settings d;
    d.mode = (sonify::Mode) std::clamp (s.value (key ("mode"), (int) d.mode).toInt(), 0, (int) sonify::Mode::spiral);
    d.quantize = s.value (key ("quantize"), d.quantize).toDouble();
    d.scale = (sonify::Scale) std::clamp (s.value (key ("scale"), (int) d.scale).toInt(), 0, (int) sonify::Scale::count - 1);
    d.root = s.value (key ("root"), d.root).toInt();
    d.lowest = s.value (key ("lowest"), d.lowest).toInt();
    d.highest = s.value (key ("highest"), d.highest).toInt();
    d.bpm = s.value (key ("bpm"), d.bpm).toDouble();
    d.columns = s.value (key ("columns"), d.columns).toInt();
    d.voices = s.value (key ("voices"), d.voices).toInt();
    d.threshold = s.value (key ("threshold"), d.threshold).toDouble();
    d.drums = s.value (key ("drums"), d.drums).toBool();
    d.fibonacciDrums = s.value (key ("fibonacciDrums"), d.fibonacciDrums).toBool();
    d.bendRange = s.value (key ("bendRange"), d.bendRange).toDouble();
    sonifyPanel->setSettings (d);
    sonifyPanel->setTarget (s.value (key ("target"), (int) SonifyPanel::builtInSynth).toInt());
    sonifyTarget = sonifyPanel->target();
    sonifier->setSettings (sonifyPanel->settings());
    sonifyPanel->setSource (sonifySource);
    visualizer->setSonifyPanel (sonifyPanel);

    connect (sonifyPanel, &SonifyPanel::playToggled, this, [this] (bool on) { setSonifying (on); });
    connect (sonifyPanel, &SonifyPanel::sourceChosen, this, [this] (int source) { chooseSonifySource (source); });
    connect (sonifyPanel, &SonifyPanel::changed, this, [this]
    {
        if (sonifying && sonifyPanel->target() != sonifyTarget)
        {
            // Let the old instrument's notes go before playing another.
            std::vector<sonify::Message> out;
            sonifier->stop (out);
            sendSonified (out);
        }
        sonifyTarget = sonifyPanel->target();
        sonifier->setSettings (sonifyPanel->settings());
        saveSonifySettings();
    });
}

void MainWindow::saveSonifySettings()
{
    auto s = appSettings();
    const auto d = sonifyPanel->settings();
    s.setValue (key ("mode"), (int) d.mode);
    s.setValue (key ("quantize"), d.quantize);
    s.setValue (key ("scale"), (int) d.scale);
    s.setValue (key ("root"), d.root);
    s.setValue (key ("lowest"), d.lowest);
    s.setValue (key ("highest"), d.highest);
    s.setValue (key ("bpm"), d.bpm);
    s.setValue (key ("columns"), d.columns);
    s.setValue (key ("voices"), d.voices);
    s.setValue (key ("threshold"), d.threshold);
    s.setValue (key ("drums"), d.drums);
    s.setValue (key ("fibonacciDrums"), d.fibonacciDrums);
    s.setValue (key ("bendRange"), d.bendRange);
    s.setValue (key ("target"), sonifyPanel->target());
}

void MainWindow::setSonifying (bool on)
{
    auto* window = visualizerWindow();   // and its Sonify bar
    if (on && ! sonifying)
    {
        sonifier->setSettings (sonifyPanel->settings());
        sonifyTarget = sonifyPanel->target();
        if (sonifySource != SonifyPanel::web && ! visualizerShowing())
        {
            window->show();
            window->raise();
        }
        window->showSonifyPanel (true);
        sonifyLast = sonifyClock.elapsed();
        *sonifyPicture = {};
        sonifying = true;
        const bool synth = sonifyTarget == SonifyPanel::builtInSynth && sonifySynth() >= 0;
        statusBar()->showMessage (synth ? tr ("Sonifying on the built-in Synth (Synth %1 of the patchbay)").arg (sonifySlot + 1)
                                        : tr ("Sonifying on what the MIDI keyboard plays"), 5000);
    }
    else if (! on && sonifying)
    {
        std::vector<sonify::Message> out;
        sonifier->stop (out);
        sendSonified (out);
        sonifying = false;
        for (auto* view : { window->view(), visualBackdrop })
            if (view != nullptr)
                view->setSonifyMarks (-1.0, {});
    }
    sonifyPanel->setPlaying (sonifying);
    commands.action (QStringLiteral ("visualizer.sonify"))->setChecked (sonifying);
}

bool MainWindow::chooseSonifySource (int source)
{
    visualizerWindow();
    bool ok = true;
    if (source == SonifyPanel::file)
    {
        auto s = appSettings();
        auto folder = s.value (key ("folder")).toString();
        if (folder.isEmpty())
            folder = QStandardPaths::writableLocation (QStandardPaths::PicturesLocation);
        QStringList patterns;
        for (const auto& format : QImageReader::supportedImageFormats())
            patterns << QStringLiteral ("*.") + QString::fromLatin1 (format);
        patterns << QStringLiteral ("*.mp4 *.mkv *.webm *.mov *.avi *.m4v *.mpg *.mpeg *.ogv *.wmv *.flv");
        const auto path = QFileDialog::getOpenFileName (this, tr ("Sonify a Picture or Video"), folder,
                                                        tr ("Pictures and videos (%1);;All files (*)").arg (patterns.join (QLatin1Char (' '))));
        ok = ! path.isEmpty() && openSonifyFile (path);
        if (ok)
            s.setValue (key ("folder"), QFileInfo (path).absolutePath());
    }
    else if (source == SonifyPanel::camera)
        ok = startSonifyCamera();
    else
        stopSonifyMotion();
#if PHINYX_WEB_BACKDROP
    if (ok && source == SonifyPanel::web && (webBackdrop == nullptr || ! webBackdrop->isVisible()))
    {
        toggleWebBackdrop();
        ok = webBackdrop != nullptr && webBackdrop->isVisible();
    }
#endif
    if (ok)
    {
        sonifySource = source;
        if (source != SonifyPanel::web)
            for (auto* view : { visualizer->view(), visualBackdrop })
                if (view != nullptr)
                    view->setSonifyMarks (-1.0, {});
    }
    sonifyPanel->setSource (sonifySource);
    *sonifyPicture = {};
    return ok;
}

// A still picture, or a video played over and over; either shows in the Picture view.
bool MainWindow::openSonifyFile (const QString& path)
{
    static const QStringList moving { "mp4", "mkv", "webm", "mov", "avi", "m4v", "gif", "mpg", "mpeg", "ogv", "wmv", "flv", "ts" };
    const QFileInfo info (path);
    stopSonifyMotion();
    if (! moving.contains (info.suffix().toLower()))
    {
        QImageReader reader (path);
        reader.setAutoTransform (true);
        const auto image = reader.read();
        if (! image.isNull())
        {
            showSonifyPicture (image);
            setVisualMode (VisualizerView::modeIndex (QStringLiteral ("picture")), true);
            sonifyPanel->setNote (info.fileName());
            return true;
        }
    }
    sonifyVideo = std::make_unique<perf::VideoFile>();
    sonifyVideo->start (path.toStdString(), 480);
    sonifySerial = 0;
    setVisualMode (VisualizerView::modeIndex (QStringLiteral ("picture")), true);
    sonifyPanel->setNote (info.fileName());
    return true;
}

bool MainWindow::startSonifyCamera()
{
    const auto cameras = perf::listCameras();
    if (cameras.empty())
    {
        QMessageBox::information (this, tr ("Sonify a Camera"), tr ("No camera was found. Plug one in (a webcam or a capture card) and try again."));
        return false;
    }
    size_t chosen = 0;
    if (cameras.size() > 1)
    {
        QStringList names;
        for (const auto& c : cameras)
            names << QStringLiteral ("%1 (%2)").arg (QString::fromStdString (c.name), QString::fromStdString (c.path));
        bool ok = false;
        const auto pick = QInputDialog::getItem (this, tr ("Sonify a Camera"), tr ("Camera:"), names, 0, false, &ok);
        if (! ok)
            return false;
        chosen = (size_t) std::max (0, (int) names.indexOf (pick));
    }
    stopSonifyMotion();
    sonifyCamera = std::make_unique<perf::Camera>();
    sonifyCamera->start (cameras[chosen].path, 320, 240, 15);
    sonifySerial = 0;
    setVisualMode (VisualizerView::modeIndex (QStringLiteral ("picture")), true);
    sonifyPanel->setNote (QString::fromStdString (cameras[chosen].name));
    return true;
}

void MainWindow::stopSonifyMotion()
{
    sonifyCamera.reset();   // stops and waits for its thread
    sonifyVideo.reset();
    if (sonifyPanel != nullptr)
        sonifyPanel->setNote ({});
}

void MainWindow::showSonifyPicture (const QImage& image)
{
    for (auto* view : { visualizer != nullptr ? visualizer->view() : nullptr, visualBackdrop })
        if (view != nullptr)
            view->setPicture (image);
}

// From the UI timer, after the visualizers' tick.
void MainWindow::sonifyTick()
{
    if (sonifier == nullptr)
        return;

    // The camera's or video's newest picture, into the Picture view.
    if (sonifyCamera != nullptr || sonifyVideo != nullptr)
    {
        const auto picture = sonifyCamera != nullptr ? sonifyCamera->latest() : sonifyVideo->latest();
        if (picture != nullptr && picture->serial != sonifySerial)
        {
            sonifySerial = picture->serial;
            const QImage wrapped (picture->pixels.data(), picture->width, picture->height, picture->width * 4, QImage::Format_RGB32);
            showSonifyPicture (wrapped.copy());
        }
        const bool running = sonifyCamera != nullptr ? sonifyCamera->isRunning() : sonifyVideo->isRunning();
        const auto why = sonifyCamera != nullptr ? sonifyCamera->error() : sonifyVideo->error();
        if (! running)
        {
            if (! why.empty())
                statusBar()->showMessage (QString::fromStdString (why), 8000);
            stopSonifyMotion();
        }
    }
    if (! sonifying)
        return;

    const auto now = sonifyClock.elapsed();
    auto* window = visualizer != nullptr && visualizer->isVisible() ? visualizer->view() : nullptr;
    auto* behind = visualBackdrop != nullptr && visualBackdrop->isVisible() ? visualBackdrop : nullptr;
    auto* view = sonifySource == SonifyPanel::web ? nullptr : window != nullptr ? window : behind;

    // What is shown, small: painted once more by the view on its next paint, so it is a tick late.
    QImage seen;
    if (view != nullptr)
    {
        seen = view->takeFrame();
        if (now - sonifyAsked >= frameEvery)
        {
            sonifyAsked = now;
            view->requestFrame (QSize (pictureWidth, pictureHeight));
        }
    }
#if PHINYX_WEB_BACKDROP
    else if (sonifySource == SonifyPanel::web && webBackdrop != nullptr && webBackdrop->isVisible() && now - sonifyAsked >= frameEvery)
    {
        sonifyAsked = now;
        seen = webBackdrop->grab().toImage().scaled (pictureWidth, pictureHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
#endif
    if (! seen.isNull())
    {
        if (seen.format() != QImage::Format_RGB32 && seen.format() != QImage::Format_ARGB32_Premultiplied)
            seen = seen.convertToFormat (QImage::Format_RGB32);
        *sonifyPicture = sonify::fromPixels (seen.constBits(), seen.width(), seen.height(), (int) seen.bytesPerLine(), pictureWidth, pictureHeight);
    }

    std::vector<sonify::Message> out;
    sonifier->advance (*sonifyPicture, (now - sonifyLast) / 1000.0, out);
    sonifyLast = now;
    sendSonified (out);

    if (sonifySource != SonifyPanel::web)
        for (auto* v : { window, behind })
            if (v != nullptr)
                v->setSonifyMarks (sonifier->scanPosition(), sonifier->sounding());
}

void MainWindow::sendSonified (const std::vector<sonify::Message>& out)
{
    if (out.empty())
        return;
    if (sonifyTarget == SonifyPanel::builtInSynth)
        if (const int slot = sonifySynth(); slot >= 0)
        {
            for (const auto& m : out)
                patchBay->liveMidi (slot, m.status, m.data1, m.data2);
            return;
        }
    for (const auto& m : out)
    {
        const int status = m.status & 0xF0;
        playMidi (status, (m.status & 0x0F) + 1, status == 0xE0 ? m.data1 | (m.data2 << 7) : m.data1, m.data2);
    }
}

// The built-in Synth in the patchbay (the one there, or a new one in the first free slot).
int MainWindow::sonifySynth()
{
    if (patchBay == nullptr)
        return -1;
    const auto isSynth = [this] (int i)
    {
        const auto s = patchBay->synth (i);
        return s != nullptr && s->kind() == mixer::Instrument::Kind::synth;
    };
    if (sonifySlot >= 0 && isSynth (sonifySlot))
        return sonifySlot;
    sonifySlot = -1;
    for (int i = 0; i < patchbay::maxSynths && sonifySlot < 0; ++i)
        if (isSynth (i))
            sonifySlot = i;
    for (int i = 0; i < patchbay::maxSynths && sonifySlot < 0; ++i)
        if (patchBay->synth (i) == nullptr)
        {
            playInstruments();
            patchBay->setSynth (i, std::make_shared<mixer::Synth>());
            if (patchBayWin != nullptr)
                patchBayWin->refill();
            patchBaySave.start();
            sonifySlot = i;
        }
    if (sonifySlot >= 0)
        playInstruments();
    else
    {
        sonifyTarget = SonifyPanel::keyboard;
        if (sonifyPanel != nullptr)
            sonifyPanel->setTarget (sonifyTarget);
        statusBar()->showMessage (tr ("All %1 patchbay synths are in use: sonifying plays what the MIDI keyboard plays instead").arg (patchbay::maxSynths), 6000);
    }
    return sonifySlot;
}
