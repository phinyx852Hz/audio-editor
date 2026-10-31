// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioEngine.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;

// A dither choice as menus name it (dsp::ditherOrder is the order to offer them in).
QString ditherLabel (dsp::Dither);

// WaveLab-style Master Section: plugin slots applied to playback and renders,
// then the master gain, then the final dither (always there, never in a slot). In the slot list: Enter edits parameters (or adds a
// plugin to an empty slot), Insert picks a plugin, Delete removes it, B
// bypasses, E opens the plugin's own window, A measures it in the Plugin Analyzer,
// Ctrl+Up/Down reorders. Under the slots, the Aliasing scope watches the whole section.
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
    void analyzeRequested (int slot);   // the Plugin Analyzer on a slot, -1 the whole Master Section
    void message (const QString&);

protected:
    bool eventFilter (QObject*, QEvent*) override;

private:
    int currentSlot() const;
    void editParameters (int slot);

    AudioEngine& engine;
    QListWidget* slotList;
    QDoubleSpinBox* gainBox;
    QComboBox* ditherBits;
    QComboBox* ditherType;
    QCheckBox* bypassBox;
    QLabel* summary;
    class AliasScope* aliasScope;
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
    ParameterDialog (AudioEngine&, ChainPath, QWidget* parent);   // a device anywhere, inside Prisms too

protected:
    void keyPressEvent (QKeyEvent*) override;

private:
    void rebuild();
    void syncFromPlugin();
    void applyFilter();

    AudioEngine& engine;
    ChainPath path;
    std::string pluginId;
    QLineEdit* filter;
    QWidget* rowsHost;
    struct Row { QWidget* container; class QSlider* slider; QLabel* value; QString name; };
    std::vector<Row> rows;
};
