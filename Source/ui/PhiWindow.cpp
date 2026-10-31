// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiWindow.h"
#include "EditorWindow.h"
#include "MasterPanel.h"
#include "PhiMark.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
    constexpr double phi = 1.6180339887498949;
    const QColor gold (0xc8, 0xa2, 0x4a);
    const QColor black (0x0b, 0x0b, 0x0c);
    const QColor panel (0x11, 0x11, 0x13);
    const QColor line (0x1f, 0x1f, 0x22);
    const QColor text (0xe8, 0xe4, 0xda);
    const QColor muted (0x8a, 0x86, 0x7e);
    const QColor faint (0x5c, 0x58, 0x50);

    // Spacing on Fibonacci steps.
    constexpr int small = 8, medium = 13, large = 21;
}

//==============================================================================
// The effect's picture, from its DeviceDisplay. Its nodes (Φ EQ's bands and cuts) are
// moved with the mouse: dragged across along the golden grid and up and down for gain
// (Shift finer), the wheel for width, a right click for the type, a double click to reset.
class PhiDisplay final : public QWidget
{
public:
    PhiDisplay (AudioEngine& e, ChainPath p, QWidget* parent)
        : QWidget (parent), engine (e), path (std::move (p))
    {
        setMouseTracking (true);
    }

    void set (DeviceDisplay d)
    {
        shown = std::move (d);
        if (chosen >= (int) shown.handles.size() || (chosen < 0 && ! shown.handles.empty()))
            choose (shown.handles.empty() ? -1 : 0);
        update();
    }

    int chosenHandle() const { return chosen; }
    void choose (int handle)
    {
        if (handle == chosen)
            return;
        chosen = handle;
        update();
        if (onChoose)
            onChoose (handle);
    }

    std::function<void (int)> onChoose;   // a node chosen (or -1)
    std::function<void()> onChange;       // a parameter moved from here

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        g.setRenderHint (QPainter::Antialiasing);
        const QRectF area = QRectF (rect()).adjusted (0.5, 0.5, -0.5, -0.5);
        g.setPen (line);
        g.setBrush (panel);
        g.drawRoundedRect (area, small, small);
        g.setClipRect (area.adjusted (1, 1, -1, -1));

        auto f = font();
        f.setPixelSize (10);
        g.setFont (f);

        const auto inner = area.adjusted (1, medium, -1, -1);
        switch (shown.kind)
        {
            case DeviceDisplay::Kind::frequency: frequency (g, inner); break;
            case DeviceDisplay::Kind::taps:      taps (g, inner); break;
            case DeviceDisplay::Kind::curve:     curve (g, inner); break;
            case DeviceDisplay::Kind::steps:     steps (g, inner); break;
            case DeviceDisplay::Kind::none:      break;
        }

        if (! shown.caption.empty())
        {
            g.setPen (muted);
            g.drawText (area.adjusted (medium, small, -medium, 0), Qt::AlignRight | Qt::AlignTop, QString::fromStdString (shown.caption));
        }
    }

private:
    static double xOfHz (const QRectF& r, double hz) { return r.left() + r.width() * std::log (hz / 20.0) / std::log (1000.0); }

    void frequency (QPainter& g, const QRectF& r)
    {
        // Octave-ish grid with labels, then the level heard (grey) and the curve (gold).
        for (const auto& [hz, label] : { std::pair { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
                                         { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } })
        {
            const auto x = xOfHz (r, hz);
            g.setPen (line);
            g.drawLine (QPointF (x, r.top()), QPointF (x, r.bottom()));
            g.setPen (faint);
            g.drawText (QPointF (x + 3, r.bottom() - 5), QString::fromLatin1 (label));
        }

        auto x = [&] (size_t i, size_t n) { return r.left() + r.width() * (double) i / (double) std::max<size_t> (1, n - 1); };
        if (! shown.back.empty())
        {
            // dB full scale, -90 at the bottom to 0 at the top.
            QPainterPath fill;
            fill.moveTo (r.bottomLeft());
            for (size_t i = 0; i < shown.back.size(); ++i)
                fill.lineTo (x (i, shown.back.size()), r.bottom() - r.height() * std::clamp ((shown.back[i] + 90.0) / 90.0, 0.0, 1.0));
            fill.lineTo (r.bottomRight());
            fill.closeSubpath();
            QLinearGradient shade (r.topLeft(), r.bottomLeft());
            shade.setColorAt (0.0, QColor (0x8a, 0x86, 0x7e, 90));
            shade.setColorAt (1.0, QColor (0x8a, 0x86, 0x7e, 0));
            g.setPen (QPen (muted, 1.0));
            g.setBrush (shade);
            g.drawPath (fill);
        }

        const auto span = std::max (1.0f, shown.highDb - shown.lowDb);
        auto y = [&] (double db) { return r.top() + r.height() * (shown.highDb - std::clamp (db, (double) shown.lowDb, (double) shown.highDb)) / span; };
        if (shown.lowDb < 0.0f && shown.highDb >= 0.0f)
        {
            g.setPen (QPen (QColor (0x3a, 0x37, 0x30), 1.0, Qt::DashLine));
            g.drawLine (QPointF (r.left(), y (0.0)), QPointF (r.right(), y (0.0)));
        }
        QPainterPath curve;
        for (size_t i = 0; i < shown.front.size(); ++i)
        {
            const QPointF p (x (i, shown.front.size()), y (shown.front[i]));
            i == 0 ? curve.moveTo (p) : curve.lineTo (p);
        }
        g.setPen (QPen (gold, 2.2));
        g.setBrush (Qt::NoBrush);
        g.drawPath (curve);

        plot = r;
        if (shown.handles.empty())
            return;

        // The golden grid along the bottom, every fourth step (a whole φ) longer.
        for (size_t i = 0; i < shown.grid.size(); ++i)
        {
            const auto gx = xOfHz (r, shown.grid[i]);
            const bool whole = i % 4 == 0;
            g.setPen (QPen (whole ? QColor (0x5c, 0x4c, 0x26) : QColor (0x2e, 0x2a, 0x22), 1.0));
            g.drawLine (QPointF (gx, r.bottom() - (whole ? 8 : 4)), QPointF (gx, r.bottom()));
        }

        auto nf = font();
        nf.setPixelSize (9);
        nf.setWeight (QFont::DemiBold);
        g.setFont (nf);
        for (int i = 0; i < (int) shown.handles.size(); ++i)
        {
            const auto c = handleAt (i);
            const auto& h = shown.handles[(size_t) i];
            const bool picked = i == chosen, over = i == hovered;
            auto ring = gold;
            if (! h.on)
                ring.setAlpha (110);
            g.setPen (QPen (picked || over ? text : ring, picked ? 2.0 : 1.4));
            g.setBrush (picked ? QBrush (gold) : QBrush (black));
            g.drawEllipse (c, radius, radius);
            g.setPen (picked ? black : (h.on ? gold : faint));
            g.drawText (QRectF (c.x() - radius, c.y() - radius, 2 * radius, 2 * radius), Qt::AlignCenter, QString::fromStdString (h.label));
        }
    }

    double xOfHzIn (double hz) const { return xOfHz (plot, hz); }
    double hzOfX (double x) const { return 20.0 * std::pow (1000.0, (x - plot.left()) / std::max (1.0, plot.width())); }
    double yOfDb (double db) const
    {
        const auto span = std::max (1.0f, shown.highDb - shown.lowDb);
        return plot.top() + plot.height() * (shown.highDb - std::clamp (db, (double) shown.lowDb, (double) shown.highDb)) / span;
    }

    QPointF handleAt (int i) const
    {
        const auto& h = shown.handles[(size_t) i];
        return { std::clamp (xOfHz (plot, h.hz), plot.left() + radius, plot.right() - radius),
                 std::clamp (yOfDb (h.db), plot.top() + radius, plot.bottom() - radius) };
    }

    int handleNear (QPointF p) const
    {
        if (shown.kind != DeviceDisplay::Kind::frequency)
            return -1;
        int best = -1;
        double nearest = radius + 5.0;
        for (int i = 0; i < (int) shown.handles.size(); ++i)
            if (const auto d = QLineF (p, handleAt (i)).length(); d < nearest)
            {
                nearest = d;
                best = i;
            }
        return best;
    }

    PluginParameterInfo parameter (int index) const
    {
        const auto all = engine.chainParameters (path);
        return index >= 0 && index < (int) all.size() ? all[(size_t) index] : PluginParameterInfo {};
    }

    void changed()
    {
        if (onChange)
            onChange();
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        const auto i = handleNear (e->position());
        if (i < 0)
            return QWidget::mousePressEvent (e);
        choose (i);
        const auto& h = shown.handles[(size_t) i];
        if (e->button() == Qt::RightButton)
        {
            typeMenu (h, e->globalPosition().toPoint());
            return;
        }
        if (e->button() != Qt::LeftButton)
            return;
        dragging = true;
        dragFrom = e->position();
        dragGain = 0.0;
        if (h.dbParameter >= 0)
            dragGain = QString::fromStdString (parameter (h.dbParameter).valueText).section (' ', 0, 0).toDouble();
        lastText.clear();
        e->accept();
    }

    void mouseMoveEvent (QMouseEvent* e) override
    {
        if (! dragging)
        {
            const auto over = handleNear (e->position());
            if (over != hovered)
            {
                hovered = over;
                setCursor (over >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
                update();
            }
            return;
        }
        if (chosen < 0 || chosen >= (int) shown.handles.size())
            return;
        const auto& h = shown.handles[(size_t) chosen];

        // Across: the nearest step of the grid (no free frequencies), or off past its end.
        if (h.hzParameter >= 0 && ! shown.grid.empty())
        {
            const auto hz = hzOfX (e->position().x());
            const auto edge = std::pow (phi, 0.125);
            std::string text;
            if ((h.offSide < 0 && hz < shown.grid.front() / edge) || (h.offSide > 0 && hz > shown.grid.back() * edge))
                text = "off";
            else
            {
                const auto best = std::min_element (shown.grid.begin(), shown.grid.end(), [hz] (float a, float b)
                    { return std::abs (std::log (a / hz)) < std::abs (std::log (b / hz)); });
                text = std::to_string (*best);
            }
            if (text != lastText)
            {
                lastText = text;
                engine.setChainParameterText (path, h.hzParameter, text);
                changed();
            }
        }

        // Up and down: the gain, from where the drag began; Shift ten times finer.
        if (h.dbParameter >= 0)
        {
            const auto perPixel = (shown.highDb - shown.lowDb) / std::max (1.0, plot.height()) * ((e->modifiers() & Qt::ShiftModifier) != 0 ? 0.1 : 1.0);
            const auto db = std::clamp (dragGain + (dragFrom.y() - e->position().y()) * perPixel, -12.0, 12.0);
            const auto text = QString::number (std::round (db * 10.0) / 10.0, 'f', 1).toStdString();
            if (text != lastGain)
            {
                lastGain = text;
                engine.setChainParameterText (path, h.dbParameter, text);
                changed();
            }
        }
        e->accept();
    }

    void mouseReleaseEvent (QMouseEvent* e) override
    {
        dragging = false;
        lastGain.clear();
        QWidget::mouseReleaseEvent (e);
    }

    void mouseDoubleClickEvent (QMouseEvent* e) override
    {
        const auto i = handleNear (e->position());
        if (i < 0)
            return QWidget::mouseDoubleClickEvent (e);
        for (const auto& [index, value] : shown.handles[(size_t) i].reset)
            engine.setChainParameter (path, index, value);
        dragging = false;
        changed();
    }

    void wheelEvent (QWheelEvent* e) override
    {
        // The width, a golden step (√φ) at a time: up narrower, down wider.
        auto i = handleNear (e->position());
        if (i < 0)
            i = chosen;
        if (i < 0 || i >= (int) shown.handles.size() || shown.handles[(size_t) i].qParameter < 0)
            return QWidget::wheelEvent (e);
        const auto q = shown.handles[(size_t) i].qParameter;
        const auto info = parameter (q);
        const auto steps = std::max (1, info.numSteps - 1);
        const auto at = (int) std::lround (info.value * steps) + (e->angleDelta().y() > 0 ? 1 : -1);
        engine.setChainParameter (path, q, (float) std::clamp (at, 0, steps) / (float) steps);
        changed();
        e->accept();
    }

    void leaveEvent (QEvent*) override
    {
        hovered = -1;
        update();
    }

    void typeMenu (const DeviceDisplay::Handle& h, QPoint at)
    {
        if (h.typeParameter < 0 || shown.types.empty())
            return;
        const auto info = parameter (h.typeParameter);
        const auto steps = std::max (1, (int) shown.types.size() - 1);
        const auto current = (int) std::lround (info.value * steps);
        QMenu menu (this);
        for (int t = 0; t < (int) shown.types.size(); ++t)
        {
            auto* a = menu.addAction (QString::fromStdString (shown.types[(size_t) t]));
            a->setCheckable (true);
            a->setChecked (t == current);
            a->setData (t);
        }
        if (auto* picked = menu.exec (at))
        {
            engine.setChainParameter (path, h.typeParameter, (float) picked->data().toInt() / (float) steps);
            changed();
        }
    }

    void axis (QPainter& g, const QRectF& r)
    {
        if (shown.axis.empty())
            return;
        g.setPen (faint);
        g.drawText (QPointF (r.left() + small, r.bottom() - 5), QString::fromStdString (shown.axis));
    }

    void taps (QPainter& g, const QRectF& r)
    {
        const auto inner = r.adjusted (large, large, -large, -large);
        g.setPen (QPen (line, 1.0));
        g.drawLine (inner.bottomLeft(), inner.bottomRight());
        for (size_t i = 0; i + 1 < shown.front.size(); i += 2)
        {
            const auto x = inner.left() + inner.width() * std::clamp (shown.front[i], 0.0f, 1.0f);
            const auto top = inner.bottom() - inner.height() * std::clamp (shown.front[i + 1], 0.0f, 1.0f);
            g.setPen (QPen (gold, 3.0, Qt::SolidLine, Qt::RoundCap));
            g.drawLine (QPointF (x, inner.bottom()), QPointF (x, top));
            g.setPen (Qt::NoPen);
            g.setBrush (gold);
            g.drawEllipse (QPointF (x, top), 4.0, 4.0);
        }
        axis (g, r);
    }

    void curve (QPainter& g, const QRectF& r)
    {
        const auto inner = r.adjusted (0, large, 0, -large);
        if (shown.back.size() > 1)
        {
            // A reference behind, in grey (a compressor's in = out).
            QPainterPath reference;
            for (size_t i = 0; i < shown.back.size(); ++i)
            {
                const QPointF p (inner.left() + inner.width() * (double) i / (double) (shown.back.size() - 1),
                                 inner.bottom() - inner.height() * std::clamp (shown.back[i], 0.0f, 1.0f));
                i == 0 ? reference.moveTo (p) : reference.lineTo (p);
            }
            g.setPen (QPen (QColor (0x3a, 0x37, 0x30), 1.0, Qt::DashLine));
            g.setBrush (Qt::NoBrush);
            g.drawPath (reference);
        }
        QPainterPath path, fill;
        const auto n = shown.front.size();
        for (size_t i = 0; i < n; ++i)
        {
            const QPointF p (inner.left() + inner.width() * (double) i / (double) std::max<size_t> (1, n - 1),
                             inner.bottom() - inner.height() * std::clamp (shown.front[i], 0.0f, 1.0f));
            i == 0 ? path.moveTo (p) : path.lineTo (p);
        }
        fill = path;
        fill.lineTo (inner.bottomRight());
        fill.lineTo (inner.bottomLeft());
        fill.closeSubpath();
        auto glow = gold;
        glow.setAlpha (28);
        g.setPen (Qt::NoPen);
        g.setBrush (glow);
        g.drawPath (fill);
        g.setPen (QPen (gold, 2.0));
        g.setBrush (Qt::NoBrush);
        g.drawPath (path);
        axis (g, r);
    }

    void steps (QPainter& g, const QRectF& r)
    {
        const auto n = (int) shown.front.size();
        if (n == 0)
            return;
        const auto cell = std::min (r.width() / n, r.height() / phi);
        const auto gap = cell / (phi * phi * phi);
        const auto left = r.center().x() - cell * n / 2.0;
        for (int i = 0; i < n; ++i)
        {
            const QRectF c (left + i * cell + gap / 2, r.center().y() - cell / 2, cell - gap, cell - gap);
            const bool on = shown.front[(size_t) i] > 0.5f;
            const bool now = i < (int) shown.back.size() && shown.back[(size_t) i] > 0.5f;
            g.setPen (now ? QPen (text, 2.0) : QPen (on ? gold : line, 1.0));
            auto fillColour = gold;
            fillColour.setAlpha (now ? 255 : 200);
            g.setBrush (on ? QBrush (fillColour) : QBrush (Qt::NoBrush));
            g.drawRoundedRect (c, gap, gap);
        }
    }

    AudioEngine& engine;
    ChainPath path;
    DeviceDisplay shown;
    QRectF plot;
    static constexpr double radius = 8.0;
    int chosen = -1, hovered = -1;
    bool dragging = false;
    QPointF dragFrom;
    double dragGain = 0.0;
    std::string lastText, lastGain;
};

//==============================================================================
// A gold knob: a 270 degree track filled in gold from the start, a pointer, dragged up
// and down (Shift for fine steps) or turned with the wheel.
class PhiKnob final : public QDial
{
public:
    using QDial::QDial;

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter p (this);
        p.setRenderHint (QPainter::Antialiasing);
        const auto side = std::min (width(), height()) - 8.0;
        const QRectF face ((width() - side) / 2.0 + 4, (height() - side) / 2.0 + 4, side - 8, side - 8);
        const QRectF track = face.adjusted (-4, -4, 4, 4);
        const auto range = std::max (1, maximum() - minimum());
        const auto angle = 225.0 - 270.0 * (double) (value() - minimum()) / range;

        p.setPen (QPen (QColor (0x26, 0x26, 0x2a), 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawArc (track, 225 * 16, -270 * 16);
        p.setPen (QPen (hasFocus() ? text : gold, 3.0, Qt::SolidLine, Qt::RoundCap));
        if (value() > minimum())
            p.drawArc (track, 225 * 16, (int) ((angle - 225.0) * 16));

        QRadialGradient shade (face.center() - QPointF (face.width() * 0.1, face.height() * 0.15), face.width() * 0.6);
        shade.setColorAt (0.0, QColor (0x2a, 0x2a, 0x2e));
        shade.setColorAt (1.0, QColor (0x12, 0x12, 0x14));
        p.setPen (QPen (QColor (0x2c, 0x2c, 0x30), 1.0));
        p.setBrush (shade);
        p.drawEllipse (face);

        const auto radians = angle * M_PI / 180.0;
        const auto c = face.center();
        const QPointF tip (c.x() + std::cos (radians) * face.width() * 0.31, c.y() - std::sin (radians) * face.height() * 0.31);
        p.setPen (QPen (gold, std::max (2.0, face.width() / 16.0), Qt::SolidLine, Qt::RoundCap));
        p.drawLine (QPointF (c.x() + (tip.x() - c.x()) * 0.2, c.y() + (tip.y() - c.y()) * 0.2), tip);
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton)
            return QDial::mousePressEvent (e);
        dragY = e->position().y();
        dragStart = value();
        setSliderDown (true);
        e->accept();
    }

    void mouseMoveEvent (QMouseEvent* e) override
    {
        if (! isSliderDown())
            return;
        // 233 pixels (a Fibonacci number) cover the whole range; Shift is ten times finer.
        const auto pixels = dragY - e->position().y();
        const auto perPixel = (maximum() - minimum()) / 233.0 * ((e->modifiers() & Qt::ShiftModifier) != 0 ? 0.1 : 1.0);
        setValue ((int) std::lround (dragStart + pixels * perPixel));
        e->accept();
    }

    void mouseReleaseEvent (QMouseEvent* e) override
    {
        if (isSliderDown())
            setSliderDown (false);
        e->accept();
    }

private:
    double dragY = 0.0;
    int dragStart = 0;
};

//==============================================================================
PhiWindow::PhiWindow (AudioEngine& e, ChainPath p, QWidget* parent)
    : QDialog (parent), engine (e), path (std::move (p)), pluginId (e.chainDevice (path).pluginId), display (new PhiDisplay (e, path, this))
{
    const auto info = engine.chainDevice (path);
    const auto name = phimark::withoutPhi (QString::fromStdString (info.name));
    setWindowTitle (QString::fromStdString (info.name));
    phimark::makePhiWindow (this);
    resize (809, 500);   // 500 × φ: a golden rectangle
    setMinimumSize (618, 382);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (large, medium, large, medium);
    layout->setSpacing (medium);

    // The Φ and the name.
    auto* header = new QHBoxLayout;
    header->setSpacing (medium);
    auto* title = new QLabel (name, this);
    auto f = title->font();
    f.setPixelSize (22);
    f.setWeight (QFont::DemiBold);
    title->setFont (f);
    auto* mark = new PhiMark (34, this);
    header->addWidget (mark);
    header->addWidget (title, 1);
    layout->addLayout (header);

    display->setMinimumHeight (144);
    layout->addWidget (display, 618);

    knobArea = new QWidget (this);
    layout->addWidget (knobArea, 382);
    display->onChoose = [this] (int) { buildKnobs(); };
    display->onChange = [this] { sync (true); };
    display->set (engine.chainDisplay (path));
    buildKnobs();

    // The picture moves at 30 frames a second; the knobs follow automation and the card.
    auto* timer = new QTimer (this);
    connect (timer, &QTimer::timeout, this, [this] { sync (++ticks % 5 == 0); });
    timer->start (33);
    sync (true);
}

void PhiWindow::buildKnobs()
{
    // The knobs: the main controls large, the rest smaller; switches as pills. With nodes
    // (Φ EQ), the chosen node's knobs large, then those the effect always shows.
    controls.clear();
    for (auto* child : knobArea->findChildren<QWidget*> (Qt::FindDirectChildrenOnly))
    {
        child->hide();
        child->deleteLater();
    }
    delete knobArea->layout();

    const auto info = engine.chainDevice (path);
    const auto params = engine.chainParameters (path);
    const auto shown = engine.chainDisplay (path);
    std::vector<int> main, order;
    bool many = false;
    shownPage = shown.pages.empty() ? -1 : std::clamp (shown.page, 0, (int) shown.pages.size() - 1);
    if (shownPage >= 0 && shownPage < (int) shown.pageKnobs.size())
    {
        // A page's knobs, its first two large (its mix or rate, and the next).
        order = shown.pageKnobs[(size_t) shownPage];
        main.assign (order.begin(), order.begin() + std::min<size_t> (2, order.size()));
    }
    else if (! shown.handles.empty())
    {
        const auto chosen = display->chosenHandle();
        if (chosen >= 0 && chosen < (int) shown.handles.size())
            main = shown.handles[(size_t) chosen].knobs;
        order = main;
        for (const auto i : shown.knobs)
            if (std::find (order.begin(), order.end(), i) == order.end())
                order.push_back (i);
    }
    else
    {
        main = info.mainParameters;
        if (main.empty())
            for (int i = 0; i < (int) params.size() && (int) main.size() < 4; ++i)
                if (! params[(size_t) i].isBoolean && params[(size_t) i].numSteps == 0)
                    main.push_back (i);
        many = params.size() > 8;   // all the same size when there are many
        order = main;
        for (int i = 0; i < (int) params.size(); ++i)
            if (std::find (order.begin(), order.end(), i) == order.end())
                order.push_back (i);
    }

    auto* outer = new QVBoxLayout (knobArea);
    outer->setContentsMargins (0, 0, 0, 0);
    outer->setSpacing (small);
    if (shownPage >= 0)
    {
        // The pages as pills; choosing one sets the effect's page, which the picture follows.
        auto* tabs = new QHBoxLayout;
        tabs->setSpacing (small);
        tabs->addStretch (1);
        const auto count = (int) shown.pages.size();
        for (int p = 0; p < count; ++p)
        {
            auto* tab = new QToolButton (knobArea);
            tab->setText (QString::fromStdString (shown.pages[(size_t) p]));
            tab->setCheckable (true);
            tab->setChecked (p == shownPage);
            tab->setAccessibleName (tab->text());
            tab->setStyleSheet (QStringLiteral ("QToolButton { color: #8a867e; background: #1a1a1c; border: 1px solid #3a3730; border-radius: 13px; padding: 4px 21px; }"
                                                "QToolButton:checked { color: #0b0b0c; background: #c8a24a; border-color: #c8a24a; }"));
            const auto parameter = shown.pageParameter;
            connect (tab, &QToolButton::clicked, this, [this, p, count, parameter]
            {
                if (parameter >= 0)
                    engine.setChainParameter (path, parameter, count > 1 ? (float) p / (float) (count - 1) : 0.0f);
                display->set (engine.chainDisplay (path));
                buildKnobs();
            });
            tabs->addWidget (tab);
        }
        tabs->addStretch (1);
        outer->addLayout (tabs);
    }
    auto* row = new QHBoxLayout;
    row->setContentsMargins (0, 0, 0, 0);
    row->setSpacing (small);
    outer->addLayout (row, 1);
    bool smallerStarted = false;
    for (const auto i : order)
    {
        if (i < 0 || i >= (int) params.size())
            continue;
        const auto& param = params[(size_t) i];
        const bool big = ! many && std::find (main.begin(), main.end(), i) != main.end();
        if (! big && ! smallerStarted && ! many)
        {
            row->addSpacing (large);
            smallerStarted = true;
        }

        Control c;
        c.index = i;
        auto* column = new QVBoxLayout;
        column->setSpacing (2);
        c.value = new QLabel (QString::fromStdString (param.valueText), knobArea);
        c.value->setAlignment (Qt::AlignHCenter);
        auto vf = c.value->font();
        vf.setPixelSize (11);
        c.value->setFont (vf);
        auto pal = c.value->palette();
        pal.setColor (QPalette::WindowText, gold);
        c.value->setPalette (pal);

        if (param.isBoolean)
        {
            c.toggle = new QToolButton (knobArea);
            c.toggle->setText (QString::fromStdString (param.name));
            c.toggle->setCheckable (true);
            c.toggle->setChecked (param.value > 0.5f);
            c.toggle->setAccessibleName (QString::fromStdString (param.name));
            c.toggle->setStyleSheet (QStringLiteral ("QToolButton { color: #8a867e; background: #1a1a1c; border: 1px solid #3a3730; border-radius: 13px; padding: 4px 13px; }"
                                                     "QToolButton:checked { color: #0b0b0c; background: #c8a24a; border-color: #c8a24a; }"));
            const auto index = i;
            connect (c.toggle, &QToolButton::toggled, this, [this, index] (bool on) { engine.setChainParameter (path, index, on ? 1.0f : 0.0f); sync (true); });
            column->addStretch (1);
            column->addWidget (c.toggle, 0, Qt::AlignHCenter);
            column->addStretch (1);
            c.value->hide();
        }
        else
        {
            c.steps = param.numSteps > 1 ? param.numSteps - 1 : 1000;
            c.knob = new PhiKnob (knobArea);
            c.knob->setRange (0, c.steps);
            c.knob->setSingleStep (std::max (1, c.steps / 100));
            c.knob->setPageStep (std::max (1, c.steps / 10));
            c.knob->setValue ((int) std::lround (param.value * c.steps));
            c.knob->setAccessibleName (QString::fromStdString (param.name));
            const auto side = big ? 89 : 55;   // Fibonacci sizes
            c.knob->setFixedSize (side, side);
            const auto index = i, steps = c.steps;
            connect (c.knob, &QDial::valueChanged, this, [this, index, steps] (int v)
            {
                engine.setChainParameter (path, index, (float) v / (float) steps);
                sync (true);
            });
            auto* label = new QLabel (QString::fromStdString (param.name), knobArea);
            label->setAlignment (Qt::AlignHCenter);
            auto lf = label->font();
            lf.setPixelSize (big ? 13 : 11);
            label->setFont (lf);
            label->setWordWrap (true);
            column->addStretch (1);
            column->addWidget (c.knob, 0, Qt::AlignHCenter);
            column->addWidget (label);
            column->addWidget (c.value);
            column->addStretch (1);
        }
        row->addLayout (column, big ? 3 : 2);
        controls.push_back (c);
    }
}

void PhiWindow::sync (bool parameters)
{
    if (engine.chainDevice (path).pluginId != pluginId)
    {
        close();   // removed or replaced
        return;
    }
    const auto shown = engine.chainDisplay (path);
    const auto page = shown.pages.empty() ? -1 : std::clamp (shown.page, 0, (int) shown.pages.size() - 1);
    display->set (shown);
    if (page != shownPage)
    {
        buildKnobs();   // the page moved by automation, or by the card
        return;
    }
    if (! parameters)
        return;

    const auto params = engine.chainParameters (path);
    for (auto& c : controls)
    {
        if (c.index >= (int) params.size())
            continue;
        const auto& p = params[(size_t) c.index];
        c.value->setText (QString::fromStdString (p.valueText));
        if (c.knob != nullptr && ! c.knob->isSliderDown())
        {
            const QSignalBlocker block (c.knob);
            c.knob->setValue ((int) std::lround (p.value * c.steps));
        }
        if (c.toggle != nullptr)
        {
            const QSignalBlocker block (c.toggle);
            c.toggle->setChecked (p.value > 0.5f);
        }
    }
}

void PhiWindow::keyPressEvent (QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape)
        close();
    else
        QDialog::keyPressEvent (e);
}

QDialog* openParameterWindow (AudioEngine& engine, const ChainPath& path, QWidget* parent)
{
    QDialog* dialog = phimark::isPhiPlugin (engine.chainDevice (path).pluginId) ? (QDialog*) new PhiWindow (engine, path, parent)
                                                                               : (QDialog*) new ParameterDialog (engine, path, parent);
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->show();
    editorwindow::mark (dialog);
    return dialog;
}
