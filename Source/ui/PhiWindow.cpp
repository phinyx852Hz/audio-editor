// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiWindow.h"
#include "MasterPanel.h"
#include "PhiMark.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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
// The effect's picture, from its DeviceDisplay.
class PhiDisplay final : public QWidget
{
public:
    using QWidget::QWidget;

    void set (DeviceDisplay d)
    {
        shown = std::move (d);
        update();
    }

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

    DeviceDisplay shown;
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
    : QDialog (parent), engine (e), path (std::move (p)), pluginId (e.chainDevice (path).pluginId), display (new PhiDisplay (this))
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

    // The knobs: the main controls large, the rest smaller; switches as pills.
    const auto params = engine.chainParameters (path);
    std::vector<int> main = info.mainParameters;
    if (main.empty())
        for (int i = 0; i < (int) params.size() && (int) main.size() < 4; ++i)
            if (! params[(size_t) i].isBoolean && params[(size_t) i].numSteps == 0)
                main.push_back (i);
    const bool many = params.size() > 8;   // Φ EQ's bands: all the same size

    auto* row = new QHBoxLayout;
    row->setSpacing (small);
    std::vector<int> order = main;
    for (int i = 0; i < (int) params.size(); ++i)
        if (std::find (order.begin(), order.end(), i) == order.end())
            order.push_back (i);
    bool smallerStarted = false;
    for (const auto i : order)
    {
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
        c.value = new QLabel (QString::fromStdString (param.valueText), this);
        c.value->setAlignment (Qt::AlignHCenter);
        auto vf = c.value->font();
        vf.setPixelSize (11);
        c.value->setFont (vf);
        auto pal = c.value->palette();
        pal.setColor (QPalette::WindowText, gold);
        c.value->setPalette (pal);

        if (param.isBoolean)
        {
            c.toggle = new QToolButton (this);
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
            c.knob = new PhiKnob (this);
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
            auto* label = new QLabel (QString::fromStdString (param.name), this);
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
    layout->addLayout (row, 382);

    // The picture moves at 30 frames a second; the knobs follow automation and the card.
    auto* timer = new QTimer (this);
    connect (timer, &QTimer::timeout, this, [this] { sync (++ticks % 5 == 0); });
    timer->start (33);
    sync (true);
}

void PhiWindow::sync (bool parameters)
{
    if (engine.chainDevice (path).pluginId != pluginId)
    {
        close();   // removed or replaced
        return;
    }
    display->set (engine.chainDisplay (path));
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
    return dialog;
}
