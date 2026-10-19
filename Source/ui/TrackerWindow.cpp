// SPDX-License-Identifier: AGPL-3.0-only
// The Tracker window: MilkyTracker's module player (MilkyPlay) and its way of editing
// patterns, in PHINYX. The keyboard follows MilkyTracker's FastTracker 2 mode.
#include "TrackerWindow.h"

#include "../tracker/PatternText.h"
#include "Theme.h"

#include "XModule.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

TrackerEngine::Block PatternView::clipboard;

namespace
{
    QColor colour (const char* key) { return Theme::current().color (QString::fromLatin1 (key)); }

    QFont trackerFont()
    {
        auto f = QFontDatabase::systemFont (QFontDatabase::FixedFont);
        f.setPointSizeF (std::max (9.0, f.pointSizeF()));
        return f;
    }

    QString hexRow (int row) { return QStringLiteral ("%1").arg (row, 2, 16, QLatin1Char ('0')).toUpper(); }
    QString hex2 (int value) { return QStringLiteral ("%1").arg (value, 2, 16, QLatin1Char ('0')).toUpper(); }

    // Where each column of a cell starts, in characters, and how wide its cursor is.
    constexpr int columnChar[TrackerEngine::numColumns] { 0, 4, 5, 7, 8, 10, 11, 12 };
    constexpr int columnChars[TrackerEngine::numColumns] { 3, 1, 1, 1, 1, 1, 1, 1 };

    // The keyboard as a piano by key position (PC scan codes, so any layout works),
    // as in MilkyTracker's FastTracker 2 mode: Z to / is the base octave with S, D, G,
    // H, J, L and ; as the sharps; Q to ] the octave above with the digits as sharps.
    struct NoteKey { int scanCode, octave, semitone; };
    constexpr NoteKey noteKeys[] {
        { 0x2C, 0, 0 }, { 0x1F, 0, 1 }, { 0x2D, 0, 2 }, { 0x20, 0, 3 }, { 0x2E, 0, 4 }, { 0x2F, 0, 5 },
        { 0x22, 0, 6 }, { 0x30, 0, 7 }, { 0x23, 0, 8 }, { 0x31, 0, 9 }, { 0x24, 0, 10 }, { 0x32, 0, 11 },
        { 0x33, 1, 0 }, { 0x26, 1, 1 }, { 0x34, 1, 2 }, { 0x27, 1, 3 }, { 0x35, 1, 4 },
        { 0x10, 1, 0 }, { 0x03, 1, 1 }, { 0x11, 1, 2 }, { 0x04, 1, 3 }, { 0x12, 1, 4 }, { 0x13, 1, 5 },
        { 0x06, 1, 6 }, { 0x14, 1, 7 }, { 0x07, 1, 8 }, { 0x15, 1, 9 }, { 0x08, 1, 10 }, { 0x16, 1, 11 },
        { 0x17, 2, 0 }, { 0x0A, 2, 1 }, { 0x18, 2, 2 }, { 0x0B, 2, 3 }, { 0x19, 2, 4 }, { 0x1A, 2, 5 },
        { 0x0D, 2, 6 }, { 0x1B, 2, 7 },
    };
    constexpr int keyOffKeys[] { 0x29, 0x3A, 0x02, 0x56 };   // the key left of 1, Caps Lock, 1, and <> where there is one

    // X11 and Wayland key codes are the evdev code + 8; evdev codes are the PC scan codes.
    int scanCodeOf (const QKeyEvent* e) { return (int) e->nativeScanCode() - 8; }

    QString noteName (int note)
    {
        static const char* names[12] { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
        if (note == TrackerEngine::keyOff)
            return QStringLiteral ("key off");
        return QString::fromLatin1 (names[(note - 1) % 12]) + QString::number ((note - 1) / 12);
    }
}

//==============================================================================
PatternView::PatternView (TrackerEngine& e, QWidget* parent) : QWidget (parent), engine (e)
{
    setFocusPolicy (Qt::StrongFocus);
    setFont (trackerFont());
    setAccessibleName (tr ("Pattern"));
    setMinimumSize (320, 200);
}

void PatternView::moduleChanged()
{
    currentOrder = cursorRow = cursorChannel = cursorColumn = firstChannel = 0;
    playRow = -1;
    selecting = false;
    update();
}

int PatternView::patternIndex() const { return engine.patternAt (currentOrder); }

int PatternView::rows() const
{
    const auto* p = engine.pattern (patternIndex());
    return p != nullptr ? p->rows : 0;
}

void PatternView::setOrder (int order)
{
    order = std::clamp (order, 0, std::max (0, engine.numOrders() - 1));
    if (order == currentOrder)
        return;
    currentOrder = order;
    cursorRow = std::clamp (cursorRow, 0, std::max (0, rows() - 1));
    selecting = false;
    update();
}

void PatternView::setRow (int row)
{
    cursorRow = std::clamp (row, 0, std::max (0, rows() - 1));
    update();
}

void PatternView::setPlayRow (int row)
{
    if (row != playRow)
    {
        playRow = row;
        update();
    }
}

void PatternView::setEditing (bool on)
{
    if (on == editMode)
        return;
    editMode = on;
    setAccessibleName (on ? tr ("Pattern, editing") : tr ("Pattern"));
    Q_EMIT editingChanged (on);
    update();
}

void PatternView::setOctave (int o)
{
    baseOctave = std::clamp (o, 0, 7);
    Q_EMIT settingsChanged();
}

void PatternView::setEditStep (int s)
{
    step = std::clamp (s, 0, 16);
    Q_EMIT settingsChanged();
}

void PatternView::setInstrument (int i)
{
    currentInstrument = std::clamp (i, 1, TrackerEngine::numInstruments);
    Q_EMIT settingsChanged();
}

int PatternView::columnWidth() const { return QFontMetrics (font()).horizontalAdvance (QLatin1Char ('0')) * 14 + 6; }
int PatternView::rowNumberWidth() const { return QFontMetrics (font()).horizontalAdvance (QLatin1Char ('0')) * 3 + 8; }
int PatternView::lineHeight() const { return QFontMetrics (font()).height() + 2; }

int PatternView::rowAt (int y) const
{
    const int area = height() - headerHeight;
    const int middle = headerHeight + area / 2 - lineHeight() / 2;
    const int focusRow = playRow >= 0 ? playRow : cursorRow;
    return std::clamp (focusRow + (int) std::floor ((y - middle) / (double) lineHeight()), 0, std::max (0, rows() - 1));
}

void PatternView::keepChannelVisible()
{
    const int visible = std::max (1, (width() - rowNumberWidth()) / columnWidth());
    if (cursorChannel < firstChannel)
        firstChannel = cursorChannel;
    else if (cursorChannel >= firstChannel + visible)
        firstChannel = cursorChannel - visible + 1;
}

void PatternView::announce()
{
    const auto* p = engine.pattern (patternIndex());
    if (p == nullptr || cursorChannel >= p->channum)
        return;
    const auto c = PatternText::cell (*p, cursorRow, cursorChannel);
    static const char* columnNames[] { "note", "instrument", "instrument", "volume", "volume", "effect", "operand", "operand" };
    setAccessibleDescription (tr ("Row %1, channel %2, %3 column: %4 %5 %6 %7%8")
                                  .arg (cursorRow).arg (cursorChannel + 1).arg (tr (columnNames[cursorColumn]))
                                  .arg (QString::fromStdString (c.note), QString::fromStdString (c.instrument),
                                        QString::fromStdString (c.volume), QString::fromStdString (c.effect),
                                        QString::fromStdString (c.operand)));
}

void PatternView::selectionBounds (int& row0, int& row1, int& channel0, int& channel1) const
{
    row0 = std::min (anchorRow, cursorRow);
    row1 = std::max (anchorRow, cursorRow);
    channel0 = std::min (anchorChannel, cursorChannel);
    channel1 = std::max (anchorChannel, cursorChannel);
}

// Tab and Shift+Tab step through the channels, as in FastTracker 2; Ctrl+Tab leaves the pattern.
bool PatternView::event (QEvent* e)
{
    if (e->type() == QEvent::KeyPress)
        if (auto* k = static_cast<QKeyEvent*> (e); k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab)
        {
            if (k->modifiers().testFlag (Qt::ControlModifier))
                focusNextPrevChild (k->key() == Qt::Key_Tab && ! k->modifiers().testFlag (Qt::ShiftModifier));
            else
                keyPressEvent (k);
            return true;
        }
    return QWidget::event (e);
}

void PatternView::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.fillRect (rect(), Theme::withOpacity (colour ("waveformBackground")));
    const QFontMetrics fm (font());
    const int cw = fm.horizontalAdvance (QLatin1Char ('0'));
    const int lh = lineHeight();
    const auto text = colour ("text"), dim = colour ("placeholderText"), accent = palette().color (QPalette::Link);
    const auto cursorColour = editMode ? colour ("meterHigh") : accent;

    if (! engine.isLoaded())
    {
        g.setPen (dim);
        g.drawText (rect().adjusted (16, 0, -16, 0), Qt::AlignCenter | Qt::TextWordWrap,
                    tr ("Ctrl+N starts a new song; Ctrl+O opens a module: XM, MOD, IT, S3M and the other formats MilkyTracker reads."));
        return;
    }

    const auto* p = engine.pattern (patternIndex());
    const int channels = engine.numChannels();
    const int numberWidth = rowNumberWidth(), colWidth = columnWidth();
    const int visibleChannels = std::max (1, (width() - numberWidth) / colWidth);

    // Channel headers: number, M, S and the track.
    for (int i = 0; i < visibleChannels && firstChannel + i < channels; ++i)
    {
        const int ch = firstChannel + i;
        const QRect head (numberWidth + i * colWidth, 0, colWidth - 2, headerHeight - 4);
        g.fillRect (head, Theme::withOpacity (palette().color (QPalette::Button)));
        const bool heard = engine.isHeard (ch);
        g.setPen (ch == cursorChannel && hasFocus() ? cursorColour : heard ? text : dim);
        g.drawText (head.adjusted (6, 2, 0, -head.height() / 2), Qt::AlignLeft | Qt::AlignVCenter, tr ("Channel %1").arg (ch + 1));

        const int boxW = cw * 3 + 4, y = head.top() + head.height() / 2 + 1, h = head.height() / 2 - 3;
        const QRect mute (head.left() + 6, y, boxW, h), solo (mute.right() + 4, y, boxW, h), trackBox (solo.right() + 4, y, boxW + cw, h);
        auto box = [&] (QRect r, const QString& label, bool on, QColor onColour)
        {
            g.setPen (dim);
            g.setBrush (on ? onColour : Qt::transparent);
            g.drawRoundedRect (r, 3, 3);
            g.setPen (on ? QColor (Qt::black) : text);
            g.drawText (r, Qt::AlignCenter, label);
        };
        box (mute, QStringLiteral ("M"), engine.isMuted (ch), colour ("meterHigh"));
        box (solo, QStringLiteral ("S"), engine.isSoloed (ch), colour ("meterMid"));
        box (trackBox, QStringLiteral ("T%1").arg (engine.track (ch) + 1), false, accent);
        g.setBrush (Qt::NoBrush);
    }

    if (p == nullptr)
    {
        g.setPen (dim);
        g.drawText (rect().adjusted (0, headerHeight, 0, 0), Qt::AlignCenter | Qt::TextWordWrap,
                    tr ("This order position has an empty pattern."));
        return;
    }

    int sr0 = 0, sr1 = -1, sc0 = 0, sc1 = -1;
    if (selecting)
        selectionBounds (sr0, sr1, sc0, sc1);

    // Rows, with the cursor's (or the playing) row held in the middle.
    const int focusRow = playRow >= 0 ? playRow : cursorRow;
    const int area = height() - headerHeight;
    const int middle = headerHeight + area / 2 - lh / 2;
    const int first = std::max (0, focusRow - (middle - headerHeight) / lh - 1);
    for (int r = first; r < p->rows; ++r)
    {
        const int y = middle + (r - focusRow) * lh;
        if (y > height())
            break;
        if (y + lh < headerHeight)
            continue;

        const QRect line (0, y, width(), lh);
        if (r == playRow)
            g.fillRect (line, Theme::withOpacity (palette().color (QPalette::Highlight)).darker (140));
        else if (r == cursorRow && editMode)
            g.fillRect (line, Theme::withOpacity (cursorColour.darker (300)));
        else if (r % 16 == 0)
            g.fillRect (line, Theme::withOpacity (palette().color (QPalette::AlternateBase)).lighter (130));
        else if (r % 4 == 0)
            g.fillRect (line, Theme::withOpacity (palette().color (QPalette::AlternateBase)));

        g.setPen (r % 4 == 0 ? text : dim);
        g.drawText (QRect (4, y, numberWidth - 8, lh), Qt::AlignRight | Qt::AlignVCenter, hexRow (r));

        for (int i = 0; i < visibleChannels && firstChannel + i < channels; ++i)
        {
            const int ch = firstChannel + i;
            const auto c = PatternText::cell (*p, r, ch);
            const bool heard = engine.isHeard (ch);
            const int cellX = numberWidth + i * colWidth;
            int x = cellX + 4;
            if (r >= sr0 && r <= sr1 && ch >= sc0 && ch <= sc1)
            {
                auto shade = palette().color (QPalette::Highlight);
                shade.setAlphaF (0.45f);
                g.fillRect (QRect (cellX, y, colWidth - 2, lh), shade);
            }
            if (r == cursorRow && ch == cursorChannel)
            {
                const QRectF box (cellX + 4 + columnChar[cursorColumn] * cw - 1, y, columnChars[cursorColumn] * cw + 2, lh - 1);
                auto fill = cursorColour;
                fill.setAlphaF (hasFocus() ? 0.35f : 0.15f);
                g.fillRect (box, fill);
                g.setPen (QPen (hasFocus() ? cursorColour : dim, 1.5));
                g.drawRect (box);
            }
            auto part = [&] (const std::string& s, QColor c2, int chars)
            {
                auto shade = heard ? c2 : dim;
                if (! heard)
                    shade.setAlphaF (0.5f);
                g.setPen (shade);
                g.drawText (QPoint (x, y + fm.ascent() + 1), QString::fromStdString (s));
                x += cw * (chars + 1);
            };
            const bool hasNote = c.note.find ("\xc2\xb7") == std::string::npos;
            part (c.note, hasNote ? colour ("waveform") : dim, 3);
            part (c.instrument, c.instrument.find ("\xc2\xb7") == std::string::npos ? accent : dim, 2);
            part (c.volume, c.volume.find ("\xc2\xb7") == std::string::npos ? colour ("meterLow") : dim, 2);
            // The effect and its operand run together, like FastTracker 2.
            const bool hasEffect = c.effect.find ("\xc2\xb7") == std::string::npos;
            g.setPen (heard ? (hasEffect ? colour ("meterMid") : dim) : dim);
            g.drawText (QPoint (x, y + fm.ascent() + 1), QString::fromStdString (c.effect + c.operand));
        }
    }

    // Lines between channels.
    g.setPen (QPen (colour ("mid"), 1));
    for (int i = 0; i <= visibleChannels && firstChannel + i <= channels; ++i)
    {
        const int x = numberWidth + i * colWidth - 2;
        g.drawLine (x, headerHeight - 2, x, height());
    }

    if (editMode)
    {
        g.setPen (QPen (cursorColour, 2));
        g.setBrush (Qt::NoBrush);
        g.drawRect (QRectF (1, 1, width() - 2, height() - 2));
    }
}

//==============================================================================
int PatternView::noteForKey (const QKeyEvent* e) const
{
    const int code = scanCodeOf (e);
    for (const auto k : keyOffKeys)
        if (k == code)
            return TrackerEngine::keyOff;
    for (const auto& k : noteKeys)
        if (k.scanCode == code)
        {
            const int note = (baseOctave + k.octave) * 12 + k.semitone + 1;
            return note >= 1 && note <= 96 ? note : -1;
        }
    return -1;
}

void PatternView::advance()
{
    const int n = rows();
    if (n > 0 && step > 0)
        cursorRow = (cursorRow + step) % n;
}

void PatternView::afterEdit (bool changed)
{
    if (changed)
        Q_EMIT edited();
    announce();
    update();
}

// A digit or letter typed at the instrument, volume or effect column (PatternEditorControl::handleKeyChar).
bool PatternView::typeDigit (const QKeyEvent* e)
{
    const auto t = e->text().toLower();
    if (t.size() != 1)
        return false;
    const auto ch = t.at (0).toLatin1();
    int value = -1;
    if (cursorColumn == TrackerEngine::volumeHigh)
    {
        switch (ch)
        {
            case '-': value = 5; break;
            case '+': value = 6; break;
            case 'd': value = 7; break;
            case 'u': value = 8; break;
            case 's': value = 9; break;
            case 'v': value = 0xA; break;
            case 'p': value = 0xB; break;
            case 'l': case '<': value = 0xC; break;
            case 'r': case '>': value = 0xD; break;
            case 'm': value = 0xE; break;
            default:
                if (ch >= '0' && ch <= '4')
                    value = ch - '0';
                break;
        }
    }
    else if (ch >= '0' && ch <= '9')
        value = ch - '0';
    else if (ch >= 'a' && ch <= 'f')
        value = ch - 'a' + 10;
    else if (cursorColumn == TrackerEngine::effectColumn && ch >= 'g' && ch <= 'z')
        value = ch - 'a' + 10;
    if (value < 0)
        return false;

    const auto changed = engine.writeDigit (patternIndex(), cursorRow, cursorChannel, cursorColumn, value);
    if (! changed)
        Q_EMIT message (cursorColumn == TrackerEngine::effectColumn ? tr ("%1 is not a FastTracker 2 effect").arg (t.toUpper())
                                                                    : tr ("That value does not fit this column"));
    else
        advance();
    afterEdit (changed);
    return true;
}

void PatternView::keyPressEvent (QKeyEvent* e)
{
    if (! engine.isLoaded())
    {
        QWidget::keyPressEvent (e);
        return;
    }
    const int n = rows(), channels = engine.numChannels();
    const auto mods = e->modifiers() & ~Qt::KeypadModifier;
    const bool ctrl = mods.testFlag (Qt::ControlModifier), shift = mods.testFlag (Qt::ShiftModifier), alt = mods.testFlag (Qt::AltModifier);
    const int index = patternIndex();

    auto moveTo = [&] (int row, int channel, int column, bool extend)
    {
        if (extend && ! selecting)
        {
            selecting = true;
            anchorRow = cursorRow;
            anchorChannel = cursorChannel;
        }
        else if (! extend)
            selecting = false;
        cursorRow = std::clamp (row, 0, std::max (0, n - 1));
        cursorChannel = std::clamp (channel, 0, std::max (0, channels - 1));
        cursorColumn = std::clamp (column, 0, TrackerEngine::numColumns - 1);
        keepChannelVisible();
        announce();
        update();
    };

    // Undo, the clipboard, the edit step and the order position.
    if (ctrl && ! alt)
    {
        int r0 = cursorRow, r1 = cursorRow, c0 = cursorChannel, c1 = cursorChannel;
        if (selecting)
            selectionBounds (r0, r1, c0, c1);
        switch (e->key())
        {
            case Qt::Key_Z:
                if (shift ? engine.redo() : engine.undo())
                    afterEdit (true);
                return;
            case Qt::Key_Y:
                if (engine.redo())
                    afterEdit (true);
                return;
            case Qt::Key_C:
            case Qt::Key_X:
                clipboard = engine.copyBlock (index, r0, r1, c0, c1);
                if (e->key() == Qt::Key_X && editMode)
                    afterEdit (engine.clearBlock (index, r0, r1, c0, c1));
                Q_EMIT message (tr ("%n row(s) copied", nullptr, clipboard.rows));
                return;
            case Qt::Key_V:
                if (! editMode)
                    Q_EMIT message (tr ("Turn on editing (Shift+Space) to paste"));
                else
                    afterEdit (engine.pasteBlock (index, cursorRow, cursorChannel, clipboard));
                return;
            case Qt::Key_A:
                selecting = true;
                anchorRow = 0;
                anchorChannel = shift ? 0 : cursorChannel;
                cursorRow = std::max (0, n - 1);
                if (shift)
                    cursorChannel = std::max (0, channels - 1);
                update();
                return;
            case Qt::Key_Up:
            case Qt::Key_Down:
                setEditStep (step + (e->key() == Qt::Key_Up ? 1 : -1));
                Q_EMIT message (tr ("Edit step %1").arg (step));
                return;
            case Qt::Key_Left:
                Q_EMIT orderStep (-1);
                return;
            case Qt::Key_Right:
                Q_EMIT orderStep (1);
                return;
            default:
                break;
        }
    }

    // Alt: mute, solo and track (plain M, S and T are notes while editing); Alt+Up/Down transposes.
    if (alt && ! ctrl)
    {
        int r0 = cursorRow, r1 = cursorRow, c0 = cursorChannel, c1 = cursorChannel;
        if (selecting)
            selectionBounds (r0, r1, c0, c1);
        switch (e->key())
        {
            case Qt::Key_M: engine.setMuted (cursorChannel, ! engine.isMuted (cursorChannel)); Q_EMIT channelsChanged(); update(); return;
            case Qt::Key_S: engine.setSoloed (cursorChannel, ! engine.isSoloed (cursorChannel)); Q_EMIT channelsChanged(); update(); return;
            case Qt::Key_T:
                engine.setTrack (cursorChannel, (engine.track (cursorChannel) + (shift ? TrackerEngine::numTracks - 1 : 1)) % TrackerEngine::numTracks);
                Q_EMIT channelsChanged();
                update();
                return;
            case Qt::Key_Up:
            case Qt::Key_Down:
                if (editMode)
                    afterEdit (engine.transpose (index, r0, r1, c0, c1, (e->key() == Qt::Key_Up ? 1 : -1) * (shift ? 12 : 1)));
                return;
            default:
                break;
        }
    }

    // F1 to F8: the octave, as in FastTracker 2.
    if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F8 && ! ctrl && ! alt)
    {
        setOctave (e->key() - Qt::Key_F1);
        Q_EMIT message (tr ("Octave %1").arg (baseOctave));
        return;
    }

    switch (e->key())
    {
        case Qt::Key_Up:
            if (! shift && cursorRow == 0)
            {
                Q_EMIT orderStep (-1);
                moveTo (rows() - 1, cursorChannel, cursorColumn, false);
            }
            else
                moveTo (cursorRow - 1, cursorChannel, cursorColumn, shift);
            return;
        case Qt::Key_Down:
            if (! shift && cursorRow >= n - 1)
            {
                Q_EMIT orderStep (1);
                moveTo (0, cursorChannel, cursorColumn, false);
            }
            else
                moveTo (cursorRow + 1, cursorChannel, cursorColumn, shift);
            return;
        case Qt::Key_PageUp:   moveTo (cursorRow - 16, cursorChannel, cursorColumn, shift); return;
        case Qt::Key_PageDown: moveTo (cursorRow + 16, cursorChannel, cursorColumn, shift); return;
        case Qt::Key_Home:     moveTo (0, cursorChannel, cursorColumn, shift); return;
        case Qt::Key_End:      moveTo (n - 1, cursorChannel, cursorColumn, shift); return;
        case Qt::Key_Left:
            if (alt)
                break;
            if (shift)
                moveTo (cursorRow, cursorChannel - 1, cursorColumn, true);
            else if (cursorColumn > 0)
                moveTo (cursorRow, cursorChannel, cursorColumn - 1, false);
            else if (cursorChannel > 0)
                moveTo (cursorRow, cursorChannel - 1, TrackerEngine::numColumns - 1, false);
            return;
        case Qt::Key_Right:
            if (alt)
                break;
            if (shift)
                moveTo (cursorRow, cursorChannel + 1, cursorColumn, true);
            else if (cursorColumn < TrackerEngine::numColumns - 1)
                moveTo (cursorRow, cursorChannel, cursorColumn + 1, false);
            else if (cursorChannel < channels - 1)
                moveTo (cursorRow, cursorChannel + 1, 0, false);
            return;
        case Qt::Key_Tab:     moveTo (cursorRow, (cursorChannel + 1) % std::max (1, channels), 0, false); return;
        case Qt::Key_Backtab: moveTo (cursorRow, (cursorChannel + channels - 1) % std::max (1, channels), 0, false); return;
        case Qt::Key_Escape:
            if (selecting)
            {
                selecting = false;
                update();
                return;
            }
            break;
        default:
            break;
    }

    if (editMode && ! ctrl && ! alt)
    {
        switch (e->key())
        {
            case Qt::Key_Delete:
                if (selecting)
                {
                    int r0, r1, c0, c1;
                    selectionBounds (r0, r1, c0, c1);
                    afterEdit (engine.clearBlock (index, r0, r1, c0, c1));
                    return;
                }
                {
                    const auto changed = shift ? engine.clearCell (index, cursorRow, cursorChannel)
                                               : engine.clearAt (index, cursorRow, cursorChannel, cursorColumn);
                    advance();
                    afterEdit (changed);
                }
                return;
            case Qt::Key_Insert:
                afterEdit (engine.insertRow (index, cursorRow, shift ? -1 : cursorChannel));
                return;
            case Qt::Key_Backspace:
                if (cursorRow > 0)
                {
                    --cursorRow;
                    afterEdit (engine.deleteRow (index, cursorRow, shift ? -1 : cursorChannel));
                }
                return;
            default:
                break;
        }
    }

    // Plain M, S and T stay commands when not editing; F (Follow) goes to the window.
    if (! editMode && ! ctrl && ! alt)
        switch (e->key())
        {
            case Qt::Key_M: engine.setMuted (cursorChannel, ! engine.isMuted (cursorChannel)); Q_EMIT channelsChanged(); update(); return;
            case Qt::Key_S: engine.setSoloed (cursorChannel, ! engine.isSoloed (cursorChannel)); Q_EMIT channelsChanged(); update(); return;
            case Qt::Key_T:
                engine.setTrack (cursorChannel, (engine.track (cursorChannel) + (shift ? TrackerEngine::numTracks - 1 : 1)) % TrackerEngine::numTracks);
                Q_EMIT channelsChanged();
                update();
                return;
            case Qt::Key_F:
                QWidget::keyPressEvent (e);
                return;
            default:
                break;
        }

    // Notes: written while editing at the note column, heard either way.
    if (! ctrl && ! alt && e->key() != Qt::Key_Space && (! editMode || cursorColumn == TrackerEngine::noteColumn))
    {
        const int note = noteForKey (e);
        if (note > 0)
        {
            if (! e->isAutoRepeat())
            {
                Q_EMIT notePreview (cursorChannel, note, currentInstrument);
                if (std::find (heldKeys.begin(), heldKeys.end(), scanCodeOf (e)) == heldKeys.end())
                    heldKeys.push_back (scanCodeOf (e));
            }
            if (editMode)
            {
                const auto changed = engine.writeNote (index, cursorRow, cursorChannel, note, currentInstrument);
                if (changed)
                    advance();
                afterEdit (changed);
            }
            else
                Q_EMIT message (tr ("%1 (Shift+Space to write notes)").arg (noteName (note)));
            return;
        }
    }

    if (editMode && ! ctrl && ! alt && cursorColumn != TrackerEngine::noteColumn && typeDigit (e))
        return;

    QWidget::keyPressEvent (e);
}

void PatternView::keyReleaseEvent (QKeyEvent* e)
{
    const auto at = std::find (heldKeys.begin(), heldKeys.end(), scanCodeOf (e));
    if (! e->isAutoRepeat() && at != heldKeys.end())
    {
        heldKeys.erase (at);
        if (heldKeys.empty())
            Q_EMIT noteRelease (cursorChannel, currentInstrument);
        return;
    }
    QWidget::keyReleaseEvent (e);
}

void PatternView::mousePressEvent (QMouseEvent* e)
{
    setFocus();
    const int numberWidth = rowNumberWidth(), colWidth = columnWidth();
    const auto pos = e->position().toPoint();
    if (pos.x() < numberWidth)
        return;
    const int ch = firstChannel + (pos.x() - numberWidth) / colWidth;
    if (ch >= engine.numChannels())
        return;
    cursorChannel = ch;

    if (pos.y() < headerHeight)
    {
        // The header's boxes: M, S, then the track (left click next, right click previous).
        const int cw = QFontMetrics (font()).horizontalAdvance (QLatin1Char ('0'));
        const int x = pos.x() - (numberWidth + (ch - firstChannel) * colWidth) - 6, boxW = cw * 3 + 8;
        if (pos.y() > headerHeight / 2 - 4)
        {
            if (x >= 0 && x < boxW)
                engine.setMuted (ch, ! engine.isMuted (ch));
            else if (x >= boxW && x < 2 * boxW)
                engine.setSoloed (ch, ! engine.isSoloed (ch));
            else if (x >= 2 * boxW && x < 3 * boxW + cw)
                engine.setTrack (ch, (engine.track (ch) + (e->button() == Qt::RightButton ? TrackerEngine::numTracks - 1 : 1)) % TrackerEngine::numTracks);
            Q_EMIT channelsChanged();
        }
    }
    else
    {
        const int cw = QFontMetrics (font()).horizontalAdvance (QLatin1Char ('0'));
        const int charInCell = (pos.x() - numberWidth - (ch - firstChannel) * colWidth - 4) / std::max (1, cw);
        cursorColumn = 0;
        for (int c = 0; c < TrackerEngine::numColumns; ++c)
            if (charInCell >= columnChar[c])
                cursorColumn = c;
        cursorRow = rowAt (pos.y());
        selecting = false;
        anchorRow = cursorRow;
        anchorChannel = cursorChannel;
    }
    announce();
    update();
}

void PatternView::mouseMoveEvent (QMouseEvent* e)
{
    if (! e->buttons().testFlag (Qt::LeftButton) || e->position().y() < headerHeight)
        return;
    const int numberWidth = rowNumberWidth(), colWidth = columnWidth();
    const int ch = std::clamp (firstChannel + (int) (e->position().x() - numberWidth) / colWidth, 0, std::max (0, engine.numChannels() - 1));
    const int row = rowAt ((int) e->position().y());
    if (row != anchorRow || ch != anchorChannel)
        selecting = true;
    cursorRow = row;
    cursorChannel = ch;
    update();
}

void PatternView::wheelEvent (QWheelEvent* e)
{
    cursorRow = std::clamp (cursorRow - e->angleDelta().y() / 40, 0, std::max (0, rows() - 1));
    update();
}

//==============================================================================
TrackerWindow::TrackerWindow (TrackerEngine& e, QWidget* parent) : QWidget (parent, Qt::Window), engine (e)
{
    setWindowTitle (tr ("Tracker"));
    setAccessibleName (tr ("Tracker"));
    Theme::makeTranslucent (this);   // the same theme and window opacity as the rest of PHINYX
    resize (1180, 720);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (8, 8, 8, 8);
    layout->setSpacing (6);

    // Files and transport.
    auto* bar = new QHBoxLayout;
    auto* newSong = new QPushButton (tr ("&New"), this);
    auto* open = new QPushButton (tr ("&Open..."), this);
    auto* save = new QPushButton (tr ("Sa&ve"), this);
    auto* saveAs = new QPushButton (tr ("Save &As..."), this);
    playSong = new QPushButton (tr ("Play &Song"), this);
    auto* playPattern = new QPushButton (tr ("Play &Pattern"), this);
    stop = new QPushButton (tr ("S&top"), this);
    follow = new QCheckBox (tr ("&Follow"), this);
    follow->setChecked (true);
    follow->setToolTip (tr ("The pattern view follows what is playing (F)"));
    edit = new QCheckBox (tr ("&Edit"), this);
    edit->setToolTip (tr ("Editing: the keyboard writes notes into the pattern (Shift+Space)"));
    auto* record = new QToolButton (this);
    record->setText (tr ("&Record to Editor"));
    record->setPopupMode (QToolButton::InstantPopup);
    auto* menu = new QMenu (record);
    menu->addAction (tr ("Stereo"), this, [this] { Q_EMIT recordRequested (false); });
    menu->addAction (tr ("Multi-Channel (a stereo pair per track)"), this, [this] { Q_EMIT recordRequested (true); });
    record->setMenu (menu);
    for (QWidget* w : std::initializer_list<QWidget*> { newSong, open, save, saveAs, playSong, playPattern, stop, follow, edit, record })
    {
        w->setFocusPolicy (Qt::TabFocus);
        bar->addWidget (w);
    }
    bar->addStretch (1);
    title = new QLabel (this);
    title->setTextFormat (Qt::PlainText);
    bar->addWidget (title);
    layout->addLayout (bar);

    // The song and the edit settings.
    auto* song = new QHBoxLayout;
    auto addField = [&] (const QString& label, QWidget* field)
    {
        auto* l = new QLabel (label, this);
        l->setBuddy (field);
        song->addWidget (l);
        song->addWidget (field);
    };
    auto spin = [this] (int lo, int hi, const QString& name)
    {
        auto* s = new QSpinBox (this);
        s->setRange (lo, hi);
        s->setAccessibleName (name);
        s->setKeyboardTracking (false);
        return s;
    };
    songTitle = new QLineEdit (this);
    songTitle->setMaxLength (20);
    songTitle->setAccessibleName (tr ("Song title"));
    songTitle->setMinimumWidth (160);
    bpm = spin (32, 255, tr ("Tempo in BPM"));
    speed = spin (1, 31, tr ("Speed in ticks per row"));
    channels = spin (2, TrackerEngine::maxChannels, tr ("Channels"));
    channels->setSingleStep (2);
    patternRows = spin (1, 256, tr ("Rows in this pattern"));
    octave = spin (0, 7, tr ("Octave"));
    stepBox = spin (0, 16, tr ("Edit step"));
    addField (tr ("T&itle:"), songTitle);
    addField (tr ("&BPM:"), bpm);
    addField (tr ("Spee&d:"), speed);
    addField (tr ("C&hannels:"), channels);
    addField (tr ("Ro&ws:"), patternRows);
    addField (tr ("Octa&ve:"), octave);
    addField (tr ("Ste&p:"), stepBox);
    song->addStretch (1);
    layout->addLayout (song);

    connect (newSong, &QPushButton::clicked, this, &TrackerWindow::newRequested);
    connect (open, &QPushButton::clicked, this, &TrackerWindow::openRequested);
    connect (save, &QPushButton::clicked, this, [this] { Q_EMIT saveRequested (false); });
    connect (saveAs, &QPushButton::clicked, this, [this] { Q_EMIT saveRequested (true); });
    connect (playSong, &QPushButton::clicked, this, &TrackerWindow::playSongRequested);
    connect (playPattern, &QPushButton::clicked, this, &TrackerWindow::playPatternRequested);
    connect (stop, &QPushButton::clicked, this, &TrackerWindow::stopRequested);
    connect (edit, &QCheckBox::toggled, this, [this] (bool on)
    {
        if (on && ! ensureEditable())
        {
            const QSignalBlocker b (edit);
            edit->setChecked (false);
            return;
        }
        pattern->setEditing (on);
        pattern->setFocus();
    });

    auto songEdit = [this] (auto fn)
    {
        return [this, fn] (int value)
        {
            if (syncing || ! engine.isLoaded() || ! ensureEditable())
                return;
            fn (value);
            songChanged();
        };
    };
    connect (songTitle, &QLineEdit::editingFinished, this, [this]
    {
        if (! syncing && engine.isLoaded() && QString::fromStdString (engine.title()) != songTitle->text() && ensureEditable())
        {
            engine.setTitle (songTitle->text().toStdString());
            songChanged();
        }
    });
    connect (bpm, &QSpinBox::valueChanged, this, songEdit ([this] (int v) { engine.setInitialTempo (v); }));
    connect (speed, &QSpinBox::valueChanged, this, songEdit ([this] (int v) { engine.setInitialSpeed (v); }));
    connect (channels, &QSpinBox::valueChanged, this, songEdit ([this] (int v) { engine.setNumChannels (v); pattern->update(); }));
    connect (patternRows, &QSpinBox::valueChanged, this, songEdit ([this] (int v)
    {
        engine.setPatternRows (engine.patternAt (pattern->order()), v);
        pattern->setRow (pattern->row());
    }));
    connect (octave, &QSpinBox::valueChanged, this, [this] (int v) { if (! syncing) pattern->setOctave (v); });
    connect (stepBox, &QSpinBox::valueChanged, this, [this] (int v) { if (! syncing) pattern->setEditStep (v); });

    auto smallButton = [this] (const QString& text, const QString& tip)
    {
        auto* b = new QToolButton (this);
        b->setText (text);
        b->setToolTip (tip);
        b->setFocusPolicy (Qt::NoFocus);
        return b;
    };

    // Order list, pattern, instruments.
    auto* split = new QSplitter (Qt::Horizontal, this);

    auto* orderSide = new QWidget (split);
    auto* orderLayout = new QVBoxLayout (orderSide);
    orderLayout->setContentsMargins (0, 0, 0, 0);
    orders = new QListWidget (orderSide);
    orders->setAccessibleName (tr ("Order list"));
    orders->setAccessibleDescription (tr ("Insert repeats this pattern, Shift+Insert adds a new pattern, Delete removes the position, "
                                          "plus and minus change its pattern"));
    orders->setFont (trackerFont());
    orders->installEventFilter (this);
    orderLayout->addWidget (orders, 1);
    auto* orderButtons = new QHBoxLayout;
    auto* insButton = smallButton (tr ("Ins"), tr ("Insert a position that repeats this pattern (Insert)"));
    auto* seqButton = smallButton (tr ("New"), tr ("Insert a position with a new, empty pattern (Shift+Insert)"));
    auto* delButton = smallButton (tr ("Del"), tr ("Remove this position (Delete)"));
    auto* minusButton = smallButton (QStringLiteral ("−"), tr ("Play the previous pattern here (minus)"));
    auto* plusButton = smallButton (QStringLiteral ("+"), tr ("Play the next pattern here (plus)"));
    for (auto* b : { insButton, seqButton, delButton, minusButton, plusButton })
        orderButtons->addWidget (b);
    orderLayout->addLayout (orderButtons);
    connect (insButton, &QToolButton::clicked, this, [this] { orderInsert (false); });
    connect (seqButton, &QToolButton::clicked, this, [this] { orderInsert (true); });
    connect (delButton, &QToolButton::clicked, this, [this] { orderDelete(); });
    connect (minusButton, &QToolButton::clicked, this, [this] { orderChangePattern (-1); });
    connect (plusButton, &QToolButton::clicked, this, [this] { orderChangePattern (1); });
    orderSide->setMaximumWidth (200);

    pattern = new PatternView (engine, split);

    auto* instrumentSide = new QWidget (split);
    auto* instrumentLayout = new QVBoxLayout (instrumentSide);
    instrumentLayout->setContentsMargins (0, 0, 0, 0);
    instruments = new QListWidget (instrumentSide);
    instruments->setAccessibleName (tr ("Instruments"));
    instruments->setAccessibleDescription (tr ("The instrument new notes use. Enter loads a sample into it, F2 renames it"));
    instruments->setFont (trackerFont());
    instruments->installEventFilter (this);
    instrumentLayout->addWidget (instruments, 1);
    auto* instrumentButtons = new QHBoxLayout;
    auto* loadButton = smallButton (tr ("Load Sample..."), tr ("Load an audio file into this instrument (Ctrl+L)"));
    auto* renameButton = smallButton (tr ("Rename..."), tr ("Rename this instrument (F2)"));
    instrumentButtons->addWidget (loadButton);
    instrumentButtons->addWidget (renameButton);
    instrumentButtons->addStretch (1);
    instrumentLayout->addLayout (instrumentButtons);
    connect (loadButton, &QToolButton::clicked, this, [this] { Q_EMIT loadSampleRequested (currentInstrument()); });
    connect (renameButton, &QToolButton::clicked, this, [this] { renameInstrument(); });
    instrumentSide->setMaximumWidth (280);

    split->addWidget (orderSide);
    split->addWidget (pattern);
    split->addWidget (instrumentSide);
    split->setStretchFactor (1, 1);
    layout->addWidget (split, 1);

    status = new QLabel (this);
    status->setWordWrap (true);
    layout->addWidget (status);

    connect (orders, &QListWidget::currentRowChanged, this, [this] (int row) { if (row >= 0) showOrder (row); });
    connect (instruments, &QListWidget::currentRowChanged, this, [this] (int row)
    {
        if (row >= 0 && ! syncing)
            pattern->setInstrument (row + 1);
    });
    connect (instruments, &QListWidget::itemActivated, this, [this] { Q_EMIT loadSampleRequested (currentInstrument()); });
    connect (pattern, &PatternView::orderStep, this, [this] (int step)
    {
        const int next = std::clamp (pattern->order() + step, 0, std::max (0, engine.numOrders() - 1));
        orders->setCurrentRow (next);
    });
    connect (pattern, &PatternView::channelsChanged, this, [this] { refresh(); });
    connect (pattern, &PatternView::edited, this, [this] { songChanged(); });
    connect (pattern, &PatternView::message, this, [this] (const QString& text) { status->setText (text); });
    connect (pattern, &PatternView::notePreview, this, &TrackerWindow::notePreview);
    connect (pattern, &PatternView::noteRelease, this, &TrackerWindow::noteRelease);
    connect (pattern, &PatternView::settingsChanged, this, [this] { syncSettings(); refresh(); });
    connect (pattern, &PatternView::editingChanged, this, [this] (bool on)
    {
        if (on && ! ensureEditable())
        {
            pattern->setEditing (false);
            return;
        }
        const QSignalBlocker b (edit);
        edit->setChecked (on);
        refresh();
    });

    moduleChanged();
}

int TrackerWindow::currentInstrument() const { return std::max (1, instruments->currentRow() + 1); }

bool TrackerWindow::ensureEditable()
{
    if (! engine.isLoaded())
    {
        Q_EMIT newRequested();
        return engine.isLoaded();
    }
    if (engine.isEditable())
        return true;
    if (QMessageBox::question (this, tr ("Edit Module"),
                               tr ("This module is %1. To edit it, the Tracker converts it to FastTracker 2 (XM), as MilkyTracker does; "
                                   "Save then writes an XM file and the original stays as it is. Convert it?")
                                   .arg (QString::fromStdString (engine.formatName())))
        != QMessageBox::Yes)
        return false;
    Q_EMIT stopRequested();
    const auto error = engine.makeEditable();
    if (! error.empty())
    {
        QMessageBox::warning (this, tr ("Edit Module"), QString::fromStdString (error));
        return false;
    }
    const auto order = pattern->order(), row = pattern->row();
    moduleChanged();
    showOrder (order);
    pattern->setRow (row);
    return true;
}

void TrackerWindow::fillOrders()
{
    const QSignalBlocker b (orders);
    const int row = orders->currentRow();
    orders->clear();
    for (int i = 0; i < engine.numOrders(); ++i)
        orders->addItem (QStringLiteral ("%1  %2").arg (hex2 (i), hex2 (engine.patternAt (i))));
    orders->setCurrentRow (std::clamp (row < 0 ? 0 : row, 0, std::max (0, engine.numOrders() - 1)));
}

void TrackerWindow::fillInstruments()
{
    const QSignalBlocker b (instruments);
    const int row = instruments->currentRow();
    instruments->clear();
    const auto names = engine.instrumentNames();
    for (size_t i = 0; i < names.size(); ++i)
        instruments->addItem (QStringLiteral ("%1 %2 %3").arg (hex2 ((int) i + 1), engine.hasSample ((int) i + 1) ? QStringLiteral ("●") : QStringLiteral (" "),
                                                               QString::fromStdString (names[i])));
    instruments->setCurrentRow (std::clamp (row < 0 ? pattern->instrument() - 1 : row, 0, std::max (0, instruments->count() - 1)));
}

void TrackerWindow::syncSettings()
{
    syncing = true;
    if (! songTitle->hasFocus())
        songTitle->setText (QString::fromStdString (engine.title()));
    bpm->setValue (engine.initialTempo());
    speed->setValue (engine.initialSpeed());
    channels->setValue (engine.numChannels());
    const auto* p = engine.pattern (engine.patternAt (pattern->order()));
    patternRows->setValue (p != nullptr ? p->rows : 64);
    octave->setValue (pattern->octave());
    stepBox->setValue (pattern->editStep());
    if (instruments->currentRow() != pattern->instrument() - 1 && pattern->instrument() - 1 < instruments->count())
        instruments->setCurrentRow (pattern->instrument() - 1);
    for (auto* w : std::initializer_list<QWidget*> { songTitle, bpm, speed, channels, patternRows })
        w->setEnabled (engine.isLoaded());
    syncing = false;
}

void TrackerWindow::moduleChanged()
{
    pattern->moduleChanged();
    fillOrders();
    fillInstruments();
    syncSettings();
    if (! engine.isLoaded())
    {
        title->setText (tr ("No song"));
        setWindowTitle (tr ("Tracker"));
        status->setText (tr ("Ctrl+N starts a new song and Ctrl+O opens a module. Space plays the song, Enter the pattern, "
                             "Shift+Space turns on editing."));
        return;
    }
    songChanged();
    refresh();
}

void TrackerWindow::songChanged()
{
    if (engine.numOrders() != orders->count())
        fillOrders();
    else
        for (int i = 0; i < engine.numOrders(); ++i)
            orders->item (i)->setText (QStringLiteral ("%1  %2").arg (hex2 (i), hex2 (engine.patternAt (i))));
    fillInstruments();
    syncSettings();
    auto name = QString::fromStdString (engine.title());
    if (name.isEmpty() && ! engine.path().empty())
        name = QFileInfo (QString::fromStdString (engine.path())).fileName();
    const auto shown = name.isEmpty() ? tr ("(untitled)") : name;
    title->setText (tr ("%1   %2, %3 channels").arg (shown, QString::fromStdString (engine.formatName())).arg (engine.numChannels()));
    setWindowTitle (tr ("Tracker: %1%2").arg (engine.isModified() ? QStringLiteral ("*") : QString(), shown));
    pattern->update();
}

void TrackerWindow::showOrder (int order)
{
    pattern->setOrder (order);
    if (orders->currentRow() != order)
        orders->setCurrentRow (order);
    syncSettings();
}

void TrackerWindow::orderInsert (bool newPattern)
{
    if (! ensureEditable())
        return;
    const int at = std::max (0, orders->currentRow());
    int index = engine.patternAt (at);
    if (newPattern)
    {
        index = engine.unusedPattern();
        if (index < 0)
        {
            status->setText (tr ("All 256 patterns are in use"));
            return;
        }
    }
    if (engine.insertOrder (at + 1, index))
    {
        songChanged();
        orders->setCurrentRow (at + 1);
    }
}

void TrackerWindow::orderDelete()
{
    if (! ensureEditable())
        return;
    if (engine.deleteOrder (orders->currentRow()))
    {
        songChanged();
        showOrder (std::min (orders->currentRow(), engine.numOrders() - 1));
    }
}

void TrackerWindow::orderChangePattern (int by)
{
    if (! ensureEditable())
        return;
    const int at = std::max (0, orders->currentRow());
    const int next = std::clamp (engine.patternAt (at) + by, 0, TrackerEngine::maxPatterns - 1);
    if (engine.setOrderPattern (at, next))
    {
        songChanged();
        pattern->update();
    }
}

void TrackerWindow::renameInstrument()
{
    if (! engine.isLoaded() || ! ensureEditable())
        return;
    const int i = currentInstrument();
    const auto names = engine.instrumentNames();
    bool ok = false;
    const auto name = QInputDialog::getText (this, tr ("Rename Instrument"), tr ("Instrument %1:").arg (hex2 (i)), QLineEdit::Normal,
                                             i - 1 < (int) names.size() ? QString::fromStdString (names[(size_t) i - 1]) : QString(), &ok);
    if (! ok)
        return;
    engine.setInstrumentName (i, name.left (22).toStdString());
    fillInstruments();
    songChanged();
}

bool TrackerWindow::eventFilter (QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter (watched, event);
    auto* key = static_cast<QKeyEvent*> (event);
    const bool shift = key->modifiers().testFlag (Qt::ShiftModifier);
    if (watched == orders)
        switch (key->key())
        {
            case Qt::Key_Insert: orderInsert (shift); return true;
            case Qt::Key_Delete: orderDelete(); return true;
            case Qt::Key_Plus: orderChangePattern (1); return true;
            case Qt::Key_Minus: orderChangePattern (-1); return true;
            default: break;
        }
    if (watched == instruments && key->key() == Qt::Key_F2)
    {
        renameInstrument();
        return true;
    }
    return QWidget::eventFilter (watched, event);
}

bool TrackerWindow::following() const { return follow->isChecked(); }
void TrackerWindow::setFollowing (bool on) { follow->setChecked (on); }

void TrackerWindow::refresh()
{
    if (! engine.isLoaded())
        return;
    const bool playing = engine.isPlaying();
    int order = 0, row = 0;
    if (playing)
    {
        engine.position (order, row);
        if (follow->isChecked() && order != pattern->order() && order >= 0 && order < engine.numOrders())
            showOrder (order);
        pattern->setPlayRow (order == pattern->order() ? row : -1);
    }
    else
        pattern->setPlayRow (-1);

    int muted = 0;
    for (int c = 0; c < engine.numChannels(); ++c)
        muted += engine.isHeard (c) ? 0 : 1;
    const auto where = playing ? tr ("Playing order %1, row %2, %3 BPM, speed %4").arg (hex2 (order), hexRow (row)).arg (engine.tempo()).arg (engine.speed())
                               : tr ("Order %1, row %2").arg (hex2 (pattern->order()), hexRow (pattern->row()));
    const auto keys = pattern->isEditing()
                          ? tr ("EDITING: the keyboard is a piano (F1-F8 octave), hex digits in the other columns, Delete clears, "
                                "Insert/Backspace push and pull rows, Ctrl+Z undo, Shift+arrows select, Ctrl+C/X/V, Alt+Up/Down transpose. "
                                "Shift+Space stops editing.")
                          : tr ("Space: play / stop, Enter: play pattern, Shift+Space: edit, M / S / T: mute, solo, track, "
                                "Ctrl+R: record to the editor.");
    status->setText (tr ("%1.   Octave %2, step %3, instrument %4.   %5 of %6 channels heard.   %7")
                         .arg (where).arg (pattern->octave()).arg (pattern->editStep()).arg (hex2 (pattern->instrument()))
                         .arg (engine.numChannels() - muted).arg (engine.numChannels()).arg (keys));
}

void TrackerWindow::keyPressEvent (QKeyEvent* e)
{
    const bool ctrl = e->modifiers().testFlag (Qt::ControlModifier), shift = e->modifiers().testFlag (Qt::ShiftModifier);
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier)
    {
        Q_EMIT engine.isPlaying() ? stopRequested() : playSongRequested();
        return;
    }
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::ShiftModifier)
    {
        edit->toggle();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && e->modifiers() == Qt::NoModifier)
    {
        Q_EMIT playPatternRequested();
        return;
    }
    if (ctrl)
        switch (e->key())
        {
            case Qt::Key_N: Q_EMIT newRequested(); return;
            case Qt::Key_O: Q_EMIT openRequested(); return;
            case Qt::Key_S: Q_EMIT saveRequested (shift); return;
            case Qt::Key_L: Q_EMIT loadSampleRequested (currentInstrument()); return;
            case Qt::Key_R: Q_EMIT recordRequested (shift); return;
            default: break;
        }
    if (e->key() == Qt::Key_F && e->modifiers() == Qt::NoModifier)
    {
        follow->toggle();
        return;
    }
    if (e->key() == Qt::Key_Escape)
    {
        close();
        return;
    }
    QWidget::keyPressEvent (e);
}

void TrackerWindow::closeEvent (QCloseEvent* e)
{
    Q_EMIT closed();
    QWidget::closeEvent (e);
}
