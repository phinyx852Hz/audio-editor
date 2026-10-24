// SPDX-License-Identifier: AGPL-3.0-only
#include "MasterPanel.h"
#include "Panels.h"
#include "PhiMark.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

//==============================================================================
MasterPanel::MasterPanel (AudioEngine& e, QWidget* home, QWidget* parent)
    : QWidget (parent), engine (e), slotList (new QListWidget (this)), gainBox (new QDoubleSpinBox (this)),
      bypassBox (new QCheckBox (tr ("&Bypass Master Section"), this)), summary (new QLabel (this))
{
    setAccessibleName (tr ("Master Section"));
    slotList->setAccessibleName (tr ("Effect slots"));
    slotList->setAccessibleDescription (tr ("Enter edits parameters or adds a plugin, Insert picks a plugin, Delete removes it, "
                                            "B bypasses, E opens the plugin's own window, Ctrl+Up and Ctrl+Down reorder."));
    slotList->installEventFilter (this);
    slotList->installEventFilter (new NavigationKeyGuard (home, { Qt::Key_Delete, Qt::Key_Backspace, Qt::Key_Insert }, slotList));
    setFocusProxy (slotList);

    gainBox->setRange (-60.0, 24.0);
    gainBox->setDecimals (1);
    gainBox->setSingleStep (0.5);
    gainBox->setSuffix (tr (" dB"));
    gainBox->setAccessibleName (tr ("Master gain"));
    gainBox->installEventFilter (new NavigationKeyGuard (home, {}, gainBox));

    ditherBits = new QComboBox (this);
    ditherBits->addItem (tr ("Off"), 0);
    ditherBits->addItem (tr ("16-bit"), 16);
    ditherBits->addItem (tr ("20-bit"), 20);
    ditherBits->addItem (tr ("24-bit"), 24);
    ditherBits->setAccessibleName (tr ("Final dither bit depth"));
    ditherBits->setToolTip (tr ("Last in the Master Section, after the gain: rounds playback and Render in Place to this bit depth. "
                                "Exports round to their own bit depth instead."));
    ditherType = new QComboBox (this);
    ditherType->addItem (tr ("Plain rounding"), (int) dsp::Dither::none);
    ditherType->addItem (tr ("TPDF"), (int) dsp::Dither::tpdf);
    ditherType->addItem (tr ("Shaped (noise shaping)"), (int) dsp::Dither::shaped);
    ditherType->setAccessibleName (tr ("Final dither noise"));
    ditherType->setToolTip (tr ("TPDF hides the rounding as faint even hiss; shaped moves that hiss up where the ear hears it least."));
    for (auto* box : { ditherBits, ditherType })
        box->installEventFilter (new NavigationKeyGuard (home, {}, box));

    summary->setWordWrap (true);
    summary->setForegroundRole (QPalette::PlaceholderText);

    auto* addButton = new QPushButton (tr ("&Add Plugin..."), this);
    auto* renderButton = new QPushButton (tr ("&Render in Place"), this);
    auto* scanButton = new QPushButton (tr ("&Scan Plugins"), this);
    for (auto* b : { addButton, renderButton, scanButton })
        b->setAutoDefault (false);

    auto* gainRow = new QFormLayout;
    gainRow->addRow (tr ("Master &gain:"), gainBox);
    auto* ditherRow = new QHBoxLayout;
    ditherRow->addWidget (ditherBits);
    ditherRow->addWidget (ditherType, 1);
    auto* ditherLabel = new QLabel (tr ("Final &dither:"), this);
    ditherLabel->setBuddy (ditherBits);
    gainRow->addRow (ditherLabel, ditherRow);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget (addButton);
    buttons->addWidget (renderButton);
    buttons->addWidget (scanButton);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (6, 6, 6, 6);
    layout->addWidget (slotList, 1);
    layout->addLayout (gainRow);
    layout->addWidget (bypassBox);
    layout->addLayout (buttons);
    layout->addWidget (summary);

    connect (slotList, &QListWidget::itemActivated, this, [this] (QListWidgetItem*)
    {
        const auto slot = currentSlot();
        if (engine.masterSlot (slot).empty)
            choosePlugin (slot);
        else
            editParameters (slot);
    });
    connect (gainBox, &QDoubleSpinBox::valueChanged, this, [this] (double db) { engine.setMasterGainDb (db); refresh(); });
    auto applyDither = [this]
    {
        engine.setMasterDither (ditherBits->currentData().toInt(), (dsp::Dither) ditherType->currentData().toInt());
        refresh();
    };
    connect (ditherBits, &QComboBox::currentIndexChanged, this, applyDither);
    connect (ditherType, &QComboBox::currentIndexChanged, this, applyDither);
    connect (bypassBox, &QCheckBox::toggled, this, [this] (bool b) { engine.setMasterBypassed (b); refresh(); });
    connect (addButton, &QPushButton::clicked, this, [this] { choosePlugin (currentSlot()); });
    connect (renderButton, &QPushButton::clicked, this, &MasterPanel::renderRequested);
    connect (scanButton, &QPushButton::clicked, this, &MasterPanel::scanPlugins);

    refresh();
    slotList->setCurrentRow (0);
}

int MasterPanel::currentSlot() const
{
    return std::max (0, slotList->currentRow());
}

void MasterPanel::focusSlots()
{
    slotList->setFocus (Qt::ShortcutFocusReason);
}

void MasterPanel::refresh()
{
    const auto row = std::max (0, slotList->currentRow());
    const QSignalBlocker blocker (slotList);
    slotList->clear();

    int latency = 0, active = 0;
    for (int i = 0; i < AudioEngine::numMasterSlots; ++i)
    {
        const auto info = engine.masterSlot (i);
        QString text;
        if (info.empty)
            text = tr ("%1   (empty)").arg (i + 1);
        else
        {
            text = tr ("%1   %2   %3").arg (i + 1).arg (QString::fromStdString (info.name), QString::fromStdString (info.format));
            if (info.channels > 0 && info.channels < engine.masterChannels())
                text += info.channels <= 2 ? tr ("   [front L/R only]") : tr ("   [first %1 channels]").arg (info.channels);
            if (info.bypassed)
                text += tr ("   [bypassed]");
            else
            {
                latency += info.latencySamples;
                ++active;
            }
        }

        slotList->addItem (text);
    }

    slotList->setCurrentRow (std::min (row, AudioEngine::numMasterSlots - 1));

    {
        const QSignalBlocker b1 (gainBox);
        const QSignalBlocker b2 (bypassBox);
        const QSignalBlocker b3 (ditherBits);
        const QSignalBlocker b4 (ditherType);
        gainBox->setValue (engine.masterGainDb());
        ditherBits->setCurrentIndex (std::max (0, ditherBits->findData (engine.masterDitherBits())));
        ditherType->setCurrentIndex (std::max (0, ditherType->findData ((int) engine.masterDitherType())));
        ditherType->setEnabled (engine.masterDitherBits() > 0);
        bypassBox->setChecked (engine.isMasterBypassed());
    }

    auto text = engine.isMasterBypassed() ? tr ("Bypassed: playback and renders skip the Master Section.")
                                          : tr ("%n active plugin(s)", nullptr, active);
    if (! engine.isMasterBypassed() && latency > 0)
        text += tr (", %1 samples latency (compensated)").arg (latency);
    if (! engine.isMasterBypassed() && engine.masterDitherBits() > 0)
        text += tr (", dithered to %1-bit last").arg (engine.masterDitherBits());
    summary->setText (text);
}

void MasterPanel::choosePlugin (int slot)
{
    if (slot < 0)
    {
        slot = 0;
        for (int i = 0; i < AudioEngine::numMasterSlots; ++i)
            if (engine.masterSlot (i).empty)
            {
                slot = i;
                break;
            }
    }

    auto plugins = engine.knownPlugins();
    if (plugins.empty())
    {
        if (QMessageBox::question (this, tr ("No plugins yet"),
                                   tr ("No effect plugins have been found yet. Scan for VST3, LV2 and LADSPA plugins now?"))
            != QMessageBox::Yes)
            return;

        scanPlugins();
        plugins = engine.knownPlugins();
        if (plugins.empty())
            return;
    }

    PluginBrowser browser (plugins, this);
    browser.setWindowTitle (tr ("Choose a Plugin for Slot %1").arg (slot + 1));
    if (browser.exec() != QDialog::Accepted || browser.selectedId().empty())
        return;

    QApplication::setOverrideCursor (Qt::WaitCursor);
    const auto error = engine.loadMasterPlugin (slot, browser.selectedId());
    QApplication::restoreOverrideCursor();

    if (! error.empty())
        QMessageBox::warning (this, tr ("Plugin"), QString::fromStdString (error));

    slotList->setCurrentRow (slot);
    refresh();
    slotList->setFocus (Qt::OtherFocusReason);
    Q_EMIT message (error.empty() ? tr ("Loaded %1 in slot %2").arg (QString::fromStdString (engine.masterSlot (slot).name)).arg (slot + 1)
                                  : QString());
}

void MasterPanel::scanPlugins()
{
    const auto total = engine.startPluginScan();
    QProgressDialog progress (tr ("Scanning plugins..."), tr ("Stop"), 0, std::max (1, total), this);
    progress.setWindowTitle (tr ("Scan Plugins"));
    progress.setWindowModality (Qt::WindowModal);
    progress.setMinimumDuration (0);

    std::string file;
    int done = 0;
    while (! progress.wasCanceled() && engine.scanNextPlugin (file))
    {
        progress.setValue (++done);
        progress.setLabelText (tr ("Scanning %1").arg (QString::fromStdString (file).section (QLatin1Char ('/'), -1)));
        QApplication::processEvents();
    }

    const auto summaryText = QString::fromStdString (engine.finishPluginScan());
    progress.close();
    Q_EMIT message (summaryText);

    if (total == 0)
    {
        QStringList folders;
        for (const auto& f : engine.pluginSearchFolders())
            folders << QString::fromStdString (f);
        QMessageBox::information (this, tr ("Scan Plugins"),
                                  tr ("No plugin files were found. PHI looks in:\n%1").arg (folders.join (QLatin1Char ('\n'))));
    }
}

void MasterPanel::editParameters (int slot)
{
    if (engine.masterSlot (slot).empty)
        return;

    auto* dialog = new ParameterDialog (engine, slot, window());
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->show();
}

bool MasterPanel::eventFilter (QObject* watched, QEvent* e)
{
    if (watched != slotList || e->type() != QEvent::KeyPress)
        return false;

    auto* k = static_cast<QKeyEvent*> (e);
    const auto slot = currentSlot();
    const auto info = engine.masterSlot (slot);

    if (k->modifiers() == Qt::ControlModifier && (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down))
    {
        const auto target = slot + (k->key() == Qt::Key_Up ? -1 : 1);
        if (target >= 0 && target < AudioEngine::numMasterSlots)
        {
            engine.moveMasterSlot (slot, target);
            slotList->setCurrentRow (target);
            refresh();
        }
        return true;
    }

    if (k->modifiers() != Qt::NoModifier)
        return false;

    switch (k->key())
    {
        case Qt::Key_Insert:
            choosePlugin (slot);
            return true;

        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            if (! info.empty)
            {
                engine.clearMasterSlot (slot);
                refresh();
                Q_EMIT message (tr ("Removed %1").arg (QString::fromStdString (info.name)));
            }
            return true;

        case Qt::Key_B:
            if (! info.empty)
            {
                engine.setMasterSlotBypassed (slot, ! info.bypassed);
                refresh();
            }
            return true;

        case Qt::Key_E:
            if (engine.canShowPluginWindow (slot))
                engine.showPluginWindow (slot);
            else if (! info.empty)
                Q_EMIT message (engine.pluginWindowsLost()
                                    ? tr ("Plugin windows were force-closed by the window manager; restart PHI to open them again (Enter edits parameters meanwhile)")
                                : info.hasEditor ? tr ("Plugin windows need XWayland or X11; use Enter for the parameter editor")
                                                 : tr ("This plugin has no window of its own; use Enter for the parameter editor"));
            return true;

        default:
            return false;
    }
}

//==============================================================================
PluginBrowser::PluginBrowser (const std::vector<PluginInfo>& p, QWidget* parent)
    : QDialog (parent), plugins (p), search (new QLineEdit (this)), list (new QTreeWidget (this))
{
    resize (640, 440);
    search->setPlaceholderText (tr ("Type to search by name, maker, format or category"));
    search->setAccessibleName (tr ("Search plugins"));
    search->installEventFilter (this);

    list->setColumnCount (4);
    list->setHeaderLabels ({ tr ("Plugin"), tr ("Maker"), tr ("Format"), tr ("Category") });
    list->setRootIsDecorated (false);
    list->setUniformRowHeights (true);
    list->header()->setSectionResizeMode (0, QHeaderView::Stretch);

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (search);
    layout->addWidget (list);

    connect (search, &QLineEdit::textChanged, this, &PluginBrowser::refilter);
    connect (list, &QTreeWidget::itemActivated, this, [this] (QTreeWidgetItem* item)
    {
        chosen = item->data (0, Qt::UserRole).toString().toStdString();
        accept();
    });

    refilter();
    search->setFocus();
}

void PluginBrowser::refilter()
{
    const auto words = search->text().split (QLatin1Char (' '), Qt::SkipEmptyParts);
    list->clear();

    for (const auto& p : plugins)
    {
        const auto haystack = QString::fromStdString (p.name + " " + p.manufacturer + " " + p.format + " " + p.category);
        if (! std::all_of (words.begin(), words.end(), [&] (const QString& w) { return haystack.contains (w, Qt::CaseInsensitive); }))
            continue;

        auto* item = new QTreeWidgetItem (list, { QString::fromStdString (p.name), QString::fromStdString (p.manufacturer),
                                                  QString::fromStdString (p.format), QString::fromStdString (p.category) });
        item->setData (0, Qt::UserRole, QString::fromStdString (p.id));
    }

    if (list->topLevelItemCount() > 0)
        list->setCurrentItem (list->topLevelItem (0));

    for (int c = 1; c < 4; ++c)
        list->resizeColumnToContents (c);
}

bool PluginBrowser::eventFilter (QObject* watched, QEvent* e)
{
    // Up/Down/Enter in the search box drive the list, so the mouse is never needed.
    if (watched == search && e->type() == QEvent::KeyPress)
    {
        auto* k = static_cast<QKeyEvent*> (e);
        switch (k->key())
        {
            case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_PageUp: case Qt::Key_PageDown:
                QApplication::sendEvent (list, e);
                return true;

            case Qt::Key_Return: case Qt::Key_Enter:
                if (auto* item = list->currentItem())
                {
                    chosen = item->data (0, Qt::UserRole).toString().toStdString();
                    accept();
                }
                return true;

            default:
                break;
        }
    }

    return QDialog::eventFilter (watched, e);
}

//==============================================================================
namespace
{
    int sliderSteps (const PluginParameterInfo& p)
    {
        if (p.isBoolean)
            return 1;
        return p.numSteps > 1 ? p.numSteps - 1 : 1000;
    }
}

ParameterDialog::ParameterDialog (AudioEngine& e, int s, QWidget* parent) : ParameterDialog (e, ChainPath { s, {} }, parent) {}

ParameterDialog::ParameterDialog (AudioEngine& e, ChainPath p, QWidget* parent)
    : QDialog (parent), engine (e), path (std::move (p)), pluginId (e.chainDevice (path).pluginId),
      filter (new QLineEdit (this)), rowsHost (new QWidget)
{
    const auto info = engine.chainDevice (path);
    setWindowTitle (tr ("%1 - Parameters").arg (QString::fromStdString (info.name)));
    resize (520, 560);

    filter->setPlaceholderText (tr ("Filter parameters"));
    filter->setAccessibleName (tr ("Filter parameters"));

    auto* scroll = new QScrollArea (this);
    scroll->setWidgetResizable (true);
    scroll->setWidget (rowsHost);

    auto* windowButton = new QPushButton (tr ("Plugin &Window"), this);
    windowButton->setAutoDefault (false);
    windowButton->setEnabled (engine.canShowChainWindow (path));
    auto* closeButtons = new QDialogButtonBox (QDialogButtonBox::Close, this);
    closeButtons->addButton (windowButton, QDialogButtonBox::ActionRole);

    auto* layout = new QVBoxLayout (this);
    if (phimark::isPhiPlugin (pluginId))
    {
        // PHI's own Φ effects carry the Φ mark and their name in a header.
        auto* header = new QHBoxLayout;
        auto* name = new QLabel (phimark::withoutPhi (QString::fromStdString (info.name)), this);
        auto f = name->font();
        f.setPointSizeF (f.pointSizeF() * 1.618);
        f.setBold (true);
        name->setFont (f);
        header->addWidget (new PhiMark (QFontMetrics (f).height() + 8, this));
        header->addWidget (name, 1);
        layout->addLayout (header);
    }
    layout->addWidget (filter);
    layout->addWidget (scroll, 1);
    layout->addWidget (closeButtons);

    connect (filter, &QLineEdit::textChanged, this, &ParameterDialog::applyFilter);
    connect (closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect (windowButton, &QPushButton::clicked, this, [this] { engine.showChainWindow (path); });

    rebuild();

    auto* timer = new QTimer (this);
    connect (timer, &QTimer::timeout, this, &ParameterDialog::syncFromPlugin);
    timer->start (150);

    if (! rows.empty())
        rows.front().slider->setFocus();
}

void ParameterDialog::rebuild()
{
    auto* grid = new QFormLayout (rowsHost);
    grid->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);

    const auto params = engine.chainParameters (path);
    for (int i = 0; i < (int) params.size(); ++i)
    {
        const auto& p = params[(size_t) i];
        const auto name = QString::fromStdString (p.name);

        auto* container = new QWidget (rowsHost);
        auto* row = new QHBoxLayout (container);
        row->setContentsMargins (0, 0, 0, 0);

        auto* slider = new QSlider (Qt::Horizontal, container);
        const auto steps = sliderSteps (p);
        slider->setRange (0, steps);
        slider->setPageStep (std::max (1, steps / 10));
        slider->setValue ((int) std::lround (p.value * steps));
        slider->setAccessibleName (name);

        auto* value = new QLabel (QString::fromStdString (p.valueText), container);
        value->setMinimumWidth (110);
        row->addWidget (slider, 1);
        row->addWidget (value);

        auto* label = new QLabel (name, rowsHost);
        label->setBuddy (slider);
        grid->addRow (label, container);

        connect (slider, &QSlider::valueChanged, this, [this, i, steps] (int v)
        {
            engine.setChainParameter (path, i, (float) v / (float) steps);
            syncFromPlugin();
        });

        rows.push_back ({ container, slider, value, name });
        Q_UNUSED (label);
    }

    if (params.empty())
        grid->addRow (new QLabel (tr ("This plugin has no parameters."), rowsHost));
}

void ParameterDialog::syncFromPlugin()
{
    // The slot changed under us (removed, replaced or moved): this dialog no longer applies.
    if (engine.chainDevice (path).pluginId != pluginId)
    {
        close();
        return;
    }

    const auto params = engine.chainParameters (path);
    for (size_t i = 0; i < rows.size() && i < params.size(); ++i)
    {
        const auto text = QString::fromStdString (params[i].valueText);
        if (rows[i].value->text() != text)
            rows[i].value->setText (text);

        if (! rows[i].slider->isSliderDown())
        {
            const QSignalBlocker blocker (rows[i].slider);
            rows[i].slider->setValue ((int) std::lround (params[i].value * rows[i].slider->maximum()));
        }
    }
}

void ParameterDialog::applyFilter()
{
    const auto text = filter->text().trimmed();
    auto* grid = static_cast<QFormLayout*> (rowsHost->layout());

    for (size_t i = 0; i < rows.size(); ++i)
        grid->setRowVisible ((int) i, text.isEmpty() || rows[i].name.contains (text, Qt::CaseInsensitive));
}

void ParameterDialog::keyPressEvent (QKeyEvent* e)
{
    // Typing a name anywhere jumps to the filter, like a search-as-you-type list.
    const auto text = e->text();
    if (! text.isEmpty() && text.at (0).isPrint() && ! text.at (0).isSpace()
        && (e->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier)) == Qt::NoModifier)
    {
        filter->setFocus (Qt::ShortcutFocusReason);
        filter->insert (text);
        return;
    }

    QDialog::keyPressEvent (e);
}
