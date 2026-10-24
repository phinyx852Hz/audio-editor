// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiMark.h"

#include <QPainter>
#include <QPainterPath>

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

PhiButton::PhiButton (QWidget* parent) : QToolButton (parent)
{
    setAutoRaise (true);
    setAccessibleName (QString::fromUtf8 ("Φ"));
    const auto side = fontMetrics().height();
    setIconSize (QSize (side, side));
    setIcon (QIcon (phimark::pixmap (side, phimark::Style::compact, devicePixelRatioF())));
}
