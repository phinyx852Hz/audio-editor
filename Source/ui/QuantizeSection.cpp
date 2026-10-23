// SPDX-License-Identifier: AGPL-3.0-only
// The Quantize panel: Cubase's Hitpoints and Quantize Panel and Live's warping in one
// place. Hitpoints are found with the panel's detection (core/Quantize.h), quantizing
// places warp markers on them (the stretching is in WarpSection.cpp), and the warp
// algorithm re-renders what is already warped.
#include "MainWindow.h"

#include "Panels.h"
#include "QuantizePanel.h"
#include "Settings.h"
#include "WaveformView.h"
#include "../core/TimeStretch.h"

#include <QFileInfo>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>

#include <cmath>

namespace
{
    // "1/16", "1/8 triplet", "1/4 dotted" or the groove's name, for undo and the status bar.
    QString describe (const audioquantize::Settings& s)
    {
        if (s.groove.isSet())
            return QString::fromStdString (s.groove.name);
        for (const auto& [factor, kind] : { std::pair { 1.0, "" }, std::pair { 2.0 / 3.0, " triplet" }, std::pair { 1.5, " dotted" } })
        {
            const auto straight = s.division / factor;
            const auto denominator = 4.0 / straight;
            if (std::abs (denominator - std::round (denominator)) < 1e-6 && denominator >= 1.0)
                return QStringLiteral ("1/%1%2").arg (std::lround (denominator)).arg (QLatin1String (kind));
        }
        return QStringLiteral ("%1 beats").arg (s.division);
    }
}

void MainWindow::buildQuantizePanel()
{
    quantizeWidget = new QuantizePanel (this);
    // It scrolls inside its dock rather than squeezing its controls when the dock is short.
    auto* area = new QScrollArea (this);
    area->setWidget (quantizeWidget);
    area->setWidgetResizable (true);
    area->setFrameShape (QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    area->setMinimumWidth (quantizeWidget->sizeHint().width() + area->verticalScrollBar()->sizeHint().width());
    area->setMinimumHeight (std::min (quantizeWidget->sizeHint().height(), 480));
    area->setFocusProxy (quantizeWidget);
    area->viewport()->setAutoFillBackground (false);
    quantizeWidget->setAutoFillBackground (false);
    quantizePanel = new DockablePanel (*this, QStringLiteral ("quantizePanel"), tr ("Quantize"), area, Qt::RightDockWidgetArea);
    panels << quantizePanel;

    // Moving the threshold finds the hits again once it rests; Auto Apply quantizes again the same way.
    detectTimer = new QTimer (this);
    detectTimer->setSingleShot (true);
    detectTimer->setInterval (150);
    connect (detectTimer, &QTimer::timeout, this, [this] { detectHitpoints(); });
    requantizeTimer = new QTimer (this);
    requantizeTimer->setSingleShot (true);
    requantizeTimer->setInterval (250);
    connect (requantizeTimer, &QTimer::timeout, this, [this] { quantizeNow (true); });

    connect (quantizeWidget, &QuantizePanel::editToggled, this, [this] (bool on) { setWarpEditing (on); });
    connect (quantizeWidget, &QuantizePanel::detectionChanged, detectTimer, qOverload<> (&QTimer::start));
    connect (quantizeWidget, &QuantizePanel::settingsChanged, this, [this]
    {
        if (quantizeWidget->autoApply())
            requantizeTimer->start();
    });
    connect (quantizeWidget, &QuantizePanel::quantizeRequested, this, [this] { quantizeNow (false); });
    connect (quantizeWidget, &QuantizePanel::resetRequested, this, [this] { resetWarp(); });
    connect (quantizeWidget, &QuantizePanel::markersFromHitsRequested, this, [this] { warpMarkersFromHits(); });
    connect (quantizeWidget, &QuantizePanel::sliceRequested, this, [this] { sliceAtHits(); });
    connect (quantizeWidget, &QuantizePanel::grooveFromHitsRequested, this, [this] { grooveFromHits(); });
    connect (quantizeWidget, &QuantizePanel::modeChanged, this, [this] (int m) { setWarpMode (m); });
    connect (quantizeWidget, &QuantizePanel::snapChanged, this, [this] { refreshGrid(); });
    connect (quantizeWidget, &QuantizePanel::stretchRequested, this, [this] (double speed) { stretchBy (speed); });
    connect (quantizeWidget, &QuantizePanel::stretchToTempoRequested, this, [this] { stretchToTempo(); });
    connect (quantizeWidget, &QuantizePanel::detectTempoRequested, this, [this] { detectBeatGrid(); });
    connect (quantizeWidget, &QuantizePanel::setTempoRequested, this, [this] { setTempoDialog(); });
    connect (waveform, &WaveformView::stretchDragged, this, [this] (Frame s, Frame e, Frame newEnd)
    {
        if (data() == nullptr || refuseAlbumEdit() || newEnd <= s)
            return;
        applyStretch (tr ("Stretch Selection"), s, e, newEnd - s, quantizeWidget->mode(), 0.0);
    });
}

void MainWindow::registerQuantizeCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Warp");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (action);
        return action;
    };
    add ("warp.quantizePanel", tr ("Q&uantize Panel"), { KS (Qt::SHIFT | Qt::Key_Q) }, [this]
    {
        quantizePanel->toggle();
        lastPanel = quantizePanel;
        updateQuantizePanel();
    })->setStatusTip (tr ("Hitpoints, grid, swing, grooves and warp algorithms in one panel that docks or pops out"));
    add ("warp.resetWarp", tr ("&Reset Warp"), {}, [this] { resetWarp(); })
        ->setStatusTip (tr ("Remove the warp markers in the selection (or all of them): the timing as it was recorded"));
    add ("warp.markersFromHits", tr ("Warp Markers on &Hits"), {}, [this] { warpMarkersFromHits(); })
        ->setStatusTip (tr ("A warp marker on every hit in the selection or file, where it is"));
    add ("warp.sliceAtHits", tr ("S&lice at Hits"), {}, [this] { sliceAtHits(); })
        ->setStatusTip (tr ("A slice marker at every hit in the selection or file"));
}

std::vector<Frame> MainWindow::findHits (const AudioData& audio) const
{
    auto hits = audioquantize::hits (audio, quantizeWidget->detection());
    if (const auto every = quantizeWidget->hitFilter(); every > 0.0 && doc.isOpen() && doc.current().grid.isSet())
        hits = audioquantize::onGrid (hits, doc.current().grid, audio.sampleRate(), every);
    return hits;
}

void MainWindow::updateQuantizePanel()
{
    if (quantizeWidget == nullptr || ! quantizePanel->isShown())
        return;
    QuantizePanel::State state;
    const auto d = data();
    state.open = d != nullptr && ! doc.isAlbum() && ! loading;
    if (state.open)
    {
        state.grid = doc.current().grid;
        state.editing = warpEditing;
        const auto w = currentWarp();
        if (w != nullptr)
        {
            state.hits = (int) w->transients.size();
            state.markers = (int) w->markers.size();
        }
        state.range = hasSelection() ? tr ("the selection (%1 s)").arg ((double) (selEnd - selStart) / d->sampleRate(), 0, 'f', 2)
                                     : tr ("the whole file");
        if (state.grid.isSet() && w != nullptr)
        {
            const auto [s, e] = processRange();
            const auto bar = (double) state.grid.beatsPerBar;
            for (const auto t : w->transients)
            {
                const auto now = w->toTarget (t);
                if (now < s || now >= e)
                    continue;
                const auto beat = state.grid.beatAt ((double) now / d->sampleRate());
                state.hitsInBar.push_back (beat - bar * std::floor (beat / bar));
                if (state.hitsInBar.size() >= 4000)
                    break;
            }
        }
    }
    quantizeWidget->setState (state);
}

std::vector<std::pair<Frame, Frame>> MainWindow::hitsInRange()
{
    std::vector<std::pair<Frame, Frame>> out;
    const auto d = data();
    if (d == nullptr)
        return out;
    const auto [s, e] = processRange();
    if (const auto w = currentWarp(); w != nullptr)
    {
        for (const auto t : w->transients)
            if (const auto now = w->toTarget (t); now >= s && now < e)
                out.push_back ({ t, now });
        return out;
    }
    for (const auto t : findHits (*d))
        if (t >= s && t < e)
            out.push_back ({ t, t });
    return out;
}

void MainWindow::detectHitpoints()
{
    const auto w = currentWarp();
    if (w == nullptr)
        return;
    auto next = std::make_shared<WarpEdit> (*w);
    next->transients = findHits (*w->source);
    const auto count = next->transients.size();
    if (doc.current().warp == w)
    {
        // Hits found again replace the last finding in the history rather than piling up.
        const auto description = tr ("Detect Hitpoints").toStdString();
        if (doc.undoDescription() == description)
            doc.undo();
        auto state = doc.current();
        state.warp = std::move (next);
        doc.apply (description, std::move (state));
        refreshFromDocument (true);
    }
    else
    {
        warpDraft = std::move (next);
        refreshGrid();
    }
    quantizeWidget->setStatus (tr ("%n hit(s) found", nullptr, (int) count));
}

void MainWindow::quantizeNow (bool replaceLast)
{
    const auto d = data();
    if (d == nullptr || doc.isAlbum())
        return;
    // Auto Apply only redoes a quantize just made on this audio, never a fresh one.
    if (replaceLast)
    {
        if (lastQuantize.result != d || doc.historyPosition() != lastQuantize.position || ! doc.canUndo())
            return;
        doc.undo();
        refreshFromDocument (true);
    }
    else if (refuseAlbumEdit())
        return;
    const auto& grid = doc.current().grid;
    if (! grid.isSet())
    {
        const auto text = tr ("Quantizing needs the tempo: Warp > Detect Beat Grid, or set it with Ctrl+Shift+J");
        statusBar()->showMessage (text, 6000);
        quantizeWidget->setStatus (text);
        return;
    }
    if (! warpEditing)
        setWarpEditing (true);
    const auto w = currentWarp();
    if (w == nullptr)
        return;

    const auto settings = quantizeWidget->settings();
    const bool iterative = quantizeWidget->iterative();
    // Placed from the original performance, or from where the hits are now when iterative.
    const auto [s, e] = processRange();
    const auto from = w->toSource (s), to = w->toSource (e);
    std::vector<audioquantize::Hit> hits;
    for (const auto t : w->transients)
        hits.push_back ({ t, iterative ? w->toTarget (t) : t });
    const auto rate = d->sampleRate();
    auto next = std::make_shared<WarpEdit> (*w);
    next->mode = quantizeWidget->mode();
    std::vector<WarpEdit::Marker> kept;
    for (const auto& m : w->markers)
        if (m.source < from || m.source >= to)
            kept.push_back (m);
    const auto placed = iterative ? audioquantize::place (hits, grid, rate, settings, s, e)
                                  : audioquantize::place (hits, grid, rate, settings, from, to);
    for (const auto& [source, target] : placed)
        kept.push_back ({ source, target });
    std::sort (kept.begin(), kept.end(), [] (const WarpEdit::Marker& a, const WarpEdit::Marker& b) { return a.source < b.source; });
    next->markers.clear();
    const auto gap = (Frame) (0.001 * rate);
    for (const auto& m : kept)
        if (next->markers.empty() || (m.source > next->markers.back().source + gap && m.target > next->markers.back().target + gap))
            next->markers.push_back (m);
    const auto name = describe (settings);
    applyWarp (tr ("Quantize %1").arg (name), std::move (next));
    lastQuantize = { data(), doc.historyPosition() };
    const auto text = tr ("Quantized %n hit(s) to %1 at %2 %; Ctrl+Z undoes it", nullptr, (int) placed.size())
                          .arg (name).arg (std::lround (settings.strength * 100.0));
    statusBar()->showMessage (text, 6000);
    quantizeWidget->setStatus (text);
}

void MainWindow::warpMarkersFromHits()
{
    if (data() == nullptr || refuseAlbumEdit())
        return;
    if (! warpEditing)
        setWarpEditing (true);
    const auto w = currentWarp();
    if (w == nullptr)
        return;
    const auto rate = w->source->sampleRate();
    const auto near = (Frame) (0.002 * rate);
    auto next = std::make_shared<WarpEdit> (*w);
    int added = 0;
    for (const auto& [source, now] : hitsInRange())
    {
        if (source <= near || source >= w->source->length() - near)
            continue;
        const auto at = std::lower_bound (next->markers.begin(), next->markers.end(), source,
                                          [] (const WarpEdit::Marker& x, Frame f) { return x.source < f; });
        if ((at != next->markers.end() && at->source - source <= near) || (at != next->markers.begin() && source - (at - 1)->source <= near))
            continue;
        next->markers.insert (at, { source, now });
        ++added;
    }
    if (added == 0)
    {
        quantizeWidget->setStatus (tr ("Every hit in the range already has a warp marker"));
        return;
    }
    auto state = doc.current();
    state.warp = std::move (next);
    doc.apply (tr ("Warp Markers on Hits").toStdString(), std::move (state));
    refreshFromDocument (true);
    quantizeWidget->setStatus (tr ("%n warp marker(s) added; drag one along the top to stretch the audio around it", nullptr, added));
}

void MainWindow::sliceAtHits()
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit())
        return;
    auto state = doc.current();
    int added = 0;
    for (const auto& [source, at] : hitsInRange())
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
        quantizeWidget->setStatus (tr ("No hits to slice at"));
        return;
    }
    std::sort (state.markers.begin(), state.markers.end(), [] (const Marker& a, const Marker& b) { return a.start < b.start; });
    doc.apply (tr ("Slice at Hits").toStdString(), std::move (state));
    refreshFromDocument (true);
    const auto text = tr ("%n slice(s): Markers > Export Slices saves them, or send the file to the Slicer", nullptr, added);
    statusBar()->showMessage (text, 6000);
    quantizeWidget->setStatus (text);
}

void MainWindow::grooveFromHits()
{
    const auto d = data();
    if (d == nullptr)
        return;
    const auto& grid = doc.current().grid;
    if (! grid.isSet())
    {
        quantizeWidget->setStatus (tr ("A groove is measured against the tempo grid: detect or set the tempo first"));
        return;
    }
    auto settings = quantizeWidget->settings();
    settings.groove = {};
    std::vector<Frame> now;
    for (const auto& [source, at] : hitsInRange())
        now.push_back (at);
    const auto steps = std::max (1, (int) std::lround (grid.beatsPerBar / settings.division));
    const auto [s, e] = processRange();
    auto groove = audioquantize::extract (now, grid, d->sampleRate(), settings.division, steps, s, e);
    if (! groove.isSet())
    {
        quantizeWidget->setStatus (tr ("Too few hits near the %1 grid to measure a groove").arg (describe (settings)));
        return;
    }
    const auto base = QFileInfo (QString::fromStdString (doc.path())).completeBaseName();
    groove.name = tr ("%1 groove (%2)").arg (base.isEmpty() ? tr ("Untitled") : base, describe (settings)).toStdString();
    quantizeWidget->addGroove (groove);
    quantizeWidget->setStatus (tr ("Groove \"%1\" saved and chosen: quantize other audio to it").arg (QString::fromStdString (groove.name)));
}

void MainWindow::resetWarp()
{
    const auto w = currentWarp();
    if (w == nullptr || w->markers.empty())
    {
        if (quantizeWidget != nullptr)
            quantizeWidget->setStatus (tr ("There are no warp markers to remove"));
        return;
    }
    const auto [s, e] = processRange();
    auto next = std::make_shared<WarpEdit> (*w);
    std::erase_if (next->markers, [s, e] (const WarpEdit::Marker& m) { return m.target >= s && m.target < e; });
    const auto removed = w->markers.size() - next->markers.size();
    applyWarp (tr ("Reset Warp"), std::move (next));
    quantizeWidget->setStatus (tr ("%n warp marker(s) removed", nullptr, (int) removed));
}

void MainWindow::setWarpMode (int mode)
{
    appSettings().setValue (QStringLiteral ("warp/mode"), mode);
    const auto w = currentWarp();
    if (w == nullptr || w->mode == mode)
        return;
    auto next = std::make_shared<WarpEdit> (*w);
    next->mode = mode;
    const auto name = tr (timestretch::modeName ((timestretch::Mode) mode));
    if (doc.current().warp == w && ! w->markers.empty())
    {
        applyWarp (tr ("Warp Algorithm: %1").arg (name), std::move (next));
        quantizeWidget->setStatus (tr ("Warped again with %1").arg (name));
    }
    else if (doc.current().warp == nullptr)
        warpDraft = std::move (next);
}
