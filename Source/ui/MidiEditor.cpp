// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiEditor.h"
#include "MidiStaff.h"
#include "ModeAtlas.h"
#include "TransportBar.h"

#include "../core/Scala.h"
#include "../engine/AudioCapture.h"
#include "../mixer/DrumsToMidi.h"
#include "../mixer/Rhythms.h"
#include "../mixer/Tunings.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
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
#include <limits>
#include <map>

using namespace noteedit;

namespace
{
    enum Tool { toolPick, toolDraw, toolErase, toolSplit, toolGlue, toolCurve };

    constexpr int keyWidth = 96, rulerHeight = 22, laneHeight = 110, laneGap = 6;
    constexpr int laneVelocity = -1;

    const char* const rootNames[12] { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" };

    // The grids, in quarter notes.
    struct GridChoice { const char* name; double quarters; };
    constexpr int straightGrids = 6;   // 1/1 to 1/32, then the triplets
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

    bool sameSystem (const PitchSystem& a, const PitchSystem& b)
    {
        if (a.name != b.name || std::abs (a.tonic - b.tonic) > 1.0e-6 || std::abs (a.period - b.period) > 1.0e-6 || a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i)
            if (std::abs (a.degrees[(size_t) i] - b.degrees[(size_t) i]) > 1.0e-6)
                return false;
        return true;
    }

    // The notes Ctrl+C and Ctrl+X took, from the first one's start: pasted in any MIDI editor.
    std::vector<Note> noteClipboard;

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

    // Scrolled so the tick is in view: at the left edge for the start, else in the middle.
    void reveal (int64_t tick)
    {
        updateBars();
        horizontalScrollBar()->setValue (tick <= 0 ? 0 : (int) std::max (0.0, tick * pixelsPerTick - viewport()->width() / 2.0));
        viewport()->update();
    }

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

        // Tuning changes: where a stretch is in another system than the rows, its degrees drawn
        // across it.
        if (! ed.seq.systems.empty())
        {
            const auto high = pitchAt (top, false), low = pitchAt (top + gridHeight(), false);
            for (size_t k = 0; k <= ed.seq.systems.size(); ++k)
            {
                const auto& sys = k == 0 ? ed.base : ed.seq.systems[k - 1].system;
                if (sameSystem (sys, ed.system))
                    continue;
                const auto x0 = std::max ((double) keyWidth, xOf (k == 0 ? 0 : ed.seq.systems[k - 1].tick));
                const auto x1 = k < ed.seq.systems.size() ? std::min ((double) w, xOf (ed.seq.systems[k].tick)) : (double) w;
                if (x1 <= x0)
                    continue;
                auto c = QColor (0xe0, 0x8a, 0x2a);
                c.setAlphaF (0.07);
                g.fillRect (QRectF (x0, rulerHeight, x1 - x0, gridHeight()), c);
                for (auto st : sys.steps (low - 50.0, high + 50.0))
                {
                    const auto y = rulerHeight + centerY (sys.pitchOf (st)) - top;
                    c.setAlphaF (sys.degreeOf (st) == 0 ? 0.8 : 0.45);
                    g.setPen (QPen (c, sys.degreeOf (st) == 0 ? 1.4 : 0.8));
                    g.drawLine (QPointF (x0, y), QPointF (x1, y));
                }
            }
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
            if (! n.curve.empty())
            {
                // Its pitch curve, through the rows it bends to.
                QPainterPath path;
                const auto every = std::max<int64_t> (1, (int64_t) (2.0 / pixelsPerTick));
                for (int64_t t = 0; t <= n.length; t = t == n.length ? t + 1 : std::min (n.length, t + every))
                {
                    const QPointF at (xOf (n.start + t), rulerHeight + centerY (n.pitch + n.bendAt (t)) - top);
                    if (t == 0)
                        path.moveTo (at);
                    else
                        path.lineTo (at);
                }
                g.setPen (QPen (n.selected ? pal.color (QPalette::Text) : QColor (0xe0, 0x8a, 0x2a), 2.0));
                g.setBrush (Qt::NoBrush);
                g.drawPath (path);
                g.setBrush (c);
            }
            if (r.width() > 28 && r.height() >= 9)
            {
                const auto& sys = ed.systemAt (n.start);
                const auto step = sys.nearestStep (n.pitch);
                const auto off = n.pitch - sys.pitchOf (step);
                auto text = QString::fromStdString (label (sys, step, naming()));
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
        // The tuning changes, flagged with their system's name.
        for (const auto& c : ed.seq.systems)
        {
            const auto x = xOf (c.tick);
            if (x < keyWidth || x > w)
                continue;
            g.fillRect (QRectF (x, 2, 3, rulerHeight - 2), QColor (0xe0, 0x8a, 0x2a));
            g.drawText (QPointF (x + 5, 11), QString::fromStdString (c.system.name));
        }
        g.fillRect (QRect (0, 0, keyWidth, rulerHeight), pal.color (QPalette::Window));
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
            // The ruler: the step cursor and the playhead go there, playing on from there if it plays.
            ed.goTo (snapTick (tickAt (x), false), false);
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
            if (hit >= 0 && tool == toolCurve)
                ed.edit ([hit] (Sequence& s) { s.notes[(size_t) hit].curve.clear(); });
            else if (hit >= 0)
                ed.edit ([hit] (Sequence& s) { s.notes.erase (s.notes.begin() + hit); });
            return;
        }
        switch (tool)
        {
            case toolCurve:
                if (hit >= 0)
                {
                    ed.undoList.push_back (ed.seq);
                    ed.redoList.clear();
                    before = ed.seq;
                    drawn = hit;
                    drag = Drag::curve;
                    curveMoved (e->position(), e->modifiers() & Qt::AltModifier);
                }
                return;
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
                    if (e->modifiers() & Qt::ShiftModifier)
                    {
                        // Freehand (Live's Alt in Draw, which is "between the degrees" here): a note a
                        // grid step wherever the mouse goes.
                        drag = Drag::painting;
                        paintedTo = snapTick (t, false) + ed.noteLength();
                    }
                    return;
                }
                // As in Live's Draw mode: a click on a note erases it, so drawing needs no other tool.
                ed.edit ([hit] (Sequence& s) { s.notes.erase (s.notes.begin() + hit); });
                drag = Drag::erasing;
                return;
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
        if (e->modifiers() & Qt::ControlModifier)
        {
            // Ctrl-click picks or unpicks the note (on the release, if it was picked); Ctrl-drag
            // sets the picked notes' velocity, as in Live.
            toggled = n.selected ? hit : -1;
            n.selected = true;
            anchorTick = n.start;
            anchorPitch = n.pitch;
            ed.undoList.push_back (ed.seq);
            ed.redoList.clear();
            before = ed.seq;
            drag = Drag::velocity;
            ed.updateInfo();
            viewport()->update();
            return;
        }
        if (e->modifiers() & Qt::ShiftModifier)
        {
            // From the last note clicked to this one: the notes between them in time and pitch.
            if (anchorTick >= 0)
            {
                const auto from = std::min (anchorTick, n.start), to = std::max (anchorTick, n.start);
                const auto low = std::min (anchorPitch, n.pitch) - 0.5, high = std::max (anchorPitch, n.pitch) + 0.5;
                for (auto& m : ed.seq.notes)
                    if (m.start >= from && m.start <= to && m.pitch >= low && m.pitch <= high)
                        m.selected = true;
            }
            n.selected = true;
        }
        else if (! n.selected)
        {
            selectAll (ed.seq, false);
            n.selected = true;
        }
        if (! (e->modifiers() & Qt::ShiftModifier))
        {
            anchorTick = n.start;
            anchorPitch = n.pitch;
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
        if (tool != toolPick)
            return;   // Draw has drawn it on the press already (or erased the note there)
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
            case Drag::painting:
            {
                const auto at = snapTick (tickAt (x), false);
                if (at >= paintedTo)
                {
                    Note n;
                    n.start = at;
                    n.length = ed.noteLength();
                    n.pitch = pitchAt (e->position().y() - rulerHeight + verticalScrollBar()->value(), ! free);
                    n.velocity = ed.step.velocity;
                    ed.seq.notes.push_back (n);
                    ed.seq.sort();
                    ed.seq.lastTick = std::max (ed.seq.lastTick, ed.seq.end());
                    paintedTo = at + n.length;
                    ed.audition (n.pitch, n.velocity);
                    ed.changed();
                }
                break;
            }
            case Drag::velocity:
                if (std::abs (dy) > 2.0 || velocityDragged)
                {
                    velocityDragged = true;
                    ed.seq = before;
                    for (auto& n : ed.seq.notes)
                        if (n.selected)
                            n.velocity = std::clamp (n.velocity - (int) std::lround (dy / 2.0), 1, 127);
                    ed.changed();
                }
                break;
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
            case Drag::curve:
                curveMoved (e->position(), free);
                break;
            case Drag::none: break;
        }
    }

    void mouseReleaseEvent (QMouseEvent*) override
    {
        if (drag == Drag::rubber && (currentPos - pressPos).manhattanLength() < 4.0)
        {
            // A click on empty space, not a box: the playhead there (playing on from there if it plays).
            drag = Drag::none;
            ed.goTo (snapTick (tickAt (pressPos.x()), false), false);
            return;
        }
        if (drag == Drag::curve && ! ed.undoList.empty() && drawn >= 0 && (size_t) drawn < ed.seq.notes.size()
            && ed.seq.notes[(size_t) drawn].curve == before.notes[(size_t) drawn].curve)
            ed.undoList.pop_back();
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
        if (drag == Drag::velocity && ! velocityDragged && ! ed.undoList.empty())
        {
            // A Ctrl-click: the note unpicked if it was picked, and no undo step.
            ed.undoList.pop_back();
            if (toggled >= 0 && (size_t) toggled < ed.seq.notes.size())
                ed.seq.notes[(size_t) toggled].selected = false;
            ed.updateInfo();
        }
        drag = Drag::none;
        drawn = -1;
        toggled = -1;
        velocityDragged = false;
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
    enum class Drag { none, move, resize, drawing, painting, erasing, rubber, lane, laneErase, curve, velocity };
    int64_t paintedTo = 0;           // painting: where the last note painted ends
    int toggled = -1;                // a Ctrl-click on a picked note: unpicked unless it drags
    bool velocityDragged = false;
    int64_t anchorTick = -1;         // the last note clicked, where Shift-click picks from
    double anchorPitch = 0.0;

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
        // On a degree of the rows shown, but after a tuning change: that stretch's nearest degree.
        if (const auto& sys = ed.systemAt (start); std::abs (pitch - ed.system.pitchOf (ed.system.nearestStep (pitch))) < 1.0e-6 && ! sameSystem (sys, ed.system))
            pitch = sys.pitchOf (sys.nearestStep (pitch));
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

    // The Curve tool: the note's pitch drawn where the mouse goes, on the degrees unless Alt is
    // held; a drag back in time redraws from there.
    void curveMoved (QPointF p, bool free)
    {
        if (drawn < 0 || (size_t) drawn >= ed.seq.notes.size())
            return;
        auto& n = ed.seq.notes[(size_t) drawn];
        const auto t = std::clamp<int64_t> (tickAt (p.x()) - n.start, 0, n.length);
        const auto cents = pitchAt (p.y() - rulerHeight + verticalScrollBar()->value(), ! free) - n.pitch;
        if (n.curve.empty())
            n.curve.push_back ({ 0, 0.0 });
        std::erase_if (n.curve, [&] (const auto& q) { return q.first >= t && q.first > 0; });
        if (n.curve.empty() || n.curve.back().first < t)
            n.curve.push_back ({ t, cents });
        else
            n.curve.back().second = cents;
        ed.changed();
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
// The rising spectrum (owner, 2026-10-26: "the vertical MIDI view ... with a spectral frequency
// view to show the frequencies rolling up as a representation of the notes"): frequency across
// on a log scale, time down the page, each note drawn at its fundamental and its harmonics
// fading above it, so what a chord puts in the air shows as a comb of lines. The system's degrees
// are the lines across the bottom, named, with the twelve keys dotted where they differ. While it
// plays, the notes roll up past the line where they sound.
class MidiEditor::Spectrum final : public QWidget
{
public:
    explicit Spectrum (MidiEditor& e) : QWidget (&e), ed (e)
    {
        setMinimumSize (640, 360);
        setFocusPolicy (Qt::StrongFocus);
        setMouseTracking (true);
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
            low = high = ed.system.tonic;
        lowCents = std::max (1500.0, low - 1200.0);
        highCents = std::min (13500.0, high + 1200.0 * std::log2 (std::max (2, ed.harmonicsBox->value())) + 600.0);
        topTick = 0;
        update();
    }

    void keyPressEvent (QKeyEvent* e) override { ed.keyPressEvent (e); }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        const auto pal = palette();
        g.fillRect (rect(), pal.color (QPalette::Base));
        const auto w = width(), h = height() - rulerHeight;
        const auto playing = ed.playTimer->isActive() && ed.playheadTick >= 0;
        const auto ppt = pixelsPerTick();
        // Time down the page; while it plays the playhead holds still near the bottom and the notes rise.
        const auto yOf = [&] (int64_t t)
        {
            if (playing)
                return h * 0.8 - (double) (ed.playheadTick - t) * ppt;
            return (double) (t - topTick) * ppt;
        };

        // The degrees, across the whole range, and the keys dotted where the system has none.
        auto faint = pal.color (QPalette::Mid);
        faint.setAlphaF (0.35);
        if (! ed.system.isTwelveTet())
        {
            g.setPen (QPen (faint, 0.5, Qt::DotLine));
            for (int key = (int) std::ceil (lowCents / 100.0); key * 100.0 <= highCents; ++key)
                g.drawLine (QPointF (xOf (key * 100.0), 0), QPointF (xOf (key * 100.0), h));
        }
        auto small = font();
        small.setPointSizeF (std::max (6.5, small.pointSizeF() * 0.8));
        g.setFont (small);
        double lastLabel = -1.0e9;
        for (auto st : ed.system.steps (lowCents, highCents))
        {
            const auto x = xOf (ed.system.pitchOf (st));
            const auto tonic = ed.system.degreeOf (st) == 0;
            g.setPen (QPen (tonic ? pal.color (QPalette::Highlight) : pal.color (QPalette::Mid), tonic ? 1.2 : 0.5));
            g.drawLine (QPointF (x, 0), QPointF (x, h));
            if (x - lastLabel > 26.0 || tonic)
            {
                g.setPen (tonic ? pal.color (QPalette::Highlight) : pal.color (QPalette::WindowText));
                g.drawText (QRectF (x - 30, h + 2, 60, 14), Qt::AlignCenter,
                            QString::fromStdString (label (ed.system, st, (Naming) ed.namingBox->currentData().toInt())));
                lastLabel = x;
            }
        }
        // Frequencies along the bottom.
        g.setPen (pal.color (QPalette::PlaceholderText));
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const auto c = 6900.0 + 1200.0 * std::log2 (hz / 440.0);
            if (c < lowCents || c > highCents)
                continue;
            g.drawText (QRectF (xOf (c) - 30, h + 15, 60, 13), Qt::AlignCenter,
                        hz >= 1000.0 ? QStringLiteral ("%1 kHz").arg (hz / 1000.0) : QStringLiteral ("%1 Hz").arg (hz));
        }

        // Bars across, faintly.
        const auto bar = (int64_t) 4 * ed.seq.ticksPerQuarter;
        auto barColour = pal.color (QPalette::Mid);
        barColour.setAlphaF (0.3);
        g.setPen (QPen (barColour, 0.5));
        for (int64_t t = 0; t <= ed.seq.end() + bar; t += bar)
            if (const auto y = yOf (t); y >= 0 && y <= h)
                g.drawLine (QPointF (0, y), QPointF (w, y));

        // The ghosts, then the notes: the fundamental, and each harmonic fainter and thinner.
        const auto harmonics = ed.harmonicsBox->value();
        const auto drawNote = [&] (const Note& n, bool ghost)
        {
            const auto y0 = yOf (n.start), y1 = yOf (n.end());
            if (y1 < 0 || y0 > h)
                return;
            QColor c = ghost ? pal.color (QPalette::Text) : n.selected ? pal.color (QPalette::Highlight) : QColor (0x4f, 0x9d, 0xd8);
            if (! ghost && n.channel == 9)
                c = QColor (0xc8, 0x8a, 0x3a);
            const auto sounding = playing && n.start <= ed.playheadTick && n.end() > ed.playheadTick;
            for (int k = 1; k <= (ghost ? 1 : harmonics); ++k)
            {
                const auto pc = n.pitch + 1200.0 * std::log2 ((double) k);
                if (pc > highCents)
                    break;
                const auto x = xOf (pc);
                auto col = c;
                col.setAlphaF (ghost ? 0.25 : std::clamp ((0.35 + 0.65 * n.velocity / 127.0) / std::pow ((double) k, 0.8) * (sounding ? 1.6 : 1.0), 0.05, 1.0));
                const auto width = k == 1 ? 5.0 : std::max (1.0, 3.0 / std::sqrt ((double) k));
                if (! n.curve.empty() && ! ghost)
                {
                    // A curved note: each partial follows the bend.
                    QPainterPath path;
                    constexpr int samples = 32;
                    for (int i = 0; i <= samples; ++i)
                    {
                        const auto t = n.length * i / samples;
                        const QPointF at (xOf (pc + n.bendAt (t)), yOf (n.start + t));
                        if (i == 0)
                            path.moveTo (at);
                        else
                            path.lineTo (at);
                    }
                    g.setPen (QPen (col, width, Qt::SolidLine, Qt::FlatCap));
                    g.setBrush (Qt::NoBrush);
                    g.drawPath (path);
                    continue;
                }
                g.fillRect (QRectF (x - width / 2.0, std::max (0.0, y0), width, std::min ((double) h, y1) - std::max (0.0, y0)), col);
            }
        };
        if (ed.ghostBox->isChecked())
            for (const auto& n : ed.ghosts.notes)
                drawNote (n, true);
        for (const auto& n : ed.seq.notes)
            drawNote (n, false);

        // The line where notes sound, the step cursor.
        if (playing)
        {
            g.setPen (QPen (pal.color (QPalette::Text), 1.5));
            g.drawLine (QPointF (0, h * 0.8), QPointF (w, h * 0.8));
        }
        else if (ed.stepBox->isChecked())
        {
            g.setPen (QPen (QColor (0xe0, 0x50, 0x50), 2.0));
            const auto y = yOf (ed.step.position);
            g.drawLine (QPointF (0, y), QPointF (w, y));
        }
        g.fillRect (QRect (0, h, w, rulerHeight), Qt::transparent);
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        setFocus();
        // A note picked by its fundamental: the nearest under the click.
        const auto t = topTick + (int64_t) std::lround (e->position().y() / pixelsPerTick());
        const auto c = centsAt (e->position().x());
        int best = -1;
        double bestDistance = 1.0e9;
        for (size_t i = 0; i < ed.seq.notes.size(); ++i)
        {
            const auto& n = ed.seq.notes[i];
            if (t < n.start || t > n.end())
                continue;
            const auto d = std::abs (xOf (n.pitch) - xOf (c));
            if (d < bestDistance && d < 8.0)
            {
                bestDistance = d;
                best = (int) i;
            }
        }
        if (! (e->modifiers() & Qt::ShiftModifier))
            selectAll (ed.seq, false);
        if (best >= 0)
        {
            ed.seq.notes[(size_t) best].selected = true;
            ed.audition (ed.seq.notes[(size_t) best].pitch, ed.seq.notes[(size_t) best].velocity);
        }
        else
            ed.audition (ed.system.pitchOf (ed.system.nearestStep (c)));
        ed.changed();
    }

    void wheelEvent (QWheelEvent* e) override
    {
        const auto steps = e->angleDelta().y() / 120.0;
        if (e->modifiers() & Qt::ControlModifier)
        {
            // Zoom across, about the pointer.
            const auto at = centsAt (e->position().x());
            const auto f = std::pow (0.85, steps);
            lowCents = std::max (0.0, at - (at - lowCents) * f);
            highCents = std::min (14400.0, at + (highCents - at) * f);
        }
        else if (e->modifiers() & Qt::ShiftModifier)
        {
            const auto shift = (highCents - lowCents) * 0.1 * -steps;
            lowCents = std::max (0.0, lowCents + shift);
            highCents = std::min (14400.0, highCents + shift);
        }
        else if (e->modifiers() & Qt::AltModifier)
            zoomY = std::clamp (zoomY * std::pow (1.2, steps), 0.1, 10.0);
        else
            topTick = std::max<int64_t> (0, topTick - (int64_t) std::lround (steps * 60.0 / pixelsPerTick()));
        update();
    }

private:
    static constexpr int rulerHeight = 30;
    double pixelsPerTick() const { return 0.25 * zoomY; }
    double xOf (double cents) const { return (cents - lowCents) / std::max (1.0, highCents - lowCents) * width(); }
    double centsAt (double x) const { return lowCents + x / std::max (1, width()) * (highCents - lowCents); }

    MidiEditor& ed;
    double lowCents = 3600.0, highCents = 10800.0, zoomY = 1.0;
    int64_t topTick = 0;
};

//==============================================================================
// The scale wheel and the interval clock (after MIDIRestyle's degree view and XenRoll's clock
// diagram): the period as a circle, the tonic at the top, each degree at its true angle against
// the twelve keys' ticks, so a neutral third sits between the major and minor. The ring outside
// shows how long each degree sounds in the clip. The notes sounding at the playhead (or the step
// cursor, or the picked ones) are lit, joined by lines marked with their intervals in cents, and
// the meter says how rough they are together.
class MidiEditor::Wheel final : public QWidget
{
public:
    explicit Wheel (MidiEditor& e) : QWidget (&e), ed (e) { setFixedWidth (280); }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        g.setRenderHint (QPainter::Antialiasing);
        const auto pal = palette();
        g.fillRect (rect(), pal.color (QPalette::Window));
        const auto size = std::min (width() - 20.0, height() - 90.0);
        const QPointF centre (width() / 2.0, 14 + size / 2.0);
        const auto radius = size / 2.0 - 26.0;
        const auto period = ed.system.period > 0 ? ed.system.period : 1200.0;
        const auto at = [&] (double cents, double r)
        {
            const auto a = cents / period * 2.0 * 3.141592653589793;
            return QPointF (centre.x() + r * std::sin (a), centre.y() - r * std::cos (a));
        };
        g.setPen (QPen (pal.color (QPalette::Mid), 1.0));
        g.setBrush (pal.color (QPalette::Base));
        g.drawEllipse (centre, radius, radius);
        // The twelve keys' ticks (by the octave, whatever the period).
        g.setPen (QPen (pal.color (QPalette::Mid), 1.0));
        for (int k = 0; k < 12; ++k)
            g.drawLine (at (k * 100.0 * period / 1200.0, radius - 6), at (k * 100.0 * period / 1200.0, radius));

        // How long each degree sounds: a bar in from the rim, longer the more it sounds.
        const auto use = degreeUsage (ed.seq, ed.system);
        const auto most = std::max (1.0, *std::max_element (use.begin(), use.end()));
        auto useColour = pal.color (QPalette::Highlight);
        useColour.setAlphaF (0.35);
        for (int d = 0; d < ed.system.size(); ++d)
        {
            if (use[(size_t) d] <= 0.0)
                continue;
            const auto len = 4.0 + 22.0 * use[(size_t) d] / most;
            g.setPen (QPen (useColour, 5.0, Qt::SolidLine, Qt::FlatCap));
            g.drawLine (at (ed.system.degrees[(size_t) d], radius - 8), at (ed.system.degrees[(size_t) d], radius - 8 - len));
        }

        // What sounds now.
        std::vector<double> now;
        const auto playing = ed.playTimer->isActive() || ed.stepBox->isChecked();
        if (playing)
            for (auto i : soundingAt (ed.seq, ed.nowTick()))
                if (ed.seq.notes[i].channel != 9)
                    now.push_back (ed.seq.notes[i].pitch);
        if (now.empty())
            for (const auto& n : ed.seq.notes)
                if (n.selected && n.channel != 9)
                    now.push_back (n.pitch);
        const auto within = [&] (double p)
        {
            auto c = std::fmod (p - ed.system.tonic, period);
            return c < 0 ? c + period : c;
        };
        // Each place on the wheel once (an octave doubling is the same place).
        std::vector<double> places;
        for (auto p : now)
        {
            const auto c = within (p);
            if (std::none_of (places.begin(), places.end(), [&] (double x) { return std::abs (x - c) < 0.5; }))
                places.push_back (c);
        }

        // The degrees, named, lit when they sound.
        auto small = font();
        small.setPointSizeF (std::max (6.5, small.pointSizeF() * 0.85));
        g.setFont (small);
        const auto naming = (Naming) ed.namingBox->currentData().toInt();
        for (int d = 0; d < ed.system.size(); ++d)
        {
            const auto c = ed.system.degrees[(size_t) d];
            bool lit = false;
            for (auto p : now)
                lit = lit || std::abs (within (p) - c) < 1.0 || std::abs (within (p) - c - period) < 1.0;
            g.setPen (Qt::NoPen);
            g.setBrush (lit ? pal.color (QPalette::Highlight) : d == 0 ? pal.color (QPalette::Highlight).darker (130) : pal.color (QPalette::Text));
            g.drawEllipse (at (c, radius), lit ? 6.0 : 3.5, lit ? 6.0 : 3.5);
            if (ed.system.size() <= 24 || d % std::max (1, ed.system.size() / 24) == 0)
            {
                g.setPen (pal.color (QPalette::WindowText));
                // The name without its octave (the degree is in the tonic's period, so no dots).
                auto name = QString::fromStdString (label (ed.system, d, naming)).section (QLatin1Char (' '), 0, 0);
                if (naming == Naming::letters || (naming == Naming::tradition && name.size() > 1 && name.back().isDigit()))
                    name.remove (QRegularExpression (QStringLiteral ("-?\\d+$")));
                const auto p = at (c, radius + 15);
                g.drawText (QRectF (p.x() - 24, p.y() - 8, 48, 16), Qt::AlignCenter, name);
            }
        }

        // The interval clock: each pair joined, the interval in cents at the middle.
        g.setFont (small);
        for (size_t i = 0; i < places.size(); ++i)
            for (size_t j = i + 1; j < places.size(); ++j)
            {
                const auto a = at (places[i], radius), b = at (places[j], radius);
                g.setPen (QPen (pal.color (QPalette::Highlight), 1.5));
                g.drawLine (a, b);
                if (places.size() > 4)
                    continue;   // too many to read: the lines alone
                // The interval the shorter way round, as a chord's inversion is heard.
                auto interval = std::abs (places[j] - places[i]);
                interval = std::min (interval, period - interval);
                g.setPen (pal.color (QPalette::Text));
                g.drawText (QRectF ((a.x() + b.x()) / 2 - 22, (a.y() + b.y()) / 2 - 7, 44, 14), Qt::AlignCenter,
                            QString::number ((int) std::lround (interval)) + QStringLiteral ("¢"));
            }
        // Each sounding note: a mark at its own angle, so one off its degree shows.
        for (auto c : places)
        {
            g.setPen (QPen (QColor (0xe0, 0x50, 0x50), 2.0));
            g.drawLine (at (c, radius - 8), at (c, radius + 6));
        }

        // The consonance meter.
        const auto r = roughness (now);
        const auto level = r / (r + 0.35);
        const QRectF meter (16, centre.y() + size / 2.0 + 18, width() - 32, 10);
        g.setPen (QPen (pal.color (QPalette::Mid), 1.0));
        g.setBrush (pal.color (QPalette::Base));
        g.drawRect (meter);
        g.setPen (Qt::NoPen);
        g.setBrush (QColor::fromHsvF ((1.0 - level) * 0.33, 0.7, 0.85));
        g.drawRect (QRectF (meter.left(), meter.top(), meter.width() * level, meter.height()));
        g.setPen (pal.color (QPalette::WindowText));
        g.drawText (QRectF (meter.left(), meter.bottom() + 2, meter.width(), 16), Qt::AlignLeft,
                    now.size() < 2 ? MidiEditor::tr ("Play or pick notes: how rough they sound")
                                   : MidiEditor::tr ("Roughness %1 (Sethares)").arg (r, 0, 'f', 2));
        g.drawText (QRectF (meter.left(), meter.bottom() + 18, meter.width(), 16), Qt::AlignLeft,
                    QString::fromStdString (ed.system.name) + QStringLiteral (" · ") + QString::fromStdString (letterName (ed.system.tonic, false)));
    }

private:
    MidiEditor& ed;
};

//==============================================================================
MidiEditor::MidiEditor (const midifile::Song& song, Host h, QWidget* parent)
    : QDialog (parent), host (std::move (h)), original (song)
{
    setWindowTitle (host.title.isEmpty() ? tr ("MIDI Editor") : tr ("MIDI Editor: %1").arg (host.title));
    if (host.embedded)
        setWindowFlags (Qt::Widget);
    else
    {
        setWindowFlag (Qt::WindowMaximizeButtonHint);
        setSizeGripEnabled (true);
        resize (1180, 760);
    }
    seq = decode (song);
    step.length = step.step = seq.ticksPerQuarter / 2;
    playTimer = new QTimer (this);
    playTimer->setTimerType (Qt::PreciseTimer);
    playTimer->setInterval (4);
    connect (playTimer, &QTimer::timeout, this, &MidiEditor::playTick);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (8, 8, 8, 8);
    layout->setSpacing (6);

    // The transport, over the roll and the staff alike: its own clock, not the main window's.
    using S = TransportButton::Symbol;
    auto* transport = new QHBoxLayout;
    transport->setSpacing (2);
    auto* startButton = new TransportButton (S::start, tr ("To the start (Home)"), this);
    playButton = new TransportButton (S::play, tr ("Play the notes as they will be saved from the playhead, or pause where it is (Space). "
                                                   "Click the ruler to move the playhead"), this);
    auto* stopButton = new TransportButton (S::stop, tr ("Stop, back to where Play was pressed; pressed again, to the start"), this);
    auto* endButton = new TransportButton (S::end, tr ("To the end (End)"), this);
    recordButton = new TransportButton (S::record, tr ("Record: plays from the playhead and puts in what the MIDI keyboard plays, "
                                                       "a take one undo; again to stop recording"), this);
    recordButton->setCheckable (true);
    loopButton = new TransportButton (S::loop, tr ("Loop the picked notes' span, or the whole clip when none is picked"), this);
    loopButton->setCheckable (true);
    for (auto* b : { startButton, playButton, stopButton, endButton, recordButton, loopButton })
        transport->addWidget (b);
    transport->addSpacing (10);
    timeLabel = new QLabel (this);
    timeLabel->setToolTip (tr ("The playhead: minutes and seconds, and bar.beat"));
    timeLabel->setMinimumWidth (timeLabel->fontMetrics().horizontalAdvance (QStringLiteral ("00:00.000   bar 000.0")));
    {
        auto f = timeLabel->font();
        f.setStyleHint (QFont::Monospace);
        f.setFamily (QStringLiteral ("monospace"));
        timeLabel->setFont (f);
    }
    transport->addWidget (timeLabel);
    abButton = new QToolButton (this);
    abButton->setText (tr ("Original"));
    abButton->setCheckable (true);
    abButton->setToolTip (tr ("Hear the clip as it was, to compare (switches while it plays)"));
    transport->addWidget (abButton);
    transport->addStretch (1);
    layout->addLayout (transport);
    connect (startButton, &QToolButton::clicked, this, [this] { goTo (0); });
    connect (endButton, &QToolButton::clicked, this, [this] { goTo (seq.end()); });
    connect (stopButton, &QToolButton::clicked, this, &MidiEditor::stopToStart);
    connect (recordButton, &QToolButton::toggled, this, &MidiEditor::setRecording);
    connect (loopButton, &QToolButton::toggled, this, [this] (bool on)
    {
        if (on && playTimer->isActive())
            loopRange (loopFrom, loopTo);
    });

    // The tools, the grid and the functions.
    auto* tools = new QHBoxLayout;
    auto* group = new QButtonGroup (this);
    const std::pair<QString, QString> toolNames[] { { tr ("Pick"), tr ("Pick, move (Alt: by the cent) and stretch notes; drag on empty space to pick several. Ctrl-click picks or "
                                                                                 "unpicks a note, Ctrl-drag sets the picked notes' velocity, Shift-click picks from the last note "
                                                                                 "clicked to this one; Ctrl-click on empty space draws (1)") },
                                                    { tr ("Draw"), tr ("Click to put a note, drag to its length, click a note to erase it; Shift-drag paints notes "
                                                                       "freehand along the mouse; Alt puts them between the degrees (2, or B to switch Draw on and off)") },
                                                    { tr ("Erase"), tr ("Click or drag over notes to delete them (3)") },
                                                    { tr ("Split"), tr ("Cut a note in two where you click (4)") },
                                                    { tr ("Glue"), tr ("Join a note to the next of its pitch (5)") },
                                                    { tr ("Curve"), tr ("Drag across a note to draw its pitch curve, a mīṇḍ or gamak, on the degrees (Alt: freely); right-click clears it (6)") } };
    for (int i = 0; i < 6; ++i)
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
    layout->addLayout (tools);

    // The pitch system.
    auto* systems = new QHBoxLayout;
    systems->addWidget (new QLabel (tr ("System"), this));
    systemBox = new QComboBox (this);
    systemBox->setMaxVisibleItems (30);
    systemBox->setToolTip (tr ("What the rows are: 12-TET, a mode, raga, maqam, dastgah, laras, mbira tuning, lü or tori, or a "
                               "Scala file. The notes keep their pitches; Functions > Snap or Restyle moves them into it"));
    systems->addWidget (systemBox, 2);
    // The atlas (ModeAtlas.h): every mode and tuning by region, tradition and people, and a search.
    auto* atlas = new QToolButton (this);
    atlas->setText (tr ("Atlas"));
    atlas->setToolTip (tr ("Find a mode or tuning by region, tradition and people, or search them all"));
    atlas->setPopupMode (QToolButton::InstantPopup);
    auto* atlasMenu = new QMenu (atlas);
    atlas->setMenu (atlasMenu);
    const auto pickSystem = [this] (int index)
    {
        if (const auto at = systemBox->findData (index); at >= 0)
        {
            systemBox->setCurrentIndex (at);
            systemChosen();
        }
    };
    connect (atlasMenu, &QMenu::aboutToShow, this, [this, atlasMenu, pickSystem]
    {
        atlasMenu->clear();
        atlasMenu->addAction (tr ("Search..."), this, [this, pickSystem]
        {
            if (const auto index = modeatlas::find (this, mixer::melodic | mixer::tuning, tr ("Find a Mode or Tuning")); index >= 0)
                pickSystem (index);
        });
        atlasMenu->addSeparator();
        modeatlas::fill (atlasMenu, mixer::melodic | mixer::tuning, systemBox->currentData().toInt(), pickSystem);
    });
    systems->addWidget (atlas);
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
    systems->addWidget (new QLabel (tr ("View"), this));
    viewBox = new QComboBox (this);
    viewBox->addItem (tr ("Piano roll"));
    viewBox->addItem (tr ("Rising spectrum"));
    viewBox->addItem (tr ("Staff notation"));
    viewBox->setToolTip (tr ("The rising spectrum: frequency across, time down, each note at its fundamental with its harmonics, "
                             "rolling up while it plays. Ctrl+wheel zooms across, Shift+wheel moves, Alt+wheel stretches time. "
                             "Staff notation: the notes written on a staff or in cipher, in the roll's place"));
    systems->addWidget (viewBox);
    // The staff notation, toggled with the roll in the same place (owner, 2026-10-29), and what it is written in.
    staffButton = new QToolButton (this);
    staffButton->setText (tr ("Staff"));
    staffButton->setCheckable (true);
    staffButton->setShortcut (QKeySequence (tr ("Ctrl+Shift+N")));
    staffButton->setToolTip (tr ("Toggle between the piano roll and the microtonal staff notation (Ctrl+Shift+N)"));
    systems->addWidget (staffButton);
    notationButton = new QToolButton (this);
    notationButton->setText (tr ("Notation"));
    notationButton->setPopupMode (QToolButton::InstantPopup);
    auto* notationMenu = new QMenu (notationButton);
    notationButton->setMenu (notationMenu);
    notationButton->setToolTip (tr ("What the notes are written in: PHI's microtonal staff (the maqams' half-flats, koron and sori, HEJI, Sagittal), "
                                    "each tradition's own (sargam, kepatihan, jianpu), any of Xen Tuner's notations, or a Xen Tuner config of your own"));
    connect (notationMenu, &QMenu::aboutToShow, this, [this, notationMenu] { fillNotationMenu (notationMenu); });
    systems->addWidget (notationButton);
    harmonicsBox = new QSpinBox (this);
    harmonicsBox->setRange (1, 32);
    harmonicsBox->setValue (8);
    harmonicsBox->setPrefix (tr ("harmonics "));
    harmonicsBox->setToolTip (tr ("How many of each note's harmonics the rising spectrum draws"));
    systems->addWidget (harmonicsBox);
    wheelBox = new QCheckBox (tr ("Wheel"), this);
    wheelBox->setToolTip (tr ("The scale wheel: each degree at its true angle against the twelve keys, how long each sounds, "
                              "and the notes sounding now joined with their intervals, with how rough they sound together"));
    systems->addWidget (wheelBox);
    ghostBox = new QCheckBox (tr ("Ghosts"), this);
    ghostBox->setChecked (true);
    ghostBox->setToolTip (tr ("The notes as they were before the last restyle, drawn behind"));
    systems->addWidget (ghostBox);
    layout->addLayout (systems);

    roll = new Roll (*this);
    if (host.embedded)
        setFocusProxy (roll);   // a panel given the keyboard (Alt+arrows, F6) gives it to the roll
    spectrum = new Spectrum (*this);
    spectrum->hide();
    staff = new Staff (*this);
    staff->hide();
    wheel = new Wheel (*this);
    wheel->hide();
    auto* views = new QHBoxLayout;
    views->setSpacing (6);
    views->addWidget (roll, 1);
    views->addWidget (spectrum, 1);
    views->addWidget (staff, 1);
    views->addWidget (wheel);
    layout->addLayout (views, 1);

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
    auto* hum = new QToolButton (this);
    hum->setText (tr ("Hum or Sing..."));
    hum->setToolTip (tr ("Sing, hum or play a melody into the audio input, or open a recording: its notes are put in at the step cursor, "
                         "on the system's degrees, its slides and shakes kept as curves"));
    connect (hum, &QToolButton::clicked, this, &MidiEditor::humDialog);
    lanes->addSpacing (10);
    lanes->addWidget (hum);
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
    buttons->setVisible (! host.embedded);
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
    const auto bySys = [this] (auto f) { return [this, f] { editBySystem (f); }; };
    menu->addAction (tr ("Up a Degree (Up)"), this, bySys ([] (Sequence& s, const PitchSystem& sys) { transposeSteps (s, sys, 1); }));
    menu->addAction (tr ("Down a Degree (Down)"), this, bySys ([] (Sequence& s, const PitchSystem& sys) { transposeSteps (s, sys, -1); }));
    menu->addAction (tr ("Up a Period (Shift+Up)"), this, bySys ([] (Sequence& s, const PitchSystem& sys) { transposeSteps (s, sys, sys.size()); }));
    menu->addAction (tr ("Down a Period (Shift+Down)"), this, bySys ([] (Sequence& s, const PitchSystem& sys) { transposeSteps (s, sys, -sys.size()); }));
    menu->addAction (tr ("Snap to the System's Degrees"), this, bySys ([] (Sequence& s, const PitchSystem& sys) { snapToSystem (s, sys); }));
    menu->addAction (tr ("Restyle into This System..."), this, &MidiEditor::restyleDialog);
    // Ornaments (owner, 2026-10-26: "pitch curves for meend, gamaka and glides, plus PHI rāga's
    // ornaments"), as curves on the picked notes, each in the system it is in.
    auto* ornaments = menu->addMenu (tr ("Ornaments"));
    const auto beat = [this] { return (int64_t) seq.ticksPerQuarter; };
    const auto bpm = [this] { return tempo(); };
    ornaments->addAction (tr ("mīṇḍ to the next note, through the degrees"), this, [this] { editBySystem ([] (Sequence& s, const PitchSystem& sys) { glideToNext (s, sys, 0.4, true); }); });
    ornaments->addAction (tr ("Glide to the next note"), this, [this] { editBySystem ([] (Sequence& s, const PitchSystem& sys) { glideToNext (s, sys, 0.5, false); }); });
    ornaments->addAction (tr ("Slide in from the degree below"), this, [this, beat] { editBySystem ([beat] (Sequence& s, const PitchSystem& sys) { slideIn (s, sys, -1, beat() / 4); }); });
    ornaments->addAction (tr ("Slide in from the degree above"), this, [this, beat] { editBySystem ([beat] (Sequence& s, const PitchSystem& sys) { slideIn (s, sys, 1, beat() / 4); }); });
    ornaments->addAction (tr ("kaṇ from the degree above"), this, [this, beat] { editBySystem ([beat] (Sequence& s, const PitchSystem& sys) { kan (s, sys, 1, beat() / 8); }); });
    ornaments->addAction (tr ("kaṇ from the degree below"), this, [this, beat] { editBySystem ([beat] (Sequence& s, const PitchSystem& sys) { kan (s, sys, -1, beat() / 8); }); });
    ornaments->addSeparator();
    ornaments->addAction (tr ("āndolan (a slow swing under the note)"), this, [this, bpm] { edit ([this, bpm] (Sequence& s) { swing (s, 30.0, 1.5, bpm(), true); }); });
    ornaments->addAction (tr ("gamak (a shake to the degree above)"), this, [this, bpm] { editBySystem ([bpm] (Sequence& s, const PitchSystem& sys) { gamak (s, sys, 5.0, bpm()); }); });
    ornaments->addAction (tr ("Vibrato"), this, [this, bpm] { edit ([this, bpm] (Sequence& s) { swing (s, 25.0, 5.5, bpm(), false); }); });
    ornaments->addSeparator();
    ornaments->addAction (tr ("murkī (a quick turn above and below the note)"), this, [this, beat] { editBySystem ([beat] (Sequence& s, const PitchSystem& sys) { turn (s, sys, { 0, 1, 0, -1, 0 }, beat() / 12); }); });
    ornaments->addAction (tr ("khaṭkā (from above, round the note)"), this, [this, beat] { editBySystem ([beat] (Sequence& s, const PitchSystem& sys) { turn (s, sys, { 1, 0, -1, 0 }, beat() / 12); }); });
    ornaments->addAction (tr ("zamzama (the note and the one above, quickly)"), this, [this, beat] { editBySystem ([beat] (Sequence& s, const PitchSystem& sys) { turn (s, sys, { 1, 0, 1, 0, -1, 0 }, beat() / 16); }); });
    ornaments->addSeparator();
    ornaments->addAction (tr ("Clear the Curves"), this, on ([] (Sequence& s) { clearCurves (s); }));
    // Tuning changes (owner, 2026-10-26: "tuning changes within a piece").
    auto* changes = menu->addMenu (tr ("Tuning Changes"));
    changes->addAction (tr ("Change the System at the Cursor..."), this, &MidiEditor::changeSystemHere);
    changes->addAction (tr ("Take Out the Change in Force at the Cursor"), this, &MidiEditor::removeSystemHere);
    changes->addAction (tr ("Take Out All the Changes"), this, on ([] (Sequence& s) { s.systems.clear(); }));
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
    connect (namingBox, &QComboBox::activated, this, [this] { roll->refresh(); viewsUpdate(); updateInfo(); });
    connect (rowsBox, &QComboBox::activated, this, [this] (int i) { roll->byPitch = i == 1; roll->rebuild(); roll->showNotes(); });
    connect (gridBox, &QComboBox::activated, this, [this] { roll->refresh(); });
    connect (lengthBox, &QComboBox::activated, this, [this] { step.length = step.step = noteLength(); });
    connect (ghostBox, &QCheckBox::toggled, this, [this] { roll->refresh(); viewsUpdate(); });
    connect (viewBox, &QComboBox::activated, this, &MidiEditor::showView);
    connect (staffButton, &QToolButton::clicked, this, [this] (bool on) { showView (on ? 2 : 0); });
    connect (harmonicsBox, &QSpinBox::valueChanged, this, [this] { viewsUpdate(); });
    connect (wheelBox, &QCheckBox::toggled, this, [this] (bool b) { wheel->setVisible (b); });
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
    // The system the clip was saved in, and its tuning changes, from its markers.
    if (! seq.systems.empty() && seq.systems.front().tick == 0)
    {
        system = seq.systems.front().system;
        seq.systems.erase (seq.systems.begin());
        selectSystemItem (system);
    }
    base = system;
    const auto e = suggestedEncoding (seq);
    encodingBox->setCurrentIndex (encodingBox->findData ((int) e));
    step.length = step.step = noteLength();
    roll->rebuild();
    changed();
    QTimer::singleShot (0, this, [this]
    {
        roll->showNotes();
        roll->setFocus();
        // A notation asked for by its id (the Compose menu's New Score In), else PHI's staff
        // when the staff is (Host::staff).
        if (! host.notation.isEmpty())
        {
            const auto id = host.notation.toStdString();
            notation::Choice c;
            if (notation::fromId (id, system, Staff::tuningsFolder().toStdString(), c))
            {
                staff->eachTradition = id == "tradition";
                staff->setNotation (c, id == "phi" || id == "tradition");
            }
            showView (2);
        }
        else if (host.staff)
            showView (2);
    });
    updateTransport();
}

void MidiEditor::showView (int i)
{
    roll->setVisible (i == 0);
    spectrum->setVisible (i == 1);
    staff->setVisible (i == 2);
    notationButton->setEnabled (i == 2);
    {
        const QSignalBlocker block (viewBox);
        viewBox->setCurrentIndex (i);
    }
    staffButton->setChecked (i == 2);
    if (i == 1)
        spectrum->showNotes();
    if (i == 2)
        staff->showNotes();
    (i == 0 ? (QWidget*) roll : i == 1 ? (QWidget*) spectrum : (QWidget*) staff)->setFocus();
}

void MidiEditor::fillNotationMenu (QMenu* menu)
{
    using notation::Choice;
    using notation::Kind;
    menu->clear();
    const auto current = staff->choiceFor (system);
    const auto pick = [this] (const Choice& c, bool automatic)
    {
        staff->setNotation (c, automatic);
        staff->showNotes();
    };
    const auto item = [&] (QMenu* m, const QString& text, const Choice& c)
    {
        auto* a = m->addAction (text, this, [pick, c] { pick (c, false); });
        a->setCheckable (true);
        a->setChecked (! staff->automatic() && current == c);
        return a;
    };
    const auto tunings = Staff::tuningsFolder().toStdString();
    const auto automatic = [&] (const QString& text, const Choice& c, bool eachTradition)
    {
        auto* a = menu->addAction (text.arg (QString::fromStdString (notation::title (c))), this, [this, pick, eachTradition]
        {
            staff->eachTradition = eachTradition;
            pick ({}, true);
        });
        a->setCheckable (true);
        a->setChecked (staff->automatic() && staff->eachTradition == eachTradition);
    };
    automatic (tr ("PHI Staff (%1)"), notation::suggested (system, tunings), false);
    automatic (tr ("Each Tradition's Own (%1)"), notation::traditional (system, tunings), true);
    auto* flats = menu->addAction (tr ("Flats Before Sharps"), this, [this] (bool on) { staff->flats = on; staff->refresh(); });
    flats->setCheckable (true);
    flats->setChecked (staff->flats);
    menu->addSeparator();

    const auto make = [] (Kind k, std::string config = "phi:12-TET", notation::CipherStyle style = notation::CipherStyle::tradition)
    {
        Choice c;
        c.kind = k;
        c.config = std::move (config);
        c.cipher = style;
        return c;
    };
    // The beat-by-beat notations (NotationGrid.h), each tradition's own way of writing a melody.
    item (menu, tr ("Hindustani Sargam (Bhatkhande)"), make (Kind::bhatkhande));
    item (menu, tr ("Carnatic Swaras"), make (Kind::carnatic));
    item (menu, tr ("Jianpu (Numbered)"), make (Kind::jianpu));
    item (menu, tr ("Kepatihan (Gamelan, Not Angka)"), make (Kind::angka));
    item (menu, tr ("Jeongganbo (Korean)"), make (Kind::jeongganbo));
    auto* cipher = menu->addMenu (tr ("Cipher Notation"));
    item (cipher, tr ("The Tradition's Names (Sargam, Kepatihan)"), make (Kind::cipher, {}, notation::CipherStyle::tradition));
    item (cipher, tr ("Jianpu Numbers"), make (Kind::cipher, {}, notation::CipherStyle::jianpu));
    item (cipher, tr ("Degree Numbers"), make (Kind::cipher, {}, notation::CipherStyle::degrees));
    menu->addSeparator();
    for (const auto& b : notation::builtins())
        item (menu, QString::fromStdString (b.title), make (Kind::staff, "phi:" + b.name));
    if (const auto n = notation::equalDivisions (system); n > 0 && n != 12)
        item (menu, tr ("%1-EDO, Ups and Downs").arg (n), make (Kind::staff, "edo:" + std::to_string (n)));

    // The svaras' script.
    auto* scripts = menu->addMenu (tr ("Script (Sargam and Swaras)"));
    for (const auto sc : { notation::Script::roman, notation::Script::devanagari, notation::Script::bengali, notation::Script::kannada,
                           notation::Script::tamil, notation::Script::telugu, notation::Script::malayalam })
    {
        auto* a = scripts->addAction (QString::fromLatin1 (notation::scriptName (sc)), this, [this, sc] { staff->script = sc; staff->refresh(); });
        a->setCheckable (true);
        a->setChecked (current.script == sc && (current.kind == Kind::bhatkhande || current.kind == Kind::carnatic));
    }

    // The cycle the clip counts in (a tāl, a tāḷam, a jangdan...), from the step cursor on.
    auto* cycles = menu->addMenu (tr ("Rhythmic Cycle"));
    const auto* inForce = noteedit::cycleAt (seq, step.position);
    const auto setCycle = [this] (const std::string& plain, int64_t beat)
    {
        edit ([&] (noteedit::Sequence& s)
        {
            const auto* was = noteedit::cycleAt (s, step.position);
            noteedit::Cycle c;
            c.tick = s.cycles.empty() ? 0 : step.position;
            c.rhythm = plain;
            c.beat = beat > 0 ? beat : was != nullptr ? was->beat : 0;
            if (plain.empty() && was != nullptr)
                c.tick = was->tick;
            noteedit::setCycle (s, c);
        });
        staff->refresh();
    };
    auto* none = cycles->addAction (tr ("None (Bars of 4/4)"), this, [setCycle] { setCycle ({}, 0); });
    none->setCheckable (true);
    none->setChecked (inForce == nullptr);
    auto* beats = cycles->addMenu (tr ("Beat"));
    const auto tpq = seq.ticksPerQuarter;
    for (const auto& [name, ticks] : std::vector<std::pair<QString, int64_t>> { { tr ("Quarter"), tpq }, { tr ("Eighth"), tpq / 2 },
                                                                                { tr ("Half"), 2 * tpq }, { tr ("Dotted Quarter"), tpq * 3 / 2 } })
    {
        auto* a = beats->addAction (name, this, [setCycle, inForce, ticks = ticks]
        {
            if (inForce != nullptr)
                setCycle (inForce->rhythm, ticks);
        });
        a->setCheckable (true);
        a->setEnabled (inForce != nullptr);
        a->setChecked (inForce != nullptr && (inForce->beat > 0 ? inForce->beat : tpq) == ticks);
    }
    cycles->addSeparator();
    std::map<std::string, QMenu*> groupMenus;
    for (const auto& r : rhythm::rhythms())
    {
        auto*& m = groupMenus[r.group];
        if (m == nullptr)
            m = cycles->addMenu (QString::fromStdString (r.group));
        auto* a = m->addAction (QString::fromStdString (r.name), this, [setCycle, plain = r.plain] { setCycle (plain, 0); });
        a->setCheckable (true);
        a->setChecked (inForce != nullptr && inForce->rhythm == r.plain);
    }

    // The staff's key signature: the system's own (when LilyPond has it), none, or any of LilyPond's.
    auto* keys = menu->addMenu (tr ("Key Signature (Maqam, Makam, Dastgah)"));
    auto* own = keys->addAction (tr ("The Mode's Own"), this, [this] { staff->key.reset(); staff->refresh(); });
    own->setCheckable (true);
    own->setChecked (! staff->key);
    auto* noKey = keys->addAction (tr ("None"), this, [this] { staff->key = std::string ("none"); staff->refresh(); });
    noKey->setCheckable (true);
    noKey->setChecked (staff->key && *staff->key == "none");
    keys->addSeparator();
    // On the system's tonic for LilyPond's Arabic and Persian modes (they name none); Turkish ones on their own.
    const auto tonicLetter = std::string (1, "cdefgab"[std::clamp ((int) std::lround (std::fmod (std::fmod (system.tonic, 1200.0) + 1200.0, 1200.0) / 1200.0 * 7.0 - 0.01), 0, 6)]);
    std::map<std::string, QMenu*> keyMenus;
    for (const auto& k : notation::keySignatures())
    {
        auto*& m = keyMenus[k.tradition];
        if (m == nullptr)
            m = keys->addMenu (k.tradition == "arabic" ? tr ("Arabic (LilyPond)") : k.tradition == "turkish" ? tr ("Turkish (LilyPond)") : tr ("Persian (LilyPond)"));
        const auto value = k.tradition + ":" + k.name + "@" + (k.tonic.empty() ? tonicLetter : k.tonic);
        auto* a = m->addAction (QString::fromStdString (k.name + (k.tonic.empty() ? "" : " (" + k.tonic + ")")), this, [this, value]
        {
            staff->key = value;
            staff->refresh();
        });
        a->setCheckable (true);
        a->setChecked (staff->key && *staff->key == value);
    }
    menu->addSeparator();

    // Xen Tuner's library, a submenu a folder, the configs in number order.
    auto* library = menu->addMenu (tr ("Xen Tuner"));
    const QDir root (Staff::tuningsFolder());
    for (const auto& folder : root.entryList (QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        auto* sub = library->addMenu (folder);
        QStringList files;
        QDirIterator it (root.filePath (folder), { QStringLiteral ("*.txt") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            files << root.relativeFilePath (it.next());
        std::sort (files.begin(), files.end(), [] (const QString& a, const QString& b)
        {
            static const QRegularExpression number (QStringLiteral ("(\\d+)"));
            const auto ma = number.match (QFileInfo (a).fileName()), mb = number.match (QFileInfo (b).fileName());
            if (ma.hasMatch() && mb.hasMatch() && ma.captured (1).toInt() != mb.captured (1).toInt())
                return ma.captured (1).toInt() < mb.captured (1).toInt();
            return a < b;
        });
        for (const auto& f : files)
            item (sub, QFileInfo (f).completeBaseName(), make (Kind::staff, f.toStdString()));
    }
    menu->addAction (tr ("Open a Xen Tuner Config (.txt)..."), this, [this, pick, make]
    {
        const auto file = QFileDialog::getOpenFileName (this, tr ("Open a Xen Tuner Tuning Config"), host.folder, tr ("Xen Tuner configs (*.txt)"));
        if (! file.isEmpty())
            pick (make (Kind::staff, QFileInfo (file).absoluteFilePath().toStdString()), false);
    });
}

MidiEditor::~MidiEditor()
{
    recording = false;
    stopPlay();
}

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
    if (choice <= -10 && -choice - 10 < (int) otherSystems.size())
    {
        setSystem (otherSystems[(size_t) (-choice - 10)]);
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
    // In a stretch after a tuning change, the change's system; else the clip's.
    const auto at = step.position;
    if (std::any_of (seq.systems.begin(), seq.systems.end(), [at] (const SystemChange& c) { return c.tick <= at; }))
        edit ([&] (Sequence& q) { changeSystem (q, std::max_element (q.systems.begin(), q.systems.end(), [at] (const SystemChange& a, const SystemChange& b)
                                                                                    { return (a.tick <= at ? a.tick : -1) < (b.tick <= at ? b.tick : -1); })->tick, s); });
    else
        base = s;
    system = std::move (s);
    roll->rebuild();
    roll->showNotes();
    if (spectrum->isVisible())
        spectrum->showNotes();
    changed();
}

const PitchSystem& MidiEditor::systemAt (int64_t tick) const { return noteedit::systemAt (seq, base, tick); }

void MidiEditor::showSystemAt (int64_t tick)
{
    const auto& s = systemAt (tick);
    if (sameSystem (s, system))
        return;
    system = s;
    selectSystemItem (system);
    roll->rebuild();
    if (spectrum->isVisible())
        spectrum->update();
    viewsUpdate();
    updateInfo();
}

void MidiEditor::selectSystemItem (const PitchSystem& s)
{
    const QSignalBlocker blockSystem (systemBox), blockRoot (rootBox);
    rootBox->setCurrentIndex (((int) std::lround (s.tonic / 100.0) % 12 + 12) % 12);
    // A tuning of PHI's on that root, 12-TET, the Scala scale open, else one read from the clip.
    const auto root = rootBox->currentIndex();
    if (sameSystem (s, twelveTet (root)))
    {
        systemBox->setCurrentIndex (systemBox->findData (-1));
        return;
    }
    const auto& all = mixer::tuningSystems();
    for (int t = 0; t < (int) all.size(); ++t)
        if (sameSystem (s, fromTuning (all[(size_t) t], root)))
        {
            systemBox->setCurrentIndex (systemBox->findData (t));
            return;
        }
    for (size_t i = 0; i < otherSystems.size(); ++i)
        if (sameSystem (s, otherSystems[i]))
        {
            systemBox->setCurrentIndex (systemBox->findData (-10 - (int) i));
            return;
        }
    otherSystems.push_back (s);
    systemBox->insertItem (1, tr ("%1 (from the clip)").arg (QString::fromStdString (s.name)), -10 - (int) (otherSystems.size() - 1));
    systemBox->setCurrentIndex (1);
}

void MidiEditor::changeSystemHere()
{
    if (step.position <= 0)
    {
        QMessageBox::information (this, tr ("Tuning Changes"), tr ("Put the step cursor where the new system starts (click the ruler), then change it there."));
        return;
    }
    edit ([this] (Sequence& s) { changeSystem (s, step.position, system); });
    // The system from here on is picked next.
    systemBox->setFocus();
    systemBox->showPopup();
}

void MidiEditor::removeSystemHere()
{
    edit ([this] (Sequence& s) { removeSystemChange (s, step.position); });
}

void MidiEditor::editBySystem (const std::function<void (Sequence&, const PitchSystem&)>& f)
{
    edit ([&] (Sequence& s) { bySystem (s, base, f); });
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
    const auto curved = std::any_of (seq.notes.begin(), seq.notes.end(), [] (const Note& n) { return ! n.curve.empty() && n.channel != 9; });
    if (encoding == Encoding::keys && curved)
        encodingNote->setText (tr ("The pitch curves are sent as pitch bend on each note's channel, over the track's tuning"));
    else if (encoding == Encoding::keys && suggestedEncoding (seq) != Encoding::keys)
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
    showSystemAt (step.position);
    roll->refresh();
    viewsUpdate();
    updateInfo();
}

void MidiEditor::viewsUpdate()
{
    if (spectrum->isVisible())
        spectrum->update();
    if (staff->isVisible())
        staff->update();
    if (wheel->isVisible())
        wheel->update();
    updateTransport();
}

int64_t MidiEditor::nowTick() const
{
    return playTimer->isActive() && playheadTick >= 0 ? playheadTick : step.position;
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
        const auto& sys = systemAt (n.start);
        const auto stepAt = sys.nearestStep (n.pitch);
        const auto off = n.pitch - sys.pitchOf (stepAt);
        auto text = picked.size() > 1 ? tr ("%1 notes picked, the first: ").arg (picked.size()) : QString();
        text += tr ("%1 · %2, degree %3 of %4 · %5 Hz")
                    .arg (barText (n.start, seq.ticksPerQuarter),
                          QString::fromStdString (label (sys, stepAt, naming)) + (std::abs (off) >= 0.5 ? QStringLiteral (" ") + QString::fromStdString (centsText (off)) : QString()))
                    .arg (sys.degreeOf (stepAt) + 1)
                    .arg (sys.size())
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
            {
                const auto& sys = systemAt (n.start);
                n.pitch = std::clamp (sys.pitchOf (sys.nearestStep (n.pitch)) + off, 0.0, 12700.0);
            }
        }
    });
    // Typing in the boxes one change after another is one undo step.
    if (merge)
        undoList.pop_back();
    infoMark = undoList.size();
}

double MidiEditor::tempo() const
{
    return host.bpm > 0.0 ? host.bpm : seq.tempos.empty() ? 120.0 : 60.0e6 / seq.tempos.front().microsPerQuarter;
}

void MidiEditor::putInHeard (const std::vector<Heard>& heard, const HumOptions& options)
{
    const auto at = step.position;
    auto notes = notesFromPitch (heard, systemAt (at), options, seq.ticksPerQuarter, tempo(), at);
    if (notes.empty())
        return;
    int64_t end = at;
    for (const auto& n : notes)
        end = std::max (end, n.end());
    edit ([&] (Sequence& s)
    {
        selectAll (s, false);
        for (auto& n : notes)
        {
            n.selected = true;
            s.notes.push_back (n);
        }
        s.sort();
        s.lastTick = std::max (s.lastTick, s.end());
    });
    // The next take goes on after this one.
    step.position = grid() > 0 ? (end + grid() - 1) / grid() * grid() : end;
    changed();
}

bool MidiEditor::putInAudio (const AudioData& audio, bool drums)
{
    if (! drums)
    {
        const auto before = seq.notes.size();
        putInHeard (listen (audio), HumOptions {});
        return seq.notes.size() > before;
    }
    mixer::Clip clip;
    clip.audio = mixer::makeAudioClip (audio, {});
    clip.length = clip.audio->length();
    const auto rate = audio.sampleRate();
    const auto hits = mixer::findDrums (clip, rate);
    if (hits.empty() || rate <= 0.0)
        return false;
    // At the editor's tempo from the step cursor, each hit a 16th long.
    const auto at = step.position;
    const auto perFrame = tempo() / 60.0 * seq.ticksPerQuarter / rate;
    std::vector<Note> notes;
    int64_t end = at;
    for (const auto& h : hits)
    {
        Note n;
        n.start = at + (int64_t) std::llround ((double) h.at * perFrame);
        n.length = std::max (1, seq.ticksPerQuarter / 4);
        n.pitch = h.note * 100.0;
        n.velocity = std::clamp (h.velocity, 1, 127);
        n.channel = 9;
        n.selected = true;
        end = std::max (end, n.end());
        notes.push_back (std::move (n));
    }
    edit ([&] (Sequence& s)
    {
        selectAll (s, false);
        for (auto& n : notes)
            s.notes.push_back (n);
        s.sort();
        s.lastTick = std::max (s.lastTick, s.end());
    });
    step.position = grid() > 0 ? (end + grid() - 1) / grid() * grid() : end;
    changed();
    return true;
}

void MidiEditor::humDialog()
{
    QDialog d (this);
    d.setWindowTitle (tr ("Hum or Sing to Notes"));
    auto* form = new QVBoxLayout (&d);
    auto* intro = new QLabel (tr ("Sing, hum, whistle or play a melody: its notes are put in from the step cursor (%1), in %2.")
                                  .arg (barText (step.position, seq.ticksPerQuarter), QString::fromStdString (systemAt (step.position).name)), &d);
    intro->setWordWrap (true);
    form->addWidget (intro);

    auto* sourceBox = new QComboBox (&d);
    auto* record = new QPushButton (tr ("Record"), &d);
    record->setCheckable (true);
    auto* fromFile = new QPushButton (tr ("From a Recording..."), &d);
    auto* heardLabel = new QLabel (tr ("Heard: nothing yet"), &d);
    auto* snap = new QCheckBox (tr ("On the system's degrees (else as sung, to the cent)"), &d);
    snap->setChecked (true);
    auto* curves = new QCheckBox (tr ("Keep the slides and shakes as pitch curves (mīṇḍ, gamak)"), &d);
    curves->setChecked (true);
    auto* quantise = new QCheckBox (tr ("Starts and lengths on the grid"), &d);
    auto* holdMs = new QSpinBox (&d);
    holdMs->setRange (20, 400);
    holdMs->setValue (60);
    holdMs->setSuffix (tr (" ms"));
    holdMs->setToolTip (tr ("How long the voice must stay on a new degree for a new note; shorter moves are ornaments of the note"));
    auto* playAlong = new QCheckBox (tr ("Play the clip from the cursor while recording"), &d);
    auto* status = new QLabel (&d);
    status->setWordWrap (true);

    if (! AudioCapture::isAvailable())
    {
        sourceBox->addItem (tr ("This build of PHI has no PipeWire, so no audio input"));
        sourceBox->setEnabled (false);
        record->setEnabled (false);
    }
    else
    {
        if (capture == nullptr)
            capture = std::make_unique<AudioCapture>();
        if (! capture->isConnected())
            if (const auto why = capture->connect(); ! why.empty())
                status->setText (tr ("The audio input could not be opened: %1").arg (QString::fromStdString (why)));
        for (const auto& source : capture->sources())
        {
            const auto kind = source.kind == CaptureSource::Kind::input ? tr ("Input") : source.kind == CaptureSource::Kind::desktop ? tr ("Desktop")
                                                                                                                                    : tr ("Application");
            sourceBox->addItem (tr ("%1: %2").arg (kind, QString::fromStdString (source.name)), QString::fromStdString (source.target));
            // The mixer's audio input, else the first input.
            if ((! host.inputSource.isEmpty() && QString::fromStdString (source.target) == host.inputSource)
                || (host.inputSource.isEmpty() && source.kind == CaptureSource::Kind::input && sourceBox->property ("picked").isNull()))
            {
                sourceBox->setCurrentIndex (sourceBox->count() - 1);
                sourceBox->setProperty ("picked", true);
            }
        }
        if (sourceBox->count() == 0)
        {
            sourceBox->addItem (tr ("No audio input"));
            record->setEnabled (false);
        }
    }
    fromFile->setEnabled ((bool) host.loadAudio);

    auto* row = new QHBoxLayout;
    row->addWidget (sourceBox, 1);
    row->addWidget (record);
    form->addLayout (row);
    form->addWidget (heardLabel);
    form->addWidget (fromFile, 0, Qt::AlignLeft);
    form->addSpacing (6);
    form->addWidget (snap);
    form->addWidget (curves);
    form->addWidget (quantise);
    auto* holdRow = new QHBoxLayout;
    holdRow->addWidget (new QLabel (tr ("A new note after"), &d));
    holdRow->addWidget (holdMs);
    holdRow->addWidget (new QLabel (tr ("on a degree"), &d));
    holdRow->addStretch (1);
    form->addLayout (holdRow);
    form->addWidget (playAlong);
    form->addWidget (status);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, &d);
    form->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);

    const auto options = [&]
    {
        HumOptions o;
        o.snap = snap->isChecked();
        o.curves = curves->isChecked();
        o.holdSeconds = holdMs->value() / 1000.0;
        o.grid = quantise->isChecked() ? grid() : 0;
        return o;
    };
    const auto putIn = [&] (const std::vector<Heard>& heard)
    {
        const auto before = seq.notes.size();
        putInHeard (heard, options());
        const auto added = (int) (seq.notes.size() - before);
        status->setText (added > 0 ? tr ("%n note(s) put in; Ctrl+Z takes them out. The cursor is now at %1, for the next take.", nullptr, added)
                                         .arg (barText (step.position, seq.ticksPerQuarter))
                                   : tr ("No notes were heard: a single voice or instrument, clearly, works best."));
        intro->setText (tr ("Sing, hum, whistle or play a melody: its notes are put in from the step cursor (%1), in %2.")
                            .arg (barText (step.position, seq.ticksPerQuarter), QString::fromStdString (systemAt (step.position).name)));
    };

    // Recording: what comes in is kept, and its pitch shown as it is sung.
    std::vector<float> take;
    int rate = 48000;
    QTimer timer;
    timer.setInterval (50);
    connect (&timer, &QTimer::timeout, &d, [&]
    {
        std::vector<float> block (4096);
        for (int n; (n = capture->read (block.data(), (int) block.size())) > 0;)
            take.insert (take.end(), block.begin(), block.begin() + n);
        if (take.size() < 2048)
            return;
        AudioData::Builder b (1, rate);
        const auto from = take.size() - std::min<size_t> (take.size(), (size_t) rate / 5);
        const float* ch[] { take.data() + from };
        b.append (ch, (int) (take.size() - from));
        const auto now = listen (*b.finish());
        for (auto it = now.rbegin(); it != now.rend(); ++it)
            if (it->pitch >= 0.0)
            {
                const auto& sys = systemAt (step.position);
                const auto st = sys.nearestStep (it->pitch);
                const auto off = it->pitch - sys.pitchOf (st);
                heardLabel->setText (tr ("Heard: %1 %2 · %3 Hz").arg (QString::fromStdString (label (sys, st, (Naming) namingBox->currentData().toInt())),
                                                                      QString::fromStdString (centsText (off)))
                                         .arg (hzOf (it->pitch), 0, 'f', 1));
                return;
            }
        heardLabel->setText (tr ("Heard: no clear pitch"));
    });
    const auto stopRecording = [&]
    {
        timer.stop();
        if (capture != nullptr && capture->isRunning())
        {
            std::vector<float> block (4096);
            for (int n; (n = capture->read (block.data(), (int) block.size())) > 0;)
                take.insert (take.end(), block.begin(), block.begin() + n);
            capture->stop();
        }
        if (playTimer->isActive())
            stopPlay();
        record->setText (tr ("Record"));
        if (take.empty())
            return;
        AudioData::Builder b (1, rate);
        const float* ch[] { take.data() };
        b.append (ch, (int) take.size());
        take.clear();
        putIn (listen (*b.finish()));
    };
    connect (record, &QPushButton::toggled, &d, [&] (bool on)
    {
        if (! on)
        {
            stopRecording();
            return;
        }
        const auto target = sourceBox->currentData().toString().toStdString();
        CaptureSource source;
        for (const auto& s2 : capture->sources())
            if (s2.target == target)
                source = s2;
        take.clear();
        rate = 48000;
        if (const auto why = capture->start (source, rate, 1); ! why.empty())
        {
            status->setText (tr ("The audio input could not start: %1").arg (QString::fromStdString (why)));
            const QSignalBlocker block (record);
            record->setChecked (false);
            return;
        }
        rate = capture->sampleRate();
        if (playAlong->isChecked())
        {
            playheadTick = step.position;
            startPlay (false);
        }
        record->setText (tr ("Stop"));
        status->setText (tr ("Recording: sing, then Stop."));
        timer.start();
    });
    connect (fromFile, &QPushButton::clicked, &d, [&]
    {
        const auto file = QFileDialog::getOpenFileName (&d, tr ("A Recording of a Melody"), host.folder,
                                                        tr ("Sound files (*.wav *.flac *.ogg *.mp3 *.aif *.aiff *.m4a *.opus);;All files (*)"));
        if (file.isEmpty())
            return;
        QString why;
        QApplication::setOverrideCursor (Qt::WaitCursor);
        const auto audio = host.loadAudio (file, why);
        const auto heard = audio != nullptr ? listen (*audio) : std::vector<Heard> {};
        QApplication::restoreOverrideCursor();
        if (audio == nullptr)
        {
            status->setText (tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), why));
            return;
        }
        putIn (heard);
    });
    d.exec();
    stopRecording();
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
    // Into the system each stretch of the clip is in, when its tuning changes.
    editBySystem ([&] (Sequence& s, const PitchSystem& to) { restyle (s, source, to, byDegree); });
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
    if (recording && playTimer->isActive())
    {
        // A take: each key from where it went down to where it came up, at the playhead.
        const auto at = std::max<int64_t> (0, playheadTick);
        if (kind == 0x90 && data2 > 0)
            held[data1] = { at, data2, pitchForKey (data1) };
        else if ((kind == 0x80 || kind == 0x90) && held.count (data1) != 0)
        {
            const auto h = held[data1];
            held.erase (data1);
            Note n;
            n.start = h.start;
            n.length = std::max<int64_t> (1, at - h.start);
            n.pitch = h.pitch;
            n.velocity = h.velocity;
            seq.notes.push_back (n);
            seq.sort();
            changed();
        }
        return;
    }
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

bool MidiEditor::event (QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride)
    {
        // In the main window the editor's keys would otherwise go to the window's own shortcuts
        // (Ctrl+A and Ctrl+D picking and duplicating audio, Space playing it...): the keys
        // keyPressEvent handles are taken here first.
        const auto* k = static_cast<QKeyEvent*> (e);
        const auto mods = k->modifiers() & ~Qt::KeypadModifier;
        const auto key = k->key();
        const auto in = [key] (std::initializer_list<int> keys) { return std::find (keys.begin(), keys.end(), key) != keys.end(); };
        const auto arrow = in ({ Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right });
        bool ours = false;
        if (mods == Qt::NoModifier)
            ours = arrow || in ({ Qt::Key_Home, Qt::Key_End, Qt::Key_Delete, Qt::Key_Backspace, Qt::Key_Space, Qt::Key_Q, Qt::Key_B, Qt::Key_Return,
                                  Qt::Key_Enter, Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5, Qt::Key_6 });
        else if (mods == Qt::ShiftModifier)
            ours = arrow;
        else if (mods == Qt::ControlModifier)
            ours = in ({ Qt::Key_Z, Qt::Key_Y, Qt::Key_A, Qt::Key_D, Qt::Key_C, Qt::Key_X, Qt::Key_V, Qt::Key_U, Qt::Key_J, Qt::Key_E, Qt::Key_L,
                         Qt::Key_Up, Qt::Key_Down, Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_Delete, Qt::Key_Backspace });
        else if (mods == (Qt::ControlModifier | Qt::ShiftModifier))
            ours = in ({ Qt::Key_Z, Qt::Key_A, Qt::Key_D, Qt::Key_Delete, Qt::Key_Backspace, Qt::Key_Up, Qt::Key_Down });
        // Step input: the computer's keys are notes.
        if (stepBox->isChecked() && (mods & ~Qt::ShiftModifier) == Qt::NoModifier && k->text().size() == 1 && k->text()[0].isPrint())
            ours = true;
        if (ours)
        {
            e->accept();
            return true;
        }
    }
    return QDialog::event (e);
}

void MidiEditor::goTo (int64_t tick, bool reveal)
{
    const bool playing = playTimer->isActive();
    if (playing)
        stopPlay();
    step.position = std::max<int64_t> (0, tick);
    playheadTick = step.position;
    showSystemAt (step.position);
    if (reveal)
        roll->reveal (step.position);
    if (playing)
        startPlay (playingOriginal);   // on from there
    viewsUpdate();
}

void MidiEditor::keyPressEvent (QKeyEvent* e)
{
    const auto mods = e->modifiers();
    if ((mods & ~Qt::KeypadModifier) == Qt::NoModifier && (e->key() == Qt::Key_Home || e->key() == Qt::Key_End))
    {
        goTo (e->key() == Qt::Key_Home ? 0 : seq.end());
        return;
    }
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
    const auto snapGrid = [this] { return snapBox->isChecked() ? grid() : (int64_t) 0; };
    const auto setTool = [this] (int t)
    {
        toolButtons[(size_t) t]->setChecked (true);
        roll->tool = t;
    };
    // Live's MIDI editor keys, where they leave PHI's own alone.
    if (ctrl && ! alt)
        switch (e->key())
        {
            case Qt::Key_A:
                if (shift)
                    invertSelection (seq);
                else
                    selectAll (seq, true);
                changed();
                return;
            case Qt::Key_D:
                edit ([&] (Sequence& s) { duplicate (s, snapGrid(), shift); });
                return;
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
                if (shift)
                {
                    edit ([&] (Sequence& s) { deleteAndClose (s, snapGrid()); });
                    return;
                }
                break;
            case Qt::Key_C:
            case Qt::Key_X:
            {
                if (! seq.anySelected())
                    return;
                noteClipboard.clear();
                for (const auto& n : seq.notes)
                    if (n.selected)
                        noteClipboard.push_back (n);
                const auto first = noteClipboard.front().start;   // the notes are in start order
                for (auto& n : noteClipboard)
                    n.start -= first;
                if (e->key() == Qt::Key_X)
                    edit ([] (Sequence& s) { deleteTargets (s); });
                return;
            }
            case Qt::Key_V:
            {
                if (noteClipboard.empty())
                    return;
                // At the playhead (the step cursor), the pasted notes picked and the cursor after them.
                const auto at = step.position;
                int64_t end = at;
                edit ([&] (Sequence& s)
                {
                    selectAll (s, false);
                    for (auto n : noteClipboard)
                    {
                        n.start += at;
                        n.selected = true;
                        end = std::max (end, n.end());
                        s.notes.push_back (n);
                    }
                    s.sort();
                    s.lastTick = std::max (s.lastTick, s.end());
                });
                goTo (end);
                return;
            }
            case Qt::Key_U:
                edit ([this] (Sequence& s) { quantizeStarts (s, grid(), strengthBox->value() / 100.0, swingBox->value() / 100.0); });
                return;
            case Qt::Key_J:
                edit ([] (Sequence& s) { glue (s); });
                return;
            case Qt::Key_E:
                edit ([this] (Sequence& s) { chop (s, grid()); });
                return;
            case Qt::Key_L:
                loopButton->toggle();
                return;
            case Qt::Key_Up:
            case Qt::Key_Down:
                // The picked notes' velocity (the staff takes Ctrl+Up and Down for the next written pitch first).
                if (seq.anySelected())
                {
                    const auto by = (e->key() == Qt::Key_Up ? 1 : -1) * (shift ? 1 : 5);
                    edit ([by] (Sequence& s) { scaleVelocity (s, 1.0, by); });
                }
                return;
            case Qt::Key_1:
            case Qt::Key_2:
            {
                // The grid narrower (Ctrl+1) or wider (Ctrl+2), straight or triplet as it is.
                const auto at = gridBox->currentIndex();
                const auto triplet = at >= straightGrids;
                const auto first = triplet ? straightGrids : 0, last = triplet ? gridBox->count() - 1 : straightGrids - 1;
                gridBox->setCurrentIndex (std::clamp (at + (e->key() == Qt::Key_1 ? 1 : -1), first, last));
                return;
            }
            case Qt::Key_3:
            {
                // Triplets on and off: 1/4 to 1/4 triplet, 1/8 to 1/8 triplet, 1/16 to 1/16 triplet.
                const auto at = gridBox->currentIndex();
                gridBox->setCurrentIndex (at >= straightGrids ? at - straightGrids + 2 : straightGrids + std::clamp (at - 2, 0, 2));
                return;
            }
            case Qt::Key_4:
                snapBox->toggle();
                return;
            default: break;
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
            showSystemAt (step.position);
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
                editBySystem ([dir, shift] (Sequence& s, const PitchSystem& sys) { transposeSteps (s, sys, dir * (shift ? sys.size() : 1)); });
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
                if (shift)   // shorter or longer, as in Live
                    edit ([by, alt, this] (Sequence& s) { lengthen (s, by, alt ? 1 : grid()); });
                else
                    edit ([by] (Sequence& s) { nudge (s, by); });
            }
            return;
        case Qt::Key_Space:
            togglePlay();
            return;
        case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4: case Qt::Key_5: case Qt::Key_6:
            if (! ctrl)
            {
                setTool (e->key() - Qt::Key_1);
                return;
            }
            break;
        case Qt::Key_B:
            // Live's Draw mode key: Draw on, or back to Pick.
            if (! ctrl && ! alt)
            {
                setTool (roll->tool == toolDraw ? toolPick : toolDraw);
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
        stopPlay();   // paused: the playhead stays where it is
    else
        startPlay (abButton->isChecked());
}

bool MidiEditor::loopRange (int64_t& from, int64_t& to) const
{
    from = 0;
    to = seq.end();
    if (seq.anySelected())
    {
        from = std::numeric_limits<int64_t>::max();
        to = 0;
        for (const auto& n : seq.notes)
            if (n.selected)
            {
                from = std::min (from, n.start);
                to = std::max (to, n.end());
            }
    }
    return to > from;
}

void MidiEditor::startPlay (bool originalVersion)
{
    if (! host.play)
        return;
    const bool going = playTimer->isActive() && playSong != nullptr;
    if (! going)
    {
        // A loop starts inside its span: from the playhead when it is there, else the span's start.
        if (loopButton->isChecked() && loopRange (loopFrom, loopTo)
            && (playheadTick < loopFrom || playheadTick >= loopTo))
            playheadTick = loopFrom;
        pressedAt = std::max<int64_t> (0, playheadTick);
    }
    // Going on from where it is (the A/B switch), else from the playhead.
    double from = 0.0;
    if (going)
        from = playFrom + (QDateTime::currentMSecsSinceEpoch() - playStarted) / 1000.0;
    else if (playheadTick > 0)
        from = (originalVersion ? original : *encode (seq, encodeOptions()).song).seconds (playheadTick, host.bpm);
    const bool keepRecording = recording;
    recording = false;   // not closed by the restart
    stopPlay();
    recording = keepRecording;
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
    playTimer->start();
    updateTransport();
}

void MidiEditor::stopPlay()
{
    if (playTimer == nullptr || ! playTimer->isActive())
        return;
    playTimer->stop();
    if (recording)
        closeHeld();
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
    roll->refresh();
    viewsUpdate();
    updateTransport();
}

void MidiEditor::stopToStart()
{
    const bool wasPlaying = playTimer->isActive();
    recordButton->setChecked (false);
    stopPlay();
    playheadTick = wasPlaying ? pressedAt : 0;
    roll->refresh();
    viewsUpdate();
    updateTransport();
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
        while (recording && hi < (int64_t (1) << 40) && playSong->seconds (hi, host.bpm) < now)
            hi *= 2;   // a take goes on past the clip's end
        while (lo < hi)
        {
            const auto mid = (lo + hi) / 2;
            if (playSong->seconds (mid, host.bpm) < now)
                lo = mid + 1;
            else
                hi = mid;
        }
        playheadTick = lo;
        viewsUpdate();
        if (loopButton->isChecked() && loopTo > loopFrom && playheadTick >= loopTo)
        {
            // Round again: the notes held in a take end at the loop's end and go on as new ones.
            auto still = held;
            if (recording)
            {
                playheadTick = loopTo;
                closeHeld();
            }
            stopPlay();
            playheadTick = loopFrom;
            startPlay (playingOriginal);
            pressedAt = loopFrom;
            if (recording)
                for (auto& [key, h] : still)
                    held[key] = { loopFrom, h.velocity, h.pitch };
            return;
        }
        if (playAt >= playing.size() && now > playSong->lengthSeconds (host.bpm) + 0.3 && ! recording)
        {
            playheadTick = 0;
            stopPlay();
            return;
        }
    }
    roll->viewport()->update();
}

void MidiEditor::updateTransport()
{
    if (timeLabel == nullptr || encodingBox == nullptr)
        return;
    const bool going = playTimer->isActive();
    playButton->setSymbol (going ? TransportButton::Symbol::pause : TransportButton::Symbol::play);
    const auto tick = std::max<int64_t> (0, playheadTick);
    double seconds = 0.0;
    if (going)
        seconds = playFrom + (QDateTime::currentMSecsSinceEpoch() - playStarted) / 1000.0;
    else
    {
        // On the clip's tempo map, as the song it plays is timed.
        midifile::Song timing;
        timing.ticksPerQuarter = seq.ticksPerQuarter;
        timing.tempos = seq.tempos;
        seconds = timing.seconds (tick, host.bpm);
    }
    const auto ms = (int64_t) std::llround (std::max (0.0, seconds) * 1000.0);
    const auto clock = QStringLiteral ("%1:%2.%3").arg (ms / 60000, 2, 10, QLatin1Char ('0'))
                                                   .arg (ms / 1000 % 60, 2, 10, QLatin1Char ('0'))
                                                   .arg (ms % 1000, 3, 10, QLatin1Char ('0'));
    const auto quarters = tick / std::max (1, seq.ticksPerQuarter);
    // Bars of 4/4, unless the clip counts in cycles of its own: then its beats.
    const auto where = seq.cycles.empty() ? tr ("bar %1.%2").arg (quarters / 4 + 1).arg (quarters % 4 + 1)
                                          : tr ("beat %1").arg (quarters + 1);
    timeLabel->setText (clock + QStringLiteral ("   ") + where);
}

void MidiEditor::setRecording (bool on)
{
    if (on == recording)
        return;
    if (on)
    {
        // The take is one undo: the notes as they were before it.
        undoList.push_back (seq);
        redoList.clear();
        if (undoList.size() > 200)
            undoList.erase (undoList.begin());
        held.clear();
        recording = true;
        if (! playTimer->isActive())
            startPlay (false);
        if (! playTimer->isActive())   // nothing to play it on
        {
            recording = false;
            undoList.pop_back();
            const QSignalBlocker block (recordButton);
            recordButton->setChecked (false);
        }
        return;
    }
    closeHeld();
    recording = false;
    // Nothing played: no empty step on the undo list.
    if (! undoList.empty() && undoList.back().notes.size() == seq.notes.size())
        undoList.pop_back();
}

void MidiEditor::closeHeld()
{
    if (held.empty())
        return;
    const auto at = std::max<int64_t> (0, playheadTick);
    for (const auto& [key, h] : held)
    {
        Note n;
        n.start = h.start;
        n.length = std::max<int64_t> (1, at - h.start);
        n.pitch = h.pitch;
        n.velocity = h.velocity;
        seq.notes.push_back (n);
    }
    held.clear();
    seq.sort();
    changed();
}

void MidiEditor::done (int r)
{
    stopPlay();
    if (host.embedded)
        return;   // a panel stays where it is
    QDialog::done (r);
}

midifile::SongPtr MidiEditor::result() const
{
    auto s = seq;
    for (auto& n : s.notes)
        n.selected = false;
    // The clip's own system, kept with it as a marker at the start, before its changes.
    if (s.systems.empty() || s.systems.front().tick > 0)
        s.systems.insert (s.systems.begin(), { 0, base });
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
