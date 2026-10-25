// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginAnalyzer.h"
#include "AnalyzerPlot.h"
#include "Brand.h"
#include "MasterPanel.h"
#include "Theme.h"
#include "../engine/PluginProbe.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

//==============================================================================
ProbeRunner::ProbeRunner (AudioEngine& e, QObject* parent) : QObject (parent), engine (e), timer (new QTimer (this))
{
    connect (timer, &QTimer::timeout, this, &ProbeRunner::tick);
    timer->start (300);
    connect (&watcher, &QFutureWatcher<Outcome>::finished, this, [this]
    {
        retired.clear();
        if (cancelled.exchange (false))
        {
            dirty = true;   // stopped for something newer: measure again
            return;
        }
        Q_EMIT finished (watcher.result());
    });
}

ProbeRunner::~ProbeRunner()
{
    cancelled = true;
    watcher.waitForFinished();
}

void ProbeRunner::setProbe (std::shared_ptr<PluginProbe> p)
{
    if (probe != nullptr)
        retired.push_back (probe);
    probe = std::move (p);
    dirty = true;
    error.clear();
    if (busy())
        cancelled = true;
    else
        retired.clear();
}

void ProbeRunner::tick()
{
    if (probe == nullptr || busy())
        return;

    // Copy what changed across (message thread), then measure in the background.
    std::string why;
    const auto changed = probe->sync (why);
    if (why != error && ! why.empty())
        Q_EMIT problem (QString::fromStdString (why));
    error = why;
    if (changed && live)
        dirty = true;
    if (! dirty || probe->empty())
        return;

    dirty = false;
    cancelled = false;
    fraction = 0.0;
    Q_EMIT started();
    watcher.setFuture (QtConcurrent::run ([this, p = probe, s = settings, f = forensics]
    {
        Outcome o;
        o.title = p->title();
        o.results = measure::run (*p, s, [this] (double x)
        {
            fraction = x;
            return ! cancelled.load();
        });
        if (f && ! cancelled.load())
            o.forensics = forensic::analyse (o.results);
        return o;
    }));
}

//==============================================================================
namespace
{
    QColor colour (const char* key) { return Theme::current().color (QString::fromLatin1 (key)); }

    std::vector<double> asDoubles (const std::vector<float>& v)
    {
        return { v.begin(), v.end() };
    }

    QString db (double v) { return v <= -199.0 ? QStringLiteral ("-inf") : QString::number (v, 'f', 1); }
}

PluginAnalyzer::PluginAnalyzer (AudioEngine& e, QWidget* parent)
    : QWidget (parent, Qt::Window), engine (e), runner (new ProbeRunner (e, this))
{
    setWindowTitle (tr ("Plugin Analyzer") + QStringLiteral (" - " APP_BRAND_NAME));
    setAccessibleName (tr ("Plugin Analyzer"));
    resize (980, 700);

    source = new QComboBox (this);
    source->setAccessibleName (tr ("What to measure"));
    auto* loadButton = new QPushButton (tr ("&Load a Plugin..."), this);
    loadButton->setToolTip (tr ("Puts a plugin into a free Master Section slot, bypassed so it is not heard, and measures it"));
    windowButton = new QPushButton (tr ("Plugin &Window"), this);
    windowButton->setToolTip (tr ("Opens the plugin's own window (or its parameters) to turn its knobs while it is measured"));

    level = new QDoubleSpinBox (this);
    level->setRange (-60.0, 6.0);
    level->setValue (-12.0);
    level->setSuffix (tr (" dBFS"));
    level->setAccessibleName (tr ("Test level"));
    level->setToolTip (tr ("How loud the test signals are (their peak). Distortion and compression depend on it"));
    input = new QComboBox (this);
    input->addItem (tr ("Both channels"), (int) measure::Input::both);
    input->addItem (tr ("Left only"), (int) measure::Input::left);
    input->addItem (tr ("Right only"), (int) measure::Input::right);
    input->setAccessibleName (tr ("Input"));
    input->setToolTip (tr ("Which channels the test signals go into; one alone shows the crosstalk into the other"));
    const QStringList names { tr ("&Frequency"), tr ("&Harmonics"), tr ("&Aliasing"), tr ("&Dynamics"), tr ("M&odulation") };
    for (std::size_t i = 0; i < tests.size(); ++i)
    {
        tests[i] = new QCheckBox (names[(int) i], this);
        tests[i]->setChecked (i < 3);
        tests[i]->setToolTip (tr ("Measure this; the fewer tests, the faster Live follows the knobs"));
    }
    liveBox = new QCheckBox (tr ("&Live"), this);
    liveBox->setChecked (true);
    liveBox->setToolTip (tr ("Measure again whenever a setting of the plugin changes"));
    measureButton = new QPushButton (tr ("&Measure"), this);
    measureButton->setToolTip (tr ("Measure now (F5)"));
    auto* copyButton = new QPushButton (tr ("&Copy Report"), this);

    progressBar = new QProgressBar (this);
    progressBar->setRange (0, 1000);
    progressBar->setTextVisible (false);
    progressBar->setMaximumWidth (180);
    status = new QLabel (this);
    status->setWordWrap (true);

    auto plot = [this] (bool log, double x0, double x1, double y0, double y1, const QString& xu, const QString& yu, bool fit = false, double span = 12.0)
    {
        auto* p = new AnalyzerPlot (this);
        p->setAxes (log, x0, x1, y0, y1, xu, yu, fit, span);
        p->setEmptyText (tr ("Choose what to measure; it is measured on a copy, so nothing you hear changes"));
        return p;
    };
    magnitude = plot (true, 10.0, 24000.0, -120.0, 40.0, tr ("Hz"), tr ("dB"), true, 12.0);
    phase = plot (true, 10.0, 24000.0, -180.0, 180.0, tr ("Hz"), tr ("°"));
    impulse = plot (false, -1.0, 10.0, -1.2, 1.2, tr ("ms"), QString(), true, 0.1);
    harmonicSpectrum = plot (true, 10.0, 24000.0, -180.0, 20.0, tr ("Hz"), tr ("dB"), true, 40.0);
    harmonicSweep = plot (true, 20.0, 12000.0, -160.0, 0.0, tr ("Hz (input)"), tr ("dB"), true, 30.0);
    aliasTones = plot (true, 800.0, 22000.0, -180.0, 0.0, tr ("Hz (tone)"), tr ("dB"), true, 30.0);
    aliasSpectrum = plot (true, 10.0, 24000.0, -180.0, 20.0, tr ("Hz"), tr ("dB"), true, 40.0);
    transfer = plot (false, -60.0, 0.0, -80.0, 24.0, tr ("dBFS in"), tr ("dB"), true, 20.0);
    step = plot (false, -100.0, 3500.0, -60.0, 24.0, tr ("ms"), tr ("dB"), true, 6.0);
    envelope = plot (false, 0.0, 12.0, -100.0, 24.0, tr ("s"), tr ("dB"), true, 3.0);
    scan = plot (false, 1.40, 1.90, 0.0, 10.0, tr ("ratio tried"), tr ("hits"), true, 3.0);
    for (auto* l : { &harmonicText, &aliasText, &dynamicsText, &modulationText, &verdict })
    {
        *l = new QLabel (this);
        (*l)->setWordWrap (true);
        (*l)->setTextInteractionFlags (Qt::TextSelectableByMouse);
    }
    auto big = verdict->font();
    big.setPointSizeF (big.pointSizeF() * 1.4);
    big.setBold (true);
    verdict->setFont (big);
    forensicText = new QPlainTextEdit (this);
    forensicText->setReadOnly (true);
    forensicText->setAccessibleName (tr ("Forensic report"));

    phaseView = new QComboBox (this);
    phaseView->addItems ({ tr ("Phase"), tr ("Group delay") });
    impulseView = new QComboBox (this);
    impulseView->addItems ({ tr ("First 10 ms"), tr ("First 50 ms"), tr ("First 200 ms"), tr ("All of it") });
    impulseView->setCurrentIndex (1);

    tabs = new QTabWidget (this);
    auto page = [this] (std::initializer_list<QWidget*> widgets, std::initializer_list<int> stretches)
    {
        auto* w = new QWidget (tabs);
        auto* l = new QVBoxLayout (w);
        l->setContentsMargins (4, 4, 4, 4);
        auto s = stretches.begin();
        for (auto* child : widgets)
            l->addWidget (child, s != stretches.end() ? *s++ : 0);
        return w;
    };
    tabs->addTab (page ({ magnitude, phaseView, phase }, { 3, 0, 2 }), tr ("Frequency"));
    tabs->addTab (page ({ impulseView, impulse }, { 0, 1 }), tr ("Impulse"));
    tabs->addTab (page ({ harmonicText, harmonicSpectrum, harmonicSweep }, { 0, 3, 2 }), tr ("Harmonics"));
    tabs->addTab (page ({ aliasText, aliasTones, aliasSpectrum }, { 0, 2, 3 }), tr ("Aliasing"));
    tabs->addTab (page ({ dynamicsText, transfer, step }, { 0, 3, 2 }), tr ("Dynamics"));
    tabs->addTab (page ({ modulationText, envelope }, { 0, 1 }), tr ("Modulation"));
    tabs->addTab (page ({ verdict, scan, forensicText }, { 0, 2, 3 }), QString::fromUtf8 ("Forensic Φ"));

    auto* top = new QHBoxLayout;
    auto* measureLabel = new QLabel (tr ("Mea&sure:"), this);
    measureLabel->setBuddy (source);
    top->addWidget (measureLabel);
    top->addWidget (source, 1);
    top->addWidget (loadButton);
    top->addWidget (windowButton);
    auto* options = new QHBoxLayout;
    auto* levelLabel = new QLabel (tr ("Le&vel:"), this);
    levelLabel->setBuddy (level);
    options->addWidget (levelLabel);
    options->addWidget (level);
    options->addWidget (input);
    for (auto* t : tests)
        options->addWidget (t);
    options->addStretch (1);
    options->addWidget (liveBox);
    options->addWidget (measureButton);
    options->addWidget (copyButton);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget (status, 1);
    bottom->addWidget (progressBar);
    auto* layout = new QVBoxLayout (this);
    layout->addLayout (top);
    layout->addLayout (options);
    layout->addWidget (tabs, 1);
    layout->addLayout (bottom);

    connect (source, &QComboBox::activated, this, &PluginAnalyzer::sourceChosen);
    connect (loadButton, &QPushButton::clicked, this, &PluginAnalyzer::loadPlugin);
    connect (windowButton, &QPushButton::clicked, this, [this]
    {
        if (slot < 0)
            return;
        if (engine.canShowPluginWindow (slot))
            engine.showPluginWindow (slot);
        else
        {
            auto* dialog = new ParameterDialog (engine, slot, this);
            dialog->setAttribute (Qt::WA_DeleteOnClose);
            dialog->show();
        }
    });
    connect (level, &QDoubleSpinBox::valueChanged, this, &PluginAnalyzer::applySettings);
    connect (input, &QComboBox::currentIndexChanged, this, &PluginAnalyzer::applySettings);
    for (auto* t : tests)
        connect (t, &QCheckBox::toggled, this, &PluginAnalyzer::applySettings);
    connect (liveBox, &QCheckBox::toggled, this, [this] (bool on) { runner->setLive (on); });
    connect (measureButton, &QPushButton::clicked, this, [this] { runner->measureNow(); });
    connect (copyButton, &QPushButton::clicked, this, [this]
    {
        QApplication::clipboard()->setText (reportText());
        status->setText (tr ("The report is on the clipboard"));
    });
    connect (phaseView, &QComboBox::currentIndexChanged, this, &PluginAnalyzer::showView);
    connect (impulseView, &QComboBox::currentIndexChanged, this, &PluginAnalyzer::showView);
    connect (runner, &ProbeRunner::started, this, [this] { status->setText (tr ("Measuring...")); });
    connect (runner, &ProbeRunner::finished, this, &PluginAnalyzer::display);
    connect (runner, &ProbeRunner::problem, this, [this] (const QString& why) { status->setText (tr ("Could not copy: %1").arg (why)); });

    poll = new QTimer (this);
    connect (poll, &QTimer::timeout, this, [this]
    {
        progressBar->setValue (runner->busy() ? (int) (runner->progress() * 1000.0) : 0);
        progressBar->setVisible (runner->busy());
    });
    poll->start (100);
    applySettings();
}

PluginAnalyzer::~PluginAnalyzer() = default;

void PluginAnalyzer::refreshSources()
{
    const auto current = source->currentData().toInt();
    source->clear();
    for (int i = 0; i < AudioEngine::numMasterSlots; ++i)
    {
        const auto info = engine.masterSlot (i);
        if (info.empty)
            continue;
        auto text = tr ("Slot %1: %2").arg (i + 1).arg (QString::fromStdString (info.name));
        if (info.bypassed)
            text += tr (" (bypassed)");
        source->addItem (text, i);
    }
    source->addItem (tr ("The whole Master Section"), -1);
    if (! inserts.empty())
        source->addItem (tr ("Inserts of %1").arg (insertsName), -2);
    const auto index = source->findData (current);
    source->setCurrentIndex (index >= 0 ? index : source->findData (slot));
}

void PluginAnalyzer::analyseSlot (int s)
{
    slot = s;
    refreshSources();
    source->setCurrentIndex (std::max (0, source->findData (s)));
    sourceChosen();
}

void PluginAnalyzer::analyseMasterSection()
{
    analyseSlot (-1);
}

void PluginAnalyzer::analyseInserts (const mixer::Inserts& list, const QString& name)
{
    inserts = list;
    insertsName = name;
    analyseSlot (-2);
}

void PluginAnalyzer::sourceChosen()
{
    slot = source->currentData().toInt();
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    PluginProbe::Source s;
    if (slot >= 0)
    {
        ChainPath path;
        path.slot = slot;
        s = PluginProbe::masterDevice (engine, path);
    }
    else if (slot == -1)
        s = PluginProbe::masterSection (engine);
    else
        s = PluginProbe::inserts (inserts);
    runner->setProbe (std::make_shared<PluginProbe> (engine, std::move (s), rate, 512));
    windowButton->setEnabled (slot >= 0);
    applySettings();
}

void PluginAnalyzer::loadPlugin()
{
    PluginBrowser browser (engine.knownPlugins(), this);
    browser.setWindowTitle (tr ("Choose a Plugin to Measure"));
    if (browser.exec() != QDialog::Accepted || browser.selectedId().empty())
        return;
    int free = -1;
    for (int i = 0; i < AudioEngine::numMasterSlots && free < 0; ++i)
        if (engine.masterSlot (i).empty)
            free = i;
    if (free < 0)
    {
        status->setText (tr ("All %1 Master Section slots are in use; remove a plugin first").arg (AudioEngine::numMasterSlots));
        return;
    }
    const auto error = engine.loadMasterPlugin (free, browser.selectedId());
    if (! error.empty())
    {
        status->setText (QString::fromStdString (error));
        return;
    }
    engine.setMasterSlotBypassed (free, true);
    Q_EMIT chainChanged();
    analyseSlot (free);
    status->setText (tr ("Loaded into slot %1, bypassed so it is not heard; turn its knobs there or with Plugin Window").arg (free + 1));
}

void PluginAnalyzer::applySettings()
{
    measure::Settings s;
    s.sampleRate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    s.levelDb = level->value();
    s.input = (measure::Input) input->currentData().toInt();
    s.linear = tests[0]->isChecked();
    s.harmonics = tests[1]->isChecked();
    s.aliasing = tests[2]->isChecked();
    s.dynamics = tests[3]->isChecked();
    s.modulation = tests[4]->isChecked();
    // The whole Master Section, or a chain, is slower to copy and run: fewer aliasing tones while Live.
    if (slot < 0)
        s.aliasTones = { 1000.0, 5000.0, 10000.0, 15000.0, 20000.0 };
    if (runner->busy())
        runner->cancel();
    runner->setSettings (s);
}

void PluginAnalyzer::display (const ProbeRunner::Outcome& o)
{
    last = std::make_unique<ProbeRunner::Outcome> (o);
    const auto& r = o.results;
    const auto left = colour ("scope"), right = colour ("accent"), warm = colour ("meterHigh"), soft = colour ("levelSoft");
    const auto nyquist = r.sampleRate / 2;

    QString text = QString::fromStdString (o.title) + QStringLiteral (": ");
    text += tr ("latency %1 samples reported").arg (r.reportedLatency);
    if (r.hasLinear)
        text += tr (", %1 measured").arg (r.measuredLatency, 0, 'f', 1);
    for (const auto& n : r.notes)
        text += QStringLiteral (". ") + QString::fromStdString (n);
    status->setText (text);

    if (r.hasLinear)
    {
        magnitude->setAxes (true, 10.0, nyquist, -120.0, 40.0, tr ("Hz"), tr ("dB"), true, 12.0);
        magnitude->setSeries ({ { tr ("Left"), r.hz, r.magnitudeDb[0], left }, { tr ("Right"), r.hz, r.magnitudeDb[1], right, true } });
    }
    else
        magnitude->clear();

    if (r.hasHarmonics)
    {
        harmonicSpectrum->setAxes (true, 10.0, nyquist, -180.0, 20.0, tr ("Hz"), tr ("dB"), true, 40.0);
        harmonicSpectrum->setSeries ({ { tr ("Left"), r.sineSpectrum.hz, r.sineSpectrum.db[0], left },
                                       { tr ("Right"), r.sineSpectrum.hz, r.sineSpectrum.db[1], right, true } });
        std::vector<AnalyzerPlot::Marker> marks;
        for (std::size_t i = 0; i < std::min<std::size_t> (r.otherLines.size(), 6); ++i)
            marks.push_back ({ r.otherLines[i].hz, QString(), soft });
        harmonicSpectrum->setMarkers (marks);
        QString h;
        for (int c = 0; c < 2; ++c)
        {
            h += (c == 0 ? tr ("Left: ") : tr ("    Right: "));
            h += tr ("THD %1 %, THD+N %2 %, gain %3 dB; H2 to H5:").arg (r.thdPercent[(std::size_t) c], 0, 'g', 3)
                     .arg (r.thdnPercent[(std::size_t) c], 0, 'g', 3).arg (r.gainDb[(std::size_t) c], 0, 'f', 2);
            for (std::size_t k = 1; k < std::min<std::size_t> (5, r.harmonicDb[(std::size_t) c].size()); ++k)
                h += QStringLiteral (" ") + db (r.harmonicDb[(std::size_t) c][k]);
        }
        h += tr ("  (a %1 Hz tone)").arg (r.sineSpectrum.toneHz, 0, 'f', 1);
        if (! r.otherLines.empty())
            h += tr (". Also tones that are no harmonic, at %1 Hz").arg (r.otherLines.front().hz, 0, 'f', 1);
        harmonicText->setText (h);
    }
    else
    {
        harmonicSpectrum->clear();
        harmonicText->setText (tr ("Tick Harmonics to measure them"));
    }
    if (r.hasLinear)
    {
        const QColor shades[] = { left, right, warm, soft };
        std::vector<AnalyzerPlot::Series> list;
        for (std::size_t k = 0; k < 4; ++k)
            list.push_back ({ QStringLiteral ("H%1").arg (k + 2), r.hz, r.sweepHarmonicDb[0][k], shades[k] });
        harmonicSweep->setSeries (list);
    }
    else
        harmonicSweep->clear();

    if (r.hasAliasing)
    {
        std::vector<double> tones, worst;
        double top = -200.0, topHz = 0.0;
        for (const auto& p : r.alias)
        {
            tones.push_back (p.toneHz);
            worst.push_back (std::max (p.aliasDb[0], p.aliasDb[1]));
            if (worst.back() > top)
            {
                top = worst.back();
                topHz = p.toneHz;
            }
        }
        aliasTones->setAxes (true, 800.0, nyquist, -180.0, 0.0, tr ("Hz (tone)"), tr ("dB"), true, 30.0);
        aliasTones->setSeries ({ { tr ("Aliasing"), tones, worst, warm, false, true } });
        aliasSpectrum->setAxes (true, 10.0, nyquist, -180.0, 20.0, tr ("Hz"), tr ("dB"), true, 40.0);
        aliasSpectrum->setSeries ({ { tr ("Left"), r.aliasSpectrum.hz, r.aliasSpectrum.db[0], left },
                                    { tr ("Right"), r.aliasSpectrum.hz, r.aliasSpectrum.db[1], right, true } });
        std::vector<AnalyzerPlot::Marker> marks;
        for (const auto& l : r.aliasLines)
            if (l.folded && l.db > -120.0 && marks.size() < 12)
                marks.push_back ({ l.hz, QString(), warm });
        aliasSpectrum->setMarkers (marks);
        aliasText->setText (top < -140.0
            ? tr ("No aliasing to speak of: everything folded back is below -140 dB.")
            : tr ("Aliasing (harmonics folded back from above %1 kHz, against the tone): worst %2 dB, with the %3 Hz tone. "
                  "The spectrum is of the %4 Hz tone; dashed lines mark folded harmonics.")
                  .arg (nyquist / 1000.0, 0, 'f', 1).arg (top, 0, 'f', 1).arg (topHz, 0, 'f', 0).arg (r.aliasSpectrum.toneHz, 0, 'f', 0));
    }
    else
    {
        aliasTones->clear();
        aliasSpectrum->clear();
        aliasText->setText (tr ("Tick Aliasing to measure it"));
    }

    if (r.hasDynamics)
    {
        std::vector<double> unity = r.levelInDb;
        transfer->setSeries ({ { tr ("Left"), r.levelInDb, r.levelOutDb[0], left }, { tr ("Right"), r.levelInDb, r.levelOutDb[1], right, true },
                               { tr ("Unity"), r.levelInDb, unity, soft, true } });
        step->setSeries ({ { tr ("Gain, left"), r.stepMs, r.stepGainDb[0], left }, { tr ("Gain, right"), r.stepMs, r.stepGainDb[1], right, true } });
        auto d = r.ratio > 1.05 ? (r.ratio >= 99.0 ? tr ("Limits above about %1 dBFS").arg (r.thresholdDb, 0, 'f', 0)
                                                  : tr ("Ratio %1:1 above about %2 dBFS").arg (r.ratio, 0, 'f', 2).arg (r.thresholdDb, 0, 'f', 0))
                                : tr ("No compression from %1 to %2 dBFS").arg (r.levelInDb.front()).arg (r.levelInDb.back());
        d += tr (". A step from %1 to %2 dBFS: attack %3 ms, release %4 ms (to 63 %).")
                 .arg (measure::Settings().stepLowDb).arg (measure::Settings().stepHighDb).arg (r.attackMs, 0, 'f', 1).arg (r.releaseMs, 0, 'f', 1);
        dynamicsText->setText (d);
    }
    else
    {
        transfer->clear();
        step->clear();
        dynamicsText->setText (tr ("Tick Dynamics to measure them"));
    }

    if (r.hasModulation)
    {
        std::vector<double> seconds;
        for (auto ms : r.envelopeMs)
            seconds.push_back (ms / 1000.0);
        envelope->setAxes (false, 0.0, seconds.empty() ? 1.0 : seconds.back(), -100.0, 24.0, tr ("s"), tr ("dB"), true, 3.0);
        envelope->setSeries ({ { tr ("Left"), seconds, r.envelopeDb[0], left }, { tr ("Right"), seconds, r.envelopeDb[1], right, true } });
        auto m = tr ("A steady tone's level moves %1 dB and its pitch %2 cents.").arg (r.modulationDepthDb, 0, 'f', 2).arg (r.pitchDepthCents, 0, 'f', 1);
        if (! r.modulationHz.empty())
        {
            m += tr (" Rates:");
            for (auto hz : r.modulationHz)
                m += QStringLiteral (" %1 Hz").arg (hz, 0, 'f', 3);
        }
        if (r.gated)
            m += tr (" Gated in steps of %1 ms: %2 open, %3 closed.").arg (r.gateStepMs, 0, 'f', 1).arg (r.gateOpenSteps).arg (r.gateClosedSteps);
        modulationText->setText (m);
    }
    else
    {
        envelope->clear();
        modulationText->setText (tr ("Tick Modulation to measure it (it takes a while: %1 s of sound)").arg (measure::Settings().modulationSeconds));
    }

    const auto& f = o.forensics;
    verdict->setText (QString::fromUtf8 ("Φ  ") + QString::fromStdString (f.summary));   // the summary names the verdict
    verdict->setAccessibleName (QString::fromStdString (forensic::verdictName (f.verdict)));
    if (! f.scan.empty())
    {
        std::vector<double> bases, hits;
        for (const auto& p : f.scan)
        {
            bases.push_back (p.base);
            hits.push_back (p.hits);
        }
        scan->setSeries ({ { tr ("Hits for each ratio"), bases, hits, left } });
        scan->setMarkers ({ { 1.6180339887, QString::fromUtf8 ("φ"), warm } });
    }
    else
    {
        scan->clear();
        scan->setEmptyText (tr ("The control scan runs when one kind of evidence has two or more golden matches"));
    }
    forensicText->setPlainText (QString::fromStdString (forensic::text (f)));
    showView();
}

void PluginAnalyzer::showView()
{
    if (last == nullptr)
        return;
    const auto& r = last->results;
    const auto left = colour ("scope"), right = colour ("accent");
    if (r.hasLinear)
    {
        if (phaseView->currentIndex() == 0)
        {
            phase->setAxes (true, 10.0, r.sampleRate / 2, -180.0, 180.0, tr ("Hz"), tr ("°"));
            phase->setSeries ({ { tr ("Left"), r.hz, r.phaseDeg[0], left }, { tr ("Right"), r.hz, r.phaseDeg[1], right, true } });
        }
        else
        {
            phase->setAxes (true, 10.0, r.sampleRate / 2, -50.0, 200.0, tr ("Hz"), tr ("ms"), true, 0.5);
            phase->setSeries ({ { tr ("Left"), r.hz, r.groupDelayMs[0], left }, { tr ("Right"), r.hz, r.groupDelayMs[1], right, true } });
        }
        const double spans[] = { 10.0, 50.0, 200.0, 1.0e9 };
        const auto toMs = 1000.0 / r.sampleRate;
        std::vector<double> ms;
        for (std::size_t i = 0; i < r.impulse[0].size(); ++i)
            ms.push_back (((double) i - r.impulsePre) * toMs);
        const auto end = std::min (ms.empty() ? 1.0 : ms.back(), spans[impulseView->currentIndex()]);
        impulse->setAxes (false, -r.impulsePre * toMs, end, -2.0, 2.0, tr ("ms"), QString(), true, 0.1);
        impulse->setSeries ({ { tr ("Left"), ms, asDoubles (r.impulse[0]), left }, { tr ("Right"), ms, asDoubles (r.impulse[1]), right, true } });
        impulse->setMarkers ({ { r.reportedLatency * toMs, tr ("reported latency"), colour ("levelSoft") } });
    }
    else
    {
        phase->clear();
        impulse->clear();
    }
}

QString PluginAnalyzer::reportText() const
{
    if (last == nullptr)
        return {};
    return QString::fromStdString (measure::report (last->results, last->title) + "\n" + forensic::text (last->forensics));
}

void PluginAnalyzer::keyPressEvent (QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape)
        close();
    else if (e->key() == Qt::Key_F5)
        runner->measureNow();
    else
        QWidget::keyPressEvent (e);
}

void PluginAnalyzer::showEvent (QShowEvent* e)
{
    QWidget::showEvent (e);
    refreshSources();
}

void PluginAnalyzer::closeEvent (QCloseEvent* e)
{
    runner->setProbe (nullptr);   // nothing is measured while it is closed
    Q_EMIT closed();
    QWidget::closeEvent (e);
}
