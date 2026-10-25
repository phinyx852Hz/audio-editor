// SPDX-License-Identifier: AGPL-3.0-only
#include "QuantizePanel.h"

#include "Settings.h"
#include "Theme.h"
#include "../core/TimeStretch.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace
{
    // Note values in beats (a quarter note is one beat).
    const std::pair<const char*, double> divisions[] { { "1/1", 4.0 }, { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 },
                                                       { "1/16", 0.25 }, { "1/32", 0.125 }, { "1/64", 0.0625 } };
    const std::pair<const char*, double> tuplets[] { { QT_TRANSLATE_NOOP ("QuantizePanel", "Straight"), 1.0 },
                                                     { QT_TRANSLATE_NOOP ("QuantizePanel", "Triplet"), 2.0 / 3.0 },
                                                     { QT_TRANSLATE_NOOP ("QuantizePanel", "Dotted"), 1.5 } };
    const std::pair<const char*, double> filters[] { { QT_TRANSLATE_NOOP ("QuantizePanel", "All hits"), 0.0 }, { "1/4", 1.0 },
                                                     { "1/8", 0.5 }, { "1/16", 0.25 } };

    QString key (const char* name) { return QStringLiteral ("quantize/") + QLatin1String (name); }
}

//==============================================================================
QuantizeGridView::QuantizeGridView (QWidget* parent) : QWidget (parent)
{
    setToolTip (tr ("One bar of the grid: hits are pulled to the lines; the shading is the catch range, "
                    "the darker middle the non-quantize zone. Ticks above are the hits of the range."));
}

void QuantizeGridView::setGrid (int perBar, const audioquantize::Settings& s)
{
    beatsPerBar = std::max (1, perBar);
    settings = s;
    update();
}

void QuantizeGridView::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto& theme = Theme::current();
    const QRectF area = QRectF (rect()).adjusted (1, 1, -1, -1);
    g.fillRect (area, Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
    const auto bar = (double) beatsPerBar;
    const auto xFor = [&] (double beat) { return area.left() + beat / bar * area.width(); };
    const auto division = settings.groove.isSet() ? settings.groove.division : settings.division;
    const auto accent = theme.color (QStringLiteral ("accent"));
    const auto steps = division > 0.0 ? (int) std::ceil (bar / division - 1e-9) : 0;
    const auto top = area.top() + 12.0;
    if (steps > 0 && steps <= 256)
        for (int k = 0; k < steps; ++k)
        {
            const auto line = audioquantize::lineFor (k * division, settings);
            auto catchShade = accent;
            catchShade.setAlpha (settings.reach >= 1.0 ? 0 : 36);
            const auto reach = std::clamp (settings.reach, 0.0, 1.0) * division / 2.0;
            g.fillRect (QRectF (QPointF (xFor (line - reach), top), QPointF (xFor (line + reach), area.bottom())), catchShade);
            if (settings.leaveWithin > 0.0)
            {
                auto safe = theme.color (QStringLiteral ("windowText"));
                safe.setAlpha (50);
                const auto zone = settings.leaveWithin * division / 2.0;
                g.fillRect (QRectF (QPointF (xFor (line - zone), top), QPointF (xFor (line + zone), area.bottom())), safe);
            }
            const auto onBeat = std::abs (k * division - std::round (k * division)) < 1e-9;
            g.setPen (QPen (accent, onBeat ? 2.0 : 1.0));
            g.drawLine (QPointF (xFor (line), top + (onBeat ? 0.0 : 8.0)), QPointF (xFor (line), area.bottom()));
        }
    // Beat numbers.
    g.setPen (theme.color (QStringLiteral ("windowText")));
    auto font = g.font();
    font.setPointSizeF (std::max (6.0, font.pointSizeF() * 0.8));
    g.setFont (font);
    for (int b = 0; b < beatsPerBar; ++b)
        g.drawText (QPointF (xFor (b) + 3.0, area.top() + 10.0), QString::number (b + 1));
    // The hits, folded into one bar.
    auto tick = theme.color (QStringLiteral ("windowText"));
    tick.setAlpha (200);
    g.setPen (QPen (tick, 1.5));
    for (const auto h : hits)
        g.drawLine (QPointF (xFor (h), top - 2.0), QPointF (xFor (h), top + 9.0));
    g.setPen (theme.color (QStringLiteral ("mid")));
    g.drawRect (area);
}

//==============================================================================
QuantizePanel::QuantizePanel (QWidget* parent) : QWidget (parent)
{
    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (6, 6, 6, 6);

    edit = new QPushButton (tr ("&Edit Warp Markers"), this);
    edit->setCheckable (true);
    edit->setToolTip (tr ("Show hits as ticks and warp markers as flags along the top of the waveform. Drag a tick or flag to "
                          "stretch the audio either side of it; Shift+drag a flag to move it without moving the audio; "
                          "double-click to add or remove a flag. Ctrl+Alt+drag the end of a selection to stretch the selection."));
    layout->addWidget (edit);

    auto slider = [this] (int maximum, const QString& tip)
    {
        auto* s = new QSlider (Qt::Horizontal, this);
        s->setRange (0, maximum);
        s->setToolTip (tip);
        return s;
    };
    // A slider with its value written beside it.
    auto row = [this] (QGridLayout* grid, int r, const QString& text, QSlider* s, const QString& suffix)
    {
        auto* label = new QLabel (text, this);
        label->setBuddy (s);
        auto* value = new QLabel (this);
        value->setMinimumWidth (fontMetrics().horizontalAdvance (QStringLiteral ("100 %")) + 4);
        value->setAlignment (Qt::AlignRight | Qt::AlignVCenter);
        auto show = [value, suffix] (int v) { value->setText (QString::number (v) + suffix); };
        connect (s, &QSlider::valueChanged, value, show);
        show (s->value());
        grid->addWidget (label, r, 0);
        grid->addWidget (s, r, 1);
        grid->addWidget (value, r, 2);
    };

    // Hitpoints.
    auto* hitBox = new QGroupBox (tr ("Hitpoints"), this);
    auto* hitGrid = new QGridLayout (hitBox);
    method = new QComboBox (hitBox);
    for (const auto m : { audioquantize::Method::level, audioquantize::Method::spectral, audioquantize::Method::combined })
        method->addItem (tr (audioquantize::methodName (m)), (int) m);
    method->setToolTip (tr ("Level finds sharp rises in loudness (drums, plucks). Spectral also finds notes that change "
                            "without getting louder (legato bass, vocals, chords). Combined keeps both."));
    auto* methodLabel = new QLabel (tr ("&Detect:"), hitBox);
    methodLabel->setBuddy (method);
    hitGrid->addWidget (methodLabel, 0, 0);
    hitGrid->addWidget (method, 0, 1, 1, 2);
    threshold = slider (100, tr ("Higher finds quieter hits; lower keeps only the strongest"));
    row (hitGrid, 1, tr ("&Threshold:"), threshold, QStringLiteral (" %"));
    gap = new QSpinBox (hitBox);
    gap->setRange (5, 1000);
    gap->setSuffix (tr (" ms"));
    gap->setToolTip (tr ("Hits closer than this to the one before are ignored (flams, rolls and buzzing decays)"));
    auto* gapLabel = new QLabel (tr ("Min &gap:"), hitBox);
    gapLabel->setBuddy (gap);
    hitGrid->addWidget (gapLabel, 2, 0);
    hitGrid->addWidget (gap, 2, 1, 1, 2);
    filter = new QComboBox (hitBox);
    for (const auto& [name, value] : filters)
        filter->addItem (tr (name), value);
    filter->setToolTip (tr ("Keep only hits near these grid lines, as Cubase's Beats filter: 1/4 keeps the main beats of a busy part"));
    auto* filterLabel = new QLabel (tr ("&Keep:"), hitBox);
    filterLabel->setBuddy (filter);
    hitGrid->addWidget (filterLabel, 3, 0);
    hitGrid->addWidget (filter, 3, 1, 1, 2);
    hitCount = new QLabel (hitBox);
    hitGrid->addWidget (hitCount, 4, 0, 1, 3);
    auto* hitButtons = new QGridLayout;
    auto* toMarkers = new QPushButton (tr ("Create &Warp Markers"), hitBox);
    toMarkers->setToolTip (tr ("A warp marker on every hit in the range, where it is (nothing moves yet)"));
    auto* slice = new QPushButton (tr ("Create &Slices"), hitBox);
    slice->setToolTip (tr ("A slice marker at every hit in the range, for Export Slices, the Slicer and moving hits by hand"));
    auto* grooveFrom = new QPushButton (tr ("Create &Groove"), hitBox);
    grooveFrom->setToolTip (tr ("The timing of the range's hits as a groove template (one bar of the grid), to quantize other audio to"));
    hitButtons->addWidget (toMarkers, 0, 0);
    hitButtons->addWidget (slice, 0, 1);
    hitButtons->addWidget (grooveFrom, 1, 0);
    hitGrid->addLayout (hitButtons, 5, 0, 1, 3);
    hitGrid->setColumnStretch (1, 1);
    layout->addWidget (hitBox);

    // Quantize.
    auto* qBox = new QGroupBox (tr ("Quantize"), this);
    auto* qGrid = new QGridLayout (qBox);
    division = new QComboBox (qBox);
    for (const auto& [name, value] : divisions)
        division->addItem (QString::fromLatin1 (name), value);
    tuplet = new QComboBox (qBox);
    for (const auto& [name, value] : tuplets)
        tuplet->addItem (tr (name), value);
    auto* gridLabel = new QLabel (tr ("G&rid:"), qBox);
    gridLabel->setBuddy (division);
    auto* gridRow = new QHBoxLayout;
    gridRow->addWidget (division, 1);
    gridRow->addWidget (tuplet, 1);
    qGrid->addWidget (gridLabel, 0, 0);
    qGrid->addLayout (gridRow, 0, 1, 1, 2);
    groove = new QComboBox (qBox);
    groove->setToolTip (tr ("A groove replaces the grid and swing: hits are pulled to its lines instead"));
    auto* grooveLabel = new QLabel (tr ("Gr&oove:"), qBox);
    grooveLabel->setBuddy (groove);
    qGrid->addWidget (grooveLabel, 1, 0);
    qGrid->addWidget (groove, 1, 1, 1, 2);
    gridView = new QuantizeGridView (qBox);
    gridView->setMinimumHeight (48);
    qGrid->addWidget (gridView, 2, 0, 1, 3);
    strength = slider (100, tr ("How far each hit moves towards its line: 100 % puts it on the line; 50 to 80 % tightens and keeps the feel (Ableton's Amount)"));
    row (qGrid, 3, tr ("S&trength:"), strength, QStringLiteral (" %"));
    swing = slider (100, tr ("Every second step later, up to a third of a step at 100 % (a triplet feel)"));
    row (qGrid, 4, tr ("S&wing:"), swing, QStringLiteral (" %"));
    catchRange = slider (100, tr ("Catch range: only hits this close to a line move, as a share of the step (100 % catches every hit)"));
    row (qGrid, 5, tr ("&Catch range:"), catchRange, QStringLiteral (" %"));
    nonQ = slider (100, tr ("Non-quantize zone: hits already this close to their line stay as played, as a share of the step"));
    row (qGrid, 6, tr ("&Non-Q zone:"), nonQ, QStringLiteral (" %"));
    randomMs = new QDoubleSpinBox (qBox);
    randomMs->setRange (0.0, 50.0);
    randomMs->setDecimals (1);
    randomMs->setSingleStep (0.5);
    randomMs->setSuffix (tr (" ms"));
    randomMs->setToolTip (tr ("Each moved hit lands up to this far either side of its line, for a human feel"));
    reseed = new QPushButton (tr ("New"), qBox);
    reseed->setToolTip (tr ("Different random offsets"));
    auto* randomLabel = new QLabel (tr ("R&andomize:"), qBox);
    randomLabel->setBuddy (randomMs);
    auto* randomRow = new QHBoxLayout;
    randomRow->addWidget (randomMs, 1);
    randomRow->addWidget (reseed);
    qGrid->addWidget (randomLabel, 7, 0);
    qGrid->addLayout (randomRow, 7, 1, 1, 2);
    iterate = new QCheckBox (tr ("&Iterative"), qBox);
    iterate->setToolTip (tr ("Quantize from where the hits are now, so each press moves them further by the strength (Cubase's iterative quantize)"));
    autoBox = new QCheckBox (tr ("A&uto apply"), qBox);
    autoBox->setToolTip (tr ("Quantize again as you change the settings, replacing the last quantize"));
    auto* checks = new QHBoxLayout;
    checks->addWidget (iterate);
    checks->addWidget (autoBox);
    checks->addStretch (1);
    qGrid->addLayout (checks, 8, 0, 1, 3);
    rangeLabel = new QLabel (qBox);
    rangeLabel->setWordWrap (true);
    qGrid->addWidget (rangeLabel, 9, 0, 1, 3);
    auto* qButtons = new QHBoxLayout;
    auto* reset = new QPushButton (tr ("Rese&t"), qBox);
    reset->setToolTip (tr ("Remove the warp markers in the range: the timing goes back to how it was recorded"));
    quantize = new QPushButton (tr ("&Quantize"), qBox);
    quantize->setToolTip (tr ("Move the hits in the range towards the grid by stretching the audio between them (Q)"));
    quantize->setDefault (true);
    qButtons->addWidget (reset);
    qButtons->addStretch (1);
    qButtons->addWidget (quantize);
    qGrid->addLayout (qButtons, 10, 0, 1, 3);
    qGrid->setColumnStretch (1, 1);
    layout->addWidget (qBox);

    // Warp algorithm and stretching.
    auto* wBox = new QGroupBox (tr ("Warp"), this);
    auto* wGrid = new QGridLayout (wBox);
    algorithm = new QComboBox (wBox);
    for (const auto m : timestretch::modes())
        algorithm->addItem (tr (timestretch::modeName (m)), (int) m);
    auto* algorithmLabel = new QLabel (tr ("A&lgorithm:"), wBox);
    algorithmLabel->setBuddy (algorithm);
    wGrid->addWidget (algorithmLabel, 0, 0);
    wGrid->addWidget (algorithm, 0, 1, 1, 3);
    algorithmHelp = new QLabel (wBox);
    algorithmHelp->setWordWrap (true);
    algorithmHelp->setForegroundRole (QPalette::PlaceholderText);
    wGrid->addWidget (algorithmHelp, 1, 0, 1, 4);
    snap = new QCheckBox (tr ("Dragged markers snap to the &grid"), wBox);
    snap->setToolTip (tr ("Dragged ticks and flags land on the quantize grid; hold Alt while dragging to place them freely"));
    wGrid->addWidget (snap, 2, 0, 1, 4);
    auto* shrink = new QPushButton (tr ("÷2"), wBox);
    shrink->setToolTip (tr ("The selection (or the whole file) half as long, at double speed"));
    auto* longer = new QPushButton (tr ("×2"), wBox);
    longer->setToolTip (tr ("The selection (or the whole file) twice as long, at half speed"));
    auto* toTempo = new QPushButton (tr ("Stretch to Tem&po..."), wBox);
    wGrid->addWidget (shrink, 3, 0);
    wGrid->addWidget (longer, 3, 1);
    wGrid->addWidget (toTempo, 3, 2, 1, 2);
    tempo = new QLabel (wBox);
    auto* detect = new QPushButton (tr ("Detect"), wBox);
    detect->setToolTip (tr ("Find the tempo and bar 1 (Warp > Detect Beat Grid)"));
    auto* setTempo = new QPushButton (tr ("Set..."), wBox);
    setTempo->setToolTip (tr ("Type the tempo, beats per bar and where bar 1 starts"));
    wGrid->addWidget (tempo, 4, 0, 1, 2);
    wGrid->addWidget (detect, 4, 2);
    wGrid->addWidget (setTempo, 4, 3);
    auto* extreme = new QPushButton (tr ("E&xtreme Stretch (Paulstretch)..."), wBox);
    extreme->setToolTip (tr ("Stretch the selection or the whole file 2 to 10000 times into a smooth, evolving wash, "
                             "with PaulXStretch-style harmonics, ratio mix, spread, filter and more (Ctrl+Shift+X; X repeats it)"));
    wGrid->addWidget (extreme, 5, 0, 1, 4);
    wGrid->setColumnStretch (1, 1);
    layout->addWidget (wBox);

    status = new QLabel (this);
    status->setWordWrap (true);
    layout->addWidget (status);
    layout->addStretch (1);

    load();
    fillGrooves();
    updateEnabled();
    showGrid();

    auto changed = [this]
    {
        if (updating)
            return;
        save();
        updateEnabled();
        showGrid();
        Q_EMIT settingsChanged();
    };
    auto redetect = [this]
    {
        if (updating)
            return;
        save();
        Q_EMIT detectionChanged();
    };
    connect (edit, &QPushButton::toggled, this, [this] (bool on) { if (! updating) Q_EMIT editToggled (on); });
    connect (method, &QComboBox::currentIndexChanged, this, redetect);
    connect (threshold, &QSlider::valueChanged, this, redetect);
    connect (gap, &QSpinBox::valueChanged, this, redetect);
    connect (filter, &QComboBox::currentIndexChanged, this, redetect);
    connect (toMarkers, &QPushButton::clicked, this, &QuantizePanel::markersFromHitsRequested);
    connect (slice, &QPushButton::clicked, this, &QuantizePanel::sliceRequested);
    connect (grooveFrom, &QPushButton::clicked, this, &QuantizePanel::grooveFromHitsRequested);
    for (auto* box : { division, tuplet, groove })
        connect (box, &QComboBox::currentIndexChanged, this, changed);
    for (auto* s : { strength, swing, catchRange, nonQ })
        connect (s, &QSlider::valueChanged, this, changed);
    connect (randomMs, &QDoubleSpinBox::valueChanged, this, changed);
    connect (reseed, &QPushButton::clicked, this, [this, changed]
    {
        ++seed;
        changed();
    });
    connect (iterate, &QCheckBox::toggled, this, [this] { if (! updating) save(); });
    connect (autoBox, &QCheckBox::toggled, this, [this] { if (! updating) save(); });
    connect (reset, &QPushButton::clicked, this, &QuantizePanel::resetRequested);
    connect (quantize, &QPushButton::clicked, this, &QuantizePanel::quantizeRequested);
    connect (algorithm, &QComboBox::currentIndexChanged, this, [this]
    {
        algorithmHelp->setText (tr (timestretch::modeDescription ((timestretch::Mode) mode())));
        if (updating)
            return;
        save();
        Q_EMIT modeChanged (mode());
    });
    algorithmHelp->setText (tr (timestretch::modeDescription ((timestretch::Mode) mode())));
    connect (snap, &QCheckBox::toggled, this, [this] { if (! updating) { save(); Q_EMIT snapChanged(); } });
    connect (shrink, &QPushButton::clicked, this, [this] { Q_EMIT stretchRequested (2.0); });
    connect (longer, &QPushButton::clicked, this, [this] { Q_EMIT stretchRequested (0.5); });
    connect (toTempo, &QPushButton::clicked, this, &QuantizePanel::stretchToTempoRequested);
    connect (extreme, &QPushButton::clicked, this, &QuantizePanel::extremeStretchRequested);
    connect (detect, &QPushButton::clicked, this, &QuantizePanel::detectTempoRequested);
    connect (setTempo, &QPushButton::clicked, this, &QuantizePanel::setTempoRequested);
}

void QuantizePanel::load()
{
    updating = true;
    auto s = appSettings();
    method->setCurrentIndex (std::max (0, method->findData (s.value (key ("method"), 0).toInt())));
    threshold->setValue ((int) (s.value (QStringLiteral ("warp/sensitivity"), 0.5).toDouble() * 100.0 + 0.5));
    gap->setValue (s.value (key ("gapMs"), 50).toInt());
    filter->setCurrentIndex (std::clamp (s.value (key ("filter"), 0).toInt(), 0, filter->count() - 1));
    division->setCurrentIndex (std::clamp (s.value (key ("division"), 4).toInt(), 0, division->count() - 1));
    tuplet->setCurrentIndex (std::clamp (s.value (key ("tuplet"), 0).toInt(), 0, tuplet->count() - 1));
    strength->setValue (s.value (QStringLiteral ("warp/quantizeStrength"), 100).toInt());
    swing->setValue (s.value (QStringLiteral ("warp/quantizeSwing"), 0).toInt());
    catchRange->setValue (s.value (QStringLiteral ("warp/quantizeReach"), 100).toInt());
    nonQ->setValue (s.value (QStringLiteral ("warp/quantizeLeaveWithin"), 0).toInt());
    randomMs->setValue (s.value (key ("randomMs"), 0.0).toDouble());
    seed = s.value (key ("seed"), 1u).toUInt();
    iterate->setChecked (s.value (key ("iterative"), false).toBool());
    autoBox->setChecked (s.value (key ("autoApply"), false).toBool());
    algorithm->setCurrentIndex (std::max (0, algorithm->findData (s.value (QStringLiteral ("warp/mode"), (int) timestretch::Mode::fullMix).toInt())));
    snap->setChecked (s.value (key ("snapDrag"), true).toBool());
    updating = false;
}

void QuantizePanel::save() const
{
    auto s = appSettings();
    s.setValue (key ("method"), method->currentData().toInt());
    s.setValue (QStringLiteral ("warp/sensitivity"), threshold->value() / 100.0);
    s.setValue (key ("gapMs"), gap->value());
    s.setValue (key ("filter"), filter->currentIndex());
    s.setValue (key ("division"), division->currentIndex());
    s.setValue (key ("tuplet"), tuplet->currentIndex());
    s.setValue (QStringLiteral ("warp/quantizeStrength"), strength->value());
    s.setValue (QStringLiteral ("warp/quantizeSwing"), swing->value());
    s.setValue (QStringLiteral ("warp/quantizeReach"), catchRange->value());
    s.setValue (QStringLiteral ("warp/quantizeLeaveWithin"), nonQ->value());
    s.setValue (key ("randomMs"), randomMs->value());
    s.setValue (key ("seed"), seed);
    s.setValue (key ("iterative"), iterate->isChecked());
    s.setValue (key ("autoApply"), autoBox->isChecked());
    s.setValue (key ("groove"), groove->currentData().toString());
    s.setValue (QStringLiteral ("warp/mode"), mode());
    s.setValue (key ("snapDrag"), snap->isChecked());
}

void QuantizePanel::fillGrooves()
{
    const bool was = updating;
    updating = true;
    const auto chosen = appSettings().value (key ("groove")).toString();
    groove->clear();
    groove->addItem (tr ("None (grid and swing)"), QString());
    for (const auto& g : audioquantize::builtInGrooves())
        groove->addItem (QString::fromStdString (g.name), QString::fromStdString (audioquantize::toText (g)));
    const auto saved = appSettings().value (key ("grooves")).toStringList();
    if (! saved.isEmpty())
        groove->insertSeparator (groove->count());
    for (const auto& text : saved)
        if (const auto g = audioquantize::fromText (text.toStdString()); g.isSet())
            groove->addItem (QString::fromStdString (g.name), text);
    groove->setCurrentIndex (std::max (0, groove->findData (chosen)));
    updating = was;
}

void QuantizePanel::addGroove (const audioquantize::Groove& g)
{
    const auto text = QString::fromStdString (audioquantize::toText (g));
    auto s = appSettings();
    auto saved = s.value (key ("grooves")).toStringList();
    saved.removeAll (text);
    saved.append (text);
    while (saved.size() > 32)
        saved.removeFirst();
    s.setValue (key ("grooves"), saved);
    s.setValue (key ("groove"), text);
    fillGrooves();
    updateEnabled();
    showGrid();
}

void QuantizePanel::updateEnabled()
{
    const bool grooved = ! groove->currentData().toString().isEmpty();
    division->setEnabled (! grooved);
    tuplet->setEnabled (! grooved);
    swing->setEnabled (! grooved);
    reseed->setEnabled (randomMs->value() > 0.0);
    const bool ready = state.open && state.grid.isSet();
    quantize->setEnabled (ready);
}

void QuantizePanel::showGrid()
{
    gridView->setGrid (state.grid.isSet() ? state.grid.beatsPerBar : 4, settings());
}

audioquantize::Detection QuantizePanel::detection() const
{
    audioquantize::Detection d;
    d.method = (audioquantize::Method) method->currentData().toInt();
    d.sensitivity = threshold->value() / 100.0;
    d.minGapMs = gap->value();
    return d;
}

double QuantizePanel::hitFilter() const { return filter->currentData().toDouble(); }

audioquantize::Settings QuantizePanel::settings() const
{
    audioquantize::Settings s;
    s.division = division->currentData().toDouble() * tuplet->currentData().toDouble();
    s.strength = strength->value() / 100.0;
    s.swing = swing->value() / 100.0;
    s.reach = catchRange->value() / 100.0;
    s.leaveWithin = nonQ->value() / 100.0;
    s.randomMs = randomMs->value();
    s.seed = seed;
    s.groove = audioquantize::fromText (groove->currentData().toString().toStdString());
    return s;
}

double QuantizePanel::snapDivision() const
{
    if (! snap->isChecked())
        return 0.0;
    const auto s = settings();
    return s.groove.isSet() ? s.groove.division : s.division;
}

bool QuantizePanel::autoApply() const { return autoBox->isChecked(); }
bool QuantizePanel::iterative() const { return iterate->isChecked(); }
int QuantizePanel::mode() const       { return algorithm->currentData().toInt(); }

void QuantizePanel::setMode (int m)
{
    updating = true;
    algorithm->setCurrentIndex (std::max (0, algorithm->findData (m)));
    updating = false;
}

void QuantizePanel::setState (const State& s)
{
    state = s;
    updating = true;
    edit->setChecked (s.editing);
    edit->setText (s.editing ? tr ("✓ &Editing Warp Markers") : tr ("&Edit Warp Markers"));
    updating = false;
    edit->setEnabled (s.open);
    hitCount->setText (! s.open ? tr ("No file") : ! s.editing ? tr ("Turn on Edit Warp Markers to see the hits")
                                                               : tr ("%n hit(s), %1 warp marker(s)", nullptr, s.hits).arg (s.markers));
    rangeLabel->setText (! s.open ? QString()
                         : ! s.grid.isSet() ? tr ("Quantizing needs the tempo: Detect it below, or Set it.")
                                            : tr ("Quantizes %1.").arg (s.range));
    tempo->setText (s.grid.isSet() ? tr ("%1 BPM, %2/4").arg (s.grid.bpm, 0, 'f', 2).arg (s.grid.beatsPerBar) : tr ("No tempo"));
    gridView->setHits (s.hitsInBar);
    updateEnabled();
    showGrid();
}

void QuantizePanel::setStatus (const QString& text) { status->setText (text); }

QWidget* QuantizePanel::focusTarget() const { return quantize->isEnabled() ? (QWidget*) quantize : (QWidget*) edit; }
