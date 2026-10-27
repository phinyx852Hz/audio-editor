// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiEditor.h"

#include "../core/Scala.h"
#include "../mixer/Tunings.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace noteedit;

namespace
{
    enum Tool { toolPick, toolDraw, toolErase, toolSplit, toolGlue };

    constexpr int keyWidth = 96, rulerHeight = 22, laneHeight = 110, laneGap = 6;
    constexpr int laneVelocity = -1;

    const char* const rootNames[12] { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" };

    // The grids, in quarter notes.
    struct GridChoice { const char* name; double quarters; };
    const GridChoice grids[] { { "1/1", 4.0 }, { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 },
                               { "1/4 triplet", 2.0 / 3.0 }, { "1/8 triplet", 1.0 / 3.0 }, { "1/16 triplet", 1.0 / 6.0 } };

    // C major and minor in 12-TET, the systems a "regular" file is usually in, for restyling from.
    PitchSystem majorOrMinor (int root, bool minor)
    {
        PitchSystem s;
        s.name = minor ? "minor" : "major";
        s.family = "equal";
        s.degrees = minor ? std::vector<double> { 0, 200, 300, 500, 700, 800, 1000 } : std::vector<double> { 0, 200, 400, 500, 700, 900, 1100 };
        s.tonic = 6000.0 + 100.0 * root;
        return s;
    }

    double hzOf (double pitch) { return 440.0 * std::pow (2.0, (pitch - 6900.0) / 1200.0); }

    QString barText (int64_t tick, int tpq)
    {
        const auto bar = (int64_t) 4 * tpq;
        return QStringLiteral ("%1.%2.%3").arg (tick / bar + 1).arg ((tick % bar) / tpq + 1).arg (tick % tpq);
    }
}

//==============================================================================
// The roll: the keyboard down the left with the system's names, the bar ruler along the top, the
// notes, and the lane under them. Its own scroll bars; Ctrl and the wheel zoom in time, Alt and the
// wheel the rows.
class MidiEditor::Roll final : public QAbstractScrollArea
{
public:
    explicit Roll (MidiEditor& e) : QAbstractScrollArea (&e), ed (e)
    {
        setMouseTracking (true);
        viewport()->setMouseTracking (true);
        setFocusPolicy (Qt::StrongFocus);
        setMinimumSize (640, 360);
    }

    int tool = toolPick;
    bool byPitch = false;   // a cents axis (a row a degree otherwise)

    void rebuild()
    {
        rows = ed.system.steps (0.0, 12700.0);
        pitches.clear();
        for (auto s : rows)
            pitches.push_back (ed.system.pitchOf (s));
        updateBars();
        viewport()->update();
    }

    void showNotes()
    {
        double low = 1.0e9, high = -1.0e9;
        for (const auto& n : ed.seq.notes)
        {
            low = std::min (low, n.pitch);
            high = std::max (high, n.pitch);
        }
        if (low > high)
            low = high = ed.system.tonic + ed.system.period / 2.0;
        const auto mid = (centerY (low) + centerY (high)) / 2.0;
        updateBars();
        verticalScrollBar()->setValue ((int) (mid - gridHeight() / 2.0));
        horizontalScrollBar()->setValue (0);
    }

    void refresh() { updateBars(); viewport()->update(); }

    void keyPressEvent (QKeyEvent* e) override { ed.keyPressEvent (e); }

protected:
    void resizeEvent (QResizeEvent* e) override
    {
        QAbstractScrollArea::resizeEvent (e);
        updateBars();
    }

    void scrollContentsBy (int, int) override { viewport()->update(); }

    void paintEvent (QPaintEvent*) override
    {
        QPainter g (viewport());
        const auto pal = palette();
        const auto w = viewport()->width();
        const auto gridBottom = rulerHeight + gridHeight();
        g.fillRect (viewport()->rect(), pal.color (QPalette::Base));

        // Rows: a band each in degree mode (the tonic's marked), lines at each degree by pitch.
        g.save();
        g.setClipRect (QRect (0, rulerHeight, w, gridHeight()));
        const auto top = (double) verticalScrollBar()->value();
        const auto rowH = rowHeight();
        auto shade = pal.color (QPalette::AlternateBase);
        auto tonicShade = pal.color (QPalette::Highlight);
        tonicShade.setAlphaF (0.10);
        const auto twelve = ed.system.isTwelveTet();
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto y = rulerHeight + centerY (pitches[i]) - top;
            if (y < rulerHeight - rowH || y > gridBottom + rowH)
                continue;
            const auto degree = ed.system.degreeOf (rows[i]);
            const QRectF band (keyWidth, y - rowH / 2.0, w - keyWidth, rowH);
            if (byPitch)
            {
                g.setPen (QPen (degree == 0 ? pal.color (QPalette::Highlight) : pal.color (QPalette::Mid), degree == 0 ? 1.5 : 0.6));
                g.drawLine (QPointF (keyWidth, y), QPointF (w, y));
            }
            else if (degree == 0)
                g.fillRect (band, tonicShade);
            else if (twelve ? (degree == 1 || degree == 3 || degree == 6 || degree == 8 || degree == 10) : degree % 2 == 1)
                g.fillRect (band, shade);
        }
        // By pitch, the twelve keys faintly behind, so how far a degree is from them shows.
        if (byPitch && ! twelve)
        {
            auto faint = pal.color (QPalette::Mid);
            faint.setAlphaF (0.35);
            g.setPen (QPen (faint, 0.5, Qt::DotLine));
            for (int key = 0; key <= 127; ++key)
            {
                const auto y = rulerHeight + centerY (key * 100.0) - top;
                if (y >= rulerHeight && y <= gridBottom)
                    g.drawLine (QPointF (keyWidth, y), QPointF (w, y));
            }
        }

        // Bars, beats and the grid.
        const auto tpq = ed.seq.ticksPerQuarter;
        const auto gridTicks = ed.grid();
        const auto first = tickAt (keyWidth), last = tickAt (w);
        auto lineColour = pal.color (QPalette::Mid);
        for (int64_t t = std::max<int64_t> (0, first / gridTicks * gridTicks); t <= last; t += gridTicks)
        {
            const auto x = xOf (t);
            const bool bar = t % (4 * tpq) == 0, beat = t % tpq == 0;
            lineColour.setAlphaF (bar ? 0.9 : beat ? 0.5 : 0.22);
            g.setPen (QPen (lineColour, bar ? 1.2 : 0.7));
            g.drawLine (QPointF (x, rulerHeight), QPointF (x, gridBottom));
            if (pixelsPerTick * gridTicks < 3.0)
                t += gridTicks * (int64_t) std::ceil (3.0 / (pixelsPerTick * gridTicks));
        }
        // The clip's end.
        {
            const auto x = xOf (ed.seq.end());
            auto c = pal.color (QPalette::Text);
            c.setAlphaF (0.5);
            g.setPen (QPen (c, 1.0, Qt::DashLine));
            g.drawLine (QPointF (x, rulerHeight), QPointF (x, gridBottom));
        }

        // Ghosts: the notes before the restyle.
        if (ed.ghostBox->isChecked())
        {
            auto c = pal.color (QPalette::Text);
            c.setAlphaF (0.28);
            g.setPen (QPen (c, 1.0, Qt::DashLine));
            g.setBrush (Qt::NoBrush);
            for (const auto& n : ed.ghosts.notes)
                g.drawRoundedRect (noteRect (n).adjusted (0.5, 0.5, -0.5, -0.5), 2, 2);
        }

        // The notes.
        const auto font = g.font();
        auto small = font;
        small.setPointSizeF (std::max (6.5, font.pointSizeF() * 0.8));
        g.setFont (small);
        for (const auto& n : ed.seq.notes)
        {
            const auto r = noteRect (n);
            if (r.right() < keyWidth || r.left() > w || r.bottom() < rulerHeight || r.top() > gridBottom)
                continue;
            auto c = n.selected ? pal.color (QPalette::Highlight) : QColor (0x4f, 0x9d, 0xd8);
            if (n.channel == 9)
                c = QColor (0xc8, 0x8a, 0x3a);
            c.setAlphaF (0.45 + 0.55 * n.velocity / 127.0);
            g.setPen (QPen (n.selected ? pal.color (QPalette::Text) : c.darker (160), 1.0));
            g.setBrush (c);
            g.drawRoundedRect (r, 2, 2);
            if (r.width() > 28 && r.height() >= 9)
            {
                const auto step = ed.system.nearestStep (n.pitch);
                const auto off = n.pitch - ed.system.pitchOf (step);
                auto text = QString::fromStdString (label (ed.system, step, naming()));
                if (std::abs (off) >= 0.5)
                    text += QStringLiteral (" ") + QString::fromStdString (centsText (off));
                g.setPen (pal.color (QPalette::HighlightedText));
                g.drawText (r.adjusted (3, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
            }
        }
        g.setFont (font);

        // The step cursor and the playhead.
        if (ed.stepBox->isChecked())
        {
            const auto x = xOf (ed.step.position);
            g.setPen (QPen (QColor (0xe0, 0x50, 0x50), 2.0));
            g.drawLine (QPointF (x, rulerHeight), QPointF (x, gridBottom));
        }
        if (ed.playheadTick >= 0)
        {
            const auto x = xOf (ed.playheadTick);
            g.setPen (QPen (pal.color (QPalette::Text), 1.5));
            g.drawLine (QPointF (x, 0), QPointF (x, gridBottom));
        }
        // The rubber band.
        if (drag == Drag::rubber)
        {
            auto c = pal.color (QPalette::Highlight);
            g.setPen (QPen (c, 1.0));
            c.setAlphaF (0.15);
            g.setBrush (c);
            g.drawRect (QRectF (pressPos, currentPos).normalized());
        }
        g.restore();

        // The keyboard: each row's name.
        g.fillRect (QRect (0, rulerHeight, keyWidth, gridHeight()), pal.color (QPalette::Window));
        g.save();
        g.setClipRect (QRect (0, rulerHeight, keyWidth, gridHeight()));
        g.setFont (small);
        const auto showEvery = rowH >= 10.0 ? 1 : rowH >= 5.0 ? 2 : 4;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto y = rulerHeight + centerY (pitches[i]) - top;
            if (y < rulerHeight - rowH || y > gridBottom + rowH)
                continue;
            const auto degree = ed.system.degreeOf (rows[i]);
            if (degree != 0 && (int) i % showEvery != 0)
                continue;
            g.setPen (degree == 0 ? pal.color (QPalette::Highlight) : pal.color (QPalette::WindowText));
            const auto name = QString::fromStdString (label (ed.system, rows[i], naming()));
            g.drawText (QRectF (4, y - 8, keyWidth - 8, 16), Qt::AlignVCenter | Qt::AlignRight, name);
            g.setPen (QPen (pal.color (QPalette::Mid), 0.5));
            g.drawLine (QPointF (keyWidth - 3, y), QPointF (keyWidth, y));
        }
        g.restore();
        g.setPen (pal.color (QPalette::Mid));
        g.drawLine (keyWidth, rulerHeight, keyWidth, gridBottom);

        // The ruler.
        g.fillRect (QRect (0, 0, w, rulerHeight), pal.color (QPalette::Window));
        g.setPen (pal.color (QPalette::WindowText));
        g.setFont (small);
        const auto barTicks = (int64_t) 4 * tpq;
        const auto everyBars = std::max<int64_t> (1, (int64_t) std::ceil (40.0 / (barTicks * pixelsPerTick)));
        for (int64_t t = std::max<int64_t> (0, first / barTicks * barTicks); t <= last; t += barTicks * everyBars)
        {
            const auto x = xOf (t);
            if (x < keyWidth)
                continue;
            g.drawLine (QPointF (x, rulerHeight - 6), QPointF (x, rulerHeight));
            g.drawText (QPointF (x + 3, rulerHeight - 7), QString::number (t / barTicks + 1));
        }
        g.drawText (QRectF (4, 0, keyWidth - 8, rulerHeight), Qt::AlignVCenter | Qt::AlignLeft,
                    QString::fromStdString (ed.system.name.size() > 14 ? ed.system.name.substr (0, 13) + "…" : ed.system.name));

        // The lane.
        const auto laneTop = gridBottom + laneGap;
        g.fillRect (QRect (0, gridBottom, w, laneGap), pal.color (QPalette::Window));
        g.fillRect (QRect (keyWidth, laneTop, w - keyWidth, laneHeight), pal.color (QPalette::Base));
        g.fillRect (QRect (0, laneTop, keyWidth, laneHeight), pal.color (QPalette::Window));
        g.setPen (pal.color (QPalette::WindowText));
        g.drawText (QRectF (4, laneTop, keyWidth - 8, laneHeight), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, ed.laneBox->currentText());
        g.setClipRect (QRect (keyWidth, laneTop, w - keyWidth, laneHeight));
        const auto lane = ed.laneBox->currentData().toInt();
        if (lane == laneVelocity)
        {
            for (const auto& n : ed.seq.notes)
            {
                const auto x = xOf (n.start);
                if (x < keyWidth - 4 || x > w)
                    continue;
                const auto h = laneHeight * n.velocity / 127.0;
                auto c = n.selected ? pal.color (QPalette::Highlight) : QColor (0x4f, 0x9d, 0xd8);
                g.fillRect (QRectF (x, laneTop + laneHeight - h, 3.0, h), c);
                g.fillRect (QRectF (x - 1.5, laneTop + laneHeight - h, 6.0, 2.0), c.darker (130));
            }
        }
        else
        {
            const auto top = lane == laneBend ? 16383.0 : 127.0;
            const auto pts = lanePoints (ed.seq, lane, ed.laneChannelBox->value() - 1);
            QPainterPath path;
            auto level = [&] (int v) { return laneTop + laneHeight - laneHeight * v / top; };
            double lastY = lane == laneBend ? level (8192) : level (0);
            path.moveTo (keyWidth, lastY);
            for (const auto& p : pts)
            {
                const auto x = xOf (p.tick);
                path.lineTo (x, lastY);
                lastY = level (p.value);
                path.lineTo (x, lastY);
            }
            path.lineTo (w, lastY);
            g.setPen (QPen (QColor (0x4f, 0x9d, 0xd8), 1.5));
            g.setBrush (Qt::NoBrush);
            g.drawPath (path);
            for (const auto& p : pts)
                g.fillRect (QRectF (xOf (p.tick) - 2, level (p.value) - 2, 4, 4), QColor (0x4f, 0x9d, 0xd8));
        }
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        setFocus();
        pressPos = currentPos = e->position();
        const auto x = e->position().x(), y = e->position().y();
        const auto gridBottom = rulerHeight + gridHeight();
        drag = Drag::none;
        if (y < rulerHeight && x >= keyWidth)
        {
            // The ruler: the step cursor and the playhead go there.
            ed.step.position = snapTick (tickAt (x), false);
            if (! ed.playTimer->isActive())
                ed.playheadTick = ed.step.position;
            viewport()->update();
            return;
        }
        if (y > gridBottom + laneGap && x >= keyWidth)
        {
            ed.undoList.push_back (ed.seq);
            ed.redoList.clear();
            before = ed.seq;
            drag = e->button() == Qt::RightButton ? Drag::laneErase : Drag::lane;
            laneMoved (e->position());
            return;
        }
        if (y < rulerHeight || y > gridBottom)
            return;
        const auto pitch = pitchAt (y - rulerHeight + verticalScrollBar()->value(), ! (e->modifiers() & Qt::AltModifier));
        if (x < keyWidth)
        {
            // The keyboard: heard, and entered when step input is on.
            ed.audition (pitch);
            if (ed.stepBox->isChecked())
                ed.stepPitch (pitch, e->modifiers() & Qt::ShiftModifier);
            return;
        }

        const auto hit = noteAt (e->position());
        const auto t = tickAt (x);
        if (e->button() == Qt::RightButton)
        {
            if (hit >= 0)
                ed.edit ([hit] (Sequence& s) { s.notes.erase (s.notes.begin() + hit); });
            return;
        }
        switch (tool)
        {
            case toolErase:
                if (hit >= 0)
                {
                    ed.edit ([hit] (Sequence& s) { s.notes.erase (s.notes.begin() + hit); });
                    drag = Drag::erasing;
                }
                return;
            case toolSplit:
                if (hit >= 0)
                {
                    const auto at = snapTick (t, false);
                    ed.edit ([hit, at] (Sequence& s)
                    {
                        selectAll (s, false);
                        s.notes[(size_t) hit].selected = true;
                        splitAt (s, at);
                        selectAll (s, false);
                    });
                }
                return;
            case toolGlue:
                if (hit >= 0)
                    ed.edit ([hit] (Sequence& s)
                    {
                        // This note and the next of its pitch.
                        selectAll (s, false);
                        const auto p = s.notes[(size_t) hit].pitch;
                        s.notes[(size_t) hit].selected = true;
                        for (size_t i = 0; i < s.notes.size(); ++i)
                            if (s.notes[i].start > s.notes[(size_t) hit].start && std::abs (s.notes[i].pitch - p) < 0.5)
                            {
                                s.notes[i].selected = true;
                                break;
                            }
                        glue (s);
                        selectAll (s, false);
                    });
                return;
            case toolDraw:
                if (hit < 0)
                {
                    newNote (t, pitch);
                    return;
                }
                break;
            default: break;
        }
        if (hit < 0)
        {
            if (e->modifiers() & Qt::ControlModifier || e->button() == Qt::MiddleButton)
            {
                newNote (t, pitch);
                return;
            }
            if (! (e->modifiers() & Qt::ShiftModifier))
                selectAll (ed.seq, false);
            drag = Drag::rubber;
            selectedBefore.clear();
            for (const auto& n : ed.seq.notes)
                selectedBefore.push_back (n.selected);
            ed.updateInfo();
            viewport()->update();
            return;
        }
        auto& n = ed.seq.notes[(size_t) hit];
        if (e->modifiers() & Qt::ShiftModifier)
            n.selected = ! n.selected;
        else if (! n.selected)
        {
            selectAll (ed.seq, false);
            n.selected = true;
        }
        if (n.selected)
            ed.audition (n.pitch, n.velocity);
        ed.undoList.push_back (ed.seq);
        ed.redoList.clear();
        before = ed.seq;
        drag = std::abs (x - noteRect (n).right()) <= 5.0 ? Drag::resize : Drag::move;
        ed.updateInfo();
        viewport()->update();
    }

    void mouseDoubleClickEvent (QMouseEvent* e) override
    {
        const auto x = e->position().x(), y = e->position().y();
        if (x < keyWidth || y < rulerHeight || y > rulerHeight + gridHeight())
            return;
        const auto hit = noteAt (e->position());
        if (hit >= 0)
        {
            if (tool == toolPick)
                ed.edit ([hit] (Sequence& s) { s.notes.erase (s.notes.begin() + hit); });
            return;
        }
        newNote (tickAt (x), pitchAt (y - rulerHeight + verticalScrollBar()->value(), ! (e->modifiers() & Qt::AltModifier)));
    }

    void mouseMoveEvent (QMouseEvent* e) override
    {
        currentPos = e->position();
        const auto x = e->position().x();
        if (drag == Drag::none)
        {
            const auto hit = noteAt (e->position());
            const auto edge = hit >= 0 && std::abs (x - noteRect (ed.seq.notes[(size_t) hit]).right()) <= 5.0;
            viewport()->setCursor (edge && tool == toolPick ? Qt::SizeHorCursor : tool == toolDraw ? Qt::CrossCursor : Qt::ArrowCursor);
            return;
        }
        const auto dx = x - pressPos.x(), dy = e->position().y() - pressPos.y();
        const auto free = (e->modifiers() & Qt::AltModifier) != 0;
        switch (drag)
        {
            case Drag::move:
            case Drag::resize:
            {
                auto dt = (int64_t) std::lround (dx / pixelsPerTick);
                if (ed.snapBox->isChecked() && ! free)
                    dt = dt / ed.grid() * ed.grid();
                ed.seq = before;
                for (auto& n : ed.seq.notes)
                {
                    if (! n.selected)
                        continue;
                    if (drag == Drag::resize)
                    {
                        n.length = std::max<int64_t> (ed.snapBox->isChecked() && ! free ? ed.grid() / 2 : 1, n.length + dt);
                        continue;
                    }
                    n.start = std::max<int64_t> (0, n.start + dt);
                    n.pitch = movedPitch (n.pitch, dy, free);
                }
                ed.seq.sort();
                ed.changed();
                break;
            }
            case Drag::drawing:
            {
                if (drawn >= 0 && (size_t) drawn < ed.seq.notes.size())
                {
                    auto& n = ed.seq.notes[(size_t) drawn];
                    const auto end = snapTick (tickAt (x), true);
                    n.length = std::max<int64_t> (ed.snapBox->isChecked() ? ed.grid() : 1, end - n.start);
                    ed.changed();
                }
                break;
            }
            case Drag::erasing:
                if (const auto hit = noteAt (e->position()); hit >= 0)
                {
                    ed.seq.notes.erase (ed.seq.notes.begin() + hit);
                    ed.changed();
                }
                break;
            case Drag::rubber:
            {
                const auto r = QRectF (pressPos, currentPos).normalized();
                for (size_t i = 0; i < ed.seq.notes.size(); ++i)
                    ed.seq.notes[i].selected = (i < selectedBefore.size() && selectedBefore[i]) || r.intersects (noteRect (ed.seq.notes[i]));
                ed.updateInfo();
                viewport()->update();
                break;
            }
            case Drag::lane:
            case Drag::laneErase:
                laneMoved (e->position());
                break;
            case Drag::none: break;
        }
    }

    void mouseReleaseEvent (QMouseEvent*) override
    {
        if ((drag == Drag::move || drag == Drag::resize) && ! ed.undoList.empty())
        {
            // A click that moved nothing leaves no undo step.
            bool same = before.notes.size() == ed.seq.notes.size();
            for (size_t i = 0; same && i < before.notes.size(); ++i)
                same = before.notes[i].start == ed.seq.notes[i].start && before.notes[i].length == ed.seq.notes[i].length
                       && before.notes[i].pitch == ed.seq.notes[i].pitch;
            if (same)
                ed.undoList.pop_back();
            else if (drag == Drag::move)
                for (const auto& n : ed.seq.notes)
                    if (n.selected)
                    {
                        ed.audition (n.pitch, n.velocity);
                        break;
                    }
        }
        drag = Drag::none;
        drawn = -1;
        viewport()->update();
    }

    void wheelEvent (QWheelEvent* e) override
    {
        const auto steps = e->angleDelta().y() / 120.0;
        if (e->modifiers() & Qt::ControlModifier)
        {
            const auto anchor = tickAt (e->position().x());
            pixelsPerTick = std::clamp (pixelsPerTick * std::pow (1.25, steps), 0.005, 2.0);
            updateBars();
            horizontalScrollBar()->setValue ((int) (anchor * pixelsPerTick - (e->position().x() - keyWidth)));
        }
        else if (e->modifiers() & Qt::AltModifier)
        {
            const auto steps2 = (e->angleDelta().x() + e->angleDelta().y()) / 120.0;
            const auto anchor = pitchAt (e->position().y() - rulerHeight + verticalScrollBar()->value(), false);
            rowPixels = std::clamp (rowPixels * std::pow (1.15, steps2), 3.0, 48.0);
            updateBars();
            verticalScrollBar()->setValue ((int) (centerY (anchor) - (e->position().y() - rulerHeight)));
        }
        else if (e->modifiers() & Qt::ShiftModifier)
            horizontalScrollBar()->setValue (horizontalScrollBar()->value() - (int) (steps * 80));
        else
            QAbstractScrollArea::wheelEvent (e);
        viewport()->update();
    }

private:
    enum class Drag { none, move, resize, drawing, erasing, rubber, lane, laneErase };

    double rowHeight() const { return rowPixels; }
    double pixelsPerCent() const { return rowPixels / 100.0 * std::max (1.0, 1200.0 / std::max (1.0, ed.system.period) * ed.system.size() / 12.0); }
    double gridHeight() const { return std::max (40, viewport()->height() - rulerHeight - laneGap - laneHeight); }
    double contentHeight() const
    {
        return byPitch ? 12700.0 * pixelsPerCent() + rowHeight() : (double) rows.size() * rowHeight();
    }
    Naming naming() const { return (Naming) ed.namingBox->currentData().toInt(); }

    // A pitch's centre, from the top of the rows.
    double centerY (double pitch) const
    {
        if (byPitch)
            return (12700.0 - pitch) * pixelsPerCent() + rowHeight() / 2.0;
        if (pitches.empty())
            return 0.0;
        const auto n = (double) pitches.size();
        double row;
        if (pitch <= pitches.front())
            row = 0.0;
        else if (pitch >= pitches.back())
            row = n - 1.0;
        else
        {
            const auto at = (size_t) (std::upper_bound (pitches.begin(), pitches.end(), pitch) - pitches.begin()) - 1;
            row = (double) at + (pitch - pitches[at]) / (pitches[at + 1] - pitches[at]);
        }
        return (n - 1.0 - row) * rowHeight() + rowHeight() / 2.0;
    }

    // The pitch at a height from the top of the rows: a degree's, or between them (free).
    double pitchAt (double y, bool snapToDegree) const
    {
        double p;
        if (byPitch)
            p = 12700.0 - (y - rowHeight() / 2.0) / pixelsPerCent();
        else
        {
            if (pitches.empty())
                return 6000.0;
            const auto n = (int) pitches.size();
            const auto row = std::clamp ((double) n - 1.0 - (y - rowHeight() / 2.0) / rowHeight(), 0.0, (double) n - 1.0);
            if (snapToDegree)
                return pitches[(size_t) std::lround (row)];
            const auto at = std::min ((int) std::floor (row), n - 2);
            p = at < 0 ? pitches.front() : pitches[(size_t) at] + (row - at) * (pitches[(size_t) at + 1] - pitches[(size_t) at]);
        }
        p = std::clamp (p, 0.0, 12700.0);
        return snapToDegree ? ed.system.pitchOf (ed.system.nearestStep (p)) : std::round (p);
    }

    double movedPitch (double pitch, double dy, bool free) const
    {
        const auto step = ed.system.nearestStep (pitch);
        const auto off = pitch - ed.system.pitchOf (step);
        if (free)
            return std::clamp (pitch - std::round (dy), 0.0, 12700.0);   // a cent a pixel
        if (byPitch)
        {
            const auto target = pitch - dy / pixelsPerCent();
            return std::clamp (ed.system.pitchOf (ed.system.nearestStep (target)) + off, 0.0, 12700.0);
        }
        const auto rowsMoved = (int) std::lround (-dy / rowHeight());
        // The rows are the degrees from 0 to 12700, so a step moves a row.
        return std::clamp (ed.system.pitchOf (step + rowsMoved) + off, 0.0, 12700.0);
    }

    double xOf (int64_t tick) const { return keyWidth + tick * pixelsPerTick - horizontalScrollBar()->value(); }
    int64_t tickAt (double x) const { return std::max<int64_t> (0, (int64_t) std::lround ((x - keyWidth + horizontalScrollBar()->value()) / pixelsPerTick)); }
    int64_t snapTick (int64_t t, bool round) const
    {
        if (! ed.snapBox->isChecked())
            return t;
        const auto g = ed.grid();
        return round ? (int64_t) std::lround ((double) t / (double) g) * g : t / g * g;
    }

    QRectF noteRect (const Note& n) const
    {
        const auto y = rulerHeight + centerY (n.pitch) - verticalScrollBar()->value();
        const auto h = std::max (3.0, rowHeight() - 1.0);
        return { xOf (n.start), y - h / 2.0, std::max (2.0, n.length * pixelsPerTick), h };
    }

    int noteAt (QPointF p) const
    {
        for (int i = (int) ed.seq.notes.size() - 1; i >= 0; --i)
            if (noteRect (ed.seq.notes[(size_t) i]).adjusted (-1, 0, 1, 0).contains (p))
                return i;
        return -1;
    }

    void newNote (int64_t t, double pitch)
    {
        const auto start = snapTick (t, false);
        const auto length = ed.noteLength();
        ed.edit ([&] (Sequence& s)
        {
            selectAll (s, false);
            Note n;
            n.start = start;
            n.length = length;
            n.pitch = pitch;
            n.velocity = ed.step.velocity;
            n.selected = true;
            s.notes.push_back (n);
            s.sort();
            s.lastTick = std::max (s.lastTick, s.end());
        });
        for (size_t i = 0; i < ed.seq.notes.size(); ++i)
            if (ed.seq.notes[i].selected)
                drawn = (int) i;
        drag = Drag::drawing;
        ed.audition (pitch, ed.step.velocity);
    }

    void laneMoved (QPointF p)
    {
        const auto laneTop = rulerHeight + gridHeight() + laneGap;
        const auto lane = ed.laneBox->currentData().toInt();
        const auto top = lane == laneBend ? 16383 : 127;
        const auto valueAt = [&] (double y) { return std::clamp ((int) std::lround ((laneTop + laneHeight - y) / laneHeight * top), lane == laneVelocity ? 1 : 0, top); };
        const auto t0 = tickAt (pressPos.x()), t1 = tickAt (p.x());
        const auto v0 = valueAt (pressPos.y()), v1 = valueAt (p.y());
        ed.seq = before;
        if (lane == laneVelocity)
        {
            // A line drawn over the notes: those starting under it take its height there.
            const auto from = std::min (t0, t1), to = std::max (t0, t1);
            const auto picked = ed.seq.anySelected();
            const auto slack = (int64_t) std::lround (3.0 / pixelsPerTick);
            for (auto& n : ed.seq.notes)
                if ((! picked || n.selected) && n.start >= from - slack && n.start <= to + slack)
                {
                    const auto x = t1 != t0 ? (double) (n.start - t0) / (double) (t1 - t0) : 0.0;
                    n.velocity = std::clamp ((int) std::lround (v0 + (v1 - v0) * std::clamp (x, 0.0, 1.0)), 1, 127);
                }
        }
        else if (drag == Drag::laneErase)
            eraseLane (ed.seq, lane, ed.laneChannelBox->value() - 1, t0, t1);
        else
            drawLane (ed.seq, lane, ed.laneChannelBox->value() - 1, t0, t1, v0, v1, std::max<int64_t> (1, ed.seq.ticksPerQuarter / 16));
        ed.changed();
    }

    void updateBars()
    {
        const auto width = (double) (ed.seq.end() + 8 * 4 * ed.seq.ticksPerQuarter) * pixelsPerTick;
        horizontalScrollBar()->setRange (0, std::max (0, (int) (width - (viewport()->width() - keyWidth))));
        horizontalScrollBar()->setPageStep (viewport()->width());
        verticalScrollBar()->setRange (0, std::max (0, (int) (contentHeight() - gridHeight())));
        verticalScrollBar()->setPageStep ((int) gridHeight());
        verticalScrollBar()->setSingleStep ((int) std::max (4.0, rowHeight()));
    }

    MidiEditor& ed;
    std::vector<int> rows;
    std::vector<double> pitches;
    double pixelsPerTick = 0.12, rowPixels = 14.0;
    Drag drag = Drag::none;
    QPointF pressPos, currentPos;
    Sequence before;
    std::vector<bool> selectedBefore;
    int drawn = -1;
};

//==============================================================================
MidiEditor::MidiEditor (const midifile::Song& song, Host h, QWidget* parent)
    : QDialog (parent), host (std::move (h)), original (song)
{
    setWindowTitle (host.title.isEmpty() ? tr ("MIDI Editor") : tr ("MIDI Editor: %1").arg (host.title));
    setWindowFlag (Qt::WindowMaximizeButtonHint);
    setSizeGripEnabled (true);
    resize (1180, 760);
    seq = decode (song);
    step.length = step.step = seq.ticksPerQuarter / 2;
    playTimer = new QTimer (this);
    playTimer->setTimerType (Qt::PreciseTimer);
    playTimer->setInterval (4);
    connect (playTimer, &QTimer::timeout, this, &MidiEditor::playTick);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (8, 8, 8, 8);
    layout->setSpacing (6);

    // The tools, the grid and the functions.
    auto* tools = new QHBoxLayout;
    auto* group = new QButtonGroup (this);
    const std::pair<QString, QString> toolNames[] { { tr ("Pick"), tr ("Pick, move (Alt: by the cent) and stretch notes; drag on empty space to pick several. Ctrl-click draws (1)") },
                                                    { tr ("Draw"), tr ("Click to put a note, drag to its length; Alt puts it between the degrees (2)") },
                                                    { tr ("Erase"), tr ("Click or drag over notes to delete them (3)") },
                                                    { tr ("Split"), tr ("Cut a note in two where you click (4)") },
                                                    { tr ("Glue"), tr ("Join a note to the next of its pitch (5)") } };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = new QToolButton (this);
        b->setText (toolNames[i].first);
        b->setToolTip (toolNames[i].second);
        b->setCheckable (true);
        b->setChecked (i == 0);
        group->addButton (b, i);
        tools->addWidget (b);
        toolButtons.push_back (b);
    }
    connect (group, &QButtonGroup::idClicked, this, [this] (int id) { roll->tool = id; });
    tools->addSpacing (10);
    snapBox = new QCheckBox (tr ("Snap"), this);
    snapBox->setChecked (true);
    snapBox->setToolTip (tr ("Notes start and end on the grid; hold Alt to place them freely"));
    tools->addWidget (snapBox);
    gridBox = new QComboBox (this);
    for (const auto& g : grids)
        gridBox->addItem (QString::fromUtf8 (g.name), g.quarters);
    gridBox->setCurrentIndex (4);
    gridBox->setToolTip (tr ("The grid, and the quantise"));
    tools->addWidget (gridBox);
    tools->addWidget (new QLabel (tr ("Length"), this));
    lengthBox = new QComboBox (this);
    lengthBox->addItem (tr ("Grid"), 0.0);
    for (const auto& g : grids)
        lengthBox->addItem (QString::fromUtf8 (g.name), g.quarters);
    lengthBox->setToolTip (tr ("How long a drawn or step-entered note is"));
    tools->addWidget (lengthBox);
    tools->addSpacing (10);
    auto* quantize = new QPushButton (tr ("Quantise"), this);
    quantize->setToolTip (tr ("The picked notes' starts (all, if none is picked) to the grid, by the strength, with the swing (Q)"));
    tools->addWidget (quantize);
    strengthBox = new QDoubleSpinBox (this);
    strengthBox->setRange (0, 100);
    strengthBox->setValue (100);
    strengthBox->setSuffix (QStringLiteral ("%"));
    strengthBox->setDecimals (0);
    strengthBox->setToolTip (tr ("Quantise strength: how far toward the grid"));
    tools->addWidget (strengthBox);
    swingBox = new QDoubleSpinBox (this);
    swingBox->setRange (0, 100);
    swingBox->setSuffix (tr ("% swing"));
    swingBox->setDecimals (0);
    swingBox->setToolTip (tr ("Every second grid line later: 50% is a triplet feel"));
    tools->addWidget (swingBox);
    auto* functions = new QToolButton (this);
    functions->setText (tr ("Functions"));
    functions->setPopupMode (QToolButton::InstantPopup);
    auto* menu = new QMenu (functions);
    functions->setMenu (menu);
    tools->addWidget (functions);
    tools->addStretch (1);
    auto* undoButton = new QToolButton (this);
    undoButton->setText (tr ("Undo"));
    undoButton->setToolTip (tr ("Ctrl+Z"));
    auto* redoButton = new QToolButton (this);
    redoButton->setText (tr ("Redo"));
    redoButton->setToolTip (tr ("Ctrl+Shift+Z"));
    tools->addWidget (undoButton);
    tools->addWidget (redoButton);
    playButton = new QToolButton (this);
    playButton->setText (tr ("Play"));
    playButton->setToolTip (tr ("The notes as they will be saved, from the playhead (click the ruler to move it). Space"));
    abButton = new QToolButton (this);
    abButton->setText (tr ("Original"));
    abButton->setCheckable (true);
    abButton->setToolTip (tr ("Hear the clip as it was, to compare (switches while it plays)"));
    tools->addWidget (playButton);
    tools->addWidget (abButton);
    layout->addLayout (tools);

    // The pitch system.
    auto* systems = new QHBoxLayout;
    systems->addWidget (new QLabel (tr ("System"), this));
    systemBox = new QComboBox (this);
    systemBox->setMaxVisibleItems (30);
    systemBox->setToolTip (tr ("What the rows are: 12-TET, a mode, raga, maqam, dastgah, laras, mbira tuning, lü or tori, or a "
                               "Scala file. The notes keep their pitches; Functions > Snap or Restyle moves them into it"));
    systems->addWidget (systemBox, 2);
    systems->addWidget (new QLabel (tr ("on"), this));
    rootBox = new QComboBox (this);
    for (const auto* n : rootNames)
        rootBox->addItem (QString::fromUtf8 (n));
    rootBox->setCurrentIndex (std::clamp (host.root, 0, 11));
    rootBox->setToolTip (tr ("The tonic: Sa, the final, gong, the gong tone"));
    systems->addWidget (rootBox);
    systems->addWidget (new QLabel (tr ("Names"), this));
    namingBox = new QComboBox (this);
    namingBox->addItem (tr ("The tradition's"), (int) Naming::tradition);
    namingBox->addItem (tr ("Letters and cents"), (int) Naming::letters);
    namingBox->addItem (tr ("Degrees"), (int) Naming::degrees);
    namingBox->addItem (tr ("Cents"), (int) Naming::cents);
    namingBox->setToolTip (tr ("Sargam for ragas, kepatihan numbers for gamelan, wǔshēng for Chinese modes, quarter-tone letters "
                               "for maqams; letters where a tradition has no names here"));
    systems->addWidget (namingBox);
    systems->addWidget (new QLabel (tr ("Rows"), this));
    rowsBox = new QComboBox (this);
    rowsBox->addItem (tr ("A row a degree"));
    rowsBox->addItem (tr ("By pitch"));
    rowsBox->setToolTip (tr ("A row a degree of the system, or a height for each cent with the system's degrees drawn across "
                             "(the twelve keys dotted behind)"));
    systems->addWidget (rowsBox);
    auto* scalaButton = new QToolButton (this);
    scalaButton->setText (tr ("Scala"));
    scalaButton->setPopupMode (QToolButton::InstantPopup);
    auto* scalaMenu = new QMenu (scalaButton);
    scalaMenu->addAction (tr ("Open a Scale (.scl)..."), this, &MidiEditor::loadScala);
    scalaMenu->addAction (tr ("Open a Keyboard Map (.kbm)..."), this, &MidiEditor::loadKeyboardMap);
    scalaMenu->addSeparator();
    scalaMenu->addAction (tr ("Save This System as .scl and .kbm..."), this, &MidiEditor::exportScala);
    scalaButton->setMenu (scalaMenu);
    scalaButton->setToolTip (tr ("Scala tuning files, read and written by PHI itself"));
    systems->addWidget (scalaButton);
    ghostBox = new QCheckBox (tr ("Ghosts"), this);
    ghostBox->setChecked (true);
    ghostBox->setToolTip (tr ("The notes as they were before the last restyle, drawn behind"));
    systems->addWidget (ghostBox);
    layout->addLayout (systems);

    roll = new Roll (*this);
    layout->addWidget (roll, 1);

    // The lane and step input.
    auto* lanes = new QHBoxLayout;
    lanes->addWidget (new QLabel (tr ("Lane"), this));
    laneBox = new QComboBox (this);
    laneBox->addItem (tr ("Velocity"), laneVelocity);
    for (auto [name, value] : { std::pair { tr ("Modulation (CC 1)"), 1 }, { tr ("Volume (CC 7)"), 7 }, { tr ("Pan (CC 10)"), 10 },
                                { tr ("Expression (CC 11)"), 11 }, { tr ("Sustain (CC 64)"), 64 }, { tr ("Timbre (CC 74)"), 74 },
                                { tr ("Pressure"), lanePressure }, { tr ("Pitch bend"), laneBend } })
        laneBox->addItem (name, value);
    laneBox->setToolTip (tr ("Draw a line in the lane to set it; right-drag clears a controller"));
    lanes->addWidget (laneBox);
    laneChannelBox = new QSpinBox (this);
    laneChannelBox->setRange (1, 16);
    laneChannelBox->setPrefix (tr ("channel "));
    laneChannelBox->setToolTip (tr ("The controller's channel. With MPE, channel 1 is the zone's master: what it sends, every note follows"));
    lanes->addWidget (laneChannelBox);
    lanes->addSpacing (16);
    stepBox = new QCheckBox (tr ("Step input"), this);
    stepBox->setToolTip (tr ("Notes from the MIDI keyboard, the keyboard on the left, or the computer's keys go in at the red cursor, "
                             "which moves on a length each time. Space for a rest, Backspace to step back"));
    lanes->addWidget (stepBox);
    chordBox = new QCheckBox (tr ("Chord"), this);
    chordBox->setToolTip (tr ("The cursor stays, so notes go in together (keys held together on a MIDI keyboard make a chord anyway; "
                              "Shift with a computer key too)"));
    lanes->addWidget (chordBox);
    insertBox = new QCheckBox (tr ("Insert"), this);
    insertBox->setToolTip (tr ("The notes after the cursor move later to make room"));
    lanes->addWidget (insertBox);
    octaveBox = new QSpinBox (this);
    octaveBox->setRange (-1, 9);
    octaveBox->setValue (4);
    octaveBox->setPrefix (tr ("period "));
    octaveBox->setToolTip (tr ("Where the computer's keys play: A S D F... are the degrees up from the tonic in this period, Q W E R... "
                               "the period above, Z X C V... the one below"));
    lanes->addWidget (octaveBox);
    lanes->addStretch (1);
    layout->addLayout (lanes);

    // The info line: the picked notes.
    auto* info = new QHBoxLayout;
    infoLabel = new QLabel (this);
    infoLabel->setTextInteractionFlags (Qt::TextSelectableByMouse);
    info->addWidget (infoLabel, 1);
    info->addWidget (new QLabel (tr ("Length"), this));
    infoLength = new QSpinBox (this);
    infoLength->setRange (1, 1 << 24);
    infoLength->setSuffix (tr (" ticks"));
    info->addWidget (infoLength);
    info->addWidget (new QLabel (tr ("Off the degree"), this));
    infoPitch = new QDoubleSpinBox (this);
    infoPitch->setRange (-600, 600);
    infoPitch->setDecimals (1);
    infoPitch->setSuffix (QStringLiteral ("¢"));
    infoPitch->setToolTip (tr ("How far the picked notes are from their degree, in cents (inflections: a sruti's, a maqam's "
                               "comma, the ensemble's own tuning)"));
    info->addWidget (infoPitch);
    info->addWidget (new QLabel (tr ("Velocity"), this));
    infoVelocity = new QSpinBox (this);
    infoVelocity->setRange (1, 127);
    info->addWidget (infoVelocity);
    layout->addLayout (info);

    // Saving.
    auto* save = new QHBoxLayout;
    save->addWidget (new QLabel (tr ("Save as"), this));
    encodingBox = new QComboBox (this);
    encodingBox->addItem (tr ("MPE (each note its own pitch)"), (int) Encoding::mpe);
    encodingBox->addItem (tr ("Pitch bend by channel"), (int) Encoding::bendChannels);
    encodingBox->addItem (tr ("Keys (the track's tuning plays them)"), (int) Encoding::keys);
    encodingBox->setToolTip (tr ("MPE gives every note a channel and bends it to its pitch, for any system; pitch bend by channel "
                                 "shares a channel among notes bent alike, for synths without MPE; keys keeps each note on its "
                                 "nearest key"));
    save->addWidget (encodingBox);
    save->addWidget (new QLabel (tr ("bend range"), this));
    bendRangeBox = new QDoubleSpinBox (this);
    bendRangeBox->setRange (1, 96);
    bendRangeBox->setDecimals (0);
    bendRangeBox->setValue (48);
    bendRangeBox->setSuffix (tr (" semitones"));
    bendRangeBox->setToolTip (tr ("Sent to each channel (RPN 0); MPE's default is 48, most synths' 2"));
    save->addWidget (bendRangeBox);
    encodingNote = new QLabel (this);
    encodingNote->setForegroundRole (QPalette::PlaceholderText);
    encodingNote->setWordWrap (true);
    save->addWidget (encodingNote, 1);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("Apply to the Clip"));
    for (auto* b : buttons->buttons())
        if (auto* p = qobject_cast<QPushButton*> (b))
            p->setAutoDefault (false);
    save->addWidget (buttons);
    layout->addLayout (save);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // The functions menu.
    const auto on = [this] (auto f) { return [this, f] { edit (f); }; };
    menu->addAction (tr ("Quantise Starts"), this, [this] { edit ([this] (Sequence& s) { quantizeStarts (s, grid(), strengthBox->value() / 100.0, swingBox->value() / 100.0); }); });
    menu->addAction (tr ("Quantise Ends"), this, [this] { edit ([this] (Sequence& s) { quantizeEnds (s, grid()); }); });
    menu->addAction (tr ("Quantise Lengths"), this, [this] { edit ([this] (Sequence& s) { quantizeLengths (s, grid()); }); });
    menu->addSeparator();
    menu->addAction (tr ("Legato"), this, on ([] (Sequence& s) { legato (s); }));
    menu->addAction (tr ("Fixed Lengths (the Length)"), this, [this] { edit ([this] (Sequence& s) { fixedLength (s, noteLength()); }); });
    menu->addAction (tr ("Delete Overlaps (same pitch)"), this, on ([] (Sequence& s) { deleteOverlaps (s, false); }));
    menu->addAction (tr ("Delete Overlaps (one voice)"), this, on ([] (Sequence& s) { deleteOverlaps (s, true); }));
    menu->addAction (tr ("Glue Each to the Next of Its Pitch"), this, on ([] (Sequence& s) { glue (s); }));
    menu->addAction (tr ("Split at the Cursor"), this, [this] { const auto at = step.position; edit ([at] (Sequence& s) { splitAt (s, at); }); });
    menu->addSeparator();
    menu->addAction (tr ("Velocity..."), this, &MidiEditor::velocityDialog);
    menu->addSeparator();
    menu->addAction (tr ("Up a Degree (Up)"), this, [this] { edit ([this] (Sequence& s) { transposeSteps (s, system, 1); }); });
    menu->addAction (tr ("Down a Degree (Down)"), this, [this] { edit ([this] (Sequence& s) { transposeSteps (s, system, -1); }); });
    menu->addAction (tr ("Up a Period (Shift+Up)"), this, [this] { edit ([this] (Sequence& s) { transposeSteps (s, system, system.size()); }); });
    menu->addAction (tr ("Down a Period (Shift+Down)"), this, [this] { edit ([this] (Sequence& s) { transposeSteps (s, system, -system.size()); }); });
    menu->addAction (tr ("Snap to the System's Degrees"), this, [this] { edit ([this] (Sequence& s) { snapToSystem (s, system); }); });
    menu->addAction (tr ("Restyle into This System..."), this, &MidiEditor::restyleDialog);
    menu->addSeparator();
    menu->addAction (tr ("Select All (Ctrl+A)"), this, [this] { selectAll (seq, true); changed(); });
    menu->addAction (tr ("Select None"), this, [this] { selectAll (seq, false); changed(); });
    menu->addAction (tr ("Delete the Picked Notes (Delete)"), this, on ([] (Sequence& s) { deleteTargets (s); }));

    connect (quantize, &QPushButton::clicked, this, [this] { edit ([this] (Sequence& s) { quantizeStarts (s, grid(), strengthBox->value() / 100.0, swingBox->value() / 100.0); }); });
    connect (undoButton, &QToolButton::clicked, this, &MidiEditor::undo);
    connect (redoButton, &QToolButton::clicked, this, &MidiEditor::redo);
    connect (playButton, &QToolButton::clicked, this, &MidiEditor::togglePlay);
    connect (abButton, &QToolButton::toggled, this, [this] (bool b)
    {
        if (! playTimer->isActive())
            return;
        startPlay (b);   // on from where it is, the other version
    });
    connect (systemBox, &QComboBox::activated, this, &MidiEditor::systemChosen);
    connect (rootBox, &QComboBox::activated, this, &MidiEditor::systemChosen);
    connect (namingBox, &QComboBox::activated, this, [this] { roll->refresh(); updateInfo(); });
    connect (rowsBox, &QComboBox::activated, this, [this] (int i) { roll->byPitch = i == 1; roll->rebuild(); roll->showNotes(); });
    connect (gridBox, &QComboBox::activated, this, [this] { roll->refresh(); });
    connect (lengthBox, &QComboBox::activated, this, [this] { step.length = step.step = noteLength(); });
    connect (ghostBox, &QCheckBox::toggled, this, [this] { roll->refresh(); });
    connect (stepBox, &QCheckBox::toggled, this, [this] (bool b) { if (b) roll->setFocus(); roll->refresh(); });
    connect (laneBox, &QComboBox::activated, this, [this] { roll->refresh(); });
    connect (laneChannelBox, &QSpinBox::valueChanged, this, [this] { roll->refresh(); });
    connect (encodingBox, &QComboBox::activated, this, [this]
    {
        encodingChosen = true;
        if ((Encoding) encodingBox->currentData().toInt() == Encoding::bendChannels && bendRangeBox->value() == 48)
            bendRangeBox->setValue (2);
        else if ((Encoding) encodingBox->currentData().toInt() == Encoding::mpe && bendRangeBox->value() == 2)
            bendRangeBox->setValue (48);
        changed();
    });
    connect (infoVelocity, &QSpinBox::valueChanged, this, &MidiEditor::infoEdited);
    connect (infoPitch, &QDoubleSpinBox::valueChanged, this, &MidiEditor::infoEdited);
    connect (infoLength, &QSpinBox::valueChanged, this, &MidiEditor::infoEdited);

    buildSystems();
    if (host.system >= 0 && host.system < (int) mixer::tuningSystems().size())
    {
        systemBox->setCurrentIndex (std::max (0, systemBox->findData (host.system)));
        system = fromTuning (mixer::tuningSystems()[(size_t) host.system], host.root);
    }
    else
        system = twelveTet (host.root);
    const auto e = suggestedEncoding (seq);
    encodingBox->setCurrentIndex (encodingBox->findData ((int) e));
    step.length = step.step = noteLength();
    roll->rebuild();
    changed();
    QTimer::singleShot (0, this, [this] { roll->showNotes(); roll->setFocus(); });
}

MidiEditor::~MidiEditor() { stopPlay(); }

void MidiEditor::buildSystems()
{
    systemBox->clear();
    systemBox->addItem (tr ("12-TET (the piano)"), -1);
    QString family;
    const auto& all = mixer::tuningSystems();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto f = QString::fromStdString (all[(size_t) i].family);
        if (f != family)
        {
            systemBox->insertSeparator (systemBox->count());
            family = f;
        }
        systemBox->addItem (QString::fromStdString (all[(size_t) i].name) + QStringLiteral ("  ·  ") + f, i);
    }
    systemBox->insertSeparator (systemBox->count());
    systemBox->addItem (tr ("Open a Scala file..."), -3);
}

void MidiEditor::systemChosen()
{
    const auto choice = systemBox->currentData().toInt();
    const auto root = rootBox->currentIndex();
    if (choice == -3)
    {
        loadScala();
        return;
    }
    if (choice == -2 && scala != nullptr)
    {
        // A Scala scale: on the root, unless its keyboard map says where.
        auto map = keyboardMap != nullptr ? *keyboardMap : phi::scala::linearMap (60 + root);
        setSystem (fromScala (*scala, map, scalaName.toStdString()));
        return;
    }
    if (choice < 0)
        setSystem (twelveTet (root));
    else
        setSystem (fromTuning (mixer::tuningSystems()[(size_t) choice], root));
}

void MidiEditor::setSystem (PitchSystem s)
{
    system = std::move (s);
    roll->rebuild();
    roll->showNotes();
    changed();
}

void MidiEditor::loadScala()
{
    const auto file = QFileDialog::getOpenFileName (this, tr ("Open a Scala Scale"), host.folder, tr ("Scala scales (*.scl);;All files (*)"));
    if (file.isEmpty())
    {
        systemChosen();   // back to what it was showing (the item picked stays picked)
        return;
    }
    auto s = std::make_unique<phi::scala::Scale>();
    std::string why;
    if (! phi::scala::readScl (file.toStdString(), *s, why))
    {
        QMessageBox::warning (this, tr ("Scala"), tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), QString::fromStdString (why)));
        return;
    }
    host.folder = QFileInfo (file).absolutePath();
    scala = std::move (s);
    scalaName = QFileInfo (file).completeBaseName();
    keyboardMap.reset();
    // A keyboard map of the same name beside it is used with it.
    const auto kbm = QFileInfo (file).absolutePath() + QStringLiteral ("/") + scalaName + QStringLiteral (".kbm");
    if (QFileInfo::exists (kbm))
    {
        auto m = std::make_unique<phi::scala::KeyboardMap>();
        if (phi::scala::readKbm (kbm.toStdString(), *m, why))
            keyboardMap = std::move (m);
    }
    auto at = systemBox->findData (-2);
    const auto text = tr ("%1 (Scala, %2 notes)").arg (scalaName).arg (scala->size());
    if (at < 0)
    {
        systemBox->insertItem (1, text, -2);
        at = 1;
    }
    else
        systemBox->setItemText (at, text);
    systemBox->setCurrentIndex (at);
    systemChosen();
}

void MidiEditor::loadKeyboardMap()
{
    const auto file = QFileDialog::getOpenFileName (this, tr ("Open a Scala Keyboard Map"), host.folder, tr ("Scala keyboard maps (*.kbm);;All files (*)"));
    if (file.isEmpty())
        return;
    auto m = std::make_unique<phi::scala::KeyboardMap>();
    std::string why;
    if (! phi::scala::readKbm (file.toStdString(), *m, why))
    {
        QMessageBox::warning (this, tr ("Scala"), tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), QString::fromStdString (why)));
        return;
    }
    keyboardMap = std::move (m);
    if (scala != nullptr)
    {
        systemBox->setCurrentIndex (systemBox->findData (-2));
        systemChosen();
    }
}

void MidiEditor::exportScala()
{
    const auto stem = QString::fromStdString (system.name).replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    const auto file = QFileDialog::getSaveFileName (this, tr ("Save as a Scala Scale"), host.folder + QStringLiteral ("/") + stem + QStringLiteral (".scl"),
                                                    tr ("Scala scales (*.scl)"));
    if (file.isEmpty())
        return;
    auto path = file;
    if (! path.endsWith (QStringLiteral (".scl"), Qt::CaseInsensitive))
        path += QStringLiteral (".scl");
    const auto s = phi::scala::fromCents (system.degrees, system.period, system.name + " (" + system.family + ", from PHI)");
    // The keyboard map: the tonic on its nearest key at its own pitch, a degree a key.
    auto map = phi::scala::linearMap (std::clamp ((int) std::lround (system.tonic / 100.0), 0, 127));
    map.referenceKey = map.middleKey;
    map.referenceHz = hzOf (system.tonic);
    const auto kbm = path.left (path.size() - 4) + QStringLiteral (".kbm");
    std::string why;
    if (! phi::scala::saveText (path.toStdString(), phi::scala::writeScl (s, QFileInfo (path).fileName().toStdString()), why)
        || ! phi::scala::saveText (kbm.toStdString(), phi::scala::writeKbm (map, QFileInfo (kbm).fileName().toStdString()), why))
        QMessageBox::warning (this, tr ("Scala"), tr ("The files could not be written: %1").arg (QString::fromStdString (why)));
}

int64_t MidiEditor::grid() const
{
    return std::max<int64_t> (1, (int64_t) std::lround (gridBox->currentData().toDouble() * seq.ticksPerQuarter));
}

int64_t MidiEditor::noteLength() const
{
    const auto q = lengthBox->currentData().toDouble();
    return q <= 0.0 ? grid() : std::max<int64_t> (1, (int64_t) std::lround (q * seq.ticksPerQuarter));
}

EncodeOptions MidiEditor::encodeOptions() const
{
    EncodeOptions o;
    o.encoding = (Encoding) encodingBox->currentData().toInt();
    o.bendRange = bendRangeBox->value();
    return o;
}

void MidiEditor::edit (const std::function<void (Sequence&)>& f)
{
    undoList.push_back (seq);
    redoList.clear();
    if (undoList.size() > 200)
        undoList.erase (undoList.begin());
    f (seq);
    changed();
}

void MidiEditor::undo()
{
    if (undoList.empty())
        return;
    redoList.push_back (seq);
    seq = undoList.back();
    undoList.pop_back();
    changed();
}

void MidiEditor::redo()
{
    if (redoList.empty())
        return;
    undoList.push_back (seq);
    seq = redoList.back();
    redoList.pop_back();
    changed();
}

void MidiEditor::changed()
{
    // A note off the keys wants a bent encoding: the keys alone would round it. Unless the track's
    // tuning plays the keys, or keys were picked by hand, MPE is taken.
    if ((Encoding) encodingBox->currentData().toInt() == Encoding::keys && ! encodingChosen && host.system < 0
        && suggestedEncoding (seq) != Encoding::keys)
        encodingBox->setCurrentIndex (encodingBox->findData ((int) Encoding::mpe));
    const auto encoding = (Encoding) encodingBox->currentData().toInt();
    if (encoding == Encoding::keys && suggestedEncoding (seq) != Encoding::keys)
        encodingNote->setText (tr ("Some notes are between the keys: saved as keys they are rounded, unless the track's tuning has them"));
    else if (encoding != Encoding::keys)
    {
        const auto e = encode (seq, encodeOptions());
        auto text = tr ("%n channel(s) for notes", nullptr, e.channels);
        if (e.clashes > 0)
            text += tr (", %n note(s) share a channel bent for another pitch (more than 15 sound at once)", nullptr, e.clashes);
        if (host.system >= 0)
            text += tr (". The track has a tuning of its own: set it to 12-TET so it is not added on top");
        encodingNote->setText (text);
    }
    else
        encodingNote->setText (host.system >= 0 ? tr ("The track's tuning plays the keys") : QString());
    roll->refresh();
    updateInfo();
}

void MidiEditor::updateInfo()
{
    updatingInfo = true;
    std::vector<const Note*> picked;
    for (const auto& n : seq.notes)
        if (n.selected)
            picked.push_back (&n);
    infoVelocity->setEnabled (! picked.empty());
    infoPitch->setEnabled (! picked.empty());
    infoLength->setEnabled (! picked.empty());
    const auto naming = (Naming) namingBox->currentData().toInt();
    if (picked.empty())
        infoLabel->setText (tr ("%n note(s) · %1 · tonic %2", nullptr, (int) seq.notes.size())
                                .arg (QString::fromStdString (system.name),
                                      QString::fromStdString (letterName (system.tonic, system.family == "maqam" || system.family == "dastgah"))));
    else
    {
        const auto& n = *picked.front();
        const auto stepAt = system.nearestStep (n.pitch);
        const auto off = n.pitch - system.pitchOf (stepAt);
        auto text = picked.size() > 1 ? tr ("%1 notes picked, the first: ").arg (picked.size()) : QString();
        text += tr ("%1 · %2, degree %3 of %4 · %5 Hz")
                    .arg (barText (n.start, seq.ticksPerQuarter),
                          QString::fromStdString (label (system, stepAt, naming)) + (std::abs (off) >= 0.5 ? QStringLiteral (" ") + QString::fromStdString (centsText (off)) : QString()))
                    .arg (system.degreeOf (stepAt) + 1)
                    .arg (system.size())
                    .arg (hzOf (n.pitch), 0, 'f', 2);
        if (n.channel == 9)
            text += tr (" · drums");
        infoLabel->setText (text);
        infoVelocity->setValue (n.velocity);
        infoPitch->setValue (off);
        infoLength->setValue ((int) std::min<int64_t> (n.length, 1 << 24));
    }
    updatingInfo = false;
}

void MidiEditor::infoEdited()
{
    if (updatingInfo || ! seq.anySelected())
        return;
    const auto velocity = infoVelocity->value();
    const auto off = infoPitch->value();
    const auto length = infoLength->value();
    const auto* changedBox = sender();
    const auto merge = undoList.size() == infoMark && ! undoList.empty();
    edit ([&] (Sequence& s)
    {
        for (auto& n : s.notes)
        {
            if (! n.selected)
                continue;
            if (changedBox == infoVelocity)
                n.velocity = velocity;
            else if (changedBox == infoLength)
                n.length = length;
            else if (changedBox == infoPitch)
                n.pitch = std::clamp (system.pitchOf (system.nearestStep (n.pitch)) + off, 0.0, 12700.0);
        }
    });
    // Typing in the boxes one change after another is one undo step.
    if (merge)
        undoList.pop_back();
    infoMark = undoList.size();
}

void MidiEditor::velocityDialog()
{
    QDialog d (this);
    d.setWindowTitle (tr ("Velocity"));
    auto* form = new QFormLayout (&d);
    auto* mode = new QComboBox (&d);
    mode->addItems ({ tr ("Set"), tr ("Compress or expand"), tr ("Add"), tr ("Ramp") });
    auto* a = new QSpinBox (&d);
    a->setRange (-127, 400);
    a->setValue (100);
    auto* b = new QSpinBox (&d);
    b->setRange (1, 127);
    b->setValue (100);
    form->addRow (tr ("How"), mode);
    form->addRow (tr ("Value (or %, or from)"), a);
    form->addRow (tr ("Ramp to"), b);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    const auto m = mode->currentIndex();
    const auto x = a->value(), y = b->value();
    edit ([m, x, y] (Sequence& s)
    {
        switch (m)
        {
            case 0: setVelocity (s, x); break;
            case 1: scaleVelocity (s, x / 100.0); break;
            case 2: scaleVelocity (s, 1.0, x); break;
            default: rampVelocity (s, std::clamp (x, 1, 127), y); break;
        }
    });
}

void MidiEditor::restyleDialog()
{
    QDialog d (this);
    d.setWindowTitle (tr ("Restyle"));
    auto* form = new QFormLayout (&d);
    const auto key = detectKey (seq);
    auto* from = new QComboBox (&d);
    from->setMaxVisibleItems (30);
    from->addItem (tr ("As found: %1 %2").arg (QString::fromUtf8 (rootNames[key.root]), key.minor ? tr ("minor") : tr ("major")), -10);
    from->addItem (tr ("Major"), -11);
    from->addItem (tr ("Minor"), -12);
    from->addItem (tr ("12-TET (the piano)"), -1);
    const auto& all = mixer::tuningSystems();
    for (int i = 0; i < (int) all.size(); ++i)
        from->addItem (QString::fromStdString (all[(size_t) i].name) + QStringLiteral ("  ·  ") + QString::fromStdString (all[(size_t) i].family), i);
    auto* fromRoot = new QComboBox (&d);
    for (const auto* n : rootNames)
        fromRoot->addItem (QString::fromUtf8 (n));
    fromRoot->setCurrentIndex (key.root);
    auto* how = new QComboBox (&d);
    how->addItem (tr ("By degree: the melody's shape kept"));
    how->addItem (tr ("To the nearest pitch"));
    form->addRow (new QLabel (tr ("Every picked note (all, if none is picked) moved into <b>%1</b> on %2. The notes it had are kept as "
                                  "ghosts behind; Ctrl+Z puts them back.").arg (QString::fromStdString (system.name).toHtmlEscaped(),
                                                                               QString::fromStdString (letterName (system.tonic, false))), &d));
    form->addRow (tr ("The melody is in"), from);
    form->addRow (tr ("on"), fromRoot);
    form->addRow (tr ("Move each note"), how);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    const auto choice = from->currentData().toInt();
    const auto root = fromRoot->currentIndex();
    PitchSystem source;
    if (choice == -10)
        source = majorOrMinor (root, key.minor);
    else if (choice == -11 || choice == -12)
        source = majorOrMinor (root, choice == -12);
    else if (choice == -1)
        source = twelveTet (root);
    else
        source = fromTuning (all[(size_t) choice], root);
    ghosts = seq;
    for (auto& n : ghosts.notes)
        n.selected = false;
    const auto byDegree = how->currentIndex() == 0;
    edit ([&] (Sequence& s) { restyle (s, source, system, byDegree); });
}

double MidiEditor::pitchForKey (int key) const
{
    // A system of up to twelve degrees in the octave: a key plays the degree nearest it, as the
    // track's tuning would. Larger ones: a degree a key from the tonic's key.
    if (system.size() <= 12 && std::abs (system.period - 1200.0) < 1.0e-6)
        return system.pitchOf (system.nearestStep (key * 100.0));
    const auto tonicKey = (int) std::lround (system.tonic / 100.0);
    return std::clamp (system.pitchOf (key - tonicKey), 0.0, 12700.0);
}

void MidiEditor::stepPitch (double pitch, bool chord)
{
    if (chord && chordAt >= 0)
    {
        auto in = step;
        in.position = chordAt;
        in.chord = true;
        in.insert = false;
        edit ([&] (Sequence& s) { stepNote (s, in, pitch); });
    }
    else
    {
        chordAt = step.position;
        step.chord = chordBox->isChecked();
        step.insert = insertBox->isChecked();
        step.velocity = std::clamp (step.velocity, 1, 127);
        edit ([&] (Sequence& s) { stepNote (s, step, pitch); });
    }
    // The cursor kept in view.
    roll->refresh();
}

void MidiEditor::midiIn (uint8_t status, uint8_t data1, uint8_t data2)
{
    const auto kind = status & 0xF0;
    if (kind == 0x90 && data2 > 0)
    {
        if (stepBox->isChecked())
        {
            step.velocity = data2;
            stepPitch (pitchForKey (data1), heldKeys > 0);
        }
        ++heldKeys;
    }
    else if (kind == 0x80 || kind == 0x90)
        heldKeys = std::max (0, heldKeys - 1);
}

void MidiEditor::keyPressEvent (QKeyEvent* e)
{
    const auto mods = e->modifiers();
    const auto ctrl = (mods & Qt::ControlModifier) != 0, shift = (mods & Qt::ShiftModifier) != 0, alt = (mods & Qt::AltModifier) != 0;
    if (ctrl && e->key() == Qt::Key_Z)
    {
        shift ? redo() : undo();
        return;
    }
    if (ctrl && e->key() == Qt::Key_Y)
    {
        redo();
        return;
    }
    if (ctrl && e->key() == Qt::Key_A)
    {
        selectAll (seq, true);
        changed();
        return;
    }
    if (stepBox->isChecked() && ! ctrl && ! alt)
    {
        // The computer's keys as the system's degrees: A row from the tonic, Q row the period up,
        // Z row the period down.
        static const QString rows[3] { QStringLiteral ("zxcvbnm,./"), QStringLiteral ("asdfghjkl;'"), QStringLiteral ("qwertyuiop[]") };
        const auto text = e->text().toLower();
        if (text.size() == 1)
            for (int r = 0; r < 3; ++r)
                if (const auto at = rows[r].indexOf (text[0]); at >= 0)
                {
                    const auto periods = octaveBox->value() - ((int) std::floor (system.tonic / 1200.0) - 1) + r - 1;
                    const auto pitch = std::clamp (system.pitchOf (periods * system.size() + at), 0.0, 12700.0);
                    audition (pitch);
                    stepPitch (pitch, shift);
                    return;
                }
        if (e->key() == Qt::Key_Space)
        {
            edit ([this] (Sequence& s) { stepRest (s, step); });
            return;
        }
        if (e->key() == Qt::Key_Backspace)
        {
            edit ([this] (Sequence& s) { stepBack (s, step); });
            return;
        }
        if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right)
        {
            step.position = std::max<int64_t> (0, step.position + (e->key() == Qt::Key_Left ? -step.step : step.step));
            roll->refresh();
            return;
        }
    }
    switch (e->key())
    {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            edit ([] (Sequence& s) { deleteTargets (s); });
            return;
        case Qt::Key_Q:
            edit ([this] (Sequence& s) { quantizeStarts (s, grid(), strengthBox->value() / 100.0, swingBox->value() / 100.0); });
            return;
        case Qt::Key_Up:
        case Qt::Key_Down:
        {
            const auto dir = e->key() == Qt::Key_Up ? 1 : -1;
            if (! seq.anySelected())
                return;
            if (alt)
                edit ([dir] (Sequence& s) { transposeCents (s, dir); });
            else
                edit ([this, dir, shift] (Sequence& s) { transposeSteps (s, system, dir * (shift ? system.size() : 1)); });
            for (const auto& n : seq.notes)
                if (n.selected)
                {
                    audition (n.pitch, n.velocity);
                    break;
                }
            return;
        }
        case Qt::Key_Left:
        case Qt::Key_Right:
            if (seq.anySelected())
            {
                const auto by = (e->key() == Qt::Key_Left ? -1 : 1) * (alt ? 1 : grid());
                edit ([by] (Sequence& s) { nudge (s, by); });
            }
            return;
        case Qt::Key_Space:
            togglePlay();
            return;
        case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4: case Qt::Key_5:
            if (! ctrl)
            {
                const auto t = e->key() - Qt::Key_1;
                toolButtons[(size_t) t]->setChecked (true);
                roll->tool = t;
                return;
            }
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return;   // not "Apply": the dialog stays open
        default: break;
    }
    QDialog::keyPressEvent (e);
}

void MidiEditor::audition (double pitch, int velocity)
{
    if (! host.play)
        return;
    // On channel 16 (an MPE member), bent to the pitch with a range of two semitones, unless
    // the notes are saved as keys (then the track's tuning has them).
    constexpr uint8_t ch = 15;
    const auto key = std::clamp ((int) std::lround (pitch / 100.0), 0, 127);
    if ((Encoding) encodingBox->currentData().toInt() != Encoding::keys)
    {
        const auto bend = std::clamp ((int) std::lround (8192.0 + (pitch - key * 100.0) / 200.0 * 8192.0), 0, 16383);
        host.play (0xB0 | ch, 101, 0);
        host.play (0xB0 | ch, 100, 0);
        host.play (0xB0 | ch, 6, 2);
        host.play (0xB0 | ch, 38, 0);
        host.play (0xE0 | ch, (uint8_t) (bend & 0x7F), (uint8_t) (bend >> 7));
    }
    host.play (0x90 | ch, (uint8_t) key, (uint8_t) std::clamp (velocity, 1, 127));
    auto play = host.play;
    QTimer::singleShot (320, this, [play, key] { play (0x80 | ch, (uint8_t) key, 64); });
}

void MidiEditor::togglePlay()
{
    if (playTimer->isActive())
        stopPlay();
    else
        startPlay (abButton->isChecked());
}

void MidiEditor::startPlay (bool originalVersion)
{
    if (! host.play)
        return;
    // Going on from where it is (the A/B switch), else from the playhead.
    double from = 0.0;
    if (playTimer->isActive() && playSong != nullptr)
        from = playFrom + (QDateTime::currentMSecsSinceEpoch() - playStarted) / 1000.0;
    else if (playheadTick > 0)
        from = (originalVersion ? original : *encode (seq, encodeOptions()).song).seconds (playheadTick, host.bpm);
    stopPlay();
    playingOriginal = originalVersion;
    playSong = originalVersion ? std::make_shared<midifile::Song> (original) : encode (seq, encodeOptions()).song;
    playing.clear();
    // The set-up (programs, bend ranges, the MPE zone) before the start is sent at once.
    for (const auto& e : playSong->events)
    {
        const auto at = playSong->seconds (e.tick, host.bpm);
        if (at < from && (e.kind() == 0x90 || e.kind() == 0x80))
            continue;
        playing.push_back ({ std::max (at, from), e.status, e.data1, e.data2 });
    }
    std::stable_sort (playing.begin(), playing.end(), [] (const Timed& a, const Timed& b) { return a.seconds < b.seconds; });
    playAt = 0;
    playFrom = from;
    playStarted = QDateTime::currentMSecsSinceEpoch();
    playButton->setText (tr ("Stop"));
    playTimer->start();
}

void MidiEditor::stopPlay()
{
    if (playTimer == nullptr || ! playTimer->isActive())
        return;
    playTimer->stop();
    if (host.play)
        for (uint8_t c = 0; c < 16; ++c)
        {
            host.play (0xB0 | c, 123, 0);   // all notes off
            host.play (0xE0 | c, 0, 64);    // the bend back to the middle
        }
    // No MPE zone left behind on the track's instrument: an ordinary clip bends ±2 again.
    if (host.play)
        for (auto [cc, v] : { std::pair { 101, 0 }, { 100, 6 }, { 6, 0 }, { 101, 127 }, { 100, 127 } })
            host.play (0xB0, (uint8_t) cc, (uint8_t) v);
    playButton->setText (tr ("Play"));
    roll->refresh();
}

void MidiEditor::playTick()
{
    const auto now = playFrom + (QDateTime::currentMSecsSinceEpoch() - playStarted) / 1000.0;
    while (playAt < playing.size() && playing[playAt].seconds <= now)
    {
        const auto& t = playing[playAt++];
        host.play (t.status, t.data1, t.data2);
    }
    // The playhead: the tick at this time, found on the song's own tempo map.
    if (playSong != nullptr)
    {
        int64_t lo = 0, hi = std::max<int64_t> (1, playSong->lastTick);
        while (lo < hi)
        {
            const auto mid = (lo + hi) / 2;
            if (playSong->seconds (mid, host.bpm) < now)
                lo = mid + 1;
            else
                hi = mid;
        }
        playheadTick = lo;
        if (playAt >= playing.size() && now > playSong->lengthSeconds (host.bpm) + 0.3)
        {
            playheadTick = 0;
            stopPlay();
            return;
        }
    }
    roll->viewport()->update();
}

void MidiEditor::done (int r)
{
    stopPlay();
    QDialog::done (r);
}

midifile::SongPtr MidiEditor::result() const
{
    auto s = seq;
    for (auto& n : s.notes)
        n.selected = false;
    return encode (s, encodeOptions()).song;
}

QString MidiEditor::summary() const
{
    const auto o = encodeOptions();
    const auto e = encode (seq, o);
    switch (o.encoding)
    {
        case Encoding::mpe: return tr ("Edited, in %1, saved as MPE (%n channel(s))", nullptr, e.channels).arg (QString::fromStdString (system.name));
        case Encoding::bendChannels: return tr ("Edited, in %1, saved with pitch bend on %n channel(s)", nullptr, e.channels).arg (QString::fromStdString (system.name));
        case Encoding::keys: break;
    }
    return tr ("Edited, in %1").arg (QString::fromStdString (system.name));
}
