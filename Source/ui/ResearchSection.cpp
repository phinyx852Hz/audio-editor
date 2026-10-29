// SPDX-License-Identifier: AGPL-3.0-only
// The Research menu (owner, 2026-10-29): short references on the ideas behind PHI, each what it
// is and its history, with links for further reading. Not a how-to; that is the manual's.
// The entries are in share/research/references.md, which the owner curates: "# Group" makes a
// submenu, "## Entry" an item, and the Markdown under it is what its panel shows.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Panels.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QRegularExpression>
#include <QShortcut>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

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

// The research panel: the entry, with a find box, in the left-hand column.
class ResearchView final : public QWidget
{
public:
    ResearchView (std::function<void()> onClose, QWidget* parent) : QWidget (parent)
    {
        heading = new QLabel (this);
        heading->setWordWrap (true);
        auto font = heading->font();
        font.setBold (true);
        heading->setFont (font);
        auto* close = new QToolButton (this);
        close->setText (QStringLiteral ("✕"));
        close->setAutoRaise (true);
        close->setToolTip (MainWindow::tr ("Close the research panel; the panel it replaced comes back (Esc)"));
        close->setAccessibleName (MainWindow::tr ("Close research"));
        connect (close, &QToolButton::clicked, this, [onClose] { onClose(); });
        auto* escape = new QShortcut (QKeySequence (Qt::Key_Escape), this);
        escape->setContext (Qt::WidgetWithChildrenShortcut);
        connect (escape, &QShortcut::activated, this, [onClose] { onClose(); });

        find = new QLineEdit (this);
        find->setPlaceholderText (MainWindow::tr ("Type to find; Enter finds the next one"));
        find->setClearButtonEnabled (true);
        find->setAccessibleName (MainWindow::tr ("Find in this reference"));

        browser = new QTextBrowser (this);
        browser->setOpenExternalLinks (true);
        browser->setFrameShape (QFrame::NoFrame);

        const auto findNext = [this] (bool fromTop)
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
        connect (find, &QLineEdit::textChanged, this, [findNext] { findNext (true); });
        connect (find, &QLineEdit::returnPressed, this, [findNext] { findNext (false); });

        auto* top = new QHBoxLayout;
        top->addWidget (heading, 1);
        top->addWidget (close, 0, Qt::AlignTop);
        auto* layout = new QVBoxLayout (this);
        layout->setContentsMargins (6, 6, 6, 6);
        layout->addLayout (top);
        layout->addWidget (find);
        layout->addWidget (browser, 1);
        setMinimumWidth (270);
    }

    void setEntry (const QString& title, const QString& markdown)
    {
        heading->setText (title);
        browser->setAccessibleName (title);
        browser->setMarkdown (markdown);
        browser->moveCursor (QTextCursor::Start);
        find->clear();
    }

    QWidget* reading() const { return browser; }

private:
    QLabel* heading = nullptr;
    QLineEdit* find = nullptr;
    QTextBrowser* browser = nullptr;
};

void MainWindow::showResearchEntry (const QString& title, const QString& markdown)
{
    if (researchPanel == nullptr)
    {
        researchView = new ResearchView ([this] { closeResearch(); }, this);
        researchPanel = new DockablePanel (*this, QStringLiteral ("researchPanel"), tr ("Research"), researchView, Qt::LeftDockWidgetArea);
        focusFollowsMouse->addAreas ({ researchView });
        // Closed from its title bar too, the panel it replaced comes back.
        connect (researchPanel->dock(), &QDockWidget::visibilityChanged, this, [this] (bool visible)
        {
            if (! visible && researchShowing && ! researchPanel->isShown())
                closeResearch();
        });
    }
    researchView->setEntry (title, markdown);

    // It takes the place of what is on the left now (owner, 2026-10-29): the Find panel, or
    // the docked mixer's Inspector. Opening another entry keeps what it is to give back.
    if (! researchShowing)
    {
        researchShowing = true;
        researchHidMixerSide = mixerWin != nullptr && mixerDocked() && centralStack->currentWidget() == mixerWin;
        researchHidFind = findPanel->isShown();
        if (researchHidFind)
        {
            if (! findPanel->isPoppedOut() && ! findPanel->dock()->isFloating())
                addDockWidget (dockWidgetArea (findPanel->dock()), researchPanel->dock());
            hidingFindForMixer = true;   // not a hand-closed Find, so the mixer's Inspector stays put
            findPanel->hide();
            hidingFindForMixer = false;
        }
        if (researchHidMixerSide)
            mixerWin->hideSideColumn (true);
    }
    researchPanel->show (false);
    researchView->reading()->setFocus();
}

void MainWindow::closeResearch()
{
    if (! researchShowing)
        return;
    researchShowing = false;
    researchPanel->hide();
    if (researchHidMixerSide && mixerWin != nullptr)
        mixerWin->hideSideColumn (false);   // back to the Inspector, or the Finder it had
    if (researchHidFind && ! findPanel->isShown())
        findPanel->show (false);
    researchHidMixerSide = researchHidFind = false;
}
