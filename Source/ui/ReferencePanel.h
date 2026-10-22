// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;

// The A/B Reference panel: a list of reference recordings (finished masters or
// earlier versions) to compare with your audio. MainWindow holds the references
// and does the work (ReferenceSection.cpp); this only shows them and asks.
// Keys in the list: Enter hears the chosen reference (Enter again goes back),
// Insert adds, Delete removes, + and - change its level offset.
class ReferencePanel final : public QWidget
{
    Q_OBJECT

public:
    explicit ReferencePanel (QWidget* parent);

    struct Row
    {
        QString name, detail;   // "Song.flac", "-9.1 LUFS, heard 4.9 dB quieter"
        double offsetDb = 0.0;
    };
    void setRows (const QList<Row>&, int current, bool hearing);
    void setClean (bool);
    void setStatus (const QString&);
    int currentRow() const;
    QWidget* focusTarget() const;

Q_SIGNALS:
    void addRequested();
    void filesDropped (const QStringList& paths);   // dragged here from Find or a file manager
    void removeRequested (int row);
    void chosen (int row);              // selection moved
    void hearRequested (int row);       // Enter or double-click: hear it, or go back to your audio
    void offsetChanged (int row, double db);
    void cleanChanged (bool);
    void matchRequested();

protected:
    bool eventFilter (QObject*, QEvent*) override;
    void dragEnterEvent (class QDragEnterEvent*) override;
    void dropEvent (class QDropEvent*) override;

private:
    QListWidget* list;
    QDoubleSpinBox* offset;
    QCheckBox* clean;
    QLabel* status;
    bool updating = false;
};
