// SPDX-License-Identifier: AGPL-3.0-only
// Tools > Manage Plug-ins and Reset Configuration.
#include "MainWindow.h"
#include "Settings.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

void MainWindow::managePlugins()
{
    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Manage Plug-ins"));
    dialog.resize (860, 560);

    auto* filter = new QLineEdit;
    filter->setPlaceholderText (tr ("Type to find a plug-in"));
    filter->setClearButtonEnabled (true);
    filter->setAccessibleName (tr ("Find a plug-in"));

    auto* list = new QTreeWidget;
    list->setAccessibleName (tr ("Plug-ins"));
    list->setRootIsDecorated (false);
    list->setUniformRowHeights (true);
    list->setHeaderLabels ({ tr ("Plug-in"), tr ("Maker"), tr ("Format"), tr ("Where") });
    list->header()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
    list->header()->setStretchLastSection (true);

    const auto plugins = engine.allPlugins();
    for (const auto& p : plugins)
    {
        auto* item = new QTreeWidgetItem (list, { QString::fromStdString (p.name), QString::fromStdString (p.manufacturer),
                                                 QString::fromStdString (p.format),
                                                 p.file.empty() ? tr ("Built in") : QString::fromStdString (p.file) });
        item->setFlags (item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState (0, p.enabled ? Qt::Checked : Qt::Unchecked);
        item->setData (0, Qt::UserRole, QString::fromStdString (p.id));
        item->setToolTip (3, item->text (3));
    }
    auto* count = new QLabel;
    const auto recount = [list, count]
    {
        int on = 0;
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            on += list->topLevelItem (i)->checkState (0) == Qt::Checked;
        count->setText (tr ("%1 of %2 plug-ins turned on. Turned off ones are not offered when you add a plug-in.").arg (on).arg (list->topLevelItemCount()));
    };
    recount();
    QObject::connect (list, &QTreeWidget::itemChanged, &dialog, [this, recount] (QTreeWidgetItem* item, int column)
    {
        if (column != 0)
            return;
        engine.setPluginEnabled (item->data (0, Qt::UserRole).toString().toStdString(), item->checkState (0) == Qt::Checked);
        recount();
    });
    // Space turns the current one on or off.
    QObject::connect (filter, &QLineEdit::textChanged, &dialog, [list] (const QString& text)
    {
        for (int i = 0; i < list->topLevelItemCount(); ++i)
        {
            auto* item = list->topLevelItem (i);
            bool match = text.isEmpty();
            for (int c = 0; c < 4 && ! match; ++c)
                match = item->text (c).contains (text, Qt::CaseInsensitive);
            item->setHidden (! match);
        }
    });

    auto* turnAll = new QHBoxLayout;
    for (const bool on : { true, false })
    {
        auto* b = new QPushButton (on ? tr ("Turn &All On") : tr ("Turn All O&ff"));
        b->setAutoDefault (false);
        QObject::connect (b, &QPushButton::clicked, &dialog, [list, on]
        {
            for (int i = 0; i < list->topLevelItemCount(); ++i)
                if (! list->topLevelItem (i)->isHidden())
                    list->topLevelItem (i)->setCheckState (0, on ? Qt::Checked : Qt::Unchecked);
        });
        turnAll->addWidget (b);
    }
    turnAll->addStretch();

    // Files that crashed or hung while being checked.
    auto* skipped = new QListWidget;
    skipped->setAccessibleName (tr ("Skipped plug-in files"));
    for (const auto& f : engine.skippedPluginFiles())
        skipped->addItem (QString::fromStdString (f));
    auto* skippedLabel = new QLabel (tr ("&Skipped: these files crashed or hung while being checked, so they are not checked again."));
    skippedLabel->setWordWrap (true);
    skippedLabel->setBuddy (skipped);
    auto* retry = new QPushButton (tr ("&Try Again at the Next Scan"));
    retry->setAutoDefault (false);
    retry->setEnabled (false);
    QObject::connect (skipped, &QListWidget::currentRowChanged, retry, [retry] (int row) { retry->setEnabled (row >= 0); });
    QObject::connect (retry, &QPushButton::clicked, &dialog, [this, skipped]
    {
        if (auto* item = skipped->currentItem())
        {
            engine.retrySkippedPlugin (item->text().toStdString());
            delete item;
        }
    });
    const bool anySkipped = skipped->count() > 0;
    skipped->setVisible (anySkipped);
    skippedLabel->setVisible (anySkipped);
    retry->setVisible (anySkipped);
    skipped->setMaximumHeight (110);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close);
    auto* rescan = buttons->addButton (tr ("&Rescan Plug-ins"), QDialogButtonBox::ActionRole);
    for (auto* b : buttons->buttons())
        if (auto* push = qobject_cast<QPushButton*> (b))
            push->setAutoDefault (false);   // Enter in the search box moves to the list instead of closing
    bool rescanAfter = false;
    QObject::connect (rescan, &QPushButton::clicked, &dialog, [&] { rescanAfter = true; dialog.accept(); });
    QObject::connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* layout = new QVBoxLayout (&dialog);
    layout->addWidget (filter);
    layout->addWidget (list, 1);
    layout->addWidget (count);
    layout->addLayout (turnAll);
    layout->addWidget (skippedLabel);
    layout->addWidget (skipped);
    layout->addWidget (retry, 0, Qt::AlignLeft);
    layout->addWidget (buttons);
    filter->setFocus();
    QObject::connect (filter, &QLineEdit::returnPressed, list, [list] { list->setFocus(); });

    dialog.exec();
    if (rescanAfter)
        QTimer::singleShot (0, this, [this] { commands.action (QStringLiteral ("master.scan"))->trigger(); });
    waveform->setFocus();
}

//==============================================================================
MainWindow::Reset MainWindow::pendingReset;

void MainWindow::resetConfiguration()
{
    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Reset Configuration"));
    auto* about = new QLabel (tr ("<p>Puts " APP_DISPLAY_NAME " back the way it was when first installed: window layout and panels, "
                                  "theme, waveform colours, spectrogram settings, audio device choice, and every value the windows "
                                  "remember. " APP_DISPLAY_NAME " then restarts.</p>"
                                  "<p>Your files, macros, Master Section and plug-in settings are kept.</p>"));
    about->setWordWrap (true);
    auto* keys = new QCheckBox (tr ("Also reset the &keyboard shortcuts"));
    auto* plugins = new QCheckBox (tr ("Also forget the &plug-in list, and turn every plug-in back on (they are found again at the next scan)"));
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Cancel);
    auto* reset = buttons->addButton (tr ("&Reset and Restart"), QDialogButtonBox::DestructiveRole);
    QObject::connect (reset, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout (&dialog);
    layout->addWidget (about);
    layout->addWidget (keys);
    layout->addWidget (plugins);
    layout->addWidget (buttons);
    dialog.resize (520, dialog.sizeHint().height());
    buttons->button (QDialogButtonBox::Cancel)->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return;

    pendingReset = { true, keys->isChecked(), plugins->isChecked() };
    if (! close())   // asks about unsaved files; the reset happens once the window is gone (see finishReset)
        pendingReset = {};
}

bool MainWindow::finishReset()
{
    if (! pendingReset.requested)
        return false;
    auto settings = appSettings();
    settings.clear();
    settings.sync();
    const auto config = QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) + QStringLiteral ("/" APP_ID "/");
    if (pendingReset.keys)
        QFile::remove (CommandRegistry::keymapPath());
    if (pendingReset.plugins)
        for (const auto* f : { "plugins.xml", "disabled-plugins.txt", "plugin-scan-in-progress.txt" })
            QFile::remove (config + QLatin1String (f));
    pendingReset = {};
    return true;
}

//==============================================================================
void moveOldSettings()
{
    // Before PHI it was PHINYX, with its folders named phinyx: the settings,
    // plugin list, themes, HRTF sets, recovery files and the rest move over, once.
    for (const auto location : { QStandardPaths::GenericConfigLocation, QStandardPaths::GenericDataLocation, QStandardPaths::GenericCacheLocation })
    {
        const QDir base (QStandardPaths::writableLocation (location));
        if (base.exists (QStringLiteral ("phinyx")) && ! base.exists (QStringLiteral (APP_ID)))
            base.rename (QStringLiteral ("phinyx"), QStringLiteral (APP_ID));
    }

    QSettings old (QStringLiteral ("Unknown Organization"), QStringLiteral ("phinyx"));   // Qt's default place, which only earlier versions wrote to
    if (! QFile::exists (old.fileName()))
        return;
    auto settings = appSettings();
    for (const auto& key : old.allKeys())
        if (! settings.contains (key))
            settings.setValue (key, old.value (key));
    settings.sync();
    const auto file = old.fileName();
    old.clear();
    old.sync();
    QFile::remove (file);
    QDir().rmdir (QFileInfo (file).absolutePath());   // only if nothing else is in it
}
