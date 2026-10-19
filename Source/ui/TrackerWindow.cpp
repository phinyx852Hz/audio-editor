// SPDX-License-Identifier: AGPL-3.0-only
// The Tracker window: MilkyTracker's module player (MilkyPlay) with a pattern view in PHINYX.
#include "TrackerWindow.h"

#include "../tracker/PatternText.h"
#include "../tracker/TrackerEngine.h"
#include "Theme.h"

#include "XModule.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

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
    currentOrder = cursorRow = cursorChannel = firstChannel = 0;
    playRow = -1;
    update();
}

int PatternView::rows() const
{
    const auto* p = engine.pattern (engine.patternAt (currentOrder));
    return p != nullptr ? p->rows : 0;
}

void PatternView::setOrder (int order)
{
    if (order == currentOrder)
        return;
    currentOrder = std::clamp (order, 0, std::max (0, engine.numOrders() - 1));
    cursorRow = std::clamp (cursorRow, 0, std::max (0, rows() - 1));
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

int PatternView::columnWidth() const { return QFontMetrics (font()).horizontalAdvance (QLatin1Char ('0')) * 14 + 6; }
int PatternView::rowNumberWidth() const { return QFontMetrics (font()).horizontalAdvance (QLatin1Char ('0')) * 3 + 8; }

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
    const auto* p = engine.pattern (engine.patternAt (currentOrder));
    if (p == nullptr || cursorChannel >= p->channum)
        return;
    const auto c = PatternText::cell (*p, cursorRow, cursorChannel);
    setAccessibleDescription (tr ("Row %1, channel %2: %3 %4 %5 %6%7")
                                  .arg (cursorRow).arg (cursorChannel + 1)
                                  .arg (QString::fromStdString (c.note), QString::fromStdString (c.instrument),
                                        QString::fromStdString (c.volume), QString::fromStdString (c.effect),
                                        QString::fromStdString (c.operand)));
}

void PatternView::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.fillRect (rect(), Theme::withOpacity (colour ("waveformBackground")));
    const QFontMetrics fm (font());
    const int cw = fm.horizontalAdvance (QLatin1Char ('0'));
    const int lineHeight = fm.height() + 2;
    const auto text = colour ("text"), dim = colour ("placeholderText"), accent = palette().color (QPalette::Link);

    if (! engine.isLoaded())
    {
        g.setPen (dim);
        g.drawText (rect().adjusted (16, 0, -16, 0), Qt::AlignCenter | Qt::TextWordWrap, tr ("Open a module with Ctrl+O: XM, MOD, IT, S3M and the other formats MilkyTracker reads."));
        return;
    }

    const auto* p = engine.pattern (engine.patternAt (currentOrder));
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
        g.setPen (ch == cursorChannel && hasFocus() ? accent : heard ? text : dim);
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
        g.drawText (rect().adjusted (0, headerHeight, 0, 0), Qt::AlignCenter, tr ("This order position has an empty pattern."));
        return;
    }

    // Rows, with the cursor's (or the playing) row held in the middle.
    const int focusRow = playRow >= 0 ? playRow : cursorRow;
    const int area = height() - headerHeight;
    const int middle = headerHeight + area / 2 - lineHeight / 2;
    const int first = std::max (0, focusRow - (middle - headerHeight) / lineHeight - 1);
    for (int r = first; r < p->rows; ++r)
    {
        const int y = middle + (r - focusRow) * lineHeight;
        if (y > height())
            break;
        if (y + lineHeight < headerHeight)
            continue;

        const QRect line (0, y, width(), lineHeight);
        if (r == playRow)
            g.fillRect (line, Theme::withOpacity (palette().color (QPalette::Highlight)).darker (140));
        else if (r % 16 == 0)
            g.fillRect (line, Theme::withOpacity (palette().color (QPalette::AlternateBase)).lighter (130));
        else if (r % 4 == 0)
            g.fillRect (line, Theme::withOpacity (palette().color (QPalette::AlternateBase)));

        g.setPen (r % 4 == 0 ? text : dim);
        g.drawText (QRect (4, y, numberWidth - 8, lineHeight), Qt::AlignRight | Qt::AlignVCenter, hexRow (r));

        for (int i = 0; i < visibleChannels && firstChannel + i < p->channum; ++i)
        {
            const int ch = firstChannel + i;
            const auto c = PatternText::cell (*p, r, ch);
            const bool heard = engine.isHeard (ch);
            int x = numberWidth + i * colWidth + 4;
            if (r == cursorRow && ch == cursorChannel)
            {
                g.setPen (QPen (hasFocus() ? accent : dim, 1.5));
                g.drawRect (QRectF (x - 3, y, colWidth - 4, lineHeight - 1));
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
}

void PatternView::keyPressEvent (QKeyEvent* e)
{
    if (! engine.isLoaded())
    {
        QWidget::keyPressEvent (e);
        return;
    }
    const int n = rows(), channels = engine.numChannels();
    const bool ctrl = e->modifiers().testFlag (Qt::ControlModifier), shift = e->modifiers().testFlag (Qt::ShiftModifier);
    switch (e->key())
    {
        case Qt::Key_Up:
            if (cursorRow == 0)
            {
                Q_EMIT orderStep (-1);
                cursorRow = std::max (0, rows() - 1);
            }
            else
                --cursorRow;
            break;
        case Qt::Key_Down:
            if (cursorRow >= n - 1)
            {
                Q_EMIT orderStep (1);
                cursorRow = 0;
            }
            else
                ++cursorRow;
            break;
        case Qt::Key_PageUp:   cursorRow = std::max (0, cursorRow - 16); break;
        case Qt::Key_PageDown: cursorRow = std::min (n - 1, cursorRow + 16); break;
        case Qt::Key_Home:     cursorRow = 0; break;
        case Qt::Key_End:      cursorRow = std::max (0, n - 1); break;
        case Qt::Key_Left:
            if (ctrl)
                Q_EMIT orderStep (-1);
            else
                cursorChannel = std::max (0, cursorChannel - 1);
            break;
        case Qt::Key_Right:
            if (ctrl)
                Q_EMIT orderStep (1);
            else
                cursorChannel = std::min (channels - 1, cursorChannel + 1);
            break;
        case Qt::Key_Tab:     cursorChannel = (cursorChannel + 1) % std::max (1, channels); break;
        case Qt::Key_Backtab: cursorChannel = (cursorChannel + channels - 1) % std::max (1, channels); break;
        case Qt::Key_M:
            engine.setMuted (cursorChannel, ! engine.isMuted (cursorChannel));
            Q_EMIT channelsChanged();
            break;
        case Qt::Key_S:
            engine.setSoloed (cursorChannel, ! engine.isSoloed (cursorChannel));
            Q_EMIT channelsChanged();
            break;
        case Qt::Key_T:
            engine.setTrack (cursorChannel, (engine.track (cursorChannel) + (shift ? TrackerEngine::numTracks - 1 : 1)) % TrackerEngine::numTracks);
            Q_EMIT channelsChanged();
            break;
        default:
            QWidget::keyPressEvent (e);
            return;
    }
    cursorRow = std::clamp (cursorRow, 0, std::max (0, rows() - 1));
    keepChannelVisible();
    announce();
    update();
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
        const QFontMetrics fm (font());
        const int lineHeight = fm.height() + 2;
        const int area = height() - headerHeight;
        const int middle = headerHeight + area / 2 - lineHeight / 2;
        const int focusRow = playRow >= 0 ? playRow : cursorRow;
        cursorRow = std::clamp (focusRow + (int) std::floor ((pos.y() - middle) / (double) lineHeight), 0, std::max (0, rows() - 1));
    }
    announce();
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
    resize (1100, 680);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (8, 8, 8, 8);
    layout->setSpacing (6);

    auto* bar = new QHBoxLayout;
    auto* open = new QPushButton (tr ("&Open Module..."), this);
    playSong = new QPushButton (tr ("Play &Song"), this);
    auto* playPattern = new QPushButton (tr ("Play &Pattern"), this);
    stop = new QPushButton (tr ("S&top"), this);
    follow = new QCheckBox (tr ("&Follow"), this);
    follow->setChecked (true);
    follow->setToolTip (tr ("The pattern view follows what is playing (F)"));
    auto* record = new QToolButton (this);
    record->setText (tr ("&Record to Editor"));
    record->setPopupMode (QToolButton::InstantPopup);
    auto* menu = new QMenu (record);
    menu->addAction (tr ("Stereo"), this, [this] { Q_EMIT recordRequested (false); });
    menu->addAction (tr ("Multi-Channel (a stereo pair per track)"), this, [this] { Q_EMIT recordRequested (true); });
    record->setMenu (menu);
    for (QWidget* w : { (QWidget*) open, (QWidget*) playSong, (QWidget*) playPattern, (QWidget*) stop, (QWidget*) follow, (QWidget*) record })
    {
        w->setFocusPolicy (Qt::TabFocus);
        bar->addWidget (w);
    }
    bar->addStretch (1);
    title = new QLabel (this);
    title->setTextFormat (Qt::PlainText);
    bar->addWidget (title);
    layout->addLayout (bar);

    connect (open, &QPushButton::clicked, this, &TrackerWindow::openRequested);
    connect (playSong, &QPushButton::clicked, this, &TrackerWindow::playSongRequested);
    connect (playPattern, &QPushButton::clicked, this, &TrackerWindow::playPatternRequested);
    connect (stop, &QPushButton::clicked, this, &TrackerWindow::stopRequested);

    auto* split = new QSplitter (Qt::Horizontal, this);
    orders = new QListWidget (split);
    orders->setAccessibleName (tr ("Order list"));
    orders->setFont (trackerFont());
    orders->setMaximumWidth (190);
    pattern = new PatternView (engine, split);
    instruments = new QListWidget (split);
    instruments->setAccessibleName (tr ("Instruments"));
    instruments->setFont (trackerFont());
    instruments->setMaximumWidth (260);
    split->addWidget (orders);
    split->addWidget (pattern);
    split->addWidget (instruments);
    split->setStretchFactor (1, 1);
    layout->addWidget (split, 1);

    status = new QLabel (this);
    status->setWordWrap (true);
    layout->addWidget (status);

    connect (orders, &QListWidget::currentRowChanged, this, [this] (int row) { if (row >= 0) showOrder (row); });
    connect (pattern, &PatternView::orderStep, this, [this] (int step)
    {
        const int next = std::clamp (pattern->order() + step, 0, std::max (0, engine.numOrders() - 1));
        orders->setCurrentRow (next);
    });
    connect (pattern, &PatternView::channelsChanged, this, [this] { refresh(); });

    moduleChanged();
}

void TrackerWindow::moduleChanged()
{
    orders->clear();
    instruments->clear();
    pattern->moduleChanged();
    if (! engine.isLoaded())
    {
        title->setText (tr ("No module"));
        status->setText (tr ("Ctrl+O opens a module. Space plays the song, Enter the pattern; M, S and T mute, solo and pick the track of a channel."));
        return;
    }
    for (int i = 0; i < engine.numOrders(); ++i)
        orders->addItem (QStringLiteral ("%1  %2").arg (i, 3, 10, QLatin1Char ('0')).arg (engine.patternAt (i), 3, 10, QLatin1Char ('0')));
    const auto names = engine.instrumentNames();
    for (size_t i = 0; i < names.size(); ++i)
        instruments->addItem (QStringLiteral ("%1  %2").arg (QStringLiteral ("%1").arg ((int) i + 1, 2, 16, QLatin1Char ('0')).toUpper(),
                                                             QString::fromStdString (names[i])));
    const auto name = QString::fromStdString (engine.title());
    title->setText (tr ("%1   %2, %3 channels, %4 BPM, speed %5")
                        .arg (name.isEmpty() ? tr ("(untitled)") : name, QString::fromStdString (engine.formatName()))
                        .arg (engine.numChannels()).arg (engine.initialTempo()).arg (engine.initialSpeed()));
    setWindowTitle (tr ("Tracker: %1").arg (name.isEmpty() ? tr ("(untitled)") : name));
    orders->setCurrentRow (0);
    refresh();
}

void TrackerWindow::showOrder (int order)
{
    pattern->setOrder (order);
    if (orders->currentRow() != order)
        orders->setCurrentRow (order);
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
        if (follow->isChecked())
        {
            if (order != pattern->order() && order >= 0 && order < engine.numOrders())
                showOrder (order);
            pattern->setPlayRow (order == pattern->order() ? row : -1);
        }
        else
            pattern->setPlayRow (order == pattern->order() ? row : -1);
    }
    else
        pattern->setPlayRow (-1);

    int muted = 0;
    for (int c = 0; c < engine.numChannels(); ++c)
        muted += engine.isHeard (c) ? 0 : 1;
    const auto where = playing ? tr ("Playing order %1, row %2, %3 BPM, speed %4").arg (order).arg (hexRow (row)).arg (engine.tempo()).arg (engine.speed())
                               : tr ("Stopped at order %1, row %2").arg (pattern->order()).arg (hexRow (pattern->row()));
    status->setText (tr ("%1.   %2 of %3 channels heard; a multi-channel recording has %4 stereo pairs.   "
                         "Space: play / stop, Enter: play pattern, M / S / T: mute, solo, track, Ctrl+R: record to the editor.")
                         .arg (where).arg (engine.numChannels() - muted).arg (engine.numChannels()).arg (engine.tracksInUse()));
}

void TrackerWindow::keyPressEvent (QKeyEvent* e)
{
    const bool ctrl = e->modifiers().testFlag (Qt::ControlModifier);
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier)
    {
        Q_EMIT engine.isPlaying() ? stopRequested() : playSongRequested();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && e->modifiers() == Qt::NoModifier)
    {
        Q_EMIT playPatternRequested();
        return;
    }
    if (e->key() == Qt::Key_O && ctrl)
    {
        Q_EMIT openRequested();
        return;
    }
    if (e->key() == Qt::Key_R && ctrl)
    {
        Q_EMIT recordRequested (e->modifiers().testFlag (Qt::ShiftModifier));
        return;
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
