// SPDX-License-Identifier: AGPL-3.0-only
// The Research menu (owner, 2026-10-29): short references on the ideas behind PHI, each what it
// is and its history, with links for further reading. Not a how-to; that is the manual's.
// The entries are in share/research/references.md, which the owner curates: "# Group" makes a
// submenu, "## Entry" an item, and the Markdown under it is what the entry's window shows.
#include "MainWindow.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
    // Your own copy, which Edit References makes; it wins over the one PHI ships with.
    QString userReferences()
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/research/references.md");
    }

    // The shipped list: next to the installed binary, else the source tree.
    QString shippedReferences()
    {
        const auto installed = QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHI_SHARE_RELATIVE "/research/references.md"));
        if (QFileInfo::exists (installed))
            return QDir::cleanPath (installed);
        return QStringLiteral (PHI_SHARE_SOURCE_DIR "/research/references.md");
    }

    QString referencesPath()
    {
        const auto mine = userReferences();
        return QFileInfo::exists (mine) ? mine : shippedReferences();
    }

    struct Entry
    {
        QString group, title, markdown;   // markdown: the entry's heading and all under it
    };

    // Read again each time the menu opens, so edits to the file show at once.
    QList<Entry> readReferences()
    {
        QFile file (referencesPath());
        if (! file.open (QIODevice::ReadOnly | QIODevice::Text))
            return {};
        auto text = QString::fromUtf8 (file.readAll());
        text.remove (QRegularExpression (QStringLiteral ("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption));

        QList<Entry> entries;
        QString group;
        for (const auto& line : text.split (QLatin1Char ('\n')))
        {
            if (line.startsWith (QLatin1String ("# ")))
                group = line.mid (2).trimmed();
            else if (line.startsWith (QLatin1String ("## ")))
                entries.append ({ group, line.mid (3).trimmed(), line + QLatin1Char ('\n') });
            else if (! entries.isEmpty())
                entries.last().markdown += line + QLatin1Char ('\n');
        }
        return entries;
    }

    QString menuText (QString s)
    {
        return s.replace (QLatin1Char ('&'), QLatin1String ("&&"));
    }
}

void MainWindow::registerResearchCommands()
{
    const auto category = tr ("Research");
    commands.add (QStringLiteral ("research.all"), category, tr ("&All References..."), {}, [this]
    {
        QString markdown;
        QString group;
        for (const auto& e : readReferences())
        {
            if (e.group != group)
            {
                group = e.group;
                if (! group.isEmpty())
                    markdown += QStringLiteral ("# %1\n\n").arg (group);
            }
            markdown += e.markdown + QLatin1Char ('\n');
        }
        showResearchEntry (tr ("References"), markdown);
    });
    commands.add (QStringLiteral ("research.edit"), category, tr ("&Edit References..."), {}, [this]
    {
        // Your own copy, made from the shipped one the first time.
        const auto mine = userReferences();
        if (! QFileInfo::exists (mine))
        {
            QDir().mkpath (QFileInfo (mine).absolutePath());
            QFile::copy (shippedReferences(), mine);
            QFile (mine).setPermissions (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        }
        QDesktopServices::openUrl (QUrl::fromLocalFile (mine));
        statusBar()->showMessage (tr ("The Research menu follows this file as soon as you save it"), 8000);
    });
}

void MainWindow::fillResearchMenu (QMenu* menu)
{
    menu->clear();
    QMap<QString, QMenu*> groups;
    for (const auto& entry : readReferences())
    {
        auto* into = menu;
        if (! entry.group.isEmpty())
        {
            if (! groups.contains (entry.group))
                groups[entry.group] = menu->addMenu (menuText (entry.group));
            into = groups[entry.group];
        }
        const auto title = entry.title;
        const auto markdown = entry.markdown;
        auto* action = into->addAction (menuText (title));
        connect (action, &QAction::triggered, this, [this, title, markdown] { showResearchEntry (title, markdown); });
    }
    if (! menu->isEmpty())
        menu->addSeparator();
    menu->addAction (commands.action (QStringLiteral ("research.all")));
    menu->addAction (commands.action (QStringLiteral ("research.edit")));
}

void MainWindow::showResearchEntry (const QString& title, const QString& markdown)
{
    // Not modal, so it can stay open beside the editor.
    auto* dialog = new QDialog (this);
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setWindowTitle (title);
    dialog->resize (720, 600);

    auto* find = new QLineEdit (dialog);
    find->setPlaceholderText (tr ("Type to find; Enter finds the next one"));
    find->setClearButtonEnabled (true);
    find->setAccessibleName (tr ("Find in this reference"));

    auto* browser = new QTextBrowser (dialog);
    browser->setAccessibleName (title);
    browser->setOpenExternalLinks (true);
    browser->setMarkdown (markdown);

    const auto findNext = [browser, find] (bool fromTop)
    {
        if (find->text().isEmpty())
            return;
        if (fromTop)
            browser->moveCursor (QTextCursor::Start);
        if (! browser->find (find->text()))
        {
            browser->moveCursor (QTextCursor::Start);   // round to the top
            browser->find (find->text());
        }
    };
    connect (find, &QLineEdit::textChanged, dialog, [findNext] { findNext (true); });
    connect (find, &QLineEdit::returnPressed, dialog, [findNext] { findNext (false); });

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, dialog);
    connect (buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);

    auto* layout = new QVBoxLayout (dialog);
    layout->addWidget (find);
    layout->addWidget (browser, 1);
    layout->addWidget (buttons);
    dialog->show();
}
