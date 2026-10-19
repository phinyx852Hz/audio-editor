// SPDX-License-Identifier: AGPL-3.0-only
// The Warp menu: the beat grid (tempo, downbeat, bars over the waveform) and
// stretching to a tempo with Rubber Band (core/TimeStretch.h). Warp markers,
// quantizing and loop export build on these.
#include "MainWindow.h"

#include "Settings.h"
#include "WaveformView.h"
#include "../core/TimeStretch.h"
#include "../engine/VampHost.h"

#include <QCheckBox>
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
    add ("warp.detectGrid", tr ("&Detect Beat Grid"), {}, [this] { detectBeatGrid(); })
        ->setStatusTip (tr ("Find the beats and bars and fit a steady tempo grid to them"));
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
}

void MainWindow::refreshGrid()
{
    waveform->setGrid (doc.isOpen() ? doc.current().grid : BeatGrid(),
                       commands.action (QStringLiteral ("warp.showGrid"))->isChecked());
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
    std::vector<double> beats;
    std::vector<int> inBar;
    for (const auto& f : result.features)
    {
        beats.push_back ((double) f.start / d->sampleRate());
        inBar.push_back (QString::fromStdString (f.label).trimmed().toInt());
    }
    int perBar = 0;
    for (const auto b : inBar)
        perBar = std::max (perBar, b);
    const auto grid = beatgrid::align (*d, beatgrid::fit (beats, inBar, std::clamp (perBar, 2, 16)), true);
    if (! grid.isSet())
    {
        QMessageBox::information (this, title, tr ("No steady beat was found in %1.").arg (hasSelection() ? tr ("the selection") : tr ("the file")));
        return;
    }
    applyGrid (title, grid);
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
