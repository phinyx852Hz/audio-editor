// SPDX-License-Identifier: AGPL-3.0-only
#include "InfoStrip.h"
#include "Theme.h"

#include <QEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{
    qreal digitWidth (const QFontMetricsF& fm)
    {
        qreal w = 0.0;
        for (char c = '0'; c <= '9'; ++c)
            w = std::max (w, fm.horizontalAdvance (QChar (c)));
        return w;
    }

    // Splits text into runs of digits and runs of anything else, calling
    // f (run, isDigits) for each, in order.
    template <typename F>
    void forEachRun (const QString& text, F&& f)
    {
        int i = 0;
        while (i < text.size())
        {
            const bool digits = text[i].isDigit();
            int j = i;
            while (j < text.size() && text[j].isDigit() == digits)
                ++j;
            f (text.mid (i, j - i), digits);
            i = j;
        }
    }

    constexpr qreal margin = 10.0, gap = 22.0;
}

qreal steady::width (const QFontMetricsF& fm, const QString& text)
{
    const auto cell = digitWidth (fm);
    qreal w = 0.0;
    forEachRun (text, [&] (const QString& run, bool digits) { w += digits ? cell * run.size() : fm.horizontalAdvance (run); });
    return w;
}

void steady::draw (QPainter& g, qreal left, qreal baseline, const QString& text)
{
    const QFontMetricsF fm (g.font());
    const auto cell = digitWidth (fm);
    auto x = left;
    forEachRun (text, [&] (const QString& run, bool digits)
    {
        if (! digits)
        {
            g.drawText (QPointF (x, baseline), run);
            x += fm.horizontalAdvance (run);
            return;
        }
        for (const auto ch : run)   // each digit centred in its cell
        {
            g.drawText (QPointF (x + (cell - fm.horizontalAdvance (ch)) / 2.0, baseline), QString (ch));
            x += cell;
        }
    });
}

//==============================================================================
InfoStrip::InfoStrip (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Status"));
    setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Fixed);
}

void InfoStrip::setFields (const QStringList& newFields, const QString& newRight)
{
    if (newFields == fields && newRight == right)
        return;

    fields = newFields;
    right = newRight;
    const QFontMetricsF fm (font());
    reserved.resize (std::max (reserved.size(), (size_t) fields.size()), 0.0);
    for (int i = 0; i < fields.size(); ++i)
        reserved[(size_t) i] = std::max (reserved[(size_t) i], steady::width (fm, fields[i]));
    setAccessibleDescription (text());
    update();
}

QString InfoStrip::text() const
{
    QStringList parts;
    for (const auto& f : fields)
        if (! f.isEmpty())
            parts << f;
    if (! right.isEmpty())
        parts << right;
    return parts.join (QStringLiteral ("   "));
}

QSize InfoStrip::sizeHint() const
{
    return { 400, (int) std::ceil (QFontMetricsF (font()).height()) + 10 };
}

QSize InfoStrip::minimumSizeHint() const { return { 0, sizeHint().height() }; }

void InfoStrip::changeEvent (QEvent* e)
{
    if (e->type() == QEvent::FontChange)
    {
        reserved.clear();   // measured in the old font
        const auto f = fields;
        fields.clear();
        setFields (f, right);
        updateGeometry();
    }
    QWidget::changeEvent (e);
}

void InfoStrip::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.setRenderHint (QPainter::TextAntialiasing);
    g.setPen (Theme::current().color (QStringLiteral ("windowText")));
    const QFontMetricsF fm (font());
    const auto baseline = (height() - fm.height()) / 2.0 + fm.ascent();

    auto x = margin;
    for (int i = 0; i < fields.size(); ++i)
    {
        steady::draw (g, x, baseline, fields[i]);
        if (reserved[(size_t) i] > 0.0)
            x += reserved[(size_t) i] + gap;
    }

    // The device on the right, shortened if it would run into the fields.
    if (! right.isEmpty())
    {
        const auto room = width() - margin - x;
        if (room > fm.averageCharWidth() * 4)
        {
            const auto shown = fm.elidedText (right, Qt::ElideRight, room);
            g.drawText (QPointF (width() - margin - fm.horizontalAdvance (shown), baseline), shown);
        }
    }
}
