// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QStringList>
#include <QWidget>
#include <functional>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// Something the Find panel can list: a process (any command), an effect, a
// plugin or an audio file. `key` identifies it for the colour collections.
struct FindItem
{
    enum class Kind { process, effect, plugin, file };

    Kind kind = Kind::process;
    QString key;          // "command:file.open", "plugin:<id>", "file:<path>"
    QString name;
    QString detail;       // shortcut, maker and format, or folder
    QString group;        // menu, maker or folder the results are grouped under
    QString searchText;   // everything the search matches against, besides name and detail
    bool enabled = true;
    std::function<void()> activate;
};

// Ableton-style browser on the left: one search field that finds anything,
// a sidebar of colour collections, categories and places, and the results.
// Keyboard: typing searches everything, Up/Down and Page Up/Down move through
// the results, Enter runs or loads the selection, Tab moves between search,
// sidebar and results, 1 to 7 toggle a colour on the selected result (0 clears
// them), Escape clears the search or goes back to the editor.
class FindPanel final : public QWidget
{
    Q_OBJECT

public:
    FindPanel (QWidget* home, QWidget* parent);
    ~FindPanel() override;

    // Supplies the processes, effects and plugins; files come from the places.
    std::function<QList<FindItem>()> collect;

    void focusSearch();          // selects the search text, ready to type over
    bool searchHasFocus() const;
    void reload();               // collects the items again and refilters

    static constexpr int numColours = 7;
    static QString colourName (int index);
    static QColor colour (int index);

Q_SIGNALS:
    void openRequested (const QString& path);

protected:
    bool eventFilter (QObject*, QEvent*) override;
    void showEvent (QShowEvent*) override;

private:
    enum class Section { colour, all, processes, effects, plugins, files, place, addPlace };

    void buildSidebar();
    void refilter();
    void activate (QTreeWidgetItem*);
    void toggleColour (QTreeWidgetItem*, int colour);   // -1 clears every colour
    void addPlace();
    void scanFiles();
    QIcon dots (int mask) const;
    void loadCollections();
    void saveCollections() const;

    QWidget* homeWidget;
    QLineEdit* search;
    QTreeWidget* sidebar;
    QTreeWidget* results;

    QList<FindItem> items;          // from `collect`
    QList<FindItem> files;          // from scanning the places
    QStringList places;
    QFutureWatcher<QList<FindItem>> fileScan;
    bool filesStale = true;
    QHash<QString, int> collections;   // item key -> bit mask of colours
};
