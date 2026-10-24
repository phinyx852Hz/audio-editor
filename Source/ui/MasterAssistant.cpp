// SPDX-License-Identifier: AGPL-3.0-only
#include "MasterAssistant.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
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
    struct Preset
    {
        const char* name;
        double lufs, ceiling;
    };

    // The same loudness targets as the Analysis Meters and Export.
    const Preset presets[] {
        { QT_TRANSLATE_NOOP ("MasterAssistant", "Streaming (Spotify, YouTube, Tidal)"), -14.0, -1.0 },
        { QT_TRANSLATE_NOOP ("MasterAssistant", "Apple Music and podcasts"), -16.0, -1.0 },
        { QT_TRANSLATE_NOOP ("MasterAssistant", "Amazon Music"), -14.0, -2.0 },
        { QT_TRANSLATE_NOOP ("MasterAssistant", "Loud: CD and club"), -9.0, -1.0 },
        { QT_TRANSLATE_NOOP ("MasterAssistant", "Broadcast (EBU R128)"), -23.0, -1.0 },
    };

    QString db (double v, const char* unit) { return v < -100.0 ? QStringLiteral ("-") : QString::number (v, 'f', 1) + " " + unit; }

    // The root of the mean square difference between two tonal balances, dB.
    double toneDifference (const assist::Profile& a, const std::array<double, assist::Profile::numBands>& target)
    {
        double sum = 0.0;
        for (int k = 0; k < assist::Profile::numBands; ++k)
            sum += std::pow (a.bandsDb[(size_t) k] - target[(size_t) k], 2.0);
        return std::sqrt (sum / assist::Profile::numBands);
    }
}

MasterAssistant::MasterAssistant (AudioEngine& e, AudioDataPtr s, Frame start, Frame end, References refs,
                                  std::function<void()> toggle, QWidget* parent)
    : QDialog (parent), engine (e), source (std::move (s)), rangeStart (start), rangeEnd (end),
      references (std::move (refs)), playStop (std::move (toggle))
{
    setWindowTitle (tr ("Master Assistant"));
    auto* layout = new QVBoxLayout (this);

    auto* intro = new QLabel (tr ("Tunes the plugins already in the Master Section towards a target. "
                                  "It only turns the knobs ticked below; it never adds, removes or moves a plugin."), this);
    intro->setWordWrap (true);
    layout->addWidget (intro);

    // What to aim for.
    auto* targetBox = new QGroupBox (tr ("Target"), this);
    auto* form = new QFormLayout (targetBox);
    aim = new QComboBox (targetBox);
    for (int i = 0; i < references.names.size(); ++i)
        aim->addItem (tr ("Reference: %1").arg (references.names[i]), QVariant (i));
    for (int i = 0; i < (int) std::size (presets); ++i)
        aim->addItem (tr (presets[i].name), QVariant (-1 - i));
    if (references.current >= 0 && references.current < references.names.size())
        aim->setCurrentIndex (references.current);
    form->addRow (tr ("&Aim for:"), aim);

    loudness = new QDoubleSpinBox (targetBox);
    loudness->setRange (-40.0, 0.0);
    loudness->setDecimals (1);
    loudness->setSingleStep (0.5);
    loudness->setSuffix (tr (" LUFS"));
    form->addRow (tr ("&Loudness:"), loudness);
    ceiling = new QDoubleSpinBox (targetBox);
    ceiling->setRange (-6.0, 0.0);
    ceiling->setDecimals (1);
    ceiling->setSingleStep (0.1);
    ceiling->setValue (-1.0);
    ceiling->setSuffix (tr (" dBTP"));
    form->addRow (tr ("True peak &ceiling:"), ceiling);
    tone = new QComboBox (targetBox);
    form->addRow (tr ("&Tone:"), tone);

    auto* aspects = new QHBoxLayout;
    matchLoudness = new QCheckBox (tr ("Loudness"), targetBox);
    matchTone = new QCheckBox (tr ("Tone"), targetBox);
    matchDynamics = new QCheckBox (tr ("Dynamics"), targetBox);
    matchWidth = new QCheckBox (tr ("Width"), targetBox);
    for (auto* c : { matchLoudness, matchTone, matchDynamics, matchWidth })
    {
        c->setChecked (true);
        aspects->addWidget (c);
    }
    aspects->addStretch();
    form->addRow (tr ("Match:"), aspects);

    freedom = new QComboBox (targetBox);
    freedom->addItem (tr ("A little"), 0.15);
    freedom->addItem (tr ("Some"), 0.3);
    freedom->addItem (tr ("A lot"), 0.5);
    freedom->setCurrentIndex (1);
    freedom->setToolTip (tr ("How far a knob may move from where you left it"));
    form->addRow (tr ("&Freedom:"), freedom);
    layout->addWidget (targetBox);

    // The knobs it may turn.
    knobList = new QTreeWidget (this);
    knobList->setHeaderLabels ({ tr ("Plugin and parameter"), tr ("Yours"), tr ("Suggested") });
    knobList->header()->setSectionResizeMode (0, QHeaderView::Stretch);
    knobList->setMinimumHeight (220);
    auto* knobLabel = new QLabel (tr ("&Knobs it may turn (ticked):"), this);
    knobLabel->setBuddy (knobList);
    layout->addWidget (knobLabel);
    layout->addWidget (knobList, 1);

    measures = new QTreeWidget (this);
    measures->setHeaderLabels ({ tr ("Measure"), tr ("Yours"), tr ("Suggested"), tr ("Target") });
    measures->setRootIsDecorated (false);
    measures->setMinimumHeight (130);
    measures->header()->setSectionResizeMode (0, QHeaderView::Stretch);
    layout->addWidget (measures);

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
    auto* play = new QPushButton (tr ("Play / Stop (Space)"), this);
    hearRow->addWidget (amountLabel);
    hearRow->addWidget (amount, 1);
    hearRow->addWidget (abButton);
    hearRow->addWidget (play);
    layout->addLayout (hearRow);

    auto* buttons = new QDialogButtonBox (this);
    keepButton = buttons->addButton (tr ("&Keep"), QDialogButtonBox::AcceptRole);
    revertButton = buttons->addButton (tr ("&Revert"), QDialogButtonBox::RejectRole);
    layout->addWidget (buttons);

    connect (aim, &QComboBox::currentIndexChanged, this, &MasterAssistant::targetChanged);
    connect (suggestButton, &QPushButton::clicked, this, [this]
    {
        if (running)
            cancel->store (true);
        else
            suggest();
    });
    connect (amount, &QSlider::valueChanged, this, [this] (int v)
    {
        amountLabel->setText (tr ("A&mount: %1 %").arg (v));
        if (suggested && ! hearingYours)
            apply (v / 100.0);
    });
    connect (abButton, &QPushButton::toggled, this, &MasterAssistant::setHearingYours);
    connect (play, &QPushButton::clicked, this, [this] { if (playStop) playStop(); });
    connect (new QShortcut (QKeySequence (Qt::Key_Space), this), &QShortcut::activated, this, [this] { if (playStop) playStop(); });
    connect (new QShortcut (QKeySequence (Qt::Key_A), this), &QShortcut::activated, this, [this]
    {
        if (abButton->isEnabled())
            abButton->toggle();
    });
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (&watcher, &QFutureWatcher<Outcome>::finished, this, &MasterAssistant::finished);

    auto* poll = new QTimer (this);
    connect (poll, &QTimer::timeout, this, [this] { if (running) bar->setValue ((int) (progress->load() * 1000.0f)); });
    poll->start (100);

    collectKnobs();
    targetChanged();
    setRunning (false);
    resize (720, 760);
}

MasterAssistant::~MasterAssistant()
{
    cancel->store (true);
    watcher.waitForFinished();
}

void MasterAssistant::addKnobs (const ChainPath& path, const MasterSlotInfo& info, QTreeWidgetItem* parent)
{
    if (info.empty || info.failed || info.bypassed)
        return;

    if (info.prism)
    {
        // A Prism's own parameters are its macros, which turn knobs inside it; the knobs themselves are offered.
        const auto prism = engine.prismInfo (path);
        auto* item = parent != nullptr ? new QTreeWidgetItem (parent) : new QTreeWidgetItem (knobList);
        item->setText (0, QString::fromStdString (info.name));
        for (int r = 0; r < (int) prism.rays.size(); ++r)
            if (prism.rays[(size_t) r].heard && ! prism.rays[(size_t) r].muted)
                for (int d = 0; d < (int) prism.rays[(size_t) r].devices.size(); ++d)
                    addKnobs (path.inside (r, d), prism.rays[(size_t) r].devices[(size_t) d], item);
        item->setExpanded (true);
        return;
    }

    auto* device = parent != nullptr ? new QTreeWidgetItem (parent) : new QTreeWidgetItem (knobList);
    device->setText (0, QString::fromStdString (info.name));
    device->setFlags (device->flags() | Qt::ItemIsAutoTristate | Qt::ItemIsUserCheckable);
    const auto params = engine.chainParameters (path);
    for (int i = 0; i < (int) params.size(); ++i)
    {
        const auto& p = params[(size_t) i];
        Knob k;
        k.path = path;
        k.index = i;
        k.device = QString::fromStdString (info.name);
        k.name = QString::fromStdString (p.name);
        k.original = k.suggested = p.value;
        k.originalText = k.suggestedText = QString::fromStdString (p.valueText);
        k.item = new QTreeWidgetItem (device, { k.name, k.originalText, QString() });
        k.item->setFlags (k.item->flags() | Qt::ItemIsUserCheckable);
        k.item->setCheckState (0, assist::movedByDefault (p.name, p.numSteps, p.isBoolean) ? Qt::Checked : Qt::Unchecked);
        k.item->setData (0, Qt::UserRole, (int) knobs.size());
        knobs.push_back (k);
    }
    device->setExpanded (true);
}

void MasterAssistant::collectKnobs()
{
    knobList->clear();
    knobs.clear();
    for (int slot = 0; slot < AudioEngine::numMasterSlots; ++slot)
    {
        const ChainPath path { slot, {} };
        addKnobs (path, engine.chainDevice (path), nullptr);
    }
}

void MasterAssistant::targetChanged()
{
    const auto choice = aim->currentData().toInt();
    const auto isReference = choice >= 0;
    const auto toneWas = tone->currentData().toInt();
    tone->clear();
    if (isReference)
        tone->addItem (tr ("Match the reference"), -1);
    tone->addItem (tr ("Keep mine"), (int) assist::Tone::keep);
    tone->addItem (tr ("Balanced"), (int) assist::Tone::balanced);
    tone->addItem (tr ("Warm"), (int) assist::Tone::warm);
    tone->addItem (tr ("Bright"), (int) assist::Tone::bright);
    if (! isReference && toneWas >= 0)
        tone->setCurrentIndex (std::max (0, tone->findData (toneWas)));

    // A reference sets its own loudness when measured; a preset sets it now.
    loudness->setToolTip (isReference ? tr ("The reference's own loudness is the target") : QString());
    if (! isReference)
    {
        const auto& p = presets[-1 - choice];
        loudness->setValue (p.lufs);
        ceiling->setValue (p.ceiling);
    }
    updateEnabled();
}

void MasterAssistant::updateEnabled()
{
    const auto isReference = aim->currentData().toInt() >= 0;
    for (QWidget* w : std::initializer_list<QWidget*> { aim, ceiling, tone, matchLoudness, matchTone, freedom, knobList, keepButton })
        w->setEnabled (! running);
    loudness->setEnabled (! running && ! isReference);
    matchDynamics->setEnabled (! running && isReference);
    matchWidth->setEnabled (! running && isReference);
    abButton->setEnabled (! running && suggested);
    amount->setEnabled (! running && suggested);
}

bool MasterAssistant::buildTarget (assist::Target& t)
{
    const auto choice = aim->currentData().toInt();
    const auto toneChoice = tone->currentData().toInt();
    if (choice >= 0)
    {
        auto reference = references.load ? references.load (choice) : nullptr;
        if (reference == nullptr)
        {
            status->setText (tr ("Could not load that reference."));
            return false;
        }
        const auto profile = assist::measure (*reference, 0, reference->length());
        if (! profile.valid)
        {
            status->setText (tr ("The reference is silent."));
            return false;
        }
        t = assist::fromReference (profile, ceiling->value());
        loudness->setValue (profile.lufs);
        if (! matchDynamics->isChecked())
            t.dynamicsWeight = 0.0;
        if (! matchWidth->isChecked())
            t.widthWeight = 0.0;
    }
    else
    {
        t = {};
        t.lufs = loudness->value();
        t.truePeakDb = ceiling->value();
    }

    if (toneChoice >= 0)
    {
        t.bandsDb = assist::toneCurve ((assist::Tone) toneChoice);
        t.toneWeight = toneChoice == (int) assist::Tone::keep ? 0.0 : 0.5;
    }
    if (! matchTone->isChecked())
        t.toneWeight = 0.0;
    if (! matchLoudness->isChecked())
        t.loudnessWeight = 0.0;
    return true;
}

void MasterAssistant::setRunning (bool on)
{
    running = on;
    suggestButton->setText (on ? tr ("&Stop") : suggested ? tr ("&Suggest Again") : tr ("&Suggest"));
    updateEnabled();
    if (on)
        bar->setValue (0);
}

void MasterAssistant::suggest()
{
    if (engine.isMasterBypassed())
    {
        status->setText (tr ("The Master Section is bypassed. Turn it on (Ctrl+Shift+B) so the assistant can hear the chain."));
        return;
    }

    // Start from your settings, as they are in the plugins now.
    if (suggested)
    {
        abButton->setChecked (false);
        apply (0.0);
    }
    for (auto& k : knobs)
    {
        const auto p = engine.chainParameter (k.path, k.index);
        k.original = k.suggested = p.value;
        k.originalText = k.suggestedText = QString::fromStdString (p.valueText);
        k.item->setText (1, k.originalText);
        k.item->setText (2, QString());
    }
    assist::Target target;
    if (! buildTarget (target))
        return;

    // Only the ticked knobs whose job is part of the target (an EQ knob when the tone is matched...).
    const auto free = freedom->currentData().toDouble();
    assist::Settings settings;
    moving.clear();
    int ticked = 0;
    for (int i = 0; i < (int) knobs.size(); ++i)
    {
        if (knobs[(size_t) i].item->checkState (0) != Qt::Checked)
            continue;
        ++ticked;
        const auto rule = assist::policy (assist::guessRole (knobs[(size_t) i].name.toStdString()), target, free);
        if (! rule.moves)
            continue;
        moving.push_back (i);
        settings.freedomEach.push_back (rule.freedom);
        settings.stayEach.push_back (rule.stay);
    }
    if (moving.empty())
    {
        status->setText (ticked == 0 ? tr ("Tick at least one knob for the assistant to turn.")
                                     : tr ("None of the ticked knobs does what this target asks for: tick a gain for loudness, "
                                           "an EQ band for tone, a width for width."));
        return;
    }
    leftAlone = ticked - (int) moving.size();

    engine.stop();
    suggested = false;
    cancel->store (false);
    progress->store (0.0f);
    setRunning (true);
    status->setText (tr ("Listening to the chain: each try renders about 30 seconds of the loudest parts through it..."));

    std::vector<double> start;
    std::vector<std::pair<ChainPath, int>> where;
    for (const auto i : moving)
    {
        start.push_back (knobs[(size_t) i].original);
        where.push_back ({ knobs[(size_t) i].path, knobs[(size_t) i].index });
    }
    settings.freedom = free;
    settings.maxEvaluations = std::clamp (3 * (int) start.size() + 12, 25, 150);

    auto audio = source;
    const auto s = rangeStart, e = rangeEnd;
    auto stop = cancel;
    auto prog = progress;
    watcher.setFuture (QtConcurrent::run ([this, audio, s, e, stop, prog, start, where, settings, target]
    {
        Outcome out;
        const auto clip = assist::excerpt (*audio, s, e);
        out.wholeSource = assist::measure (*audio, s, e);
        out.excerptSource = assist::measure (*clip, 0, clip->length());
        out.target = target.forExcerpt (out.wholeSource, out.excerptSource);

        auto render = [&] (const std::vector<double>& values, assist::Profile& profile)
        {
            if (stop->load())
                return false;
            // Plugins are told about their new values on the message thread, as when you turn a knob.
            QMetaObject::invokeMethod (this, [&]
            {
                for (size_t i = 0; i < values.size(); ++i)
                    engine.setChainParameter (where[i].first, where[i].second, (float) values[i]);
            }, Qt::BlockingQueuedConnection);
            const auto rendered = engine.renderThroughMaster (*clip, 0, clip->length(), out.error, nullptr, stop.get(), false);
            if (rendered == nullptr)
                return false;
            profile = assist::measure (*rendered, 0, rendered->length());
            return true;
        };

        bool first = true;
        const assist::Evaluate evaluate = [&] (const std::vector<double>& values, std::vector<double>& residuals)
        {
            assist::Profile profile;
            if (! render (values, profile))
                return false;
            if (first)
                out.before = profile;
            first = false;
            residuals = assist::residuals (profile, out.target);
            return true;
        };
        out.result = assist::optimise (start, evaluate, settings, [prog] (double p) { prog->store ((float) p * 0.95f); });
        // Once more with the best values, to report on them (and leave them in the plugins).
        if (! render (out.result.values, out.after))
            out.after = out.before;
        prog->store (1.0f);
        return out;
    }));
}

void MasterAssistant::finished()
{
    const auto out = watcher.result();
    setRunning (false);
    bar->setValue (1000);

    if (closeWhenDone)
    {
        // Closed while it ran: what the plugins hold now is a try, not the suggestion.
        apply (0.0);
        if (closeAccepting)
            QDialog::accept();
        else
            QDialog::reject();
        return;
    }

    if (out.result.evaluations == 0 || out.result.values.size() != moving.size())
    {
        apply (0.0);
        status->setText (out.error.empty() ? tr ("Stopped before the first render.") : QString::fromStdString (out.error));
        return;
    }

    for (size_t i = 0; i < moving.size(); ++i)
        knobs[(size_t) moving[i]].suggested = out.result.values[i];
    suggested = true;
    // The plugins hold the best values now (the last render used them); read back how they read.
    for (auto& k : knobs)
    {
        k.suggestedText = QString::fromStdString (engine.chainParameter (k.path, k.index).valueText);
        k.item->setText (2, std::abs (k.suggested - k.original) > 1.0e-4 ? k.suggestedText : QString());
        auto f = k.item->font (2);
        f.setBold (std::abs (k.suggested - k.original) > 1.0e-4);
        k.item->setFont (2, f);
    }
    amount->blockSignals (true);
    amount->setValue (100);
    amount->blockSignals (false);
    amountLabel->setText (tr ("A&mount: %1 %").arg (100));
    hearingYours = false;
    abButton->blockSignals (true);
    abButton->setChecked (false);
    abButton->blockSignals (false);
    abButton->setText (tr ("Hearing the suggestion (A)"));
    setRunning (false);   // again, now that there is a suggestion to hear
    showMeasures (out);

    const auto changed = std::count_if (knobs.begin(), knobs.end(), [] (const Knob& k) { return std::abs (k.suggested - k.original) > 1.0e-4; });
    const auto better = out.result.startError > 0.0 ? 100.0 * (1.0 - out.result.error / out.result.startError) : 0.0;
    QString text = changed == 0 ? tr ("Your settings are already as close to the target as these knobs can get.")
                                : tr ("%n knob(s) changed, %1 % closer to the target, after %2 renders. "
                                      "Press A to compare with yours, set the amount, then Keep or Revert.", nullptr, (int) changed)
                                      .arg (std::max (0.0, better), 0, 'f', 0).arg (out.result.evaluations);
    if (leftAlone > 0)
        text += tr (" %n ticked knob(s) left alone, since what they do is not part of this target.", nullptr, leftAlone);
    if (out.result.stopped)
        text = tr ("Stopped early; this is the best it found so far. ") + text;
    status->setText (text);
}

void MasterAssistant::showMeasures (const Outcome& out)
{
    measures->clear();
    // Measured on the excerpt, shown as for the whole song (the excerpt's difference added back).
    const auto& w = out.wholeSource;
    const auto& p = out.excerptSource;
    auto row = [this] (const QString& name, const QString& yours, const QString& suggestion, const QString& target)
    {
        new QTreeWidgetItem (measures, { name, yours, suggestion, target });
    };
    const auto shiftLufs = w.lufs - p.lufs, shiftPlr = w.plr - p.plr;
    row (tr ("Loudness"), db (out.before.lufs + shiftLufs, "LUFS"), db (out.after.lufs + shiftLufs, "LUFS"),
         out.target.loudnessWeight > 0.0 ? db (out.target.lufs + shiftLufs, "LUFS") : tr ("(not matched)"));
    row (tr ("True peak (loud parts)"), db (out.before.truePeakDb, "dBTP"), db (out.after.truePeakDb, "dBTP"),
         tr ("at most %1").arg (db (out.target.truePeakDb, "dBTP")));
    row (tr ("Peak to loudness"), db (out.before.plr + shiftPlr, "dB"), db (out.after.plr + shiftPlr, "dB"),
         out.target.dynamicsWeight > 0.0 ? db (out.target.plr + shiftPlr, "dB") : tr ("(not matched)"));
    if (out.before.sideToMidDb > -100.0)
        row (tr ("Side against mid"), db (out.before.sideToMidDb, "dB"), db (out.after.sideToMidDb, "dB"),
             out.target.widthWeight > 0.0 ? db (out.target.sideToMidDb, "dB") : tr ("(not matched)"));
    if (out.target.toneWeight > 0.0)
        row (tr ("Tone, away from the target"), db (toneDifference (out.before, out.target.bandsDb), "dB"),
             db (toneDifference (out.after, out.target.bandsDb), "dB"), QStringLiteral ("0.0 dB"));
}

void MasterAssistant::apply (double a)
{
    for (const auto& k : knobs)
        if (std::abs (k.suggested - k.original) > 1.0e-6 || a <= 0.0)
            engine.setChainParameter (k.path, k.index, (float) (k.original + a * (k.suggested - k.original)));
}

void MasterAssistant::setHearingYours (bool yours)
{
    hearingYours = yours;
    abButton->setText (yours ? tr ("Hearing yours (A)") : tr ("Hearing the suggestion (A)"));
    apply (yours ? 0.0 : amount->value() / 100.0);
}

void MasterAssistant::accept()
{
    if (running)
    {
        closeWhenDone = closeAccepting = true;
        cancel->store (true);
        return;
    }
    // What you hear is what stays: your settings when A is on yours, else the blend.
    if (suggested)
        apply (hearingYours ? 0.0 : amount->value() / 100.0);
    engine.saveMasterSection();
    QDialog::accept();
}

void MasterAssistant::reject()
{
    if (running)
    {
        closeWhenDone = true;
        closeAccepting = false;
        cancel->store (true);
        return;
    }
    apply (0.0);
    QDialog::reject();
}
