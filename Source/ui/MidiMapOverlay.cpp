// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiMapOverlay.h"

#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>

MidiMapOverlay::MidiMapOverlay (MixerWindow& m, control::Learn& l) : QWidget (&m), mixer (m), learn (l)
{
    setFocusPolicy (Qt::StrongFocus);
    setGeometry (mixer.rect());
    mixer.installEventFilter (this);
    connect (&redraw, &QTimer::timeout, this, qOverload<> (&QWidget::update));
    redraw.start (150);
    raise();
    show();
    setFocus();
}

void MidiMapOverlay::learned()
{
    // One mapping for each control, as in Live: what was mapped to it before gives way.
    auto all = learn.mappings();
    if (chosen.has_value() && ! all.empty() && same (all.back().target, *chosen))
    {
        const auto last = all.back();
        all.pop_back();
        all.erase (std::remove_if (all.begin(), all.end(), [&last] (const control::Mapping& m) { return same (m.target, last.target); }), all.end());
        all.push_back (last);
        learn.setMappings (all);
    }
    chosen.reset();
    update();
}

bool MidiMapOverlay::eventFilter (QObject* o, QEvent* e)
{
    if (o == &mixer && (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest))
    {
        setGeometry (mixer.rect());
        raise();
    }
    return QWidget::eventFilter (o, e);
}

std::vector<MidiMapOverlay::Shown> MidiMapOverlay::shown() const
{
    std::vector<Shown> out;
    for (const auto& c : mixer.mappable())
    {
        auto* w = c.widget;
        if (w == nullptr || ! w->isVisible() || w->window() != mixer.window())
            continue;
        const auto local = c.area.isEmpty() ? w->rect() : c.area;
        const auto visible = local.intersected (w->visibleRegion().boundingRect());
        if (visible.width() < 4 || visible.height() < 4)
            continue;
        out.push_back ({ QRect (w->mapTo (&mixer, visible.topLeft()), visible.size()), c });
    }
    return out;
}

bool MidiMapOverlay::same (const control::Command& a, const control::Command& b)
{
    return a.action == b.action && a.track == b.track && a.scene == b.scene
           && (a.action != control::Action::send || a.index == b.index)
           && (a.action != control::Action::selectBy || (int) a.value == (int) b.value);
}

QString MidiMapOverlay::label (const control::Command& c)
{
    const auto* t = control::targetOf (c);
    auto text = t != nullptr ? QApplication::translate ("MainWindow", t->name) : QString();
    if (c.track >= 0)
        text = QApplication::translate ("MainWindow", "Track %1: %2").arg (c.track + 1).arg (text);
    if (c.scene >= 0)
        text += QApplication::translate ("MainWindow", ", scene %1").arg (c.scene + 1);
    return text;
}

QString MidiMapOverlay::shortSource (const control::Mapping& m)
{
    switch (m.source)
    {
        case control::Mapping::Source::cc: return QStringLiteral ("CC %1").arg (m.number);
        case control::Mapping::Source::note: return QStringLiteral ("N %1").arg (m.number);
        case control::Mapping::Source::pitchBend: return QStringLiteral ("PB");
    }
    return {};
}

void MidiMapOverlay::paintEvent (QPaintEvent*)
{
    QPainter p (this);
    p.fillRect (rect(), QColor (0, 0, 0, 40));
    const auto mappings = learn.mappings();
    const QColor free (90, 110, 255, 110), mapped (80, 200, 150, 140), outline (255, 255, 255);
    QFont small = font();
    small.setPixelSize (10);
    small.setBold (true);
    for (const auto& s : shown())
    {
        QStringList sources;
        for (const auto& m : mappings)
            if (same (m.target, s.control.target))
                sources << shortSource (m);
        p.fillRect (s.rect, sources.isEmpty() ? free : mapped);
        if (chosen.has_value() && same (*chosen, s.control.target))
        {
            p.setPen (QPen (outline, 2));
            p.setBrush (Qt::NoBrush);
            p.drawRect (s.rect.adjusted (1, 1, -1, -1));
        }
        if (! sources.isEmpty())
        {
            p.setFont (small);
            p.setPen (outline);
            p.drawText (s.rect.adjusted (1, 0, -1, 0), Qt::AlignCenter | Qt::TextWrapAnywhere, sources.join (QLatin1Char ('\n')));
        }
    }

    // What to do, along the top.
    const auto text = chosen.has_value()
                          ? QApplication::translate ("MainWindow", "MIDI Map: move a knob or fader, or press a key or button, for %1. "
                                                                   "Delete removes its mappings; Escape or Ctrl+M leaves.")
                                .arg (label (*chosen))
                          : QApplication::translate ("MainWindow", "MIDI Map: click a control, then move a knob or fader, or press a key or "
                                                                   "button, on any keyboard or controller. Escape or Ctrl+M leaves.");
    QFont bold = font();
    bold.setBold (true);
    p.setFont (bold);
    const int bannerHeight = p.fontMetrics().height() + 10;   // along the bottom, clear of the transport
    const QRect banner (0, height() - bannerHeight, width(), bannerHeight);
    p.fillRect (banner, QColor (40, 50, 140, 230));
    p.setPen (QColor (255, 255, 255));
    p.drawText (banner.adjusted (10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                p.fontMetrics().elidedText (text, Qt::ElideRight, banner.width() - 20));
}

void MidiMapOverlay::mousePressEvent (QMouseEvent* e)
{
    // The smallest control under the click (a slot, rather than the grid around it).
    const Shown* hit = nullptr;
    const auto list = shown();
    for (const auto& s : list)
        if (s.rect.contains (e->position().toPoint())
            && (hit == nullptr || s.rect.width() * s.rect.height() < hit->rect.width() * hit->rect.height()))
            hit = &s;
    if (hit == nullptr)
    {
        chosen.reset();
        learn.cancel();
    }
    else
    {
        chosen = hit->control.target;
        learn.learn (hit->control.target, hit->control.continuous);
    }
    setFocus();
    update();
}

void MidiMapOverlay::wheelEvent (QWheelEvent* e)
{
    // Scrolling still scrolls what is under the overlay (the strips, the launcher).
    setAttribute (Qt::WA_TransparentForMouseEvents, true);
    auto* under = mixer.childAt (mapTo (&mixer, e->position().toPoint()));
    setAttribute (Qt::WA_TransparentForMouseEvents, false);
    if (under == nullptr)
        return;
    const auto at = under->mapFrom (this, e->position());
    QWheelEvent copy (at, e->globalPosition(), e->pixelDelta(), e->angleDelta(), e->buttons(), e->modifiers(), e->phase(), e->inverted());
    QApplication::sendEvent (under, &copy);
    update();
}

void MidiMapOverlay::keyPressEvent (QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape)
    {
        if (finished)
            finished();
        return;
    }
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && chosen.has_value())
    {
        auto all = learn.mappings();
        all.erase (std::remove_if (all.begin(), all.end(), [this] (const control::Mapping& m) { return same (m.target, *chosen); }), all.end());
        learn.setMappings (all);
        learn.cancel();
        chosen.reset();
        if (mappingsChanged)
            mappingsChanged();
        update();
        return;
    }
    QWidget::keyPressEvent (e);
}
