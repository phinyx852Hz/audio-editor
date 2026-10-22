// SPDX-License-Identifier: AGPL-3.0-only
#include "PatchBayWindow.h"
#include "MixerWindow.h"
#include "Theme.h"
#include "../engine/AudioEngine.h"

#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace patchbay;

namespace
{
    QString dbText (float db)
    {
        if (db < PatchBay::minDb)
            return QObject::tr ("off");
        if (std::abs (db) < 0.05f)
            return QStringLiteral ("0 dB");
        return QString::fromUtf8 ("%1%2 dB").arg (db > 0.0f ? QStringLiteral ("+") : QString::fromUtf8 ("−")).arg (std::abs (db), 0, 'f', 1);
    }

    std::vector<int> allSources()
    {
        std::vector<int> out;
        for (int n = 0; n < numNodes; ++n)
            if (hasOutput (n))
                out.push_back (n);
        return out;
    }

    std::vector<int> allDestinations()
    {
        std::vector<int> out;
        for (int n = 0; n < numNodes; ++n)
            if (hasInput (n))
                out.push_back (n);
        return out;
    }
}

//==============================================================================
PatchGrid::PatchGrid (PatchBay& b, QWidget* parent) : QWidget (parent), bay (b)
{
    setFocusPolicy (Qt::StrongFocus);
    setMouseTracking (true);
    setAttribute (Qt::WA_OpaquePaintEvent, false);
}

double PatchGrid::headerWidth() const  { return std::clamp (width() * 0.2, 120.0, 190.0); }
double PatchGrid::headerHeight() const { return 58.0; }

double PatchGrid::cell() const
{
    const auto w = (width() - headerWidth() - 8.0) / size;
    const auto h = (height() - headerHeight() - 8.0) / size;
    return std::max (16.0, std::min (w, h));
}

QRectF PatchGrid::cellRect (int row, int column) const
{
    const auto c = cell();
    return { headerWidth() + 4.0 + column * c, headerHeight() + 4.0 + row * c, c, c };
}

QRectF PatchGrid::rowHeader (int row) const
{
    const auto c = cell();
    return { 0.0, headerHeight() + 4.0 + row * c, headerWidth(), c };
}

QRectF PatchGrid::columnHeader (int column) const
{
    const auto c = cell();
    return { headerWidth() + 4.0 + column * c, 0.0, c, headerHeight() };
}

PatchGrid::Hit PatchGrid::hitAt (QPointF p) const
{
    Hit h;
    for (int i = 0; i < size; ++i)
    {
        if (rowHeader (i).contains (p))
            return { i, -1, true };
        if (columnHeader (i).contains (p))
            return { -1, i, true };
    }
    for (int r = 0; r < size; ++r)
        for (int c = 0; c < size; ++c)
            if (cellRect (r, c).contains (p))
                return { r, c, false };
    return h;
}

// Each kind of node has its colour from the theme, so the grid reads at a glance in any theme.
QColor PatchGrid::kindColour (int node) const
{
    const auto& theme = Theme::current();
    switch (kindOf (node))
    {
        case Kind::editor:  return theme.color (QStringLiteral ("waveform"));
        case Kind::sampler:
        case Kind::slicer:  return theme.color (QStringLiteral ("accent"));
        case Kind::synth:   return theme.color (QStringLiteral ("region"));
        case Kind::track:   return theme.color (QStringLiteral ("levelMedium"));
        case Kind::mixBus:  return theme.color (QStringLiteral ("meterMid"));
        case Kind::fxBus:   return theme.color (QStringLiteral ("levelSoft"));
        case Kind::mainOut: return theme.color (QStringLiteral ("highlight"));
    }
    return theme.color (QStringLiteral ("text"));
}

void PatchGrid::tick()
{
    bool moved = false;
    for (int n = 0; n < numNodes; ++n)
    {
        const auto peak = std::min (1.0f, bay.takePeak (n));
        // Falls by about 20 dB a second; shown on a 60 dB scale.
        const auto shown = peak > 1.0e-6f ? std::clamp (1.0f + std::log10 (peak) / 3.0f, 0.0f, 1.0f) : 0.0f;
        auto& level = levels[(size_t) n];
        const auto next = std::max (shown, level - 0.011f);
        if (std::abs (next - level) > 0.002f)
            moved = true;
        level = std::max (0.0f, next);
    }
    if (moved)
        update();
}

void PatchGrid::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto text = theme.color (QStringLiteral ("text"));
    const auto dim = theme.color (QStringLiteral ("disabledText"));
    const auto mid = theme.color (QStringLiteral ("mid"));
    const auto accent = theme.color (QStringLiteral ("accent"));
    auto surface = Theme::withOpacity (theme.color (QStringLiteral ("base")));
    g.fillRect (rect(), surface);

    auto bold = font();
    bold.setBold (true);
    auto small = font();
    small.setPointSizeF (std::max (6.5, font().pointSizeF() * 0.82));
    const QFontMetricsF boldMetrics (bold), smallMetrics (small);

    auto meterColour = [&theme] (float level)
    {
        return theme.color (level > 0.95f ? QStringLiteral ("meterHigh") : level > 0.8f ? QStringLiteral ("meterMid") : QStringLiteral ("meterLow"));
    };

    // Row headers: the sources.
    for (int r = 0; r < size; ++r)
    {
        const auto n = rows[(size_t) r];
        const auto box = rowHeader (r).adjusted (2, 2, -2, -2);
        const bool lit = r == hoverRow;
        const bool active = activeOf == nullptr || activeOf (n);
        g.setPen (Qt::NoPen);
        g.setBrush (Theme::withOpacity (lit ? mid : theme.color (QStringLiteral ("alternateBase"))));
        g.drawRoundedRect (box, 4, 4);
        g.setBrush (kindColour (n));
        g.drawRoundedRect (QRectF (box.left(), box.top(), 4, box.height()), 2, 2);
        const auto inner = box.adjusted (10, 4, -6, -8);
        g.setFont (bold);
        g.setPen (active ? text : dim);
        g.drawText (QRectF (inner.left(), inner.top(), inner.width(), boldMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    boldMetrics.elidedText (nameOf ? nameOf (n) : QString::fromStdString (defaultName (n)), Qt::ElideRight, inner.width()));
        g.setFont (small);
        g.setPen (dim);
        if (detailOf != nullptr)
            g.drawText (QRectF (inner.left(), inner.top() + boldMetrics.height(), inner.width(), smallMetrics.height() * 2.2),
                        Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, detailOf (n));
        const auto level = levels[(size_t) n];
        const QRectF meter (box.left() + 10, box.bottom() - 6, box.width() - 16, 3);
        g.setPen (Qt::NoPen);
        g.setBrush (theme.color (QStringLiteral ("meterBackground")));
        g.drawRect (meter);
        g.setBrush (meterColour (level));
        g.drawRect (QRectF (meter.left(), meter.top(), meter.width() * level, meter.height()));
    }

    // Column headers: the destinations.
    for (int c = 0; c < size; ++c)
    {
        const auto n = columns[(size_t) c];
        const auto box = columnHeader (c).adjusted (2, 2, -2, -2);
        const bool lit = c == hoverColumn;
        const bool active = activeOf == nullptr || activeOf (n);
        g.setPen (Qt::NoPen);
        g.setBrush (Theme::withOpacity (lit ? mid : theme.color (QStringLiteral ("alternateBase"))));
        g.drawRoundedRect (box, 4, 4);
        g.setBrush (kindColour (n));
        g.drawRoundedRect (QRectF (box.left(), box.top(), box.width(), 4), 2, 2);
        const auto inner = box.adjusted (3, 7, -3, -8);
        g.setFont (bold);
        g.setPen (active ? text : dim);
        g.drawText (QRectF (inner.left(), inner.top(), inner.width(), boldMetrics.height()), Qt::AlignCenter,
                    boldMetrics.elidedText (nameOf ? nameOf (n) : QString::fromStdString (defaultName (n)), Qt::ElideRight, inner.width()));
        g.setFont (small);
        g.setPen (dim);
        if (detailOf != nullptr)
            g.drawText (QRectF (inner.left(), inner.top() + boldMetrics.height(), inner.width(), smallMetrics.height() * 1.2), Qt::AlignCenter,
                        smallMetrics.elidedText (detailOf (n), Qt::ElideRight, inner.width()));
        const auto level = levels[(size_t) n];
        const QRectF meter (box.left() + 4, box.bottom() - 6, box.width() - 8, 3);
        g.setPen (Qt::NoPen);
        g.setBrush (theme.color (QStringLiteral ("meterBackground")));
        g.drawRect (meter);
        g.setBrush (meterColour (level));
        g.drawRect (QRectF (meter.left(), meter.top(), meter.width() * level, meter.height()));
    }

    // The jacks.
    for (int r = 0; r < size; ++r)
        for (int c = 0; c < size; ++c)
        {
            const auto from = rows[(size_t) r], to = columns[(size_t) c];
            const auto box = cellRect (r, c).adjusted (2, 2, -2, -2);
            const bool crossLit = r == hoverRow || c == hoverColumn;
            const bool on = bay.linked (from, to);
            const auto why = on ? Refusal::none : bay.check (from, to);
            g.setPen (Qt::NoPen);
            auto back = theme.color (QStringLiteral ("alternateBase"));
            if (why == Refusal::self || why == Refusal::noPort)
                back = theme.color (QStringLiteral ("meterBackground"));
            else if (crossLit)
                back = mid;
            g.setBrush (Theme::withOpacity (back));
            g.drawRoundedRect (box, 5, 5);

            const auto centre = box.center() - QPointF (0, on ? box.height() * 0.08 : 0.0);
            const auto radius = box.width() * 0.22;
            if (why == Refusal::self || why == Refusal::noPort)
            {
                g.setPen (QPen (dim, 1.2));
                g.drawLine (centre - QPointF (radius * 0.4, 0), centre + QPointF (radius * 0.4, 0));
            }
            else if (why == Refusal::loop)
            {
                g.setPen (QPen (dim, 1.2));
                g.setBrush (Qt::NoBrush);
                g.drawEllipse (centre, radius, radius);
                const auto d = radius * 0.5;
                g.drawLine (centre + QPointF (-d, -d), centre + QPointF (d, d));
                g.drawLine (centre + QPointF (-d, d), centre + QPointF (d, -d));
            }
            else if (! on)
            {
                g.setPen (QPen (theme.color (QStringLiteral ("centerLine")).lighter (crossLit ? 140 : 115), 2.0));
                g.setBrush (Theme::withOpacity (theme.color (QStringLiteral ("meterBackground"))));
                g.drawEllipse (centre, radius, radius);
                g.setPen (Qt::NoPen);
                g.setBrush (theme.color (QStringLiteral ("centerLine")));
                g.drawEllipse (centre, radius * 0.35, radius * 0.35);
            }
            else
            {
                const auto db = bay.gainDb (from, to);
                const auto colour = kindColour (from);
                // The glow: what goes through, the source's level after the patch's gain.
                const auto through = std::clamp (levels[(size_t) from] + (db < PatchBay::minDb ? -1.0f : db / 60.0f), 0.0f, 1.0f);
                if (through > 0.0f)
                {
                    QRadialGradient glow (centre, radius * 2.0);
                    auto inner = colour;
                    inner.setAlphaF (0.55 * through);
                    auto outer = colour;
                    outer.setAlphaF (0.0);
                    glow.setColorAt (0.4, inner);
                    glow.setColorAt (1.0, outer);
                    g.setPen (Qt::NoPen);
                    g.setBrush (glow);
                    g.drawEllipse (centre, radius * 2.0, radius * 2.0);
                }
                auto fill = colour;
                if (db < PatchBay::minDb)
                    fill.setAlphaF (0.35);
                g.setPen (QPen (colour.lighter (130), 1.5));
                g.setBrush (fill);
                g.drawEllipse (centre, radius, radius);
                g.setPen (Qt::NoPen);
                g.setBrush (theme.color (QStringLiteral ("meterBackground")));
                g.drawEllipse (centre, radius * 0.35, radius * 0.35);
                if (std::abs (db) >= 0.05f && box.height() > 34)
                {
                    g.setFont (small);
                    g.setPen (text);
                    g.drawText (QRectF (box.left(), centre.y() + radius + 1, box.width(), smallMetrics.height()), Qt::AlignCenter, dbText (db));
                }
            }

            if (hasFocus() && r == focusRow && c == focusColumn)
            {
                g.setPen (QPen (accent, 1.5));
                g.setBrush (Qt::NoBrush);
                g.drawRoundedRect (box.adjusted (1, 1, -1, -1), 5, 5);
            }
        }
}

void PatchGrid::describe (int row, int column)
{
    if (row < 0 || column < 0)
    {
        Q_EMIT hovered ({});
        return;
    }
    const auto from = rows[(size_t) row], to = columns[(size_t) column];
    const auto name = [this] (int n) { return nameOf ? nameOf (n) : QString::fromStdString (defaultName (n)); };
    const auto pair = QString::fromUtf8 ("%1 → %2").arg (name (from), name (to));
    if (bay.linked (from, to))
    {
        Q_EMIT hovered (tr ("%1: patched at %2. Drag up or down, scroll, or press + and − for the gain; click to unpatch.")
                            .arg (pair, dbText (bay.gainDb (from, to))));
        return;
    }
    switch (bay.check (from, to))
    {
        case Refusal::none:   Q_EMIT hovered (tr ("%1: click to patch.").arg (pair)); break;
        case Refusal::self:   Q_EMIT hovered (tr ("%1 can't be patched into itself.").arg (name (from))); break;
        case Refusal::noPort: Q_EMIT hovered (tr ("%1: nothing to patch here.").arg (pair)); break;
        case Refusal::loop:   Q_EMIT hovered (tr ("%1: %2 already feeds %3, so this would feed back into itself.").arg (pair, name (to), name (from))); break;
    }
}

void PatchGrid::toggle (int row, int column)
{
    const auto from = rows[(size_t) row], to = columns[(size_t) column];
    if (bay.linked (from, to))
        bay.disconnect (from, to);
    else if (bay.connect (from, to) != Refusal::none)
    {
        describe (row, column);
        return;
    }
    describe (row, column);
    update();
    Q_EMIT patched();
}

void PatchGrid::setGain (int row, int column, float db)
{
    const auto from = rows[(size_t) row], to = columns[(size_t) column];
    if (! bay.linked (from, to))
        return;
    // Below the bottom of the range is off; the top is +12 dB.
    db = db < PatchBay::minDb ? PatchBay::minDb - 1.0f : std::min (db, PatchBay::maxDb);
    bay.setGainDb (from, to, db);
    describe (row, column);
    update();
    Q_EMIT patched();
}

void PatchGrid::nudge (int row, int column, float db)
{
    const auto from = rows[(size_t) row], to = columns[(size_t) column];
    auto now = bay.gainDb (from, to);
    if (now < PatchBay::minDb)
        now = db > 0.0f ? PatchBay::minDb - db : now;   // up from off starts at the bottom
    setGain (row, column, std::round ((now + db) * 10.0f) / 10.0f);
}

void PatchGrid::mousePressEvent (QMouseEvent* e)
{
    const auto h = hitAt (e->position());
    if (e->button() != Qt::LeftButton)
        return;
    if (h.header)
    {
        Q_EMIT headerClicked (h.row >= 0, h.row >= 0 ? h.row : h.column, e->globalPosition().toPoint());
        return;
    }
    if (h.row < 0)
        return;
    focusRow = h.row;
    focusColumn = h.column;
    dragRow = h.row;
    dragColumn = h.column;
    dragY = e->position().y();
    dragDb = bay.gainDb (rows[(size_t) h.row], columns[(size_t) h.column]);
    if (dragDb < PatchBay::minDb)
        dragDb = PatchBay::minDb;
    dragMoved = false;
    update();
}

void PatchGrid::mouseMoveEvent (QMouseEvent* e)
{
    if (dragRow >= 0 && (e->buttons() & Qt::LeftButton) != 0)
    {
        const auto dy = dragY - e->position().y();
        if (std::abs (dy) > 3.0)
            dragMoved = true;
        if (dragMoved && bay.linked (rows[(size_t) dragRow], columns[(size_t) dragColumn]))
        {
            const auto perPixel = (e->modifiers() & Qt::ShiftModifier) != 0 ? 0.05f : 0.25f;
            setGain (dragRow, dragColumn, std::round ((dragDb + (float) dy * perPixel) * 10.0f) / 10.0f);
        }
        return;
    }
    const auto h = hitAt (e->position());
    if (h.row != hoverRow || h.column != hoverColumn)
    {
        hoverRow = h.row;
        hoverColumn = h.column;
        if (h.header)
            Q_EMIT hovered (tr ("Click to put something else here, or for its instrument, effect or window."));
        else
            describe (h.row, h.column);
        update();
    }
}

void PatchGrid::mouseReleaseEvent (QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && dragRow >= 0)
    {
        if (! dragMoved && hitAt (e->position()).row == dragRow && hitAt (e->position()).column == dragColumn)
            toggle (dragRow, dragColumn);
        dragRow = dragColumn = -1;
    }
}

void PatchGrid::mouseDoubleClickEvent (QMouseEvent* e)
{
    const auto h = hitAt (e->position());
    if (h.header || h.row < 0)
        return;
    // The first click patched it; the double click is 0 dB.
    if (bay.linked (rows[(size_t) h.row], columns[(size_t) h.column]))
        setGain (h.row, h.column, 0.0f);
}

void PatchGrid::wheelEvent (QWheelEvent* e)
{
    const auto h = hitAt (e->position());
    if (h.header || h.row < 0 || ! bay.linked (rows[(size_t) h.row], columns[(size_t) h.column]))
    {
        e->ignore();
        return;
    }
    const auto delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();   // Shift turns the wheel sideways
    const auto step = (e->modifiers() & Qt::ShiftModifier) != 0 ? 0.1f : 1.0f;
    nudge (h.row, h.column, delta > 0 ? step : -step);
    e->accept();
}

void PatchGrid::keyPressEvent (QKeyEvent* e)
{
    const auto from = rows[(size_t) focusRow], to = columns[(size_t) focusColumn];
    switch (e->key())
    {
        case Qt::Key_Left:  focusColumn = std::max (0, focusColumn - 1); break;
        case Qt::Key_Right: focusColumn = std::min (size - 1, focusColumn + 1); break;
        case Qt::Key_Up:    focusRow = std::max (0, focusRow - 1); break;
        case Qt::Key_Down:  focusRow = std::min (size - 1, focusRow + 1); break;
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter: toggle (focusRow, focusColumn); return;
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            if (bay.linked (from, to))
                toggle (focusRow, focusColumn);
            return;
        case Qt::Key_Plus:
        case Qt::Key_Equal:        nudge (focusRow, focusColumn, 1.0f); return;
        case Qt::Key_Minus:        nudge (focusRow, focusColumn, -1.0f); return;
        case Qt::Key_BracketRight: nudge (focusRow, focusColumn, 0.1f); return;
        case Qt::Key_BracketLeft:  nudge (focusRow, focusColumn, -0.1f); return;
        case Qt::Key_0:            setGain (focusRow, focusColumn, 0.0f); return;
        default: QWidget::keyPressEvent (e); return;
    }
    describe (focusRow, focusColumn);
    update();
}

void PatchGrid::leaveEvent (QEvent*)
{
    hoverRow = hoverColumn = -1;
    Q_EMIT hovered ({});
    update();
}

void PatchGrid::contextMenuEvent (QContextMenuEvent* e)
{
    const auto h = hitAt (e->pos());
    if (h.header)
    {
        Q_EMIT headerClicked (h.row >= 0, h.row >= 0 ? h.row : h.column, e->globalPos());
        return;
    }
    if (h.row < 0)
        return;
    const auto from = rows[(size_t) h.row], to = columns[(size_t) h.column];
    QMenu menu (this);
    const bool on = bay.linked (from, to);
    if (on)
        menu.addAction (tr ("&Unpatch"), this, [this, h] { toggle (h.row, h.column); });
    else
        menu.addAction (tr ("&Patch"), this, [this, h] { toggle (h.row, h.column); })->setEnabled (bay.check (from, to) == Refusal::none);
    menu.addSeparator();
    for (const float db : { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f, -24.0f })
    {
        auto* a = menu.addAction (dbText (db), this, [this, h, db] { setGain (h.row, h.column, db); });
        a->setEnabled (on);
        a->setCheckable (true);
        a->setChecked (on && std::abs (bay.gainDb (from, to) - db) < 0.05f);
    }
    menu.exec (e->globalPos());
}

//==============================================================================
PatchBayWindow::PatchBayWindow (PatchBay& b, AudioEngine& a, Hooks h, QWidget* parent)
    : QWidget (parent, Qt::Window), bay (b), audio (a), hooks (std::move (h))
{
    setWindowTitle (tr ("Patchbay"));
    setObjectName (QStringLiteral ("PatchBayWindow"));

    auto* title = new QLabel (tr ("Patchbay"), this);
    auto bold = title->font();
    bold.setBold (true);
    bold.setPointSizeF (bold.pointSizeF() * 1.15);
    title->setFont (bold);
    auto* legend = new QLabel (tr ("Sources down the side, destinations across the top"), this);
    legend->setEnabled (false);

    autoButton = new QPushButton (tr ("&Auto-fill"), this);
    autoButton->setCheckable (true);
    autoButton->setChecked (true);
    autoButton->setToolTip (tr ("Fill the rows and columns with what is in use, as it changes. Choosing a row or column yourself turns this off."));
    auto* reset = new QPushButton (tr ("&Default Patches"), this);
    reset->setToolTip (tr ("Every source back to where it is heard without a patchbay: the editor, Sampler, Slicer, synths, Mix Bus and FX buses to the Main Out, the tracks to the Mix Bus"));
    auto* panic = new QPushButton (tr ("All Notes &Off"), this);

    grid = new PatchGrid (bay, this);
    grid->nameOf = [this] (int n) { return nodeName (n); };
    grid->activeOf = [this] (int n) { return inUse[(size_t) n]; };
    grid->detailOf = [this] (int n) { return details[(size_t) n]; };
    const auto sources = allSources(), destinations = allDestinations();
    for (int i = 0; i < PatchGrid::size; ++i)
    {
        grid->rows[(size_t) i] = sources[(size_t) i];
        grid->columns[(size_t) i] = destinations[(size_t) i];
    }

    status = new QLabel (this);
    status->setWordWrap (true);
    status->setMinimumHeight (status->fontMetrics().height() * 2);
    midiStatus = new QLabel (this);
    midiStatus->setAlignment (Qt::AlignRight | Qt::AlignTop);

    auto* top = new QHBoxLayout;
    top->addWidget (title);
    top->addSpacing (8);
    top->addWidget (legend, 1);
    top->addWidget (autoButton);
    top->addWidget (reset);
    top->addWidget (panic);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget (status, 1);
    bottom->addWidget (midiStatus);
    auto* layout = new QVBoxLayout (this);
    layout->addLayout (top);
    layout->addWidget (grid, 1);
    layout->addLayout (bottom);

    connect (autoButton, &QPushButton::toggled, this, [this] (bool on) { setAutoFill (on); Q_EMIT changed(); });
    connect (reset, &QPushButton::clicked, this, [this]
    {
        if (bay.isDefault())
            return;
        if (QMessageBox::question (this, tr ("Default Patches"), tr ("Take out every patch you made and put each source back where it is heard without a patchbay?"))
            != QMessageBox::Yes)
            return;
        bay.resetLinks();
        grid->update();
        Q_EMIT changed();
    });
    connect (panic, &QPushButton::clicked, this, [this] { bay.panic(); });
    connect (grid, &PatchGrid::patched, this, &PatchBayWindow::changed);
    connect (grid, &PatchGrid::hovered, this, [this] (const QString& t)
    {
        status->setText (t.isEmpty() ? tr ("Click a jack to patch a source (row) into a destination (column). Crossed-out jacks would feed back.") : t);
    });
    connect (grid, &PatchGrid::headerClicked, this, &PatchBayWindow::headerMenu);
    Q_EMIT grid->hovered ({});

    connect (&meters, &QTimer::timeout, this, [this]
    {
        grid->tick();
        if (++sinceRefill >= 15)
            refill();
    });
    refreshInfo();
    meters.start (33);
    resize (900, 700);
}

QString PatchBayWindow::nodeName (int n) const
{
    switch (kindOf (n))
    {
        case Kind::editor:  return tr ("Editor");
        case Kind::sampler: return tr ("Sampler");
        case Kind::slicer:  return tr ("Slicer");
        case Kind::synth:   return tr ("Synth %1").arg (indexOf (n) + 1);
        case Kind::track:   return tr ("Track %1").arg (indexOf (n) + 1);
        case Kind::mixBus:  return tr ("Mix Bus");
        case Kind::fxBus:   return tr ("FX Bus %1").arg (indexOf (n) + 1);
        case Kind::mainOut: return tr ("Main Out");
    }
    return {};
}

void PatchBayWindow::setShown (const std::array<int, PatchGrid::size>& rows, const std::array<int, PatchGrid::size>& columns)
{
    for (int i = 0; i < PatchGrid::size; ++i)
    {
        if (hasOutput (rows[(size_t) i]))
            grid->rows[(size_t) i] = rows[(size_t) i];
        if (hasInput (columns[(size_t) i]))
            grid->columns[(size_t) i] = columns[(size_t) i];
    }
    grid->update();
}

void PatchBayWindow::setAutoFill (bool on)
{
    autoFilling = on;
    {
        const QSignalBlocker block (autoButton);
        autoButton->setChecked (on);
    }
    if (on)
        refill();
}

void PatchBayWindow::refreshInfo()
{
    for (int n = 0; n < numNodes; ++n)
    {
        // An FX bus counts once something goes into it.
        inUse[(size_t) n] = (hooks.active != nullptr && hooks.active (n)) || (kindOf (n) == Kind::fxBus && ! bay.sourcesOf (n).empty());
        if (kindOf (n) == Kind::synth)
        {
            const auto s = bay.synth (indexOf (n));
            details[(size_t) n] = s != nullptr ? QString::fromStdString (s->name()) : tr ("empty");
        }
        else if (kindOf (n) == Kind::fxBus)
        {
            const auto fx = bay.effect (indexOf (n));
            details[(size_t) n] = fx != nullptr ? QString::fromStdString (fx->name()) : tr ("no effect");
        }
        else
            details[(size_t) n] = hooks.detail != nullptr ? hooks.detail (n) : QString();
    }
}

// What is in use comes first, in the usual order; the rest of the 8 are the next
// ones that could be. The Main Out always has a column, at the right.
void PatchBayWindow::refill()
{
    sinceRefill = 0;
    refreshInfo();
    if (! autoFilling)
    {
        grid->update();
        return;
    }
    auto used = [this] (int n) { return inUse[(size_t) n]; };
    auto pick = [&] (const std::vector<int>& all, int reserved)
    {
        std::vector<int> chosen;
        const auto room = PatchGrid::size - (reserved >= 0 ? 1 : 0);
        for (const auto n : all)
            if (n != reserved && (int) chosen.size() < room && used (n))
                chosen.push_back (n);
        for (const auto n : all)
            if (n != reserved && (int) chosen.size() < room && std::find (chosen.begin(), chosen.end(), n) == chosen.end())
                chosen.push_back (n);
        std::sort (chosen.begin(), chosen.end());
        if (reserved >= 0)
            chosen.push_back (reserved);
        return chosen;
    };
    const auto rows = pick (allSources(), -1);
    const auto columns = pick (allDestinations(), node::mainOut);
    bool moved = false;
    for (int i = 0; i < PatchGrid::size; ++i)
    {
        moved = moved || grid->rows[(size_t) i] != rows[(size_t) i] || grid->columns[(size_t) i] != columns[(size_t) i];
        grid->rows[(size_t) i] = rows[(size_t) i];
        grid->columns[(size_t) i] = columns[(size_t) i];
    }
    grid->update();   // details may have changed too
    if (moved)
        Q_EMIT changed();
}

void PatchBayWindow::setMidiStatus (const QString& text)
{
    midiStatus->setText (text);
}

void PatchBayWindow::headerMenu (bool row, int slot, QPoint at)
{
    auto& shown = row ? grid->rows : grid->columns;
    const auto n = shown[(size_t) slot];
    QMenu menu (this);
    menu.addSection (nodeName (n));

    switch (kindOf (n))
    {
        case Kind::synth:
        {
            const auto i = indexOf (n);
            const auto current = bay.synth (i);
            const auto currentId = current == nullptr ? std::string()
                                 : current->kind() == mixer::Instrument::Kind::synth ? std::string ("synth")
                                                                                     : AudioEngine::instrumentPluginId (*current);
            auto* choose = menu.addMenu (tr ("&Instrument"));
            auto item = [&] (const QString& text, const std::string& id)
            {
                auto* a = choose->addAction (text, this, [this, i, id] { chooseInstrument (i, id); });
                a->setCheckable (true);
                a->setChecked (id == currentId);
            };
            item (tr ("None"), {});
            item (tr ("Built-in Synth"), "synth");
            const auto plugins = audio.knownInstruments();
            if (! plugins.empty())
                choose->addSeparator();
            for (const auto& p : plugins)
                item (QStringLiteral ("%1 (%2)").arg (QString::fromStdString (p.name), QString::fromStdString (p.format)), p.id);
            menu.addAction (tr ("&Edit Instrument..."), this, [this, n] { editNode (n); })->setEnabled (current != nullptr);
            menu.addAction (tr ("&Play from the MIDI Keyboard"), this, [this, i] { if (hooks.playSynth) hooks.playSynth (i); })
                ->setEnabled (current != nullptr);
            break;
        }
        case Kind::fxBus:
        {
            const auto bus = indexOf (n);
            const auto current = bay.effect (bus);
            const auto currentId = current != nullptr ? current->pluginId() : std::string();
            auto* choose = menu.addMenu (tr ("&Effect"));
            auto item = [&] (const QString& text, const std::string& id)
            {
                auto* a = choose->addAction (text, this, [this, bus, id] { chooseEffect (bus, id); });
                a->setCheckable (true);
                a->setChecked (id == currentId);
            };
            item (tr ("None (passes through)"), {});
            auto plugins = audio.knownPlugins();
            std::sort (plugins.begin(), plugins.end(), [] (const PluginInfo& x, const PluginInfo& y) { return x.name < y.name; });
            if (! plugins.empty())
                choose->addSeparator();
            for (const auto& p : plugins)
                item (QStringLiteral ("%1 (%2)").arg (QString::fromStdString (p.name), QString::fromStdString (p.format)), p.id);
            menu.addAction (tr ("&Edit Effect..."), this, [this, n] { editNode (n); })->setEnabled (current != nullptr && current->hasWindow());
            break;
        }
        case Kind::sampler: menu.addAction (tr ("Open the &Sampler"), this, [this, n] { if (hooks.open) hooks.open (n); }); break;
        case Kind::slicer:  menu.addAction (tr ("Open the S&licer"), this, [this, n] { if (hooks.open) hooks.open (n); }); break;
        case Kind::track:
        case Kind::mixBus:  menu.addAction (tr ("Open the &Mixer"), this, [this, n] { if (hooks.open) hooks.open (n); }); break;
        default: break;
    }

    const auto patches = row ? bay.destinationsOf (n) : bay.sourcesOf (n);
    menu.addAction (row ? tr ("&Unpatch Its Outputs") : tr ("&Unpatch Its Inputs"), this, [this, n, row]
    {
        for (const auto other : row ? bay.destinationsOf (n) : bay.sourcesOf (n))
            row ? bay.disconnect (n, other) : bay.disconnect (other, n);
        grid->update();
        Q_EMIT changed();
    })->setEnabled (! patches.empty());

    menu.addSeparator();
    auto* place = menu.addMenu (row ? tr ("Show &Here (Source)") : tr ("Show &Here (Destination)"));
    for (const auto other : row ? allSources() : allDestinations())
    {
        auto* a = place->addAction (inUse[(size_t) other] ? tr ("%1 (in use)").arg (nodeName (other)) : nodeName (other), this, [this, row, slot, other]
        {
            auto& list = row ? grid->rows : grid->columns;
            // Already shown elsewhere: the two swap places.
            const auto there = std::find (list.begin(), list.end(), other);
            if (there != list.end())
                *there = list[(size_t) slot];
            list[(size_t) slot] = other;
            setAutoFill (false);
            grid->update();
            Q_EMIT changed();
        });
        a->setCheckable (true);
        a->setChecked (other == n);
    }
    menu.addAction (tr ("&Fill from What Is in Use"), this, [this] { setAutoFill (true); Q_EMIT changed(); });
    menu.exec (at);
}

void PatchBayWindow::chooseInstrument (int i, const std::string& id)
{
    mixer::InstrumentPtr made;
    if (id == "synth")
        made = std::make_shared<mixer::Synth>();
    else if (! id.empty())
    {
        std::string error;
        made = audio.loadInstrumentPlugin (id, bay.sampleRate(), error);
        if (made == nullptr)
        {
            QMessageBox::warning (this, tr ("Patchbay"), tr ("The instrument could not be started: %1").arg (QString::fromStdString (error)));
            return;
        }
    }
    bay.setSynth (i, made);
    if (made != nullptr && hooks.playSynth)
        hooks.playSynth (i);
    refill();
    Q_EMIT changed();
}

void PatchBayWindow::chooseEffect (int bus, const std::string& id)
{
    EffectPtr made;
    if (! id.empty())
    {
        std::string error;
        made = audio.loadEffectPlugin (id, bay.sampleRate(), error);
        if (made == nullptr)
        {
            QMessageBox::warning (this, tr ("Patchbay"), tr ("The effect could not be started: %1").arg (QString::fromStdString (error)));
            return;
        }
    }
    bay.setEffect (bus, made);
    refill();
    Q_EMIT changed();
}

void PatchBayWindow::editNode (int n)
{
    if (kindOf (n) == Kind::fxBus)
    {
        if (auto fx = bay.effect (indexOf (n)); fx != nullptr && fx->hasWindow())
            fx->showWindow();
        return;
    }
    if (kindOf (n) != Kind::synth)
        return;
    auto inst = bay.synth (indexOf (n));
    if (inst == nullptr)
        return;
    if (inst->kind() == mixer::Instrument::Kind::synth)
    {
        auto* d = synthDialog (inst, nodeName (n), this, [this] { Q_EMIT changed(); });
        d->show();
        d->raise();
        d->activateWindow();
    }
    else if (inst->hasWindow())
        inst->showWindow();
}

void PatchBayWindow::changeEvent (QEvent* e)
{
    if (e->type() == QEvent::ActivationChange && isActiveWindow())
        Q_EMIT activated();
    QWidget::changeEvent (e);
}

void PatchBayWindow::closeEvent (QCloseEvent* e)
{
    QWidget::closeEvent (e);
    Q_EMIT closed();
}
