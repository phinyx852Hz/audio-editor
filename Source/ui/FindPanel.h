// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <vector>

class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// Something the Find panel can list: a process (any command), an effect, a
// plugin, a visualization or an audio file. `key` identifies it for the Selector's colours.
struct FindItem
{
    enum class Kind { process, effect, plugin, visual, file };

    Kind kind = Kind::process;
    QString key;          // "command:file.open", "plugin:<id>", "file:<path>"
    QString name;
    QString detail;       // shortcut, maker and format, or folder
    QString group;        // menu, maker or folder the results are grouped under
    QString searchText;   // everything the search matches against, besides name and detail
    bool enabled = true;
    std::function<void()> activate;
    std::function<void()> preview;   // run when the row is selected, for browsing (visualizations)
};

// The browser on the left: one search field that finds anything, a sidebar with
// the Selector (eight colours of favourites), categories and places, and the results.
// The Selector has sets of the eight: Favorites (the colour names, the default) and one
// per activity (Live Performance, Mixing, Mastering, Podcast, Chakra), each with its own names and its own favourites; the
// set chosen above the sidebar is the one shown and marked, and the mixer can start
// a mix from it (its tracks are the set's colours and names, bottom up).
// Files and places are browsed one folder at a time until something is typed:
// Enter or Right opens a folder, Left or Backspace goes up.
// Moving onto a file with the keys, or clicking it, auditions it; clicking it
// again or K stops. Keyboard: typing searches everything, Up/Down and Page Up/Down
// move through the results, Enter runs or loads the selection, Tab moves between search,
// sidebar and results, 1 to 8 toggle a colour on the selected result (0 clears
// them), F2 or a double-click renames a colour (F2 on the set's box renames the set), Escape clears the search or goes
// back to the editor.
class FindPanel final : public QWidget
{
    Q_OBJECT

public:
    FindPanel (QWidget* home, QWidget* parent);
    QSize sizeHint() const override;
    ~FindPanel() override;

    // Supplies the processes, effects and plugins; files come from the places.
    std::function<QList<FindItem>()> collect;
    // Plays a file without opening it, and stops it.
    std::function<void (const QString& path)> audition;
    std::function<void()> stopAudition;
    void auditionEnded();   // the file played to its end or something else took the sound

    void focusSearch();          // selects the search text, ready to type over
    bool searchIsReady() const;  // the search has the keyboard with nothing to keep: empty or all selected
    void reload();               // collects the items again and refilters

    // The Selector, top to bottom: Silver, Violet, Indigo, Azure, Emerald, Gold, Saffron, Ruby.
    static constexpr int numColours = 8;
    static QString colourName (int index);
    static QColor colour (int index);
    int numSets() const { return (int) sets.size(); }
    int currentSet() const { return setIndex; }
    QString setName (int set) const;                // the user's name for it, or the default
    QString markName (int set, int index) const;    // a colour's name in a set (F2 renames)

Q_SIGNALS:
    void openRequested (const QString& path);

protected:
    bool eventFilter (QObject*, QEvent*) override;
    void showEvent (QShowEvent*) override;

private:
    enum class Section { colour, all, processes, effects, plugins, visuals, files, place, addPlace };

    void buildSidebar();
    void refilter();
    void activate (QTreeWidgetItem*);
    void toggleColour (QTreeWidgetItem*, int colour);   // -1 clears every colour
    void addPlace();
    void scanFiles();
    bool browsing() const;                        // a folder is listed rather than search results
    void listFolder (const QString& select);      // the browse folder's subfolders and files
    void openFolder (const QString& folder, const QString& select = {});
    void folderUp();
    bool atTopOfPlace() const;   // browsing the very folder chosen under Places
    void startAudition (QTreeWidgetItem*);
    void endAudition();
    QIcon dots (int mask) const;
    void loadSelector();
    void saveSelector() const;
    void chooseSet (int set);
    void renameSet();
    QHash<QString, int>& marks() { return sets[(size_t) setIndex].marks; }
    const QHash<QString, int>& marks() const { return sets[(size_t) setIndex].marks; }

    QWidget* homeWidget;
    QLineEdit* search;
    QTreeWidget* sidebar;
    int rowBeforeDown = -1;   // the sidebar entry chosen before Down went into it from the search
    QTreeWidget* results;

    QList<FindItem> items;          // from `collect`
    QList<FindItem> files;          // from scanning the places
    QStringList places;
    QString browseFolder;           // empty: the list of places (Files), else a folder in one
    QList<FindItem> folderItems;    // the files of the folder listed
    QString auditioning;            // the file being auditioned
    QFutureWatcher<QList<FindItem>> fileScan;
    bool filesStale = true;
    struct Set
    {
        QString name;                // empty: the default name
        QStringList names;           // per colour; empty entries keep the default
        QHash<QString, int> marks;   // item key -> bit mask of colours
    };
    std::vector<Set> sets;
    int setIndex = 0;
    QComboBox* setBox;
};
