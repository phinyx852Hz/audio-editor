// SPDX-License-Identifier: AGPL-3.0-only
#include "FindPanel.h"
#include "PhiMark.h"
#include "../core/Phi.h"

#include <QApplication>
#include <QColorDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QLineEdit>
#include <QMap>
#include <QPainter>
#include <QRandomGenerator>
#include <QSplitter>
#include <QStyle>
#include <QStandardPaths>
#include <QToolTip>
#include <QTreeWidget>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>
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
    constexpr int folderRole  = Qt::UserRole + 5;   // results: the folder a folder row opens

    constexpr int folderRow = -2;   // indexRole of a folder row (or the row that goes up)
    const QString placesRoot = QStringLiteral (":places");   // folderRole of the row back up to the places

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
            case FindItem::Kind::instrument: return QObject::tr ("Instruments");
            case FindItem::Kind::visual:  return QObject::tr ("Visualizations");
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

    // The Selector's sets: Favorites, the neutral default, then one per activity,
    // each with a name for every colour. The ids are kept in find.json.
    const char* const setIds[] { "favorites", "live", "mixing", "mastering", "podcast", "chakra" };
    const char* const setNames[] { QT_TR_NOOP ("Favorites"), QT_TR_NOOP ("Live Performance"), QT_TR_NOOP ("Mixing"),
                                   QT_TR_NOOP ("Mastering"), QT_TR_NOOP ("Podcast"), QT_TR_NOOP ("Chakra") };
    constexpr int defaultSets = (int) std::size (setNames);
    constexpr int liveSet = 1;

    // Live Performance names its colours after the parts of a set, top to bottom;
    // the other sets go by the colour names until they are given theirs.
    QString setId (int set)
    {
        return set < defaultSets ? QString::fromLatin1 (setIds[set]) : QStringLiteral ("set%1").arg (set + 1);
    }

    QString defaultMarkName (int set, int index)
    {
        static const QStringList live { "FX", "PD", "ARP", "VOX", "LD", "PR", "BA", "DR" };
        return set == liveSet ? live.value (index) : FindPanel::colourName (index);
    }
}

QString FindPanel::colourName (int index)
{
    static const QStringList names { QObject::tr ("Silver"), QObject::tr ("Violet"), QObject::tr ("Indigo"), QObject::tr ("Azure"),
                                     QObject::tr ("Emerald"), QObject::tr ("Gold"), QObject::tr ("Saffron"), QObject::tr ("Ruby") };
    return names.value (index);
}

QColor FindPanel::colour (int index)
{
    // The named colours (silver C0C0C0, violet 8F00FF, indigo 4B0082, azure 007FFF, emerald
    // 50C878, gold FFD700, saffron FF9933, ruby 9B111E), violet and indigo lifted so they
    // read on a dark theme.
    static const QColor colours[numColours] { QColor (0xc0, 0xc0, 0xc0), QColor (0xa0, 0x40, 0xff), QColor (0x6a, 0x3d, 0xd8),
                                              QColor (0x00, 0x7f, 0xff), QColor (0x50, 0xc8, 0x78), QColor (0xff, 0xd7, 0x00),
                                              QColor (0xff, 0x99, 0x33), QColor (0x9b, 0x11, 0x1e) };
    return index >= 0 && index < numColours ? colours[index] : QColor();
}

QString FindPanel::setName (int set) const
{
    if (set < 0 || set >= numSets())
        return {};
    const auto name = sets[(size_t) set].name.trimmed();
    return ! name.isEmpty() ? name : set < defaultSets ? tr (setNames[set]) : tr ("Set %1").arg (set + 1);
}

QColor FindPanel::markColour (int set, int index) const
{
    if (set >= 0 && set < numSets())
        if (const auto c = sets[(size_t) set].colours.value (index); c.isValid())
            return c;
    return colour (index);
}

QString FindPanel::markName (int set, int index) const
{
    if (set < 0 || set >= numSets())
        return {};
    const auto name = sets[(size_t) set].names.value (index).trimmed();
    return name.isEmpty() ? defaultMarkName (set, index) : name;
}

//==============================================================================
QSize FindPanel::sizeHint() const
{
    // Wide enough for the categories and readable names side by side.
    return { 480, 520 };
}

static const FindItem* itemOf (QTreeWidgetItem* row);

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
    results->setIconSize (QSize (52, 10));
    results->header()->setStretchLastSection (false);
    results->header()->setSectionResizeMode (0, QHeaderView::Stretch);
    results->header()->setSectionResizeMode (1, QHeaderView::ResizeToContents);   // colour dots
    results->header()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
    // The dots column takes no room until something has a colour, and the type sits right
    // after the name, so names get all the width there is.
    results->header()->setMinimumSectionSize (4);
    // Long names lose their middle, so "Loop-1" and "Loop-7" stay told apart.
    results->setTextElideMode (Qt::ElideMiddle);
    results->setAccessibleName (tr ("Results"));
    results->setAccessibleDescription (tr ("Enter runs or loads the selection, 1 to 8 toggle a Selector colour, 0 clears the colours."));
    results->installEventFilter (this);
    results->viewport()->installEventFilter (this);   // files drag out onto Simplest, the Slicer, the mixer...
    setAcceptDrops (true);   // and dropping them back here does nothing (rather than opening them)

    // "Selector" above the sidebar; a right-click on it or on a colour picks the set.
    auto* side = new QWidget (this);
    auto* sideLayout = new QVBoxLayout (side);
    sideLayout->setContentsMargins (0, 0, 0, 0);
    sideLayout->setSpacing (2);
    auto* selectorLabel = new QLabel (tr ("Selector"), side);
    selectorLabel->setForegroundRole (QPalette::PlaceholderText);
    selectorLabel->setContentsMargins (4, 2, 0, 0);
    selectorLabel->setToolTip (tr ("Right-click to choose a set (Favorites, Live Performance, Mixing, Mastering, Podcast, Chakra or your own), make a new one, or change a colour"));
    selectorLabel->setContextMenuPolicy (Qt::CustomContextMenu);
    connect (selectorLabel, &QLabel::customContextMenuRequested, this, [this, selectorLabel] (const QPoint& at) { selectorMenu (selectorLabel, at); });
    // The Φ random selector at the end of the Selector line: a click deals one of each
    // colour's favourite files (to the clip launcher when it is shown, else onto the
    // mixer's tracks), Shift+click deals again in place of the last deal, Ctrl+click composes a
    // whole Architectonics song from the favourites into the launcher, right-click chooses.
    auto* dealButton = new PhiButton (tr ("Φ random selector"), side, false);
    dealButton->setToolTip (tr ("Φ random selector: deals one of each colour's favourite sounds, picked by the golden ratio, "
                                "into a new scene of the clip launcher (when it is shown) or onto the mixer's tracks. "
                                "Again for another combination; Shift+click replaces the last deal; Ctrl+click composes a whole song "
                                "by Architectonics into the clip launcher; right-click to choose where"));
    dealButton->setContextMenuPolicy (Qt::CustomContextMenu);
    connect (dealButton, &QToolButton::clicked, this, [this]
    {
        const auto mods = QApplication::keyboardModifiers();
        if ((mods & Qt::ControlModifier) != 0 && phiCompose)
            return phiCompose();
        deal (DealTo::automatic, (mods & Qt::ShiftModifier) != 0);
    });
    connect (dealButton, &QToolButton::customContextMenuRequested, this, [this, dealButton] (const QPoint& at) { dealMenu (dealButton, at); });
    auto* selectorLine = new QHBoxLayout;
    selectorLine->setContentsMargins (0, 0, 0, 0);
    selectorLine->setSpacing (0);
    selectorLine->addWidget (selectorLabel, 1);
    selectorLine->addWidget (dealButton);
    sideLayout->addLayout (selectorLine);
    dealSeed = QRandomGenerator::global()->generateDouble();
    sideLayout->addWidget (sidebar, 1);

    auto* split = new QSplitter (Qt::Horizontal, this);
    split->addWidget (side);
    split->addWidget (results);
    split->setStretchFactor (0, 0);
    split->setStretchFactor (1, 1);
    split->setSizes ({ 130, 350 });
    sidebar->setMinimumWidth (90);
    results->setMinimumWidth (160);
    split->setChildrenCollapsible (false);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (4, 4, 4, 4);
    layout->addWidget (search);
    layout->addWidget (split, 1);

    // Tab goes from the search to the set and the sidebar, then the results (as Down and Right do).
    setTabOrder (search, sidebar);
    setTabOrder (sidebar, results);
    results->setIndentation (12);

    loadSelector();
    buildSidebar();
    sidebar->setContextMenuPolicy (Qt::CustomContextMenu);
    connect (sidebar, &QTreeWidget::customContextMenuRequested, this, [this] (const QPoint& at)
    {
        auto* item = sidebar->itemAt (at);
        if (item == nullptr && at == QPoint())   // the menu key
            item = sidebar->currentItem();
        if (item != nullptr && static_cast<Section> (item->data (0, sectionRole).toInt()) == Section::colour)
        {
            sidebar->setCurrentItem (item);
            selectorMenu (sidebar->viewport(), item != nullptr ? sidebar->visualItemRect (item).bottomLeft() : at);
        }
    });

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
    // Browsing: moving onto a visualization shows it in the visualizer that is open.
    connect (results, &QTreeWidget::currentItemChanged, this, [this] (QTreeWidgetItem* row)
    {
        if (const auto* item = itemOf (row); item != nullptr && item->preview)
            item->preview();
        // Moving onto a file with the keys auditions it (clicks are handled below).
        if (results->hasFocus() && QApplication::mouseButtons() == Qt::NoButton)
        {
            if (const auto* item = itemOf (row); item != nullptr && item->kind == FindItem::Kind::file)
                startAudition (row);
        }
    });
    // One click auditions a file, a second click on it stops.
    connect (results, &QTreeWidget::itemClicked, this, [this] (QTreeWidgetItem* row)
    {
        const auto* item = itemOf (row);
        if (item == nullptr || item->kind != FindItem::Kind::file)
            return;
        if (auditioning == item->key.mid (5))
            endAudition();
        else
            startAudition (row);
    });
    connect (sidebar, &QTreeWidget::itemChanged, this, [this] (QTreeWidgetItem* item)
    {
        if (static_cast<Section> (item->data (0, sectionRole).toInt()) != Section::colour)
            return;

        // An empty name goes back to the default.
        const auto c = item->data (0, colourRole).toInt();
        auto& names = sets[(size_t) setIndex].names;
        while (names.size() < numColours)
            names.push_back ({});
        names[c] = item->text (0).trimmed() == defaultMarkName (setIndex, c) ? QString() : item->text (0).trimmed();
        saveSelector();
        const QSignalBlocker blocker (sidebar);
        item->setText (0, markName (setIndex, c));
        Q_EMIT setChanged();
    });
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

bool FindPanel::searchIsReady() const
{
    return search->hasFocus() && (search->text().isEmpty() || search->selectedText() == search->text());
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
    rowBeforeDown = -1;

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

    for (int c = 0; c < numColours; ++c)
    {
        QPixmap square (10, 10);
        square.fill (markColour (setIndex, c));
        auto* e = entry (markName (setIndex, c), Section::colour);
        e->setFlags (e->flags() | Qt::ItemIsEditable);
        e->setToolTip (0, tr ("%1 (key %2): F2 or a double-click renames it; right-click the Selector for its colour").arg (colourName (c)).arg (c + 1));
        e->setIcon (0, QIcon (square));
        e->setData (0, colourRole, c);
    }

    heading (tr ("Find"));
    entry (tr ("All"), Section::all);
    entry (tr ("Processes"), Section::processes);
    entry (tr ("Effects"), Section::effects);
    entry (tr ("Plugins"), Section::plugins);
    entry (tr ("Instruments"), Section::instruments);
    entry (tr ("Visualizations"), Section::visuals);
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
    auto& cache = dotIcons;
    if (auto it = cache.find (mask); it != cache.end())
        return *it;

    QPixmap pixmap (52, 10);
    pixmap.fill (Qt::transparent);
    QPainter g (&pixmap);
    g.setRenderHint (QPainter::Antialiasing);
    g.setPen (Qt::NoPen);
    int x = 1;
    for (int c = 0; c < numColours; ++c)
        if (mask & (1 << c))
        {
            g.setBrush (markColour (setIndex, c));
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
    if (browsing())
    {
        listFolder ({});
        return;
    }
    results->setHeaderLabels ({ tr ("Name"), QString(), QString() });

    auto wanted = [&] (const FindItem& item)
    {
        switch (section)
        {
            case Section::colour:    return (marks().value (item.key) & (1 << current->data (0, colourRole).toInt())) != 0;
            case Section::processes: return item.kind == FindItem::Kind::process;
            case Section::effects:   return item.kind == FindItem::Kind::effect;
            case Section::plugins:   return item.kind == FindItem::Kind::plugin;
            case Section::instruments: return item.kind == FindItem::Kind::instrument;
            case Section::visuals:   return item.kind == FindItem::Kind::visual;
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
                // Among visualizations, PHI's own views come before the many MilkDrop presets.
                const bool aView = a->key.startsWith (QStringLiteral ("command:")), bView = b->key.startsWith (QStringLiteral ("command:"));
                if (a->kind == FindItem::Kind::visual && b->kind == FindItem::Kind::visual && aView != bView)
                    return aView;
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
            if (const auto mask = marks().value (item->key); mask != 0)
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
            ? tr ("Empty: press %1 on a result to add it").arg (current->data (0, colourRole).toInt() + 1)
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

QString FindPanel::currentFile() const
{
    const auto* item = itemOf (results->currentItem());
    return item != nullptr && item->kind == FindItem::Kind::file ? item->key.mid (5) : QString();
}

void FindPanel::activate (QTreeWidgetItem* row)
{
    if (row != nullptr && row->data (0, indexRole).toInt() == folderRow)
    {
        const auto folder = row->data (0, folderRole).toString();
        if (row->text (0) == QStringLiteral (".."))
            folderUp();
        else
            openFolder (folder);
        return;
    }
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
        endAudition();
        const auto path = item->key.mid (5);
        int colour = -1;
        if (const auto* shown = sidebar->currentItem();
            shown != nullptr && static_cast<Section> (shown->data (0, sectionRole).toInt()) == Section::colour)
            colour = shown->data (0, colourRole).toInt();
        else if (const auto mask = marks().value (item->key); mask != 0)
            for (colour = 0; (mask & (1 << colour)) == 0; ++colour) {}
        if (sendFile && sendFile (path, colour))
            return;
        Q_EMIT openRequested (path);
        return;
    }

    // Processes act on the editor, so it gets the keyboard back first.
    if (item->kind != FindItem::Kind::plugin && item->kind != FindItem::Kind::instrument)
        homeWidget->setFocus (Qt::OtherFocusReason);
    if (item->activate)
        item->activate();
}

void FindPanel::toggleColour (QTreeWidgetItem* row, int c)
{
    const auto* item = itemOf (row);
    if (item == nullptr)
        return;

    auto mask = marks().value (item->key);
    mask = c < 0 ? 0 : mask ^ (1 << c);
    if (mask == 0)
        marks().remove (item->key);
    else
        marks().insert (item->key, mask);
    row->setIcon (1, mask != 0 ? dots (mask) : QIcon());
    saveSelector();
}

//==============================================================================
bool FindPanel::browsing() const
{
    const auto* current = sidebar->currentItem();
    if (current == nullptr || ! search->text().trimmed().isEmpty())
        return false;
    const auto section = static_cast<Section> (current->data (0, sectionRole).toInt());
    return section == Section::files || section == Section::place;
}

void FindPanel::listFolder (const QString& select)
{
    const auto* current = sidebar->currentItem();
    const bool inPlace = static_cast<Section> (current->data (0, sectionRole).toInt()) == Section::place;
    const auto place = inPlace ? current->data (0, placeRole).toString() : QString();

    // Stay inside the place (or the places) chosen in the sidebar. Places can sit inside
    // one another (a whole library, and one folder of it added again), so the place a
    // folder belongs to is the one chosen in the sidebar, or else the closest one.
    auto within = [] (const QString& folder, const QString& p) { return folder == p || folder.startsWith (p + QLatin1Char ('/')); };
    auto placeOf = [&] (const QString& folder)
    {
        if (inPlace)
            return within (folder, place) ? place : QString();
        QString best;
        for (const auto& p : places)
            if (within (folder, p) && p.size() > best.size())
                best = p;
        return best;
    };
    if (inPlace && (browseFolder.isEmpty() || ! within (browseFolder, place)))
        browseFolder = place;
    if (! inPlace && ! browseFolder.isEmpty() && placeOf (browseFolder).isEmpty())
        browseFolder.clear();

    // The header shows where we are: the place, then each folder inside it.
    const auto root = placeOf (browseFolder);
    QString where = browseFolder.isEmpty() ? tr ("Places") : QDir (root).dirName();
    if (! browseFolder.isEmpty() && browseFolder != root)
        where += QStringLiteral (" / ") + QDir (root).relativeFilePath (browseFolder).replace (QLatin1Char ('/'), QStringLiteral (" / "));
    results->setHeaderLabels ({ where, QString(), QString() });

    folderItems.clear();
    struct Folder { QString name, path; };
    std::vector<Folder> folders;
    if (browseFolder.isEmpty())
    {
        for (const auto& p : places)
            folders.push_back ({ QDir (p).dirName(), p });
    }
    else
    {
        const QDir dir (browseFolder);
        for (const auto& info : dir.entryInfoList (QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase))
            folders.push_back ({ info.fileName(), info.absoluteFilePath() });
        for (const auto& info : dir.entryInfoList (audioPatterns, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase))
        {
            FindItem item;
            item.kind = FindItem::Kind::file;
            item.key = QStringLiteral ("file:") + info.absoluteFilePath();
            item.name = info.fileName();
            item.detail = info.suffix().toUpper();
            item.group = where;
            folderItems.push_back (item);
        }
    }

    // Keep the row that was chosen when the same folder is listed again (after a rescan).
    auto keep = select;
    if (keep.isEmpty() && results->currentItem() != nullptr)
        keep = results->currentItem()->toolTip (0);

    results->setUpdatesEnabled (false);
    results->clear();
    const auto folderIcon = style()->standardIcon (QStyle::SP_DirIcon);
    const auto disabled = palette().color (QPalette::Disabled, QPalette::Text);
    auto folderLine = [&] (const QString& name, const QString& path, const QString& detail)
    {
        auto* row = new QTreeWidgetItem (results, { name, QString(), detail });
        row->setData (0, indexRole, folderRow);
        row->setData (0, folderRole, path);
        row->setIcon (0, folderIcon);
        row->setToolTip (0, path == placesRoot ? tr ("Back to the places") : path);
        return row;
    };
    if (! browseFolder.isEmpty() && (browseFolder != root || ! inPlace))
    {
        auto* up = folderLine (QStringLiteral (".."), browseFolder == root && ! inPlace ? placesRoot : QFileInfo (browseFolder).absolutePath(), tr ("Up"));
        up->setIcon (0, style()->standardIcon (QStyle::SP_FileDialogToParent));
    }
    QTreeWidgetItem* chosen = nullptr;
    for (const auto& f : folders)
    {
        auto* row = folderLine (f.name, f.path, tr ("Folder"));
        if (f.path == keep)
            chosen = row;
    }
    for (const auto& item : folderItems)
    {
        auto* row = new QTreeWidgetItem (results, { item.name, QString(), item.detail });
        row->setData (0, indexRole, 0);
        row->setData (0, itemRole, QVariant::fromValue (static_cast<const void*> (&item)));
        if (const auto mask = marks().value (item.key); mask != 0)
            row->setIcon (1, dots (mask));
        row->setToolTip (0, item.key.mid (5));
        if (item.key.mid (5) == keep)
            chosen = row;
    }
    if (results->topLevelItemCount() == 0)
    {
        auto* note = new QTreeWidgetItem (results, { browseFolder.isEmpty() ? tr ("No places yet: choose Add Folder...")
                                                                            : tr ("No audio files or folders here") });
        note->setData (0, indexRole, -1);
        note->setFlags (Qt::ItemIsEnabled);
        note->setForeground (0, disabled);
    }
    results->setUpdatesEnabled (true);

    // Selecting here must not audition: the keys did not move onto it.
    const QSignalBlocker blocker (results);
    // A new folder starts on its first entry, past "..".
    if (chosen == nullptr)
        chosen = results->topLevelItem (results->topLevelItemCount() > 1 && results->topLevelItem (0)->text (0) == QStringLiteral ("..") ? 1 : 0);
    results->setCurrentItem (chosen);
    if (auto* row = results->currentItem())
        results->scrollToItem (row);
}

void FindPanel::openFolder (const QString& folder, const QString& select)
{
    browseFolder = folder == placesRoot ? QString() : folder;
    listFolder (select);
}

void FindPanel::folderUp()
{
    if (browseFolder.isEmpty())
        return;
    const auto from = browseFolder;
    if (atTopOfPlace())
        return;   // the top of the place chosen in the sidebar
    // In Files, a place's top goes back to the list of places.
    const auto* current = sidebar->currentItem();
    const bool inPlace = static_cast<Section> (current->data (0, sectionRole).toInt()) == Section::place;
    openFolder (! inPlace && places.contains (browseFolder) ? placesRoot : QFileInfo (browseFolder).absolutePath(), from);
}

bool FindPanel::atTopOfPlace() const
{
    const auto* current = sidebar->currentItem();
    return current != nullptr && static_cast<Section> (current->data (0, sectionRole).toInt()) == Section::place
        && browseFolder == current->data (0, placeRole).toString();
}

void FindPanel::dragFiles (QTreeWidgetItem* pressed)
{
    // The pressed file, with the other selected files when it is one of them.
    QList<QUrl> urls;
    const auto selected = results->selectedItems();
    for (auto* row : selected.contains (pressed) ? selected : QList<QTreeWidgetItem*> { pressed })
        if (const auto* item = itemOf (row); item != nullptr && item->kind == FindItem::Kind::file)
            urls << QUrl::fromLocalFile (item->key.mid (5));   // "file:<path>"
    if (urls.isEmpty())
        return;
    auto* mime = new QMimeData;
    mime->setUrls (urls);
    auto* drag = new QDrag (results);
    drag->setMimeData (mime);
    // Dropped somewhere that took it (the clip launcher, the tracks, the editor): the
    // audition has done its job and stops.
    if (drag->exec (Qt::CopyAction, Qt::CopyAction) != Qt::IgnoreAction)
        endAudition();
}

void FindPanel::startAudition (QTreeWidgetItem* row)
{
    const auto* item = itemOf (row);
    if (item == nullptr || item->kind != FindItem::Kind::file || ! audition)
        return;
    auditioning = item->key.mid (5);
    audition (auditioning);
}

void FindPanel::endAudition()
{
    // Stopped even when the panel thinks nothing plays: a sound still decoding, or one that
    // was reported ended, must not carry on behind a file just sent somewhere.
    auditioning.clear();
    if (stopAudition)
        stopAudition();
}

void FindPanel::auditionEnded()
{
    auditioning.clear();
}

void FindPanel::addPlace()
{
    const auto folder = QFileDialog::getExistingDirectory (this, tr ("Add a Folder to Find"), QDir::homePath());
    if (folder.isEmpty() || places.contains (folder))
        return;

    places.push_back (folder);
    saveSelector();
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

void FindPanel::loadSelector()
{
    sets.assign ((size_t) defaultSets, {});
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

    if (root.contains (QStringLiteral ("sets")))
    {
        // Sets saved before Favorites came first carry no id and are in the activities' order.
        const auto list = root.value (QStringLiteral ("sets")).toArray();
        for (int i = 0; i < list.size(); ++i)
        {
            const auto o = list[i].toObject();
            const auto id = o.value (QStringLiteral ("id")).toString (i + 1 < defaultSets ? QString::fromLatin1 (setIds[i + 1]) : QString());
            int index = numSets();
            for (int k = 0; k < numSets(); ++k)
                if (id == setId (k))
                    index = k;
            if (index >= numSets())
                sets.resize ((size_t) index + 1);
            auto& set = sets[(size_t) index];
            set.name = o.value (QStringLiteral ("name")).toString();
            for (const auto& c : o.value (QStringLiteral ("colours")).toArray())
                set.colours.push_back (QColor (c.toString()));   // "" stays invalid: the Selector's
            for (const auto& n : o.value (QStringLiteral ("names")).toArray())
                set.names.push_back (n.toString());
            const auto m = o.value (QStringLiteral ("marks")).toObject();
            for (auto it = m.begin(); it != m.end(); ++it)
                if (const auto mask = it.value().toInt() & ((1 << numColours) - 1); mask != 0)
                    set.marks.insert (it.key(), mask);
        }
        setIndex = liveSet;   // where the older sets were
        for (int i = 0; i < numSets(); ++i)
            if (root.value (QStringLiteral ("setId")).toString() == setId (i))
                setIndex = i;
        return;
    }

    // Before the Selector: seven collections (Favorites in red, Orange, Yellow, Green,
    // Blue, Purple, Gray). Each goes to the nearest colour of Favorites, keeping
    // the names given to it.
    constexpr int oldToNew[] { 7, 6, 5, 4, 3, 1, 0 };
    auto& live = sets[0];   // Favorites
    const auto oldNames = root.value (QStringLiteral ("collectionNames")).toArray();
    for (int i = 0; i < oldNames.size() && i < (int) std::size (oldToNew); ++i)
        if (const auto name = oldNames[i].toString().trimmed(); ! name.isEmpty())
        {
            while (live.names.size() < numColours)
                live.names.push_back ({});
            live.names[oldToNew[i]] = name;
        }
    const auto colours = root.value (QStringLiteral ("colours")).toObject();
    for (auto it = colours.begin(); it != colours.end(); ++it)
    {
        int mask = 0;
        for (int i = 0; i < (int) std::size (oldToNew); ++i)
            if (it.value().toInt() & (1 << i))
                mask |= 1 << oldToNew[i];
        if (mask != 0)
            live.marks.insert (it.key(), mask);
    }
}

void FindPanel::saveSelector() const
{
    QJsonArray list;
    for (const auto& set : sets)
    {
        QJsonObject m;
        for (auto it = set.marks.begin(); it != set.marks.end(); ++it)
            m.insert (it.key(), it.value());
        QJsonArray colours;
        for (const auto& c : set.colours)
            colours.push_back (c.isValid() ? c.name() : QString());
        list.push_back (QJsonObject { { QStringLiteral ("id"), setId ((int) list.size()) },
                                      { QStringLiteral ("colours"), colours },
                                      { QStringLiteral ("name"), set.name },
                                      { QStringLiteral ("names"), QJsonArray::fromStringList (set.names) },
                                      { QStringLiteral ("marks"), m } });
    }

    QDir().mkpath (QFileInfo (storePath()).absolutePath());
    QFile file (storePath());
    if (file.open (QIODevice::WriteOnly | QIODevice::Truncate))
        file.write (QJsonDocument (QJsonObject { { QStringLiteral ("places"), QJsonArray::fromStringList (places) },
                                                 { QStringLiteral ("setId"), setId (setIndex) },
                                                 { QStringLiteral ("sets"), list } }).toJson());
}

FindPanel::DealPick FindPanel::pickDeal()
{
    // Each colour's favourites in the set shown, by kind (sorted, so a deal number always
    // means the same pick), then one of each by phi::deal.
    std::vector<QStringList> files ((size_t) numColours), instruments ((size_t) numColours), effects ((size_t) numColours);
    for (auto it = marks().cbegin(); it != marks().cend(); ++it)
    {
        const auto& key = it.key();
        std::vector<QStringList>* kind = nullptr;
        QString value;
        if (key.startsWith (QStringLiteral ("file:")))
        {
            value = key.mid (5);
            kind = QFileInfo::exists (value) ? &files : nullptr;
        }
        else if (value = instrumentOf ? instrumentOf (key) : QString(); ! value.isEmpty())
            kind = &instruments;
        else if (key.startsWith (QStringLiteral ("plugin:")))
        {
            value = key.mid (7);
            kind = &effects;
        }
        for (int c = 0; c < numColours && kind != nullptr; ++c)
            if ((it.value() & (1 << c)) != 0)
                (*kind)[(size_t) c].push_back (value);
    }

    // Each kind walks from its own start, so a colour's file and instrument don't move together.
    auto pick = [this] (std::vector<QStringList>& lists, double offset)
    {
        QStringList picked;
        for (int c = 0; c < numColours; ++c)
        {
            auto& list = lists[(size_t) c];
            list.sort();
            list.removeDuplicates();
            picked.push_back (list.isEmpty() ? QString() : list[phi::deal (deals, c, (int) list.size(), dealSeed + offset)]);
        }
        return picked;
    };
    DealPick out;
    for (int c = 0; c < numColours; ++c)
    {
        out.names.push_back (markName (setIndex, c));
        out.colours.push_back (colour (c));
    }
    out.files = pick (files, 0.0);
    out.instruments = pick (instruments, phi::inverse);
    out.effects = pick (effects, phi::inverse * phi::inverse);
    ++deals;
    return out;
}

void FindPanel::deal (DealTo target, bool again)
{
    const auto p = pickDeal();
    auto none = [] (const QStringList& l) { return std::all_of (l.begin(), l.end(), [] (const QString& v) { return v.isEmpty(); }); };
    // With only effects marked, the deal is a Prism.
    if (target == DealTo::automatic && none (p.files) && none (p.instruments) && ! none (p.effects))
        target = DealTo::prism;
    if (target == DealTo::prism)
    {
        if (phiPrism)
            phiPrism (p.effects, p.names, p.colours);
    }
    else if (phiDeal)
        phiDeal (p.files, p.instruments, p.names, p.colours, target, again);
}

void FindPanel::dealMenu (QWidget* on, const QPoint& at)
{
    QMenu menu (this);
    menu.addAction (tr ("Φ Deal into a New &Scene (Clip Launcher)"), this, [this] { deal (DealTo::launcher, false); });
    menu.addAction (tr ("Φ Deal onto the &Tracks (Mixer)"), this, [this] { deal (DealTo::tracks, false); });
    menu.addAction (tr ("Φ Deal a &Prism of Effects (Device Chain)"), this, [this] { deal (DealTo::prism, false); });
    menu.addSeparator();
    menu.addAction (tr ("Deal &Again in Place of the Last (Shift+Click)"), this, [this] { deal (DealTo::automatic, true); });
    if (phiCompose)
        menu.addAction (tr ("Φ &Compose a Song by Architectonics (Ctrl+Click)"), this, [this] { phiCompose(); });
    menu.exec (on->mapToGlobal (at));
}

void FindPanel::chooseSet (int set)
{
    if (set < 0 || set >= numSets())
        return;
    setIndex = set;
    dotIcons.clear();
    saveSelector();
    buildSidebar();
    refilter();
    Q_EMIT setChanged();
}

// A set of the user's own: its names start as the colour names, its colours as the Selector's.
void FindPanel::newSet()
{
    bool ok = false;
    const auto name = QInputDialog::getText (this, tr ("New Selector Set"), tr ("Name of the new set:"), QLineEdit::Normal, {}, &ok).trimmed();
    if (! ok)
        return;
    Set set;
    set.name = name;
    sets.push_back (set);
    chooseSet (numSets() - 1);
}

void FindPanel::deleteSet()
{
    if (setIndex < defaultSets || setIndex >= numSets())
        return;
    if (QMessageBox::question (this, tr ("Delete Set"), tr ("Delete the set \u201c%1\u201d, its names, colours and favourites?").arg (setName (setIndex)))
        != QMessageBox::Yes)
        return;
    sets.erase (sets.begin() + setIndex);
    chooseSet (0);
}

// A set's colour scheme: any of its eight colours can be changed, for this set only.
void FindPanel::chooseColour (int c)
{
    if (c < 0 || c >= numColours)
        return;
    const auto chosen = QColorDialog::getColor (markColour (setIndex, c), this, tr ("Colour of %1").arg (markName (setIndex, c)));
    if (! chosen.isValid())
        return;
    auto& colours = sets[(size_t) setIndex].colours;
    while (colours.size() < numColours)
        colours.push_back ({});
    colours[c] = chosen == colour (c) ? QColor() : chosen;
    dotIcons.clear();
    saveSelector();
    buildSidebar();
    refilter();
    Q_EMIT setChanged();
}

void FindPanel::selectorMenu (QWidget* on, const QPoint& at)
{
    QMenu menu (this);
    menu.addSection (tr ("Selector Set"));
    for (int i = 0; i < numSets(); ++i)
    {
        auto* action = menu.addAction (setName (i), this, [this, i] { chooseSet (i); });
        action->setCheckable (true);
        action->setChecked (i == setIndex);
    }
    menu.addSeparator();
    if (auto* item = sidebar->currentItem(); item != nullptr && static_cast<Section> (item->data (0, sectionRole).toInt()) == Section::colour)
    {
        menu.addAction (tr ("Rename %1").arg (item->text (0)), this, [this, item] { sidebar->editItem (item); })->setShortcut (Qt::Key_F2);
        const auto c = item->data (0, colourRole).toInt();
        menu.addAction (tr ("Colour of %1...").arg (item->text (0)), this, [this, c] { chooseColour (c); });
    }
    menu.addAction (tr ("Rename Set \u201c%1\u201d...").arg (setName (setIndex)), this, &FindPanel::renameSet);
    menu.addSeparator();
    menu.addAction (tr ("New Set..."), this, &FindPanel::newSet);
    if (setIndex >= defaultSets)
        menu.addAction (tr ("Delete Set \u201c%1\u201d...").arg (setName (setIndex)), this, &FindPanel::deleteSet);
    if (! sets[(size_t) setIndex].colours.isEmpty())
        menu.addAction (tr ("Reset the Colours"), this, [this]
        {
            sets[(size_t) setIndex].colours.clear();
            dotIcons.clear();
            saveSelector();
            buildSidebar();
            refilter();
            Q_EMIT setChanged();
        });
    menu.exec (on->mapToGlobal (at));
}

void FindPanel::renameSet()
{
    bool ok = false;
    const auto name = QInputDialog::getText (this, tr ("Rename Set"), tr ("Name of this Selector set (empty for the default):"),
                                             QLineEdit::Normal, setName (setIndex), &ok).trimmed();
    if (! ok)
        return;
    sets[(size_t) setIndex].name = setIndex < defaultSets && name == tr (setNames[setIndex]) ? QString() : name;
    saveSelector();
    Q_EMIT setChanged();
}

//==============================================================================
bool FindPanel::eventFilter (QObject* watched, QEvent* e)
{
    if (watched == results->viewport())
    {
        auto* m = static_cast<QMouseEvent*> (e);
        if (e->type() == QEvent::MouseButtonPress && m->button() == Qt::LeftButton)
        {
            pressedAt = m->position().toPoint();
            mayDrag = true;
        }
        else if (e->type() == QEvent::MouseButtonRelease)
            mayDrag = false;
        else if (e->type() == QEvent::MouseMove && mayDrag)
        {
            if ((m->buttons() & Qt::LeftButton) == 0)
                mayDrag = false;
            else if ((m->position().toPoint() - pressedAt).manhattanLength() >= QApplication::startDragDistance())
            {
                mayDrag = false;
                if (const auto* item = itemOf (results->itemAt (pressedAt)); item != nullptr && item->kind == FindItem::Kind::file)
                {
                    dragFiles (results->itemAt (pressedAt));
                    return true;
                }
            }
        }
        return QWidget::eventFilter (watched, e);
    }
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
            case Qt::Key_Home: case Qt::Key_End: case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Escape: case Qt::Key_F2:
            case Qt::Key_Backspace:
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

    // K (in the results or the sidebar: in the search it types) or Escape stops a file
    // being auditioned; Escape's other jobs wait for the next press.
    if (plain && ! auditioning.isEmpty() && (k->key() == Qt::Key_Escape || (k->key() == Qt::Key_K && watched != search)))
    {
        endAudition();
        return true;
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

    // F2 renames the colour chosen in the sidebar.
    if (plain && k->key() == Qt::Key_F2)
    {
        auto* item = sidebar->currentItem();
        if (item != nullptr && (item->flags() & Qt::ItemIsEditable))
        {
            sidebar->setFocus (Qt::OtherFocusReason);
            sidebar->editItem (item);
        }
        else
            QToolTip::showText (sidebar->mapToGlobal (QPoint (8, 8)),
                                tr ("Choose a Selector colour, then press F2 to rename it"), sidebar);
        return true;
    }

    // The first item a list can select, from the top.
    auto firstSelectable = [] (QTreeWidget* tree) -> QTreeWidgetItem*
    {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (auto* item = tree->topLevelItem (i); (item->flags() & Qt::ItemIsSelectable) && ! item->isHidden())
                return item;
        return nullptr;
    };

    if (watched == search)
    {
        switch (k->key())
        {
            case Qt::Key_Down:
                if (! plain)
                    return false;
                // Down from an empty search goes to the Selector and sections;
                // after typing it goes to what was found.
                if (search->text().isEmpty())
                {
                    rowBeforeDown = sidebar->indexOfTopLevelItem (sidebar->currentItem());
                    sidebar->setFocus (Qt::OtherFocusReason);
                    if (auto* first = firstSelectable (sidebar))
                        sidebar->setCurrentItem (first);
                }
                else
                {
                    results->setFocus (Qt::OtherFocusReason);
                    if (results->currentItem() == nullptr)
                        results->setCurrentItem (firstSelectable (results));
                }
                return true;
            case Qt::Key_Up: case Qt::Key_PageUp: case Qt::Key_PageDown:
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
        if (browsing() && plain)
        {
            auto* row = results->currentItem();
            const bool onFolder = row != nullptr && row->data (0, indexRole).toInt() == folderRow;
            if (k->key() == Qt::Key_Backspace || (k->key() == Qt::Key_Left && ! browseFolder.isEmpty() && ! atTopOfPlace()))
            {
                folderUp();
                return true;
            }
            if (k->key() == Qt::Key_Right && onFolder)
            {
                activate (row);
                return true;
            }
        }
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

    // Up from the top of either list goes back to the search.
    if ((watched == sidebar || watched == results) && plain && k->key() == Qt::Key_Up)
    {
        auto* tree = static_cast<QTreeWidget*> (watched);
        auto* current = tree->currentItem();
        if (current == nullptr || current == firstSelectable (tree) || tree->itemAbove (current) == nullptr)
        {
            // Back up from the sidebar without choosing: the search goes back to
            // where it was (usually All), not the colour passed through.
            if (watched == sidebar && rowBeforeDown >= 0 && rowBeforeDown < sidebar->topLevelItemCount())
                sidebar->setCurrentItem (sidebar->topLevelItem (rowBeforeDown));
            rowBeforeDown = -1;
            search->setFocus (Qt::OtherFocusReason);
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
