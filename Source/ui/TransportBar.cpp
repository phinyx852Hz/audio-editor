// SPDX-License-Identifier: AGPL-3.0-only
#include "TransportBar.h"
#include "CommandRegistry.h"
#include "InfoStrip.h"
#include "Theme.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <cmath>

namespace
{
    const Command* findCommand (const CommandRegistry& commands, const QString& id)
    {
        for (const auto& c : commands.all())
            if (c.id == id)
                return &c;
        return nullptr;
    }
}

//==============================================================================
TransportButton::TransportButton (Symbol s, const QString& id, CommandRegistry& registry, QWidget* parent)
    : QToolButton (parent), symbol (s), commandId (id), commands (registry)
{
    setFocusPolicy (Qt::NoFocus);   // the keyboard already has every one of these
    setAutoRaise (true);
    setFixedSize (s == Symbol::play ? 38 : 32, s == Symbol::play ? 38 : 32);
    if (const auto* c = findCommand (commands, commandId))
    {
        setAccessibleName (c->name());
        connect (this, &QToolButton::clicked, c->action, &QAction::trigger);
        if (c->action->isCheckable())
        {
            setCheckable (true);
            connect (c->action, &QAction::toggled, this, &QToolButton::setChecked);
        }
    }
}

void TransportButton::setSymbol (Symbol s)
{
    if (s != symbol)
    {
        symbol = s;
        update();
    }
}

bool TransportButton::event (QEvent* e)
{
    // Built when shown, so a changed keymap is always right.
    if (e->type() == QEvent::ToolTip)
    {
        if (const auto* c = findCommand (commands, commandId))
        {
            auto name = c->name();
            if (commandId == QStringLiteral ("transport.playStop"))
                name = symbol == Symbol::pause ? tr ("Pause") : tr ("Play");
            const auto keys = c->shortcutText();
            QToolTip::showText (static_cast<QHelpEvent*> (e)->globalPos(),
                                keys.isEmpty() ? name : tr ("%1 (%2)").arg (name, keys), this);
        }
        return true;
    }
    return QToolButton::event (e);
}

void TransportButton::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);

    // Flat: just the symbol. Hover and Loop on light it in the accent colour.
    const auto r = QRectF (rect()).adjusted (1.5, 1.5, -1.5, -1.5);
    auto ink = theme.color (QStringLiteral ("windowText"));
    if (! isEnabled())
        ink = theme.color (QStringLiteral ("disabledText"));
    else if (isDown())
        ink = theme.color (QStringLiteral ("highlight")).darker (120);
    else if (isChecked() || underMouse())
        ink = theme.color (QStringLiteral ("highlight"));
    g.setPen (Qt::NoPen);
    g.setBrush (ink);

    const auto c = r.center();
    const auto u = r.width() / 7.0;   // one unit of the symbol grid
    auto triangle = [&] (double left, double right, bool pointsRight)
    {
        QPainterPath p;
        p.moveTo (pointsRight ? left : right, c.y() - 1.6 * u);
        p.lineTo (pointsRight ? left : right, c.y() + 1.6 * u);
        p.lineTo (pointsRight ? right : left, c.y());
        p.closeSubpath();
        g.drawPath (p);
    };

    switch (symbol)
    {
        case Symbol::play:   triangle (c.x() - 1.1 * u, c.x() + 1.7 * u, true); break;
        case Symbol::pause:
            g.drawRect (QRectF (c.x() - 1.3 * u, c.y() - 1.5 * u, 0.9 * u, 3.0 * u));
            g.drawRect (QRectF (c.x() + 0.4 * u, c.y() - 1.5 * u, 0.9 * u, 3.0 * u));
            break;
        case Symbol::stop:   g.drawRect (QRectF (c.x() - 1.3 * u, c.y() - 1.3 * u, 2.6 * u, 2.6 * u)); break;
        case Symbol::start:
            g.drawRect (QRectF (c.x() - 1.6 * u, c.y() - 1.6 * u, 0.6 * u, 3.2 * u));
            triangle (c.x() - 1.0 * u, c.x() + 1.6 * u, false);
            break;
        case Symbol::end:
            triangle (c.x() - 1.6 * u, c.x() + 1.0 * u, true);
            g.drawRect (QRectF (c.x() + 1.0 * u, c.y() - 1.6 * u, 0.6 * u, 3.2 * u));
            break;
        case Symbol::record:
            g.setBrush (isEnabled() ? QColor (0xe0, 0x40, 0x40) : ink);
            g.drawEllipse (c, 1.5 * u, 1.5 * u);
            break;
        case Symbol::loop:
        {
            // Three arrows chasing each other round a circle.
            const auto radius = 1.75 * u, stroke = 0.42 * u, head = 0.62 * u;
            const QRectF ring (c.x() - radius, c.y() - radius, 2.0 * radius, 2.0 * radius);
            for (int k = 0; k < 3; ++k)
            {
                const auto from = 100.0 - 120.0 * k;   // degrees, anticlockwise from 3 o'clock
                const auto sweep = -78.0;              // clockwise
                QPainterPath arc;
                arc.arcMoveTo (ring, from);
                arc.arcTo (ring, from, sweep);
                g.setBrush (Qt::NoBrush);
                g.setPen (QPen (ink, stroke, Qt::SolidLine, Qt::FlatCap));
                g.drawPath (arc);

                // The head, pointing on round the circle from the arc's end.
                const auto end = (from + sweep) * M_PI / 180.0;
                const QPointF p (c.x() + radius * std::cos (end), c.y() - radius * std::sin (end));
                const QPointF along (std::sin (end), std::cos (end));    // clockwise on screen
                const QPointF out (std::cos (end), -std::sin (end));
                QPainterPath tip;
                tip.moveTo (p + along * (1.3 * head));
                tip.lineTo (p + out * head);
                tip.lineTo (p - out * head);
                tip.closeSubpath();
                g.setPen (Qt::NoPen);
                g.setBrush (ink);
                g.drawPath (tip);
            }
            break;
        }
    }
}

//==============================================================================
TimeDisplay::TimeDisplay (QWidget* parent) : QWidget (parent)
{
    setFocusPolicy (Qt::NoFocus);
    setAccessibleName (tr ("Playback position"));
    setToolTip (tr ("Playback position: hours, minutes, seconds and milliseconds"));
}

void TimeDisplay::setPosition (Frame p, double sampleRate)
{
    if (p != position || std::abs (sampleRate - rate) > 0.0)
    {
        position = p;
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        setAccessibleDescription (text());
        update();
    }
}

QString TimeDisplay::text() const
{
    const auto ms = (long long) ((double) position / rate * 1000.0 + 0.5);
    return QString::asprintf ("%02lld:%02lld:%02lld.%03lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
}

QFont TimeDisplay::displayFont() const
{
    QFont f = font();
    f.setPointSizeF (f.pointSizeF() * 1.5);
    return f;
}

QSize TimeDisplay::sizeHint() const
{
    const QFontMetricsF fm (displayFont());
    return { (int) std::ceil (steady::width (fm, QStringLiteral ("000:00:00.000"))) + 8, 38 };
}

void TimeDisplay::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.setRenderHint (QPainter::TextAntialiasing);
    g.setFont (displayFont());
    g.setPen (Theme::current().color (QStringLiteral ("windowText")));
    const QFontMetricsF fm (g.font());
    const auto t = text();
    steady::draw (g, (width() - steady::width (fm, t)) / 2.0, (height() - fm.height()) / 2.0 + fm.ascent(), t);
}

//==============================================================================
TransportBar::TransportBar (CommandRegistry& registry, QWidget* parent)
    : QWidget (parent), commands (registry)
{
    setAccessibleName (tr ("Transport"));
    using S = TransportButton::Symbol;
    auto* row = new QHBoxLayout (this);
    row->setContentsMargins (8, 4, 8, 4);
    row->setSpacing (4);
    row->addStretch (1);
    row->addWidget (new TransportButton (S::start, QStringLiteral ("transport.start"), commands, this));
    row->addWidget (playButton = new TransportButton (S::play, QStringLiteral ("transport.playStop"), commands, this));
    row->addWidget (new TransportButton (S::stop, QStringLiteral ("transport.stop"), commands, this));
    row->addWidget (new TransportButton (S::end, QStringLiteral ("transport.end"), commands, this));
    row->addWidget (new TransportButton (S::record, QStringLiteral ("file.record"), commands, this));
    row->addWidget (loopButton = new TransportButton (S::loop, QStringLiteral ("transport.loop"), commands, this));
    row->addSpacing (16);
    row->addWidget (time = new TimeDisplay (this));
    row->addStretch (1);
}

void TransportBar::setState (Frame position, double sampleRate, bool playing, bool looping)
{
    playButton->setSymbol (playing ? TransportButton::Symbol::pause : TransportButton::Symbol::play);
    if (loopButton->isChecked() != looping)
        loopButton->setChecked (looping);
    time->setPosition (position, sampleRate);
}
