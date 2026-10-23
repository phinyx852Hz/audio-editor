// SPDX-License-Identifier: AGPL-3.0-only
// The Warp menu: the beat grid (tempo, downbeat, bars over the waveform) and
// stretching to a tempo with Rubber Band (core/TimeStretch.h). Warp markers,
// quantizing (QuantizeSection.cpp) and loop export build on these.
#include "MainWindow.h"

#include "QuantizePanel.h"
#include "Settings.h"
#include "TimeRuler.h"
#include "TransportBar.h"
#include "WaveformView.h"
#include "../core/TimeStretch.h"
#include "../engine/VampHost.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <cmath>

namespace
{
    constexpr auto beatTracker = "qm-vamp-plugins:qm-barbeattracker:beats";

    QString bpmText (double bpm)
    {
        return QString::number (bpm, 'f', 2).remove (QRegularExpression (QStringLiteral ("\\.?0+$")));
    }

    constexpr double longestLoop = 60.0;   // seconds: longer files are not detected on opening

    // The grid for [s, e) from the beat tracker's beats and the hits. Loops are cut to
    // whole bars, which pins the tempo exactly; that also reads short, busy breaks the
    // beat tracker cannot follow (it needs several seconds of steady beat). Call from any thread.
    BeatGrid gridFrom (const AudioData& d, Frame s, Frame e, const vamphost::Result& result, double sensitivity)
    {
        std::vector<double> beats;
        std::vector<int> inBar;
        for (const auto& f : result.features)
        {
            beats.push_back ((double) f.start / d.sampleRate());
            inBar.push_back (QString::fromStdString (f.label).trimmed().toInt());
        }
        int perBar = 0;
        for (const auto b : inBar)
            perBar = std::max (perBar, b);
        auto grid = beatgrid::fit (beats, inBar, std::clamp (perBar, 2, 16));

        const auto rate = d.sampleRate();
        std::vector<double> hits;
        for (const auto h : timestretch::transients (d, sensitivity))
            if (h >= s && h < e)
                hits.push_back ((double) (h - s) / rate);
        // Bar 1 of a loop is where it starts.
        auto loop = beatgrid::fromLoop ((double) (e - s) / rate, hits, grid.bpm);
        if (loop.isSet())
        {
            loop.downbeat += (double) s / rate;
            loop.normalise();
            grid = loop;
        }
        return beatgrid::align (d, grid, ! loop.isSet());
    }
}

void MainWindow::registerWarpCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Warp");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (action);
        return action;
    };

    auto* show = add ("warp.showGrid", tr ("Show Beat &Grid"), { KS (Qt::CTRL | Qt::Key_J) }, [this]
    {
        appSettings().setValue (QStringLiteral ("warp/showGrid"), commands.action (QStringLiteral ("warp.showGrid"))->isChecked());
        refreshGrid();
    });
    show->setCheckable (true);
    show->setChecked (appSettings().value (QStringLiteral ("warp/showGrid"), true).toBool());
    show->setStatusTip (tr ("Bar and beat lines over the waveform, from the file's tempo"));

    // The time ruler counts time, bars and beats, or both (bars and beats need a tempo).
    auto* rulerModes = new QActionGroup (this);
    const auto rulerSetting = appSettings().value (QStringLiteral ("view/ruler"), QStringLiteral ("both")).toString();
    for (const auto& [id, text, value, tip] : { std::tuple { "view.rulerTime", tr ("Ruler in T&ime"), "time", tr ("The ruler counts minutes and seconds") },
                                                std::tuple { "view.rulerBeats", tr ("Ruler in &Bars and Beats"), "beats", tr ("The ruler counts bars, beats and sixteenths (1, 1.2, 1.2.3) once the file has a tempo") },
                                                std::tuple { "view.rulerBoth", tr ("Ruler in Time &and Beats"), "both", tr ("Bars and beats above, time below") } })
    {
        auto* a = add (id, text, {}, [this, value = QString::fromLatin1 (value)]
        {
            appSettings().setValue (QStringLiteral ("view/ruler"), value);
            refreshGrid();
        });
        a->setCheckable (true);
        a->setChecked (rulerSetting == QLatin1String (value));
        a->setStatusTip (tip);
        rulerModes->addAction (a);
    }
    add ("warp.detectGrid", tr ("&Detect Beat Grid"), {}, [this] { detectBeatGrid(); })
        ->setStatusTip (tr ("Find the beats and bars and fit a steady tempo grid to them"));
    auto* autoDetect = add ("warp.autoDetect", tr ("Detect Tem&po When Opening Loops"), {}, [this]
    {
        appSettings().setValue (QStringLiteral ("warp/autoDetect"), commands.action (QStringLiteral ("warp.autoDetect"))->isChecked());
    });
    autoDetect->setCheckable (true);
    autoDetect->setChecked (appSettings().value (QStringLiteral ("warp/autoDetect"), true).toBool());
    autoDetect->setStatusTip (tr ("Files up to a minute long with no tempo get one as they open, as in Acid and Live"));
    add ("warp.setTempo", tr ("Set &Tempo and Grid..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_J) }, [this] { setTempoDialog(); })
        ->setStatusTip (tr ("Type the tempo, beats per bar and where bar 1 starts"));
    add ("warp.downbeat", tr ("Bar 1 Starts at the &Cursor"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_J) }, [this]
    {
        auto grid = doc.current().grid;
        if (! grid.isSet())
            grid.bpm = 120.0;
        grid.downbeat = (double) cursor / data()->sampleRate();
        grid.normalise();
        applyGrid (tr ("Set Downbeat"), grid);
    })->setStatusTip (tr ("Move the grid so a bar starts where the cursor is"));
    add ("warp.halve", tr ("&Halve Grid Tempo"), {}, [this]
    {
        auto grid = doc.current().grid;
        grid.bpm /= 2.0;
        grid.normalise();
        applyGrid (tr ("Halve Tempo"), grid);
    })->setStatusTip (tr ("Count beats half as fast (the audio does not change); Stretch ×2 makes the audio twice as long"));
    add ("warp.double", tr ("D&ouble Grid Tempo"), {}, [this]
    {
        auto grid = doc.current().grid;
        grid.bpm *= 2.0;
        grid.normalise();
        applyGrid (tr ("Double Tempo"), grid);
    })->setStatusTip (tr ("Count beats twice as fast (the audio does not change); Shrink ÷2 makes the audio half as long"));
    auto* edit = add ("warp.edit", tr ("Edit &Warp Markers"), { KS (Qt::SHIFT | Qt::Key_W) }, [this]
    {
        setWarpEditing (commands.action (QStringLiteral ("warp.edit"))->isChecked());
    });
    edit->setCheckable (true);
    edit->setStatusTip (tr ("Show the hits as ticks; drag one to move it, Shift+drag a flag to move it without moving the audio, double-click to add or remove a flag"));
    add ("warp.previousHit", tr ("Pre&vious Hit"), { KS (Qt::Key_Comma) }, [this] { stepTransient (false); });
    add ("warp.nextHit", tr ("Ne&xt Hit"), { KS (Qt::Key_Period) }, [this] { stepTransient (true); });
    add ("warp.toggleMarker", tr ("Add or Remove Warp &Marker"), { KS (Qt::Key_W) }, [this]
    {
        const auto w = currentWarp();
        if (w == nullptr)
            return;
        // The hit at the cursor (within 20 ms), else the cursor itself.
        const auto rate = data()->sampleRate();
        Frame source = w->toSource (cursor);
        for (const auto t : w->transients)
            if (std::abs (w->toTarget (t) - cursor) <= (Frame) (0.02 * rate))
                source = t;
        for (const auto& m : w->markers)
            if (std::abs (m.target - cursor) <= (Frame) (0.02 * rate))
                source = m.source;
        warpToggle (source);
    });
    add ("warp.nudgeEarlier", tr ("Nudge Warp Marker &Earlier"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_Left) }, [this] { nudgeWarpMarker (-1, false); });
    add ("warp.nudgeLater", tr ("Nudge Warp Marker &Later"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_Right) }, [this] { nudgeWarpMarker (1, false); });
    add ("warp.nudgeEarlierFine", tr ("Nudge Warp Marker 1 ms Earlier"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Left) }, [this] { nudgeWarpMarker (-1, true); });
    add ("warp.nudgeLaterFine", tr ("Nudge Warp Marker 1 ms Later"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Right) }, [this] { nudgeWarpMarker (1, true); });
    add ("warp.quantize", tr ("&Quantize"), { KS (Qt::Key_Q) }, [this] { quantizeNow (false); })
        ->setStatusTip (tr ("Move the hits towards the beat grid, by stretching the audio between them, with the Quantize panel's settings (Shift+Q)"));
    connect (waveform, &WaveformView::warpMoved, this, [this] (Frame source, Frame target, int marker, bool reanchor)
    {
        warpMove (source, target, marker, reanchor);
    });
    connect (waveform, &WaveformView::warpToggled, this, [this] (Frame source) { warpToggle (source); });
    add ("warp.stretch", tr ("&Stretch to Tempo..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_J) }, [this] { stretchToTempo(); })
        ->setStatusTip (tr ("Change the tempo of the selection or the whole file, keeping the pitch (or with it, in Varispeed)"));
    add ("warp.halfSpeed", tr ("Stretch &×2 (Twice as Long)"), { KS (Qt::Key_Asterisk), KS (Qt::CTRL | Qt::ALT | Qt::Key_Down) }, [this] { stretchBy (0.5); })
        ->setStatusTip (tr ("Make the selection or the whole file twice as long (half speed, half-time), keeping the pitch; press again for 4×, 8×..."));
    add ("warp.doubleSpeed", tr ("Shrink &÷2 (Half as Long)"), { KS (Qt::Key_Slash), KS (Qt::CTRL | Qt::ALT | Qt::Key_Up) }, [this] { stretchBy (2.0); })
        ->setStatusTip (tr ("Make the selection or the whole file half as long (double speed), keeping the pitch; press again for a quarter..."));
    add ("warp.exportLoop", tr ("Export &Loop..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_L) }, [this] { exportLoop(); })
        ->setStatusTip (tr ("Save whole bars as a WAV loop with its tempo and loop points, for samplers and DAWs"));
    registerQuantizeCommands();
}

void MainWindow::updateWarpCommandStates()
{
    const bool open = doc.isOpen() && ! loading && ! doc.isAlbum();
    const bool grid = open && doc.current().grid.isSet();
    for (const auto* a : { "warp.detectGrid", "warp.setTempo", "warp.downbeat", "warp.stretch", "warp.doubleSpeed", "warp.halfSpeed" })
        commands.action (QString::fromLatin1 (a))->setEnabled (open);
    for (const auto* a : { "warp.halve", "warp.double", "warp.exportLoop" })
        commands.action (QString::fromLatin1 (a))->setEnabled (grid);
    commands.action (QStringLiteral ("warp.edit"))->setEnabled (open);
    for (const auto* a : { "warp.quantize", "warp.markersFromHits", "warp.sliceAtHits" })
        commands.action (QString::fromLatin1 (a))->setEnabled (open);
    commands.action (QStringLiteral ("warp.resetWarp"))->setEnabled (open && currentWarp() != nullptr && ! currentWarp()->markers.empty());
    if (! open && warpEditing)
        setWarpEditing (false);
    for (const auto* a : { "warp.previousHit", "warp.nextHit", "warp.toggleMarker", "warp.nudgeEarlier", "warp.nudgeLater",
                           "warp.nudgeEarlierFine", "warp.nudgeLaterFine" })
        commands.action (QString::fromLatin1 (a))->setEnabled (open && warpEditing);
    updateQuantizePanel();
}

void MainWindow::refreshGrid()
{
    const auto grid = doc.isOpen() ? doc.current().grid : BeatGrid();
    waveform->setGrid (grid, commands.action (QStringLiteral ("warp.showGrid"))->isChecked());
    const auto ruler = commands.action (QStringLiteral ("view.rulerBeats"))->isChecked() ? TimeRuler::Mode::beats
                     : commands.action (QStringLiteral ("view.rulerBoth"))->isChecked() ? TimeRuler::Mode::both : TimeRuler::Mode::time;
    timeRuler->setGrid (grid, ruler);
    transportBar->setTempo (grid.isSet() ? grid.bpm : 0.0);
    // Any other edit of the audio ends the warp; editing goes on with the hits of the audio as it is now.
    if (warpEditing && doc.isOpen() && ! doc.isAlbum() && data() != nullptr && currentWarp() == nullptr)
    {
        auto draft = std::make_shared<WarpEdit>();
        draft->source = data();
        draft->mode = quantizeWidget != nullptr ? quantizeWidget->mode()
                                                : appSettings().value (QStringLiteral ("warp/mode"), (int) timestretch::Mode::fullMix).toInt();
        draft->transients = findHits (*draft->source);
        warpDraft = std::move (draft);
    }
    waveform->setWarp (currentWarp(), warpEditing);
    waveform->setWarpSnap (quantizeWidget != nullptr ? quantizeWidget->snapDivision() : 0.25);
    updateQuantizePanel();
}

std::shared_ptr<const WarpEdit> MainWindow::currentWarp() const
{
    if (! doc.isOpen())
        return nullptr;
    if (doc.current().warp != nullptr)
        return doc.current().warp;
    return warpDraft != nullptr && warpDraft->source == doc.current().data ? warpDraft : nullptr;
}

void MainWindow::setWarpEditing (bool on)
{
    const auto d = data();
    on = on && d != nullptr && ! doc.isAlbum();
    if (on && currentWarp() == nullptr)
    {
        // The hits of the audio as it is now; markers placed on them stretch it from here.
        auto draft = std::make_shared<WarpEdit>();
        draft->source = d;
        draft->mode = appSettings().value (QStringLiteral ("warp/mode"), (int) timestretch::Mode::fullMix).toInt();
        draft->transients = findHits (*d);
        warpDraft = std::move (draft);
    }
    warpEditing = on;
    commands.action (QStringLiteral ("warp.edit"))->setChecked (on);
    updateWarpCommandStates();
    refreshGrid();
    if (on)
        statusBar()->showMessage (tr ("Warp markers: %1 hits. Drag a tick to move that hit; double-click or W adds a flag; , and . step through the hits; Q quantizes.")
                                      .arg (currentWarp()->transients.size()), 8000);
}

void MainWindow::applyWarp (const QString& description, std::shared_ptr<WarpEdit> next)
{
    const auto old = currentWarp();
    if (old == nullptr || next == nullptr || refuseAlbumEdit())
        return;
    const auto& source = *next->source;
    if (warpCache == nullptr)
        warpCache = std::make_shared<timestretch::SegmentCache>();
    std::vector<timestretch::Anchor> anchors;
    for (const auto& m : next->markers)
        anchors.push_back ({ m.source, m.target });
    AudioDataPtr result;
    std::string error;
    if (next->markers.empty())
        result = next->source;
    else
    {
        const auto ran = runWithProgress (tr ("Warp"), [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
        {
            result = timestretch::render (source, 0, source.length(), source.length(), anchors, (timestretch::Mode) next->mode,
                                          error, &progress, &cancel, warpCache.get(), next->transients);
        });
        if (! ran || result == nullptr)
        {
            if (ran)
                QMessageBox::warning (this, tr ("Warp"), QString::fromStdString (error));
            return;
        }
    }
    auto state = doc.current();
    // Markers stay on the audio they were on.
    for (auto& m : state.markers)
        for (auto* f : { &m.start, &m.end })
            *f = std::clamp<Frame> (next->toTarget (old->toSource (*f)), 0, result->length());
    const auto cursorSource = old->toSource (cursor);
    state.data = result;
    state.warp = std::move (next);
    doc.apply (description.toStdString(), std::move (state));
    cursor = std::clamp<Frame> (doc.current().warp->toTarget (cursorSource), 0, result->length());
    refreshFromDocument (true);
    waveform->setFocus();
}

namespace
{
    // The markers kept in order: the one at `index` is held between its neighbours (1 ms apart) and the ends.
    void keepInOrder (WarpEdit& w, size_t index, double rate)
    {
        const auto gap = std::max<Frame> (1, (Frame) (0.001 * rate));
        const auto length = w.source->length();
        auto& m = w.markers[index];
        const auto lowSource = index > 0 ? w.markers[index - 1].source + gap : gap;
        const auto highSource = index + 1 < w.markers.size() ? w.markers[index + 1].source - gap : length - gap;
        const auto lowTarget = index > 0 ? w.markers[index - 1].target + gap : gap;
        const auto highTarget = index + 1 < w.markers.size() ? w.markers[index + 1].target - gap : length - gap;
        m.source = std::clamp (m.source, lowSource, std::max (lowSource, highSource));
        m.target = std::clamp (m.target, lowTarget, std::max (lowTarget, highTarget));
    }
}

void MainWindow::warpMove (Frame source, Frame target, int marker, bool reanchor)
{
    const auto old = currentWarp();
    if (old == nullptr)
        return;
    auto next = std::make_shared<WarpEdit> (*old);
    const auto rate = old->source->sampleRate();
    size_t index = 0;
    if (marker >= 0 && marker < (int) next->markers.size())
    {
        index = (size_t) marker;
        if (reanchor)
            next->markers[index] = { old->toSource (target), target };   // the audio stays; the flag moves to what is under it
        else
            next->markers[index].target = target;
    }
    else
    {
        WarpEdit::Marker m { source, target };
        const auto at = std::lower_bound (next->markers.begin(), next->markers.end(), source,
                                          [] (const WarpEdit::Marker& x, Frame s) { return x.source < s; });
        index = (size_t) (at - next->markers.begin());
        next->markers.insert (at, m);
    }
    keepInOrder (*next, index, rate);
    applyWarp (reanchor ? tr ("Move Warp Marker") : tr ("Warp"), std::move (next));
}

void MainWindow::warpToggle (Frame source)
{
    const auto old = currentWarp();
    if (old == nullptr)
        return;
    auto next = std::make_shared<WarpEdit> (*old);
    const auto rate = old->source->sampleRate();
    const auto near = (Frame) (0.002 * rate);
    for (auto it = next->markers.begin(); it != next->markers.end(); ++it)
        if (std::abs (it->source - source) <= near)
        {
            next->markers.erase (it);
            applyWarp (tr ("Remove Warp Marker"), std::move (next));
            return;
        }
    // A new flag where the audio already is: nothing moves.
    const WarpEdit::Marker m { source, old->toTarget (source) };
    const auto at = std::lower_bound (next->markers.begin(), next->markers.end(), source,
                                      [] (const WarpEdit::Marker& x, Frame s) { return x.source < s; });
    const auto index = (size_t) (at - next->markers.begin());
    next->markers.insert (at, m);
    keepInOrder (*next, index, rate);
    auto state = doc.current();
    state.warp = std::move (next);
    doc.apply (tr ("Add Warp Marker").toStdString(), std::move (state));
    refreshFromDocument (true);
}

void MainWindow::stepTransient (bool forward)
{
    const auto w = currentWarp();
    if (w == nullptr)
        return;
    Frame best = -1;
    for (const auto t : w->transients)
    {
        const auto at = w->toTarget (t);
        if (forward ? at > cursor && (best < 0 || at < best) : at < cursor && (best < 0 || at > best))
            best = at;
    }
    if (best >= 0)
    {
        selStart = selEnd = 0;
        setCursor (best);
    }
}

void MainWindow::nudgeWarpMarker (int direction, bool fine)
{
    const auto w = currentWarp();
    if (w == nullptr)
        return;
    const auto rate = w->source->sampleRate();
    // The flag at the cursor (within 50 ms), else the hit there, which becomes a flag.
    int marker = -1;
    Frame source = -1, target = cursor;
    Frame closest = (Frame) (0.05 * rate);
    for (size_t i = 0; i < w->markers.size(); ++i)
        if (const auto d = std::abs (w->markers[i].target - cursor); d <= closest)
        {
            closest = d;
            marker = (int) i;
            source = w->markers[i].source;
            target = w->markers[i].target;
        }
    if (marker < 0)
        for (const auto t : w->transients)
            if (const auto d = std::abs (w->toTarget (t) - cursor); d <= closest)
            {
                closest = d;
                source = t;
                target = w->toTarget (t);
            }
    if (source < 0)
    {
        statusBar()->showMessage (tr ("No hit or warp marker at the cursor"), 3000);
        return;
    }
    const auto& grid = doc.current().grid;
    Frame moved = target;
    if (fine)
        moved += direction * std::max<Frame> (1, (Frame) (0.001 * rate));
    else if (grid.isSet())
    {
        // To the next sixteenth line in that direction.
        const auto beat = grid.beatAt ((double) target / rate);
        const auto line = direction > 0 ? std::floor (beat / 0.25 + 1e-6) * 0.25 + 0.25 : std::ceil (beat / 0.25 - 1e-6) * 0.25 - 0.25;
        moved = (Frame) std::llround (grid.secondsAt (line) * rate);
    }
    else
        moved += direction * (Frame) (0.01 * rate);
    warpMove (source, std::clamp<Frame> (moved, 0, w->source->length()), marker, false);
    setCursor (currentWarp() != nullptr ? currentWarp()->toTarget (source) : moved);
}

void MainWindow::applyGrid (const QString& description, BeatGrid grid)
{
    if (! doc.isOpen() || refuseAlbumEdit())
        return;
    grid.bpm = std::clamp (grid.bpm, 20.0, 400.0);
    grid.normalise();
    auto state = doc.current();
    state.grid = grid;
    // The tempo goes with the file too (WAV acid chunk, FLAC and MP3 BPM).
    state.tags[tag::bpm] = bpmText (grid.bpm).toStdString();
    doc.apply (description.toStdString(), std::move (state));
    refreshFromDocument (true);
    statusBar()->showMessage (tr ("Grid: %1 BPM, %2 beats to the bar, bar 1 at %3 s")
                                  .arg (bpmText (grid.bpm)).arg (grid.beatsPerBar).arg (grid.downbeat, 0, 'f', 3), 6000);
}

void MainWindow::detectBeatGrid()
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit())
        return;
    const auto title = tr ("Detect Beat Grid");
    const auto [s, e] = processRange();
    vamphost::Result result;
    if (! runAnalysis (title, *d, s, e, QString::fromLatin1 (beatTracker), result))
        return;
    const auto grid = gridFrom (*d, s, e, result, appSettings().value (QStringLiteral ("warp/sensitivity"), 0.5).toDouble());
    if (! grid.isSet())
    {
        QMessageBox::information (this, title, tr ("No steady beat was found in %1. If you know the tempo, type it in with "
                                                   "Warp > Set Tempo and Grid (Ctrl+Shift+J).")
                                                   .arg (hasSelection() ? tr ("the selection") : tr ("the file")));
        return;
    }
    applyGrid (title, grid);
}

void MainWindow::autoDetectGrid()
{
    // Like Acid and Live: a loop with no tempo yet gets one as it opens, quietly
    // (no undo step, nothing to save unless something else changes).
    const auto d = data();
    if (d == nullptr || doc.isAlbum() || doc.current().grid.isSet() || d->lengthSeconds() > longestLoop || d->lengthSeconds() < 1.0
        || ! commands.action (QStringLiteral ("warp.autoDetect"))->isChecked() || vamphost::find (beatTracker) == nullptr)
        return;
    const auto sensitivity = appSettings().value (QStringLiteral ("warp/sensitivity"), 0.5).toDouble();
    auto* watcher = new QFutureWatcher<BeatGrid> (this);
    connect (watcher, &QFutureWatcher<BeatGrid>::finished, this, [this, watcher, d]
    {
        watcher->deleteLater();
        const auto grid = watcher->result();
        // Only if the same audio is still in front, untouched.
        if (! grid.isSet() || data() != d || doc.historyPosition() != 0 || doc.current().grid.isSet())
            return;
        doc.setGrid (grid);
        refreshGrid();
        statusBar()->showMessage (tr ("Tempo detected: %1 BPM. Type another in the BPM box, or use Warp > Halve or Double Grid Tempo.")
                                      .arg (bpmText (grid.bpm)), 8000);
    });
    watcher->setFuture (QtConcurrent::run ([d, sensitivity]
    {
        const auto result = vamphost::run (*d, 0, d->length(), beatTracker);
        return gridFrom (*d, 0, d->length(), result, sensitivity);
    }));
}

void MainWindow::setTempoDialog()
{
    if (data() == nullptr || refuseAlbumEdit())
        return;
    auto grid = doc.current().grid;
    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Set Tempo and Grid"));
    auto* form = new QFormLayout (&dialog);
    auto* bpm = new QDoubleSpinBox (&dialog);
    bpm->setRange (20.0, 400.0);
    bpm->setDecimals (2);
    bpm->setSuffix (tr (" BPM"));
    bpm->setValue (grid.isSet() ? grid.bpm : 120.0);
    auto* perBar = new QSpinBox (&dialog);
    perBar->setRange (1, 16);
    perBar->setValue (grid.beatsPerBar);
    auto* downbeat = new QDoubleSpinBox (&dialog);
    downbeat->setRange (0.0, 60.0);
    downbeat->setDecimals (3);
    downbeat->setSuffix (tr (" s"));
    downbeat->setValue (grid.downbeat);
    form->addRow (tr ("&Tempo:"), bpm);
    form->addRow (tr ("&Beats per bar:"), perBar);
    form->addRow (tr ("Bar 1 &starts at:"), downbeat);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    bpm->selectAll();
    if (dialog.exec() != QDialog::Accepted)
    {
        waveform->setFocus();
        return;
    }
    grid.bpm = bpm->value();
    grid.beatsPerBar = perBar->value();
    grid.downbeat = downbeat->value();
    applyGrid (tr ("Set Tempo"), grid);
    waveform->setFocus();
}

void MainWindow::stretchToTempo()
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit())
        return;
    const auto title = tr ("Stretch to Tempo");
    const auto [s, e] = processRange();
    const auto rate = d->sampleRate();
    const bool whole = s == 0 && e == d->length();
    const auto grid = doc.current().grid;

    QDialog dialog (this);
    dialog.setWindowTitle (title);
    auto* layout = new QVBoxLayout (&dialog);
    auto* form = new QFormLayout;
    layout->addLayout (form);
    auto* from = new QDoubleSpinBox (&dialog);
    auto* to = new QDoubleSpinBox (&dialog);
    for (auto* box : { from, to })
    {
        box->setRange (20.0, 400.0);
        box->setDecimals (2);
        box->setSuffix (tr (" BPM"));
    }
    from->setValue (grid.isSet() ? grid.bpm : 120.0);
    to->setValue (grid.isSet() ? std::round (grid.bpm) : 120.0);
    auto* fit = new QCheckBox (tr ("&Fit to exactly"), &dialog);
    auto* bars = new QSpinBox (&dialog);
    bars->setRange (1, 999);
    bars->setSuffix (tr (" bars"));
    bars->setValue (std::max (1, (int) std::lround ((double) (e - s) / rate / (grid.isSet() ? grid.barSeconds() : 2.0))));
    auto* mode = new QComboBox (&dialog);
    for (const auto m : timestretch::modes())
        mode->addItem (tr (timestretch::modeName (m)), (int) m);
    mode->setCurrentIndex (std::max (0, mode->findData (appSettings().value (QStringLiteral ("warp/mode"), (int) timestretch::Mode::fullMix).toInt())));
    form->addRow (tr ("Its tempo &now:"), from);
    form->addRow (tr ("&New tempo:"), to);
    form->addRow (fit, bars);
    form->addRow (tr ("&Mode:"), mode);
    auto* summary = new QLabel (&dialog);
    summary->setWordWrap (true);
    layout->addWidget (summary);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("Stretch"));
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    const auto length = e - s;
    auto newLength = [&]
    {
        if (fit->isChecked())
            return (Frame) std::llround (bars->value() * 60.0 / to->value() * (grid.isSet() ? grid.beatsPerBar : 4) * rate);
        return (Frame) std::llround ((double) length * from->value() / to->value());
    };
    auto describe = [&]
    {
        bars->setEnabled (fit->isChecked());
        from->setEnabled (! fit->isChecked());
        const auto n = newLength();
        const auto m = (timestretch::Mode) mode->currentData().toInt();
        summary->setText (tr ("%1 goes from %2 s to %3 s (%4 %). %5")
                              .arg (whole ? tr ("The whole file") : tr ("The selection"))
                              .arg ((double) length / rate, 0, 'f', 3).arg ((double) n / rate, 0, 'f', 3)
                              .arg (100.0 * (double) n / (double) length, 0, 'f', 1)
                              .arg (m == timestretch::Mode::varispeed ? tr ("Varispeed changes the pitch with the speed, like tape.")
                                                                      : tr ("The pitch stays the same.")));
    };
    for (auto* box : { from, to })
        connect (box, &QDoubleSpinBox::valueChanged, &dialog, describe);
    connect (bars, &QSpinBox::valueChanged, &dialog, describe);
    connect (fit, &QCheckBox::toggled, &dialog, describe);
    connect (mode, &QComboBox::currentIndexChanged, &dialog, describe);
    describe();
    to->selectAll();
    to->setFocus();
    if (dialog.exec() != QDialog::Accepted)
    {
        waveform->setFocus();
        return;
    }

    const auto m = (timestretch::Mode) mode->currentData().toInt();
    appSettings().setValue (QStringLiteral ("warp/mode"), (int) m);
    applyStretch (title, s, e, newLength(), (int) m, to->value());
}

void MainWindow::stretchBy (double speed)
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit())
        return;
    const auto [s, e] = processRange();
    const auto m = (timestretch::Mode) appSettings().value (QStringLiteral ("warp/mode"), (int) timestretch::Mode::fullMix).toInt();
    const auto title = speed > 1.0 ? tr ("Shrink") : tr ("Stretch");

    // Pressed again on what the last one made: undo it and stretch the audio from before it
    // at the total ratio, so five presses are one clean stretch, not five on top of each other.
    const auto& c = speedChain;
    const bool again = c.result != nullptr && c.result == d && c.start == s
                    && e - s == (Frame) std::llround ((double) (c.baseEnd - c.start) / c.speed) && doc.canUndo();
    const auto total = again ? c.speed * speed : speed;
    if (again && (total > 64.0 || total < 1.0 / 64.0))
    {
        statusBar()->showMessage (tr ("That is as far as it goes (64 times)"), 3000);
        return;
    }
    if (again)
    {
        const auto base = c.base;
        const auto baseEnd = c.baseEnd;
        const auto baseBpm = c.baseBpm;
        doc.undo();
        if (doc.current().data != base)
        {
            doc.redo();   // something else happened in between: stretch what is there
            speedChain = {};
            return stretchBy (speed);
        }
        if (hasSelection())
            selEnd = baseEnd;
        applyStretch ((total < 1.0 ? tr ("%1 ×%2") : tr ("%1 ÷%2")).arg (title).arg (total >= 1.0 ? total : 1.0 / total), s, baseEnd,
                      (Frame) std::llround ((double) (baseEnd - s) / total), (int) m, baseBpm > 0.0 ? baseBpm * total : 0.0);
        speedChain = { base, data(), s, baseEnd, total, baseBpm };
        return;
    }
    const auto grid = doc.current().grid;
    const auto bpm = grid.isSet() ? grid.bpm : 0.0;
    applyStretch (title, s, e, (Frame) std::llround ((double) (e - s) / speed), (int) m, bpm > 0.0 ? bpm * speed : 0.0);
    speedChain = data() != d ? SpeedChain { d, data(), s, e, speed, bpm } : SpeedChain {};
}

void MainWindow::applyStretch (const QString& title, Frame s, Frame e, Frame target, int modeIndex, double newBpm)
{
    const auto d = data();
    const auto rate = d->sampleRate();
    const auto length = e - s;
    const bool whole = s == 0 && e == d->length();
    const auto m = (timestretch::Mode) modeIndex;
    if (target == length || target <= 0)
        return;
    AudioDataPtr stretched;
    std::string error;
    const auto ran = runWithProgress (title, [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
    {
        stretched = timestretch::render (*d, s, e, target, {}, m, error, &progress, &cancel);
    });
    waveform->setFocus();
    if (! ran || stretched == nullptr)
    {
        if (ran && error != "Cancelled.")
            QMessageBox::warning (this, title, QString::fromStdString (error));
        return;
    }

    AudioData::Builder builder (d->numChannels(), rate);
    builder.appendShared (*d, 0, s);
    builder.appendShared (*stretched, 0, stretched->length());
    builder.appendShared (*d, e, d->length());
    auto state = doc.current();
    state.data = builder.finish();
    // Markers inside move with the audio; those after shift by the change in length.
    const auto ratio = (double) target / (double) length;
    for (auto& marker : state.markers)
        for (auto* f : { &marker.start, &marker.end })
            *f = *f >= e ? *f + (target - length) : *f > s ? s + (Frame) std::llround ((double) (*f - s) * ratio) : *f;
    if (whole && newBpm > 0.0)
    {
        state.grid.bpm = newBpm;
        state.grid.downbeat *= ratio;
        state.grid.normalise();
        state.tags[tag::bpm] = bpmText (newBpm).toStdString();
    }
    doc.apply ((title + QStringLiteral (" (%1)").arg (tr (timestretch::modeName (m)))).toStdString(), std::move (state));
    if (hasSelection())
        selEnd = s + target;
    refreshFromDocument (true);
    statusBar()->showMessage (tr ("Stretched to %1 s").arg ((double) target / rate, 0, 'f', 3), 6000);
}

void MainWindow::duplicateSelection()
{
    if (! hasSelection())
    {
        statusBar()->showMessage (tr ("Select something to duplicate first"), 3000);
        return;
    }
    moveSelection (selStart, selEnd, selEnd, true);
}

void MainWindow::moveSelection (Frame from, Frame to, Frame start, bool copy)
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit() || to <= from || start < 0)
        return;
    const auto length = to - from;
    const auto clip = d->slice (from, to);
    AudioDataPtr result = d;
    if (! copy)
    {
        // What is left behind is silence, as when a clip is moved in Live.
        AudioData::Builder silence (d->numChannels(), d->sampleRate());
        std::vector<std::vector<float>> zeros ((size_t) d->numChannels(), std::vector<float> (4096, 0.0f));
        std::vector<const float*> pointers;
        for (const auto& z : zeros)
            pointers.push_back (z.data());
        for (Frame done = 0; done < length;)
        {
            const auto n = (int) std::min<Frame> (4096, length - done);
            silence.append (pointers.data(), n);
            done += n;
        }
        const auto gap = silence.finish();
        result = result->replaced (from, to, gap.get());
    }
    const auto end = std::min (start + length, result->length());
    result = result->replaced (start, end, clip.get());   // past the end of the file, it grows

    auto state = doc.current();
    state.data = result;
    const auto what = copy ? (start == to ? tr ("Duplicate") : tr ("Copy Selection")) : tr ("Move Selection");
    doc.apply (what.toStdString(), std::move (state));
    selAnchor = selStart = start;
    selEnd = start + length;
    cursor = start;
    refreshFromDocument (true);
    statusBar()->showMessage (tr ("%1: %2 s").arg (what).arg ((double) length / d->sampleRate(), 0, 'f', 3), 3000);
}

void MainWindow::splitAtCursor()
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit())
        return;
    std::vector<Frame> cuts;
    if (hasSelection())
        cuts = { selStart, selEnd };
    else
        cuts = { cursor };
    auto state = doc.current();
    int added = 0;
    for (const auto at : cuts)
    {
        if (at <= 0 || at >= d->length())
            continue;
        if (std::any_of (state.markers.begin(), state.markers.end(), [at] (const Marker& m) { return m.start == at || m.end == at; }))
            continue;
        Marker m;
        m.id = doc.nextMarkerId();
        m.start = m.end = at;
        m.name = tr ("Slice").toStdString();
        state.markers.push_back (m);
        ++added;
    }
    if (added == 0)
    {
        statusBar()->showMessage (tr ("There is already a slice there"), 3000);
        return;
    }
    std::sort (state.markers.begin(), state.markers.end(), [] (const Marker& a, const Marker& b) { return a.start < b.start; });
    doc.apply (tr ("Split").toStdString(), std::move (state));
    refreshFromDocument (true);
    statusBar()->showMessage (tr ("Split: double-click between slices to select one; R reverses it, Ctrl+D duplicates it, drag it to move it"), 5000);
}

void MainWindow::showGridStep()
{
    const auto step = waveform->gridStep();
    const auto perBar = doc.isOpen() ? doc.current().grid.beatsPerBar : 4;
    QString name;
    if (step >= perBar)
        name = step < perBar + 0.5 ? tr ("1 bar") : tr ("%1 bars").arg (step / perBar);
    else
        name = QStringLiteral ("1/%1").arg (std::lround (4.0 / step));
    statusBar()->showMessage (tr ("Grid: %1 at this zoom").arg (name), 3000);
}

// A loop of whole bars for samplers and DAWs: the bars from the grid, the seam crossfaded
// with what comes just before bar 1 (so the end runs into the start as it did in the
// recording), saved as WAV with its tempo, loop points and length in beats (the smpl and
// acid chunks Acid, Live, FL Studio and hardware samplers read).
void MainWindow::exportLoop()
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit())
        return;
    const auto title = tr ("Export Loop");
    const auto grid = doc.current().grid;
    if (! grid.isSet())
    {
        statusBar()->showMessage (tr ("The file needs a tempo first: Detect Beat Grid (Warp menu) or type it with Ctrl+Shift+J"), 8000);
        return;
    }
    const auto rate = d->sampleRate();
    const auto bar = grid.barSeconds();
    const auto [s, e] = processRange();
    const bool whole = s == 0 && e == d->length();
    // Bars are numbered from 1 at the downbeat; the part before it is bar 0.
    auto barAt = [&] (double seconds) { return (int) std::lround (grid.beatAt (seconds) / grid.beatsPerBar) + 1; };
    const int firstBar = (int) std::floor (grid.beatAt (0.0) / grid.beatsPerBar) + 1;
    const int lastBar = std::max (firstBar, (int) std::ceil (grid.beatAt (d->lengthSeconds()) / grid.beatsPerBar));

    QDialog dialog (this);
    dialog.setWindowTitle (title);
    auto* layout = new QVBoxLayout (&dialog);
    auto* form = new QFormLayout;
    layout->addLayout (form);
    auto* start = new QSpinBox (&dialog);
    start->setRange (firstBar, lastBar);
    start->setPrefix (tr ("bar "));
    start->setValue (std::clamp (barAt ((double) s / rate), firstBar, lastBar));
    auto* bars = new QSpinBox (&dialog);
    bars->setRange (1, 256);
    bars->setSuffix (tr (" bars"));
    const int selected = std::max (1, (int) std::lround ((double) (e - s) / rate / bar));
    bars->setValue (whole ? std::max (1, std::min (selected, lastBar - start->value() + 1)) : selected);
    auto* fade = new QSpinBox (&dialog);
    fade->setRange (0, 50);
    fade->setSuffix (tr (" ms"));
    fade->setValue (appSettings().value (QStringLiteral ("warp/loopFade"), 5).toInt());
    fade->setToolTip (tr ("The end of the loop is blended into what comes just before its start, so it runs round without a click"));
    auto* depth = new QComboBox (&dialog);
    depth->addItem (tr ("16-bit"), 16);
    depth->addItem (tr ("24-bit"), 24);
    depth->addItem (tr ("32-bit float"), 32);
    depth->setCurrentIndex (std::max (0, depth->findData (saveOptions.bitDepth)));
    form->addRow (tr ("&Start at:"), start);
    form->addRow (tr ("&Length:"), bars);
    form->addRow (tr ("Seam &crossfade:"), fade);
    form->addRow (tr ("&Bit depth:"), depth);
    auto* summary = new QLabel (&dialog);
    summary->setWordWrap (true);
    layout->addWidget (summary);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("Export..."));
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // A file of whole bars is a loop already: it is read round, so a grid a few ms off
    // the start takes its last few ms from the start of the file, and the seam blends
    // with the end of the file.
    const auto fileBars = d->lengthSeconds() / bar;
    const bool circular = std::lround (fileBars) >= 1 && std::abs (fileBars - (double) std::lround (fileBars)) * bar <= 0.05;
    auto range = [&]
    {
        const auto a = (Frame) std::llround (grid.secondsAt ((double) (start->value() - 1) * grid.beatsPerBar) * rate);
        const auto b = (Frame) std::llround (grid.secondsAt ((double) (start->value() - 1 + bars->value()) * grid.beatsPerBar) * rate);
        return std::pair { std::max<Frame> (0, a), b };
    };
    auto describe = [&]
    {
        const auto [a, b] = range();
        auto text = tr ("%1 bars of %2/4 at %3 BPM, %4 s from %5 s, %6 beats long.")
                        .arg (bars->value()).arg (grid.beatsPerBar).arg (bpmText (grid.bpm))
                        .arg ((double) (b - a) / rate, 0, 'f', 3).arg ((double) a / rate, 0, 'f', 3)
                        .arg (bars->value() * grid.beatsPerBar);
        if (b > d->length() && ! circular)
            text += QLatin1Char (' ') + tr ("It runs past the end of the file, which is filled with silence.");
        else if (fade->value() > 0 && a < (Frame) (fade->value() * 0.001 * rate) && ! circular)
            text += QLatin1Char (' ') + tr ("Nothing comes before bar %1, so the end fades out instead of blending.").arg (start->value());
        summary->setText (text);
    };
    connect (start, &QSpinBox::valueChanged, &dialog, describe);
    connect (bars, &QSpinBox::valueChanged, &dialog, describe);
    connect (fade, &QSpinBox::valueChanged, &dialog, describe);
    describe();
    bars->selectAll();
    bars->setFocus();
    if (dialog.exec() != QDialog::Accepted)
    {
        waveform->setFocus();
        return;
    }
    appSettings().setValue (QStringLiteral ("warp/loopFade"), fade->value());

    const auto source = QFileInfo (QString::fromStdString (doc.path()));
    const auto base = source.completeBaseName().isEmpty() ? tr ("Loop") : source.completeBaseName();
    const auto suggested = QDir (source.exists() ? source.absolutePath() : QDir::homePath())
                               .filePath (tr ("%1 loop %2 BPM %3 bars.wav").arg (base, bpmText (grid.bpm)).arg (bars->value()));
    auto path = QFileDialog::getSaveFileName (this, title, suggested, tr ("WAV loops (*.wav)"));
    waveform->setFocus();
    if (path.isEmpty())
        return;
    if (! path.endsWith (QLatin1String (".wav"), Qt::CaseInsensitive))
        path += QStringLiteral (".wav");

    // The loop, then its last `n` frames blended with the `n` before bar 1: played round,
    // the end becomes the audio that led into the start.
    const auto [a, b] = range();
    const auto length = b - a;
    const int channels = d->numChannels();
    std::vector<std::vector<float>> loop ((size_t) channels, std::vector<float> ((size_t) length));
    std::vector<float*> out;
    for (auto& c : loop)
        out.push_back (c.data());
    // Reads [from, from + count), round the file when it is a loop.
    auto readRound = [&] (Frame from, Frame count, std::vector<float*> dest)
    {
        const auto total = d->length();
        while (count > 0)
        {
            if (circular)
                from = ((from % total) + total) % total;
            const auto part = circular ? std::min (count, total - from) : count;
            d->read (from, (int) part, dest.data());
            for (auto& p : dest)
                p += part;
            from += part;
            count -= part;
        }
    };
    readRound (a, length, out);
    const auto n = std::min<Frame> ((Frame) std::llround (fade->value() * 0.001 * rate), length / 2);
    if (n > 0 && (a >= n || circular))
    {
        std::vector<std::vector<float>> before ((size_t) channels, std::vector<float> ((size_t) n));
        std::vector<float*> in;
        for (auto& c : before)
            in.push_back (c.data());
        readRound (a - n, n, in);
        for (int ch = 0; ch < channels; ++ch)
            for (Frame i = 0; i < n; ++i)
            {
                const auto t = (float) (i + 1) / (float) (n + 1);
                auto& x = loop[(size_t) ch][(size_t) (length - n + i)];
                x = x * (1.0f - t) + before[(size_t) ch][(size_t) i] * t;
            }
    }
    else if (n > 0)
    {
        for (auto& c : loop)
            for (Frame i = 0; i < n; ++i)
                c[(size_t) (length - n + i)] *= (float) (n - 1 - i) / (float) n;
    }
    AudioData::Builder builder (channels, rate);
    std::vector<const float*> pointers;
    for (const auto& c : loop)
        pointers.push_back (c.data());
    for (Frame done = 0; done < length;)
    {
        const auto count = (int) std::min<Frame> (65536, length - done);
        std::vector<const float*> at;
        for (const auto* p : pointers)
            at.push_back (p + done);
        builder.append (at.data(), count);
        done += count;
    }
    const auto result = builder.finish();

    auto tags = doc.current().tags;
    tags[tag::bpm] = bpmText (grid.bpm).toStdString();
    auto options = saveOptions;
    options.bitDepth = depth->currentData().toInt();
    options.loopStart = 0;
    options.loopEnd = length;
    options.loopBeats = bars->value() * grid.beatsPerBar;
    options.beatsPerBar = grid.beatsPerBar;
    std::string error;
    if (! AudioEngine::saveFile (path.toStdString(), *result, {}, tags, options, error))
    {
        QMessageBox::warning (this, title, QString::fromStdString (error));
        return;
    }
    statusBar()->showMessage (tr ("Exported a %1-bar loop at %2 BPM to %3").arg (bars->value()).arg (bpmText (grid.bpm), path), 8000);
}
