// SPDX-License-Identifier: AGPL-3.0-only
#include "TransportBar.h"
#include "CommandRegistry.h"
#include "Theme.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
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
    setFixedSize (s == Symbol::play ? 44 : 36, s == Symbol::play ? 44 : 36);
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

    const auto r = QRectF (rect()).adjusted (1.5, 1.5, -1.5, -1.5);
    const bool hot = underMouse() || isDown();
    auto face = theme.color (QStringLiteral ("button"));
    if (isChecked())
        face = theme.color (QStringLiteral ("highlight"));
    else if (hot)
        face = face.lighter (theme.dark ? 135 : 110);
    g.setPen (QPen (theme.color (hot ? QStringLiteral ("highlight") : QStringLiteral ("mid")), 1.2));
    g.setBrush (face);
    g.drawEllipse (r);

    auto ink = isChecked() ? theme.color (QStringLiteral ("highlightedText")) : theme.color (QStringLiteral ("buttonText"));
    if (! isEnabled())
        ink = theme.color (QStringLiteral ("disabledText"));
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
            // Two arrows chasing round a rounded rectangle.
            g.setBrush (Qt::NoBrush);
            g.setPen (QPen (ink, 0.45 * u, Qt::SolidLine, Qt::FlatCap));
            const QRectF box (c.x() - 1.7 * u, c.y() - 1.1 * u, 3.4 * u, 2.2 * u);
            g.drawArc (box, 30 * 16, 150 * 16);
            g.drawArc (box, 210 * 16, 150 * 16);
            g.setPen (Qt::NoPen);
            g.setBrush (ink);
            QPainterPath a;
            a.moveTo (box.right() - 0.15 * u, c.y() - 0.9 * u);
            a.lineTo (box.right() + 0.65 * u, c.y() + 0.25 * u);
            a.lineTo (box.right() - 0.75 * u, c.y() + 0.35 * u);
            a.closeSubpath();
            g.drawPath (a);
            QPainterPath b;
            b.moveTo (box.left() + 0.15 * u, c.y() + 0.9 * u);
            b.lineTo (box.left() - 0.65 * u, c.y() - 0.25 * u);
            b.lineTo (box.left() + 0.75 * u, c.y() - 0.35 * u);
            b.closeSubpath();
            g.drawPath (b);
            break;
        }
    }
}

//==============================================================================
TimeDisplay::TimeDisplay (QWidget* parent) : QWidget (parent)
{
    setFocusPolicy (Qt::NoFocus);
    setCursor (Qt::PointingHandCursor);
    setAccessibleName (tr ("Playback position"));
    format = (Format) std::clamp (QSettings().value (QStringLiteral ("transport/timeFormat"), 0).toInt(), 0, 3);
    setToolTip (tr ("Playback position. Click to change how it is shown."));
}

void TimeDisplay::setPosition (Frame p, double sampleRate)
{
    if (p != position || std::abs (sampleRate - rate) > 0.0)
    {
        position = p;
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        update();
    }
}

QString TimeDisplay::text() const
{
    const auto seconds = (double) position / rate;
    const auto whole = (long long) std::floor (seconds);
    const auto h = whole / 3600, m = (whole / 60) % 60, s = whole % 60;
    auto two = [] (long long v) { return QStringLiteral ("%1").arg (v, 2, 10, QLatin1Char ('0')); };
    switch (format)
    {
        case Format::seconds:  return QStringLiteral ("%1 s").arg (seconds, 0, 'f', 3);
        case Format::samples:  return QStringLiteral ("%1").arg (position);
        case Format::cdFrames:
            return QStringLiteral ("%1 h %2 m %3 s %4 f").arg (two (h), two (m), two (s), two ((long long) ((seconds - (double) whole) * 75.0)));
        case Format::hms:
        default:
            return QStringLiteral ("%1 h %2 m %3.%4 s").arg (two (h), two (m), two (s))
                .arg ((long long) ((seconds - (double) whole) * 1000.0), 3, 10, QLatin1Char ('0'));
    }
}

QSize TimeDisplay::sizeHint() const
{
    QFont f = font();
    f.setPointSizeF (f.pointSizeF() * 1.6);
    f.setFamily (QStringLiteral ("monospace"));
    return { QFontMetrics (f).horizontalAdvance (QStringLiteral ("00 h 00 m 00 s 00 f")) + 28, 44 };
}

void TimeDisplay::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    g.setPen (QPen (theme.color (QStringLiteral ("mid")), 1.0));
    g.setBrush (theme.color (QStringLiteral ("waveformBackground")));
    g.drawRoundedRect (QRectF (rect()).adjusted (0.5, 2.5, -0.5, -2.5), 6, 6);

    QFont f = font();
    f.setPointSizeF (f.pointSizeF() * 1.6);
    f.setFamily (QStringLiteral ("monospace"));
    f.setStyleHint (QFont::Monospace);
    g.setFont (f);
    g.setPen (theme.color (QStringLiteral ("waveform")));
    g.drawText (rect(), Qt::AlignCenter, text());
}

void TimeDisplay::mousePressEvent (QMouseEvent*) { chooseFormat(); }

void TimeDisplay::keyPressEvent (QKeyEvent* e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Space)
        chooseFormat();
    else
        QWidget::keyPressEvent (e);
}

void TimeDisplay::chooseFormat()
{
    QMenu menu (this);
    const std::pair<Format, QString> choices[] {
        { Format::hms, tr ("Hours, minutes, seconds and milliseconds") },
        { Format::seconds, tr ("Seconds") },
        { Format::samples, tr ("Samples") },
        { Format::cdFrames, tr ("Hours, minutes, seconds and CD frames (75 a second)") },
    };
    for (const auto& [f, label] : choices)
    {
        auto* a = menu.addAction (label);
        a->setCheckable (true);
        a->setChecked (f == format);
        connect (a, &QAction::triggered, this, [this, f = f]
        {
            format = f;
            QSettings().setValue (QStringLiteral ("transport/timeFormat"), (int) f);
            updateGeometry();
            update();
        });
    }
    menu.exec (mapToGlobal (QPoint (0, height())));
}

//==============================================================================
TransportBar::TransportBar (CommandRegistry& registry, QWidget* parent)
    : QWidget (parent), commands (registry)
{
    setAccessibleName (tr ("Transport"));
    using S = TransportButton::Symbol;
    auto* row = new QHBoxLayout (this);
    row->setContentsMargins (8, 4, 8, 4);
    row->setSpacing (6);
    row->addStretch (1);
    row->addWidget (new TransportButton (S::start, QStringLiteral ("transport.start"), commands, this));
    row->addWidget (playButton = new TransportButton (S::play, QStringLiteral ("transport.playStop"), commands, this));
    row->addWidget (new TransportButton (S::stop, QStringLiteral ("transport.stop"), commands, this));
    row->addWidget (new TransportButton (S::end, QStringLiteral ("transport.end"), commands, this));
    row->addWidget (new TransportButton (S::record, QStringLiteral ("file.record"), commands, this));
    row->addWidget (loopButton = new TransportButton (S::loop, QStringLiteral ("transport.loop"), commands, this));
    row->addSpacing (14);
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
