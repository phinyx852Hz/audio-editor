// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiStaff.h"

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>

using namespace noteedit;

namespace
{
    constexpr int rulerHeight = 22, clefWidth = 58;

    // SMuFL's code points (Bravura): the clefs and the black notehead.
    constexpr char32_t gClef = 0xE050, fClef = 0xE062, noteheadBlack = 0xE0A4;
    // The staves' lines: the treble's E4 to F5, the bass's G2 to A3 (positions, C0 0).
    constexpr int trebleLines[] { 30, 32, 34, 36, 38 }, bassLines[] { 18, 20, 22, 24, 26 };

    QString glyph (char32_t c) { return QString::fromUcs4 (&c, 1); }

    int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

    // A note's length line, in the roll's colours (the drums' their own); its velocity in how strong.
    QColor lengthColour (const Note& n, const QPalette& pal)
    {
        auto c = n.selected ? pal.color (QPalette::Highlight) : n.channel == 9 ? QColor (0xc8, 0x8a, 0x3a) : QColor (0x4f, 0x9d, 0xd8);
        c.setAlphaF (0.35 + 0.5 * n.velocity / 127.0);
        return c;
    }
    const QColor curveColour (0xe0, 0x8a, 0x2a);   // a pitch curve, as the roll draws it

    QString letterOf (const phi::xen::Config& c, int staff)
    {
        // Seven nominals are the letters on the staff; other counts are numbered from the reference.
        if (c.nominals.size() == 7)
            return QString (QChar ("CDEFGAB"[((staff % 7) + 7) % 7])) + QString::number (floorDiv (staff, 7));
        const auto n = (int) c.nominals.size();
        const auto k = staff - c.referenceStaff();
        return QCoreApplication::translate ("MidiStaff", "nominal %1, equave %2").arg (((k % n) + n) % n + 1).arg (floorDiv (k, n));
    }
}

MidiEditor::Staff::Staff (MidiEditor& e) : QWidget (&e), ed (e)
{
    setFocusPolicy (Qt::StrongFocus);
    setMouseTracking (true);
    setMinimumHeight (220);
    setAttribute (Qt::WA_OpaquePaintEvent);
    scroll = new QScrollBar (Qt::Horizontal, this);
    connect (scroll, &QScrollBar::valueChanged, this, [this] { if (! following) update(); });

    static const QString family = []
    {
        const auto id = QFontDatabase::addApplicationFont (QStringLiteral (":/fonts/Bravura.otf"));
        const auto families = id >= 0 ? QFontDatabase::applicationFontFamilies (id) : QStringList();
        return families.isEmpty() ? QString() : families.front();
    }();
    musicFamily = family;
    static const QString jeonggan = []
    {
        const auto id = QFontDatabase::addApplicationFont (QStringLiteral (":/fonts/TteoiJeonggan.ttf"));
        const auto families = id >= 0 ? QFontDatabase::applicationFontFamilies (id) : QStringList();
        return families.isEmpty() ? QString() : families.front();
    }();
    jeongganFamily = jeonggan;
    lines = new QScrollBar (Qt::Vertical, this);
    lines->hide();
    connect (lines, &QScrollBar::valueChanged, this, [this] { if (! following) update(); });
    setToolTip (tr ("The staff: click picks a note, double-click writes one on that line or space, Ctrl+Up/Down moves the picked notes "
                    "to the next pitch the notation writes, Ctrl+wheel zooms"));
}

QString MidiEditor::Staff::tuningsFolder()
{
    // Looked for once: the staff asks for it at each note of each frame.
    static const QString folder = []
    {
        const auto installed = QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHI_SHARE_RELATIVE "/xen-tuner/tunings"));
        if (QFileInfo (installed).isDir())
            return QDir::cleanPath (installed);
        return QStringLiteral (PHI_SHARE_SOURCE_DIR "/xen-tuner/tunings");
    }();
    return folder;
}

void MidiEditor::Staff::setNotation (const notation::Choice& c, bool automatic)
{
    choice = c;
    isAutomatic = automatic;
    loadError.clear();
    update();
}

notation::Choice MidiEditor::Staff::choiceFor (const PitchSystem& s) const
{
    const auto tunings = tuningsFolder().toStdString();
    auto c = ! isAutomatic ? choice : eachTradition ? notation::traditional (s, tunings) : notation::suggested (s, tunings);
    if (script && (c.kind == notation::Kind::bhatkhande || c.kind == notation::Kind::carnatic))
        c.script = *script;
    if (key && c.kind == notation::Kind::staff)
        c.key = *key;
    return c;
}

QString MidiEditor::Staff::titleFor (const PitchSystem& s) const
{
    return QString::fromStdString (notation::title (choiceFor (s)));
}

const phi::xen::Speller* MidiEditor::Staff::spellerFor (const PitchSystem& s) const
{
    if (! painting)
        return makeSpeller (s);
    auto [it, fresh] = paintSpellers.emplace (&s, nullptr);
    if (fresh)
        it->second = makeSpeller (s);
    return it->second;
}

const phi::xen::Spelling& MidiEditor::Staff::spelled (const phi::xen::Speller& sp, double pitch) const
{
    if (spellings.size() > 8192)
        spellings.clear();
    auto [it, fresh] = spellings.try_emplace ({ &sp, pitch, flats });
    if (fresh)
        it->second = sp.best (pitch, flats);
    return it->second;
}

const phi::xen::Speller* MidiEditor::Staff::makeSpeller (const PitchSystem& s) const
{
    const auto c = choiceFor (s);
    if (c.kind != notation::Kind::staff)
        return nullptr;
    // Anchored on the system's tonic, so it sits on its letter (Sa on C, C in 31-EDO).
    const auto key = c.config + "@" + std::to_string (std::lround (s.tonic * 1000.0));
    auto& slot = spellers[key];
    if (slot == nullptr)
    {
        phi::xen::Config conf;
        std::string error;
        if (! notation::load (c, tuningsFolder().toStdString(), conf, error))
        {
            loadError = QString::fromStdString (error);
            notation::load (notation::Choice {}, {}, conf, error);
        }
        slot = std::make_unique<phi::xen::Speller> (phi::xen::Speller (std::move (conf)).anchoredAt (s.tonic));
    }
    return slot.get();
}

void MidiEditor::Staff::refresh() { update(); }

void MidiEditor::Staff::showNotes()
{
    layoutScroll();
    lines->setValue (0);
    if (! ed.seq.notes.empty())
        scroll->setValue ((int) std::max (0.0, ed.seq.notes.front().start * pixelsPerTick - 40.0));
    update();
}

void MidiEditor::Staff::layoutScroll()
{
    const auto h = scroll->sizeHint().height();
    scroll->setGeometry (0, height() - h, width(), h);
    const auto v = lines->sizeHint().width();
    lines->setGeometry (width() - v, rulerHeight, v, height() - rulerHeight - h);
    const auto length = (double) std::max<int64_t> (ed.seq.end(), 4 * ed.seq.ticksPerQuarter) + 4 * ed.seq.ticksPerQuarter;
    scroll->setRange (0, std::max (0, (int) (length * pixelsPerTick) - (width() - clefWidth)));
    scroll->setPageStep (std::max (1, width() - clefWidth));
    scroll->setSingleStep (20);
}

void MidiEditor::Staff::resizeEvent (QResizeEvent*) { layoutScroll(); }

double MidiEditor::Staff::xOf (int64_t tick) const { return clefWidth + keyWidth + 12.0 + tick * pixelsPerTick - scroll->value(); }

int64_t MidiEditor::Staff::tickAt (double x) const { return std::max<int64_t> (0, (int64_t) std::llround ((x - clefWidth - keyWidth - 12.0 + scroll->value()) / pixelsPerTick)); }

double MidiEditor::Staff::yOf (double staff) const { return rulerHeight + 16.0 + (highest - staff) * half; }

double MidiEditor::Staff::staffAt (double y) const { return highest - (y - rulerHeight - 16.0) / half; }

void MidiEditor::Staff::fit()
{
    // The grand staff, and room for the notes' ledger lines above and below.
    int low = 14, high = 42;
    for (const auto& n : ed.seq.notes)
    {
        if (n.channel == 9)
            continue;
        if (const auto* sp = spellerFor (ed.systemAt (n.start)))
        {
            const auto s = spelled (*sp, n.pitch).staff;
            low = std::min (low, s - 3);
            high = std::max (high, s + 3);
        }
    }
    const auto room = height() - rulerHeight - 16.0 - scroll->height() - 18.0;
    half = std::clamp (room / std::max (1, high - low), 3.0, 10.0);
    // Centred in the room there is.
    highest = high + std::max (0.0, (room / half - (high - low)) / 2.0);
}

void MidiEditor::Staff::playheadMoved()
{
    if (! ed.playTimer->isActive() || ed.playheadTick < 0)
    {
        update();
        return;
    }
    following = true;
    const auto scrolled = followPlayhead();
    following = false;
    // A frame at most every 15 ms (the play clock ticks every 4); the next tick draws what this skips.
    if (! sinceFrame.isValid() || sinceFrame.elapsed() >= 15 || (scrolled && sinceFrame.elapsed() >= 8))
        update();
}

bool MidiEditor::Staff::followPlayhead()
{
    const auto tick = ed.playheadTick;
    if (writingShown && grid.shown)
    {
        const auto index = (int64_t) std::floor ((tick - grid.start) / (double) grid.beat);
        const auto at = (int64_t) std::floor (index / (double) grid.per) - grid.first;
        if (grid.columns)
        {
            // Read right to left: the playhead's column at the right edge once it would leave the view.
            if (scroll->isSliderDown())
                return false;
            const auto x = width() - 20.0 - (at + 1) * grid.step + scroll->value() + 14.0;
            if (x >= 0.0 && x + grid.step <= width())
                return false;
            const auto before = scroll->value();
            scroll->setValue ((int) std::lround (std::max<int64_t> (0, at) * grid.step));
            return scroll->value() != before;
        }
        // Down the lines: the playhead's line second from the top once it reaches the bottom.
        if (lines->isSliderDown())
            return false;
        const auto y = grid.top + at * grid.step - lines->value();
        if (y >= rulerHeight && y + grid.step <= height())
            return false;
        const auto before = lines->value();
        lines->setValue ((int) std::lround (std::max<int64_t> (0, at - 1) * grid.step));
        return lines->value() != before;
    }
    // Across the staff (and the cipher): it moves on to a third of the way, then the page slides
    // under it and it stays there; out of view (a jump, a loop round), the page goes to it.
    if (scroll->isSliderDown())
        return false;
    const auto left = clefWidth + keyWidth + 12.0;
    const auto hold = (width() - left) / 3.0;
    const auto x = xOf (tick);
    if (x >= left && x <= left + hold)
        return false;
    const auto before = scroll->value();
    scroll->setValue ((int) std::lround (std::max (0.0, tick * pixelsPerTick - hold)));
    return scroll->value() != before;
}

void MidiEditor::Staff::paintEvent (QPaintEvent*)
{
    sinceFrame.start();
    painting = true;
    paintSpellers.clear();
    struct Done { const Staff& s; ~Done() { s.painting = false; s.paintSpellers.clear(); } } done { *this };
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto pal = palette();
    const auto w = width();
    g.fillRect (rect(), pal.color (QPalette::Base));

    // The beat-by-beat notations take the whole view, the clip from its start (in the system it
    // starts in: each note is still named in its own).
    {
        const auto c = choiceFor (ed.systemAt (0));
        writingShown = c.kind != notation::Kind::staff && c.kind != notation::Kind::cipher;
        hits.clear();
        if (writingShown)
        {
            keyWidth = 0.0;
            const auto written = writing (c);
            if (written.layout == notation::Writing::columns)
                paintColumns (g, written, w);
            else
                paintLines (g, c, written, w);
            auto small = font();
            small.setPointSizeF (std::max (6.5, font().pointSizeF() * 0.8));
            g.setFont (small);
            g.setPen (pal.color (QPalette::PlaceholderText));
            const auto bottom = height() - (scroll->isVisible() ? scroll->height() : 0);
            g.drawText (QRectF (8, bottom - 18, w - 16 - (lines->isVisible() ? lines->width() : 0), 16), Qt::AlignRight | Qt::AlignVCenter,
                        QString::fromStdString (notation::title (c)));
            return;
        }
        lines->hide();
        scroll->show();
        // Room after the clefs for the key signature.
        int altered = 0;
        if (c.kind == notation::Kind::staff)
            for (const auto a : notation::keyFor (choiceFor (ed.systemAt (tickAt (clefWidth))), ed.systemAt (tickAt (clefWidth))))
                altered += std::abs (a) >= 1.0 ? 1 : 0;
        keyWidth = altered > 0 ? 8.0 + 11.0 * altered : 0.0;
    }

    // The ruler: bars and the clip's markers (sections, a rāga-mālā's parts).
    const auto tpq = ed.seq.ticksPerQuarter;
    const auto bar = (int64_t) 4 * tpq;
    g.fillRect (QRect (0, 0, w, rulerHeight), pal.color (QPalette::Window));
    auto small = font();
    small.setPointSizeF (std::max (6.5, font().pointSizeF() * 0.8));
    g.setFont (small);
    const auto first = tickAt (clefWidth), last = tickAt (w);
    for (int64_t t = first / bar * bar; t <= last; t += bar)
    {
        const auto x = xOf (t);
        if (x < clefWidth)
            continue;
        g.setPen (pal.color (QPalette::Text));
        g.drawText (QPointF (x + 3, rulerHeight - 7), QString::number (t / bar + 1));
        g.setPen (QPen (pal.color (QPalette::Mid), 1.0));
        g.drawLine (QPointF (x, rulerHeight - 6), QPointF (x, rulerHeight));
    }
    for (const auto& m : ed.seq.markers)
    {
        const auto x = xOf (m.tick);
        if (x < clefWidth || x > w)
            continue;
        g.setPen (QColor (0xe0, 0x8a, 0x2a));
        g.drawText (QPointF (x + 3, 10), QString::fromStdString (m.text));
    }

    const auto& sys = ed.systemAt (std::max<int64_t> (0, first));
    if (choiceFor (sys).kind == notation::Kind::cipher)
        paintCipher (g, w);
    else
        paintStaff (g, w);

    // The step cursor and the playhead.
    const auto bottom = height() - scroll->height();
    if (ed.stepBox->isChecked())
    {
        const auto x = xOf (ed.step.position);
        g.setPen (QPen (QColor (0xe0, 0x50, 0x50), 2.0));
        g.drawLine (QPointF (x, rulerHeight), QPointF (x, bottom));
    }
    if (ed.playheadTick >= 0)
    {
        const auto x = xOf (ed.playheadTick);
        if (x >= clefWidth)
        {
            g.setPen (QPen (pal.color (QPalette::Text), 1.5));
            g.drawLine (QPointF (x, 0), QPointF (x, bottom));
        }
    }

    // The corner: what it is written in.
    g.setFont (small);
    auto corner = titleFor (sys);
    if (! loadError.isEmpty())
        corner += QStringLiteral (" (") + loadError + QStringLiteral (")");
    g.setPen (pal.color (QPalette::PlaceholderText));
    g.drawText (QRectF (clefWidth + 6, bottom - 18, w - clefWidth - 12, 16), Qt::AlignRight | Qt::AlignVCenter, corner);
}

void MidiEditor::Staff::paintStaff (QPainter& g, int w)
{
    fit();
    const auto pal = palette();
    const auto text = pal.color (QPalette::Text);
    const auto space = 2.0 * half;
    const auto music = ! musicFamily.isEmpty();
    QFont mf (music ? musicFamily : font().family());
    mf.setPixelSize (std::max (8, (int) std::lround (4.0 * space)));   // SMuFL's em is four spaces
    QFont accidentalText = font();
    accidentalText.setPixelSize (std::max (8, (int) std::lround (1.6 * space)));
    accidentalText.setBold (true);
    QFont small = font();
    small.setPointSizeF (std::max (6.5, font().pointSizeF() * 0.75));
    const QFontMetricsF mm (mf), tm (accidentalText);
    const auto headWidth = music ? mm.horizontalAdvance (glyph (noteheadBlack)) : 1.3 * space;

    // The staves' lines, bars and the beats.
    g.setPen (QPen (text, 1.0));
    for (const auto l : trebleLines)
        g.drawLine (QPointF (0, yOf (l)), QPointF (w, yOf (l)));
    for (const auto l : bassLines)
        g.drawLine (QPointF (0, yOf (l)), QPointF (w, yOf (l)));
    const auto tpq = ed.seq.ticksPerQuarter;
    const auto first = tickAt (clefWidth), last = tickAt (w);
    for (int64_t t = first / (4 * tpq) * (4 * tpq); t <= last; t += 4 * tpq)
    {
        const auto x = xOf (t) - 0.6 * space;
        if (x > clefWidth)
            g.drawLine (QPointF (x, yOf (38)), QPointF (x, yOf (18)));
    }
    auto beat = pal.color (QPalette::Mid);
    beat.setAlphaF (0.35);
    g.setPen (QPen (beat, 0.6, Qt::DotLine));
    for (int64_t t = first / tpq * tpq; t <= last; t += tpq)
        if (t % (4 * tpq) != 0 && xOf (t) > clefWidth)
            g.drawLine (QPointF (xOf (t), yOf (38)), QPointF (xOf (t), yOf (18)));

    // The notes, chord by chord: accidentals stepped left where their notes are close.
    const auto& notes = ed.seq.notes;
    std::vector<size_t> order (notes.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return notes[a].start < notes[b].start; });
    int64_t chordTick = -1;
    std::vector<std::pair<int, double>> placed;   // this chord's (staff, accidentals' left edge)
    // Each system's key signature: the accidentals it puts on each letter, which its notes then
    // leave out (and a natural where a note leaves one off).
    std::map<const PitchSystem*, std::array<std::vector<phi::xen::Symbol>, 7>> keys;
    const auto natural = phi::xen::symbolCode ("n");
    for (const auto i : order)
    {
        const auto& n = notes[i];
        const auto x = xOf (n.start), xEnd = xOf (n.end());
        if (xEnd < clefWidth - 40 || x > w + 40)
            continue;
        const auto& sys = ed.systemAt (n.start);
        const auto* sp = spellerFor (sys);
        if (sp == nullptr)
            continue;
        const auto spelled = this->spelled (*sp, n.pitch);
        const auto y = yOf (spelled.staff);
        const auto colour = n.selected ? pal.color (QPalette::Highlight) : text;
        if (n.start != chordTick)
        {
            chordTick = n.start;
            placed.clear();
        }

        // Ledger lines.
        g.setPen (QPen (text, 1.0));
        const auto ledger = [&] (int l) { g.drawLine (QPointF (x - 0.4 * space, yOf (l)), QPointF (x + headWidth + 0.4 * space, yOf (l))); };
        for (int l = 40; l <= spelled.staff; l += 2)
            ledger (l);
        for (int l = 16; l >= spelled.staff; l -= 2)
            ledger (l);
        if (spelled.staff == 28)
            ledger (28);   // middle C, between the staves

        // Its length, and its pitch curve along the staff (a cent a staff step's share).
        const auto centsPerStep = std::fabs (sp->config().equave) / std::max<size_t> (1, sp->config().nominals.size());
        g.setPen (QPen (n.curve.empty() || n.selected ? lengthColour (n, pal) : curveColour, std::max (1.2, 0.22 * space), Qt::SolidLine, Qt::RoundCap));
        if (n.curve.empty())
            g.drawLine (QPointF (x + headWidth, y), QPointF (std::max (x + headWidth, xEnd), y));
        else
        {
            QPainterPath path;
            const auto every = std::max<int64_t> (1, (int64_t) (2.0 / pixelsPerTick));
            for (int64_t t = 0; t <= n.length; t = t == n.length ? t + 1 : std::min (n.length, t + every))
            {
                const QPointF at (std::max (x + headWidth, xOf (n.start + t)), y - (spelled.off + n.bendAt (t)) / centsPerStep * half);
                t == 0 ? path.moveTo (at) : path.lineTo (at);
            }
            g.setBrush (Qt::NoBrush);
            g.drawPath (path);
        }

        // The head.
        g.setPen (colour);
        g.setBrush (colour);
        if (music)
        {
            g.setFont (mf);
            g.drawText (QPointF (x, y), glyph (noteheadBlack));
        }
        else
            g.drawEllipse (QRectF (x, y - half, headWidth, 2 * half));

        // The accidentals, right to left before the head; a column further left for a close note.
        double right = x - 0.25 * space;
        for (const auto& [staff, left] : placed)
            if (std::abs (staff - spelled.staff) < 6)
                right = std::min (right, left - 0.2 * space);
        g.setPen (n.selected ? pal.color (QPalette::Highlight) : text);
        auto shown = spelled.symbols;
        if (sp->config().nominals.size() == 7)
        {
            auto [k, fresh] = keys.emplace (&sys, std::array<std::vector<phi::xen::Symbol>, 7> {});
            if (fresh)
                k->second = keySymbols (*sp, notation::keyFor (choiceFor (sys), sys));
            const auto& inKey = k->second[(size_t) ((spelled.staff % 7) + 7) % 7];
            if (! inKey.empty())
            {
                if (shown == inKey)
                    shown.clear();
                else if (shown.empty() && natural > 0)
                    shown.push_back ({ natural, {} });
            }
        }
        for (auto it = shown.rbegin(); it != shown.rend(); ++it)
        {
            const auto point = phi::xen::codePoint (it->code);
            if (it->code > 0 && point != 0 && music)
            {
                const auto s = glyph (point);
                right -= mm.horizontalAdvance (s);
                g.setFont (mf);
                g.drawText (QPointF (right, y), s);
            }
            else
            {
                const auto s = QString::fromStdString (it->code > 0 ? phi::xen::textCode (it->code) : it->text);
                right -= tm.horizontalAdvance (s) + 1.0;
                g.setFont (accidentalText);
                g.drawText (QPointF (right, y + tm.ascent() * 0.35), s);
            }
        }
        placed.push_back ({ spelled.staff, right });

        // What it is off the spelling, in cents, above (Xen Tuner's "display cents").
        if (std::abs (spelled.off) >= 0.5)
        {
            g.setFont (small);
            g.setPen (pal.color (QPalette::PlaceholderText));
            g.drawText (QPointF (x - 2, y - 1.6 * space), QString::fromStdString (centsText (spelled.off)));
        }
    }

    // The clefs and the key signature, over the notes that have scrolled under them.
    g.fillRect (QRectF (0, rulerHeight, clefWidth + keyWidth, height() - rulerHeight), pal.color (QPalette::Base));
    g.setPen (QPen (text, 1.0));
    for (const auto l : trebleLines)
        g.drawLine (QPointF (0, yOf (l)), QPointF (clefWidth + keyWidth, yOf (l)));
    for (const auto l : bassLines)
        g.drawLine (QPointF (0, yOf (l)), QPointF (clefWidth + keyWidth, yOf (l)));
    if (keyWidth > 0.0)
    {
        const auto& sys = ed.systemAt (tickAt (clefWidth + keyWidth));
        if (const auto* sp = spellerFor (sys))
            paintKey (g, *sp, choiceFor (sys), notation::keyFor (choiceFor (sys), sys));
    }
    g.drawLine (QPointF (4, yOf (38)), QPointF (4, yOf (18)));
    if (music)
    {
        g.setFont (mf);
        g.setPen (text);
        g.drawText (QPointF (10, yOf (32)), glyph (gClef));
        g.drawText (QPointF (10, yOf (24)), glyph (fClef));
    }
    else
    {
        g.setFont (small);
        g.drawText (QPointF (10, yOf (32)), tr ("treble"));
        g.drawText (QPointF (10, yOf (24)), tr ("bass"));
    }
}

void MidiEditor::Staff::paintCipher (QPainter& g, int w)
{
    // A line of names a part, as sargam, kepatihan and jianpu are written: the chords' notes
    // stacked, a dot above for each period up and below for each down, a line under for the length.
    const auto pal = palette();
    const auto text = pal.color (QPalette::Text);
    QFont big = font();
    big.setPixelSize (18);
    QFont small = font();
    small.setPointSizeF (std::max (6.5, font().pointSizeF() * 0.75));
    const QFontMetricsF fm (big);
    const auto rowHeight = 46.0;
    const auto baseline = rulerHeight + 40.0;
    const auto tpq = ed.seq.ticksPerQuarter;
    const auto first = tickAt (clefWidth), last = tickAt (w);
    g.setPen (QPen (pal.color (QPalette::Mid), 1.0));
    for (int64_t t = first / (4 * tpq) * (4 * tpq); t <= last; t += 4 * tpq)
        if (xOf (t) > clefWidth)
            g.drawLine (QPointF (xOf (t) - 4, rulerHeight + 4), QPointF (xOf (t) - 4, height() - scroll->height() - 20));

    std::map<int64_t, int> stacked;   // the notes already written at a tick
    for (const auto i : topDown())
    {
        const auto& n = ed.seq.notes[i];
        const auto x = xOf (n.start), xEnd = xOf (n.end());
        if (xEnd < clefWidth - 40 || x > w + 40)
            continue;
        const auto& sys = ed.systemAt (n.start);
        const auto c = notation::cipher (sys, n.pitch, choiceFor (sys).cipher);
        const auto row = stacked[n.start]++;
        const auto y = baseline + row * rowHeight;
        const auto colour = lengthColour (n, pal);
        const auto name = QString::fromStdString (c.name);
        const auto width = fm.horizontalAdvance (name);
        g.setFont (big);
        g.setPen (n.selected ? colour : text);
        g.drawText (QPointF (x, y), name);
        g.setBrush (n.selected ? colour : text);
        g.setPen (Qt::NoPen);
        for (int d = 0; d < std::abs (c.period); ++d)
        {
            const auto dy = c.period > 0 ? y - fm.ascent() - 3.0 - d * 5.0 : y + 5.0 + d * 5.0;
            g.drawEllipse (QPointF (x + width / 2.0, dy), 1.8, 1.8);
        }
        g.setPen (QPen (colour, 2.0, Qt::SolidLine, Qt::RoundCap));
        const auto under = y + 9.0 + std::max (0, -c.period) * 5.0;
        g.drawLine (QPointF (x, under), QPointF (std::max (x + width, xEnd - 3.0), under));
        if (! c.off.empty())
        {
            g.setFont (small);
            g.setPen (pal.color (QPalette::PlaceholderText));
            g.drawText (QPointF (x + width + 2, y - fm.ascent() + 6), QString::fromStdString (c.off));
        }
    }
    g.fillRect (QRectF (0, rulerHeight, clefWidth, height() - rulerHeight), pal.color (QPalette::Base));
    g.setFont (small);
    g.setPen (text);
    const auto& sys = ed.systemAt (std::max<int64_t> (0, first));
    g.drawText (QRectF (6, rulerHeight + 8, clefWidth - 8, 60), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, QString::fromStdString (sys.name));
}

std::vector<size_t> MidiEditor::Staff::topDown() const
{
    std::vector<size_t> order (ed.seq.notes.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort (order.begin(), order.end(), [this] (size_t a, size_t b)
    {
        const auto& x = ed.seq.notes[a];
        const auto& y = ed.seq.notes[b];
        return x.start != y.start ? x.start < y.start : x.pitch > y.pitch;
    });
    return order;
}

int MidiEditor::Staff::noteAt (QPointF p) const
{
    if (writingShown)
    {
        for (const auto& [r, note] : hits)
            if (r.contains (p))
                return note;
        return -1;
    }
    const auto t = tickAt (p.x());
    const auto& sys = ed.systemAt (t);
    const auto cipherView = choiceFor (sys).kind == notation::Kind::cipher;
    int best = -1;
    double nearest = 1e9;
    std::map<int64_t, int> stacked;
    for (const auto k : topDown())
    {
        const auto i = (int) k;
        const auto& n = ed.seq.notes[k];
        const auto x = xOf (n.start), xEnd = std::max (x + 12.0, xOf (n.end()));
        double y = 0.0;
        if (cipherView)
            y = rulerHeight + 40.0 + stacked[n.start]++ * 46.0 - 6.0;
        else if (const auto* sp = spellerFor (ed.systemAt (n.start)))
            y = yOf (sp->best (n.pitch, flats).staff);
        if (p.x() < x - 4 || p.x() > xEnd + 2)
            continue;
        const auto d = std::abs (p.y() - y);
        if (d < (cipherView ? 14.0 : 1.4 * half) && d < nearest)
        {
            nearest = d;
            best = i;
        }
    }
    return best;
}

QString MidiEditor::Staff::describe (const Note& n) const
{
    const auto& sys = ed.systemAt (n.start);
    const auto* sp = spellerFor (sys);
    if (sp == nullptr)
    {
        const auto c = notation::cipher (sys, n.pitch, choiceFor (sys).cipher);
        return QString::fromStdString (c.name + (c.off.empty() ? "" : " " + c.off));
    }
    const auto all = sp->spell (n.pitch, flats);
    if (all.empty())
        return {};
    const auto name = [&] (const phi::xen::Spelling& s)
    {
        const auto acc = QString::fromStdString (phi::xen::asText (s.symbols));
        return letterOf (sp->config(), s.staff) + (acc.isEmpty() ? QString() : QStringLiteral (" ") + acc);
    };
    auto text = name (all.front());
    if (std::abs (all.front().off) >= 0.5)
        text += QStringLiteral (" ") + QString::fromStdString (centsText (all.front().off));
    QStringList others;
    for (size_t k = 1; k < all.size() && k < 4; ++k)
        others << name (all[k]);
    if (! others.isEmpty())
        text += tr (" (also %1)").arg (others.join (QStringLiteral (", ")));
    return text;
}

void MidiEditor::Staff::mousePressEvent (QMouseEvent* e)
{
    setFocus();
    const auto hit = noteAt (e->position());
    if (hit < 0)
    {
        if (! (e->modifiers() & Qt::ShiftModifier))
            selectAll (ed.seq, false);
        if (writingShown)
        {
            ed.changed();
            return;
        }
        auto t = tickAt (e->position().x());
        if (ed.snapBox->isChecked())
            t = t / ed.grid() * ed.grid();
        ed.step.position = t;
        ed.showSystemAt (t);
        ed.changed();
        return;
    }
    auto& n = ed.seq.notes[(size_t) hit];
    if (e->modifiers() & Qt::ShiftModifier)
        n.selected = ! n.selected;
    else
    {
        selectAll (ed.seq, false);
        n.selected = true;
    }
    if (n.selected)
        ed.audition (n.pitch, n.velocity);
    ed.changed();
}

void MidiEditor::Staff::mouseDoubleClickEvent (QMouseEvent* e)
{
    if (writingShown)
        return;   // written in the roll or on the staff; picked and played here
    if (noteAt (e->position()) >= 0)
        return;
    auto t = tickAt (e->position().x());
    if (ed.snapBox->isChecked())
        t = t / ed.grid() * ed.grid();
    const auto& sys = ed.systemAt (t);
    const auto* sp = spellerFor (sys);
    if (sp == nullptr)
        return;
    // The line or space clicked, its nominal without accidentals.
    const auto& c = sp->config();
    const auto staff = (int) std::lround (staffAt (e->position().y()));
    const auto count = (int) c.nominals.size();
    const auto k = staff - c.referenceStaff();
    const auto nominal = ((k % count) + count) % count;
    if (! c.nominals[(size_t) nominal])
        return;
    const auto pitch = std::clamp (c.referencePitch() + *c.nominals[(size_t) nominal] + floorDiv (k, count) * c.equave, 0.0, 12700.0);
    const auto length = ed.noteLength();
    ed.edit ([&] (Sequence& s)
    {
        selectAll (s, false);
        Note n;
        n.start = t;
        n.length = length;
        n.pitch = pitch;
        n.velocity = ed.step.velocity;
        n.selected = true;
        s.notes.push_back (n);
        s.sort();
    });
    ed.audition (pitch, ed.step.velocity);
}

void MidiEditor::Staff::mouseMoveEvent (QMouseEvent* e)
{
    const auto hit = noteAt (e->position());
    if (hit >= 0)
        QToolTip::showText (e->globalPosition().toPoint(), describe (ed.seq.notes[(size_t) hit]), this);
    else
        QToolTip::hideText();
}

void MidiEditor::Staff::wheelEvent (QWheelEvent* e)
{
    const auto dy = e->angleDelta().y();
    if (writingShown)
    {
        auto* bar = lines->isVisible() ? lines : scroll;
        bar->setValue (bar->value() - (dy != 0 ? dy : e->angleDelta().x()));
        update();
        return;
    }
    if (e->modifiers() & Qt::ControlModifier)
    {
        const auto at = tickAt (e->position().x());
        pixelsPerTick = std::clamp (pixelsPerTick * (dy > 0 ? 1.2 : 1.0 / 1.2), 0.01, 2.0);
        layoutScroll();
        scroll->setValue ((int) (at * pixelsPerTick - (e->position().x() - clefWidth - 12.0)));
    }
    else
        scroll->setValue (scroll->value() - (dy != 0 ? dy : e->angleDelta().x()));
    update();
}

void MidiEditor::Staff::keyPressEvent (QKeyEvent* e)
{
    if ((e->modifiers() & Qt::ControlModifier) && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && ed.seq.anySelected())
    {
        // Xen Tuner's up and down: each to the next pitch its notation writes.
        const auto dir = e->key() == Qt::Key_Up ? 1 : -1;
        ed.edit ([&] (Sequence& s)
        {
            for (auto& n : s.notes)
                if (n.selected)
                    if (const auto* sp = spellerFor (ed.systemAt (n.start)))
                        n.pitch = std::clamp (sp->next (n.pitch, dir), 0.0, 12700.0);
        });
        for (const auto& n : ed.seq.notes)
            if (n.selected)
            {
                ed.audition (n.pitch, n.velocity);
                break;
            }
        return;
    }
    e->ignore();   // the editor's keys
}

//==============================================================================
// Key signatures (LilyPond's, notation::keySignatures()).
std::array<std::vector<phi::xen::Symbol>, 7> MidiEditor::Staff::keySymbols (const phi::xen::Speller& sp, const std::array<double, 7>& key) const
{
    // Each altered letter's natural (on the staff an octave above middle C's) moved by the key's
    // cents, spelled on that letter.
    std::array<std::vector<phi::xen::Symbol>, 7> out;
    const auto& c = sp.config();
    if (c.nominals.size() != 7)
        return out;
    for (int letter = 0; letter < 7; ++letter)
    {
        if (std::abs (key[(size_t) letter]) < 1.0)
            continue;
        const auto staff = 28 + letter;
        const auto k = staff - c.referenceStaff();
        const auto& nominal = c.nominals[(size_t) (((k % 7) + 7) % 7)];
        if (! nominal)
            continue;
        const auto pitch = c.referencePitch() + *nominal + floorDiv (k, 7) * c.equave + key[(size_t) letter];
        double best = 1e9;
        for (const auto& spelling : sp.spell (pitch, key[(size_t) letter] < 0.0))
            if (spelling.staff == staff && std::abs (spelling.off) < best && ! spelling.symbols.empty())
            {
                best = std::abs (spelling.off);
                out[(size_t) letter] = spelling.symbols;
            }
    }
    return out;
}

void MidiEditor::Staff::paintKey (QPainter& g, const phi::xen::Speller& sp, const notation::Choice& c, const std::array<double, 7>& key)
{
    // Flats on B4 E5 A4 D5 G4 C5 F4, sharps on F5 C5 G5 D5 A4 E5 B4, in the tradition's order
    // (LilyPond's keyAlterationOrder), the bass staff two octaves down.
    static const int flatAt[7] { 35, 36, 37, 31, 32, 33, 34 }, sharpAt[7] { 35, 36, 37, 38, 39, 33, 34 };
    const auto symbols = keySymbols (sp, key);
    std::string tradition = c.config == "phi:turkish aeu" ? "turkish" : c.config == "phi:persian koron and sori" ? "persian" : "arabic";
    std::vector<int> letters;
    for (const auto& [letter, cents] : notation::keyOrder (tradition))
        if (letter >= 0 && letter < 7 && std::abs (key[(size_t) letter]) >= 1.0 && (cents < 0.0) == (key[(size_t) letter] < 0.0)
            && std::find (letters.begin(), letters.end(), letter) == letters.end())
            letters.push_back (letter);
    for (int letter = 0; letter < 7; ++letter)
        if (std::abs (key[(size_t) letter]) >= 1.0 && std::find (letters.begin(), letters.end(), letter) == letters.end())
            letters.push_back (letter);

    const auto space = 2.0 * half;
    const auto music = ! musicFamily.isEmpty();
    QFont mf (music ? musicFamily : font().family());
    mf.setPixelSize (std::max (8, (int) std::lround (4.0 * space)));
    QFont tf = font();
    tf.setPixelSize (std::max (8, (int) std::lround (1.4 * space)));
    g.setPen (palette().color (QPalette::Text));
    double x = clefWidth + 2.0;
    const auto step = std::max (6.0, (keyWidth - 8.0) / std::max<size_t> (1, letters.size()));
    for (const auto letter : letters)
    {
        const auto flat = key[(size_t) letter] < 0.0;
        const auto at = flat ? flatAt[letter] : sharpAt[letter];
        for (const auto staff : { at, at - 14 })
        {
            double dx = x;
            for (const auto& sym : symbols[(size_t) letter])
            {
                const auto point = phi::xen::codePoint (sym.code);
                if (sym.code > 0 && point != 0 && music)
                {
                    g.setFont (mf);
                    g.drawText (QPointF (dx, yOf (staff)), glyph (point));
                    dx += QFontMetricsF (mf).horizontalAdvance (glyph (point));
                }
                else
                {
                    g.setFont (tf);
                    const auto t = QString::fromStdString (sym.code > 0 ? phi::xen::textCode (sym.code) : sym.text);
                    g.drawText (QPointF (dx, yOf (staff) + 4), t);
                    dx += QFontMetricsF (tf).horizontalAdvance (t);
                }
            }
        }
        x += step;
    }
}

//==============================================================================
// The beat-by-beat notations (mixer/NotationGrid.h).
notation::Writing MidiEditor::Staff::writing (const notation::Choice& c) const
{
    const auto& s = ed.seq;
    const auto to = s.end() + 1;
    switch (c.kind)
    {
        case notation::Kind::bhatkhande: return notation::bhatkhande (s, ed.system, c.script, 0, to);
        case notation::Kind::carnatic: return notation::carnatic (s, ed.system, c.script, 0, to);
        case notation::Kind::jianpu: return notation::jianpu (s, ed.system, false, 0, to);
        case notation::Kind::angka: return notation::jianpu (s, ed.system, true, 0, to);
        case notation::Kind::jeongganbo: return notation::jeongganbo (s, ed.system, 0, to);
        case notation::Kind::staff: case notation::Kind::cipher: break;
    }
    return {};
}

void MidiEditor::Staff::paintLines (QPainter& g, const notation::Choice& c, const notation::Writing& wr, int w)
{
    using notation::Sign;
    const auto pal = palette();
    const auto text = pal.color (QPalette::Text);
    const auto faint = pal.color (QPalette::PlaceholderText);
    const auto jianpuLike = c.kind == notation::Kind::jianpu || c.kind == notation::Kind::angka;
    const auto carnatic = c.kind == notation::Kind::carnatic;
    QFont big = font();
    big.setPixelSize (20);
    QFont small = font();
    small.setPixelSize (11);
    QFont tiny = font();
    tiny.setPixelSize (9);
    const QFontMetricsF bm (big), sm (small);

    scroll->hide();
    // A beat's width: the widest beat's signs, so the beats stand in columns as the books print them.
    double beatWidth = 36.0;
    bool anyLyric = false;
    for (const auto& b : wr.beats)
    {
        double width = 12.0;
        for (const auto& sg : b.signs)
        {
            width += bm.horizontalAdvance (QString::fromStdString (sg.text)) + sm.horizontalAdvance (QString::fromStdString (sg.before)) + 6.0 + sg.dots * 5.0;
            anyLyric = anyLyric || ! sg.lyric.empty();
        }
        beatWidth = std::max (beatWidth, std::min (width, 180.0));
    }
    const auto left = 18.0, top = (double) rulerHeight + 8.0;
    const auto room = w - left - 24.0 - lines->sizeHint().width();
    // A cycle a line, or as many whole cycles as make eight beats or more, or what fits.
    auto perLine = std::max (1, wr.beatsPerLine);
    while (perLine < 8 && (perLine * 2) * beatWidth <= room)
        perLine *= 2;
    if (perLine * beatWidth > room)
        perLine = std::max (1, (int) (room / beatWidth));
    const auto lineHeight = anyLyric ? 104.0 : 84.0;
    const auto beat = std::max<int64_t> (1, wr.beat);
    const auto firstIndex = wr.beats.empty() ? 0 : (int64_t) std::floor ((wr.beats.front().tick - wr.start) / (double) beat);
    const auto firstLine = (int64_t) std::floor (firstIndex / (double) perLine);
    const auto lineCount = wr.beats.empty() ? 1 : (int64_t) std::floor ((((wr.beats.back().tick - wr.start) / beat) / (double) perLine)) - firstLine + 1;

    grid = { true, false, wr.start, beat, firstLine, perLine, top + 28.0, lineHeight };
    lines->show();
    lines->setRange (0, std::max (0, (int) (top + 28.0 + lineCount * lineHeight - (height() - rulerHeight))));
    lines->setPageStep (std::max (1, height() - rulerHeight - 40));
    lines->setSingleStep (20);

    // The heading: what it counts in.
    g.fillRect (QRect (0, 0, w, rulerHeight), pal.color (QPalette::Window));
    g.setFont (small);
    g.setPen (text);
    g.drawText (QRectF (8, 0, w - 16, rulerHeight), Qt::AlignLeft | Qt::AlignVCenter, QString::fromStdString (wr.legend));

    struct Placed { QPointF at; double width; };
    std::map<int, Placed> placedNotes;                 // each note's sign, for the glides
    std::vector<std::pair<int, int>> glides;           // (from note, to note)
    const auto mel = notation::melody (ed.seq);
    g.save();
    g.setClipRect (QRectF (0, rulerHeight, w - lines->width(), height() - rulerHeight));
    for (size_t i = 0; i < wr.beats.size(); ++i)
    {
        const auto& b = wr.beats[i];
        const auto index = (int64_t) std::floor ((b.tick - wr.start) / (double) beat);
        const auto line = (int64_t) std::floor (index / (double) perLine) - firstLine;
        const auto column = index - (line + firstLine) * perLine;
        const auto x = left + column * beatWidth;
        const auto y = top + 28.0 + line * lineHeight - lines->value();
        if (y + lineHeight < rulerHeight || y - 40 > height())
            continue;
        const auto baseline = y + 26.0;
        const QRectF cell (x, y - 6.0, beatWidth, lineHeight - 8.0);

        // The playhead's beat, and the playhead through it; the step cursor's.
        if (ed.playheadTick >= b.tick && ed.playheadTick < b.tick + b.length)
        {
            auto h = pal.color (QPalette::Highlight);
            h.setAlphaF (0.18);
            g.fillRect (cell, h);
            const auto px = cell.left() + cell.width() * (double) (ed.playheadTick - b.tick) / (double) std::max<int64_t> (1, b.length);
            g.setPen (QPen (text, 1.5));
            g.drawLine (QPointF (px, cell.top()), QPointF (px, cell.bottom()));
        }
        if (ed.stepBox->isChecked() && ed.step.position >= b.tick && ed.step.position < b.tick + b.length)
        {
            g.setPen (QPen (QColor (0xe0, 0x50, 0x50), 1.5));
            g.setBrush (Qt::NoBrush);
            g.drawRect (cell.adjusted (1, 1, -1, -1));
        }

        // The bars: | before a section (a vibhāg, an aṅga, a bar), || at a cycle's start, or
        // (Karnatak) || closing it.
        g.setPen (QPen (text, 1.2));
        const auto bar = [&] (double at, bool twice)
        {
            g.drawLine (QPointF (at, baseline - 22), QPointF (at, baseline + 8));
            if (twice)
                g.drawLine (QPointF (at - 3.5, baseline - 22), QPointF (at - 3.5, baseline + 8));
        };
        if (wr.doubleBarAtCycle)
        {
            const auto last = i + 1 >= wr.beats.size() || wr.beats[i + 1].cycleStart;
            if (b.sectionStart && ! b.cycleStart)
                bar (x - 1.0, false);
            if (last)
                bar (x + beatWidth - 2.0, true);
        }
        else if (b.sectionStart || b.cycleStart)
            bar (x - 1.0, b.cycleStart && ! jianpuLike);

        // The section's sign under its first beat (Bhatkhande's ×, 0, 2; the aṅgas' I₄, O, U).
        if (! b.sign.empty())
        {
            g.setFont (small);
            g.setPen (text);
            g.drawText (QPointF (x + 4.0, baseline + 26.0), QString::fromStdString (b.sign));
        }

        // The signs, sharing the beat.
        const auto share = (beatWidth - 8.0) / std::max<size_t> (1, b.signs.size());
        std::vector<QRectF> boxes;
        for (size_t k = 0; k < b.signs.size(); ++k)
        {
            const auto& sg = b.signs[k];
            const auto name = QString::fromStdString (sg.text);
            const auto before = QString::fromStdString (sg.before);
            const auto nameWidth = bm.horizontalAdvance (name);
            const auto beforeWidth = sm.horizontalAdvance (before);
            const auto sx = x + 4.0 + k * share + std::max (0.0, (share - nameWidth - beforeWidth - sg.dots * 5.0) / 2.0);
            const auto nx = sx + beforeWidth;
            const auto picked = sg.note >= 0 && ed.seq.notes[(size_t) sg.note].selected;
            const auto colour = picked ? pal.color (QPalette::Highlight) : sg.kind == Sign::Kind::note ? text : faint;
            g.setPen (colour);
            if (! before.isEmpty())
            {
                g.setFont (small);
                g.drawText (QPointF (sx, baseline - 6.0), before);
            }
            g.setFont (big);
            g.drawText (QPointF (nx, baseline), name);
            const QRectF box (nx, baseline - bm.ascent(), nameWidth, bm.ascent() + bm.descent());
            boxes.push_back (box);
            if (sg.note >= 0 && sg.kind == Sign::Kind::note)
            {
                hits.push_back ({ box.adjusted (-3, -6, 3, 6), sg.note });
                placedNotes[sg.note] = { QPointF (nx + nameWidth / 2.0, baseline - bm.ascent()), nameWidth };
                if (sg.glide)
                {
                    const auto it = std::upper_bound (mel.begin(), mel.end(), sg.note, [&] (int a, int m) { return ed.seq.notes[(size_t) a].start < ed.seq.notes[(size_t) m].start; });
                    if (it != mel.end())
                        glides.push_back ({ sg.note, *it });
                }
            }
            // Octave dots, above or below; with jianpu's lines under, the dots below go under them.
            g.setPen (Qt::NoPen);
            g.setBrush (colour);
            for (int d = 0; d < std::abs (sg.octave); ++d)
            {
                const auto dy = sg.octave > 0 ? baseline - bm.ascent() - 3.0 - d * 5.0
                                              : baseline + 5.0 + d * 5.0 + (jianpuLike ? sg.lines * 3.5 : 0.0) + (sg.lowered ? 3.0 : 0.0);
                g.drawEllipse (QPointF (nx + nameWidth / 2.0, dy), 1.7, 1.7);
            }
            g.setBrush (Qt::NoBrush);
            g.setPen (QPen (colour, 1.3));
            // Komal: a line under; tīvra: a stroke above.
            if (sg.lowered)
                g.drawLine (QPointF (nx, baseline + 3.0), QPointF (nx + nameWidth, baseline + 3.0));
            if (sg.raised)
                g.drawLine (QPointF (nx + nameWidth / 2.0, baseline - bm.ascent() - 1.0), QPointF (nx + nameWidth / 2.0, baseline - bm.ascent() - 9.0));
            // Jianpu's dots after (a dotted note).
            for (int d = 0; d < sg.dots; ++d)
            {
                g.setBrush (colour);
                g.drawEllipse (QPointF (nx + nameWidth + 3.0 + d * 5.0, baseline - bm.ascent() / 3.0), 1.6, 1.6);
                g.setBrush (Qt::NoBrush);
            }
            // A kaṇ or a grace note, small above and before.
            if (! sg.grace.empty())
            {
                g.setFont (tiny);
                g.setPen (colour);
                g.drawText (QPointF (nx - 6.0, baseline - bm.ascent() - 2.0), QString::fromStdString (sg.grace));
            }
            // The cents off its degree, and its mark, small above.
            if (! sg.off.empty())
            {
                g.setFont (tiny);
                g.setPen (faint);
                g.drawText (QPointF (nx + nameWidth + 1.0, baseline - bm.ascent() + 4.0), QString::fromStdString (sg.off));
            }
            if (! sg.mark.empty())
            {
                g.setFont (tiny);
                g.setPen (QColor (0xe0, 0x8a, 0x2a));
                g.drawText (QPointF (nx, baseline - bm.ascent() - 12.0 - (sg.octave > 0 ? 5.0 * sg.octave : 0.0)), QString::fromStdString (sg.mark));
            }
            // The words under.
            if (! sg.lyric.empty())
            {
                g.setFont (small);
                g.setPen (text);
                g.drawText (QPointF (nx, baseline + (b.sign.empty() ? 30.0 : 44.0) + (b.signs.empty() ? 0.0 : 0.0)), QString::fromStdString (sg.lyric));
            }
        }

        if (boxes.empty())
            continue;
        g.setPen (QPen (text, 1.2));
        g.setBrush (Qt::NoBrush);
        if (jianpuLike)
        {
            // Jianpu's lines under, beamed across the beat where the signs run on at that many.
            for (int level = 1; level <= 4; ++level)
                for (size_t k = 0; k < b.signs.size(); ++k)
                {
                    if (b.signs[k].lines < level)
                        continue;
                    auto end = k;
                    while (end + 1 < b.signs.size() && b.signs[end + 1].lines >= level)
                        ++end;
                    const auto yy = baseline + 3.0 + (level - 1) * 3.5;
                    g.drawLine (QPointF (boxes[k].left(), yy), QPointF (boxes[end].right(), yy));
                    k = end;
                }
        }
        else if (carnatic)
        {
            // The speed: lines over the akṣara's svaras.
            for (int level = 0; level < b.signs.front().lines; ++level)
            {
                const auto yy = baseline - bm.ascent() - 4.0 - level * 3.0 - (std::any_of (b.signs.begin(), b.signs.end(), [] (const Sign& x) { return x.octave > 0; }) ? 6.0 : 0.0);
                g.drawLine (QPointF (boxes.front().left(), yy), QPointF (boxes.back().right(), yy));
            }
        }
        else if (b.grouped)
        {
            // Bhatkhande: an arc under the svaras that share a mātrā.
            QPainterPath arc;
            const auto yy = baseline + 8.0 + (std::any_of (b.signs.begin(), b.signs.end(), [] (const Sign& x) { return x.octave < 0; }) ? 6.0 : 0.0);
            arc.moveTo (boxes.front().left(), yy);
            arc.quadTo ((boxes.front().left() + boxes.back().right()) / 2.0, yy + 7.0, boxes.back().right(), yy);
            g.drawPath (arc);
        }
    }

    // Mīṇḍ and slides: an arc over, from the note to the next.
    g.setPen (QPen (curveColour, 1.3));
    for (const auto& [from, to] : glides)
    {
        const auto a = placedNotes.find (from), b = placedNotes.find (to);
        if (a == placedNotes.end() || b == placedNotes.end() || std::abs (a->second.at.y() - b->second.at.y()) > 1.0)
            continue;
        QPainterPath arc;
        const auto yy = a->second.at.y() - 6.0;
        arc.moveTo (a->second.at.x(), yy);
        arc.quadTo ((a->second.at.x() + b->second.at.x()) / 2.0, yy - 10.0, b->second.at.x(), yy);
        g.drawPath (arc);
    }
    g.restore();
}

void MidiEditor::Staff::paintColumns (QPainter& g, const notation::Writing& wr, int w)
{
    // Jeongganbo: a column (gak) a cycle, read down, the columns right to left; a box (jeonggan) a
    // beat, its signs in its rows and columns; a thick line where a section (daegang) begins.
    using notation::Sign;
    const auto pal = palette();
    const auto text = pal.color (QPalette::Text);
    const auto faint = pal.color (QPalette::PlaceholderText);
    lines->hide();
    scroll->show();
    const auto family = jeongganFamily.isEmpty() ? font().family() : jeongganFamily;
    const auto top = (double) rulerHeight + 10.0;
    const auto bottom = (double) height() - scroll->height() - 22.0;
    const auto box = 46.0;
    const auto columnWidth = box + 30.0;
    const auto beat = std::max<int64_t> (1, wr.beat);
    // A cycle a column, cut where the view is too short for it.
    auto perColumn = std::max (1, wr.beatsPerLine);
    perColumn = std::max (1, std::min (perColumn, (int) ((bottom - top) / box)));
    const auto firstIndex = wr.beats.empty() ? 0 : (int64_t) std::floor ((wr.beats.front().tick - wr.start) / (double) beat);
    const auto firstColumn = (int64_t) std::floor (firstIndex / (double) perColumn);
    const auto columns = wr.beats.empty() ? 1 : (int64_t) std::floor (((wr.beats.back().tick - wr.start) / beat) / (double) perColumn) - firstColumn + 1;
    grid = { true, true, wr.start, beat, firstColumn, perColumn, top, columnWidth };
    scroll->setRange (0, std::max (0, (int) (columns * columnWidth + 40.0 - w)));
    scroll->setPageStep (std::max (1, w - 40));
    scroll->setSingleStep (20);

    g.fillRect (QRect (0, 0, w, rulerHeight), pal.color (QPalette::Window));
    QFont small = font();
    small.setPixelSize (11);
    g.setFont (small);
    g.setPen (text);
    g.drawText (QRectF (8, 0, w - 16, rulerHeight), Qt::AlignRight | Qt::AlignVCenter, QString::fromStdString (wr.legend));

    QFont lyricFont = font();
    lyricFont.setPixelSize (10);
    for (const auto& b : wr.beats)
    {
        const auto index = (int64_t) std::floor ((b.tick - wr.start) / (double) beat);
        const auto column = (int64_t) std::floor (index / (double) perColumn) - firstColumn;
        const auto row = index - (column + firstColumn) * perColumn;
        // From the right: the scroll bar's 0 shows the first column at the right edge.
        const auto x = w - 20.0 - (column + 1) * columnWidth + scroll->value() + 14.0;
        const auto y = top + row * box;
        if (x + columnWidth < 0 || x > w)
            continue;
        const QRectF cell (x, y, box, box);
        if (ed.playheadTick >= b.tick && ed.playheadTick < b.tick + b.length)
        {
            auto h = pal.color (QPalette::Highlight);
            h.setAlphaF (0.18);
            g.fillRect (cell, h);
            // The playhead down the box, as the box is read.
            const auto py = cell.top() + cell.height() * (double) (ed.playheadTick - b.tick) / (double) std::max<int64_t> (1, b.length);
            g.setPen (QPen (text, 1.5));
            g.drawLine (QPointF (cell.left(), py), QPointF (cell.right(), py));
        }
        g.setPen (QPen (text, 1.0));
        g.setBrush (Qt::NoBrush);
        g.drawRect (cell);
        if (b.sectionStart || row == 0)
        {
            g.setPen (QPen (text, 2.6));
            g.drawLine (cell.topLeft(), cell.topRight());
        }
        if (ed.stepBox->isChecked() && ed.step.position >= b.tick && ed.step.position < b.tick + b.length)
        {
            g.setPen (QPen (QColor (0xe0, 0x50, 0x50), 1.5));
            g.drawRect (cell.adjusted (2, 2, -2, -2));
        }
        const auto rows = std::max (1, b.rows), cols = std::max (1, b.columns);
        const auto ch = box / rows, cw = box / cols;
        QFont signFont (family);
        signFont.setPixelSize (std::max (9, (int) (std::min (ch, cw) * 0.62)));
        for (const auto& sg : b.signs)
        {
            const QRectF part (x + sg.column * cw, y + sg.row * ch, cw, ch);
            const auto picked = sg.note >= 0 && ed.seq.notes[(size_t) sg.note].selected;
            g.setPen (picked ? pal.color (QPalette::Highlight) : sg.kind == Sign::Kind::note ? text : faint);
            g.setFont (signFont);
            g.drawText (part, Qt::AlignCenter, QString::fromStdString (sg.text));
            if (sg.note >= 0 && sg.kind == Sign::Kind::note)
                hits.push_back ({ part, sg.note });
            // The sigimsae, beside the box on the right (in the font that draws it).
            if (! sg.mark.empty())
            {
                QFont markFont (family);
                markFont.setPixelSize (std::max (8, (int) (ch * 0.45)));
                g.setFont (markFont);
                g.setPen (QColor (0xe0, 0x8a, 0x2a));
                g.drawText (QRectF (x + box + 1.0, part.top(), 14.0, ch), Qt::AlignLeft | Qt::AlignVCenter, QString::fromStdString (sg.mark));
            }
            // The words on the left.
            if (! sg.lyric.empty())
            {
                g.setFont (lyricFont);
                g.setPen (text);
                g.drawText (QRectF (x - 15.0, part.top(), 14.0, ch), Qt::AlignRight | Qt::AlignVCenter, QString::fromStdString (sg.lyric));
            }
        }
    }
}

void MidiEditor::Staff::contextMenuEvent (QContextMenuEvent* e)
{
    // A note's lyric and mark (NoteEditor.h): what the notations write with it beyond its pitch.
    const auto hit = noteAt (e->pos());
    if (hit < 0)
        return;
    const auto index = (size_t) hit;
    const auto set = [this, index] (std::string Note::* field, const std::string& value)
    {
        ed.edit ([&] (Sequence& s)
        {
            if (index < s.notes.size())
                s.notes[index].*field = value;
        });
    };
    QMenu menu (this);
    const auto& n = ed.seq.notes[index];
    menu.addAction (tr ("Lyric..."), this, [this, index, set]
    {
        bool ok = false;
        const auto now = QString::fromStdString (ed.seq.notes[index].lyric);
        const auto t = QInputDialog::getText (this, tr ("Lyric"), tr ("The syllable sung on this note (a bandish's or kṛti's words, a song's):"), QLineEdit::Normal, now, &ok);
        if (ok)
            set (&Note::lyric, t.toStdString());
    });
    menu.addAction (tr ("Mark..."), this, [this, index, set]
    {
        bool ok = false;
        const auto now = QString::fromStdString (ed.seq.notes[index].mark);
        const auto t = QInputDialog::getText (this, tr ("Mark"), tr ("How it is played (a stroke, a fingering, a technique: dā, rā, oshide...):"), QLineEdit::Normal, now, &ok);
        if (ok)
            set (&Note::mark, t.toStdString());
    });
    auto* sig = menu.addMenu (tr ("Sigimsae (Jeongganbo)"));
    for (const auto& s : notation::sigimsae())
    {
        auto* a = sig->addAction (QString::fromStdString (s.key + "  ") + QString::fromStdString (s.text), this, [set, text = s.text] { set (&Note::mark, text); });
        if (! jeongganFamily.isEmpty())
            a->setFont (QFont (jeongganFamily));
    }
    if (! n.lyric.empty() || ! n.mark.empty())
    {
        menu.addSeparator();
        menu.addAction (tr ("Clear Lyric and Mark"), this, [this, index]
        {
            ed.edit ([&] (Sequence& s)
            {
                if (index < s.notes.size())
                    s.notes[index].lyric.clear(), s.notes[index].mark.clear();
            });
        });
    }
    menu.exec (e->globalPos());
}
