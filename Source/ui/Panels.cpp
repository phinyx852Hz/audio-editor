// SPDX-License-Identifier: AGPL-3.0-only
#include "Panels.h"
#include <QAbstractSpinBox>
#include <QLineEdit>
#include "../core/Dsp.h"
#include "Theme.h"

#include <QApplication>
#include <QDockWidget>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMainWindow>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    QString formatTime (double seconds)
    {
        const auto ms = (long long) (seconds * 1000.0 + 0.5);
        if (ms >= 3600000)
            return QString::asprintf ("%lld:%02lld:%02lld.%03lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
        return QString::asprintf ("%02lld:%02lld.%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    }

    double lufs (double power) { return power > 0.0 ? -0.691 + 10.0 * std::log10 (power) : -200.0; }

    void focusHome (QWidget* home)
    {
        home->window()->activateWindow();
        home->setFocus (Qt::ShortcutFocusReason);
    }
}

//==============================================================================
QWidget* FocusFollowsMouse::areaOf (QWidget* w) const
{
    for (auto* a : areas)
        if (w != nullptr && (w == a || a->isAncestorOf (w)))
            return a;
    return nullptr;
}

bool FocusFollowsMouse::eventFilter (QObject* watched, QEvent* e)
{
    if (! enabled || e->type() != QEvent::Enter || ! watched->isWidgetType())
        return false;

    auto* area = areaOf (static_cast<QWidget*> (watched));
    if (area == nullptr || ! area->isVisible() || QApplication::mouseButtons() != Qt::NoButton
        || QApplication::activePopupWidget() != nullptr || QApplication::activeModalWidget() != nullptr
        || ! area->window()->isActiveWindow())
        return false;

    // Leave a text field or anything outside the areas alone; just move between areas.
    auto* focused = QApplication::focusWidget();
    const auto* from = areaOf (focused);
    if (from == area || (focused != nullptr && from == nullptr))
        return false;
    if (qobject_cast<QLineEdit*> (focused) != nullptr || qobject_cast<QAbstractSpinBox*> (focused) != nullptr)
        return false;

    area->setFocus (Qt::MouseFocusReason);
    return false;
}

NavigationKeyGuard::NavigationKeyGuard (QWidget* home, QList<int> extraKeys, QObject* parent)
    : QObject (parent), homeWidget (home), extra (std::move (extraKeys))
{
}

bool NavigationKeyGuard::eventFilter (QObject*, QEvent* e)
{
    if (e->type() != QEvent::ShortcutOverride && e->type() != QEvent::KeyPress)
        return false;

    auto* k = static_cast<QKeyEvent*> (e);
    const bool plain = (k->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;

    if (k->key() == Qt::Key_Escape && plain)
    {
        if (e->type() == QEvent::ShortcutOverride)
        {
            e->accept();
            return false;
        }

        focusHome (homeWidget);
        return true;
    }

    if (e->type() == QEvent::ShortcutOverride && plain)
    {
        switch (k->key())
        {
            case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
            case Qt::Key_Home: case Qt::Key_End: case Qt::Key_PageUp: case Qt::Key_PageDown:
            case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space:
                e->accept();
                break;
            default:
                if (extra.contains (k->key()))
                    e->accept();
                break;
        }
    }

    return false;
}

//==============================================================================
DockablePanel::DockablePanel (QMainWindow& main, const QString& id, const QString& panelTitle, QWidget* content,
                              Qt::DockWidgetArea area)
    : QObject (&main), mainWindow (main), panelId (id), title (panelTitle), contentWidget (content),
      dockWidget (new QDockWidget (panelTitle, &main))
{
    dockWidget->setObjectName (id);
    dockWidget->setWidget (content);
    main.addDockWidget (area, dockWidget);
    dockWidget->hide();
}

DockablePanel::~DockablePanel()
{
    delete window;
}

bool DockablePanel::isShown() const
{
    return window != nullptr ? window->isVisible() : dockWidget->isVisible();
}

bool DockablePanel::containsFocus() const
{
    auto* f = QApplication::focusWidget();
    return f != nullptr && (f == contentWidget || contentWidget->isAncestorOf (f));
}

void DockablePanel::show (bool giveFocus)
{
    if (window != nullptr)
    {
        window->show();
        window->raise();
        window->activateWindow();
    }
    else
    {
        dockWidget->show();
        dockWidget->raise();

        // A restored layout can leave a dock squeezed to nothing; give it room.
        QTimer::singleShot (0, this, [this]
        {
            if (window != nullptr || dockWidget->isFloating() || ! dockWidget->isVisible())
                return;

            const auto hint = contentWidget->sizeHint().expandedTo (QSize (220, 180));
            if (dockWidget->height() < 80)
                mainWindow.resizeDocks ({ dockWidget }, { hint.height() }, Qt::Vertical);
            if (dockWidget->width() < 80)
                mainWindow.resizeDocks ({ dockWidget }, { hint.width() }, Qt::Horizontal);
        });
    }

    if (giveFocus && contentWidget->focusPolicy() != Qt::NoFocus)
    {
        contentWidget->setFocus (Qt::ShortcutFocusReason);

        // A dock that was hidden only becomes focusable once laid out, so ask again then.
        QTimer::singleShot (0, contentWidget, [w = contentWidget]
        {
            if (w->isVisible())
            {
                w->window()->activateWindow();
                w->setFocus (Qt::ShortcutFocusReason);
            }
        });
    }
}

void DockablePanel::hide()
{
    if (window != nullptr)
        dockBack (false);
    else
        dockWidget->hide();
}

void DockablePanel::toggle()
{
    const bool focusable = contentWidget->focusPolicy() != Qt::NoFocus;

    if (! isShown() || (focusable && ! containsFocus()))
        show (true);
    else
        hide();
}

void DockablePanel::setPoppedOut (bool popOut)
{
    if (popOut == isPoppedOut())
        return;

    if (! popOut)
    {
        dockBack (true);
        show (true);
        return;
    }

    const auto size = dockWidget->isVisible() ? dockWidget->size() : contentWidget->sizeHint();

    // A parentless window, so tiling window managers treat it as a normal window.
    window = new QWidget (nullptr, Qt::Window);
    window->setObjectName (panelId + QStringLiteral ("Window"));
    window->setWindowTitle (title + QStringLiteral (" - " APP_DISPLAY_NAME));
    window->setAttribute (Qt::WA_DeleteOnClose, false);
    window->installEventFilter (this);
    Theme::makeTranslucent (window);

    // Every command shortcut works in the popped-out window too.
    window->addActions (mainWindow.actions());

    auto* layout = new QVBoxLayout (window);
    layout->setContentsMargins (0, 0, 0, 0);
    layout->addWidget (contentWidget);
    contentWidget->show();
    dockWidget->hide();
    window->resize (size.expandedTo (QSize (200, 150)));
    show (true);
}

void DockablePanel::dockBack (bool keepVisible)
{
    if (window == nullptr)
        return;

    auto* old = window;
    window = nullptr;
    old->removeEventFilter (this);
    dockWidget->setWidget (contentWidget);
    old->hide();
    old->deleteLater();
    dockWidget->setVisible (keepVisible);
}

bool DockablePanel::eventFilter (QObject* watched, QEvent* e)
{
    // Closing the popped-out window (for example with the window manager's close key) puts the panel back in its dock, hidden.
    if (watched == window && e->type() == QEvent::Close)
        QTimer::singleShot (0, this, [this] { dockBack (false); });

    return false;
}

//==============================================================================
MarkerList::MarkerList (QWidget* parent) : QTreeWidget (parent)
{
    setAccessibleName (tr ("Markers"));
    setColumnCount (4);
    setHeaderLabels ({ tr ("Name"), tr ("Start"), tr ("End"), tr ("Length") });
    setRootIsDecorated (false);
    setUniformRowHeights (true);
    header()->setStretchLastSection (false);
    header()->setSectionResizeMode (0, QHeaderView::Stretch);

    connect (this, &QTreeWidget::itemActivated, this, [this] (QTreeWidgetItem* item)
    {
        Q_EMIT jumpRequested (item->data (0, Qt::UserRole).toInt());
    });
}

void MarkerList::setMarkers (const std::vector<Marker>& markers, double sampleRate)
{
    const auto selectedId = currentMarkerId();
    auto sorted = markers;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const Marker& a, const Marker& b) { return a.start < b.start; });

    clear();
    for (const auto& m : sorted)
    {
        auto* item = new QTreeWidgetItem (this);
        item->setData (0, Qt::UserRole, m.id);
        item->setText (0, QString::fromStdString (m.name));
        item->setText (1, formatTime ((double) m.start / sampleRate));
        if (m.isRegion())
        {
            item->setText (2, formatTime ((double) m.end / sampleRate));
            item->setText (3, formatTime ((double) (m.end - m.start) / sampleRate));
        }

        if (m.id == selectedId)
            setCurrentItem (item);
    }

    if (currentItem() == nullptr && topLevelItemCount() > 0)
        setCurrentItem (topLevelItem (0));

    for (int c = 1; c < 4; ++c)
        resizeColumnToContents (c);
}

int MarkerList::currentMarkerId() const
{
    return currentItem() != nullptr ? currentItem()->data (0, Qt::UserRole).toInt() : 0;
}

void MarkerList::keyPressEvent (QKeyEvent* e)
{
    const auto id = currentMarkerId();

    if (id != 0 && e->modifiers() == Qt::NoModifier)
    {
        if (e->key() == Qt::Key_F2)
        {
            Q_EMIT renameRequested (id);
            return;
        }

        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
        {
            Q_EMIT deleteRequested (id);
            return;
        }
    }

    QTreeWidget::keyPressEvent (e);
}

//==============================================================================
MeterPanel::MeterPanel (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Level and loudness meters"));
    setFocusPolicy (Qt::NoFocus);
    reset();
    clock.start();
}

void MeterPanel::reset()
{
    std::fill (std::begin (level), std::end (level), -100.0f);
    std::fill (std::begin (hold), std::end (hold), -100.0f);
    std::fill (std::begin (holdAge), std::end (holdAge), 0.0);
    maxTruePeak = maxSamplePeak = -200.0f;
    powers.clear();
    momentary = shortTerm = integrated = -200.0;
    update();
}

void MeterPanel::addBlocks (const std::vector<MeterBlock>& blocks)
{
    const auto dt = clock.restart() / 1000.0;
    constexpr float fallDbPerSecond = 24.0f;
    constexpr double holdSeconds = 2.0;

    float blockPeak[maxChannels];
    std::fill (std::begin (blockPeak), std::end (blockPeak), -100.0f);

    for (const auto& b : blocks)
    {
        numChannels = std::clamp (b.numChannels, 1, maxChannels);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto sp = (float) dsp::gainToDb (b.samplePeak[ch]);
            const auto tp = (float) dsp::gainToDb (std::max (b.truePeak[ch], b.samplePeak[ch]));
            blockPeak[ch] = std::max (blockPeak[ch], sp);
            maxSamplePeak = std::max (maxSamplePeak, sp);
            maxTruePeak = std::max (maxTruePeak, tp);
        }

        powers.push_back (b.loudnessPower);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        level[ch] = std::max (blockPeak[ch], std::max (-100.0f, level[ch] - fallDbPerSecond * (float) dt));

        if (blockPeak[ch] >= hold[ch])
        {
            hold[ch] = blockPeak[ch];
            holdAge[ch] = 0.0;
        }
        else if ((holdAge[ch] += dt) > holdSeconds)
            hold[ch] = level[ch];
    }

    if (! blocks.empty())
    {
        auto meanOfLast = [this] (size_t count)
        {
            const auto n = std::min (count, powers.size());
            double sum = 0.0;
            for (size_t i = powers.size() - n; i < powers.size(); ++i)
                sum += powers[i];
            return n > 0 ? sum / (double) n : 0.0;
        };

        momentary = lufs (meanOfLast (4));
        shortTerm = lufs (meanOfLast (30));
        integrated = dsp::integratedLoudness (powers);
    }

    update();
}

void MeterPanel::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.fillRect (rect(), theme.color (QStringLiteral ("window")));

    constexpr float floorDb = -60.0f;
    const QRectF area (8, 8, std::min (width() * (numChannels > 2 ? 0.6 : 0.45), 40.0 + numChannels * 26.0), height() - 30);
    const auto barsWidth = area.width() - 28.0;
    const auto barWidth = barsWidth / numChannels;
    const auto yFor = [&] (float db) { return area.top() + area.height() * (double) (std::clamp (db, floorDb, 0.0f) / floorDb); };

    g.fillRect (QRectF (area.left(), area.top(), barsWidth, area.height()), theme.color (QStringLiteral ("meterBackground")));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto x = area.left() + ch * barWidth + 1.0;
        const auto w = barWidth - 2.0;

        // Green below -18 dB, yellow up to -6 dB, red above.
        const std::pair<float, const char*> zones[] { { -18.0f, "meterLow" }, { -6.0f, "meterMid" }, { 0.0f, "meterHigh" } };
        float zoneStart = floorDb;
        for (const auto& [zoneEnd, key] : zones)
        {
            const auto top = std::min (level[ch], zoneEnd);
            if (top > zoneStart)
                g.fillRect (QRectF (x, yFor (top), w, yFor (zoneStart) - yFor (top)), theme.color (QString::fromLatin1 (key)));
            zoneStart = zoneEnd;
        }

        if (hold[ch] > floorDb)
        {
            g.setPen (QPen (theme.color (QStringLiteral ("meterPeakHold")), 2.0));
            g.drawLine (QPointF (x, yFor (hold[ch])), QPointF (x + w, yFor (hold[ch])));
        }
    }

    // Scale
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (small);
    g.setPen (theme.color (QStringLiteral ("placeholderText")));

    // Speaker names under the bars, smaller when the bars are narrow
    auto labelFont = small;
    if (barWidth < 22.0)
        labelFont.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (labelFont);
    for (int ch = 0; ch < numChannels; ++ch)
        g.drawText (QRectF (area.left() + ch * barWidth, area.bottom() + 2, barWidth, 14), Qt::AlignCenter,
                    QString::fromStdString (dsp::channelName (ch, numChannels)));
    g.setFont (small);

    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        g.drawText (QRectF (area.left() + barsWidth + 2, yFor (db) - 8, 26, 16), Qt::AlignLeft | Qt::AlignVCenter,
                    QString::number ((int) db));

    // Readouts
    auto fmt = [] (double v, const char* unit)
    {
        return v <= -199.0 ? QStringLiteral ("-inf %1").arg (QLatin1String (unit))
                           : QStringLiteral ("%1 %2").arg (v, 0, 'f', 1).arg (QLatin1String (unit));
    };

    const std::pair<QString, QString> rows[] {
        { tr ("Momentary"),   fmt (momentary, "LUFS") },
        { tr ("Short-term"),  fmt (shortTerm, "LUFS") },
        { tr ("Integrated"),  fmt (integrated, "LUFS") },
        { tr ("True peak"),   fmt (maxTruePeak, "dBTP") },
        { tr ("Sample peak"), fmt (maxSamplePeak, "dBFS") },
    };

    g.setFont (font());
    const auto left = area.right() + 12;
    const auto lineHeight = fontMetrics().height() * 1.6;
    auto y = area.top();
    for (const auto& [label, value] : rows)
    {
        g.setPen (theme.color (QStringLiteral ("placeholderText")));
        g.drawText (QRectF (left, y, width() - left - 8, lineHeight * 0.5), Qt::AlignLeft | Qt::AlignVCenter, label);
        g.setPen (label == tr ("True peak") && maxTruePeak > -1.0f ? theme.color (QStringLiteral ("meterHigh"))
                                                                  : theme.color (QStringLiteral ("windowText")));
        g.drawText (QRectF (left, y + lineHeight * 0.45, width() - left - 8, lineHeight * 0.55), Qt::AlignLeft | Qt::AlignVCenter, value);
        y += lineHeight * 1.15;
    }
}
