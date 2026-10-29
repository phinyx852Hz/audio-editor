// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiStaff.h"

#include <QCheckBox>
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
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
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
    connect (scroll, &QScrollBar::valueChanged, this, [this] { update(); });

    static const QString family = []
    {
        const auto id = QFontDatabase::addApplicationFont (QStringLiteral (":/fonts/Bravura.otf"));
        const auto families = id >= 0 ? QFontDatabase::applicationFontFamilies (id) : QStringList();
        return families.isEmpty() ? QString() : families.front();
    }();
    musicFamily = family;
    setToolTip (tr ("The staff: click picks a note, double-click writes one on that line or space, Ctrl+Up/Down moves the picked notes "
                    "to the next pitch the notation writes, Ctrl+wheel zooms"));
}

QString MidiEditor::Staff::tuningsFolder()
{
    const auto installed = QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHI_SHARE_RELATIVE "/xen-tuner/tunings"));
    if (QFileInfo (installed).isDir())
        return QDir::cleanPath (installed);
    return QStringLiteral (PHI_SHARE_SOURCE_DIR "/xen-tuner/tunings");
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
    return isAutomatic ? notation::suggested (s, tuningsFolder().toStdString()) : choice;
}

QString MidiEditor::Staff::titleFor (const PitchSystem& s) const
{
    return QString::fromStdString (notation::title (choiceFor (s)));
}

const phi::xen::Speller* MidiEditor::Staff::spellerFor (const PitchSystem& s) const
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
    if (! ed.seq.notes.empty())
        scroll->setValue ((int) std::max (0.0, ed.seq.notes.front().start * pixelsPerTick - 40.0));
    update();
}

void MidiEditor::Staff::layoutScroll()
{
    const auto h = scroll->sizeHint().height();
    scroll->setGeometry (0, height() - h, width(), h);
    const auto length = (double) std::max<int64_t> (ed.seq.end(), 4 * ed.seq.ticksPerQuarter) + 4 * ed.seq.ticksPerQuarter;
    scroll->setRange (0, std::max (0, (int) (length * pixelsPerTick) - (width() - clefWidth)));
    scroll->setPageStep (std::max (1, width() - clefWidth));
    scroll->setSingleStep (20);
}

void MidiEditor::Staff::resizeEvent (QResizeEvent*) { layoutScroll(); }

double MidiEditor::Staff::xOf (int64_t tick) const { return clefWidth + 12.0 + tick * pixelsPerTick - scroll->value(); }

int64_t MidiEditor::Staff::tickAt (double x) const { return std::max<int64_t> (0, (int64_t) std::llround ((x - clefWidth - 12.0 + scroll->value()) / pixelsPerTick)); }

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
            const auto s = sp->best (n.pitch, flats).staff;
            low = std::min (low, s - 3);
            high = std::max (high, s + 3);
        }
    }
    const auto room = height() - rulerHeight - 16.0 - scroll->height() - 18.0;
    half = std::clamp (room / std::max (1, high - low), 3.0, 10.0);
    // Centred in the room there is.
    highest = high + std::max (0.0, (room / half - (high - low)) / 2.0);
}

void MidiEditor::Staff::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto pal = palette();
    const auto w = width();
    g.fillRect (rect(), pal.color (QPalette::Base));

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
        const auto spelled = sp->best (n.pitch, flats);
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
        for (auto it = spelled.symbols.rbegin(); it != spelled.symbols.rend(); ++it)
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

    // The clefs, over the notes that have scrolled under them.
    g.fillRect (QRectF (0, rulerHeight, clefWidth, height() - rulerHeight), pal.color (QPalette::Base));
    g.setPen (QPen (text, 1.0));
    for (const auto l : trebleLines)
        g.drawLine (QPointF (0, yOf (l)), QPointF (clefWidth, yOf (l)));
    for (const auto l : bassLines)
        g.drawLine (QPointF (0, yOf (l)), QPointF (clefWidth, yOf (l)));
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
