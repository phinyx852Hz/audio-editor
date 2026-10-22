// SPDX-License-Identifier: AGPL-3.0-only
#include "SlicerWindow.h"
#include "Settings.h"
#include "Theme.h"
#include "../core/TimeStretch.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QApplication>
#include <QHideEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    // Note names as Live and Push show them: C3 is MIDI note 60.
    QString noteName (int note)
    {
        static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return note < 0 ? QString() : QString::fromLatin1 (names[note % 12]) + QString::number (note / 12 - 2);
    }

    QString padName (int pad)
    {
        return QString (QChar ('A' + pad / slicer::padsPerBank)) + QString::number (pad % slicer::padsPerBank + 1);
    }

    // The pads on the computer keyboard by key position (PC scan codes): the bottom row
    // is pads 1 to 4, as on Push.
    constexpr int padKeys[16] {
        0x2C, 0x2D, 0x2E, 0x2F,   // Z X C V
        0x1E, 0x1F, 0x20, 0x21,   // A S D F
        0x10, 0x11, 0x12, 0x13,   // Q W E R
        0x02, 0x03, 0x04, 0x05,   // 1 2 3 4
    };
    constexpr int bankKeys[4] { 0x06, 0x07, 0x08, 0x09 };   // 5 6 7 8
    int scanCodeOf (const QKeyEvent* e) { return (int) e->nativeScanCode() - 8; }   // X11 and Wayland: evdev + 8

    enum Method { transients, equalParts, beatGrid, editorMarkers };
}

//==============================================================================
SliceView::SliceView (slicer::Engine& e, QWidget* parent) : QWidget (parent), engine (e)
{
    setFocusPolicy (Qt::ClickFocus);
    setMouseTracking (true);
    setToolTip (tr ("Click a slice to play it. Drag a line to move it, double-click to add one, right-click a line to remove it"));
}

void SliceView::setSample (sampler::SamplePtr s)
{
    sample = std::move (s);
    buildPeaks();
    update();
}

double SliceView::xFor (double frame) const
{
    const auto n = sample != nullptr ? std::max<Frame> (1, sample->length()) : 1;
    return frame / (double) n * width();
}

Frame SliceView::frameAt (double x) const
{
    const auto n = sample != nullptr ? sample->length() : 0;
    return std::clamp<Frame> ((Frame) std::llround (x / std::max (1, width()) * (double) n), 0, n);
}

Frame SliceView::cutNear (double x) const
{
    double best = 6.0;
    Frame found = -1;
    for (const auto& p : kit.pads)
        if (p.used && p.start > 0)
            if (const auto d = std::abs (xFor ((double) p.start) - x); d <= best)
            {
                best = d;
                found = p.start;
            }
    return found;
}

int SliceView::padAt (Frame f) const
{
    for (int i = 0; i < slicer::numPads; ++i)
        if (const auto& p = kit.pads[(size_t) i]; p.used && f >= p.start && f < p.end)
            return i;
    return -1;
}

void SliceView::resizeEvent (QResizeEvent*)
{
    buildPeaks();
}

void SliceView::buildPeaks()
{
    peaks.clear();
    if (sample == nullptr || sample->length() == 0 || width() <= 0)
        return;
    const auto columns = width();
    peaks.assign ((size_t) columns, { 0.0f, 0.0f });
    const auto n = sample->length();
    for (int c = 0; c < columns; ++c)
    {
        const auto from = (Frame) ((double) c / columns * (double) n);
        const auto to = std::max (from + 1, (Frame) ((double) (c + 1) / columns * (double) n));
        float lo = 0.0f, hi = 0.0f;
        for (Frame i = from; i < std::min (to, n); ++i)
        {
            const auto v = 0.5f * (sample->left[(size_t) i] + sample->right[(size_t) i]);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        peaks[(size_t) c] = { lo, hi };
    }
}

void SliceView::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
    if (sample == nullptr)
    {
        g.setPen (theme.color (QStringLiteral ("placeholderText")));
        g.drawText (rect(), Qt::AlignCenter, tr ("Send a sound here from the editor: Sample > Send to Slicer (F8)"));
        return;
    }
    const auto mid = height() / 2.0;

    // The chosen slice is shaded, every other one striped lightly so they read apart.
    auto shade = theme.color (QStringLiteral ("region"));
    for (int i = 0; i < slicer::numPads; ++i)
        if (const auto& p = kit.pads[(size_t) i]; p.used)
        {
            shade.setAlpha (i == selected ? 90 : (i % 2 == 0 ? 0 : 22));
            g.fillRect (QRectF (QPointF (xFor ((double) p.start), 0), QPointF (xFor ((double) p.end), height())), shade);
        }
    const auto wave = theme.color (QStringLiteral ("waveform"));
    g.setPen (wave);
    for (size_t c = 0; c < peaks.size(); ++c)
    {
        const auto x = (double) c + 0.5;
        g.drawLine (QPointF (x, mid - peaks[c].second * mid * 0.95), QPointF (x, mid - peaks[c].first * mid * 0.95));
    }
    g.setPen (QPen (theme.color (QStringLiteral ("centerLine")), 1.0));
    g.drawLine (QPointF (0, mid), QPointF (width(), mid));

    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (small);
    const auto line = theme.color (QStringLiteral ("marker"));
    const auto text = theme.color (QStringLiteral ("windowText"));
    for (int i = 0; i < slicer::numPads; ++i)
        if (const auto& p = kit.pads[(size_t) i]; p.used)
        {
            const auto x = xFor ((double) p.start);
            g.setPen (QPen (line, i == selected ? 2.0 : 1.0));
            g.drawLine (QPointF (x, 0), QPointF (x, height()));
            if (xFor ((double) p.end) - x > 18)
            {
                g.setPen (text);
                g.drawText (QRectF (x + 3, 2, 40, 14), Qt::AlignLeft | Qt::AlignVCenter, padName (i));
            }
        }

    g.setPen (QPen (theme.color (QStringLiteral ("cursor")), 1.5));
    for (const auto head : engine.playheads())
        g.drawLine (QPointF (xFor (head), 0), QPointF (xFor (head), height()));
}

void SliceView::mousePressEvent (QMouseEvent* e)
{
    if (sample == nullptr)
        return;
    const auto x = e->position().x();
    if (e->button() == Qt::RightButton)
    {
        if (const auto cut = cutNear (x); cut > 0)
            Q_EMIT cutRemoved (cut);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    dragging = cutNear (x);
    if (dragging >= 0)
        return;
    pressedPad = padAt (frameAt (x));
    if (pressedPad >= 0)
        Q_EMIT padPressed (pressedPad);
}

void SliceView::mouseMoveEvent (QMouseEvent* e)
{
    if (dragging < 0 || ! (e->buttons() & Qt::LeftButton))
    {
        setCursor (sample != nullptr && cutNear (e->position().x()) > 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    if (moveCut)
        dragging = moveCut (dragging, frameAt (e->position().x()));
}

void SliceView::mouseReleaseEvent (QMouseEvent*)
{
    dragging = -1;
    if (pressedPad >= 0)
        Q_EMIT padReleased (pressedPad);
    pressedPad = -1;
}

void SliceView::mouseDoubleClickEvent (QMouseEvent* e)
{
    if (sample != nullptr && e->button() == Qt::LeftButton && cutNear (e->position().x()) < 0)
    {
        if (pressedPad >= 0)
            Q_EMIT padReleased (pressedPad);
        pressedPad = -1;
        Q_EMIT cutAdded (frameAt (e->position().x()));
    }
}

//==============================================================================
PadGrid::PadGrid (slicer::Engine& e, QWidget* parent) : QWidget (parent), engine (e)
{
    setFocusPolicy (Qt::StrongFocus);
    QSizePolicy policy (QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth (true);
    setSizePolicy (policy);
}

QRectF PadGrid::padRect (int index) const
{
    const auto side = std::min (width(), height());
    const auto cell = side / 4.0;
    const auto row = index / 4, column = index % 4;
    return QRectF (column * cell, (3 - row) * cell, cell, cell).adjusted (3, 3, -3, -3);
}

void PadGrid::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto lit = theme.color (QStringLiteral ("highlight"));
    const auto text = theme.color (QStringLiteral ("windowText"));
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    for (int i = 0; i < slicer::padsPerBank; ++i)
    {
        const auto pad = bank * slicer::padsPerBank + i;
        const auto& p = kit.pads[(size_t) pad];
        const auto r = padRect (i);
        const bool on = engine.isSounding (pad) || pad == pressed;
        g.setPen (QPen (pad == selected ? theme.color (QStringLiteral ("marker")) : QColor (20, 20, 22), pad == selected ? 3.0 : 1.0));
        g.setBrush (on ? lit : (p.used ? QColor (70, 72, 80) : QColor (38, 38, 42)));
        g.drawRoundedRect (r, 5, 5);
        g.setPen (on ? QColor (20, 20, 20) : (p.used ? QColor (235, 235, 238) : QColor (110, 110, 115)));
        g.setFont (font());
        g.drawText (r.adjusted (6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop, padName (pad));
        g.setFont (small);
        g.drawText (r.adjusted (6, 4, -6, -4), Qt::AlignRight | Qt::AlignBottom, noteName (slicer::noteForPad (pad, kit.layout)));
        if (p.used && (p.reverse || p.choke > 0 || p.play == slicer::Play::gate))
        {
            QStringList tags;
            if (p.reverse) tags << tr ("Rev");
            if (p.play == slicer::Play::gate) tags << tr ("Gate");
            if (p.choke > 0) tags << tr ("Ch%1").arg (p.choke);
            g.drawText (r.adjusted (6, 4, -6, -4), Qt::AlignLeft | Qt::AlignBottom, tags.join (QLatin1Char (' ')));
        }
    }
    if (hasFocus())
    {
        g.setPen (QPen (lit, 2.0));
        g.setBrush (Qt::NoBrush);
        const auto side = std::min (width(), height());
        g.drawRect (QRectF (1, 1, side - 2, side - 2));
    }
    (void) text;
}

void PadGrid::mousePressEvent (QMouseEvent* e)
{
    setFocus();
    for (int i = 0; i < slicer::padsPerBank; ++i)
        if (const auto r = padRect (i); r.contains (e->position()))
        {
            pressed = bank * slicer::padsPerBank + i;
            const auto velocity = (float) std::clamp (0.35 + 0.65 * (1.0 - (e->position().y() - r.top()) / r.height()), 0.1, 1.0);
            Q_EMIT padPressed (pressed, velocity);
            break;
        }
    update();
}

void PadGrid::mouseReleaseEvent (QMouseEvent*)
{
    if (pressed >= 0)
        Q_EMIT padReleased (pressed);
    pressed = -1;
    update();
}

//==============================================================================
SlicerWindow::SlicerWindow (slicer::Engine& e, QWidget* parent)
    : QWidget (parent), engine (e)
{
    setFocusPolicy (Qt::StrongFocus);   // docked, it takes the playing keys when shown or clicked
    setWindowTitle (tr ("Slicer"));
    setObjectName (QStringLiteral ("SlicerWindow"));
    kit = engine.kit();
    auto stored = appSettings();

    title = new QLabel (tr ("No sound yet"), this);
    auto bold = title->font();
    bold.setBold (true);
    title->setFont (bold);
    view = new SliceView (engine, this);
    pads = new PadGrid (engine, this);
    status = new QLabel (this);
    status->setWordWrap (true);
    midiStatus = new QLabel (this);
    midiStatus->setAlignment (Qt::AlignRight | Qt::AlignVCenter);

    followBox = new QCheckBox (tr ("&Follow editor"), this);
    followBox->setChecked (true);
    followBox->setToolTip (tr ("Edits you make to this sound in the editor reach the Slicer straight away"));
    auto* refresh = new QPushButton (tr ("&Refresh"), this);
    refresh->setToolTip (tr ("Take the sound from the editor again"));
    auto* midi = new QPushButton (tr ("&MIDI Inputs..."), this);
    auto* top = new QHBoxLayout;
    top->addWidget (followBox);
    top->addStretch (1);
    top->addWidget (refresh);
    top->addWidget (midi);

    // How the sound is cut.
    methodBox = new QComboBox (this);
    methodBox->addItems ({ tr ("Transients"), tr ("Equal parts"), tr ("Beat grid"), tr ("Editor markers") });
    methodBox->setToolTip (tr ("Transients: a slice at every hit. Equal parts: the same length each. "
                               "Beat grid: on the file's tempo grid (Warp menu). Editor markers: at the markers you placed"));
    sensitivityBox = new QSpinBox (this);
    sensitivityBox->setRange (0, 100);
    sensitivityBox->setSingleStep (5);
    sensitivityBox->setSuffix (tr (" %"));
    sensitivityBox->setToolTip (tr ("Higher finds softer hits"));
    partsBox = new QSpinBox (this);
    partsBox->setRange (2, slicer::numPads);
    partsBox->setToolTip (tr ("How many slices"));
    divisionBox = new QComboBox (this);
    divisionBox->addItems ({ tr ("Bars"), tr ("Beats"), tr ("8ths"), tr ("16ths") });
    methodOptions = new QStackedWidget (this);
    auto option = [this] (const QString& label, QWidget* w)
    {
        auto* box = new QWidget (this);
        auto* l = new QHBoxLayout (box);
        l->setContentsMargins (0, 0, 0, 0);
        auto* text = new QLabel (label, box);
        text->setBuddy (w);
        l->addWidget (text);
        l->addWidget (w);
        l->addStretch (1);
        methodOptions->addWidget (box);
    };
    option (tr ("Sensiti&vity"), sensitivityBox);
    option (tr ("&Slices"), partsBox);
    option (tr ("&Every"), divisionBox);
    methodOptions->addWidget (new QWidget (this));
    auto* sliceButton = new QPushButton (tr ("Slice A&gain"), this);
    sliceButton->setToolTip (tr ("Cut the sound again with these settings (each pad keeps its own settings)"));
    snapBox = new QCheckBox (tr ("Snap to &zero"), this);
    snapBox->setChecked (true);
    snapBox->setToolTip (tr ("Lines you drag land where the wave crosses zero, so slices do not click"));
    auto* method = new QHBoxLayout;
    auto* methodLabel = new QLabel (tr ("Sl&ice by"), this);
    methodLabel->setBuddy (methodBox);
    method->addWidget (methodLabel);
    method->addWidget (methodBox);
    method->addWidget (methodOptions, 1);
    auto* method2 = new QHBoxLayout;
    method2->addWidget (sliceButton);
    method2->addStretch (1);
    method2->addWidget (snapBox);

    // The banks and the pads.
    banks = new QButtonGroup (this);
    auto* bankRow = new QHBoxLayout;
    for (int b = 0; b < 4; ++b)
    {
        auto* button = new QPushButton (QString (QChar ('A' + b)), this);
        button->setCheckable (true);
        button->setFocusPolicy (Qt::NoFocus);
        button->setToolTip (tr ("Bank %1: pads %2 to %3 (key %4)").arg (QChar ('A' + b)).arg (b * 16 + 1).arg (b * 16 + 16).arg (b + 5));
        banks->addButton (button, b);
        bankRow->addWidget (button);
    }
    banks->button (0)->setChecked (true);
    layoutBox = new QComboBox (this);
    layoutBox->addItems ({ tr ("Push 8x8"), tr ("Chromatic") });
    layoutBox->setToolTip (tr ("Push 8x8: Push's pads in User mode, bank A bottom left, B bottom right, C top left, D top right. "
                               "Chromatic: 16 notes a bank from C1, as Live's drum racks"));
    auto* left = new QVBoxLayout;
    left->addLayout (bankRow);
    left->addWidget (pads, 1);
    auto* layoutRow = new QHBoxLayout;
    auto* layoutLabel = new QLabel (tr ("&Layout"), this);
    layoutLabel->setBuddy (layoutBox);
    layoutRow->addWidget (layoutLabel);
    layoutRow->addWidget (layoutBox, 1);
    left->addLayout (layoutRow);

    // The chosen pad's settings, and the kit's.
    auto real = [this] (double lo, double hi, double step, int decimals, const QString& suffix)
    {
        auto* b = new QDoubleSpinBox (this);
        b->setRange (lo, hi);
        b->setSingleStep (step);
        b->setDecimals (decimals);
        b->setSuffix (suffix);
        b->setAccelerated (true);
        return b;
    };
    auto whole = [this] (int lo, int hi, int step, const QString& suffix)
    {
        auto* b = new QSpinBox (this);
        b->setRange (lo, hi);
        b->setSingleStep (step);
        b->setSuffix (suffix);
        b->setAccelerated (true);
        return b;
    };
    gainBox = real (-48.0, 12.0, 0.5, 1, tr (" dB"));
    panBox = whole (-100, 100, 5, QString());
    panBox->setToolTip (tr ("-100 is left, 100 right"));
    pitchBox = whole (-24, 24, 1, tr (" st"));
    reverseBox = new QCheckBox (tr ("Re&verse"), this);
    playBox = new QComboBox (this);
    playBox->addItems ({ tr ("One shot"), tr ("Gate") });
    playBox->setToolTip (tr ("One shot plays the whole slice; Gate stops when you let go of the pad"));
    attackBox = real (0.0, 2000.0, 1.0, 1, tr (" ms"));
    releaseBox = real (1.0, 5000.0, 5.0, 0, tr (" ms"));
    chokeBox = whole (0, 8, 1, QString());
    chokeBox->setSpecialValueText (tr ("None"));
    chokeBox->setToolTip (tr ("Pads in the same group cut each other off, like open and closed hi-hats"));
    velocityBox = whole (0, 100, 5, tr (" %"));
    velocityBox->setToolTip (tr ("How much softer hits are quieter; 0% plays every hit at full level"));
    voicesBox = whole (1, slicer::Engine::maxVoices, 1, QString());
    voicesBox->setToolTip (tr ("How many slices can sound at once"));

    padTitle = new QLabel (this);
    padTitle->setFont (bold);
    auto* padForm = new QFormLayout;
    padForm->addRow (padTitle);
    padForm->addRow (tr ("G&ain"), gainBox);
    padForm->addRow (tr ("&Pan"), panBox);
    padForm->addRow (tr ("Pi&tch"), pitchBox);
    padForm->addRow (QString(), reverseBox);
    padForm->addRow (tr ("Pla&y"), playBox);
    padForm->addRow (tr ("Atta&ck"), attackBox);
    padForm->addRow (tr ("Rele&ase"), releaseBox);
    padForm->addRow (tr ("C&hoke group"), chokeBox);
    auto* kitTitle = new QLabel (tr ("Whole kit"), this);
    kitTitle->setFont (bold);
    padForm->addRow (kitTitle);
    padForm->addRow (tr ("Veloc&ity"), velocityBox);
    padForm->addRow (tr ("V&oices"), voicesBox);

    auto* middle = new QHBoxLayout;
    middle->addLayout (left);
    middle->addSpacing (16);
    middle->addLayout (padForm);
    middle->addStretch (1);

    midiStatus->setAlignment (Qt::AlignLeft | Qt::AlignVCenter);
    midiStatus->setWordWrap (true);

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (title);
    layout->addLayout (top);
    layout->addWidget (view, 1);
    layout->addLayout (method);
    layout->addLayout (method2);
    layout->addLayout (middle, 2);
    layout->addWidget (status);
    layout->addWidget (midiStatus);

    // The slicing settings are remembered.
    methodBox->setCurrentIndex (std::clamp (stored.value (QStringLiteral ("slicer/method"), 0).toInt(), 0, 3));
    sensitivityBox->setValue (stored.value (QStringLiteral ("slicer/sensitivity"), 50).toInt());
    partsBox->setValue (stored.value (QStringLiteral ("slicer/parts"), 16).toInt());
    divisionBox->setCurrentIndex (std::clamp (stored.value (QStringLiteral ("slicer/division"), 3).toInt(), 0, 3));
    kit.layout = stored.value (QStringLiteral ("slicer/layout"), 0).toInt() == 1 ? slicer::Layout::chromatic : slicer::Layout::push;
    engine.setKit (kit);
    methodOptions->setCurrentIndex (methodBox->currentIndex());

    for (auto* w : findChildren<QWidget*>())
        if (qobject_cast<QAbstractSpinBox*> (w) != nullptr || qobject_cast<QComboBox*> (w) != nullptr)
            w->installEventFilter (this);

    view->moveCut = [this] (Frame from, Frame to) { return moveCut (from, to); };
    connect (view, &SliceView::padPressed, this, [this] (int pad) { select (pad); engine.hit (pad, 1.0f); });
    connect (view, &SliceView::padReleased, this, [this] (int pad) { engine.letGo (pad); });
    connect (view, &SliceView::cutAdded, this, [this] (Frame at)
    {
        auto cuts = currentCuts();
        cuts.push_back (snap (at));
        applyCuts (cuts);
    });
    connect (view, &SliceView::cutRemoved, this, [this] (Frame at)
    {
        auto cuts = currentCuts();
        cuts.erase (std::remove (cuts.begin(), cuts.end(), at), cuts.end());
        applyCuts (cuts);
    });
    connect (pads, &PadGrid::padPressed, this, [this] (int pad, float velocity) { select (pad); engine.hit (pad, velocity); });
    connect (pads, &PadGrid::padReleased, this, [this] (int pad) { engine.letGo (pad); });
    connect (banks, &QButtonGroup::idClicked, this, [this] (int b) { setBank (b); select (b * slicer::padsPerBank + selected % slicer::padsPerBank); });
    connect (refresh, &QPushButton::clicked, this, &SlicerWindow::refreshRequested);
    connect (midi, &QPushButton::clicked, this, &SlicerWindow::midiInputsRequested);
    connect (sliceButton, &QPushButton::clicked, this, &SlicerWindow::slice);
    connect (methodBox, &QComboBox::currentIndexChanged, this, &SlicerWindow::methodChanged);
    connect (divisionBox, &QComboBox::currentIndexChanged, this, &SlicerWindow::methodChanged);
    connect (sensitivityBox, &QSpinBox::valueChanged, this, &SlicerWindow::methodChanged);
    connect (partsBox, &QSpinBox::valueChanged, this, &SlicerWindow::methodChanged);
    connect (layoutBox, &QComboBox::currentIndexChanged, this, [this] (int i)
    {
        if (updating)
            return;
        kit.layout = i == 1 ? slicer::Layout::chromatic : slicer::Layout::push;
        appSettings().setValue (QStringLiteral ("slicer/layout"), i);
        setKit (kit);
    });
    connect (reverseBox, &QCheckBox::toggled, this, &SlicerWindow::padFromControls);
    connect (playBox, &QComboBox::currentIndexChanged, this, &SlicerWindow::padFromControls);
    for (auto* b : { gainBox, attackBox, releaseBox })
        connect (b, &QDoubleSpinBox::valueChanged, this, &SlicerWindow::padFromControls);
    for (auto* b : { panBox, pitchBox, chokeBox, velocityBox, voicesBox })
        connect (b, &QSpinBox::valueChanged, this, &SlicerWindow::padFromControls);

    // Pads light as they sound; a pad hit from MIDI becomes the one being edited.
    redraw.setInterval (33);
    connect (&redraw, &QTimer::timeout, this, [this]
    {
        if (const auto hit = engine.lastHit(); hit != lastSeenHit)
        {
            lastSeenHit = hit;
            if (hit >= 0 && hit != selected)
                select (hit);
        }
        for (int b = 0; b < 4; ++b)
        {
            bool on = false;
            for (int i = 0; i < slicer::padsPerBank && ! on; ++i)
                on = engine.isSounding (b * slicer::padsPerBank + i);
            const auto label = QString (QChar ('A' + b)) + (on ? QStringLiteral (" ●") : QString());
            if (banks->button (b)->text() != label)
                banks->button (b)->setText (label);
        }
        view->update();
        pads->update();
    });
    redraw.start();
    connect (qApp, &QApplication::focusChanged, this, &SlicerWindow::focusMoved);
    setKit (kit);
    select (0);
    showStatus();
    adjustSize();
    pads->setFocus();
}

void SlicerWindow::setSample (sampler::SamplePtr s, AudioDataPtr a, double r, const QString& n, bool keepSlices,
                              const BeatGrid& g, const std::vector<Frame>& m)
{
    const auto length = s != nullptr ? s->length() : 0;
    bool fits = keepSlices;
    for (const auto& p : kit.pads)
        fits = fits && (! p.used || p.end <= length);
    sample = s;
    audio = std::move (a);
    rate = r;
    name = n;
    grid = g;
    markers = m;
    engine.setSample (s);
    view->setSample (s);
    title->setText (tr ("%1   %2 s").arg (name).arg ((double) length / rate, 0, 'f', 3));
    setWindowTitle (tr ("Slicer: %1").arg (name));
    if (fits && kit.pads[0].used)
        setKit (kit);
    else
        slice();
}

bool SlicerWindow::followsEditor() const { return followBox->isChecked(); }

void SlicerWindow::setMidiStatus (const QString& text) { midiStatus->setText (text); }

void SlicerWindow::methodChanged()
{
    if (updating)
        return;
    methodOptions->setCurrentIndex (methodBox->currentIndex());
    auto stored = appSettings();
    stored.setValue (QStringLiteral ("slicer/method"), methodBox->currentIndex());
    stored.setValue (QStringLiteral ("slicer/sensitivity"), sensitivityBox->value());
    stored.setValue (QStringLiteral ("slicer/parts"), partsBox->value());
    stored.setValue (QStringLiteral ("slicer/division"), divisionBox->currentIndex());
    if (sample != nullptr)
        slice();
}

// Cuts the sound by the chosen method; each pad keeps its own settings.
void SlicerWindow::slice()
{
    if (sample == nullptr)
        return;
    const auto length = sample->length();
    std::vector<Frame> cuts;
    QString note;
    switch (methodBox->currentIndex())
    {
        case transients:
            if (audio != nullptr)
                cuts = timestretch::transients (*audio, sensitivityBox->value() / 100.0);
            break;
        case equalParts:
            for (const auto& [from, to] : slicer::equalSlices (length, partsBox->value()))
                cuts.push_back (from);
            break;
        case beatGrid:
        {
            if (! grid.isSet())
            {
                note = tr ("This file has no tempo grid yet (Warp > Detect Beat Grid), so it was cut in %1 equal parts.").arg (partsBox->value());
                for (const auto& [from, to] : slicer::equalSlices (length, partsBox->value()))
                    cuts.push_back (from);
                break;
            }
            static const double divisions[] { 0.0, 1.0, 0.5, 0.25 };
            const auto beats = divisionBox->currentIndex() == 0 ? (double) grid.beatsPerBar : divisions[divisionBox->currentIndex()];
            const auto step = beats * grid.beatSeconds();
            const auto seconds = (double) length / rate;
            for (auto t = grid.downbeat - std::floor (grid.downbeat / step) * step; t < seconds; t += step)
                cuts.push_back ((Frame) std::llround (t * rate));
            break;
        }
        default:
            cuts = markers;
            if (cuts.empty())
                note = tr ("No markers in this part of the file: place some in the editor (M), then Slice Again.");
            break;
    }
    applyCuts (cuts);
    if (! note.isEmpty())
        status->setText (note);
}

void SlicerWindow::applyCuts (std::vector<Frame> cuts)
{
    if (sample == nullptr)
        return;
    auto slices = slicer::slicesFromCuts (std::move (cuts), sample->length(), (Frame) (rate * 0.01));
    found = (int) slices.size();
    if (slices.size() > (size_t) slicer::numPads)
        slices.resize ((size_t) slicer::numPads);
    slicer::assign (kit, slices);
    setKit (kit);
    if (! kit.pads[(size_t) selected].used)
        select (0);
    showStatus();
}

std::vector<Frame> SlicerWindow::currentCuts() const
{
    std::vector<Frame> cuts;
    for (const auto& p : kit.pads)
        if (p.used)
        {
            cuts.push_back (p.start);
            cuts.push_back (p.end);
        }
    return cuts;
}

Frame SlicerWindow::moveCut (Frame from, Frame to)
{
    if (sample == nullptr)
        return from;
    const auto gap = (Frame) (rate * 0.005);
    Frame lo = 0, hi = sample->length();
    for (const auto& p : kit.pads)
        if (p.used)
        {
            if (p.end == from)
                lo = std::max (lo, p.start + gap);
            if (p.start == from)
                hi = std::min (hi, p.end - gap);
        }
    to = std::clamp<Frame> (snap (to), lo, std::max (lo, hi));
    if (to == from)
        return from;
    for (auto& p : kit.pads)
        if (p.used)
        {
            if (p.start == from)
                p.start = to;
            if (p.end == from)
                p.end = to;
        }
    setKit (kit);
    return to;
}

Frame SlicerWindow::snap (Frame f) const
{
    if (sample == nullptr || ! snapBox->isChecked())
        return f;
    // The nearest rising zero crossing within 10 ms.
    const auto reach = (Frame) (rate * 0.01);
    const auto n = sample->length();
    auto value = [this] (Frame i) { return sample->left[(size_t) i] + sample->right[(size_t) i]; };
    for (Frame d = 0; d <= reach; ++d)
        for (const auto i : { f - d, f + d })
            if (i > 0 && i < n && value (i - 1) <= 0.0f && value (i) > 0.0f)
                return i;
    return f;
}

void SlicerWindow::setKit (const slicer::Kit& k)
{
    kit = k;
    engine.setKit (kit);
    view->setKit (kit);
    pads->setKit (kit);
    updating = true;
    layoutBox->setCurrentIndex (kit.layout == slicer::Layout::chromatic ? 1 : 0);
    updating = false;
    padToControls();
}

void SlicerWindow::select (int pad)
{
    selected = std::clamp (pad, 0, slicer::numPads - 1);
    setBank (selected / slicer::padsPerBank);
    view->setSelected (selected);
    pads->setSelected (selected);
    padToControls();
}

void SlicerWindow::setBank (int b)
{
    bank = std::clamp (b, 0, 3);
    banks->button (bank)->setChecked (true);
    pads->setBank (bank);
    showStatus();
}

void SlicerWindow::padToControls()
{
    updating = true;
    const auto& p = kit.pads[(size_t) selected];
    const auto note = slicer::noteForPad (selected, kit.layout);
    padTitle->setText (p.used ? tr ("Pad %1 · %2 · %3 s").arg (padName (selected), noteName (note)).arg ((double) (p.end - p.start) / rate, 0, 'f', 3)
                              : tr ("Pad %1 · %2 · empty").arg (padName (selected), noteName (note)));
    gainBox->setValue (p.gainDb);
    panBox->setValue ((int) std::lround (p.pan * 100.0));
    pitchBox->setValue ((int) std::lround (p.pitch));
    reverseBox->setChecked (p.reverse);
    playBox->setCurrentIndex (p.play == slicer::Play::gate ? 1 : 0);
    attackBox->setValue (p.attack * 1000.0);
    releaseBox->setValue (p.release * 1000.0);
    chokeBox->setValue (p.choke);
    velocityBox->setValue ((int) std::lround (kit.velocity * 100.0));
    voicesBox->setValue (kit.voices);
    for (auto* w : std::initializer_list<QWidget*> { gainBox, panBox, pitchBox, reverseBox, playBox, attackBox, releaseBox, chokeBox })
        w->setEnabled (p.used);
    updating = false;
}

void SlicerWindow::padFromControls()
{
    if (updating)
        return;
    auto& p = kit.pads[(size_t) selected];
    p.gainDb = gainBox->value();
    p.pan = panBox->value() / 100.0;
    p.pitch = pitchBox->value();
    p.reverse = reverseBox->isChecked();
    p.play = playBox->currentIndex() == 1 ? slicer::Play::gate : slicer::Play::oneShot;
    p.attack = attackBox->value() / 1000.0;
    p.release = releaseBox->value() / 1000.0;
    p.choke = chokeBox->value();
    kit.velocity = velocityBox->value() / 100.0;
    kit.voices = voicesBox->value();
    setKit (kit);
}

void SlicerWindow::showStatus()
{
    QString text = tr ("Z X C V, A S D F, Q W E R, 1 2 3 4 play bank %1 from the bottom row · 5 to 8 choose banks")
                       .arg (QChar ('A' + bank));
    if (found > slicer::numPads)
        text = tr ("%1 slices found: the first 64 are on the pads (lower the sensitivity for fewer). ").arg (found) + text;
    status->setText (text);
}

void SlicerWindow::keyPressEvent (QKeyEvent* e)
{
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
    {
        QWidget::keyPressEvent (e);
        return;
    }
    if (e->isAutoRepeat())
        return;
    const auto code = scanCodeOf (e);
    for (int b = 0; b < 4; ++b)
        if (code == bankKeys[b])
        {
            setBank (b);
            select (b * slicer::padsPerBank + selected % slicer::padsPerBank);
            return;
        }
    for (int i = 0; i < 16; ++i)
        if (code == padKeys[i])
        {
            const auto pad = bank * slicer::padsPerBank + i;
            heldKeys.emplace_back (code, pad);
            select (pad);
            engine.hit (pad, 0.8f);
            return;
        }
    QWidget::keyPressEvent (e);
}

void SlicerWindow::keyReleaseEvent (QKeyEvent* e)
{
    if (e->isAutoRepeat())
        return;
    const auto code = scanCodeOf (e);
    for (auto it = heldKeys.begin(); it != heldKeys.end(); ++it)
        if (it->first == code)
        {
            engine.letGo (it->second);
            heldKeys.erase (it);
            return;
        }
    QWidget::keyReleaseEvent (e);
}

void SlicerWindow::releaseKeys()
{
    for (const auto& [code, pad] : heldKeys)
        engine.letGo (pad);
    heldKeys.clear();
}

void SlicerWindow::focusMoved (QWidget* old, QWidget* now)
{
    // Keys let go while the focus was elsewhere would stay down: release them.
    const bool was = old != nullptr && (old == this || isAncestorOf (old));
    const bool is = now != nullptr && (now == this || isAncestorOf (now));
    if (was && ! is)
        releaseKeys();
    if (is && ! was)
        Q_EMIT activated();
}

bool SlicerWindow::event (QEvent* e)
{
    // Docked or popped out, the editor's single-key shortcuts would take the pad keys.
    if (e->type() == QEvent::ShortcutOverride)
    {
        auto* k = static_cast<QKeyEvent*> (e);
        const auto code = scanCodeOf (k);
        bool ours = false;
        for (const auto key : padKeys)
            ours = ours || key == code;
        for (const auto key : bankKeys)
            ours = ours || key == code;
        if (ours && ! (k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
        {
            e->accept();
            return true;
        }
    }
    return QWidget::event (e);
}

bool SlicerWindow::eventFilter (QObject* o, QEvent* e)
{
    // Esc in a field goes back to playing.
    if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*> (e)->key() == Qt::Key_Escape)
    {
        pads->setFocus();
        return true;
    }
    return QWidget::eventFilter (o, e);
}

void SlicerWindow::hideEvent (QHideEvent* e)
{
    // Closed (not minimised with the main window): silent at once.
    QWidget::hideEvent (e);
    if (e->spontaneous())
        return;
    releaseKeys();
    engine.panic();
    Q_EMIT closed();
}
