// SPDX-License-Identifier: AGPL-3.0-only
// The Warp menu: the beat grid (tempo, downbeat, bars over the waveform) and
// stretching to a tempo with Rubber Band (core/TimeStretch.h). Warp markers,
// quantizing and loop export build on these.
#include "MainWindow.h"

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
#include <QDoubleSpinBox>
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
    for (const auto& [id, text, value, tip] : { std::tuple { "view.rulerTime", tr ("Ruler in &Time"), "time", tr ("The ruler counts minutes and seconds") },
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
    auto* autoDetect = add ("warp.autoDetect", tr ("Detect Tempo When &Opening Loops"), {}, [this]
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
    add ("warp.halve", tr ("&Halve Tempo"), {}, [this]
    {
        auto grid = doc.current().grid;
        grid.bpm /= 2.0;
        grid.normalise();
        applyGrid (tr ("Halve Tempo"), grid);
    });
    add ("warp.double", tr ("D&ouble Tempo"), {}, [this]
    {
        auto grid = doc.current().grid;
        grid.bpm *= 2.0;
        grid.normalise();
        applyGrid (tr ("Double Tempo"), grid);
    });
    auto* edit = add ("warp.edit", tr ("Edit &Warp Markers"), { KS (Qt::SHIFT | Qt::Key_W) }, [this]
    {
        setWarpEditing (commands.action (QStringLiteral ("warp.edit"))->isChecked());
    });
    edit->setCheckable (true);
    edit->setStatusTip (tr ("Show the hits as ticks; drag one to move it, Shift+drag a flag to move it without moving the audio, double-click to add or remove a flag"));
    add ("warp.previousHit", tr ("Previous &Hit"), { KS (Qt::Key_Comma) }, [this] { stepTransient (false); });
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
    add ("warp.quantize", tr ("&Quantize..."), { KS (Qt::Key_Q) }, [this] { quantizeDialog(); })
        ->setStatusTip (tr ("Move the hits towards the beat grid, by stretching the audio between them"));
    connect (waveform, &WaveformView::warpMoved, this, [this] (Frame source, Frame target, int marker, bool reanchor)
    {
        warpMove (source, target, marker, reanchor);
    });
    connect (waveform, &WaveformView::warpToggled, this, [this] (Frame source) { warpToggle (source); });
    add ("warp.stretch", tr ("&Stretch to Tempo..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_J) }, [this] { stretchToTempo(); })
        ->setStatusTip (tr ("Change the tempo of the selection or the whole file, keeping the pitch (or with it, in Varispeed)"));
}

void MainWindow::updateWarpCommandStates()
{
    const bool open = doc.isOpen() && ! loading && ! doc.isAlbum();
    const bool grid = open && doc.current().grid.isSet();
    for (const auto* a : { "warp.detectGrid", "warp.setTempo", "warp.downbeat", "warp.stretch" })
        commands.action (QString::fromLatin1 (a))->setEnabled (open);
    for (const auto* a : { "warp.halve", "warp.double" })
        commands.action (QString::fromLatin1 (a))->setEnabled (grid);
    commands.action (QStringLiteral ("warp.edit"))->setEnabled (open);
    commands.action (QStringLiteral ("warp.quantize"))->setEnabled (open);
    if (! open && warpEditing)
        setWarpEditing (false);
    for (const auto* a : { "warp.previousHit", "warp.nextHit", "warp.toggleMarker", "warp.nudgeEarlier", "warp.nudgeLater",
                           "warp.nudgeEarlierFine", "warp.nudgeLaterFine" })
        commands.action (QString::fromLatin1 (a))->setEnabled (open && warpEditing);
}

void MainWindow::refreshGrid()
{
    const auto grid = doc.isOpen() ? doc.current().grid : BeatGrid();
    waveform->setGrid (grid, commands.action (QStringLiteral ("warp.showGrid"))->isChecked());
    const auto ruler = commands.action (QStringLiteral ("view.rulerBeats"))->isChecked() ? TimeRuler::Mode::beats
                     : commands.action (QStringLiteral ("view.rulerBoth"))->isChecked() ? TimeRuler::Mode::both : TimeRuler::Mode::time;
    timeRuler->setGrid (grid, ruler);
    transportBar->setTempo (grid.isSet() ? grid.bpm : 0.0);
    waveform->setWarp (currentWarp(), warpEditing);
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
        draft->transients = timestretch::transients (*d, appSettings().value (QStringLiteral ("warp/sensitivity"), 0.5).toDouble());
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
                                          error, &progress, &cancel, warpCache.get());
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

void MainWindow::quantizeDialog()
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit())
        return;
    const auto title = tr ("Quantize");
    const auto& grid = doc.current().grid;
    if (! grid.isSet())
    {
        QMessageBox::information (this, title, tr ("Quantizing needs the beat grid. Use Warp > Detect Beat Grid, or set the tempo with Ctrl+Shift+J."));
        return;
    }
    if (! warpEditing)
        setWarpEditing (true);
    const auto w = currentWarp();
    if (w == nullptr)
        return;

    QDialog dialog (this);
    dialog.setWindowTitle (title);
    auto* form = new QFormLayout (&dialog);
    auto* division = new QComboBox (&dialog);
    const std::pair<QString, double> divisions[] { { tr ("1/4 (quarter notes)"), 1.0 }, { tr ("1/8"), 0.5 }, { tr ("1/16"), 0.25 },
                                                   { tr ("1/32"), 0.125 }, { tr ("1/8 triplets"), 1.0 / 3.0 }, { tr ("1/16 triplets"), 1.0 / 6.0 } };
    for (const auto& [name, value] : divisions)
        division->addItem (name, value);
    division->setCurrentIndex (appSettings().value (QStringLiteral ("warp/quantizeDivision"), 2).toInt());
    auto percent = [&dialog] (const QString& key, int fallback)
    {
        auto* box = new QSpinBox (&dialog);
        box->setRange (0, 100);
        box->setSuffix (QStringLiteral (" %"));
        box->setValue (appSettings().value (key, fallback).toInt());
        return box;
    };
    auto* strength = percent (QStringLiteral ("warp/quantizeStrength"), 100);
    auto* swing = percent (QStringLiteral ("warp/quantizeSwing"), 0);
    auto* leave = percent (QStringLiteral ("warp/quantizeLeaveWithin"), 0);
    auto* reach = percent (QStringLiteral ("warp/quantizeReach"), 100);
    auto* mode = new QComboBox (&dialog);
    for (const auto m : timestretch::modes())
        mode->addItem (tr (timestretch::modeName (m)), (int) m);
    mode->setCurrentIndex (std::max (0, mode->findData (w->mode)));
    form->addRow (tr ("&Grid:"), division);
    form->addRow (tr ("&Strength:"), strength);
    form->addRow (tr ("S&wing:"), swing);
    form->addRow (tr ("&Leave within:"), leave);
    form->addRow (tr ("&Reach:"), reach);
    form->addRow (tr ("&Mode:"), mode);
    auto* hint = new QLabel (tr ("Strength 100 % puts every hit on the grid, good for loops; 50 to 80 % tightens a performance and keeps its feel. "
                                 "Leave within: hits this close to their line (as a share of one step) stay put. Reach: only hits this close move."),
                             &dialog);
    hint->setWordWrap (true);
    form->addRow (hint);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("Quantize"));
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    division->setFocus();
    if (dialog.exec() != QDialog::Accepted)
    {
        waveform->setFocus();
        return;
    }
    appSettings().setValue (QStringLiteral ("warp/quantizeDivision"), division->currentIndex());
    appSettings().setValue (QStringLiteral ("warp/quantizeStrength"), strength->value());
    appSettings().setValue (QStringLiteral ("warp/quantizeSwing"), swing->value());
    appSettings().setValue (QStringLiteral ("warp/quantizeLeaveWithin"), leave->value());
    appSettings().setValue (QStringLiteral ("warp/quantizeReach"), reach->value());
    appSettings().setValue (QStringLiteral ("warp/mode"), mode->currentData().toInt());

    timestretch::Quantize q;
    q.division = division->currentData().toDouble();
    q.strength = strength->value() / 100.0;
    q.swing = swing->value() / 100.0;
    q.leaveWithin = leave->value() / 100.0;
    q.reach = reach->value() / 100.0;
    // In the selection (or the whole file), placed from the original performance.
    const auto [s, e] = processRange();
    const auto from = w->toSource (s), to = w->toSource (e);
    const auto rate = d->sampleRate();
    auto next = std::make_shared<WarpEdit> (*w);
    next->mode = mode->currentData().toInt();
    std::vector<WarpEdit::Marker> kept;
    for (const auto& m : w->markers)
        if (m.source < from || m.source >= to)
            kept.push_back (m);
    // The grid is on the result's timeline; hits are placed as if the source were unwarped.
    for (const auto& [source, target] : timestretch::quantize (w->transients, grid, rate, q, from, to))
        kept.push_back ({ source, target });
    std::sort (kept.begin(), kept.end(), [] (const WarpEdit::Marker& a, const WarpEdit::Marker& b) { return a.source < b.source; });
    next->markers.clear();
    const auto gap = (Frame) (0.001 * rate);
    for (const auto& m : kept)
        if (next->markers.empty() || (m.source > next->markers.back().source + gap && m.target > next->markers.back().target + gap))
            next->markers.push_back (m);
    applyWarp (tr ("Quantize %1").arg (division->currentText()), std::move (next));
    statusBar()->showMessage (tr ("Quantized to %1 at %2 %; Ctrl+Z undoes it").arg (division->currentText()).arg (strength->value()), 6000);
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
        statusBar()->showMessage (tr ("Tempo detected: %1 BPM. Type another in the BPM box, or use Warp > Halve or Double Tempo.")
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
    const auto target = newLength();
    if (target == length)
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
    if (whole)
    {
        state.grid.bpm = to->value();
        state.grid.downbeat *= ratio;
        state.grid.normalise();
        state.tags[tag::bpm] = bpmText (to->value()).toStdString();
    }
    doc.apply ((title + QStringLiteral (" (%1)").arg (tr (timestretch::modeName (m)))).toStdString(), std::move (state));
    if (hasSelection())
        selEnd = s + target;
    refreshFromDocument (true);
    statusBar()->showMessage (tr ("Stretched to %1 s").arg ((double) target / rate, 0, 'f', 3), 6000);
}
