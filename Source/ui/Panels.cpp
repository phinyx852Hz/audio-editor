// SPDX-License-Identifier: AGPL-3.0-only
#include "Panels.h"
#include <QAbstractSpinBox>
#include <QLineEdit>
#include "../core/Dsp.h"
#include "Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
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

//==============================================================================
namespace
{
    QSettings& diagnosticsSettings()
    {
        static QSettings settings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                                       + QStringLiteral ("/" APP_ID "/settings.ini"),
                                   QSettings::IniFormat);
        return settings;
    }

    QString channelList (uint64_t channels, int count)
    {
        if (channels == 0 || count <= 1)
            return DiagnosticsPanel::tr ("All");
        QStringList names;
        for (int ch = 0; ch < std::min (count, 64); ++ch)
            if (channels & ((uint64_t) 1 << ch))
                names << QString::fromStdString (dsp::channelName (ch, count));
        return names.size() == count ? DiagnosticsPanel::tr ("All") : names.join (QStringLiteral (", "));
    }
}

DiagnosticsPanel::DiagnosticsPanel (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Diagnostics"));
    auto& stored = diagnosticsSettings();

    kindBox = new QComboBox (this);
    kindBox->addItems ({ tr ("Clicks and crackle"), tr ("Clipping"), tr ("Dropouts (lost samples)"), tr ("Silence") });
    kindBox->setCurrentIndex (stored.value (QStringLiteral ("diagnostics/kind"), 0).toInt());

    settings = new QStackedWidget (this);
    auto page = [this] (QWidget* field, const QString& label)
    {
        auto* w = new QWidget (settings);
        auto* form = new QFormLayout (w);
        form->setContentsMargins (0, 0, 0, 0);
        if (field != nullptr)
            form->addRow (label, field);
        else
            form->addRow (new QLabel (label, w));
        settings->addWidget (w);
        return form;
    };

    sensitivityBox = new QComboBox;
    sensitivityBox->addItems ({ tr ("Low (loud clicks only)"), tr ("Medium"), tr ("High (vinyl crackle)") });
    sensitivityBox->setCurrentIndex (stored.value (QStringLiteral ("diagnostics/sensitivity"), 1).toInt());
    page (sensitivityBox, tr ("&Sensitivity:"));

    clipBox = new QDoubleSpinBox;
    clipBox->setRange (80.0, 100.0);
    clipBox->setDecimals (1);
    clipBox->setSingleStep (0.5);
    clipBox->setSuffix (tr (" % of the peak"));
    clipBox->setValue (stored.value (QStringLiteral ("diagnostics/clipLevel"), 99.0).toDouble());
    page (clipBox, tr ("&Clipped at or above:"));

    page (nullptr, tr ("Finds stretches of digital silence with sound on both sides."));

    silenceDbBox = new QDoubleSpinBox;
    silenceDbBox->setRange (-90.0, -20.0);
    silenceDbBox->setDecimals (0);
    silenceDbBox->setSuffix (tr (" dB"));
    silenceDbBox->setValue (stored.value (QStringLiteral ("diagnostics/silenceDb"), -50.0).toDouble());
    silenceSecondsBox = new QDoubleSpinBox;
    silenceSecondsBox->setRange (0.1, 60.0);
    silenceSecondsBox->setDecimals (1);
    silenceSecondsBox->setSingleStep (0.5);
    silenceSecondsBox->setSuffix (tr (" s"));
    silenceSecondsBox->setValue (stored.value (QStringLiteral ("diagnostics/silenceSeconds"), 2.0).toDouble());
    auto* silenceForm = page (silenceDbBox, tr ("&Quieter than:"));
    silenceForm->addRow (tr ("For at &least:"), silenceSecondsBox);

    scanButton = new QPushButton (tr ("Sca&n"), this);
    scanButton->setToolTip (tr ("Scan the selection, or the whole file when nothing is selected"));

    tree = new QTreeWidget (this);
    tree->setAccessibleName (tr ("Problems found"));
    tree->setColumnCount (4);
    tree->setHeaderLabels ({ tr ("Start"), tr ("Length"), tr ("Channels"), tr ("Detail") });
    tree->setRootIsDecorated (false);
    tree->setUniformRowHeights (true);
    tree->setSelectionMode (QAbstractItemView::ExtendedSelection);
    tree->header()->setStretchLastSection (true);

    summary = new QLabel (tr ("Choose what to look for, then Scan."), this);
    summary->setWordWrap (true);

    repairButton = new QPushButton (tr ("&Repair"), this);
    repairAllButton = new QPushButton (tr ("Repair &All"), this);
    markButton = new QPushButton (tr ("&Mark"), this);
    markButton->setToolTip (tr ("Add a region marker for each chosen problem (or all of them)"));
    deleteButton = new QPushButton (tr ("&Delete"), this);
    deleteButton->setToolTip (tr ("Delete the chosen silent stretches"));

    auto* top = new QHBoxLayout;
    auto* findLabel = new QLabel (tr ("&Find:"), this);
    findLabel->setBuddy (kindBox);
    top->addWidget (findLabel);
    top->addWidget (kindBox, 1);
    top->addWidget (scanButton);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget (repairButton);
    buttons->addWidget (repairAllButton);
    buttons->addWidget (markButton);
    buttons->addWidget (deleteButton);
    buttons->addStretch (1);
    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (6, 6, 6, 6);
    layout->addLayout (top);
    layout->addWidget (settings);
    layout->addWidget (tree, 1);
    layout->addWidget (summary);
    layout->addLayout (buttons);

    settings->setCurrentIndex (kindBox->currentIndex());
    connect (kindBox, &QComboBox::currentIndexChanged, this, [this] (int i)
    {
        settings->setCurrentIndex (i);
        diagnosticsSettings().setValue (QStringLiteral ("diagnostics/kind"), i);
        clearProblems (tr ("Choose what to look for, then Scan."));
    });
    connect (sensitivityBox, &QComboBox::currentIndexChanged, this, [] (int i) { diagnosticsSettings().setValue (QStringLiteral ("diagnostics/sensitivity"), i); });
    connect (clipBox, &QDoubleSpinBox::valueChanged, this, [] (double v) { diagnosticsSettings().setValue (QStringLiteral ("diagnostics/clipLevel"), v); });
    connect (silenceDbBox, &QDoubleSpinBox::valueChanged, this, [] (double v) { diagnosticsSettings().setValue (QStringLiteral ("diagnostics/silenceDb"), v); });
    connect (silenceSecondsBox, &QDoubleSpinBox::valueChanged, this, [] (double v) { diagnosticsSettings().setValue (QStringLiteral ("diagnostics/silenceSeconds"), v); });

    connect (scanButton, &QPushButton::clicked, this, &DiagnosticsPanel::scanRequested);
    connect (repairButton, &QPushButton::clicked, this, [this] { Q_EMIT repairRequested (chosen()); });
    connect (repairAllButton, &QPushButton::clicked, this, [this] { Q_EMIT repairRequested (allRows()); });
    connect (markButton, &QPushButton::clicked, this, [this]
    {
        // With nothing picked out, mark the whole list.
        Q_EMIT markRequested (tree->selectedItems().size() > 1 ? chosen() : allRows());
    });
    connect (deleteButton, &QPushButton::clicked, this, [this] { Q_EMIT deleteRequested (chosen()); });
    connect (tree, &QTreeWidget::currentItemChanged, this, [this] (QTreeWidgetItem* item)
    {
        if (item == nullptr)
            return;
        const auto& p = list[(size_t) item->data (0, Qt::UserRole).toInt()];
        Q_EMIT showRequested (p.start, p.end);
    });
    connect (tree, &QTreeWidget::itemSelectionChanged, this, &DiagnosticsPanel::updateButtons);
    updateButtons();
}

DiagnosticsPanel::Kind DiagnosticsPanel::kind() const { return (Kind) kindBox->currentIndex(); }

double DiagnosticsPanel::clickSensitivity() const
{
    const double thresholds[] { 9.0, 6.5, 4.5 };   // the same steps as DeClick
    return thresholds[std::clamp (sensitivityBox->currentIndex(), 0, 2)];
}

double DiagnosticsPanel::clipLevel() const       { return clipBox->value(); }
double DiagnosticsPanel::silenceDb() const       { return silenceDbBox->value(); }
double DiagnosticsPanel::silenceSeconds() const  { return silenceSecondsBox->value(); }
QWidget* DiagnosticsPanel::listWidget() const    { return tree; }

void DiagnosticsPanel::setProblems (std::vector<repair::Problem> found, double sampleRate, int channels, const QString& where)
{
    list = std::move (found);
    rate = sampleRate;
    numChannels = channels;
    tree->clear();
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& p = list[i];
        auto* item = new QTreeWidgetItem (tree);
        item->setData (0, Qt::UserRole, (int) i);
        item->setText (0, formatTime ((double) p.start / rate));
        const auto ms = (double) (p.end - p.start) / rate * 1000.0;
        item->setText (1, ms < 1000.0 ? tr ("%1 ms").arg (ms, 0, 'f', ms < 10.0 ? 2 : 0) : formatTime (ms / 1000.0));
        item->setText (2, channelList (p.channels, channels));
        switch (p.kind)
        {
            case Kind::click:    item->setText (3, tr ("%1 times the sound around it").arg (p.size, 0, 'f', 0)); break;
            case Kind::clipping: item->setText (3, tr ("%1 samples at the limit").arg (p.end - p.start)); break;
            case Kind::dropout:  item->setText (3, tr ("digital silence")); break;
            case Kind::silence:  item->setText (3, tr ("quiet")); break;
        }
    }
    for (int c = 0; c < 3; ++c)
        tree->resizeColumnToContents (c);

    const QString one[] { tr ("click"), tr ("clipped stretch"), tr ("dropout"), tr ("silent stretch") };
    const QString many[] { tr ("clicks"), tr ("clipped stretches"), tr ("dropouts"), tr ("silent stretches") };
    const auto k = (int) kind();
    const auto action = kind() == Kind::silence ? tr ("Delete removes the chosen ones.") : tr ("Repair fixes the chosen ones.");
    summary->setText (list.empty() ? tr ("Found no %1 %2.").arg (many[k], where)
                                   : tr ("Found %1 %2 %3. Moving through the list shows each one; %4")
                                         .arg (list.size()).arg (list.size() == 1 ? one[k] : many[k], where, action));
    if (! list.empty())
        tree->setCurrentItem (tree->topLevelItem (0), 0, QItemSelectionModel::NoUpdate);
    updateButtons();
}

void DiagnosticsPanel::clearProblems (const QString& why)
{
    list.clear();
    tree->clear();
    if (! why.isEmpty())
        summary->setText (why);
    updateButtons();
}

std::vector<int> DiagnosticsPanel::allRows() const
{
    std::vector<int> rows ((size_t) list.size());
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i] = (int) i;
    return rows;
}

std::vector<int> DiagnosticsPanel::chosen() const
{
    std::vector<int> rows;
    for (auto* item : tree->selectedItems())
        rows.push_back (item->data (0, Qt::UserRole).toInt());
    if (rows.empty() && tree->currentItem() != nullptr)
        rows.push_back (tree->currentItem()->data (0, Qt::UserRole).toInt());
    std::sort (rows.begin(), rows.end());
    return rows;
}

void DiagnosticsPanel::remove (const std::vector<int>& rows)
{
    auto kept = list;
    kept.clear();
    for (size_t i = 0; i < list.size(); ++i)
        if (! std::binary_search (rows.begin(), rows.end(), (int) i))
            kept.push_back (list[i]);
    const auto current = tree->currentItem() != nullptr ? tree->indexOfTopLevelItem (tree->currentItem()) : 0;
    setProblems (std::move (kept), rate, numChannels, {});
    summary->setText (list.empty() ? tr ("All done.") : tr ("%1 left in the list.").arg (list.size()));
    if (! list.empty())
        tree->setCurrentItem (tree->topLevelItem (std::min (current, tree->topLevelItemCount() - 1)));
}

void DiagnosticsPanel::updateButtons()
{
    const auto silence = kind() == Kind::silence;
    repairButton->setVisible (! silence);
    repairAllButton->setVisible (! silence);
    deleteButton->setVisible (silence);
    for (auto* b : { repairButton, repairAllButton, markButton, deleteButton })
        b->setEnabled (! list.empty());
}
