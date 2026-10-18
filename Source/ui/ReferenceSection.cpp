// SPDX-License-Identifier: AGPL-3.0-only
// The A/B Reference section of MainWindow: reference recordings you switch to
// at the same moment as your audio, matched in loudness so the comparison is
// fair, and heard with or without the Master Section. The engine side is in
// AudioEngine (setReference, setHearingReference).
#include "MainWindow.h"
#include "Panels.h"
#include "ReferencePanel.h"
#include "Theme.h"

#include "../core/Dsp.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>

#include <cmath>

namespace
{
    QSettings referenceSettings()
    {
        return QSettings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                              + QStringLiteral ("/" APP_ID "/settings.ini"), QSettings::IniFormat);
    }

    bool measured (double lufs) { return lufs > -199.0; }
}

//==============================================================================
void MainWindow::buildReferences()
{
    referencePanel = new ReferencePanel (this);
    referencesPanel = new DockablePanel (*this, QStringLiteral ("referencesPanel"), tr ("References"), referencePanel, Qt::RightDockWidgetArea);

    referenceBadge = new QLabel (waveform);
    referenceBadge->setAttribute (Qt::WA_TransparentForMouseEvents);
    referenceBadge->hide();

    connect (referencePanel, &ReferencePanel::addRequested, this, [this] { addReferences(); });
    connect (referencePanel, &ReferencePanel::removeRequested, this, [this] (int row) { removeReference (row); });
    connect (referencePanel, &ReferencePanel::chosen, this, [this] (int row)
    {
        // Moving through the list while a reference plays switches to the new one.
        currentReference = row;
        if (engine.isHearingReference())
            hearReference (row);
        else
            updateReferencePanel();
        saveReferences();
    });
    connect (referencePanel, &ReferencePanel::hearRequested, this, [this] (int row)
    {
        if (engine.isHearingReference() && row == currentReference)
            setHearingReference (false);
        else
            hearReference (row);
    });
    connect (referencePanel, &ReferencePanel::offsetChanged, this, [this] (int row, double db)
    {
        if (row < 0 || row >= (int) references.size())
            return;
        references[(size_t) row].offsetDb = db;
        if (row == currentReference)
            engine.setReferenceGain (referenceGain (row));
        updateReferencePanel();
        saveReferences();
    });
    connect (referencePanel, &ReferencePanel::cleanChanged, this, [this] (bool on)
    {
        referenceClean = on;
        engine.setReferenceBypassesMaster (on);
        saveReferences();
        // What you hear of your own audio changes the match only through the Master Section.
        matchedFor = nullptr;
        if (engine.isHearingReference())
            hearReference (currentReference);
    });
    connect (referencePanel, &ReferencePanel::matchRequested, this, [this]
    {
        matchedFor = nullptr;
        if (matchLoudness() && currentReference >= 0)
            engine.setReferenceGain (referenceGain (currentReference));
        updateReferencePanel();
    });

    // The list is remembered; the audio is loaded when a reference is first heard.
    auto settings = referenceSettings();
    const auto paths = settings.value (QStringLiteral ("references/paths")).toStringList();
    const auto offsets = settings.value (QStringLiteral ("references/offsets")).toStringList();
    for (int i = 0; i < paths.size(); ++i)
    {
        ReferenceTrack r;
        r.path = paths[i];
        r.offsetDb = i < offsets.size() ? offsets[i].toDouble() : 0.0;
        references.push_back (std::move (r));
    }
    referenceClean = settings.value (QStringLiteral ("references/clean"), true).toBool();
    currentReference = references.empty() ? -1 : std::clamp (settings.value (QStringLiteral ("references/current"), 0).toInt(), 0, (int) references.size() - 1);
    engine.setReferenceBypassesMaster (referenceClean);
    referencePanel->setClean (referenceClean);
    updateReferencePanel();
}

void MainWindow::registerReferenceCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("References");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        addAction (commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn)));
    };

    add ("reference.toggle", tr ("&Switch Between Your Audio and Reference"), { KS (Qt::Key_A) }, [this]
    {
        if (engine.isHearingReference())
            setHearingReference (false);
        else if (references.empty())
            addReferences();
        else
            hearReference (std::max (0, currentReference));
    });
    add ("reference.next", tr ("&Next Reference"), { KS (Qt::SHIFT | Qt::Key_A) }, [this]
    {
        if (references.empty())
            return;
        hearReference ((currentReference + 1) % (int) references.size());
    });
    add ("reference.add", tr ("&Add Reference..."), {}, [this] { addReferences(); });
    add ("reference.match", tr ("&Match Reference Loudness Again"), {}, [this]
    {
        matchedFor = nullptr;
        if (matchLoudness() && currentReference >= 0)
            engine.setReferenceGain (referenceGain (currentReference));
        updateReferencePanel();
    });
    add ("view.references", tr ("&References Panel"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_R) }, [this]
    {
        referencesPanel->toggle();
        lastPanel = referencesPanel;
    });
}

//==============================================================================
void MainWindow::addReferences()
{
    QStringList patterns;
    for (const auto& ext : AudioEngine::readableExtensions())
        patterns << QStringLiteral ("*.") + QString::fromStdString (ext);
    const auto start = references.empty() ? QStandardPaths::writableLocation (QStandardPaths::MusicLocation)
                                          : QFileInfo (references.back().path).absolutePath();
    const auto paths = QFileDialog::getOpenFileNames (this, tr ("Add Reference Tracks"), start,
                                                      tr ("Audio files (%1)").arg (patterns.join (QLatin1Char (' '))));
    waveform->setFocus();
    if (paths.isEmpty())
        return;

    const auto first = (int) references.size();
    for (const auto& p : paths)
    {
        ReferenceTrack r;
        r.path = p;
        references.push_back (std::move (r));
    }
    currentReference = first;
    saveReferences();
    referencesPanel->show (false);
    statusBar()->showMessage ((paths.size() == 1 ? tr ("Added a reference.") : tr ("Added %1 references.").arg (paths.size()))
                              + tr (" Press A to switch between your audio and the reference, Shift+A for the next one."), 8000);
    updateReferencePanel();
}

void MainWindow::removeReference (int row)
{
    if (row < 0 || row >= (int) references.size())
        return;
    if (row == currentReference && engine.isHearingReference())
        setHearingReference (false);
    references.erase (references.begin() + row);
    currentReference = references.empty() ? -1 : std::min (currentReference, (int) references.size() - 1);
    engine.setReference (nullptr, 1.0f);
    saveReferences();
    updateReferencePanel();
}

bool MainWindow::loadReference (int row)
{
    auto& r = references[(size_t) row];
    const auto d = data();
    const auto rate = d != nullptr ? d->sampleRate() : 0.0;
    if (r.original != nullptr && (r.atRate == nullptr || std::abs (r.atRate->sampleRate() - rate) < 1.0e-6))
        return true;

    // Loading, measuring and converting to your audio's sample rate happen once per reference (and rate).
    std::string error;
    AudioDataPtr original = r.original, atRate;
    double lufs = r.lufs;
    const auto ran = runWithProgress (tr ("Loading Reference"), [&] (std::atomic<float>& progress, const std::atomic<bool>&)
    {
        if (original == nullptr)
        {
            std::vector<Marker> markers;
            Tags tags;
            original = AudioEngine::loadFile (r.path.toStdString(), markers, tags, error, &progress);
            if (original != nullptr)
                lufs = dsp::measureLoudness (*original, 0, original->length()).integratedLufs;
        }
        if (original != nullptr && rate > 0.0)
            atRate = std::abs (original->sampleRate() - rate) < 1.0e-6 ? original : dsp::resample (*original, rate);
    });
    waveform->setFocus();
    if (! ran)
        return false;
    if (original == nullptr)
    {
        QMessageBox::warning (this, tr ("References"), tr ("Could not open %1:\n%2").arg (r.path, QString::fromStdString (error)));
        return false;
    }
    r.original = original;
    r.atRate = atRate;
    r.lufs = lufs;
    return true;
}

bool MainWindow::matchLoudness()
{
    const auto d = data();
    if (d == nullptr)
        return false;
    if (matchedFor == d.get())
        return true;

    // Your audio as you hear it: through the Master Section when it does anything,
    // since references are heard without it by default.
    const bool throughMaster = engine.masterIsActive() && ! engine.isMasterBypassed();
    double lufs = -200.0;
    std::string error;
    const auto ran = runWithProgress (tr ("Measuring Loudness"), [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
    {
        if (throughMaster)
        {
            const auto rendered = engine.renderThroughMaster (*d, 0, d->length(), error, &progress, &cancel);
            if (rendered != nullptr)
                lufs = dsp::measureLoudness (*rendered, 0, rendered->length()).integratedLufs;
        }
        else
            lufs = dsp::measureLoudness (*d, 0, d->length()).integratedLufs;
    });
    waveform->setFocus();
    if (! ran)
        return false;
    if (! error.empty())
        statusBar()->showMessage (tr ("Could not measure through the Master Section: %1").arg (QString::fromStdString (error)), 8000);
    yourLufs = lufs;
    yourThroughMaster = throughMaster;
    matchedFor = d.get();
    matchedData = d;
    return true;
}

float MainWindow::referenceGain (int row) const
{
    const auto& r = references[(size_t) row];
    const auto match = measured (yourLufs) && measured (r.lufs) ? yourLufs - r.lufs : 0.0;
    return (float) dsp::dbToGain (match + r.offsetDb);
}

void MainWindow::hearReference (int row)
{
    if (data() == nullptr || row < 0 || row >= (int) references.size())
        return;
    if (! loadReference (row) || ! matchLoudness())
        return;

    currentReference = row;
    const auto& r = references[(size_t) row];
    engine.setReference (r.atRate, referenceGain (row));
    setHearingReference (true);
    saveReferences();
}

void MainWindow::setHearingReference (bool on)
{
    engine.setHearingReference (on);
    if (! on)
        statusBar()->showMessage (tr ("Hearing your audio"), 3000);
    else if (currentReference >= 0)
        statusBar()->showMessage (tr ("Hearing reference %1: %2").arg (currentReference + 1).arg (QFileInfo (references[(size_t) currentReference].path).fileName()), 3000);
    updateReferencePanel();
}

void MainWindow::syncReference()
{
    // A different tab or sample rate: the reference follows your audio.
    const auto d = data();
    if (! engine.isHearingReference())
        return;
    if (d == nullptr || currentReference < 0)
    {
        setHearingReference (false);
        return;
    }
    const auto& r = references[(size_t) currentReference];
    if (r.atRate == nullptr || std::abs (r.atRate->sampleRate() - d->sampleRate()) > 1.0e-6)
    {
        engine.setHearingReference (false);
        hearReference (currentReference);
    }
}

void MainWindow::placeReferenceBadge()
{
    const bool preview = ! previewName.isEmpty();
    const bool show = engine.isHearingReference() && (preview || currentReference >= 0) && data() != nullptr;
    if (show)
    {
        const auto text = preview ? previewName : tr ("B  %1").arg (QFileInfo (references[(size_t) currentReference].path).completeBaseName());
        if (referenceBadge->text() != text)
        {
            referenceBadge->setText (text);
            referenceBadge->setStyleSheet (QStringLiteral ("color: %1; font-weight: bold; background: transparent;")
                                               .arg (Theme::current().color (QStringLiteral ("accent")).name()));
            referenceBadge->adjustSize();
        }
        referenceBadge->move (waveform->width() - referenceBadge->width() - 10, 4);
    }
    if (referenceBadge->isVisible() != show)
        referenceBadge->setVisible (show);
}

void MainWindow::updateReferencePanel()
{
    QList<ReferencePanel::Row> rows;
    for (const auto& r : references)
    {
        ReferencePanel::Row row;
        row.name = QFileInfo (r.path).fileName();
        row.offsetDb = r.offsetDb;
        if (measured (r.lufs))
        {
            const auto change = measured (yourLufs) ? yourLufs - r.lufs + r.offsetDb : r.offsetDb;
            row.detail = tr ("%1 LUFS, heard %2").arg (r.lufs, 0, 'f', 1)
                             .arg (std::abs (change) < 0.05 ? tr ("as it is")
                                   : change < 0 ? tr ("%1 dB quieter").arg (-change, 0, 'f', 1) : tr ("%1 dB louder").arg (change, 0, 'f', 1));
        }
        else
            row.detail = tr ("not measured yet");
        rows << row;
    }
    referencePanel->setRows (rows, currentReference, engine.isHearingReference());

    QString status;
    if (references.empty())
        status = tr ("Add finished songs to compare with yours (Insert). A switches between your audio and the reference, at the same place.");
    else if (measured (yourLufs))
        status = tr ("Matched to your audio at %1 LUFS%2. A switches, Shift+A goes to the next reference.")
                     .arg (yourLufs, 0, 'f', 1).arg (yourThroughMaster ? tr (" (through the Master Section)") : QString());
    else
        status = tr ("A switches between your audio and the reference; the first switch measures loudness so both sound equally loud.");
    referencePanel->setStatus (status);
}

void MainWindow::saveReferences()
{
    QStringList paths, offsets;
    for (const auto& r : references)
    {
        paths << r.path;
        offsets << QString::number (r.offsetDb, 'f', 2);
    }
    auto settings = referenceSettings();
    settings.setValue (QStringLiteral ("references/paths"), paths);
    settings.setValue (QStringLiteral ("references/offsets"), offsets);
    settings.setValue (QStringLiteral ("references/clean"), referenceClean);
    settings.setValue (QStringLiteral ("references/current"), currentReference);
}
