// SPDX-License-Identifier: AGPL-3.0-only
// Live edits for MainWindow: the floating gain knob, its keyboard nudges and
// the fade handles change what you hear and see while you adjust them, then
// become a single undo step.
#include "MainWindow.h"
#include "GainHud.h"

#include "../core/Dsp.h"

#include <QSettings>
#include <QStatusBar>

void MainWindow::buildLiveEdits()
{
    gainHud = new GainHud (waveform);
    gainHud->setVisible (QSettings().value (QStringLiteral ("view/gainKnob"), true).toBool());

    liveTimer.setSingleShot (true);
    liveTimer.setInterval (40);
    connect (&liveTimer, &QTimer::timeout, this, [this]
    {
        if (liveBase == nullptr || liveMake == nullptr)
            return;
        if (data() != liveBase)
        {
            cancelLive();   // something else changed the audio meanwhile
            return;
        }
        livePreview = liveMake (*liveBase);
        const auto shown = livePreview != nullptr ? livePreview : liveBase;
        engine.setData (shown);
        waveform->setData (shown, true);
    });

    nudgeTimer.setSingleShot (true);
    nudgeTimer.setInterval (900);
    connect (&nudgeTimer, &QTimer::timeout, this, [this]
    {
        if (std::abs (nudgeDb) < 0.005)
            cancelLive();
        else
            finishLive();
        nudgeDb = 0.0;
        gainHud->setValue (0.0);
    });

    auto previewGain = [this] (double db)
    {
        const auto [s, e] = liveBase != nullptr ? std::pair { liveStart, liveEnd } : processRange();
        previewLive (tr ("Gain %1%2 dB").arg (db > 0 ? QStringLiteral ("+") : QString()).arg (db, 0, 'f', 1), s, e,
                     [s, e, db] (const AudioData& d) { return std::abs (db) < 0.005 ? nullptr : dsp::applyGain (d, s, e, db); });
    };
    connect (gainHud, &GainHud::changed, this, previewGain);
    connect (gainHud, &GainHud::finished, this, [this] (double db)
    {
        if (std::abs (db) < 0.005)
            cancelLive();
        else
            finishLive();
        gainHud->setValue (0.0);
    });
    connect (gainHud, &GainHud::typeRequested, this, [this] { gain(); });

    connect (waveform, &WaveformView::fadeDragged, this, [this] (bool in, Frame length, double curve)
    {
        const auto d = liveBase != nullptr ? liveBase : data();
        if (d == nullptr)
            return;
        const auto s = in ? Frame (0) : d->length() - length;
        const auto e = in ? length : d->length();
        previewLive (in ? tr ("Fade In") : tr ("Fade Out"), s, e, [s, e, curve, in] (const AudioData& a)
        {
            return e > s ? dsp::curvedFade (a, s, e, curve, in) : nullptr;
        });
    });
    connect (waveform, &WaveformView::fadeReleased, this, [this] (bool, Frame length, double)
    {
        if (length > 0)
            finishLive();
        else
            cancelLive();
    });
}

void MainWindow::placeGainHud()
{
    const bool show = QSettings().value (QStringLiteral ("view/gainKnob"), true).toBool()
                   && data() != nullptr && ! doc.isAlbum() && ! waveform->isSpectral();
    if (gainHud->isVisible() != show)
        gainHud->setVisible (show);
    const QPoint at ((waveform->width() - gainHud->width()) / 2, 6);
    if (gainHud->pos() != at)
        gainHud->move (at);
}

void MainWindow::previewLive (const QString& description, Frame start, Frame end, std::function<AudioDataPtr (const AudioData&)> make)
{
    if (liveBase == nullptr)
    {
        if (data() == nullptr || refuseAlbumEdit())
            return;
        liveBase = data();
    }
    liveDescription = description;
    liveStart = start;
    liveEnd = end;
    liveMake = std::move (make);
    if (! liveTimer.isActive())
        liveTimer.start();
}

void MainWindow::finishLive()
{
    liveTimer.stop();
    const auto base = std::exchange (liveBase, nullptr);
    const auto make = std::exchange (liveMake, nullptr);
    livePreview = nullptr;
    if (base == nullptr)
        return;
    if (data() != base || make == nullptr)
    {
        refreshFromDocument (true);
        return;
    }

    auto result = make (*base);
    if (result == nullptr)
        refreshFromDocument (true);
    else
        commit (liveDescription, std::move (result), liveStart, liveEnd, liveEnd - liveStart);
}

void MainWindow::cancelLive()
{
    liveTimer.stop();
    const bool wasLive = liveBase != nullptr;
    liveBase = nullptr;
    liveMake = nullptr;
    livePreview = nullptr;
    if (wasLive)
        refreshFromDocument (true);
}

void MainWindow::nudgeGain (double db)
{
    if (data() == nullptr || refuseAlbumEdit())
        return;
    nudgeDb = std::clamp (nudgeDb + db, GainHud::minDb, GainHud::maxDb);
    gainHud->setValue (nudgeDb);
    const auto [s, e] = liveBase != nullptr ? std::pair { liveStart, liveEnd } : processRange();
    const auto total = nudgeDb;
    previewLive (tr ("Gain %1%2 dB").arg (total > 0 ? QStringLiteral ("+") : QString()).arg (total, 0, 'f', 1), s, e,
                 [s, e, total] (const AudioData& d) { return std::abs (total) < 0.005 ? nullptr : dsp::applyGain (d, s, e, total); });
    nudgeTimer.start();
    statusBar()->showMessage (tr ("Gain %1%2 dB on %3; it is applied when you stop pressing")
                                  .arg (total > 0 ? QStringLiteral ("+") : QString()).arg (total, 0, 'f', 1)
                                  .arg (hasSelection() ? tr ("the selection") : tr ("the whole file")), 3000);
}
