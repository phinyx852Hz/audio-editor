// SPDX-License-Identifier: AGPL-3.0-only
#include "MontageView.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int laneGap = 3;
}

MontageView::MontageView (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Montage songs"));
    setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Fixed);
}

QSize MontageView::sizeHint() const
{
    return { 400, 2 * ((int) std::ceil (QFontMetricsF (font()).height()) + 10) + 3 * laneGap };
}

void MontageView::setMontage (std::shared_ptr<const Montage> m)
{
    montage = std::move (m);
    update();
}

void MontageView::setView (double s, double l, Frame c)
{
    // Exact comparison on purpose: the values come straight from the editor.
    if (std::abs (s - start) <= 0.0 && std::abs (l - length) <= 0.0 && c == cursor)
        return;
    start = s;
    length = l;
    cursor = c;
    update();
}

double MontageView::frameToX (double frame) const
{
    return length > 0.0 ? (frame - start) / length * width() : 0.0;
}

int MontageView::clipAtX (double x, double y) const
{
    if (montage == nullptr || length <= 0.0)
        return -1;
    const auto frame = start + x / std::max (1, width()) * length;
    const auto lane = y < height() / 2.0 ? 0 : 1;
    for (int i = 0; i < (int) montage->clips.size(); ++i)
        if (i % 2 == lane && frame >= (double) montage->clipStart (i) && frame < (double) montage->clipEnd (i))
            return i;
    return montage->clipAt ((Frame) frame);
}

void MontageView::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto& theme = Theme::current();
    const auto ink = theme.color (QStringLiteral ("windowText"));
    auto faint = ink;
    faint.setAlphaF (0.45f);
    g.setPen (faint);
    g.drawLine (QPointF (0, height() - 0.5), QPointF (width(), height() - 0.5));

    if (montage != nullptr && montage->clips.empty())
    {
        g.setPen (ink);
        g.drawText (rect().adjusted (8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    tr ("Empty montage: press Alt+Insert to add songs, or open files from the Find panel."));
        return;
    }
    if (montage == nullptr || length <= 0.0)
        return;

    g.setRenderHint (QPainter::Antialiasing);
    const auto laneHeight = (height() - 3.0 * laneGap) / 2.0;
    const auto current = montage->clipAt (cursor);
    const auto rate = montage->sampleRate;
    const QFontMetricsF fm (font());

    for (int i = 0; i < (int) montage->clips.size(); ++i)
    {
        const auto& c = montage->clips[(size_t) i];
        const auto s = montage->clipStart (i), e = montage->clipEnd (i);
        const auto x0 = frameToX ((double) s), x1 = frameToX ((double) e);
        if (x1 < 0 || x0 > width())
            continue;

        const auto top = laneGap + (i % 2) * (laneHeight + laneGap);
        const QRectF box (x0, top, std::max (1.0, x1 - x0), laneHeight);

        auto fill = theme.color (QStringLiteral ("waveform"));
        fill.setAlphaF (i == current ? 0.45f : 0.25f);
        g.setPen (Qt::NoPen);
        g.setBrush (fill);
        g.drawRoundedRect (box, 3, 3);

        // Fades and crossfades as slopes from the bottom corners.
        const auto fadeIn = std::max (montage->overlapBefore (i), c.fadeIn);
        const auto fadeOut = std::max (i + 1 < (int) montage->clips.size() ? montage->overlapBefore (i + 1) : 0, c.fadeOut);
        g.setPen (QPen (ink, 1.2));
        if (fadeIn > 0)
            g.drawLine (QPointF (x0, box.bottom()), QPointF (frameToX ((double) (s + fadeIn)), box.top() + 1));
        if (fadeOut > 0)
            g.drawLine (QPointF (frameToX ((double) (e - fadeOut)), box.top() + 1), QPointF (x1, box.bottom()));

        if (i == current)
        {
            g.setPen (QPen (theme.color (QStringLiteral ("accent")), 2.0));
            g.setBrush (Qt::NoBrush);
            g.drawRoundedRect (box.adjusted (1, 1, -1, -1), 3, 3);
        }

        // Number, name and gain, kept in view while the song is.
        auto text = QStringLiteral ("%1. %2").arg (i + 1).arg (QString::fromStdString (montage->clipName (i)));
        if (std::abs (c.gainDb) > 0.005)
            text += QStringLiteral ("   %1%2 dB").arg (c.gainDb > 0 ? QStringLiteral ("+") : QString()).arg (c.gainDb, 0, 'f', 1);
        const auto textLeft = std::max (x0, 0.0) + 6.0;
        const QRectF textBox (textLeft, top, std::max (0.0, std::min (x1, (double) width()) - textLeft - 4.0), laneHeight);
        g.setPen (ink);
        g.drawText (textBox, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText (text, Qt::ElideRight, textBox.width()));

        // The pause before the song, written in the gap when it fits.
        if (i > 0 && c.pauseBefore > 0)
        {
            const auto gapLeft = frameToX ((double) montage->clipEnd (i - 1));
            const auto label = tr ("%1 s").arg ((double) c.pauseBefore / rate, 0, 'f', 1);
            if (x0 - gapLeft > fm.horizontalAdvance (label) + 6)
            {
                g.setPen (faint);
                g.drawText (QRectF (gapLeft, 0, x0 - gapLeft, height()), Qt::AlignCenter, label);
            }
        }
    }

    const auto cx = frameToX ((double) cursor);
    if (cx >= 0 && cx <= width())
    {
        g.setRenderHint (QPainter::Antialiasing, false);
        g.setPen (theme.color (QStringLiteral ("cursor")));
        g.drawLine (QPointF (cx, 0), QPointF (cx, height()));
    }
}

void MontageView::mousePressEvent (QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || montage == nullptr)
        return;
    const auto i = clipAtX (e->position().x(), e->position().y());
    if (i >= 0)
        Q_EMIT cursorRequested (montage->clipStart (i));
}

void MontageView::mouseDoubleClickEvent (QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && montage != nullptr && clipAtX (e->position().x(), e->position().y()) >= 0)
        Q_EMIT propertiesRequested();
}
