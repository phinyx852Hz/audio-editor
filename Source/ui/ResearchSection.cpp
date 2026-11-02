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
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QPair>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace
{
    // Your own copies, which Edit References makes; each wins over the shipped file of its name.
    QString userFolder()
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/research");
    }

    // The shipped references: next to the installed binary, else the source tree.
    QString shippedFolder()
    {
        const auto installed = QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHI_SHARE_RELATIVE "/research"));
        if (QFileInfo::exists (installed + QStringLiteral ("/references.md")))
            return QDir::cleanPath (installed);
        return QStringLiteral (PHI_SHARE_SOURCE_DIR "/research");
    }

    // Every references file: references.md first, then the rest (modes-europe.md...) by name.
    QStringList referenceFiles()
    {
        QMap<QString, QString> byName;
        for (const auto& folder : { shippedFolder(), userFolder() })   // yours last, so they win
            for (const auto& info : QDir (folder).entryInfoList ({ QStringLiteral ("*.md") }, QDir::Files))
                byName[info.fileName()] = info.absoluteFilePath();
        QStringList files;
        if (byName.contains (QStringLiteral ("references.md")))
            files << byName.take (QStringLiteral ("references.md"));
        return files + byName.values();
    }

    struct Entry
    {
        QString group, title, markdown;   // group: "Modes / Europe" nests; markdown: the entry's heading and all under it
    };

    // Read again each time the menu opens, so edits to the files show at once.
    QList<Entry> readReferences()
    {
        QList<Entry> entries;
        for (const auto& path : referenceFiles())
        {
            QFile file (path);
            if (! file.open (QIODevice::ReadOnly | QIODevice::Text))
                continue;
            auto text = QString::fromUtf8 (file.readAll());
            text.remove (QRegularExpression (QStringLiteral ("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption));

            QString group;
            bool inEntry = false;
            for (const auto& line : text.split (QLatin1Char ('\n')))
            {
                if (line.startsWith (QLatin1String ("# ")))
                {
                    group = line.mid (2).trimmed();
                    inEntry = false;
                }
                else if (line.startsWith (QLatin1String ("## ")))
                {
                    entries.append ({ group, line.mid (3).trimmed(), line + QLatin1Char ('\n') });
                    inEntry = true;
                }
                else if (inEntry)
                    entries.last().markdown += line + QLatin1Char ('\n');
            }
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
    commands.add (QStringLiteral ("research.search"), category, tr ("&Search Research..."), {}, [this] { openResearchSearch(); });
    commands.add (QStringLiteral ("research.edit"), category, tr ("&Edit References..."), {}, [this]
    {
        // Your own copies, made from the shipped ones the first time; new shipped files join them.
        const auto mine = userFolder();
        QDir().mkpath (mine);
        for (const auto& info : QDir (shippedFolder()).entryInfoList ({ QStringLiteral ("*.md") }, QDir::Files))
        {
            const auto copy = mine + QLatin1Char ('/') + info.fileName();
            if (! QFileInfo::exists (copy) && QFile::copy (info.absoluteFilePath(), copy))
                QFile (copy).setPermissions (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        }
        QDesktopServices::openUrl (QUrl::fromLocalFile (mine));
        statusBar()->showMessage (tr ("The Research menu follows these files as soon as you save them"), 8000);
    });
}

void MainWindow::fillResearchMenu (QMenu* menu)
{
    menu->clear();
    QMap<QString, QMenu*> groups;
    for (const auto& entry : readReferences())
    {
        // "Modes / Europe" is Europe inside Modes.
        auto* into = menu;
        QString path;
        for (const auto& part : entry.group.split (QLatin1Char ('/'), Qt::SkipEmptyParts))
        {
            path += QLatin1Char ('/') + part.trimmed();
            if (! groups.contains (path))
                groups[path] = into->addMenu (menuText (part.trimmed()));
            into = groups[path];
        }
        const auto title = entry.title;
        const auto markdown = entry.markdown;
        auto* action = into->addAction (menuText (title));
        connect (action, &QAction::triggered, this, [this, title, markdown] { showResearchEntry (title, markdown); });
    }
    if (! menu->isEmpty())
        menu->addSeparator();
    menu->addAction (commands.action (QStringLiteral ("research.search")));
    menu->addAction (commands.action (QStringLiteral ("research.all")));
    menu->addAction (commands.action (QStringLiteral ("research.edit")));
}

// An entry's text. Its images ("![...](modality-diagram.png)") come from the references' folders,
// yours first, and shrink to the panel's width; they fit again when the panel is resized.
class ResearchBrowser final : public QTextBrowser
{
public:
    using QTextBrowser::QTextBrowser;

    void display (const QString& markdown)
    {
        shown = markdown;
        hasImages = false;
        document()->clear();   // forgets the images, so they load again at the new width
        setMarkdown (markdown);
    }

    QVariant loadResource (int type, const QUrl& name) override
    {
        if (type != QTextDocument::ImageResource || ! name.isRelative())
            return QTextBrowser::loadResource (type, name);
        const auto file = QFileInfo (name.path()).fileName();
        for (const auto& folder : { userFolder(), shippedFolder() })
        {
            QImage image (folder + QLatin1Char ('/') + file);
            if (image.isNull())
                continue;
            hasImages = true;
            const auto room = viewport()->width() - 2 * int (document()->documentMargin()) - 4;
            if (room > 0 && image.width() > room)
                image = image.scaledToWidth (room, Qt::SmoothTransformation);
            return QVariant::fromValue (image);
        }
        return {};
    }

protected:
    void resizeEvent (QResizeEvent* e) override
    {
        QTextBrowser::resizeEvent (e);
        if (hasImages && e->oldSize().width() != e->size().width())
        {
            const auto at = verticalScrollBar()->value();
            display (shown);
            verticalScrollBar()->setValue (at);
        }
    }

private:
    QString shown;
    bool hasImages = false;
};

// The research panel: a search box, then the entry (or the search's results) with its links.
class ResearchView final : public QWidget
{
public:
    struct Hooks
    {
        std::function<void()> close, undock;
        std::function<void (const QString&)> search;
        std::function<void (const QUrl&)> link;   // a research: link, to another entry
        std::function<void (const QString&)> run; // a phi: link, a menu command by its id ("phi:compose.organum")
    };

    ResearchView (Hooks h, QWidget* parent) : QWidget (parent), hooks (std::move (h))
    {
        back = button (QStringLiteral ("←"), MainWindow::tr ("Back to what was showing before"), MainWindow::tr ("Back"));
        connect (back, &QToolButton::clicked, this, [this] { goBack(); });
        heading = new QLabel (this);
        heading->setWordWrap (true);
        auto font = heading->font();
        font.setBold (true);
        heading->setFont (font);
        undock = button (QStringLiteral ("⧉"), {}, MainWindow::tr ("Undock research"));
        connect (undock, &QToolButton::clicked, this, [this] { hooks.undock(); });
        auto* close = button (QStringLiteral ("✕"), MainWindow::tr ("Close the research panel; the panel it replaced comes back (Esc)"),
                              MainWindow::tr ("Close research"));
        connect (close, &QToolButton::clicked, this, [this] { hooks.close(); });
        auto* escape = new QShortcut (QKeySequence (Qt::Key_Escape), this);
        escape->setContext (Qt::WidgetWithChildrenShortcut);
        connect (escape, &QShortcut::activated, this, [this] { hooks.close(); });

        searchBox = new QLineEdit (this);
        searchBox->setPlaceholderText (MainWindow::tr ("Search research: any term, then Enter"));
        searchBox->setClearButtonEnabled (true);
        searchBox->setAccessibleName (MainWindow::tr ("Search research"));
        connect (searchBox, &QLineEdit::returnPressed, this, [this]
        {
            if (! searchBox->text().trimmed().isEmpty())
                hooks.search (searchBox->text().trimmed());
        });

        browser = new ResearchBrowser (this);
        browser->setOpenLinks (false);   // research: links open entries here, phi: links run a command, the rest the web browser
        browser->setFrameShape (QFrame::NoFrame);
        connect (browser, &QTextBrowser::anchorClicked, this, [this] (const QUrl& url)
        {
            if (url.scheme() == QLatin1String ("research"))
                hooks.link (url);
            else if (url.scheme() == QLatin1String ("phi"))
                hooks.run (url.path (QUrl::FullyDecoded));
            else
                QDesktopServices::openUrl (url);
        });

        auto* top = new QHBoxLayout;
        top->addWidget (back, 0, Qt::AlignTop);
        top->addWidget (heading, 1);
        top->addWidget (undock, 0, Qt::AlignTop);
        top->addWidget (close, 0, Qt::AlignTop);
        auto* layout = new QVBoxLayout (this);
        layout->setContentsMargins (6, 6, 6, 6);
        layout->addWidget (searchBox);
        layout->addLayout (top);
        layout->addWidget (browser, 1);
        setMinimumWidth (270);
        updateButtons (false);
    }

    void setEntry (const QString& title, const QString& markdown)
    {
        if (! shownTitle.isEmpty() && shownTitle != title)
            history.append ({ shownTitle, shownMarkdown });
        display (title, markdown);
    }

    void updateButtons (bool poppedOut)
    {
        back->setEnabled (! history.isEmpty());
        undock->setToolTip (poppedOut ? MainWindow::tr ("Dock the research panel back on the left")
                                      : MainWindow::tr ("Undock the research panel into a window of its own"));
    }

    QWidget* reading() const { return browser; }
    QLineEdit* search() const { return searchBox; }

private:
    QToolButton* button (const QString& text, const QString& tip, const QString& name)
    {
        auto* b = new QToolButton (this);
        b->setText (text);
        b->setAutoRaise (true);
        b->setToolTip (tip);
        b->setAccessibleName (name);
        return b;
    }

    void display (const QString& title, const QString& markdown)
    {
        shownTitle = title;
        shownMarkdown = markdown;
        heading->setText (title);
        browser->setAccessibleName (title);
        browser->display (markdown);
        browser->moveCursor (QTextCursor::Start);
        back->setEnabled (! history.isEmpty());
    }

    void goBack()
    {
        if (history.isEmpty())
            return;
        const auto [title, markdown] = history.takeLast();
        display (title, markdown);
    }

    Hooks hooks;
    QToolButton* back = nullptr;
    QToolButton* undock = nullptr;
    QLabel* heading = nullptr;
    QLineEdit* searchBox = nullptr;
    ResearchBrowser* browser = nullptr;
    QString shownTitle, shownMarkdown;
    QList<QPair<QString, QString>> history;
};

namespace
{
    // The first sentence of an entry, for a search result.
    QString firstSentence (const QString& markdown)
    {
        auto text = markdown.section (QLatin1Char ('\n'), 1).trimmed();
        text = text.section (QStringLiteral ("\n\n"), 0, 0).simplified();
        text.remove (QRegularExpression (QStringLiteral ("[*_`]")));
        const auto end = text.indexOf (QStringLiteral (". "));
        if (end > 0)
            text = text.left (end + 1);
        return text.length() > 220 ? text.left (217) + QStringLiteral ("...") : text;
    }

    QString researchLink (const QString& title)
    {
        return QStringLiteral ("research:") + QString::fromLatin1 (QUrl::toPercentEncoding (title));
    }
}

void MainWindow::searchResearch (const QString& term)
{
    const auto words = term.split (QLatin1Char (' '), Qt::SkipEmptyParts);
    const auto has = [&words] (const QString& text)
    {
        for (const auto& w : words)
            if (! text.contains (w, Qt::CaseInsensitive))
                return false;
        return ! words.isEmpty();
    };

    const auto entries = readReferences();
    for (const auto& e : entries)
        if (e.title.compare (term, Qt::CaseInsensitive) == 0)
        {
            showResearchEntry (e.title, e.markdown);   // the very entry: straight to it
            return;
        }

    // References whose title has the term first, then those that mention it.
    QStringList inTitle, inText;
    for (const auto& e : entries)
    {
        const auto line = QStringLiteral ("- [%1](%2): %3").arg (e.title, researchLink (e.title), firstSentence (e.markdown));
        if (has (e.title))
            inTitle << line;
        else if (has (e.markdown))
            inText << line;
    }
    // What in PHI goes by that name, so a term met in a menu can be looked up.
    QStringList inPhi;
    for (const auto& c : commands.all())
        if (has (c.name()) && ! c.id.startsWith (QLatin1String ("research.")))
            inPhi << QStringLiteral ("- %1 (%2)").arg (c.name(), c.category);
    inPhi.removeDuplicates();
    if (inPhi.size() > 12)
        inPhi = inPhi.mid (0, 12) << QStringLiteral ("- ...");

    const auto query = QString::fromLatin1 (QUrl::toPercentEncoding (term));
    QString md;
    if (inTitle.isEmpty() && inText.isEmpty())
        md += tr ("No reference mentions \"%1\" yet.").arg (term) + QStringLiteral ("\n\n");
    else
        md += QStringLiteral ("## %1\n\n%2\n\n").arg (tr ("References"), (inTitle + inText).join (QLatin1Char ('\n')));
    if (! inPhi.isEmpty())
        md += QStringLiteral ("## %1\n\n%2\n\n").arg (tr ("In PHI"), inPhi.join (QLatin1Char ('\n')));
    md += QStringLiteral ("## %1\n\n").arg (tr ("Further reading"));
    md += QStringLiteral ("- [Wikipedia](https://en.wikipedia.org/w/index.php?search=%1)\n").arg (query);
    md += QStringLiteral ("- [Google Scholar](https://scholar.google.com/scholar?q=%1)\n").arg (query);
    md += QStringLiteral ("- [JSTOR](https://www.jstor.org/action/doBasicSearch?Query=%1)\n").arg (query);
    md += QStringLiteral ("- [Internet Archive](https://archive.org/search?query=%1)\n").arg (query);
    showResearchEntry (tr ("Search: %1").arg (term), md);
}

void MainWindow::showResearchEntry (const QString& title, const QString& markdown)
{
    if (researchPanel == nullptr)
    {
        ResearchView::Hooks hooks;
        hooks.close = [this] { closeResearch(); };
        hooks.search = [this] (const QString& term) { searchResearch (term); };
        hooks.link = [this] (const QUrl& url)
        {
            const auto title = url.path (QUrl::FullyDecoded);
            for (const auto& e : readReferences())
                if (e.title == title)
                    return showResearchEntry (e.title, e.markdown);
            searchResearch (title);
        };
        hooks.run = [this] (const QString& id)
        {
            // An entry's way to the tool it is about (Compose > Europe > Organum...).
            auto* action = commands.action (id);
            if (action == nullptr || ! action->isEnabled())
                return statusBar()->showMessage (tr ("That is not in this version of PHI, or not available now"), 6000);
            action->trigger();
        };
        hooks.undock = [this]
        {
            // Undocked, it no longer takes anything's place; docked again, it does.
            if (researchPanel->isPoppedOut())
            {
                takeLeftForResearch();
                researchPanel->setPoppedOut (false);
            }
            else
            {
                giveBackLeftFromResearch();
                researchPanel->setPoppedOut (true);
            }
            researchView->updateButtons (researchPanel->isPoppedOut());
        };
        researchView = new ResearchView (std::move (hooks), this);
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
    if (! researchPanel->opensInWindow())
        takeLeftForResearch();
    researchPanel->show (false);
    researchView->updateButtons (researchPanel->isPoppedOut());
    researchView->reading()->setFocus();
}

// It takes the place of what is on the left now (owner, 2026-10-29): the Find panel, or the
// docked mixer's Inspector. Opening another entry keeps what it is to give back.
void MainWindow::takeLeftForResearch()
{
    if (researchShowing)
        return;
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

void MainWindow::giveBackLeftFromResearch()
{
    if (! researchShowing)
        return;
    researchShowing = false;
    if (researchHidMixerSide && mixerWin != nullptr)
        mixerWin->hideSideColumn (false);   // back to the Inspector, or the Finder it had
    if (researchHidFind && ! findPanel->isShown())
        findPanel->show (false);
    researchHidMixerSide = researchHidFind = false;
}

void MainWindow::closeResearch()
{
    if (researchPanel == nullptr)
        return;
    const bool wasShowing = researchShowing;
    researchShowing = false;   // so hiding it does not come back here
    researchPanel->hide();
    researchShowing = wasShowing;
    giveBackLeftFromResearch();
}

void MainWindow::openResearchSearch()
{
    if (researchPanel == nullptr || ! researchPanel->isShown())
        showResearchEntry (tr ("Research"), tr ("Type a term in the search box above, then Enter: a mode, a rhythm, a composer, "
                                                "a technique, anything you meet in PHI."));
    researchView->search()->setFocus();
    researchView->search()->selectAll();
}
