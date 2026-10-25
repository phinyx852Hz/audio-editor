// SPDX-License-Identifier: AGPL-3.0-only
#include "AliasScope.h"
#include "Theme.h"
#include "../engine/PluginProbe.h"

#include <QKeyEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{
    // The level at which aliasing is a worry, and at which it is plainly heard.
    constexpr double faintDb = -96.0, audibleDb = -60.0, floorDb = -140.0;

    double worst (const measure::AliasPoint& p) { return std::max (p.aliasDb[0], p.aliasDb[1]); }
}

AliasScope::AliasScope (AudioEngine& e, QWidget* parent)
    : QWidget (parent), engine (e), runner (new ProbeRunner (e, this))
{
    setAccessibleName (tr ("Aliasing scope"));
    setToolTip (tr ("Aliasing: harmonics folded back from above Nyquist, for tones from 1 kHz to 20 kHz through the chain."
                    " Measured on copies of the plugins, again when a setting changes. Double-click for the Plugin Analyzer"));
    setFocusPolicy (Qt::StrongFocus);
    runner->setForensics (false);
    connect (runner, &ProbeRunner::started, this, [this] { state = tr ("measuring"); update(); });
    connect (runner, &ProbeRunner::finished, this, &AliasScope::display);
    connect (runner, &ProbeRunner::problem, this, [this] (const QString& why) { state = why; update(); });
    restart();
}

void AliasScope::setInserts (const mixer::Inserts& list)
{
    master = false;
    inserts = list;
    restart();
}

void AliasScope::followMaster()
{
    master = true;
    inserts.clear();
    restart();
}

void AliasScope::restart()
{
    rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    measure::Settings s;
    s.sampleRate = rate;
    s.levelDb = -6.0;   // mastering levels: where saturation and clipping fold the most
    s.linear = s.harmonics = s.dynamics = s.modulation = false;
    s.aliasing = true;
    s.fftOrder = 14;
    s.aliasTones = { 1000.0, 2500.0, 5000.0, 7500.0, 10000.0, 12500.0, 15000.0, 17500.0, 20000.0 };
    runner->setSettings (s);
    points.clear();
    title.clear();
    state = tr ("waiting");
    if (isVisible())
        runner->setProbe (std::make_shared<PluginProbe> (engine, master ? PluginProbe::masterSection (engine) : PluginProbe::inserts (inserts), rate, 512));
    update();
}

void AliasScope::showEvent (QShowEvent*)
{
    restart();
}

void AliasScope::hideEvent (QHideEvent*)
{
    runner->setProbe (nullptr);   // nothing to measure while nobody looks
}

void AliasScope::display (const ProbeRunner::Outcome& o)
{
    points = o.results.alias;
    title = QString::fromStdString (o.title);
    state.clear();
    if (o.results.silent)
        state = tr ("silent");
    else if (! points.empty())
    {
        const auto& p = *std::max_element (points.begin(), points.end(), [] (const auto& a, const auto& b) { return worst (a) < worst (b); });
        const auto w = worst (p);
        state = w <= floorDb ? tr ("no aliasing")
              : tr ("worst %1 dB at %2 kHz").arg (w, 0, 'f', 0).arg (p.toneHz / 1000.0, 0, 'g', 3);
    }
    setAccessibleDescription (title + QStringLiteral (": ") + state);
    update();
}

void AliasScope::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.fillRect (rect(), theme.color (QStringLiteral ("meterBackground")));
    g.setRenderHint (QPainter::Antialiasing);
    auto text = theme.color (QStringLiteral ("text"));
    auto dim = text;
    dim.setAlphaF (0.6);
    const auto fm = fontMetrics();

    g.setPen (text);
    const auto heading = tr ("Aliasing") + QStringLiteral ("  ") + (master ? tr ("Master Section") : tr ("Inserts"));
    g.drawText (QPointF (6, fm.ascent() + 3), heading);
    g.setPen (dim);
    g.drawText (QRectF (fm.horizontalAdvance (heading) + 16, 3, width() - fm.horizontalAdvance (heading) - 22, fm.height()),
                Qt::AlignRight | Qt::AlignVCenter, fm.elidedText (state, Qt::ElideRight, width() / 2));

    const QRectF area (6, fm.height() + 8, width() - 12, height() - fm.height() * 2 - 14);
    if (area.height() < 8)
        return;
    // Lines at the levels that matter.
    const auto toY = [&] (double v) { return area.bottom() - (std::clamp (v, floorDb, 0.0) - floorDb) / -floorDb * area.height(); };
    g.setPen (QPen (theme.color (QStringLiteral ("mid")), 0.5, Qt::DashLine));
    for (auto v : { audibleDb, faintDb })
    {
        g.drawLine (QPointF (area.left(), toY (v)), QPointF (area.right(), toY (v)));
        g.drawText (QPointF (area.right() - fm.horizontalAdvance (QStringLiteral ("-96 dB")), toY (v) - 2), QString::number (v, 'f', 0) + QStringLiteral (" dB"));
    }
    if (points.empty())
    {
        g.setPen (dim);
        g.drawText (area, Qt::AlignCenter, title.isEmpty() ? tr ("Nothing to measure yet") : title);
        return;
    }

    const auto slot = area.width() / (double) points.size();
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto& p = points[i];
        const auto w = worst (p);
        const auto colour = theme.color (w > audibleDb ? QStringLiteral ("meterHigh") : w > faintDb ? QStringLiteral ("accent") : QStringLiteral ("scope"));
        const auto x = area.left() + slot * (double) i;
        if (w > floorDb)
            g.fillRect (QRectF (x + slot * 0.2, toY (w), slot * 0.6, area.bottom() - toY (w)), colour);
        g.setPen (dim);
        const auto label = QString::number (std::round (p.toneHz / 100.0) / 10.0, 'g', 3) + (i == 0 ? QStringLiteral (" kHz") : QString());
        g.drawText (QRectF (x, area.bottom() + 2, slot, fm.height()), Qt::AlignHCenter | Qt::AlignTop, label);
    }
}

void AliasScope::mouseDoubleClickEvent (QMouseEvent*)
{
    Q_EMIT analyzeRequested();
}

void AliasScope::keyPressEvent (QKeyEvent* e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_A)
        Q_EMIT analyzeRequested();
    else
        QWidget::keyPressEvent (e);
}
