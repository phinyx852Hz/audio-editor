// SPDX-License-Identifier: AGPL-3.0-only
#include "StepSequencerView.h"
#include "Settings.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace
{
    // The step a lane plays at a beat from the pattern's start; -1: none yet.
    int stepAt (const slicer::Lane& lane, double beat)
    {
        if (beat < 0.0)
            return -1;
        return slicer::stepIndex (lane, (int64_t) std::floor (beat / slicer::rateBeats (lane.rate) + 1.0e-9));
    }

    // The settings StepValues draws: their range, and where the bars grow from.
    struct Value
    {
        const char* name;
        int low, high, zero;
    };
    constexpr Value valueOf[] {
        { "Velocity", 1, 127, 1 }, { "Gate", 1, 100, 1 }, { "Pitch", -24, 24, 0 }, { "Chance", 0, 100, 0 },
        { "Repeats", 1, slicer::maxRepeats, 1 }, { "Ramp", -100, 100, 0 }, { "Shift", -50, 50, 0 } };

    int get (const slicer::Step& s, StepValues::Which w)
    {
        switch (w)
        {
            case StepValues::velocity: return s.velocity;
            case StepValues::gate:     return s.gate;
            case StepValues::pitch:    return s.pitch;
            case StepValues::chance:   return s.chance;
            case StepValues::repeats:  return s.repeats;
            case StepValues::ramp:     return s.ramp;
            case StepValues::shift:    return s.shift;
        }
        return 0;
    }

    void set (slicer::Step& s, StepValues::Which w, int v)
    {
        const auto& r = valueOf[w];
        v = std::clamp (v, r.low, r.high);
        switch (w)
        {
            case StepValues::velocity: s.velocity = (uint8_t) v; break;
            case StepValues::gate:     s.gate = (uint8_t) v; break;
            case StepValues::pitch:    s.pitch = (int8_t) v; break;
            case StepValues::chance:   s.chance = (uint8_t) v; break;
            case StepValues::repeats:  s.repeats = (uint8_t) v; break;
            case StepValues::ramp:     s.ramp = (int8_t) v; break;
            case StepValues::shift:    s.shift = (int8_t) v; break;
        }
    }
}

// --- StepGrid ---------------------------------------------------------------------------

StepGrid::StepGrid (slicer::Pattern& p, const PadLooks& l, QWidget* parent) : QWidget (parent), pattern (p), looks (l)
{
    setMouseTracking (false);
    setToolTip (tr ("Click a step to turn it on or off, drag to paint. Click a pad's name to choose it; "
                    "M and S mute and solo its lane. Right-click a step to choose its pad"));
}

void StepGrid::setPlayhead (bool on, double at)
{
    if (on == playing && (! on || at == beat))
        return;
    playing = on;
    beat = at;
    update();
}

int StepGrid::columns() const
{
    int most = 16;
    for (int i = 0; i < slicer::padsPerBank; ++i)
        most = std::max<int> (most, pattern.lanes[(size_t) (bank * slicer::padsPerBank + i)].steps);
    return std::min (slicer::maxSteps, (most + 15) / 16 * 16);
}

QRectF StepGrid::cell (int row, int column) const
{
    const auto w = (width() - header - 1) / (double) columns();
    return QRectF (header + column * w, row * rowHeight + 1, w, rowHeight).adjusted (1, 1, -1, -1);
}

int StepGrid::columnAt (double x) const
{
    if (x < header)
        return -1;
    const auto w = (width() - header - 1) / (double) columns();
    return std::clamp ((int) ((x - header) / w), 0, columns() - 1);
}

void StepGrid::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    const auto lit = theme.color (QStringLiteral ("highlight"));
    const auto marker = theme.color (QStringLiteral ("marker"));
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.85);
    g.setFont (small);
    const int n = columns();
    bool soloing = false;
    for (const auto& l : pattern.lanes)
        soloing = soloing || (l.solo && ! l.empty());
    for (int row = 0; row < slicer::padsPerBank; ++row)
    {
        const auto pad = bank * slicer::padsPerBank + row;
        const auto& lane = pattern.lanes[(size_t) pad];
        const bool quiet = lane.mute || (soloing && ! lane.solo);
        // The pad: its colour, its name, M and S.
        const QRectF head (0, row * rowHeight + 1, header - 4, rowHeight - 2);
        g.setPen (Qt::NoPen);
        g.setBrush (pad == selected ? lit.darker (180) : QColor (44, 44, 48));
        g.drawRect (head);
        if (const auto c = looks.colour (pad); c.isValid())
        {
            g.setBrush (c);
            g.drawRect (QRectF (head.left(), head.top(), 4, head.height()));
        }
        const QRectF m (head.right() - 36, head.top() + 2, 16, head.height() - 4), s (head.right() - 18, head.top() + 2, 16, head.height() - 4);
        g.setPen (quiet ? QColor (120, 120, 126) : QColor (225, 225, 230));
        g.drawText (head.adjusted (8, 0, -40, 0), Qt::AlignLeft | Qt::AlignVCenter,
                    g.fontMetrics().elidedText (looks.name (pad), Qt::ElideRight, (int) head.width() - 48));
        for (const auto& [r, on, label, colour] : { std::tuple { m, lane.mute, tr ("M"), QColor (230, 160, 40) },
                                                    std::tuple { s, lane.solo, tr ("S"), QColor (60, 160, 230) } })
        {
            g.setPen (QColor (20, 20, 22));
            g.setBrush (on ? colour : QColor (62, 62, 68));
            g.drawRect (r);
            g.setPen (on ? QColor (20, 20, 20) : QColor (170, 170, 175));
            g.drawText (r, Qt::AlignCenter, label);
        }
        // Its steps, in beats of four.
        const auto now = playing ? stepAt (lane, beat) : -1;
        for (int c = 0; c < n; ++c)
        {
            const auto r = cell (row, c);
            const bool inside = c < lane.steps;
            const auto& step = lane.step[(size_t) c];
            QColor fill = ! inside ? QColor (30, 30, 32) : ((c / 4) % 2 == 0 ? QColor (66, 66, 72) : QColor (54, 54, 60));
            if (inside && step.on)
            {
                auto on = looks.colour (pad).isValid() ? looks.colour (pad) : lit;
                fill = quiet ? on.darker (220) : on.darker (static_cast<int> (100 + 120 * (1.0 - step.velocity / 127.0)));
            }
            g.setPen (c == now && inside ? QPen (marker, 2.0) : Qt::NoPen);
            g.setBrush (fill);
            g.drawRect (r);
            // A step that hits more than once, or not every time, says so.
            if (inside && step.on && (step.repeats > 1 || step.chance < 100))
            {
                g.setPen (QColor (20, 20, 20));
                g.drawText (r, Qt::AlignCenter, step.repeats > 1 ? QString::number (step.repeats) : QStringLiteral ("?"));
            }
        }
    }
}

void StepGrid::mousePressEvent (QMouseEvent* e)
{
    const auto at = e->position();
    const auto row = std::clamp ((int) (at.y() / rowHeight), 0, slicer::padsPerBank - 1);
    const auto pad = bank * slicer::padsPerBank + row;
    auto& lane = pattern.lanes[(size_t) pad];
    if (at.x() < header)
    {
        if (e->button() != Qt::LeftButton)
            return;
        // M or S, or the pad's name.
        if (at.x() >= header - 4 - 36 && at.x() < header - 4 - 20)
            lane.mute = ! lane.mute;
        else if (at.x() >= header - 4 - 18 && at.x() < header - 4)
            lane.solo = ! lane.solo;
        else
        {
            Q_EMIT padChosen (pad);
            return;
        }
        Q_EMIT laneEdited (pad);
        update();
        return;
    }
    const auto c = columnAt (at.x());
    if (e->button() == Qt::RightButton)
    {
        Q_EMIT padChosen (pad);
        return;
    }
    if (e->button() != Qt::LeftButton || c >= lane.steps)
        return;
    paintRow = row;
    paintOn = ! lane.step[(size_t) c].on;
    paintStep (at);
}

void StepGrid::mouseMoveEvent (QMouseEvent* e)
{
    if (paintRow >= 0 && (e->buttons() & Qt::LeftButton))
        paintStep (e->position());
}

void StepGrid::mouseReleaseEvent (QMouseEvent*)
{
    paintRow = -1;
}

// Paints along the row the drag began on.
void StepGrid::paintStep (QPointF at)
{
    const auto c = columnAt (at.x());
    const auto pad = bank * slicer::padsPerBank + paintRow;
    auto& lane = pattern.lanes[(size_t) pad];
    if (c < 0 || c >= lane.steps || lane.step[(size_t) c].on == paintOn)
        return;
    lane.step[(size_t) c].on = paintOn;
    Q_EMIT laneEdited (pad);
    update();
}

// --- StepValues -------------------------------------------------------------------------

StepValues::StepValues (slicer::Pattern& p, QWidget* parent) : QWidget (parent), pattern (p)
{
    setMouseTracking (true);
    setToolTip (tr ("Drag across to draw the chosen pad's steps"));
}

QRectF StepValues::bars() const
{
    return QRectF (StepGrid::header, 2, width() - StepGrid::header - 1, height() - 4);
}

void StepValues::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    const auto& lanes = pattern.lanes[(size_t) lane];
    const auto& v = valueOf[which];
    const auto area = bars();
    g.fillRect (area, QColor (36, 36, 40));
    const auto w = area.width() / shown;
    auto y = [&] (int value) { return area.bottom() - (value - v.low) / (double) (v.high - v.low) * area.height(); };
    const auto base = y (v.zero);
    if (v.zero > v.low)
    {
        g.setPen (QColor (90, 90, 96));
        g.drawLine (QPointF (area.left(), base), QPointF (area.right(), base));
    }
    const auto lit = theme.color (QStringLiteral ("highlight"));
    for (int c = 0; c < std::min<int> (shown, lanes.steps); ++c)
    {
        const auto& s = lanes.step[(size_t) c];
        const auto top = y (get (s, which));
        const QRectF r (area.left() + c * w + 1, std::min (top, base), w - 2, std::max (1.0, std::abs (base - top)));
        g.fillRect (r, s.on ? lit : lit.darker (300));
    }
    // The setting's name and, under the pointer, the step's value.
    const QRectF head (0, area.top(), StepGrid::header - 4, area.height());
    g.fillRect (head, QColor (44, 44, 48));
    g.setPen (QColor (225, 225, 230));
    auto label = tr (v.name);
    if (hovered >= 0 && hovered < lanes.steps)
        label += QStringLiteral ("  %1: %2").arg (hovered + 1).arg (get (lanes.step[(size_t) hovered], which));
    g.drawText (head.adjusted (8, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, label);
}

void StepValues::draw (QPointF at)
{
    const auto area = bars();
    const auto c = (int) std::floor ((at.x() - area.left()) / (area.width() / shown));
    auto& l = pattern.lanes[(size_t) lane];
    if (c < 0 || c >= l.steps)
        return;
    const auto& v = valueOf[which];
    const auto value = (int) std::lround (v.low + (area.bottom() - at.y()) / area.height() * (v.high - v.low));
    auto& s = l.step[(size_t) c];
    if (get (s, which) == std::clamp (value, v.low, v.high))
        return;
    set (s, which, value);
    Q_EMIT laneEdited (lane);
    update();
}

void StepValues::mousePressEvent (QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        draw (e->position());
}

void StepValues::mouseMoveEvent (QMouseEvent* e)
{
    const auto area = bars();
    hovered = e->position().x() < area.left() ? -1 : (int) std::floor ((e->position().x() - area.left()) / (area.width() / shown));
    if (e->buttons() & Qt::LeftButton)
        draw (e->position());
    update();
}

void StepValues::leaveEvent (QEvent*)
{
    hovered = -1;
    update();
}

// --- StepSequencerView ------------------------------------------------------------------

StepSequencerView::StepSequencerView (slicer::Engine& e, PadLooks l, QWidget* parent)
    : QWidget (parent), engine (e), looks (std::move (l))
{
    shown = engine.pattern (0);
    grid = new StepGrid (shown, looks, this);
    values = new StepValues (shown, this);

    // The transport.
    playButton = new QPushButton (tr ("Play"), this);
    playButton->setCheckable (true);
    playButton->setToolTip (tr ("Play the patterns, or stop (Space in the step grid)"));
    recordBox = new QCheckBox (tr ("Rec&ord"), this);
    recordBox->setToolTip (tr ("While playing, pads you hit land on the nearest step of the pattern"));
    followBox = new QCheckBox (tr ("Follow &Mixer"), this);
    followBox->setToolTip (tr ("Play in time with the Mixer: its transport starts and stops the patterns, at its tempo"));
    tempoBox = new QSpinBox (this);
    tempoBox->setRange (20, 400);
    tempoBox->setSuffix (tr (" BPM"));
    tempoBox->setToolTip (tr ("The patterns' own tempo (the Mixer's while following it)"));

    // Patterns 1 to 16.
    patterns = new QButtonGroup (this);
    patterns->setExclusive (true);
    auto* patternRow = new QHBoxLayout;
    patternRow->setSpacing (2);
    patternRow->addWidget (new QLabel (tr ("Pattern"), this));
    for (int i = 0; i < slicer::numPatterns; ++i)
    {
        auto* b = new QPushButton (QString::number (i + 1), this);
        b->setCheckable (true);
        b->setFocusPolicy (Qt::NoFocus);
        b->setFixedWidth (24);
        b->setToolTip (tr ("Edit pattern %1 and play it next (from the next bar while playing)").arg (i + 1));
        patterns->addButton (b, i);
        patternRow->addWidget (b);
    }
    patterns->button (0)->setChecked (true);
    auto* patternMenu = new QPushButton (tr ("Edit"), this);
    auto* menu = new QMenu (patternMenu);
    menu->addAction (tr ("Copy Pattern"), this, [this] { copied = std::make_unique<slicer::Pattern> (shown); });
    menu->addAction (tr ("Paste Pattern"), this, [this]
    {
        if (copied == nullptr)
            return;
        shown = *copied;
        engine.setPattern (index, shown);
        showPattern (index);
    });
    menu->addAction (tr ("Clear Pattern"), this, [this]
    {
        for (auto& lane : shown.lanes)
            lane = {};
        engine.setPattern (index, shown);
        showPattern (index);
    });
    patternMenu->setMenu (menu);
    patternRow->addWidget (patternMenu);
    patternRow->addStretch (1);

    barsBox = new QSpinBox (this);
    barsBox->setRange (1, 16);
    barsBox->setPrefix (tr ("bars "));
    barsBox->setToolTip (tr ("How long the pattern plays in a chain before the next"));
    swingBox = new QSpinBox (this);
    swingBox->setRange (0, 100);
    swingBox->setSuffix (tr (" %"));
    swingBox->setToolTip (tr ("Every other step later, by up to half a step"));
    chainEdit = new QLineEdit (this);
    chainEdit->setPlaceholderText (tr ("1 1 2 3"));
    chainEdit->setToolTip (tr ("The patterns a chain plays in turn, by number"));
    chainBox = new QCheckBox (tr ("C&hain"), this);
    chainBox->setToolTip (tr ("Play the chain from its start instead of one pattern"));

    auto* transport = new QHBoxLayout;
    transport->addWidget (playButton);
    transport->addWidget (recordBox);
    transport->addWidget (followBox);
    transport->addWidget (tempoBox);
    transport->addSpacing (12);
    auto* swingLabel = new QLabel (tr ("S&wing"), this);
    swingLabel->setBuddy (swingBox);
    transport->addWidget (swingLabel);
    transport->addWidget (swingBox);
    transport->addWidget (barsBox);
    transport->addSpacing (12);
    transport->addWidget (chainBox);
    transport->addWidget (chainEdit, 1);

    // The chosen pad's lane.
    laneTitle = new QLabel (this);
    stepsBox = new QSpinBox (this);
    stepsBox->setRange (1, slicer::maxSteps);
    stepsBox->setSuffix (tr (" steps"));
    stepsBox->setToolTip (tr ("The lane's length: lanes of different lengths run against each other"));
    rateBox = new QComboBox (this);
    rateBox->addItems ({ tr ("1/4"), tr ("1/4 dotted"), tr ("1/4 triplet"), tr ("1/8"), tr ("1/8 dotted"), tr ("1/8 triplet"),
                         tr ("1/16"), tr ("1/16 dotted"), tr ("1/16 triplet"), tr ("1/32"), tr ("1/32 triplet"), tr ("1/64") });
    rateBox->setToolTip (tr ("How long each of the lane's steps is"));
    directionBox = new QComboBox (this);
    directionBox->addItems ({ tr ("Forward"), tr ("Backward"), tr ("Back and forth"), tr ("Random") });
    offsetBox = new QSpinBox (this);
    offsetBox->setRange (0, slicer::maxSteps - 1);
    offsetBox->setPrefix (tr ("from "));
    offsetBox->setToolTip (tr ("The step the lane starts on"));
    laneSwingBox = new QSpinBox (this);
    laneSwingBox->setRange (0, 100);
    laneSwingBox->setPrefix (tr ("swing "));
    laneSwingBox->setSuffix (tr (" %"));
    laneSwingBox->setToolTip (tr ("The lane's own swing, on top of the pattern's"));
    auto* laneMenuButton = new QPushButton (tr ("Lane"), this);
    auto* laneMenu = new QMenu (laneMenuButton);
    for (const auto every : { 1, 2, 4, 8 })
        laneMenu->addAction (every == 1 ? tr ("Fill Every Step") : tr ("Fill Every %1 Steps").arg (every), this, [this, every]
        {
            for (int i = 0; i < lane().steps; ++i)
                lane().step[(size_t) i].on = i % every == 0;
            laneEdited (selected);
        });
    laneMenu->addSeparator();
    laneMenu->addAction (tr ("Shift Left"), this, [this]
    {
        auto& l = lane();
        std::rotate (l.step.begin(), l.step.begin() + 1, l.step.begin() + l.steps);
        laneEdited (selected);
    });
    laneMenu->addAction (tr ("Shift Right"), this, [this]
    {
        auto& l = lane();
        std::rotate (l.step.begin(), l.step.begin() + l.steps - 1, l.step.begin() + l.steps);
        laneEdited (selected);
    });
    laneMenu->addAction (tr ("Clear Lane"), this, [this]
    {
        lane() = {};
        laneEdited (selected);
    });
    laneMenuButton->setMenu (laneMenu);
    valuesBox = new QComboBox (this);
    for (const auto& v : valueOf)
        valuesBox->addItem (tr (v.name));
    valuesBox->setToolTip (tr ("Which setting of the steps the lane below draws"));

    auto* laneRow = new QHBoxLayout;
    laneRow->addWidget (laneTitle);
    laneRow->addWidget (stepsBox);
    laneRow->addWidget (rateBox);
    laneRow->addWidget (directionBox);
    laneRow->addWidget (offsetBox);
    laneRow->addWidget (laneSwingBox);
    laneRow->addWidget (laneMenuButton);
    laneRow->addStretch (1);
    auto* drawLabel = new QLabel (tr ("&Draw"), this);
    drawLabel->setBuddy (valuesBox);
    laneRow->addWidget (drawLabel);
    laneRow->addWidget (valuesBox);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (0, 0, 0, 0);
    layout->setSpacing (3);
    layout->addLayout (transport);
    layout->addLayout (patternRow);
    // The grid scrolls when the place it docks in is lower than its sixteen rows.
    auto* gridScroll = new QScrollArea (this);
    gridScroll->setWidget (grid);
    gridScroll->setWidgetResizable (true);
    gridScroll->setFrameShape (QFrame::NoFrame);
    gridScroll->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    gridScroll->setMinimumHeight (64);
    gridScroll->viewport()->setAutoFillBackground (false);
    layout->addWidget (gridScroll, 1);
    layout->addLayout (laneRow);
    layout->addWidget (values);

    connect (grid, &StepGrid::laneEdited, this, &StepSequencerView::laneEdited);
    connect (values, &StepValues::laneEdited, this, &StepSequencerView::laneEdited);
    connect (grid, &StepGrid::padChosen, this, &StepSequencerView::padChosen);
    connect (playButton, &QPushButton::clicked, this, [this] { playOrStop(); });
    connect (recordBox, &QCheckBox::toggled, this, [this] (bool on) { engine.setRecording (on); });
    connect (followBox, &QCheckBox::toggled, this, &StepSequencerView::follow);
    connect (tempoBox, &QSpinBox::valueChanged, this, [this] (int bpm)
    {
        if (! updating)
            engine.setTempo (bpm);
    });
    connect (patterns, &QButtonGroup::idClicked, this, [this] (int i)
    {
        engine.queuePattern (i);
        showPattern (i);
    });
    connect (barsBox, &QSpinBox::valueChanged, this, [this] (int bars)
    {
        if (updating)
            return;
        shown.bars = (uint8_t) bars;
        engine.setBars (index, bars);
    });
    connect (swingBox, &QSpinBox::valueChanged, this, [this] (int swing)
    {
        if (updating)
            return;
        shown.swing = (uint8_t) swing;
        engine.setSwing (index, swing);
        grid->update();
    });
    connect (chainEdit, &QLineEdit::editingFinished, this, &StepSequencerView::chainEdited);
    connect (chainBox, &QCheckBox::toggled, this, &StepSequencerView::chainEdited);
    for (auto* box : { stepsBox, offsetBox, laneSwingBox })
        connect (box, &QSpinBox::valueChanged, this, &StepSequencerView::laneFromControls);
    for (auto* box : { rateBox, directionBox })
        connect (box, &QComboBox::currentIndexChanged, this, &StepSequencerView::laneFromControls);
    connect (valuesBox, &QComboBox::currentIndexChanged, this, [this] (int i)
    {
        values->setWhich ((StepValues::Which) i);
        appSettings().setValue (QStringLiteral ("slicer/stepValues"), i);
    });
    valuesBox->setCurrentIndex (std::clamp (appSettings().value (QStringLiteral ("slicer/stepValues"), 0).toInt(), 0, (int) std::size (valueOf) - 1));

    timer.setInterval (33);
    connect (&timer, &QTimer::timeout, this, &StepSequencerView::tick);
    refresh();
}

void StepSequencerView::setMixerClock (std::function<slicer::Engine::Clock()> make)
{
    mixerClock = std::move (make);
    followBox->setEnabled (mixerClock != nullptr);
    if (mixerClock != nullptr && appSettings().value (QStringLiteral ("slicer/followMixer"), false).toBool())
        followBox->setChecked (true);
}

void StepSequencerView::follow (bool on)
{
    on = on && mixerClock != nullptr;
    engine.setClock (on ? mixerClock() : slicer::Engine::Clock {});
    appSettings().setValue (QStringLiteral ("slicer/followMixer"), on);
    playButton->setEnabled (! on);
    tempoBox->setEnabled (! on);
    tick();
}

void StepSequencerView::playOrStop()
{
    if (engine.followsClock())
        return;
    if (engine.isPlaying())
        engine.stop();
    else
        engine.play();
    tick();
}

void StepSequencerView::refresh()
{
    updating = true;
    tempoBox->setValue ((int) std::lround (engine.tempo()));
    const auto chain = engine.chain();
    QStringList numbers;
    for (const auto p : chain)
        numbers << QString::number (p + 1);
    chainEdit->setText (numbers.join (QLatin1Char (' ')));
    chainBox->setChecked (engine.chainOn());
    updating = false;
    showPattern (engine.isPlaying() ? engine.playingPattern() : index);
}

void StepSequencerView::looksChanged()
{
    laneToControls();
    grid->update();
}

void StepSequencerView::setBank (int b)
{
    bank = b;
    grid->setBank (b);
    values->setColumns (grid->columns());
}

void StepSequencerView::setSelected (int pad)
{
    selected = std::clamp (pad, 0, slicer::numLanes - 1);
    grid->setSelected (selected);
    if (auto* area = qobject_cast<QScrollArea*> (grid->parentWidget()->parentWidget()))
    {
        const int rowHeight = grid->height() / slicer::padsPerBank;
        area->ensureVisible (0, (selected % slicer::padsPerBank) * rowHeight + rowHeight / 2, 0, rowHeight);
    }
    values->setLane (selected);
    laneToControls();
}

void StepSequencerView::showPattern (int i)
{
    index = std::clamp (i, 0, slicer::numPatterns - 1);
    shown = engine.pattern (index);
    patterns->button (index)->setChecked (true);
    updating = true;
    barsBox->setValue (shown.bars);
    swingBox->setValue (shown.swing);
    updating = false;
    laneToControls();
    values->setColumns (grid->columns());
    grid->update();
    values->update();
}

void StepSequencerView::laneEdited (int l)
{
    engine.setLane (index, l, shown.lanes[(size_t) l]);
    values->setColumns (grid->columns());
    grid->update();
    values->update();
}

void StepSequencerView::laneToControls()
{
    updating = true;
    const auto& l = lane();
    laneTitle->setText (looks.name (selected));
    stepsBox->setValue (l.steps);
    rateBox->setCurrentIndex ((int) l.rate);
    directionBox->setCurrentIndex ((int) l.direction);
    offsetBox->setValue (l.offset);
    laneSwingBox->setValue (l.swing);
    updating = false;
}

void StepSequencerView::laneFromControls()
{
    if (updating)
        return;
    auto& l = lane();
    l.steps = (uint8_t) stepsBox->value();
    l.rate = (slicer::Rate) rateBox->currentIndex();
    l.direction = (slicer::Direction) directionBox->currentIndex();
    l.offset = (uint8_t) offsetBox->value();
    l.swing = (uint8_t) laneSwingBox->value();
    laneEdited (selected);
}

void StepSequencerView::chainEdited()
{
    if (updating)
        return;
    std::vector<int> chain;
    for (const auto& word : chainEdit->text().split (QRegularExpression (QStringLiteral ("[\\s,]+")), Qt::SkipEmptyParts))
        if (bool ok = false; word.toInt (&ok) >= 1 && ok && word.toInt() <= slicer::numPatterns)
            chain.push_back (word.toInt() - 1);
    engine.setChain (chain, chainBox->isChecked());
}

// The playhead, the pattern playing and the one queued, and steps recorded.
void StepSequencerView::tick()
{
    const bool playing = engine.isPlaying();
    if (playButton->isChecked() != playing)
        playButton->setChecked (playing);
    playButton->setText (playing ? tr ("Stop") : tr ("Play"));
    const auto now = engine.playingPattern(), next = engine.queuedPattern();
    if (now != playingWas || next != queuedWas)
    {
        for (int i = 0; i < slicer::numPatterns; ++i)
            patterns->button (i)->setText ((i == now && playing ? QStringLiteral ("▸") : QString())
                                           + QString::number (i + 1) + (i == next ? QStringLiteral ("…") : QString()));
        // Following a chain, the grid shows what plays.
        if (playing && now != index && engine.chainOn())
            showPattern (now);
        playingWas = now;
        queuedWas = next;
    }
    if (const auto r = engine.recorded(); r != recordedWas)
    {
        recordedWas = r;
        if (now == index)
            showPattern (index);
    }
    grid->setPlayhead (playing && now == index, engine.playingBeat());
}

void StepSequencerView::showEvent (QShowEvent* e)
{
    QWidget::showEvent (e);
    timer.start();
    tick();
}

void StepSequencerView::hideEvent (QHideEvent* e)
{
    timer.stop();
    QWidget::hideEvent (e);
}
