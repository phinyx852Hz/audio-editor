// SPDX-License-Identifier: AGPL-3.0-only
#include "SamplerWindow.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QApplication>
#include <QHideEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QResizeEvent>
#include <QUrl>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    // Note names as Live and Push show them: C3 is MIDI note 60.
    QString noteName (int note)
    {
        static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return QString::fromLatin1 (names[note % 12]) + QString::number (note / 12 - 2);
    }

    // Synced LFO rates as note values, in beats a cycle.
    constexpr double lfoBeats[] { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };

    bool isBlack (int note)
    {
        const auto n = note % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    // The computer keyboard as Live's: by key position (PC scan codes), so any layout works.
    struct NoteKey { int scanCode, semitone; };
    constexpr NoteKey noteKeys[] {
        { 0x1E, 0 }, { 0x11, 1 }, { 0x1F, 2 }, { 0x12, 3 }, { 0x20, 4 }, { 0x21, 5 }, { 0x14, 6 }, { 0x22, 7 },
        { 0x15, 8 }, { 0x23, 9 }, { 0x16, 10 }, { 0x24, 11 }, { 0x25, 12 }, { 0x18, 13 }, { 0x26, 14 }, { 0x19, 15 }, { 0x27, 16 },
    };
    constexpr int keyZ = 0x2C, keyX = 0x2D, keyC = 0x2E, keyV = 0x2F;
    int scanCodeOf (const QKeyEvent* e) { return (int) e->nativeScanCode() - 8; }   // X11 and Wayland: evdev + 8
}

//==============================================================================
SampleView::SampleView (sampler::Engine& e, QWidget* parent) : QWidget (parent), engine (e)
{
    setFocusPolicy (Qt::StrongFocus);
    setMouseTracking (true);
    setToolTip (tr ("Drag the white lines to set where the sound starts and ends, and the shaded part to set the loop"));
}

void SampleView::setSample (sampler::SamplePtr s, double r)
{
    sample = std::move (s);
    rate = r;
    buildPeaks();
    update();
}

void SampleView::setReversed (bool r)
{
    if (r == reversed)
        return;
    reversed = r;
    buildPeaks();
    update();
}

void SampleView::setMarkers (Frame start, Frame end, Frame loopStart, Frame loopEnd, bool loop)
{
    markers[0] = start;
    markers[1] = end;
    markers[2] = loopStart;
    markers[3] = loopEnd;
    showLoop = loop;
    update();
}

double SampleView::xFor (double frame) const
{
    const auto n = sample != nullptr ? std::max<Frame> (1, sample->length()) : 1;
    return frame / (double) n * width();
}

Frame SampleView::frameAt (double x) const
{
    const auto n = sample != nullptr ? sample->length() : 0;
    return std::clamp<Frame> ((Frame) std::llround (x / std::max (1, width()) * (double) n), 0, n);
}

void SampleView::resizeEvent (QResizeEvent*)
{
    buildPeaks();
}

void SampleView::buildPeaks()
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
            const auto at = (size_t) (reversed ? n - 1 - i : i);
            const auto v = 0.5f * (sample->left[at] + sample->right[at]);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        peaks[(size_t) c] = { lo, hi };
    }
}

void SampleView::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
    if (sample == nullptr)
    {
        g.setPen (theme.color (QStringLiteral ("placeholderText")));
        g.drawText (rect(), Qt::AlignCenter, tr ("Send a sound here from the editor: Sample > Send to Simplest (F7), or drag an audio file here from Find or a file manager"));
        return;
    }
    const auto mid = height() / 2.0;
    const auto start = xFor ((double) markers[0]), end = xFor ((double) markers[1]);

    // Outside start..end is dimmed; the loop is shaded.
    if (showLoop)
    {
        auto loop = theme.color (QStringLiteral ("region"));
        loop.setAlpha (70);
        g.fillRect (QRectF (QPointF (xFor ((double) markers[2]), 0), QPointF (xFor ((double) markers[3]), height())), loop);
    }
    auto wave = theme.color (QStringLiteral ("waveform"));
    auto dim = wave;
    dim.setAlpha (70);
    for (size_t c = 0; c < peaks.size(); ++c)
    {
        const auto x = (double) c + 0.5;
        g.setPen ((x < start || x > end) ? dim : wave);
        g.drawLine (QPointF (x, mid - peaks[c].second * mid * 0.95), QPointF (x, mid - peaks[c].first * mid * 0.95));
    }
    g.setPen (QPen (theme.color (QStringLiteral ("centerLine")), 1.0));
    g.drawLine (QPointF (0, mid), QPointF (width(), mid));

    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.85);
    g.setFont (small);
    const auto text = theme.color (QStringLiteral ("windowText"));
    const auto drawMarker = [&] (double x, QColor colour, const QString& label, bool atTop)
    {
        g.setPen (QPen (colour, 2.0));
        g.drawLine (QPointF (x, 0), QPointF (x, height()));
        g.setPen (text);
        g.drawText (QRectF (x + 3, atTop ? 2 : height() - 16, 80, 14), Qt::AlignLeft | Qt::AlignVCenter, label);
    };
    drawMarker (start, theme.color (QStringLiteral ("windowText")), tr ("S"), true);
    drawMarker (end, theme.color (QStringLiteral ("windowText")), tr ("E"), true);
    if (showLoop)
    {
        const auto accent = theme.color (QStringLiteral ("marker"));
        drawMarker (xFor ((double) markers[2]), accent, tr ("L"), false);
        drawMarker (xFor ((double) markers[3]), accent, QString(), false);
    }

    // Where the voices are.
    g.setPen (QPen (theme.color (QStringLiteral ("cursor")), 1.5));
    for (const auto head : engine.playheads())
        if (head >= 0.0)
            g.drawLine (QPointF (xFor (head), 0), QPointF (xFor (head), height()));

    g.setPen (theme.color (QStringLiteral ("placeholderText")));
    g.drawText (QRectF (4, 2, width() - 8, 14), Qt::AlignRight | Qt::AlignVCenter,
                tr ("%1 s").arg ((double) (markers[1] - markers[0]) / rate, 0, 'f', 3));
}

void SampleView::mousePressEvent (QMouseEvent* e)
{
    if (sample == nullptr || e->button() != Qt::LeftButton)
        return;
    // The nearest marker within 8 px; the loop handles only while the loop shows.
    double best = 8.0;
    dragging = -1;
    for (int i = 0; i < (showLoop ? 4 : 2); ++i)
        if (const auto d = std::abs (xFor ((double) markers[i]) - e->position().x()); d <= best)
        {
            best = d;
            dragging = i;
        }
    // Away from the markers a click does nothing, so nothing jumps.
}

void SampleView::mouseMoveEvent (QMouseEvent* e)
{
    if (dragging < 0 || ! (e->buttons() & Qt::LeftButton))
    {
        bool near = false;
        for (int i = 0; i < (showLoop ? 4 : 2) && sample != nullptr; ++i)
            near = near || std::abs (xFor ((double) markers[i]) - e->position().x()) <= 8.0;
        setCursor (near ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    Q_EMIT markerMoved (dragging, frameAt (e->position().x()));
}

void SampleView::mouseReleaseEvent (QMouseEvent*)
{
    dragging = -1;
}

//==============================================================================
PianoKeys::PianoKeys (sampler::Engine& e, QWidget* parent) : QWidget (parent), engine (e)
{
    setFocusPolicy (Qt::StrongFocus);
    setMinimumHeight (60);
}

void PianoKeys::setLowestNote (int note)
{
    lowest = std::clamp (note - note % 12, 0, 96);
    update();
}

int PianoKeys::whiteCount() const
{
    int n = 0;
    for (int note = lowest; note < std::min (128, lowest + 48); ++note)
        n += isBlack (note) ? 0 : 1;
    return std::max (1, n);
}

QRectF PianoKeys::keyRect (int note) const
{
    const auto whiteWidth = (double) width() / whiteCount();
    int whites = 0;
    for (int n = lowest; n < note; ++n)
        whites += isBlack (n) ? 0 : 1;
    if (isBlack (note))
        return { whites * whiteWidth - whiteWidth * 0.3, 0.0, whiteWidth * 0.6, height() * 0.6 };
    return { whites * whiteWidth, 0.0, whiteWidth, (double) height() };
}

int PianoKeys::noteAt (QPointF p, float* velocity) const
{
    for (int pass = 0; pass < 2; ++pass)   // black keys sit on top
        for (int note = lowest; note < std::min (128, lowest + 48); ++note)
            if (isBlack (note) == (pass == 0) && keyRect (note).contains (p))
            {
                if (velocity != nullptr)
                    *velocity = (float) std::clamp (0.35 + 0.65 * (p.y() / keyRect (note).height()), 0.1, 1.0);
                return note;
            }
    return -1;
}

void PianoKeys::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto lit = theme.color (QStringLiteral ("highlight"));
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.75);
    g.setFont (small);
    for (int pass = 0; pass < 2; ++pass)
        for (int note = lowest; note < std::min (128, lowest + 48); ++note)
        {
            if (isBlack (note) != (pass == 1))
                continue;
            const auto r = keyRect (note).adjusted (0.5, 0.5, -0.5, -0.5);
            const bool on = engine.isSounding (note) || note == pressed;
            g.setPen (QPen (QColor (40, 40, 40), 1.0));
            g.setBrush (on ? lit : (isBlack (note) ? QColor (30, 30, 34) : QColor (225, 225, 228)));
            g.drawRoundedRect (r, 2, 2);
            if (! isBlack (note) && (note % 12 == 0 || note == root))
            {
                g.setPen (note == root ? theme.color (QStringLiteral ("marker")) : QColor (90, 90, 90));
                g.drawText (QRectF (r.left(), r.bottom() - 16, r.width(), 14), Qt::AlignCenter, noteName (note));
            }
        }
    if (hasFocus())
    {
        g.setPen (QPen (lit, 2.0));
        g.setBrush (Qt::NoBrush);
        g.drawRect (rect().adjusted (1, 1, -1, -1));
    }
}

void PianoKeys::mousePressEvent (QMouseEvent* e)
{
    float velocity = 1.0f;
    pressed = noteAt (e->position(), &velocity);
    if (pressed >= 0)
        Q_EMIT played (pressed, velocity);
    setFocus();
    update();
}

void PianoKeys::mouseMoveEvent (QMouseEvent* e)
{
    if (! (e->buttons() & Qt::LeftButton))
        return;
    float velocity = 1.0f;
    const auto note = noteAt (e->position(), &velocity);
    if (note == pressed)
        return;
    if (pressed >= 0)
        Q_EMIT released (pressed);
    pressed = note;
    if (pressed >= 0)
        Q_EMIT played (pressed, velocity);
    update();
}

void PianoKeys::mouseReleaseEvent (QMouseEvent*)
{
    if (pressed >= 0)
        Q_EMIT released (pressed);
    pressed = -1;
    update();
}

//==============================================================================
SamplerWindow::SamplerWindow (sampler::Engine& e, QWidget* parent)
    : QWidget (parent), engine (e)
{
    setFocusPolicy (Qt::StrongFocus);   // docked, it takes the playing keys when shown or clicked
    setWindowTitle (tr ("Simplest"));
    setObjectName (QStringLiteral ("SamplerWindow"));
    settings = engine.settings();

    title = new QLabel (tr ("No sound yet"), this);
    auto bold = title->font();
    bold.setBold (true);
    title->setFont (bold);
    view = new SampleView (engine, this);
    piano = new PianoKeys (engine, this);
    status = new QLabel (this);
    midiStatus = new QLabel (this);
    midiStatus->setAlignment (Qt::AlignRight | Qt::AlignVCenter);

    // The mode as two buttons, as Simpler's: each shows only the controls it has.
    modeButtons = new QButtonGroup (this);
    auto* classicButton = new QPushButton (tr ("&Classic"), this);
    classicButton->setToolTip (tr ("A pitched instrument: loops while a key is held and fades by the envelope"));
    auto* shotButton = new QPushButton (tr ("&1-Shot"), this);
    shotButton->setToolTip (tr ("Drum hits and phrases: plays the whole sound, with only fades"));
    for (auto* b : { classicButton, shotButton })
        b->setCheckable (true);
    modeButtons->addButton (classicButton, 0);
    modeButtons->addButton (shotButton, 1);
    auto* edit = new QToolButton (this);
    edit->setText (tr ("&Edit"));
    edit->setCheckable (true);
    edit->setToolTip (tr ("Show the rest: loop modes, pitch, filter type, key range, quality and the sample tools"));
    editButton = edit;
    loopBox = new QCheckBox (tr ("&Loop"), this);
    loopBox->setToolTip (tr ("Classic: keep playing the shaded part while the key is held"));
    snapBox = new QCheckBox (tr ("S&nap to zero"), this);
    snapBox->setChecked (true);
    snapBox->setToolTip (tr ("Markers land where the wave crosses zero, so starts and loops do not click"));
    followBox = new QCheckBox (tr ("&Follow editor"), this);
    followBox->setChecked (true);
    followBox->setToolTip (tr ("Edits you make to this sound in the editor reach Simplest straight away"));
    auto* refresh = new QPushButton (tr ("&Refresh"), this);
    refresh->setToolTip (tr ("Take the sound from the editor again (after Crop, the whole sound comes back)"));
    auto* midi = new QPushButton (tr ("&MIDI Inputs..."), this);

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
    auto notes = [this]
    {
        auto* b = new QComboBox (this);
        for (int n = 0; n < 128; ++n)
            b->addItem (noteName (n));
        return b;
    };
    gainBox = real (-48.0, 24.0, 0.5, 1, tr (" dB"));
    panBox = whole (-100, 100, 5, QString());
    panBox->setToolTip (tr ("-100 is left, 100 right"));
    transposeBox = whole (-48, 48, 1, tr (" st"));
    detuneBox = whole (-50, 50, 1, tr (" ct"));
    rootBox = notes();
    rootBox->setToolTip (tr ("The key that plays the sound at its own pitch"));
    voicesBox = whole (1, sampler::Engine::maxVoices, 1, QString());
    voicesBox->setToolTip (tr ("How many notes can sound at once; 1 is monophonic (with Legato and the held keys remembered)"));
    velocityBox = whole (0, 100, 5, tr (" %"));
    velocityBox->setToolTip (tr ("How much softer notes are quieter; 0% plays every note at full level"));
    attackBox = real (0.0, 10000.0, 5.0, 0, tr (" ms"));
    decayBox = real (1.0, 20000.0, 10.0, 0, tr (" ms"));
    sustainBox = whole (0, 100, 5, tr (" %"));
    releaseBox = real (1.0, 20000.0, 10.0, 0, tr (" ms"));
    fadeInBox = real (0.0, 2000.0, 1.0, 0, tr (" ms"));
    fadeOutBox = real (0.0, 2000.0, 1.0, 0, tr (" ms"));
    gateBox = new QComboBox (this);
    gateBox->addItems ({ tr ("Trigger"), tr ("Gate") });
    gateBox->setToolTip (tr ("Trigger plays to the end whatever the key does; Gate fades out over Fade out when the key is let go"));

    filterBox = new QCheckBox (tr ("Filter"), this);
    filterBox->setToolTip (tr ("Shape the tone with a filter on every note"));
    cutoffBox = whole (20, 20000, 50, tr (" Hz"));
    resonanceBox = whole (0, 100, 5, tr (" %"));
    filterTypeBox = new QComboBox (this);
    filterTypeBox->addItems ({ tr ("Low pass"), tr ("High pass"), tr ("Band pass"), tr ("Notch") });
    steepBox = new QCheckBox (tr ("24 dB"), this);
    steepBox->setToolTip (tr ("A steeper filter: 24 dB an octave instead of 12"));
    driveBox = real (0.0, 36.0, 1.0, 1, tr (" dB"));
    driveBox->setToolTip (tr ("Saturation before the filter"));
    keyFollowBox = whole (0, 100, 10, tr (" %"));
    keyFollowBox->setToolTip (tr ("How far the cutoff follows the key; at 100% it moves an octave for each octave played"));
    filterVelocityBox = whole (0, 100, 10, tr (" %"));
    filterVelocityBox->setToolTip (tr ("How far soft notes close the filter (up to four octaves), so harder playing is brighter"));

    loopModeBox = new QComboBox (this);
    loopModeBox->addItems ({ tr ("Continuous"), tr ("Alternate"), tr ("Once"), tr ("Until release"), tr ("Alternate until release") });
    loopModeBox->setToolTip (tr ("Continuous and Alternate (back and forth) loop for as long as the note sounds; Once goes round one time; "
                                 "the Until release modes stop looping when the key is let go and play on to the end"));
    crossfadeBox = real (0.0, 2000.0, 5.0, 0, tr (" ms"));
    crossfadeBox->setToolTip (tr ("Blend the loop's end into its start, so the seam does not click (forward loops)"));
    qualityBox = new QComboBox (this);
    qualityBox->addItems ({ tr ("Standard"), tr ("Vintage"), tr ("Turntable 45"), tr ("Turntable 78") });
    qualityBox->setToolTip (tr ("Vintage plays at 12 bits and 26 kHz as early samplers did; Turntable 45 and 78 go further, "
                                "as a record sampled fast and tuned down"));
    reverseBox = new QCheckBox (tr ("Re&verse"), this);
    reverseBox->setToolTip (tr ("Play the sound backwards (the sound itself is left as it is)"));
    fixedBox = new QCheckBox (tr ("Fi&xed pitch"), this);
    fixedBox->setToolTip (tr ("Every key plays the sound at its own pitch"));
    glideBox = real (0.0, 5000.0, 10.0, 0, tr (" ms"));
    glideBox->setToolTip (tr ("How long a note takes to slide from the last one; 0 is off"));
    fingeredBox = new QCheckBox (tr ("Fingered"), this);
    fingeredBox->setToolTip (tr ("Glide only between notes played legato (one held while the next starts)"));
    legatoBox = new QCheckBox (tr ("Legato"), this);
    legatoBox->setToolTip (tr ("With one voice: a key played over a held one changes the pitch without starting the sound again"));
    keyLowBox = notes();
    keyLowBox->setToolTip (tr ("The lowest key that plays"));
    keyHighBox = notes();
    keyHighBox->setToolTip (tr ("The highest key that plays"));
    auto* cropButton = new QPushButton (tr ("Cro&p"), this);
    cropButton->setToolTip (tr ("Keep only what is between S and E, as a copy (the editor's sound is left as it is)"));
    auto* normalizeButton = new QPushButton (tr ("Normali&ze"), this);
    normalizeButton->setToolTip (tr ("Set Gain so the loudest peak between S and E reaches 0 dB"));

    auto* top = new QHBoxLayout;
    top->addWidget (classicButton);
    top->addWidget (shotButton);
    top->addSpacing (12);
    top->addWidget (followBox);
    top->addStretch (1);
    top->addWidget (refresh);
    top->addWidget (midi);

    auto labelled = [this] (const QString& text, QWidget* w)
    {
        auto* box = new QWidget (this);
        auto* l = new QVBoxLayout (box);
        l->setContentsMargins (0, 0, 0, 0);
        l->setSpacing (2);
        auto* label = new QLabel (text, box);
        label->setBuddy (w);
        l->addWidget (label);
        l->addWidget (w);
        return box;
    };
    auto row = [this] (std::initializer_list<QWidget*> widgets)
    {
        auto* box = new QWidget (this);
        auto* l = new QHBoxLayout (box);
        l->setContentsMargins (0, 0, 0, 0);
        for (auto* w : widgets)
            l->addWidget (w);
        return box;
    };

    // One row, as Simpler's: the controls a sound needs most.
    envelopeRow = row ({ labelled (tr ("&Attack"), attackBox), labelled (tr ("Deca&y"), decayBox),
                         labelled (tr ("&Sustain"), sustainBox), labelled (tr ("&Release"), releaseBox) });
    fadeRow = row ({ labelled (tr ("P&lay"), gateBox), labelled (tr ("Fade &in"), fadeInBox), labelled (tr ("Fade o&ut"), fadeOutBox) });
    auto* filterRow = row ({ labelled (QString(), filterBox), labelled (tr ("Cutoff"), cutoffBox), labelled (tr ("Resonance"), resonanceBox) });
    auto* main = new QHBoxLayout;
    main->addWidget (labelled (tr ("&Gain"), gainBox));
    main->addWidget (labelled (tr ("&Transpose"), transposeBox));
    main->addWidget (labelled (tr ("V&oices"), voicesBox));
    main->addWidget (envelopeRow);
    main->addWidget (fadeRow);
    main->addSpacing (8);
    main->addWidget (filterRow);
    main->addStretch (1);
    main->addWidget (labelled (QString(), loopBox), 0, Qt::AlignBottom);
    main->addWidget (edit, 0, Qt::AlignBottom);

    // Edit's controls for the envelopes, the LFOs and how it plays.
    envelopeChoice = new QComboBox (this);
    envelopeChoice->addItems ({ tr ("Pitch"), tr ("Filter") });
    envelopeChoice->setToolTip (tr ("Which envelope to edit: Pitch moves the pitch, Filter the cutoff"));
    envOnBox = new QCheckBox (tr ("On"), this);
    envAttackBox = real (0.0, 10000.0, 5.0, 0, tr (" ms"));
    envDecayBox = real (0.0, 20000.0, 10.0, 0, tr (" ms"));
    envSustainBox = whole (0, 100, 5, tr (" %"));
    envReleaseBox = real (0.0, 20000.0, 10.0, 0, tr (" ms"));
    envCurveBox = whole (-100, 100, 10, tr (" %"));
    envCurveBox->setToolTip (tr ("Bends each part of the envelope: above 0 it moves fast first, below 0 slowly first"));
    envAmountBox = real (-48.0, 48.0, 1.0, 1, QString());
    envAmountBox->setToolTip (tr ("How far the envelope moves at its top: semitones for Pitch, octaves for Filter; 0 does nothing"));
    envVelocityBox = whole (0, 100, 10, tr (" %"));
    envVelocityBox->setToolTip (tr ("How much less soft notes move it"));

    lfoChoice = new QComboBox (this);
    lfoChoice->addItems ({ tr ("LFO 1"), tr ("LFO 2") });
    lfoChoice->setToolTip (tr ("Which LFO to edit; both run at once, shared by every note"));
    waveBox = new QComboBox (this);
    waveBox->addItems ({ tr ("Sine"), tr ("Triangle"), tr ("Saw"), tr ("Pulse"), tr ("Ramp"), tr ("Log"), tr ("S&&H 1"), tr ("S&&H 2") });
    waveBox->setToolTip (tr ("S&H 1 steps to a new random value each cycle; S&H 2 alternates random highs and lows"));
    shapeBox = whole (0, 100, 5, tr (" %"));
    shapeBox->setToolTip (tr ("Changes the wave: harmonics on the sine, a trapezoid from the triangle, ramp down to up on the saw, "
                              "the pulse's width, a pause before the ramp, the log's bend, glides between random steps"));
    syncBox = new QCheckBox (tr ("Sync"), this);
    syncBox->setToolTip (tr ("Count the rate in beats at Tempo instead of in Hz"));
    rateBox = real (0.01, 50.0, 0.1, 2, tr (" Hz"));
    beatsBox = new QComboBox (this);
    beatsBox->addItems ({ tr ("4 bars"), tr ("2 bars"), tr ("1 bar"), tr ("1/2"), tr ("1/4"), tr ("1/8"), tr ("1/16"), tr ("1/32") });   // as lfoBeats
    retriggerBox = new QComboBox (this);
    retriggerBox->addItems ({ tr ("Off"), tr ("First note"), tr ("Each note") });
    retriggerBox->setToolTip (tr ("Start the cycle again at every note, or only when no other key is held"));
    phaseBox = whole (0, 100, 5, tr (" %"));
    phaseBox->setToolTip (tr ("Where a cycle starts again"));
    randomBox = new QCheckBox (tr ("Random"), this);
    randomBox->setToolTip (tr ("Start each cycle at a random place"));
    modWheelBox = whole (0, 100, 10, tr (" %"));
    modWheelBox->setToolTip (tr ("How far the mod wheel lets it in: at 100% it is silent until the wheel goes up"));
    toPitchBox = real (0.0, 24.0, 0.1, 1, tr (" st"));
    toFilterBox = real (0.0, 8.0, 0.1, 1, tr (" oct"));
    toVolumeBox = whole (0, 100, 5, tr (" %"));
    toPanBox = whole (0, 100, 5, tr (" %"));
    bendBox = real (0.0, 24.0, 1.0, 0, tr (" st"));
    bendBox->setToolTip (tr ("How far a full pitch bend moves the pitch"));
    tempoBox = real (20.0, 300.0, 1.0, 1, tr (" BPM"));
    tempoBox->setToolTip (tr ("The tempo synced LFOs count beats at"));

    // Edit: everything else, as tabs one row high, so Simplest stays a short strip.
    auto* tabs = new QTabWidget (this);
    tabs->setDocumentMode (true);
    editPanel = tabs;
    auto page = [&] (const QString& name, std::initializer_list<QWidget*> widgets)
    {
        auto* r = row (widgets);
        static_cast<QHBoxLayout*> (r->layout())->addStretch (1);
        auto* w = new QWidget (this);
        auto* l = new QVBoxLayout (w);   // at the top, however tall the LFOs' two rows make the tabs
        l->setContentsMargins (4, 4, 4, 0);
        l->addWidget (r);
        l->addStretch (1);
        tabs->addTab (w, name);
        return w;
    };
    loopRow = row ({ labelled (tr ("Loop mode"), loopModeBox), labelled (tr ("Crossfade"), crossfadeBox) });
    page (tr ("Sample"), { labelled (QString(), reverseBox), labelled (QString(), snapBox), labelled (QString(), cropButton),
                           labelled (QString(), normalizeButton), loopRow, labelled (tr ("&Quality"), qualityBox) });
    page (tr ("Pitch"), { labelled (tr ("Roo&t key"), rootBox), labelled (tr ("&Detune"), detuneBox), labelled (tr ("Glide"), glideBox),
                          labelled (QString(), fingeredBox), labelled (QString(), legatoBox), labelled (QString(), fixedBox),
                          labelled (tr ("Bend"), bendBox) });
    page (tr ("Filter"), { labelled (tr ("Type"), filterTypeBox), labelled (QString(), steepBox), labelled (tr ("Drive"), driveBox),
                           labelled (tr ("&Key follow"), keyFollowBox), labelled (tr ("Velocity"), filterVelocityBox) });
    page (tr ("Envelopes"), { labelled (QString(), envelopeChoice), labelled (QString(), envOnBox), labelled (tr ("Attack"), envAttackBox),
                              labelled (tr ("Decay"), envDecayBox), labelled (tr ("Sustain"), envSustainBox),
                              labelled (tr ("Release"), envReleaseBox), labelled (tr ("Curve"), envCurveBox),
                              labelled (tr ("Amount"), envAmountBox), labelled (tr ("Velocity"), envVelocityBox) });
    rateField = labelled (tr ("Rate"), rateBox);
    beatsField = labelled (tr ("Rate"), beatsBox);
    auto* lfoPage = new QWidget (this);
    auto* lfoRows = new QVBoxLayout (lfoPage);
    lfoRows->setContentsMargins (4, 4, 4, 0);
    lfoRows->setSpacing (2);
    auto* waveRow = row ({ labelled (QString(), lfoChoice), labelled (tr ("Wave"), waveBox), labelled (tr ("Shape"), shapeBox),
                           labelled (QString(), syncBox), rateField, beatsField, labelled (tr ("Retrigger"), retriggerBox),
                           labelled (tr ("Phase"), phaseBox), labelled (QString(), randomBox) });
    auto* toRow = row ({ labelled (tr ("To pitch"), toPitchBox), labelled (tr ("To cutoff"), toFilterBox), labelled (tr ("To volume"), toVolumeBox),
                         labelled (tr ("To pan"), toPanBox), labelled (tr ("Mod wheel"), modWheelBox) });
    for (auto* r : { waveRow, toRow })
    {
        static_cast<QHBoxLayout*> (r->layout())->addStretch (1);
        lfoRows->addWidget (r);
    }
    tabs->addTab (lfoPage, tr ("LFOs"));
    page (tr ("Play"), { labelled (tr ("Lo&west"), keyLowBox), labelled (tr ("&Highest"), keyHighBox),
                         labelled (tr ("Velocity"), velocityBox), labelled (tr ("Pan"), panBox), labelled (tr ("Tempo"), tempoBox) });
    editPanel->hide();

    status->setWordWrap (true);
    midiStatus->setAlignment (Qt::AlignLeft | Qt::AlignVCenter);
    midiStatus->setWordWrap (true);

    // The sound on one side and its controls on the other along the bottom of the window;
    // one above the other in a narrow dock or window (see resizeEvent).
    auto* soundSide = new QVBoxLayout;
    soundSide->addWidget (title);
    soundSide->addLayout (top);
    soundSide->addWidget (view, 1);
    auto* controlSide = new QVBoxLayout;
    controlSide->addLayout (main);
    controlSide->addWidget (editPanel);
    controlSide->addWidget (piano);
    piano->hide();   // the computer keyboard and MIDI play it; the piano stays for later
    controlSide->addWidget (status);
    controlSide->addWidget (midiStatus);
    controlSide->addStretch (0);
    columns = new QBoxLayout (QBoxLayout::TopToBottom, this);
    columns->addLayout (soundSide, 1);
    columns->addLayout (controlSide, 0);
    setAcceptDrops (true);

    toControls();
    for (auto* w : findChildren<QWidget*>())
        if (qobject_cast<QAbstractSpinBox*> (w) != nullptr || qobject_cast<QComboBox*> (w) != nullptr)
            w->installEventFilter (this);

    connect (view, &SampleView::markerMoved, this, &SamplerWindow::markerMoved);
    connect (piano, &PianoKeys::played, this, [this] (int note, float velocity) { engine.noteOn (note, velocity); });
    connect (piano, &PianoKeys::released, this, [this] (int note) { engine.noteOff (note); });
    connect (refresh, &QPushButton::clicked, this, &SamplerWindow::refreshRequested);
    connect (midi, &QPushButton::clicked, this, &SamplerWindow::midiInputsRequested);
    connect (modeButtons, &QButtonGroup::idClicked, this, &SamplerWindow::fromControls);
    connect (edit, &QToolButton::toggled, this, [this] (bool on)
    {
        editPanel->setVisible (on);
        adjustSize();
    });
    connect (reverseBox, &QCheckBox::toggled, this, &SamplerWindow::reverse);
    for (auto* b : { envelopeChoice, lfoChoice })
        connect (b, &QComboBox::currentIndexChanged, this, &SamplerWindow::toControls);
    for (auto* b : { waveBox, beatsBox, retriggerBox })
        connect (b, &QComboBox::currentIndexChanged, this, &SamplerWindow::fromControls);
    for (auto* b : { envOnBox, syncBox, randomBox })
        connect (b, &QCheckBox::toggled, this, &SamplerWindow::fromControls);
    for (auto* b : { envAttackBox, envDecayBox, envReleaseBox, envAmountBox, rateBox, toPitchBox, toFilterBox, bendBox, tempoBox })
        connect (b, &QDoubleSpinBox::valueChanged, this, &SamplerWindow::fromControls);
    for (auto* b : { envSustainBox, envCurveBox, envVelocityBox, shapeBox, phaseBox, modWheelBox, toVolumeBox, toPanBox })
        connect (b, &QSpinBox::valueChanged, this, &SamplerWindow::fromControls);
    connect (cropButton, &QPushButton::clicked, this, &SamplerWindow::crop);
    connect (normalizeButton, &QPushButton::clicked, this, &SamplerWindow::normalize);
    for (auto* b : { rootBox, gateBox, filterTypeBox, loopModeBox, qualityBox, keyLowBox, keyHighBox })
        connect (b, &QComboBox::currentIndexChanged, this, &SamplerWindow::fromControls);
    for (auto* b : { loopBox, snapBox, filterBox, steepBox, fixedBox, fingeredBox, legatoBox })
        connect (b, &QCheckBox::toggled, this, &SamplerWindow::fromControls);
    for (auto* b : { gainBox, attackBox, decayBox, releaseBox, fadeInBox, fadeOutBox, driveBox, crossfadeBox, glideBox })
        connect (b, &QDoubleSpinBox::valueChanged, this, &SamplerWindow::fromControls);
    for (auto* b : { panBox, transposeBox, detuneBox, voicesBox, velocityBox, sustainBox, cutoffBox, resonanceBox, keyFollowBox, filterVelocityBox })
        connect (b, &QSpinBox::valueChanged, this, &SamplerWindow::fromControls);

    // The playheads and lit keys follow the sound.
    redraw.setInterval (33);
    connect (&redraw, &QTimer::timeout, this, [this]
    {
        view->update();
        piano->update();
    });
    redraw.start();
    connect (qApp, &QApplication::focusChanged, this, &SamplerWindow::focusMoved);
    piano->setLowestNote (12 * (octave + 2) - 12);
    showStatus();
    adjustSize();
    view->setFocus();
}

void SamplerWindow::setSample (sampler::SamplePtr s, double r, const QString& n, bool keepMarkers)
{
    const auto length = s != nullptr ? s->length() : 0;
    const bool fits = keepMarkers && settings.end > 0 && settings.end <= length;
    if (! fits)
    {
        settings.start = 0;
        settings.end = length;
        settings.loopStart = 0;
        settings.loopEnd = length;
    }
    rate = r;
    name = n;
    engine.setSample (s);
    engine.setSettings (settings);
    view->setSample (s, rate);
    title->setText (tr ("%1   %2 s").arg (name).arg ((double) length / rate, 0, 'f', 3));
    setWindowTitle (tr ("Simplest: %1").arg (name));
    toControls();
}

bool SamplerWindow::followsEditor() const { return followBox->isChecked(); }

void SamplerWindow::setMidiStatus (const QString& text) { midiStatus->setText (text); }

void SamplerWindow::toControls()
{
    updating = true;
    const bool classic = settings.mode == sampler::Mode::classic;
    modeButtons->button (classic ? 0 : 1)->setChecked (true);
    loopBox->setChecked (settings.loop);
    gainBox->setValue (settings.gainDb);
    panBox->setValue ((int) std::lround (settings.pan * 100.0));
    transposeBox->setValue ((int) std::lround (settings.transpose));
    detuneBox->setValue ((int) std::lround (settings.detune));
    rootBox->setCurrentIndex (std::clamp (settings.rootNote, 0, 127));
    voicesBox->setValue (settings.voices);
    velocityBox->setValue ((int) std::lround (settings.velocity * 100.0));
    attackBox->setValue (settings.attack * 1000.0);
    decayBox->setValue (settings.decay * 1000.0);
    sustainBox->setValue ((int) std::lround (settings.sustain * 100.0));
    releaseBox->setValue (settings.release * 1000.0);
    fadeInBox->setValue (settings.fadeIn * 1000.0);
    fadeOutBox->setValue (settings.fadeOut * 1000.0);
    gateBox->setCurrentIndex (settings.gate ? 1 : 0);
    filterBox->setChecked (settings.filter);
    cutoffBox->setValue ((int) std::lround (settings.cutoff));
    resonanceBox->setValue ((int) std::lround (settings.resonance * 100.0));
    filterTypeBox->setCurrentIndex ((int) settings.filterType);
    steepBox->setChecked (settings.steep);
    driveBox->setValue (settings.drive);
    keyFollowBox->setValue ((int) std::lround (settings.keyFollow * 100.0));
    filterVelocityBox->setValue ((int) std::lround (settings.filterVelocity * 100.0));
    loopModeBox->setCurrentIndex ((int) settings.loopMode);
    crossfadeBox->setValue (settings.loopCrossfade * 1000.0);
    qualityBox->setCurrentIndex ((int) settings.quality);
    reverseBox->setChecked (settings.reverse);
    fixedBox->setChecked (settings.fixedPitch);
    glideBox->setValue (settings.glide * 1000.0);
    fingeredBox->setChecked (settings.fingered);
    legatoBox->setChecked (settings.legato);
    keyLowBox->setCurrentIndex (std::clamp (settings.keyLow, 0, 127));
    keyHighBox->setCurrentIndex (std::clamp (settings.keyHigh, 0, 127));
    const bool pitchEnvelope = envelopeChoice->currentIndex() == 0;
    const auto& env = pitchEnvelope ? settings.pitchEnvelope : settings.filterEnvelope;
    envOnBox->setChecked (env.on);
    envAttackBox->setValue (env.attack * 1000.0);
    envDecayBox->setValue (env.decay * 1000.0);
    envSustainBox->setValue ((int) std::lround (env.sustain * 100.0));
    envReleaseBox->setValue (env.release * 1000.0);
    envCurveBox->setValue ((int) std::lround (env.curve * 100.0));
    envAmountBox->setRange (pitchEnvelope ? -48.0 : -8.0, pitchEnvelope ? 48.0 : 8.0);
    envAmountBox->setSuffix (pitchEnvelope ? tr (" st") : tr (" oct"));
    envAmountBox->setValue (env.amount);
    envVelocityBox->setValue ((int) std::lround (env.velocity * 100.0));
    const auto& lfo = settings.lfo[(size_t) std::clamp (lfoChoice->currentIndex(), 0, 1)];
    waveBox->setCurrentIndex ((int) lfo.wave);
    shapeBox->setValue ((int) std::lround (lfo.shape * 100.0));
    syncBox->setChecked (lfo.sync);
    rateBox->setValue (lfo.rate);
    int nearest = 4;
    for (int i = 0; i < (int) std::size (lfoBeats); ++i)
        if (std::abs (std::log2 (lfoBeats[i] / std::max (lfo.beats, 1.0e-3))) < std::abs (std::log2 (lfoBeats[nearest] / std::max (lfo.beats, 1.0e-3))))
            nearest = i;
    beatsBox->setCurrentIndex (nearest);
    retriggerBox->setCurrentIndex ((int) lfo.retrigger);
    phaseBox->setValue ((int) std::lround (lfo.phase * 100.0));
    randomBox->setChecked (lfo.randomPhase);
    modWheelBox->setValue ((int) std::lround (lfo.modWheel * 100.0));
    toPitchBox->setValue (lfo.toPitch);
    toFilterBox->setValue (lfo.toFilter);
    toVolumeBox->setValue ((int) std::lround (lfo.toVolume * 100.0));
    toPanBox->setValue ((int) std::lround (lfo.toPan * 100.0));
    bendBox->setValue (settings.bendRange);
    tempoBox->setValue (settings.tempo);
    updating = false;

    for (auto* w : std::initializer_list<QWidget*> { envAttackBox, envDecayBox, envSustainBox, envReleaseBox, envCurveBox, envAmountBox, envVelocityBox })
        w->setEnabled (env.on);
    rateField->setVisible (! lfo.sync);
    beatsField->setVisible (lfo.sync);
    phaseBox->setEnabled (! lfo.randomPhase);

    // Only what the mode has, as Simpler: Classic has the envelope and loop, 1-Shot its fades.
    loopBox->setVisible (classic);
    envelopeRow->setVisible (classic);
    fadeRow->setVisible (! classic);
    loopRow->setEnabled (classic && settings.loop);
    for (auto* w : std::initializer_list<QWidget*> { cutoffBox, resonanceBox, filterTypeBox, steepBox, driveBox, keyFollowBox, filterVelocityBox })
        w->setEnabled (settings.filter);
    fingeredBox->setEnabled (settings.glide > 0.0);
    legatoBox->setEnabled (settings.voices == 1);
    piano->setRootNote (settings.rootNote);
    view->setReversed (settings.reverse);
    view->setMarkers (settings.start, settings.end, settings.loopStart, settings.loopEnd, classic && settings.loop);
}

void SamplerWindow::fromControls()
{
    if (updating)
        return;
    settings.mode = modeButtons->checkedId() == 1 ? sampler::Mode::oneShot : sampler::Mode::classic;
    settings.loop = loopBox->isChecked();
    settings.gainDb = gainBox->value();
    settings.pan = panBox->value() / 100.0;
    settings.transpose = transposeBox->value();
    settings.detune = detuneBox->value();
    settings.rootNote = rootBox->currentIndex();
    settings.voices = voicesBox->value();
    settings.velocity = velocityBox->value() / 100.0;
    settings.attack = attackBox->value() / 1000.0;
    settings.decay = decayBox->value() / 1000.0;
    settings.sustain = sustainBox->value() / 100.0;
    settings.release = releaseBox->value() / 1000.0;
    settings.fadeIn = fadeInBox->value() / 1000.0;
    settings.fadeOut = fadeOutBox->value() / 1000.0;
    settings.gate = gateBox->currentIndex() == 1;
    settings.filter = filterBox->isChecked();
    settings.cutoff = cutoffBox->value();
    settings.resonance = resonanceBox->value() / 100.0;
    settings.filterType = (sampler::FilterType) filterTypeBox->currentIndex();
    settings.steep = steepBox->isChecked();
    settings.drive = driveBox->value();
    settings.keyFollow = keyFollowBox->value() / 100.0;
    settings.filterVelocity = filterVelocityBox->value() / 100.0;
    settings.loopMode = (sampler::LoopMode) loopModeBox->currentIndex();
    settings.loopCrossfade = crossfadeBox->value() / 1000.0;
    settings.quality = (sampler::Quality) qualityBox->currentIndex();
    settings.fixedPitch = fixedBox->isChecked();
    settings.glide = glideBox->value() / 1000.0;
    settings.fingered = fingeredBox->isChecked();
    settings.legato = legatoBox->isChecked();
    settings.keyLow = keyLowBox->currentIndex();
    settings.keyHigh = std::max (settings.keyLow, keyHighBox->currentIndex());
    auto& env = envelopeChoice->currentIndex() == 0 ? settings.pitchEnvelope : settings.filterEnvelope;
    env.on = envOnBox->isChecked();
    env.attack = envAttackBox->value() / 1000.0;
    env.decay = envDecayBox->value() / 1000.0;
    env.sustain = envSustainBox->value() / 100.0;
    env.release = envReleaseBox->value() / 1000.0;
    env.curve = envCurveBox->value() / 100.0;
    env.amount = envAmountBox->value();
    env.velocity = envVelocityBox->value() / 100.0;
    auto& lfo = settings.lfo[(size_t) std::clamp (lfoChoice->currentIndex(), 0, 1)];
    lfo.wave = (sampler::Wave) waveBox->currentIndex();
    lfo.shape = shapeBox->value() / 100.0;
    lfo.sync = syncBox->isChecked();
    lfo.rate = rateBox->value();
    lfo.beats = lfoBeats[std::clamp (beatsBox->currentIndex(), 0, (int) std::size (lfoBeats) - 1)];
    lfo.retrigger = (sampler::Retrigger) retriggerBox->currentIndex();
    lfo.phase = phaseBox->value() / 100.0;
    lfo.randomPhase = randomBox->isChecked();
    lfo.modWheel = modWheelBox->value() / 100.0;
    lfo.toPitch = toPitchBox->value();
    lfo.toFilter = toFilterBox->value();
    lfo.toVolume = toVolumeBox->value() / 100.0;
    lfo.toPan = toPanBox->value() / 100.0;
    settings.bendRange = bendBox->value();
    settings.tempo = tempoBox->value();
    engine.setSettings (settings);
    toControls();
}

// Reverse leaves the sound as it is and plays it from the end; the markers are
// turned round with it, so the same part plays (backwards).
void SamplerWindow::reverse (bool on)
{
    if (updating || on == settings.reverse)
        return;
    const auto s = engine.sample();
    if (s != nullptr)
    {
        const auto n = s->length();
        const auto start = settings.start, end = settings.end, loopStart = settings.loopStart, loopEnd = settings.loopEnd;
        settings.start = n - end;
        settings.end = n - start;
        settings.loopStart = n - loopEnd;
        settings.loopEnd = n - loopStart;
    }
    settings.reverse = on;
    engine.setSettings (settings);
    toControls();
}

// Crop keeps a copy of what is between S and E; the editor's sound is not touched,
// so Follow editor goes off until Refresh brings the whole sound back.
void SamplerWindow::crop()
{
    const auto s = engine.sample();
    if (s == nullptr || (settings.start <= 0 && settings.end >= s->length()))
        return;
    const auto n = s->length();
    const auto from = settings.reverse ? n - settings.end : settings.start;
    const auto to = settings.reverse ? n - settings.start : settings.end;
    auto cropped = std::make_shared<sampler::Sample>();
    cropped->left.assign (s->left.begin() + from, s->left.begin() + to);
    cropped->right.assign (s->right.begin() + from, s->right.begin() + to);
    const auto shift = settings.start;
    settings.loopStart -= shift;
    settings.loopEnd -= shift;
    settings.end -= shift;
    settings.start = 0;
    followBox->setChecked (false);
    setSample (cropped, rate, name.endsWith (tr (" (cropped)")) ? name : name + tr (" (cropped)"), true);
}

void SamplerWindow::normalize()
{
    const auto s = engine.sample();
    if (s == nullptr)
        return;
    const auto n = s->length();
    const auto from = std::clamp<Frame> (settings.reverse ? n - settings.end : settings.start, 0, n);
    const auto to = std::clamp<Frame> (settings.reverse ? n - settings.start : settings.end, from, n);
    float peak = 0.0f;
    for (auto i = from; i < to; ++i)
        peak = std::max ({ peak, std::abs (s->left[(size_t) i]), std::abs (s->right[(size_t) i]) });
    if (peak <= 0.0f)
        return;
    gainBox->setValue (-20.0 * std::log10 ((double) peak));   // within the box's range
}

Frame SamplerWindow::snap (Frame f) const
{
    const auto s = engine.sample();
    if (s == nullptr || ! snapBox->isChecked())
        return f;
    // The nearest rising zero crossing within 10 ms.
    const auto reach = (Frame) (rate * 0.01);
    const auto n = s->length();
    auto value = [&] (Frame i) { return s->left[(size_t) i] + s->right[(size_t) i]; };
    for (Frame d = 0; d <= reach; ++d)
        for (const auto i : { f + d, f - d })
            if (i > 0 && i < n && value (i - 1) <= 0.0f && value (i) > 0.0f)
                return i;
    return f;
}

void SamplerWindow::markerMoved (int which, Frame position)
{
    const auto s = engine.sample();
    if (s == nullptr)
        return;
    const auto n = s->length();
    position = which == 1 || which == 3 ? (position >= n ? n : snap (position)) : snap (position);
    const auto gap = (Frame) (rate * 0.002);   // markers stay at least 2 ms apart
    // A loop that ran from the start (or to the end) keeps doing so.
    const bool loopAtStart = settings.loopStart == settings.start, loopAtEnd = settings.loopEnd == settings.end;
    switch (which)
    {
        case 0: settings.start = std::clamp<Frame> (position, 0, settings.end - gap); break;
        case 1: settings.end = std::clamp<Frame> (position, settings.start + gap, n); break;
        case 2: settings.loopStart = std::clamp<Frame> (position, settings.start, settings.loopEnd - gap); break;
        default: settings.loopEnd = std::clamp<Frame> (position, settings.loopStart + gap, settings.end); break;
    }
    if (which == 0 && loopAtStart)
        settings.loopStart = settings.start;
    if (which == 1 && loopAtEnd)
        settings.loopEnd = settings.end;
    settings.loopStart = std::clamp<Frame> (settings.loopStart, settings.start, settings.end - gap);
    settings.loopEnd = std::clamp<Frame> (settings.loopEnd, settings.loopStart + gap, settings.end);
    engine.setSettings (settings);
    toControls();
}

void SamplerWindow::showStatus()
{
    status->setText (tr ("Keys A to K play from %1 · Z/X octave · C/V velocity %2 · Esc back to playing")
                         .arg (noteName (12 * (octave + 2))).arg (keyVelocity));
}

int SamplerWindow::noteForKey (const QKeyEvent* e) const
{
    const auto code = scanCodeOf (e);
    for (const auto& k : noteKeys)
        if (k.scanCode == code)
            return std::clamp (12 * (octave + 2) + k.semitone, 0, 127);
    return -1;
}

void SamplerWindow::keyPressEvent (QKeyEvent* e)
{
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
    {
        QWidget::keyPressEvent (e);
        return;
    }
    if (e->isAutoRepeat())
        return;
    const auto code = scanCodeOf (e);
    if (code == keyZ || code == keyX)
    {
        octave = std::clamp (octave + (code == keyX ? 1 : -1), -2, 8);
        piano->setLowestNote (12 * (octave + 2) - 12);
        showStatus();
        return;
    }
    if (code == keyC || code == keyV)
    {
        keyVelocity = std::clamp (keyVelocity + (code == keyV ? 20 : -20), 1, 127);
        if (keyVelocity > 100 && keyVelocity < 120)
            keyVelocity = code == keyV ? 127 : 100;
        showStatus();
        return;
    }
    if (const auto note = noteForKey (e); note >= 0)
    {
        heldKeys.emplace_back (code, note);
        engine.noteOn (note, (float) keyVelocity / 127.0f);
        return;
    }
    QWidget::keyPressEvent (e);
}

void SamplerWindow::keyReleaseEvent (QKeyEvent* e)
{
    if (e->isAutoRepeat())
        return;
    const auto code = scanCodeOf (e);
    for (auto it = heldKeys.begin(); it != heldKeys.end(); ++it)
        if (it->first == code)
        {
            engine.noteOff (it->second);
            heldKeys.erase (it);
            return;
        }
    QWidget::keyReleaseEvent (e);
}

void SamplerWindow::resizeEvent (QResizeEvent* e)
{
    // Some room either way, so it does not flip back and forth at one width.
    const auto direction = columns->direction();
    if (direction == QBoxLayout::TopToBottom && width() >= 980)
        columns->setDirection (QBoxLayout::LeftToRight);
    else if (direction == QBoxLayout::LeftToRight && width() < 900)
        columns->setDirection (QBoxLayout::TopToBottom);
    QWidget::resizeEvent (e);
}

void SamplerWindow::dragEnterEvent (QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() && e->mimeData()->urls().first().isLocalFile())
        e->acceptProposedAction();
}

void SamplerWindow::dropEvent (QDropEvent* e)
{
    if (! e->mimeData()->hasUrls() || ! e->mimeData()->urls().first().isLocalFile())
        return;
    e->acceptProposedAction();
    const auto path = e->mimeData()->urls().first().toLocalFile();
    QTimer::singleShot (0, this, [this, path] { Q_EMIT fileDropped (path); });
}

void SamplerWindow::releaseKeys()
{
    for (const auto& [code, note] : heldKeys)
        engine.noteOff (note);
    heldKeys.clear();
}

void SamplerWindow::focusMoved (QWidget* old, QWidget* now)
{
    // Keys let go while the focus was elsewhere would stay down: release them.
    const bool was = old != nullptr && (old == this || isAncestorOf (old));
    const bool is = now != nullptr && (now == this || isAncestorOf (now));
    if (was && ! is)
        releaseKeys();
    if (is && ! was)
        Q_EMIT activated();
}

bool SamplerWindow::event (QEvent* e)
{
    // Docked or popped out, the editor's single-key shortcuts would take the playing keys.
    if (e->type() == QEvent::ShortcutOverride)
    {
        auto* k = static_cast<QKeyEvent*> (e);
        const auto code = scanCodeOf (k);
        const bool ours = noteForKey (k) >= 0 || code == keyZ || code == keyX || code == keyC || code == keyV;
        if (ours && ! (k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
        {
            e->accept();
            return true;
        }
    }
    return QWidget::event (e);
}

bool SamplerWindow::eventFilter (QObject* o, QEvent* e)
{
    // Esc in a field goes back to playing.
    if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*> (e)->key() == Qt::Key_Escape)
    {
        view->setFocus();
        return true;
    }
    return QWidget::eventFilter (o, e);
}

void SamplerWindow::hideEvent (QHideEvent* e)
{
    // Closed (not minimised with the main window): silent at once.
    QWidget::hideEvent (e);
    if (e->spontaneous())
        return;
    releaseKeys();
    engine.panic();
    Q_EMIT closed();
}
