// SPDX-License-Identifier: AGPL-3.0-only
#include "SlicerWindow.h"
#include "Settings.h"
#include "SlicerKit.h"
#include "Potentiometer.h"
#include "Theme.h"
#include "../core/TimeStretch.h"
#include "../engine/AudioEngine.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QApplication>
#include <QHideEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QTimer>
#include <QUrl>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QIcon>
#include <QSignalBlocker>
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
#include <tuple>

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

    // What a pad shows: its name, else its own sound's file, else "Slice".
    QString soundName (const slicer::Pad& p)
    {
        if (! p.name.empty())
            return QString::fromStdString (p.name);
        if (p.layerCount() > 1)
            return SlicerWindow::tr ("Multi");
        for (const auto& l : p.layers)
            if (l.used)
                return l.sound == nullptr ? SlicerWindow::tr ("Slice")
                                          : (l.path.empty() ? SlicerWindow::tr ("Sound") : QFileInfo (QString::fromStdString (l.path)).completeBaseName());
        return {};
    }

    // Sixteen pad colours, as Cubase's Drum Machine offers.
    QColor padColour (int c)
    {
        static const QColor colours[slicer::numColours] {
            { 214, 69, 65 }, { 230, 126, 34 }, { 241, 196, 15 }, { 160, 196, 60 }, { 46, 170, 90 }, { 26, 188, 156 },
            { 52, 172, 224 }, { 52, 120, 220 }, { 92, 96, 220 }, { 142, 84, 210 }, { 190, 80, 190 }, { 232, 90, 150 },
            { 150, 110, 80 }, { 120, 130, 140 }, { 200, 200, 200 }, { 90, 90, 96 } };
        return c >= 1 && c <= slicer::numColours ? colours[c - 1] : QColor();
    }

    // A bank as a 4x4 of dots (pad 1 bottom left), its letter over it: bit i a pad that holds
    // a sound, bit 16 + i one sounding.
    QIcon bankIcon (uint32_t look, QChar letter)
    {
        QPixmap pix (44, 44);
        pix.fill (Qt::transparent);
        QPainter g (&pix);
        g.setRenderHint (QPainter::Antialiasing);
        const auto lit = Theme::current().color (QStringLiteral ("highlight"));
        for (int i = 0; i < slicer::padsPerBank; ++i)
        {
            const auto row = i / 4, column = i % 4;
            const QRectF r (2 + column * 10, 2 + (3 - row) * 10, 8, 8);
            const bool sounding = (look >> (i + 16)) & 1u, holds = (look >> i) & 1u;
            g.fillRect (r, sounding ? lit : (holds ? QColor (120, 120, 128) : QColor (58, 58, 64)));
        }
        auto f = g.font();
        f.setBold (true);
        f.setPixelSize (20);
        g.setFont (f);
        g.setPen (QColor (0, 0, 0, 150));
        g.drawText (QRectF (1, 1, 44, 44), Qt::AlignCenter, letter);
        g.setPen (QColor (240, 240, 240));
        g.drawText (QRectF (0, 0, 44, 44), Qt::AlignCenter, letter);
        return QIcon (pix);
    }

    QString colourName (int c)
    {
        static const char* names[slicer::numColours] { "Red", "Orange", "Yellow", "Lime", "Green", "Teal", "Sky", "Blue",
                                                       "Indigo", "Violet", "Magenta", "Pink", "Brown", "Slate", "Silver", "Charcoal" };
        return c >= 1 && c <= slicer::numColours ? SlicerWindow::tr (names[c - 1]) : SlicerWindow::tr ("None");
    }

    // The pads on the computer keyboard by key position (PC scan codes): the bottom row
    // is pads 1 to 4, as on Push.
    constexpr int padKeys[16] {
        0x2C, 0x2D, 0x2E, 0x2F,   // Z X C V
        0x1E, 0x1F, 0x20, 0x21,   // A S D F
        0x10, 0x11, 0x12, 0x13,   // Q W E R
        0x02, 0x03, 0x04, 0x05,   // 1 2 3 4
    };
    constexpr int bankKeys[slicer::numBanks] { 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D };   // 5 6 7 8 9 0 - =
    int scanCodeOf (const QKeyEvent* e) { return (int) e->nativeScanCode() - 8; }   // X11 and Wayland: evdev + 8

    const auto padMime = QStringLiteral ("application/x-phi-slicer-pad");

    enum Method { transients, equalParts, beatGrid, editorMarkers };

    // A MIDI note by name (C1 is 36); -1 is Auto, the note of the pad in the kit's layout.
    class NoteBox final : public QSpinBox
    {
    public:
        explicit NoteBox (QWidget* parent) : QSpinBox (parent)
        {
            setRange (-1, 127);
            setSpecialValueText (SlicerWindow::tr ("Auto"));
        }

    protected:
        QString textFromValue (int v) const override { return v < 0 ? specialValueText() : noteName (v) + QStringLiteral (" (%1)").arg (v); }
        int valueFromText (const QString& text) const override
        {
            bool ok = false;
            const auto n = text.section (QLatin1Char ('('), -1).remove (QLatin1Char (')')).trimmed().toInt (&ok);
            if (ok)
                return n;
            for (int v = 0; v < 128; ++v)
                if (noteName (v).compare (text.trimmed(), Qt::CaseInsensitive) == 0)
                    return v;
            return -1;
        }
        QValidator::State validate (QString& text, int&) const override
        {
            if (text.trimmed().compare (specialValueText(), Qt::CaseInsensitive) == 0 || valueFromText (text) >= 0)
                return QValidator::Acceptable;
            return QValidator::Intermediate;
        }
    };
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

void SliceView::setKit (const slicer::Kit& k)
{
    kit = k;
    if (shown() != peaksOf)
        buildPeaks();
    update();
}

void SliceView::setSelected (int pad)
{
    selected = pad;
    if (shown() != peaksOf)
        buildPeaks();
    update();
}

bool SliceView::ownSound() const
{
    const auto& l = kit.pads[(size_t) std::clamp (selected, 0, slicer::numPads - 1)].layers[0];
    return l.used && l.sound != nullptr;
}

const sampler::Sample* SliceView::shown() const
{
    return ownSound() ? kit.pads[(size_t) selected].layers[0].sound.get() : sample.get();
}

double SliceView::xFor (double frame) const
{
    const auto* s = shown();
    const auto n = s != nullptr ? std::max<Frame> (1, s->length()) : 1;
    return frame / (double) n * width();
}

Frame SliceView::frameAt (double x) const
{
    const auto* s = shown();
    const auto n = s != nullptr ? s->length() : 0;
    return std::clamp<Frame> ((Frame) std::llround (x / std::max (1, width()) * (double) n), 0, n);
}

Frame SliceView::cutNear (double x) const
{
    if (ownSound())
        return -1;
    double best = 6.0;
    Frame found = -1;
    for (const auto& p : kit.pads)
        if (p.isSlice() && p.layers[0].start > 0)
            if (const auto d = std::abs (xFor ((double) p.layers[0].start) - x); d <= best)
            {
                best = d;
                found = p.layers[0].start;
            }
    return found;
}

int SliceView::padAt (Frame f) const
{
    if (ownSound())
        return selected;
    for (int i = 0; i < slicer::numPads; ++i)
        if (const auto& p = kit.pads[(size_t) i]; p.isSlice() && f >= p.layers[0].start && f < p.layers[0].end)
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
    const auto* s = shown();
    peaksOf = s;
    if (s == nullptr || s->length() == 0 || width() <= 0)
        return;
    const auto columns = width();
    peaks.assign ((size_t) columns, { 0.0f, 0.0f });
    const auto n = s->length();
    for (int c = 0; c < columns; ++c)
    {
        const auto from = (Frame) ((double) c / columns * (double) n);
        const auto to = std::max (from + 1, (Frame) ((double) (c + 1) / columns * (double) n));
        float lo = 0.0f, hi = 0.0f;
        for (Frame i = from; i < std::min (to, n); ++i)
        {
            const auto v = 0.5f * (s->left[(size_t) i] + s->right[(size_t) i]);
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
    if (shown() == nullptr)
    {
        g.setPen (theme.color (QStringLiteral ("placeholderText")));
        g.drawText (rect().adjusted (8, 0, -8, 0), Qt::AlignCenter | Qt::TextWordWrap,
                    tr ("Send a sound here from the editor: Sample > Send to Slicer (F8), or drag an audio file here from Find or a file manager. "
                        "Drop files on a pad to give it a sound of its own"));
        return;
    }
    const auto mid = height() / 2.0;
    const bool own = ownSound();

    // The chosen slice is shaded, every other one striped lightly so they read apart.
    auto shade = theme.color (QStringLiteral ("region"));
    if (own)
    {
        const auto& l = kit.pads[(size_t) selected].layers[0];
        shade.setAlpha (90);
        g.fillRect (QRectF (QPointF (xFor ((double) l.start), 0), QPointF (xFor ((double) l.end), height())), shade);
    }
    else
        for (int i = 0; i < slicer::numPads; ++i)
            if (const auto& p = kit.pads[(size_t) i]; p.isSlice())
            {
                shade.setAlpha (i == selected ? 90 : (i % 2 == 0 ? 0 : 22));
                g.fillRect (QRectF (QPointF (xFor ((double) p.layers[0].start), 0), QPointF (xFor ((double) p.layers[0].end), height())), shade);
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
    if (own)
    {
        g.setPen (text);
        g.drawText (QRectF (4, 2, width() - 8, 14), Qt::AlignLeft | Qt::AlignVCenter,
                    tr ("Pad %1's own sound: %2").arg (padName (selected), soundName (kit.pads[(size_t) selected])));
    }
    else
        for (int i = 0; i < slicer::numPads; ++i)
            if (const auto& p = kit.pads[(size_t) i]; p.isSlice())
            {
                const auto x = xFor ((double) p.layers[0].start);
                g.setPen (QPen (line, i == selected ? 2.0 : 1.0));
                g.drawLine (QPointF (x, 0), QPointF (x, height()));
                if (xFor ((double) p.layers[0].end) - x > 18)
                {
                    g.setPen (text);
                    g.drawText (QRectF (x + 3, 2, 40, 14), Qt::AlignLeft | Qt::AlignVCenter, padName (i));
                }
            }

    g.setPen (QPen (theme.color (QStringLiteral ("cursor")), 1.5));
    for (const auto head : engine.playheads (own ? selected : -1))
        g.drawLine (QPointF (xFor (head), 0), QPointF (xFor (head), height()));
}

void SliceView::mousePressEvent (QMouseEvent* e)
{
    if (shown() == nullptr)
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
        setCursor (shown() != nullptr && cutNear (e->position().x()) > 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
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
    if (sample != nullptr && ! ownSound() && e->button() == Qt::LeftButton && cutNear (e->position().x()) < 0)
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
    setAcceptDrops (true);
    setToolTip (tr ("Click a pad to play it (higher is louder). Drag a pad onto another to swap them, drop audio files on a pad "
                    "to load them (several fill upward), right-click for its menu"));
}

QRectF PadGrid::padRect (int index) const
{
    const auto side = std::min (width(), height());
    const auto cell = side / 4.0;
    const auto row = index / 4, column = index % 4;
    return QRectF (column * cell, (3 - row) * cell, cell, cell).adjusted (3, 3, -3, -3);
}

QRectF PadGrid::muteRect (int index) const
{
    const auto r = padRect (index);
    const auto side = std::clamp (r.width() * 0.2, 12.0, 18.0);
    return QRectF (r.left() + 4, r.top() + 4, side, side);
}

QRectF PadGrid::soloRect (int index) const
{
    return muteRect (index).translated (muteRect (index).width() + 2, 0);
}

int PadGrid::padAt (QPointF at) const
{
    for (int i = 0; i < slicer::padsPerBank; ++i)
        if (padRect (i).contains (at))
            return bank * slicer::padsPerBank + i;
    return -1;
}

void PadGrid::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto lit = theme.color (QStringLiteral ("highlight"));
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    for (int i = 0; i < slicer::padsPerBank; ++i)
    {
        const auto pad = bank * slicer::padsPerBank + i;
        const auto& p = kit.pads[(size_t) pad];
        const auto r = padRect (i);
        const bool on = engine.isSounding (pad) || pad == pressed;
        const bool used = p.used();
        auto fill = used ? QColor (70, 72, 80) : QColor (38, 38, 42);
        if (const auto c = padColour (p.colour); c.isValid())
            fill = used ? c.darker (130) : c.darker (260);
        g.setPen (QPen (pad == selected ? theme.color (QStringLiteral ("marker")) : QColor (20, 20, 22), pad == selected ? 3.0 : 1.0));
        g.setBrush (on ? lit : fill);
        g.drawRoundedRect (r, 5, 5);
        const auto inner = r.adjusted (6, 4, -6, -4);
        const auto note = noteName (kit.receiveNote (pad));
        // M and S at its top left, as on the Drum Machine's pads.
        for (const auto& [box, lit2, label, colour] : { std::tuple { muteRect (i), p.mute, QStringLiteral ("M"), QColor (230, 160, 40) },
                                                        std::tuple { soloRect (i), p.solo, QStringLiteral ("S"), QColor (60, 160, 230) } })
        {
            g.setPen (Qt::NoPen);
            g.setBrush (lit2 ? colour : QColor (0, 0, 0, 70));
            g.drawRoundedRect (box, 2, 2);
            g.setPen (lit2 ? QColor (20, 20, 20) : QColor (170, 170, 175));
            g.setFont (small);
            g.drawText (box, Qt::AlignCenter, label);
        }
        // The note beside M and S, or under them when the pad is too narrow for both.
        g.setFont (small);
        auto noteArea = inner.adjusted (soloRect (i).right() + 3 - inner.left(), 0, 0, 0);
        if (g.fontMetrics().horizontalAdvance (note) > noteArea.width())
            noteArea = inner.adjusted (0, muteRect (i).bottom() + 2 - inner.top(), 0, 0);
        if (! used && p.name.empty())
        {
            // An empty pad shows only its note, as in Drum Rack.
            g.setPen (on ? QColor (20, 20, 20) : QColor (110, 110, 115));
            g.drawText (noteArea, Qt::AlignRight | Qt::AlignTop, note);
            continue;
        }
        g.setPen (on ? QColor (20, 20, 20) : (used ? QColor (235, 235, 238) : QColor (150, 150, 155)));
        g.drawText (noteArea, Qt::AlignRight | Qt::AlignTop, note);
        g.drawText (inner.adjusted (0, 0, 0, 0), Qt::AlignLeft | Qt::AlignBottom, padName (pad));
        g.setFont (font());
        g.drawText (inner.adjusted (0, inner.height() * 0.3, 0, -inner.height() * 0.3), Qt::AlignCenter,
                    g.fontMetrics().elidedText (soundName (p), Qt::ElideRight, (int) inner.width()));
        QStringList tags;
        if (p.layers[0].reverse) tags << tr ("Rev");
        if (p.play == slicer::Play::gate) tags << tr ("Gate");
        if (p.choke > 0) tags << tr ("Ch%1").arg (p.choke);
        if (! tags.isEmpty())
        {
            g.setFont (small);
            g.drawText (inner, Qt::AlignRight | Qt::AlignBottom, tags.join (QLatin1Char (' ')));
        }
    }
    if (hasFocus())
    {
        g.setPen (QPen (lit, 2.0));
        g.setBrush (Qt::NoBrush);
        const auto side = std::min (width(), height());
        g.drawRect (QRectF (1, 1, side - 2, side - 2));
    }
}

void PadGrid::mousePressEvent (QMouseEvent* e)
{
    setFocus();
    if (e->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < slicer::padsPerBank; ++i)
        if (const auto r = padRect (i); r.contains (e->position()))
        {
            if (muteRect (i).contains (e->position()) || soloRect (i).contains (e->position()))
            {
                Q_EMIT muteSoloClicked (bank * slicer::padsPerBank + i, soloRect (i).contains (e->position()));
                return;
            }
            pressed = bank * slicer::padsPerBank + i;
            pressedAt = e->position();
            const auto velocity = (float) std::clamp (0.35 + 0.65 * (1.0 - (e->position().y() - r.top()) / r.height()), 0.1, 1.0);
            Q_EMIT padPressed (pressed, velocity);
            break;
        }
    update();
}

void PadGrid::mouseMoveEvent (QMouseEvent* e)
{
    // Dragged far enough: the pad goes where it is dropped (onto another pad, they swap).
    if (pressed < 0 || ! (e->buttons() & Qt::LeftButton) || (e->position() - pressedAt).manhattanLength() < QApplication::startDragDistance())
        return;
    const auto from = pressed;
    Q_EMIT padReleased (from);
    pressed = -1;
    auto* mime = new QMimeData;
    mime->setData (padMime, QByteArray::number (from));
    auto* drag = new QDrag (this);
    drag->setMimeData (mime);
    QPixmap face (padRect (from % slicer::padsPerBank).size().toSize());
    face.fill (Qt::transparent);
    render (&face, {}, QRegion (padRect (from % slicer::padsPerBank).toRect()));
    drag->setPixmap (face);
    drag->exec (Qt::MoveAction);
    update();
}

void PadGrid::mouseReleaseEvent (QMouseEvent*)
{
    if (pressed >= 0)
        Q_EMIT padReleased (pressed);
    pressed = -1;
    update();
}

void PadGrid::contextMenuEvent (QContextMenuEvent* e)
{
    const auto pad = e->reason() == QContextMenuEvent::Keyboard ? selected : padAt (e->pos());
    if (pad >= 0)
        Q_EMIT padMenu (pad, e->globalPos());
}

void PadGrid::dragEnterEvent (QDragEnterEvent* e)
{
    if (e->mimeData()->hasFormat (padMime) || (e->mimeData()->hasUrls() && e->mimeData()->urls().first().isLocalFile()))
        e->acceptProposedAction();
}

void PadGrid::dragMoveEvent (QDragMoveEvent* e)
{
    if (padAt (e->position()) >= 0)
        e->acceptProposedAction();
    else
        e->ignore();
}

void PadGrid::dropEvent (QDropEvent* e)
{
    const auto pad = padAt (e->position());
    if (pad < 0)
        return;
    e->acceptProposedAction();
    if (e->mimeData()->hasFormat (padMime))
    {
        const auto from = e->mimeData()->data (padMime).toInt();
        if (from != pad)
            QTimer::singleShot (0, this, [this, from, pad] { Q_EMIT padsSwapped (from, pad); });
        return;
    }
    QStringList paths;
    for (const auto& url : e->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    QTimer::singleShot (0, this, [this, pad, paths] { Q_EMIT filesDropped (pad, paths); });
}

//==============================================================================
SlicerWindow::SlicerWindow (slicer::Engine& e, QWidget* parent)
    : QWidget (parent), engine (e)
{
    setFocusPolicy (Qt::StrongFocus);   // docked, it takes the playing keys when shown or clicked
    setAcceptDrops (true);
    setWindowTitle (tr ("Slicer"));
    setObjectName (QStringLiteral ("SlicerWindow"));
    kit = engine.kit();
    auto stored = appSettings();

    title = new QLabel (tr ("No sound yet"), this);
    auto bold = title->font();
    bold.setBold (true);
    title->setFont (bold);
    title->setMaximumWidth (220);
    view = new SliceView (engine, this);
    pads = new PadGrid (engine, this);
    pads->setMinimumWidth (240);   // room for each pad's M, S and note
    status = new QLabel (this);
    status->setMinimumWidth (100);
    status->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
    midiStatus = new QLabel (this);
    midiStatus->setAlignment (Qt::AlignRight | Qt::AlignVCenter);

    followBox = new QCheckBox (tr ("&Follow editor"), this);
    followBox->setChecked (true);
    followBox->setToolTip (tr ("Edits you make to this sound in the editor reach the Slicer straight away"));
    auto tool = [this] (const QString& text, const QString& tip)
    {
        auto* b = new QPushButton (text, this);
        b->setToolTip (tip);
        b->setFocusPolicy (Qt::NoFocus);
        return b;
    };
    auto* midi = tool (tr ("&MIDI..."), tr ("Choose the MIDI keyboards and pads (Push) that play the Slicer"));
    auto* kitButton = tool (tr ("&Kit"), tr ("Save the kit (pads, settings, sounds and patterns) as a .phikit file or open one, "
                                             "take the sound from the editor again, or follow its edits"));
    auto* kitMenu = new QMenu (kitButton);
    kitMenu->addAction (tr ("Save Kit..."), this, &SlicerWindow::saveKit);
    kitMenu->addAction (tr ("Load Kit..."), this, &SlicerWindow::loadKit);
    kitMenu->addSeparator();
    // Follow editor and Refresh live in the Kit menu, so the top bar stays narrow.
    kitMenu->addAction (tr ("Refresh from Editor"), this, &SlicerWindow::refreshRequested);
    auto* followAction = kitMenu->addAction (followBox->text());
    followAction->setCheckable (true);
    followAction->setChecked (followBox->isChecked());
    followAction->setToolTip (followBox->toolTip());
    connect (followAction, &QAction::toggled, followBox, &QCheckBox::setChecked);
    connect (followBox, &QCheckBox::toggled, followAction, &QAction::setChecked);
    followBox->hide();
    kitButton->setMenu (kitMenu);

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
    auto* sliceButton = new QPushButton (tr ("A&gain"), this);
    sliceButton->setToolTip (tr ("Cut the sound again with these settings (each pad keeps its own settings, and pads with sounds of their own keep them)"));
    snapBox = new QCheckBox (tr ("Snap &zero"), this);
    snapBox->setChecked (true);
    snapBox->setToolTip (tr ("Lines you drag land where the wave crosses zero, so slices do not click"));
    auto* method = new QHBoxLayout;
    auto* methodLabel = new QLabel (tr ("Sl&ice"), this);
    methodLabel->setBuddy (methodBox);
    method->addWidget (methodLabel);
    method->addWidget (methodBox);
    method->addWidget (methodOptions);
    method->addWidget (sliceButton);
    method->addWidget (snapBox);

    // The eight banks as small 4x4 grids above the pads, as in Cubase's Drum Machine.
    banks = new QButtonGroup (this);
    auto* bankRow = new QHBoxLayout;
    bankRow->setSpacing (3);
    static const char* bankKeyNames[slicer::numBanks] { "5", "6", "7", "8", "9", "0", "-", "=" };
    for (int b = 0; b < slicer::numBanks; ++b)
    {
        auto* button = new QPushButton (this);
        button->setCheckable (true);
        button->setFocusPolicy (Qt::NoFocus);
        button->setFixedSize (24, 24);
        button->setIconSize (QSize (18, 18));
        button->setToolTip (tr ("Bank %1: pads %2 to %3 (key %4)").arg (QChar ('A' + b)).arg (b * 16 + 1).arg (b * 16 + 16)
                                .arg (QString::fromLatin1 (bankKeyNames[b])));
        banks->addButton (button, b);
        bankRow->addWidget (button);
    }
    bankRow->addStretch (1);
    banks->button (0)->setChecked (true);
    bankLooks.fill (~0u);   // none drawn yet
    layoutBox = new QComboBox (this);
    layoutBox->addItems ({ tr ("Push 8x8"), tr ("Chromatic") });
    layoutBox->setToolTip (tr ("Push 8x8: Push's pads in User mode, bank A bottom left, B bottom right, C top left, D top right "
                               "(or E to H, with Push plays). Chromatic: pad n is note n, 16 notes a bank, as in Cubase (C1 is pad C5)"));
    pageBox = new QComboBox (this);
    pageBox->addItems ({ tr ("Push: A to D"), tr ("Push: E to H") });
    pageBox->setToolTip (tr ("Which four banks Push's 8x8 grid plays; the other four answer the notes left over"));

    // Instrument or Steps, as the Drum Machine's tabs along its top.
    pageButtons = new QButtonGroup (this);
    auto* topBar = new QHBoxLayout;
    for (const auto& [id, text, tip] : { std::tuple { 0, tr ("Instrument"), tr ("The chosen pad's layers, sound and settings") },
                                         std::tuple { 1, tr ("Steps"), tr ("The step sequencer: patterns played on the bank's pads") } })
    {
        auto* button = new QPushButton (text, this);
        button->setCheckable (true);
        button->setFocusPolicy (Qt::NoFocus);
        button->setToolTip (tip);
        pageButtons->addButton (button, id);
        topBar->addWidget (button);
    }
    pageButtons->button (0)->setChecked (true);
    topBar->addSpacing (12);
    topBar->addWidget (title);
    topBar->addSpacing (12);
    topBar->addLayout (method);
    topBar->addStretch (1);
    topBar->addWidget (kitButton);
    topBar->addWidget (midi);
    // Under the pads: how notes reach them.
    auto* layoutLabel = new QLabel (tr ("&Layout"), this);
    layoutLabel->setBuddy (layoutBox);

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
    playBox = new QComboBox (this);
    playBox->addItems ({ tr ("One shot"), tr ("Gate") });
    playBox->setToolTip (tr ("One shot plays the whole sound; Gate stops when you let go of the pad"));
    attackBox = real (0.0, 2000.0, 1.0, 1, tr (" ms"));
    releaseBox = real (1.0, 5000.0, 5.0, 0, tr (" ms"));
    chokeBox = whole (0, slicer::numChokes, 1, QString());
    chokeBox->setSpecialValueText (tr ("None"));
    chokeBox->setToolTip (tr ("Pads in the same group cut each other off, like open and closed hi-hats"));
    muteBox = new QCheckBox (tr ("M&ute"), this);
    soloBox = new QCheckBox (tr ("Sol&o"), this);
    soloBox->setToolTip (tr ("While any pad is soloed, only soloed pads sound"));
    receiveBox = new NoteBox (this);
    receiveBox->setToolTip (tr ("The MIDI note this pad answers. Auto: its note in the layout. Pads given the same note play together"));
    velocityBox = whole (0, 100, 5, tr (" %"));
    velocityBox->setToolTip (tr ("How much softer hits are quieter; 0% plays every hit at full level"));
    voicesBox = whole (1, slicer::Engine::maxVoices, 1, QString());
    voicesBox->setToolTip (tr ("How many sounds can play at once (each layer of a hit takes one)"));

    // The Pad section on the right: Level, Pan and Pitch as knobs, the rest beneath.
    padTitle = new QLabel (this);
    padTitle->setFont (bold);
    padTitle->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* knobs = new QHBoxLayout;
    auto knob = [this, knobs] (const QString& label, QAbstractSpinBox* box, int lo, int hi, int origin,
                               std::function<int()> get, std::function<void (int)> set)
    {
        auto* dial = new Potentiometer (this);
        dial->setRange (lo, hi);
        dial->setOrigin (origin);
        dial->setFixedSize (32, 32);
        dial->setToolTip (box->toolTip());
        auto* column = new QVBoxLayout;
        column->setSpacing (1);
        auto* text = new QLabel (label, this);
        text->setAlignment (Qt::AlignCenter);
        text->setBuddy (box);
        box->setMaximumWidth (78);
        column->addWidget (text);
        column->addWidget (dial, 0, Qt::AlignHCenter);
        column->addWidget (box);
        knobs->addLayout (column);
        // The knob and its box follow each other.
        connect (dial, &QDial::valueChanged, this, [set] (int v) { set (v); });
        auto follow = [dial, get]
        {
            const QSignalBlocker quiet (dial);
            dial->setValue (get());
        };
        if (auto* d = qobject_cast<QDoubleSpinBox*> (box))
            connect (d, &QDoubleSpinBox::valueChanged, dial, follow);
        else if (auto* s = qobject_cast<QSpinBox*> (box))
            connect (s, &QSpinBox::valueChanged, dial, follow);
        enableFollowers.push_back ([dial, box] { dial->setEnabled (box->isEnabled()); });
    };
    knob (tr ("Le&vel"), gainBox, -480, 120, 0, [this] { return (int) std::lround (gainBox->value() * 10.0); },
          [this] (int v) { gainBox->setValue (v / 10.0); });
    knob (tr ("&Pan"), panBox, -100, 100, 0, [this] { return panBox->value(); }, [this] (int v) { panBox->setValue (v); });
    knob (tr ("Pi&tch"), pitchBox, -24, 24, 0, [this] { return pitchBox->value(); }, [this] (int v) { pitchBox->setValue (v); });
    // Two columns of settings under the knobs, so the section stays as low as the pads.
    auto* padForm = new QGridLayout;
    padForm->setHorizontalSpacing (6);
    padForm->setVerticalSpacing (2);
    int row = 0;
    for (const auto& [text, w] : { std::pair<QString, QWidget*> { tr ("Pla&y"), playBox }, { tr ("C&hoke"), chokeBox },
                                   { tr ("Atta&ck"), attackBox }, { tr ("Rele&ase"), releaseBox },
                                   { tr ("Recei&ve"), receiveBox }, { QString(), nullptr },
                                   { tr ("Kit veloc&ity"), velocityBox }, { tr ("Voi&ces"), voicesBox } })
    {
        const int r = row / 2, c = (row % 2) * 2;
        ++row;
        if (w == nullptr)
        {
            auto* muteSolo = new QHBoxLayout;
            muteSolo->addWidget (muteBox);
            muteSolo->addWidget (soloBox);
            muteSolo->addStretch (1);
            padForm->addLayout (muteSolo, r, c, 1, 2);
            continue;
        }
        auto* label = new QLabel (text, this);
        label->setBuddy (w);
        padForm->addWidget (label, r, c);
        padForm->addWidget (w, r, c + 1);
    }
    padArea = new QWidget (this);
    auto* padColumn = new QVBoxLayout (padArea);
    padColumn->setContentsMargins (0, 0, 0, 0);
    padColumn->setSpacing (2);
    padColumn->addWidget (padTitle);
    padColumn->addLayout (knobs);
    padColumn->addLayout (padForm);
    padColumn->addStretch (1);
    padArea->setFixedWidth (320);

    // The layer slots: four a pad, each with its power and level, as the Drum Machine's.
    layers = new QButtonGroup (this);
    auto* slots = new QGridLayout;
    slots->setSpacing (2);
    for (int n = 0; n < slicer::maxLayers; ++n)
    {
        auto* power = new QPushButton (QStringLiteral ("⏻"), this);
        power->setCheckable (true);
        power->setFocusPolicy (Qt::NoFocus);
        power->setFixedSize (24, 22);
        power->setToolTip (tr ("Layer %1 on or off").arg (n + 1));
        auto* button = new QPushButton (tr ("Layer %1").arg (n + 1), this);
        button->setCheckable (true);
        button->setFocusPolicy (Qt::NoFocus);
        button->setStyleSheet (QStringLiteral ("text-align: left; padding-left: 6px;"));
        button->setFixedHeight (22);
        button->setToolTip (tr ("Edit layer %1 below").arg (n + 1));
        auto* level = new Potentiometer (this);
        level->setRange (-480, 120);
        level->setOrigin (0);
        level->setFixedSize (22, 22);
        level->setToolTip (tr ("Layer %1's level, on top of the pad's").arg (n + 1));
        layers->addButton (button, n);
        slots->addWidget (power, n, 0);
        slots->addWidget (button, n, 1);
        slots->addWidget (level, n, 2);
        layerPowers[(size_t) n] = power;
        layerLevels[(size_t) n] = level;
        connect (power, &QPushButton::toggled, this, [this, n] (bool on)
        {
            if (updating)
                return;
            auto& l = kit.pads[(size_t) selected].layers[(size_t) n];
            const bool playable = l.sound != nullptr || (n == 0 && l.end > l.start && sample != nullptr);
            l.used = on && playable;
            changed();
        });
        connect (level, &QDial::valueChanged, this, [this, n] (int v)
        {
            if (updating)
                return;
            kit.pads[(size_t) selected].layers[(size_t) n].gainDb = v / 10.0;
            changed();
        });
    }
    slots->setColumnMinimumWidth (1, 120);
    slots->setRowStretch (slicer::maxLayers, 1);
    layers->button (0)->setChecked (true);
    layerOnBox = new QCheckBox (this);   // the chosen layer's power and level, kept with the slots above
    layerGainBox = real (-48.0, 12.0, 0.5, 1, tr (" dB"));
    layerOnBox->hide();
    layerGainBox->hide();
    layerSound = new QLabel (this);
    layerLoad = new QPushButton (tr ("Loa&d Sound..."), this);
    layerClear = new QPushButton (tr ("Clea&r"), this);
    layerPitchBox = real (-48.0, 48.0, 0.1, 2, tr (" st"));
    layerStartBox = real (0.0, 3600000.0, 1.0, 1, tr (" ms"));
    layerEndBox = real (0.0, 3600000.0, 1.0, 1, tr (" ms"));
    fadeInBox = real (0.0, 10000.0, 1.0, 1, tr (" ms"));
    fadeOutBox = real (0.0, 10000.0, 1.0, 1, tr (" ms"));
    reverseLayerBox = new QCheckBox (tr ("Re&verse"), this);
    hotSwapBox = new QCheckBox (tr ("&Hot swap"), this);
    hotSwapBox->setToolTip (tr ("While on, a sound sent here (F8 in Find, or dropped on the window) goes onto this layer to try, "
                                "keeping its settings; Keep or Undo Swap when you have heard it"));
    keepSwap = new QPushButton (tr ("Keep"), this);
    undoSwap = new QPushButton (tr ("Undo Swap"), this);
    keepSwap->hide();
    undoSwap->hide();
    // The chosen layer, under the slots: two columns of settings.
    auto* layerGrid = new QGridLayout;
    layerGrid->setHorizontalSpacing (6);
    layerGrid->setVerticalSpacing (2);
    int at = 0;
    for (const auto& [text, w] : { std::pair<QString, QWidget*> { tr ("Tune"), layerPitchBox }, { tr ("Start"), layerStartBox },
                                   { tr ("End"), layerEndBox }, { tr ("Fade in"), fadeInBox }, { tr ("Fade out"), fadeOutBox } })
    {
        auto* label = new QLabel (text, this);
        label->setBuddy (w);
        w->setMaximumWidth (110);
        layerGrid->addWidget (label, at / 2, (at % 2) * 2);
        layerGrid->addWidget (w, at / 2, (at % 2) * 2 + 1);
        ++at;
    }
    layerGrid->addWidget (reverseLayerBox, 2, 2, 1, 2);
    auto* loadRow = new QHBoxLayout;
    loadRow->addWidget (layerLoad);
    loadRow->addWidget (layerClear);
    loadRow->addWidget (hotSwapBox);
    loadRow->addWidget (keepSwap);
    loadRow->addWidget (undoSwap);
    loadRow->addStretch (1);
    auto* layerColumn = new QVBoxLayout;
    layerColumn->setSpacing (3);
    layerColumn->addLayout (slots);
    layerColumn->addLayout (layerGrid);
    layerColumn->addLayout (loadRow);
    layerColumn->addStretch (1);
    layerSound->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);

    // The middle: the layers beside the sound (drop a sample on it), the slicing under the sound.
    auto* instrumentPage = new QWidget (this);
    auto* middle = new QHBoxLayout (instrumentPage);
    middle->setContentsMargins (0, 0, 0, 0);
    middle->addLayout (layerColumn);
    auto* soundColumn = new QVBoxLayout;
    soundColumn->setSpacing (3);
    soundColumn->addWidget (layerSound);
    soundColumn->addWidget (view, 1);
    middle->addLayout (soundColumn, 1);
    middle->addWidget (padArea);   // the steps take its room too

    // The pads, or the step sequencer playing them.
    PadLooks looks;
    looks.name = [this] (int pad)
    {
        const auto& p = kit.pads[(size_t) pad];
        const auto sound = soundName (p);
        return padName (pad) + QLatin1Char (' ') + (sound.isEmpty() ? noteName (kit.receiveNote (pad)) : sound);
    };
    looks.colour = [this] (int pad) { return padColour (kit.pads[(size_t) pad].colour); };
    stepsView = new StepSequencerView (engine, std::move (looks), this);
    pages = new QStackedWidget (this);
    pages->addWidget (instrumentPage);
    pages->addWidget (stepsView);

    // Banks and pads on the left, the page in the middle, the pad on the right.
    auto* left = new QVBoxLayout;
    left->setSpacing (3);
    left->addLayout (bankRow);
    left->addWidget (pads, 1);
    auto* body = new QHBoxLayout;
    body->addLayout (left);
    body->addWidget (pages, 1);

    midiStatus->setAlignment (Qt::AlignRight | Qt::AlignVCenter);
    // Along the bottom: how notes reach the pads, then what is going on.
    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget (layoutLabel);
    statusRow->addWidget (layoutBox);
    statusRow->addWidget (pageBox);
    statusRow->addSpacing (12);
    statusRow->addWidget (status, 1);
    midiStatus->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
    midiStatus->setMinimumWidth (120);
    statusRow->addWidget (midiStatus);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (6, 3, 6, 3);
    layout->setSpacing (3);
    layout->addLayout (topBar);
    layout->addLayout (body, 1);
    layout->addLayout (statusRow);

    // The slicing settings are remembered.
    methodBox->setCurrentIndex (std::clamp (stored.value (QStringLiteral ("slicer/method"), 0).toInt(), 0, 3));
    sensitivityBox->setValue (stored.value (QStringLiteral ("slicer/sensitivity"), 50).toInt());
    partsBox->setValue (stored.value (QStringLiteral ("slicer/parts"), 16).toInt());
    divisionBox->setCurrentIndex (std::clamp (stored.value (QStringLiteral ("slicer/division"), 3).toInt(), 0, 3));
    kit.layout = stored.value (QStringLiteral ("slicer/layout"), 0).toInt() == 1 ? slicer::Layout::chromatic : slicer::Layout::push;
    kit.pushPage = std::clamp (stored.value (QStringLiteral ("slicer/pushPage"), 0).toInt(), 0, 1);
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
    connect (pads, &PadGrid::padMenu, this, &SlicerWindow::padMenu);
    connect (pads, &PadGrid::muteSoloClicked, this, [this] (int pad, bool solo)
    {
        auto& p = kit.pads[(size_t) pad];
        (solo ? p.solo : p.mute) = ! (solo ? p.solo : p.mute);
        changed();
    });
    connect (pads, &PadGrid::padsSwapped, this, [this] (int from, int to)
    {
        slicer::swapPads (kit, from, to);
        changed();
        select (to);
    });
    connect (pads, &PadGrid::filesDropped, this, &SlicerWindow::loadPadFiles);
    connect (banks, &QButtonGroup::idClicked, this, [this] (int b) { setBank (b); select (b * slicer::padsPerBank + selected % slicer::padsPerBank); });
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
    connect (pageBox, &QComboBox::currentIndexChanged, this, [this] (int i)
    {
        if (updating)
            return;
        kit.pushPage = i;
        appSettings().setValue (QStringLiteral ("slicer/pushPage"), i);
        setKit (kit);
        setBank (i * 4 + bank % 4);
    });
    connect (playBox, &QComboBox::currentIndexChanged, this, &SlicerWindow::padFromControls);
    for (auto* b : { gainBox, attackBox, releaseBox })
        connect (b, &QDoubleSpinBox::valueChanged, this, &SlicerWindow::padFromControls);
    for (auto* b : { panBox, pitchBox, chokeBox, receiveBox, velocityBox, voicesBox })
        connect (b, &QSpinBox::valueChanged, this, &SlicerWindow::padFromControls);
    for (auto* b : { muteBox, soloBox })
        connect (b, &QCheckBox::toggled, this, &SlicerWindow::padFromControls);

    connect (layers, &QButtonGroup::idClicked, this, [this] (int n) { layer = n; layerToControls(); });
    for (auto* b : { layerOnBox, reverseLayerBox })
        connect (b, &QCheckBox::toggled, this, &SlicerWindow::layerFromControls);
    for (auto* b : { layerGainBox, layerPitchBox, layerStartBox, layerEndBox, fadeInBox, fadeOutBox })
        connect (b, &QDoubleSpinBox::valueChanged, this, &SlicerWindow::layerFromControls);
    connect (layerLoad, &QPushButton::clicked, this, &SlicerWindow::loadLayerFile);
    connect (layerClear, &QPushButton::clicked, this, [this]
    {
        kit.pads[(size_t) selected].layers[(size_t) layer] = {};
        changed();
    });
    connect (hotSwapBox, &QCheckBox::toggled, this, [this] (bool on)
    {
        if (! on && swapping)
            endHotSwap (true);
    });
    connect (keepSwap, &QPushButton::clicked, this, [this] { endHotSwap (true); });
    connect (undoSwap, &QPushButton::clicked, this, [this] { endHotSwap (false); });

    connect (pageButtons, &QButtonGroup::idClicked, this, &SlicerWindow::showPage);
    connect (stepsView, &StepSequencerView::padChosen, this, [this] (int pad) { select (pad); });

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
        for (int b = 0; b < slicer::numBanks; ++b)
        {
            // Each bank's 4x4: held pads grey, sounding ones lit; drawn again only when that changes.
            uint32_t look = 0;
            for (int i = 0; i < slicer::padsPerBank; ++i)
            {
                const auto pad = b * slicer::padsPerBank + i;
                look |= (kit.pads[(size_t) pad].used() ? 1u : 0u) << i;
                look |= (engine.isSounding (pad) ? 1u : 0u) << (i + 16);
            }
            if (bankLooks[(size_t) b] != look)
            {
                bankLooks[(size_t) b] = look;
                banks->button (b)->setIcon (bankIcon (look, QChar ('A' + b)));
            }
        }
        view->update();
        pads->update();
    });
    redraw.start();
    connect (qApp, &QApplication::focusChanged, this, &SlicerWindow::focusMoved);
    setKit (kit);
    select (0);
    showPage (std::clamp (stored.value (QStringLiteral ("slicer/page"), 0).toInt(), 0, 1));
    showStatus();
    adjustSize();
    pads->setFocus();
}

void SlicerWindow::setSample (sampler::SamplePtr s, AudioDataPtr a, double r, const QString& n, bool keepSlices,
                              const BeatGrid& g, const std::vector<Frame>& m)
{
    const auto length = s != nullptr ? s->length() : 0;
    bool fits = keepSlices, any = false;
    for (const auto& p : kit.pads)
    {
        fits = fits && (! p.isSlice() || p.layers[0].end <= length);
        any = any || p.used();
    }
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
    if (fits && any)
        setKit (kit);
    else
        slice();
}

bool SlicerWindow::followsEditor() const { return followBox->isChecked(); }

void SlicerWindow::setMidiStatus (const QString& text) { midiStatus->setText (text); }

double SlicerWindow::soundRate() const { return engine.sampleRate(); }

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
    slicer::assign (kit, slices);
    setKit (kit);
    if (! kit.pads[(size_t) selected].used())
        select (0);
    showStatus();
}

std::vector<Frame> SlicerWindow::currentCuts() const
{
    std::vector<Frame> cuts;
    for (const auto& p : kit.pads)
        if (p.isSlice())
        {
            cuts.push_back (p.layers[0].start);
            cuts.push_back (p.layers[0].end);
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
        if (p.isSlice())
        {
            if (p.layers[0].end == from)
                lo = std::max (lo, p.layers[0].start + gap);
            if (p.layers[0].start == from)
                hi = std::min (hi, p.layers[0].end - gap);
        }
    to = std::clamp<Frame> (snap (to), lo, std::max (lo, hi));
    if (to == from)
        return from;
    for (auto& p : kit.pads)
        if (p.isSlice())
        {
            if (p.layers[0].start == from)
                p.layers[0].start = to;
            if (p.layers[0].end == from)
                p.layers[0].end = to;
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
    stepsView->looksChanged();
    updating = true;
    layoutBox->setCurrentIndex (kit.layout == slicer::Layout::chromatic ? 1 : 0);
    pageBox->setCurrentIndex (kit.pushPage);
    pageBox->setEnabled (kit.layout == slicer::Layout::push);
    updating = false;
    padToControls();
}

void SlicerWindow::changed() { setKit (kit); }

void SlicerWindow::select (int pad)
{
    pad = std::clamp (pad, 0, slicer::numPads - 1);
    if (swapping && swapping->first != pad)
        endHotSwap (true);
    selected = pad;
    setBank (selected / slicer::padsPerBank);
    view->setSelected (selected);
    pads->setSelected (selected);
    stepsView->setSelected (selected);
    padToControls();
}

void SlicerWindow::setBank (int b)
{
    bank = std::clamp (b, 0, slicer::numBanks - 1);
    banks->button (bank)->setChecked (true);
    pads->setBank (bank);
    stepsView->setBank (bank);
    showStatus();
}

void SlicerWindow::showPage (int page)
{
    pageButtons->button (page)->setChecked (true);
    pages->setCurrentIndex (page);
    appSettings().setValue (QStringLiteral ("slicer/page"), page);
}

void SlicerWindow::setMixerClock (std::function<slicer::Engine::Clock()> make)
{
    stepsView->setMixerClock (std::move (make));
}

void SlicerWindow::padToControls()
{
    updating = true;
    const auto& p = kit.pads[(size_t) selected];
    const auto note = kit.receiveNote (selected);
    const auto& first = p.layers[0];
    const auto* source = first.sound != nullptr ? first.sound.get() : sample.get();
    QString about = tr ("empty");
    if (p.used())
    {
        about = soundName (p);
        if (first.used && source != nullptr)
            about += tr (" · %1 s").arg ((double) (first.end - first.start) / soundRate(), 0, 'f', 3);
    }
    padTitle->setText (tr ("Pad %1 · %2 · %3").arg (padName (selected), noteName (note), about));
    gainBox->setValue (p.gainDb);
    panBox->setValue ((int) std::lround (p.pan * 100.0));
    pitchBox->setValue ((int) std::lround (p.pitch));
    playBox->setCurrentIndex (p.play == slicer::Play::gate ? 1 : 0);
    attackBox->setValue (p.attack * 1000.0);
    releaseBox->setValue (p.release * 1000.0);
    chokeBox->setValue (p.choke);
    muteBox->setChecked (p.mute);
    soloBox->setChecked (p.solo);
    receiveBox->setValue (p.receive);
    velocityBox->setValue ((int) std::lround (kit.velocity * 100.0));
    voicesBox->setValue (kit.voices);
    for (auto* w : std::initializer_list<QWidget*> { gainBox, panBox, pitchBox, playBox, attackBox, releaseBox, chokeBox, muteBox, soloBox })
        w->setEnabled (p.used());
    for (const auto& follow : enableFollowers)
        follow();
    updating = false;
    layerToControls();
}

void SlicerWindow::padFromControls()
{
    if (updating)
        return;
    auto& p = kit.pads[(size_t) selected];
    p.gainDb = gainBox->value();
    p.pan = panBox->value() / 100.0;
    p.pitch = pitchBox->value();
    p.play = playBox->currentIndex() == 1 ? slicer::Play::gate : slicer::Play::oneShot;
    p.attack = attackBox->value() / 1000.0;
    p.release = releaseBox->value() / 1000.0;
    p.choke = chokeBox->value();
    p.mute = muteBox->isChecked();
    p.solo = soloBox->isChecked();
    p.receive = receiveBox->value();
    kit.velocity = velocityBox->value() / 100.0;
    kit.voices = voicesBox->value();
    setKit (kit);
}

void SlicerWindow::layerToControls()
{
    updating = true;
    const auto& p = kit.pads[(size_t) selected];
    // Each slot: what it holds, its power and its level.
    for (int n = 0; n < slicer::maxLayers; ++n)
    {
        const auto& slot = p.layers[(size_t) n];
        const bool holds = slot.sound != nullptr || (n == 0 && slot.end > slot.start && sample != nullptr);
        QString text = tr ("Empty");
        if (slot.sound != nullptr)
            text = slot.path.empty() ? tr ("Sound") : QFileInfo (QString::fromStdString (slot.path)).completeBaseName();
        else if (holds)
            text = tr ("Slice");
        auto* button = layers->button (n);
        button->setText (QStringLiteral ("%1  ").arg (n + 1) + button->fontMetrics().elidedText (text, Qt::ElideRight, 150));
        layerPowers[(size_t) n]->setChecked (slot.used);
        layerPowers[(size_t) n]->setEnabled (holds);
        layerLevels[(size_t) n]->setValue ((int) std::lround (slot.gainDb * 10.0));
        layerLevels[(size_t) n]->setEnabled (slot.used);
    }
    layers->button (layer)->setChecked (true);
    const auto& l = p.layers[(size_t) layer];
    const auto* source = l.sound != nullptr ? l.sound.get() : (layer == 0 && l.end > l.start ? sample.get() : nullptr);
    if (l.sound != nullptr)
        layerSound->setText (l.path.empty() ? tr ("A sound of its own") : QFileInfo (QString::fromStdString (l.path)).fileName());
    else if (source != nullptr)
        layerSound->setText (tr ("A slice of %1").arg (name));
    else
        layerSound->setText (tr ("Empty: load a sound or drop one on the pad"));
    layerOnBox->setChecked (l.used);
    const auto ms = 1000.0 / soundRate();
    const auto length = source != nullptr ? (double) source->length() * ms : 0.0;
    layerStartBox->setMaximum (std::max (0.0, length));
    layerEndBox->setMaximum (std::max (0.0, length));
    layerStartBox->setValue ((double) l.start * ms);
    layerEndBox->setValue ((double) l.end * ms);
    layerGainBox->setValue (l.gainDb);
    layerPitchBox->setValue (l.pitch);
    fadeInBox->setValue (l.fadeIn * 1000.0);
    fadeOutBox->setValue (l.fadeOut * 1000.0);
    reverseLayerBox->setChecked (l.reverse);
    layerOnBox->setEnabled (source != nullptr);
    for (auto* w : std::initializer_list<QWidget*> { layerGainBox, layerPitchBox, layerStartBox, layerEndBox, fadeInBox, fadeOutBox, reverseLayerBox })
        w->setEnabled (l.used && source != nullptr);
    layerClear->setEnabled (l.used || l.sound != nullptr);
    updating = false;
}

void SlicerWindow::layerFromControls()
{
    if (updating)
        return;
    auto& l = kit.pads[(size_t) selected].layers[(size_t) layer];
    const auto frames = soundRate() / 1000.0;
    l.used = layerOnBox->isChecked();
    l.gainDb = layerGainBox->value();
    l.pitch = layerPitchBox->value();
    l.start = (Frame) std::llround (layerStartBox->value() * frames);
    l.end = std::max (l.start + 2, (Frame) std::llround (layerEndBox->value() * frames));
    l.fadeIn = fadeInBox->value() / 1000.0;
    l.fadeOut = fadeOutBox->value() / 1000.0;
    l.reverse = reverseLayerBox->isChecked();
    changed();
}

void SlicerWindow::padMenu (int pad, QPoint at)
{
    select (pad);
    auto& p = kit.pads[(size_t) pad];
    QMenu menu (this);
    menu.addAction (tr ("&Rename..."), this, [this, pad]
    {
        bool ok = false;
        auto& target = kit.pads[(size_t) pad];
        const auto text = QInputDialog::getText (this, tr ("Rename Pad %1").arg (padName (pad)), tr ("Name (empty: the sound's name)"),
                                                 QLineEdit::Normal, QString::fromStdString (target.name), &ok);
        if (ok)
        {
            target.name = text.trimmed().toStdString();
            changed();
        }
    });
    auto* colours = menu.addMenu (tr ("Set &Colour"));
    for (int c = 0; c <= slicer::numColours; ++c)
    {
        QPixmap swatch (14, 14);
        swatch.fill (c == 0 ? Qt::transparent : padColour (c));
        auto* a = colours->addAction (QIcon (swatch), colourName (c), this, [this, pad, c]
        {
            kit.pads[(size_t) pad].colour = c;
            changed();
        });
        a->setCheckable (true);
        a->setChecked (p.colour == c);
    }
    auto* mute = menu.addAction (tr ("&Mute Pad"), this, [this, pad] { kit.pads[(size_t) pad].mute = ! kit.pads[(size_t) pad].mute; changed(); });
    mute->setCheckable (true);
    mute->setChecked (p.mute);
    auto* solo = menu.addAction (tr ("&Solo Pad"), this, [this, pad] { kit.pads[(size_t) pad].solo = ! kit.pads[(size_t) pad].solo; changed(); });
    solo->setCheckable (true);
    solo->setChecked (p.solo);
    menu.addAction (tr ("&Load Sound..."), this, [this, pad]
    {
        const auto files = QFileDialog::getOpenFileNames (this, tr ("Sounds for Pad %1").arg (padName (pad)),
                                                          appSettings().value (QStringLiteral ("slicer/folder"), QDir::homePath()).toString());
        if (! files.isEmpty())
            loadPadFiles (pad, files);
    });
    menu.addSeparator();
    menu.addAction (tr ("Cu&t Pad"), this, [this, pad]
    {
        clipboard = kit.pads[(size_t) pad];
        const auto receive = kit.pads[(size_t) pad].receive;
        kit.pads[(size_t) pad] = {};
        kit.pads[(size_t) pad].receive = receive;
        changed();
    });
    menu.addAction (tr ("C&opy Pad"), this, [this, pad] { clipboard = kit.pads[(size_t) pad]; });
    menu.addAction (tr ("&Paste Pad"), this, [this, pad]
    {
        const auto receive = kit.pads[(size_t) pad].receive;
        kit.pads[(size_t) pad] = *clipboard;
        kit.pads[(size_t) pad].receive = receive;
        changed();
    })->setEnabled (clipboard.has_value());
    menu.addSeparator();
    menu.addAction (tr ("R&eset Pad"), this, [this, pad] { kit.pads[(size_t) pad] = {}; changed(); });
    menu.addAction (tr ("Reset All M&ute"), this, [this] { for (auto& q : kit.pads) q.mute = false; changed(); });
    menu.addAction (tr ("Reset All S&olo"), this, [this] { for (auto& q : kit.pads) q.solo = false; changed(); });
    menu.addAction (tr ("Reset &All Pads"), this, [this]
    {
        for (auto& q : kit.pads)
            q = {};
        changed();
        slice();
    });
    menu.exec (at);
}

// Files dropped on a pad: the first goes on it, the rest on the pads after it.
void SlicerWindow::loadPadFiles (int pad, const QStringList& paths)
{
    QStringList problems;
    int placed = 0;
    for (const auto& path : paths)
    {
        const auto target = pad + placed;
        if (target >= slicer::numPads)
            break;
        QString why;
        auto sound = slicerkit::loadSound (path, soundRate(), why);
        if (sound == nullptr)
        {
            problems << tr ("%1 could not be read: %2").arg (QFileInfo (path).fileName(), why);
            continue;
        }
        slicer::placeSound (kit, target, 0, std::move (sound), path.toStdString());
        ++placed;
        appSettings().setValue (QStringLiteral ("slicer/folder"), QFileInfo (path).absolutePath());
    }
    changed();
    select (pad);
    if (placed > 0)
        engine.hit (pad, 0.8f);
    status->setText (problems.isEmpty() ? (placed > 1 ? tr ("%1 sounds on pads %2 to %3").arg (placed).arg (padName (pad), padName (pad + placed - 1))
                                                      : tr ("A sound of its own on pad %1").arg (padName (pad)))
                                        : problems.join (QLatin1Char ('\n')));
}

void SlicerWindow::loadLayerFile()
{
    const auto file = QFileDialog::getOpenFileName (this, tr ("Sound for Pad %1, Layer %2").arg (padName (selected)).arg (layer + 1),
                                                    appSettings().value (QStringLiteral ("slicer/folder"), QDir::homePath()).toString());
    if (file.isEmpty())
        return;
    QString why;
    auto sound = slicerkit::loadSound (file, soundRate(), why);
    if (sound == nullptr)
    {
        status->setText (tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), why));
        return;
    }
    appSettings().setValue (QStringLiteral ("slicer/folder"), QFileInfo (file).absolutePath());
    slicer::placeSound (kit, selected, layer, std::move (sound), file.toStdString());
    changed();
    engine.hit (selected, 0.8f);
}

bool SlicerWindow::hotSwapping() const { return hotSwapBox->isChecked(); }

void SlicerWindow::hotSwap (const QString& path)
{
    QString why;
    auto sound = slicerkit::loadSound (path, soundRate(), why);
    if (sound == nullptr)
    {
        status->setText (tr ("%1 could not be read: %2").arg (QFileInfo (path).fileName(), why));
        return;
    }
    if (! swapping)
        swapping = std::make_pair (selected, kit.pads[(size_t) selected]);
    // The new sound keeps the layer's settings, as Cubase's hot swap does.
    auto& l = kit.pads[(size_t) selected].layers[(size_t) layer];
    const auto kept = l;
    slicer::placeSound (kit, selected, layer, std::move (sound), path.toStdString());
    if (kept.used)
    {
        l.gainDb = kept.gainDb;
        l.pitch = kept.pitch;
        l.fadeIn = kept.fadeIn;
        l.fadeOut = kept.fadeOut;
        l.reverse = kept.reverse;
    }
    changed();
    keepSwap->show();
    undoSwap->show();
    engine.hit (selected, 0.8f);
    status->setText (tr ("Trying %1 on pad %2, layer %3: Keep, or Undo Swap").arg (QFileInfo (path).fileName(), padName (selected)).arg (layer + 1));
}

void SlicerWindow::endHotSwap (bool keep)
{
    if (! swapping)
        return;
    const auto [pad, was] = *swapping;
    swapping.reset();
    if (! keep)
    {
        kit.pads[(size_t) pad] = was;
        changed();
    }
    keepSwap->hide();
    undoSwap->hide();
    showStatus();
}

void SlicerWindow::saveKit()
{
    auto stored = appSettings();
    const auto suggested = stored.value (QStringLiteral ("slicer/kitFolder"), QDir::homePath()).toString() + QLatin1Char ('/')
                         + (name.isEmpty() ? tr ("Kit") : name) + QStringLiteral (".phikit");
    auto file = QFileDialog::getSaveFileName (this, tr ("Save Kit"), suggested, tr ("Slicer kits (*.phikit)"));
    if (file.isEmpty())
        return;
    if (! file.endsWith (QLatin1String (".phikit")))
        file += QStringLiteral (".phikit");
    stored.setValue (QStringLiteral ("slicer/kitFolder"), QFileInfo (file).absolutePath());
    QString error;
    const auto song = engine.song();
    if (! slicerkit::saveKitFile (file, kit, sample, soundRate(), error, song.get()))
        status->setText (tr ("The kit could not be saved: %1").arg (error));
    else
        status->setText (error.isEmpty() ? tr ("Saved %1").arg (QFileInfo (file).fileName())
                                         : tr ("Saved %1, but a pad's sound was left out: %2").arg (QFileInfo (file).fileName(), error));
}

void SlicerWindow::loadKit()
{
    auto stored = appSettings();
    const auto file = QFileDialog::getOpenFileName (this, tr ("Load Kit"), stored.value (QStringLiteral ("slicer/kitFolder"), QDir::homePath()).toString(),
                                                    tr ("Slicer kits (*.phikit)"));
    if (file.isEmpty())
        return;
    stored.setValue (QStringLiteral ("slicer/kitFolder"), QFileInfo (file).absolutePath());
    slicerkit::Loaded loaded;
    QString error;
    if (! slicerkit::loadKitFile (file, soundRate(), loaded, error))
    {
        status->setText (tr ("%1 could not be opened: %2").arg (QFileInfo (file).fileName(), error));
        return;
    }
    engine.panic();
    kit = loaded.kit;
    if (loaded.song != nullptr)
    {
        engine.setSong (*loaded.song);
        stepsView->refresh();
    }
    if (loaded.sample != nullptr)
    {
        followBox->setChecked (false);   // the kit's sample came from no tab
        setSample (loaded.sample, loaded.audio, soundRate(), QFileInfo (file).completeBaseName(), true, BeatGrid {}, {});
    }
    else
        setKit (kit);
    select (0);
    status->setText (loaded.problems.isEmpty() ? tr ("Opened %1").arg (QFileInfo (file).fileName()) : loaded.problems.join (QLatin1Char ('\n')));
}

void SlicerWindow::showStatus()
{
    QString text = tr ("Z X C V, A S D F, Q W E R, 1 2 3 4 play bank %1 from the bottom row · 5 6 7 8 9 0 - = choose banks A to H")
                       .arg (QChar ('A' + bank));
    if (found > slicer::numPads)
        text = tr ("%1 slices found: the first %2 are on the pads (lower the sensitivity for fewer). ").arg (found).arg (slicer::numPads) + text;
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
    if (e->key() == Qt::Key_Space && pages->currentIndex() == 1)
    {
        stepsView->playOrStop();
        return;
    }
    const auto code = scanCodeOf (e);
    for (int b = 0; b < slicer::numBanks; ++b)
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

void SlicerWindow::dragEnterEvent (QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() && e->mimeData()->urls().first().isLocalFile())
        e->acceptProposedAction();
}

void SlicerWindow::dropEvent (QDropEvent* e)
{
    if (! e->mimeData()->hasUrls() || ! e->mimeData()->urls().first().isLocalFile())
        return;
    e->acceptProposedAction();
    const auto path = e->mimeData()->urls().first().toLocalFile();
    if (hotSwapping())
        QTimer::singleShot (0, this, [this, path] { hotSwap (path); });
    else
        QTimer::singleShot (0, this, [this, path] { Q_EMIT fileDropped (path); });
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
