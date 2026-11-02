// SPDX-License-Identifier: AGPL-3.0-only
#include "ChainPresets.h"
#include "../engine/AudioEngine.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace chainpresets
{
    QString factoryFolder()
    {
        // Next to the installed binary, else the source tree.
        const auto installed = QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHI_SHARE_RELATIVE "/master-presets"));
        if (QFileInfo (installed).isDir())
            return QDir::cleanPath (installed);
        return QStringLiteral (PHI_SHARE_SOURCE_DIR "/master-presets");
    }

    QString userFolder()
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/master-presets");
    }

    std::vector<Entry> all()
    {
        std::vector<Entry> out;
        auto scan = [&] (const QString& folder, bool factory)
        {
            std::vector<Entry> found;
            QDirIterator it (folder, { QStringLiteral ("*.phchain") }, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
            {
                const auto path = it.next();
                const auto info = AudioEngine::masterPresetInfo (path.toStdString());
                if (! info.valid)
                    continue;
                Entry e;
                e.path = path;
                e.name = QString::fromStdString (info.name);
                e.description = QString::fromStdString (info.description);
                for (const auto& p : info.plugins)
                    e.plugins << QString::fromStdString (p);
                e.factory = factory;
                const auto sub = QDir (folder).relativeFilePath (QFileInfo (path).absolutePath());
                const auto base = factory ? QCoreApplication::translate ("ChainPresets", "Factory")
                                          : QCoreApplication::translate ("ChainPresets", "Yours");
                e.group = sub == QStringLiteral (".") ? base : base + QStringLiteral (": ") + sub;
                found.push_back (e);
            }
            std::sort (found.begin(), found.end(), [] (const Entry& a, const Entry& b)
            {
                return a.group != b.group ? a.group < b.group : a.name.compare (b.name, Qt::CaseInsensitive) < 0;
            });
            out.insert (out.end(), found.begin(), found.end());
        };
        scan (factoryFolder(), true);
        scan (userFolder(), false);
        return out;
    }

    bool load (AudioEngine& engine, QWidget* parent, const QString& path)
    {
        const auto info = AudioEngine::masterPresetInfo (path.toStdString());
        if (! info.valid)
        {
            QMessageBox::warning (parent, QCoreApplication::translate ("ChainPresets", "Chain Preset"),
                                  QCoreApplication::translate ("ChainPresets", "This is not a PHI chain preset:\n%1").arg (path));
            return false;
        }
        bool any = false;
        for (int slot = 0; slot < AudioEngine::numMasterSlots; ++slot)
            any = any || ! engine.masterSlot (slot).empty;
        if (any && QMessageBox::question (parent, QCoreApplication::translate ("ChainPresets", "Load Chain Preset"),
                                          QCoreApplication::translate ("ChainPresets", "Load \"%1\" in place of the plugins in the Master Section now?\n"
                                                                                       "Save Chain Preset first to keep them.")
                                              .arg (QString::fromStdString (info.name)))
                       != QMessageBox::Yes)
            return false;

        QApplication::setOverrideCursor (Qt::WaitCursor);
        const auto problems = QString::fromStdString (engine.loadMasterPreset (path.toStdString()));
        QApplication::restoreOverrideCursor();
        if (! problems.isEmpty())
            QMessageBox::warning (parent, QCoreApplication::translate ("ChainPresets", "Chain Preset"),
                                  QCoreApplication::translate ("ChainPresets", "\"%1\" loaded, but not all of it:\n%2")
                                      .arg (QString::fromStdString (info.name), problems));
        return true;
    }

    QString saveCurrent (AudioEngine& engine, QWidget* parent)
    {
        QDialog dialog (parent);
        dialog.setWindowTitle (QCoreApplication::translate ("ChainPresets", "Save Chain Preset"));
        auto* form = new QFormLayout (&dialog);
        auto* name = new QLineEdit (&dialog);
        auto* description = new QPlainTextEdit (&dialog);
        description->setPlaceholderText (QCoreApplication::translate ("ChainPresets", "What it is for, what it is made of (optional)"));
        description->setMaximumHeight (100);
        form->addRow (QCoreApplication::translate ("ChainPresets", "&Name:"), name);
        form->addRow (QCoreApplication::translate ("ChainPresets", "&Description:"), description);
        auto* buttons = new QDialogButtonBox (QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        form->addRow (buttons);
        QObject::connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
            return {};

        // The file is named after the preset, without what a file name cannot hold.
        auto fileName = name->text().trimmed();
        fileName.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("-"));
        QDir().mkpath (userFolder());
        const auto path = userFolder() + QLatin1Char ('/') + fileName + QStringLiteral (".phchain");
        if (QFile::exists (path)
            && QMessageBox::question (parent, QCoreApplication::translate ("ChainPresets", "Save Chain Preset"),
                                      QCoreApplication::translate ("ChainPresets", "There is a preset named \"%1\" already. Replace it?").arg (name->text().trimmed()))
                   != QMessageBox::Yes)
            return {};
        const auto error = engine.saveMasterPreset (path.toStdString(), name->text().trimmed().toStdString(),
                                                    description->toPlainText().trimmed().toStdString());
        if (! error.empty())
        {
            QMessageBox::warning (parent, QCoreApplication::translate ("ChainPresets", "Save Chain Preset"), QString::fromStdString (error));
            return {};
        }
        return path;
    }
}

//==============================================================================
ChainPresetDialog::ChainPresetDialog (AudioEngine& e, QWidget* parent) : QDialog (parent), engine (e)
{
    setWindowTitle (tr ("Chain Presets"));
    resize (720, 480);
    auto* layout = new QVBoxLayout (this);

    list = new QTreeWidget (this);
    list->setHeaderLabels ({ tr ("Preset"), tr ("Plugins") });
    list->header()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    list->setRootIsDecorated (true);
    layout->addWidget (list, 3);

    about = new QLabel (this);
    about->setWordWrap (true);
    about->setAlignment (Qt::AlignTop | Qt::AlignLeft);
    about->setTextInteractionFlags (Qt::TextSelectableByMouse);
    about->setMinimumHeight (90);
    layout->addWidget (about, 1);

    auto* buttons = new QDialogButtonBox (this);
    loadButton = buttons->addButton (tr ("&Load"), QDialogButtonBox::AcceptRole);
    deleteButton = buttons->addButton (tr ("&Delete"), QDialogButtonBox::DestructiveRole);
    buttons->addButton (QDialogButtonBox::Close);
    layout->addWidget (buttons);

    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (loadButton, &QPushButton::clicked, this, [this] { loadChosen(); });
    connect (deleteButton, &QPushButton::clicked, this, [this] { deleteChosen(); });
    connect (list, &QTreeWidget::currentItemChanged, this, [this] { showChosen(); });
    connect (list, &QTreeWidget::itemActivated, this, [this] { loadChosen(); });

    fill();
}

void ChainPresetDialog::fill()
{
    list->clear();
    std::map<QString, QTreeWidgetItem*> groups;
    QTreeWidgetItem* first = nullptr;
    for (const auto& p : chainpresets::all())
    {
        auto*& group = groups[p.group];
        if (group == nullptr)
        {
            group = new QTreeWidgetItem (list, { p.group });
            group->setFlags (Qt::ItemIsEnabled);
            group->setExpanded (true);
        }
        auto* item = new QTreeWidgetItem (group, { p.name, p.plugins.join (QStringLiteral (" → ")) });
        item->setData (0, Qt::UserRole, p.path);
        item->setData (0, Qt::UserRole + 1, p.description);
        item->setData (0, Qt::UserRole + 2, p.factory);
        item->setToolTip (1, p.plugins.join (QLatin1Char ('\n')));
        if (first == nullptr)
            first = item;
    }
    if (first != nullptr)
        list->setCurrentItem (first);
    else
        about->setText (tr ("No chain presets yet. Build a chain in the Master Section, then Master > Save Chain Preset."));
    showChosen();
}

void ChainPresetDialog::showChosen()
{
    auto* item = list->currentItem();
    const auto path = item != nullptr ? item->data (0, Qt::UserRole).toString() : QString();
    loadButton->setEnabled (! path.isEmpty());
    deleteButton->setEnabled (! path.isEmpty() && ! item->data (0, Qt::UserRole + 2).toBool());
    if (path.isEmpty())
        return;
    auto text = item->data (0, Qt::UserRole + 1).toString().toHtmlEscaped().replace (QLatin1Char ('\n'), QStringLiteral ("<br>"));
    QStringList chain;
    for (const auto& p : item->text (1).split (QStringLiteral (" → ")))
        chain << p.toHtmlEscaped();
    about->setText (QStringLiteral ("<b>%1</b><br>%2<br><br>%3").arg (item->text (0).toHtmlEscaped(), text,
                                                                       tr ("Chain: %1").arg (chain.join (QStringLiteral (" → ")))));
}

void ChainPresetDialog::loadChosen()
{
    auto* item = list->currentItem();
    const auto path = item != nullptr ? item->data (0, Qt::UserRole).toString() : QString();
    if (path.isEmpty() || ! chainpresets::load (engine, this, path))
        return;
    loaded = true;
    accept();
}

void ChainPresetDialog::deleteChosen()
{
    auto* item = list->currentItem();
    if (item == nullptr || item->data (0, Qt::UserRole + 2).toBool())
        return;
    const auto path = item->data (0, Qt::UserRole).toString();
    if (QMessageBox::question (this, tr ("Delete Chain Preset"), tr ("Delete your preset \"%1\"?").arg (item->text (0))) != QMessageBox::Yes)
        return;
    QFile::remove (path);
    fill();
}
