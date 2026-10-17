// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioEngine.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;

// WaveLab-style Master Section: plugin slots applied to playback and renders,
// then the master gain. In the slot list: Enter edits parameters (or adds a
// plugin to an empty slot), Insert picks a plugin, Delete removes it, B
// bypasses, E opens the plugin's own window, Ctrl+Up/Down reorders.
class MasterPanel final : public QWidget
{
    Q_OBJECT

public:
    MasterPanel (AudioEngine&, QWidget* home, QWidget* parent);

    void refresh();
    void choosePlugin (int slot);   // -1 = first empty slot
    void scanPlugins();
    void focusSlots();

Q_SIGNALS:
    void renderRequested();
    void message (const QString&);

protected:
    bool eventFilter (QObject*, QEvent*) override;

private:
    int currentSlot() const;
    void editParameters (int slot);

    AudioEngine& engine;
    QListWidget* slotList;
    QDoubleSpinBox* gainBox;
    QCheckBox* bypassBox;
    QLabel* summary;
};

// Searchable list of effect plugins. Enter picks the highlighted one.
class PluginBrowser final : public QDialog
{
    Q_OBJECT

public:
    PluginBrowser (const std::vector<PluginInfo>&, QWidget* parent);
    std::string selectedId() const { return chosen; }

protected:
    bool eventFilter (QObject*, QEvent*) override;

private:
    void refilter();

    std::vector<PluginInfo> plugins;
    QLineEdit* search;
    QTreeWidget* list;
    std::string chosen;
};

// Keyboard-friendly editor for every parameter of a plugin: Tab moves between
// parameters, arrows and Page Up/Down change values, typing filters the list.
class ParameterDialog final : public QDialog
{
    Q_OBJECT

public:
    ParameterDialog (AudioEngine&, int slot, QWidget* parent);

protected:
    void keyPressEvent (QKeyEvent*) override;

private:
    void rebuild();
    void syncFromPlugin();
    void applyFilter();

    AudioEngine& engine;
    int slot;
    std::string pluginId;
    QLineEdit* filter;
    QWidget* rowsHost;
    struct Row { QWidget* container; class QSlider* slider; QLabel* value; QString name; };
    std::vector<Row> rows;
};
