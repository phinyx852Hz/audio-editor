// SPDX-License-Identifier: AGPL-3.0-only
#include "MixAssistant.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <cmath>

namespace
{
    enum Column { trackColumn, roleColumn, faderColumn, panColumn, yoursColumn, suggestedColumn, levelColumn };

    QString panText (double pan)
    {
        const auto v = (int) std::lround (pan * 100.0);
        return v == 0 ? QStringLiteral ("C") : (v < 0 ? QStringLiteral ("L%1") : QStringLiteral ("R%1")).arg (std::abs (v));
    }

    QString faderText (double db) { return db <= -96.0 ? QStringLiteral ("-inf") : QString::number (db, 'f', 1) + " dB"; }
    QString setting (double db, double pan) { return faderText (db) + ", " + panText (pan); }
}

MixAssistant::MixAssistant (mixer::Engine& e, QStringList trackNames, References refs, std::function<void()> play,
                            std::function<void()> changedStrips, QWidget* parent)
    : QDialog (parent), engine (e), references (std::move (refs)), playStop (std::move (play)), stripsChanged (std::move (changedStrips))
{
    setWindowTitle (tr ("Mix Assistant"));
    auto* layout = new QVBoxLayout (this);
    auto* intro = new QLabel (tr ("Sets the faders and pans of your mix: each track at the level its role suggests, with headroom "
                                  "for mastering. It only moves the faders and pans you allow; clips, instruments, trim and the "
                                  "patchbay stay as they are."), this);
    intro->setWordWrap (true);
    layout->addWidget (intro);

    auto* targetBox = new QGroupBox (tr ("Target"), this);
    auto* form = new QFormLayout (targetBox);
    loudness = new QDoubleSpinBox (targetBox);
    loudness->setRange (-40.0, -6.0);
    loudness->setDecimals (1);
    loudness->setSingleStep (0.5);
    loudness->setValue (-18.0);
    loudness->setSuffix (tr (" LUFS"));
    form->addRow (tr ("Mix &loudness:"), loudness);
    ceiling = new QDoubleSpinBox (targetBox);
    ceiling->setRange (-24.0, 0.0);
    ceiling->setDecimals (1);
    ceiling->setValue (-6.0);
    ceiling->setSuffix (tr (" dBTP"));
    ceiling->setToolTip (tr ("Headroom left for mastering: the mix's true peak at most"));
    form->addRow (tr ("&Peaks at most:"), ceiling);
    reference = new QComboBox (targetBox);
    reference->addItem (tr ("None"), -1);
    for (int i = 0; i < references.names.size(); ++i)
        reference->addItem (references.names[i], i);
    form->addRow (tr ("&Tone like:"), reference);
    auto* what = new QHBoxLayout;
    balanceBox = new QCheckBox (tr ("&Balance by role"), targetBox);
    panBox = new QCheckBox (tr ("Pa&n by rule"), targetBox);
    keepPansBox = new QCheckBox (tr ("&Keep pans I set"), targetBox);
    for (auto* c : { balanceBox, panBox, keepPansBox })
    {
        c->setChecked (true);
        what->addWidget (c);
    }
    what->addStretch();
    form->addRow (tr ("Do:"), what);
    layout->addWidget (targetBox);

    table = new QTreeWidget (this);
    table->setRootIsDecorated (false);
    table->setHeaderLabels ({ tr ("Track"), tr ("Role"), tr ("Fader"), tr ("Pan"), tr ("Yours"), tr ("Suggested"), tr ("Level in the mix") });
    table->header()->setSectionResizeMode (QHeaderView::ResizeToContents);
    table->setMinimumHeight (240);
    for (int t = 0; t < engine.numTracks(); ++t)
    {
        Row r;
        r.track = t;
        r.name = t < trackNames.size() && ! trackNames[t].isEmpty() ? trackNames[t] : tr ("Track %1").arg (t + 1);
        auto* item = new QTreeWidgetItem (table, { r.name });
        r.role = new QComboBox (table);
        for (int k = 0; k < mixassist::numRoles; ++k)
            r.role->addItem (tr (mixassist::roleName ((mixassist::Role) k)), k);
        r.role->setCurrentIndex ((int) mixassist::guessRole (r.name.toStdString()));
        r.moveFader = new QCheckBox (tr ("move"), table);
        r.movePan = new QCheckBox (tr ("move"), table);
        r.moveFader->setChecked (true);
        r.movePan->setChecked (true);
        table->setItemWidget (item, roleColumn, r.role);
        table->setItemWidget (item, faderColumn, r.moveFader);
        table->setItemWidget (item, panColumn, r.movePan);
        rows.push_back (r);
    }
    layout->addWidget (table, 1);

    overlaps = new QLabel (this);
    overlaps->setWordWrap (true);
    overlaps->setTextInteractionFlags (Qt::TextSelectableByMouse);
    eqButton = new QPushButton (tr ("Apply &EQ Cuts"), this);
    eqButton->setToolTip (tr ("Puts each cut below in a Φ EQ in the inserts of the track that covers the other; "
                              "open it from the strip to adjust it, or remove it to undo"));
    eqButton->setEnabled (false);
    auto* overlapRow = new QHBoxLayout;
    overlapRow->addWidget (overlaps, 1);
    overlapRow->addWidget (eqButton, 0, Qt::AlignTop);
    layout->addLayout (overlapRow);

    auto* runRow = new QHBoxLayout;
    suggestButton = new QPushButton (tr ("&Suggest"), this);
    suggestButton->setDefault (true);
    bar = new QProgressBar (this);
    bar->setRange (0, 1000);
    bar->setTextVisible (false);
    runRow->addWidget (suggestButton);
    runRow->addWidget (bar, 1);
    layout->addLayout (runRow);
    status = new QLabel (this);
    status->setWordWrap (true);
    layout->addWidget (status);

    auto* hearRow = new QHBoxLayout;
    amountLabel = new QLabel (tr ("A&mount: 100 %"), this);
    amount = new QSlider (Qt::Horizontal, this);
    amount->setRange (0, 100);
    amount->setValue (100);
    amountLabel->setBuddy (amount);
    abButton = new QPushButton (tr ("Hearing the suggestion (A)"), this);
    abButton->setCheckable (true);
    auto* playButton = new QPushButton (tr ("Play / Stop (Space)"), this);
    hearRow->addWidget (amountLabel);
    hearRow->addWidget (amount, 1);
    hearRow->addWidget (abButton);
    hearRow->addWidget (playButton);
    layout->addLayout (hearRow);

    auto* buttons = new QDialogButtonBox (this);
    keepButton = buttons->addButton (tr ("&Keep"), QDialogButtonBox::AcceptRole);
    buttons->addButton (tr ("&Revert"), QDialogButtonBox::RejectRole);
    layout->addWidget (buttons);

    connect (suggestButton, &QPushButton::clicked, this, [this]
    {
        if (running)
            cancel->store (true);
        else
            suggest();
    });
    connect (eqButton, &QPushButton::clicked, this, [this]
    {
        if (! applyEq)
            return;
        QStringList done, problems;
        for (const auto& [row, list] : cuts)
        {
            const auto error = applyEq (rows[(size_t) row].track, list);
            if (error.isEmpty())
                done << rows[(size_t) row].name;
            else
                problems << error;
        }
        eqButton->setEnabled (false);
        status->setText (problems.isEmpty() ? tr ("A Φ EQ with the cuts is in the inserts of %1.").arg (done.join (", "))
                                            : problems.join (' '));
    });
    connect (amount, &QSlider::valueChanged, this, [this] (int v)
    {
        amountLabel->setText (tr ("A&mount: %1 %").arg (v));
        if (suggested && ! hearingYours)
            apply (v / 100.0);
    });
    connect (abButton, &QPushButton::toggled, this, &MixAssistant::setHearingYours);
    connect (playButton, &QPushButton::clicked, this, [this] { if (playStop) playStop(); });
    connect (new QShortcut (QKeySequence (Qt::Key_Space), this), &QShortcut::activated, this, [this] { if (playStop) playStop(); });
    connect (new QShortcut (QKeySequence (Qt::Key_A), this), &QShortcut::activated, this, [this]
    {
        if (abButton->isEnabled())
            abButton->toggle();
    });
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (&watcher, &QFutureWatcher<Outcome>::finished, this, &MixAssistant::finished);

    auto* poll = new QTimer (this);
    connect (poll, &QTimer::timeout, this, [this] { if (running) bar->setValue ((int) (progress->load() * 1000.0f)); });
    poll->start (100);

    setRunning (false);
    resize (820, 640);
}

MixAssistant::~MixAssistant()
{
    cancel->store (true);
    watcher.waitForFinished();
}

void MixAssistant::setRunning (bool on)
{
    running = on;
    suggestButton->setText (on ? tr ("&Stop") : suggested ? tr ("&Suggest Again") : tr ("&Suggest"));
    for (QWidget* w : std::initializer_list<QWidget*> { loudness, ceiling, reference, balanceBox, panBox, keepPansBox, table, keepButton })
        w->setEnabled (! on);
    abButton->setEnabled (! on && suggested);
    amount->setEnabled (! on && suggested);
    if (on)
        bar->setValue (0);
}

void MixAssistant::suggest()
{
    if (suggested)
    {
        abButton->setChecked (false);
        apply (0.0);
    }

    // Your settings as they are now, and which tracks are heard (muted or soloed-out ones are left alone).
    bool anySolo = false;
    for (const auto& r : rows)
        anySolo = anySolo || engine.strip (r.track).solo.load();
    std::vector<mixassist::Track> tracks;
    std::vector<int> used;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto& r = rows[(size_t) i];
        const auto& s = engine.strip (r.track);
        r.faderDb = r.suggestedDb = s.faderDb.load();
        r.pan = r.suggestedPan = s.pan.load();
        r.heard = ! s.mute.load() && (! anySolo || s.solo.load()) && engine.trackEnd (r.track) > 0;
        auto* item = table->topLevelItem (i);
        item->setText (yoursColumn, setting (r.faderDb, r.pan));
        item->setText (suggestedColumn, r.heard ? QString() : tr ("not heard: left alone"));
        item->setText (levelColumn, QString());
        if (! r.heard)
            continue;

        mixassist::Track t;
        t.name = r.name.toStdString();
        t.role = (mixassist::Role) r.role->currentData().toInt();
        t.faderDb = std::max (-40.0, r.faderDb);
        t.pan = r.pan;
        t.moveFader = r.moveFader->isChecked();
        t.movePan = r.movePan->isChecked();
        const auto clips = engine.clips (r.track);
        t.monoAudio = ! clips.empty() && std::all_of (clips.begin(), clips.end(), [] (const mixer::Clip& c) { return c.audio != nullptr && c.audio->mono; });
        tracks.push_back (t);
        used.push_back (i);
    }
    if (tracks.empty())
    {
        status->setText (tr ("No track is heard: put audio or MIDI on a track, and unmute it."));
        return;
    }

    mixassist::Settings settings;
    settings.mixLufs = loudness->value();
    settings.ceilingDb = ceiling->value();
    settings.balance = balanceBox->isChecked();
    settings.panning = panBox->isChecked();
    settings.keepPans = keepPansBox->isChecked();
    settings.maxEvaluations = std::clamp (6 * (int) tracks.size() + 20, 30, 120);
    AudioDataPtr referenceAudio;
    if (const auto row = reference->currentData().toInt(); row >= 0 && references.load)
    {
        referenceAudio = references.load (row);
        if (referenceAudio == nullptr)
        {
            status->setText (tr ("Could not load that reference."));
            return;
        }
    }

    engine.stop();
    suggested = false;
    cancel->store (false);
    progress->store (0.0f);
    setRunning (true);
    status->setText (tr ("Bouncing each track on its own, then fitting the faders..."));

    auto* m = &engine;
    auto stop = cancel;
    auto prog = progress;
    watcher.setFuture (QtConcurrent::run ([m, stop, prog, tracks, used, settings, referenceAudio]() mutable
    {
        Outcome out;
        out.tracks = used;
        // The bounces take most of the time when there are instruments; the fit is quick.
        for (size_t k = 0; k < tracks.size(); ++k)
        {
            std::atomic<float> one { 0.0f };
            tracks[k].stem = m->mixdown (10.0, out.error, &one, stop.get(), used[k]);
            if (stop->load())
                return out;
            prog->store (0.7f * (float) (k + 1) / (float) tracks.size());
        }
        assist::Profile referenceProfile;
        if (referenceAudio != nullptr)
        {
            referenceProfile = assist::measure (*referenceAudio, 0, referenceAudio->length());
            settings.reference = &referenceProfile;
        }
        out.suggestion = mixassist::suggest (tracks, settings, stop.get(), [prog] (double p) { prog->store (0.7f + 0.3f * (float) p); });
        settings.reference = nullptr;
        prog->store (1.0f);
        return out;
    }));
}

void MixAssistant::finished()
{
    const auto out = watcher.result();
    setRunning (false);
    bar->setValue (1000);
    if (closeWhenDone)
    {
        if (closeAccepting)
            QDialog::accept();
        else
            QDialog::reject();
        return;
    }
    const auto& s = out.suggestion;
    if (s.faderDb.size() != out.tracks.size() || s.evaluations == 0)
    {
        status->setText (out.error.empty() || out.error == "Cancelled." ? tr ("Stopped before a suggestion.") : QString::fromStdString (out.error));
        return;
    }

    for (size_t k = 0; k < out.tracks.size(); ++k)
    {
        auto& r = rows[(size_t) out.tracks[k]];
        r.suggestedDb = r.moveFader->isChecked() ? s.faderDb[k] : r.faderDb;
        r.suggestedPan = s.pan[k];
    }
    suggested = true;
    hearingYours = false;
    abButton->blockSignals (true);
    abButton->setChecked (false);
    abButton->blockSignals (false);
    abButton->setText (tr ("Hearing the suggestion (A)"));
    amount->blockSignals (true);
    amount->setValue (100);
    amount->blockSignals (false);
    amountLabel->setText (tr ("A&mount: %1 %").arg (100));
    apply (1.0);
    setRunning (false);
    showResult (out);
}

void MixAssistant::showResult (const Outcome& out)
{
    const auto& s = out.suggestion;
    for (size_t k = 0; k < out.tracks.size(); ++k)
    {
        const auto& r = rows[(size_t) out.tracks[k]];
        auto* item = table->topLevelItem (out.tracks[k]);
        const auto changed = std::abs (r.suggestedDb - r.faderDb) > 0.05 || std::abs (r.suggestedPan - r.pan) > 0.005;
        item->setText (suggestedColumn, changed ? setting (r.suggestedDb, r.suggestedPan) : tr ("as it is"));
        auto f = item->font (suggestedColumn);
        f.setBold (changed);
        item->setFont (suggestedColumn, f);
        if (k < s.levelBefore.size())
            item->setText (levelColumn, tr ("%1 → %2 dB (aim %3)").arg (s.levelBefore[k], 0, 'f', 1).arg (s.levelAfter[k], 0, 'f', 1)
                                                                   .arg (s.levelAim[k], 0, 'f', 1));
    }

    QStringList lines;
    cuts.clear();
    for (const auto& o : s.overlaps)
    {
        const auto hidden = rows[(size_t) out.tracks[(size_t) o.hidden]].name;
        const auto hiding = rows[(size_t) out.tracks[(size_t) o.hiding]].name;
        const auto hz = assist::Profile::bandCentres()[(size_t) o.band];
        cuts[out.tracks[(size_t) o.hiding]].push_back ({ hz, -std::abs (o.cutDb) });
        const auto where = hz >= 1000.0 ? tr ("%1 kHz").arg (hz / 1000.0) : tr ("%1 Hz").arg (hz);
        lines << tr ("• %1 covers %2 around %3: cut %1 there by about %4 dB").arg (hiding, hidden, where).arg (o.cutDb, 0, 'f', 0);
    }
    eqButton->setEnabled (! cuts.empty() && applyEq != nullptr);
    overlaps->setText (lines.isEmpty() ? QString()
                                       : tr ("Where one track hides another (Apply EQ Cuts puts these in the tracks' inserts):\n")
                                             + lines.join ('\n'));

    QString text = tr ("Mix: %1 LUFS, %2 dBTP before; %3 LUFS, %4 dBTP suggested. Press A to compare with yours, set the amount, "
                       "then Keep or Revert.")
                       .arg (s.before.lufs, 0, 'f', 1).arg (s.before.truePeakDb, 0, 'f', 1)
                       .arg (s.after.lufs, 0, 'f', 1).arg (s.after.truePeakDb, 0, 'f', 1);
    // A fader at its top that still leaves the track short: only its trim can give more.
    QStringList short_;
    for (size_t k = 0; k < out.tracks.size(); ++k)
    {
        const auto& r = rows[(size_t) out.tracks[k]];
        if (r.suggestedDb >= 5.95 && k < s.levelAfter.size() && s.levelAfter[k] < s.levelAim[k] - 1.0)
            short_ << tr ("%1 by %2 dB").arg (r.name).arg (s.levelAim[k] - s.levelAfter[k], 0, 'f', 0);
    }
    if (! short_.isEmpty())
        text += tr (" Some faders are at the top and still short; raise the trim of %1, then Suggest Again.").arg (short_.join (", "));
    if (s.stopped)
        text = tr ("Stopped early; this is the best it found so far. ") + text;
    status->setText (text);
}

void MixAssistant::apply (double a)
{
    for (const auto& r : rows)
    {
        auto& s = engine.strip (r.track);
        // A fader at -inf starts from -40 dB, where the suggestion's range begins.
        const auto from = std::max (-40.0, r.faderDb);
        const auto db = a <= 0.0 ? r.faderDb : from + a * (r.suggestedDb - from);
        s.faderDb = (float) db;
        s.pan = (float) (r.pan + a * (r.suggestedPan - r.pan));
    }
    if (stripsChanged)
        stripsChanged();
}

void MixAssistant::setHearingYours (bool yours)
{
    hearingYours = yours;
    abButton->setText (yours ? tr ("Hearing yours (A)") : tr ("Hearing the suggestion (A)"));
    apply (yours ? 0.0 : amount->value() / 100.0);
}

void MixAssistant::accept()
{
    if (running)
    {
        closeWhenDone = closeAccepting = true;
        cancel->store (true);
        return;
    }
    if (suggested)
        apply (hearingYours ? 0.0 : amount->value() / 100.0);
    QDialog::accept();
}

void MixAssistant::reject()
{
    if (running)
    {
        closeWhenDone = true;
        closeAccepting = false;
        cancel->store (true);
        return;
    }
    if (suggested)
        apply (0.0);
    QDialog::reject();
}
