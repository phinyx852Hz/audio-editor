// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiMark.h"

#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double phi = 1.6180339887498949;

    // The mark's outline in a box 618.03 wide and 1000 tall (see PhiMark.h for the proportions).
    // The compact mark is as tall as the ring is wide (618.03, a square), with no serifs.
    QPainterPath outline (bool compact)
    {
        const double height = compact ? 1000.0 / phi : 1000.0;
        const double ringWidth = 1000.0 / phi;
        const double ringHeight = ringWidth / phi;
        const double stem = 1000.0 / std::pow (phi, 5.0);
        const double serifWidth = stem * phi * phi;
        const double serifHeight = stem / (phi * phi);
        const QPointF centre (ringWidth / 2.0, height / 2.0);

        QPainterPath ring;
        ring.setFillRule (Qt::OddEvenFill);
        ring.addEllipse (centre, ringWidth / 2.0, ringHeight / 2.0);
        ring.addEllipse (centre, ringWidth / 2.0 - stem, ringHeight / 2.0 - stem / phi);

        // Joined with united() rather than added, so the overlaps don't cancel out as holes.
        const auto rect = [] (QRectF r) { QPainterPath p; p.addRect (r); return p; };
        auto shape = ring.united (rect (QRectF (centre.x() - stem / 2.0, 0.0, stem, height)));
        if (! compact)
        {
            shape = shape.united (rect (QRectF (centre.x() - serifWidth / 2.0, 0.0, serifWidth, serifHeight)));
            shape = shape.united (rect (QRectF (centre.x() - serifWidth / 2.0, height - serifHeight, serifWidth, serifHeight)));
        }
        return shape;
    }

    const QColor gold (0xc8, 0xa2, 0x4a);
    const QColor black (0x0b, 0x0b, 0x0c);

    // Paints the mark centred in `area`: on a black rounded tile when gold, bare when white.
    void paint (QPainter& g, const QRectF& area, phimark::Style style)
    {
        g.setRenderHint (QPainter::Antialiasing);

        auto inner = area;
        if (style == phimark::Style::goldOnBlack)
        {
            const auto radius = area.height() / (phi * phi * phi);
            g.setPen (Qt::NoPen);
            g.setBrush (black);
            g.drawRoundedRect (area, radius, radius);
            const auto margin = area.height() / (phi * phi * phi * phi);   // about 15 %
            inner = area.adjusted (margin, margin, -margin, -margin);
        }

        static const auto full = outline (false);
        static const auto compact = outline (true);
        const auto& path = style == phimark::Style::compact ? compact : full;
        const auto scale = inner.height() / path.boundingRect().height();
        g.save();
        g.translate (inner.center().x() - path.boundingRect().width() * scale / 2.0,
                     inner.center().y() - path.boundingRect().height() * scale / 2.0);
        g.scale (scale, scale);
        g.setPen (Qt::NoPen);
        g.setBrush (style == phimark::Style::goldOnBlack ? gold : QColor (Qt::white));
        g.drawPath (path);
        g.restore();
    }
}

namespace phimark
{
    QPixmap pixmap (int height, Style style, qreal devicePixelRatio)
    {
        QPixmap p (QSize (height, height) * devicePixelRatio);
        p.setDevicePixelRatio (devicePixelRatio);
        p.fill (Qt::transparent);
        QPainter g (&p);
        paint (g, QRectF (0, 0, height, height), style);
        return p;
    }

    void makePhiWindow (QWidget* window)
    {
        QPalette p = window->palette();
        const QColor text (0xe8, 0xe4, 0xda);
        for (const auto role : { QPalette::Window, QPalette::Base, QPalette::Button })
            p.setColor (role, role == QPalette::Window ? black : QColor (0x1a, 0x1a, 0x1c));
        p.setColor (QPalette::AlternateBase, QColor (0x14, 0x14, 0x16));
        for (const auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
            p.setColor (role, text);
        p.setColor (QPalette::PlaceholderText, QColor (0x8a, 0x86, 0x7e));
        p.setColor (QPalette::Highlight, gold);
        p.setColor (QPalette::HighlightedText, black);
        window->setPalette (p);
        window->setAutoFillBackground (true);
    }

    bool isPhiPlugin (const std::string& pluginId)
    {
        return pluginId.rfind ("phinyx:phi-", 0) == 0;
    }

    QString withoutPhi (const QString& name)
    {
        return name.startsWith (QString::fromUtf8 ("Φ ")) ? name.mid (2) : name;
    }
}

PhiMark::PhiMark (int height, QWidget* parent) : QWidget (parent)
{
    setFixedSize (height, height);
    setAttribute (Qt::WA_TransparentForMouseEvents);
}

void PhiMark::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    paint (g, QRectF (rect()), phimark::Style::goldOnBlack);
}

PhiButton::PhiButton (const QString& accessibleName, QWidget* parent, bool isSwitch) : QToolButton (parent)
{
    setCheckable (isSwitch);
    setAutoRaise (true);
    setFocusPolicy (Qt::StrongFocus);
    setAccessibleName (accessibleName);
    const auto side = fontMetrics().height();
    setIconSize (QSize (side, side));
    // On: the mark in full white. Off: the same mark faded to 1/φ², so the state reads at a glance.
    QIcon icon;
    for (const qreal ratio : { 1.0, 2.0 })
    {
        const auto on = phimark::pixmap (side, phimark::Style::compact, ratio);
        QPixmap off (on.size());
        off.setDevicePixelRatio (ratio);
        off.fill (Qt::transparent);
        QPainter g (&off);
        g.setOpacity (1.0 / (phi * phi));
        g.drawPixmap (0, 0, on);
        g.end();
        icon.addPixmap (on, QIcon::Normal, QIcon::On);
        icon.addPixmap (isSwitch ? off : on, QIcon::Normal, QIcon::Off);
    }
    setIcon (icon);
}

//==============================================================================
PhiSplash::PhiSplash() : QWidget (nullptr, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute (Qt::WA_TranslucentBackground);   // the rounded corners
    setAttribute (Qt::WA_DeleteOnClose);
    const auto h = (int) std::lround (1000.0 / (phi * phi));   // 382
    resize ((int) std::lround (h * phi), h);                    // 618 x 382, a golden rectangle
    if (const auto* screen = QGuiApplication::primaryScreen())
        move (screen->availableGeometry().center() - rect().center());
    shown.start();
    if (startupSound())
        QTimer::singleShot (0, this, [] { startupSound()(); });
}

void PhiSplash::finish (QWidget* mainWindow)
{
    const auto left = std::max<qint64> (0, (qint64) std::lround (phi * 1000.0) - shown.elapsed());
    QTimer::singleShot ((int) left, this, [this, mainWindow]
    {
        close();
        if (mainWindow != nullptr)
            mainWindow->activateWindow();
    });
}

std::function<void()>& PhiSplash::startupSound()
{
    static std::function<void()> sound;
    return sound;
}

void PhiSplash::paintEvent (QPaintEvent*)
{
    // A golden rectangle: the mark in its square on the left, the name in the golden
    // rectangle that is left over, and the golden spiral drawn faintly through both, a
    // quarter turn in each square as the rectangle divides again and again.
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const QRectF all (rect());
    const auto radius = all.height() / (phi * phi * phi * phi);
    g.setPen (Qt::NoPen);
    g.setBrush (black);
    g.drawRoundedRect (all, radius, radius);

    auto spiral = gold;
    spiral.setAlphaF (0.16f);
    g.setPen (QPen (spiral, 1.2));
    g.setBrush (Qt::NoBrush);
    QRectF left = all;
    for (int turn = 0; turn < 10 && left.width() > 1.0 && left.height() > 1.0; ++turn)
    {
        const auto side = std::min (left.width(), left.height());
        QRectF square;
        QPointF centre;
        int start = 0;
        switch (turn % 4)
        {
            case 0: square = QRectF (left.left(), left.top(), side, side);  centre = square.bottomRight(); start = 90;  left.setLeft (square.right()); break;
            case 1: square = QRectF (left.left(), left.top(), side, side);  centre = square.bottomLeft();  start = 0;   left.setTop (square.bottom()); break;
            case 2: square = QRectF (left.right() - side, left.top(), side, side); centre = square.topLeft(); start = 270; left.setRight (square.left()); break;
            default: square = QRectF (left.left(), left.bottom() - side, side, side); centre = square.topRight(); start = 180; left.setBottom (square.top()); break;
        }
        if (turn == 0)
        {
            g.drawLine (QPointF (square.right(), all.top() + radius), QPointF (square.right(), all.bottom() - radius));
            paint (g, square, phimark::Style::goldOnBlack);
            g.setPen (QPen (spiral, 1.2));
            g.setBrush (Qt::NoBrush);
        }
        g.drawArc (QRectF (centre.x() - side, centre.y() - side, 2 * side, 2 * side), start * 16, 90 * 16);
    }

    // The name and version in the leftover rectangle, the name's baseline on its golden section.
    const QRectF text (all.height(), 0, all.width() - all.height(), all.height());
    auto f = font();
    f.setPixelSize ((int) std::lround (text.width() / (phi * phi)));
    f.setWeight (QFont::DemiBold);
    f.setLetterSpacing (QFont::PercentageSpacing, 108);
    g.setFont (f);
    g.setPen (QColor (0xe8, 0xe4, 0xda));
    const auto baseline = text.top() + text.height() / phi;
    g.drawText (QRectF (text.left(), text.top(), text.width(), baseline - text.top()), Qt::AlignHCenter | Qt::AlignBottom, QStringLiteral ("PHI"));
    f.setPixelSize (std::max (9, (int) std::lround (text.width() / (phi * phi * phi * phi * phi))));
    f.setWeight (QFont::Normal);
    f.setLetterSpacing (QFont::PercentageSpacing, 100);
    g.setFont (f);
    g.setPen (gold);
    g.drawText (QRectF (text.left(), baseline + text.height() / (phi * phi * phi * phi * phi * phi), text.width(), text.height() / 8),
                Qt::AlignHCenter | Qt::AlignTop, QCoreApplication::applicationVersion());
}

void PhiSplash::mousePressEvent (QMouseEvent*)
{
    close();
}
