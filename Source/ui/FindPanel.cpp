// SPDX-License-Identifier: AGPL-3.0-only
#include "FindPanel.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMap>
#include <QPainter>
#include <QSplitter>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

namespace
{
    // Item data roles
    constexpr int sectionRole = Qt::UserRole;       // sidebar: Section
    constexpr int placeRole   = Qt::UserRole + 1;   // sidebar: the place's folder
    constexpr int colourRole  = Qt::UserRole + 2;   // sidebar: colour index
    constexpr int indexRole   = Qt::UserRole + 3;   // results: 0 for an item, -1 for a group or note
    constexpr int itemRole    = Qt::UserRole + 4;   // results: the FindItem

    constexpr int filesPerPlace = 50000;   // stops a scan of a huge folder tree
    constexpr int shownPerGroup = 300;     // when searching everything; a category shows more

    const QStringList audioPatterns { "*.wav", "*.flac", "*.mp3", "*.ogg", "*.opus", "*.aif", "*.aiff",
                                      "*.w64", "*.caf", "*.m4a", "*.wv", "*.bwf", "*.rf64" };

    QString storePath()
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
             + QStringLiteral ("/" APP_ID "/find.json");
    }

    QString kindName (FindItem::Kind k)
    {
        switch (k)
        {
            case FindItem::Kind::process: return QObject::tr ("Processes");
            case FindItem::Kind::effect:  return QObject::tr ("Effects");
            case FindItem::Kind::plugin:  return QObject::tr ("Plugins");
            case FindItem::Kind::file:    return QObject::tr ("Files");
        }
        return {};
    }

    QList<FindItem> scanPlaces (const QStringList& places)
    {
        QList<FindItem> found;
        for (const auto& place : places)
        {
            const QDir root (place);
            int count = 0;
            QDirIterator it (place, audioPatterns, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (it.hasNext() && count++ < filesPerPlace)
            {
                const QFileInfo info (it.next());
                FindItem item;
                item.kind = FindItem::Kind::file;
                item.key = QStringLiteral ("file:") + info.absoluteFilePath();
                item.name = info.fileName();
                const auto folder = root.relativeFilePath (info.absolutePath());
                item.group = QDir (place).dirName() + (folder == QStringLiteral (".") ? QString() : QStringLiteral ("/") + folder);
                item.detail = info.suffix().toUpper();
                item.searchText = item.group;
                found.push_back (item);
            }
        }
        return found;
    }
}

QString FindPanel::colourName (int index)
{
    static const QStringList names { QObject::tr ("Favorites"), QObject::tr ("Orange"), QObject::tr ("Yellow"), QObject::tr ("Green"),
                                     QObject::tr ("Blue"), QObject::tr ("Purple"), QObject::tr ("Gray") };
    return names.value (index);
}

QColor FindPanel::colour (int index)
{
    static const QColor colours[numColours] { QColor (0xf0, 0x3e, 0x3e), QColor (0xf5, 0x9b, 0x2b), QColor (0xf2, 0xd7, 0x3a),
                                              QColor (0x3d, 0xd6, 0x8c), QColor (0x3d, 0x8e, 0xf0), QColor (0xa8, 0x6c, 0xf0),
                                              QColor (0x8c, 0x8c, 0x8c) };
    return index >= 0 && index < numColours ? colours[index] : QColor();
}

//==============================================================================
FindPanel::FindPanel (QWidget* home, QWidget* parent)
    : QWidget (parent), homeWidget (home), search (new QLineEdit (this)), sidebar (new QTreeWidget (this)), results (new QTreeWidget (this))
{
    setAccessibleName (tr ("Find"));
    search->setPlaceholderText (tr ("Search everything (Ctrl+F)"));
    search->setAccessibleName (tr ("Search"));
    search->setClearButtonEnabled (true);
    search->installEventFilter (this);
    setFocusProxy (search);

    sidebar->setHeaderHidden (true);
    sidebar->setRootIsDecorated (false);
    sidebar->setIndentation (0);
    sidebar->setAccessibleName (tr ("Categories"));
    sidebar->installEventFilter (this);

    results->setColumnCount (3);
    results->setHeaderLabels ({ tr ("Name"), QString(), QString() });
    results->setUniformRowHeights (true);
    results->setIconSize (QSize (44, 10));
    results->header()->setStretchLastSection (false);
    results->header()->setSectionResizeMode (0, QHeaderView::Stretch);
    results->header()->setSectionResizeMode (1, QHeaderView::ResizeToContents);   // colour dots
    results->header()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
    results->setAccessibleName (tr ("Results"));
    results->setAccessibleDescription (tr ("Enter runs or loads the selection, 1 to 7 toggle a colour, 0 clears the colours."));
    results->installEventFilter (this);

    auto* split = new QSplitter (Qt::Horizontal, this);
    split->addWidget (sidebar);
    split->addWidget (results);
    split->setStretchFactor (0, 0);
    split->setStretchFactor (1, 1);
    split->setSizes ({ 150, 330 });
    split->setChildrenCollapsible (false);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (4, 4, 4, 4);
    layout->addWidget (search);
    layout->addWidget (split, 1);

    // Tab goes from the search to the results, then the sidebar.
    setTabOrder (search, results);
    setTabOrder (results, sidebar);
    results->setIndentation (12);

    loadCollections();
    buildSidebar();

    connect (search, &QLineEdit::textChanged, this, &FindPanel::refilter);
    connect (sidebar, &QTreeWidget::currentItemChanged, this, [this] (QTreeWidgetItem* item)
    {
        if (item != nullptr && static_cast<Section> (item->data (0, sectionRole).toInt()) != Section::addPlace)
            refilter();
    });
    connect (sidebar, &QTreeWidget::itemActivated, this, [this] (QTreeWidgetItem* item)
    {
        if (static_cast<Section> (item->data (0, sectionRole).toInt()) == Section::addPlace)
            addPlace();
        else
            results->setFocus (Qt::OtherFocusReason);
    });
    connect (results, &QTreeWidget::itemActivated, this, &FindPanel::activate);
    connect (&fileScan, &QFutureWatcher<QList<FindItem>>::finished, this, [this]
    {
        files = fileScan.result();
        refilter();
    });
}

FindPanel::~FindPanel()
{
    fileScan.waitForFinished();
}

void FindPanel::focusSearch()
{
    search->setFocus (Qt::ShortcutFocusReason);
    search->selectAll();
}

bool FindPanel::searchHasFocus() const
{
    return search->hasFocus();
}

void FindPanel::reload()
{
    items = collect ? collect() : QList<FindItem>();
    if (filesStale)
        scanFiles();
    refilter();
}

void FindPanel::showEvent (QShowEvent* e)
{
    QWidget::showEvent (e);
    reload();
}

//==============================================================================
void FindPanel::buildSidebar()
{
    const QSignalBlocker blocker (sidebar);
    const auto* current = sidebar->currentItem();
    const auto oldSection = current != nullptr ? current->data (0, sectionRole).toInt() : (int) Section::all;
    const auto oldExtra = current != nullptr ? current->data (0, placeRole).toString() + current->data (0, colourRole).toString() : QString();
    sidebar->clear();

    auto heading = [this] (const QString& text)
    {
        auto* h = new QTreeWidgetItem (sidebar, { text });
        h->setFlags (Qt::NoItemFlags);   // a heading: arrow keys skip it
        return h;
    };
    auto entry = [this] (const QString& text, Section s)
    {
        auto* e = new QTreeWidgetItem (sidebar, { text });
        e->setData (0, sectionRole, (int) s);
        return e;
    };

    heading (tr ("Collections"));
    for (int c = 0; c < numColours; ++c)
    {
        QPixmap square (10, 10);
        square.fill (colour (c));
        auto* e = entry (colourName (c), Section::colour);
        e->setIcon (0, QIcon (square));
        e->setData (0, colourRole, c);
    }

    heading (tr ("Find"));
    entry (tr ("All"), Section::all);
    entry (tr ("Processes"), Section::processes);
    entry (tr ("Effects"), Section::effects);
    entry (tr ("Plugins"), Section::plugins);
    entry (tr ("Files"), Section::files);

    heading (tr ("Places"));
    for (const auto& p : places)
    {
        auto* e = entry (QDir (p).dirName(), Section::place);
        e->setData (0, placeRole, p);
        e->setToolTip (0, p);
    }
    entry (tr ("Add Folder..."), Section::addPlace);

    // Keep the same entry selected, or All.
    QTreeWidgetItem* select = nullptr;
    for (int i = 0; i < sidebar->topLevelItemCount() && select == nullptr; ++i)
    {
        auto* it = sidebar->topLevelItem (i);
        if ((it->flags() & Qt::ItemIsSelectable) && it->data (0, sectionRole).toInt() == oldSection
            && it->data (0, placeRole).toString() + it->data (0, colourRole).toString() == oldExtra)
            select = it;
    }
    for (int i = 0; i < sidebar->topLevelItemCount() && select == nullptr; ++i)
        if (sidebar->topLevelItem (i)->data (0, sectionRole).toInt() == (int) Section::all
            && (sidebar->topLevelItem (i)->flags() & Qt::ItemIsSelectable))
            select = sidebar->topLevelItem (i);
    sidebar->setCurrentItem (select);
}

QIcon FindPanel::dots (int mask) const
{
    static QHash<int, QIcon> cache;
    if (auto it = cache.find (mask); it != cache.end())
        return *it;

    QPixmap pixmap (44, 10);
    pixmap.fill (Qt::transparent);
    QPainter g (&pixmap);
    g.setRenderHint (QPainter::Antialiasing);
    g.setPen (Qt::NoPen);
    int x = 1;
    for (int c = 0; c < numColours; ++c)
        if (mask & (1 << c))
        {
            g.setBrush (colour (c));
            g.drawEllipse (QRectF (x, 1.5, 7, 7));
            x += 6;
        }
    g.end();
    return cache.insert (mask, QIcon (pixmap)).value();
}

void FindPanel::refilter()
{
    const auto* current = sidebar->currentItem();
    if (current == nullptr || ! (current->flags() & Qt::ItemIsSelectable))
        current = nullptr;
    const auto section = current != nullptr ? static_cast<Section> (current->data (0, sectionRole).toInt()) : Section::all;
    const auto words = search->text().split (QLatin1Char (' '), Qt::SkipEmptyParts);
    const bool everything = section == Section::all && words.isEmpty();

    auto wanted = [&] (const FindItem& item)
    {
        switch (section)
        {
            case Section::colour:    return (collections.value (item.key) & (1 << current->data (0, colourRole).toInt())) != 0;
            case Section::processes: return item.kind == FindItem::Kind::process;
            case Section::effects:   return item.kind == FindItem::Kind::effect;
            case Section::plugins:   return item.kind == FindItem::Kind::plugin;
            case Section::files:     return item.kind == FindItem::Kind::file;
            case Section::place:     return item.kind == FindItem::Kind::file
                                         && item.key.startsWith (QStringLiteral ("file:") + current->data (0, placeRole).toString() + QLatin1Char ('/'));
            case Section::all:
            case Section::addPlace:  return true;
        }
        return true;
    };
    auto matches = [&] (const FindItem& item)
    {
        return std::all_of (words.begin(), words.end(), [&] (const QString& w)
        {
            return item.name.contains (w, Qt::CaseInsensitive) || item.detail.contains (w, Qt::CaseInsensitive)
                || item.group.contains (w, Qt::CaseInsensitive) || item.searchText.contains (w, Qt::CaseInsensitive);
        });
    };

    // Searching everything groups by kind; a category groups by menu, maker or folder.
    const bool byKind = section == Section::all || section == Section::colour;
    QMap<QString, QList<const FindItem*>> groups;
    QStringList order;
    for (const auto* list : { &items, &files })
        for (const auto& item : *list)
            if (wanted (item) && matches (item))
            {
                const auto g = byKind ? kindName (item.kind) : item.group;
                if (! groups.contains (g))
                    order.push_back (g);
                groups[g].push_back (&item);
            }

    // Built-in plugins first, then makers and folders alphabetically; menus keep the menu bar's order.
    if (section == Section::plugins)
        std::stable_sort (order.begin(), order.end(), [] (const QString& a, const QString& b)
        {
            const bool aBuiltIn = a == QStringLiteral ("Built-in"), bBuiltIn = b == QStringLiteral ("Built-in");
            return aBuiltIn != bBuiltIn ? aBuiltIn : QString::localeAwareCompare (a, b) < 0;
        });
    else if (section == Section::files || section == Section::place)
        std::sort (order.begin(), order.end(), [] (const QString& a, const QString& b) { return QString::localeAwareCompare (a, b) < 0; });

    results->setUpdatesEnabled (false);
    results->clear();
    const auto disabled = palette().color (QPalette::Disabled, QPalette::Text);
    const int limit = section == Section::all || section == Section::colour ? shownPerGroup : 5000;
    for (const auto& g : order)
    {
        auto list = groups.value (g);
        if (section == Section::plugins || section == Section::files || section == Section::place || byKind)
            std::stable_sort (list.begin(), list.end(), [] (const FindItem* a, const FindItem* b)
            {
                return QString::localeAwareCompare (a->name, b->name) < 0;
            });

        auto* header = new QTreeWidgetItem (results, { g, QString(), QString::number (list.size()) });
        header->setData (0, indexRole, -1);
        header->setFlags (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto font = header->font (0);
        font.setBold (true);
        header->setFont (0, font);

        int shown = 0;
        for (const auto* item : list)
        {
            if (shown++ == limit)
            {
                auto* more = new QTreeWidgetItem (header, { tr ("%1 more: type to narrow the search").arg (list.size() - limit) });
                more->setData (0, indexRole, -1);
                more->setForeground (0, disabled);
                break;
            }
            auto* row = new QTreeWidgetItem (header, { item->name, QString(), item->detail });
            row->setData (0, indexRole, 0);
            row->setData (0, itemRole, QVariant::fromValue (static_cast<const void*> (item)));
            if (const auto mask = collections.value (item->key); mask != 0)
                row->setIcon (1, dots (mask));
            row->setToolTip (0, item->kind == FindItem::Kind::file ? item->key.mid (5) : item->name);
            if (! item->enabled)
            {
                row->setForeground (0, disabled);
                row->setForeground (2, disabled);
            }
        }
        header->setExpanded (! everything || order.size() == 1);
    }
    if (order.isEmpty())
    {
        const auto where = current != nullptr ? current->text (0) : tr ("All");
        auto* note = new QTreeWidgetItem (results, { section == Section::colour && words.isEmpty()
            ? tr ("Empty: press 1 to 7 on a result to add it")
            : tr ("Nothing found in %1").arg (where) });
        note->setData (0, indexRole, -1);
        note->setFlags (Qt::ItemIsEnabled);
        note->setForeground (0, disabled);
    }
    results->setUpdatesEnabled (true);

    // Select the first result so Enter works straight from the search field.
    for (int i = 0; i < results->topLevelItemCount(); ++i)
        if (auto* top = results->topLevelItem (i); top->isExpanded() && top->childCount() > 0)
        {
            results->setCurrentItem (top->child (0));
            return;
        }
    if (results->topLevelItemCount() > 0)
        results->setCurrentItem (results->topLevelItem (0));
}

static const FindItem* itemOf (QTreeWidgetItem* row)
{
    if (row == nullptr || row->data (0, indexRole).toInt() < 0)
        return nullptr;
    return static_cast<const FindItem*> (row->data (0, itemRole).value<const void*>());
}

void FindPanel::activate (QTreeWidgetItem* row)
{
    if (row != nullptr && row->data (0, indexRole).toInt() < 0)
    {
        row->setExpanded (! row->isExpanded());
        return;
    }

    const auto* item = itemOf (row);
    if (item == nullptr)
        return;

    if (item->kind == FindItem::Kind::file)
    {
        Q_EMIT openRequested (item->key.mid (5));
        return;
    }

    // Processes act on the editor, so it gets the keyboard back first.
    if (item->kind != FindItem::Kind::plugin)
        homeWidget->setFocus (Qt::OtherFocusReason);
    if (item->activate)
        item->activate();
}

void FindPanel::toggleColour (QTreeWidgetItem* row, int c)
{
    const auto* item = itemOf (row);
    if (item == nullptr)
        return;

    auto mask = collections.value (item->key);
    mask = c < 0 ? 0 : mask ^ (1 << c);
    if (mask == 0)
        collections.remove (item->key);
    else
        collections.insert (item->key, mask);
    row->setIcon (1, mask != 0 ? dots (mask) : QIcon());
    saveCollections();
}

//==============================================================================
void FindPanel::addPlace()
{
    const auto folder = QFileDialog::getExistingDirectory (this, tr ("Add a Folder to Find"), QDir::homePath());
    if (folder.isEmpty() || places.contains (folder))
        return;

    places.push_back (folder);
    saveCollections();
    buildSidebar();
    filesStale = true;
    scanFiles();
}

void FindPanel::scanFiles()
{
    if (fileScan.isRunning())
        return;
    filesStale = false;
    fileScan.setFuture (QtConcurrent::run (scanPlaces, places));
}

void FindPanel::loadCollections()
{
    QFile file (storePath());
    if (! file.open (QIODevice::ReadOnly))
    {
        // First run: the user's Music folder, when there is one.
        const auto music = QStandardPaths::writableLocation (QStandardPaths::MusicLocation);
        if (! music.isEmpty() && QDir (music).exists() && music != QDir::homePath())
            places = { music };
        return;
    }

    const auto root = QJsonDocument::fromJson (file.readAll()).object();
    for (const auto& p : root.value (QStringLiteral ("places")).toArray())
        places.push_back (p.toString());
    const auto colours = root.value (QStringLiteral ("colours")).toObject();
    for (auto it = colours.begin(); it != colours.end(); ++it)
        if (const auto mask = it.value().toInt(); mask != 0)
            collections.insert (it.key(), mask);
}

void FindPanel::saveCollections() const
{
    QJsonObject colours;
    for (auto it = collections.begin(); it != collections.end(); ++it)
        colours.insert (it.key(), it.value());

    QDir().mkpath (QFileInfo (storePath()).absolutePath());
    QFile file (storePath());
    if (file.open (QIODevice::WriteOnly | QIODevice::Truncate))
        file.write (QJsonDocument (QJsonObject { { QStringLiteral ("places"), QJsonArray::fromStringList (places) },
                                                 { QStringLiteral ("colours"), colours } }).toJson());
}

//==============================================================================
bool FindPanel::eventFilter (QObject* watched, QEvent* e)
{
    if (e->type() != QEvent::ShortcutOverride && e->type() != QEvent::KeyPress)
        return QWidget::eventFilter (watched, e);

    auto* k = static_cast<QKeyEvent*> (e);
    const auto mods = k->modifiers() & ~Qt::KeypadModifier;
    const bool plain = mods == Qt::NoModifier;
    const bool typing = (plain || mods == Qt::ShiftModifier) && ! k->text().isEmpty() && k->text().at (0).isPrint();
    const bool digit = plain && k->key() >= Qt::Key_0 && k->key() <= Qt::Key_0 + numColours;

    // Keep these keys from the window's shortcuts (Space plays, for example).
    if (e->type() == QEvent::ShortcutOverride)
    {
        switch (k->key())
        {
            case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_PageUp: case Qt::Key_PageDown:
            case Qt::Key_Home: case Qt::Key_End: case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Escape:
                if (plain)
                    e->accept();
                break;
            default:
                if (typing || digit)
                    e->accept();
                break;
        }
        return false;
    }

    if (k->key() == Qt::Key_Escape && plain)
    {
        if (watched == search && ! search->text().isEmpty())
            search->clear();
        else
        {
            homeWidget->window()->activateWindow();
            homeWidget->setFocus (Qt::ShortcutFocusReason);
        }
        return true;
    }

    if (watched == search)
    {
        switch (k->key())
        {
            case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_PageUp: case Qt::Key_PageDown:
                QApplication::sendEvent (results, e);
                return true;
            case Qt::Key_Return: case Qt::Key_Enter:
                activate (results->currentItem());
                return true;
            default:
                return false;
        }
    }

    if (watched == results)
    {
        if (digit)
        {
            const int n = k->key() - Qt::Key_0;
            toggleColour (results->currentItem(), n == 0 ? -1 : n - 1);
            return true;
        }
        if (k->key() == Qt::Key_Left && plain)
        {
            auto* row = results->currentItem();
            if (row != nullptr && row->parent() != nullptr)
                results->setCurrentItem (row->parent());
            else if (row != nullptr && row->isExpanded())
                row->setExpanded (false);
            else
                sidebar->setFocus (Qt::OtherFocusReason);
            return true;
        }
    }

    if (watched == sidebar && plain && k->key() == Qt::Key_Right)
    {
        results->setFocus (Qt::OtherFocusReason);
        return true;
    }

    // Typing in the sidebar or results starts a new search.
    if (typing && ! digit && k->key() != Qt::Key_Space)
    {
        search->setFocus (Qt::OtherFocusReason);
        search->setText (k->text());
        return true;
    }

    return QWidget::eventFilter (watched, e);
}
